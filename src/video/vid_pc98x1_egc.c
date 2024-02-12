/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          Emulation of the EGC graphics processor used by
 *          the NEC PC-98x1 series of computers.
 *
 *
 *
 * Authors: TAKEDA toshiya,
 *          yui/Neko Project II
 *
 *          Copyright 2009-2023 TAKEDA, toshiya.
 *          Copyright 2008-2023 yui/Neko Project II.
 */
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <stdlib.h>
#include <wchar.h>
#include <math.h>
#include <86box/86box.h>
#include "cpu.h"
#include <86box/io.h>
#include <86box/timer.h>
#include <86box/pit.h>
#include <86box/mem.h>
#include <86box/device.h>
#include <86box/video.h>
#include <86box/vid_pc98x1_egc.h>
#include <86box/vid_pc98x1_disp.h>
#include <86box/plat_unused.h>

/***********************************************************/
/* EGC (based on Neko Project 2) */

/* shift sub */

static const uint8_t egc_bytemask_u0[64] = {
    0x80, 0x40, 0x20, 0x10, 0x08, 0x04, 0x02, 0x01,
    0xc0, 0x60, 0x30, 0x18, 0x0c, 0x06, 0x03, 0x01,
    0xe0, 0x70, 0x38, 0x1c, 0x0e, 0x07, 0x03, 0x01,
    0xf0, 0x78, 0x3c, 0x1e, 0x0f, 0x07, 0x03, 0x01,
    0xf8, 0x7c, 0x3e, 0x1f, 0x0f, 0x07, 0x03, 0x01,
    0xfc, 0x7e, 0x3f, 0x1f, 0x0f, 0x07, 0x03, 0x01,
    0xfe, 0x7f, 0x3f, 0x1f, 0x0f, 0x07, 0x03, 0x01,
    0xff, 0x7f, 0x3f, 0x1f, 0x0f, 0x07, 0x03, 0x01
};
static const uint8_t egc_bytemask_u1[8] =  {
    0x80, 0xc0, 0xe0, 0xf0, 0xf8, 0xfc, 0xfe, 0xff
};
static const uint8_t egc_bytemask_d0[64] = {
    0x01, 0x02, 0x04, 0x08, 0x10, 0x20, 0x40, 0x80,
    0x03, 0x06, 0x0c, 0x18, 0x30, 0x60, 0xc0, 0x80,
    0x07, 0x0e, 0x1c, 0x38, 0x70, 0xe0, 0xc0, 0x80,
    0x0f, 0x1e, 0x3c, 0x78, 0xf0, 0xe0, 0xc0, 0x80,
    0x1f, 0x3e, 0x7c, 0xf8, 0xf0, 0xe0, 0xc0, 0x80,
    0x3f, 0x7e, 0xfc, 0xf8, 0xf0, 0xe0, 0xc0, 0x80,
    0x7f, 0xfe, 0xfc, 0xf8, 0xf0, 0xe0, 0xc0, 0x80,
    0xff, 0xfe, 0xfc, 0xf8, 0xf0, 0xe0, 0xc0, 0x80
};
static const uint8_t egc_bytemask_d1[8] = {
    0x01, 0x03, 0x07, 0x0f, 0x1f, 0x3f, 0x7f, 0xff
};

/* draw command */

static const int gdc_vectdir[16][4] = {
    { 0, 1, 1, 0}, { 1, 1, 1,-1}, { 1, 0, 0,-1}, { 1,-1,-1,-1},
    { 0,-1,-1, 0}, {-1,-1,-1, 1}, {-1, 0, 0, 1}, {-1, 1, 1, 1},
    { 0, 1, 1, 1}, { 1, 1, 1, 0}, { 1, 0, 1,-1}, { 1,-1, 0,-1},
    { 0,-1,-1,-1}, {-1,-1,-1, 0}, {-1, 0,-1, 1}, {-1, 1, 0, 1}
};

static void
egc_shift(egc_t *dev)
{
    uint8_t src8, dst8;

    dev->remain = (dev->leng & 0xfff) + 1;
    dev->func = (dev->sft >> 12) & 1;
    if (!dev->func) {
        dev->inptr = dev->buf;
        dev->outptr = dev->buf;
    } else {
        dev->inptr = dev->buf + (4096 / 8) + 3;
        dev->outptr = dev->buf + (4096 / 8) + 3;
    }
    dev->srcbit = dev->sft & 0x0f;
    dev->dstbit = (dev->sft >> 4) & 0x0f;

    src8 = dev->srcbit & 0x07;
    dst8 = dev->dstbit & 0x07;
    if (src8 < dst8) {
        dev->func += 2;
        dev->sft8bitr = dst8 - src8;
        dev->sft8bitl = 8 - dev->sft8bitr;
    } else if (src8 > dst8) {
        dev->func += 4;
        dev->sft8bitl = src8 - dst8;
        dev->sft8bitr = 8 - dev->sft8bitl;
    }
    dev->stack = 0;
}

static void
egc_sftb_upn_sub(egc_t *dev, uint32_t ext)
{
    if (dev->dstbit >= 8) {
        dev->dstbit -= 8;
        dev->srcmask.b[ext] = 0;
        return;
    }
    if (dev->dstbit) {
        if ((dev->dstbit + dev->remain) >= 8) {
            dev->srcmask.b[ext] = egc_bytemask_u0[dev->dstbit + (7 * 8)];
            dev->remain -= (8 - dev->dstbit);
            dev->dstbit = 0;
        } else {
            dev->srcmask.b[ext] = egc_bytemask_u0[dev->dstbit + (dev->remain - 1) * 8];
            dev->remain = 0;
            dev->dstbit = 0;
        }
    } else {
        if (dev->remain >= 8)
            dev->remain -= 8;
        else {
            dev->srcmask.b[ext] = egc_bytemask_u1[dev->remain - 1];
            dev->remain = 0;
        }
    }
    dev->vram_src.b[0][ext] = dev->outptr[0];
    dev->vram_src.b[1][ext] = dev->outptr[4];
    dev->vram_src.b[2][ext] = dev->outptr[8];
    dev->vram_src.b[3][ext] = dev->outptr[12];
    dev->outptr++;
}

static void
egc_sftb_dnn_sub(egc_t *dev, uint32_t ext)
{
    if (dev->dstbit >= 8) {
        dev->dstbit -= 8;
        dev->srcmask.b[ext] = 0;
        return;
    }
    if (dev->dstbit) {
        if ((dev->dstbit + dev->remain) >= 8) {
            dev->srcmask.b[ext] = egc_bytemask_d0[dev->dstbit + (7 * 8)];
            dev->remain -= (8 - dev->dstbit);
            dev->dstbit = 0;
        } else {
            dev->srcmask.b[ext] = egc_bytemask_d0[dev->dstbit + (dev->remain - 1) * 8];
            dev->remain = 0;
            dev->dstbit = 0;
        }
    } else {
        if (dev->remain >= 8)
            dev->remain -= 8;
        else {
            dev->srcmask.b[ext] = egc_bytemask_d1[dev->remain - 1];
            dev->remain = 0;
        }
    }
    dev->vram_src.b[0][ext] = dev->outptr[0];
    dev->vram_src.b[1][ext] = dev->outptr[4];
    dev->vram_src.b[2][ext] = dev->outptr[8];
    dev->vram_src.b[3][ext] = dev->outptr[12];
    dev->outptr--;
}

static void
egc_sftb_upr_sub(egc_t *dev, uint32_t ext)
{
    if (dev->dstbit >= 8) {
        dev->dstbit -= 8;
        dev->srcmask.b[ext] = 0;
        return;
    }
    if (dev->dstbit) {
        if ((dev->dstbit + dev->remain) >= 8) {
            dev->srcmask.b[ext] = egc_bytemask_u0[dev->dstbit + (7 * 8)];
            dev->remain -= (8 - dev->dstbit);
        } else {
            dev->srcmask.b[ext] = egc_bytemask_u0[dev->dstbit + (dev->remain - 1) * 8];
            dev->remain = 0;
        }
        dev->dstbit = 0;
        dev->vram_src.b[0][ext] = (dev->outptr[0] >> dev->sft8bitr);
        dev->vram_src.b[1][ext] = (dev->outptr[4] >> dev->sft8bitr);
        dev->vram_src.b[2][ext] = (dev->outptr[8] >> dev->sft8bitr);
        dev->vram_src.b[3][ext] = (dev->outptr[12] >> dev->sft8bitr);
    } else {
        if (dev->remain >= 8)
            dev->remain -= 8;
        else {
            dev->srcmask.b[ext] = egc_bytemask_u1[dev->remain - 1];
            dev->remain = 0;
        }
        dev->vram_src.b[0][ext] = (dev->outptr[0] << dev->sft8bitl) | (dev->outptr[1] >> dev->sft8bitr);
        dev->vram_src.b[1][ext] = (dev->outptr[4] << dev->sft8bitl) | (dev->outptr[5] >> dev->sft8bitr);
        dev->vram_src.b[2][ext] = (dev->outptr[8] << dev->sft8bitl) | (dev->outptr[9] >> dev->sft8bitr);
        dev->vram_src.b[3][ext] = (dev->outptr[12] << dev->sft8bitl) | (dev->outptr[13] >> dev->sft8bitr);
        dev->outptr++;
    }
}

static void
egc_sftb_dnr_sub(egc_t *dev, uint32_t ext)
{
    if (dev->dstbit >= 8) {
        dev->dstbit -= 8;
        dev->srcmask.b[ext] = 0;
        return;
    }
    if (dev->dstbit) {
        if ((dev->dstbit + dev->remain) >= 8) {
            dev->srcmask.b[ext] = egc_bytemask_d0[dev->dstbit + (7 * 8)];
            dev->remain -= (8 - dev->dstbit);
        } else {
            dev->srcmask.b[ext] = egc_bytemask_d0[dev->dstbit + (dev->remain - 1) * 8];
            dev->remain = 0;
        }
        dev->dstbit = 0;
        dev->vram_src.b[0][ext] = (dev->outptr[0] << dev->sft8bitr);
        dev->vram_src.b[1][ext] = (dev->outptr[4] << dev->sft8bitr);
        dev->vram_src.b[2][ext] = (dev->outptr[8] << dev->sft8bitr);
        dev->vram_src.b[3][ext] = (dev->outptr[12] << dev->sft8bitr);
    } else {
        if (dev->remain >= 8)
            dev->remain -= 8;
        else {
            dev->srcmask.b[ext] = egc_bytemask_d1[dev->remain - 1];
            dev->remain = 0;
        }
        dev->outptr--;
        dev->vram_src.b[0][ext] = (dev->outptr[1] >> dev->sft8bitl) | (dev->outptr[0] << dev->sft8bitr);
        dev->vram_src.b[1][ext] = (dev->outptr[5] >> dev->sft8bitl) | (dev->outptr[4] << dev->sft8bitr);
        dev->vram_src.b[2][ext] = (dev->outptr[9] >> dev->sft8bitl) | (dev->outptr[8] << dev->sft8bitr);
        dev->vram_src.b[3][ext] = (dev->outptr[13] >> dev->sft8bitl) | (dev->outptr[12] << dev->sft8bitr);
    }
}

static void
egc_sftb_upl_sub(egc_t *dev, uint32_t ext)
{
    if (dev->dstbit >= 8) {
        dev->dstbit -= 8;
        dev->srcmask.b[ext] = 0;
        return;
    }
    if (dev->dstbit) {
        if ((dev->dstbit + dev->remain) >= 8) {
            dev->srcmask.b[ext] = egc_bytemask_u0[dev->dstbit + (7 * 8)];
            dev->remain -= (8 - dev->dstbit);
            dev->dstbit = 0;
        } else {
            dev->srcmask.b[ext] = egc_bytemask_u0[dev->dstbit + (dev->remain - 1) * 8];
            dev->remain = 0;
            dev->dstbit = 0;
        }
    } else {
        if (dev->remain >= 8)
            dev->remain -= 8;
        else {
            dev->srcmask.b[ext] = egc_bytemask_u1[dev->remain - 1];
            dev->remain = 0;
        }
    }
    dev->vram_src.b[0][ext] = (dev->outptr[0] << dev->sft8bitl) | (dev->outptr[1] >> dev->sft8bitr);
    dev->vram_src.b[1][ext] = (dev->outptr[4] << dev->sft8bitl) | (dev->outptr[5] >> dev->sft8bitr);
    dev->vram_src.b[2][ext] = (dev->outptr[8] << dev->sft8bitl) | (dev->outptr[9] >> dev->sft8bitr);
    dev->vram_src.b[3][ext] = (dev->outptr[12] << dev->sft8bitl) | (dev->outptr[13] >> dev->sft8bitr);
    dev->outptr++;
}

static void
egc_sftb_dnl_sub(egc_t *dev, uint32_t ext)
{
    if (dev->dstbit >= 8) {
        dev->dstbit -= 8;
        dev->srcmask.b[ext] = 0;
        return;
    }
    if (dev->dstbit) {
        if ((dev->dstbit + dev->remain) >= 8) {
            dev->srcmask.b[ext] = egc_bytemask_d0[dev->dstbit + (7 * 8)];
            dev->remain -= (8 - dev->dstbit);
            dev->dstbit = 0;
        } else {
            dev->srcmask.b[ext] = egc_bytemask_d0[dev->dstbit + (dev->remain - 1) * 8];
            dev->remain = 0;
            dev->dstbit = 0;
        }
    } else {
        if (dev->remain >= 8)
            dev->remain -= 8;
        else {
            dev->srcmask.b[ext] = egc_bytemask_d1[dev->remain - 1];
            dev->remain = 0;
        }
    }
    dev->outptr--;
    dev->vram_src.b[0][ext] = (dev->outptr[1] >> dev->sft8bitl) | (dev->outptr[0] << dev->sft8bitr);
    dev->vram_src.b[1][ext] = (dev->outptr[5] >> dev->sft8bitl) | (dev->outptr[4] << dev->sft8bitr);
    dev->vram_src.b[2][ext] = (dev->outptr[9] >> dev->sft8bitl) | (dev->outptr[8] << dev->sft8bitr);
    dev->vram_src.b[3][ext] = (dev->outptr[13] >> dev->sft8bitl) | (dev->outptr[12] << dev->sft8bitr);
}

static void
egc_sftb_upn0(egc_t *dev, uint32_t ext)
{
    if (dev->stack < ((uint32_t)(8 - dev->dstbit))) {
        dev->srcmask.b[ext] = 0;
        return;
    }
    dev->stack -= (8 - dev->dstbit);
    egc_sftb_upn_sub(dev, ext);
    if (!dev->remain)
        egc_shift(dev);
}

static void
egc_sftw_upn0(egc_t *dev)
{
    if (dev->stack < ((uint32_t)(16 - dev->dstbit))) {
        dev->srcmask.w = 0;
        return;
    }
    dev->stack -= (16 - dev->dstbit);
    egc_sftb_upn_sub(dev, 0);
    if (dev->remain) {
        egc_sftb_upn_sub(dev, 1);
        if (dev->remain)
            return;
    } else
        dev->srcmask.b[1] = 0;

    egc_shift(dev);
}

static void
egc_sftb_dnn0(egc_t *dev, uint32_t ext)
{
    if (dev->stack < ((uint32_t)(8 - dev->dstbit))) {
        dev->srcmask.b[ext] = 0;
        return;
    }
    dev->stack -= (8 - dev->dstbit);
    egc_sftb_dnn_sub(dev, ext);
    if (!dev->remain)
        egc_shift(dev);
}

static void
egc_sftw_dnn0(egc_t *dev)
{
    if (dev->stack < ((uint32_t)(16 - dev->dstbit))) {
        dev->srcmask.w = 0;
        return;
    }
    dev->stack -= (16 - dev->dstbit);
    egc_sftb_dnn_sub(dev, 1);
    if (dev->remain) {
        egc_sftb_dnn_sub(dev, 0);
        if (dev->remain)
            return;
    } else
        dev->srcmask.b[0] = 0;

    egc_shift(dev);
}

static void
egc_sftb_upr0(egc_t *dev, uint32_t ext)
{
    if (dev->stack < ((uint32_t)(8 - dev->dstbit))) {
        dev->srcmask.b[ext] = 0;
        return;
    }
    dev->stack -= (8 - dev->dstbit);
    egc_sftb_upr_sub(dev, ext);
    if (!dev->remain)
        egc_shift(dev);
}

static void
egc_sftw_upr0(egc_t *dev)
{
    if (dev->stack < ((uint32_t)(16 - dev->dstbit))) {
        dev->srcmask.w = 0;
        return;
    }
    dev->stack -= (16 - dev->dstbit);
    egc_sftb_upr_sub(dev, 0);
    if (dev->remain) {
        egc_sftb_upr_sub(dev, 1);
        if (dev->remain)
            return;
    } else
        dev->srcmask.b[1] = 0;

    egc_shift(dev);
}

static void
egc_sftb_dnr0(egc_t *dev, uint32_t ext)
{
    if (dev->stack < ((uint32_t)(8 - dev->dstbit))) {
        dev->srcmask.b[ext] = 0;
        return;
    }
    dev->stack -= (8 - dev->dstbit);
    egc_sftb_dnr_sub(dev, ext);
    if (!dev->remain)
        egc_shift(dev);
}

static void
egc_sftw_dnr0(egc_t *dev)
{
    if (dev->stack < ((uint32_t)(16 - dev->dstbit))) {
        dev->srcmask.w = 0;
        return;
    }
    dev->stack -= (16 - dev->dstbit);
    egc_sftb_dnr_sub(dev, 1);
    if (dev->remain) {
        egc_sftb_dnr_sub(dev, 0);
        if (dev->remain)
            return;
    } else {
        dev->srcmask.b[0] = 0;
    }
    egc_shift(dev);
}

static void
egc_sftb_upl0(egc_t *dev, uint32_t ext)
{
    if (dev->stack < ((uint32_t)(8 - dev->dstbit))) {
        dev->srcmask.b[ext] = 0;
        return;
    }
    dev->stack -= (8 - dev->dstbit);
    egc_sftb_upl_sub(dev, ext);
    if (!dev->remain)
        egc_shift(dev);
}

static void
egc_sftw_upl0(egc_t *dev)
{
    if (dev->stack < ((uint32_t)(16 - dev->dstbit))) {
        dev->srcmask.w = 0;
        return;
    }
    dev->stack -= (16 - dev->dstbit);
    egc_sftb_upl_sub(dev, 0);
    if (dev->remain) {
        egc_sftb_upl_sub(dev, 1);
        if (dev->remain)
            return;
    } else
        dev->srcmask.b[1] = 0;

    egc_shift(dev);
}

static void
egc_sftb_dnl0(egc_t *dev, uint32_t ext)
{
    if (dev->stack < ((uint32_t)(8 - dev->dstbit))) {
        dev->srcmask.b[ext] = 0;
        return;
    }
    dev->stack -= (8 - dev->dstbit);
    egc_sftb_dnl_sub(dev, ext);
    if (!dev->remain)
        egc_shift(dev);
}

static void
egc_sftw_dnl0(egc_t *dev)
{
    if (dev->stack < ((uint32_t)(16 - dev->dstbit))) {
        dev->srcmask.w = 0;
        return;
    }
    dev->stack -= (16 - dev->dstbit);
    egc_sftb_dnl_sub(dev, 1);
    if (dev->remain) {
        egc_sftb_dnl_sub(dev, 0);
        if (dev->remain)
            return;
    } else
        dev->srcmask.b[0] = 0;

    egc_shift(dev);
}

/* shift */

typedef void (*PC98EGCSFTB)(egc_t *dev, uint32_t ext);
typedef void (*PC98EGCSFTW)(egc_t *dev);

static const PC98EGCSFTB egc_sftb[6] = {
    egc_sftb_upn0, egc_sftb_dnn0,
    egc_sftb_upr0, egc_sftb_dnr0,
    egc_sftb_upl0, egc_sftb_dnl0
};
static const PC98EGCSFTW egc_sftw[6] = {
    egc_sftw_upn0, egc_sftw_dnn0,
    egc_sftw_upr0, egc_sftw_dnr0,
    egc_sftw_upl0, egc_sftw_dnl0
};

static void
egc_shiftinput_byte(egc_t *dev, uint32_t ext)
{
    if (dev->stack <= 16) {
        if (dev->srcbit >= 8)
            dev->srcbit -= 8;
        else {
            dev->stack += (8 - dev->srcbit);
            dev->srcbit = 0;
        }
        if (!(dev->sft & 0x1000))
            dev->inptr++;
        else
            dev->inptr--;
    }
    dev->srcmask.b[ext] = 0xff;
    (*egc_sftb[dev->func])(dev, ext);
}

static void
egc_shiftinput_incw(egc_t *dev)
{
    if (dev->stack <= 16) {
        dev->inptr += 2;
        if (dev->srcbit >= 8)
            dev->outptr++;

        dev->stack += (16 - dev->srcbit);
        dev->srcbit = 0;
    }
    dev->srcmask.w = 0xffff;
    (*egc_sftw[dev->func])(dev);
}

static void
egc_shiftinput_decw(egc_t *dev)
{
    if (dev->stack <= 16) {
        dev->inptr -= 2;
        if (dev->srcbit >= 8)
            dev->outptr--;

        dev->stack += (16 - dev->srcbit);
        dev->srcbit = 0;
    }
    dev->srcmask.w = 0xffff;
    (*egc_sftw[dev->func])(dev);
}

/* operation */

#define PC98EGC_OPE_SHIFTB \
    do { \
        if (dev->ope & 0x400) { \
            dev->inptr[ 0] = (uint8_t)value; \
            dev->inptr[ 4] = (uint8_t)value; \
            dev->inptr[ 8] = (uint8_t)value; \
            dev->inptr[12] = (uint8_t)value; \
            egc_shiftinput_byte(dev, addr & 1); \
        } \
    } while (0)

#define PC98EGC_OPE_SHIFTW \
    do { \
        if (dev->ope & 0x400) { \
            if (!(dev->sft & 0x1000)) { \
                dev->inptr[ 0] = (uint8_t)value; \
                dev->inptr[ 1] = (uint8_t)(value >> 8); \
                dev->inptr[ 4] = (uint8_t)value; \
                dev->inptr[ 5] = (uint8_t)(value >> 8); \
                dev->inptr[ 8] = (uint8_t)value; \
                dev->inptr[ 9] = (uint8_t)(value >> 8); \
                dev->inptr[12] = (uint8_t)value; \
                dev->inptr[13] = (uint8_t)(value >> 8); \
                egc_shiftinput_incw(dev); \
            } else { \
                dev->inptr[-1] = (uint8_t)value; \
                dev->inptr[ 0] = (uint8_t)(value >> 8); \
                dev->inptr[ 3] = (uint8_t)value; \
                dev->inptr[ 4] = (uint8_t)(value >> 8); \
                dev->inptr[ 7] = (uint8_t)value; \
                dev->inptr[ 8] = (uint8_t)(value >> 8); \
                dev->inptr[11] = (uint8_t)value; \
                dev->inptr[12] = (uint8_t)(value >> 8); \
                egc_shiftinput_decw(dev); \
            }  \
        } \
    } while (0)

static uint64_t
egc_ope_00(egc_t *dev, uint8_t ope, uint32_t addr)
{
    return 0;
}

static uint64_t
egc_ope_0f(egc_t *dev, uint8_t ope, uint32_t addr)
{
    dev->vram_data.d[0] = ~dev->vram_src.d[0];
    dev->vram_data.d[1] = ~dev->vram_src.d[1];
    return dev->vram_data.q;
}

static uint64_t
egc_ope_c0(egc_t *dev, uint8_t ope, uint32_t addr)
{
    egcquad_t dst;

    dst.w[0] = *(uint16_t *)(&dev->vram_b[addr]);
    dst.w[1] = *(uint16_t *)(&dev->vram_r[addr]);
    dst.w[2] = *(uint16_t *)(&dev->vram_g[addr]);
    dst.w[3] = *(uint16_t *)(&dev->vram_e[addr]);
    dev->vram_data.d[0] = (dev->vram_src.d[0] & dst.d[0]);
    dev->vram_data.d[1] = (dev->vram_src.d[1] & dst.d[1]);
    return dev->vram_data.q;
}

static uint64_t
egc_ope_f0(egc_t *dev, uint8_t ope, uint32_t addr)
{
    return dev->vram_src.q;
}

static uint64_t
egc_ope_fc(egc_t *dev, uint8_t ope, uint32_t addr)
{
    egcquad_t dst;

    dst.w[0] = *(uint16_t *)(&dev->vram_b[addr]);
    dst.w[1] = *(uint16_t *)(&dev->vram_r[addr]);
    dst.w[2] = *(uint16_t *)(&dev->vram_g[addr]);
    dst.w[3] = *(uint16_t *)(&dev->vram_e[addr]);
    dev->vram_data.d[0] = dev->vram_src.d[0];
    dev->vram_data.d[0] |= ((~dev->vram_src.d[0]) & dst.d[0]);
    dev->vram_data.d[1] = dev->vram_src.d[1];
    dev->vram_data.d[1] |= ((~dev->vram_src.d[1]) & dst.d[1]);
    return dev->vram_data.q;
}

static uint64_t
egc_ope_ff(egc_t *dev, uint8_t ope, uint32_t addr)
{
    return ~0;
}

static uint64_t
egc_ope_nd(egc_t *dev, uint8_t ope, uint32_t addr)
{
    egcquad_t pat;

    switch(dev->fgbg & 0x6000) {
    case 0x2000:
        pat.d[0] = dev->bgc.d[0];
        pat.d[1] = dev->bgc.d[1];
        break;
    case 0x4000:
        pat.d[0] = dev->fgc.d[0];
        pat.d[1] = dev->fgc.d[1];
        break;
    default:
        if ((dev->ope & 0x0300) == 0x0100) {
            pat.d[0] = dev->vram_src.d[0];
            pat.d[1] = dev->vram_src.d[1];
        } else {
            pat.d[0] = dev->patreg.d[0];
            pat.d[1] = dev->patreg.d[1];
        }
        break;
    }
    dev->vram_data.d[0] = 0;
    dev->vram_data.d[1] = 0;
    if (ope & 0x80) {
        dev->vram_data.d[0] |= (pat.d[0] & dev->vram_src.d[0]);
        dev->vram_data.d[1] |= (pat.d[1] & dev->vram_src.d[1]);
    }
    if (ope & 0x40) {
        dev->vram_data.d[0] |= ((~pat.d[0]) & dev->vram_src.d[0]);
        dev->vram_data.d[1] |= ((~pat.d[1]) & dev->vram_src.d[1]);
    }
    if (ope & 0x08) {
        dev->vram_data.d[0] |= (pat.d[0] & (~dev->vram_src.d[0]));
        dev->vram_data.d[1] |= (pat.d[1] & (~dev->vram_src.d[1]));
    }
    if (ope & 0x04) {
        dev->vram_data.d[0] |= ((~pat.d[0]) & (~dev->vram_src.d[0]));
        dev->vram_data.d[1] |= ((~pat.d[1]) & (~dev->vram_src.d[1]));
    }
    return dev->vram_data.q;
}

static uint64_t
egc_ope_np(egc_t *dev, uint8_t ope, uint32_t addr)
{
    egcquad_t dst;

    dst.w[0] = *(uint16_t *)(&dev->vram_b[addr]);
    dst.w[1] = *(uint16_t *)(&dev->vram_r[addr]);
    dst.w[2] = *(uint16_t *)(&dev->vram_g[addr]);
    dst.w[3] = *(uint16_t *)(&dev->vram_e[addr]);

    dev->vram_data.d[0] = 0;
    dev->vram_data.d[1] = 0;
    if (ope & 0x80) {
        dev->vram_data.d[0] |= (dev->vram_src.d[0] & dst.d[0]);
        dev->vram_data.d[1] |= (dev->vram_src.d[1] & dst.d[1]);
    }
    if (ope & 0x20) {
        dev->vram_data.d[0] |= (dev->vram_src.d[0] & (~dst.d[0]));
        dev->vram_data.d[1] |= (dev->vram_src.d[1] & (~dst.d[1]));
    }
    if (ope & 0x08) {
        dev->vram_data.d[0] |= ((~dev->vram_src.d[0]) & dst.d[0]);
        dev->vram_data.d[1] |= ((~dev->vram_src.d[1]) & dst.d[1]);
    }
    if (ope & 0x02) {
        dev->vram_data.d[0] |= ((~dev->vram_src.d[0]) & (~dst.d[0]));
        dev->vram_data.d[1] |= ((~dev->vram_src.d[1]) & (~dst.d[1]));
    }
    return dev->vram_data.q;
}

static uint64_t
egc_ope_xx(egc_t *dev, uint8_t ope, uint32_t addr)
{
    egcquad_t pat;
    egcquad_t dst;

    switch(dev->fgbg & 0x6000) {
    case 0x2000:
        pat.d[0] = dev->bgc.d[0];
        pat.d[1] = dev->bgc.d[1];
        break;
    case 0x4000:
        pat.d[0] = dev->fgc.d[0];
        pat.d[1] = dev->fgc.d[1];
        break;
    default:
        if ((dev->ope & 0x0300) == 0x0100) {
            pat.d[0] = dev->vram_src.d[0];
            pat.d[1] = dev->vram_src.d[1];
        } else {
            pat.d[0] = dev->patreg.d[0];
            pat.d[1] = dev->patreg.d[1];
        }
        break;
    }
    dst.w[0] = *(uint16_t *)(&dev->vram_b[addr]);
    dst.w[1] = *(uint16_t *)(&dev->vram_r[addr]);
    dst.w[2] = *(uint16_t *)(&dev->vram_g[addr]);
    dst.w[3] = *(uint16_t *)(&dev->vram_e[addr]);

    dev->vram_data.d[0] = 0;
    dev->vram_data.d[1] = 0;
    if (ope & 0x80) {
        dev->vram_data.d[0] |= (pat.d[0] & dev->vram_src.d[0] & dst.d[0]);
        dev->vram_data.d[1] |= (pat.d[1] & dev->vram_src.d[1] & dst.d[1]);
    }
    if (ope & 0x40) {
        dev->vram_data.d[0] |= ((~pat.d[0]) & dev->vram_src.d[0] & dst.d[0]);
        dev->vram_data.d[1] |= ((~pat.d[1]) & dev->vram_src.d[1] & dst.d[1]);
    }
    if (ope & 0x20) {
        dev->vram_data.d[0] |= (pat.d[0] & dev->vram_src.d[0] & (~dst.d[0]));
        dev->vram_data.d[1] |= (pat.d[1] & dev->vram_src.d[1] & (~dst.d[1]));
    }
    if (ope & 0x10) {
        dev->vram_data.d[0] |= ((~pat.d[0]) & dev->vram_src.d[0] & (~dst.d[0]));
        dev->vram_data.d[1] |= ((~pat.d[1]) & dev->vram_src.d[1] & (~dst.d[1]));
    }
    if (ope & 0x08) {
        dev->vram_data.d[0] |= (pat.d[0] & (~dev->vram_src.d[0]) & dst.d[0]);
        dev->vram_data.d[1] |= (pat.d[1] & (~dev->vram_src.d[1]) & dst.d[1]);
    }
    if (ope & 0x04) {
        dev->vram_data.d[0] |= ((~pat.d[0]) & (~dev->vram_src.d[0]) & dst.d[0]);
        dev->vram_data.d[1] |= ((~pat.d[1]) & (~dev->vram_src.d[1]) & dst.d[1]);
    }
    if (ope & 0x02) {
        dev->vram_data.d[0] |= (pat.d[0] & (~dev->vram_src.d[0]) & (~dst.d[0]));
        dev->vram_data.d[1] |= (pat.d[1] & (~dev->vram_src.d[1]) & (~dst.d[1]));
    }
    if (ope & 0x01) {
        dev->vram_data.d[0] |= ((~pat.d[0]) & (~dev->vram_src.d[0]) & (~dst.d[0]));
        dev->vram_data.d[1] |= ((~pat.d[1]) & (~dev->vram_src.d[1]) & (~dst.d[1]));
    }
    return dev->vram_data.q;
}

typedef uint64_t (*PC98EGCOPEFN)(egc_t *dev, uint8_t ope, uint32_t addr);

static const PC98EGCOPEFN egc_opefn[256] = {
    egc_ope_00, egc_ope_xx, egc_ope_xx, egc_ope_np,
    egc_ope_xx, egc_ope_nd, egc_ope_xx, egc_ope_xx,
    egc_ope_xx, egc_ope_xx, egc_ope_nd, egc_ope_xx,
    egc_ope_np, egc_ope_xx, egc_ope_xx, egc_ope_0f,
    egc_ope_xx, egc_ope_xx, egc_ope_xx, egc_ope_xx,
    egc_ope_xx, egc_ope_xx, egc_ope_xx, egc_ope_xx,
    egc_ope_xx, egc_ope_xx, egc_ope_xx, egc_ope_xx,
    egc_ope_xx, egc_ope_xx, egc_ope_xx, egc_ope_xx,
    egc_ope_xx, egc_ope_xx, egc_ope_xx, egc_ope_xx,
    egc_ope_xx, egc_ope_xx, egc_ope_xx, egc_ope_xx,
    egc_ope_xx, egc_ope_xx, egc_ope_xx, egc_ope_xx,
    egc_ope_xx, egc_ope_xx, egc_ope_xx, egc_ope_xx,
    egc_ope_np, egc_ope_xx, egc_ope_xx, egc_ope_np,
    egc_ope_xx, egc_ope_xx, egc_ope_xx, egc_ope_xx,
    egc_ope_xx, egc_ope_xx, egc_ope_xx, egc_ope_xx,
    egc_ope_np, egc_ope_xx, egc_ope_xx, egc_ope_np,
    egc_ope_xx, egc_ope_xx, egc_ope_xx, egc_ope_xx,
    egc_ope_xx, egc_ope_xx, egc_ope_xx, egc_ope_xx,
    egc_ope_xx, egc_ope_xx, egc_ope_xx, egc_ope_xx,
    egc_ope_xx, egc_ope_xx, egc_ope_xx, egc_ope_xx,
    egc_ope_nd, egc_ope_xx, egc_ope_xx, egc_ope_xx,
    egc_ope_xx, egc_ope_nd, egc_ope_xx, egc_ope_xx,
    egc_ope_xx, egc_ope_xx, egc_ope_nd, egc_ope_xx,
    egc_ope_xx, egc_ope_xx, egc_ope_xx, egc_ope_nd,
    egc_ope_xx, egc_ope_xx, egc_ope_xx, egc_ope_xx,
    egc_ope_xx, egc_ope_xx, egc_ope_xx, egc_ope_xx,
    egc_ope_xx, egc_ope_xx, egc_ope_xx, egc_ope_xx,
    egc_ope_xx, egc_ope_xx, egc_ope_xx, egc_ope_xx,
    egc_ope_xx, egc_ope_xx, egc_ope_xx, egc_ope_xx,
    egc_ope_xx, egc_ope_xx, egc_ope_xx, egc_ope_xx,
    egc_ope_xx, egc_ope_xx, egc_ope_xx, egc_ope_xx,
    egc_ope_xx, egc_ope_xx, egc_ope_xx, egc_ope_xx,
    egc_ope_xx, egc_ope_xx, egc_ope_xx, egc_ope_xx,
    egc_ope_xx, egc_ope_xx, egc_ope_xx, egc_ope_xx,
    egc_ope_xx, egc_ope_xx, egc_ope_xx, egc_ope_xx,
    egc_ope_xx, egc_ope_xx, egc_ope_xx, egc_ope_xx,
    egc_ope_xx, egc_ope_xx, egc_ope_xx, egc_ope_xx,
    egc_ope_xx, egc_ope_xx, egc_ope_xx, egc_ope_xx,
    egc_ope_xx, egc_ope_xx, egc_ope_xx, egc_ope_xx,
    egc_ope_xx, egc_ope_xx, egc_ope_xx, egc_ope_xx,
    egc_ope_nd, egc_ope_xx, egc_ope_xx, egc_ope_xx,
    egc_ope_xx, egc_ope_nd, egc_ope_xx, egc_ope_xx,
    egc_ope_xx, egc_ope_xx, egc_ope_nd, egc_ope_xx,
    egc_ope_xx, egc_ope_xx, egc_ope_xx, egc_ope_nd,
    egc_ope_xx, egc_ope_xx, egc_ope_xx, egc_ope_xx,
    egc_ope_xx, egc_ope_xx, egc_ope_xx, egc_ope_xx,
    egc_ope_xx, egc_ope_xx, egc_ope_xx, egc_ope_xx,
    egc_ope_xx, egc_ope_xx, egc_ope_xx, egc_ope_xx,
    egc_ope_c0, egc_ope_xx, egc_ope_xx, egc_ope_np,
    egc_ope_xx, egc_ope_xx, egc_ope_xx, egc_ope_xx,
    egc_ope_xx, egc_ope_xx, egc_ope_xx, egc_ope_xx,
    egc_ope_np, egc_ope_xx, egc_ope_xx, egc_ope_np,
    egc_ope_xx, egc_ope_xx, egc_ope_xx, egc_ope_xx,
    egc_ope_xx, egc_ope_xx, egc_ope_xx, egc_ope_xx,
    egc_ope_xx, egc_ope_xx, egc_ope_xx, egc_ope_xx,
    egc_ope_xx, egc_ope_xx, egc_ope_xx, egc_ope_xx,
    egc_ope_xx, egc_ope_xx, egc_ope_xx, egc_ope_xx,
    egc_ope_xx, egc_ope_xx, egc_ope_xx, egc_ope_xx,
    egc_ope_xx, egc_ope_xx, egc_ope_xx, egc_ope_xx,
    egc_ope_xx, egc_ope_xx, egc_ope_xx, egc_ope_xx,
    egc_ope_f0, egc_ope_xx, egc_ope_xx, egc_ope_np,
    egc_ope_xx, egc_ope_nd, egc_ope_xx, egc_ope_xx,
    egc_ope_xx, egc_ope_xx, egc_ope_nd, egc_ope_xx,
    egc_ope_fc, egc_ope_xx, egc_ope_xx, egc_ope_ff
};

static uint64_t
egc_opeb(egc_t *dev, uint32_t addr, uint8_t value)
{
    uint32_t tmp;

    dev->mask2.w = dev->mask.w;
    switch(dev->ope & 0x1800) {
        case 0x0800:
            PC98EGC_OPE_SHIFTB;
            dev->mask2.w &= dev->srcmask.w;
            tmp = dev->ope & 0xff;
            return (*egc_opefn[tmp])(dev, (uint8_t)tmp, addr & (~1));
        case 0x1000:
            switch(dev->fgbg & 0x6000) {
                case 0x2000:
                    return dev->bgc.q;
                case 0x4000:
                    return dev->fgc.q;
                default:
                    PC98EGC_OPE_SHIFTB;
                    dev->mask2.w &= dev->srcmask.w;
                    return dev->vram_src.q;
            }
            break;
        default:
            tmp = value & 0xff;
            tmp = tmp | (tmp << 8);
            dev->vram_data.w[0] = (uint16_t)tmp;
            dev->vram_data.w[1] = (uint16_t)tmp;
            dev->vram_data.w[2] = (uint16_t)tmp;
            dev->vram_data.w[3] = (uint16_t)tmp;
            return dev->vram_data.q;
    }
}

static uint64_t
egc_opew(egc_t *dev, uint32_t addr, uint16_t value)
{
    uint32_t tmp;

    dev->mask2.w = dev->mask.w;
    switch(dev->ope & 0x1800) {
        case 0x0800:
            PC98EGC_OPE_SHIFTW;
            dev->mask2.w &= dev->srcmask.w;
            tmp = dev->ope & 0xff;
            return (*egc_opefn[tmp])(dev, (uint8_t)tmp, addr);
        case 0x1000:
            switch(dev->fgbg & 0x6000) {
                case 0x2000:
                    return dev->bgc.q;
                case 0x4000:
                    return dev->fgc.q;
                default:
                    PC98EGC_OPE_SHIFTW;
                    dev->mask2.w &= dev->srcmask.w;
                    return dev->vram_src.q;
            }
            break;
        default:
            dev->vram_data.w[0] = (uint16_t)value;
            dev->vram_data.w[1] = (uint16_t)value;
            dev->vram_data.w[2] = (uint16_t)value;
            dev->vram_data.w[3] = (uint16_t)value;
            return dev->vram_data.q;
    }
}

/* memory */

static const uint16_t egc_maskword[16][4] = {
    {0x0000, 0x0000, 0x0000, 0x0000}, {0xffff, 0x0000, 0x0000, 0x0000},
    {0x0000, 0xffff, 0x0000, 0x0000}, {0xffff, 0xffff, 0x0000, 0x0000},
    {0x0000, 0x0000, 0xffff, 0x0000}, {0xffff, 0x0000, 0xffff, 0x0000},
    {0x0000, 0xffff, 0xffff, 0x0000}, {0xffff, 0xffff, 0xffff, 0x0000},
    {0x0000, 0x0000, 0x0000, 0xffff}, {0xffff, 0x0000, 0x0000, 0xffff},
    {0x0000, 0xffff, 0x0000, 0xffff}, {0xffff, 0xffff, 0x0000, 0xffff},
    {0x0000, 0x0000, 0xffff, 0xffff}, {0xffff, 0x0000, 0xffff, 0xffff},
    {0x0000, 0xffff, 0xffff, 0xffff}, {0xffff, 0xffff, 0xffff, 0xffff}
};


void
egc_mem_writeb(egc_t *dev, uint32_t addr1, uint8_t value)
{
    uint32_t addr = addr1 & 0x7fff;
    uint32_t ext = addr1 & 1;
    egcquad_t data;

    if ((dev->ope & 0x0300) == 0x0200) {
        dev->patreg.b[0][ext] = dev->vram_b[addr];
        dev->patreg.b[1][ext] = dev->vram_r[addr];
        dev->patreg.b[2][ext] = dev->vram_g[addr];
        dev->patreg.b[3][ext] = dev->vram_e[addr];
    }
    data.q = egc_opeb(dev, addr, value);
    if (dev->mask2.b[ext]) {
        if (!(dev->access & 1)) {
            dev->vram_b[addr] &= ~dev->mask2.b[ext];
            dev->vram_b[addr] |= data.b[0][ext] & dev->mask2.b[ext];
        }
        if (!(dev->access & 2)) {
            dev->vram_r[addr] &= ~dev->mask2.b[ext];
            dev->vram_r[addr] |= data.b[1][ext] & dev->mask2.b[ext];
        }
        if (!(dev->access & 4)) {
            dev->vram_g[addr] &= ~dev->mask2.b[ext];
            dev->vram_g[addr] |= data.b[2][ext] & dev->mask2.b[ext];
        }
        if (!(dev->access & 8)) {
            dev->vram_e[addr] &= ~dev->mask2.b[ext];
            dev->vram_e[addr] |= data.b[3][ext] & dev->mask2.b[ext];
        }
    }
}

void
egc_mem_writew(egc_t *dev, uint32_t addr1, uint16_t value)
{
    uint32_t addr = addr1 & 0x7fff;
    egcquad_t data;

    if (!(addr & 1)) {
        if ((dev->ope & 0x0300) == 0x0200) {
            dev->patreg.w[0] = *(uint16_t *)(&dev->vram_b[addr]);
            dev->patreg.w[1] = *(uint16_t *)(&dev->vram_r[addr]);
            dev->patreg.w[2] = *(uint16_t *)(&dev->vram_g[addr]);
            dev->patreg.w[3] = *(uint16_t *)(&dev->vram_e[addr]);
        }
        data.q = egc_opew(dev, addr, value);
        if (dev->mask2.w) {
            if (!(dev->access & 1)) {
                *(uint16_t *)(&dev->vram_b[addr]) &= ~dev->mask2.w;
                *(uint16_t *)(&dev->vram_b[addr]) |= data.w[0] & dev->mask2.w;
            }
            if (!(dev->access & 2)) {
                *(uint16_t *)(&dev->vram_r[addr]) &= ~dev->mask2.w;
                *(uint16_t *)(&dev->vram_r[addr]) |= data.w[1] & dev->mask2.w;
            }
            if (!(dev->access & 4)) {
                *(uint16_t *)(&dev->vram_g[addr]) &= ~dev->mask2.w;
                *(uint16_t *)(&dev->vram_g[addr]) |= data.w[2] & dev->mask2.w;
            }
            if (!(dev->access & 8)) {
                *(uint16_t *)(&dev->vram_e[addr]) &= ~dev->mask2.w;
                *(uint16_t *)(&dev->vram_e[addr]) |= data.w[3] & dev->mask2.w;
            }
        }
    } else if (!(dev->sft & 0x1000)) {
        egc_mem_writeb(s, addr1, value & 0xff);
        egc_mem_writeb(s, addr1 + 1, (value >> 8) & 0xff);
    } else {
        egc_mem_writeb(s, addr1, (value >> 8) & 0xff);
        egc_mem_writeb(s, addr1 + 1, value & 0xff);
    }
}

uint8_t
egc_mem_readb(egc_t *dev, uint32_t addr1)
{
    uint32_t addr = addr1 & 0x7fff;
    uint32_t ext = addr1 & 1;

    dev->lastvram.b[0][ext] = dev->vram_b[addr];
    dev->lastvram.b[1][ext] = dev->vram_r[addr];
    dev->lastvram.b[2][ext] = dev->vram_g[addr];
    dev->lastvram.b[3][ext] = dev->vram_e[addr];

    if (!(dev->ope & 0x400)) {
        dev->inptr[0] = dev->lastvram.b[0][ext];
        dev->inptr[4] = dev->lastvram.b[1][ext];
        dev->inptr[8] = dev->lastvram.b[2][ext];
        dev->inptr[12] = dev->lastvram.b[3][ext];
        egc_shiftinput_byte(dev, ext);
    }
    if ((dev->ope & 0x0300) == 0x0100) {
        dev->patreg.b[0][ext] = dev->vram_b[addr];
        dev->patreg.b[1][ext] = dev->vram_r[addr];
        dev->patreg.b[2][ext] = dev->vram_g[addr];
        dev->patreg.b[3][ext] = dev->vram_e[addr];
    }
    if (!(dev->ope & 0x2000)) {
        int pl = (dev->fgbg >> 8) & 3;
        if (!(dev->ope & 0x400))
            return dev->vram_src.b[pl][ext];
        else
            return dev->vram_ptr[addr | (0x8000 * pl)];
    }
    return dev->vram_ptr[addr1];
}

uint16_t
egc_mem_readw(egc_t *dev, uint32_t addr1)
{
    uint32_t addr = addr1 & 0x7fff;
    uint16_t value;

    if (!(addr & 1)) {
        dev->lastvram.w[0] = *(uint16_t *)(&dev->vram_b[addr]);
        dev->lastvram.w[1] = *(uint16_t *)(&dev->vram_r[addr]);
        dev->lastvram.w[2] = *(uint16_t *)(&dev->vram_g[addr]);
        dev->lastvram.w[3] = *(uint16_t *)(&dev->vram_e[addr]);

        if (!(dev->ope & 0x400)) {
            if (!(dev->sft & 0x1000)) {
                dev->inptr[ 0] = dev->lastvram.b[0][0];
                dev->inptr[ 1] = dev->lastvram.b[0][1];
                dev->inptr[ 4] = dev->lastvram.b[1][0];
                dev->inptr[ 5] = dev->lastvram.b[1][1];
                dev->inptr[ 8] = dev->lastvram.b[2][0];
                dev->inptr[ 9] = dev->lastvram.b[2][1];
                dev->inptr[12] = dev->lastvram.b[3][0];
                dev->inptr[13] = dev->lastvram.b[3][1];
                egc_shiftinput_incw(dev);
            } else {
                dev->inptr[-1] = dev->lastvram.b[0][0];
                dev->inptr[ 0] = dev->lastvram.b[0][1];
                dev->inptr[ 3] = dev->lastvram.b[1][0];
                dev->inptr[ 4] = dev->lastvram.b[1][1];
                dev->inptr[ 7] = dev->lastvram.b[2][0];
                dev->inptr[ 8] = dev->lastvram.b[2][1];
                dev->inptr[11] = dev->lastvram.b[3][0];
                dev->inptr[12] = dev->lastvram.b[3][1];
                egc_shiftinput_decw(dev);
            }
        }
        if ((dev->ope & 0x0300) == 0x0100) {
            dev->patreg.d[0] = dev->lastvram.d[0];
            dev->patreg.d[1] = dev->lastvram.d[1];
        }
        if (!(dev->ope & 0x2000)) {
            int pl = (dev->fgbg >> 8) & 3;
            if (!(dev->ope & 0x400))
                return dev->vram_src.w[pl];
            else
                return *(uint16_t *)(&dev->vram_ptr[addr | (0x8000 * pl)]);
        }
        return *(uint16_t *)(&dev->vram_ptr[addr1]);
    } else if (!(dev->sft & 0x1000)) {
        value = egc_mem_readb(dev, addr1);
        value |= (egc_mem_readb(dev, addr1 + 1) << 8);
        return value;
    } else {
        value = (egc_mem_readb(dev, addr1) << 8);
        value |= egc_mem_readb(dev, addr1 + 1);
        return value;
    }
}

/* i/o */

void
egc_ioport_writeb(uint16_t addr, uint8_t value, void *priv)
{
    /* ioport 0x4a0 - 0x4af */
    egc_t *dev = (egc_t *)priv;
    pc98x1_vid_t *vid = (pc98x1_vid_t *)dev->priv;

    if (!((vid->grcg_mode & GRCG_CG_MODE) && vid->mode2[MODE2_EGC]))
        return;

    switch (addr) {
        case 0x4a0:
            dev->access &= 0xff00;
            dev->access |= value;
            break;
        case 0x4a1:
            dev->access &= 0x00ff;
            dev->access |= value << 8;
            break;
        case 0x4a2:
            dev->fgbg &= 0xff00;
            dev->fgbg |= value;
            break;
        case 0x4a3:
            dev->fgbg &= 0x00ff;
            dev->fgbg |= value << 8;
            break;
        case 0x4a4:
            dev->ope &= 0xff00;
            dev->ope |= value;
            break;
        case 0x4a5:
            dev->ope &= 0x00ff;
            dev->ope |= value << 8;
            break;
        case 0x4a6:
            dev->fg &= 0xff00;
            dev->fg |= value;
            dev->fgc.d[0] = *(uint32_t *)(egc_maskword[value & 0x0f] + 0);
            dev->fgc.d[1] = *(uint32_t *)(egc_maskword[value & 0x0f] + 2);
            break;
        case 0x4a7:
            dev->fg &= 0x00ff;
            dev->fg |= value << 8;
            break;
        case 0x4a8:
            if (!(dev->fgbg & 0x6000))
                dev->mask.b[0] = value;
            break;
        case 0x4a9:
            if (!(dev->fgbg & 0x6000))
                dev->mask.b[1] = value;
            break;
        case 0x4aa:
            dev->bg &= 0xff00;
            dev->bg |= value;
            dev->bgc.d[0] = *(uint32_t *)(egc_maskword[value & 0x0f] + 0);
            dev->bgc.d[1] = *(uint32_t *)(egc_maskword[value & 0x0f] + 2);
            break;
        case 0x4ab:
            dev->bg &= 0x00ff;
            dev->bg |= value << 8;
            break;
        case 0x4ac:
            dev->sft &= 0xff00;
            dev->sft |= value;
            egc_shift(dev);
            dev->srcmask.w = 0xffff;
            break;
        case 0x4ad:
            dev->sft &= 0x00ff;
            dev->sft |= value << 8;
            egc_shift(dev);
            dev->srcmask.w = 0xffff;
            break;
        case 0x4ae:
            dev->leng &= 0xff00;
            dev->leng |= value;
            egc_shift(dev);
            dev->srcmask.w = 0xffff;
            break;
        case 0x4af:
            dev->leng &= 0x00ff;
            dev->leng |= value << 8;
            egc_shift(dev);
            dev->srcmask.w = 0xffff;
            break;
    }
}

void
egc_ioport_writew(uint16_t addr, uint16_t value, void *priv)
{
    /* ioport 0x4a0 - 0x4af */
    egc_t *dev = (egc_t *)priv;
    pc98x1_vid_t *vid = (pc98x1_vid_t *)dev->priv;

    if (!((vid->grcg_mode & GRCG_CG_MODE) && vid->mode2[MODE2_EGC]))
        return;

    switch(addr) {
        case 0x4a0:
            dev->access = value;
            break;
        case 0x4a2:
            dev->fgbg = value;
            break;
        case 0x4a4:
            dev->ope = value;
            break;
        case 0x4a6:
            dev->fg = value;
            dev->fgc.d[0] = *(uint32_t *)(egc_maskword[value & 0x0f] + 0);
            dev->fgc.d[1] = *(uint32_t *)(egc_maskword[value & 0x0f] + 2);
            break;
        case 0x4a8:
            if (!(dev->fgbg & 0x6000))
                dev->mask.w = value;
            break;
        case 0x4aa:
            dev->bg = value;
            dev->bgc.d[0] = *(uint32_t *)(egc_maskword[value & 0x0f] + 0);
            dev->bgc.d[1] = *(uint32_t *)(egc_maskword[value & 0x0f] + 2);
            break;
        case 0x4ac:
            dev->sft = value;
            egc_shift(dev);
            dev->srcmask.w = 0xffff;
            break;
        case 0x4ae:
            dev->leng = value;
            egc_shift(dev);
            dev->srcmask.w = 0xffff;
            break;
    }
}

/* interface */

void
egc_set_vram(egc_t *dev, uint8_t *vram_ptr)
{
    dev->vram_ptr = vram_ptr;
    dev->vram_b = vram_ptr + 0x00000;
    dev->vram_r = vram_ptr + 0x08000;
    dev->vram_g = vram_ptr + 0x10000;
    dev->vram_e = vram_ptr + 0x18000;
}

void
egc_reset(egc_t *dev)
{
    dev->access = 0xfff0;
    dev->fgbg = 0x00ff;
    dev->mask.w = 0xffff;
    dev->leng = 0x000f;
    egc_shift(dev);
    dev->srcmask.w = 0xffff;
}

void
egc_init(egc_t *dev, void *priv)
{
    dev->priv = priv;
    dev->inptr = dev->buf;
    dev->outptr = dev->buf;
}
