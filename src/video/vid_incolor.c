/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          Hercules InColor emulation.
 *
 *
 *
 * Authors: Sarah Walker, <https://pcem-emulator.co.uk/>
 *          Miran Grca, <mgrca8@gmail.com>
 *
 *          Copyright 2008-2018 Sarah Walker.
 *          Copyright 2016-2025 Miran Grca.
 */
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>
#include <86box/86box.h>
#include <86box/io.h>
#include <86box/timer.h>
#include <86box/lpt.h>
#include <86box/pit.h>
#include <86box/mem.h>
#include <86box/rom.h>
#include <86box/device.h>
#include <86box/video.h>
#include <86box/plat_unused.h>

/* extended CRTC registers */
#define INCOLOR_CRTC_XMODE   20 /* xMode register */
#define INCOLOR_CRTC_UNDER   21 /* Underline */
#define INCOLOR_CRTC_OVER    22 /* Overstrike */
#define INCOLOR_CRTC_EXCEPT  23 /* Exception */
#define INCOLOR_CRTC_MASK    24 /* Plane display mask & write mask */
#define INCOLOR_CRTC_RWCTRL  25 /* Read/write control */
#define INCOLOR_CRTC_RWCOL   26 /* Read/write colour */
#define INCOLOR_CRTC_PROTECT 27 /* Latch protect */
#define INCOLOR_CRTC_PALETTE 28 /* Palette */

/* character width */
#define INCOLOR_CW ((dev->crtc[INCOLOR_CRTC_XMODE] & INCOLOR_XMODE_90COL) ? 8 : 9)

/* mode control register */
#define INCOLOR_CTRL_GRAPH  0x02
#define INCOLOR_CTRL_ENABLE 0x08
#define INCOLOR_CTRL_BLINK  0x20
#define INCOLOR_CTRL_PAGE1  0x80

/* CRTC status register */
#define INCOLOR_STATUS_HSYNC 0x01 /* horizontal sync */
#define INCOLOR_STATUS_LIGHT 0x02
#define INCOLOR_STATUS_VIDEO 0x08
#define INCOLOR_STATUS_ID    0x50 /* Card identification */
#define INCOLOR_STATUS_VSYNC 0x80 /* -vertical sync */

/* configuration switch register */
#define INCOLOR_CTRL2_GRAPH 0x01
#define INCOLOR_CTRL2_PAGE1 0x02

/* extended mode register */
#define INCOLOR_XMODE_RAMFONT 0x01
#define INCOLOR_XMODE_90COL   0x02

/* Read/write control */
#define INCOLOR_RWCTRL_WRMODE   0x30
#define INCOLOR_RWCTRL_POLARITY 0x40

/* exception register */
#define INCOLOR_EXCEPT_CURSOR  0x0F /* Cursor colour */
#define INCOLOR_EXCEPT_PALETTE 0x10 /* Enable palette register */
#define INCOLOR_EXCEPT_ALTATTR 0x20 /* Use alternate attributes */

/* Default palette */
static const uint8_t defpal[16] = {
    0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07,
    0x38, 0x39, 0x3A, 0x3B, 0x3C, 0x3D, 0x3E, 0x3F
};

/* Mapping of inks to RGB */
static const uint8_t init_rgb[64][3] = {
  /* rgbRGB */
    {0x00,  0x00, 0x00}, /* 000000 */
    { 0x00, 0x00, 0xaa}, /* 000001 */
    { 0x00, 0xaa, 0x00}, /* 000010 */
    { 0x00, 0xaa, 0xaa}, /* 000011 */
    { 0xaa, 0x00, 0x00}, /* 000100 */
    { 0xaa, 0x00, 0xaa}, /* 000101 */
    { 0xaa, 0xaa, 0x00}, /* 000110 */
    { 0xaa, 0xaa, 0xaa}, /* 000111 */
    { 0x00, 0x00, 0x55}, /* 001000 */
    { 0x00, 0x00, 0xff}, /* 001001 */
    { 0x00, 0xaa, 0x55}, /* 001010 */
    { 0x00, 0xaa, 0xff}, /* 001011 */
    { 0xaa, 0x00, 0x55}, /* 001100 */
    { 0xaa, 0x00, 0xff}, /* 001101 */
    { 0xaa, 0xaa, 0x55}, /* 001110 */
    { 0xaa, 0xaa, 0xff}, /* 001111 */
    { 0x00, 0x55, 0x00}, /* 010000 */
    { 0x00, 0x55, 0xaa}, /* 010001 */
    { 0x00, 0xff, 0x00}, /* 010010 */
    { 0x00, 0xff, 0xaa}, /* 010011 */
    { 0xaa, 0x55, 0x00}, /* 010100 */
    { 0xaa, 0x55, 0xaa}, /* 010101 */
    { 0xaa, 0xff, 0x00}, /* 010110 */
    { 0xaa, 0xff, 0xaa}, /* 010111 */
    { 0x00, 0x55, 0x55}, /* 011000 */
    { 0x00, 0x55, 0xff}, /* 011001 */
    { 0x00, 0xff, 0x55}, /* 011010 */
    { 0x00, 0xff, 0xff}, /* 011011 */
    { 0xaa, 0x55, 0x55}, /* 011100 */
    { 0xaa, 0x55, 0xff}, /* 011101 */
    { 0xaa, 0xff, 0x55}, /* 011110 */
    { 0xaa, 0xff, 0xff}, /* 011111 */
    { 0x55, 0x00, 0x00}, /* 100000 */
    { 0x55, 0x00, 0xaa}, /* 100001 */
    { 0x55, 0xaa, 0x00}, /* 100010 */
    { 0x55, 0xaa, 0xaa}, /* 100011 */
    { 0xff, 0x00, 0x00}, /* 100100 */
    { 0xff, 0x00, 0xaa}, /* 100101 */
    { 0xff, 0xaa, 0x00}, /* 100110 */
    { 0xff, 0xaa, 0xaa}, /* 100111 */
    { 0x55, 0x00, 0x55}, /* 101000 */
    { 0x55, 0x00, 0xff}, /* 101001 */
    { 0x55, 0xaa, 0x55}, /* 101010 */
    { 0x55, 0xaa, 0xff}, /* 101011 */
    { 0xff, 0x00, 0x55}, /* 101100 */
    { 0xff, 0x00, 0xff}, /* 101101 */
    { 0xff, 0xaa, 0x55}, /* 101110 */
    { 0xff, 0xaa, 0xff}, /* 101111 */
    { 0x55, 0x55, 0x00}, /* 110000 */
    { 0x55, 0x55, 0xaa}, /* 110001 */
    { 0x55, 0xff, 0x00}, /* 110010 */
    { 0x55, 0xff, 0xaa}, /* 110011 */
    { 0xff, 0x55, 0x00}, /* 110100 */
    { 0xff, 0x55, 0xaa}, /* 110101 */
    { 0xff, 0xff, 0x00}, /* 110110 */
    { 0xff, 0xff, 0xaa}, /* 110111 */
    { 0x55, 0x55, 0x55}, /* 111000 */
    { 0x55, 0x55, 0xff}, /* 111001 */
    { 0x55, 0xff, 0x55}, /* 111010 */
    { 0x55, 0xff, 0xff}, /* 111011 */
    { 0xff, 0x55, 0x55}, /* 111100 */
    { 0xff, 0x55, 0xff}, /* 111101 */
    { 0xff, 0xff, 0x55}, /* 111110 */
    { 0xff, 0xff, 0xff}, /* 111111 */
};

typedef struct {
    mem_mapping_t mapping;

    uint8_t crtc[32];
    int     crtcreg;

    uint8_t ctrl, ctrl2, stat;

    uint64_t   dispontime, dispofftime;
    pc_timer_t timer;

    int firstline, lastline;

    int      linepos, displine;
    int      vc, sc;
    uint16_t ma, maback;
    int      con, coff, cursoron;
    int      dispon, blink;
    int      vsynctime;
    int      vadj;

    uint8_t palette[16]; /* EGA-style 16 -> 64 palette registers */
    uint8_t palette_idx; /* Palette write index */
    uint8_t latch[4];    /* Memory read/write latches */

    uint32_t rgb[64];

    uint8_t *vram;
} incolor_t;

static video_timings_t timing_incolor = { .type = VIDEO_ISA, .write_b = 8, .write_w = 16, .write_l = 32, .read_b = 8, .read_w = 16, .read_l = 32 };

static void
recalc_timings(incolor_t *dev)
{
    double disptime;
    double _dispontime;
    double _dispofftime;

    disptime     = dev->crtc[0] + 1;
    _dispontime  = dev->crtc[1];
    _dispofftime = disptime - _dispontime;
    _dispontime *= HERCCONST;
    _dispofftime *= HERCCONST;

    dev->dispontime  = (uint64_t) (_dispontime);
    dev->dispofftime = (uint64_t) (_dispofftime);
}

static void
incolor_out(uint16_t port, uint8_t val, void *priv)
{
    incolor_t *dev = (incolor_t *) priv;
    uint8_t    old;

    switch (port) {
        case 0x3b0:
        case 0x3b2:
        case 0x3b4:
        case 0x3b6:
            dev->crtcreg = val & 31;
            return;

        case 0x3b1:
        case 0x3b3:
        case 0x3b5:
        case 0x3b7:
            if (dev->crtcreg > 28)
                return;
            /* Palette load register */
            if (dev->crtcreg == INCOLOR_CRTC_PALETTE) {
                dev->palette[dev->palette_idx % 16] = val;
                ++dev->palette_idx;
            }
            old                     = dev->crtc[dev->crtcreg];
            dev->crtc[dev->crtcreg] = val;

            if (dev->crtc[10] == 6 && dev->crtc[11] == 7) {
                /*Fix for Generic Turbo XT BIOS,
                 * which sets up cursor registers wrong*/
                dev->crtc[10] = 0xb;
                dev->crtc[11] = 0xc;
            }
            if (old ^ val)
                recalc_timings(dev);
            return;

        case 0x3b8:
            old       = dev->ctrl;
            dev->ctrl = val;
            if (old ^ val)
                recalc_timings(dev);
            return;

        case 0x3bf:
            dev->ctrl2 = val;
            if (val & 2)
                mem_mapping_set_addr(&dev->mapping, 0xb0000, 0x10000);
            else
                mem_mapping_set_addr(&dev->mapping, 0xb0000, 0x08000);
            return;

        default:
            break;
    }
}

static uint8_t
incolor_in(uint16_t port, void *priv)
{
    incolor_t *dev = (incolor_t *) priv;
    uint8_t    ret = 0xff;

    switch (port) {
        case 0x3b0:
        case 0x3b2:
        case 0x3b4:
        case 0x3b6:
            ret = dev->crtcreg;
            break;

        case 0x3b1:
        case 0x3b3:
        case 0x3b5:
        case 0x3b7:
            if (dev->crtcreg > 28)
                break;

            dev->palette_idx = 0; /* Read resets the palette index */
            ret              = dev->crtc[dev->crtcreg];
            break;

        case 0x3ba:
            /* 0x50: InColor card identity */
            ret = (dev->stat & 0xf) | ((dev->stat & 8) << 4) | 0x50;
            break;

        default:
            break;
    }

    return ret;
}

static void
incolor_write(uint32_t addr, uint8_t val, void *priv)
{
    incolor_t    *dev   = (incolor_t *) priv;
    unsigned char wmask = dev->crtc[INCOLOR_CRTC_MASK];
    unsigned char wmode = dev->crtc[INCOLOR_CRTC_RWCTRL] & INCOLOR_RWCTRL_WRMODE;
    unsigned char fg    = dev->crtc[INCOLOR_CRTC_RWCOL] & 0x0F;
    unsigned char bg    = (dev->crtc[INCOLOR_CRTC_RWCOL] >> 4) & 0x0F;
    unsigned char w     = 0;
    unsigned char pmask; /* Mask of plane within colour value */
    unsigned char latch;

    addr &= 0xffff;

    /* In text mode, writes to the bottom 16k always touch all 4 planes */
    if (!(dev->ctrl & INCOLOR_CTRL_GRAPH) && addr < 0x4000) {
        dev->vram[addr] = val;
        return;
    }

    /* There are four write modes:
     * 0: 1 => foreground,    0 => background
     * 1: 1 => foreground,    0 => source latch
     * 2: 1 => source latch,  0 => background
     * 3: 1 => source latch,  0 => ~source latch
     */
    pmask = 1;
    for (uint8_t plane = 0; plane < 4; pmask <<= 1, wmask >>= 1, addr += 0x10000, plane++) {
        if (wmask & 0x10) /* Ignore writes to selected plane */
        {
            continue;
        }
        latch = dev->latch[plane];
        for (unsigned char vmask = 0x80 /* Mask of bit within byte */; vmask != 0; vmask >>= 1) {
            switch (wmode) {
                case 0x00:
                    if (val & vmask)
                        w = (fg & pmask);
                    else
                        w = (bg & pmask);
                    break;

                case 0x10:
                    if (val & vmask)
                        w = (fg & pmask);
                    else
                        w = (latch & vmask);
                    break;

                case 0x20:
                    if (val & vmask)
                        w = (latch & vmask);
                    else
                        w = (bg & pmask);
                    break;

                case 0x30:
                    if (val & vmask)
                        w = (latch & vmask);
                    else
                        w = ((~latch) & vmask);
                    break;

                default:
                    break;
            }

            /* w is nonzero to write a 1, zero to write a 0 */
            if (w)
                dev->vram[addr] |= vmask;
            else
                dev->vram[addr] &= ~vmask;
        }
    }
}

static uint8_t
incolor_read(uint32_t addr, void *priv)
{
    incolor_t    *dev = (incolor_t *) priv;
    unsigned      plane;
    unsigned char lp    = dev->crtc[INCOLOR_CRTC_PROTECT];
    unsigned char value = 0;
    unsigned char dc; /* "don't care" register */
    unsigned char bg; /* background colour */
    unsigned char fg;
    unsigned char pmask;

    addr &= 0xffff;

    /* Read the four planes into latches */
    for (plane = 0; plane < 4; plane++, addr += 0x10000) {
        dev->latch[plane] &= lp;
        dev->latch[plane] |= (dev->vram[addr] & ~lp);
    }
    addr &= 0xffff;

    /* In text mode, reads from the bottom 16k assume all planes have
     * the same contents */
    if (!(dev->ctrl & INCOLOR_CTRL_GRAPH) && addr < 0x4000) {
        return dev->latch[0];
    }

    /* For each pixel, work out if its colour matches the background */
    for (unsigned char mask = 0x80; mask != 0; mask >>= 1) {
        fg = 0;
        dc = dev->crtc[INCOLOR_CRTC_RWCTRL] & 0x0F;
        bg = (dev->crtc[INCOLOR_CRTC_RWCOL] >> 4) & 0x0F;
        for (plane = 0, pmask = 1; plane < 4; plane++, pmask <<= 1) {
            if (dc & pmask) {
                fg |= (bg & pmask);
            } else if (dev->latch[plane] & mask) {
                fg |= pmask;
            }
        }
        if (bg == fg)
            value |= mask;
    }

    if (dev->crtc[INCOLOR_CRTC_RWCTRL] & INCOLOR_RWCTRL_POLARITY)
        value = ~value;

    return value;
}

static void
draw_char_rom(incolor_t *dev, int x, uint8_t chr, uint8_t attr)
{
    int                  elg;
    int                  blk;
    unsigned             ull;
    unsigned             val;
    unsigned             ifg;
    unsigned             ibg;
    const unsigned char *fnt;
    uint32_t             fg;
    uint32_t             bg;
    int                  cw = INCOLOR_CW;

    blk = 0;
    if (dev->ctrl & INCOLOR_CTRL_BLINK) {
        if (attr & 0x80) {
            blk = (dev->blink & 16);
        }
        attr &= 0x7f;
    }

    if (dev->crtc[INCOLOR_CRTC_EXCEPT] & INCOLOR_EXCEPT_ALTATTR) {
        /* MDA-compatible attributes */
        ibg = 0;
        ifg = 7;
        if ((attr & 0x77) == 0x70) /* Invert */
        {
            ifg = 0;
            ibg = 7;
        }
        if (attr & 8) {
            ifg |= 8; /* High intensity FG */
        }
        if (attr & 0x80) {
            ibg |= 8; /* High intensity BG */
        }
        if ((attr & 0x77) == 0) /* Blank */
        {
            ifg = ibg;
        }
        ull = ((attr & 0x07) == 1) ? 13 : 0xffff;
    } else {
        /* CGA-compatible attributes */
        ull = 0xffff;
        ifg = attr & 0x0F;
        ibg = (attr >> 4) & 0x0F;
    }
    if (dev->crtc[INCOLOR_CRTC_EXCEPT] & INCOLOR_EXCEPT_PALETTE) {
        fg = dev->rgb[dev->palette[ifg]];
        bg = dev->rgb[dev->palette[ibg]];
    } else {
        fg = dev->rgb[defpal[ifg]];
        bg = dev->rgb[defpal[ibg]];
    }

    /* ELG set to stretch 8px character to 9px */
    if (dev->crtc[INCOLOR_CRTC_XMODE] & INCOLOR_XMODE_90COL) {
        elg = 0;
    } else {
        elg = ((chr >= 0xc0) && (chr <= 0xdf));
    }

    fnt = &(fontdatm[chr][dev->sc]);

    if (blk) {
        val = 0x000; /* Blinking, draw all background */
    } else if (dev->sc == ull) {
        val = 0x1ff; /* Underscore, draw all foreground */
    } else {
        val = fnt[0] << 1;

        if (elg) {
            val |= (val >> 1) & 1;
        }
    }
    for (int i = 0; i < cw; i++) {
        buffer32->line[dev->displine][x * cw + i] = (val & 0x100) ? fg : bg;
        val                                       = val << 1;
    }
}

static void
draw_char_ram4(incolor_t *dev, int x, uint8_t chr, uint8_t attr)
{
    int                  elg;
    int                  blk;
    unsigned             ull;
    unsigned             val[4];
    unsigned             ifg;
    unsigned             ibg;
    unsigned             cfg;
    unsigned             pmask;
    const unsigned char *fnt;
    uint32_t             fg;
    int                  cw      = INCOLOR_CW;
    int                  blink   = dev->ctrl & INCOLOR_CTRL_BLINK;
    int                  altattr = dev->crtc[INCOLOR_CRTC_EXCEPT] & INCOLOR_EXCEPT_ALTATTR;
    int                  palette = dev->crtc[INCOLOR_CRTC_EXCEPT] & INCOLOR_EXCEPT_PALETTE;

    blk = 0;
    if (blink) {
        if (attr & 0x80) {
            blk = (dev->blink & 16);
        }
        attr &= 0x7f;
    }

    if (altattr) {
        /* MDA-compatible attributes */
        ibg = 0;
        ifg = 7;
        if ((attr & 0x77) == 0x70) /* Invert */
        {
            ifg = 0;
            ibg = 7;
        }
        if (attr & 8) {
            ifg |= 8; /* High intensity FG */
        }
        if (attr & 0x80) {
            ibg |= 8; /* High intensity BG */
        }
        if ((attr & 0x77) == 0) /* Blank */
        {
            ifg = ibg;
        }
        ull = ((attr & 0x07) == 1) ? 13 : 0xffff;
    } else {
        /* CGA-compatible attributes */
        ull = 0xffff;
        ifg = attr & 0x0F;
        ibg = (attr >> 4) & 0x0F;
    }
    if (dev->crtc[INCOLOR_CRTC_XMODE] & INCOLOR_XMODE_90COL) {
        elg = 0;
    } else {
        elg = ((chr >= 0xc0) && (chr <= 0xdf));
    }
    fnt = dev->vram + 0x4000 + 16 * chr + dev->sc;

    if (blk) {
        /* Blinking, draw all background */
        val[0] = val[1] = val[2] = val[3] = 0x000;
    } else if (dev->sc == ull) {
        /* Underscore, draw all foreground */
        val[0] = val[1] = val[2] = val[3] = 0x1ff;
    } else {
        val[0] = fnt[0x00000] << 1;
        val[1] = fnt[0x10000] << 1;
        val[2] = fnt[0x20000] << 1;
        val[3] = fnt[0x30000] << 1;

        if (elg) {
            val[0] |= (val[0] >> 1) & 1;
            val[1] |= (val[1] >> 1) & 1;
            val[2] |= (val[2] >> 1) & 1;
            val[3] |= (val[3] >> 1) & 1;
        }
    }
    for (int i = 0; i < cw; i++) {
        /* Generate pixel colour */
        cfg   = 0;
        pmask = 1;
        for (uint8_t plane = 0; plane < 4; plane++, pmask = pmask << 1) {
            if (val[plane] & 0x100)
                cfg |= (ifg & pmask);
            else
                cfg |= (ibg & pmask);
        }
        /* cfg = colour of foreground pixels */
        if (altattr && (attr & 0x77) == 0)
            cfg = ibg; /* 'blank' attribute */
        if (palette) {
            fg = dev->rgb[dev->palette[cfg]];
        } else {
            fg = dev->rgb[defpal[cfg]];
        }

        buffer32->line[dev->displine][x * cw + i] = fg;
        val[0]                                    = val[0] << 1;
        val[1]                                    = val[1] << 1;
        val[2]                                    = val[2] << 1;
        val[3]                                    = val[3] << 1;
    }
}

static void
draw_char_ram48(incolor_t *dev, int x, uint8_t chr, uint8_t attr)
{
    int                  elg;
    int                  blk;
    int                  ul;
    int                  ol;
    int                  bld;
    unsigned             ull;
    unsigned             oll;
    unsigned             ulc = 0;
    unsigned             olc = 0;
    unsigned             val[4];
    unsigned             ifg = 0;
    unsigned             ibg;
    unsigned             cfg;
    unsigned             pmask;
    const unsigned char *fnt;
    uint32_t             fg;
    int                  cw      = INCOLOR_CW;
    int                  blink   = dev->ctrl & INCOLOR_CTRL_BLINK;
    int                  altattr = dev->crtc[INCOLOR_CRTC_EXCEPT] & INCOLOR_EXCEPT_ALTATTR;
    int                  palette = dev->crtc[INCOLOR_CRTC_EXCEPT] & INCOLOR_EXCEPT_PALETTE;
    int                  font    = (attr & 0x0F);

    if (font >= 12)
        font &= 7;

    blk = 0;
    if (blink && altattr) {
        if (attr & 0x40) {
            blk = (dev->blink & 16);
        }
        attr &= 0x7f;
    }
    if (altattr) {
        /* MDA-compatible attributes */
        if (blink) {
            ibg = (attr & 0x80) ? 8 : 0;
            bld = 0;
            ol  = (attr & 0x20) ? 1 : 0;
            ul  = (attr & 0x10) ? 1 : 0;
        } else {
            bld = (attr & 0x80) ? 1 : 0;
            ibg = (attr & 0x40) ? 0x0F : 0;
            ol  = (attr & 0x20) ? 1 : 0;
            ul  = (attr & 0x10) ? 1 : 0;
        }
    } else {
        /* CGA-compatible attributes */
        ibg = 0;
        ifg = (attr >> 4) & 0x0F;
        ol  = 0;
        ul  = 0;
        bld = 0;
    }
    if (ul) {
        ull = dev->crtc[INCOLOR_CRTC_UNDER] & 0x0F;
        ulc = (dev->crtc[INCOLOR_CRTC_UNDER] >> 4) & 0x0F;
        if (ulc == 0)
            ulc = 7;
    } else {
        ull = 0xFFFF;
    }
    if (ol) {
        oll = dev->crtc[INCOLOR_CRTC_OVER] & 0x0F;
        olc = (dev->crtc[INCOLOR_CRTC_OVER] >> 4) & 0x0F;
        if (olc == 0)
            olc = 7;
    } else {
        oll = 0xFFFF;
    }

    if (dev->crtc[INCOLOR_CRTC_XMODE] & INCOLOR_XMODE_90COL) {
        elg = 0;
    } else {
        elg = ((chr >= 0xc0) && (chr <= 0xdf));
    }
    fnt = dev->vram + 0x4000 + 16 * chr + 4096 * font + dev->sc;

    if (blk) {
        /* Blinking, draw all background */
        val[0] = val[1] = val[2] = val[3] = 0x000;
    } else if (dev->sc == ull) {
        /* Underscore, draw all foreground */
        val[0] = val[1] = val[2] = val[3] = 0x1ff;
    } else {
        val[0] = fnt[0x00000] << 1;
        val[1] = fnt[0x10000] << 1;
        val[2] = fnt[0x20000] << 1;
        val[3] = fnt[0x30000] << 1;

        if (elg) {
            val[0] |= (val[0] >> 1) & 1;
            val[1] |= (val[1] >> 1) & 1;
            val[2] |= (val[2] >> 1) & 1;
            val[3] |= (val[3] >> 1) & 1;
        }
        if (bld) {
            val[0] |= (val[0] >> 1);
            val[1] |= (val[1] >> 1);
            val[2] |= (val[2] >> 1);
            val[3] |= (val[3] >> 1);
        }
    }
    for (int i = 0; i < cw; i++) {
        /* Generate pixel colour */
        cfg   = 0;
        pmask = 1;
        if (dev->sc == oll) {
            cfg = olc ^ ibg; /* Strikethrough */
        } else if (dev->sc == ull) {
            cfg = ulc ^ ibg; /* Underline */
        } else {
            for (uint8_t plane = 0; plane < 4; plane++, pmask = pmask << 1) {
                if (val[plane] & 0x100) {
                    if (altattr)
                        cfg |= ((~ibg) & pmask);
                    else
                        cfg |= ((~ifg) & pmask);
                } else if (altattr)
                    cfg |= (ibg & pmask);
            }
        }
        if (palette) {
            fg = dev->rgb[dev->palette[cfg]];
        } else {
            fg = dev->rgb[defpal[cfg]];
        }

        buffer32->line[dev->displine][x * cw + i] = fg;
        val[0]                                    = val[0] << 1;
        val[1]                                    = val[1] << 1;
        val[2]                                    = val[2] << 1;
        val[3]                                    = val[3] << 1;
    }
}

static void
text_line(incolor_t *dev, uint16_t ca)
{
    int      drawcursor;
    uint8_t  chr;
    uint8_t  attr;
    uint32_t col;

    for (uint8_t x = 0; x < dev->crtc[1]; x++) {
        if (dev->ctrl & 8) {
            chr  = dev->vram[(dev->ma << 1) & 0x3fff];
            attr = dev->vram[((dev->ma << 1) + 1) & 0x3fff];
        } else
            chr = attr = 0;

        drawcursor = ((dev->ma == ca) && dev->con && dev->cursoron);

        switch (dev->crtc[INCOLOR_CRTC_XMODE] & 5) {
            case 0:
            case 4: /* ROM font */
                draw_char_rom(dev, x, chr, attr);
                break;

            case 1: /* 4k RAMfont */
                draw_char_ram4(dev, x, chr, attr);
                break;

            case 5: /* 48k RAMfont */
                draw_char_ram48(dev, x, chr, attr);
                break;

            default:
                break;
        }
        ++dev->ma;

        if (drawcursor) {
            int     cw  = INCOLOR_CW;
            uint8_t ink = dev->crtc[INCOLOR_CRTC_EXCEPT] & INCOLOR_EXCEPT_CURSOR;
            if (ink == 0)
                ink = (attr & 0x08) | 7;

            /* In MDA-compatible mode, cursor brightness comes from
             * background */
            if (dev->crtc[INCOLOR_CRTC_EXCEPT] & INCOLOR_EXCEPT_ALTATTR) {
                ink = (attr & 0x08) | (ink & 7);
            }
            if (dev->crtc[INCOLOR_CRTC_EXCEPT] & INCOLOR_EXCEPT_PALETTE) {
                col = dev->rgb[dev->palette[ink]];
            } else {
                col = dev->rgb[defpal[ink]];
            }
            for (int c = 0; c < cw; c++) {
                buffer32->line[dev->displine][x * cw + c] = col;
            }
        }
    }
}

static void
graphics_line(incolor_t *dev)
{
    uint8_t  mask;
    uint16_t ca;
    int      plane;
    int      col;
    uint8_t  ink;
    uint16_t val[4];

    /* Graphics mode. */
    ca = (dev->sc & 3) * 0x2000;
    if ((dev->ctrl & INCOLOR_CTRL_PAGE1) && (dev->ctrl2 & INCOLOR_CTRL2_PAGE1))
        ca += 0x8000;

    for (uint8_t x = 0; x < dev->crtc[1]; x++) {
        mask = dev->crtc[INCOLOR_CRTC_MASK]; /* Planes to display */
        for (plane = 0; plane < 4; plane++, mask = mask >> 1) {
            if (dev->ctrl & 8) {
                if (mask & 1)
                    val[plane] = (dev->vram[((dev->ma << 1) & 0x1fff) + ca + 0x10000 * plane] << 8) | dev->vram[((dev->ma << 1) & 0x1fff) + ca + 0x10000 * plane + 1];
                else
                    val[plane] = 0;
            } else
                val[plane] = 0;
        }
        dev->ma++;

        for (uint8_t c = 0; c < 16; c++) {
            ink = 0;
            for (plane = 0; plane < 4; plane++) {
                ink = ink >> 1;
                if (val[plane] & 0x8000)
                    ink |= 8;
                val[plane] = val[plane] << 1;
            }
            /* Is palette in use? */
            if (dev->crtc[INCOLOR_CRTC_EXCEPT] & INCOLOR_EXCEPT_PALETTE)
                col = dev->palette[ink];
            else
                col = defpal[ink];

            buffer32->line[dev->displine][(x << 4) + c] = dev->rgb[col];
        }
    }
}

static void
incolor_poll(void *priv)
{
    incolor_t *dev = (incolor_t *) priv;
    uint16_t   ca  = (dev->crtc[15] | (dev->crtc[14] << 8)) & 0x3fff;
    int        x;
    int        oldvc;
    int        oldsc;
    int        cw      = INCOLOR_CW;

    if (!dev->linepos) {
        timer_advance_u64(&dev->timer, dev->dispofftime);
        dev->stat |= 1;
        dev->linepos = 1;
        oldsc        = dev->sc;
        if ((dev->crtc[8] & 3) == 3)
            dev->sc = (dev->sc << 1) & 7;

        if (dev->dispon) {
            if (dev->displine < dev->firstline) {
                dev->firstline = dev->displine;
                video_wait_for_buffer();
            }
            dev->lastline = dev->displine;
            if ((dev->ctrl & INCOLOR_CTRL_GRAPH) && (dev->ctrl2 & INCOLOR_CTRL2_GRAPH))
                graphics_line(dev);
            else
                text_line(dev, ca);
        }
        dev->sc = oldsc;
        if (dev->vc == dev->crtc[7] && !dev->sc)
            dev->stat |= 8;
        dev->displine++;
        if (dev->displine >= 500)
            dev->displine = 0;
    } else {
        timer_advance_u64(&dev->timer, dev->dispontime);
        if (dev->dispon)
            dev->stat &= ~1;
        dev->linepos = 0;
        if (dev->vsynctime) {
            dev->vsynctime--;
            if (!dev->vsynctime)
                dev->stat &= ~8;
        }

        if (dev->sc == (dev->crtc[11] & 31) || ((dev->crtc[8] & 3) == 3 && dev->sc == ((dev->crtc[11] & 31) >> 1))) {
            dev->con  = 0;
            dev->coff = 1;
        }

        if (dev->vadj) {
            dev->sc++;
            dev->sc &= 31;
            dev->ma = dev->maback;
            dev->vadj--;
            if (!dev->vadj) {
                dev->dispon = 1;
                dev->ma = dev->maback = (dev->crtc[13] | (dev->crtc[12] << 8)) & 0x3fff;
                dev->sc               = 0;
            }
        } else if (dev->sc == dev->crtc[9] || ((dev->crtc[8] & 3) == 3 && dev->sc == (dev->crtc[9] >> 1))) {
            dev->maback = dev->ma;
            dev->sc     = 0;
            oldvc       = dev->vc;
            dev->vc++;
            dev->vc &= 127;
            if (dev->vc == dev->crtc[6])
                dev->dispon = 0;
            if (oldvc == dev->crtc[4]) {
                dev->vc   = 0;
                dev->vadj = dev->crtc[5];
                if (!dev->vadj)
                    dev->dispon = 1;
                if (!dev->vadj)
                    dev->ma = dev->maback = (dev->crtc[13] | (dev->crtc[12] << 8)) & 0x3fff;
                if ((dev->crtc[10] & 0x60) == 0x20)
                    dev->cursoron = 0;
                else
                    dev->cursoron = dev->blink & 16;
            }

            if (dev->vc == dev->crtc[7]) {
                dev->dispon    = 0;
                dev->displine  = 0;
                dev->vsynctime = 16;
                if (dev->crtc[7]) {
                    if ((dev->ctrl & INCOLOR_CTRL_GRAPH) && (dev->ctrl2 & INCOLOR_CTRL2_GRAPH))
                        x = dev->crtc[1] << 4;
                    else
                        x = dev->crtc[1] * cw;
                    dev->lastline++;
                    if ((dev->ctrl & 8) && ((x != xsize) || ((dev->lastline - dev->firstline) != ysize) || video_force_resize_get())) {
                        xsize = x;
                        ysize = dev->lastline - dev->firstline;
                        if (xsize < 64)
                            xsize = 656;
                        if (ysize < 32)
                            ysize = 200;
                        set_screen_size(xsize, ysize);

                        if (video_force_resize_get())
                            video_force_resize_set(0);
                    }
                    video_blit_memtoscreen(0, dev->firstline, xsize, dev->lastline - dev->firstline);
                    frames++;
                    if ((dev->ctrl & INCOLOR_CTRL_GRAPH) && (dev->ctrl2 & INCOLOR_CTRL2_GRAPH)) {
                        video_res_x = dev->crtc[1] * 16;
                        video_res_y = dev->crtc[6] * 4;
                        video_bpp   = 1;
                    } else {
                        video_res_x = dev->crtc[1];
                        video_res_y = dev->crtc[6];
                        video_bpp   = 0;
                    }
                }
                dev->firstline = 1000;
                dev->lastline  = 0;
                dev->blink++;
            }
        } else {
            dev->sc++;
            dev->sc &= 31;
            dev->ma = dev->maback;
        }

        if (dev->sc == (dev->crtc[10] & 31) || ((dev->crtc[8] & 3) == 3 && dev->sc == ((dev->crtc[10] & 31) >> 1)))
            dev->con = 1;
    }
}

static void *
incolor_init(UNUSED(const device_t *info))
{
    incolor_t *dev;
    int        c;

    dev = (incolor_t *) malloc(sizeof(incolor_t));
    memset(dev, 0x00, sizeof(incolor_t));

    dev->vram = (uint8_t *) malloc(0x40000); /* 4 planes of 64k */

    timer_add(&dev->timer, incolor_poll, dev, 1);

    mem_mapping_add(&dev->mapping, 0xb0000, 0x08000,
                    incolor_read, NULL, NULL, incolor_write, NULL, NULL,
                    NULL, MEM_MAPPING_EXTERNAL, dev);

    io_sethandler(0x03b0, 16,
                  incolor_in, NULL, NULL, incolor_out, NULL, NULL, dev);

    for (c = 0; c < 64; c++) {
        dev->rgb[c] = makecol32(init_rgb[c][0], init_rgb[c][1], init_rgb[c][2]);
    }

    /* Initialise CRTC regs to safe values */
    dev->crtc[INCOLOR_CRTC_MASK]   = 0x0F; /* All planes displayed */
    dev->crtc[INCOLOR_CRTC_RWCTRL] = INCOLOR_RWCTRL_POLARITY;
    dev->crtc[INCOLOR_CRTC_RWCOL]  = 0x0F; /* White on black */
    dev->crtc[INCOLOR_CRTC_EXCEPT] = INCOLOR_EXCEPT_ALTATTR;
    for (c = 0; c < 16; c++)
        dev->palette[c] = defpal[c];
    dev->palette_idx = 0;

    video_inform(VIDEO_FLAG_TYPE_MDA, &timing_incolor);

    /* Force the LPT3 port to be enabled. */
    lpt3_setup(LPT_MDA_ADDR);

    return dev;
}

static void
incolor_close(void *priv)
{
    incolor_t *dev = (incolor_t *) priv;

    if (!dev)
        return;

    if (dev->vram)
        free(dev->vram);

    free(dev);
}

static void
speed_changed(void *priv)
{
    incolor_t *dev = (incolor_t *) priv;

    recalc_timings(dev);
}

const device_t incolor_device = {
    .name          = "Hercules InColor",
    .internal_name = "incolor",
    .flags         = DEVICE_ISA,
    .local         = 0,
    .init          = incolor_init,
    .close         = incolor_close,
    .reset         = NULL,
    .available     = NULL,
    .speed_changed = speed_changed,
    .force_redraw  = NULL,
    .config        = NULL
};
