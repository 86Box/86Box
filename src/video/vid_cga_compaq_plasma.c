/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          Emulation of the plasma displays on early Compaq Portables and laptops.
 *
 * Authors: Sarah Walker, <https://pcem-emulator.co.uk/>
 *          Miran Grca, <mgrca8@gmail.com>
 *
 *          Copyright 2008-2020 Sarah Walker.
 *          Copyright 2016-2020 Miran Grca.
 *          Copyright 2025 starfrost
 */
#include <stdarg.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <stdlib.h>
#include <wchar.h>
#include <math.h>
#define HAVE_STDARG_H
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
#include <86box/vid_cga_comp.h>
#include <86box/plat_unused.h>

static video_timings_t timing_compaq_plasma = { .type = VIDEO_ISA, .write_b = 8, .write_w = 16, .write_l = 32, .read_b = 8, .read_w = 16, .read_l = 32 };

#define CGA_RGB       0
#define CGA_COMPOSITE 1

/*Very rough estimate*/
#define VID_CLOCK (double) (651 * 416 * 60)

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

typedef struct compaq_plasma_t {
    cga_t         cga;
    mem_mapping_t font_ram_mapping;
    uint8_t      *font_ram;
    uint8_t       port_13c6;
    uint8_t       port_23c6;
    uint8_t       port_27c6;
    uint8_t       internal_monitor;
} compaq_plasma_t;


static void compaq_plasma_recalcattrs(compaq_plasma_t *self);

static void
compaq_plasma_recalctimings(compaq_plasma_t *self)
{
    double _dispontime;
    double _dispofftime;
    double disptime;

    if (!self->internal_monitor && !(self->port_23c6 & 0x01)) {
        cga_recalctimings(&self->cga);
        return;
    }

    disptime = 651;
    _dispontime = 640;
    _dispofftime = disptime - _dispontime;
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

    if (self->port_23c6 & 0x08)
        self->font_ram[addr & 0x1fff] = val;
    else
        self->cga.vram[addr & 0x7fff] = val;

    compaq_plasma_waitstates(&self->cga);
}
static uint8_t
compaq_plasma_read(uint32_t addr, void *priv)
{
    compaq_plasma_t *self = (compaq_plasma_t *) priv;
    uint8_t          ret;

    compaq_plasma_waitstates(&self->cga);

    if (self->port_23c6 & 0x08)
        ret = (self->font_ram[addr & 0x1fff]);
    else
        ret = (self->cga.vram[addr & 0x7fff]);

    return ret;
}

static void
compaq_plasma_out(uint16_t addr, uint8_t val, void *priv)
{
    compaq_plasma_t *self = (compaq_plasma_t *) priv;

    switch (addr) {
        /* Emulated CRTC, register select */
        case 0x3d0:
        case 0x3d2:
        case 0x3d4:
        case 0x3d6:
            cga_out(addr, val, &self->cga);
            break;

        /* Emulated CRTC, value */
        case 0x3d1:
        case 0x3d3:
        case 0x3d5:
        case 0x3d7:
            cga_out(addr, val, &self->cga);
            compaq_plasma_recalctimings(self);
            break;
        case 0x3d8:
        case 0x3d9:
            cga_out(addr, val, &self->cga);
            break;

        case 0x13c6:
            self->port_13c6 = val;
            compaq_plasma_display_set((self->port_13c6 & 0x08) ? 1 : 0);
            /*
               For bits 2-0, John gives 0 = CGA, 1 = EGA, 3 = MDA;
               Another source (Ralf Brown?) gives 4 = CGA, 5 = EGA, 7 = MDA;
               This leads me to believe bit 2 is not relevant to the mode.
             */
            if ((val & 0x07) == 0x03)
                mem_mapping_set_addr(&self->cga.mapping, 0xb0000, 0x08000);
            else
                mem_mapping_set_addr(&self->cga.mapping, 0xb8000, 0x08000);
            break;

        case 0x23c6:
            self->port_23c6 = val;
            break;

        case 0x27c6:
            self->port_27c6 = val;
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
        case 0x3db:
        case 0x3dc:
            ret = cga_in(addr, &self->cga);
            break;

        case 0x3d1:
        case 0x3d3:
        case 0x3d5:
        case 0x3d7:
            ret = cga_in(addr, &self->cga);
            break;

        case 0x3d8:
            ret = self->cga.cgamode;
            break;

        case 0x13c6:
            ret = self->port_13c6;
            break;

        case 0x17c6:
            ret = 0xe6;
            break;

        case 0x1bc6:
            ret = 0x40;
            break;

        case 0x23c6:
            ret = self->port_23c6;
            break;

        case 0x27c6:
            ret = self->port_27c6 & 0x3f;
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
    uint8_t  scanline;
    uint16_t memaddr  = (self->cga.crtc[CGA_CRTC_START_ADDR_LOW] | (self->cga.crtc[CGA_CRTC_START_ADDR_HIGH] << 8)) & 0x7fff;
    uint16_t cursoraddr  = (self->cga.crtc[CGA_CRTC_CURSOR_ADDR_LOW] | (self->cga.crtc[CGA_CRTC_CURSOR_ADDR_HIGH] << 8)) & 0x7fff;
    uint16_t addr;
    int      drawcursor;
    int      cursorline;
    int      blink     = 0;
    int      underline = 0;
    int      cursorvisible = 0;
    int      cursorinvisible = 0;
    int      c;
    int      x;
    uint32_t ink = 0;
    uint32_t fg = (self->cga.cgacol & 0x0f) ? amber : black;
    uint32_t bg = black;
    uint32_t black_half = black;
    uint32_t amber_half = amber;
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
    if (!self->internal_monitor && !(self->port_23c6 & 0x01)) {
        /* standard cga mode */
        cga_poll(&self->cga);
        return;
    } else {
        /* mode 40h or text mode */
        if (!self->cga.linepos) {
            timer_advance_u64(&self->cga.timer, self->cga.dispofftime);
            self->cga.cgastat |= 1;
            self->cga.linepos = 1;
            if (self->cga.cgadispon) {
                if (self->cga.displine == 0)
                    video_wait_for_buffer();

                /* 80-col */
                if (self->cga.cgamode & 0x01) {
                    scanline = self->cga.displine & 0x0f;
                    addr = ((memaddr & ~1) + (self->cga.displine >> 4) * 80) << 1;
                    memaddr += (self->cga.displine >> 4) * 80;

                    if ((self->cga.crtc[CGA_CRTC_CURSOR_START] & 0x60) == 0x20)
                        cursorline = 0;
                    else {
                        if ((self->cga.crtc[CGA_CRTC_CURSOR_START] & 0x0f) > 0x07)
                            cursorvisible = (self->cga.crtc[CGA_CRTC_CURSOR_START] & 0x0f) + 1;
                        else
                            cursorvisible = ((self->cga.crtc[CGA_CRTC_CURSOR_START] & 0x0f) << 1);

                        if ((self->cga.crtc[CGA_CRTC_CURSOR_END] & 0x0f) > 0x07)
                            cursorinvisible = (self->cga.crtc[CGA_CRTC_CURSOR_END] & 0x0f) + 2;
                        else
                            cursorinvisible = ((self->cga.crtc[CGA_CRTC_CURSOR_END] & 0x0f) << 1);

                        cursorline = (cursorvisible <= scanline) && (cursorinvisible >= scanline);
                    }

                    /* for each text column */
                    for (x = 0; x < 80; x++) {
                        /* video output enabled */
                        if (self->cga.cgamode & 0x08) {
                            /* character */
                            chr = self->cga.vram[(addr + (x << 1)) & 0x7fff];
                            /* text attributes */
                            attr = self->cga.vram[(addr + ((x << 1) + 1)) & 0x7fff];
                        } else {
                            chr = 0x00;
                            attr = 0x00;
                        }

                        uint8_t hi_bit = attr & 0x08;
                        /* check if cursor has to be drawn */
                        drawcursor = ((memaddr == cursoraddr) && cursorline && (self->cga.cgamode & 0x08) && (self->cga.cgablink & 0x10));
                        /* check if character underline mode should be set */
                        underline = ((attr & 0x07) == 0x01);
                        underline = underline || (((self->port_23c6 >> 5) == 1) && hi_bit);
                        if (underline) {
                            /* set forecolor to white */
                            attr = attr | 0x7;
                        }
                        blink = 0;
                        /* set foreground */
                        cols[1] = blinkcols[attr][1];
                        /* blink active */
                        if (self->cga.cgamode & CGA_MODE_FLAG_BLINK) {
                            cols[0] = blinkcols[attr][0];
                            /* attribute 7 active and not cursor */
                            if ((self->cga.cgablink & 0x08) && (attr & 0x80) && !drawcursor) {
                                /* set blinking */
                                cols[1] = cols[0];
                                blink   = 1;
                            }
                        } else {
                            /* Set intensity bit */
                            cols[1] = normcols[attr][1];
                            cols[0] = normcols[attr][0];
                        }

                        /* character address */
                        uint16_t chr_addr = ((chr * 16) + scanline) & 0x0fff;
                        if (((self->port_23c6 >> 5) == 3) && hi_bit)
                            chr_addr |= 0x1000;

                        /* character underline active and 7th row of pixels in character height being drawn */
                        if (underline) {
                            /* for each pixel in character width */
                            for (c = 0; c < 8; c++)
                                buffer32->line[self->cga.displine][(x << 3) + c] = mdaattr[attr][blink][1];
                        } else if (drawcursor) {
                            for (c = 0; c < 8; c++)
                                buffer32->line[self->cga.displine][(x << 3) + c] = cols[(self->font_ram[chr_addr] & (1 << (c ^ 7))) ? 1 : 0] ^ (amber ^ black);
                        } else {
                            for (c = 0; c < 8; c++)
                                buffer32->line[self->cga.displine][(x << 3) + c] = cols[(self->font_ram[chr_addr] & (1 << (c ^ 7))) ? 1 : 0];
                        }

                        if (hi_bit) {
                            if ((self->port_23c6 >> 5) == 4) {
                                uint8_t b = (cols[1] & 0xff) >> 1;
                                uint8_t g = ((cols[1] >> 8) & 0xff) >> 1;
                                uint8_t r = ((cols[1] >> 16) & 0xff) >> 1;
                                cols[1] = b | (g << 8) | (r << 16);
                                b = (cols[0] & 0xff) >> 1;
                                g = ((cols[0] >> 8) & 0xff) >> 1;
                                r = ((cols[0] >> 16) & 0xff) >> 1;
                                cols[0] = b | (g << 8) | (r << 16);
                                if (drawcursor) {
                                    black_half = black;
                                    amber_half = amber;
                                    uint8_t bB = (black & 0xff) >> 1;
                                    uint8_t gB = ((black >> 8) & 0xff) >> 1;
                                    uint8_t rB = ((black >> 16) & 0xff) >> 1;
                                    black_half = bB | (gB << 8) | (rB << 16);
                                    uint8_t bA = (amber & 0xff) >> 1;
                                    uint8_t gA = ((amber >> 8) & 0xff) >> 1;
                                    uint8_t rA = ((amber >> 16) & 0xff) >> 1;
                                    amber_half = bA | (gA << 8) | (rA << 16);
                                    for (c = 0; c < 8; c++)
                                        buffer32->line[self->cga.displine][(x << 3) + c] = cols[(self->font_ram[chr_addr] & (1 << (c ^ 7))) ? 1 : 0] ^ (amber_half ^ black_half);
                                } else {
                                    for (c = 0; c < 8; c++)
                                        buffer32->line[self->cga.displine][(x << 3) + c] = cols[(self->font_ram[chr_addr] & (1 << (c ^ 7))) ? 1 : 0];
                                }
                            } else if ((self->port_23c6 >> 5) == 2) {
                                if (drawcursor) {
                                    for (c = 0; c < 8; c++)
                                        buffer32->line[self->cga.displine][(x << 3) + c] = cols[(self->font_ram[chr_addr] & (1 << (c ^ 7))) ? 0 : 1] ^ (amber ^ black);
                                } else {
                                    for (c = 0; c < 8; c++)
                                        buffer32->line[self->cga.displine][(x << 3) + c] = cols[(self->font_ram[chr_addr] & (1 << (c ^ 7))) ? 0 : 1];
                                }
                            }
                        }
                        memaddr++;
                    }
                }
                /* 40-col */
                else if (!(self->cga.cgamode & 0x02)) {
                    scanline = self->cga.displine & 0x0f;
                    addr = ((memaddr & ~1) + (self->cga.displine >> 4) * 40) << 1;
                    memaddr += (self->cga.displine >> 4) * 40;

                    if ((self->cga.crtc[CGA_CRTC_CURSOR_START] & 0x60) == 0x20)
                        cursorline = 0;
                    else {
                        if ((self->cga.crtc[CGA_CRTC_CURSOR_START] & 0x0f) > 0x07)
                            cursorvisible = (self->cga.crtc[CGA_CRTC_CURSOR_START] & 0x0f) + 1;
                        else
                            cursorvisible = ((self->cga.crtc[CGA_CRTC_CURSOR_START] & 0x0f) << 1);

                        if ((self->cga.crtc[CGA_CRTC_CURSOR_END] & 0x0f) > 0x07)
                            cursorinvisible = (self->cga.crtc[CGA_CRTC_CURSOR_END] & 0x0f) + 2;
                        else
                            cursorinvisible = ((self->cga.crtc[CGA_CRTC_CURSOR_END] & 0x0f) << 1);

                        cursorline = (cursorvisible <= scanline) && (cursorinvisible >= scanline);
                    }

                    for (x = 0; x < 40; x++) {
                        /* video output enabled */
                        if (self->cga.cgamode & 0x08) {
                            /* character */
                            chr = self->cga.vram[(addr + (x << 1)) & 0x7fff];
                            /* text attributes */
                            attr = self->cga.vram[(addr + ((x << 1) + 1)) & 0x7fff];
                        } else {
                            chr = 0x00;
                            attr = 0x00;
                        }

                        uint8_t hi_bit = attr & 0x08;
                        /* check if cursor has to be drawn */
                        drawcursor = ((memaddr == cursoraddr) && cursorline && (self->cga.cgamode & 0x08) && (self->cga.cgablink & 0x10));
                        /* check if character underline mode should be set */
                        underline = ((attr & 0x07) == 0x01);
                        underline = underline || (((self->port_23c6 >> 5) == 1) && hi_bit);
                        if (underline) {
                            /* set forecolor to white */
                            attr = attr | 0x7;
                        }
                        blink = 0;
                        /* set foreground */
                        cols[1] = blinkcols[attr][1];
                        /* blink active */
                        if (self->cga.cgamode & CGA_MODE_FLAG_BLINK) {
                            cols[0] = blinkcols[attr][0];
                            /* attribute 7 active and not cursor */
                            if ((self->cga.cgablink & 0x08) && (attr & 0x80) && !drawcursor) {
                                /* set blinking */
                                cols[1] = cols[0];
                                blink   = 1;
                            }
                        } else {
                            /* Set intensity bit */
                            cols[1] = normcols[attr][1];
                            cols[0] = normcols[attr][0];
                        }

                        /* character address */
                        uint16_t chr_addr = ((chr * 16) + scanline) & 0x0fff;
                        if (((self->port_23c6 >> 5) == 3) && hi_bit)
                            chr_addr |= 0x1000;

                        /* character underline active and 7th row of pixels in character height being drawn */
                        if (underline && (scanline == 7)) {
                            /* for each pixel in character width */
                            for (c = 0; c < 8; c++)
                                buffer32->line[self->cga.displine][(x << 4) + (c << 1)] = buffer32->line[self->cga.displine][(x << 4) + (c << 1) + 1] = mdaattr[attr][blink][1];
                        } else if (drawcursor) {
                            for (c = 0; c < 8; c++)
                                buffer32->line[self->cga.displine][(x << 4) + (c << 1)] = buffer32->line[self->cga.displine][(x << 4) + (c << 1) + 1] = cols[(self->font_ram[chr_addr] & (1 << (c ^ 7))) ? 1 : 0] ^ (amber ^ black);
                        } else {
                            for (c = 0; c < 8; c++)
                                buffer32->line[self->cga.displine][(x << 4) + (c << 1)] = buffer32->line[self->cga.displine][(x << 4) + (c << 1) + 1] = cols[(self->font_ram[chr_addr] & (1 << (c ^ 7))) ? 1 : 0];
                        }

                        if (hi_bit) {
                            if ((self->port_23c6 >> 5) == 4) {
                                uint8_t b = (cols[1] & 0xff) >> 1;
                                uint8_t g = ((cols[1] >> 8) & 0xff) >> 1;
                                uint8_t r = ((cols[1] >> 16) & 0xff) >> 1;
                                cols[1] = b | (g << 8) | (r << 16);
                                b = (cols[0] & 0xff) >> 1;
                                g = ((cols[0] >> 8) & 0xff) >> 1;
                                r = ((cols[0] >> 16) & 0xff) >> 1;
                                cols[0] = b | (g << 8) | (r << 16);
                                if (drawcursor) {
                                    black_half = black;
                                    amber_half = amber;
                                    uint8_t bB = (black & 0xff) >> 1;
                                    uint8_t gB = ((black >> 8) & 0xff) >> 1;
                                    uint8_t rB = ((black >> 16) & 0xff) >> 1;
                                    black_half = bB | (gB << 8) | (rB << 16);
                                    uint8_t bA = (amber & 0xff) >> 1;
                                    uint8_t gA = ((amber >> 8) & 0xff) >> 1;
                                    uint8_t rA = ((amber >> 16) & 0xff) >> 1;
                                    amber_half = bA | (gA << 8) | (rA << 16);
                                    for (c = 0; c < 8; c++)
                                        buffer32->line[self->cga.displine][(x << 4) + (c << 1)] = buffer32->line[self->cga.displine][(x << 4) + (c << 1) + 1] = cols[(self->font_ram[chr_addr] & (1 << (c ^ 7))) ? 1 : 0] ^ (amber_half ^ black_half);
                                } else {
                                    for (c = 0; c < 8; c++)
                                        buffer32->line[self->cga.displine][(x << 4) + (c << 1)] = buffer32->line[self->cga.displine][(x << 4) + (c << 1) + 1] = cols[(self->font_ram[chr_addr] & (1 << (c ^ 7))) ? 1 : 0];
                                }
                            } else if ((self->port_23c6 >> 5) == 2) {
                                if (drawcursor) {
                                    for (c = 0; c < 8; c++)
                                        buffer32->line[self->cga.displine][(x << 4) + (c << 1)] = buffer32->line[self->cga.displine][(x << 4) + (c << 1) + 1] = cols[(self->font_ram[chr_addr] & (1 << (c ^ 7))) ? 0 : 1] ^ (amber ^ black);
                                } else {
                                    for (c = 0; c < 8; c++)
                                        buffer32->line[self->cga.displine][(x << 4) + (c << 1)] = buffer32->line[self->cga.displine][(x << 4) + (c << 1) + 1] = cols[(self->font_ram[chr_addr] & (1 << (c ^ 7))) ? 0 : 1];
                                }
                            }
                        }
                        memaddr++;
                    }
                } else {
                    if (self->cga.cgamode & CGA_MODE_FLAG_HIGHRES_GRAPHICS) {
                        /* 640x400 mode */
                        if (self->port_23c6 & 0x01) /* 640*400 */ {
                            addr = ((self->cga.displine) & 1) * 0x2000 + ((self->cga.displine >> 1) & 1) * 0x4000 + (self->cga.displine >> 2) * 80 + ((memaddr & ~1) << 1);
                        } else {
                            addr = ((self->cga.displine >> 1) & 1) * 0x2000 + (self->cga.displine >> 2) * 80 + ((memaddr & ~1) << 1);
                        }
                        for (uint8_t x = 0; x < 80; x++) {
                            dat = self->cga.vram[addr & 0x7fff];
                            addr++;

                            for (uint8_t c = 0; c < 8; c++) {
                                ink = (dat & 0x80) ? fg : bg;
                                if (!(self->cga.cgamode & 0x08))
                                    ink = black;
                                buffer32->line[self->cga.displine][(x << 3) + c] = ink;
                                dat <<= 1;
                            }
                        }
                    } else {
                        addr = ((self->cga.displine >> 1) & 1) * 0x2000 + (self->cga.displine >> 2) * 80 + ((memaddr & ~1) << 1);
                        for (uint8_t x = 0; x < 80; x++) {
                            dat = self->cga.vram[addr & 0x7fff];
                            addr++;

                            for (uint8_t c = 0; c < 4; c++) {
                                pattern = (dat & 0xC0) >> 6;
                                if (!(self->cga.cgamode & 0x08))
                                    pattern = 0;

                                switch (pattern & 3) {
                                    case 0:
                                        ink0 = ink1 = black;
                                        break;
                                    case 1:
                                        if (self->cga.displine & 0x01) {
                                            ink0 = black;
                                            ink1 = black;
                                        } else {
                                            ink0 = amber;
                                            ink1 = black;
                                        }
                                        break;
                                    case 2:
                                        if (self->cga.displine & 0x01) {
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
                                buffer32->line[self->cga.displine][(x << 3) + (c << 1)]     = ink0;
                                buffer32->line[self->cga.displine][(x << 3) + (c << 1) + 1] = ink1;
                                dat <<= 2;
                            }
                        }
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
            timer_advance_u64(&self->cga.timer, self->cga.dispontime);
            if (self->cga.cgadispon)
                self->cga.cgastat &= ~1;

            self->cga.linepos = 0;

            if (self->cga.displine == 400) {
                xsize = 640;
                ysize = 400;

                if ((self->cga.cgamode & 0x08) || video_force_resize_get()) {
                    set_screen_size(xsize, ysize);

                    if (video_force_resize_get())
                        video_force_resize_set(0);
                }
                /* Plasma specific */
                video_blit_memtoscreen(0, 0, xsize, ysize);
                frames++;

                /* Fixed 640x400 resolution */
                video_res_x = 640;
                video_res_y = 400;

                if (self->cga.cgamode & 0x02) {
                    if (self->cga.cgamode & CGA_MODE_FLAG_HIGHRES_GRAPHICS)
                        video_bpp = 1;
                    else
                        video_bpp = 2;
                } else
                    video_bpp = 0;

                self->cga.cgablink++;
            }
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
    black = makecol(0x64, 0x19, 0x00);

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
        blinkcols[n][0] = normcols[n][0] = black;
        blinkcols[n][1] = normcols[n][1] = amber;
    }
    /* Set up the 01-0E range, controlled by bits 0 and 1 of the
     * passed value. When blinking is enabled this also affects 81-8E. */
    for (n = 0x01; n <= 0x0E; n++) {
        if (n == 7)
            continue;
        blinkcols[n][0] = normcols[n][0] = black;
        blinkcols[n][1] = normcols[n][1] = amber;
        blinkcols[n + 128][0]            = black;
        blinkcols[n + 128][1]            = amber;
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

    cga_init(&self->cga);
    video_inform(VIDEO_FLAG_TYPE_CGA, &timing_compaq_plasma);

    self->cga.composite = 0;
    self->cga.revision  = 0;

    self->cga.vram             = malloc(0x8000);
    self->internal_monitor = 1;
    self->font_ram             = malloc(0x2000);

    cga_comp_init(self->cga.revision);
    timer_set_callback(&self->cga.timer, compaq_plasma_poll);
    timer_set_p(&self->cga.timer, self);

    mem_mapping_add(&self->cga.mapping, 0xb8000, 0x08000,
                    compaq_plasma_read, NULL, NULL,
                    compaq_plasma_write, NULL, NULL,
                    NULL, MEM_MAPPING_EXTERNAL, self);
    for (int i = 1; i <= 2; i++) {
        io_sethandler(0x03c6 + (i << 12), 0x0001, compaq_plasma_in, NULL, NULL, compaq_plasma_out, NULL, NULL, self);
        io_sethandler(0x07c6 + (i << 12), 0x0001, compaq_plasma_in, NULL, NULL, compaq_plasma_out, NULL, NULL, self);
        io_sethandler(0x0bc6 + (i << 12), 0x0001, compaq_plasma_in, NULL, NULL, compaq_plasma_out, NULL, NULL, self);
    }
    io_sethandler(0x03d0, 0x0010, compaq_plasma_in, NULL, NULL, compaq_plasma_out, NULL, NULL, self);

    overscan_x = overscan_y = 16;
    compaq_plasma_recalcattrs(self);

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
    free(self->font_ram);
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
