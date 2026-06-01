/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          Nuked OPL3 emulator.
 *
 *          Thanks:
 *            MAME Development Team(Jarek Burczynski, Tatsuyuki Satoh):
 *            Feedback and Rhythm part calculation information.
 *            forums.submarine.org.uk(carbon14, opl3):
 *            Tremolo and phase generator calculation information.
 *            OPLx decapsulated(Matthew Gambrell, Olli Niemitalo):
 *            OPL2 ROMs.
 *            siliconpr0n.org(John McMaster, digshadow):
 *            YMF262 and VRC VII decaps and die shots.
 *
 *          Version: 1.8-fast-1 (Nuked-OPL3-fast fork)
 *
 *          Translation from C++ into C done by Miran Grca.
 *
 * **TODO** The OPL3 is a stereo chip, and, thus, always generates
 *          a two-sample stream of data, for the L and R channels,
 *          in that order. The OPL2, however, is mono. What should
 *          we generate for that?
 *
 * Authors: Fred N. van Kempen, <decwiz@yahoo.com>
 *          Miran Grca, <mgrca8@gmail.com>
 *          Alexey Khokholov (Nuke.YKT)
 *          Tony Gies
 *
 *          Copyright 2017-2020 Fred N. van Kempen.
 *          Copyright 2016-2020 Miran Grca.
 *          Copyright 2013-2020 Alexey Khokholov (Nuke.YKT)
 *          Copyright 2026 Tony Gies
 */
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define HAVE_STDARG_H
#include <86box/86box.h>
#include <86box/sound.h>
#include "cpu.h"
#include <86box/timer.h>
#include <86box/device.h>
#include <86box/snd_opl.h>
#include <86box/snd_opl3_nuked.h>

#include <86box/snd_opl3_nuked_wf_rom.h>

#if OPL3_ENABLE_STEREOEXT && !defined OPL3_SIN
#ifndef _USE_MATH_DEFINES
#define _USE_MATH_DEFINES 1
#endif
#include <math.h>
/* input: [0, 256), output: [0, 65536] */
#define OPL3_SIN(x) ((int32_t)(sin((x) * M_PI / 512.0) * 65536.0))
#endif

/* Quirk: Some FM channels are output one sample later on the left side than the right. */
#ifndef OPL3_QUIRK_CHANNELSAMPLEDELAY
#define OPL3_QUIRK_CHANNELSAMPLEDELAY (!OPL3_ENABLE_STEREOEXT)
#endif

#define RSM_FRAC    10

/* Channel types */

enum {
    ch_2op = 0,
    ch_4op = 1,
    ch_4op2 = 2,
    ch_drum = 3
};

/* Envelope key types */

enum {
    egk_norm = 0x01,
    egk_drum = 0x02
};

#ifdef ENABLE_NUKED_OPL3_LOG
int nuked_opl3_do_log = ENABLE_NUKED_OPL3_LOG;

static void
nuked_opl3_log(const char *fmt, ...)
{
    va_list ap;

    if (nuked_opl3_do_log) {
        va_start(ap, fmt);
        pclog_ex(fmt, ap);
        va_end(ap);
    }
}
#else
#    define nuked_opl3_log(fmt, ...)
#endif
/*
    exp table
*/

static const uint16_t exprom[256] = {
    0xff4, 0xfea, 0xfde, 0xfd4, 0xfc8, 0xfbe, 0xfb4, 0xfa8,
    0xf9e, 0xf92, 0xf88, 0xf7e, 0xf72, 0xf68, 0xf5c, 0xf52,
    0xf48, 0xf3e, 0xf32, 0xf28, 0xf1e, 0xf14, 0xf08, 0xefe,
    0xef4, 0xeea, 0xee0, 0xed4, 0xeca, 0xec0, 0xeb6, 0xeac,
    0xea2, 0xe98, 0xe8e, 0xe84, 0xe7a, 0xe70, 0xe66, 0xe5c,
    0xe52, 0xe48, 0xe3e, 0xe34, 0xe2a, 0xe20, 0xe16, 0xe0c,
    0xe04, 0xdfa, 0xdf0, 0xde6, 0xddc, 0xdd2, 0xdca, 0xdc0,
    0xdb6, 0xdac, 0xda4, 0xd9a, 0xd90, 0xd88, 0xd7e, 0xd74,
    0xd6a, 0xd62, 0xd58, 0xd50, 0xd46, 0xd3c, 0xd34, 0xd2a,
    0xd22, 0xd18, 0xd10, 0xd06, 0xcfe, 0xcf4, 0xcec, 0xce2,
    0xcda, 0xcd0, 0xcc8, 0xcbe, 0xcb6, 0xcae, 0xca4, 0xc9c,
    0xc92, 0xc8a, 0xc82, 0xc78, 0xc70, 0xc68, 0xc60, 0xc56,
    0xc4e, 0xc46, 0xc3c, 0xc34, 0xc2c, 0xc24, 0xc1c, 0xc12,
    0xc0a, 0xc02, 0xbfa, 0xbf2, 0xbea, 0xbe0, 0xbd8, 0xbd0,
    0xbc8, 0xbc0, 0xbb8, 0xbb0, 0xba8, 0xba0, 0xb98, 0xb90,
    0xb88, 0xb80, 0xb78, 0xb70, 0xb68, 0xb60, 0xb58, 0xb50,
    0xb48, 0xb40, 0xb38, 0xb32, 0xb2a, 0xb22, 0xb1a, 0xb12,
    0xb0a, 0xb02, 0xafc, 0xaf4, 0xaec, 0xae4, 0xade, 0xad6,
    0xace, 0xac6, 0xac0, 0xab8, 0xab0, 0xaa8, 0xaa2, 0xa9a,
    0xa92, 0xa8c, 0xa84, 0xa7c, 0xa76, 0xa6e, 0xa68, 0xa60,
    0xa58, 0xa52, 0xa4a, 0xa44, 0xa3c, 0xa36, 0xa2e, 0xa28,
    0xa20, 0xa18, 0xa12, 0xa0c, 0xa04, 0x9fe, 0x9f6, 0x9f0,
    0x9e8, 0x9e2, 0x9da, 0x9d4, 0x9ce, 0x9c6, 0x9c0, 0x9b8,
    0x9b2, 0x9ac, 0x9a4, 0x99e, 0x998, 0x990, 0x98a, 0x984,
    0x97c, 0x976, 0x970, 0x96a, 0x962, 0x95c, 0x956, 0x950,
    0x948, 0x942, 0x93c, 0x936, 0x930, 0x928, 0x922, 0x91c,
    0x916, 0x910, 0x90a, 0x904, 0x8fc, 0x8f6, 0x8f0, 0x8ea,
    0x8e4, 0x8de, 0x8d8, 0x8d2, 0x8cc, 0x8c6, 0x8c0, 0x8ba,
    0x8b4, 0x8ae, 0x8a8, 0x8a2, 0x89c, 0x896, 0x890, 0x88a,
    0x884, 0x87e, 0x878, 0x872, 0x86c, 0x866, 0x860, 0x85a,
    0x854, 0x850, 0x84a, 0x844, 0x83e, 0x838, 0x832, 0x82c,
    0x828, 0x822, 0x81c, 0x816, 0x810, 0x80c, 0x806, 0x800,
};

/*
    freq mult table multiplied by 2

    1/2, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 10, 12, 12, 15, 15
*/

static const uint8_t mt[16] = {
    1, 2, 4, 6, 8, 10, 12, 14, 16, 18, 20, 20, 24, 24, 30, 30
};

/*
    ksl table
*/

static const uint8_t kslrom[16] = {
    0, 32, 40, 45, 48, 51, 53, 55, 56, 58, 59, 60, 61, 62, 63, 64
};

static const uint8_t kslshift[4] = {
    8, 1, 2, 0
};

/*
    envelope generator constants
*/

static const uint8_t eg_incstep[4][4] = {
    { 0, 0, 0, 0 },
    { 1, 0, 0, 0 },
    { 1, 0, 1, 0 },
    { 1, 1, 1, 0 }
};

/*
    address decoding
*/

static const int8_t ad_slot[0x20] = {
    0, 1, 2, 3, 4, 5, -1, -1, 6, 7, 8, 9, 10, 11, -1, -1,
    12, 13, 14, 15, 16, 17, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1
};

static const uint8_t ch_slot[18] = {
    0, 1, 2, 6, 7, 8, 12, 13, 14, 18, 19, 20, 24, 25, 26, 30, 31, 32
};

#if OPL3_ENABLE_STEREOEXT
/*
    stereo extension panning table
*/

static int32_t panpot_lut[256];
static uint8_t panpot_lut_build = 0;
#endif

/*
    Envelope generator
*/

enum envelope_gen_num {
    envelope_gen_num_attack  = 0,
    envelope_gen_num_decay   = 1,
    envelope_gen_num_sustain = 2,
    envelope_gen_num_release = 3
};

static void
OPL3_EnvelopeUpdateKSL(opl3_slot *slot)
{
    int16_t ksl = (kslrom[slot->channel->f_num >> 6u] << 2)
        - ((0x08 - slot->channel->block) << 5);
    if (ksl < 0) {
        ksl = 0;
    }
    slot->eg_ksl = (uint8_t) ksl;
    /* Refresh the cached (reg_tl << 2) + (eg_ksl >> kslshift[reg_ksl])
     * sum used by OPL3_EnvelopeCalc. Both reg_tl/reg_ksl-driven changes
     * (via SlotWrite40, which calls this function) and eg_ksl-driven
     * changes (f_num/block updates via Channel{A0,B0}) flow through
     * here, so this covers all dirty cases. */
    slot->eg_tl_ksl = (uint16_t) ((slot->reg_tl << 2)
                                  + (slot->eg_ksl >> kslshift[slot->reg_ksl]));
}

static void
OPL3_EnvelopeUpdateRate(opl3_slot *slot)
{
    uint8_t ii;

    slot->eg_ks = slot->channel->ksv >> ((slot->reg_ksr ^ 1) << 1);
    for (ii = 0; ii < 4; ii++) {
        uint8_t rate    = slot->eg_ks + (slot->eg_rates[ii] << 2);
        uint8_t rate_hi = rate >> 2;
        if (rate_hi & 0x10) {
            rate_hi = 0x0f;
        }
        slot->eg_rate_hi[ii] = rate_hi;
        slot->eg_rate_lo[ii] = rate & 0x03;
    }
}

static void
OPL3_EnvelopeCalc(opl3_slot *slot)
{
    uint8_t  nonzero;
    uint8_t  rate_hi;
    uint8_t  rate_lo;
    uint8_t  reg_rate = 0;
    uint8_t  eg_shift, shift;
    uint16_t eg_rout;
    int16_t  eg_inc;
    uint8_t  eg_off;
    uint8_t  reset = 0;

    slot->eg_out = slot->eg_rout + slot->eg_tl_ksl + *slot->trem;
    if (slot->key && slot->eg_gen == envelope_gen_num_release) {
        reset    = 1;
        reg_rate = slot->eg_rates[0];
    } else {
        reg_rate = slot->eg_rates[slot->eg_gen];
    }
    slot->pg_reset = reset;
    nonzero        = (reg_rate != 0);
    if (reset) {
        rate_hi = slot->eg_rate_hi[0];
        rate_lo = slot->eg_rate_lo[0];
    } else {
        rate_hi = slot->eg_rate_hi[slot->eg_gen];
        rate_lo = slot->eg_rate_lo[slot->eg_gen];
    }
    eg_shift = rate_hi + slot->chip->eg_add;
    shift    = 0;
    if (nonzero) {
        if (rate_hi < 12) {
            if (slot->chip->eg_state) {
                switch (eg_shift) {
                    case 12:
                        shift = 1;
                        break;
                    case 13:
                        shift = (rate_lo >> 1) & 0x01;
                        break;
                    case 14:
                        shift = rate_lo & 0x01;
                        break;
                    default:
                        break;
                }
            }
        } else {
            shift = (rate_hi & 0x03) + eg_incstep[rate_lo][slot->chip->eg_timer_lo];
            if (shift & 0x04) {
                shift = 0x03;
            }
            if (!shift) {
                shift = slot->chip->eg_state;
            }
        }
    }
    eg_rout = slot->eg_rout;
    eg_inc  = 0;
    eg_off  = 0;
    /* Instant attack */
    if (reset && rate_hi == 0x0f) {
        eg_rout = 0x00;
    }
    /* Envelope off */
    if ((slot->eg_rout & 0x1f8) == 0x1f8) {
        eg_off = 1;
    }
    if (slot->eg_gen != envelope_gen_num_attack && !reset && eg_off) {
        eg_rout = 0x1ff;
    }
    switch (slot->eg_gen) {
        case envelope_gen_num_attack:
            if (!slot->eg_rout) {
                slot->eg_gen = envelope_gen_num_decay;
            } else if (slot->key && shift > 0 && rate_hi != 0x0f) {
                eg_inc = ~slot->eg_rout >> (4 - shift);
            }
            break;
        case envelope_gen_num_decay:
            if ((slot->eg_rout >> 4) == slot->reg_sl) {
                slot->eg_gen = envelope_gen_num_sustain;
            } else if (!eg_off && !reset && shift > 0) {
                eg_inc = 1 << (shift - 1);
            }
            break;
        case envelope_gen_num_sustain:
        case envelope_gen_num_release:
            if (!eg_off && !reset && shift > 0) {
                eg_inc = 1 << (shift - 1);
            }
            break;
    }
    slot->eg_rout = (eg_rout + eg_inc) & 0x1ff;
    /* Key off */
    if (reset) {
        slot->eg_gen = envelope_gen_num_attack;
    }
    if (!slot->key) {
        slot->eg_gen = envelope_gen_num_release;
    }
}

static void
OPL3_EnvelopeKeyOn(opl3_slot *slot, uint8_t type)
{
    slot->key |= type;
}

static void
OPL3_EnvelopeKeyOff(opl3_slot *slot, uint8_t type)
{
    slot->key &= ~type;
}

/*
    Phase Generator
*/

static void
OPL3_PhaseUpdateInc(opl3_slot *slot)
{
    uint32_t basefreq = ((uint32_t) slot->channel->f_num << slot->channel->block) >> 1;
    slot->pg_inc      = (basefreq * mt[slot->reg_mult]) >> 1;
}

static void
OPL3_PhaseGenerate(opl3_slot *slot)
{
    opl3_chip *chip;
    uint16_t   f_num;
    uint32_t   basefreq;
    uint32_t   phaseinc;
    uint8_t    rm_xor, n_bit;
    uint32_t   noise;
    uint16_t   phase;

    chip = slot->chip;
    if (slot->reg_vib) {
        int8_t  range;
        uint8_t vibpos;

        f_num  = slot->channel->f_num;
        range  = (f_num >> 7) & 7;
        vibpos = slot->chip->vibpos;

        if (!(vibpos & 3)) {
            range = 0;
        } else if (vibpos & 1) {
            range >>= 1;
        }
        range >>= slot->chip->vibshift;

        if (vibpos & 4) {
            range = -range;
        }
        f_num += range;
        basefreq = (f_num << slot->channel->block) >> 1;
        phaseinc = (basefreq * mt[slot->reg_mult]) >> 1;
    } else {
        phaseinc = slot->pg_inc;
    }
    phase = (uint16_t) (slot->pg_phase >> 9);
    if (slot->pg_reset) {
        slot->pg_phase = 0;
    }
    slot->pg_phase += phaseinc;
    /* Rhythm mode: dispatch on slot_num via a single switch so non-rhythm
     * slots (33 of 36) hit the default case and skip everything. The
     * fused switch also lets gcc emit a jump table instead of branches. */
    noise              = chip->noise;
    slot->pg_phase_out = phase;
    switch (slot->slot_num) {
        case 13: /* hh */
            chip->rm_hh_bit2 = (phase >> 2) & 1;
            chip->rm_hh_bit3 = (phase >> 3) & 1;
            chip->rm_hh_bit7 = (phase >> 7) & 1;
            chip->rm_hh_bit8 = (phase >> 8) & 1;
            if (chip->rhy & 0x20) {
                rm_xor = (chip->rm_hh_bit2 ^ chip->rm_hh_bit7)
                    | (chip->rm_hh_bit3 ^ chip->rm_tc_bit5)
                    | (chip->rm_tc_bit3 ^ chip->rm_tc_bit5);
                slot->pg_phase_out = rm_xor << 9;
                if (rm_xor ^ (noise & 1)) {
                    slot->pg_phase_out |= 0xd0;
                } else {
                    slot->pg_phase_out |= 0x34;
                }
            }
            break;
        case 16: /* sd */
            if (chip->rhy & 0x20) {
                slot->pg_phase_out = (chip->rm_hh_bit8 << 9)
                    | ((chip->rm_hh_bit8 ^ (noise & 1)) << 8);
            }
            break;
        case 17: /* tc */
            if (chip->rhy & 0x20) {
                chip->rm_tc_bit3 = (phase >> 3) & 1;
                chip->rm_tc_bit5 = (phase >> 5) & 1;
                rm_xor           = (chip->rm_hh_bit2 ^ chip->rm_hh_bit7)
                    | (chip->rm_hh_bit3 ^ chip->rm_tc_bit5)
                    | (chip->rm_tc_bit3 ^ chip->rm_tc_bit5);
                slot->pg_phase_out = (rm_xor << 9) | 0x80;
            }
            break;
        default:
            break;
    }
    n_bit       = ((noise >> 14) ^ noise) & 0x01;
    chip->noise = (noise >> 1) | (n_bit << 22);
}

/*
    Slot
*/

static void
OPL3_SlotWrite20(opl3_slot *slot, uint8_t data)
{
    if ((data >> 7) & 0x01) {
        slot->trem = &slot->chip->tremolo;
    } else {
        slot->trem = (uint8_t *) &slot->chip->zeromod;
    }
    slot->reg_vib     = (data >> 6) & 0x01;
    slot->reg_type    = (data >> 5) & 0x01;
    slot->eg_rates[2] = slot->reg_type ? 0 : slot->reg_rr;
    slot->reg_ksr     = (data >> 4) & 0x01;
    slot->reg_mult    = data & 0x0f;
    OPL3_EnvelopeUpdateRate(slot);
    OPL3_PhaseUpdateInc(slot);
}

static void
OPL3_SlotWrite40(opl3_slot *slot, uint8_t data)
{
    slot->reg_ksl = (data >> 6) & 0x03;
    slot->reg_tl  = data & 0x3f;
    OPL3_EnvelopeUpdateKSL(slot);
}

static void
OPL3_SlotWrite60(opl3_slot *slot, uint8_t data)
{
    slot->reg_ar      = (data >> 4) & 0x0f;
    slot->reg_dr      = data & 0x0f;
    slot->eg_rates[0] = slot->reg_ar;
    slot->eg_rates[1] = slot->reg_dr;
    OPL3_EnvelopeUpdateRate(slot);
}

static void
OPL3_SlotWrite80(opl3_slot *slot, uint8_t data)
{
    slot->reg_sl = (data >> 4) & 0x0f;
    if (slot->reg_sl == 0x0f) {
        slot->reg_sl = 0x1f;
    }
    slot->reg_rr      = data & 0x0f;
    slot->eg_rates[2] = slot->reg_type ? 0 : slot->reg_rr;
    slot->eg_rates[3] = slot->reg_rr;
    OPL3_EnvelopeUpdateRate(slot);
}

static void
OPL3_SlotWriteE0(opl3_slot *slot, uint8_t data)
{
    slot->reg_wf = data & 0x07;
    if (slot->chip->newm == 0x00) {
        slot->reg_wf &= 0x03;
    }
}

static inline void
OPL3_SlotGenerate(opl3_slot *slot)
{
    uint16_t phase    = slot->pg_phase_out + *slot->mod;
    uint16_t envelope = slot->eg_out;
    uint16_t wf_data  = logsin_wf[slot->reg_wf][phase & 0x3ff];
    uint16_t neg      = (uint16_t) (((int16_t) wf_data) >> 15);
    uint32_t level    = (wf_data & 0x7fff) + (envelope << 3);
    if (level > 0x1fff) {
        level = 0x1fff;
    }
    slot->out = ((exprom[level & 0xffu] >> (level >> 8)) ^ neg);
}

/* Silent-regime variant: when the caller has proven eg_out >= 0x180, the
 * exprom lookup always reads through to zero (max exprom value 0xff4 >> 12
 * = 0), so the final out reduces to just the sign bit of wf_data. Skips a
 * load, an add, a clamp, a shift, and a xor. */
static inline void
OPL3_SlotGenerateSilent(opl3_slot *slot)
{
    uint16_t phase   = slot->pg_phase_out + *slot->mod;
    uint16_t wf_data = logsin_wf[slot->reg_wf][phase & 0x3ff];
    slot->out        = (int16_t) wf_data >> 15;
}

static inline void
OPL3_SlotCalcFB(opl3_slot *slot)
{
    if (slot->channel->fb != 0x00) {
        slot->fbmod = (slot->prout + slot->out) >> (0x09 - slot->channel->fb);
    } else {
        slot->fbmod = 0;
    }
    slot->prout = slot->out;
}

/*
    Channel
*/

static void OPL3_ChannelSetupAlg(opl3_channel *channel);

static void
OPL3_ChannelUpdateRhythm(opl3_chip *chip, uint8_t data)
{
    opl3_channel *channel6;
    opl3_channel *channel7;
    opl3_channel *channel8;
    uint8_t       chnum;

    chip->rhy = data & 0x3f;
    if (chip->rhy & 0x20) {
        channel6          = &chip->channel[6];
        channel7          = &chip->channel[7];
        channel8          = &chip->channel[8];
        channel6->out[0]  = &channel6->slotz[1]->out;
        channel6->out[1]  = &channel6->slotz[1]->out;
        channel6->out[2]  = &chip->zeromod;
        channel6->out[3]  = &chip->zeromod;
        channel6->out_cnt = 2;
        channel7->out[0]  = &channel7->slotz[0]->out;
        channel7->out[1]  = &channel7->slotz[0]->out;
        channel7->out[2]  = &channel7->slotz[1]->out;
        channel7->out[3]  = &channel7->slotz[1]->out;
        channel7->out_cnt = 4;
        channel8->out[0]  = &channel8->slotz[0]->out;
        channel8->out[1]  = &channel8->slotz[0]->out;
        channel8->out[2]  = &channel8->slotz[1]->out;
        channel8->out[3]  = &channel8->slotz[1]->out;
        channel8->out_cnt = 4;
        for (chnum = 6; chnum < 9; chnum++) {
            chip->channel[chnum].chtype = ch_drum;
        }
        OPL3_ChannelSetupAlg(channel6);
        OPL3_ChannelSetupAlg(channel7);
        OPL3_ChannelSetupAlg(channel8);
        /* hh */
        if (chip->rhy & 0x01) {
            OPL3_EnvelopeKeyOn(channel7->slotz[0], egk_drum);
        } else {
            OPL3_EnvelopeKeyOff(channel7->slotz[0], egk_drum);
        }
        /* tc */
        if (chip->rhy & 0x02) {
            OPL3_EnvelopeKeyOn(channel8->slotz[1], egk_drum);
        } else {
            OPL3_EnvelopeKeyOff(channel8->slotz[1], egk_drum);
        }
        /* tom */
        if (chip->rhy & 0x04) {
            OPL3_EnvelopeKeyOn(channel8->slotz[0], egk_drum);
        } else {
            OPL3_EnvelopeKeyOff(channel8->slotz[0], egk_drum);
        }
        /* sd */
        if (chip->rhy & 0x08) {
            OPL3_EnvelopeKeyOn(channel7->slotz[1], egk_drum);
        } else {
            OPL3_EnvelopeKeyOff(channel7->slotz[1], egk_drum);
        }
        /* bd */
        if (chip->rhy & 0x10) {
            OPL3_EnvelopeKeyOn(channel6->slotz[0], egk_drum);
            OPL3_EnvelopeKeyOn(channel6->slotz[1], egk_drum);
        } else {
            OPL3_EnvelopeKeyOff(channel6->slotz[0], egk_drum);
            OPL3_EnvelopeKeyOff(channel6->slotz[1], egk_drum);
        }
    } else {
        for (chnum = 6; chnum < 9; chnum++) {
            chip->channel[chnum].chtype = ch_2op;
            OPL3_ChannelSetupAlg(&chip->channel[chnum]);
            OPL3_EnvelopeKeyOff(chip->channel[chnum].slotz[0], egk_drum);
            OPL3_EnvelopeKeyOff(chip->channel[chnum].slotz[1], egk_drum);
        }
    }
}

static void
OPL3_ChannelWriteA0(opl3_channel *channel, uint8_t data)
{
    if (channel->chip->newm && channel->chtype == ch_4op2) {
        return;
    }
    channel->f_num = (channel->f_num & 0x300) | data;
    channel->ksv   = (channel->block << 1)
        | ((channel->f_num >> (0x09 - channel->chip->nts)) & 0x01);
    OPL3_EnvelopeUpdateKSL(channel->slotz[0]);
    OPL3_EnvelopeUpdateKSL(channel->slotz[1]);
    OPL3_EnvelopeUpdateRate(channel->slotz[0]);
    OPL3_EnvelopeUpdateRate(channel->slotz[1]);
    OPL3_PhaseUpdateInc(channel->slotz[0]);
    OPL3_PhaseUpdateInc(channel->slotz[1]);
    if (channel->chip->newm && channel->chtype == ch_4op) {
        channel->pair->f_num = channel->f_num;
        channel->pair->ksv   = channel->ksv;
        OPL3_EnvelopeUpdateKSL(channel->pair->slotz[0]);
        OPL3_EnvelopeUpdateKSL(channel->pair->slotz[1]);
        OPL3_EnvelopeUpdateRate(channel->pair->slotz[0]);
        OPL3_EnvelopeUpdateRate(channel->pair->slotz[1]);
        OPL3_PhaseUpdateInc(channel->pair->slotz[0]);
        OPL3_PhaseUpdateInc(channel->pair->slotz[1]);
    }
}

static void
OPL3_ChannelWriteB0(opl3_channel *channel, uint8_t data)
{
    if (channel->chip->newm && channel->chtype == ch_4op2) {
        return;
    }
    channel->f_num = (channel->f_num & 0xff) | ((data & 0x03) << 8);
    channel->block = (data >> 2) & 0x07;
    channel->ksv   = (channel->block << 1)
        | ((channel->f_num >> (0x09 - channel->chip->nts)) & 0x01);
    OPL3_EnvelopeUpdateKSL(channel->slotz[0]);
    OPL3_EnvelopeUpdateKSL(channel->slotz[1]);
    OPL3_EnvelopeUpdateRate(channel->slotz[0]);
    OPL3_EnvelopeUpdateRate(channel->slotz[1]);
    OPL3_PhaseUpdateInc(channel->slotz[0]);
    OPL3_PhaseUpdateInc(channel->slotz[1]);
    if (channel->chip->newm && channel->chtype == ch_4op) {
        channel->pair->f_num = channel->f_num;
        channel->pair->block = channel->block;
        channel->pair->ksv   = channel->ksv;
        OPL3_EnvelopeUpdateKSL(channel->pair->slotz[0]);
        OPL3_EnvelopeUpdateKSL(channel->pair->slotz[1]);
        OPL3_EnvelopeUpdateRate(channel->pair->slotz[0]);
        OPL3_EnvelopeUpdateRate(channel->pair->slotz[1]);
        OPL3_PhaseUpdateInc(channel->pair->slotz[0]);
        OPL3_PhaseUpdateInc(channel->pair->slotz[1]);
    }
}

static void
OPL3_ChannelSetupAlg(opl3_channel *channel)
{
    if (channel->chtype == ch_drum) {
        if (channel->ch_num == 7 || channel->ch_num == 8) {
            channel->slotz[0]->mod = &channel->chip->zeromod;
            channel->slotz[1]->mod = &channel->chip->zeromod;
            return;
        }
        switch (channel->alg & 0x01) {
            case 0x00:
                channel->slotz[0]->mod = &channel->slotz[0]->fbmod;
                channel->slotz[1]->mod = &channel->slotz[0]->out;
                break;
            case 0x01:
                channel->slotz[0]->mod = &channel->slotz[0]->fbmod;
                channel->slotz[1]->mod = &channel->chip->zeromod;
                break;
        }
        return;
    }
    if (channel->alg & 0x08) {
        return;
    }
    if (channel->alg & 0x04) {
        channel->pair->out[0]  = &channel->chip->zeromod;
        channel->pair->out[1]  = &channel->chip->zeromod;
        channel->pair->out[2]  = &channel->chip->zeromod;
        channel->pair->out[3]  = &channel->chip->zeromod;
        channel->pair->out_cnt = 0;
        switch (channel->alg & 0x03) {
            case 0x00:
                channel->pair->slotz[0]->mod = &channel->pair->slotz[0]->fbmod;
                channel->pair->slotz[1]->mod = &channel->pair->slotz[0]->out;
                channel->slotz[0]->mod       = &channel->pair->slotz[1]->out;
                channel->slotz[1]->mod       = &channel->slotz[0]->out;
                channel->out[0]              = &channel->slotz[1]->out;
                channel->out[1]              = &channel->chip->zeromod;
                channel->out[2]              = &channel->chip->zeromod;
                channel->out[3]              = &channel->chip->zeromod;
                channel->out_cnt             = 1;
                break;
            case 0x01:
                channel->pair->slotz[0]->mod = &channel->pair->slotz[0]->fbmod;
                channel->pair->slotz[1]->mod = &channel->pair->slotz[0]->out;
                channel->slotz[0]->mod       = &channel->chip->zeromod;
                channel->slotz[1]->mod       = &channel->slotz[0]->out;
                channel->out[0]              = &channel->pair->slotz[1]->out;
                channel->out[1]              = &channel->slotz[1]->out;
                channel->out[2]              = &channel->chip->zeromod;
                channel->out[3]              = &channel->chip->zeromod;
                channel->out_cnt             = 2;
                break;
            case 0x02:
                channel->pair->slotz[0]->mod = &channel->pair->slotz[0]->fbmod;
                channel->pair->slotz[1]->mod = &channel->chip->zeromod;
                channel->slotz[0]->mod       = &channel->pair->slotz[1]->out;
                channel->slotz[1]->mod       = &channel->slotz[0]->out;
                channel->out[0]              = &channel->pair->slotz[0]->out;
                channel->out[1]              = &channel->slotz[1]->out;
                channel->out[2]              = &channel->chip->zeromod;
                channel->out[3]              = &channel->chip->zeromod;
                channel->out_cnt             = 2;
                break;
            case 0x03:
                channel->pair->slotz[0]->mod = &channel->pair->slotz[0]->fbmod;
                channel->pair->slotz[1]->mod = &channel->chip->zeromod;
                channel->slotz[0]->mod       = &channel->pair->slotz[1]->out;
                channel->slotz[1]->mod       = &channel->chip->zeromod;
                channel->out[0]              = &channel->pair->slotz[0]->out;
                channel->out[1]              = &channel->slotz[0]->out;
                channel->out[2]              = &channel->slotz[1]->out;
                channel->out[3]              = &channel->chip->zeromod;
                channel->out_cnt             = 3;
                break;
        }
    } else {
        switch (channel->alg & 0x01) {
            case 0x00:
                channel->slotz[0]->mod = &channel->slotz[0]->fbmod;
                channel->slotz[1]->mod = &channel->slotz[0]->out;
                channel->out[0]        = &channel->slotz[1]->out;
                channel->out[1]        = &channel->chip->zeromod;
                channel->out[2]        = &channel->chip->zeromod;
                channel->out[3]        = &channel->chip->zeromod;
                channel->out_cnt       = 1;
                break;
            case 0x01:
                channel->slotz[0]->mod = &channel->slotz[0]->fbmod;
                channel->slotz[1]->mod = &channel->chip->zeromod;
                channel->out[0]        = &channel->slotz[0]->out;
                channel->out[1]        = &channel->slotz[1]->out;
                channel->out[2]        = &channel->chip->zeromod;
                channel->out[3]        = &channel->chip->zeromod;
                channel->out_cnt       = 2;
                break;
        }
    }
}

static void
OPL3_ChannelUpdateAlg(opl3_channel *channel)
{
    channel->alg = channel->con;
    if (channel->chip->newm) {
        if (channel->chtype == ch_4op) {
            channel->pair->alg = 0x04 | (channel->con << 1) | (channel->pair->con);
            channel->alg       = 0x08;
            OPL3_ChannelSetupAlg(channel->pair);
        } else if (channel->chtype == ch_4op2) {
            channel->alg       = 0x04 | (channel->pair->con << 1) | (channel->con);
            channel->pair->alg = 0x08;
            OPL3_ChannelSetupAlg(channel);
        } else {
            OPL3_ChannelSetupAlg(channel);
        }
    } else {
        OPL3_ChannelSetupAlg(channel);
    }
}

static void
OPL3_ChannelWriteC0(opl3_channel *channel, uint8_t data)
{
    channel->fb  = (data & 0x0e) >> 1;
    channel->con = data & 0x01;
    OPL3_ChannelUpdateAlg(channel);
    if (channel->chip->newm) {
        channel->cha = ((data >> 4) & 0x01) ? ~0 : 0;
        channel->chb = ((data >> 5) & 0x01) ? ~0 : 0;
        channel->chc = ((data >> 6) & 0x01) ? ~0 : 0;
        channel->chd = ((data >> 7) & 0x01) ? ~0 : 0;
    } else {
        channel->cha = channel->chb = (uint16_t) ~0;
        // TODO: Verify on real chip if DAC2 output is disabled in compat mode
        channel->chc = channel->chd = 0;
    }
#if OPL3_ENABLE_STEREOEXT
    if (!channel->chip->stereoext) {
        channel->leftpan  = channel->cha << 16;
        channel->rightpan = channel->chb << 16;
    }
#endif
}

#if OPL3_ENABLE_STEREOEXT
static void
OPL3_ChannelWriteD0(opl3_channel *channel, uint8_t data)
{
    if (channel->chip->stereoext) {
        channel->leftpan  = panpot_lut[data ^ 0xffu];
        channel->rightpan = panpot_lut[data];
    }
}
#endif

static void
OPL3_ChannelKeyOn(opl3_channel *channel)
{
    if (channel->chip->newm) {
        if (channel->chtype == ch_4op) {
            OPL3_EnvelopeKeyOn(channel->slotz[0], egk_norm);
            OPL3_EnvelopeKeyOn(channel->slotz[1], egk_norm);
            OPL3_EnvelopeKeyOn(channel->pair->slotz[0], egk_norm);
            OPL3_EnvelopeKeyOn(channel->pair->slotz[1], egk_norm);
        } else if (channel->chtype == ch_2op || channel->chtype == ch_drum) {
            OPL3_EnvelopeKeyOn(channel->slotz[0], egk_norm);
            OPL3_EnvelopeKeyOn(channel->slotz[1], egk_norm);
        }
    } else {
        OPL3_EnvelopeKeyOn(channel->slotz[0], egk_norm);
        OPL3_EnvelopeKeyOn(channel->slotz[1], egk_norm);
    }
}

static void
OPL3_ChannelKeyOff(opl3_channel *channel)
{
    if (channel->chip->newm) {
        if (channel->chtype == ch_4op) {
            OPL3_EnvelopeKeyOff(channel->slotz[0], egk_norm);
            OPL3_EnvelopeKeyOff(channel->slotz[1], egk_norm);
            OPL3_EnvelopeKeyOff(channel->pair->slotz[0], egk_norm);
            OPL3_EnvelopeKeyOff(channel->pair->slotz[1], egk_norm);
        } else if (channel->chtype == ch_2op || channel->chtype == ch_drum) {
            OPL3_EnvelopeKeyOff(channel->slotz[0], egk_norm);
            OPL3_EnvelopeKeyOff(channel->slotz[1], egk_norm);
        }
    } else {
        OPL3_EnvelopeKeyOff(channel->slotz[0], egk_norm);
        OPL3_EnvelopeKeyOff(channel->slotz[1], egk_norm);
    }
}

static void
OPL3_ChannelSet4Op(opl3_chip *chip, uint8_t data)
{
    uint8_t bit;
    uint8_t chnum;
    for (bit = 0; bit < 6; bit++) {
        chnum = bit;
        if (bit >= 3) {
            chnum += 9 - 3;
        }
        if ((data >> bit) & 0x01) {
            chip->channel[chnum].chtype      = ch_4op;
            chip->channel[chnum + 3u].chtype = ch_4op2;
            OPL3_ChannelUpdateAlg(&chip->channel[chnum]);
        } else {
            chip->channel[chnum].chtype      = ch_2op;
            chip->channel[chnum + 3u].chtype = ch_2op;
            OPL3_ChannelUpdateAlg(&chip->channel[chnum]);
            OPL3_ChannelUpdateAlg(&chip->channel[chnum + 3u]);
        }
    }
}

static void
OPL3_ProcessSlot(opl3_slot *slot)
{
    /* Fast path for fully-attenuated key-off non-rhythm slots. The envelope
     * rate machine cannot change eg_rout here, but the full path still updates
     * feedback history, eg_out/eg_gen/pg_reset, phase output, noise, and out. */
    if (!slot->key && slot->eg_rout == 0x1ff
        && slot->slot_num != 13 && slot->slot_num != 16 && slot->slot_num != 17) {
        opl3_chip *chip = slot->chip;
        uint32_t   phaseinc;
        uint16_t   phase;
        uint32_t   noise = chip->noise;
        uint8_t    n_bit = ((noise >> 14) ^ noise) & 0x01;

        if (slot->channel->fb == 0 && slot->pg_inc == 0 && slot->out == 0
            && *slot->mod == 0 && slot->eg_tl_ksl == 0 && *slot->trem == 0
            && slot->pg_phase == 0 && slot->reg_vib == 0 && slot->reg_wf == 0) {
            slot->fbmod        = 0;
            slot->prout        = 0;
            slot->eg_out       = 0x1ff;
            slot->pg_reset     = 0;
            slot->eg_gen       = envelope_gen_num_release;
            slot->pg_phase_out = 0;
            chip->noise        = (noise >> 1) | (n_bit << 22);
            return;
        }

        OPL3_SlotCalcFB(slot);

        slot->eg_out   = slot->eg_rout + slot->eg_tl_ksl + *slot->trem;
        slot->pg_reset = 0;
        slot->eg_gen   = envelope_gen_num_release;

        if (slot->reg_vib) {
            uint16_t f_num  = slot->channel->f_num;
            int8_t   range  = (f_num >> 7) & 7;
            uint8_t  vibpos = chip->vibpos;

            if (!(vibpos & 3)) {
                range = 0;
            } else if (vibpos & 1) {
                range >>= 1;
            }
            range >>= chip->vibshift;

            if (vibpos & 4) {
                range = -range;
            }
            f_num += range;
            phaseinc = (((uint32_t) f_num << slot->channel->block) >> 1)
                    * mt[slot->reg_mult]
                >> 1;
        } else {
            phaseinc = slot->pg_inc;
        }

        phase = (uint16_t) (slot->pg_phase >> 9);
        slot->pg_phase += phaseinc;
        slot->pg_phase_out = phase;
        chip->noise        = (noise >> 1) | (n_bit << 22);

        /* eg_out = eg_rout + eg_tl_ksl + *trem >= 0x1ff here, so the
         * silent-regime shortcut is always valid. */
        OPL3_SlotGenerateSilent(slot);
        return;
    }
    if (slot->eg_gen == envelope_gen_num_sustain && slot->key
        && slot->eg_rates[envelope_gen_num_sustain] == 0) {
        OPL3_SlotCalcFB(slot);
        slot->eg_out   = slot->eg_rout + slot->eg_tl_ksl + *slot->trem;
        slot->pg_reset = 0;
        if ((slot->eg_rout & 0x1f8) == 0x1f8) {
            slot->eg_rout = 0x1ff;
        }

        if (!slot->reg_vib
            && slot->slot_num != 13 && slot->slot_num != 16 && slot->slot_num != 17) {
            opl3_chip *chip  = slot->chip;
            uint32_t   noise = chip->noise;
            uint8_t    n_bit = ((noise >> 14) ^ noise) & 0x01;
            uint16_t   phase = (uint16_t) (slot->pg_phase >> 9);

            slot->pg_phase += slot->pg_inc;
            slot->pg_phase_out = phase;
            chip->noise        = (noise >> 1) | (n_bit << 22);
        } else {
            OPL3_PhaseGenerate(slot);
        }

        OPL3_SlotGenerate(slot);
        return;
    }
    OPL3_SlotCalcFB(slot);
    OPL3_EnvelopeCalc(slot);
    OPL3_PhaseGenerate(slot);
    OPL3_SlotGenerate(slot);
}

static inline void
OPL3_Generate4Ch(void *priv, int32_t *buf4)
{
    opl3_chip     *chip = (opl3_chip *) priv;
    opl3_channel  *channel;
    opl3_writebuf *writebuf;
    int16_t      **out;
    int32_t        mix[2];
    uint8_t        ii;
    int16_t        accm;
    uint8_t        shift = 0;
    uint8_t        update_tremolo;

    buf4[1] = chip->mixbuff[1];
    buf4[3] = chip->mixbuff[3];

#if OPL3_QUIRK_CHANNELSAMPLEDELAY
    for (ii = 0; ii < 15; ii++)
#else
    for (ii = 0; ii < 36; ii++)
#endif
    {
        OPL3_ProcessSlot(&chip->slot[ii]);
    }

    mix[0] = mix[1] = 0;
    for (ii = 0; ii < 18; ii++) {
        channel = &chip->channel[ii];
        if (!channel->out_cnt)
            continue;
#if OPL3_ENABLE_STEREOEXT
        if (!(channel->leftpan | channel->chc))
            continue;
#else
        if (!(channel->cha | channel->chc))
            continue;
#endif
        out  = channel->out;
        accm = *out[0];
        if (channel->out_cnt > 1) {
            accm += *out[1];
            if (channel->out_cnt > 2) {
                accm += *out[2];
                if (channel->out_cnt > 3)
                    accm += *out[3];
            }
        }
#if OPL3_ENABLE_STEREOEXT
        mix[0] += (int16_t) ((accm * channel->leftpan) >> 16);
#else
        mix[0] += (int16_t) (accm & channel->cha);
#endif
        mix[1] += (int16_t) (accm & channel->chc);
    }
    chip->mixbuff[0] = mix[0];
    chip->mixbuff[2] = mix[1];

#if OPL3_QUIRK_CHANNELSAMPLEDELAY
    for (ii = 15; ii < 18; ii++) {
        OPL3_ProcessSlot(&chip->slot[ii]);
    }
#endif

    buf4[0] = chip->mixbuff[0];
    buf4[2] = chip->mixbuff[2];

#if OPL3_QUIRK_CHANNELSAMPLEDELAY
    for (ii = 18; ii < 33; ii++) {
        OPL3_ProcessSlot(&chip->slot[ii]);
    }
#endif

    mix[0] = mix[1] = 0;
    for (ii = 0; ii < 18; ii++) {
        channel = &chip->channel[ii];
        if (!channel->out_cnt)
            continue;
        out  = channel->out;
        accm = *out[0];
        if (channel->out_cnt > 1) {
            accm += *out[1];
            if (channel->out_cnt > 2) {
                accm += *out[2];
                if (channel->out_cnt > 3)
                    accm += *out[3];
            }
        }
#if OPL3_ENABLE_STEREOEXT
        mix[0] += (int16_t) ((accm * channel->rightpan) >> 16);
#else
        mix[0] += (int16_t) (accm & channel->chb);
#endif
        mix[1] += (int16_t) (accm & channel->chd);
    }
    chip->mixbuff[1] = mix[0];
    chip->mixbuff[3] = mix[1];

#if OPL3_QUIRK_CHANNELSAMPLEDELAY
    for (ii = 33; ii < 36; ii++) {
        OPL3_ProcessSlot(&chip->slot[ii]);
    }
#endif

    update_tremolo = chip->tremolo_dirty;
    if ((chip->timer & 0x3f) == 0x3f) {
        chip->tremolopos++;
        if (chip->tremolopos == 210) {
            chip->tremolopos = 0;
        }
        update_tremolo = 1;
    }
    if (update_tremolo) {
        if (chip->tremolopos < 105) {
            chip->tremolo = chip->tremolopos >> chip->tremoloshift;
        } else {
            chip->tremolo = (210 - chip->tremolopos) >> chip->tremoloshift;
        }
        chip->tremolo_dirty = 0;
    }

    if ((chip->timer & 0x3ff) == 0x3ff) {
        chip->vibpos = (chip->vibpos + 1) & 7;
    }

    chip->timer++;

    if (chip->eg_state) {
        uint32_t eg_timer_low = (uint32_t) chip->eg_timer & 0x1fffu;
        if (!eg_timer_low) {
            chip->eg_add = 0;
        } else {
#if defined(__GNUC__) || defined(__clang__)
            shift = (uint8_t) __builtin_ctz(eg_timer_low);
#else
            while (((eg_timer_low >> shift) & 1) == 0) {
                shift++;
            }
#endif
            chip->eg_add = shift + 1;
        }
        chip->eg_timer_lo = (uint8_t) (chip->eg_timer & 0x3u);
    }

    if (chip->eg_timerrem || chip->eg_state) {
        if (chip->eg_timer == UINT64_C(0xfffffffff)) {
            chip->eg_timer    = 0;
            chip->eg_timerrem = 1;
        } else {
            chip->eg_timer++;
            chip->eg_timerrem = 0;
        }
    }

    chip->eg_state ^= 1;

    while ((writebuf = &chip->writebuf[chip->writebuf_cur]), writebuf->time <= chip->writebuf_samplecnt) {
        if (!(writebuf->reg & 0x200)) {
            break;
        }
        writebuf->reg &= 0x1ff;
        OPL3_WriteReg(chip, writebuf->reg, writebuf->data);
        chip->writebuf_cur = (chip->writebuf_cur + 1) % OPL3_WRITEBUF_SIZE;
    }
    chip->writebuf_samplecnt++;
}

void
OPL3_Generate(opl3_chip *chip, int32_t *buf)
{
    int32_t samples[4];

    OPL3_Generate4Ch(chip, samples);

    buf[0] = samples[0];
    buf[1] = samples[1];
}

void
OPL3_Generate4ChResampled(opl3_chip *chip, int32_t *buf4)
{
    while (chip->samplecnt >= chip->rateratio) {
        chip->oldsamples[0] = chip->samples[0];
        chip->oldsamples[1] = chip->samples[1];
        chip->oldsamples[2] = chip->samples[2];
        chip->oldsamples[3] = chip->samples[3];
        OPL3_Generate4Ch(chip, chip->samples);
        chip->samplecnt -= chip->rateratio;
    }

    buf4[0] = (int32_t) ((chip->oldsamples[0] * (chip->rateratio - chip->samplecnt)
                          + chip->samples[0] * chip->samplecnt)
                         / chip->rateratio);
    buf4[1] = (int32_t) ((chip->oldsamples[1] * (chip->rateratio - chip->samplecnt)
                          + chip->samples[1] * chip->samplecnt)
                         / chip->rateratio);
    buf4[2] = (int32_t) ((chip->oldsamples[2] * (chip->rateratio - chip->samplecnt)
                          + chip->samples[2] * chip->samplecnt)
                         / chip->rateratio);
    buf4[3] = (int32_t) ((chip->oldsamples[3] * (chip->rateratio - chip->samplecnt)
                          + chip->samples[3] * chip->samplecnt)
                         / chip->rateratio);

    chip->samplecnt += 1 << RSM_FRAC;
}

void
OPL3_GenerateResampled(opl3_chip *chip, int32_t *buf)
{
    int32_t samples[4];
    OPL3_Generate4ChResampled(chip, samples);
    buf[0] = samples[0];
    buf[1] = samples[1];
}

void
OPL3_Reset(opl3_chip *chip, uint32_t samplerate)
{
    opl3_slot    *slot;
    opl3_channel *channel;
    uint8_t       local_ch_slot;

    memset(chip, 0x00, sizeof(opl3_chip));

    for (uint8_t slotnum = 0; slotnum < 36; slotnum++) {
        slot           = &chip->slot[slotnum];
        slot->chip     = chip;
        slot->mod      = &chip->zeromod;
        slot->eg_rout  = 0x1ff;
        slot->eg_out   = 0x1ff;
        slot->eg_gen   = envelope_gen_num_release;
        slot->trem     = (uint8_t *) &chip->zeromod;
        slot->slot_num = slotnum;
    }

    for (uint8_t channum = 0; channum < 18; channum++) {
        channel                                = &chip->channel[channum];
        local_ch_slot                          = ch_slot[channum];
        channel->slotz[0]                      = &chip->slot[local_ch_slot];
        channel->slotz[1]                      = &chip->slot[local_ch_slot + 3u];
        chip->slot[local_ch_slot].channel      = channel;
        chip->slot[local_ch_slot + 3u].channel = channel;

        if ((channum % 9) < 3)
            channel->pair = &chip->channel[channum + 3u];
        else if ((channum % 9) < 6)
            channel->pair = &chip->channel[channum - 3u];

        channel->chip   = chip;
        channel->out[0] = &chip->zeromod;
        channel->out[1] = &chip->zeromod;
        channel->out[2] = &chip->zeromod;
        channel->out[3] = &chip->zeromod;
        channel->chtype = ch_2op;
        channel->cha    = 0xffff;
        channel->chb    = 0xffff;
#if OPL3_ENABLE_STEREOEXT
        channel->leftpan  = 0x10000;
        channel->rightpan = 0x10000;
#endif
        channel->ch_num = channum;

        OPL3_ChannelSetupAlg(channel);
    }

    chip->noise        = 1;
    chip->rateratio    = (samplerate << RSM_FRAC) / FREQ_49716;
    chip->tremoloshift = 4;
    chip->vibshift     = 1;

#if OPL3_ENABLE_STEREOEXT
    if (!panpot_lut_build) {
        for (int32_t i = 0; i < 256; i++)
            panpot_lut[i] = OPL3_SIN(i);

        panpot_lut_build = 1;
    }
#endif
}

uint16_t
nuked_opl3_write_addr(void *priv, uint16_t port, uint8_t val)
{
    const opl3_chip *chip = (opl3_chip *) priv;
    uint16_t         addr = val;

    if ((port & 0x0002) && ((addr == 0x0005) || chip->newm))
        addr |= 0x0100;

    return addr;
}

void
OPL3_WriteReg(void *priv, uint16_t reg, uint8_t val)
{
    opl3_chip *chip = (opl3_chip *) priv;
    uint8_t    high = (reg >> 8) & 0x01;
    uint8_t    regm = reg & 0xff;
    switch (regm & 0xf0) {
        case 0x00:
            if (high) {
                switch (regm & 0x0f) {
                    case 0x04:
                        OPL3_ChannelSet4Op(chip, val);
                        break;
                    case 0x05:
                        chip->newm = val & 0x01;
#if OPL3_ENABLE_STEREOEXT
                        chip->stereoext = (val >> 1) & 0x01;
#endif
                        break;
                }
            } else {
                switch (regm & 0x0f) {
                    case 0x08:
                        chip->nts = (val >> 6) & 0x01;
                        break;
                }
            }
            break;
        case 0x20:
        case 0x30:
            if (ad_slot[regm & 0x1fu] >= 0) {
                OPL3_SlotWrite20(&chip->slot[18u * high + ad_slot[regm & 0x1fu]], val);
            }
            break;
        case 0x40:
        case 0x50:
            if (ad_slot[regm & 0x1fu] >= 0) {
                OPL3_SlotWrite40(&chip->slot[18u * high + ad_slot[regm & 0x1fu]], val);
            }
            break;
        case 0x60:
        case 0x70:
            if (ad_slot[regm & 0x1fu] >= 0) {
                OPL3_SlotWrite60(&chip->slot[18u * high + ad_slot[regm & 0x1fu]], val);
            }
            break;
        case 0x80:
        case 0x90:
            if (ad_slot[regm & 0x1fu] >= 0) {
                OPL3_SlotWrite80(&chip->slot[18u * high + ad_slot[regm & 0x1fu]], val);
            }
            break;
        case 0xe0:
        case 0xf0:
            if (ad_slot[regm & 0x1fu] >= 0) {
                OPL3_SlotWriteE0(&chip->slot[18u * high + ad_slot[regm & 0x1fu]], val);
            }
            break;
        case 0xa0:
            if ((regm & 0x0f) < 9) {
                OPL3_ChannelWriteA0(&chip->channel[9u * high + (regm & 0x0fu)], val);
            }
            break;
        case 0xb0:
            if (regm == 0xbd && !high) {
                uint8_t tremoloshift = (((val >> 7) ^ 1) << 1) + 2;
                if (chip->tremoloshift != tremoloshift) {
                    chip->tremolo_dirty = 1;
                }
                chip->tremoloshift = tremoloshift;
                chip->vibshift     = ((val >> 6) & 0x01) ^ 1;
                OPL3_ChannelUpdateRhythm(chip, val);
            } else if ((regm & 0x0f) < 9) {
                OPL3_ChannelWriteB0(&chip->channel[9u * high + (regm & 0x0fu)], val);
                if (val & 0x20) {
                    OPL3_ChannelKeyOn(&chip->channel[9u * high + (regm & 0x0fu)]);
                } else {
                    OPL3_ChannelKeyOff(&chip->channel[9u * high + (regm & 0x0fu)]);
                }
            }
            break;
        case 0xc0:
            if ((regm & 0x0f) < 9) {
                OPL3_ChannelWriteC0(&chip->channel[9u * high + (regm & 0x0fu)], val);
            }
            break;
#if OPL3_ENABLE_STEREOEXT
        case 0xd0:
            if ((regm & 0x0f) < 9) {
                OPL3_ChannelWriteD0(&chip->channel[9u * high + (regm & 0x0fu)], val);
            }
            break;
#endif
    }
}

void
OPL3_WriteRegBuffered(void *priv, uint16_t reg, uint8_t val)
{
    opl3_chip     *chip = (opl3_chip *) priv;
    uint64_t       time1, time2;
    opl3_writebuf *writebuf;
    uint32_t       writebuf_last;

    writebuf_last = chip->writebuf_last;
    writebuf      = &chip->writebuf[writebuf_last];

    if (writebuf->reg & 0x200) {
        OPL3_WriteReg(chip, writebuf->reg & 0x1ff, writebuf->data);

        chip->writebuf_cur       = (writebuf_last + 1) % OPL3_WRITEBUF_SIZE;
        chip->writebuf_samplecnt = writebuf->time;
    }

    writebuf->reg  = reg | 0x200;
    writebuf->data = val;
    time1          = chip->writebuf_lasttime + OPL3_WRITEBUF_DELAY;
    time2          = chip->writebuf_samplecnt;

    if (time1 < time2) {
        time1 = time2;
    }

    writebuf->time          = time1;
    chip->writebuf_lasttime = time1;
    chip->writebuf_last     = (writebuf_last + 1) % OPL3_WRITEBUF_SIZE;
}

void
OPL3_Generate4ChStream(opl3_chip *chip, int32_t *sndptr1, int32_t *sndptr2, uint32_t numsamples)
{
    int32_t samples[4];

    for (uint_fast32_t i = 0; i < numsamples; i++) {
        OPL3_Generate4Ch(chip, samples);
        sndptr1[0] = samples[0];
        sndptr1[1] = samples[1];
        sndptr2[0] = samples[2];
        sndptr2[1] = samples[3];
        sndptr1 += 2;
        sndptr2 += 2;
    }
}

void
OPL3_GenerateStream(opl3_chip *chip, int32_t *sndptr, uint32_t numsamples)
{
    for (uint_fast32_t i = 0; i < numsamples; i++) {
        OPL3_Generate(chip, sndptr);

        sndptr += 2;
    }
}

void
OPL3_GenerateResampledStream(opl3_chip *chip, int32_t *sndptr, uint32_t numsamples)
{
    for (uint_fast32_t i = 0; i < numsamples; i++) {
        OPL3_GenerateResampled(chip, sndptr);

        sndptr += 2;
    }
}

static void
nuked_opl3_timer_tick(nuked_opl3_drv_t *dev, int tmr)
{
    dev->timer_cur_count[tmr] = (dev->timer_cur_count[tmr] + 1) & 0xff;

    nuked_opl3_log("Ticking timer %i, count now %02X...\n", tmr, dev->timer_cur_count[tmr]);

    if (dev->timer_cur_count[tmr] == 0x00) {
        dev->status |= ((STAT_TMR1_OVER >> tmr) & ~dev->timer_ctrl);
        dev->timer_cur_count[tmr] = dev->timer_count[tmr];

        nuked_opl3_log("Count wrapped around to zero, reloading timer %i (%02X), status = %02X...\n", tmr, (STAT_TMR1_OVER >> tmr), dev->status);
    }

    timer_on_auto(&dev->timers[tmr], (tmr == 1) ? 320.0 : 80.0);
}

static void
nuked_opl3_timer_control(nuked_opl3_drv_t *dev, int tmr, int start)
{
    timer_on_auto(&dev->timers[tmr], 0.0);

    if (start) {
        nuked_opl3_log("Loading timer %i count: %02X = %02X\n", tmr, dev->timer_cur_count[tmr], dev->timer_count[tmr]);
        dev->timer_cur_count[tmr] = dev->timer_count[tmr];
        if (dev->flags & FLAG_OPL3)
            nuked_opl3_timer_tick(dev, tmr); // Per the YMF 262 datasheet, OPL3 starts counting immediately, unlike OPL2.
        else
            timer_on_auto(&dev->timers[tmr], (tmr == 1) ? 320.0 : 80.0);
    } else {
        nuked_opl3_log("Timer %i stopped\n", tmr);
        if (tmr == 1) {
            dev->status &= ~STAT_TMR2_OVER;
        } else
            dev->status &= ~STAT_TMR1_OVER;
    }
}

static void
nuked_opl3_timer_1(void *priv)
{
    nuked_opl3_drv_t *dev = (nuked_opl3_drv_t *) priv;

    nuked_opl3_timer_tick(dev, 0);
}

static void
nuked_opl3_timer_2(void *priv)
{
    nuked_opl3_drv_t *dev = (nuked_opl3_drv_t *) priv;

    nuked_opl3_timer_tick(dev, 1);
}

static void
nuked_opl3_drv_set_do_cycles(void *priv, int8_t do_cycles)
{
    nuked_opl3_drv_t *dev = (nuked_opl3_drv_t *) priv;

    if (do_cycles)
        dev->flags |= FLAG_CYCLES;
    else
        dev->flags &= ~FLAG_CYCLES;
}

static int32_t *
nuked_opl3_drv_update(void *priv)
{
    nuked_opl3_drv_t *dev = (nuked_opl3_drv_t *) priv;

    if (dev->pos >= music_pos_global)
        return dev->buffer;

    OPL3_GenerateStream(&dev->opl,
                        &dev->buffer[dev->pos * 2],
                        music_pos_global - dev->pos);

    for (; dev->pos < music_pos_global; dev->pos++) {
        dev->buffer[dev->pos * 2] /= 2;
        dev->buffer[(dev->pos * 2) + 1] /= 2;
    }

    return dev->buffer;
}

static int32_t *
nuked_opl3_drv_update_48k(void *priv)
{
    nuked_opl3_drv_t *dev = (nuked_opl3_drv_t *) priv;

    if (dev->pos >= sound_pos_global)
        return dev->buffer;

    OPL3_GenerateResampledStream(&dev->opl,
                                 &dev->buffer[dev->pos * 2],
                                 sound_pos_global - dev->pos);

    for (; dev->pos < sound_pos_global; dev->pos++) {
        dev->buffer[dev->pos * 2] /= 2;
        dev->buffer[(dev->pos * 2) + 1] /= 2;
    }

    return dev->buffer;
}

static uint8_t
nuked_opl3_drv_read(uint16_t port, void *priv)
{
    nuked_opl3_drv_t *dev = (nuked_opl3_drv_t *) priv;

    if (dev->flags & FLAG_CYCLES)
        cycles -= ((int) (isa_timing * 8));

    dev->update(dev);

    uint8_t ret = 0xff;

    if ((port & 0x0003) == 0x0000) {
        ret = dev->status;
        if (dev->status & STAT_TMR_OVER)
            ret |= STAT_TMR_ANY;
    }

    nuked_opl3_log("OPL statret = %02x, status = %02x\n", ret, dev->status);

    return ret;
}

static void
nuked_opl3_drv_write(uint16_t port, uint8_t val, void *priv)
{
    nuked_opl3_drv_t *dev = (nuked_opl3_drv_t *) priv;

    dev->update(dev);

    if ((port & 0x0001) == 0x0001) {
        OPL3_WriteRegBuffered(&dev->opl, dev->port, val);

        switch (dev->port) {
            case 0x002: // Timer 1
                dev->timer_count[0] = val;
                nuked_opl3_log("Timer 0 count now: %i\n", dev->timer_count[0]);
                break;

            case 0x003: // Timer 2
                dev->timer_count[1] = val;
                nuked_opl3_log("Timer 1 count now: %i\n", dev->timer_count[1]);
                break;

            case 0x004: // Timer control
                if (val & CTRL_RESET) {
                    nuked_opl3_log("Resetting timer status...\n");
                    dev->status &= ~STAT_TMR_OVER;
                } else {
                    dev->timer_ctrl = val;
                    nuked_opl3_timer_control(dev, 0, val & CTRL_TMR1_START);
                    nuked_opl3_timer_control(dev, 1, val & CTRL_TMR2_START);
                    nuked_opl3_log("Status mask now %02X (val = %02X)\n", (val & ~CTRL_TMR_MASK) & CTRL_TMR_MASK, val);
                }
                break;

            case 0x105:
                dev->opl.newm = val & 0x01;
                break;

            default:
                break;
        }
    } else {
        dev->port = nuked_opl3_write_addr(&dev->opl, port, val) & 0x01ff;

        if (!(dev->flags & FLAG_OPL3))
            dev->port &= 0x00ff;
    }
}

static void
nuked_opl3_drv_reset_buffer(void *priv)
{
    nuked_opl3_drv_t *dev = (nuked_opl3_drv_t *) priv;

    dev->pos = 0;
}

static void
nuked_opl3_drv_close(void *priv)
{
    nuked_opl3_drv_t *dev = (nuked_opl3_drv_t *) priv;

    free(dev);
}

static void *
nuked_opl3_drv_init(const device_t *info)
{
    nuked_opl3_drv_t *dev = (nuked_opl3_drv_t *) calloc(1, sizeof(nuked_opl3_drv_t));
    dev->flags       = FLAG_CYCLES;
    if (((info->local & FM_TYPE_MASK) == FM_YMF262) || ((info->local & FM_TYPE_MASK) == FM_YMF289B))
        dev->flags |= FLAG_OPL3;
    else
        dev->status = 0x06;

    dev->is_48k      = !!(info->local & FM_FORCE_48K);

    // Initialize the NukedOPL3 object.
    if (dev->is_48k) {
        dev->update      = nuked_opl3_drv_update_48k;
        OPL3_Reset(&dev->opl, FREQ_48000);
    } else {
        dev->update      = nuked_opl3_drv_update;
        OPL3_Reset(&dev->opl, FREQ_49716);
    }

    timer_add(&dev->timers[0], nuked_opl3_timer_1, dev, 0);
    timer_add(&dev->timers[1], nuked_opl3_timer_2, dev, 0);

    return dev;
}

const device_t ym3812_nuked_opl3_device = {
    .name          = "Yamaha YM3812 OPL2 (NUKED OPL3)",
    .internal_name = "ym3812_nuked_opl3",
    .flags         = 0,
    .local         = FM_YM3812,
    .init          = nuked_opl3_drv_init,
    .close         = nuked_opl3_drv_close,
    .reset         = NULL,
    .available     = NULL,
    .speed_changed = NULL,
    .force_redraw  = NULL,
    .config        = NULL
};

const device_t ymf262_nuked_opl3_device = {
    .name          = "Yamaha YMF262 OPL3 (NUKED OPL3)",
    .internal_name = "ymf262_nuked",
    .flags         = 0,
    .local         = FM_YMF262,
    .init          = nuked_opl3_drv_init,
    .close         = nuked_opl3_drv_close,
    .reset         = NULL,
    .available     = NULL,
    .speed_changed = NULL,
    .force_redraw  = NULL,
    .config        = NULL
};

const device_t ymf289b_nuked_opl3_device = {
    .name          = "Yamaha YMF262 OPL3 (NUKED OPL3)",
    .internal_name = "ymf289b_nuked",
    .flags         = 0,
    .local         = FM_YMF289B,
    .init          = nuked_opl3_drv_init,
    .close         = nuked_opl3_drv_close,
    .reset         = NULL,
    .available     = NULL,
    .speed_changed = NULL,
    .force_redraw  = NULL,
    .config        = NULL
};

const fm_drv_t nuked_opl3_drv = {
    .read          = &nuked_opl3_drv_read,
    .write         = &nuked_opl3_drv_write,
    .update        = &nuked_opl3_drv_update,
    .reset_buffer  = &nuked_opl3_drv_reset_buffer,
    .set_do_cycles = &nuked_opl3_drv_set_do_cycles,
    .priv          = NULL,
    .generate      = NULL,
};

const fm_drv_t nuked_opl3_drv_48k = {
    .read          = &nuked_opl3_drv_read,
    .write         = &nuked_opl3_drv_write,
    .update        = &nuked_opl3_drv_update_48k,
    .reset_buffer  = &nuked_opl3_drv_reset_buffer,
    .set_do_cycles = &nuked_opl3_drv_set_do_cycles,
    .priv          = NULL,
    .generate      = NULL,
};
