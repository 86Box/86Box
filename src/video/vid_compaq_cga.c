/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          Emulation of the Compaq CGA graphics cards.
 *
 *
 *
 * Authors: John Elliott, <jce@seasip.info>
 *          Sarah Walker, <https://pcem-emulator.co.uk/>
 *          Miran Grca, <mgrca8@gmail.com>
 *
 *          Copyright 2016-2019 John Elliott.
 *          Copyright 2008-2019 Sarah Walker.
 *          Copyright 2016-2019 Miran Grca.
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

#define CGA_RGB       0
#define CGA_COMPOSITE 1

static uint32_t vflags;
static uint8_t  mdaattr[256][2][2];

typedef struct compaq_cga_t {
    cga_t cga;
} compaq_cga_t;

#ifdef ENABLE_COMPAQ_CGA_LOG
int compaq_cga_do_log = ENABLE_COMPAQ_CGA_LOG;

static void
compaq_cga_log(const char *fmt, ...)
{
    va_list ap;

    if (compaq_cga_do_log) {
        va_start(ap, fmt);
        pclog_ex(fmt, ap);
        va_end(ap);
    }
}
#else
#    define compaq_cga_log(fmt, ...)
#endif

void
compaq_cga_recalctimings(compaq_cga_t *self)
{
    double _dispontime;
    double _dispofftime;
    double disptime;
    disptime = self->cga.crtc[0] + 1;

    _dispontime  = self->cga.crtc[1];
    _dispofftime = disptime - _dispontime;
    _dispontime *= MDACONST;
    _dispofftime *= MDACONST;
    self->cga.dispontime  = (uint64_t) (_dispontime);
    self->cga.dispofftime = (uint64_t) (_dispofftime);
}

void
compaq_cga_poll(void *priv)
{
    compaq_cga_t *self = (compaq_cga_t *) priv;
    uint16_t      ca   = (self->cga.crtc[15] | (self->cga.crtc[14] << 8)) & 0x3fff;
    int           drawcursor;
    int           x;
    int           c;
    int           xs_temp;
    int           ys_temp;
    int           oldvc;
    uint8_t       chr;
    uint8_t       attr;
    uint8_t       border;
    uint8_t       cols[4];
    int           oldsc;
    int           underline = 0;
    int           blink     = 0;

    /* If in graphics mode or character height is not 13, behave as CGA */
    if ((self->cga.cgamode & 0x12) || (self->cga.crtc[9] != 13)) {
        overscan_x = overscan_y = 16;
        cga_poll(&self->cga);
        return;
    } else
        overscan_x = overscan_y = 0;

    /* We are in Compaq 350-line CGA territory */
    if (!self->cga.linepos) {
        timer_advance_u64(&self->cga.timer, self->cga.dispofftime);
        self->cga.cgastat |= 1;
        self->cga.linepos = 1;
        oldsc             = self->cga.sc;
        if ((self->cga.crtc[8] & 3) == 3)
            self->cga.sc = ((self->cga.sc << 1) + self->cga.oddeven) & 7;
        if (self->cga.cgadispon) {
            if (self->cga.displine < self->cga.firstline) {
                self->cga.firstline = self->cga.displine;
                video_wait_for_buffer();
                compaq_cga_log("Firstline %i\n", self->cga.firstline);
            }
            self->cga.lastline = self->cga.displine;

            cols[0] = (self->cga.cgacol & 15) + 16;

            for (c = 0; c < 8; c++) {
                buffer32->line[self->cga.displine][c] = cols[0];
                if (self->cga.cgamode & 1)
                    buffer32->line[self->cga.displine][c + (self->cga.crtc[1] << 3) + 8] = cols[0];
                else
                    buffer32->line[self->cga.displine][c + (self->cga.crtc[1] << 4) + 8] = cols[0];
            }

            if (self->cga.cgamode & 1) {
                for (x = 0; x < self->cga.crtc[1]; x++) {
                    chr        = self->cga.charbuffer[x << 1];
                    attr       = self->cga.charbuffer[(x << 1) + 1];
                    drawcursor = ((self->cga.ma == ca) && self->cga.con && self->cga.cursoron);

                    if (vflags) {
                        underline = 0;
                        blink     = ((self->cga.cgablink & 8) && (self->cga.cgamode & 0x20) && (attr & 0x80) && !drawcursor);
                    }

                    if (vflags && (self->cga.cgamode & 0x80)) {
                        cols[0] = mdaattr[attr][blink][0];
                        cols[1] = mdaattr[attr][blink][1];

                        if ((self->cga.sc == 12) && ((attr & 7) == 1))
                            underline = 1;
                    } else if (self->cga.cgamode & 0x20) {
                        cols[1] = (attr & 15) + 16;
                        cols[0] = ((attr >> 4) & 7) + 16;

                        if (vflags) {
                            if (blink)
                                cols[1] = cols[0];
                        } else {
                            if ((self->cga.cgablink & 8) && (attr & 0x80) && !self->cga.drawcursor)
                                cols[1] = cols[0];
                        }
                    } else {
                        cols[1] = (attr & 15) + 16;
                        cols[0] = (attr >> 4) + 16;
                    }

                    if (vflags && underline) {
                        for (c = 0; c < 8; c++)
                            buffer32->line[self->cga.displine][(x << 3) + c + 8] = mdaattr[attr][blink][1];
                    } else if (drawcursor) {
                        for (c = 0; c < 8; c++)
                            buffer32->line[self->cga.displine][(x << 3) + c + 8] = cols[(fontdatm[chr + self->cga.fontbase][self->cga.sc & 15] & (1 << (c ^ 7))) ? 1 : 0] ^ 15;
                    } else {
                        for (c = 0; c < 8; c++)
                            buffer32->line[self->cga.displine][(x << 3) + c + 8] = cols[(fontdatm[chr + self->cga.fontbase][self->cga.sc & 15] & (1 << (c ^ 7))) ? 1 : 0];
                    }
                    self->cga.ma++;
                }
            } else {
                for (x = 0; x < self->cga.crtc[1]; x++) {
                    chr        = self->cga.vram[(self->cga.ma << 1) & 0x3fff];
                    attr       = self->cga.vram[((self->cga.ma << 1) + 1) & 0x3fff];
                    drawcursor = ((self->cga.ma == ca) && self->cga.con && self->cga.cursoron);

                    if (vflags) {
                        underline = 0;
                        blink     = ((self->cga.cgablink & 8) && (self->cga.cgamode & 0x20) && (attr & 0x80) && !drawcursor);
                    }

                    if (vflags && (self->cga.cgamode & 0x80)) {
                        cols[0] = mdaattr[attr][blink][0];
                        cols[1] = mdaattr[attr][blink][1];
                        if (self->cga.sc == 12 && (attr & 7) == 1)
                            underline = 1;
                    } else if (self->cga.cgamode & 0x20) {
                        cols[1] = (attr & 15) + 16;
                        cols[0] = ((attr >> 4) & 7) + 16;

                        if (vflags) {
                            if (blink)
                                cols[1] = cols[0];
                        } else {
                            if ((self->cga.cgablink & 8) && (attr & 0x80) && !self->cga.drawcursor)
                                cols[1] = cols[0];
                        }
                    } else {
                        cols[1] = (attr & 15) + 16;
                        cols[0] = (attr >> 4) + 16;
                    }
                    self->cga.ma++;

                    if (vflags && underline) {
                        for (c = 0; c < 8; c++)
                            buffer32->line[self->cga.displine][(x << 4) + (c << 1) + 8] = buffer32->line[self->cga.displine][(x << 4) + (c << 1) + 9] = mdaattr[attr][blink][1];
                    } else if (drawcursor) {
                        for (c = 0; c < 8; c++)
                            buffer32->line[self->cga.displine][(x << 4) + (c << 1) + 8] = buffer32->line[self->cga.displine][(x << 4) + (c << 1) + 1 + 8] = cols[(fontdatm[chr + self->cga.fontbase][self->cga.sc & 15] & (1 << (c ^ 7))) ? 1 : 0] ^ 15;
                    } else {
                        for (c = 0; c < 8; c++)
                            buffer32->line[self->cga.displine][(x << 4) + (c << 1) + 8] = buffer32->line[self->cga.displine][(x << 4) + (c << 1) + 1 + 8] = cols[(fontdatm[chr + self->cga.fontbase][self->cga.sc & 15] & (1 << (c ^ 7))) ? 1 : 0];
                    }
                }
            }
        } else {
            cols[0] = (self->cga.cgacol & 15) + 16;

            if (self->cga.cgamode & 1)
                hline(buffer32, 0, self->cga.displine, (self->cga.crtc[1] << 3) + 16, cols[0]);
            else
                hline(buffer32, 0, self->cga.displine, (self->cga.crtc[1] << 4) + 16, cols[0]);
        }

        if (self->cga.cgamode & 1)
            x = (self->cga.crtc[1] << 3) + 16;
        else
            x = (self->cga.crtc[1] << 4) + 16;

        if (self->cga.composite) {
            if (self->cga.cgamode & 0x10)
                border = 0x00;
            else
                border = self->cga.cgacol & 0x0f;

            if (vflags)
                Composite_Process(self->cga.cgamode & 0x7f, border, x >> 2, buffer32->line[self->cga.displine]);
            else
                Composite_Process(self->cga.cgamode, border, x >> 2, buffer32->line[self->cga.displine]);
        } else
            video_process_8(x, self->cga.displine);

        self->cga.sc = oldsc;
        if (self->cga.vc == self->cga.crtc[7] && !self->cga.sc)
            self->cga.cgastat |= 8;
        self->cga.displine++;
        if (self->cga.displine >= 500)
            self->cga.displine = 0;
    } else {
        timer_advance_u64(&self->cga.timer, self->cga.dispontime);
        self->cga.linepos = 0;
        if (self->cga.vsynctime) {
            self->cga.vsynctime--;
            if (!self->cga.vsynctime)
                self->cga.cgastat &= ~8;
        }

        if (self->cga.sc == (self->cga.crtc[11] & 31) || ((self->cga.crtc[8] & 3) == 3 && self->cga.sc == ((self->cga.crtc[11] & 31) >> 1))) {
            self->cga.con  = 0;
            self->cga.coff = 1;
        }
        if ((self->cga.crtc[8] & 3) == 3 && self->cga.sc == (self->cga.crtc[9] >> 1))
            self->cga.maback = self->cga.ma;
        if (self->cga.vadj) {
            self->cga.sc++;
            self->cga.sc &= 31;
            self->cga.ma = self->cga.maback;
            self->cga.vadj--;
            if (!self->cga.vadj) {
                self->cga.cgadispon = 1;
                self->cga.ma = self->cga.maback = (self->cga.crtc[13] | (self->cga.crtc[12] << 8)) & 0x3fff;
                self->cga.sc                    = 0;
            }
        } else if (self->cga.sc == self->cga.crtc[9]) {
            self->cga.maback = self->cga.ma;
            self->cga.sc     = 0;
            oldvc            = self->cga.vc;
            self->cga.vc++;
            self->cga.vc &= 127;

            if (self->cga.vc == self->cga.crtc[6])
                self->cga.cgadispon = 0;

            if (oldvc == self->cga.crtc[4]) {
                self->cga.vc   = 0;
                self->cga.vadj = self->cga.crtc[5];

                if (!self->cga.vadj)
                    self->cga.cgadispon = 1;

                if (!self->cga.vadj)
                    self->cga.ma = self->cga.maback = (self->cga.crtc[13] | (self->cga.crtc[12] << 8)) & 0x3fff;

                if ((self->cga.crtc[10] & 0x60) == 0x20)
                    self->cga.cursoron = 0;
                else
                    self->cga.cursoron = self->cga.cgablink & 8;
            }

            if (self->cga.vc == self->cga.crtc[7]) {
                self->cga.cgadispon = 0;
                self->cga.displine  = 0;
                self->cga.vsynctime = 16;

                if (self->cga.crtc[7]) {
                    compaq_cga_log("Lastline %i Firstline %i  %i\n", self->cga.lastline,
                                   self->cga.firstline, self->cga.lastline - self->cga.firstline);

                    if (self->cga.cgamode & 1)
                        x = (self->cga.crtc[1] << 3) + 16;
                    else
                        x = (self->cga.crtc[1] << 4) + 16;

                    self->cga.lastline++;

                    xs_temp = x;
                    ys_temp = (self->cga.lastline - self->cga.firstline);

                    if ((xs_temp > 0) && (ys_temp > 0)) {
                        if (xs_temp < 64)
                            xs_temp = 656;
                        if (ys_temp < 32)
                            ys_temp = 400;
                        if (!enable_overscan)
                            xs_temp -= 16;

                        if ((self->cga.cgamode & 8) && ((xs_temp != xsize) || (ys_temp != ysize) || video_force_resize_get())) {
                            xsize = xs_temp;
                            ysize = ys_temp;
                            set_screen_size(xsize, ysize + (enable_overscan ? 16 : 0));

                            if (video_force_resize_get())
                                video_force_resize_set(0);
                        }

                        if (enable_overscan)
                            video_blit_memtoscreen(0, self->cga.firstline - 8, xsize, (self->cga.lastline - self->cga.firstline) + 16);
                        else
                            video_blit_memtoscreen(8, self->cga.firstline, xsize, self->cga.lastline - self->cga.firstline);
                    }

                    frames++;

                    video_res_x = xsize;
                    if (enable_overscan)
                        xsize -= 16;
                    video_res_y = ysize;
                    if (self->cga.cgamode & 1) {
                        video_res_x /= 8;
                        video_res_y /= self->cga.crtc[9] + 1;
                        video_bpp = 0;
                    } else if (!(self->cga.cgamode & 2)) {
                        video_res_x /= 16;
                        video_res_y /= self->cga.crtc[9] + 1;
                        video_bpp = 0;
                    } else if (!(self->cga.cgamode & 16)) {
                        video_res_x /= 2;
                        video_bpp = 2;
                    } else
                        video_bpp = 1;
                }

                self->cga.firstline = 1000;
                self->cga.lastline  = 0;
                self->cga.cgablink++;
                self->cga.oddeven ^= 1;
            }
        } else {
            self->cga.sc++;
            self->cga.sc &= 31;
            self->cga.ma = self->cga.maback;
        }

        if (self->cga.cgadispon)
            self->cga.cgastat &= ~1;

        if (self->cga.sc == (self->cga.crtc[10] & 31) || ((self->cga.crtc[8] & 3) == 3 && self->cga.sc == ((self->cga.crtc[10] & 31) >> 1)))
            self->cga.con = 1;

        if (self->cga.cgadispon && (self->cga.cgamode & 1)) {
            for (x = 0; x < (self->cga.crtc[1] << 1); x++)
                self->cga.charbuffer[x] = self->cga.vram[((self->cga.ma << 1) + x) & 0x3fff];
        }
    }
}

void *
compaq_cga_init(const device_t *info)
{
    int           display_type;
    compaq_cga_t *self = malloc(sizeof(compaq_cga_t));
    memset(self, 0, sizeof(compaq_cga_t));

    display_type           = device_get_config_int("display_type");
    self->cga.composite    = (display_type != CGA_RGB);
    self->cga.revision     = device_get_config_int("composite_type");
    self->cga.snow_enabled = device_get_config_int("snow_enabled");

    self->cga.vram = malloc(0x4000);

    cga_comp_init(self->cga.revision);
    timer_add(&self->cga.timer, compaq_cga_poll, self, 1);
    mem_mapping_add(&self->cga.mapping, 0xb8000, 0x08000, cga_read, NULL, NULL, cga_write, NULL, NULL, NULL /*self->cga.vram*/, MEM_MAPPING_EXTERNAL, self);
    io_sethandler(0x03d0, 0x0010, cga_in, NULL, NULL, cga_out, NULL, NULL, self);

    if (info->local) {
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

    vflags = info->local;

    overscan_x = overscan_y = 16;

    self->cga.rgb_type = device_get_config_int("rgb_type");
    cga_palette        = (self->cga.rgb_type << 1);
    cgapal_rebuild();

    self->cga.crtc[9] = 13;

    return self;
}

void
compaq_cga_close(void *priv)
{
    compaq_cga_t *self = (compaq_cga_t *) priv;

    free(self->cga.vram);
    free(self);
}

void
compaq_cga_speed_changed(void *priv)
{
    compaq_cga_t *self = (compaq_cga_t *) priv;

    if (self->cga.crtc[9] == 13) /* Character height */
        compaq_cga_recalctimings(self);
    else
        cga_recalctimings(&self->cga);
}

extern const device_config_t cga_config[];

const device_t compaq_cga_device = {
    .name          = "Compaq CGA",
    .internal_name = "compaq_cga",
    .flags         = DEVICE_ISA,
    .local         = 0,
    .init          = compaq_cga_init,
    .close         = compaq_cga_close,
    .reset         = NULL,
    .available     = NULL,
    .speed_changed = compaq_cga_speed_changed,
    .force_redraw  = NULL,
    .config        = cga_config
};

const device_t compaq_cga_2_device = {
    .name          = "Compaq CGA 2",
    .internal_name = "compaq_cga_2",
    .flags         = DEVICE_ISA,
    .local         = 1,
    .init          = compaq_cga_init,
    .close         = compaq_cga_close,
    .reset         = NULL,
    .available     = NULL,
    .speed_changed = compaq_cga_speed_changed,
    .force_redraw  = NULL,
    .config        = cga_config
};
