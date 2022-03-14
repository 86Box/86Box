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
 *          Version: 1.8.0
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
 *          Copyright 2013-2018 Alexey Khokholov (Nuke.YKT)
 */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <86box/86box.h>
#include <86box/snd_opl_nuked.h>
#include <86box/sound.h>
#include <86box/timer.h>

#define WRBUF_SIZE  1024
#define WRBUF_DELAY 1
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

enum envelope_gen_num {
    envelope_gen_num_attack  = 0,
    envelope_gen_num_decay   = 1,
    envelope_gen_num_sustain = 2,
    envelope_gen_num_release = 3
};

struct chan;
struct chip;

typedef struct slot {
    struct chan *chan;
    struct chip *dev;
    int16_t      out;
    int16_t      fbmod;
    int16_t     *mod;
    int16_t      prout;
    int16_t      eg_rout;
    int16_t      eg_out;
    uint8_t      eg_inc;
    uint8_t      eg_gen;
    uint8_t      eg_rate;
    uint8_t      eg_ksl;
    uint8_t     *trem;
    uint8_t      reg_vib;
    uint8_t      reg_type;
    uint8_t      reg_ksr;
    uint8_t      reg_mult;
    uint8_t      reg_ksl;
    uint8_t      reg_tl;
    uint8_t      reg_ar;
    uint8_t      reg_dr;
    uint8_t      reg_sl;
    uint8_t      reg_rr;
    uint8_t      reg_wf;
    uint8_t      key;
    uint32_t     pg_reset;
    uint32_t     pg_phase;
    uint16_t     pg_phase_out;
    uint8_t      slot_num;
} slot_t;

typedef struct chan {
    slot_t      *slots[2];
    struct chan *pair;
    struct chip *dev;
    int16_t     *out[4];
    uint8_t      chtype;
    uint16_t     f_num;
    uint8_t      block;
    uint8_t      fb;
    uint8_t      con;
    uint8_t      alg;
    uint8_t      ksv;
    uint16_t     cha,
        chb;
    uint8_t ch_num;
} chan_t;

typedef struct wrbuf {
    uint64_t time;
    uint16_t reg;
    uint8_t  data;
} wrbuf_t;

typedef struct chip {
    chan_t   chan[18];
    slot_t   slot[36];
    uint16_t timer;
    uint64_t eg_timer;
    uint8_t  eg_timerrem;
    uint8_t  eg_state;
    uint8_t  eg_add;
    uint8_t  newm;
    uint8_t  nts;
    uint8_t  rhy;
    uint8_t  vibpos;
    uint8_t  vibshift;
    uint8_t  tremolo;
    uint8_t  tremolopos;
    uint8_t  tremoloshift;
    uint32_t noise;
    int16_t  zeromod;
    int32_t  mixbuff[2];
    uint8_t  rm_hh_bit2;
    uint8_t  rm_hh_bit3;
    uint8_t  rm_hh_bit7;
    uint8_t  rm_hh_bit8;
    uint8_t  rm_tc_bit3;
    uint8_t  rm_tc_bit5;

    // OPL3L
    int32_t rateratio;
    int32_t samplecnt;
    int32_t oldsamples[2];
    int32_t samples[2];

    uint64_t wrbuf_samplecnt;
    uint32_t wrbuf_cur;
    uint32_t wrbuf_last;
    uint64_t wrbuf_lasttime;
    wrbuf_t  wrbuf[WRBUF_SIZE];
} nuked_t;

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
    {0,  0, 0, 0},
    { 1, 0, 0, 0},
    { 1, 0, 1, 0},
    { 1, 1, 1, 0}
};

// address decoding
static const int8_t ad_slot[0x20] = {
    0, 1, 2, 3, 4, 5, -1, -1, 6, 7, 8, 9, 10, 11, -1, -1,
    12, 13, 14, 15, 16, 17, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1
};

static const uint8_t ch_slot[18] = {
    0, 1, 2, 6, 7, 8, 12, 13, 14, 18, 19, 20, 24, 25, 26, 30, 31, 32
};

// Envelope generator
typedef int16_t (*env_sinfunc)(uint16_t phase, uint16_t envelope);
typedef void (*env_genfunc)(slot_t *slot);

static int16_t
env_calc_exp(uint32_t level)
{
    if (level > 0x1fff)
        level = 0x1fff;

    return ((exprom[level & 0xff] << 1) >> (level >> 8));
}

static int16_t
env_calc_sin0(uint16_t phase, uint16_t env)
{
    uint16_t out = 0;
    uint16_t neg = 0;

    phase &= 0x3ff;

    if (phase & 0x0200)
        neg = 0xffff;

    if (phase & 0x0100)
        out = logsinrom[(phase & 0xff) ^ 0xff];
    else
        out = logsinrom[phase & 0xff];

    return (env_calc_exp(out + (env << 3)) ^ neg);
}

static int16_t
env_calc_sin1(uint16_t phase, uint16_t env)
{
    uint16_t out = 0;

    phase &= 0x3ff;

    if (phase & 0x0200)
        out = 0x1000;
    else if (phase & 0x0100)
        out = logsinrom[(phase & 0xff) ^ 0xff];
    else
        out = logsinrom[phase & 0xff];

    return (env_calc_exp(out + (env << 3)));
}

static int16_t
env_calc_sin2(uint16_t phase, uint16_t env)
{
    uint16_t out = 0;

    phase &= 0x03ff;

    if (phase & 0x0100)
        out = logsinrom[(phase & 0xff) ^ 0xff];
    else
        out = logsinrom[phase & 0xff];

    return (env_calc_exp(out + (env << 3)));
}

static int16_t
env_calc_sin3(uint16_t phase, uint16_t env)
{
    uint16_t out = 0;

    phase &= 0x03ff;

    if (phase & 0x0100)
        out = 0x1000;
    else
        out = logsinrom[phase & 0xff];

    return (env_calc_exp(out + (env << 3)));
}

static int16_t
env_calc_sin4(uint16_t phase, uint16_t env)
{
    uint16_t out = 0;
    uint16_t neg = 0;

    phase &= 0x03ff;

    if ((phase & 0x0300) == 0x0100)
        neg = 0xffff;

    if (phase & 0x0200)
        out = 0x1000;
    else if (phase & 0x80)
        out = logsinrom[((phase ^ 0xff) << 1) & 0xff];
    else
        out = logsinrom[(phase << 1) & 0xff];

    return (env_calc_exp(out + (env << 3)) ^ neg);
}

static int16_t
env_calc_sin5(uint16_t phase, uint16_t env)
{
    uint16_t out = 0;

    phase &= 0x03ff;

    if (phase & 0x0200)
        out = 0x1000;
    else if (phase & 0x80)
        out = logsinrom[((phase ^ 0xff) << 1) & 0xff];
    else
        out = logsinrom[(phase << 1) & 0xff];

    return (env_calc_exp(out + (env << 3)));
}

static int16_t
env_calc_sin6(uint16_t phase, uint16_t env)
{
    uint16_t neg = 0;

    phase &= 0x03ff;

    if (phase & 0x0200)
        neg = 0xffff;

    return (env_calc_exp(env << 3) ^ neg);
}

static int16_t
env_calc_sin7(uint16_t phase, uint16_t env)
{
    uint16_t out = 0;
    uint16_t neg = 0;

    phase &= 0x03ff;

    if (phase & 0x0200) {
        neg   = 0xffff;
        phase = (phase & 0x01ff) ^ 0x01ff;
    }

    out = phase << 3;

    return (env_calc_exp(out + (env << 3)) ^ neg);
}

static const env_sinfunc env_sin[8] = {
    env_calc_sin0,
    env_calc_sin1,
    env_calc_sin2,
    env_calc_sin3,
    env_calc_sin4,
    env_calc_sin5,
    env_calc_sin6,
    env_calc_sin7
};

static void
env_update_ksl(slot_t *slot)
{
    int16_t ksl = (kslrom[slot->chan->f_num >> 6] << 2) - ((0x08 - slot->chan->block) << 5);

    if (ksl < 0)
        ksl = 0;

    slot->eg_ksl = (uint8_t) ksl;
}

static void
env_calc(slot_t *slot)
{
    uint8_t  nonzero;
    uint8_t  rate;
    uint8_t  rate_hi;
    uint8_t  rate_lo;
    uint8_t  reg_rate = 0;
    uint8_t  ks;
    uint8_t  eg_shift, shift;
    uint16_t eg_rout;
    int16_t  eg_inc;
    uint8_t  eg_off;
    uint8_t  reset = 0;

    slot->eg_out = slot->eg_rout + (slot->reg_tl << 2) + (slot->eg_ksl >> kslshift[slot->reg_ksl]) + *slot->trem;
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
        }

    slot->pg_reset = reset;
    ks             = slot->chan->ksv >> ((slot->reg_ksr ^ 1) << 1);
    nonzero        = (reg_rate != 0);
    rate           = ks + (reg_rate << 2);
    rate_hi        = rate >> 2;
    rate_lo        = rate & 0x03;
    if (rate_hi & 0x10)
        rate_hi = 0x0f;
    eg_shift = rate_hi + slot->dev->eg_add;
    shift    = 0;

    if (nonzero) {
        if (rate_hi < 12) {
            if (slot->dev->eg_state)
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
            shift = (rate_hi & 0x03) + eg_incstep[rate_lo][slot->dev->timer & 0x03];
            if (shift & 0x04)
                shift = 0x03;
            if (!shift)
                shift = slot->dev->eg_state;
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
                eg_inc = ((~slot->eg_rout) << shift) >> 4;
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
    }
    slot->eg_rout = (eg_rout + eg_inc) & 0x1ff;

    // Key off
    if (reset)
        slot->eg_gen = envelope_gen_num_attack;

    if (!slot->key)
        slot->eg_gen = envelope_gen_num_release;
}

static void
env_key_on(slot_t *slot, uint8_t type)
{
    slot->key |= type;
}

static void
env_key_off(slot_t *slot, uint8_t type)
{
    slot->key &= ~type;
}

static void
phase_generate(slot_t *slot)
{
    uint16_t f_num;
    uint32_t basefreq;
    uint8_t  rm_xor, n_bit;
    uint32_t noise;
    uint16_t phase;
    int8_t   range;
    uint8_t  vibpos;
    nuked_t *dev;

    dev   = slot->dev;
    f_num = slot->chan->f_num;
    if (slot->reg_vib) {
        range  = (f_num >> 7) & 7;
        vibpos = dev->vibpos;

        if (!(vibpos & 3))
            range = 0;
        else if (vibpos & 1)
            range >>= 1;
        range >>= dev->vibshift;

        if (vibpos & 4)
            range = -range;
        f_num += range;
    }

    basefreq = (f_num << slot->chan->block) >> 1;
    phase    = (uint16_t) (slot->pg_phase >> 9);

    if (slot->pg_reset)
        slot->pg_phase = 0;
    slot->pg_phase += (basefreq * mt[slot->reg_mult]) >> 1;

    // Rhythm mode
    noise              = dev->noise;
    slot->pg_phase_out = phase;
    if (slot->slot_num == 13) { // hh
        dev->rm_hh_bit2 = (phase >> 2) & 1;
        dev->rm_hh_bit3 = (phase >> 3) & 1;
        dev->rm_hh_bit7 = (phase >> 7) & 1;
        dev->rm_hh_bit8 = (phase >> 8) & 1;
    }
    if (slot->slot_num == 17 && (dev->rhy & 0x20)) { // tc
        dev->rm_tc_bit3 = (phase >> 3) & 1;
        dev->rm_tc_bit5 = (phase >> 5) & 1;
    }
    if (dev->rhy & 0x20) {
        rm_xor = (dev->rm_hh_bit2 ^ dev->rm_hh_bit7) | (dev->rm_hh_bit3 ^ dev->rm_tc_bit5) | (dev->rm_tc_bit3 ^ dev->rm_tc_bit5);

        switch (slot->slot_num) {
            case 13: // hh
                slot->pg_phase_out = rm_xor << 9;
                if (rm_xor ^ (noise & 1))
                    slot->pg_phase_out |= 0xd0;
                else
                    slot->pg_phase_out |= 0x34;
                break;

            case 16: // sd
                slot->pg_phase_out = (dev->rm_hh_bit8 << 9) | ((dev->rm_hh_bit8 ^ (noise & 1)) << 8);
                break;

            case 17: // tc
                slot->pg_phase_out = (rm_xor << 9) | 0x80;
                break;

            default:
                break;
        }
    }

    n_bit = ((noise >> 14) ^ noise) & 0x01;

    dev->noise = (noise >> 1) | (n_bit << 22);
}

static void
slot_write_20(slot_t *slot, uint8_t data)
{
    if ((data >> 7) & 0x01)
        slot->trem = &slot->dev->tremolo;
    else
        slot->trem = (uint8_t *) &slot->dev->zeromod;

    slot->reg_vib  = (data >> 6) & 0x01;
    slot->reg_type = (data >> 5) & 0x01;
    slot->reg_ksr  = (data >> 4) & 0x01;
    slot->reg_mult = data & 0x0f;
}

static void
slot_write_40(slot_t *slot, uint8_t data)
{
    slot->reg_ksl = (data >> 6) & 0x03;
    slot->reg_tl  = data & 0x3f;

    env_update_ksl(slot);
}

static void
slot_write_60(slot_t *slot, uint8_t data)
{
    slot->reg_ar = (data >> 4) & 0x0f;
    slot->reg_dr = data & 0x0f;
}

static void
slot_write_80(slot_t *slot, uint8_t data)
{
    slot->reg_sl = (data >> 4) & 0x0f;

    if (slot->reg_sl == 0x0f)
        slot->reg_sl = 0x1f;

    slot->reg_rr = data & 0x0f;
}

static void
slot_write_e0(slot_t *slot, uint8_t data)
{
    slot->reg_wf = data & 0x07;

    if (slot->dev->newm == 0x00)
        slot->reg_wf &= 0x03;
}

static void
slot_generate(slot_t *slot)
{
    slot->out = env_sin[slot->reg_wf](slot->pg_phase_out + *slot->mod,
                                      slot->eg_out);
}

static void
slot_calc_fb(slot_t *slot)
{
    if (slot->chan->fb != 0x00)
        slot->fbmod = (slot->prout + slot->out) >> (0x09 - slot->chan->fb);
    else
        slot->fbmod = 0;

    slot->prout = slot->out;
}

static void
channel_setup_alg(chan_t *ch)
{
    if (ch->chtype == ch_drum) {
        if (ch->ch_num == 7 || ch->ch_num == 8) {
            ch->slots[0]->mod = &ch->dev->zeromod;
            ch->slots[1]->mod = &ch->dev->zeromod;
            return;
        }

        switch (ch->alg & 0x01) {
            case 0x00:
                ch->slots[0]->mod = &ch->slots[0]->fbmod;
                ch->slots[1]->mod = &ch->slots[0]->out;
                break;

            case 0x01:
                ch->slots[0]->mod = &ch->slots[0]->fbmod;
                ch->slots[1]->mod = &ch->dev->zeromod;
                break;
        }
        return;
    }

    if (ch->alg & 0x08)
        return;

    if (ch->alg & 0x04) {
        ch->pair->out[0] = &ch->dev->zeromod;
        ch->pair->out[1] = &ch->dev->zeromod;
        ch->pair->out[2] = &ch->dev->zeromod;
        ch->pair->out[3] = &ch->dev->zeromod;

        switch (ch->alg & 0x03) {
            case 0x00:
                ch->pair->slots[0]->mod = &ch->pair->slots[0]->fbmod;
                ch->pair->slots[1]->mod = &ch->pair->slots[0]->out;
                ch->slots[0]->mod       = &ch->pair->slots[1]->out;
                ch->slots[1]->mod       = &ch->slots[0]->out;
                ch->out[0]              = &ch->slots[1]->out;
                ch->out[1]              = &ch->dev->zeromod;
                ch->out[2]              = &ch->dev->zeromod;
                ch->out[3]              = &ch->dev->zeromod;
                break;

            case 0x01:
                ch->pair->slots[0]->mod = &ch->pair->slots[0]->fbmod;
                ch->pair->slots[1]->mod = &ch->pair->slots[0]->out;
                ch->slots[0]->mod       = &ch->dev->zeromod;
                ch->slots[1]->mod       = &ch->slots[0]->out;
                ch->out[0]              = &ch->pair->slots[1]->out;
                ch->out[1]              = &ch->slots[1]->out;
                ch->out[2]              = &ch->dev->zeromod;
                ch->out[3]              = &ch->dev->zeromod;
                break;

            case 0x02:
                ch->pair->slots[0]->mod = &ch->pair->slots[0]->fbmod;
                ch->pair->slots[1]->mod = &ch->dev->zeromod;
                ch->slots[0]->mod       = &ch->pair->slots[1]->out;
                ch->slots[1]->mod       = &ch->slots[0]->out;
                ch->out[0]              = &ch->pair->slots[0]->out;
                ch->out[1]              = &ch->slots[1]->out;
                ch->out[2]              = &ch->dev->zeromod;
                ch->out[3]              = &ch->dev->zeromod;
                break;

            case 0x03:
                ch->pair->slots[0]->mod = &ch->pair->slots[0]->fbmod;
                ch->pair->slots[1]->mod = &ch->dev->zeromod;
                ch->slots[0]->mod       = &ch->pair->slots[1]->out;
                ch->slots[1]->mod       = &ch->dev->zeromod;
                ch->out[0]              = &ch->pair->slots[0]->out;
                ch->out[1]              = &ch->slots[0]->out;
                ch->out[2]              = &ch->slots[1]->out;
                ch->out[3]              = &ch->dev->zeromod;
                break;
        }
    } else
        switch (ch->alg & 0x01) {
            case 0x00:
                ch->slots[0]->mod = &ch->slots[0]->fbmod;
                ch->slots[1]->mod = &ch->slots[0]->out;
                ch->out[0]        = &ch->slots[1]->out;
                ch->out[1]        = &ch->dev->zeromod;
                ch->out[2]        = &ch->dev->zeromod;
                ch->out[3]        = &ch->dev->zeromod;
                break;

            case 0x01:
                ch->slots[0]->mod = &ch->slots[0]->fbmod;
                ch->slots[1]->mod = &ch->dev->zeromod;
                ch->out[0]        = &ch->slots[0]->out;
                ch->out[1]        = &ch->slots[1]->out;
                ch->out[2]        = &ch->dev->zeromod;
                ch->out[3]        = &ch->dev->zeromod;
                break;
        }
}

static void
channel_update_rhythm(nuked_t *dev, uint8_t data)
{
    chan_t *ch6, *ch7, *ch8;
    uint8_t chnum;

    dev->rhy = data & 0x3f;
    if (dev->rhy & 0x20) {
        ch6         = &dev->chan[6];
        ch7         = &dev->chan[7];
        ch8         = &dev->chan[8];
        ch6->out[0] = &ch6->slots[1]->out;
        ch6->out[1] = &ch6->slots[1]->out;
        ch6->out[2] = &dev->zeromod;
        ch6->out[3] = &dev->zeromod;
        ch7->out[0] = &ch7->slots[0]->out;
        ch7->out[1] = &ch7->slots[0]->out;
        ch7->out[2] = &ch7->slots[1]->out;
        ch7->out[3] = &ch7->slots[1]->out;
        ch8->out[0] = &ch8->slots[0]->out;
        ch8->out[1] = &ch8->slots[0]->out;
        ch8->out[2] = &ch8->slots[1]->out;
        ch8->out[3] = &ch8->slots[1]->out;

        for (chnum = 6; chnum < 9; chnum++)
            dev->chan[chnum].chtype = ch_drum;

        channel_setup_alg(ch6);
        channel_setup_alg(ch7);
        channel_setup_alg(ch8);

        // hh
        if (dev->rhy & 0x01)
            env_key_on(ch7->slots[0], egk_drum);
        else
            env_key_off(ch7->slots[0], egk_drum);

        // tc
        if (dev->rhy & 0x02)
            env_key_on(ch8->slots[1], egk_drum);
        else
            env_key_off(ch8->slots[1], egk_drum);

        // tom
        if (dev->rhy & 0x04)
            env_key_on(ch8->slots[0], egk_drum);
        else
            env_key_off(ch8->slots[0], egk_drum);

        // sd
        if (dev->rhy & 0x08)
            env_key_on(ch7->slots[1], egk_drum);
        else
            env_key_off(ch7->slots[1], egk_drum);

        // bd
        if (dev->rhy & 0x10) {
            env_key_on(ch6->slots[0], egk_drum);
            env_key_on(ch6->slots[1], egk_drum);
        } else {
            env_key_off(ch6->slots[0], egk_drum);
            env_key_off(ch6->slots[1], egk_drum);
        }
    } else {
        for (chnum = 6; chnum < 9; chnum++) {
            dev->chan[chnum].chtype = ch_2op;

            channel_setup_alg(&dev->chan[chnum]);
            env_key_off(dev->chan[chnum].slots[0], egk_drum);
            env_key_off(dev->chan[chnum].slots[1], egk_drum);
        }
    }
}

static void
channel_write_a0(chan_t *ch, uint8_t data)
{
    if (ch->dev->newm && ch->chtype == ch_4op2)
        return;

    ch->f_num = (ch->f_num & 0x300) | data;
    ch->ksv   = (ch->block << 1) | ((ch->f_num >> (0x09 - ch->dev->nts)) & 0x01);

    env_update_ksl(ch->slots[0]);
    env_update_ksl(ch->slots[1]);

    if (ch->dev->newm && ch->chtype == ch_4op) {
        ch->pair->f_num = ch->f_num;
        ch->pair->ksv   = ch->ksv;

        env_update_ksl(ch->pair->slots[0]);
        env_update_ksl(ch->pair->slots[1]);
    }
}

static void
channel_write_b0(chan_t *ch, uint8_t data)
{
    if (ch->dev->newm && ch->chtype == ch_4op2)
        return;

    ch->f_num = (ch->f_num & 0xff) | ((data & 0x03) << 8);
    ch->block = (data >> 2) & 0x07;
    ch->ksv   = (ch->block << 1) | ((ch->f_num >> (0x09 - ch->dev->nts)) & 0x01);

    env_update_ksl(ch->slots[0]);
    env_update_ksl(ch->slots[1]);

    if (ch->dev->newm && ch->chtype == ch_4op) {
        ch->pair->f_num = ch->f_num;
        ch->pair->block = ch->block;
        ch->pair->ksv   = ch->ksv;

        env_update_ksl(ch->pair->slots[0]);
        env_update_ksl(ch->pair->slots[1]);
    }
}

static void
channel_write_c0(chan_t *ch, uint8_t data)
{
    ch->fb  = (data & 0x0e) >> 1;
    ch->con = data & 0x01;
    ch->alg = ch->con;

    if (ch->dev->newm) {
        if (ch->chtype == ch_4op) {
            ch->pair->alg = 0x04 | (ch->con << 1) | ch->pair->con;
            ch->alg       = 0x08;
            channel_setup_alg(ch->pair);
        } else if (ch->chtype == ch_4op2) {
            ch->alg       = 0x04 | (ch->pair->con << 1) | ch->con;
            ch->pair->alg = 0x08;
            channel_setup_alg(ch);
        } else
            channel_setup_alg(ch);
    } else
        channel_setup_alg(ch);

    if (ch->dev->newm) {
        ch->cha = ((data >> 4) & 0x01) ? ~0 : 0;
        ch->chb = ((data >> 5) & 0x01) ? ~0 : 0;
    } else
        ch->cha = ch->chb = (uint16_t) ~0;
}

static void
channel_key_on(chan_t *ch)
{
    if (ch->dev->newm) {
        if (ch->chtype == ch_4op) {
            env_key_on(ch->slots[0], egk_norm);
            env_key_on(ch->slots[1], egk_norm);
            env_key_on(ch->pair->slots[0], egk_norm);
            env_key_on(ch->pair->slots[1], egk_norm);
        } else if (ch->chtype == ch_2op || ch->chtype == ch_drum) {
            env_key_on(ch->slots[0], egk_norm);
            env_key_on(ch->slots[1], egk_norm);
        }
    } else {
        env_key_on(ch->slots[0], egk_norm);
        env_key_on(ch->slots[1], egk_norm);
    }
}

static void
channel_key_off(chan_t *ch)
{
    if (ch->dev->newm) {
        if (ch->chtype == ch_4op) {
            env_key_off(ch->slots[0], egk_norm);
            env_key_off(ch->slots[1], egk_norm);
            env_key_off(ch->pair->slots[0], egk_norm);
            env_key_off(ch->pair->slots[1], egk_norm);
        } else if (ch->chtype == ch_2op || ch->chtype == ch_drum) {
            env_key_off(ch->slots[0], egk_norm);
            env_key_off(ch->slots[1], egk_norm);
        }
    } else {
        env_key_off(ch->slots[0], egk_norm);
        env_key_off(ch->slots[1], egk_norm);
    }
}

static void
channel_set_4op(nuked_t *dev, uint8_t data)
{
    uint8_t chnum;
    uint8_t bit;

    for (bit = 0; bit < 6; bit++) {
        chnum = bit;

        if (bit >= 3)
            chnum += 9 - 3;

        if ((data >> bit) & 0x01) {
            dev->chan[chnum].chtype     = ch_4op;
            dev->chan[chnum + 3].chtype = ch_4op2;
        } else {
            dev->chan[chnum].chtype     = ch_2op;
            dev->chan[chnum + 3].chtype = ch_2op;
        }
    }
}

uint16_t
nuked_write_addr(void *priv, uint16_t port, uint8_t val)
{
    nuked_t *dev = (nuked_t *) priv;
    uint16_t addr;

    addr = val;
    if ((port & 0x0002) && ((addr == 0x0005) || dev->newm))
        addr |= 0x0100;

    return (addr);
}

void
nuked_write_reg(void *priv, uint16_t reg, uint8_t val)
{
    nuked_t *dev  = (nuked_t *) priv;
    uint8_t  high = (reg >> 8) & 0x01;
    uint8_t  regm = reg & 0xff;

    switch (regm & 0xf0) {
        case 0x00:
            if (high)
                switch (regm & 0x0f) {
                    case 0x04:
                        channel_set_4op(dev, val);
                        break;

                    case 0x05:
                        dev->newm = val & 0x01;
                        break;
                }
            else
                switch (regm & 0x0f) {
                    case 0x08:
                        dev->nts = (val >> 6) & 0x01;
                        break;
                }
            break;

        case 0x20:
        case 0x30:
            if (ad_slot[regm & 0x1f] >= 0)
                slot_write_20(&dev->slot[18 * high + ad_slot[regm & 0x1f]], val);
            break;

        case 0x40:
        case 0x50:
            if (ad_slot[regm & 0x1f] >= 0)
                slot_write_40(&dev->slot[18 * high + ad_slot[regm & 0x1f]], val);
            break;

        case 0x60:
        case 0x70:
            if (ad_slot[regm & 0x1f] >= 0)
                slot_write_60(&dev->slot[18 * high + ad_slot[regm & 0x1f]], val);
            break;

        case 0x80:
        case 0x90:
            if (ad_slot[regm & 0x1f] >= 0)
                slot_write_80(&dev->slot[18 * high + ad_slot[regm & 0x1f]], val);
            break;

        case 0xa0:
            if ((regm & 0x0f) < 9)
                channel_write_a0(&dev->chan[9 * high + (regm & 0x0f)], val);
            break;

        case 0xb0:
            if (regm == 0xbd && !high) {
                dev->tremoloshift = (((val >> 7) ^ 1) << 1) + 2;
                dev->vibshift     = ((val >> 6) & 0x01) ^ 1;
                channel_update_rhythm(dev, val);
            } else if ((regm & 0x0f) < 9) {
                channel_write_b0(&dev->chan[9 * high + (regm & 0x0f)], val);

                if (val & 0x20)
                    channel_key_on(&dev->chan[9 * high + (regm & 0x0f)]);
                else
                    channel_key_off(&dev->chan[9 * high + (regm & 0x0f)]);
            }
            break;

        case 0xc0:
            if ((regm & 0x0f) < 9)
                channel_write_c0(&dev->chan[9 * high + (regm & 0x0f)], val);
            break;

        case 0xe0:
        case 0xf0:
            if (ad_slot[regm & 0x1f] >= 0)
                slot_write_e0(&dev->slot[18 * high + ad_slot[regm & 0x1f]], val);
            break;
    }
}

void
nuked_write_reg_buffered(void *priv, uint16_t reg, uint8_t val)
{
    nuked_t *dev = (nuked_t *) priv;
    uint64_t time1, time2;

    if (dev->wrbuf[dev->wrbuf_last].reg & 0x0200) {
        nuked_write_reg(dev, dev->wrbuf[dev->wrbuf_last].reg & 0x01ff,
                        dev->wrbuf[dev->wrbuf_last].data);

        dev->wrbuf_cur       = (dev->wrbuf_last + 1) % WRBUF_SIZE;
        dev->wrbuf_samplecnt = dev->wrbuf[dev->wrbuf_last].time;
    }

    dev->wrbuf[dev->wrbuf_last].reg  = reg | 0x0200;
    dev->wrbuf[dev->wrbuf_last].data = val;
    time1                            = dev->wrbuf_lasttime + WRBUF_DELAY;
    time2                            = dev->wrbuf_samplecnt;

    if (time1 < time2)
        time1 = time2;

    dev->wrbuf[dev->wrbuf_last].time = time1;
    dev->wrbuf_lasttime              = time1;
    dev->wrbuf_last                  = (dev->wrbuf_last + 1) % WRBUF_SIZE;
}

void
nuked_generate(void *priv, int32_t *bufp)
{
    nuked_t *dev = (nuked_t *) priv;
    int16_t  accm, shift = 0;
    uint8_t  i, j;

    bufp[1] = dev->mixbuff[1];

    for (i = 0; i < 15; i++) {
        slot_calc_fb(&dev->slot[i]);
        env_calc(&dev->slot[i]);
        phase_generate(&dev->slot[i]);
        slot_generate(&dev->slot[i]);
    }

    dev->mixbuff[0] = 0;

    for (i = 0; i < 18; i++) {
        accm = 0;

        for (j = 0; j < 4; j++)
            accm += *dev->chan[i].out[j];

        dev->mixbuff[0] += (int16_t) (accm & dev->chan[i].cha);
    }
    for (i = 15; i < 18; i++) {
        slot_calc_fb(&dev->slot[i]);
        env_calc(&dev->slot[i]);
        phase_generate(&dev->slot[i]);
        slot_generate(&dev->slot[i]);
    }

    bufp[0] = dev->mixbuff[0];

    for (i = 18; i < 33; i++) {
        slot_calc_fb(&dev->slot[i]);
        env_calc(&dev->slot[i]);
        phase_generate(&dev->slot[i]);
        slot_generate(&dev->slot[i]);
    }

    dev->mixbuff[1] = 0;

    for (i = 0; i < 18; i++) {
        accm = 0;

        for (j = 0; j < 4; j++)
            accm += *dev->chan[i].out[j];

        dev->mixbuff[1] += (int16_t) (accm & dev->chan[i].chb);
    }

    for (i = 33; i < 36; i++) {
        slot_calc_fb(&dev->slot[i]);
        env_calc(&dev->slot[i]);
        phase_generate(&dev->slot[i]);
        slot_generate(&dev->slot[i]);
    }

    if ((dev->timer & 0x3f) == 0x3f)
        dev->tremolopos = (dev->tremolopos + 1) % 210;

    if (dev->tremolopos < 105)
        dev->tremolo = dev->tremolopos >> dev->tremoloshift;
    else
        dev->tremolo = (210 - dev->tremolopos) >> dev->tremoloshift;

    if ((dev->timer & 0x03ff) == 0x03ff)
        dev->vibpos = (dev->vibpos + 1) & 7;

    dev->timer++;
    dev->eg_add = 0;

    if (dev->eg_timer) {
        while (shift < 36 && ((dev->eg_timer >> shift) & 1) == 0)
            shift++;

        if (shift > 12)
            dev->eg_add = 0;
        else
            dev->eg_add = shift + 1;
    }

    if (dev->eg_timerrem || dev->eg_state) {
        if (dev->eg_timer == 0xfffffffff) {
            dev->eg_timer    = 0;
            dev->eg_timerrem = 1;
        } else {
            dev->eg_timer++;
            dev->eg_timerrem = 0;
        }
    }

    dev->eg_state ^= 1;

    while (dev->wrbuf[dev->wrbuf_cur].time <= dev->wrbuf_samplecnt) {
        if (!(dev->wrbuf[dev->wrbuf_cur].reg & 0x200))
            break;

        dev->wrbuf[dev->wrbuf_cur].reg &= 0x01ff;

        nuked_write_reg(dev, dev->wrbuf[dev->wrbuf_cur].reg,
                        dev->wrbuf[dev->wrbuf_cur].data);

        dev->wrbuf_cur = (dev->wrbuf_cur + 1) % WRBUF_SIZE;
    }

    dev->wrbuf_samplecnt++;
}

void
nuked_generate_resampled(void *priv, int32_t *bufp)
{
    nuked_t *dev = (nuked_t *) priv;

    while (dev->samplecnt >= dev->rateratio) {
        dev->oldsamples[0] = dev->samples[0];
        dev->oldsamples[1] = dev->samples[1];
        nuked_generate(dev, dev->samples);
        dev->samplecnt -= dev->rateratio;
    }

    bufp[0] = (int32_t) ((dev->oldsamples[0] * (dev->rateratio - dev->samplecnt)
                          + dev->samples[0] * dev->samplecnt)
                         / dev->rateratio);
    bufp[1] = (int32_t) ((dev->oldsamples[1] * (dev->rateratio - dev->samplecnt)
                          + dev->samples[1] * dev->samplecnt)
                         / dev->rateratio);

    dev->samplecnt += 1 << RSM_FRAC;
}

void
nuked_generate_stream(void *priv, int32_t *sndptr, uint32_t num)
{
    nuked_t *dev = (nuked_t *) priv;
    uint32_t i;

    for (i = 0; i < num; i++) {
        nuked_generate_resampled(dev, sndptr);
        sndptr += 2;
    }
}

void *
nuked_init(uint32_t samplerate)
{
    nuked_t *dev;
    uint8_t  i;

    dev = (nuked_t *) malloc(sizeof(nuked_t));
    memset(dev, 0x00, sizeof(nuked_t));

    for (i = 0; i < 36; i++) {
        dev->slot[i].dev      = dev;
        dev->slot[i].mod      = &dev->zeromod;
        dev->slot[i].eg_rout  = 0x01ff;
        dev->slot[i].eg_out   = 0x01ff;
        dev->slot[i].eg_gen   = envelope_gen_num_release;
        dev->slot[i].trem     = (uint8_t *) &dev->zeromod;
        dev->slot[i].slot_num = i;
    }

    for (i = 0; i < 18; i++) {
        dev->chan[i].slots[0]          = &dev->slot[ch_slot[i]];
        dev->chan[i].slots[1]          = &dev->slot[ch_slot[i] + 3];
        dev->slot[ch_slot[i]].chan     = &dev->chan[i];
        dev->slot[ch_slot[i] + 3].chan = &dev->chan[i];

        if ((i % 9) < 3)
            dev->chan[i].pair = &dev->chan[i + 3];
        else if ((i % 9) < 6)
            dev->chan[i].pair = &dev->chan[i - 3];

        dev->chan[i].dev    = dev;
        dev->chan[i].out[0] = &dev->zeromod;
        dev->chan[i].out[1] = &dev->zeromod;
        dev->chan[i].out[2] = &dev->zeromod;
        dev->chan[i].out[3] = &dev->zeromod;
        dev->chan[i].chtype = ch_2op;
        dev->chan[i].cha    = 0xffff;
        dev->chan[i].chb    = 0xffff;
        dev->chan[i].ch_num = i;

        channel_setup_alg(&dev->chan[i]);
    }

    dev->noise        = 1;
    dev->rateratio    = (samplerate << RSM_FRAC) / 49716;
    dev->tremoloshift = 4;
    dev->vibshift     = 1;

    return (dev);
}

void
nuked_close(void *priv)
{
    nuked_t *dev = (nuked_t *) priv;

    free(dev);
}
