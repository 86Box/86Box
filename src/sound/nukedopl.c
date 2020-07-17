//
// Copyright (C) 2013-2018 Alexey Khokholov (Nuke.YKT)
//
// This program is free software; you can redistribute it and/or
// modify it under the terms of the GNU General Public License
// as published by the Free Software Foundation; either version 2
// of the License, or (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
//
//  Nuked OPL3 emulator.
//  Thanks:
//      MAME Development Team(Jarek Burczynski, Tatsuyuki Satoh):
//          Feedback and Rhythm part calculation information.
//      forums.submarine.org.uk(carbon14, opl3):
//          Tremolo and phase generator calculation information.
//      OPLx decapsulated(Matthew Gambrell, Olli Niemitalo):
//          OPL2 ROMs.
//      siliconpr0n.org(John McMaster, digshadow):
//          YMF262 and VRC VII decaps and die shots.
//
// version: 1.8
//

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "nukedopl.h"

#define RSM_FRAC    10

// Channel types

enum {
    ch_2op = 0,
    ch_4op = 1,
    ch_4op2 = 2,
    ch_drum = 3
};

// Envelope key types

enum {
    egk_norm = 0x01,
    egk_drum = 0x02
};


//
// logsin table
//

static const Bit16u logsinrom[256] = {
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

//
// exp table
//

static const Bit16u exprom[256] = {
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

//
// freq mult table multiplied by 2
//
// 1/2, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 10, 12, 12, 15, 15
//

static const Bit8u mt[16] = {
    1, 2, 4, 6, 8, 10, 12, 14, 16, 18, 20, 20, 24, 24, 30, 30
};

//
// ksl table
//

static const Bit8u kslrom[16] = {
    0, 32, 40, 45, 48, 51, 53, 55, 56, 58, 59, 60, 61, 62, 63, 64
};

static const Bit8u kslshift[4] = {
    8, 1, 2, 0
};

//
// envelope generator constants
//

static const Bit8u eg_incstep[4][4] = {
    { 0, 0, 0, 0 },
    { 1, 0, 0, 0 },
    { 1, 0, 1, 0 },
    { 1, 1, 1, 0 }
};

//
// address decoding
//

static const Bit8s ad_slot[0x20] = {
    0, 1, 2, 3, 4, 5, -1, -1, 6, 7, 8, 9, 10, 11, -1, -1,
    12, 13, 14, 15, 16, 17, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1
};

static const Bit8u ch_slot[18] = {
    0, 1, 2, 6, 7, 8, 12, 13, 14, 18, 19, 20, 24, 25, 26, 30, 31, 32
};

//
// Envelope generator
//

typedef Bit16s(*envelope_sinfunc)(Bit16u phase, Bit16u envelope);
typedef void(*envelope_genfunc)(struct opl3_slot *slott);

static Bit16s OPL3_EnvelopeCalcExp(Bit32u level)
{
    if (level > 0x1fff)
    {
        level = 0x1fff;
    }
    return (exprom[level & 0xff] << 1) >> (level >> 8);
}

static Bit16s OPL3_EnvelopeCalcSin0(Bit16u phase, Bit16u envelope)
{
    Bit16u out = 0;
    Bit16u neg = 0;
    phase &= 0x3ff;
    if (phase & 0x200)
    {
        neg = 0xffff;
    }
    if (phase & 0x100)
    {
        out = logsinrom[(phase & 0xff) ^ 0xff];
    }
    else
    {
        out = logsinrom[phase & 0xff];
    }
    return OPL3_EnvelopeCalcExp(out + (envelope << 3)) ^ neg;
}

static Bit16s OPL3_EnvelopeCalcSin1(Bit16u phase, Bit16u envelope)
{
    Bit16u out = 0;
    phase &= 0x3ff;
    if (phase & 0x200)
    {
        out = 0x1000;
    }
    else if (phase & 0x100)
    {
        out = logsinrom[(phase & 0xff) ^ 0xff];
    }
    else
    {
        out = logsinrom[phase & 0xff];
    }
    return OPL3_EnvelopeCalcExp(out + (envelope << 3));
}

static Bit16s OPL3_EnvelopeCalcSin2(Bit16u phase, Bit16u envelope)
{
    Bit16u out = 0;
    phase &= 0x3ff;
    if (phase & 0x100)
    {
        out = logsinrom[(phase & 0xff) ^ 0xff];
    }
    else
    {
        out = logsinrom[phase & 0xff];
    }
    return OPL3_EnvelopeCalcExp(out + (envelope << 3));
}

static Bit16s OPL3_EnvelopeCalcSin3(Bit16u phase, Bit16u envelope)
{
    Bit16u out = 0;
    phase &= 0x3ff;
    if (phase & 0x100)
    {
        out = 0x1000;
    }
    else
    {
        out = logsinrom[phase & 0xff];
    }
    return OPL3_EnvelopeCalcExp(out + (envelope << 3));
}

static Bit16s OPL3_EnvelopeCalcSin4(Bit16u phase, Bit16u envelope)
{
    Bit16u out = 0;
    Bit16u neg = 0;
    phase &= 0x3ff;
    if ((phase & 0x300) == 0x100)
    {
        neg = 0xffff;
    }
    if (phase & 0x200)
    {
        out = 0x1000;
    }
    else if (phase & 0x80)
    {
        out = logsinrom[((phase ^ 0xff) << 1) & 0xff];
    }
    else
    {
        out = logsinrom[(phase << 1) & 0xff];
    }
    return OPL3_EnvelopeCalcExp(out + (envelope << 3)) ^ neg;
}

static Bit16s OPL3_EnvelopeCalcSin5(Bit16u phase, Bit16u envelope)
{
    Bit16u out = 0;
    phase &= 0x3ff;
    if (phase & 0x200)
    {
        out = 0x1000;
    }
    else if (phase & 0x80)
    {
        out = logsinrom[((phase ^ 0xff) << 1) & 0xff];
    }
    else
    {
        out = logsinrom[(phase << 1) & 0xff];
    }
    return OPL3_EnvelopeCalcExp(out + (envelope << 3));
}

static Bit16s OPL3_EnvelopeCalcSin6(Bit16u phase, Bit16u envelope)
{
    Bit16u neg = 0;
    phase &= 0x3ff;
    if (phase & 0x200)
    {
        neg = 0xffff;
    }
    return OPL3_EnvelopeCalcExp(envelope << 3) ^ neg;
}

static Bit16s OPL3_EnvelopeCalcSin7(Bit16u phase, Bit16u envelope)
{
    Bit16u out = 0;
    Bit16u neg = 0;
    phase &= 0x3ff;
    if (phase & 0x200)
    {
        neg = 0xffff;
        phase = (phase & 0x1ff) ^ 0x1ff;
    }
    out = phase << 3;
    return OPL3_EnvelopeCalcExp(out + (envelope << 3)) ^ neg;
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

enum envelope_gen_num
{
    envelope_gen_num_attack = 0,
    envelope_gen_num_decay = 1,
    envelope_gen_num_sustain = 2,
    envelope_gen_num_release = 3
};

static void OPL3_EnvelopeUpdateKSL(struct opl3_slot *slot)
{
    Bit16s ksl = (kslrom[slot->channel->f_num >> 6] << 2)
               - ((0x08 - slot->channel->block) << 5);
    if (ksl < 0)
    {
        ksl = 0;
    }
    slot->eg_ksl = (Bit8u)ksl;
}

static void OPL3_EnvelopeCalc(struct opl3_slot *slot)
{
    Bit8u nonzero;
    Bit8u rate;
    Bit8u rate_hi;
    Bit8u rate_lo;
    Bit8u reg_rate = 0;
    Bit8u ks;
    Bit8u eg_shift, shift;
    Bit16u eg_rout;
    Bit16s eg_inc;
    Bit8u eg_off;
    Bit8u reset = 0;
    slot->eg_out = slot->eg_rout + (slot->reg_tl << 2)
                 + (slot->eg_ksl >> kslshift[slot->reg_ksl]) + *slot->trem;
    if (slot->key && slot->eg_gen == envelope_gen_num_release)
    {
        reset = 1;
        reg_rate = slot->reg_ar;
    }
    else
    {
        switch (slot->eg_gen)
        {
        case envelope_gen_num_attack:
            reg_rate = slot->reg_ar;
            break;
        case envelope_gen_num_decay:
            reg_rate = slot->reg_dr;
            break;
        case envelope_gen_num_sustain:
            if (!slot->reg_type)
            {
                reg_rate = slot->reg_rr;
            }
            break;
        case envelope_gen_num_release:
            reg_rate = slot->reg_rr;
            break;
        }
    }
    slot->pg_reset = reset;
    ks = slot->channel->ksv >> ((slot->reg_ksr ^ 1) << 1);
    nonzero = (reg_rate != 0);
    rate = ks + (reg_rate << 2);
    rate_hi = rate >> 2;
    rate_lo = rate & 0x03;
    if (rate_hi & 0x10)
    {
        rate_hi = 0x0f;
    }
    eg_shift = rate_hi + slot->chip->eg_add;
    shift = 0;
    if (nonzero)
    {
        if (rate_hi < 12)
        {
            if (slot->chip->eg_state)
            {
                switch (eg_shift)
                {
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
        }
        else
        {
            shift = (rate_hi & 0x03) + eg_incstep[rate_lo][slot->chip->timer & 0x03];
            if (shift & 0x04)
            {
                shift = 0x03;
            }
            if (!shift)
            {
                shift = slot->chip->eg_state;
            }
        }
    }
    eg_rout = slot->eg_rout;
    eg_inc = 0;
    eg_off = 0;
    // Instant attack
    if (reset && rate_hi == 0x0f)
    {
        eg_rout = 0x00;
    }
    // Envelope off
    if ((slot->eg_rout & 0x1f8) == 0x1f8)
    {
        eg_off = 1;
    }
    if (slot->eg_gen != envelope_gen_num_attack && !reset && eg_off)
    {
        eg_rout = 0x1ff;
    }
    switch (slot->eg_gen)
    {
    case envelope_gen_num_attack:
        if (!slot->eg_rout)
        {
            slot->eg_gen = envelope_gen_num_decay;
        }
        else if (slot->key && shift > 0 && rate_hi != 0x0f)
        {
            eg_inc = ((~slot->eg_rout) << shift) >> 4;
        }
        break;
    case envelope_gen_num_decay:
        if ((slot->eg_rout >> 4) == slot->reg_sl)
        {
            slot->eg_gen = envelope_gen_num_sustain;
        }
        else if (!eg_off && !reset && shift > 0)
        {
            eg_inc = 1 << (shift - 1);
        }
        break;
    case envelope_gen_num_sustain:
    case envelope_gen_num_release:
        if (!eg_off && !reset && shift > 0)
        {
            eg_inc = 1 << (shift - 1);
        }
        break;
    }
    slot->eg_rout = (eg_rout + eg_inc) & 0x1ff;
    // Key off
    if (reset)
    {
        slot->eg_gen = envelope_gen_num_attack;
    }
    if (!slot->key)
    {
        slot->eg_gen = envelope_gen_num_release;
    }
}

static void OPL3_EnvelopeKeyOn(struct opl3_slot *slot, Bit8u type)
{
    slot->key |= type;
}

static void OPL3_EnvelopeKeyOff(struct opl3_slot *slot, Bit8u type)
{
    slot->key &= ~type;
}

//
// Phase Generator
//

static void OPL3_PhaseGenerate(struct opl3_slot *slot)
{
    struct opl3_chip *chip;
    Bit16u f_num;
    Bit32u basefreq;
    Bit8u rm_xor, n_bit;
    Bit32u noise;
    Bit16u phase;

    chip = slot->chip;
    f_num = slot->channel->f_num;
    if (slot->reg_vib)
    {
        Bit8s range;
        Bit8u vibpos;

        range = (f_num >> 7) & 7;
        vibpos = slot->chip->vibpos;

        if (!(vibpos & 3))
        {
            range = 0;
        }
        else if (vibpos & 1)
        {
            range >>= 1;
        }
        range >>= slot->chip->vibshift;

        if (vibpos & 4)
        {
            range = -range;
        }
        f_num += range;
    }
    basefreq = (f_num << slot->channel->block) >> 1;
    phase = (Bit16u)(slot->pg_phase >> 9);
    if (slot->pg_reset)
    {
        slot->pg_phase = 0;
    }
    slot->pg_phase += (basefreq * mt[slot->reg_mult]) >> 1;
    // Rhythm mode
    noise = chip->noise;
    slot->pg_phase_out = phase;
    if (slot->slot_num == 13) // hh
    {
        chip->rm_hh_bit2 = (phase >> 2) & 1;
        chip->rm_hh_bit3 = (phase >> 3) & 1;
        chip->rm_hh_bit7 = (phase >> 7) & 1;
        chip->rm_hh_bit8 = (phase >> 8) & 1;
    }
    if (slot->slot_num == 17 && (chip->rhy & 0x20)) // tc
    {
        chip->rm_tc_bit3 = (phase >> 3) & 1;
        chip->rm_tc_bit5 = (phase >> 5) & 1;
    }
    if (chip->rhy & 0x20)
    {
        rm_xor = (chip->rm_hh_bit2 ^ chip->rm_hh_bit7)
               | (chip->rm_hh_bit3 ^ chip->rm_tc_bit5)
               | (chip->rm_tc_bit3 ^ chip->rm_tc_bit5);
        switch (slot->slot_num)
        {
        case 13: // hh
            slot->pg_phase_out = rm_xor << 9;
            if (rm_xor ^ (noise & 1))
            {
                slot->pg_phase_out |= 0xd0;
            }
            else
            {
                slot->pg_phase_out |= 0x34;
            }
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

//
// Slot
//

static void OPL3_SlotWrite20(struct opl3_slot *slot, Bit8u data)
{
    if ((data >> 7) & 0x01)
    {
        slot->trem = &slot->chip->tremolo;
    }
    else
    {
        slot->trem = (Bit8u*)&slot->chip->zeromod;
    }
    slot->reg_vib = (data >> 6) & 0x01;
    slot->reg_type = (data >> 5) & 0x01;
    slot->reg_ksr = (data >> 4) & 0x01;
    slot->reg_mult = data & 0x0f;
}

static void OPL3_SlotWrite40(struct opl3_slot *slot, Bit8u data)
{
    slot->reg_ksl = (data >> 6) & 0x03;
    slot->reg_tl = data & 0x3f;
    OPL3_EnvelopeUpdateKSL(slot);
}

static void OPL3_SlotWrite60(struct opl3_slot *slot, Bit8u data)
{
    slot->reg_ar = (data >> 4) & 0x0f;
    slot->reg_dr = data & 0x0f;
}

static void OPL3_SlotWrite80(struct opl3_slot *slot, Bit8u data)
{
    slot->reg_sl = (data >> 4) & 0x0f;
    if (slot->reg_sl == 0x0f)
    {
        slot->reg_sl = 0x1f;
    }
    slot->reg_rr = data & 0x0f;
}

static void OPL3_SlotWriteE0(struct opl3_slot *slot, Bit8u data)
{
    slot->reg_wf = data & 0x07;
    if (slot->chip->newm == 0x00)
    {
        slot->reg_wf &= 0x03;
    }
}

static void OPL3_SlotGenerate(struct opl3_slot *slot)
{
    slot->out = envelope_sin[slot->reg_wf](slot->pg_phase_out + *slot->mod, slot->eg_out);
}

static void OPL3_SlotCalcFB(struct opl3_slot *slot)
{
    if (slot->channel->fb != 0x00)
    {
        slot->fbmod = (slot->prout + slot->out) >> (0x09 - slot->channel->fb);
    }
    else
    {
        slot->fbmod = 0;
    }
    slot->prout = slot->out;
}

//
// Channel
//

static void OPL3_ChannelSetupAlg(struct opl3_channel *channel);

static void OPL3_ChannelUpdateRhythm(struct opl3_chip *chip, Bit8u data)
{
    struct opl3_channel *channel6;
    struct opl3_channel *channel7;
    struct opl3_channel *channel8;
    Bit8u chnum;

    chip->rhy = data & 0x3f;
    if (chip->rhy & 0x20)
    {
        channel6 = &chip->channel[6];
        channel7 = &chip->channel[7];
        channel8 = &chip->channel[8];
        channel6->out[0] = &channel6->slots[1]->out;
        channel6->out[1] = &channel6->slots[1]->out;
        channel6->out[2] = &chip->zeromod;
        channel6->out[3] = &chip->zeromod;
        channel7->out[0] = &channel7->slots[0]->out;
        channel7->out[1] = &channel7->slots[0]->out;
        channel7->out[2] = &channel7->slots[1]->out;
        channel7->out[3] = &channel7->slots[1]->out;
        channel8->out[0] = &channel8->slots[0]->out;
        channel8->out[1] = &channel8->slots[0]->out;
        channel8->out[2] = &channel8->slots[1]->out;
        channel8->out[3] = &channel8->slots[1]->out;
        for (chnum = 6; chnum < 9; chnum++)
        {
            chip->channel[chnum].chtype = ch_drum;
        }
        OPL3_ChannelSetupAlg(channel6);
        OPL3_ChannelSetupAlg(channel7);
        OPL3_ChannelSetupAlg(channel8);
        //hh
        if (chip->rhy & 0x01)
        {
            OPL3_EnvelopeKeyOn(channel7->slots[0], egk_drum);
        }
        else
        {
            OPL3_EnvelopeKeyOff(channel7->slots[0], egk_drum);
        }
        //tc
        if (chip->rhy & 0x02)
        {
            OPL3_EnvelopeKeyOn(channel8->slots[1], egk_drum);
        }
        else
        {
            OPL3_EnvelopeKeyOff(channel8->slots[1], egk_drum);
        }
        //tom
        if (chip->rhy & 0x04)
        {
            OPL3_EnvelopeKeyOn(channel8->slots[0], egk_drum);
        }
        else
        {
            OPL3_EnvelopeKeyOff(channel8->slots[0], egk_drum);
        }
        //sd
        if (chip->rhy & 0x08)
        {
            OPL3_EnvelopeKeyOn(channel7->slots[1], egk_drum);
        }
        else
        {
            OPL3_EnvelopeKeyOff(channel7->slots[1], egk_drum);
        }
        //bd
        if (chip->rhy & 0x10)
        {
            OPL3_EnvelopeKeyOn(channel6->slots[0], egk_drum);
            OPL3_EnvelopeKeyOn(channel6->slots[1], egk_drum);
        }
        else
        {
            OPL3_EnvelopeKeyOff(channel6->slots[0], egk_drum);
            OPL3_EnvelopeKeyOff(channel6->slots[1], egk_drum);
        }
    }
    else
    {
        for (chnum = 6; chnum < 9; chnum++)
        {
            chip->channel[chnum].chtype = ch_2op;
            OPL3_ChannelSetupAlg(&chip->channel[chnum]);
            OPL3_EnvelopeKeyOff(chip->channel[chnum].slots[0], egk_drum);
            OPL3_EnvelopeKeyOff(chip->channel[chnum].slots[1], egk_drum);
        }
    }
}

static void OPL3_ChannelWriteA0(struct opl3_channel *channel, Bit8u data)
{
    if (channel->chip->newm && channel->chtype == ch_4op2)
    {
        return;
    }
    channel->f_num = (channel->f_num & 0x300) | data;
    channel->ksv = (channel->block << 1)
                 | ((channel->f_num >> (0x09 - channel->chip->nts)) & 0x01);
    OPL3_EnvelopeUpdateKSL(channel->slots[0]);
    OPL3_EnvelopeUpdateKSL(channel->slots[1]);
    if (channel->chip->newm && channel->chtype == ch_4op)
    {
        channel->pair->f_num = channel->f_num;
        channel->pair->ksv = channel->ksv;
        OPL3_EnvelopeUpdateKSL(channel->pair->slots[0]);
        OPL3_EnvelopeUpdateKSL(channel->pair->slots[1]);
    }
}

static void OPL3_ChannelWriteB0(struct opl3_channel *channel, Bit8u data)
{
    if (channel->chip->newm && channel->chtype == ch_4op2)
    {
        return;
    }
    channel->f_num = (channel->f_num & 0xff) | ((data & 0x03) << 8);
    channel->block = (data >> 2) & 0x07;
    channel->ksv = (channel->block << 1)
                 | ((channel->f_num >> (0x09 - channel->chip->nts)) & 0x01);
    OPL3_EnvelopeUpdateKSL(channel->slots[0]);
    OPL3_EnvelopeUpdateKSL(channel->slots[1]);
    if (channel->chip->newm && channel->chtype == ch_4op)
    {
        channel->pair->f_num = channel->f_num;
        channel->pair->block = channel->block;
        channel->pair->ksv = channel->ksv;
        OPL3_EnvelopeUpdateKSL(channel->pair->slots[0]);
        OPL3_EnvelopeUpdateKSL(channel->pair->slots[1]);
    }
}

static void OPL3_ChannelSetupAlg(struct opl3_channel *channel)
{
    if (channel->chtype == ch_drum)
    {
        if (channel->ch_num == 7 || channel->ch_num == 8)
        {
            channel->slots[0]->mod = &channel->chip->zeromod;
            channel->slots[1]->mod = &channel->chip->zeromod;
            return;
        }
        switch (channel->alg & 0x01)
        {
        case 0x00:
            channel->slots[0]->mod = &channel->slots[0]->fbmod;
            channel->slots[1]->mod = &channel->slots[0]->out;
            break;
        case 0x01:
            channel->slots[0]->mod = &channel->slots[0]->fbmod;
            channel->slots[1]->mod = &channel->chip->zeromod;
            break;
        }
        return;
    }
    if (channel->alg & 0x08)
    {
        return;
    }
    if (channel->alg & 0x04)
    {
        channel->pair->out[0] = &channel->chip->zeromod;
        channel->pair->out[1] = &channel->chip->zeromod;
        channel->pair->out[2] = &channel->chip->zeromod;
        channel->pair->out[3] = &channel->chip->zeromod;
        switch (channel->alg & 0x03)
        {
        case 0x00:
            channel->pair->slots[0]->mod = &channel->pair->slots[0]->fbmod;
            channel->pair->slots[1]->mod = &channel->pair->slots[0]->out;
            channel->slots[0]->mod = &channel->pair->slots[1]->out;
            channel->slots[1]->mod = &channel->slots[0]->out;
            channel->out[0] = &channel->slots[1]->out;
            channel->out[1] = &channel->chip->zeromod;
            channel->out[2] = &channel->chip->zeromod;
            channel->out[3] = &channel->chip->zeromod;
            break;
        case 0x01:
            channel->pair->slots[0]->mod = &channel->pair->slots[0]->fbmod;
            channel->pair->slots[1]->mod = &channel->pair->slots[0]->out;
            channel->slots[0]->mod = &channel->chip->zeromod;
            channel->slots[1]->mod = &channel->slots[0]->out;
            channel->out[0] = &channel->pair->slots[1]->out;
            channel->out[1] = &channel->slots[1]->out;
            channel->out[2] = &channel->chip->zeromod;
            channel->out[3] = &channel->chip->zeromod;
            break;
        case 0x02:
            channel->pair->slots[0]->mod = &channel->pair->slots[0]->fbmod;
            channel->pair->slots[1]->mod = &channel->chip->zeromod;
            channel->slots[0]->mod = &channel->pair->slots[1]->out;
            channel->slots[1]->mod = &channel->slots[0]->out;
            channel->out[0] = &channel->pair->slots[0]->out;
            channel->out[1] = &channel->slots[1]->out;
            channel->out[2] = &channel->chip->zeromod;
            channel->out[3] = &channel->chip->zeromod;
            break;
        case 0x03:
            channel->pair->slots[0]->mod = &channel->pair->slots[0]->fbmod;
            channel->pair->slots[1]->mod = &channel->chip->zeromod;
            channel->slots[0]->mod = &channel->pair->slots[1]->out;
            channel->slots[1]->mod = &channel->chip->zeromod;
            channel->out[0] = &channel->pair->slots[0]->out;
            channel->out[1] = &channel->slots[0]->out;
            channel->out[2] = &channel->slots[1]->out;
            channel->out[3] = &channel->chip->zeromod;
            break;
        }
    }
    else
    {
        switch (channel->alg & 0x01)
        {
        case 0x00:
            channel->slots[0]->mod = &channel->slots[0]->fbmod;
            channel->slots[1]->mod = &channel->slots[0]->out;
            channel->out[0] = &channel->slots[1]->out;
            channel->out[1] = &channel->chip->zeromod;
            channel->out[2] = &channel->chip->zeromod;
            channel->out[3] = &channel->chip->zeromod;
            break;
        case 0x01:
            channel->slots[0]->mod = &channel->slots[0]->fbmod;
            channel->slots[1]->mod = &channel->chip->zeromod;
            channel->out[0] = &channel->slots[0]->out;
            channel->out[1] = &channel->slots[1]->out;
            channel->out[2] = &channel->chip->zeromod;
            channel->out[3] = &channel->chip->zeromod;
            break;
        }
    }
}

static void OPL3_ChannelWriteC0(struct opl3_channel *channel, Bit8u data)
{
    channel->fb = (data & 0x0e) >> 1;
    channel->con = data & 0x01;
    channel->alg = channel->con;
    if (channel->chip->newm)
    {
        if (channel->chtype == ch_4op)
        {
            channel->pair->alg = 0x04 | (channel->con << 1) | (channel->pair->con);
            channel->alg = 0x08;
            OPL3_ChannelSetupAlg(channel->pair);
        }
        else if (channel->chtype == ch_4op2)
        {
            channel->alg = 0x04 | (channel->pair->con << 1) | (channel->con);
            channel->pair->alg = 0x08;
            OPL3_ChannelSetupAlg(channel);
        }
        else
        {
            OPL3_ChannelSetupAlg(channel);
        }
    }
    else
    {
        OPL3_ChannelSetupAlg(channel);
    }
    if (channel->chip->newm)
    {
        channel->cha = ((data >> 4) & 0x01) ? ~0 : 0;
        channel->chb = ((data >> 5) & 0x01) ? ~0 : 0;
    }
    else
    {
        channel->cha = channel->chb = (Bit16u)~0;
    }
}

static void OPL3_ChannelKeyOn(struct opl3_channel *channel)
{
    if (channel->chip->newm)
    {
        if (channel->chtype == ch_4op)
        {
            OPL3_EnvelopeKeyOn(channel->slots[0], egk_norm);
            OPL3_EnvelopeKeyOn(channel->slots[1], egk_norm);
            OPL3_EnvelopeKeyOn(channel->pair->slots[0], egk_norm);
            OPL3_EnvelopeKeyOn(channel->pair->slots[1], egk_norm);
        }
        else if (channel->chtype == ch_2op || channel->chtype == ch_drum)
        {
            OPL3_EnvelopeKeyOn(channel->slots[0], egk_norm);
            OPL3_EnvelopeKeyOn(channel->slots[1], egk_norm);
        }
    }
    else
    {
        OPL3_EnvelopeKeyOn(channel->slots[0], egk_norm);
        OPL3_EnvelopeKeyOn(channel->slots[1], egk_norm);
    }
}

static void OPL3_ChannelKeyOff(struct opl3_channel *channel)
{
    if (channel->chip->newm)
    {
        if (channel->chtype == ch_4op)
        {
            OPL3_EnvelopeKeyOff(channel->slots[0], egk_norm);
            OPL3_EnvelopeKeyOff(channel->slots[1], egk_norm);
            OPL3_EnvelopeKeyOff(channel->pair->slots[0], egk_norm);
            OPL3_EnvelopeKeyOff(channel->pair->slots[1], egk_norm);
        }
        else if (channel->chtype == ch_2op || channel->chtype == ch_drum)
        {
            OPL3_EnvelopeKeyOff(channel->slots[0], egk_norm);
            OPL3_EnvelopeKeyOff(channel->slots[1], egk_norm);
        }
    }
    else
    {
        OPL3_EnvelopeKeyOff(channel->slots[0], egk_norm);
        OPL3_EnvelopeKeyOff(channel->slots[1], egk_norm);
    }
}

static void OPL3_ChannelSet4Op(struct opl3_chip *chip, Bit8u data)
{
    Bit8u bit;
    Bit8u chnum;
    for (bit = 0; bit < 6; bit++)
    {
        chnum = bit;
        if (bit >= 3)
        {
            chnum += 9 - 3;
        }
        if ((data >> bit) & 0x01)
        {
            chip->channel[chnum].chtype = ch_4op;
            chip->channel[chnum + 3].chtype = ch_4op2;
        }
        else
        {
            chip->channel[chnum].chtype = ch_2op;
            chip->channel[chnum + 3].chtype = ch_2op;
        }
    }
}

void OPL3_Generate(struct opl3_chip *chip, Bit32s *buf)
{
    Bit8u ii;
    Bit8u jj;
    Bit16s accm;
    Bit8u shift = 0;

    buf[1] = chip->mixbuff[1];

    for (ii = 0; ii < 15; ii++)
    {
        OPL3_SlotCalcFB(&chip->slot[ii]);
        OPL3_EnvelopeCalc(&chip->slot[ii]);
        OPL3_PhaseGenerate(&chip->slot[ii]);
        OPL3_SlotGenerate(&chip->slot[ii]);
    }

    chip->mixbuff[0] = 0;
    for (ii = 0; ii < 18; ii++)
    {
        accm = 0;
        for (jj = 0; jj < 4; jj++)
        {
            accm += *chip->channel[ii].out[jj];
        }
        chip->mixbuff[0] += (Bit16s)(accm & chip->channel[ii].cha);
    }

    for (ii = 15; ii < 18; ii++)
    {
        OPL3_SlotCalcFB(&chip->slot[ii]);
        OPL3_EnvelopeCalc(&chip->slot[ii]);
        OPL3_PhaseGenerate(&chip->slot[ii]);
        OPL3_SlotGenerate(&chip->slot[ii]);
    }

    buf[0] = chip->mixbuff[0];

    for (ii = 18; ii < 33; ii++)
    {
        OPL3_SlotCalcFB(&chip->slot[ii]);
        OPL3_EnvelopeCalc(&chip->slot[ii]);
        OPL3_PhaseGenerate(&chip->slot[ii]);
        OPL3_SlotGenerate(&chip->slot[ii]);
    }

    chip->mixbuff[1] = 0;
    for (ii = 0; ii < 18; ii++)
    {
        accm = 0;
        for (jj = 0; jj < 4; jj++)
        {
            accm += *chip->channel[ii].out[jj];
        }
        chip->mixbuff[1] += (Bit16s)(accm & chip->channel[ii].chb);
    }

    for (ii = 33; ii < 36; ii++)
    {
        OPL3_SlotCalcFB(&chip->slot[ii]);
        OPL3_EnvelopeCalc(&chip->slot[ii]);
        OPL3_PhaseGenerate(&chip->slot[ii]);
        OPL3_SlotGenerate(&chip->slot[ii]);
    }

    if ((chip->timer & 0x3f) == 0x3f)
    {
        chip->tremolopos = (chip->tremolopos + 1) % 210;
    }
    if (chip->tremolopos < 105)
    {
        chip->tremolo = chip->tremolopos >> chip->tremoloshift;
    }
    else
    {
        chip->tremolo = (210 - chip->tremolopos) >> chip->tremoloshift;
    }

    if ((chip->timer & 0x3ff) == 0x3ff)
    {
        chip->vibpos = (chip->vibpos + 1) & 7;
    }

    chip->timer++;

    chip->eg_add = 0;
    if (chip->eg_timer)
    {
        while (shift < 36 && ((chip->eg_timer >> shift) & 1) == 0)
        {
            shift++;
        }
        if (shift > 12)
        {
            chip->eg_add = 0;
        }
        else
        {
            chip->eg_add = shift + 1;
        }
    }

    if (chip->eg_timerrem || chip->eg_state)
    {
        if (chip->eg_timer == 0xfffffffff)
        {
            chip->eg_timer = 0;
            chip->eg_timerrem = 1;
        }
        else
        {
            chip->eg_timer++;
            chip->eg_timerrem = 0;
        }
    }

    chip->eg_state ^= 1;

    while (chip->writebuf[chip->writebuf_cur].time <= chip->writebuf_samplecnt)
    {
        if (!(chip->writebuf[chip->writebuf_cur].reg & 0x200))
        {
            break;
        }
        chip->writebuf[chip->writebuf_cur].reg &= 0x1ff;
        OPL3_WriteReg(chip, chip->writebuf[chip->writebuf_cur].reg,
                      chip->writebuf[chip->writebuf_cur].data);
        chip->writebuf_cur = (chip->writebuf_cur + 1) % OPL_WRITEBUF_SIZE;
    }
    chip->writebuf_samplecnt++;
}

void OPL3_GenerateResampled(struct opl3_chip *chip, Bit32s *buf)
{
    while (chip->samplecnt >= chip->rateratio)
    {
        chip->oldsamples[0] = chip->samples[0];
        chip->oldsamples[1] = chip->samples[1];
        OPL3_Generate(chip, chip->samples);
        chip->samplecnt -= chip->rateratio;
    }
    buf[0] = (Bit32s)((chip->oldsamples[0] * (chip->rateratio - chip->samplecnt)
                     + chip->samples[0] * chip->samplecnt) / chip->rateratio);
    buf[1] = (Bit32s)((chip->oldsamples[1] * (chip->rateratio - chip->samplecnt)
                     + chip->samples[1] * chip->samplecnt) / chip->rateratio);
    chip->samplecnt += 1 << RSM_FRAC;
}

void OPL3_Reset(struct opl3_chip *chip, Bit32u samplerate)
{
    Bit8u slotnum;
    Bit8u channum;

    memset(chip, 0, sizeof(struct opl3_chip));
    for (slotnum = 0; slotnum < 36; slotnum++)
    {
        chip->slot[slotnum].chip = chip;
        chip->slot[slotnum].mod = &chip->zeromod;
        chip->slot[slotnum].eg_rout = 0x1ff;
        chip->slot[slotnum].eg_out = 0x1ff;
        chip->slot[slotnum].eg_gen = envelope_gen_num_release;
        chip->slot[slotnum].trem = (Bit8u*)&chip->zeromod;
        chip->slot[slotnum].slot_num = slotnum;
    }
    for (channum = 0; channum < 18; channum++)
    {
        chip->channel[channum].slots[0] = &chip->slot[ch_slot[channum]];
        chip->channel[channum].slots[1] = &chip->slot[ch_slot[channum] + 3];
        chip->slot[ch_slot[channum]].channel = &chip->channel[channum];
        chip->slot[ch_slot[channum] + 3].channel = &chip->channel[channum];
        if ((channum % 9) < 3)
        {
            chip->channel[channum].pair = &chip->channel[channum + 3];
        }
        else if ((channum % 9) < 6)
        {
            chip->channel[channum].pair = &chip->channel[channum - 3];
        }
        chip->channel[channum].chip = chip;
        chip->channel[channum].out[0] = &chip->zeromod;
        chip->channel[channum].out[1] = &chip->zeromod;
        chip->channel[channum].out[2] = &chip->zeromod;
        chip->channel[channum].out[3] = &chip->zeromod;
        chip->channel[channum].chtype = ch_2op;
        chip->channel[channum].cha = 0xffff;
        chip->channel[channum].chb = 0xffff;
        chip->channel[channum].ch_num = channum;
        OPL3_ChannelSetupAlg(&chip->channel[channum]);
    }
    chip->noise = 1;
    chip->rateratio = (samplerate << RSM_FRAC) / 49716;
    chip->tremoloshift = 4;
    chip->vibshift = 1;
}

Bit32u OPL3_WriteAddr(struct opl3_chip *chip, Bit32u port, Bit8u val) 
{
			Bit16u addr;
			addr = val;
			if ((port & 2) && (addr == 0x05 || chip->newm)) {
				addr |= 0x100;
			}
			return addr;
}

void OPL3_WriteReg(struct opl3_chip *chip, Bit16u reg, Bit8u v)
{
    Bit8u high = (reg >> 8) & 0x01;
    Bit8u regm = reg & 0xff;
    switch (regm & 0xf0)
    {
    case 0x00:
        if (high)
        {
            switch (regm & 0x0f)
            {
            case 0x04:
                OPL3_ChannelSet4Op(chip, v);
                break;
            case 0x05:
                chip->newm = v & 0x01;
                break;
            }
        }
        else
        {
            switch (regm & 0x0f)
            {
            case 0x08:
                chip->nts = (v >> 6) & 0x01;
                break;
            }
        }
        break;
    case 0x20:
    case 0x30:
        if (ad_slot[regm & 0x1f] >= 0)
        {
            OPL3_SlotWrite20(&chip->slot[18 * high + ad_slot[regm & 0x1f]], v);
        }
        break;
    case 0x40:
    case 0x50:
        if (ad_slot[regm & 0x1f] >= 0)
        {
            OPL3_SlotWrite40(&chip->slot[18 * high + ad_slot[regm & 0x1f]], v);
        }
        break;
    case 0x60:
    case 0x70:
        if (ad_slot[regm & 0x1f] >= 0)
        {
            OPL3_SlotWrite60(&chip->slot[18 * high + ad_slot[regm & 0x1f]], v);
        }
        break;
    case 0x80:
    case 0x90:
        if (ad_slot[regm & 0x1f] >= 0)
        {
            OPL3_SlotWrite80(&chip->slot[18 * high + ad_slot[regm & 0x1f]], v);
        }
        break;
    case 0xe0:
    case 0xf0:
        if (ad_slot[regm & 0x1f] >= 0)
        {
            OPL3_SlotWriteE0(&chip->slot[18 * high + ad_slot[regm & 0x1f]], v);
        }
        break;
    case 0xa0:
        if ((regm & 0x0f) < 9)
        {
            OPL3_ChannelWriteA0(&chip->channel[9 * high + (regm & 0x0f)], v);
        }
        break;
    case 0xb0:
        if (regm == 0xbd && !high)
        {
            chip->tremoloshift = (((v >> 7) ^ 1) << 1) + 2;
            chip->vibshift = ((v >> 6) & 0x01) ^ 1;
            OPL3_ChannelUpdateRhythm(chip, v);
        }
        else if ((regm & 0x0f) < 9)
        {
            OPL3_ChannelWriteB0(&chip->channel[9 * high + (regm & 0x0f)], v);
            if (v & 0x20)
            {
                OPL3_ChannelKeyOn(&chip->channel[9 * high + (regm & 0x0f)]);
            }
            else
            {
                OPL3_ChannelKeyOff(&chip->channel[9 * high + (regm & 0x0f)]);
            }
        }
        break;
    case 0xc0:
        if ((regm & 0x0f) < 9)
        {
            OPL3_ChannelWriteC0(&chip->channel[9 * high + (regm & 0x0f)], v);
        }
        break;
    }
}

void OPL3_WriteRegBuffered(struct opl3_chip *chip, Bit16u reg, Bit8u v)
{
    Bit64u time1, time2;

    if (chip->writebuf[chip->writebuf_last].reg & 0x200)
    {
        OPL3_WriteReg(chip, chip->writebuf[chip->writebuf_last].reg & 0x1ff,
                      chip->writebuf[chip->writebuf_last].data);

        chip->writebuf_cur = (chip->writebuf_last + 1) % OPL_WRITEBUF_SIZE;
        chip->writebuf_samplecnt = chip->writebuf[chip->writebuf_last].time;
    }

    chip->writebuf[chip->writebuf_last].reg = reg | 0x200;
    chip->writebuf[chip->writebuf_last].data = v;
    time1 = chip->writebuf_lasttime + OPL_WRITEBUF_DELAY;
    time2 = chip->writebuf_samplecnt;

    if (time1 < time2)
    {
        time1 = time2;
    }

    chip->writebuf[chip->writebuf_last].time = time1;
    chip->writebuf_lasttime = time1;
    chip->writebuf_last = (chip->writebuf_last + 1) % OPL_WRITEBUF_SIZE;
}

void OPL3_GenerateStream(struct opl3_chip *chip, Bit32s *sndptr, Bit32u numsamples)
{
    Bit32u i;

    for(i = 0; i < numsamples; i++)
    {
        OPL3_GenerateResampled(chip, sndptr);
        sndptr += 2;
    }
}
