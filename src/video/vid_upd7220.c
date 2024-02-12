/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          Emulation of the NEC uPD7220 graphic display controller.
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
#include <86box/vid_upd7220.h>
#include <86box/vid_pc98x1_disp.h>
#include <86box/plat_unused.h>

/***********************************************************/
/* NEC uPD7220 GDC (based on Neko Project 2) */

/*
 * PC-9821 CRT sync timing (24.83KHz/400L monitor)
 *
 * VCLOCK = 56.43;
 * VLINES = 440;
*/

/* draw command */

static const int gdc_vectdir[16][4] = {
    { 0, 1, 1, 0}, { 1, 1, 1,-1}, { 1, 0, 0,-1}, { 1,-1,-1,-1},
    { 0,-1,-1, 0}, {-1,-1,-1, 1}, {-1, 0, 0, 1}, {-1, 1, 1, 1},
    { 0, 1, 1, 1}, { 1, 1, 1, 0}, { 1, 0, 1,-1}, { 1,-1, 0,-1},
    { 0,-1,-1,-1}, {-1,-1,-1, 0}, {-1, 0,-1, 1}, {-1, 1, 0, 1}
};

void
upd7220_recalctimings(upd7220_t *dev)
{
    pc98x1_vid_t *vid = (pc98x1_vid_t *) dev->priv;
    double        crtcconst;
    double        _dispontime;
    double        _dispofftime;

    if (vid->mode2[MODE2_256COLOR] && vid->mode2[MODE2_480LINE])
        vid->clock = (uint64_t)(cpuclock / 25175000.0 * (double) (1ULL << 32));
    else
        vid->clock = (uint64_t)(cpuclock / 21052600.0 * (double) (1ULL << 32));

    vid->width = MIN(96, dev->sync[1] + 2);
    vid->height = MIN(512, ((dev->sync[7] & 0x03) << 8) | (dev->sync[6]));

    vid->hblank = ((dev->sync[2] & 0x1f) + (dev->sync[4] & 0x3f) + 2) * 8;
    vid->htotal = ((dev->sync[3] >> 2) + 1) * 8;

    crtcconst = vid->clock * 8.0;
    _dispontime = vid->htotal - vid->hblank;
    _dispofftime = vid->hblank;

    _dispontime *= crtcconst;
    _dispofftime *= crtcconst;

    vid->dispontime  = (uint64_t) (_dispontime);
    vid->dispofftime = (uint64_t) (_dispofftime);
    if (vid->dispontime < TIMER_USEC)
        vid->dispontime = TIMER_USEC;
    if (vid->dispofftime < TIMER_USEC)
        vid->dispofftime = TIMER_USEC;
}

static void
upd7220_draw_pset(upd7220_t *dev, int x, int y)
{
    uint16_t dot = dev->pattern & 1;
    uint32_t addr = (y * 80) + (x >> 3) + 0x8000;
    uint8_t bit = 0x80 >> (x & 7);
    uint8_t cur = dev->vram_read(dev->priv, addr);

    dev->pattern = (dev->pattern >> 1) | (dot << 15);

    switch (dev->mod) {
    case 0: /* replace */
        dev->vram_write(dev->priv, addr, (cur & ~bit) | (dot ? bit : 0));
        break;
    case 1: /* complement */
        dev->vram_write(dev->priv, addr, (cur & ~bit) | ((cur ^ (dot ? 0xff : 0)) & bit));
        break;
    case 2: /* reset */
        dev->vram_write(dev->priv, addr, cur & (dot ? ~bit : 0xff));
        break;
    case 3: /* set */
        dev->vram_write(dev->priv, addr, cur | (dot ? bit : 0));
        break;
    }
    dev->dirty |= GDC_DIRTY_VRAM;
}

static void
upd7220_draw_vectl(upd7220_t *dev)
{
    int x, i;
    int step;

    dev->pattern = dev->ra[8] | (dev->ra[9] << 8);
    if (dev->dc) {
        x = dev->dx;
        y = dev->dy;

        switch (dev->dir) {
        case 0:
            for (i = 0; i <= dev->dc; i++) {
                step = (int)((((dev->d1 * i) / dev->dc) + 1) >> 1);
                upd7220_draw_pset(dev, x + step, y++);
            }
            break;
        case 1:
            for (i = 0; i <= dev->dc; i++) {
                step = (int)((((dev->d1 * i) / dev->dc) + 1) >> 1);
                upd7220_draw_pset(dev, x++, y + step);
            }
            break;
        case 2:
            for (i = 0; i <= dev->dc; i++) {
                step = (int)((((dev->d1 * i) / dev->dc) + 1) >> 1);
                upd7220_draw_pset(dev, x++, y - step);
            }
            break;
        case 3:
            for (i = 0; i <= dev->dc; i++) {
                step = (int)((((dev->d1 * i) / dev->dc) + 1) >> 1);
                upd7220_draw_pset(dev, x + step, y--);
            }
            break;
        case 4:
            for (i = 0; i <= dev->dc; i++) {
                step = (int)((((dev->d1 * i) / dev->dc) + 1) >> 1);
                upd7220_draw_pset(dev, x - step, y--);
            }
            break;
        case 5:
            for (i = 0; i <= dev->dc; i++) {
                step = (int)((((dev->d1 * i) / dev->dc) + 1) >> 1);
                upd7220_draw_pset(dev, x--, y - step);
            }
            break;
        case 6:
            for (i = 0; i <= dev->dc; i++) {
                step = (int)((((dev->d1 * i) / dev->dc) + 1) >> 1);
                upd7220_draw_pset(dev, x--, y + step);
            }
            break;
        case 7:
            for (i = 0; i <= dev->dc; i++) {
                step = (int)((((dev->d1 * i) / dev->dc) + 1) >> 1);
                upd7220_draw_pset(dev, x - step, y++);
            }
            break;
        }
    } else
        upd7220_draw_pset(dev, dev->dx, dev->dy);
}

static void
upd7220_draw_vectt(upd7220_t *dev)
{
    int vx1 = gdc_vectdir[dev->dir][0];
    int vy1 = gdc_vectdir[dev->dir][1];
    int vx2 = gdc_vectdir[dev->dir][2];
    int vy2 = gdc_vectdir[dev->dir][3];
    int muly = dev->zw + 1;
    uint16_t draw = dev->ra[8] | (dev->ra[9] << 8);
    int cx, cy, xrem, mulx;

    if (dev->sl) {
        draw = (draw & 0x0001 ? 0x8000 : 0) | (draw & 0x0002 ? 0x4000 : 0) |
               (draw & 0x0004 ? 0x2000 : 0) | (draw & 0x0008 ? 0x1000 : 0) |
               (draw & 0x0010 ? 0x0800 : 0) | (draw & 0x0020 ? 0x0400 : 0) |
               (draw & 0x0040 ? 0x0200 : 0) | (draw & 0x0080 ? 0x0100 : 0) |
               (draw & 0x0100 ? 0x0080 : 0) | (draw & 0x0200 ? 0x0040 : 0) |
               (draw & 0x0400 ? 0x0020 : 0) | (draw & 0x0800 ? 0x0010 : 0) |
               (draw & 0x1000 ? 0x0008 : 0) | (draw & 0x2000 ? 0x0004 : 0) |
               (draw & 0x8000 ? 0x0002 : 0) | (draw & 0x8000 ? 0x0001 : 0);
    }
    dev->pattern = 0xffff;
    while (muly--) {
        cx = dev->dx;
        cy = dev->dy;
        xrem = dev->d;
        while (xrem--) {
            mulx = dev->zw + 1;
            if (draw & 1) {
                draw >>= 1;
                draw |= 0x8000;
                while (mulx--) {
                    upd7220_draw_pset(dev, cx, cy);
                    cx += vx1;
                    cy += vy1;
                }
            } else {
                draw >>= 1;
                while (mulx--) {
                    cx += vx1;
                    cy += vy1;
                }
            }
        }
        dev->dx += vx2;
        dev->dy += vy2;
    }
    dev->ead = ((dev->dx >> 4) + dev->dy) * dev->pitch;
    dev->dad = dev->dx & 0x0f;
}

static void
upd7220_draw_vectc(upd7220_t *dev)
{
    int m = (dev->d * 10000 + 14141) / 14142;
    int t = (dev->dc > m) ? m : dev->dc;
    int i, c;

    dev->pattern = dev->ra[8] | (dev->ra[9] << 8);
    if (m) {
        switch (dev->dir) {
        case 0:
            for (i = dev->dm; i <= t; i++) {
                c = (dev->rt[(i << GDC_TABLEBIT) / m] * dev->d);
                c = (c + (1 << (GDC_MULBIT - 1))) >> GDC_MULBIT;
                upd7220_draw_pset(dev, (dev->dx + c), (dev->dy + i));
            }
            break;
        case 1:
            for (i = dev->dm; i <= t; i++) {
                c = (dev->rt[(i << GDC_TABLEBIT) / m] * dev->d);
                c = (c + (1 << (GDC_MULBIT - 1))) >> GDC_MULBIT;
                upd7220_draw_pset(dev, (dev->dx + i), (dev->dy + c));
            }
            break;
        case 2:
            for (i = dev->dm; i <= t; i++) {
                c = (dev->rt[(i << GDC_TABLEBIT) / m] * dev->d);
                c = (c + (1 << (GDC_MULBIT - 1))) >> GDC_MULBIT;
                upd7220_draw_pset(dev, (dev->dx + i), (dev->dy - c));
            }
            break;
        case 3:
            for (i = dev->dm; i <= t; i++) {
                c = (dev->rt[(i << GDC_TABLEBIT) / m] * dev->d);
                c = (c + (1 << (GDC_MULBIT - 1))) >> GDC_MULBIT;
                upd7220_draw_pset(dev, (dev->dx + c), (dev->dy - i));
            }
            break;
        case 4:
            for (i = dev->dm; i <= t; i++) {
                c = (dev->rt[(i << GDC_TABLEBIT) / m] * dev->d);
                c = (c + (1 << (GDC_MULBIT - 1))) >> GDC_MULBIT;
                upd7220_draw_pset(dev, (dev->dx - c), (dev->dy - i));
            }
            break;
        case 5:
            for (i = dev->dm; i <= t; i++) {
                c = (dev->rt[(i << GDC_TABLEBIT) / m] * dev->d);
                c = (c + (1 << (GDC_MULBIT - 1))) >> GDC_MULBIT;
                upd7220_draw_pset(dev, (dev->dx - i), (dev->dy - c));
            }
            break;
        case 6:
            for (i = dev->dm; i <= t; i++) {
                c = (dev->rt[(i << GDC_TABLEBIT) / m] * dev->d);
                c = (c + (1 << (GDC_MULBIT - 1))) >> GDC_MULBIT;
                upd7220_draw_pset(dev, (dev->dx - i), (dev->dy + c));
            }
            break;
        case 7:
            for (i = dev->dm; i <= t; i++) {
                c = (dev->rt[(i << GDC_TABLEBIT) / m] * dev->d);
                c = (c + (1 << (GDC_MULBIT - 1))) >> GDC_MULBIT;
                upd7220_draw_pset(dev, (dev->dx - c), (dev->dy + i));
            }
            break;
        }
    } else
        upd7220_draw_pset(dev, dev->dx, dev->dy);
}

static void
upd7220_draw_vectr(upd7220_t *dev)
{
    int vx1 = gdc_vectdir[dev->dir][0];
    int vy1 = gdc_vectdir[dev->dir][1];
    int vx2 = gdc_vectdir[dev->dir][2];
    int vy2 = gdc_vectdir[dev->dir][3];
    int i;

    dev->pattern = dev->ra[8] | (dev->ra[9] << 8);
    for (i = 0; i < dev->d; i++) {
        upd7220_draw_pset(dev, dev->dx, dev->dy);
        dev->dx += vx1;
        dev->dy += vy1;
    }
    for (i = 0; i < dev->d2; i++) {
        upd7220_draw_pset(dev, dev->dx, dev->dy);
        dev->dx += vx2;
        dev->dy += vy2;
    }
    for (i = 0; i < dev->d; i++) {
        upd7220_draw_pset(dev, dev->dx, dev->dy);
        dev->dx -= vx1;
        dev->dy -= vy1;
    }
    for (i = 0; i < dev->d2; i++) {
        upd7220_draw_pset(dev, dev->dx, dev->dy);
        dev->dx -= vx2;
        dev->dy -= vy2;
    }
    dev->ead = (dev->dx >> 4) + dev->dy * dev->pitch;
    dev->dad = dev->dx & 0x0f;
}

static void
upd7220_draw_text(upd7220_t *dev)
{
    int dir = dev->dir + (dev->sl ? 8 : 0);
    int vx1 = gdc_vectdir[dir][0];
    int vy1 = gdc_vectdir[dir][1];
    int vx2 = gdc_vectdir[dir][2];
    int vy2 = gdc_vectdir[dir][3];
    int sx = dev->d;
    int sy = dev->dc + 1;
    int index = 15;
    int mulx, muly, cx, cy;
    int xrem;
    uint8_t bit;

    while (sy--) {
        muly = dev->zw + 1;
        while (muly--) {
            cx = dev->dx;
            cy = dev->dy;
            bit = dev->ra[index];
            xrem = sx;
            while (xrem--) {
                dev->pattern = (bit & 1) ? 0xffff : 0;
                bit = (bit >> 1) | ((bit & 1) ? 0x80 : 0);
                mulx = dev->zw + 1;
                while (mulx--) {
                    upd7220_draw_pset(dev, cx, cy);
                    cx += vx1;
                    cy += vy1;
                }
            }
            dev->dx += vx2;
            dev->dy += vy2;
        }
        index = ((index - 1) & 7) | 8;
    }
    dev->ead = (dev->dx >> 4) + dev->dy * dev->pitch;
    dev->dad = dev->dx & 0x0f;
}

/* command sub */

static void
upd7220_update_vect(upd7220_t *dev)
{
    dev->dir = dev->vect[0] & 7;
    dev->diff = gdc_vectdir[dev->dir][0] + gdc_vectdir[dev->dir][1] * dev->pitch;
    dev->sl = dev->vect[0] & 0x80;
    dev->dc = (dev->vect[1] | (dev->vect[ 2] << 8)) & 0x3fff;
    dev->d  = (dev->vect[3] | (dev->vect[ 4] << 8)) & 0x3fff;
    dev->d2 = (dev->vect[5] | (dev->vect[ 6] << 8)) & 0x3fff;
    dev->d1 = (dev->vect[7] | (dev->vect[ 8] << 8)) & 0x3fff;
    dev->dm = (dev->vect[9] | (dev->vect[10] << 8)) & 0x3fff;
}

static void
upd7220_reset_vect(upd7220_t *dev)
{
    dev->vect[ 1] = 0;
    dev->vect[ 2] = 0;
    dev->vect[ 3] = 8;
    dev->vect[ 4] = 0;
    dev->vect[ 5] = 8;
    dev->vect[ 6] = 0;
    dev->vect[ 7] = 0;
    dev->vect[ 8] = 0;
    dev->vect[ 9] = 0;
    dev->vect[10] = 0;
    upd7220_update_vect(dev);
}

static void
upd7220_write_sub(upd7220_t *dev, uint32_t addr, uint8_t value)
{
    switch (dev->mod) {
        case 0: /* replace */
            dev->vram_write(dev->priv, addr, value);
            break;
        case 1: /* complement */
            dev->vram_write(dev->priv, addr, dev->vram_read(dev->priv, addr) ^ value);
            break;
        case 2: /* reset */
            dev->vram_write(dev->priv, addr, dev->vram_read(dev->priv, addr) & ~value);
            break;
        case 3: /* set */
            dev->vram_write(dev->priv, addr, dev->vram_read(dev->priv addr) | value);
            break;
    }
    dev->dirty |= GDC_DIRTY_VRAM;
}

static uint8_t
upd7220_read_sub(upd7220_t *dev, uint32_t addr)
{
    return dev->vram_read(dev->priv, addr);
}

static void
upd7220_fifo_write(upd7220_t *dev, uint8_t value)
{
    if (dev->data_count < GDC_BUFFERS) {
        dev->data[(dev->data_write++) & (GDC_BUFFERS - 1)] = value;
        dev->data_count++;
    }
}

static uint8_t
upd7220_fifo_read(upd7220_t *dev)
{
    uint8_t value;

    if (dev->data_count > 0) {
        value = dev->data[(dev->data_read++) & (GDC_BUFFERS - 1)];
        dev->data_count--;
        return value;
    }
    return 0;
}

/* command */

static void
upd7220_cmd_reset(upd7220_t *dev)
{
    dev->sync[6] = 0x90;
    dev->sync[7] = 0x01;
    dev->zoom = dev->zr = dev->zw = 0;
    dev->ra[0] = dev->ra[1] = dev->ra[2] = 0;
    dev->ra[3] = 0x1e; /*0x19;*/
    dev->cs[0] = dev->cs[1] = dev->cs[2] = 0;
    dev->ead = dev->dad = 0;
    dev->maskl = dev->maskh = 0xff;
    dev->mod = 0;
    dev->start = 0;

    dev->params_count = 0;
    dev->data_count = dev->data_read = dev->data_write = 0;

    dev->statreg = 0;
    dev->cmdreg = -1;
    dev->dirty = 0xff;
    upd7220_recalctimings(dev);
}

static void
upd7220_cmd_sync(upd7220_t *dev)
{
    int i;

    for (i = 0; (i < 8) && (i < dev->params_count); i++)
        dev->sync[i] = dev->params[i];

    dev->cmdreg = -1;
    upd7220_recalctimings(dev);
}

static void
upd7220_cmd_master(upd7220_t *dev)
{
    dev->cmdreg = -1;
}

static void
upd7220_cmd_slave(upd7220_t *dev)
{
    dev->cmdreg = -1;
}

static void
upd7220_cmd_start(upd7220_t *dev)
{
    if (!dev->start) {
        dev->start = 1;
        dev->dirty |= GDC_DIRTY_START;
    }
    dev->cmdreg = -1;
}

static void
upd7220_cmd_stop(upd7220_t *dev)
{
    if (dev->start) {
        dev->start = 0;
        dev->dirty |= GDC_DIRTY_START;
    }
    dev->cmdreg = -1;
}

static void
upd7220_cmd_zoom(upd7220_t *dev)
{
    uint8_t tmp;

    if (dev->params_count > 0) {
        tmp = dev->params[0];
        dev->zr = tmp >> 4;
        dev->zw = tmp & 0x0f;
        dev->cmdreg = -1;
    }
}

static void
upd7220_cmd_scroll(upd7220_t *dev)
{
    if (dev->params_count > 0) {
        if (dev->ra[dev->cmdreg & 0x0f] != dev->params[0]) {
            dev->ra[dev->cmdreg & 0x0f] = dev->params[0];
            dev->dirty |= GDC_DIRTY_SCROLL;
        }
        if (dev->cmdreg < 0x7f) {
            dev->cmdreg++;
            dev->params_count = 0;
        } else {
            dev->cmdreg = -1;
        }
    }
}

static void
upd7220_cmd_csrform(upd7220_t *dev)
{
    int i;

    for (i = 0; i < dev->params_count; i++) {
        if (dev->cs[i] != dev->params[i]) {
            dev->cs[i] = dev->params[i];
            dev->dirty |= GDC_DIRTY_CURSOR;
        }
    }
    if (dev->params_count > 2)
        dev->cmdreg = -1;
}

static void
upd7220_cmd_pitch(upd7220_t *dev)
{
    if (dev->params_count > 0) {
        dev->pitch = dev->params[0];
        dev->cmdreg = -1;
    }
}

static void
upd7220_cmd_lpen(upd7220_t *dev)
{
    upd7220_fifo_write(dev, dev->lad & 0xff);
    upd7220_fifo_write(dev, (dev->lad >> 8) & 0xff);
    upd7220_fifo_write(dev, (dev->lad >> 16) & 0xff);
    dev->cmdreg = -1;
}

static void
upd7220_cmd_vectw(upd7220_t *dev)
{
    int i;

    for (i = 0; (i < 11) && (i < dev->params_count); i++)
        dev->vect[i] = dev->params[i];

    upd7220_update_vect(dev);
    dev->cmdreg = -1;
}

static void
upd7220_cmd_vecte(upd7220_t *dev)
{
    dev->dx = ((dev->ead % dev->pitch) << 4) | (dev->dad & 0x0f);
    dev->dy = dev->ead / dev->pitch;
    if (!(dev->vect[0] & 0x78)) {
        dev->pattern = dev->ra[8] | (dev->ra[9] << 8);
        upd7220_draw_pset(dev, dev->dx, dev->dy);
    }
    if (dev->vect[0] & 0x08)
        upd7220_draw_vectl(dev);

    if (dev->vect[0] & 0x10)
        upd7220_draw_vectt(dev);

    if (dev->vect[0] & 0x20)
        upd7220_draw_vectc(dev);

    if (dev->vect[0] & 0x40)
        upd7220_draw_vectr(dev);

    upd7220_reset_vect(dev);
    dev->statreg |= GDC_STAT_DRAW;
    dev->cmdreg = -1;
}

static void
upd7220_cmd_texte(upd7220_t *dev)
{
    dev->dx = ((dev->ead % dev->pitch) << 4) | (dev->dad & 0x0f);
    dev->dy = dev->ead / dev->pitch;
    if (!(dev->vect[0] & 0x78)) {
        dev->pattern = dev->ra[8] | (dev->ra[9] << 8);
        upd7220_draw_pset(dev, dev->dx, dev->dy);
    }
    if (dev->vect[0] & 0x08)
        upd7220_draw_vectl(dev);

    if (dev->vect[0] & 0x10)
        upd7220_draw_text(dev);

    if (dev->vect[0] & 0x20)
        upd7220_draw_vectc(dev);

    if (dev->vect[0] & 0x40)
        upd7220_draw_vectr(dev);

    upd7220_reset_vect(dev);
    dev->statreg |= GDC_STAT_DRAW;
    dev->cmdreg = -1;
}

static void
upd7220_cmd_csrw(upd7220_t *dev)
{
    if (dev->params_count > 0) {
        dev->ead = dev->params[0];
        if (dev->params_count > 1) {
            dev->ead |= dev->params[1] << 8;
            if (dev->params_count > 2) {
                dev->ead |= dev->params[2] << 16;
                dev->cmdreg = -1;
            }
        }
        dev->dad = (dev->ead >> 20) & 0x0f;
        dev->ead &= 0x3ffff;
        dev->dirty |= GDC_DIRTY_CURSOR;
    }
}

static void
upd7220_cmd_csrr(upd7220_t *dev)
{
    upd7220_fifo_write(dev, dev->ead & 0xff);
    upd7220_fifo_write(dev, (dev->ead >> 8) & 0xff);
    upd7220_fifo_write(dev, (dev->ead >> 16) & 0x03);
    upd7220_fifo_write(dev, dev->dad & 0xff);
    upd7220_fifo_write(dev, (dev->dad >> 8) & 0xff);
    dev->cmdreg = -1;
}

static void
upd7220_cmd_mask(upd7220_t *dev)
{
    if (dev->params_count > 1) {
        dev->maskl = dev->params[0];
        dev->maskh = dev->params[1];
        dev->cmdreg = -1;
    }
}

static void
upd7220_cmd_write(upd7220_t *dev)
{
    uint8_t l, h;
    int i;

    dev->mod = dev->cmdreg & 3;
    switch (dev->cmdreg & 0x18) {
        case 0x00: /* low and high */
            if (dev->params_count > 1) {
                l = dev->params[0] & dev->maskl;
                h = dev->params[1] & dev->maskh;
                for (i = 0; i < (dev->dc + 1); i++) {
                    upd7220_write_sub(dev, dev->ead * 2, l);
                    upd7220_write_sub(dev, (dev->ead * 2) + 1, h);
                    dev->ead += dev->diff;
                }
                upd7220_reset_vect(dev);
                dev->cmdreg = -1;
            }
            break;
        case 0x10: /* low byte */
            if (dev->params_count > 0) {
                l = dev->params[0] & dev->maskl;
                for (i = 0; i < (dev->dc + 1); i++) {
                    upd7220_write_sub(dev, dev->ead * 2, l);
                    dev->ead += dev->diff;
                }
                upd7220_reset_vect(dev);
                dev->cmdreg = -1;
            }
            break;
        case 0x18: /* high byte */
            if (dev->params_count > 0) {
                h = dev->params[0] & dev->maskh;
                for (i = 0; i < (dev->dc + 1); i++) {
                    upd7220_write_sub(dev, (dev->ead * 2) + 1, h);
                    dev->ead += dev->diff;
                }
                upd7220_reset_vect(dev);
                dev->cmdreg = -1;
            }
            break;
        default:    /* invalid */
            dev->cmdreg = -1;
            break;
    }
}

static void
upd7220_cmd_read(upd7220_t *dev)
{
    int i;

    dev->mod = dev->cmdreg & 3;
    switch (dev->cmdreg & 0x18) {
        case 0x00: /* low and high */
            for (i = 0; i < dev->dc; i++) {
                upd7220_fifo_write(dev, upd7220_read_sub(dev, dev->ead * 2));
                upd7220_fifo_write(dev, upd7220_read_sub(dev, (dev->ead * 2) + 1));
                dev->ead += dev->diff;
            }
            break;
        case 0x10: /* low byte */
            for (i = 0; i < dev->dc; i++) {
                upd7220_fifo_write(dev, upd7220_read_sub(dev, dev->ead * 2));
                dev->ead += dev->diff;
            }
            break;
        case 0x18: /* high byte */
            for (i = 0; i < dev->dc; i++) {
                upd7220_fifo_write(dev, upd7220_read_sub(dev, dev->ead * 2 + 1));
                dev->ead += dev->diff;
            }
            break;
        default: /* invalid */
            break;
    }
    upd7220_reset_vect(dev);
    dev->cmdreg = -1;
}

static void
upd7220_cmd_dmaw(upd7220_t *dev)
{
    dev->mod = dev->cmdreg & 3;
    upd7220_reset_vect(upd7220);
#if 0
    dev->statreg |= GDC_STAT_DMA;
#endif
    dev->cmdreg = -1;
}

static void
upd7220_cmd_dmar(upd7220_t *dev)
{
    dev->mod = dev->cmdreg & 3;
    upd7220_reset_vect(upd7220);
#if 0
    dev->statreg |= GDC_STAT_DMA;
#endif
    dev->cmdreg = -1;
}

static void
upd7220_cmd_unk_5a(upd7220_t *dev)
{
    if (dev->params_count > 2)
        dev->cmdreg = -1;
}

static void
upd7220_check_cmd(upd7220_t *dev)
{
    switch (dev->cmdreg) {
        case GDC_CMD_RESET:
            upd7220_cmd_reset(dev);
            break;
        case GDC_CMD_SYNC + 0:
        case GDC_CMD_SYNC + 1:
            if (dev->params_count > 7)
                upd7220_cmd_sync(dev);
            break;
        case GDC_CMD_MASTER:
            upd7220_cmd_master(dev);
            break;
        case GDC_CMD_SLAVE:
            upd7220_cmd_slave(dev);
            break;
        case GDC_CMD_START:
            upd7220_cmd_start(dev);
            break;
        case GDC_CMD_BCTRL + 0:
            upd7220_cmd_stop(dev);
            break;
        case GDC_CMD_BCTRL + 1:
            upd7220_cmd_start(dev);
            break;
        case GDC_CMD_ZOOM:
            upd7220_cmd_zoom(dev);
            break;
        case GDC_CMD_SCROLL + 0:
        case GDC_CMD_SCROLL + 1:
        case GDC_CMD_SCROLL + 2:
        case GDC_CMD_SCROLL + 3:
        case GDC_CMD_SCROLL + 4:
        case GDC_CMD_SCROLL + 5:
        case GDC_CMD_SCROLL + 6:
        case GDC_CMD_SCROLL + 7:
        case GDC_CMD_TEXTW + 0:
        case GDC_CMD_TEXTW + 1:
        case GDC_CMD_TEXTW + 2:
        case GDC_CMD_TEXTW + 3:
        case GDC_CMD_TEXTW + 4:
        case GDC_CMD_TEXTW + 5:
        case GDC_CMD_TEXTW + 6:
        case GDC_CMD_TEXTW + 7:
            upd7220_cmd_scroll(dev);
            break;
        case GDC_CMD_CSRFORM:
            upd7220_cmd_csrform(dev);
            break;
        case GDC_CMD_PITCH:
            upd7220_cmd_pitch(dev);
            break;
        case GDC_CMD_LPEN:
            upd7220_cmd_lpen(dev);
            break;
        case GDC_CMD_VECTW:
            if (dev->params_count > 10)
                upd7220_cmd_vectw(dev);
            break;
        case GDC_CMD_VECTE:
            upd7220_cmd_vecte(dev);
            break;
        case GDC_CMD_TEXTE:
            upd7220_cmd_texte(dev);
            break;
        case GDC_CMD_CSRW:
            upd7220_cmd_csrw(dev);
            break;
        case GDC_CMD_CSRR:
            upd7220_cmd_csrr(dev);
            break;
        case GDC_CMD_MASK:
            upd7220_cmd_mask(dev);
            break;
        case GDC_CMD_WRITE + 0x00:
        case GDC_CMD_WRITE + 0x01:
        case GDC_CMD_WRITE + 0x02:
        case GDC_CMD_WRITE + 0x03:
        case GDC_CMD_WRITE + 0x08:
        case GDC_CMD_WRITE + 0x09:
        case GDC_CMD_WRITE + 0x0a:
        case GDC_CMD_WRITE + 0x0b:
        case GDC_CMD_WRITE + 0x10:
        case GDC_CMD_WRITE + 0x11:
        case GDC_CMD_WRITE + 0x12:
        case GDC_CMD_WRITE + 0x13:
        case GDC_CMD_WRITE + 0x18:
        case GDC_CMD_WRITE + 0x19:
        case GDC_CMD_WRITE + 0x1a:
        case GDC_CMD_WRITE + 0x1b:
            upd7220_cmd_write(dev);
            break;
        case GDC_CMD_READ + 0x00:
        case GDC_CMD_READ + 0x01:
        case GDC_CMD_READ + 0x02:
        case GDC_CMD_READ + 0x03:
        case GDC_CMD_READ + 0x08:
        case GDC_CMD_READ + 0x09:
        case GDC_CMD_READ + 0x0a:
        case GDC_CMD_READ + 0x0b:
        case GDC_CMD_READ + 0x10:
        case GDC_CMD_READ + 0x11:
        case GDC_CMD_READ + 0x12:
        case GDC_CMD_READ + 0x13:
        case GDC_CMD_READ + 0x18:
        case GDC_CMD_READ + 0x19:
        case GDC_CMD_READ + 0x1a:
        case GDC_CMD_READ + 0x1b:
            upd7220_cmd_read(dev);
            break;
        case GDC_CMD_DMAW + 0x00:
        case GDC_CMD_DMAW + 0x01:
        case GDC_CMD_DMAW + 0x02:
        case GDC_CMD_DMAW + 0x03:
        case GDC_CMD_DMAW + 0x08:
        case GDC_CMD_DMAW + 0x09:
        case GDC_CMD_DMAW + 0x0a:
        case GDC_CMD_DMAW + 0x0b:
        case GDC_CMD_DMAW + 0x10:
        case GDC_CMD_DMAW + 0x11:
        case GDC_CMD_DMAW + 0x12:
        case GDC_CMD_DMAW + 0x13:
        case GDC_CMD_DMAW + 0x18:
        case GDC_CMD_DMAW + 0x19:
        case GDC_CMD_DMAW + 0x1a:
        case GDC_CMD_DMAW + 0x1b:
            upd7220_cmd_dmaw(dev);
            break;
        case GDC_CMD_DMAR + 0x00:
        case GDC_CMD_DMAR + 0x01:
        case GDC_CMD_DMAR + 0x02:
        case GDC_CMD_DMAR + 0x03:
        case GDC_CMD_DMAR + 0x08:
        case GDC_CMD_DMAR + 0x09:
        case GDC_CMD_DMAR + 0x0a:
        case GDC_CMD_DMAR + 0x0b:
        case GDC_CMD_DMAR + 0x10:
        case GDC_CMD_DMAR + 0x11:
        case GDC_CMD_DMAR + 0x12:
        case GDC_CMD_DMAR + 0x13:
        case GDC_CMD_DMAR + 0x18:
        case GDC_CMD_DMAR + 0x19:
        case GDC_CMD_DMAR + 0x1a:
        case GDC_CMD_DMAR + 0x1b:
            upd7220_cmd_dmar(dev);
            break;
        case GDC_CMD_UNK_5A:
            upd7220_cmd_unk_5a(dev);
            break;
    }
}

static void
upd7220_process_cmd(upd7220_t *dev)
{
    switch (dev->cmdreg) {
        case GDC_CMD_RESET:
            upd7220_cmd_reset(dev);
            break;
        case GDC_CMD_SYNC + 0:
        case GDC_CMD_SYNC + 1:
            upd7220_cmd_sync(dev);
            break;
        case GDC_CMD_SCROLL + 0:
        case GDC_CMD_SCROLL + 1:
        case GDC_CMD_SCROLL + 2:
        case GDC_CMD_SCROLL + 3:
        case GDC_CMD_SCROLL + 4:
        case GDC_CMD_SCROLL + 5:
        case GDC_CMD_SCROLL + 6:
        case GDC_CMD_SCROLL + 7:
        case GDC_CMD_TEXTW + 0:
        case GDC_CMD_TEXTW + 1:
        case GDC_CMD_TEXTW + 2:
        case GDC_CMD_TEXTW + 3:
        case GDC_CMD_TEXTW + 4:
        case GDC_CMD_TEXTW + 5:
        case GDC_CMD_TEXTW + 6:
        case GDC_CMD_TEXTW + 7:
            upd7220_cmd_scroll(dev);
            break;
        case GDC_CMD_VECTW:
            upd7220_cmd_vectw(dev);
            break;
        case GDC_CMD_CSRW:
            upd7220_cmd_csrw(dev);
            break;
        }
}

/* i/o */

void
upd7220_param_write(uint16_t addr, uint8_t value, void *priv)
{
    /* ioport 0x60(chr), 0xa0(gfx) */
    upd7220_t  *dev = (upd7220_t *) priv;

    if (dev->cmdreg != -1) {
        if (dev->params_count < 16)
            dev->params[dev->params_count++] = value;

        upd7220_check_cmd(dev);
        if (dev->cmdreg == -1)
            dev->params_count = 0;
    }
}

uint8_t
upd7220_statreg_read(uint16_t addr, void *priv)
{
    /* ioport 0x60(chr), 0xa0(gfx) */
    upd7220_t  *dev = (upd7220_t *) priv;
    pc98x1_vid_t *vid = (pc98x1_vid_t *) dev->priv;
    uint8_t value = dev->statreg | vid->vsync;

#if 0
    if (dev->params_count == 0)
#endif
        value |= GDC_STAT_EMPTY;

    if (dev->params_count == 16)
        value |= GDC_STAT_FULL;

    if (dev->data_count > 0)
        value |= GDC_STAT_DRDY;

    dev->statreg &= ~(GDC_STAT_DMA | GDC_STAT_DRAW);
    /* toggle hblank bit */
    dev->statreg ^= GDC_STAT_HBLANK;
    return value;
}

void
upd7220_cmdreg_write(uint16_t addr, uint8_t value, void *priv)
{
    /* ioport 0x62(chr), 0xa2(gfx) */
    upd7220_t  *dev = (upd7220_t *) priv;

    if (dev->cmdreg != -1)
        upd7220_process_cmd(dev);

    dev->cmdreg = value;
    dev->params_count = 0;
    upd7220_check_cmd(dev);
}

uint8_t
upd7220_data_read(uint16_t addr, void *priv)
{
    /* ioport 0x62(chr), 0xa2(gfx) */
    upd7220_t  *dev = (upd7220_t *) priv;

    return upd7220_fifo_read(dev);
}

void
upd7220_reset(upd7220_t *dev)
{
    upd7220_cmd_reset(dev);
}

void upd7220_init(upd7220_t *dev, void *priv,
                  uint8_t (*vram_read)(uint32_t addr, void *priv),
                  void (*vram_write)(uint32_t addr, uint8_t val, void *priv))
{
    int i;

    for (i = 0; i <= GDC_TABLEMAX; i++)
        dev->rt[i] = (int)((double)(1 << GDC_MULBIT) * (1 - sqrt(1 - pow((0.70710678118654 * i) / GDC_TABLEMAX, 2))));

    dev->priv = priv;
    dev->vram_read = vram_read;
    dev->vram_write = vram_write;
}
