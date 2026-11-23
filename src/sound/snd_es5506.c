/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          Ensoniq ES5506 (OTTO) wavetable synthesizer.
 *
 *          Ported to C from MAME's es5506.cpp, which carries this notice:
 *
 *          license:BSD-3-Clause
 *          copyright-holders:Aaron Giles
 *
 *          Redistribution and use in source and binary forms, with or
 *          without modification, are permitted provided that the
 *          following conditions are met: (1) Redistributions of source
 *          code must retain the above copyright notice, this list of
 *          conditions and the following disclaimer. (2) Redistributions
 *          in binary form must reproduce the above copyright notice,
 *          this list of conditions and the following disclaimer in the
 *          documentation and/or other materials provided with the
 *          distribution. (3) Neither the name of the copyright holder
 *          nor the names of its contributors may be used to endorse or
 *          promote products derived from this software without specific
 *          prior written permission. THIS SOFTWARE IS PROVIDED BY THE
 *          COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND ANY EXPRESS OR
 *          IMPLIED WARRANTIES ARE DISCLAIMED.
 */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <86box/snd_es5506.h>

#define VOLUME_BIT       16
#define VOLUME_SHIFT     4  /* 16-bit register, 4-bit exponent + 8-bit mantissa table */
#define VOLUME_ACC_SHIFT 11 /* (16 + 15) - 20 */
#define ADDRESS_FRAC_BIT 11
#define ADDRESS_MASK     0xffffffffULL /* 21-bit integer, 11-bit fraction */
#define FILTER_SHIFT_5506 1

#define FINE_FILTER_BIT 16
#define FILTER_BIT      12
#define FILTER_SHIFT    (FINE_FILTER_BIT - FILTER_BIT)

#define ULAW_MAXBITS 8

enum {
    CONTROL_BS1   = 0x8000,
    CONTROL_BS0   = 0x4000,
    CONTROL_CMPD  = 0x2000,
    CONTROL_CA2   = 0x1000,
    CONTROL_CA1   = 0x0800,
    CONTROL_CA0   = 0x0400,
    CONTROL_LP4   = 0x0200,
    CONTROL_LP3   = 0x0100,
    CONTROL_IRQ   = 0x0080,
    CONTROL_DIR   = 0x0040,
    CONTROL_IRQE  = 0x0020,
    CONTROL_BLE   = 0x0010,
    CONTROL_LPE   = 0x0008,
    CONTROL_LEI   = 0x0004,
    CONTROL_STOP1 = 0x0002,
    CONTROL_STOP0 = 0x0001,

    CONTROL_LOOPMASK = (CONTROL_BLE | CONTROL_LPE),
    CONTROL_STOPMASK = (CONTROL_STOP1 | CONTROL_STOP0)
};

enum {
    LP3 = 1,
    LP4 = 2
};

static inline uint32_t
get_bank(uint32_t control)
{
    return (control >> 14) & 3;
}

static inline uint32_t
get_ca(uint32_t control)
{
    return (control >> 10) & 7;
}

static inline uint32_t
get_lp(uint32_t control)
{
    return (control >> 8) & (LP3 | LP4);
}

static inline int32_t
sext18(uint32_t v)
{
    return ((int32_t) (v << 14)) >> 14;
}

static void
es5506_update_rate(es5506_t *chip)
{
    chip->sample_rate = chip->master_clock / (16 * (chip->active_voices + 1));
    if (chip->rate_cb)
        chip->rate_cb(chip->priv, chip->sample_rate);
}

static void
es5506_compute_tables(es5506_t *chip)
{
    /* u-law table */
    for (int i = 0; i < (1 << ULAW_MAXBITS); i++) {
        const uint16_t rawval   = (i << (16 - ULAW_MAXBITS)) | (1 << (15 - ULAW_MAXBITS));
        const uint8_t  exponent = rawval >> 13;
        uint32_t       mantissa = (rawval << 3) & 0xffff;

        if (exponent == 0)
            chip->ulaw_lookup[i] = (int16_t) mantissa >> 7;
        else {
            mantissa             = (mantissa >> 1) | (~mantissa & 0x8000);
            chip->ulaw_lookup[i] = (int16_t) mantissa >> (7 - exponent);
        }
    }

    /* volume table: 4-bit exponent, 8-bit mantissa */
    for (int i = 0; i < 4096; i++) {
        const uint32_t exponent = (i >> 8) & 15;
        const uint32_t mantissa = (i & 255) | 256;

        chip->volume_lookup[i] = (mantissa << 7) >> (16 - exponent);
    }

    for (int j = 0; j < 32; j++) {
        memset(&chip->voice[j], 0, sizeof(es5506_voice_t));
        chip->voice[j].index   = j;
        chip->voice[j].control = CONTROL_STOPMASK;
        chip->voice[j].lvol    = 1 << (VOLUME_BIT - 1);
        chip->voice[j].rvol    = 1 << (VOLUME_BIT - 1);
    }
}

void
es5506_init(es5506_t *chip, uint32_t clock, int channels)
{
    chip->master_clock = clock;
    chip->channels     = ((channels >= 1) && (channels <= ES5506_MAX_CHANNELS)) ? channels : 1;
    chip->irqv         = 0x80;
    es5506_compute_tables(chip);
    es5506_reset(chip);
}

void
es5506_reset(es5506_t *chip)
{
    chip->active_voices = 0x1f;
    chip->mode          = 0x17;
    chip->current_page  = 0;
    chip->write_latch   = 0;
    chip->read_latch    = 0;
    if (!(chip->irqv & 0x80)) {
        chip->irqv = 0x80;
        if (chip->irq_cb)
            chip->irq_cb(chip->priv, 0);
    }
    es5506_update_rate(chip);
}

static inline uint32_t
get_volume(const es5506_t *chip, uint32_t volume)
{
    return chip->volume_lookup[(volume >> VOLUME_SHIFT) & 4095];
}

static inline int32_t
get_sample(const es5506_t *chip, int32_t sample, uint32_t volume)
{
    return (int32_t) (((int64_t) sample * get_volume(chip, volume)) >> VOLUME_ACC_SHIFT);
}

static inline uint32_t
get_integer_addr(uint64_t accum, int32_t bias)
{
    return (uint32_t) (((accum + ((int64_t) bias << ADDRESS_FRAC_BIT)) & ADDRESS_MASK) >> ADDRESS_FRAC_BIT);
}

static inline int32_t
interpolate(int32_t sample1, int32_t sample2, uint64_t accum)
{
    const uint32_t shifted = 1 << ADDRESS_FRAC_BIT;

    accum &= (shifted - 1);
    return (sample1 * (int32_t) (shifted - accum) + sample2 * (int32_t) accum) >> ADDRESS_FRAC_BIT;
}

static inline int32_t
apply_lowpass(int32_t out, int32_t cutoff, int32_t in)
{
    return ((int32_t) (cutoff >> FILTER_SHIFT) * (out - in) / (1 << FILTER_BIT)) + in;
}

static inline int32_t
apply_highpass(int32_t out, int32_t cutoff, int32_t in, int32_t prev)
{
    return out - prev + ((int32_t) (cutoff >> FILTER_SHIFT) * in) / (1 << (FILTER_BIT + 1)) + in / 2;
}

static inline void
apply_filters(es5506_voice_t *voice, int32_t *sample)
{
    int32_t s = *sample;

    /* pole 1 is always low-pass using K1 */
    s           = apply_lowpass(s, voice->k1, voice->o1n1);
    voice->o1n1 = s;

    /* pole 2 is always low-pass using K1 */
    s           = apply_lowpass(s, voice->k1, voice->o2n1);
    voice->o2n2 = voice->o2n1;
    voice->o2n1 = s;

    switch (get_lp(voice->control)) {
        case 0:
            s           = apply_highpass(s, voice->k2, voice->o3n1, voice->o2n2);
            voice->o3n2 = voice->o3n1;
            voice->o3n1 = s;
            s           = apply_highpass(s, voice->k2, voice->o4n1, voice->o3n2);
            voice->o4n1 = s;
            break;

        case LP3:
            s           = apply_lowpass(s, voice->k1, voice->o3n1);
            voice->o3n2 = voice->o3n1;
            voice->o3n1 = s;
            s           = apply_highpass(s, voice->k2, voice->o4n1, voice->o3n2);
            voice->o4n1 = s;
            break;

        case LP4:
            s           = apply_lowpass(s, voice->k2, voice->o3n1);
            voice->o3n2 = voice->o3n1;
            voice->o3n1 = s;
            s           = apply_lowpass(s, voice->k2, voice->o4n1);
            voice->o4n1 = s;
            break;

        case LP3 | LP4:
            s           = apply_lowpass(s, voice->k1, voice->o3n1);
            voice->o3n2 = voice->o3n1;
            voice->o3n1 = s;
            s           = apply_lowpass(s, voice->k2, voice->o4n1);
            voice->o4n1 = s;
            break;

        default:
            break;
    }

    *sample = s;
}

static inline void
update_envelopes(es5506_voice_t *voice)
{
    const uint32_t volume_max = (1 << VOLUME_BIT) - 1;

    voice->ecount--;

    if (voice->lvramp) {
        voice->lvol += (int8_t) voice->lvramp;
        if ((int32_t) voice->lvol < 0)
            voice->lvol = 0;
        else if (voice->lvol > volume_max)
            voice->lvol = volume_max;
    }

    if (voice->rvramp) {
        voice->rvol += (int8_t) voice->rvramp;
        if ((int32_t) voice->rvol < 0)
            voice->rvol = 0;
        else if (voice->rvol > volume_max)
            voice->rvol = volume_max;
    }

    if (voice->k1ramp && (((int32_t) voice->k1ramp >= 0) || !(voice->filtcount & 7))) {
        voice->k1 += (int8_t) voice->k1ramp;
        if ((int32_t) voice->k1 < 0)
            voice->k1 = 0;
        else if (voice->k1 > 0xffff)
            voice->k1 = 0xffff;
    }

    if (voice->k2ramp && (((int32_t) voice->k2ramp >= 0) || !(voice->filtcount & 7))) {
        voice->k2 += (int8_t) voice->k2ramp;
        if ((int32_t) voice->k2 < 0)
            voice->k2 = 0;
        else if (voice->k2 > 0xffff)
            voice->k2 = 0xffff;
    }

    voice->filtcount++;
}

static inline void
check_for_end_forward(es5506_voice_t *voice, uint64_t *accum)
{
    if ((*accum > voice->end) && !(voice->control & CONTROL_LEI)) {
        if (voice->control & CONTROL_IRQE)
            voice->control |= CONTROL_IRQ;

        switch (voice->control & CONTROL_LOOPMASK) {
            case 0:
                voice->control |= CONTROL_STOP0;
                break;

            case CONTROL_LPE:
                *accum = (voice->start + (*accum - voice->end)) & ADDRESS_MASK;
                break;

            case CONTROL_BLE:
                *accum         = (voice->start + (*accum - voice->end)) & ADDRESS_MASK;
                voice->control = (voice->control & ~CONTROL_LOOPMASK) | CONTROL_LEI;
                break;

            case CONTROL_LPE | CONTROL_BLE:
                *accum = (voice->end - (*accum - voice->end)) & ADDRESS_MASK;
                voice->control ^= CONTROL_DIR;
                break;

            default:
                break;
        }
    }
}

static inline void
check_for_end_reverse(es5506_voice_t *voice, uint64_t *accum)
{
    if ((*accum < voice->start) && !(voice->control & CONTROL_LEI)) {
        if (voice->control & CONTROL_IRQE)
            voice->control |= CONTROL_IRQ;

        switch (voice->control & CONTROL_LOOPMASK) {
            case 0:
                voice->control |= CONTROL_STOP0;
                break;

            case CONTROL_LPE:
                *accum = (voice->end - (voice->start - *accum)) & ADDRESS_MASK;
                break;

            case CONTROL_BLE:
                *accum         = (voice->end - (voice->start - *accum)) & ADDRESS_MASK;
                voice->control = (voice->control & ~CONTROL_LOOPMASK) | CONTROL_LEI;
                break;

            case CONTROL_LPE | CONTROL_BLE:
                *accum = (voice->start + (voice->start - *accum)) & ADDRESS_MASK;
                voice->control ^= CONTROL_DIR;
                break;

            default:
                break;
        }
    }
}

static inline uint16_t
read_sample(es5506_t *chip, es5506_voice_t *voice, uint32_t addr)
{
    if (!chip->read_sample)
        return 0;
    return chip->read_sample(chip->priv, get_bank(voice->control), addr & 0x1fffff);
}

static void
generate_voice(es5506_t *chip, es5506_voice_t *voice, int32_t *dest)
{
    const uint32_t freqcount = (uint32_t) voice->freqcount;
    uint64_t       accum     = voice->accum & ADDRESS_MASK;

    if (!(voice->control & CONTROL_STOPMASK)) {
        int32_t val1 = read_sample(chip, voice, get_integer_addr(accum, 0));
        int32_t val2 = read_sample(chip, voice, get_integer_addr(accum, 1));

        if (voice->control & CONTROL_CMPD) {
            val1 = chip->ulaw_lookup[val1 >> (16 - ULAW_MAXBITS)] << FILTER_SHIFT_5506;
            val2 = chip->ulaw_lookup[val2 >> (16 - ULAW_MAXBITS)] << FILTER_SHIFT_5506;
        } else {
            val1 = (int16_t) val1 << FILTER_SHIFT_5506;
            val2 = (int16_t) val2 << FILTER_SHIFT_5506;
        }

        val1 = interpolate(val1, val2, accum);
        if (!(voice->control & CONTROL_DIR))
            accum = (accum + freqcount) & ADDRESS_MASK;
        else
            accum = (accum - freqcount) & ADDRESS_MASK;

        apply_filters(voice, &val1);

        if (voice->ecount != 0)
            update_envelopes(voice);

        val1 >>= FILTER_SHIFT_5506;
        dest[0] += get_sample(chip, val1, voice->lvol);
        dest[1] += get_sample(chip, val1, voice->rvol);

        if (!(voice->control & CONTROL_DIR))
            check_for_end_forward(voice, &accum);
        else
            check_for_end_reverse(voice, &accum);
    } else if (voice->ecount != 0)
        update_envelopes(voice);

    voice->accum = accum;
}

void
es5506_generate(es5506_t *chip, int32_t *out)
{
    memset(out, 0, sizeof(int32_t) * 2 * chip->channels);

    for (int v = 0; v <= chip->active_voices; v++) {
        es5506_voice_t *voice = &chip->voice[v];

        /* MAME stops a voice whose start equals its end. The chip has no
           such rule: a non-looping voice stops when it passes its end, and
           the Soundscape's OPL-2 firmware starts a voice before rewriting
           start and end, so at 8 MHz an output sample can fall in between. */

        generate_voice(chip, voice, &out[(get_ca(voice->control) % chip->channels) << 1]);

        /* latch the voice into the IRQ vector if the host has acked the last one */
        if ((voice->control & CONTROL_IRQ) && (chip->irqv & 0x80)) {
            chip->irqv = v & 0x1f;
            voice->control &= ~CONTROL_IRQ;
            if (chip->irq_cb)
                chip->irq_cb(chip->priv, 1);
        }
    }

    for (int c = 0; c < 2 * chip->channels; c++) {
        if (out[c] > ((1 << 19) - 1))
            out[c] = (1 << 19) - 1;
        else if (out[c] < -(1 << 19))
            out[c] = -(1 << 19);
    }
}

static void
reg_write_low(es5506_t *chip, es5506_voice_t *voice, int offset, uint32_t data)
{
    switch (offset) {
        case 0x00 / 8: /* CR */
            voice->control = data & 0xffff;
            break;
        case 0x08 / 8: /* FC */
            voice->freqcount = data & 0x1ffff;
            break;
        case 0x10 / 8: /* LVOL */
            voice->lvol = data & 0xffff;
            break;
        case 0x18 / 8: /* LVRAMP */
            voice->lvramp = (data & 0xff00) >> 8;
            break;
        case 0x20 / 8: /* RVOL */
            voice->rvol = data & 0xffff;
            break;
        case 0x28 / 8: /* RVRAMP */
            voice->rvramp = (data & 0xff00) >> 8;
            break;
        case 0x30 / 8: /* ECOUNT */
            voice->ecount    = data & 0x1ff;
            voice->filtcount = 0;
            break;
        case 0x38 / 8: /* K2 */
            voice->k2 = data & 0xffff;
            break;
        case 0x40 / 8: /* K2RAMP */
            voice->k2ramp = ((data & 0xff00) >> 8) | ((data & 0x0001) << 31);
            break;
        case 0x48 / 8: /* K1 */
            voice->k1 = data & 0xffff;
            break;
        case 0x50 / 8: /* K1RAMP */
            voice->k1ramp = ((data & 0xff00) >> 8) | ((data & 0x0001) << 31);
            break;
        case 0x58 / 8: /* ACTV */
            chip->active_voices = data & 0x1f;
            es5506_update_rate(chip);
            break;
        case 0x60 / 8: /* MODE */
            chip->mode = data & 0x1f;
            break;
        case 0x78 / 8: /* PAGE */
            chip->current_page = data & 0x7f;
            break;
        default: /* PAR, IRQV: read only */
            break;
    }
}

static void
reg_write_high(es5506_t *chip, es5506_voice_t *voice, int offset, uint32_t data)
{
    switch (offset) {
        case 0x00 / 8: /* CR */
            voice->control = data & 0xffff;
            break;
        case 0x08 / 8: /* START */
            voice->start = data & 0xfffff800;
            break;
        case 0x10 / 8: /* END */
            voice->end = data & 0xffffff80;
            break;
        case 0x18 / 8: /* ACCUM */
            voice->accum = data;
            break;
        case 0x20 / 8: /* O4(n-1) */
            voice->o4n1 = sext18(data);
            break;
        case 0x28 / 8: /* O3(n-1) */
            voice->o3n1 = sext18(data);
            break;
        case 0x30 / 8: /* O3(n-2) */
            voice->o3n2 = sext18(data);
            break;
        case 0x38 / 8: /* O2(n-1) */
            voice->o2n1 = sext18(data);
            break;
        case 0x40 / 8: /* O2(n-2) */
            voice->o2n2 = sext18(data);
            break;
        case 0x48 / 8: /* O1(n-1) */
            voice->o1n1 = sext18(data);
            break;
        case 0x50 / 8: /* W_ST */
            chip->wst = data & 0x7f;
            break;
        case 0x58 / 8: /* W_END */
            chip->wend = data & 0x7f;
            break;
        case 0x60 / 8: /* LR_END */
            chip->lrend = data & 0x7f;
            break;
        case 0x78 / 8: /* PAGE */
            chip->current_page = data & 0x7f;
            break;
        default:
            break;
    }
}

void
es5506_write(es5506_t *chip, uint8_t offset, uint8_t val)
{
    es5506_voice_t *voice = &chip->voice[chip->current_page & 0x1f];
    const int       shift = 8 * (offset & 3);

    chip->write_latch = (chip->write_latch & ~(0xff000000 >> shift)) | ((uint32_t) val << (24 - shift));

    if (shift != 24)
        return;

    if (chip->current_page < 0x20)
        reg_write_low(chip, voice, (offset & 0x3f) / 4, chip->write_latch);
    else if (chip->current_page < 0x40)
        reg_write_high(chip, voice, (offset & 0x3f) / 4, chip->write_latch);
    else if (((offset & 0x3f) / 4) == (0x78 / 8))
        chip->current_page = chip->write_latch & 0x7f; /* test page: only PAGE matters */

    /* The latch keeps its bytes after a register write (MAME clears it).
       Soundscape firmware writes a register with movep.l and then rewrites
       the low byte, e.g. ACCUM when it starts the wave stream voice; a
       cleared latch would zero the upper bytes and play DRAM from word 0. */
}

static uint32_t
reg_read(es5506_t *chip, es5506_voice_t *voice, int offset)
{
    const int high = (chip->current_page >= 0x20);
    const int test = (chip->current_page >= 0x40);

    switch (offset) {
        case 0x68 / 8: /* PAR */
            return 0;
        case 0x70 / 8: /* IRQV */
        {
            uint32_t result = chip->irqv;

            if (!test && !(chip->irqv & 0x80)) {
                chip->irqv = 0x80;
                if (chip->irq_cb)
                    chip->irq_cb(chip->priv, 0);
            }
            return result;
        }
        case 0x78 / 8: /* PAGE */
            return chip->current_page;
        default:
            break;
    }

    if (test)
        return 0;

    if (!high)
        switch (offset) {
            case 0x00 / 8:
                return voice->control;
            case 0x08 / 8:
                return (uint32_t) voice->freqcount;
            case 0x10 / 8:
                return voice->lvol;
            case 0x18 / 8:
                return voice->lvramp << 8;
            case 0x20 / 8:
                return voice->rvol;
            case 0x28 / 8:
                return voice->rvramp << 8;
            case 0x30 / 8:
                return voice->ecount;
            case 0x38 / 8:
                return voice->k2;
            case 0x40 / 8:
                return (voice->k2ramp << 8) | (voice->k2ramp >> 31);
            case 0x48 / 8:
                return voice->k1;
            case 0x50 / 8:
                return (voice->k1ramp << 8) | (voice->k1ramp >> 31);
            case 0x58 / 8:
                return chip->active_voices;
            case 0x60 / 8:
                return chip->mode;
            default:
                return 0;
        }

    switch (offset) {
        case 0x00 / 8:
            return voice->control;
        case 0x08 / 8:
            return (uint32_t) voice->start;
        case 0x10 / 8:
            return (uint32_t) voice->end;
        case 0x18 / 8:
            return (uint32_t) voice->accum;
        case 0x20 / 8:
            return voice->o4n1 & 0x3ffff;
        case 0x28 / 8:
            return voice->o3n1 & 0x3ffff;
        case 0x30 / 8:
            return voice->o3n2 & 0x3ffff;
        case 0x38 / 8:
            return voice->o2n1 & 0x3ffff;
        case 0x40 / 8:
            return voice->o2n2 & 0x3ffff;
        case 0x48 / 8:
            return voice->o1n1 & 0x3ffff;
        case 0x50 / 8:
            return chip->wst;
        case 0x58 / 8:
            return chip->wend;
        case 0x60 / 8:
            return chip->lrend;
        default:
            return 0;
    }
}

uint8_t
es5506_read(es5506_t *chip, uint8_t offset)
{
    const int shift = 8 * (offset & 3);

    /* the full register is latched on a read of byte 0 */
    if (shift == 0)
        chip->read_latch = reg_read(chip, &chip->voice[chip->current_page & 0x1f], (offset & 0x3f) / 4);

    return (chip->read_latch >> (24 - shift)) & 0xff;
}
