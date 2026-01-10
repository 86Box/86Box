/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          Quadram Quadcolor I / I+II emulation
 *
 * Authors: Sarah Walker, <https://pcem-emulator.co.uk/>
 *          Miran Grca, <mgrca8@gmail.com>
 *          W. M. Martinez, <anikom15@outlook.com>
 *          Benedikt Freisen, <https://pcem-emulator.co.uk/>
 *          Jasmine Iwanek, <jriwanek@gmail.com>
 *
 *          Copyright 2008-2019 Sarah Walker.
 *          Copyright 2016-2019 Miran Grca.
 *          Copyright 2023      W. M. Martinez
 *          Copyright 2024      Benedikt Freisen.
 *          Copyright 2025      Jasmine Iwanek.
 */

/* This has been derived from CGA emulation */
/* omissions: simulated snow (Quadcolor has dual-ported RAM), single and dual 8x16 font configuration */
/* additions: ports 0x3dd and 0x3de, 2nd char set, 2nd VRAM bank, hi-res bg color, Quadcolor II memory and mode */
/* assumptions: MA line 12 XORed with Bank Select, hi-res bg is also border color, QC2 mode has simple address counter */

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
#include <86box/rom.h>
#include <86box/device.h>
#include <86box/video.h>
#include <86box/vid_cga.h>
#include <86box/vid_quadcolor.h>
#include <86box/vid_cga_comp.h>
#include <86box/plat_unused.h>

#define CGA_RGB       0
#define CGA_COMPOSITE 1

#define COMPOSITE_OLD 0
#define COMPOSITE_NEW 1

#define DOUBLE_NONE               0
#define DOUBLE_SIMPLE             1
#define DOUBLE_INTERPOLATE_SRGB   2
#define DOUBLE_INTERPOLATE_LINEAR 3

#define DEVICE_VRAM      0x8000
#define DEVICE_VRAM_MASK 0x7fff

typedef union {
    uint32_t color;
    struct {
        uint8_t b;
        uint8_t g;
        uint8_t r;
        uint8_t a;
    };
} color_t;

static uint8_t crtcmask[32] = {
    0xff, 0xff, 0xff, 0xff, 0x7f, 0x1f, 0x7f, 0x7f, 0xf3, 0x1f, 0x7f, 0x1f, 0x3f, 0xff, 0x3f, 0xff,
    0xff, 0xff, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00
};

static uint8_t interp_lut[2][256][256];

static video_timings_t timing_quadcolor = { .type = VIDEO_ISA, .write_b = 8, .write_w = 16, .write_l = 32, .read_b = 8, .read_w = 16, .read_l = 32 };

void quadcolor_recalctimings(quadcolor_t *quadcolor);

static void
quadcolor_update_latch(quadcolor_t *quadcolor)
{
    uint32_t lp_latch = quadcolor->displine * quadcolor->crtc[CGA_CRTC_HDISP];

    quadcolor->crtc[CGA_CRTC_LIGHT_PEN_ADDR_HIGH] = (lp_latch >> 8) & 0x3f;
    quadcolor->crtc[CGA_CRTC_LIGHT_PEN_ADDR_LOW] = lp_latch & 0xff;
}

void
quadcolor_out(uint16_t addr, uint8_t val, void *priv)
{
    quadcolor_t  *quadcolor = (quadcolor_t *) priv;
    uint8_t old;

    if ((addr >= 0x3d0) && (addr <= 0x3d7))
        addr = (addr & 0xff9) | 0x004;

    switch (addr) {
        case CGA_REGISTER_CRTC_INDEX:
            quadcolor->crtcreg = val & 31;
            return;
        case CGA_REGISTER_CRTC_DATA:
            old                     = quadcolor->crtc[quadcolor->crtcreg];
            quadcolor->crtc[quadcolor->crtcreg] = val & crtcmask[quadcolor->crtcreg];
            if (old != val) {
                // Recalc the timings if we are writing any invalid CRTC register or a valid CRTC register 
                // except the CURSOR and LIGHT PEN registers
                if ((quadcolor->crtcreg < 0xe) || (quadcolor->crtcreg > 0x11)) {
                    quadcolor->fullchange = changeframecount;
                    quadcolor_recalctimings(quadcolor);
                }
            }
            return;
        case CGA_REGISTER_MODE_CONTROL:
            old                = quadcolor->cgamode;
            quadcolor->cgamode = val;

            if (old ^ val) {
                if ((old ^ val) & 0x07)
                    update_cga16_color(val);

                quadcolor_recalctimings(quadcolor);
            }
            return;
        case CGA_REGISTER_COLOR_SELECT:
            old         = quadcolor->cgacol;
            quadcolor->cgacol = val;
            if (old ^ val)
                quadcolor_recalctimings(quadcolor);
            return;

        case CGA_REGISTER_CLEAR_LIGHT_PEN_LATCH:
            if (quadcolor->lp_strobe == 1)
                quadcolor->lp_strobe = 0;
            return;
        case CGA_REGISTER_SET_LIGHT_PEN_LATCH:
            if (quadcolor->lp_strobe == 0) {
                quadcolor->lp_strobe = 1;
                quadcolor_update_latch(quadcolor);
            }
            return;

        case 0x3dd:
            quadcolor->quadcolor_ctrl = val & 0x3f;
            /* helper variable that can be XORed onto the VRAM address to select the page to display */
            quadcolor->page_offset = (val & 0x10) << 8;
            /* in dual 8x8 font configuration, use fontbase 256 if "Character Set Select" bit is set */
            if (quadcolor->has_2nd_charset)
                quadcolor->fontbase = (val & 0x20) << 3;
            return;
        case 0x3de:
            /* NOTE: the polarity of this register is the opposite of what the manual says */
            if (quadcolor->has_quadcolor_2)
                /*
                   NOTE: PC Paintbrush writes FF and then gets stuck if it doesn't get enabled,
                         and then it expects to disable it with 0x00. The only way to square this
                         with the inverted polarity note above is if that was a value other than
                         0x00 with bit 4 clear.
                 */
                quadcolor->quadcolor_2_oe = ((!(val & 0x10)) || (val == 0xff)) && (val != 0x00);
            return;

        default:
            break;
    }
}

uint8_t
quadcolor_in(uint16_t addr, void *priv)
{
    quadcolor_t  *quadcolor = (quadcolor_t *) priv;
    uint8_t ret = 0xff;

    if ((addr >= 0x3d0) && (addr <= 0x3d7))
        addr = (addr & 0xff9) | 0x004;

    switch (addr) {
        case CGA_REGISTER_CRTC_INDEX:
            ret = quadcolor->crtcreg;
            break;
        case CGA_REGISTER_CRTC_DATA:
            ret = quadcolor->crtc[quadcolor->crtcreg];
            break;
        case CGA_REGISTER_STATUS:
            ret = quadcolor->cgastat;
            break;
        case CGA_REGISTER_CLEAR_LIGHT_PEN_LATCH:
            if (quadcolor->lp_strobe == 1)
                quadcolor->lp_strobe = 0;
            break;
        case CGA_REGISTER_SET_LIGHT_PEN_LATCH:
            if (quadcolor->lp_strobe == 0) {
                quadcolor->lp_strobe = 1;
                quadcolor_update_latch(quadcolor);
            }
            break;

        default:
            break;
    }

    return ret;
}

void
quadcolor_waitstates(UNUSED(void *priv))
{
    int ws_array[16] = { 3, 4, 5, 6, 7, 8, 4, 5, 6, 7, 8, 4, 5, 6, 7, 8 };
    int ws;

    ws = ws_array[cycles & 0xf];
    cycles -= ws;
}

void
quadcolor_write(uint32_t addr, uint8_t val, void *priv)
{
    quadcolor_t *quadcolor = (quadcolor_t *) priv;

    quadcolor->vram[addr & DEVICE_VRAM_MASK] = val;
    quadcolor_waitstates(quadcolor);
}

uint8_t
quadcolor_read(uint32_t addr, void *priv)
{
    quadcolor_t *quadcolor = (quadcolor_t *) priv;

    quadcolor_waitstates(quadcolor);
    return quadcolor->vram[addr & DEVICE_VRAM_MASK];
}

void
quadcolor_2_write(uint32_t addr, uint8_t val, void *priv)
{
        quadcolor_t *quadcolor = (quadcolor_t *) priv;
        quadcolor->vram_2[addr & 0xffff] = val;
}

uint8_t
quadcolor_2_read(uint32_t addr, void *priv)
{
        quadcolor_t *quadcolor = (quadcolor_t *) priv;
        return quadcolor->vram_2[addr & 0xffff];
}

void
quadcolor_recalctimings(quadcolor_t *quadcolor)
{
    double disptime;
    double _dispontime;
    double _dispofftime;

    if (quadcolor->cgamode & CGA_MODE_FLAG_HIGHRES) {
        disptime    = (double) (quadcolor->crtc[CGA_CRTC_HTOTAL] + 1);
        _dispontime = (double) quadcolor->crtc[CGA_CRTC_HDISP];
    } else {
        disptime    = (double) ((quadcolor->crtc[CGA_CRTC_HTOTAL] + 1) << 1);
        _dispontime = (double) (quadcolor->crtc[CGA_CRTC_HDISP] << 1);
    }
    _dispofftime     = disptime - _dispontime;
    _dispontime      = _dispontime * CGACONST;
    _dispofftime     = _dispofftime * CGACONST;
    quadcolor->dispontime  = (uint64_t) (_dispontime);
    quadcolor->dispofftime = (uint64_t) (_dispofftime);
}

static inline uint8_t
get_next_qc2_pixel(quadcolor_t *quadcolor)
{
    uint8_t mask = quadcolor->qc2mask;
    quadcolor->qc2mask = ~quadcolor->qc2mask;
    uint8_t pixel = (quadcolor->vram_2[quadcolor->qc2idx] & mask) >> (quadcolor->qc2mask & 4);

    quadcolor->qc2idx += quadcolor->qc2mask >> 7;

    return quadcolor->quadcolor_2_oe ? pixel : 0;
}

static void
quadcolor_render(quadcolor_t *quadcolor, int line)
{
    uint16_t cursoraddr  = (quadcolor->crtc[CGA_CRTC_CURSOR_ADDR_LOW] | (quadcolor->crtc[CGA_CRTC_CURSOR_ADDR_HIGH] << 8)) & DEVICE_VRAM_MASK;
    int      drawcursor;
    int      x;
    int      column;
    uint8_t  chr;
    uint8_t  attr;
    uint16_t dat;
    int      cols[4];
    int      col;

    int32_t  highres_graphics_flag = (CGA_MODE_FLAG_HIGHRES_GRAPHICS | CGA_MODE_FLAG_GRAPHICS);

    cols[0] = ((quadcolor->cgamode & highres_graphics_flag) == highres_graphics_flag) ? (quadcolor->quadcolor_ctrl & 15) :
                                                                                        (quadcolor->cgacol & 15);

    for (column = 0; column < 8; ++column) {
        buffer32->line[line][column] = cols[0];
        if (quadcolor->cgamode & CGA_MODE_FLAG_HIGHRES)
            buffer32->line[line][column + (quadcolor->crtc[CGA_CRTC_HDISP] << 3) + 8] = cols[0];
        else
            buffer32->line[line][column + (quadcolor->crtc[CGA_CRTC_HDISP] << 4) + 8] = cols[0];
    }
    if (quadcolor->cgamode & CGA_MODE_FLAG_HIGHRES) { /* 80-column text */
        for (x = 0; x < quadcolor->crtc[CGA_CRTC_HDISP]; x++) {
            if (quadcolor->cgamode & CGA_MODE_FLAG_VIDEO_ENABLE) {
                chr  = quadcolor->charbuffer[x << 1];
                attr = quadcolor->charbuffer[(x << 1) + 1];
            } else
                chr = attr = 0;
            drawcursor = ((quadcolor->memaddr == cursoraddr) && quadcolor->cursorvisible && quadcolor->cursoron);
            cols[1]    = (attr & 15) + 16;
            if (quadcolor->cgamode & CGA_MODE_FLAG_BLINK) {
                cols[0] = ((attr >> 4) & 7) + 16;
                if ((quadcolor->cgablink & 8) && (attr & 0x80) && !quadcolor->drawcursor)
                    cols[1] = cols[0];
            } else
                cols[0] = (attr >> 4) + 16;
            uint8_t charline = quadcolor->scanline & 7;
            if (drawcursor) {
                for (column = 0; column < 8; column++) {
                    dat = (cols[(fontdat[chr + quadcolor->fontbase][charline] & (1 << (column ^ 7))) ? 1 : 0] ^ 15);
                    buffer32->line[line][(x << 3) + column + 8] =
                        dat | get_next_qc2_pixel(quadcolor);
                }
            } else {
                for (column = 0; column < 8; column++) {
                    dat = cols[(fontdat[chr + quadcolor->fontbase][charline] & (1 << (column ^ 7))) ? 1 : 0];
                    buffer32->line[line][(x << 3) + column + 8] =
                        dat | get_next_qc2_pixel(quadcolor);
                }
            }
            quadcolor->memaddr++;
        }
    } else if (!(quadcolor->cgamode & CGA_MODE_FLAG_GRAPHICS)) {
        /* Not graphics (nor 80-column text) => 40-column text. */
        for (x = 0; x < quadcolor->crtc[CGA_CRTC_HDISP]; x++) {
            if (quadcolor->cgamode & CGA_MODE_FLAG_VIDEO_ENABLE) {
                chr  = quadcolor->vram[(quadcolor->page_offset ^ (quadcolor->memaddr << 1)) & DEVICE_VRAM_MASK];
                attr = quadcolor->vram[(quadcolor->page_offset ^ ((quadcolor->memaddr << 1) + 1)) & DEVICE_VRAM_MASK];
            } else
                chr = attr = 0;
            drawcursor = ((quadcolor->memaddr == cursoraddr) && quadcolor->cursorvisible && quadcolor->cursoron);
            cols[1]    = (attr & 15) + 16;
            if (quadcolor->cgamode & CGA_MODE_FLAG_BLINK) {
                cols[0] = ((attr >> 4) & 7) + 16;
                if ((quadcolor->cgablink & 8) && (attr & 0x80))
                    cols[1] = cols[0];
            } else
                cols[0] = (attr >> 4) + 16;
            quadcolor->memaddr++;
            uint8_t charline = quadcolor->scanline & 7;
            if (drawcursor) {
                for (column = 0; column < 8; column++) {
                    dat = (cols[(fontdat[chr + quadcolor->fontbase][charline] & (1 << (column ^ 7))) ? 1 : 0] ^ 15);
                    buffer32->line[line][(x << 4) + (column << 1) + 8] =
                        dat | get_next_qc2_pixel(quadcolor);
                    buffer32->line[line][(x << 4) + (column << 1) + 9] =
                        dat | get_next_qc2_pixel(quadcolor);
                }
            } else {
                for (column = 0; column < 8; column++) {
                    dat = cols[(fontdat[chr + quadcolor->fontbase][charline] & (1 << (column ^ 7))) ? 1 : 0];
                    buffer32->line[line][(x << 4) + (column << 1) + 8] =
                        dat | get_next_qc2_pixel(quadcolor);
                    buffer32->line[line][(x << 4) + (column << 1) + 9] =
                        dat | get_next_qc2_pixel(quadcolor);
                }
            }
        }
    } else if (!(quadcolor->cgamode & CGA_MODE_FLAG_HIGHRES_GRAPHICS)) {
        /* Not hi-res (but graphics) => 4-color mode. */
        cols[0] = (quadcolor->cgacol & 15) | 16;
        col     = (quadcolor->cgacol & 16) ? 24 : 16;
        if (quadcolor->cgamode & CGA_MODE_FLAG_BW) {
            cols[1] = col | 3; /* Cyan */
            cols[2] = col | 4; /* Red */
            cols[3] = col | 7; /* White */
        } else if (quadcolor->cgacol & 32) {
            cols[1] = col | 3; /* Cyan */
            cols[2] = col | 5; /* Magenta */
            cols[3] = col | 7; /* White */
        } else {
            cols[1] = col | 2; /* Green */
            cols[2] = col | 4; /* Red */
            cols[3] = col | 6; /* Yellow */
        }
        for (x = 0; x < quadcolor->crtc[CGA_CRTC_HDISP]; x++) {
            if (quadcolor->cgamode & CGA_MODE_FLAG_VIDEO_ENABLE)
                dat = (quadcolor->vram[quadcolor->page_offset ^ (((quadcolor->memaddr << 1) & 0x1fff) +
                      ((quadcolor->scanline & 1) * 0x2000))] << 8) |
                      quadcolor->vram[quadcolor->page_offset ^ (((quadcolor->memaddr << 1) & 0x1fff) +
                      ((quadcolor->scanline & 1) * 0x2000) + 1)];
            else
                dat = 0;
            quadcolor->memaddr++;
            for (column = 0; column < 8; column++) {
                buffer32->line[line][(x << 4) + (column << 1) + 8] = cols[dat >> 14] | get_next_qc2_pixel(quadcolor);
                buffer32->line[line][(x << 4) + (column << 1) + 9] = cols[dat >> 14] | get_next_qc2_pixel(quadcolor);
                dat <<= 2;
            }
        }
    } else {
        /* 2-color hi-res graphics mode. */
        /* Background color (Quadcolor-specific). */
        cols[0] = quadcolor->quadcolor_ctrl & 15;
        cols[1] = (quadcolor->cgacol & 15) + 16;
        for (x = 0; x < quadcolor->crtc[CGA_CRTC_HDISP]; x++) {
            if (quadcolor->cgamode & CGA_MODE_FLAG_VIDEO_ENABLE) /* video enabled */
                dat = (quadcolor->vram[quadcolor->page_offset ^ (((quadcolor->memaddr << 1) & 0x1fff) +
                      ((quadcolor->scanline & 1) * 0x2000))] << 8) |
                      quadcolor->vram[quadcolor->page_offset ^ (((quadcolor->memaddr << 1) & 0x1fff) +
                     ((quadcolor->scanline & 1) * 0x2000) + 1)];
            else
                /* TODO: Is Quadcolor bg color actually relevant, here? Probably. See QC2 manual p.46 1. */
                dat = quadcolor->quadcolor_ctrl & 15;
            quadcolor->memaddr++;
            for (column = 0; column < 16; column++) {
                buffer32->line[line][(x << 4) + column + 8] = cols[dat >> 15] | get_next_qc2_pixel(quadcolor);
                dat <<= 1;
            }
        }
    }
}

static void
quadcolor_render_blank(quadcolor_t *quadcolor, int line)
{
    int32_t  highres_graphics_flag = (CGA_MODE_FLAG_HIGHRES_GRAPHICS | CGA_MODE_FLAG_GRAPHICS);

    /* `+ 16` isn't in PCem's version */
    int col = ((quadcolor->cgamode & highres_graphics_flag) == highres_graphics_flag) ? (quadcolor->quadcolor_ctrl & 15) + 16 : (quadcolor->cgacol & 15) + 16;  /* TODO: Is Quadcolor bg color actually relevant, here? */

    if (quadcolor->cgamode & CGA_MODE_FLAG_HIGHRES)
        hline(buffer32, 0, line, (quadcolor->crtc[CGA_CRTC_HDISP] << 3) + 16, col);
    else
        hline(buffer32, 0, line, (quadcolor->crtc[CGA_CRTC_HDISP] << 4) + 16, col);
}

static void
quadcolor_render_process(quadcolor_t *quadcolor, int line)
{
    int      x;
    uint8_t  border;
    int32_t  highres_graphics_flag = (CGA_MODE_FLAG_HIGHRES_GRAPHICS | CGA_MODE_FLAG_GRAPHICS);

    if (quadcolor->cgamode & CGA_MODE_FLAG_HIGHRES)
        x = (quadcolor->crtc[CGA_CRTC_HDISP] << 3) + 16;
    else
        x = (quadcolor->crtc[CGA_CRTC_HDISP] << 4) + 16;

    if (quadcolor->composite) {
        border = ((quadcolor->cgamode & highres_graphics_flag) == highres_graphics_flag) ? 0 : (quadcolor->cgacol & 15);

        Composite_Process(quadcolor->cgamode, border, x >> 2, buffer32->line[line]);
    } else
        video_process_8(x, line);
}

static uint8_t
quadcolor_interpolate_srgb(uint8_t co1, uint8_t co2, double fraction)
{
    uint8_t ret = ((co2 - co1) * fraction + co1);

    return ret;
}

static uint8_t
quadcolor_interpolate_linear(uint8_t co1, uint8_t co2, double fraction)
{
    double c1, c2;
    double r1, r2;
    uint8_t ret;

    c1 = ((double) co1) / 255.0;
    c1 = pow((co1 >= 0) ? c1 : -c1, 2.19921875);
    if (co1 <= 0)
        c1 = -c1;
    c2 = ((double) co2) / 255.0;
    c2 = pow((co2 >= 0) ? c2 : -c2, 2.19921875);
    if (co2 <= 0)
        c2 = -c2;
    r1 = ((c2 - c1) * fraction + c1);
    r2 = pow((r1 >= 0.0) ? r1 : -r1, 1.0 / 2.19921875);
    if (r1 <= 0.0)
        r2 = -r2;
    ret = (uint8_t) round(r2 * 255.0);

    return ret;
}

static color_t
quadcolor_interpolate_lookup(quadcolor_t *quadcolor, color_t color1, color_t color2, UNUSED(double fraction))
{
    color_t ret;
    uint8_t dt = quadcolor->double_type - DOUBLE_INTERPOLATE_SRGB;

    ret.a = 0x00;
    ret.r = interp_lut[dt][color1.r][color2.r];
    ret.g = interp_lut[dt][color1.g][color2.g];
    ret.b = interp_lut[dt][color1.b][color2.b];

    return ret;
}

static void
quadcolor_interpolate(quadcolor_t *quadcolor, int x, int y, int w, int h)
{
    double quotient = 0.5;

    for (int i = y; i < (y + h); i++) {
        if (i & 1)  for (int j = x; j < (x + w); j++) {
            int prev = i - 1;
            int next = i + 1;
            color_t prev_color, next_color;
            color_t black;
            color_t interim_1, interim_2;
            color_t final;

            if (i < 0)
                continue;

            black.color = 0x00000000;

            if ((prev >= 0) && (prev < (y + h)))
                prev_color.color = buffer32->line[prev][j];
            else
                prev_color.color = 0x00000000;

            if ((next >= 0) && (next < (y + h)))
                next_color.color = buffer32->line[next][j];
            else
                next_color.color = 0x00000000;

            interim_1 = quadcolor_interpolate_lookup(quadcolor, prev_color, black, quotient);
            interim_2 = quadcolor_interpolate_lookup(quadcolor, black, next_color, quotient);
            final     = quadcolor_interpolate_lookup(quadcolor, interim_1, interim_2, quotient);

            buffer32->line[i][j] = final.color;
        }
    }
}

static void
quadcolor_blit_memtoscreen(quadcolor_t *quadcolor, int x, int y, int w, int h)
{
    if (quadcolor->double_type > DOUBLE_SIMPLE)
        quadcolor_interpolate(quadcolor, x, y, w, h);

    video_blit_memtoscreen(x, y, w, h);
}

void
quadcolor_poll(void *priv)
{
    quadcolor_t   *quadcolor = (quadcolor_t *) priv;
    int      x;
    int      scanline_old;
    int      oldvc;
    int      xs_temp;
    int      ys_temp;
    int      old_ma;

    if (!quadcolor->linepos) {
        timer_advance_u64(&quadcolor->timer, quadcolor->dispofftime);
        quadcolor->cgastat |= 1;
        quadcolor->linepos = 1;
        scanline_old        = quadcolor->scanline;
        if ((quadcolor->crtc[CGA_CRTC_INTERLACE] & 3) == 3)
            quadcolor->scanline = ((quadcolor->scanline << 1) + quadcolor->oddeven) & 7;
        if (quadcolor->cgadispon) {
            if (quadcolor->displine < quadcolor->firstline) {
                quadcolor->firstline = quadcolor->displine;
                video_wait_for_buffer();
            }
            quadcolor->lastline = quadcolor->displine;
            switch (quadcolor->double_type) {
                default:
                    quadcolor_render(quadcolor, quadcolor->displine << 1);
                    quadcolor_render_blank(quadcolor, (quadcolor->displine << 1) + 1);
                    break;
                case DOUBLE_NONE:
                    quadcolor_render(quadcolor, quadcolor->displine);
                    break;
                case DOUBLE_SIMPLE:
                    old_ma = quadcolor->memaddr;
                    quadcolor_render(quadcolor, quadcolor->displine << 1);
                    quadcolor->memaddr = old_ma;
                    quadcolor_render(quadcolor, (quadcolor->displine << 1) + 1);
                    break;
            }
        } else {
            switch (quadcolor->double_type) {
                default:
                    quadcolor_render_blank(quadcolor, quadcolor->displine << 1);
                    break;
                case DOUBLE_NONE:
                    quadcolor_render_blank(quadcolor, quadcolor->displine);
                    break;
                case DOUBLE_SIMPLE:
                    quadcolor_render_blank(quadcolor, quadcolor->displine << 1);
                    quadcolor_render_blank(quadcolor, (quadcolor->displine << 1) + 1);
                    break;
            }
        }

        switch (quadcolor->double_type) {
            default:
                quadcolor_render_process(quadcolor, quadcolor->displine << 1);
                quadcolor_render_process(quadcolor, (quadcolor->displine << 1) + 1);
                break;
            case DOUBLE_NONE:
                quadcolor_render_process(quadcolor, quadcolor->displine);
                break;
        }

        quadcolor->scanline = scanline_old;
        if (quadcolor->vc == quadcolor->crtc[CGA_CRTC_VSYNC] && !quadcolor->scanline)
            quadcolor->cgastat |= 8;
        quadcolor->displine++;
        if (quadcolor->displine >= 360)
            quadcolor->displine = 0;
    } else {
        timer_advance_u64(&quadcolor->timer, quadcolor->dispontime);
        quadcolor->linepos = 0;
        if (quadcolor->vsynctime) {
            quadcolor->vsynctime--;
            if (!quadcolor->vsynctime)
                quadcolor->cgastat &= ~8;
            quadcolor->qc2idx = 0;
            quadcolor->qc2mask = 0xf0;
        }
        if (quadcolor->scanline == (quadcolor->crtc[CGA_CRTC_CURSOR_END] & 31) || ((quadcolor->crtc[CGA_CRTC_INTERLACE] & 3) == 3 &&
            quadcolor->scanline == ((quadcolor->crtc[CGA_CRTC_CURSOR_END] & 31) >> 1))) {
            quadcolor->cursorvisible  = 0;
        }
        if ((quadcolor->crtc[CGA_CRTC_INTERLACE] & 3) == 3 && quadcolor->scanline == (quadcolor->crtc[CGA_CRTC_MAX_SCANLINE_ADDR] >> 1))
            quadcolor->memaddr_backup = quadcolor->memaddr;
        if (quadcolor->vadj) {
            quadcolor->scanline++;
            quadcolor->scanline &= 31;
            quadcolor->memaddr = quadcolor->memaddr_backup;
            quadcolor->vadj--;
            if (!quadcolor->vadj) {
                quadcolor->cgadispon = 1;
                quadcolor->memaddr = quadcolor->memaddr_backup = (quadcolor->crtc[CGA_CRTC_START_ADDR_LOW] | (quadcolor->crtc[CGA_CRTC_START_ADDR_HIGH] << 8)) & DEVICE_VRAM_MASK;
                quadcolor->scanline               = 0;
            }
        } else if (quadcolor->scanline == quadcolor->crtc[CGA_CRTC_MAX_SCANLINE_ADDR]) {
            quadcolor->memaddr_backup = quadcolor->memaddr;
            quadcolor->scanline     = 0;
            oldvc       = quadcolor->vc;
            quadcolor->vc++;
            quadcolor->vc &= 127;

            if (quadcolor->vc == quadcolor->crtc[CGA_CRTC_VDISP])
                quadcolor->cgadispon = 0;

            if (oldvc == quadcolor->crtc[CGA_CRTC_VTOTAL]) {
                quadcolor->vc   = 0;
                quadcolor->vadj = quadcolor->crtc[CGA_CRTC_VTOTAL_ADJUST];
                if (!quadcolor->vadj) {
                    quadcolor->cgadispon = 1;
                    quadcolor->memaddr = quadcolor->memaddr_backup = (quadcolor->crtc[CGA_CRTC_START_ADDR_LOW] | (quadcolor->crtc[CGA_CRTC_START_ADDR_HIGH] << 8)) & DEVICE_VRAM_MASK;
                }
                
                switch (quadcolor->crtc[CGA_CRTC_CURSOR_START] & 0x60) {
                    case 0x20:
                        quadcolor->cursoron = 0;
                        break;
                    case 0x60:
                        quadcolor->cursoron = quadcolor->cgablink & 0x10;
                        break;
                    default:
                        quadcolor->cursoron = quadcolor->cgablink & 0x08;
                        break;
                }
            }

            if (quadcolor->vc == quadcolor->crtc[CGA_CRTC_VSYNC]) {
                quadcolor->cgadispon = 0;
                quadcolor->displine  = 0;
                quadcolor->vsynctime = 16;
                if (quadcolor->crtc[CGA_CRTC_VSYNC]) {
                    if (quadcolor->cgamode & CGA_MODE_FLAG_HIGHRES)
                        x = (quadcolor->crtc[CGA_CRTC_HDISP] << 3) + 16;
                    else
                        x = (quadcolor->crtc[CGA_CRTC_HDISP] << 4) + 16;
                    quadcolor->lastline++;

                    xs_temp = x;
                    ys_temp = quadcolor->lastline - quadcolor->firstline;
                    if (quadcolor->double_type > DOUBLE_NONE)
                        ys_temp <<= 1;

                    if ((xs_temp > 0) && (ys_temp > 0)) {
                        if (xs_temp < 64)
                            xs_temp = 656;
                        if (ys_temp < 32)
                            ys_temp = 200;
                        if (!enable_overscan)
                            xs_temp -= 16;

                        if ((quadcolor->cgamode & CGA_MODE_FLAG_VIDEO_ENABLE) && ((xs_temp != xsize) ||
                            (ys_temp != ysize) || video_force_resize_get())) {
                            xsize = xs_temp;
                            ysize = ys_temp;
                            if (quadcolor->double_type > DOUBLE_NONE)
                                set_screen_size(xsize, ysize + (enable_overscan ? 16 : 0));
                            else
                                set_screen_size(xsize, ysize + (enable_overscan ? 8 : 0));

                            if (video_force_resize_get())
                                video_force_resize_set(0);
                        }

                        if (quadcolor->double_type > DOUBLE_NONE) {
                            if (enable_overscan)
                                quadcolor_blit_memtoscreen(quadcolor, 0, (quadcolor->firstline - 4) << 1,
                                                     xsize, ((quadcolor->lastline - quadcolor->firstline) << 1) + 16);
                            else
                                quadcolor_blit_memtoscreen(quadcolor, 8, quadcolor->firstline << 1,
                                                     xsize, (quadcolor->lastline - quadcolor->firstline) << 1);
                        } else {
                            if (enable_overscan)
                                video_blit_memtoscreen(0, quadcolor->firstline - 4,
                                                       xsize, (quadcolor->lastline - quadcolor->firstline) + 8);
                            else
                                video_blit_memtoscreen(8, quadcolor->firstline,
                                                       xsize, quadcolor->lastline - quadcolor->firstline);
                        }
                    }

                    frames++;

                    video_res_x = xsize;
                    video_res_y = ysize;
                    if (quadcolor->cgamode & CGA_MODE_FLAG_HIGHRES) {
                        video_res_x /= 8;
                        video_res_y /= quadcolor->crtc[CGA_CRTC_MAX_SCANLINE_ADDR] + 1;
                        video_bpp = 0;
                    } else if (!(quadcolor->cgamode & CGA_MODE_FLAG_GRAPHICS)) {
                        video_res_x /= 16;
                        video_res_y /= quadcolor->crtc[CGA_CRTC_MAX_SCANLINE_ADDR] + 1;
                        video_bpp = 0;
                    } else if (!(quadcolor->cgamode & CGA_MODE_FLAG_HIGHRES_GRAPHICS)) {
                        video_res_x /= 2;
                        video_bpp = 2;
                    } else
                        video_bpp = 1;
                }
                quadcolor->firstline = 1000;
                quadcolor->lastline  = 0;
                quadcolor->cgablink++;
                quadcolor->oddeven ^= 1;
            }
        } else {
            quadcolor->scanline++;
            quadcolor->scanline &= 31;
            quadcolor->memaddr = quadcolor->memaddr_backup;
        }
        if (quadcolor->cgadispon)
            quadcolor->cgastat &= ~1;
        if (quadcolor->scanline == (quadcolor->crtc[CGA_CRTC_CURSOR_START] & 31) || ((quadcolor->crtc[CGA_CRTC_INTERLACE] & 3) == 3 &&
            quadcolor->scanline == ((quadcolor->crtc[CGA_CRTC_CURSOR_START] & 31) >> 1)))
            quadcolor->cursorvisible = 1;
        if (quadcolor->cgadispon && (quadcolor->cgamode & CGA_MODE_FLAG_HIGHRES)) {
            for (x = 0; x < (quadcolor->crtc[CGA_CRTC_HDISP] << 1); x++)
                quadcolor->charbuffer[x] = quadcolor->vram[(quadcolor->page_offset ^ ((quadcolor->memaddr << 1) + x)) & DEVICE_VRAM_MASK];
        }
    }
}

void
quadcolor_init(quadcolor_t *quadcolor)
{
    timer_add(&quadcolor->timer, quadcolor_poll, quadcolor, 1);
    quadcolor->composite = 0;
}

void *
quadcolor_standalone_init(UNUSED(const device_t *info))
{
    int    display_type;
    quadcolor_t *quadcolor = calloc(1, sizeof(quadcolor_t));

    video_inform(VIDEO_FLAG_TYPE_CGA, &timing_quadcolor);

    display_type               = device_get_config_int("display_type");
    quadcolor->composite       = (display_type != CGA_RGB);
    quadcolor->revision        = device_get_config_int("composite_type");
    quadcolor->has_2nd_charset = device_get_config_int("has_2nd_charset");
    quadcolor->has_quadcolor_2 = device_get_config_int("has_quadcolor_2");

    quadcolor->vram   = malloc(DEVICE_VRAM);
    quadcolor->vram_2 = malloc(0x10000);

    cga_comp_init(quadcolor->revision);
    timer_add(&quadcolor->timer, quadcolor_poll, quadcolor, 1);
    mem_mapping_add(&quadcolor->mapping, 0xb8000, 0x08000, quadcolor_read, NULL, NULL, quadcolor_write, NULL, NULL, NULL /*quadcolor->vram*/, MEM_MAPPING_EXTERNAL, quadcolor);
    /* add mapping for vram_2 at 0xd0000, mirrored at 0xe0000 */
    if (quadcolor->has_quadcolor_2)
        mem_mapping_add(&quadcolor->mapping_2, 0xd0000, 0x20000, quadcolor_2_read, NULL, NULL, quadcolor_2_write, NULL, NULL, NULL, MEM_MAPPING_EXTERNAL,
                        quadcolor);

    io_sethandler(0x03d0, 0x0010, quadcolor_in, NULL, NULL, quadcolor_out, NULL, NULL, quadcolor);

    overscan_x = overscan_y = 16;

    quadcolor->rgb_type = device_get_config_int("rgb_type");
    cga_palette   = (quadcolor->rgb_type << 1);
    cgapal_rebuild();
    update_cga16_color(quadcolor->cgamode);

    quadcolor->double_type = device_get_config_int("double_type");

    for (uint16_t i = 0; i < 256; i++) {
        for (uint16_t j = 0; j < 256; j++) {
            interp_lut[0][i][j] = quadcolor_interpolate_srgb(i, j, 0.5);
            interp_lut[1][i][j] = quadcolor_interpolate_linear(i, j, 0.5);
        }
    }

    switch(device_get_config_int("font")) {
        case 0:
            video_load_font(FONT_IBM_MDA_437_PATH, FONT_FORMAT_MDA, LOAD_FONT_NO_OFFSET);
            break;
        case 1:
            video_load_font(FONT_IBM_MDA_437_NORDIC_PATH, FONT_FORMAT_MDA, LOAD_FONT_NO_OFFSET);
            break;
        case 4:
            video_load_font(FONT_TULIP_DGA_PATH, FONT_FORMAT_MDA, LOAD_FONT_NO_OFFSET);
            break;
    }

    monitors[monitor_index_global].mon_composite = !!quadcolor->composite;

    return quadcolor;
}

void
quadcolor_close(void *priv)
{
    quadcolor_t *quadcolor = (quadcolor_t *) priv;

    free(quadcolor->vram);
    free(quadcolor->vram_2);
    free(quadcolor);
}

void
quadcolor_speed_changed(void *priv)
{
    quadcolor_t *quadcolor = (quadcolor_t *) priv;

    quadcolor_recalctimings(quadcolor);
}

// clang-format off
const device_config_t quadcolor_config[] = {
    {
        .name           = "display_type",
        .description    = "Display type",
        .type           = CONFIG_SELECTION,
        .default_string = NULL,
        .default_int    = CGA_RGB,
        .file_filter    = NULL,
        .spinner        = { 0 },
        .selection      = {
            { .description = "RGB",       .value = CGA_RGB       },
            { .description = "Composite", .value = CGA_COMPOSITE },
            { .description = ""                                  }
        },
        .bios           = { { 0 } }
    },
    {
        .name           = "composite_type",
        .description    = "Composite type",
        .type           = CONFIG_SELECTION,
        .default_string = NULL,
        .default_int    = COMPOSITE_OLD,
        .file_filter    = NULL,
        .spinner        = { 0 },
        .selection      = {
            { .description = "Old", .value = COMPOSITE_OLD },
            { .description = "New", .value = COMPOSITE_NEW },
            { .description = ""                            }
        },
        .bios           = { { 0 } }
    },
    {
        .name           = "rgb_type",
        .description    = "RGB type",
        .type           = CONFIG_SELECTION,
        .default_string = NULL,
        .default_int    = 5,
        .file_filter    = NULL,
        .spinner        = { 0 },
        .selection      = {
            { .description = "Color (generic)",  .value = 0 },
            { .description = "Green Monochrome", .value = 1 },
            { .description = "Amber Monochrome", .value = 2 },
            { .description = "Gray Monochrome",  .value = 3 },
            { .description = "Color (no brown)", .value = 4 },
            { .description = "Color (IBM 5153)", .value = 5 },
            { .description = ""                             }
        },
        .bios           = { { 0 } }
    },
    {
        .name           = "double_type",
        .description    = "Line doubling type",
        .type           = CONFIG_SELECTION,
        .default_string = NULL,
        .default_int    = DOUBLE_NONE,
        .file_filter    = NULL,
        .spinner        = { 0 },
        .selection      = {
            { .description = "None",                 .value = DOUBLE_NONE               },
            { .description = "Simple doubling",      .value = DOUBLE_SIMPLE             },
            { .description = "sRGB interpolation",   .value = DOUBLE_INTERPOLATE_SRGB   },
            { .description = "Linear interpolation", .value = DOUBLE_INTERPOLATE_LINEAR },
            { .description = ""                                                         }
        },
        .bios           = { { 0 } }
    },
    {
        .name           = "font",
        .description    = "Font",
        .type           = CONFIG_SELECTION,
        .default_string = NULL,
        .default_int    = 0,
        .file_filter    = NULL,
        .spinner        = { 0 },
        .selection      = {
            { .description = "US (CP 437)",                 .value = 0 },
            { .description = "IBM Nordic (CP 437-Nordic)",  .value = 1 },
            { .description = "Tulip DGA",                   .value = 4 },
            { .description = ""                                        }
        },
        .bios           = { { 0 } }
    },
    {
        .name           = "has_2nd_charset",
        .description    = "Has secondary 8x8 character set",
        .type           = CONFIG_BINARY,
        .default_string = NULL,
        .default_int    = 0,
        .file_filter    = NULL,
        .spinner        = { 0 },
        .selection      = { { 0 } },
        .bios           = { { 0 } }
    },
    {
        .name           = "has_quadcolor_2",
        .description    = "Has Quadcolor II daughter board",
        .type           = CONFIG_BINARY,
        .default_string = NULL,
        .default_int    = 1,
        .file_filter    = NULL,
        .spinner        = { 0 },
        .selection      = { { 0 } },
        .bios           = { { 0 } }
    },
    {
        .name           = "contrast",
        .description    = "Alternate monochrome contrast",
        .type           = CONFIG_BINARY,
        .default_string = NULL,
        .default_int    = 0,
        .file_filter    = NULL,
        .spinner        = { 0 },
        .selection      = { { 0 } },
        .bios           = { { 0 } }
    },
    { .name = "", .description = "", .type = CONFIG_END }
};
// clang-format on

const device_t quadcolor_device = {
    .name          = "Quadram Quadcolor I / I+II",
    .internal_name = "quadcolor",
    .flags         = DEVICE_ISA,
    .local         = 0,
    .init          = quadcolor_standalone_init,
    .close         = quadcolor_close,
    .reset         = NULL,
    .available     = NULL,
    .speed_changed = quadcolor_speed_changed,
    .force_redraw  = NULL,
    .config        = quadcolor_config
};
