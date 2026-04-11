/* Nuked OPL2 Lite
 * Copyright (C) 2026 nukeykt
 *
 * This file is part of Nuked OPL2 Lite.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 *  Nuked OPL2 Lite
 *  Thanks:
 *      MAME Development Team(Jarek Burczynski, Tatsuyuki Satoh):
 *          Feedback and Rhythm part calculation information.
 *      forums.submarine.org.uk(carbon14, opl3):
 *          Tremolo and phase generator calculation information.
 *      OPLx decapsulated(Matthew Gambrell, Olli Niemitalo):
 *          OPL2 ROMs.
 *      siliconpr0n.org(John McMaster, digshadow):
 *          YMF262 and VRC VII decaps and die shots.
 *      Travis Goodspeed:
 *          YM3812 decap and die shot
 *
 * version: 0.9 beta
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
#include <86box/snd_opl2_nuked.h>

#define RSM_FRAC    10

// Channel types
enum {
    ch_normal = 0,
    ch_drum   = 1
};

// Envelope key types
enum {
    egk_norm = 0x01,
    egk_drum = 0x02
};

#ifdef ENABLE_NUKED_OPL2_LOG
int nuked_opl2_do_log = ENABLE_NUKED_OPL2_LOG;

static void
nuked_opl2_log(const char *fmt, ...)
{
    va_list ap;

    if (nuked_opl2_do_log) {
        va_start(ap, fmt);
        pclog_ex(fmt, ap);
        va_end(ap);
    }
}
#else
#    define nuked_opl2_log(fmt, ...)
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

static const int8_t ad_ch[0x10] = {
    0, 1, 2, 3, 4, 5, 6, 7, 8, 0, 1, 2, -1, -1, 0, 1
};

static const int8_t ad_ch2[0x10] = {
    0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, -1, -1, 0, 1
};

static const uint8_t ch_slot[9] = {
    0, 1, 2, 6, 7, 8, 12, 13, 14
};

// Envelope generator
typedef int16_t (*envelope_sinfunc)(uint16_t phase, uint16_t envelope);
typedef void (*envelope_genfunc)(opl2_slot *slott);

static int16_t
OPL2_EnvelopeCalcExp(uint32_t level)
{
    if (level > 0x1fff)
        level = 0x1fff;

    return ((exprom[level & 0xffu] << 1) >> (level >> 8));
}

static int16_t
OPL2_EnvelopeCalcSin0(uint16_t phase, uint16_t envelope)
{
    uint16_t out = 0;
    uint16_t neg = 0;

    if (phase & 0x200)
        neg = 0xffff;

    if (phase & 0x100)
        out = logsinrom[(phase & 0xffu) ^ 0xffu];
    else
        out = logsinrom[phase & 0xffu];

    return (OPL2_EnvelopeCalcExp(out + (envelope << 3)) ^ neg);
}

static int16_t
OPL2_EnvelopeCalcSin1(uint16_t phase, uint16_t envelope)
{
    uint16_t out = 0;

    if (phase & 0x200)
        out = 0x1000;
    else if (phase & 0x100)
        out = logsinrom[(phase & 0xffu) ^ 0xffu];
    else
        out = logsinrom[phase & 0xffu];

    return (OPL2_EnvelopeCalcExp(out + (envelope << 3)));
}

static int16_t
OPL2_EnvelopeCalcSin2(uint16_t phase, uint16_t envelope)
{
    uint16_t out = 0;

    if (phase & 0x100)
        out = logsinrom[(phase & 0xffu) ^ 0xffu];
    else
        out = logsinrom[phase & 0xffu];

    return (OPL2_EnvelopeCalcExp(out + (envelope << 3)));
}

static int16_t
OPL2_EnvelopeCalcSin3(uint16_t phase, uint16_t envelope)
{
    uint16_t out = 0;

    if (phase & 0x100)
        out = 0x1000;
    else
        out = logsinrom[phase & 0xffu];

    return (OPL2_EnvelopeCalcExp(out + (envelope << 3)));
}

static const envelope_sinfunc envelope_sin[4] = {
    OPL2_EnvelopeCalcSin0,
    OPL2_EnvelopeCalcSin1,
    OPL2_EnvelopeCalcSin2,
    OPL2_EnvelopeCalcSin3,
};

enum envelope_gen_num {
    envelope_gen_num_attack  = 0,
    envelope_gen_num_decay   = 1,
    envelope_gen_num_sustain = 2,
    envelope_gen_num_release = 3
};

static void
OPL2_EnvelopeUpdateKSL(opl2_slot *slot)
{
    int16_t ksl = (kslrom[slot->channel->f_num >> 6u] << 2)
                  - ((0x08 - slot->channel->block) << 5);

    if (ksl < 0)
        ksl = 0;

    slot->eg_ksl = (uint8_t) ksl;
}

static void
OPL2_EnvelopeCalc(opl2_slot *slot)
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
    uint8_t  key;

    slot->eg_out = slot->eg_rout + (slot->reg_tl << 2)
                 + (slot->eg_ksl >> kslshift[slot->reg_ksl]) + *slot->trem;
    key = slot->key | slot->chip->csm_kon;

    if (key && slot->eg_gen == envelope_gen_num_release) {
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

    slot->eg_mute = slot->eg_gen != envelope_gen_num_attack && !reset && eg_off;

    if (slot->eg_mute)
        eg_rout = 0x1ff;

    switch (slot->eg_gen) {
        case envelope_gen_num_attack:
            if (!slot->eg_rout)
                slot->eg_gen = envelope_gen_num_decay;
            else if (key && shift > 0 && rate_hi != 0x0f)
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

    if (!key)
        slot->eg_gen = envelope_gen_num_release;
}

static void
OPL2_EnvelopeKeyOn(opl2_slot *slot, uint8_t type)
{
    slot->key |= type;
}

static void
OPL2_EnvelopeKeyOff(opl2_slot *slot, uint8_t type)
{
    slot->key &= ~type;
}

// Phase Generator
static void
OPL2_PhaseGenerate(opl2_slot *slot)
{
    opl2_chip *chip;
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
                slot->pg_phase_out = (rm_xor << 9) | 0x100;
                break;

            default:
                break;
        }
    }

    n_bit       = ((noise >> 14) ^ noise) & 0x01;
    chip->noise = (noise >> 1) | (n_bit << 22);
}

// Slot
static void
OPL2_SlotWrite20(opl2_slot *slot, uint8_t data)
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
OPL2_SlotWrite40(opl2_slot *slot, uint8_t data)
{
    slot->reg_ksl = (data >> 6) & 0x03;
    slot->reg_tl  = data & 0x3f;

    OPL2_EnvelopeUpdateKSL(slot);
}

static void
OPL2_SlotWrite60(opl2_slot *slot, uint8_t data)
{
    slot->reg_ar = (data >> 4) & 0x0f;
    slot->reg_dr = data & 0x0f;
}

static void
OPL2_SlotWrite80(opl2_slot *slot, uint8_t data)
{
    slot->reg_sl = (data >> 4) & 0x0f;

    if (slot->reg_sl == 0x0f)
        slot->reg_sl = 0x1f;

    slot->reg_rr = data & 0x0f;
}

static void
OPL2_SlotWriteE0(opl2_slot *slot, uint8_t data)
{
    if (slot->chip->wfe)
        slot->reg_wf = data & 0x03;
}

static void
OPL2_SlotGenerate(opl2_slot *slot)
{
    slot->out = envelope_sin[slot->reg_wf](slot->pg_phase_out + *slot->mod, slot->eg_out);
}

static void
OPL2_SlotCalcFB(opl2_slot *slot)
{
    if (slot->channel->fb != 0x00)
        slot->fbmod = (slot->prout + slot->out) >> (0x09 - slot->channel->fb);
    else
        slot->fbmod = 0;

    slot->prout = slot->out;
}

// Channel
static void
OPL2_ChannelSetupCon(opl2_channel *channel);

static void
OPL2_ChannelUpdateRhythm(opl2_chip *chip, uint8_t data)
{
    opl2_channel *channel6;
    opl2_channel *channel7;
    opl2_channel *channel8;
    uint8_t       chnum;

    chip->rhy = data & 0x3f;
    if (chip->rhy & 0x20) {
        channel6         = &chip->channel[6];
        channel7         = &chip->channel[7];
        channel8         = &chip->channel[8];

        for (chnum = 6; chnum < 9; chnum++)
            chip->channel[chnum].chtype = ch_drum;

        OPL2_ChannelSetupCon(channel6);
        OPL2_ChannelSetupCon(channel7);
        OPL2_ChannelSetupCon(channel8);

        // hh
        if (chip->rhy & 0x01)
            OPL2_EnvelopeKeyOn(channel7->slotz[0], egk_drum);
        else
            OPL2_EnvelopeKeyOff(channel7->slotz[0], egk_drum);

        // tc
        if (chip->rhy & 0x02)
            OPL2_EnvelopeKeyOn(channel8->slotz[1], egk_drum);
        else
            OPL2_EnvelopeKeyOff(channel8->slotz[1], egk_drum);

        // tom
        if (chip->rhy & 0x04)
            OPL2_EnvelopeKeyOn(channel8->slotz[0], egk_drum);
        else
            OPL2_EnvelopeKeyOff(channel8->slotz[0], egk_drum);

        // sd
        if (chip->rhy & 0x08)
            OPL2_EnvelopeKeyOn(channel7->slotz[1], egk_drum);
        else
            OPL2_EnvelopeKeyOff(channel7->slotz[1], egk_drum);

        // bd
        if (chip->rhy & 0x10) {
            OPL2_EnvelopeKeyOn(channel6->slotz[0], egk_drum);
            OPL2_EnvelopeKeyOn(channel6->slotz[1], egk_drum);
        } else {
            OPL2_EnvelopeKeyOff(channel6->slotz[0], egk_drum);
            OPL2_EnvelopeKeyOff(channel6->slotz[1], egk_drum);
        }
    } else {
        for (chnum = 6; chnum < 9; chnum++) {
            chip->channel[chnum].chtype = ch_normal;

            OPL2_ChannelSetupCon(&chip->channel[chnum]);
            OPL2_EnvelopeKeyOff(chip->channel[chnum].slotz[0], egk_drum);
            OPL2_EnvelopeKeyOff(chip->channel[chnum].slotz[1], egk_drum);
        }
    }
}

static void
OPL2_ChannelWriteA0(opl2_channel *channel, uint8_t data)
{
    channel->f_num = (channel->f_num & 0x300) | data;
    channel->ksv   = (channel->block << 1)
                      | ((channel->f_num >> (0x09 - channel->chip->nts)) & 0x01);

    OPL2_EnvelopeUpdateKSL(channel->slotz[0]);
    OPL2_EnvelopeUpdateKSL(channel->slotz[1]);
}

static void
OPL2_ChannelKeyOn(opl2_channel *channel)
{
    OPL2_EnvelopeKeyOn(channel->slotz[0], egk_norm);
    OPL2_EnvelopeKeyOn(channel->slotz[1], egk_norm);
}

static void
OPL2_ChannelKeyOff(opl2_channel *channel)
{
    OPL2_EnvelopeKeyOff(channel->slotz[0], egk_norm);
    OPL2_EnvelopeKeyOff(channel->slotz[1], egk_norm);
}

static void
OPL2_ChannelWriteB0(opl2_channel *channel, uint8_t data)
{
    channel->f_num = (channel->f_num & 0xff) | ((data & 0x03) << 8);
    channel->block = (data >> 2) & 0x07;
    channel->ksv   = (channel->block << 1)
                      | ((channel->f_num >> (0x09 - channel->chip->nts)) & 0x01);

    OPL2_EnvelopeUpdateKSL(channel->slotz[0]);
    OPL2_EnvelopeUpdateKSL(channel->slotz[1]);

    if (data & 0x20)
        OPL2_ChannelKeyOn(channel);
    else
        OPL2_ChannelKeyOff(channel);
}

static void
OPL2_ChannelSetupCon(opl2_channel *channel)
{
    if (channel->chtype == ch_drum) {
        if (channel->ch_num == 7 || channel->ch_num == 8) {
            channel->slotz[0]->mod = &channel->chip->zeromod;
            channel->slotz[1]->mod = &channel->chip->zeromod;
            return;
        }

        switch (channel->con) {
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

    switch (channel->con) {
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
}

static void
OPL2_ChannelWriteC0(opl2_channel *channel, uint8_t data)
{
    channel->fb  = (data & 0x0e) >> 1;
    channel->con = data & 0x01;

    OPL2_ChannelSetupCon(channel);
}

static void
OPL2_UpdateChannelParams(opl2_chip *chip, uint8_t slot)
{
    if (slot < 12) {
        uint8_t ch = slot;

        if (ch >= 9)
            ch -= 9;

        if (chip->ch_upd_a0 & (1u << slot))
            OPL2_ChannelWriteA0(&chip->channel[ch], chip->ch_upd_a0_value[slot]);

        if (chip->ch_upd_b0 & (1u << slot))
            OPL2_ChannelWriteB0(&chip->channel[ch], chip->ch_upd_b0_value[slot]);
    }
}

static void
OPL2_ProcessSlot(opl2_slot *slot)
{
    OPL2_SlotCalcFB(slot);
    OPL2_EnvelopeCalc(slot);
    OPL2_PhaseGenerate(slot);
    OPL2_SlotGenerate(slot);
}

static void
OPL2_ProcessTimers(opl2_chip *chip)
{
    chip->csm_kon = 0;

    if (chip->t1_start && (chip->timer & 0x3) == 0x3) {
        chip->t1_value++;

        if (chip->t1_value == 0u) {
            if (chip->csm_enable)
                chip->csm_kon = 1;

            chip->t1_value = chip->t1_reg;

            if (!chip->t1_mask)
                chip->t1_status = 1;
        }
    }

    if (chip->t2_start && (chip->timer & 0xf) == 0xf) {
        chip->t2_value++;

        if (chip->t2_value == 0u) {
            chip->t2_value = chip->t2_reg;

            if (!chip->t2_mask)
                chip->t2_status = 1;
        }
    }
}

#if 0
static int16_t
OPL2_OutputCrush(int32_t sample)
{
    uint8_t shift;
    int32_t top;

    if (sample > 32767)
        sample = 32767;
    else if (sample < -32768)
        sample = -32768;

    top = sample >> 9;

    if (top < 0)
        top = (~top) & 63;
    else
        top = top & 63;

    shift = 0;

    if (top & 32)
        shift = 6;
    else if ((top & 48) == 16)
        shift = 5;
    else if ((top & 56) == 8)
        shift = 4;
    else if ((top & 60) == 4)
        shift = 3;
    else if ((top & 62) == 2)
        shift = 2;
    else if (top == 1)
        shift = 1;
    else if (top == 0)
        shift = 0;

    sample >>= shift;
    sample <<= shift;

    return (int16_t) sample;
}
#endif

void
OPL2_Generate(opl2_chip *chip, int32_t *sample)
{
    opl2_writebuf *writebuf;
    int32_t        mix;
    uint8_t        ii;
    uint8_t        shift;

    *sample = chip->mixbuff;

    for (ii = 0; ii < 15; ii++) {
        OPL2_ProcessSlot(&chip->slot[ii]);
        OPL2_UpdateChannelParams(chip, ii);
    }

    mix = 0;

    if (chip->channel[0].con && !chip->slot[0].eg_mute)
        mix += chip->slot[0].out;

    if (chip->channel[1].con && !chip->slot[1].eg_mute)
        mix += chip->slot[1].out;

    if (chip->channel[2].con && !chip->slot[2].eg_mute)
        mix += chip->slot[2].out;

    if (!chip->slot[3].eg_mute)
        mix += chip->slot[3].out;

    if (!chip->slot[4].eg_mute)
        mix += chip->slot[4].out;

    if (!chip->slot[5].eg_mute)
        mix += chip->slot[5].out;

    if (chip->channel[3].con && !chip->slot[6].eg_mute)
        mix += chip->slot[6].out;

    if (chip->channel[4].con && !chip->slot[7].eg_mute)
        mix += chip->slot[7].out;

    if (chip->channel[5].con && !chip->slot[8].eg_mute)
        mix += chip->slot[8].out;

    if (!chip->slot[9].eg_mute)
        mix += chip->slot[9].out;

    if (!chip->slot[10].eg_mute)
        mix += chip->slot[10].out;

    if (!chip->slot[11].eg_mute)
        mix += chip->slot[11].out;

    if (chip->rhy & 0x20) {
        if (!chip->slot[13].eg_mute)
            mix += chip->slot[13].out << 1;

        if (!chip->slot[14].eg_mute)
            mix += chip->slot[14].out << 1;

        if (!chip->slot[15].eg_mute)
            mix += chip->slot[15].out << 1;

        if (!chip->slot[16].eg_mute)
            mix += chip->slot[16].out << 1;

        if (!chip->slot[17].eg_mute)
            mix += chip->slot[17].out << 1;
    } else {
        if (chip->channel[6].con && !chip->slot[12].eg_mute)
            mix += chip->slot[12].out;

        if (chip->channel[7].con && !chip->slot[13].eg_mute)
            mix += chip->slot[13].out;

        if (chip->channel[8].con && !chip->slot[14].eg_mute)
            mix += chip->slot[14].out;

        if (!chip->slot[15].eg_mute)
            mix += chip->slot[15].out;

        if (!chip->slot[16].eg_mute)
            mix += chip->slot[16].out;

        if (!chip->slot[17].eg_mute)
            mix += chip->slot[17].out;
    }

    chip->mixbuff = mix;

    for (ii = 15; ii < 18; ii++) {
        OPL2_ProcessSlot(&chip->slot[ii]);
        OPL2_UpdateChannelParams(chip, ii);
    }

    chip->ch_upd_a0 = 0;
    chip->ch_upd_b0 = 0;

    OPL2_ProcessTimers(chip);

    if ((chip->timer & 0x3f) == 0x3f)
        chip->tremolopos = (chip->tremolopos + 1) % 210;

    if (chip->tremolopos < 105)
        chip->tremolo = chip->tremolopos >> chip->tremoloshift;
    else
        chip->tremolo = (210 - chip->tremolopos) >> chip->tremoloshift;

    if ((chip->timer & 0x3ff) == 0x3ff)
        chip->vibpos = (chip->vibpos + 1) & 7;

    chip->timer++;

    if (chip->eg_state) {
        shift = 0;

        while (shift < 13 && ((chip->eg_timer >> shift) & 1) == 0)
            shift++;

        if (shift > 12)
            chip->eg_add = 0;
        else
            chip->eg_add = shift + 1;

        chip->eg_timer_lo = (uint8_t)(chip->eg_timer & 0x3u);

        if (chip->eg_timer == 0x3ffff) {
            chip->eg_timer    = 0;
            chip->eg_timerrem = 1;
        } else {
            chip->eg_timer++;
            chip->eg_timer += chip->eg_timerrem;
            chip->eg_timerrem = 0;
        }
    }

    chip->eg_state ^= 1;

    while ((writebuf = &chip->writebuf[chip->writebuf_cur]), writebuf->time <= chip->writebuf_samplecnt) {
        if (!(writebuf->reg & 0x100))
            break;

        writebuf->reg &= 0xff;

        OPL2_WriteReg(chip, writebuf->reg, writebuf->data);

        chip->writebuf_cur = (chip->writebuf_cur + 1) % OPL2_WRITEBUF_SIZE;
    }

    chip->writebuf_samplecnt++;
}

#if 0
void
OPL2_Generate(opl2_chip *chip, int32_t *buf)
{
    int32_t samples[4];

    OPL2_GenerateResampled(chip, sndptr)

    buf[0] = samples[0];
    buf[1] = samples[1];
}
#endif

void
OPL2_GenerateResampled(opl2_chip *chip, int32_t *sample)
{
    while (chip->samplecnt >= chip->rateratio) {
        chip->oldsample = chip->sample;

        OPL2_Generate(chip, &chip->sample);
        chip->samplecnt -= chip->rateratio;
    }

    *sample = (int32_t) ((chip->oldsample * (chip->rateratio - chip->samplecnt)
                         + chip->sample * chip->samplecnt) / chip->rateratio);
    chip->samplecnt += 1 << RSM_FRAC;
}

void
OPL2_Reset(opl2_chip *chip, uint32_t samplerate)
{
    opl2_slot    *slot;
    opl2_channel *channel;
    uint8_t       local_ch_slot;

    memset(chip, 0x00, sizeof(opl2_chip));

    for (uint8_t slotnum = 0; slotnum < 18; slotnum++) {
        slot           = &chip->slot[slotnum];
        slot->chip     = chip;
        slot->mod      = &chip->zeromod;
        slot->eg_rout  = 0x1ff;
        slot->eg_out   = 0x1ff;
        slot->eg_gen   = envelope_gen_num_release;
        slot->trem     = (uint8_t *) &chip->zeromod;
        slot->slot_num = slotnum;
    }

    for (uint8_t channum = 0; channum < 9; channum++) {
        channel                                = &chip->channel[channum];
        local_ch_slot                          = ch_slot[channum];
        channel->slotz[0]                      = &chip->slot[local_ch_slot];
        channel->slotz[1]                      = &chip->slot[local_ch_slot + 3u];
        chip->slot[local_ch_slot].channel      = channel;
        chip->slot[local_ch_slot + 3u].channel = channel;
        channel->chip                          = chip;
        channel->chtype                        = ch_normal;
        channel->ch_num = channum;

        OPL2_ChannelSetupCon(channel);
    }

    chip->noise        = 1;
    chip->rateratio    = (samplerate << RSM_FRAC) / FREQ_49716;
    chip->tremoloshift = 4;
    chip->vibshift     = 1;
}

uint16_t
nuked_opl2_write_addr(void *priv, uint16_t port, uint8_t val)
{
//    const opl2_chip *chip = (opl2_chip *) priv;
    uint16_t         addr = val;

    // TODO: Is this needed?
    if ((port & 0x0002) && (addr == 0x0005))
        addr |= 0x0100;

    return addr;
}

void
OPL2_WriteReg(void *priv, uint8_t reg, uint8_t val)
{
    opl2_chip *chip = (opl2_chip *) priv;
    uint8_t    regm = reg & 0xff;

    switch (regm & 0xf0) {
        case 0x00:
            switch (regm & 0x0f) {
                case 0x01:
                    chip->wfe = (val >> 5) & 1;
                    break;

                case 0x02:
                    chip->t1_reg = val;
                    break;

                case 0x03:
                    chip->t2_reg = val;
                    break;

                case 0x04:
                    if (val & 0x80) {
                        chip->t1_status = 0;
                        chip->t2_status = 0;
                    } else {
                        if (!chip->t1_start && (val & 1))
                            chip->t1_value = chip->t1_reg;

                        if (!chip->t2_start && (val & 2))
                            chip->t2_value = chip->t2_reg;

                        chip->t1_mask = (val >> 6) & 1;
                        chip->t2_mask = (val >> 5) & 1;
                        chip->t1_start = (val >> 0) & 1;
                        chip->t2_start = (val >> 1) & 1;

                        if (!chip->t1_mask)
                            chip->t1_status = 0;

                        if (!chip->t2_mask)
                            chip->t2_status = 0;
                    }
                    break;

                case 0x08:
                    chip->nts        = (val >> 6) & 0x01;
                    chip->csm_enable = (val >> 7) & 1;
                    break;

                    default:
                        break;
            }
            break;

        case 0x20:
        case 0x30:
            if (ad_slot[regm & 0x1fu] >= 0)
                OPL2_SlotWrite20(&chip->slot[ad_slot[regm & 0x1fu]], val);
            break;

        case 0x40:
        case 0x50:
            if (ad_slot[regm & 0x1fu] >= 0)
                OPL2_SlotWrite40(&chip->slot[ad_slot[regm & 0x1fu]], val);
            break;

        case 0x60:
        case 0x70:
            if (ad_slot[regm & 0x1fu] >= 0)
                OPL2_SlotWrite60(&chip->slot[ad_slot[regm & 0x1fu]], val);
            break;

        case 0x80:
        case 0x90:
            if (ad_slot[regm & 0x1fu] >= 0)
                OPL2_SlotWrite80(&chip->slot[ad_slot[regm & 0x1fu]], val);
            break;

        case 0xe0:
        case 0xf0:
            if (ad_slot[regm & 0x1fu] >= 0)
                OPL2_SlotWriteE0(&chip->slot[ad_slot[regm & 0x1fu]], val);
            break;

        case 0xa0:
            if (ad_ch2[regm & 0xfu] >= 0) {
                int8_t ch = ad_ch2[regm & 0xfu];
                chip->ch_upd_a0 |= 1 << ch;
                chip->ch_upd_a0_value[ch] = val;
            }
            break;

        case 0xb0:
            if (regm == 0xbd) {
                chip->tremoloshift = (((val >> 7) ^ 1) << 1) + 2;
                chip->vibshift = ((val >> 6) & 0x01) ^ 1;
                OPL2_ChannelUpdateRhythm(chip, val);
            }
            if (ad_ch2[regm & 0xfu] >= 0) {
                int8_t ch = ad_ch2[regm & 0xfu];
                chip->ch_upd_b0 |= 1 << ch;
                chip->ch_upd_b0_value[ch] = val;
            }
            break;

        case 0xc0:
            if (ad_ch[regm & 0xfu] >= 0)
                OPL2_ChannelWriteC0(&chip->channel[ad_ch[regm & 0xfu]], val);
            break;

        default:
            break;
    }
}

void
OPL2_WriteRegBuffered(void *priv, uint8_t reg, uint8_t val)
{
    opl2_chip     *chip = (opl2_chip *) priv;
    uint64_t       time1;
    uint64_t       time2;
    opl2_writebuf *writebuf;
    uint32_t       writebuf_last;

    writebuf_last = chip->writebuf_last;
    writebuf      = &chip->writebuf[writebuf_last];

    if (writebuf->reg & 0x100) {
        OPL2_WriteReg(chip, writebuf->reg & 0xff, writebuf->data);

        chip->writebuf_cur       = (writebuf_last + 1) % OPL2_WRITEBUF_SIZE;
        chip->writebuf_samplecnt = writebuf->time;
    }

    writebuf->reg  = (uint16_t) reg | 0x100;
    writebuf->data = val;
    time1          = chip->writebuf_lasttime + OPL2_WRITEBUF_DELAY;
    time2          = chip->writebuf_samplecnt;

    if (time1 < time2)
        time1 = time2;

    writebuf->time          = time1;
    chip->writebuf_lasttime = time1;
    chip->writebuf_last     = (writebuf_last + 1) % OPL2_WRITEBUF_SIZE;
}

void
OPL2_GenerateStream(opl2_chip *chip, int32_t *sndptr, uint32_t numsamples)
{
for (uint_fast32_t i = 0; i < numsamples; i++) {
        int32_t sample;
        OPL2_Generate(chip, &sample);
        sndptr[i*2] = sample;     // Left
        sndptr[i*2 + 1] = sample; // Right
    }
}

void
OPL2_GenerateResampledStream(opl2_chip *chip, int32_t *sndptr, uint32_t numsamples)
{
for (uint_fast32_t i = 0; i < numsamples; i++) {
        int32_t sample;
        OPL2_GenerateResampled(chip, &sample);
        sndptr[i*2] = sample;     // Left
        sndptr[i*2 + 1] = sample; // Right
    }
}

uint8_t
OPL2_ReadStatus(opl2_chip *chip)
{
    uint8_t status = 0x6u;

    if (chip->t1_status)
        status |= 0xc0u;

    if (chip->t2_status)
        status |= 0xa0u;

    return status;
}

static void
nuked_opl2_timer_tick(nuked_opl2_drv_t *dev, int tmr)
{
    dev->timer_cur_count[tmr] = (dev->timer_cur_count[tmr] + 1) & 0xff;

    nuked_opl2_log("Ticking timer %i, count now %02X...\n", tmr, dev->timer_cur_count[tmr]);

    if (dev->timer_cur_count[tmr] == 0x00) {
        dev->status |= ((STAT_TMR1_OVER >> tmr) & ~dev->timer_ctrl);
        dev->timer_cur_count[tmr] = dev->timer_count[tmr];

        nuked_opl2_log("Count wrapped around to zero, reloading timer %i (%02X), status = %02X...\n", tmr, (STAT_TMR1_OVER >> tmr), dev->status);
    }

    timer_on_auto(&dev->timers[tmr], (tmr == 1) ? 320.0 : 80.0);
}

static void
nuked_opl2_timer_control(nuked_opl2_drv_t *dev, int tmr, int start)
{
    timer_on_auto(&dev->timers[tmr], 0.0);

    if (start) {
        nuked_opl2_log("Loading timer %i count: %02X = %02X\n", tmr, dev->timer_cur_count[tmr], dev->timer_count[tmr]);
        dev->timer_cur_count[tmr] = dev->timer_count[tmr];
        timer_on_auto(&dev->timers[tmr], (tmr == 1) ? 320.0 : 80.0);
    } else {
        nuked_opl2_log("Timer %i stopped\n", tmr);
        if (tmr == 1) {
            dev->status &= ~STAT_TMR2_OVER;
        } else
            dev->status &= ~STAT_TMR1_OVER;
    }
}

static void
nuked_opl2_timer_1(void *priv)
{
    nuked_opl2_drv_t *dev = (nuked_opl2_drv_t *) priv;

    nuked_opl2_timer_tick(dev, 0);
}

static void
nuked_opl2_timer_2(void *priv)
{
    nuked_opl2_drv_t *dev = (nuked_opl2_drv_t *) priv;

    nuked_opl2_timer_tick(dev, 1);
}

static void
nuked_opl2_drv_set_do_cycles(void *priv, int8_t do_cycles)
{
    nuked_opl2_drv_t *dev = (nuked_opl2_drv_t *) priv;

    if (do_cycles)
        dev->flags |= FLAG_CYCLES;
    else
        dev->flags &= ~FLAG_CYCLES;
}

static int32_t *
nuked_opl2_drv_update(void *priv)
{
    nuked_opl2_drv_t *dev = (nuked_opl2_drv_t *) priv;

    if (dev->pos >= music_pos_global)
        return dev->buffer;

    OPL2_GenerateStream(&dev->opl,
                        &dev->buffer[dev->pos * 2],
                        music_pos_global - dev->pos);

    for (; dev->pos < music_pos_global; dev->pos++) {
        dev->buffer[dev->pos * 2] /= 2;
        dev->buffer[(dev->pos * 2) + 1] /= 2;
    }

    return dev->buffer;
}

static int32_t *
nuked_opl2_drv_update_48k(void *priv)
{
    nuked_opl2_drv_t *dev = (nuked_opl2_drv_t *) priv;

    if (dev->pos >= sound_pos_global)
        return dev->buffer;

    OPL2_GenerateResampledStream(&dev->opl,
                                 &dev->buffer[dev->pos * 2],
                                 sound_pos_global - dev->pos);

    for (; dev->pos < sound_pos_global; dev->pos++) {
        dev->buffer[dev->pos * 2] /= 2;
        dev->buffer[(dev->pos * 2) + 1] /= 2;
    }

    return dev->buffer;
}

static uint8_t
nuked_opl2_drv_read(uint16_t port, void *priv)
{
    nuked_opl2_drv_t *dev = (nuked_opl2_drv_t *) priv;

    if (dev->flags & FLAG_CYCLES)
        cycles -= ((int) (isa_timing * 8));

    dev->update(dev);

    uint8_t ret = 0xff;

    if ((port & 0x0003) == 0x0000) {
        ret = dev->status;
        if (dev->status & STAT_TMR_OVER)
            ret |= STAT_TMR_ANY;
    }

    nuked_opl2_log("OPL statret = %02x, status = %02x\n", ret, dev->status);

    return ret;
}

static void
nuked_opl2_drv_write(uint16_t port, uint8_t val, void *priv)
{
    nuked_opl2_drv_t *dev = (nuked_opl2_drv_t *) priv;

    dev->update(dev);

    if ((port & 0x0001) == 0x0001) {
        OPL2_WriteRegBuffered(&dev->opl, dev->port, val);

        switch (dev->port) {
            case 0x002: // Timer 1
                dev->timer_count[0] = val;
                nuked_opl2_log("Timer 0 count now: %i\n", dev->timer_count[0]);
                break;

            case 0x003: // Timer 2
                dev->timer_count[1] = val;
                nuked_opl2_log("Timer 1 count now: %i\n", dev->timer_count[1]);
                break;

            case 0x004: // Timer control
                if (val & CTRL_RESET) {
                    nuked_opl2_log("Resetting timer status...\n");
                    dev->status &= ~STAT_TMR_OVER;
                } else {
                    dev->timer_ctrl = val;
                    nuked_opl2_timer_control(dev, 0, val & CTRL_TMR1_START);
                    nuked_opl2_timer_control(dev, 1, val & CTRL_TMR2_START);
                    nuked_opl2_log("Status mask now %02X (val = %02X)\n", (val & ~CTRL_TMR_MASK) & CTRL_TMR_MASK, val);
                }
                break;

#if 0
            // Leftover from OPL3
            case 0x105:
                dev->opl.newm = val & 0x01;
                break;
#endif

            default:
                break;
        }
    } else {
        dev->port = nuked_opl2_write_addr(&dev->opl, port, val) & 0x01ff;

        // TODO: Check this is needed
        dev->port &= 0x00ff;
    }
}

static void
nuked_opl2_drv_reset_buffer(void *priv)
{
    nuked_opl2_drv_t *dev = (nuked_opl2_drv_t *) priv;

    dev->pos = 0;
}

static void
nuked_opl2_drv_close(void *priv)
{
    nuked_opl2_drv_t *dev = (nuked_opl2_drv_t *) priv;

    free(dev);
}

static void *
nuked_opl2_drv_init(const device_t *info)
{
    nuked_opl2_drv_t *dev = (nuked_opl2_drv_t *) calloc(1, sizeof(nuked_opl2_drv_t));
    dev->flags       = FLAG_CYCLES;
    // TODO: Check this is needed
    dev->status = 0x06;

    dev->is_48k      = !!(info->local & FM_FORCE_48K);

    // Initialize the NukedOPL2 object.
    if (dev->is_48k) {
        dev->update      = nuked_opl2_drv_update_48k;
        OPL2_Reset(&dev->opl, FREQ_48000);
    } else {
        dev->update      = nuked_opl2_drv_update;
        OPL2_Reset(&dev->opl, FREQ_49716);
    }

    timer_add(&dev->timers[0], nuked_opl2_timer_1, dev, 0);
    timer_add(&dev->timers[1], nuked_opl2_timer_2, dev, 0);

    return dev;
}

const device_t ym3812_nuked_opl2_device = {
    .name          = "Yamaha YM3812 OPL2 (NUKED OPL2)",
    .internal_name = "ym3812_nuked_opl2",
    .flags         = 0,
    .local         = FM_YM3812,
    .init          = nuked_opl2_drv_init,
    .close         = nuked_opl2_drv_close,
    .reset         = NULL,
    .available     = NULL,
    .speed_changed = NULL,
    .force_redraw  = NULL,
    .config        = NULL
};

const fm_drv_t nuked_opl2_drv = {
    .read          = &nuked_opl2_drv_read,
    .write         = &nuked_opl2_drv_write,
    .update        = &nuked_opl2_drv_update,
    .reset_buffer  = &nuked_opl2_drv_reset_buffer,
    .set_do_cycles = &nuked_opl2_drv_set_do_cycles,
    .priv          = NULL,
    .generate      = NULL,
};

const fm_drv_t nuked_opl2_drv_48k = {
    .read          = &nuked_opl2_drv_read,
    .write         = &nuked_opl2_drv_write,
    .update        = &nuked_opl2_drv_update_48k,
    .reset_buffer  = &nuked_opl2_drv_reset_buffer,
    .set_do_cycles = &nuked_opl2_drv_set_do_cycles,
    .priv          = NULL,
    .generate      = NULL,
};
