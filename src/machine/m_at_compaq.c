/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          Emulation of various Compaq PC's.
 *
 *
 *
 * Authors: Sarah Walker, <https://pcem-emulator.co.uk/>
 *          Miran Grca, <mgrca8@gmail.com>
 *          TheCollector1995, <mariogplayer@gmail.com>
 *
 *          Copyright 2008-2018 Sarah Walker.
 *          Copyright 2016-2018 Miran Grca.
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
#include <86box/rom.h>
#include <86box/device.h>
#include <86box/chipset.h>
#include <86box/keyboard.h>
#include <86box/fdd.h>
#include <86box/fdc.h>
#include <86box/fdc_ext.h>
#include <86box/hdc.h>
#include <86box/hdc_ide.h>
#include <86box/machine.h>
#include <86box/video.h>
#include <86box/vid_cga.h>
#include <86box/vid_cga_comp.h>
#include <86box/plat_unused.h>


static video_timings_t timing_compaq_plasma = { .type = VIDEO_ISA, .write_b = 8, .write_w = 16, .write_l = 32, .read_b = 8, .read_w = 16, .read_l = 32 };

enum {
    COMPAQ_PORTABLEII = 0,
    COMPAQ_PORTABLEIII,
    COMPAQ_PORTABLEIII386,
    COMPAQ_DESKPRO386,
    COMPAQ_DESKPRO386_05_1988
};

#define CGA_RGB       0
#define CGA_COMPOSITE 1

/*Very rough estimate*/
#define VID_CLOCK (double) (651 * 416 * 60)

static uint8_t cga_crtcmask[32] = {
    0xff, 0xff, 0xff, 0xff, 0x7f, 0x1f, 0x7f, 0x7f, 0xf3, 0x1f, 0x7f, 0x1f, 0x3f, 0xff, 0x3f, 0xff,
    0xff, 0xff, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00
};

/* Mapping of attributes to colours */
static uint32_t amber;
static uint32_t black;
static uint32_t blinkcols[256][2];
static uint32_t normcols[256][2];

/* Video options set by the motherboard; they will be picked up by the card
 * on the next poll.
 *
 * Bit  3:   Disable built-in video (for add-on card)
 * Bit  2:   Thin font
 * Bits 0,1: Font set (not currently implemented)
 */
static int8_t cpq_st_display_internal = -1;

static uint8_t mdaattr[256][2][2];

static void
compaq_plasma_display_set(uint8_t internal)
{
    cpq_st_display_internal = internal;
}

static uint8_t
compaq_plasma_display_get(void)
{
    return cpq_st_display_internal;
}

typedef struct compaq_plasma_t {
    cga_t         cga;
    uint8_t       port_23c6;
    uint8_t       internal_monitor;
    uint8_t       attrmap;
} compaq_plasma_t;

static int compaq_machine_type = 0;

/* Compaq Deskpro 386 remaps RAM from 0xA0000-0xFFFFF to 0xFA0000-0xFFFFFF */
static mem_mapping_t ram_mapping;

static void compaq_plasma_recalcattrs(compaq_plasma_t *self);

static void
compaq_plasma_recalctimings(compaq_plasma_t *self)
{
    double _dispontime;
    double _dispofftime;
    double disptime;

    if (!self->internal_monitor && !(self->port_23c6 & 1)) {
        cga_recalctimings(&self->cga);
        return;
    }

    disptime          = 651;
    _dispontime       = 640;
    _dispofftime      = disptime - _dispontime;
    self->cga.dispontime  = (uint64_t) (_dispontime * (cpuclock / VID_CLOCK) * (double) (1ULL << 32));
    self->cga.dispofftime = (uint64_t) (_dispofftime * (cpuclock / VID_CLOCK) * (double) (1ULL << 32));
}

static void
compaq_plasma_waitstates(UNUSED(void *priv))
{
    int ws_array[16] = { 3, 4, 5, 6, 7, 8, 4, 5, 6, 7, 8, 4, 5, 6, 7, 8 };
    int ws;

    ws = ws_array[cycles & 0xf];
    sub_cycles(ws);
}

static void
compaq_plasma_write(uint32_t addr, uint8_t val, void *priv)
{
    compaq_plasma_t *self = (compaq_plasma_t *) priv;

    self->cga.vram[addr & 0x7fff] = val;
    compaq_plasma_waitstates(&self->cga);
}

static uint8_t
compaq_plasma_read(uint32_t addr, void *priv)
{
    compaq_plasma_t *self = (compaq_plasma_t *) priv;
    uint8_t          ret;

    compaq_plasma_waitstates(&self->cga);
    ret = (self->cga.vram[addr & 0x7fff]);

    return ret;
}

static void
compaq_plasma_out(uint16_t addr, uint8_t val, void *priv)
{
    compaq_plasma_t *self = (compaq_plasma_t *) priv;
    uint8_t          old;

    switch (addr) {
        /* Emulated CRTC, register select */
        case 0x3d4:
            cga_out(addr, val, &self->cga);
            break;

        /* Emulated CRTC, value */
        case 0x3d5:
            old                               = self->cga.crtc[self->cga.crtcreg];
            self->cga.crtc[self->cga.crtcreg] = val & cga_crtcmask[self->cga.crtcreg];

            /* Register 0x12 controls the attribute mappings for the
             * plasma screen. */
            if (self->cga.crtcreg == 0x12) {
                self->attrmap = val;
                compaq_plasma_recalcattrs(self);
                break;
            }

            if (old != val) {
                if (self->cga.crtcreg < 0xe || self->cga.crtcreg > 0x10) {
                    self->cga.fullchange = changeframecount;
                    compaq_plasma_recalctimings(self);
                }
            }
            break;
        case 0x3d8:
        case 0x3d9:
            cga_out(addr, val, &self->cga);
            break;

        case 0x13c6:
            compaq_plasma_display_set((val & 8) ? 1 : 0);
            break;

        case 0x23c6:
            self->port_23c6 = val;
            if (val & 8) /* Disable internal CGA */
                mem_mapping_disable(&self->cga.mapping);
            else
                mem_mapping_enable(&self->cga.mapping);
            break;

        default:
            break;
    }
}

static uint8_t
compaq_plasma_in(uint16_t addr, void *priv)
{
    compaq_plasma_t *self = (compaq_plasma_t *) priv;
    uint8_t          ret  = 0xff;

    switch (addr) {
        case 0x3d4:
        case 0x3da:
            ret = cga_in(addr, &self->cga);
            break;

        case 0x3d5:
            if (self->cga.crtcreg == 0x12) {
                ret = self->attrmap & 0x0f;
                if (self->internal_monitor)
                    ret |= 0x30; /* Plasma / CRT */
            } else
                ret = cga_in(addr, &self->cga);
            break;

        case 0x13c6:
            ret = compaq_plasma_display_get() ? 8 : 0;
            ret |= 4;
            break;

        case 0x1bc6:
            ret = 0;
            if (compaq_plasma_display_get()) {
                if ((self->cga.cgamode & 0x12) == 0x12) {
                    if (self->port_23c6 & 8)
                        ret |= 0x40;
                    else
                        ret |= 0x20;
                }
            }
            break;

        case 0x23c6:
            ret = 0;
            break;

        default:
            break;
    }

    return ret;
}

static void
compaq_plasma_poll(void *priv)
{
    compaq_plasma_t *self = (compaq_plasma_t *) priv;
    uint8_t  chr;
    uint8_t  attr;
    uint8_t  sc;
    uint16_t ma  = (self->cga.crtc[13] | (self->cga.crtc[12] << 8)) & 0x7fff;
    uint16_t ca  = (self->cga.crtc[15] | (self->cga.crtc[14] << 8)) & 0x7fff;
    uint16_t addr;
    int      drawcursor;
    int      cursorline;
    int      blink     = 0;
    int      underline = 0;
    uint32_t ink = 0;
    uint32_t fg = (self->cga.cgacol & 0x0f) ? amber : black;
    uint32_t bg = black;
    uint32_t cols[2];
    uint8_t  dat;
    uint8_t  pattern;
    uint32_t ink0 = 0;
    uint32_t ink1 = 0;

    /* Switch between internal plasma and external CRT display. */
    if ((cpq_st_display_internal != -1) && (cpq_st_display_internal != self->internal_monitor)) {
        self->internal_monitor = cpq_st_display_internal;
        compaq_plasma_recalctimings(self);
    }

    /* graphic mode and not mode 40h */
    if (!self->internal_monitor && !(self->port_23c6 & 1)) {
        cga_poll(&self->cga);
        return;
    }

    /* mode 40h or text mode */
    if (!self->cga.linepos) {
        timer_advance_u64(&self->cga.timer, self->cga.dispofftime);
        self->cga.cgastat |= 1;
        self->cga.linepos = 1;
        if (self->cga.cgadispon) {
            if (self->cga.displine == 0) {
                video_wait_for_buffer();
            }
            if (self->cga.cgamode & 2) {
                if (self->cga.cgamode & 0x10) {
                    /* 640x400 mode */
                    if (self->port_23c6 & 1) /* 640*400 */ {
                        addr = ((self->cga.displine) & 1) * 0x2000 + ((self->cga.displine >> 1) & 1) * 0x4000 + (self->cga.displine >> 2) * 80 + ((ma & ~1) << 1);
                    } else {
                        addr = ((self->cga.displine >> 1) & 1) * 0x2000 + (self->cga.displine >> 2) * 80 + ((ma & ~1) << 1);
                    }
                    for (uint8_t x = 0; x < 80; x++) {
                        dat = self->cga.vram[addr & 0x7FFF];
                        addr++;

                        for (uint8_t c = 0; c < 8; c++) {
                            ink = (dat & 0x80) ? fg : bg;
                            if (!(self->cga.cgamode & 8))
                                ink = black;
                            (buffer32->line[self->cga.displine])[x * 8 + c] = ink;
                            dat <<= 1;
                        }
                    }
                } else {
                    addr = ((self->cga.displine >> 1) & 1) * 0x2000 + (self->cga.displine >> 2) * 80 + ((ma & ~1) << 1);
                    for (uint8_t x = 0; x < 80; x++) {
                        dat = self->cga.vram[addr & 0x7fff];
                        addr++;

                        for (uint8_t c = 0; c < 4; c++) {
                            pattern = (dat & 0xC0) >> 6;
                            if (!(self->cga.cgamode & 8))
                                pattern = 0;

                            switch (pattern & 3) {
                                case 0:
                                    ink0 = ink1 = black;
                                    break;
                                case 1:
                                    if (self->cga.displine & 1) {
                                        ink0 = black;
                                        ink1 = black;
                                    } else {
                                        ink0 = amber;
                                        ink1 = black;
                                    }
                                    break;
                                case 2:
                                    if (self->cga.displine & 1) {
                                        ink0 = black;
                                        ink1 = amber;
                                    } else {
                                        ink0 = amber;
                                        ink1 = black;
                                    }
                                    break;
                                case 3:
                                    ink0 = ink1 = amber;
                                    break;

                                default:
                                    break;
                            }
                            buffer32->line[self->cga.displine][x * 8 + 2 * c]     = ink0;
                            buffer32->line[self->cga.displine][x * 8 + 2 * c + 1] = ink1;
                            dat <<= 2;
                        }
                    }
                }
            } else if (self->cga.cgamode & 1) {
                /* 80-col */
                sc = self->cga.displine & 0x0f;
                addr = ((ma & ~1) + (self->cga.displine >> 4) * 80) * 2;
                ma += (self->cga.displine >> 4) * 80;

                if ((self->cga.crtc[0x0a] & 0x60) == 0x20)
                    cursorline = 0;
                else
                    cursorline = ((self->cga.crtc[0x0a] & 0x0f) * 2 <= sc) && ((self->cga.crtc[0x0b] & 0x0F) * 2 >= sc);

                /* for each text column */
                for (uint8_t x = 0; x < 80; x++) {
                    /* video output enabled */
                    chr        = self->cga.vram[(addr + 2 * x) & 0x7FFF];
                    attr       = self->cga.vram[(addr + 2 * x + 1) & 0x7FFF];
                    drawcursor = ((ma == ca) && cursorline && (self->cga.cgamode & 8) && (self->cga.cgablink & 16));

                    blink = ((self->cga.cgablink & 16) && (self->cga.cgamode & 0x20) && (attr & 0x80) && !drawcursor);
                    underline = ((self->port_23c6 & 0x40) && (attr & 0x1) && !(attr & 0x6));
                    /* blink active */
                    if (self->cga.cgamode & 0x20) {
                        cols[1] = blinkcols[attr][1];
                        cols[0] = blinkcols[attr][0];
                        /* attribute 7 active and not cursor */
                        if (blink) {
                            /* set blinking */
                            cols[1] = cols[0];
                        }
                    } else {
                        /* Set intensity bit */
                        cols[1] = normcols[attr][1];
                        cols[0] = normcols[attr][0];
                    }
                    /* character underline active and 7th row of pixels in character height being drawn */
                    if (underline && (sc == 7)) {
                        /* for each pixel in character width */
                        for (uint8_t c = 0; c < 8; c++)
                            buffer32->line[self->cga.displine][(x << 3) + c] = mdaattr[attr][blink][1];
                    } else if (drawcursor) {
                        for (uint8_t c = 0; c < 8; c++)
                            buffer32->line[self->cga.displine][(x << 3) + c] = cols[(fontdatm2[chr + self->cga.fontbase][sc] & (1 << (c ^ 7))) ? 1 : 0] ^ (amber ^ black);
                    } else {
                        for (uint8_t c = 0; c < 8; c++)
                            buffer32->line[self->cga.displine][(x << 3) + c] = cols[(fontdatm2[chr + self->cga.fontbase][sc] & (1 << (c ^ 7))) ? 1 : 0];
                    }

                    ++ma;
                }
            } else { /* 40-col */
                sc = self->cga.displine & 0x0f;
                addr = ((ma & ~1) + (self->cga.displine >> 4) * 40) * 2;
                ma += (self->cga.displine >> 4) * 40;

                if ((self->cga.crtc[0x0a] & 0x60) == 0x20)
                    cursorline = 0;
                else
                    cursorline = ((self->cga.crtc[0x0a] & 0x0f) * 2 <= sc) && ((self->cga.crtc[0x0b] & 0x0F) * 2 >= sc);

                for (uint8_t x = 0; x < 40; x++) {
                    chr        = self->cga.vram[(addr + 2 * x) & 0x7FFF];
                    attr       = self->cga.vram[(addr + 2 * x + 1) & 0x7FFF];
                    drawcursor = ((ma == ca) && cursorline && (self->cga.cgamode & 8) && (self->cga.cgablink & 16));

                    blink = ((self->cga.cgablink & 16) && (self->cga.cgamode & 0x20) && (attr & 0x80) && !drawcursor);
                    underline = ((self->port_23c6 & 0x40) && (attr & 0x1) && !(attr & 0x6));
                    /* blink active */
                    if (self->cga.cgamode & 0x20) {
                        cols[1] = blinkcols[attr][1];
                        cols[0] = blinkcols[attr][0];
                        /* attribute 7 active and not cursor */
                        if (blink) {
                            /* set blinking */
                            cols[1] = cols[0];
                        }
                    } else {
                        /* Set intensity bit */
                        cols[1] = normcols[attr][1];
                        cols[0] = normcols[attr][0];
                    }
                    /* character underline active and 7th row of pixels in character height being drawn */
                    if (underline && (sc == 7)) {
                        /* for each pixel in character width */
                        for (uint8_t c = 0; c < 8; c++)
                            buffer32->line[self->cga.displine][(x << 4) + (c * 2)] = buffer32->line[self->cga.displine][(x << 4) + (c * 2) + 1] = mdaattr[attr][blink][1];
                    } else if (drawcursor) {
                        for (uint8_t c = 0; c < 8; c++)
                            buffer32->line[self->cga.displine][(x << 4) + c * 2] = buffer32->line[self->cga.displine][(x << 4) + c * 2 + 1] = cols[(fontdatm2[chr][sc] & (1 << (c ^ 7))) ? 1 : 0] ^ (amber ^ black);
                    } else {
                        for (uint8_t c = 0; c < 8; c++)
                            buffer32->line[self->cga.displine][(x << 4) + c * 2] = buffer32->line[self->cga.displine][(x << 4) + c * 2 + 1] = cols[(fontdatm2[chr][sc] & (1 << (c ^ 7))) ? 1 : 0];
                    }
                    ++ma;
                }
            }
        }
        self->cga.displine++;
        /* Hardcode a fixed refresh rate and VSYNC timing */
        if (self->cga.displine == 400) { /* Start of VSYNC */
            self->cga.cgastat |= 8;
            self->cga.cgadispon = 0;
        }
        if (self->cga.displine == 416) { /* End of VSYNC */
            self->cga.displine = 0;
            self->cga.cgastat &= ~8;
            self->cga.cgadispon = 1;
        }
    } else {
        if (self->cga.cgadispon)
            self->cga.cgastat &= ~1;

        timer_advance_u64(&self->cga.timer, self->cga.dispontime);
        self->cga.linepos = 0;

        if (self->cga.displine == 400) {
            /* Hardcode 640x400 window size */
            if ((640 != xsize) || (400 != ysize) || video_force_resize_get()) {
                xsize = 640;
                ysize = 400;
                if (xsize < 64)
                    xsize = 656;
                if (ysize < 32)
                    ysize = 200;
                set_screen_size(xsize, ysize);

                if (video_force_resize_get())
                    video_force_resize_set(0);
            }
            video_blit_memtoscreen(0, 0, xsize, ysize);
            frames++;

            /* Fixed 640x400 resolution */
            video_res_x = 640;
            video_res_y = 400;

            if (self->cga.cgamode & 0x02) {
                if (self->cga.cgamode & 0x10)
                    video_bpp = 1;
                else
                    video_bpp = 2;
            } else
                video_bpp = 0;

            self->cga.cgablink++;
        }
    }
}

static void
compaq_plasma_mdaattr_rebuild(void)
{
    for (uint16_t c = 0; c < 256; c++) {
        mdaattr[c][0][0] = mdaattr[c][1][0] = mdaattr[c][1][1] = 16;
        if (c & 8)
            mdaattr[c][0][1] = 15 + 16;
        else
            mdaattr[c][0][1] = 7 + 16;
    }

    mdaattr[0x70][0][1] = 16;
    mdaattr[0x70][0][0] = mdaattr[0x70][1][0] = mdaattr[0x70][1][1] = 16 + 15;
    mdaattr[0xF0][0][1]                                             = 16;
    mdaattr[0xF0][0][0] = mdaattr[0xF0][1][0] = mdaattr[0xF0][1][1] = 16 + 15;
    mdaattr[0x78][0][1]                                             = 16 + 7;
    mdaattr[0x78][0][0] = mdaattr[0x78][1][0] = mdaattr[0x78][1][1] = 16 + 15;
    mdaattr[0xF8][0][1]                                             = 16 + 7;
    mdaattr[0xF8][0][0] = mdaattr[0xF8][1][0] = mdaattr[0xF8][1][1] = 16 + 15;
    mdaattr[0x00][0][1] = mdaattr[0x00][1][1] = 16;
    mdaattr[0x08][0][1] = mdaattr[0x08][1][1] = 16;
    mdaattr[0x80][0][1] = mdaattr[0x80][1][1] = 16;
    mdaattr[0x88][0][1] = mdaattr[0x88][1][1] = 16;
}

static void
compaq_plasma_recalcattrs(compaq_plasma_t *self)
{
    int n;

    /* val behaves as follows:
     *     Bit 0: Attributes 01-06, 08-0E are inverse video
     *     Bit 1: Attributes 01-06, 08-0E are bold
     *     Bit 2: Attributes 11-16, 18-1F, 21-26, 28-2F ... F1-F6, F8-FF
     *            are inverse video
     *     Bit 3: Attributes 11-16, 18-1F, 21-26, 28-2F ... F1-F6, F8-FF
     *            are bold */

    /* Set up colours */
    amber = makecol(0xff, 0x7d, 0x00);
    black = makecol(0x64, 0x0c, 0x00);

    /* Initialize the attribute mapping. Start by defaulting everything
     * to black on amber, and with bold set by bit 3 */
    for (n = 0; n < 256; n++) {
        blinkcols[n][0] = normcols[n][0] = amber;
        blinkcols[n][1] = normcols[n][1] = black;
    }

    /* Colours 0x11-0xFF are controlled by bits 2 and 3 of the
     * passed value. Exclude x0 and x8, which are always black on
     * amber. */
    for (n = 0x11; n <= 0xFF; n++) {
        if ((n & 7) == 0)
            continue;
        if (self->attrmap & 4) { /* Inverse */
            blinkcols[n][0] = normcols[n][0] = amber;
            blinkcols[n][1] = normcols[n][1] = black;
        } else { /* Normal */
            blinkcols[n][0] = normcols[n][0] = black;
            blinkcols[n][1] = normcols[n][1] = amber;
        }
    }
    /* Set up the 01-0E range, controlled by bits 0 and 1 of the
     * passed value. When blinking is enabled this also affects 81-8E. */
    for (n = 0x01; n <= 0x0E; n++) {
        if (n == 7)
            continue;
        if (self->attrmap & 1) {
            blinkcols[n][0] = normcols[n][0] = amber;
            blinkcols[n][1] = normcols[n][1] = black;
            blinkcols[n + 128][0]            = amber;
            blinkcols[n + 128][1]            = black;
        } else {
            blinkcols[n][0] = normcols[n][0] = black;
            blinkcols[n][1] = normcols[n][1] = amber;
            blinkcols[n + 128][0]            = black;
            blinkcols[n + 128][1]            = amber;
        }
    }
    /* Colours 07 and 0F are always amber on black. If blinking is
     * enabled so are 87 and 8F. */
    for (n = 0x07; n <= 0x0F; n += 8) {
        blinkcols[n][0] = normcols[n][0] = black;
        blinkcols[n][1] = normcols[n][1] = amber;
        blinkcols[n + 128][0]            = black;
        blinkcols[n + 128][1]            = amber;
    }
    /* When not blinking, colours 81-8F are always amber on black. */
    for (n = 0x81; n <= 0x8F; n++) {
        normcols[n][0] = black;
        normcols[n][1] = amber;
    }

    /* Finally do the ones which are solid black. These differ between
     * the normal and blinking mappings */
    for (n = 0; n <= 0xFF; n += 0x11)
        normcols[n][0] = normcols[n][1] = black;

    /* In the blinking range, 00 11 22 .. 77 and 80 91 A2 .. F7 are black */
    for (n = 0; n <= 0x77; n += 0x11) {
        blinkcols[n][0] = blinkcols[n][1] = black;
        blinkcols[n + 128][0] = blinkcols[n + 128][1] = black;
    }
}

static void *
compaq_plasma_init(UNUSED(const device_t *info))
{
    compaq_plasma_t *self = calloc(1, sizeof(compaq_plasma_t));

    video_inform(VIDEO_FLAG_TYPE_CGA, &timing_compaq_plasma);
    if (compaq_machine_type == COMPAQ_PORTABLEIII)
        loadfont_ex("roms/machines/portableiii/K Combined.bin", 11, 0x4bb2);
    else
        loadfont_ex("roms/machines/portableiii/P.2 Combined.bin", 11, 0x4b49);

    self->cga.composite = 0;
    self->cga.revision  = 0;

    self->cga.vram             = malloc(0x8000);
    self->internal_monitor = 1;

    cga_comp_init(self->cga.revision);
    timer_add(&self->cga.timer, compaq_plasma_poll, self, 1);
    mem_mapping_add(&self->cga.mapping, 0xb8000, 0x08000, compaq_plasma_read, NULL, NULL, compaq_plasma_write, NULL, NULL, NULL /*self->cga.vram*/, MEM_MAPPING_EXTERNAL, self);
    io_sethandler(0x03d0, 0x0010, compaq_plasma_in, NULL, NULL, compaq_plasma_out, NULL, NULL, self);
    io_sethandler(0x13c6, 0x0001, compaq_plasma_in, NULL, NULL, compaq_plasma_out, NULL, NULL, self);
    io_sethandler(0x1bc6, 0x0001, compaq_plasma_in, NULL, NULL, compaq_plasma_out, NULL, NULL, self);
    io_sethandler(0x23c6, 0x0001, compaq_plasma_in, NULL, NULL, compaq_plasma_out, NULL, NULL, self);

    /* Default attribute mapping is 4 */
    self->attrmap = 4;
    compaq_plasma_recalcattrs(self);

    self->cga.cgastat = 0xf4;
    overscan_x = overscan_y = 16;

    self->cga.rgb_type = device_get_config_int("rgb_type");
    cga_palette        = (self->cga.rgb_type << 1);
    cgapal_rebuild();
    compaq_plasma_mdaattr_rebuild();

    return self;
}

static void
compaq_plasma_close(void *priv)
{
    compaq_plasma_t *self = (compaq_plasma_t *) priv;

    free(self->cga.vram);
    free(self);
}

static void
compaq_plasma_speed_changed(void *priv)
{
    compaq_plasma_t *self = (compaq_plasma_t *) priv;

    compaq_plasma_recalctimings(self);
}

const device_config_t compaq_plasma_config[] = {
  // clang-format off
    {
        .name = "rgb_type",
        .description = "RGB type",
        .type = CONFIG_SELECTION,
        .default_string = "",
        .default_int = 0,
        .file_filter = "",
        .spinner = { 0 },
        .selection = {
            { .description = "Color",            .value = 0 },
            { .description = "Green Monochrome", .value = 1 },
            { .description = "Amber Monochrome", .value = 2 },
            { .description = "Gray Monochrome",  .value = 3 },
            { .description = ""                             }
        }
    },
    { .name = "", .description = "", .type = CONFIG_END }
  // clang-format on
};

const device_t compaq_plasma_device = {
    .name          = "Compaq Plasma",
    .internal_name = "compaq_plasma",
    .flags         = 0,
    .local         = 0,
    .init          = compaq_plasma_init,
    .close         = compaq_plasma_close,
    .reset         = NULL,
    .available     = NULL,
    .speed_changed = compaq_plasma_speed_changed,
    .force_redraw  = NULL,
    .config        = compaq_plasma_config
};

static uint8_t
read_ram(uint32_t addr, UNUSED(void *priv))
{
    addr = (addr & 0x7ffff) + 0x80000;
    addreadlookup(mem_logical_addr, addr);

    return (ram[addr]);
}

static uint16_t
read_ramw(uint32_t addr, UNUSED(void *priv))
{
    addr = (addr & 0x7ffff) + 0x80000;
    addreadlookup(mem_logical_addr, addr);

    return (*(uint16_t *) &ram[addr]);
}

static uint32_t
read_raml(uint32_t addr, UNUSED(void *priv))
{
    addr = (addr & 0x7ffff) + 0x80000;
    addreadlookup(mem_logical_addr, addr);

    return (*(uint32_t *) &ram[addr]);
}

static void
write_ram(uint32_t addr, uint8_t val, UNUSED(void *priv))
{
    addr = (addr & 0x7ffff) + 0x80000;
    addwritelookup(mem_logical_addr, addr);

    mem_write_ramb_page(addr, val, &pages[addr >> 12]);
}

static void
write_ramw(uint32_t addr, uint16_t val, UNUSED(void *priv))
{
    addr = (addr & 0x7ffff) + 0x80000;
    addwritelookup(mem_logical_addr, addr);

    mem_write_ramw_page(addr, val, &pages[addr >> 12]);
}

static void
write_raml(uint32_t addr, uint32_t val, UNUSED(void *priv))
{
    addr = (addr & 0x7ffff) + 0x80000;
    addwritelookup(mem_logical_addr, addr);

    mem_write_raml_page(addr, val, &pages[addr >> 12]);
}

static void
machine_at_compaq_init(const machine_t *model, int type)
{
    compaq_machine_type = type;

    if (fdc_current[0] == FDC_INTERNAL)
        device_add(&fdc_at_device);

    if (type < COMPAQ_PORTABLEIII386) {
        mem_remap_top(384);
        mem_mapping_add(&ram_mapping, 0xfa0000, 0x60000,
                        read_ram, read_ramw, read_raml,
                        write_ram, write_ramw, write_raml,
                        0xa0000 + ram, MEM_MAPPING_INTERNAL, NULL);
    }

    video_reset(gfxcard[0]);

    switch (type) {
        case COMPAQ_PORTABLEII:
            machine_at_init(model);
            break;

        case COMPAQ_PORTABLEIII:
            if (hdc_current[0] == HDC_INTERNAL)
                device_add(&ide_isa_device);
            if (gfxcard[0] == VID_INTERNAL)
                device_add(&compaq_plasma_device);
            machine_at_init(model);
            break;

        case COMPAQ_PORTABLEIII386:
            if (hdc_current[0] == HDC_INTERNAL)
                device_add(&ide_isa_device);
            if (gfxcard[0] == VID_INTERNAL)
                device_add(&compaq_plasma_device);
            device_add(&compaq_386_device);
            machine_at_common_init(model);
            device_add(&keyboard_at_compaq_device);
            break;

        case COMPAQ_DESKPRO386:
        case COMPAQ_DESKPRO386_05_1988:
            device_add(&compaq_386_device);
            machine_at_common_init(model);
            device_add(&keyboard_at_compaq_device);
            break;

        default:
            break;
    }
}

int
machine_at_portableii_init(const machine_t *model)
{
    int ret;

    ret = bios_load_interleavedr("roms/machines/portableii/109740-001.rom",
                                 "roms/machines/portableii/109739-001.rom",
                                 0x000f8000, 65536, 0);

    if (bios_only || !ret)
        return ret;

    machine_at_compaq_init(model, COMPAQ_PORTABLEII);

    return ret;
}

int
machine_at_portableiii_init(const machine_t *model)
{
    int ret;

    ret = bios_load_linearr("roms/machines/portableiii/K Combined.bin",
                                0x000f8000, 65536, 0);


    if (bios_only || !ret)
        return ret;

    machine_at_compaq_init(model, COMPAQ_PORTABLEIII);

    return ret;
}

int
machine_at_portableiii386_init(const machine_t *model)
{
    int ret;

    ret = bios_load_linearr("roms/machines/portableiii/P.2 Combined.bin",
                                0x000f0000, 131072, 0);

    if (bios_only || !ret)
        return ret;

    machine_at_compaq_init(model, COMPAQ_PORTABLEIII386);

    return ret;
}

int
machine_at_deskpro386_init(const machine_t *model)
{
    int ret;

    ret = bios_load_linearr("roms/machines/deskpro386/1986-09-04-HI.json.bin",
                            0x000f8000, 65536, 0);

    if (bios_only || !ret)
        return ret;

    machine_at_compaq_init(model, COMPAQ_DESKPRO386);

    return ret;
}

int
machine_at_deskpro386_05_1988_init(const machine_t *model)
{
    int ret;

    ret = bios_load_linearr("roms/machines/deskpro386/1988-05-10.json.bin",
                            0x000f8000, 65536, 0);

    if (bios_only || !ret)
        return ret;

    machine_at_compaq_init(model, COMPAQ_DESKPRO386_05_1988);

    return ret;
}
