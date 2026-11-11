/*
 * A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the distribution.
 *
 *          Yamaha YMZ263B (MMA) recording helpers, shared by the
 *          AdLib Gold and the Tandy Sensation/VIS audio.
 *
 *          Rates (register 9 FREQ): 44.1, 22.05, 11.025 and 7.35 kHz in PCM
 *          mode; ADPCM halves them (22.05 down to 5.5125 kHz). Formats
 *          (register 0Ch DATA FMT, AdLib Gold SDTK manual): 0 is the 8 MSBs
 *          of the 12-bit sample in one byte; 1 is the 8 LSBs, then the 4
 *          MSBs sign-extended; 2 is the 4 LSBs in the high nibble, then the
 *          8 MSBs (a 16-bit word); in ADPCM mode each byte holds two 4-bit
 *          codes, the first in the high nibble. FIFO level (FIFO INT): the
 *          record flag rises when more than (7 - level) * 16 bytes wait --
 *          the SDTK's wave driver uses level 3 to be told "when more than
 *          64 bytes" are in the FIFO.
 *          Copyright 2026 Xeon3D.
 */
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>

#include <86box/86box.h>
#include <86box/sound.h>
#include <86box/snd_ymz263b.h>

void
mma_rec_input(mma_rec_t *rec, int on)
{
    if (on && !rec->started) {
        rec->wr = rec->rd = 0;
        rec->frac         = 0.0;
        rec->primed       = 0;
        rec->l = rec->r = 0;
        sound_in_start_input();
        rec->started = 1;
    } else if (!on && rec->started) {
        sound_in_stop_input();
        rec->started = 0;
    }
}

/* The host's input, as sound.c hands it over (interleaved stereo at the
   capture rate). Kept within a quarter second of the ADC. */
void
mma_rec_put_buffer(int16_t *buffer, int len, void *priv)
{
    mma_rec_t *rec      = (mma_rec_t *) priv;
    const int  cap_rate = al_capture_get_rate();

    if (!rec->started)
        return;

    rec->in_rate = (cap_rate > 0) ? cap_rate : SOUND_FREQ;

    for (int c = 0; c < len; c++) {
        const uint32_t i = rec->wr % MMA_REC_RING;

        rec->ring[i * 2]     = buffer[c * 2];
        rec->ring[i * 2 + 1] = buffer[c * 2 + 1];
        rec->wr++;
    }

    uint32_t keep = (uint32_t) (rec->in_rate / 4);
    if (keep > (MMA_REC_RING / 2))
        keep = MMA_REC_RING / 2;
    if ((rec->wr - rec->rd) > keep) {
        rec->rd   = rec->wr - (keep / 2);
        rec->frac = 0.0;
    }
}

/* One frame of the host's input at the ADC's rate (linear interpolation;
   silence while none has come). */
void
mma_rec_next_frame(mma_rec_t *rec, double rate)
{
    const uint32_t i0    = rec->rd % MMA_REC_RING;
    const uint32_t i1    = (rec->rd + 1) % MMA_REC_RING;
    const uint32_t avail = rec->wr - rec->rd;

    /* The host's input comes in blocks (20 ms); 40 ms in hand keeps the ADC
       from running dry at the end of each block, which would slip samples. */
    if (!rec->primed && rec->in_rate && (avail >= (uint32_t) (rec->in_rate / 25)))
        rec->primed = 1;
    if (!rec->primed || (avail < 2)) {
        rec->primed = 0;
        rec->l = rec->r = 0;
        return;
    }

    rec->l = (int) (rec->ring[i0 * 2] + (rec->ring[i1 * 2] - rec->ring[i0 * 2]) * rec->frac);
    rec->r = (int) (rec->ring[i0 * 2 + 1] + (rec->ring[i1 * 2 + 1] - rec->ring[i0 * 2 + 1]) * rec->frac);

    rec->frac += (double) rec->in_rate / rate;
    while ((rec->frac >= 1.0) && ((rec->wr - rec->rd) > 1)) {
        rec->frac -= 1.0;
        rec->rd++;
    }
    if (rec->frac >= 1.0)
        rec->frac = 0.0;
}

int
mma_rec_period(uint8_t reg9)
{
    static const int pcm[4]   = { 12, 24, 48, 72 }; /* 44.1, 22.05, 11.025, 7.35 kHz */
    static const int adpcm[4] = { 24, 48, 72, 96 }; /* 22.05 ... 5.5125 kHz */
    const int        fs       = (reg9 >> 3) & 3;

    return (reg9 & MMA_R9_PCM) ? pcm[fs] : adpcm[fs];
}

int
mma_rec_fifo_level(uint8_t regc)
{
    return (7 - ((regc >> 2) & 7)) * 16;
}

int
mma_rec_unit(uint8_t reg9, uint8_t regc)
{
    if (!(reg9 & MMA_R9_PCM))
        return 1;
    return (regc & 0x60) ? 2 : 1;
}

int
mma_play_bytes(uint8_t regc)
{
    const int fmt = (regc >> 5) & 3;

    return ((fmt == 1) || (fmt == 2)) ? 2 : 1;
}

int16_t
mma_play_decode(uint8_t regc, int offset8, uint8_t b0, uint8_t b1)
{
    int s12;

    switch ((regc >> 5) & 3) {
        case 1: /* 8 LSBs, then the 4 MSBs sign-extended */
            s12 = b0 | ((b1 & 0x0f) << 8);
            if (s12 & 0x800)
                s12 -= 0x1000;
            return (int16_t) (s12 * 16);
        case 2: /* 4 LSBs in the high nibble, then the 8 MSBs */
            return (int16_t) ((b1 << 8) | (b0 & 0xf0));

        default: /* 0 (and the invalid 3): the 8 MSBs */
            return (int16_t) ((offset8 ? (b0 - 0x80) : (int8_t) b0) * 256);
    }
}

void
mma_rec_adpcm_reset(mma_rec_t *rec, int ch)
{
    rec->adpcm_x[ch]    = 0;
    rec->adpcm_step[ch] = 127;
    rec->adpcm_byte[ch] = 0;
    rec->adpcm_half[ch] = 0;
}

/* Yamaha's 4-bit ADPCM (as the Y8950 / YM2608 ADPCM-B, which the MMA's is
   said to match): the decoder adds (2 * |code| + 1) * step / 8 with the
   code's sign, and scales the step by 0.9 to 2.4. The encoder runs the same
   decoder so it tracks what the playback side will rebuild. */
static uint8_t
mma_rec_adpcm_code(mma_rec_t *rec, int ch, int sample)
{
    static const int step_adj[8] = { 57, 57, 57, 57, 77, 102, 128, 153 }; /* / 64 */
    int32_t          step        = rec->adpcm_step[ch];
    int32_t          diff        = sample - rec->adpcm_x[ch];
    uint8_t          code        = 0;
    int32_t          delta;

    if (diff < 0) {
        code = 8;
        diff = -diff;
    }
    /* |code| = the nearest of 0..7 to diff * 8 / step - 1, over 2. */
    int mag = (int) (((int64_t) diff * 4) / step);
    if (mag > 7)
        mag = 7;
    code |= mag;

    delta = (((mag << 1) + 1) * step) >> 3;
    if (code & 8)
        rec->adpcm_x[ch] -= delta;
    else
        rec->adpcm_x[ch] += delta;
    if (rec->adpcm_x[ch] > 32767)
        rec->adpcm_x[ch] = 32767;
    else if (rec->adpcm_x[ch] < -32768)
        rec->adpcm_x[ch] = -32768;

    step = (step * step_adj[mag]) >> 6;
    if (step < 127)
        step = 127;
    else if (step > 24576)
        step = 24576;
    rec->adpcm_step[ch] = step;

    return code;
}

int
mma_rec_encode(mma_rec_t *rec, int ch, int sample, uint8_t reg9, uint8_t regc, uint8_t *out)
{
    int s12;

    if (sample > 32767)
        sample = 32767;
    else if (sample < -32768)
        sample = -32768;

    if (!(reg9 & MMA_R9_PCM)) {
        const uint8_t code = mma_rec_adpcm_code(rec, ch, sample);

        if (!rec->adpcm_half[ch]) {
            rec->adpcm_byte[ch] = code << 4;
            rec->adpcm_half[ch] = 1;
            return 0;
        }
        out[0]              = rec->adpcm_byte[ch] | code;
        rec->adpcm_half[ch] = 0;
        return 1;
    }

    s12 = sample >> 4; /* the 12-bit ADC */
    switch ((regc >> 5) & 3) {
        case 1: /* 8 LSBs, then the 4 MSBs sign-extended */
            out[0] = s12 & 0xff;
            out[1] = (s12 >> 8) & 0xff;
            return 2;
        case 2: /* 4 LSBs in the high nibble, then the 8 MSBs */
            out[0] = (s12 << 4) & 0xf0;
            out[1] = (s12 >> 4) & 0xff;
            return 2;

        default: /* 0 (and the invalid 3): the 8 MSBs */
            out[0] = ((s12 >> 4) & 0xff) ^ (rec->offset8 ? 0x80 : 0x00);
            return 1;
    }
}
