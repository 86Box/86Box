/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          Nuked CQM emulator.
 *
 *          Nuked CQM is free software: you can redistribute it and/or modify
 *          it under the terms of the GNU Lesser General Public License as
 *          published by the Free Software Foundation, either version 2.1
 *          of the License, or (at your option) any later version.
 *
 *          Nuked CQM is distributed in the hope that it will be useful,
 *          but WITHOUT ANY WARRANTY; without even the implied warranty of
 *          MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *          GNU Lesser General Public License for more details.
 *
 *          You should have received a copy of the GNU Lesser General Public License
 *          along with Nuked CQM. If not, see <https://www.gnu.org/licenses/>.
 *
 *          Nuked CQM emulator.
 *          Thanks:
 *              Wohlstand:
 *                  Sending CQM chips
 *              org(ogamespec):
 *                  Deroute tool
 *
 *          version: 0.9 beta
 *
 * Authors: Alexey Khokholov (Nuke.YKT),
 *          Miran Grca, <mgrca8@gmail.com>
 *
 *          Copyright 2026 Alexey Khokholov (Nuke.YKT)
 *          Copyright 2026 Miran Grca.
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
#include <86box/video.h>
#include <86box/snd_opl.h>
#include <86box/snd_cqm_nuked.h>

#define RSM_FRAC    10

static const uint8_t regaccess[48][9] = {
    {0x00, 0xc0, 0xa0, 0xb0, 0x20, 0x00, 0x40, 0x60, 0x80},
    {0x10, 0xc0, 0xa0, 0xb0, 0x30, 0x10, 0x50, 0x70, 0x90},
    {0x06, 0xc6, 0xa0, 0xb0, 0x26, 0x06, 0x46, 0x66, 0x86},
    {0x16, 0xc6, 0xa0, 0xb0, 0x36, 0x16, 0x56, 0x76, 0x96},

    {0x02, 0xc2, 0xa2, 0xb2, 0x22, 0x02, 0x42, 0x62, 0x82},
    {0x12, 0xc2, 0xa2, 0xb2, 0x32, 0x12, 0x52, 0x72, 0x92},
    {0x08, 0xc8, 0xa2, 0xb2, 0x28, 0x08, 0x48, 0x68, 0x88},
    {0x18, 0xc8, 0xa2, 0xb2, 0x38, 0x18, 0x58, 0x78, 0x98},

    {0x04, 0xc4, 0xa4, 0xb4, 0x24, 0x04, 0x44, 0x64, 0x84},
    {0x14, 0xc4, 0xa4, 0xb4, 0x34, 0x14, 0x54, 0x74, 0x94},
    {0x0a, 0xca, 0xa4, 0xb4, 0x2a, 0x0a, 0x4a, 0x6a, 0x8a},
    {0x1a, 0xca, 0xa4, 0xb4, 0x3a, 0x1a, 0x5a, 0x7a, 0x9a},

    {0x06, 0xc6, 0xa6, 0xb6, 0x26, 0x06, 0x46, 0x66, 0x86},
    {0x16, 0xc6, 0xa6, 0xb6, 0x36, 0x16, 0x56, 0x76, 0x96},

    {0x08, 0xc8, 0xa8, 0xb8, 0x28, 0x08, 0x48, 0x68, 0x88},
    {0x18, 0xc8, 0xa8, 0xb8, 0x38, 0x18, 0x58, 0x78, 0x98},

    {0x0a, 0xca, 0xaa, 0xba, 0x2a, 0x0a, 0x4a, 0x6a, 0x8a},
    {0x1a, 0xca, 0xaa, 0xba, 0x3a, 0x1a, 0x5a, 0x7a, 0x9a},

    {0x0c, 0xcc, 0xac, 0xbc, 0x2c, 0x0c, 0x4c, 0x6c, 0x8c},
    {0x1c, 0xcc, 0xac, 0xbc, 0x3c, 0x1c, 0x5c, 0x7c, 0x9c},

    {0x0e, 0xce, 0xae, 0xbe, 0x2e, 0x0e, 0x4e, 0x6e, 0x8e},
    {0x1e, 0xce, 0xae, 0xbe, 0x3e, 0x1e, 0x5e, 0x7e, 0x9e},

    {0xd0, 0xe8, 0xe4, 0xe6, 0xd4, 0xd0, 0xd8, 0xdc, 0xe0},
    {0xd2, 0xe8, 0xe4, 0xe6, 0xd6, 0xd2, 0xda, 0xde, 0xe2},

    {0x01, 0xc1, 0xa1, 0xb1, 0x21, 0x01, 0x41, 0x61, 0x81},
    {0x11, 0xc1, 0xa1, 0xb1, 0x31, 0x11, 0x51, 0x71, 0x91},
    {0x07, 0xc7, 0xa1, 0xb1, 0x27, 0x07, 0x47, 0x67, 0x87},
    {0x17, 0xc7, 0xa1, 0xb1, 0x37, 0x17, 0x57, 0x77, 0x97},

    {0x03, 0xc3, 0xa3, 0xb3, 0x23, 0x03, 0x43, 0x63, 0x83},
    {0x13, 0xc3, 0xa3, 0xb3, 0x33, 0x13, 0x53, 0x73, 0x93},
    {0x09, 0xc9, 0xa3, 0xb3, 0x29, 0x09, 0x49, 0x69, 0x89},
    {0x19, 0xc9, 0xa3, 0xb3, 0x39, 0x19, 0x59, 0x79, 0x99},

    {0x05, 0xc5, 0xa5, 0xb5, 0x25, 0x05, 0x45, 0x65, 0x85},
    {0x15, 0xc5, 0xa5, 0xb5, 0x35, 0x15, 0x55, 0x75, 0x95},
    {0x0b, 0xcb, 0xa5, 0xb5, 0x2b, 0x0b, 0x4b, 0x6b, 0x8b},
    {0x1b, 0xcb, 0xa5, 0xb5, 0x3b, 0x1b, 0x5b, 0x7b, 0x9b},

    {0x07, 0xc7, 0xa7, 0xb7, 0x27, 0x07, 0x47, 0x67, 0x87},
    {0x17, 0xc7, 0xa7, 0xb7, 0x37, 0x17, 0x57, 0x77, 0x97},

    {0x09, 0xc9, 0xa9, 0xb9, 0x29, 0x09, 0x49, 0x69, 0x89},
    {0x19, 0xc9, 0xa9, 0xb9, 0x39, 0x19, 0x59, 0x79, 0x99},

    {0x0b, 0xcb, 0xab, 0xbb, 0x2b, 0x0b, 0x4b, 0x6b, 0x8b},
    {0x1b, 0xcb, 0xab, 0xbb, 0x3b, 0x1b, 0x5b, 0x7b, 0x9b},

    {0x0d, 0xcd, 0xad, 0xbd, 0x2d, 0x0d, 0x4d, 0x6d, 0x8d},
    {0x1d, 0xcd, 0xad, 0xbd, 0x3d, 0x1d, 0x5d, 0x7d, 0x9d},

    {0x0f, 0xcf, 0xaf, 0xbf, 0x2f, 0x0f, 0x4f, 0x6f, 0x8f},
    {0x1f, 0xcf, 0xaf, 0xbf, 0x3f, 0x1f, 0x5f, 0x7f, 0x9f},

    {0xd1, 0xe9, 0xe5, 0xe7, 0xd5, 0xd1, 0xd9, 0xdd, 0xe1},
    {0xd3, 0xe9, 0xe5, 0xe7, 0xd7, 0xd3, 0xdb, 0xdf, 0xe3}
};

static const uint8_t regmap[512] = {
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
    0x20, 0x22, 0x24, 0x30, 0x32, 0x34, 0xff, 0xff, 0x26, 0x28, 0x2a, 0x36, 0x38, 0x3a, 0xff, 0xff,
    0x2c, 0x2e, 0xd4, 0x3c, 0x3e, 0xd6, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
    0x40, 0x42, 0x44, 0x50, 0x52, 0x54, 0xff, 0xff, 0x46, 0x48, 0x4a, 0x56, 0x58, 0x5a, 0xff, 0xff,
    0x4c, 0x4e, 0xd8, 0x5c, 0x5e, 0xda, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
    0x60, 0x62, 0x64, 0x70, 0x72, 0x74, 0xff, 0xff, 0x66, 0x68, 0x6a, 0x76, 0x78, 0x7a, 0xff, 0xff,
    0x6c, 0x6e, 0xdc, 0x7c, 0x7e, 0xde, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
    0x80, 0x82, 0x84, 0x90, 0x92, 0x94, 0xff, 0xff, 0x86, 0x88, 0x8a, 0x96, 0x98, 0x9a, 0xff, 0xff,
    0x8c, 0x8e, 0xe0, 0x9c, 0x9e, 0xe2, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
    0xa0, 0xa2, 0xa4, 0xa6, 0xa8, 0xaa, 0xac, 0xae, 0xe4, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
    0xb0, 0xb2, 0xb4, 0xb6, 0xb8, 0xba, 0xbc, 0xbe, 0xe6, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
    0xc0, 0xc2, 0xc4, 0xc6, 0xc8, 0xca, 0xcc, 0xce, 0xe8, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
    0x00, 0x02, 0x04, 0x10, 0x12, 0x14, 0xff, 0xff, 0x06, 0x08, 0x0a, 0x16, 0x18, 0x1a, 0xff, 0xff,
    0x0c, 0x0e, 0xd0, 0x1c, 0x1e, 0xd2, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
    0x21, 0x23, 0x25, 0x31, 0x33, 0x35, 0xff, 0xff, 0x27, 0x29, 0x2b, 0x37, 0x39, 0x3b, 0xff, 0xff,
    0x2d, 0x2f, 0xd5, 0x3d, 0x3f, 0xd7, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
    0x41, 0x43, 0x45, 0x51, 0x53, 0x55, 0xff, 0xff, 0x47, 0x49, 0x4b, 0x57, 0x59, 0x5b, 0xff, 0xff,
    0x4d, 0x4f, 0xd9, 0x5d, 0x5f, 0xdb, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
    0x61, 0x63, 0x65, 0x71, 0x73, 0x75, 0xff, 0xff, 0x67, 0x69, 0x6b, 0x77, 0x79, 0x7b, 0xff, 0xff,
    0x6d, 0x6f, 0xdd, 0x7d, 0x7f, 0xdf, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
    0x81, 0x83, 0x85, 0x91, 0x93, 0x95, 0xff, 0xff, 0x87, 0x89, 0x8b, 0x97, 0x99, 0x9b, 0xff, 0xff,
    0x8d, 0x8f, 0xe1, 0x9d, 0x9f, 0xe3, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
    0xa1, 0xa3, 0xa5, 0xa7, 0xa9, 0xab, 0xad, 0xaf, 0xe5, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
    0xb1, 0xb3, 0xb5, 0xb7, 0xb9, 0xbb, 0xbd, 0xbf, 0xe7, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
    0xc1, 0xc3, 0xc5, 0xc7, 0xc9, 0xcb, 0xcd, 0xcf, 0xe9, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
    0x01, 0x03, 0x05, 0x11, 0x13, 0x15, 0xff, 0xff, 0x07, 0x09, 0x0b, 0x17, 0x19, 0x1b, 0xff, 0xff,
    0x0d, 0x0f, 0xd1, 0x1d, 0x1f, 0xd3, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff
};

enum {
    eg_state_attack = 3,
    eg_state_decay = 2,
    eg_state_sustain = 1,
    eg_state_release = 0
};

#ifdef ENABLE_NUKED_CQM_LOG
int nuked_cqm_do_log = ENABLE_NUKED_CQM_LOG;

static void
nuked_cqm_log(const char *fmt, ...)
{
    va_list ap;

    if (nuked_cqm_do_log) {
        va_start(ap, fmt);
        pclog_ex(fmt, ap);
        va_end(ap);
    }
}
#else
#    define nuked_cqm_log(fmt, ...)
#endif

#if defined(__STDC_VERSION__) && __STDC_VERSION__ >= 199901L
static inline int doshifter(int x, int shift)
{
    if (shift > 12)
        return x << (shift - 12);
    return x >> (12 - shift);
}
#else
#   define doshifter(x, shift) (shift > 12 ? (x << (shift - 12)) : (x >> (12 - shift)))
#endif

void CQM_Generate(cqm_t* chip, int32_t* sample)
{
    int idx;
    int multi_l = 0;
    int con = 0;
    int con_prev = 0;
    int isrhy = (chip->rhy & 0x20) != 0;
    int accum[2] = { 0, 0 };

    chip->okeyl = chip->keyl;
    chip->keyl = (chip->key[0] << 0);
    chip->keyl |= (chip->key[1] << 1);
    chip->keyl |= (chip->key[2] << 2);
    chip->keyl |= (chip->key[3] << 3);
    chip->keyl |= (chip->key[4] << 4);
    chip->keyl |= (chip->key[5] << 5);
    chip->keyl |= (chip->key[6] << 6);
    chip->keyl |= (chip->key[7] << 7);
    chip->keyl |= (chip->key[7] << 8);
    chip->keyl |= (chip->key[8] << 9);
    chip->keyl |= (chip->key[8] << 10);
    chip->keyl |= (chip->key[9] << 11);
    chip->keyl |= (chip->key[10] << 12);
    chip->keyl |= (chip->key[11] << 13);
    chip->keyl |= (chip->key[12] << 14);
    chip->keyl |= (chip->key[13] << 15);
    chip->keyl |= (chip->key[14] << 16);
    chip->keyl |= (chip->key[15] << 17);
    chip->keyl |= (chip->key[16] << 18);
    chip->keyl |= (chip->key[17] << 19);

    if (isrhy)
    {
        if (chip->rhy & 1)
            chip->keyl |= 1 << 7;
        if (chip->rhy & 2)
            chip->keyl |= 1 << 10;
        if (chip->rhy & 4)
            chip->keyl |= 1 << 9;
        if (chip->rhy & 8)
            chip->keyl |= 1 << 8;
        if (chip->rhy & 16)
            chip->keyl |= 1 << 6;
    }

    for (idx = 0; idx < 48; idx++)
    {
        int wave, key, kon;
        int pmulti, fnum_lo, fnum_hi, fnum, block, multi;
        int fb_con, phase, modsub, modadd, fb, modshift, fnum_ps;
        int shift_l;

        int ch = 0;
        int op = 0;
        int is4op = 0;

        static const int multi_shift_add1[] = {
            0, 1, 2, 2, 3, 3, 3, 4, 4, 4, 4, 4, 4, 4, 5, 5
        };

        static const int multi_shift_add2[] = {
            0, 0, 0, 1, 0, 1, 2, -1, 0, 1, 2, 2, 3, 3, -1, -1
        };

        const uint8_t* access = regaccess[idx];
        cqmslot_t* slot = &chip->slotz[idx];

        int32_t *modptr;

        int keybit = 0;

        {
            int idx2 = idx;
            if (idx2 >= 24)
            {
                idx2 -= 24;
                ch += 9;
            }

            if (idx2 < 12)
            {
                ch += idx2 >> 2;
                op = idx2 & 3;

                if (ch < 9)
                    is4op = (chip->mode >> ch) & 1;
                else
                    is4op = (chip->mode >> (ch - 6)) & 1;
            }
            else
            {
                ch += (idx2 >> 1) - 3;
                op = idx2 & 1;

                if (ch >= 3 && ch < 6)
                    is4op = (chip->mode >> (ch - 3)) & 1;
                else if (ch >= 12 && ch < 15)
                    is4op = (chip->mode >> (ch - 9)) & 1;
            }
        }

        if (op == 0)
            modptr = chip->slotz[idx + 1].mod;
        else
            modptr = chip->slotz[idx].mod;

        if (ch < 7)
            keybit = ch;
        else if (ch < 9)
            keybit = ch * 2 + op - 7;
        else
            keybit = ch + 2;

        key = (chip->keyl >> keybit) & 1;
        kon = key && !(chip->okeyl & (1 << keybit));

        if (kon)
        {
            modptr[0] = 0;
            modptr[1] = 0;
        }
        else if (op != 0)
            modptr[chip->oddeven ^ 1] = chip->wave_prev << 2;

        {
            int wf;
            int phase_in;
            int phase;
            int16_t mulb, mula;

            wf = chip->regs[access[0]];
            phase = slot->phase >> 3;
            phase_in = phase;

            if (isrhy)
            {
                int nbit = chip->noise & 1;
                switch (idx)
                {
                    /* hh */
                    case 20:
                        phase_in = 0x1000 | (nbit << 13) | (chip->rhy_bit << 15);

                        chip->hh_bit1 = (slot->phase >> 12) & 1;
                        chip->hh_bit2 = (~(slot->phase >> 16) ^ (slot->phase >> 11)) & 1;
                        break;
                    /* sd */
                    case 21:
                        phase_in = (nbit << 14) | ((phase_in << 1) & 0x8000);
                        break;
                    /* tc */
                    case 23:
                    {
                        int b1 = (slot->phase >> 12) & 1;
                        int b2 = (slot->phase >> 14) & 1;

                        phase_in = (chip->rhy_bit << 15) | 0x2000;

                        chip->rhy_bit = !(
                            (!(b1 && b2 && chip->hh_bit1) && (b1 || b2 || chip->hh_bit1))
                            || chip->hh_bit2
                            );
                        break;
                    }
                }
            }

            mulb = 0;
            mula = 0;
            switch (wf)
            {
                case 0:
                    mulb = phase_in;
                    mula = mulb & 0x7fff;

                    if (!(phase_in & 0x8000))
                    {
                        mula ^= 0x7fff;
                    }
                    break;
                case 1:
                    mulb = phase_in;
                    mula = mulb ^ 0x8000;

                    if (!(phase_in & 0x8000))
                    {
                        mula = 0;
                    }
                    break;
                case 2:
                    mulb = phase_in;
                    mula = mulb ^ 0x8000;
                    break;
                case 3:
                    mulb = phase_in;
                    mula = mulb ^ 0x8000;

                    if (phase_in & 0x4000)
                    {
                        mula = 0;
                    }
                    break;
                case 4:
                    mulb = (phase_in << 1) & 0x7fff;

                    if (!(phase_in & 0x4000))
                    {
                        mulb |= 0x8000;
                    }

                    mula = mulb & 0x7fff;

                    if (phase_in & 0x4000)
                    {
                        mula ^= 0x7fff;
                    }

                    if (!(phase_in & 0x8000))
                    {
                        mula = 0;
                    }
                    break;
                case 5:
                    mulb = (phase_in << 1) & 0x7fff;

                    if (!(phase_in & 0x4000))
                    {
                        mulb |= 0x8000;
                    }

                    mula = mulb ^ 0x8000;

                    if (!(phase_in & 0x8000))
                    {
                        mula = 0;
                    }
                    break;
                case 6:
                    mulb = 0x3fff;
                    mula = mulb ^ 0x8000;
                    if (!(phase_in & 0x8000))
                    {
                        mula &= 0x7fff;
                    }
                    break;
                case 7:
                    mulb = phase_in & 0x3fff;

                    if (!(phase_in & 0x4000))
                    {
                        mulb |= 0xc000;
                    }

                    mula = mulb & 0x7fff;

                    if (!(phase_in & 0x4000))
                    {
                        mula ^= 0x7fff;
                    }

                    if (!((phase_in ^ (phase_in >> 1)) & 0x4000))
                    {
                        mula = 0;
                    }
                    break;
            }

            mulb >>= 2;
            if (mulb < 0)
                mulb++;

            wave = (mula * mulb) >> 12;
        }

        pmulti = chip->regs[access[4]];

        fnum_lo = chip->regs[access[2]];
        fnum_hi = chip->regs[access[3]];

        fnum = fnum_lo | ((fnum_hi & 3) << 8);
        block = (fnum_hi >> 2) & 7;

        {
            int gain, shift;
            int env = slot->env & 0x1ffff;
            int ksltl = chip->regs[access[6]];

            env -= (ksltl & 63) << 10;

            /* tremolo */
            if (pmulti & 0x80)
            {
                int trem = (chip->trem_cnt >> 1) & 15;
                if (chip->trem_cnt & 32)
                    trem ^= 15;
                if (chip->rhy & 0x80)
                    env -= trem << 9;
                else
                    env -= (trem >> 1) << 8;
            }

            if (ksltl & 0xc0)
            {
                static const uint8_t ksl_shift[4] = { 0, 1, 2, 0 };
                static const uint8_t ksl_table[16][8] = {
                    {0, 0, 0, 0, 0, 0, 0, 0},
                    {0, 0, 0, 0, 0, 0, 0, 0},
                    {0, 0, 0, 0, 8, 16, 24, 32},
                    {0, 0, 0, 4, 12, 20, 28, 36},
                    {0, 0, 0, 8, 16, 24, 32, 40},
                    {0, 0, 2, 10, 18, 26, 50, 58},
                    {0, 0, 4, 12, 20, 28, 36, 44},
                    {0, 0, 6, 14, 22, 30, 38, 46},
                    {0, 0, 8, 16, 24, 32, 40, 48},
                    {0, 1, 9, 17, 25, 33, 41, 49},
                    {0, 2, 10, 18, 26, 34, 42, 50},
                    {0, 3, 11, 19, 27, 35, 43, 51},
                    {0, 4, 12, 20, 28, 36, 44, 52},
                    {0, 5, 13, 21, 29, 37, 45, 53},
                    {0, 6, 14, 22, 30, 38, 46, 54},
                    {0, 7, 15, 23, 31, 39, 47, 55}
                };
                int kslv = ksl_table[fnum >> 6][block];
                env -= (kslv << 10) >> ksl_shift[ksltl >> 6];
            }

            if (env < 0)
                env = 0;

            gain = (env & 0x1fff) | 0x2000;
            shift = 15 - (env >> 13);
            gain = (gain << 1) >> shift;

            wave >>= 3;
            wave = (gain * wave) >> 10;
        }

        if (kon)
        {
            slot->mod[0] = slot->mod[1] = 0;
        }

        multi = (isrhy && ch == 7 && op == 1) ? multi_l : pmulti & 15;

        multi_l = pmulti & 15;

        if (op == 2)
        {
            con_prev = con;
        }

        fb_con = chip->regs[access[1]];
        con = fb_con & 1;

        phase = slot->phase;

        modsub = modptr[chip->oddeven ^ 1];
        modadd = modptr[chip->oddeven];

        fb = (fb_con >> 1) & 7;
        if ((op == 0 && fb == 0)
            || (isrhy && (ch == 7 || ch == 8))
            || (con && (op == 1 || (op == 3 && con_prev)))
            || (op == 2 && con && !con_prev)
            ) /* no modulation */
        {
            modsub = 0;
            modadd = 0;
        }

        /*modshift = 0; Value is never read */
        if (op == 0)
        {
            modshift = fb + 6;
        }
        else
        {
            modshift = 15;
        }

        modsub = doshifter(modsub, modshift);
        modadd = doshifter(modadd, modshift);
        if (op == 0)
        {
            phase += modsub;
            phase -= modadd;
        }
        else
        {
            phase -= modsub;
            phase += modadd;
        }

        /* fnum */
        fnum_ps = fnum << 8;
        if (block == 0)
            fnum_ps &= ~256; /* mask fnum0 */

        /* // Moved to a begin of this block at above
        static const int multi_shift_add1[] = {
            0, 1, 2, 2, 3, 3, 3, 4, 4, 4, 4, 4, 4, 4, 5, 5
        };

        static const int multi_shift_add2[] = {
            0, 0, 0, 1, 0, 1, 2, -1, 0, 1, 2, 2, 3, 3, -1, -1
        };
        */

        shift_l = block + 2 + multi_shift_add1[multi];

        phase += doshifter(fnum_ps, shift_l);

        if (multi_shift_add2[multi] != 0)
        {
            int mshift = multi_shift_add2[multi];
            int neg = 0;
            int shift_h;

            if (mshift < 0)
            {
                mshift = -mshift;
                neg = 1;
            }

            shift_h = block + 2 + mshift;

            if (neg)
                phase -= doshifter(fnum_ps, shift_h);
            else
                phase += doshifter(fnum_ps, shift_h);
        }

        /* vibrato */
        if (pmulti & 0x40)
        {
            int shift_l;
            int vibneg;
            int fnum_h = (fnum & 0x380) << 1;

            int shift_add = 0;
            if ((chip->counter & 0x600) == 0x400)
                shift_add++;
            if (chip->rhy & 0x40)
                shift_add++;

            shift_l = block + shift_add + multi_shift_add1[multi];
            vibneg = (chip->counter >> 11) & 1;

            if (vibneg)
                phase -= doshifter(fnum_h, shift_l);
            else
                phase += doshifter(fnum_h, shift_l);

            if (multi_shift_add2[multi] != 0)
            {
                int mshift = multi_shift_add2[multi];
                int neg = 0;
                int shift_h;

                if (mshift < 0)
                {
                    mshift = -mshift;
                    neg = 1;
                }

                shift_h = block + shift_add + mshift;

                if (neg ^ vibneg)
                    phase -= doshifter(fnum_h, shift_h);
                else
                    phase += doshifter(fnum_h, shift_h);
            }

        }

        if (kon)
        {
            slot->phase = 0x40000;
        }
        else
        {
            slot->phase = phase;
        }

        {
            int rate, ratesum, ratemax, ratezero, dostep, inc, of, slsub, slreach, nextstate;
            int ad = chip->regs[access[7]];
            int sr = chip->regs[access[8]];
            int egt = (pmulti >> 5) & 1;

            int state = (slot->env >> 17) & 3;
            int env = slot->env & 0x1ffff;

            int sl = (sr >> 4);

            if (sl == 15)
                sl = 28;

            rate = 0;
            switch (state)
            {
                case eg_state_attack:
                    rate = (ad >> 4) & 15;
                    break;
                case eg_state_decay:
                    rate = ad & 15;
                    break;
                case eg_state_release:
                    rate = sr & 15;
                    break;
            }

            ratesum = rate << 2;

            if (pmulti & 0x10) /* ksr */
            {
                ratesum += (fnum >> 9) & 1;
                ratesum += block << 1;
            }
            else
                ratesum += block >> 1;

            if (ratesum & 64)
                ratesum = 63;

            ratemax = rate == 15;
            ratezero = rate == 0;

            if (ratezero)
                ratesum = 0;

            /*dostep = 0; Value is never read */
            if (ratesum & 32)
                dostep = 1;
            else
            {
                dostep = chip->oddeven && ((chip->counter & 1) == 0 || (ratesum & 28) == 28)
                    && ((chip->counter & 2) == 0 || (ratesum & 24) == 24)
                    && ((chip->counter & 4) == 0 || ((ratesum & 16) != 0 && (ratesum & 12) != 0))
                    && ((chip->counter & 8) == 0 || (ratesum & 16) != 0)
                    && ((chip->counter & 16) == 0 || (ratesum & 16) != 0 || (ratesum & 12) == 12)
                    && ((chip->counter & 32) == 0 || (ratesum & 24) != 0);
            }

            inc = 0;
            if (dostep)
            {
                int shift;

                inc |= (ratesum & 3) << 13;
                if (!ratezero)
                    inc |= 1 << 15;

                shift = 0;
                if (ratesum & 32)
                    shift = (ratesum >> 2) & 7;

                if (state == eg_state_attack)
                {
                    if ((env & 0x1c000) == 0x1c000)
                        shift++;
                    else
                        shift += 4;
                    if ((env & 0x10000) == 0 || (env & 0xe000) == 0xc000)
                        shift += 2;
                }

                inc = doshifter(inc, shift);
            }

            /* of = 0; Value is never read*/
            if (state == eg_state_attack)
            {
                env += inc;
                of = (env >> 17) & 1;
            }
            else
            {
                env -= inc;
                of = (env >> 17) & 1;
                of = !of;
            }
            env &= 0x1ffff;

            slsub = sl << 12;


            slreach = !(((env + slsub) >> 17) & 1);

            if (slreach)
            {
                if (state == eg_state_attack && (of || ratemax))
                    env = 0x1ffff;

                if (state == eg_state_decay)
                    env = ((31 - sl) << 12) | 0xfff;

                if (state == eg_state_release && !of)
                    env = 0;

            }
            else
            {
                if (state == eg_state_attack && (of || ratemax))
                    env = 0x1ffff;

                if (state == eg_state_decay && !of)
                    env = ((31 - sl) << 12) | 0xfff;

                if (state == eg_state_release && !of)
                    env = 0;

            }

            nextstate = 0;
            if (kon)
                nextstate = eg_state_attack;
            else
            {
                if (state == eg_state_decay && egt && !of)
                    nextstate |= 1;

                if ((state & 1) != 0 && ((state & 2) == 0 || !of) && key)
                    nextstate |= 1;

                if (state == eg_state_attack && key)
                    nextstate |= 2;

                if (slreach && egt && state == eg_state_decay)
                    nextstate |= 1;

                if (!slreach && key && (state & 2) != 0 && of)
                    nextstate |= 2;
            }

            slot->env = env | (nextstate << 17);
        }

        {
            int doout;
            /* w1338 && (!(is4op&& op == 1) || con); // is4op && op==1 && con ->mute */
            int is4opch = (ch % 9) < 3;

            if (chip->dooutput && !(chip->is4op2 && !con))
            {
                int sumwave = doshifter(chip->wavesample << 5, chip->waveshift);

                if (chip->wavepan & 1)
                    accum[0] += sumwave;
                if (chip->wavepan & 2)
                    accum[1] += sumwave;
            }

            chip->waveshift = 7;
            if (isrhy && ch >= 6 && ch < 9)
                chip->waveshift++;
            chip->wave_prev = wave;
            chip->wavesample = wave >> 3;
            chip->wavepan = (fb_con >> 4) & 3;
            chip->is4op2 = is4opch && is4op && op == 1;

            doout = 0;

            switch (op)
            {
            case 0:
                doout = (isrhy && (ch == 7 || ch == 8))
                    || (con && (
                        is4opch ||
                        ((ch >= 6 && ch < 9) ? !(isrhy && ch == 6)
                            : !is4op)
                        ));
                break;
            case 1:
                doout = (ch >= 6 && ch < 9) || !is4op || (is4opch && !con);
                break;
            case 2:
                doout = is4op && con && con_prev;
                break;
            case 3:
                doout = is4op;
                break;
            }

            chip->dooutput = doout;
        }

        {
            /* FIXME: runs 13 times per slot on real chip */
            uint32_t bit = ((chip->noise >> 10) ^ (chip->noise >> 17)) & 1;
            chip->noise = (chip->noise << 1) | bit;
        }
    }

    chip->oddeven ^= 1;
    if (chip->oddeven == 0)
    {
        chip->counter++;

        chip->trem_cnt1 = (chip->trem_cnt1 + 1) % 3;
        if (chip->trem_cnt1 == 1)
        {
            chip->trem_cnt2 = (chip->trem_cnt2 + 1) % 5;
            if (chip->trem_cnt2 == 1)
            {
                chip->trem_cnt3 = (chip->trem_cnt3 + 1) % 7;
                if (chip->trem_cnt3 == 1)
                {
                    chip->trem_cnt++;
                }
            }
        }
    }

    accum[0] >>= 1;
    accum[1] >>= 1;

    sample[0] = accum[0];
    sample[1] = accum[1];
    
    {
        cqm_writebuf* writebuf;
        while ((writebuf = &chip->writebuf[chip->writebuf_cur]), writebuf->time <= chip->writebuf_samplecnt)
        {
            if (!(writebuf->reg & 0x200))
            {
                break;
            }
            writebuf->reg &= 0x1ff;
            CQM_WriteReg(chip, writebuf->reg, writebuf->data);
            chip->writebuf_cur = (chip->writebuf_cur + 1) % CQM_WRITEBUF_SIZE;
        }
        chip->writebuf_samplecnt++;
    }
}

void CQM_GenerateResampled(cqm_t* chip, int32_t* buf)
{
    while (chip->samplecnt >= chip->rateratio)
    {
        chip->oldsamples[0] = chip->samples[0];
        chip->oldsamples[1] = chip->samples[1];
        CQM_Generate(chip, chip->samples);
        chip->samplecnt -= chip->rateratio;
    }

    buf[0] = (int32_t)((chip->oldsamples[0] * (chip->rateratio - chip->samplecnt)
        + chip->samples[0] * chip->samplecnt) / chip->rateratio);
    buf[1] = (int32_t)((chip->oldsamples[1] * (chip->rateratio - chip->samplecnt)
        + chip->samples[1] * chip->samplecnt) / chip->rateratio);

    chip->samplecnt += 1 << RSM_FRAC;
}

void CQM_Reset(cqm_t* chip, uint32_t samplerate, uint32_t genrate)
{
    int i;
    memset(chip, 0, sizeof(cqm_t));

    for (i = 0; i < 18; i++)
    {
        chip->regs[regmap[0xc0 + (i % 9) + ((i / 9) << 8)]] = 0x30;
    }

    chip->counter = 0xfff;
    chip->trem_cnt = 0x3f;
    chip->noise = 0x3ffff;


    chip->rateratio = (samplerate << RSM_FRAC) / genrate;
}

uint16_t
nuked_cqm_write_addr(void *priv, uint16_t port, uint8_t val)
{
    const cqm_t *chip = (cqm_t *) priv;
    uint16_t     addr = val;

    if ((port & 0x0002) && ((addr == 0x0005) || chip->newm))
        addr |= 0x0100;

    return addr;
}

void CQM_WriteReg(cqm_t* chip, uint16_t reg, uint8_t data)
{
    int regh = (reg >> 8) & 1;
    int index;
    reg &= 0xff;

    if (regh && reg == 5)
    {
        chip->newm = data & 1;
        return;
    }

    regh &= chip->newm;

    index = regmap[(regh << 8) + reg];
    if (index != 0xff)
    {
        if (!chip->newm)
        {
            if ((reg & 0xf0) == 0xc0)
                data |= 0x30;
            if ((reg & 0xe0) == 0xe0)
                data &= 0xfb;
        }

        if ((reg & 0xf0) == 0xb0)
            chip->key[regh * 9 + (reg & 0xf)] = (data >> 5) & 1;

        chip->regs[index] = data;
        return;
    }

    if (!regh && reg == 0xbd)
    {
        chip->rhy = data;
        return;
    }

    if (regh && reg == 0x4)
    {
        chip->mode = data & 0x3f;
        return;
    }
}

void CQM_WriteRegBuffered(cqm_t* chip, uint16_t reg, uint8_t data)
{
    uint64_t time1, time2;
    cqm_writebuf* writebuf;
    uint32_t writebuf_last;

    writebuf_last = chip->writebuf_last;
    writebuf = &chip->writebuf[writebuf_last];

    if (writebuf->reg & 0x200)
    {
        CQM_WriteReg(chip, writebuf->reg & 0x1ff, writebuf->data);

        chip->writebuf_cur = (writebuf_last + 1) % CQM_WRITEBUF_SIZE;
        chip->writebuf_samplecnt = writebuf->time;
    }

    writebuf->reg = reg | 0x200;
    writebuf->data = data;
    time1 = chip->writebuf_lasttime + CQM_WRITEBUF_DELAY;
    time2 = chip->writebuf_samplecnt;

    if (time1 < time2)
    {
        time1 = time2;
    }

    writebuf->time = time1;
    chip->writebuf_lasttime = time1;
    chip->writebuf_last = (writebuf_last + 1) % CQM_WRITEBUF_SIZE;
}

void CQM_GenerateStream(cqm_t* chip, int32_t* sndptr, uint32_t numsamples)
{
    uint_fast32_t i;

    for (i = 0; i < numsamples; i++)
    {
        CQM_Generate(chip, sndptr);
        sndptr += 2;
    }
}

void CQM_GenerateStreamResampled(cqm_t* chip, int32_t* sndptr, uint32_t numsamples)
{
    uint_fast32_t i;

    for (i = 0; i < numsamples; i++)
    {
        CQM_GenerateResampled(chip, sndptr);
        sndptr += 2;
    }
}

#define CQM_CLOCK     46615120.0
#define CQM_UCLOCK    46615120ULL
#define CQM_OPERATORS       48.0
#define CQM_PRESCALE        20.0

static void    nuked_cqm_timer_tick(nuked_cqm_drv_t *dev, int tmr);

static void
nuked_cqm_timer_advance(nuked_cqm_drv_t *dev, int tmr, int start)
{
    if (dev->is_cs && start)
        nuked_cqm_timer_tick(dev, tmr);
    else {
        const double clock_us = (1000000.0 / CQM_CLOCK) * CQM_OPERATORS * CQM_PRESCALE;
        double       period;

        if (tmr == 1) {
            if (start) {
                /*
                   This emulates the behavior found on everything but the Crystal OPL
                   clone - the information in the YM262 (OPL3) datasheet is a typo,
                   and YMFM explains what actually goes on.

                   TSC = Currently elapsed CPU cycles, (cpu speed in Hz) cycles per second,
                   What we need is elapsed OPL clocks, (opl speed in Hz) cycles per second,
                   so we divide the TSC by (cpu speed in Hz) to get the time, then multiply
                   it by (opl speed in Hz).
                   */
                uint64_t total_clocks = ((tsc * CQM_UCLOCK) / (uint64_t) cpuclock);
                period = clock_us * (16.0 - (double) (total_clocks & 15));
            } else
                period = clock_us * 16.0;
        } else
            period = clock_us * 4.0;

        timer_on_auto(&dev->timers[tmr], period);
    }
}

static void
nuked_cqm_timer_tick(nuked_cqm_drv_t *dev, int tmr)
{
    dev->timer_cur_count[tmr] = (dev->timer_cur_count[tmr] + 1) & 0xff;

    nuked_cqm_log("Ticking timer %i, count now %02X...\n", tmr, dev->timer_cur_count[tmr]);

    if (dev->timer_cur_count[tmr] == 0x00) {
        dev->status |= ((STAT_TMR1_OVER >> tmr) & ~dev->timer_ctrl);
        dev->timer_cur_count[tmr] = dev->timer_count[tmr];

        nuked_cqm_log("Count wrapped around to zero, reloading timer %i (%02X), status = %02X...\n", tmr, (STAT_TMR1_OVER >> tmr), dev->status);
    }

    nuked_cqm_timer_advance(dev, tmr, 0);
}

static void
nuked_cqm_timer_control(nuked_cqm_drv_t *dev, int tmr, int start)
{
    timer_on_auto(&dev->timers[tmr], 0.0);

    if (start) {
        nuked_cqm_log("Loading timer %i count: %02X = %02X\n", tmr, dev->timer_cur_count[tmr], dev->timer_count[tmr]);
        dev->timer_cur_count[tmr] = dev->timer_count[tmr];

        nuked_cqm_timer_advance(dev, tmr, 1);
    } else {
        nuked_cqm_log("Timer %i stopped\n", tmr);
        if (tmr == 1) {
            dev->status &= ~STAT_TMR2_OVER;
        } else
            dev->status &= ~STAT_TMR1_OVER;
    }
}

static void
nuked_cqm_timer_1(void *priv)
{
    nuked_cqm_drv_t *dev = (nuked_cqm_drv_t *) priv;

    nuked_cqm_timer_tick(dev, 0);
}

static void
nuked_cqm_timer_2(void *priv)
{
    nuked_cqm_drv_t *dev = (nuked_cqm_drv_t *) priv;

    nuked_cqm_timer_tick(dev, 1);
}

static void
nuked_cqm_drv_set_do_cycles(void *priv, int8_t do_cycles)
{
    nuked_cqm_drv_t *dev = (nuked_cqm_drv_t *) priv;

    if (do_cycles)
        dev->flags |= FLAG_CYCLES;
    else
        dev->flags &= ~FLAG_CYCLES;
}

static int32_t *
nuked_cqm_drv_update(void *priv)
{
    nuked_cqm_drv_t *dev = (nuked_cqm_drv_t *) priv;

    if (dev->pos >= music_pos_global)
        return dev->buffer;

    CQM_GenerateStream(&dev->cqm,
                       &dev->buffer[dev->pos * 2],
             music_pos_global - dev->pos);

    for (; dev->pos < music_pos_global; dev->pos++) {
        dev->buffer[dev->pos * 2] /= 2;
        dev->buffer[(dev->pos * 2) + 1] /= 2;
    }

    return dev->buffer;
}

static int32_t *
nuked_cqm_drv_update_48k(void *priv)
{
    nuked_cqm_drv_t *dev = (nuked_cqm_drv_t *) priv;

    if (dev->pos >= sound_pos_global)
        return dev->buffer;

    CQM_GenerateStreamResampled(&dev->cqm,
                                &dev->buffer[dev->pos * 2],
                      sound_pos_global - dev->pos);

    for (; dev->pos < sound_pos_global; dev->pos++) {
        dev->buffer[dev->pos * 2] /= 2;
        dev->buffer[(dev->pos * 2) + 1] /= 2;
    }

    return dev->buffer;
}

static uint8_t
nuked_cqm_drv_read(uint16_t port, void *priv)
{
    nuked_cqm_drv_t *dev = (nuked_cqm_drv_t *) priv;

    if (dev->flags & FLAG_CYCLES)
        cycles -= ((int) (isa_timing * 8));

    dev->update(dev);

    uint8_t ret = 0xff;

    if ((port & 0x0003) == 0x0000) {
        ret = dev->status;
        if (dev->status & STAT_TMR_OVER)
            ret |= STAT_TMR_ANY;
    }

    nuked_cqm_log("OPL statret = %02x, status = %02x\n", ret, dev->status);

    return ret;
}

static void
nuked_cqm_drv_write(uint16_t port, uint8_t val, void *priv)
{
    nuked_cqm_drv_t *dev = (nuked_cqm_drv_t *) priv;

    dev->update(dev);

    if ((port & 0x0001) == 0x0001) {
        CQM_WriteRegBuffered(&dev->cqm, dev->port, val);

        switch (dev->port) {
            case 0x002: // Timer 1
                dev->timer_count[0] = val;
                nuked_cqm_log("Timer 0 count now: %i\n", dev->timer_count[0]);
                break;

            case 0x003: // Timer 2
                dev->timer_count[1] = val;
                nuked_cqm_log("Timer 1 count now: %i\n", dev->timer_count[1]);
                break;

            case 0x004: // Timer control
                if (val & CTRL_RESET) {
                    nuked_cqm_log("Resetting timer status...\n");
                    dev->status &= ~STAT_TMR_OVER;
                } else {
                    dev->timer_ctrl = val;
                    nuked_cqm_timer_control(dev, 0, val & CTRL_TMR1_START);
                    nuked_cqm_timer_control(dev, 1, val & CTRL_TMR2_START);
                    nuked_cqm_log("Status mask now %02X (val = %02X)\n", (val & ~CTRL_TMR_MASK) & CTRL_TMR_MASK, val);
                }
                break;

            default:
                break;
        }
    } else
        dev->port = nuked_cqm_write_addr(&dev->cqm, port, val) & 0x01ff;
}

static void
nuked_cqm_drv_reset_buffer(void *priv)
{
    nuked_cqm_drv_t *dev = (nuked_cqm_drv_t *) priv;

    dev->pos = 0;
}

static void
nuked_cqm_drv_close(void *priv)
{
    nuked_cqm_drv_t *dev = (nuked_cqm_drv_t *) priv;

    free(dev);
}

static void *
nuked_cqm_drv_init(const device_t *info)
{
    nuked_cqm_drv_t *dev = (nuked_cqm_drv_t *) calloc(1, sizeof(nuked_cqm_drv_t));
    dev->flags       = FLAG_CYCLES;

    dev->is_48k      = !!(info->local & FM_FORCE_48K);
    dev->is_cs       = !!(info->local & FM_CRYSTAL);

    // Initialize the NukedCQM object.
    if (dev->is_48k) {
        dev->update      = nuked_cqm_drv_update_48k;
        CQM_Reset(&dev->cqm, FREQ_48000, FREQ_49716);
    } else {
        dev->update      = nuked_cqm_drv_update;
        CQM_Reset(&dev->cqm, FREQ_49716, FREQ_49716);
    }

    timer_add(&dev->timers[0], nuked_cqm_timer_1, dev, 0);
    timer_add(&dev->timers[1], nuked_cqm_timer_2, dev, 0);

    return dev;
}

const device_t nuked_cqm_device = {
    .name          = "NUKED CQM",
    .internal_name = "cqm_nuked",
    .flags         = 0,
    .local         = 0,
    .init          = nuked_cqm_drv_init,
    .close         = nuked_cqm_drv_close,
    .reset         = NULL,
    .available     = NULL,
    .speed_changed = NULL,
    .force_redraw  = NULL,
    .config        = NULL
};

const fm_drv_t nuked_cqm_drv = {
    .read          = &nuked_cqm_drv_read,
    .write         = &nuked_cqm_drv_write,
    .update        = &nuked_cqm_drv_update,
    .reset_buffer  = &nuked_cqm_drv_reset_buffer,
    .set_do_cycles = &nuked_cqm_drv_set_do_cycles,
    .priv          = NULL,
    .generate      = NULL,
};

const fm_drv_t nuked_cqm_drv_48k = {
    .read          = &nuked_cqm_drv_read,
    .write         = &nuked_cqm_drv_write,
    .update        = &nuked_cqm_drv_update_48k,
    .reset_buffer  = &nuked_cqm_drv_reset_buffer,
    .set_do_cycles = &nuked_cqm_drv_set_do_cycles,
    .priv          = NULL,
    .generate      = NULL,
};
