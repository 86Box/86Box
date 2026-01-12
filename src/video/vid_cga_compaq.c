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

static void
compaq_cga_recalctimings(cga_t *dev)
{
    double _dispontime;
    double _dispofftime;
    double disptime;
    disptime = dev->crtc[CGA_CRTC_HTOTAL] + 1;

    _dispontime  = dev->crtc[CGA_CRTC_HDISP];
    _dispofftime = disptime - _dispontime;
    _dispontime *= MDACONST;
    _dispofftime *= MDACONST;
    dev->dispontime  = (uint64_t) (_dispontime);
    dev->dispofftime = (uint64_t) (_dispofftime);
}

static void
compaq_cga_poll(void *priv)
{
    cga_t        *dev  = (cga_t *) priv;
    uint16_t      cursoraddr   = (dev->crtc[CGA_CRTC_CURSOR_ADDR_LOW] | (dev->crtc[CGA_CRTC_CURSOR_ADDR_HIGH] << 8)) & 0x3fff;
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
    int           scanline_old;
    int           underline = 0;
    int           blink     = 0;

    int32_t  highres_graphics_flag = (CGA_MODE_FLAG_HIGHRES_GRAPHICS | CGA_MODE_FLAG_GRAPHICS);

    /* If in graphics mode or character height is not 13, behave as CGA */
    if ((dev->cgamode & highres_graphics_flag) || (dev->crtc[CGA_CRTC_MAX_SCANLINE_ADDR] != 13)) {
        overscan_x = overscan_y = 16;
        cga_poll(dev);
        return;
    } else
        overscan_x = overscan_y = 0;

    /* We are in Compaq 350-line CGA territory */
    if (!dev->linepos) {
        timer_advance_u64(&dev->timer, dev->dispofftime);
        dev->cgastat |= 1;
        dev->linepos  = 1;
        scanline_old         = dev->scanline;
        if ((dev->crtc[CGA_CRTC_INTERLACE] & 3) == 3)
            dev->scanline = ((dev->scanline << 1) + dev->oddeven) & 7;
        if (dev->cgadispon) {
            if (dev->displine < dev->firstline) {
                dev->firstline = dev->displine;
                video_wait_for_buffer();
                compaq_cga_log("Firstline %i\n", dev->firstline);
            }
            dev->lastline = dev->displine;

            cols[0] = (dev->cgacol & 15) + 16;

            for (c = 0; c < 8; c++) {
                buffer32->line[dev->displine][c] = cols[0];
                if (dev->cgamode & CGA_MODE_FLAG_HIGHRES)
                    buffer32->line[dev->displine][c + (dev->crtc[CGA_CRTC_HDISP] << 3) + 8] = cols[0];
                else
                    buffer32->line[dev->displine][c + (dev->crtc[CGA_CRTC_HDISP] << 4) + 8] = cols[0];
            }

            if (dev->cgamode & CGA_MODE_FLAG_HIGHRES) {
                for (x = 0; x < dev->crtc[CGA_CRTC_HDISP]; x++) {
                    chr        = dev->charbuffer[x << 1];
                    attr       = dev->charbuffer[(x << 1) + 1];
                    drawcursor = ((dev->memaddr == cursoraddr) && dev->cursorvisible && dev->cursoron);

                    if (vflags) {
                        underline = 0;
                        blink     = ((dev->cgablink & 8) && (dev->cgamode & CGA_MODE_FLAG_BLINK) && (attr & 0x80) && !drawcursor);
                    }

                    if (vflags && (dev->cgamode & 0x80)) {
                        cols[0] = mdaattr[attr][blink][0];
                        cols[1] = mdaattr[attr][blink][1];

                        if ((dev->scanline == 12) && ((attr & 7) == 1))
                            underline = 1;
                    } else if (dev->cgamode & CGA_MODE_FLAG_BLINK) {
                        cols[1] = (attr & 15) + 16;
                        cols[0] = ((attr >> 4) & 7) + 16;

                        if (vflags) {
                            if (blink)
                                cols[1] = cols[0];
                        } else {
                            if ((dev->cgablink & 8) && (attr & 0x80) && !dev->drawcursor)
                                cols[1] = cols[0];
                        }
                    } else {
                        cols[1] = (attr & 15) + 16;
                        cols[0] = (attr >> 4) + 16;
                    }

                    if (vflags && underline) {
                        for (c = 0; c < 8; c++)
                            buffer32->line[dev->displine][(x << 3) + c + 8] = mdaattr[attr][blink][1];
                    } else if (drawcursor) {
                        for (c = 0; c < 8; c++)
                            buffer32->line[dev->displine][(x << 3) + c + 8] =
                                cols[(fontdatm[chr + dev->fontbase][dev->scanline & 15] & (1 << (c ^ 7))) ? 1 : 0] ^ 15;
                    } else {
                        for (c = 0; c < 8; c++)
                            buffer32->line[dev->displine][(x << 3) + c + 8] =
                                cols[(fontdatm[chr + dev->fontbase][dev->scanline & 15] & (1 << (c ^ 7))) ? 1 : 0];
                    }
                    dev->memaddr++;
                }
            } else {
                for (x = 0; x < dev->crtc[CGA_CRTC_HDISP]; x++) {
                    chr        = dev->vram[(dev->memaddr << 1) & 0x3fff];
                    attr       = dev->vram[((dev->memaddr << 1) + 1) & 0x3fff];
                    drawcursor = ((dev->memaddr == cursoraddr) && dev->cursorvisible && dev->cursoron);

                    if (vflags) {
                        underline = 0;
                        blink     = ((dev->cgablink & 8) && (dev->cgamode & CGA_MODE_FLAG_BLINK) && (attr & 0x80) && !drawcursor);
                    }

                    if (vflags && (dev->cgamode & 0x80)) {
                        cols[0] = mdaattr[attr][blink][0];
                        cols[1] = mdaattr[attr][blink][1];
                        if (dev->scanline == 12 && (attr & 7) == 1)
                            underline = 1;
                    } else if (dev->cgamode & CGA_MODE_FLAG_BLINK) {
                        cols[1] = (attr & 15) + 16;
                        cols[0] = ((attr >> 4) & 7) + 16;

                        if (vflags) {
                            if (blink)
                                cols[1] = cols[0];
                        } else {
                            if ((dev->cgablink & 8) && (attr & 0x80) && !dev->drawcursor)
                                cols[1] = cols[0];
                        }
                    } else {
                        cols[1] = (attr & 15) + 16;
                        cols[0] = (attr >> 4) + 16;
                    }
                    dev->memaddr++;

                    if (vflags && underline) {
                        for (c = 0; c < 8; c++)
                            buffer32->line[dev->displine][(x << 4) + (c << 1) + 8] =
                                buffer32->line[dev->displine][(x << 4) + (c << 1) + 9] = mdaattr[attr][blink][1];
                    } else if (drawcursor) {
                        for (c = 0; c < 8; c++)
                            buffer32->line[dev->displine][(x << 4) + (c << 1) + 8] =
                                buffer32->line[dev->displine][(x << 4) + (c << 1) + 1 + 8] =
                                cols[(fontdatm[chr + dev->fontbase][dev->scanline & 15] & (1 << (c ^ 7))) ? 1 : 0] ^ 15;
                    } else {
                        for (c = 0; c < 8; c++)
                            buffer32->line[dev->displine][(x << 4) + (c << 1) + 8] =
                                buffer32->line[dev->displine][(x << 4) + (c << 1) + 1 + 8] =
                                cols[(fontdatm[chr + dev->fontbase][dev->scanline & 15] & (1 << (c ^ 7))) ? 1 : 0];
                    }
                }
            }
        } else {
            cols[0] = (dev->cgacol & 15) + 16;

            if (dev->cgamode & CGA_MODE_FLAG_HIGHRES)
                hline(buffer32, 0, dev->displine, (dev->crtc[CGA_CRTC_HDISP] << 3) + 16, cols[0]);
            else
                hline(buffer32, 0, dev->displine, (dev->crtc[CGA_CRTC_HDISP] << 4) + 16, cols[0]);
        }

        if (dev->cgamode & CGA_MODE_FLAG_HIGHRES)
            x = (dev->crtc[CGA_CRTC_HDISP] << 3) + 16;
        else
            x = (dev->crtc[CGA_CRTC_HDISP] << 4) + 16;

        if (dev->composite) {
            if (dev->cgamode & CGA_MODE_FLAG_HIGHRES_GRAPHICS)
                border = 0x00;
            else
                border = dev->cgacol & 0x0f;

            if (vflags)
                Composite_Process(dev->cgamode & 0x7f, border, x >> 2, buffer32->line[dev->displine]);
            else
                Composite_Process(dev->cgamode, border, x >> 2, buffer32->line[dev->displine]);
        } else
            video_process_8(x, dev->displine);

        dev->scanline = scanline_old;
        if (dev->vc == dev->crtc[CGA_CRTC_VSYNC] && !dev->scanline)
            dev->cgastat |= 8;
        dev->displine++;
        if (dev->displine >= 500)
            dev->displine = 0;
    } else {
        timer_advance_u64(&dev->timer, dev->dispontime);
        dev->linepos = 0;
        if (dev->vsynctime) {
            dev->vsynctime--;
            if (!dev->vsynctime)
                dev->cgastat &= ~8;
        }

        if (dev->scanline == (dev->crtc[11] & 31) || ((dev->crtc[8] & 3) == 3 && dev->scanline == ((dev->crtc[11] & 31) >> 1))) {
            dev->cursorvisible  = 0;
        }
        if ((dev->crtc[8] & 3) == 3 && dev->scanline == (dev->crtc[9] >> 1))
            dev->memaddr_backup = dev->memaddr;
        if (dev->vadj) {
            dev->scanline++;
            dev->scanline &= 31;
            dev->memaddr = dev->memaddr_backup;
            dev->vadj--;
            if (!dev->vadj) {
                dev->cgadispon = 1;
                dev->memaddr = dev->memaddr_backup = (dev->crtc[13] | (dev->crtc[12] << 8)) & 0x3fff;
                dev->scanline                    = 0;
            }
        } else if (dev->scanline == dev->crtc[9]) {
            dev->memaddr_backup = dev->memaddr;
            dev->scanline     = 0;
            oldvc            = dev->vc;
            dev->vc++;
            dev->vc &= 127;

            if (dev->vc == dev->crtc[6])
                dev->cgadispon = 0;

            if (oldvc == dev->crtc[4]) {
                dev->vc   = 0;
                dev->vadj = dev->crtc[5];

                if (!dev->vadj)
                    dev->cgadispon = 1;

                if (!dev->vadj)
                    dev->memaddr = dev->memaddr_backup = (dev->crtc[13] | (dev->crtc[12] << 8)) & 0x3fff;

                if ((dev->crtc[10] & 0x60) == 0x20)
                    dev->cursoron = 0;
                else
                    dev->cursoron = dev->cgablink & 8;
            }

            if (dev->vc == dev->crtc[7]) {
                dev->cgadispon = 0;
                dev->displine  = 0;
                dev->vsynctime = 16;

                if (dev->crtc[7]) {
                    compaq_cga_log("Lastline %i Firstline %i  %i\n", dev->lastline,
                                   dev->firstline, dev->lastline - dev->firstline);

                    if (dev->cgamode & CGA_MODE_FLAG_HIGHRES)
                        x = (dev->crtc[CGA_CRTC_HDISP] << 3) + 16;
                    else
                        x = (dev->crtc[CGA_CRTC_HDISP] << 4) + 16;

                    dev->lastline++;

                    xs_temp = x;
                    ys_temp = (dev->lastline - dev->firstline);

                    if ((xs_temp > 0) && (ys_temp > 0)) {
                        if (xs_temp < 64)
                            xs_temp = 656;
                        if (ys_temp < 32)
                            ys_temp = 400;
                        if (!enable_overscan)
                            xs_temp -= 16;

                        if ((dev->cgamode & 8) && ((xs_temp != xsize) || (ys_temp != ysize) || video_force_resize_get())) {
                            xsize = xs_temp;
                            ysize = ys_temp;
                            set_screen_size(xsize, ysize + (enable_overscan ? 16 : 0));

                            if (video_force_resize_get())
                                video_force_resize_set(0);
                        }

                        if (enable_overscan)
                            video_blit_memtoscreen(0, dev->firstline - 8, xsize, (dev->lastline - dev->firstline) + 16);
                        else
                            video_blit_memtoscreen(8, dev->firstline, xsize, dev->lastline - dev->firstline);
                    }

                    frames++;

                    video_res_x = xsize;
                    if (enable_overscan)
                        xsize -= 16;
                    video_res_y = ysize;
                    if (dev->cgamode & CGA_MODE_FLAG_HIGHRES) {
                        video_res_x /= 8;
                        video_res_y /= dev->crtc[9] + 1;
                        video_bpp = 0;
                    } else if (!(dev->cgamode & 2)) {
                        video_res_x /= 16;
                        video_res_y /= dev->crtc[9] + 1;
                        video_bpp = 0;
                    } else if (!(dev->cgamode & 16)) {
                        video_res_x /= 2;
                        video_bpp = 2;
                    } else
                        video_bpp = 1;
                }

                dev->firstline = 1000;
                dev->lastline  = 0;
                dev->cgablink++;
                dev->oddeven ^= 1;
            }
        } else {
            dev->scanline++;
            dev->scanline &= 31;
            dev->memaddr = dev->memaddr_backup;
        }

        if (dev->cgadispon)
            dev->cgastat &= ~1;

        if (dev->scanline == (dev->crtc[10] & 31) || ((dev->crtc[8] & 3) == 3 && dev->scanline == ((dev->crtc[10] & 31) >> 1)))
            dev->cursorvisible = 1;

        if (dev->cgadispon && (dev->cgamode & CGA_MODE_FLAG_HIGHRES)) {
            for (x = 0; x < (dev->crtc[CGA_CRTC_HDISP] << 1); x++)
                dev->charbuffer[x] = dev->vram[((dev->memaddr << 1) + x) & 0x3fff];
        }
    }
}

static void *
compaq_cga_init(const device_t *info)
{
    int    display_type;
    cga_t *dev          = calloc(1, sizeof(cga_t));

    display_type        = device_get_config_int("display_type");
    dev->composite      = (display_type != CGA_RGB);
    dev->revision       = device_get_config_int("composite_type");
    dev->snow_enabled   = device_get_config_int("snow_enabled");

    dev->vram           = malloc(0x4000);

    cga_comp_init(dev->revision);
    timer_add(&dev->timer, compaq_cga_poll, dev, 1);
    mem_mapping_add(&dev->mapping, 0xb8000, 0x08000,
                    cga_read, NULL, NULL,
                    cga_write, NULL, NULL,
                    NULL, MEM_MAPPING_EXTERNAL, dev);
    io_sethandler(0x03d0, 0x0010,
                  cga_in, NULL, NULL,
                  cga_out, NULL, NULL,
                  dev);

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

    dev->rgb_type = device_get_config_int("rgb_type");
    cga_palette   = (dev->rgb_type << 1);
    cgapal_rebuild();

    dev->crtc[9] = 13;

    monitors[monitor_index_global].mon_composite = !!dev->composite;

    return dev;
}

static void
compaq_cga_close(void *priv)
{
    cga_t *dev = (cga_t *) priv;

    free(dev->vram);
    free(dev);
}

static void
compaq_cga_speed_changed(void *priv)
{
    cga_t *dev = (cga_t *) priv;

    if (dev->crtc[9] == 13) /* Character height */
        compaq_cga_recalctimings(dev);
    else
        cga_recalctimings(dev);
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
