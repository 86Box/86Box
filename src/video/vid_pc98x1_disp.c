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
 *          Copyright 2009-2024 TAKEDA, toshiya.
 *          Copyright 2008-2024 yui/Neko Project II.
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
#include <86box/machine.h>
#include <86box/video.h>
#include <86box/vid_upd7220.h>
#include <86box/vid_pc98x1_egc.h>
#include <86box/vid_pc98x1_disp.h>
#include <86box/plat_unused.h>

/***********************************************************/
/* NEC PC-9821 VGA */

enum {
    PALETTE_G           = 0,
    PALETTE_R           = 1,
    PALETTE_B           = 2,
};

enum {
    DIRTY_TVRAM         = 0x01,
    DIRTY_VRAM0         = 0x02,
    DIRTY_VRAM1         = 0x04,
    DIRTY_PALETTE       = 0x10,
    DIRTY_DISPLAY       = 0x80,
};

enum {
    ATTR_ST = 0x01,
    ATTR_BL = 0x02,
    ATTR_RV = 0x04,
    ATTR_UL = 0x08,
    ATTR_VL = 0x10,
    ATTR_COL = 0xe0,
};

/* vsync */
static void
pc98x1_vsync_write(UNUSED(uint16_t addr), uint8_t value, void *priv)
{
    /* ioport 0x64 */
    pc98x1_vid_t *dev = (pc98x1_vid_t *)priv;

    dev->crtv = 1;
}

/* crtc */
static void
pc98x1_crtc_write(uint16_t addr, uint8_t value, void *priv)
{
    /* ioport 0x70-0x7a */
    pc98x1_vid_t *dev = (pc98x1_vid_t *)priv;

    switch (addr & 7) {
        case 0:
            if (dev->pl != value) {
                dev->pl = value;
                dev->dirty |= DIRTY_TVRAM;
            }
            break;
        case 1:
            if (dev->bl != value) {
                dev->bl = value;
                dev->dirty |= DIRTY_TVRAM;
            }
            break;
        case 2:
            if (dev->cl != value) {
                dev->cl = value;
                dev->dirty |= DIRTY_TVRAM;
            }
            break;
        case 3:
            if (dev->ssl != value) {
                dev->ssl = value;
                dev->dirty |= DIRTY_TVRAM;
            }
            break;
        case 4:
            if (dev->sur != value) {
                dev->sur = value;
                dev->dirty |= DIRTY_TVRAM;
            }
            break;
        case 5:
            if (dev->sdr != value) {
                dev->sdr = value;
                dev->dirty |= DIRTY_TVRAM;
            }
            break;
        default:
            break;
    }
}

/* grcg */
static void
pc98x1_grcg_write(uint16_t addr, uint8_t value, void *priv)
{
    /* ioport 0x7c-7e */
    pc98x1_vid_t *dev = (pc98x1_vid_t *)priv;

    if (addr & 1) {
        dev->grcg_tile_b[dev->grcg_tile_cnt] = value;
        dev->grcg_tile_cnt = (dev->grcg_tile_cnt + 1) & 3;
    } else {
        dev->grcg_mode = value;
        dev->grcg_tile_cnt = 0;
    }
}

static void
pc98x1_grcg_writew(uint16_t addr, uint16_t value, void *priv)
{
    /* ioport 0x7e */
    pc98x1_vid_t *dev = (pc98x1_vid_t *)priv;

    if (addr & 1) {
        dev->grcg_tile_w[dev->grcg_tile_cnt] = (value & 0xff) | (value << 8);
        dev->grcg_tile_cnt = (dev->grcg_tile_cnt + 1) & 3;
    }
}

static void
pc98x1_grcg_mem_writeb(pc98x1_vid_t *dev, uint32_t addr1, uint8_t value)
{
    uint32_t addr = addr1 & 0x7fff;

    if (dev->grcg_mode & GRCG_RW_MODE) {
        /* RMW */
        if (!(dev->grcg_mode & GRCG_PLANE_0)) {
            dev->vram16_draw_b[addr] &= ~value;
            dev->vram16_draw_b[addr] |= dev->grcg_tile_b[0] & value;
        }
        if (!(dev->grcg_mode & GRCG_PLANE_1)) {
            dev->vram16_draw_r[addr] &= ~value;
            dev->vram16_draw_r[addr] |= dev->grcg_tile_b[1] & value;
        }
        if (!(dev->grcg_mode & GRCG_PLANE_2)) {
            dev->vram16_draw_g[addr] &= ~value;
            dev->vram16_draw_g[addr] |= dev->grcg_tile_b[2] & value;
        }
        if (!(dev->grcg_mode & GRCG_PLANE_3)) {
            dev->vram16_draw_e[addr] &= ~value;
            dev->vram16_draw_e[addr] |= dev->grcg_tile_b[3] & value;
        }
    } else {
        /* TDW */
        if (!(dev->grcg_mode & GRCG_PLANE_0))
            dev->vram16_draw_b[addr] = dev->grcg_tile_b[0];

        if (!(dev->grcg_mode & GRCG_PLANE_1))
            dev->vram16_draw_r[addr] = dev->grcg_tile_b[0];

        if (!(dev->grcg_mode & GRCG_PLANE_2))
            dev->vram16_draw_g[addr] = dev->grcg_tile_b[0];

        if (!(dev->grcg_mode & GRCG_PLANE_3))
            dev->vram16_draw_e[addr] = dev->grcg_tile_b[0];
    }
}

static void
pc98x1_grcg_mem_writew(pc98x1_vid_t *dev, uint32_t addr1, uint16_t value)
{
    uint32_t addr = addr1 & 0x7fff;

    if (dev->grcg_mode & GRCG_RW_MODE) {
        /* RMW */
        if (!(dev->grcg_mode & GRCG_PLANE_0)) {
            *(uint16_t *)(dev->vram16_draw_b + addr) &= ~value;
            *(uint16_t *)(dev->vram16_draw_b + addr) |= dev->grcg_tile_w[0] & value;
        }
        if (!(dev->grcg_mode & GRCG_PLANE_1)) {
            *(uint16_t *)(dev->vram16_draw_r + addr) &= ~value;
            *(uint16_t *)(dev->vram16_draw_r + addr) |= dev->grcg_tile_w[1] & value;
        }
        if (!(dev->grcg_mode & GRCG_PLANE_2)) {
            *(uint16_t *)(dev->vram16_draw_g + addr) &= ~value;
            *(uint16_t *)(dev->vram16_draw_g + addr) |= dev->grcg_tile_w[2] & value;
        }
        if (!(dev->grcg_mode & GRCG_PLANE_3)) {
            *(uint16_t *)(dev->vram16_draw_e + addr) &= ~value;
            *(uint16_t *)(dev->vram16_draw_e + addr) |= dev->grcg_tile_w[3] & value;
        }
    } else {
        /* TDW */
        if (!(dev->grcg_mode & GRCG_PLANE_0))
            *(uint16_t *)(dev->vram16_draw_b + addr) = dev->grcg_tile_w[0];

        if (!(dev->grcg_mode & GRCG_PLANE_1))
            *(uint16_t *)(dev->vram16_draw_r + addr) = dev->grcg_tile_w[1];

        if (!(dev->grcg_mode & GRCG_PLANE_2))
            *(uint16_t *)(dev->vram16_draw_g + addr) = dev->grcg_tile_w[2];

        if (!(dev->grcg_mode & GRCG_PLANE_3))
            *(uint16_t *)(dev->vram16_draw_e + addr) = dev->grcg_tile_w[3];
    }
}

/* cg window */
static void
pc98x1_cgwindow_set_addr(pc98x1_vid_t *dev)
{
    uint32_t low = 0x7fff0, high;
    uint8_t code = dev->font_code & 0x7f;
    uint16_t lr = ((~dev->font_line) & 0x20) << 6;

    if (!(dev->font_code & 0xff00)) {
        high = 0x80000 + (dev->font_code << 4);
        if (!dev->mode1[MODE1_FONTSEL])
            high += 0x2000;
    } else {
        high = (dev->font_code & 0x7f7f) << 4;
        if ((code >= 0x56) && (code < 0x58)) {
            high += lr;
        } else if ((code >= 0x09) && (code < 0x0c)) {
            if (lr)
                high = low;
        } else if (((code >= 0x0c) && (code < 0x10)) || ((code >= 0x58) && (code < 0x60)))
            high += lr;
        else {
            low = high;
            high += 0x800;
        }
    }
    dev->cgwindow_addr_low = low;
    dev->cgwindow_addr_high = high;
}

static void
pc98x1_cgwindow_write(uint16_t addr, uint8_t value, void *priv)
{
    /* ioport 0xa1-a5 */
    pc98x1_vid_t *dev = (pc98x1_vid_t *)priv;

    switch (addr & 3) {
        case 0:
            dev->font_code = (value << 8) | (dev->font_code & 0xff);
            pc98x1_cgwindow_set_addr(dev);
            break;
        case 1:
            dev->font_code = (dev->font_code & 0xff00) | value;
            pc98x1_cgwindow_set_addr(dev);
            break;
        case 2:
            dev->font_line = value;
            pc98x1_cgwindow_set_addr(dev);
            break;
        default:
            break;
    }
}

static uint8_t
pc98x1_cgwindow_read(uint16_t addr, void *priv)
{
    /* ioport 0xa1-a5 */
    pc98x1_vid_t *dev = (pc98x1_vid_t *)priv;
    uint8_t temp = 0xff;

    switch (addr & 3) {
        case 0:
            temp = (dev->font_code >> 8) & 0xff;
            break;
        case 1:
            temp = dev->font_code & 0xff;
            break;
        case 2:
            temp = dev->font_line;
            break;
        default:
            break;
    }

    return temp;
}

static void
pc98x1_cgwindow_pattern_write(UNUSED(uint16_t addr), uint8_t value, void *priv)
{
    /* ioport 0xa9 */
    pc98x1_vid_t *dev = (pc98x1_vid_t *)priv;
    uint16_t lr = ((~dev->font_line) & 0x20) << 6;

    if ((dev->font_code & 0x7e) == 0x56)
        dev->font[((dev->font_code & 0x7f7f) << 4) + lr + (dev->font_line & 0x0f)] = value;
}

static uint8_t
pc98x1_cgwindow_pattern_read(UNUSED(uint16_t addr), void *priv)
{
    /* ioport 0xa9 */
    pc98x1_vid_t *dev = (pc98x1_vid_t *)priv;
    uint8_t type = dev->font_code & 0xff;
    uint16_t lr = ((~dev->font_line) & 0x20) << 6;

    if ((type >= 0x09) && (type < 0x0c)) {
        if (!lr)
            return dev->font[((dev->font_code & 0x7f7f) << 4) + (dev->font_line & 0x0f)];
    } else if (dev->font_code & 0xff00)
        return dev->font[((dev->font_code & 0x7f7f) << 4) + lr + (dev->font_line & 0x0f)];
    else if (!(dev->font_line & 0x10))
        return dev->font[0x80000 + (dev->font_code << 4) + (dev->font_line & 0x1f)];

    return 0;
}

static void
pc98x1_cgwindow_writeb(pc98x1_vid_t *dev, uint32_t addr, uint8_t value)
{
    if ((addr < 0x1000) && (dev->font_code & 0x7e) == 0x56) {
        if (addr & 1)
            dev->font[dev->cgwindow_addr_high + ((addr >> 1) & 0x0f)] = value;
        else
            dev->font[dev->cgwindow_addr_low + ((addr >> 1) & 0x0f)] = value;
    }
}

static void
pc98x1_cgwindow_writew(pc98x1_vid_t *dev, uint32_t addr, uint16_t value)
{
    if (addr < 0x1000) {
        pc98x1_cgwindow_writeb(dev, addr, value & 0xff);
        pc98x1_cgwindow_writeb(dev, addr + 1, (value >> 8) & 0xff);
    }
}

static void
pc98x1_cgwindow_writel(pc98x1_vid_t *dev, uint32_t addr, uint32_t value)
{
    if (addr < 0x1000) {
        pc98x1_cgwindow_writeb(dev, addr, value & 0xff);
        pc98x1_cgwindow_writeb(dev, addr + 1, (value >> 8) & 0xff);
        pc98x1_cgwindow_writeb(dev, addr + 2, (value >> 16) & 0xff);
        pc98x1_cgwindow_writeb(dev, addr + 3, (value >> 24) & 0xff);
    }
}

static uint8_t
pc98x1_cgwindow_readb(pc98x1_vid_t *dev, uint32_t addr)
{
    if (addr < 0x1000) {
        if (addr & 1)
            return dev->font[dev->cgwindow_addr_high + ((addr >> 1) & 0x0f)];
        else
            return dev->font[dev->cgwindow_addr_low + ((addr >> 1) & 0x0f)];
    }
    return 0xff;
}

static uint16_t
pc98x1_cgwindow_readw(pc98x1_vid_t *dev, uint32_t addr)
{
    uint16_t value = 0xffff;

    if (addr < 0x1000) {
        value = pc98x1_cgwindow_readb(dev, addr);
        value |= pc98x1_cgwindow_readb(dev, addr + 1) << 8;
    }
    return value;
}

static uint32_t
pc98x1_cgwindow_readl(pc98x1_vid_t *dev, uint32_t addr)
{
    uint16_t value = 0xffffffff;

    if (addr < 0x1000) {
        value = pc98x1_cgwindow_readb(dev, addr);
        value |= pc98x1_cgwindow_readb(dev, addr + 1) << 8;
        value |= pc98x1_cgwindow_readb(dev, addr + 2) << 16;
        value |= pc98x1_cgwindow_readb(dev, addr + 3) << 24;
    }
    return value;
}

/* mode flip-flop */
static void
pc98x1_mode_flipflop1_2_write(uint16_t addr, uint8_t value, void *priv)
{
    pc98x1_vid_t *dev = (pc98x1_vid_t *)priv;
    int num = 0;
    uint8_t val = 0;

    /* ioport 0x6a */
    if (addr & 1) {
        val = value & 1;
        num = (val >> 1) & 0x7f;
        switch (num) {
            case 0x00:
                /* select 8/16 color */
                if (dev->mode2[num] != val) {
                    if (!dev->mode2[MODE2_256COLOR])
                        dev->dirty |= DIRTY_PALETTE;

                    dev->mode2[num] = val;
                }
                break;
            case 0x02:
                /* select grcg/egc mode */
                if (dev->mode2[MODE2_WRITE_MASK])
                    dev->mode2[num] = val;
                break;
            case 0x10:
                /* select 16/256 color */
                if (dev->mode2[MODE2_WRITE_MASK]) {
                    if (dev->mode2[num] != val) {
                        dev->dirty |= (DIRTY_PALETTE | DIRTY_VRAM0 | DIRTY_VRAM1);
                        dev->mode2[num] = val;
                    }
                }
                break;
            case 0x34:
                /* select 400/480 lines */
                if (dev->mode2[MODE2_WRITE_MASK]) {
                    if (dev->mode2[num] != val) {
                        dev->dirty |= (DIRTY_VRAM0 | DIRTY_VRAM1);
                        dev->mode2[num] = val;
                    }
                }
                break;
            case 0x11: case 0x12: case 0x13: case 0x15: case 0x16:
            case 0x30: case 0x31: case 0x33: case 0x65:
                if (dev->mode2[MODE2_WRITE_MASK])
                    dev->mode2[num] = val;
                break;
            default:
                dev->mode2[num] = val;
                break;
        }
    } else {
        /* ioport 0x68 */
        num = (value >> 1) & 7;

        if (dev->mode1[num] != (value & 1)) {
            switch (num) {
                case MODE1_ATRSEL:
                case MODE1_COLUMN:
                    dev->dirty |= DIRTY_TVRAM;
                    break;
                case MODE1_200LINE:
                    dev->dirty |= DIRTY_VRAM0 | DIRTY_VRAM1;
                    break;
                case MODE1_DISP:
                    dev->dirty |= DIRTY_DISPLAY;
                    break;
                default:
                    break;
            }
            dev->mode1[num] = value & 1;
        }
    }
}

static uint8_t
pc98x1_mode_flipflop1_2_read(uint16_t addr, void *priv)
{

    pc98x1_vid_t *dev = (pc98x1_vid_t *)priv;
    uint8_t temp;
    uint8_t value = 0;

    /* ioport 0x6a */
    if (addr & 1) {
        if (dev->mode3[MODE3_LINE_CONNECT])
            value |= 0x01;

        if (dev->mode3[MODE3_WRITE_MASK])
            value |= 0x10;

        temp = value | 0xee;
    } else {
        /* ioport 0x68 */
        if (dev->mode1[MODE1_ATRSEL])
            value |= 0x01;

        if (dev->mode1[MODE1_GRAPHIC])
            value |= 0x04;

        if (dev->mode1[MODE1_COLUMN])
            value |= 0x08;

        if (dev->mode1[MODE1_MEMSW])
            value |= 0x40;

        if (dev->mode1[MODE1_KAC])
            value |= 0x80;

        temp = value | 0x32;
    }
    return temp;
}

static void
pc98x1_mode_flipflop3_write(UNUSED(uint16_t addr), uint8_t value, void *priv)
{
    /* ioport 0x6e */
    pc98x1_vid_t *dev = (pc98x1_vid_t *)priv;

    switch (value & 0xfe) {
        case 0x02:
            dev->mode3[MODE3_WRITE_MASK] = value & 1;
            break;
        default:
            if (dev->mode3[MODE3_WRITE_MASK])
                dev->mode3[(value >> 1) & 0x7f] = value & 1;
            break;
    }
}

static uint8_t
pc98x1_mode_flipflop3_read(UNUSED(uint16_t addr), void *priv)
{
    /* ioport 0x6e */
    pc98x1_vid_t *dev = (pc98x1_vid_t *)priv;
    uint8_t value = 0;

    if (dev->mode3[MODE3_WRITE_MASK])
        value |= 0x01;

    if (dev->mode3[MODE3_LINE_COLOR])
        value |= 0x10;

    if (dev->mode3[MODE3_NPC_COLOR])
        value |= 0x80;

    return value | 0x6e;
}

static void
pc98x1_mode_select_write(UNUSED(uint16_t addr), uint8_t value, void *priv)
{
    /* ioport 0x9a0 */
    pc98x1_vid_t *dev = (pc98x1_vid_t *)priv;

    dev->mode_select = value;
}

static uint8_t
pc98x1_mode_status_read(UNUSED(uint16_t addr), void *priv)
{
    /* ioport 0x9a0 */
    pc98x1_vid_t *dev = (pc98x1_vid_t *)priv;
    uint8_t value = 0;

    if (dev->mode2[0x42])
        value |= 2;

    switch (dev->mode_select) {
        case 0x01: if (dev->mode1[0x01]) { value |= 1; } break;
        case 0x02: if (dev->mode1[0x04]) { value |= 1; } break;
        case 0x03: if (dev->mode1[0x07]) { value |= 1; } break;
        case 0x04: if (dev->mode2[0x00]) { value |= 1; } break;
        case 0x05: if (dev->mode2[0x20]) { value |= 1; } break;
        case 0x06: if (dev->mode2[0x22]) { value |= 1; } break;
        case 0x07: if (dev->mode2[0x02]) { value |= 1; } break;
        case 0x08: if (dev->mode2[0x03]) { value |= 1; } break;
        case 0x09: if (dev->mode2[0x41]) { value |= 1; } break;
        case 0x0a: if (dev->mode2[0x10]) { value |= 1; } break;
        case 0x0b: if (dev->mode2[0x31]) { value |= 1; } break;
        case 0x0d: if (dev->mode2[0x34]) { value |= 1; } break;
        case 0x0e: if (dev->mode2[0x11]) { value |= 1; } break;
        case 0x0f: if (dev->mode2[0x12]) { value |= 1; } break;
        case 0x10: if (dev->mode2[0x35]) { value |= 1; } break;
        case 0x11: if (dev->mode2[0x13]) { value |= 1; } break;
        case 0x12: if (dev->mode2[0x16]) { value |= 1; } break;
        case 0x13: if (dev->mode2[0x14]) { value |= 1; } break;
        case 0x14: if (dev->mode2[0x33]) { value |= 1; } break;
        case 0x15: if (dev->mode2[0x30]) { value |= 1; } break;
        case 0x16: if (dev->mode2[0x61]) { value |= 1; } break;
        case 0x17: if (dev->mode2[0x36]) { value |= 1; } break;
        case 0x18: if (dev->mode2[0x15]) { value |= 1; } break;
        case 0x19: if (dev->mode2[0x24]) { value |= 1; } break;
        case 0x1a: if (dev->mode2[0x64]) { value |= 1; } break;
        case 0x1b: if (dev->mode2[0x17]) { value |= 1; } break;
        case 0x1c: if (dev->mode2[0x37]) { value |= 1; } break;
        case 0x1d: if (dev->mode2[0x60]) { value |= 1; } break;
        case 0x1e: if (dev->mode2[0x23]) { value |= 1; } break;
        case 0x1f: if (dev->mode2[0x35]) { value |= 1; } break;
        default: break;
    }
    return value;
}

/* vram bank */
static void
pc98x1_vram_bank_write(uint16_t addr, uint8_t value, void *priv)
{
    /* ioport 0xa4-a6 */
    pc98x1_vid_t *dev = (pc98x1_vid_t *)priv;

    if (addr & 1) {
        if (value & 1) {
            dev->vram16_draw_b = dev->vram16 + 0x20000;
            dev->vram16_draw_r = dev->vram16 + 0x28000;
            dev->vram16_draw_g = dev->vram16 + 0x30000;
            dev->vram16_draw_e = dev->vram16 + 0x38000;
            dev->bank_draw = DIRTY_VRAM1;
        } else {
            dev->vram16_draw_b = dev->vram16 + 0x00000;
            dev->vram16_draw_r = dev->vram16 + 0x08000;
            dev->vram16_draw_g = dev->vram16 + 0x10000;
            dev->vram16_draw_e = dev->vram16 + 0x18000;
            dev->bank_draw = DIRTY_VRAM0;
        }
        egc_set_vram(&dev->egc, dev->vram16_draw_b);
    } else {
        if (value & 1) {
            if (dev->bank_disp != DIRTY_VRAM1) {
                dev->vram16_disp_b = dev->vram16 + 0x20000;
                dev->vram16_disp_r = dev->vram16 + 0x28000;
                dev->vram16_disp_g = dev->vram16 + 0x30000;
                dev->vram16_disp_e = dev->vram16 + 0x38000;
                dev->vram256_disp = dev->vram256 + 0x40000;
                dev->bank_disp = DIRTY_VRAM1;
                dev->dirty |= DIRTY_DISPLAY;
            }
        } else {
            if (dev->bank_disp != DIRTY_VRAM0) {
                dev->vram16_disp_b = dev->vram16 + 0x00000;
                dev->vram16_disp_r = dev->vram16 + 0x08000;
                dev->vram16_disp_g = dev->vram16 + 0x10000;
                dev->vram16_disp_e = dev->vram16 + 0x18000;
                dev->vram256_disp = dev->vram256 + 0x00000;
                dev->bank_disp = DIRTY_VRAM0;
                dev->dirty |= DIRTY_DISPLAY;
            }
        }
    }
}

static uint8_t
pc98x1_vram_bank_read(uint16_t addr, void *priv)
{
    /* ioport 0xa4-a6 */
    pc98x1_vid_t *dev = (pc98x1_vid_t *)priv;
    uint8_t temp;

    if (addr & 1) {
        if (dev->bank_draw == DIRTY_VRAM0) {
            /*return 0;*/
            temp = 0xfe;
        } else {
            /*return 1;*/
            temp = 0xff;
        }
    } else {
        if (dev->bank_disp == DIRTY_VRAM0) {
            /*return 0;*/
            temp = 0xfe;
        } else {
            /*return 1;*/
            temp = 0xff;
        }
    }
    return temp;
}

/* palette */
static void
pc98x1_palette_write(uint16_t addr, uint8_t value, void *priv)
{
    /* ioport 0xa8-ae */
    pc98x1_vid_t *dev = (pc98x1_vid_t *)priv;

    switch (addr & 3) {
        case 0:
            if (dev->mode2[MODE2_256COLOR] || dev->mode2[MODE2_16COLOR])
                dev->anapal_select = value;
            else {
                if (dev->digipal[0] != value) {
                    dev->digipal[0] = value;
                    dev->dirty |= DIRTY_PALETTE;
                }
            }
            break;
        case 1:
            if (dev->mode2[MODE2_256COLOR]) {
                if (dev->anapal[PALETTE_G][dev->anapal_select] != value) {
                    dev->anapal[PALETTE_G][dev->anapal_select] = value;
                    dev->dirty |= DIRTY_PALETTE;
                }
            } else if (dev->mode2[MODE2_16COLOR]) {
                if (dev->anapal[PALETTE_G][dev->anapal_select & 0x0f] != (value & 0x0f)) {
                    dev->anapal[PALETTE_G][dev->anapal_select & 0x0f] = value & 0x0f;
                    dev->dirty |= DIRTY_PALETTE;
                }
            } else {
                if (dev->digipal[1] != value) {
                    dev->digipal[1] = value;
                    dev->dirty |= DIRTY_PALETTE;
                }
            }
            break;
        case 2:
            if (dev->mode2[MODE2_256COLOR]) {
                if (dev->anapal[PALETTE_R][dev->anapal_select] != value) {
                    dev->anapal[PALETTE_R][dev->anapal_select] = value;
                    dev->dirty |= DIRTY_PALETTE;
                }
            } else if (dev->mode2[MODE2_16COLOR]) {
                if (dev->anapal[PALETTE_R][dev->anapal_select & 0x0f] != (value & 0x0f)) {
                    dev->anapal[PALETTE_R][dev->anapal_select & 0x0f] = value & 0x0f;
                    dev->dirty |= DIRTY_PALETTE;
                }
            } else {
                if (dev->digipal[2] != value) {
                    dev->digipal[2] = value;
                    dev->dirty |= DIRTY_PALETTE;
                }
            }
            break;
        case 3:
            if (dev->mode2[MODE2_256COLOR]) {
                if (dev->anapal[PALETTE_B][dev->anapal_select] != value) {
                    dev->anapal[PALETTE_B][dev->anapal_select] = value;
                    dev->dirty |= DIRTY_PALETTE;
                }
            } else if (dev->mode2[MODE2_16COLOR]) {
                if (dev->anapal[PALETTE_B][dev->anapal_select & 0x0f] != (value & 0x0f)) {
                    dev->anapal[PALETTE_B][dev->anapal_select & 0x0f] = value & 0x0f;
                    dev->dirty |= DIRTY_PALETTE;
                }
            } else {
                if (dev->digipal[3] != value) {
                    dev->digipal[3] = value;
                    dev->dirty |= DIRTY_PALETTE;
                }
            }
            break;
        default:
            break;
    }
}

static uint8_t
pc98x1_palette_read(uint16_t addr, void *priv)
{
    /* ioport 0xa8-ae */
    pc98x1_vid_t *dev = (pc98x1_vid_t *)priv;
    uint8_t temp = 0xff;

    switch (addr & 3) {
        case 0:
            if (dev->mode2[MODE2_256COLOR] || dev->mode2[MODE2_16COLOR])
                temp =  dev->anapal_select;
            else
                temp = dev->digipal[0];
            break;
        case 1:
            if (dev->mode2[MODE2_256COLOR])
                temp = dev->anapal[PALETTE_G][dev->anapal_select];
            else if (dev->mode2[MODE2_16COLOR])
                temp = dev->anapal[PALETTE_G][dev->anapal_select & 0x0f];
            else
                temp = dev->digipal[1];
            break;
        case 2:
            if (dev->mode2[MODE2_256COLOR])
                temp = dev->anapal[PALETTE_R][dev->anapal_select];
            else if (dev->mode2[MODE2_16COLOR])
                temp = dev->anapal[PALETTE_R][dev->anapal_select & 0x0f];
            else
                temp = dev->digipal[2];
            break;
        case 3:
            if (dev->mode2[MODE2_256COLOR])
                temp = dev->anapal[PALETTE_B][dev->anapal_select];
            else if (dev->mode2[MODE2_16COLOR])
                temp = dev->anapal[PALETTE_B][dev->anapal_select & 0x0f];
            else
                temp = dev->digipal[3];
            break;
        default:
            break;
    }
    return temp;
}

/* horizontal frequency */
static void
pc98x1_horiz_freq_write(uint16_t addr, uint8_t value, void *priv)
{
    /* ioport 0x9a8 */
}

static uint8_t
pc98x1_horiz_freq_read(uint16_t addr, void *priv)
{
    /* ioport 0x9a8 */
    return 0;
}

/* memory */
static void
pc98x1_tvram_writeb(uint32_t addr, uint8_t value, void *priv)
{
    pc98x1_vid_t *dev = (pc98x1_vid_t *)priv;

    if (addr < 0x3fe2) {
        dev->tvram[addr] = value;
        dev->dirty |= DIRTY_TVRAM;
    } else if (addr < 0x4000) {
        /* memory switch */
        if (dev->mode1[MODE1_MEMSW]) {
            if (dev->tvram[addr] != value) {
                dev->tvram[addr] = value;
                /* save memory switch */
//                pc98x1_memsw_save(dev);
            }
        }
    } else
        pc98x1_cgwindow_writeb(dev, addr & 0x3fff, value);
}

static void
pc98x1_tvram_writew(uint32_t addr, uint16_t value, void *priv)
{
    pc98x1_vid_t *dev = (pc98x1_vid_t *)priv;

    if (addr < 0x3fe2) {
        *(uint16_t *)(dev->tvram + addr) = value;
        dev->dirty |= DIRTY_TVRAM;
    } else if (addr < 0x4000) {
        /* memory switch */
        if (dev->mode1[MODE1_MEMSW]) {
            if (*(uint16_t *)(dev->tvram + addr) != value) {
                *(uint16_t *)(dev->tvram + addr) = value;
                /* save memory switch */
//                pc98x1_memsw_save(dev);
            }
        }
    } else
        pc98x1_cgwindow_writew(dev, addr & 0x3fff, value);
}

static void
pc98x1_tvram_writel(uint32_t addr, uint32_t value, void *priv)
{
    pc98x1_vid_t *dev = (pc98x1_vid_t *)priv;

    if (addr < 0x3fe2) {
        *(uint32_t *)(dev->tvram + addr) = value;
        dev->dirty |= DIRTY_TVRAM;
    } else if (addr < 0x4000) {
        /* memory switch */
        if (dev->mode1[MODE1_MEMSW]) {
            if (*(uint32_t *)(dev->tvram + addr) != value) {
                *(uint32_t *)(dev->tvram + addr) = value;
                /* save memory switch */
//                pc98x1_memsw_save(dev);
            }
        }
    } else
        pc98x1_cgwindow_writel(dev, addr & 0x3fff, value);
}

static uint8_t
pc98x1_tvram_readb(uint32_t addr, void *priv)
{
    pc98x1_vid_t *dev = (pc98x1_vid_t *)priv;

    if (addr < 0x4000)
        return dev->tvram[addr];

    return pc98x1_cgwindow_readb(dev, addr & 0x3fff);
}

static uint16_t
pc98x1_tvram_readw(uint32_t addr, void *priv)
{
    pc98x1_vid_t *dev = (pc98x1_vid_t *)priv;

    if (addr < 0x4000)
        return *(uint16_t *)(dev->tvram + addr);

    return pc98x1_cgwindow_readw(dev, addr & 0x3fff);
}

static uint32_t
pc98x1_tvram_readl(uint32_t addr, void *priv)
{
    pc98x1_vid_t *dev = (pc98x1_vid_t *)priv;

    if (addr < 0x4000)
        return *(uint32_t *)(dev->tvram + addr);

    return pc98x1_cgwindow_readl(dev, addr & 0x3fff);
}

static void
pc98x1_vram_writeb(uint32_t addr, uint8_t value, void *priv)
{
    pc98x1_vid_t *dev = (pc98x1_vid_t *)priv;

    if (dev->mode2[MODE2_256COLOR]) {
        if (addr < 0x8000) {
            dev->vram256_draw_0[addr] = value;
            dev->dirty |= dev->bank256_draw_0;
        } else if (addr < 0x10000) {
            dev->vram256_draw_1[addr & 0x7fff] = value;
            dev->dirty |= dev->bank256_draw_1;
        }
    } else if (dev->grcg_mode & GRCG_CG_MODE) {
        if (dev->mode2[MODE2_EGC])
            egc_mem_writeb(&dev->egc, addr, value);
        else
            pc98x1_grcg_mem_writeb(dev, addr, value);

        dev->dirty |= dev->bank_draw;
    } else {
        dev->vram16_draw_b[addr] = value;
        dev->dirty |= dev->bank_draw;
    }
}

static void
pc98x1_vram_writew(uint32_t addr, uint16_t value, void *priv)
{
    pc98x1_vid_t *dev = (pc98x1_vid_t *)priv;

    if (dev->mode2[MODE2_256COLOR]) {
        if (addr < 0x8000) {
            *(uint16_t *)(dev->vram256_draw_0 + addr) = value;
            dev->dirty |= dev->bank256_draw_0;
        } else if (addr < 0x10000) {
            *(uint16_t *)(dev->vram256_draw_1 + (addr & 0x7fff)) = value;
            dev->dirty |= dev->bank256_draw_1;
        }
    } else if (dev->grcg_mode & GRCG_CG_MODE) {
        if (dev->mode2[MODE2_EGC])
            egc_mem_writew(&dev->egc, addr, value);
        else
            pc98x1_grcg_mem_writew(dev, addr, value);

        dev->dirty |= dev->bank_draw;
    } else {
        *(uint16_t *)(dev->vram16_draw_b + addr) = value;
        dev->dirty |= dev->bank_draw;
    }
}

static void
pc98x1_vram_writel(uint32_t addr, uint32_t value, void *priv)
{
    pc98x1_vid_t *dev = (pc98x1_vid_t *)priv;

    if (dev->mode2[MODE2_256COLOR]) {
        if (addr < 0x8000) {
            *(uint32_t *)(dev->vram256_draw_0 + addr) = value;
            dev->dirty |= dev->bank256_draw_0;
        } else if (addr < 0x10000) {
            *(uint32_t *)(dev->vram256_draw_1 + (addr & 0x7fff)) = value;
            dev->dirty |= dev->bank256_draw_1;
        }
    } else if (dev->grcg_mode & GRCG_CG_MODE) {
        if (dev->mode2[MODE2_EGC]) {
            egc_mem_writew(&dev->egc, addr, value & 0xffff);
            egc_mem_writew(&dev->egc, addr + 2, value >> 16);
        } else {
            pc98x1_grcg_mem_writew(dev, addr, value & 0xffff);
            pc98x1_grcg_mem_writew(dev, addr + 2, value >> 16);
        }
        dev->dirty |= dev->bank_draw;
    } else {
        *(uint32_t *)(dev->vram16_draw_b + addr) = value;
        dev->dirty |= dev->bank_draw;
    }
}

static uint8_t
pc98x1_vram_readb(uint32_t addr, void *priv)
{
    pc98x1_vid_t *dev = (pc98x1_vid_t *)priv;

    if (dev->mode2[MODE2_256COLOR]) {
        if (addr < 0x8000)
            return dev->vram256_draw_0[addr];
        else if (addr < 0x10000)
            return dev->vram256_draw_1[addr & 0x7fff];

        return 0xff;
    } else if (dev->grcg_mode & GRCG_CG_MODE) {
        if (dev->mode2[MODE2_EGC])
            return egc_mem_readb(&dev->egc, addr);
        else
            return pc98x1_grcg_mem_readb(dev, addr);
    }

    return dev->vram16_draw_b[addr];
}

static uint16_t
pc98x1_vram_readw(uint32_t addr, void *priv)
{
    pc98x1_vid_t *dev = (pc98x1_vid_t *)priv;

    if (dev->mode2[MODE2_256COLOR]) {
        if (addr < 0x8000)
            return *(uint16_t *)(dev->vram256_draw_0 + addr);
        else if (addr < 0x10000)
            return *(uint16_t *)(dev->vram256_draw_1 + (addr & 0x7fff));

        return 0xffff;
    } else if (dev->grcg_mode & GRCG_CG_MODE) {
        if (dev->mode2[MODE2_EGC])
            return egc_mem_readw(&dev->egc, addr);
        else
            return pc98x1_grcg_mem_readw(dev, addr);
    }

    return *(uint16_t *)(dev->vram16_draw_b + addr);
}

static uint32_t
pc98x1_vram_readl(uint32_t addr, void *priv)
{
    pc98x1_vid_t *dev = (pc98x1_vid_t *)priv;
    uint32_t value;

    if (dev->mode2[MODE2_256COLOR]) {
        if (addr < 0x8000)
            return *(uint32_t *)(dev->vram256_draw_0 + addr);
        else if (addr < 0x10000)
            return *(uint32_t *)(dev->vram256_draw_1 + (addr & 0x7fff));

        return 0xffffffff;
    } else if (dev->grcg_mode & GRCG_CG_MODE) {
        if (dev->mode2[MODE2_EGC]) {
            value = egc_mem_readw(&dev->egc, addr);
            value |= egc_mem_readw(&dev->egc, addr + 2) << 16;
            return value;
        } else {
            value = pc98x1_grcg_mem_readw(dev, addr);
            value |= pc98x1_grcg_mem_readw(dev, addr + 2) << 16;
            return value;
        }
    }
    return *(uint32_t *)(dev->vram16_draw_b + addr);
}

static void
pc98x1_vram_b0000_writeb(uint32_t addr, uint8_t value, void *priv)
{
    pc98x1_vid_t *dev = (pc98x1_vid_t *)priv;

    if (dev->ems_selected)
        dev->ems[addr] = value;
    else
        pc98x1_vram_writeb(addr + 0x8000, value, dev);
}

static void
pc98x1_vram_b0000_writew(uint32_t addr, uint16_t value, void *priv)
{
    pc98x1_vid_t *dev = (pc98x1_vid_t *)priv;

    if (dev->ems_selected)
        *(uint16_t *)(dev->ems + addr) = value;
    else
        pc98x1_vram_writew(addr + 0x8000, value, dev);
}

static void
pc98x1_vram_b0000_writel(uint32_t addr, uint32_t value, void *priv)
{
    pc98x1_vid_t *dev = (pc98x1_vid_t *)priv;

    if (dev->ems_selected)
        *(uint32_t *)(dev->ems + addr) = value;
    else
        pc98x1_vram_writel(addr + 0x8000, value, dev);
}

static uint8_t
pc98x1_vram_b0000_readb(uint32_t addr, void *priv)
{
    pc98x1_vid_t *dev = (pc98x1_vid_t *)priv;

    if (dev->ems_selected)
        return dev->ems[addr];

    return pc98x1_vram_readb(addr + 0x8000, dev);
}

static uint16_t
pc98x1_vram_b0000_readw(uint32_t addr, void *priv)
{
    pc98x1_vid_t *dev = (pc98x1_vid_t *)priv;

    if (dev->ems_selected)
        return *(uint16_t *)(dev->ems + addr);

    return pc98x1_vram_readw(addr + 0x8000, dev);
}

static uint32_t
pc98x1_vram_b0000_readl(uint32_t addr, void *priv)
{
    pc98x1_vid_t *dev = (pc98x1_vid_t *)priv;

    if (dev->ems_selected)
        return *(uint32_t *)(dev->ems + addr);

    return pc98x1_vram_readl(addr + 0x8000, dev);
}

static void
pc98x1_vram_e0000_writeb(uint32_t addr, uint8_t value, void *priv)
{
    pc98x1_vid_t *dev = (pc98x1_vid_t *)priv;
    static const uint32_t iomask8[4]  = { 0xffffff00, 0xffff00ff, 0xff00ffff, 0x00ffffff };
    uint32_t src, prev, mask;
    int offset;
    bool rop_update = false;

    if (dev->mode2[MODE2_256COLOR]) {
        switch (addr & 0x7fff) {
            case 0x0004:
                dev->vram256_bank_0 = value;
                if (value & 0x08)
                    dev->bank256_draw_0 = DIRTY_VRAM1;
                else
                    dev->bank256_draw_0 = DIRTY_VRAM0;

                dev->vram256_draw_0 = dev->vram256 dev (dev->vram256_bank_0 & 0x0f) * 0x8000;
                break;
            case 0x0006:
                dev->vram256_bank_1 = value;
                if (value & 0x08)
                    dev->bank256_draw_1 = DIRTY_VRAM1;
                else
                    dev->bank256_draw_1 = DIRTY_VRAM0;

                dev->vram256_draw_1 = dev->vram256 + (dev->vram256_bank_1 & 0x0f) * 0x8000;
                break;
            case 0x0100: /*PEGC planar*/
                pclog("ToDo later.");
                break;
            default:
                break;
        }
    } else
        pc98x1_vram_writeb(addr + 0x18000, value, dev);
}

static void
pc98x1_vram_e0000_writew(uint32_t addr, uint16_t value, void *priv)
{
    pc98x1_vid_t *dev = (pc98x1_vid_t *)priv;

    if (dev->mode2[MODE2_256COLOR])
        pc98x1_vram_e0000_writeb(addr, valu, deve);
    else
        pc98x1_vram_writew(addr + 0x18000, value, dev);
}

static void
pc98x1_vram_e0000_writel(uint32_t addr, uint32_t value, void *priv)
{
    pc98x1_vid_t *dev = (pc98x1_vid_t *)priv;

    if (dev->mode2[MODE2_256COLOR]) {
        pc98x1_vram_e0000_writew(addr, value & 0xffff, dev);
        pc98x1_vram_e0000_writew(addr + 2, value >> 16, dev);
    } else
        pc98x1_vram_writel(addr + 0x18000, value, dev);
}

static uint8_t
pc98x1_vram_e0000_readb(uint32_t addr, void *priv)
{
    pc98x1_vid_t *dev = (pc98x1_vid_t *)priv;

    if (dev->mode2[MODE2_256COLOR]) {
        switch (addr & 0x7fff) {
            case 0x0004:
                return dev->vram256_bank_0 & 0xff;
            case 0x0005:
                return dev->vram256_bank_0 >> 8;
            case 0x0006:
                return dev->vram256_bank_1 & 0xff;
            case 0x0007:
                return dev->vram256_bank_1 >> 8;
            case 0x0100:
                return 0; /* support packed pixel only */
            default:
                break;
        }
        return 0xff;
    }
    return pc98x1_vram_readb(addr + 0x18000, dev);
}

static uint16_t
pc98x1_vram_e0000_readw(uint32_t addr, void *priv)
{
    pc98x1_vid_t *dev = (pc98x1_vid_t *)priv;

    if (dev->mode2[MODE2_256COLOR]) {
        switch (addr & 0x7fff) {
            case 0x0004:
                return dev->vram256_bank_0;
            case 0x0006:
                return dev->vram256_bank_1;
            case 0x0100:
                return 0; /* support packed pixel only */
            default:
                break;
        }
        return 0xffff;
    }
    return pc98x1_vram_readw(addr + 0x18000, dev);
}

static uint32_t
pc98x1_vram_e0000_readl(uint32_t addr, void *priv)
{
    pc98x1_vid_t *dev = (pc98x1_vid_t *)priv;
    uint32_t value = 0xffffffff;

    if (dev->mode2[MODE2_256COLOR]) {
        value = pc98x1_vram_e0000_readw(addr, dev);
        value |= (pc98x1_vram_e0000_readw(addr + 1, dev) << 16);
    } else
        value = pc98x1_vram_readl(opaque, addr + 0x18000);

    return value;
}

static void
pc98x1_vram_f00000_writeb(uint32_t addr, uint8_t value, void *priv)
{
    pc98x1_vid_t *dev = (pc98x1_vid_t *)priv;

    if (dev->mode2[MODE2_256COLOR]) {
        dev->vram256[addr] = value;
        if (addr & 0x40000)
            dev->dirty |= DIRTY_VRAM1;
        else if (addr < 0x10000)
            dev->dirty |= DIRTY_VRAM0;
    }
}

static void
pc98x1_vram_f00000_writew(uint32_t addr, uint16_t value, void *priv)
{
    pc98x1_vid_t *dev = (pc98x1_vid_t *)priv;

    if (dev->mode2[MODE2_256COLOR]) {
        *(uint16_t *)(dev->vram256 + addr) = value;
        if (addr & 0x40000)
            dev->dirty |= DIRTY_VRAM1;
        else if (addr < 0x10000)
            dev->dirty |= DIRTY_VRAM0;
    }
}

static void
pc98x1_vram_f00000_writel(uint32_t addr, uint32_t value, void *priv)
{
    pc98x1_vid_t *dev = (pc98x1_vid_t *)priv;

    if (dev->mode2[MODE2_256COLOR]) {
        *(uint32_t *)(dev->vram256 + addr) = value;
        if (addr & 0x40000)
            dev->dirty |= DIRTY_VRAM1;
        else if (addr < 0x10000)
            dev->dirty |= DIRTY_VRAM0;
    }
}

static uint8_t
pc98x1_vram_f00000_readb(uint32_t addr, void *priv)
{
    pc98x1_vid_t *dev = (pc98x1_vid_t *)priv;

    if (dev->mode2[MODE2_256COLOR])
        return dev->vram256[addr];

    return 0xff;
}

static uint16_t
pc98x1_vram_f00000_readw(uint32_t addr, void *priv)
{
    pc98x1_vid_t *dev = (pc98x1_vid_t *)priv;

    if (dev->mode2[MODE2_256COLOR])
        return *(uint16_t *)(dev->vram256 + addr);

    return 0xffff;
}

static uint32_t
pc98x1_vram_f00000_readl(uint32_t addr, void *priv)
{
    pc98x1_vid_t *dev = (pc98x1_vid_t *)priv;

    if (dev->mode2[MODE2_256COLOR])
        return *(uint32_t *)(dev->vram256 + addr);

    return 0xffffffff;
}

/* gdc */
static void
pc98x1_gdc_tvram_write(uint32_t addr1, uint8_t val, void *priv)
{
    pc98x1_vid_t *dev = (pc98x1_vid_t *)priv;
    uint32_t addr = addr1 & 0x3fff;

    if (addr < 0x3fe2)
        dev->tvram[addr] = value;
}

static uint32_t
pc98x1_gdc_tvram_read(uint32_t addr1, void *priv)
{
    pc98x1_vid_t *dev = (pc98x1_vid_t *)priv;
    uint32_t addr = addr1 & 0x3fff;

    return dev->tvram[addr];
}

static void
pc98x1_gdc_vram_write(uint32_t addr1, uint8_t value, void *priv)
{
    pc98x1_vid_t *dev = (pc98x1_vid_t *)priv;
    uint32_t addr = addr1 & 0x1ffff;

    if (addr < 0x18000)
        return pc98x1_vram_writeb(addr, value, dev);

    return pc98x1_vram_e0000_writeb(addr & 0x7fff, value, dev);
}

static uint8_t
pc98x1_gdc_vram_read(uint32_t addr1, void *priv)
{
    pc98x1_vid_t *dev = (pc98x1_vid_t *)priv;
    uint32_t addr = addr1 & 0x1ffff;

    if (addr < 0x18000)
        return pc98x1_vram_readb(addr, dev);

    return pc98x1_vram_e0000_readb(addr & 0x7fff, dev);
}

static void
pc98x1_update_palette(pc98x1_vid_t *dev)
{
    int i;
    uint8_t r, g, b;

    for (i = 0; i < 8; i++) {
        r = (i & 2) ? 0xff : 0;
        g = (i & 4) ? 0xff : 0;
        b = (i & 1) ? 0xff : 0;
        dev->palette_chr[i] = makecol(r, g, b);
    }
    if (dev->mode2[MODE2_256COLOR]) {
        for (i = 0; i < 256; i++) {
            r = dev->anapal[PALETTE_R][i];
            g = dev->anapal[PALETTE_G][i];
            b = dev->anapal[PALETTE_B][i];
            dev->palette_gfx[i] = makecol(r, g, b);
        }
    } else if (dev->mode2[MODE2_16COLOR]) {
        for (i = 0; i < 16; i++) {
            r = dev->anapal[PALETTE_R][i] << 4;
            g = dev->anapal[PALETTE_G][i] << 4;
            b = dev->anapal[PALETTE_B][i] << 4;
            dev->palette_gfx[i] = makecol(r, g, b);
        }
    } else {
        for (i = 0; i < 4; i++) {
            static int lo[4] = {7, 5, 6, 4};
            static int hi[4] = {3, 1, 2, 0};
            r = (dev->digipal[i] & 0x02) ? 0xff : 0;
            g = (dev->digipal[i] & 0x04) ? 0xff : 0;
            b = (dev->digipal[i] & 0x01) ? 0xff : 0;
            dev->palette_gfx[lo[i]] = makecol(r, g, b);
            r = (dev->digipal[i] & 0x20) ? 0xff : 0;
            g = (dev->digipal[i] & 0x40) ? 0xff : 0;
            b = (dev->digipal[i] & 0x10) ? 0xff : 0;
            dev->palette_gfx[hi[i]] = makecol(r, g, b);
        }
    }
}

static void
pc98x1_render_chr_screen(pc98x1_vid_t *dev)
{
    int pl, bl, cl;
    int sur, sdr;
    uint32_t *addr, *addr2;
    uint32_t cursor_addr;
    int cursor_top, cursor_bottom;
    int ytop, ysur, ysdr;
    int l, x, y;
    int xofs, addrofs;

    pl = dev->pl & 31;
    if (pl)
        pl = 32 - pl;

    bl = dev->bl + pl + 1;
    cl = dev->cl;
    sur = dev->sur & 31;
    if (sur)
        sur = 32 - sur;

    sdr = dev->sdr + 1;

    addr = pc98x1_gdc_get_address(&dev->gdc_chr, 2, 0x1fff);
    addr2 = addr + 160 * (sur + sdr);
    pc98x1_gdc_get_cursor_address(&dev->gdc_chr, 0x1fff,
                           &cursor_addr, &cursor_top, &cursor_bottom);
    ytop = 0;
    ysur = bl * sur;
    ysdr = bl * (sur + sdr);

    if (dev->mode1[MODE1_COLUMN]) {
        xofs = 16;
        addrofs = 2;
    } else {
        xofs = 8;
        addrofs = 1;
    }
    memset(dev->tvram_buffer, 0, 640 * 480);

    for (y = 0; y < 400; y += bl) {
        uint32_t gaiji1st = 0, last = 0, offset;
        int kanji2nd = 0;
        if (y == ysur) {
            ytop = y;
            y -= dev->ssl;
            ysur = 400;
        }
        if (y >= ysdr) {
            y = ytop = ysdr;
            addr = addr2;
            ysdr = 400;
        }
        for (x = 0; x < 640; x += xofs) {
            uint16_t code = *(uint16_t *)(dev->tvram + *addr);
            uint8_t attr = dev->tvram[*addr | 0x2000];
            uint8_t color = (attr & ATTR_COL) ? (attr >> 5) : 8;
            uint8_t cursor = (*addr == cursor_addr);
            addr += addrofs;
            if (kanji2nd) {
                kanji2nd = 0;
                offset = last + 0x800;
            } else if (code & 0xff00) {
                uint16_t lo = code & 0x7f;
                uint16_t hi = (code >> 8) & 0x7f;
                offset = (lo << 4) | (hi << 12);
                if (lo == 0x56 || lo == 0x57) {
                    offset += gaiji1st;
                    gaiji1st ^= 0x800;
                } else {
                    uint16_t lo = code & 0xff;
                    if (lo < 0x09 || lo >= 0x0c)
                        kanji2nd = 1;

                    gaiji1st = 0;
                }
            } else {
                uint16_t lo = code & 0xff;
                if (dev->mode1[MODE1_FONTSEL])
                    offset = 0x80000 | (lo << 4);
                else
                    offset = 0x82000 | (lo << 4);

                gaiji1st = 0;
            }
            last = offset;
            for (l = 0; l < cl && l < 16; l++) {
                int yy = y + l + pl;
                if (yy >= ytop && yy < 480) {
                    uint8_t *dest = dev->tvram_buffer + yy * 640 + x;
                    uint8_t pattern = dev->font[offset + l];
                    if (!(attr & ATTR_ST))
                        pattern = 0;
                    else if (((attr & ATTR_BL) && (dev->blink & 0x20)) ||
                               (attr & ATTR_RV))
                        pattern = ~pattern;

                    if ((attr & ATTR_UL) && l == 15)
                        pattern = 0xff;

                    if (attr & ATTR_VL)
                        pattern |= 0x08;

                    if (cursor && l >= cursor_top && l < cursor_bottom)
                        pattern = ~pattern;

                    if (dev->mode1[MODE1_COLUMN]) {
                        if (pattern & 0x80) dest[ 0] = dest[ 1] = color;
                        if (pattern & 0x40) dest[ 2] = dest[ 3] = color;
                        if (pattern & 0x20) dest[ 4] = dest[ 5] = color;
                        if (pattern & 0x10) dest[ 6] = dest[ 7] = color;
                        if (pattern & 0x08) dest[ 8] = dest[ 9] = color;
                        if (pattern & 0x04) dest[10] = dest[11] = color;
                        if (pattern & 0x02) dest[12] = dest[13] = color;
                        if (pattern & 0x01) dest[14] = dest[15] = color;
                    } else {
                        if (pattern & 0x80) dest[0] = color;
                        if (pattern & 0x40) dest[1] = color;
                        if (pattern & 0x20) dest[2] = color;
                        if (pattern & 0x10) dest[3] = color;
                        if (pattern & 0x08) dest[4] = color;
                        if (pattern & 0x04) dest[5] = color;
                        if (pattern & 0x02) dest[6] = color;
                        if (pattern & 0x01) dest[7] = color;
                    }
                }
            }
        }
    }
}

static void
pc98x1_render_gfx_screen(pc98x1_vid_t *dev)
{
    uint8_t *dest;
    int x, y;
    uint8_t b, r, g, e = 0;

    static int prev_mode = -1;
    int mode;

    if (dev->mode2[MODE2_256COLOR]) {
        int addr = 0;
        if (dev->mode2[MODE2_480LINE]) {
            dest = dev->vram0_buffer;
            for (y = 0; y < 480; y++) {
                for (x = 0; x < 640; x++)
                    *dest++ = dev->vram256[addr++];

                addr += 128 * 3;
            }
            mode = 2;
        } else {
            if (dev->bank_disp == DIRTY_VRAM0)
                dest = dev->vram0_buffer;
            else
                dest = dev->vram1_buffer;

            for (y = 0; y < 400; y++) {
                for (x = 0; x < 640; x++) {
                    *dest++ = dev->vram256_disp[addr++];
                }
            }
            mode = 1;
        }
    } else {
        uint32_t *addr = pc98x1_gdc_get_address(&dev->gdc_gfx, 1, 0x7fff);
        if (dev->bank_disp == DIRTY_VRAM0)
            dest = dev->vram0_buffer;
        else
            dest = dev->vram1_buffer;

        for (y = 0; y < 400; y++) {
            for (x = 0; x < 640; x += 8) {
                b = dev->vram16_draw_b[*addr];
                r = dev->vram16_draw_r[*addr];
                g = dev->vram16_draw_g[*addr];
                if (dev->mode2[MODE2_16COLOR])
                    e = dev->vram16_draw_e[*addr];

                addr++;
                *dest++ = ((b & 0x80) >> 7) | ((r & 0x80) >> 6) | ((g & 0x80) >> 5) | ((e & 0x80) >> 4);
                *dest++ = ((b & 0x40) >> 6) | ((r & 0x40) >> 5) | ((g & 0x40) >> 4) | ((e & 0x40) >> 3);
                *dest++ = ((b & 0x20) >> 5) | ((r & 0x20) >> 4) | ((g & 0x20) >> 3) | ((e & 0x20) >> 2);
                *dest++ = ((b & 0x10) >> 4) | ((r & 0x10) >> 3) | ((g & 0x10) >> 2) | ((e & 0x10) >> 1);
                *dest++ = ((b & 0x08) >> 3) | ((r & 0x08) >> 2) | ((g & 0x08) >> 1) | ((e & 0x08) >> 0);
                *dest++ = ((b & 0x04) >> 2) | ((r & 0x04) >> 1) | ((g & 0x04) >> 0) | ((e & 0x04) << 1);
                *dest++ = ((b & 0x02) >> 1) | ((r & 0x02) >> 0) | ((g & 0x02) << 1) | ((e & 0x02) << 2);
                *dest++ = ((b & 0x01) >> 0) | ((r & 0x01) << 1) | ((g & 0x01) << 2) | ((e & 0x01) << 3);
            }
            if (dev->mode1[MODE1_200LINE]) {
                memset(dest, 0, 640);
                dest += 640;
                y++;
            }
        }
        mode = 0;
    }
    if (prev_mode != mode) {
        switch (mode) {
            case 0:
                pclog("pc98vga: 640x400, 4bpp\n");
                break;
            case 1:
                pclog("pc98vga: 640x400, 8bpp\n");
                break;
            case 2:
                pclog("pc98vga: 640x480, 8bpp\n");
                break;
        }
        prev_mode = mode;
    }
}

static void
pc98x1_vid_timer(void *priv)
{
    pc98x1_vid_t *dev = (pc98x1_vid_t *)priv;
    uint8_t prev_blink = dev->blink;
    uint8_t dirty;
    uint8_t *src_chr;
    uint8_t *src_gfx;

    if (!dev->linepos) {
        timer_advance_u64(&dev->timer, dev->dispofftime);
        dev->linepos = 1;
        if (dev->dispon) {
            if (dev->displine == 0)
                video_wait_for_buffer();

            if (dev->mode1[MODE1_DISP]) {
                if (dev->dirty & DIRTY_PALETTE) {
                    /* update palette */
                    pc98x1_update_palette(dev);
                    dev->dirty &= ~DIRTY_PALETTE;
                    dev->dirty |= DIRTY_DISPLAY;
                }
                if (dev->gdc_chr.dirty & GDC_DIRTY_START) {
                    dev->gdc_chr.dirty &= ~GDC_DIRTY_START;
                    dev->dirty |= DIRTY_DISPLAY;
                }
                if (dev->gdc_chr.start) {
                    if ((dev->gdc_chr.dirty & GDC_DIRTY_CHR) || (dev->dirty & DIRTY_TVRAM)) {
                        /* update text screen */
                        pc98x1_render_chr_screen(dev);
                        dev->gdc_chr.dirty &= ~GDC_DIRTY_CHR;
                        dev->dirty &= ~DIRTY_TVRAM;
                        dev->dirty |= DIRTY_DISPLAY;
                    }
                }
                if (dev->gdc_gfx.dirty & GDC_DIRTY_START) {
                    dev->gdc_gfx.dirty &= ~GDC_DIRTY_START;
                    dev->dirty |= DIRTY_DISPLAY;
                }
                if (dev->gdc_gfx.start) {
                    dirty = dev->bank_disp;
                    if (dev->mode2[MODE2_256COLOR]) {
                        if (dev->mode2[MODE2_480LINE])
                            dirty = DIRTY_VRAM0 | DIRTY_VRAM1;
                    }
                    if ((dev->gdc_gfx.dirty & GDC_DIRTY_GFX) || (dev->dirty & dirty)) {
                        /* update cg screen */
                        pc98x1_render_gfx_screen(dev);
                        dev->gdc_gfx.dirty &= ~GDC_DIRTY_GFX;
                        dev->dirty &= ~dirty;
                        dev->dirty |= DIRTY_DISPLAY;
                    }
                }
            }

            /* update screen */
            if (dev->dirty & DIRTY_DISPLAY) {
                if (dev->mode1[MODE1_DISP]) {
                    /* output screen */
                    if (!dev->gdc_chr.start || dev->mode2[MODE2_256COLOR])
                        src_chr = dev->null_buffer;
                    else
                        src_chr = dev->tvram_buffer;

                    if (!dev->gdc_gfx.start)
                        src_gfx = dev->null_buffer;
                    else if (dev->mode2[MODE2_256COLOR] && dev->mode2[MODE2_480LINE])
                        src_gfx = dev->vram0_buffer;
                    else if (dev->bank_disp == DIRTY_VRAM0)
                        src_gfx = dev->vram0_buffer;
                    else
                        src_gfx = dev->vram1_buffer;

                    for (int y = 0; y < dev->height; y++) {
                        for (int x = 0; x < dev->width; x++) {
                            if (*src_chr)
                                buffer32->line[dev->displine][x] = dev->palette_chr[*src_chr & 0x07];
                            else
                                buffer32->line[dev->displine][x] = dev->palette_gfx[*src_gfx];

                            src_chr++;
                            src_gfx++;
                        }
                    }
                } else {
                    for (int y = 0; y < dev->height; y++) {
                        for (int x = 0; x < dev->width; x++) {
                            buffer32->line[dev->displine][x] = 0;
                        }
                    }
                }
                dev->dirty &= ~DIRTY_DISPLAY;
            }
        }

        if (++dev->displine == dev->height) {
            dev->vsync |= GDC_STAT_VSYNC;
            dev->dispon = 0;
            if (dev->crtv) {
                picint(1 << 2);
                dev->crtv = 0;
            } else
                picintc(1 << 2);
        }

        if (dev->displine == (dev->height + 32)) {
            dev->vsync &= ~GDC_STAT_VSYNC;
            dev->dispon = 1;
            dev->displine  = 0;
        }
    } else {
        timer_advance_u64(&dev->timer, dev->dispontime);
        dev->linepos = 0;

        if (dev->displine == dev->height) {
            /* resize screen */
            if (dev->width != xsize || dev->height != ysize) {
                xsize = dev->width;
                ysize = dev->height;
                set_screen_size(xsize, ysize);

                if (video_force_resize_get())
                    video_force_resize_set(0);

                dev->dirty |= DIRTY_DISPLAY;
            }
            video_blit_memtoscreen(0, 0, xsize, ysize);
            frames++;

            /* blink */
            dev->blink++;
            if ((prev_blink & 0x20) != (dev->blink & 0x20)) {
                dev->dirty |= DIRTY_TVRAM;
            }
        }
    }
}

/* font (based on Neko Project 2) */
static void
pc98x1_kanji_copy(uint8_t *dst, uint8_t *src, int from, int to)
{
    int i, j, k;
    uint8_t *p, *q;

    for (i = from; i < to; i++) {
        p = src + 0x1800 + (0x60 * 32 * (i - 1));
        q = dst + 0x20000 + (i << 4);
        for (j = 0x20; j < 0x80; j++) {
            for (k = 0; k < 16; k++) {
                *(q + 0x800) = *(p + 16);
                *q++ = *p++;
            }
            p += 16;
            q += 0x1000 - 16;
        }
    }
}

void
pc98x1_font_init(pc98x1_vid_t *dev, char *s)
{
    FILE *fp;
    uint8_t *buf = NULL;
    uint8_t *p, *q;
    int i, j;

    p = dev->font + 0x81000;
    q = dev->font + 0x82000;
    for (i = 0; i < 256; i++) {
        q += 8;
        for (j = 0; j < 4; j++) {
            uint32_t bit = 0;
            if (i & (1 << j))
                bit |= 0xf0f0f0f0;
            if (i & (0x10 << j))
                bit |= 0x0f0f0f0f;
            *(uint32_t *)p = bit;
            p += 4;
            *(uint16_t *)q = (uint16_t)bit;
            q += 2;
        }
    }
    for (i = 0; i < 0x80; i++) {
        q = dev->font + (i << 12);
        memset(q + 0x000, 0, 0x0560 - 0x000);
        memset(q + 0x580, 0, 0x0d60 - 0x580);
        memset(q + 0xd80, 0, 0x1000 - 0xd80);
    }
    fp = rom_fopen(s, "rb");
    if (fp == NULL)
        return;

    fseek(fp, 0, SEEK_SET);
    buf = malloc(0x46800);
    if (fread(s, 1, 0x46800, fp) == 0x46800) {
        /* 8x8 font */
        uint8_t *dst = dev->font + 0x82000;
        uint8_t *src = buf;
        int cnt = 256;
        while (cnt--) {
            memcpy(dst, src, 8);
            dst += 16;
            src += 8;
        }
        /* 8x16 font */
        memcpy(dev->font + 0x80000, buf + 0x0800, 16 * 128);
        memcpy(dev->font + 0x80800, buf + 0x1000, 16 * 128);
        /* kanji font */
        pc98x1_kanji_copy(dev->font, buf, 0x01, 0x30);
        pc98x1_kanji_copy(dev->font, buf, 0x30, 0x56);
        pc98x1_kanji_copy(dev->font, buf, 0x58, 0x5d);
    }
    fclose(fp);
    free(buf);
}

static const uint8_t memsw_default[] = {
    0xe1, 0x48, 0xe1, 0x05, 0xe1, 0x0c, 0xe1, 0x00,
    0xe1, 0x01, 0xe1, 0x40, 0xe1, 0x00, 0xe1, 0x00,
};

static void
pc98x1_vid_reset(pc98x1_vid_t *dev)
{
    int i;

    dev->bank_disp = DIRTY_VRAM0;
    dev->bank_draw = DIRTY_VRAM0;
    dev->bank256_draw_0 = DIRTY_VRAM0;
    dev->bank256_draw_1 = DIRTY_VRAM0;

    dev->vram16_disp_b = dev->vram16 + 0x00000;
    dev->vram16_disp_r = dev->vram16 + 0x08000;
    dev->vram16_disp_g = dev->vram16 + 0x10000;
    dev->vram16_disp_e = dev->vram16 + 0x18000;
    dev->vram16_draw_b = dev->vram16 + 0x00000;
    dev->vram16_draw_r = dev->vram16 + 0x08000;
    dev->vram16_draw_g = dev->vram16 + 0x10000;
    dev->vram16_draw_e = dev->vram16 + 0x18000;

    dev->vram256_disp = dev->vram256;
    dev->vram256_draw_0 = dev->vram256;
    dev->vram256_draw_1 = dev->vram256;

    egc_set_vram(&dev->egc, dev->vram16_draw_b);

    gdc_reset(&dev->gdc_chr);
    gdc_reset(&dev->gdc_gfx);
    egc_reset(&dev->egc);

    dev->crtv = 1;

    dev->grcg_mode = 0;
    dev->pl = 0;
    dev->bl = 0x0f;
    dev->cl = 0x10;
    dev->ssl = 0;
    dev->sur = 0;
    dev->sdr = 24;

    dev->ems_selected = 0;

    memset(dev->mode1, 0, sizeof(dev->mode1));
    memset(dev->mode2, 0, sizeof(dev->mode2));
    memset(dev->mode3, 0, sizeof(dev->mode3));
    dev->mode_select = 0;

    dev->font_code = 0;
    dev->font_line = 0;
    pc98x1_cgwindow_set_addr(dev);

    /* reset palette */
    for (i = 0; i < 8; i++) {
        dev->anapal[PALETTE_B][i] = (i & 1) ? 0x07 : 0;
        dev->anapal[PALETTE_R][i] = (i & 2) ? 0x07 : 0;
        dev->anapal[PALETTE_G][i] = (i & 4) ? 0x07 : 0;
    }
    for (i = 8; i < 16; i++) {
        dev->anapal[PALETTE_B][i] = (i & 1) ? 0x0f : 0;
        dev->anapal[PALETTE_R][i] = (i & 2) ? 0x0f : 0;
        dev->anapal[PALETTE_G][i] = (i & 4) ? 0x0f : 0;
    }
    dev->anapal_select = 0;

    /* force redraw */
    dev->dirty = 0xff;
    dev->blink = 0;

    dev->width = 640;
    dev->height = 400;
}

static void *
pc98x1_vid_init(UNUSED(const device_t *info))
{
    pc98x1_vid_t *dev = (pc98x1_vid_t *)calloc(1, sizeof(pc98x1_vid_t))

    for (int i = 0; i < 16; i++)
        dev->tvram[0x3fe0 + (i << 1)] = memsw_default[i];

    upd7220_init(&dev->gdc_chr, dev, pc98x1_gdc_tvram_read, pc98x1_gdc_tvram_write);
    upd7220_init(&dev->gdc_gfx, dev, pc98x1_gdc_vram_read, pc98x1_gdc_vram_write);
    egc_init(&dev->egc, dev);

    mem_mapping_add(&dev->tvram_mapping, 0xa0000, 0x08000, pc98x1_tvram_readb, pc98x1_tvram_readw, pc98x1_tvram_readl, pc98x1_tvram_writeb, pc98x1_tvram_writew, pc98x1_tvram_writel, NULL, MEM_MAPPING_EXTERNAL, dev);
    mem_mapping_add(&dev->vram_a8000_mapping, 0xa8000, 0x08000, pc98x1_vram_readb, pc98x1_vram_readw, pc98x1_vram_readl, pc98x1_vram_writeb, pc98x1_vram_writew, pc98x1_vram_writel, NULL, MEM_MAPPING_EXTERNAL, dev);
    mem_mapping_add(&dev->vram_b0000_mapping, 0xb0000, 0x10000, pc98x1_vram_b0000_readb, pc98x1_vram_b0000_readw, pc98x1_vram_b0000_readl, pc98x1_vram_b0000_writeb, pc98x1_vram_b0000_writew, pc98x1_vram_b0000_writel, NULL, MEM_MAPPING_EXTERNAL, dev);

    mem_mapping_add(&dev->vram_e0000_mapping, 0xe0000, 0x08000, pc98x1_vram_e0000_readb, pc98x1_vram_e0000_readw, pc98x1_vram_e0000_readl, pc98x1_vram_e0000_writeb, pc98x1_vram_e0000_writew, pc98x1_vram_e0000_writel, NULL, MEM_MAPPING_EXTERNAL, dev);
    mem_mapping_add(&dev->vram_f00000_mapping, 0xf00000, 0xa0000, pc98x1_vram_f00000_readb, pc98x1_vram_f00000_readw, pc98x1_vram_f00000_readl, pc98x1_vram_f00000_writeb, pc98x1_vram_f00000_writew, pc98x1_vram_f00000_writel, NULL, MEM_MAPPING_EXTERNAL, dev);

    io_sethandler_interleaved(0x0060, 0x0001, upd7220_read, NULL, NULL, upd7220_write, NULL, NULL, &dev->gdc_chr);
    io_sethandler(0x0064, 0x0001, NULL, NULL, NULL, pc98x1_vsync_write, NULL, NULL, dev);

    io_sethandler_interleaved(0x0068, 0x0001, pc98x1_mode_flipflop1_2_read, NULL, NULL, pc98x1_mode_flipflop1_2_write, NULL, NULL, dev);
    io_sethandler(0x006e, 0x0001, pc98x1_mode_flipflop3_read, NULL, NULL, pc98x1_mode_flipflop3_write, NULL, NULL, dev);

    io_sethandler_interleaved(0x0070, 0x0005, NULL, NULL, NULL, pc98x1_crtc_write, NULL, NULL, dev);

    io_sethandler_interleaved(0x007c, 0x0001, NULL, NULL, NULL, pc98x1_grcg_write, pc98x1_grcg_writew, NULL, dev);

    io_sethandler_interleaved(0x00a0, 0x0002, upd7220_read, NULL, NULL, upd7220_write, NULL, NULL, &dev->gdc_gfx);

    io_sethandler_interleaved(0x00a1, 0x0003, pc98x1_cgwindow_read, NULL, NULL, pc98x1_cgwindow_write, NULL, NULL, dev);
    io_sethandler(0x00a9, 0x0001, pc98x1_cgwindow_pattern_read, NULL, NULL, pc98x1_cgwindow_pattern_write, NULL, NULL, dev);

    io_sethandler_interleaved(0x00a4, 0x0001, pc98x1_vram_bank_read, NULL, NULL, pc98x1_vram_bank_write, NULL, NULL, dev);

    io_sethandler_interleaved(0x00a8, 0x0004, pc98x1_palette_read, NULL, NULL, pc98x1_palette_write, NULL, NULL, dev);

    io_sethandler(0x04a0, 0x0010, NULL, NULL, NULL, egc_ioport_writeb, egc_ioport_writew, NULL, &dev->egc);

    io_sethandler(0x09a0, 0x0001, pc98x1_mode_status_read, NULL, NULL, pc98x1_mode_select_write, NULL, NULL, dev);

    io_sethandler(0x09a8, 0x0001, pc98x1_horiz_freq_read, NULL, NULL, pc98x1_horiz_freq_write, NULL, NULL, dev);

    pc98x1_vid_reset(dev);

    timer_add(&dev->timer, pc98x1_vid_timer, dev, 1);
    return dev;
}

static void
pc98x1_vid_close(void *priv)
{
    pc98x1_vid_t *dev = (pc98x1_vid_t *) priv;

    if (dev)
        free(dev);
}

static void
pc98x1_speed_changed(void *priv)
{
    pc98x1_vid_t *dev = (pc98x1_vid_t *) priv;

    upd7220_recalctimings(&dev->gdc_chr);
    upd7220_recalctimings(&dev->gdc_gfx);
}

// clang-format off
const device_t pc98x1_vid_device = {
    .name = "NEC PC-98x1 Built-in Video",
    .internal_name = "pc98x1_vid",
    .flags = DEVICE_CBUS,
    .local = 0,
    .init = pc98x1_vid_init,
    .close = pc98x1_vid_close,
    .reset = NULL,
    { .available = NULL },
    .speed_changed = pc98x1_vid_speed_changed,
    .force_redraw = NULL,
    .config = NULL
};
