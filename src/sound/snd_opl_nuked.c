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
 *          Version: 1.8
 *
 *          Translation from C++ into C done by Miran Grca.
 *
 * **TODO** The OPL3 is a stereo chip, and, thus, always generates
 *          a two-sample stream of data, for the L and R channels,
 *          in that order. The OPL2, however, is mono. What should
 *          we generate for that?
 *
 * Version: @(#)snd_opl_nuked.c    1.0.5    2020/07/16
 *
 * Authors: Fred N. van Kempen, <decwiz@yahoo.com>
 *          Miran Grca, <mgrca8@gmail.com>
 *          Alexey Khokholov (Nuke.YKT)
 *
 *          Copyright 2017-2020 Fred N. van Kempen.
 *          Copyright 2016-2020 Miran Grca.
 *          Copyright 2013-2020 Alexey Khokholov (Nuke.YKT)
 */
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define HAVE_STDARG_H
#include <86box/86box.h>
#include <86box/sound.h>
#include <86box/timer.h>
#include <86box/device.h>
#include <86box/snd_opl.h>
#include <86box/snd_opl_nuked.h>


#if OPL_ENABLE_STEREOEXT && !defined OPL_SIN
#ifndef _USE_MATH_DEFINES
#define _USE_MATH_DEFINES 1
#endif
#include <math.h>
// input: [0, 256), output: [0, 65536]
#define OPL_SIN(x) ((int32_t)(sin((x) * M_PI / 512.0) * 65536.0))
#endif

/* Quirk: Some FM channels are output one sample later on the left side than the right. */
#ifndef OPL_QUIRK_CHANNELSAMPLEDELAY
#define OPL_QUIRK_CHANNELSAMPLEDELAY (!OPL_ENABLE_STEREOEXT)
#endif

#define RSM_FRAC    10

// Channel types
enum {
    ch_2op  = 0,
    ch_4op  = 1,
    ch_4op2 = 2,
    ch_drum = 3
};

// Envelope key types
enum {
    egk_norm = 0x01,
    egk_drum = 0x02
};

#ifdef ENABLE_OPL_LOG
int nuked_do_log = ENABLE_OPL_LOG;

static void
nuked_log(const char *fmt, ...)
{
    va_list ap;

    if (nuked_do_log) {
        va_start(ap, fmt);
        pclog_ex(fmt, ap);
        va_end(ap);
    }
}
#else
#    define nuked_log(fmt, ...)
#endif

// logsin table
static const uint16_t logsinrom[256] = {
    0x859, 0x6c3, 0x607, 0x58b, 0x52e, 0x4e4, 0x4a6, 0x471,
    0x443, 0x41a, 0x3f5, 0x3d3, 0x3b5, 0x398, 0x37e, 0x365,
    0x34e, 0x339, 0x324, 0x311, 0x2ff, 0x2ed, 0x2dc, 0x2cd,
    0x2bd, 0x2af, 0x2a0, 0x293, 0x286, 0x279, 0x26d, 0x261,
    0x256, 0x24b, 0x240, 0x236, 0x22c, 0x222, 0x218, 0x20f,
    0x206, 0x1fd, 0x1f5, 0x1ec, 0x1e4, 0x1dc, 0x1d4, 0x1cd,
    0x1c5, 0x1be, 0x1b7, 0x1b0, 0x1a9, 0x1a2, 0x19b, 0x195,
    0x18f, 0x188, 0x182, 0x17c, 0x177, 0x171, 0x16b, 0x166,
    0x160, 0x15b, 0x155, 0x150, 0x14b, 0x146, 0x141, 0x13c,
    0x137, 0x133, 0x12e, 0x129, 0x125, 0x121, 0x11c, 0x118,
    0x114, 0x10f, 0x10b, 0x107, 0x103, 0x0ff, 0x0fb, 0x0f8,
    0x0f4, 0x0f0, 0x0ec, 0x0e9, 0x0e5, 0x0e2, 0x0de, 0x0db,
    0x0d7, 0x0d4, 0x0d1, 0x0cd, 0x0ca, 0x0c7, 0x0c4, 0x0c1,
    0x0be, 0x0bb, 0x0b8, 0x0b5, 0x0b2, 0x0af, 0x0ac, 0x0a9,
    0x0a7, 0x0a4, 0x0a1, 0x09f, 0x09c, 0x099, 0x097, 0x094,
    0x092, 0x08f, 0x08d, 0x08a, 0x088, 0x086, 0x083, 0x081,
    0x07f, 0x07d, 0x07a, 0x078, 0x076, 0x074, 0x072, 0x070,
    0x06e, 0x06c, 0x06a, 0x068, 0x066, 0x064, 0x062, 0x060,
    0x05e, 0x05c, 0x05b, 0x059, 0x057, 0x055, 0x053, 0x052,
    0x050, 0x04e, 0x04d, 0x04b, 0x04a, 0x048, 0x046, 0x045,
    0x043, 0x042, 0x040, 0x03f, 0x03e, 0x03c, 0x03b, 0x039,
    0x038, 0x037, 0x035, 0x034, 0x033, 0x031, 0x030, 0x02f,
    0x02e, 0x02d, 0x02b, 0x02a, 0x029, 0x028, 0x027, 0x026,
    0x025, 0x024, 0x023, 0x022, 0x021, 0x020, 0x01f, 0x01e,
    0x01d, 0x01c, 0x01b, 0x01a, 0x019, 0x018, 0x017, 0x017,
    0x016, 0x015, 0x014, 0x014, 0x013, 0x012, 0x011, 0x011,
    0x010, 0x00f, 0x00f, 0x00e, 0x00d, 0x00d, 0x00c, 0x00c,
    0x00b, 0x00a, 0x00a, 0x009, 0x009, 0x008, 0x008, 0x007,
    0x007, 0x007, 0x006, 0x006, 0x005, 0x005, 0x005, 0x004,
    0x004, 0x004, 0x003, 0x003, 0x003, 0x002, 0x002, 0x002,
    0x002, 0x001, 0x001, 0x001, 0x001, 0x001, 0x001, 0x001,
    0x000, 0x000, 0x000, 0x000, 0x000, 0x000, 0x000, 0x000
};

// exp table
static const uint16_t exprom[256] = {
    0x7fa, 0x7f5, 0x7ef, 0x7ea, 0x7e4, 0x7df, 0x7da, 0x7d4,
    0x7cf, 0x7c9, 0x7c4, 0x7bf, 0x7b9, 0x7b4, 0x7ae, 0x7a9,
    0x7a4, 0x79f, 0x799, 0x794, 0x78f, 0x78a, 0x784, 0x77f,
    0x77a, 0x775, 0x770, 0x76a, 0x765, 0x760, 0x75b, 0x756,
    0x751, 0x74c, 0x747, 0x742, 0x73d, 0x738, 0x733, 0x72e,
    0x729, 0x724, 0x71f, 0x71a, 0x715, 0x710, 0x70b, 0x706,
    0x702, 0x6fd, 0x6f8, 0x6f3, 0x6ee, 0x6e9, 0x6e5, 0x6e0,
    0x6db, 0x6d6, 0x6d2, 0x6cd, 0x6c8, 0x6c4, 0x6bf, 0x6ba,
    0x6b5, 0x6b1, 0x6ac, 0x6a8, 0x6a3, 0x69e, 0x69a, 0x695,
    0x691, 0x68c, 0x688, 0x683, 0x67f, 0x67a, 0x676, 0x671,
    0x66d, 0x668, 0x664, 0x65f, 0x65b, 0x657, 0x652, 0x64e,
    0x649, 0x645, 0x641, 0x63c, 0x638, 0x634, 0x630, 0x62b,
    0x627, 0x623, 0x61e, 0x61a, 0x616, 0x612, 0x60e, 0x609,
    0x605, 0x601, 0x5fd, 0x5f9, 0x5f5, 0x5f0, 0x5ec, 0x5e8,
    0x5e4, 0x5e0, 0x5dc, 0x5d8, 0x5d4, 0x5d0, 0x5cc, 0x5c8,
    0x5c4, 0x5c0, 0x5bc, 0x5b8, 0x5b4, 0x5b0, 0x5ac, 0x5a8,
    0x5a4, 0x5a0, 0x59c, 0x599, 0x595, 0x591, 0x58d, 0x589,
    0x585, 0x581, 0x57e, 0x57a, 0x576, 0x572, 0x56f, 0x56b,
    0x567, 0x563, 0x560, 0x55c, 0x558, 0x554, 0x551, 0x54d,
    0x549, 0x546, 0x542, 0x53e, 0x53b, 0x537, 0x534, 0x530,
    0x52c, 0x529, 0x525, 0x522, 0x51e, 0x51b, 0x517, 0x514,
    0x510, 0x50c, 0x509, 0x506, 0x502, 0x4ff, 0x4fb, 0x4f8,
    0x4f4, 0x4f1, 0x4ed, 0x4ea, 0x4e7, 0x4e3, 0x4e0, 0x4dc,
    0x4d9, 0x4d6, 0x4d2, 0x4cf, 0x4cc, 0x4c8, 0x4c5, 0x4c2,
    0x4be, 0x4bb, 0x4b8, 0x4b5, 0x4b1, 0x4ae, 0x4ab, 0x4a8,
    0x4a4, 0x4a1, 0x49e, 0x49b, 0x498, 0x494, 0x491, 0x48e,
    0x48b, 0x488, 0x485, 0x482, 0x47e, 0x47b, 0x478, 0x475,
    0x472, 0x46f, 0x46c, 0x469, 0x466, 0x463, 0x460, 0x45d,
    0x45a, 0x457, 0x454, 0x451, 0x44e, 0x44b, 0x448, 0x445,
    0x442, 0x43f, 0x43c, 0x439, 0x436, 0x433, 0x430, 0x42d,
    0x42a, 0x428, 0x425, 0x422, 0x41f, 0x41c, 0x419, 0x416,
    0x414, 0x411, 0x40e, 0x40b, 0x408, 0x406, 0x403, 0x400
};

// freq mult table multiplied by 2
//
// 1/2, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 10, 12, 12, 15, 15
static const uint8_t mt[16] = {
    1, 2, 4, 6, 8, 10, 12, 14, 16, 18, 20, 20, 24, 24, 30, 30
};

// ksl table
static const uint8_t kslrom[16] = {
    0, 32, 40, 45, 48, 51, 53, 55, 56, 58, 59, 60, 61, 62, 63, 64
};

static const uint8_t kslshift[4] = {
    8, 1, 2, 0
};

// envelope generator constants
static const uint8_t eg_incstep[4][4] = {
    { 0, 0, 0, 0 },
    { 1, 0, 0, 0 },
    { 1, 0, 1, 0 },
    { 1, 1, 1, 0 }
};

// address decoding
static const int8_t ad_slot[0x20] = {
    0, 1, 2, 3, 4, 5, -1, -1, 6, 7, 8, 9, 10, 11, -1, -1,
    12, 13, 14, 15, 16, 17, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1
};

static const uint8_t ch_slot[18] = {
    0, 1, 2, 6, 7, 8, 12, 13, 14, 18, 19, 20, 24, 25, 26, 30, 31, 32
};

#if OPL_ENABLE_STEREOEXT
/*
    stereo extension panning table
*/

static int32_t panpot_lut[256];
static uint8_t panpot_lut_build = 0;
#endif

// Envelope generator
typedef int16_t (*envelope_sinfunc)(uint16_t phase, uint16_t envelope);
typedef void (*envelope_genfunc)(opl3_slot *slot);

static int16_t
OPL3_EnvelopeCalcExp(uint32_t level)
{
    if (level > 0x1fff)
        level = 0x1fff;

    return ((exprom[level & 0xffu] << 1) >> (level >> 8));
}

static int16_t
OPL3_EnvelopeCalcSin0(uint16_t phase, uint16_t envelope)
{
    uint16_t out = 0;
    uint16_t neg = 0;

    phase &= 0x3ff;

    if (phase & 0x0200)
        neg = 0xffff;

    if (phase & 0x0100)
        out = logsinrom[(phase & 0xffu) ^ 0xffu];
    else
        out = logsinrom[phase & 0xffu];

    return (OPL3_EnvelopeCalcExp(out + (envelope << 3)) ^ neg);
}

static int16_t
OPL3_EnvelopeCalcSin1(uint16_t phase, uint16_t envelope)
{
    uint16_t out = 0;

    phase &= 0x3ff;

    if (phase & 0x0200)
        out = 0x1000;
    else if (phase & 0x0100)
        out = logsinrom[(phase & 0xffu) ^ 0xffu];
    else
        out = logsinrom[phase & 0xffu];

    return (OPL3_EnvelopeCalcExp(out + (envelope << 3)));
}

static int16_t
OPL3_EnvelopeCalcSin2(uint16_t phase, uint16_t envelope)
{
    uint16_t out = 0;

    phase &= 0x03ff;

    if (phase & 0x0100)
        out = logsinrom[(phase & 0xffu) ^ 0xffu];
    else
        out = logsinrom[phase & 0xffu];

    return (OPL3_EnvelopeCalcExp(out + (envelope << 3)));
}

static int16_t
OPL3_EnvelopeCalcSin3(uint16_t phase, uint16_t envelope)
{
    uint16_t out = 0;

    phase &= 0x03ff;

    if (phase & 0x0100)
        out = 0x1000;
    else
        out = logsinrom[phase & 0xffu];

    return (OPL3_EnvelopeCalcExp(out + (envelope << 3)));
}

static int16_t
OPL3_EnvelopeCalcSin4(uint16_t phase, uint16_t envelope)
{
    uint16_t out = 0;
    uint16_t neg = 0;

    phase &= 0x03ff;

    if ((phase & 0x0300) == 0x0100)
        neg = 0xffff;

    if (phase & 0x0200)
        out = 0x1000;
    else if (phase & 0x80)
        out = logsinrom[((phase ^ 0xffu) << 1u) & 0xffu];
    else
        out = logsinrom[(phase << 1u) & 0xffu];

    return (OPL3_EnvelopeCalcExp(out + (envelope << 3)) ^ neg);
}

static int16_t
OPL3_EnvelopeCalcSin5(uint16_t phase, uint16_t envelope)
{
    uint16_t out = 0;

    phase &= 0x03ff;

    if (phase & 0x0200)
        out = 0x1000;
    else if (phase & 0x80)
        out = logsinrom[((phase ^ 0xffu) << 1u) & 0xffu];
    else
        out = logsinrom[(phase << 1u) & 0xffu];

    return (OPL3_EnvelopeCalcExp(out + (envelope << 3)));
}

static int16_t
OPL3_EnvelopeCalcSin6(uint16_t phase, uint16_t envelope)
{
    uint16_t neg = 0;

    phase &= 0x03ff;

    if (phase & 0x0200)
        neg = 0xffff;

    return (OPL3_EnvelopeCalcExp(envelope << 3) ^ neg);
}

static int16_t
OPL3_EnvelopeCalcSin7(uint16_t phase, uint16_t envelope)
{
    uint16_t out = 0;
    uint16_t neg = 0;

    phase &= 0x03ff;

    if (phase & 0x0200) {
        neg   = 0xffff;
        phase = (phase & 0x01ff) ^ 0x01ff;
    }

    out = phase << 3;

    return (OPL3_EnvelopeCalcExp(out + (envelope << 3)) ^ neg);
}

static const envelope_sinfunc envelope_sin[8] = {
    OPL3_EnvelopeCalcSin0,
    OPL3_EnvelopeCalcSin1,
    OPL3_EnvelopeCalcSin2,
    OPL3_EnvelopeCalcSin3,
    OPL3_EnvelopeCalcSin4,
    OPL3_EnvelopeCalcSin5,
    OPL3_EnvelopeCalcSin6,
    OPL3_EnvelopeCalcSin7
};

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

    if (ksl < 0)
        ksl = 0;

    slot->eg_ksl = (uint8_t) ksl;
}

static void
OPL3_EnvelopeCalc(opl3_slot *slot)
{
    uint8_t  nonzero;
    uint8_t  rate;
    uint8_t  rate_hi;
    uint8_t  rate_lo;
    uint8_t  reg_rate = 0;
    uint8_t  ks;
    uint8_t  eg_shift;
    uint8_t  shift;
    uint16_t eg_rout;
    int16_t  eg_inc;
    uint8_t  eg_off;
    uint8_t  reset = 0;

    slot->eg_out = slot->eg_rout + (slot->reg_tl << 2)
                 + (slot->eg_ksl >> kslshift[slot->reg_ksl]) + *slot->trem;
    if (slot->key && slot->eg_gen == envelope_gen_num_release) {
        reset    = 1;
        reg_rate = slot->reg_ar;
    } else
        switch (slot->eg_gen) {
            case envelope_gen_num_attack:
                reg_rate = slot->reg_ar;
                break;

            case envelope_gen_num_decay:
                reg_rate = slot->reg_dr;
                break;

            case envelope_gen_num_sustain:
                if (!slot->reg_type)
                    reg_rate = slot->reg_rr;
                break;

            case envelope_gen_num_release:
                reg_rate = slot->reg_rr;
                break;

            default:
                break;
        }

    slot->pg_reset = reset;
    ks             = slot->channel->ksv >> ((slot->reg_ksr ^ 1) << 1);
    nonzero        = (reg_rate != 0);
    rate           = ks + (reg_rate << 2);
    rate_hi        = rate >> 2;
    rate_lo        = rate & 0x03;

    if (rate_hi & 0x10)
        rate_hi = 0x0f;

    eg_shift = rate_hi + slot->chip->eg_add;
    shift    = 0;

    if (nonzero) {
        if (rate_hi < 12) {
            if (slot->chip->eg_state)
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
        } else {
            shift = (rate_hi & 0x03) + eg_incstep[rate_lo][slot->chip->eg_timer_lo];
            if (shift & 0x04)
                shift = 0x03;
            if (!shift)
                shift = slot->chip->eg_state;
        }
    }

    eg_rout = slot->eg_rout;
    eg_inc  = 0;
    eg_off  = 0;

    // Instant attack
    if (reset && rate_hi == 0x0f)
        eg_rout = 0x00;

    // Envelope off
    if ((slot->eg_rout & 0x1f8) == 0x1f8)
        eg_off = 1;

    if (slot->eg_gen != envelope_gen_num_attack && !reset && eg_off)
        eg_rout = 0x1ff;

    switch (slot->eg_gen) {
        case envelope_gen_num_attack:
            if (!slot->eg_rout)
                slot->eg_gen = envelope_gen_num_decay;
            else if (slot->key && shift > 0 && rate_hi != 0x0f)
                eg_inc = ~slot->eg_rout >> (4 - shift);
            break;

        case envelope_gen_num_decay:
            if ((slot->eg_rout >> 4) == slot->reg_sl)
                slot->eg_gen = envelope_gen_num_sustain;
            else if (!eg_off && !reset && shift > 0)
                eg_inc = 1 << (shift - 1);
            break;

        case envelope_gen_num_sustain:
        case envelope_gen_num_release:
            if (!eg_off && !reset && shift > 0)
                eg_inc = 1 << (shift - 1);
            break;

        default:
            break;
    }
    slot->eg_rout = (eg_rout + eg_inc) & 0x1ff;

    // Key off
    if (reset)
        slot->eg_gen = envelope_gen_num_attack;

    if (!slot->key)
        slot->eg_gen = envelope_gen_num_release;
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

// Phase Generator
static void
OPL3_PhaseGenerate(opl3_slot *slot)
{
    opl3_chip *chip;
    uint16_t   f_num;
    uint32_t   basefreq;
    uint8_t    rm_xor;
    uint8_t    n_bit;
    uint32_t   noise;
    uint16_t   phase;

    chip  = slot->chip;
    f_num = slot->channel->f_num;
    if (slot->reg_vib) {
        int8_t  range;
        uint8_t vibpos;

        range  = (f_num >> 7) & 7;
        vibpos = chip->vibpos;

        if (!(vibpos & 3))
            range = 0;
        else if (vibpos & 1)
            range >>= 1;
        range >>= chip->vibshift;

        if (vibpos & 4)
            range = -range;
        f_num += range;
    }

    basefreq = (f_num << slot->channel->block) >> 1;
    phase    = (uint16_t) (slot->pg_phase >> 9);

    if (slot->pg_reset)
        slot->pg_phase = 0;
    slot->pg_phase += (basefreq * mt[slot->reg_mult]) >> 1;

    // Rhythm mode
    noise              = chip->noise;
    slot->pg_phase_out = phase;
    if (slot->slot_num == 13) { // hh
        chip->rm_hh_bit2 = (phase >> 2) & 1;
        chip->rm_hh_bit3 = (phase >> 3) & 1;
        chip->rm_hh_bit7 = (phase >> 7) & 1;
        chip->rm_hh_bit8 = (phase >> 8) & 1;
    }
    if (slot->slot_num == 17 && (chip->rhy & 0x20)) { // tc
        chip->rm_tc_bit3 = (phase >> 3) & 1;
        chip->rm_tc_bit5 = (phase >> 5) & 1;
    }
    if (chip->rhy & 0x20) {
        rm_xor = (chip->rm_hh_bit2 ^ chip->rm_hh_bit7)
                 | (chip->rm_hh_bit3 ^ chip->rm_tc_bit5)
                 | (chip->rm_tc_bit3 ^ chip->rm_tc_bit5);

        switch (slot->slot_num) {
            case 13: // hh
                slot->pg_phase_out = rm_xor << 9;
                if (rm_xor ^ (noise & 1))
                    slot->pg_phase_out |= 0xd0;
                else
                    slot->pg_phase_out |= 0x34;
                break;

            case 16: // sd
                slot->pg_phase_out = (chip->rm_hh_bit8 << 9)
                                     | ((chip->rm_hh_bit8 ^ (noise & 1)) << 8);
                break;

            case 17: // tc
                slot->pg_phase_out = (rm_xor << 9) | 0x80;
                break;

            default:
                break;
        }
    }

    n_bit = ((noise >> 14) ^ noise) & 0x01;

    chip->noise = (noise >> 1) | (n_bit << 22);
}

// Slot
static void
OPL3_SlotWrite20(opl3_slot *slot, uint8_t data)
{
    if ((data >> 7) & 0x01)
        slot->trem = &slot->chip->tremolo;
    else
        slot->trem = (uint8_t *) &slot->chip->zeromod;

    slot->reg_vib  = (data >> 6) & 0x01;
    slot->reg_type = (data >> 5) & 0x01;
    slot->reg_ksr  = (data >> 4) & 0x01;
    slot->reg_mult = data & 0x0f;
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
    slot->reg_ar = (data >> 4) & 0x0f;
    slot->reg_dr = data & 0x0f;
}

static void
OPL3_SlotWrite80(opl3_slot *slot, uint8_t data)
{
    slot->reg_sl = (data >> 4) & 0x0f;

    if (slot->reg_sl == 0x0f)
        slot->reg_sl = 0x1f;

    slot->reg_rr = data & 0x0f;
}

static void
OPL3_SlotWriteE0(opl3_slot *slot, uint8_t data)
{
    slot->reg_wf = data & 0x07;

    if (slot->chip->newm == 0x00)
        slot->reg_wf &= 0x03;
}

static void
OPL3_SlotGenerate(opl3_slot *slot)
{
    slot->out = envelope_sin[slot->reg_wf](slot->pg_phase_out + *slot->mod, slot->eg_out);
}

static void
OPL3_SlotCalcFB(opl3_slot *slot)
{
    if (slot->channel->fb != 0x00)
        slot->fbmod = (slot->prout + slot->out) >> (0x09 - slot->channel->fb);
    else
        slot->fbmod = 0;

    slot->prout = slot->out;
}

// Channel
static void
OPL3_ChannelSetupAlg(opl3_channel *channel);

static void
OPL3_ChannelUpdateRhythm(opl3_chip *chip, uint8_t data)
{
    opl3_channel *channel6;
    opl3_channel *channel7;
    opl3_channel *channel8;
    uint8_t chnum;

    chip->rhy = data & 0x3f;
    if (chip->rhy & 0x20) {
        channel6         = &chip->channel[6];
        channel7         = &chip->channel[7];
        channel8         = &chip->channel[8];
        channel6->out[0] = &channel6->slotz[1]->out;
        channel6->out[1] = &channel6->slotz[1]->out;
        channel6->out[2] = &chip->zeromod;
        channel6->out[3] = &chip->zeromod;
        channel7->out[0] = &channel7->slotz[0]->out;
        channel7->out[1] = &channel7->slotz[0]->out;
        channel7->out[2] = &channel7->slotz[1]->out;
        channel7->out[3] = &channel7->slotz[1]->out;
        channel8->out[0] = &channel8->slotz[0]->out;
        channel8->out[1] = &channel8->slotz[0]->out;
        channel8->out[2] = &channel8->slotz[1]->out;
        channel8->out[3] = &channel8->slotz[1]->out;

        for (chnum = 6; chnum < 9; chnum++)
            chip->channel[chnum].chtype = ch_drum;

        OPL3_ChannelSetupAlg(channel6);
        OPL3_ChannelSetupAlg(channel7);
        OPL3_ChannelSetupAlg(channel8);

        // hh
        if (chip->rhy & 0x01)
            OPL3_EnvelopeKeyOn(channel7->slotz[0], egk_drum);
        else
            OPL3_EnvelopeKeyOff(channel7->slotz[0], egk_drum);

        // tc
        if (chip->rhy & 0x02)
            OPL3_EnvelopeKeyOn(channel8->slotz[1], egk_drum);
        else
            OPL3_EnvelopeKeyOff(channel8->slotz[1], egk_drum);

        // tom
        if (chip->rhy & 0x04)
            OPL3_EnvelopeKeyOn(channel8->slotz[0], egk_drum);
        else
            OPL3_EnvelopeKeyOff(channel8->slotz[0], egk_drum);

        // sd
        if (chip->rhy & 0x08)
            OPL3_EnvelopeKeyOn(channel7->slotz[1], egk_drum);
        else
            OPL3_EnvelopeKeyOff(channel7->slotz[1], egk_drum);

        // bd
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
    if (channel->chip->newm && channel->chtype == ch_4op2)
        return;

    channel->f_num = (channel->f_num & 0x300) | data;
    channel->ksv   = (channel->block << 1)
                      | ((channel->f_num >> (0x09 - channel->chip->nts)) & 0x01);

    OPL3_EnvelopeUpdateKSL(channel->slotz[0]);
    OPL3_EnvelopeUpdateKSL(channel->slotz[1]);

    if (channel->chip->newm && channel->chtype == ch_4op) {
        channel->pair->f_num = channel->f_num;
        channel->pair->ksv   = channel->ksv;

        OPL3_EnvelopeUpdateKSL(channel->pair->slotz[0]);
        OPL3_EnvelopeUpdateKSL(channel->pair->slotz[1]);
    }
}

static void
OPL3_ChannelWriteB0(opl3_channel *channel, uint8_t data)
{
    if (channel->chip->newm && channel->chtype == ch_4op2)
        return;

    channel->f_num = (channel->f_num & 0xff) | ((data & 0x03) << 8);
    channel->block = (data >> 2) & 0x07;
    channel->ksv   = (channel->block << 1)
                      | ((channel->f_num >> (0x09 - channel->chip->nts)) & 0x01);

    OPL3_EnvelopeUpdateKSL(channel->slotz[0]);
    OPL3_EnvelopeUpdateKSL(channel->slotz[1]);

    if (channel->chip->newm && channel->chtype == ch_4op) {
        channel->pair->f_num = channel->f_num;
        channel->pair->block = channel->block;
        channel->pair->ksv   = channel->ksv;

        OPL3_EnvelopeUpdateKSL(channel->pair->slotz[0]);
        OPL3_EnvelopeUpdateKSL(channel->pair->slotz[1]);
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

            default:
                break;
        }
        return;
    }

    if (channel->alg & 0x08)
        return;

    if (channel->alg & 0x04) {
        channel->pair->out[0] = &channel->chip->zeromod;
        channel->pair->out[1] = &channel->chip->zeromod;
        channel->pair->out[2] = &channel->chip->zeromod;
        channel->pair->out[3] = &channel->chip->zeromod;

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
                break;

            default:
                break;
        }
    } else
        switch (channel->alg & 0x01) {
            case 0x00:
                channel->slotz[0]->mod = &channel->slotz[0]->fbmod;
                channel->slotz[1]->mod = &channel->slotz[0]->out;
                channel->out[0]        = &channel->slotz[1]->out;
                channel->out[1]        = &channel->chip->zeromod;
                channel->out[2]        = &channel->chip->zeromod;
                channel->out[3]        = &channel->chip->zeromod;
                break;

            case 0x01:
                channel->slotz[0]->mod = &channel->slotz[0]->fbmod;
                channel->slotz[1]->mod = &channel->chip->zeromod;
                channel->out[0]        = &channel->slotz[0]->out;
                channel->out[1]        = &channel->slotz[1]->out;
                channel->out[2]        = &channel->chip->zeromod;
                channel->out[3]        = &channel->chip->zeromod;
                break;

            default:
                break;
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
        } else
            OPL3_ChannelSetupAlg(channel);
    } else
        OPL3_ChannelSetupAlg(channel);
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

#if OPL_ENABLE_STEREOEXT
    if (!channel->chip->stereoext) {
        channel->leftpan  = channel->cha << 16;
        channel->rightpan = channel->chb << 16;
    }
#endif
}

#if OPL_ENABLE_STEREOEXT
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
    uint8_t chnum;

    for (uint8_t bit = 0; bit < 6; bit++) {
        chnum = bit;

        if (bit >= 3)
            chnum += 9 - 3;

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
    uint8_t        i;
    int16_t        accm;
    uint8_t        shift = 0;

    buf4[1] = chip->mixbuff[1];
    buf4[3] = chip->mixbuff[3];

#if OPL_QUIRK_CHANNELSAMPLEDELAY
    for (i = 0; i < 15; i++)
#else
    for (i = 0; i < 36; i++)
#endif
        OPL3_ProcessSlot(&chip->slot[i]);

    mix[0] = mix[1] = 0;

    for (i = 0; i < 18; i++) {
        channel = &chip->channel[i];
        out     = channel->out;
        accm    = *out[0] + *out[1] + *out[2] + *out[3];
#if OPL_ENABLE_STEREOEXT
        mix[0] += (int16_t) ((accm * channel->leftpan) >> 16);
#else
        mix[0] += (int16_t) (accm & channel->cha);
#endif
        mix[1] += (int16_t) (accm & channel->chc);
    }

    chip->mixbuff[0] = mix[0];
    chip->mixbuff[2] = mix[1];

#if OPL_QUIRK_CHANNELSAMPLEDELAY
    for (i = 15; i < 18; i++)
        OPL3_ProcessSlot(&chip->slot[i]);
#endif

    buf4[0] = chip->mixbuff[0];
    buf4[2] = chip->mixbuff[2];

#if OPL_QUIRK_CHANNELSAMPLEDELAY
    for (i = 18; i < 33; i++)
        OPL3_ProcessSlot(&chip->slot[i]);
#endif

    mix[0] = mix[1] = 0;

    for (i = 0; i < 18; i++) {
        channel = &chip->channel[i];
        out     = channel->out;
        accm    = *out[0] + *out[1] + *out[2] + *out[3];
#if OPL_ENABLE_STEREOEXT
        mix[0] += (int16_t) ((accm * channel->rightpan) >> 16);
#else
        mix[0] += (int16_t) (accm & channel->chb);
#endif
        mix[1] += (int16_t) (accm & channel->chd);
    }

    chip->mixbuff[1] = mix[0];
    chip->mixbuff[3] = mix[1];

#if OPL_QUIRK_CHANNELSAMPLEDELAY
    for (i = 33; i < 36; i++)
        OPL3_ProcessSlot(&chip->slot[i]);
#endif

    if ((chip->timer & 0x3f) == 0x3f)
        chip->tremolopos = (chip->tremolopos + 1) % 210;

    if (chip->tremolopos < 105)
        chip->tremolo = chip->tremolopos >> chip->tremoloshift;
    else
        chip->tremolo = (210 - chip->tremolopos) >> chip->tremoloshift;

    if ((chip->timer & 0x03ff) == 0x03ff)
        chip->vibpos = (chip->vibpos + 1) & 7;

    chip->timer++;

    if (chip->eg_state) {
        while (shift < 13 && ((chip->eg_timer >> shift) & 1) == 0)
            shift++;

        if (shift > 12)
            chip->eg_add = 0;
        else
            chip->eg_add = shift + 1;

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
        if (!(writebuf->reg & 0x200))
            break;

        writebuf->reg &= 0x01ff;

        OPL3_WriteReg(chip, writebuf->reg, writebuf->data);

        chip->writebuf_cur = (chip->writebuf_cur + 1) % OPL_WRITEBUF_SIZE;
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
                          + chip->samples[0] * chip->samplecnt) / chip->rateratio);
    buf4[1] = (int32_t) ((chip->oldsamples[1] * (chip->rateratio - chip->samplecnt)
                          + chip->samples[1] * chip->samplecnt) / chip->rateratio);
    buf4[2] = (int32_t) ((chip->oldsamples[2] * (chip->rateratio - chip->samplecnt)
                          + chip->samples[2] * chip->samplecnt) / chip->rateratio);
    buf4[3] = (int32_t) ((chip->oldsamples[3] * (chip->rateratio - chip->samplecnt)
                          + chip->samples[3] * chip->samplecnt) / chip->rateratio);

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
        slot             = &chip->slot[slotnum];
        slot->chip       = chip;
        slot->mod        = &chip->zeromod;
        slot->eg_rout    = 0x01ff;
        slot->eg_out     = 0x01ff;
        slot->eg_gen     = envelope_gen_num_release;
        slot->trem       = (uint8_t *) &chip->zeromod;
        slot->slot_num   = slotnum;
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
#if OPL_ENABLE_STEREOEXT
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

#if OPL_ENABLE_STEREOEXT
    if (!panpot_lut_build) {
        for (int32_t i = 0; i < 256; i++)
            panpot_lut[i] = OPL_SIN(i);
        panpot_lut_build = 1;
    }
#endif
}

uint16_t
nuked_write_addr(void *priv, uint16_t port, uint8_t val)
{
    const opl3_chip *chip = (opl3_chip *) priv;
    uint16_t addr;

    addr = val;
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
            if (high)
                switch (regm & 0x0f) {
                    case 0x04:
                        OPL3_ChannelSet4Op(chip, val);
                        break;

                    case 0x05:
                        chip->newm = val & 0x01;
#if OPL_ENABLE_STEREOEXT
                        chip->stereoext = (val >> 1) & 0x01;
#endif
                        break;

                    default:
                        break;
                }
            else
                switch (regm & 0x0f) {
                    case 0x08:
                        chip->nts = (val >> 6) & 0x01;
                        break;

                    default:
                        break;
                }
            break;

        case 0x20:
        case 0x30:
            if (ad_slot[regm & 0x1fu] >= 0)
                OPL3_SlotWrite20(&chip->slot[18u * high + ad_slot[regm & 0x1fu]], val);
            break;

        case 0x40:
        case 0x50:
            if (ad_slot[regm & 0x1fu] >= 0)
                OPL3_SlotWrite40(&chip->slot[18u * high + ad_slot[regm & 0x1fu]], val);
            break;

        case 0x60:
        case 0x70:
            if (ad_slot[regm & 0x1fu] >= 0)
                OPL3_SlotWrite60(&chip->slot[18u * high + ad_slot[regm & 0x1fu]], val);
            break;

        case 0x80:
        case 0x90:
            if (ad_slot[regm & 0x1fu] >= 0)
                OPL3_SlotWrite80(&chip->slot[18u * high + ad_slot[regm & 0x1fu]], val);
            break;

        case 0xe0:
        case 0xf0:
            if (ad_slot[regm & 0x1fu] >= 0)
                OPL3_SlotWriteE0(&chip->slot[18u * high + ad_slot[regm & 0x1fu]], val);
            break;

        case 0xa0:
            if ((regm & 0x0f) < 9)
                OPL3_ChannelWriteA0(&chip->channel[9u * high + (regm & 0x0fu)], val);
            break;

        case 0xb0:
            if (regm == 0xbd && !high) {
                chip->tremoloshift = (((val >> 7) ^ 1) << 1) + 2;
                chip->vibshift     = ((val >> 6) & 0x01) ^ 1;
                OPL3_ChannelUpdateRhythm(chip, val);
            } else if ((regm & 0x0f) < 9) {
                OPL3_ChannelWriteB0(&chip->channel[9u * high + (regm & 0x0fu)], val);

                if (val & 0x20)
                    OPL3_ChannelKeyOn(&chip->channel[9u * high + (regm & 0x0fu)]);
                else
                    OPL3_ChannelKeyOff(&chip->channel[9u * high + (regm & 0x0fu)]);
            }
            break;

        case 0xc0:
            if ((regm & 0x0f) < 9)
                OPL3_ChannelWriteC0(&chip->channel[9u * high + (regm & 0x0fu)], val);
            break;

#if OPL_ENABLE_STEREOEXT
        case 0xd0:
            if ((regm & 0x0f) < 9)
                OPL3_ChannelWriteD0(&chip->channel[9u * high + (regm & 0x0fu)], val);
            break;
#endif

        default:
            break;
    }
}

void
OPL3_WriteRegBuffered(void *priv, uint16_t reg, uint8_t val)
{
    opl3_chip     *chip = (opl3_chip *) priv;
    uint64_t       time1;
    uint64_t       time2;
    opl3_writebuf *writebuf;
    uint32_t       writebuf_last;

    writebuf_last = chip->writebuf_last;
    writebuf      = &chip->writebuf[writebuf_last];

    if (writebuf->reg & 0x0200) {
        OPL3_WriteReg(chip, writebuf->reg & 0x01ff, writebuf->data);

        chip->writebuf_cur       = (writebuf_last + 1) % OPL_WRITEBUF_SIZE;
        chip->writebuf_samplecnt = writebuf->time;
    }

    writebuf->reg  = reg | 0x0200;
    writebuf->data = val;
    time1          = chip->writebuf_lasttime + OPL_WRITEBUF_DELAY;
    time2          = chip->writebuf_samplecnt;

    if (time1 < time2)
        time1 = time2;

    writebuf->time          = time1;
    chip->writebuf_lasttime = time1;
    chip->writebuf_last     = (writebuf_last + 1) % OPL_WRITEBUF_SIZE;
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

static void
nuked_timer_tick(nuked_drv_t *dev, int tmr)
{
    dev->timer_cur_count[tmr] = (dev->timer_cur_count[tmr] + 1) & 0xff;

    nuked_log("Ticking timer %i, count now %02X...\n", tmr, dev->timer_cur_count[tmr]);

    if (dev->timer_cur_count[tmr] == 0x00) {
        dev->status |= ((STAT_TMR1_OVER >> tmr) & ~dev->timer_ctrl);
        dev->timer_cur_count[tmr] = dev->timer_count[tmr];

        nuked_log("Count wrapped around to zero, reloading timer %i (%02X), status = %02X...\n", tmr, (STAT_TMR1_OVER >> tmr), dev->status);
    }

    timer_on_auto(&dev->timers[tmr], (tmr == 1) ? 320.0 : 80.0);
}

static void
nuked_timer_control(nuked_drv_t *dev, int tmr, int start)
{
    timer_on_auto(&dev->timers[tmr], 0.0);

    if (start) {
        nuked_log("Loading timer %i count: %02X = %02X\n", tmr, dev->timer_cur_count[tmr], dev->timer_count[tmr]);
        dev->timer_cur_count[tmr] = dev->timer_count[tmr];
        if (dev->flags & FLAG_OPL3)
            nuked_timer_tick(dev, tmr); /* Per the YMF 262 datasheet, OPL3 starts counting immediately, unlike OPL2. */
        else
            timer_on_auto(&dev->timers[tmr], (tmr == 1) ? 320.0 : 80.0);
    } else {
        nuked_log("Timer %i stopped\n", tmr);
        if (tmr == 1) {
            dev->status &= ~STAT_TMR2_OVER;
        } else
            dev->status &= ~STAT_TMR1_OVER;
    }
}

static void
nuked_timer_1(void *priv)
{
    nuked_drv_t *dev = (nuked_drv_t *) priv;

    nuked_timer_tick(dev, 0);
}

static void
nuked_timer_2(void *priv)
{
    nuked_drv_t *dev = (nuked_drv_t *) priv;

    nuked_timer_tick(dev, 1);
}

static void
nuked_drv_set_do_cycles(void *priv, int8_t do_cycles)
{
    nuked_drv_t *dev = (nuked_drv_t *) priv;

    if (do_cycles)
        dev->flags |= FLAG_CYCLES;
    else
        dev->flags &= ~FLAG_CYCLES;
}

static void *
nuked_drv_init(const device_t *info)
{
    nuked_drv_t *dev = (nuked_drv_t *) calloc(1, sizeof(nuked_drv_t));
    dev->flags       = FLAG_CYCLES;
    if (info->local == FM_YMF262)
        dev->flags |= FLAG_OPL3;
    else
        dev->status = 0x06;

    /* Initialize the NukedOPL object. */
    OPL3_Reset(&dev->opl, FREQ_49716);

    timer_add(&dev->timers[0], nuked_timer_1, dev, 0);
    timer_add(&dev->timers[1], nuked_timer_2, dev, 0);

    return dev;
}

static void
nuked_drv_close(void *priv)
{
    nuked_drv_t *dev = (nuked_drv_t *) priv;
    free(dev);
}

static int32_t *
nuked_drv_update(void *priv)
{
    nuked_drv_t *dev = (nuked_drv_t *) priv;

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

static uint8_t
nuked_drv_read(uint16_t port, void *priv)
{
    nuked_drv_t *dev = (nuked_drv_t *) priv;

    if (dev->flags & FLAG_CYCLES)
        cycles -= ((int) (isa_timing * 8));

    nuked_drv_update(dev);

    uint8_t ret = 0xff;

    if ((port & 0x0003) == 0x0000) {
        ret = dev->status;
        if (dev->status & STAT_TMR_OVER)
            ret |= STAT_TMR_ANY;
    }

    nuked_log("OPL statret = %02x, status = %02x\n", ret, dev->status);

    return ret;
}

static void
nuked_drv_write(uint16_t port, uint8_t val, void *priv)
{
    nuked_drv_t *dev = (nuked_drv_t *) priv;
    nuked_drv_update(dev);

    if ((port & 0x0001) == 0x0001) {
        OPL3_WriteRegBuffered(&dev->opl, dev->port, val);

        switch (dev->port) {
            case 0x002: /* Timer 1 */
                dev->timer_count[0] = val;
                nuked_log("Timer 0 count now: %i\n", dev->timer_count[0]);
                break;

            case 0x003: /* Timer 2 */
                dev->timer_count[1] = val;
                nuked_log("Timer 1 count now: %i\n", dev->timer_count[1]);
                break;

            case 0x004: /* Timer control */
                if (val & CTRL_RESET) {
                    nuked_log("Resetting timer status...\n");
                    dev->status &= ~STAT_TMR_OVER;
                } else {
                    dev->timer_ctrl = val;
                    nuked_timer_control(dev, 0, val & CTRL_TMR1_START);
                    nuked_timer_control(dev, 1, val & CTRL_TMR2_START);
                    nuked_log("Status mask now %02X (val = %02X)\n", (val & ~CTRL_TMR_MASK) & CTRL_TMR_MASK, val);
                }
                break;

            case 0x105:
                dev->opl.newm = val & 0x01;
                break;

            default:
                break;
        }
    } else {
        dev->port = nuked_write_addr(&dev->opl, port, val) & 0x01ff;

        if (!(dev->flags & FLAG_OPL3))
            dev->port &= 0x00ff;
    }
}

static void
nuked_drv_reset_buffer(void *priv)
{
    nuked_drv_t *dev = (nuked_drv_t *) priv;

    dev->pos = 0;
}

const device_t ym3812_nuked_device = {
    .name          = "Yamaha YM3812 OPL2 (NUKED)",
    .internal_name = "ym3812_nuked",
    .flags         = 0,
    .local         = FM_YM3812,
    .init          = nuked_drv_init,
    .close         = nuked_drv_close,
    .reset         = NULL,
    .available     = NULL ,
    .speed_changed = NULL,
    .force_redraw  = NULL,
    .config        = NULL
};

const device_t ymf262_nuked_device = {
    .name          = "Yamaha YMF262 OPL3 (NUKED)",
    .internal_name = "ymf262_nuked",
    .flags         = 0,
    .local         = FM_YMF262,
    .init          = nuked_drv_init,
    .close         = nuked_drv_close,
    .reset         = NULL,
    .available     = NULL,
    .speed_changed = NULL,
    .force_redraw  = NULL,
    .config        = NULL
};

const fm_drv_t nuked_opl_drv = {
    .read          = &nuked_drv_read,
    .write         = &nuked_drv_write,
    .update        = &nuked_drv_update,
    .reset_buffer  = &nuked_drv_reset_buffer,
    .set_do_cycles = &nuked_drv_set_do_cycles,
    .priv          = NULL,
    .generate      = NULL,
};
