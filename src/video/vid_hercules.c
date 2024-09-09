/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          Hercules emulation.
 *
 *
 *
 * Authors: Sarah Walker, <https://pcem-emulator.co.uk/>
 *          Miran Grca, <mgrca8@gmail.com>
 *
 *          Copyright 2008-2019 Sarah Walker.
 *          Copyright 2016-2025 Miran Grca.
 */
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>
#include <86box/86box.h>
#include "cpu.h"
#include <86box/mem.h>
#include <86box/rom.h>
#include <86box/io.h>
#include <86box/timer.h>
#include <86box/lpt.h>
#include <86box/pit.h>
#include <86box/device.h>
#include <86box/video.h>
#include <86box/vid_hercules.h>
#include <86box/plat_unused.h>

static video_timings_t timing_hercules = { .type = VIDEO_ISA, .write_b = 8, .write_w = 16, .write_l = 32, .read_b = 8, .read_w = 16, .read_l = 32 };

static void
recalc_timings(hercules_t *dev)
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

static uint8_t crtcmask[32] = {
    0xff, 0xff, 0xff, 0xff, 0x7f, 0x1f, 0x7f, 0x7f, 0xf3, 0x1f, 0x7f, 0x1f, 0x3f, 0xff, 0x3f, 0xff,
    0xff, 0xff, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00
};

static void
hercules_out(uint16_t addr, uint8_t val, void *priv)
{
    hercules_t *dev = (hercules_t *) priv;
    uint8_t     old;

    VIDEO_MONITOR_PROLOGUE()
    switch (addr) {
        case 0x03b0:
        case 0x03b2:
        case 0x03b4:
        case 0x03b6:
            dev->crtcreg = val & 31;
            break;

        case 0x03b1:
        case 0x03b3:
        case 0x03b5:
        case 0x03b7:
            old                     = dev->crtc[dev->crtcreg];
            dev->crtc[dev->crtcreg] = val & crtcmask[dev->crtcreg];

            /*
             * Fix for Generic Turbo XT BIOS, which
             * sets up cursor registers wrong.
             */
            if (dev->crtc[10] == 6 && dev->crtc[11] == 7) {
                dev->crtc[10] = 0xb;
                dev->crtc[11] = 0xc;
            }

            if (old != val) {
                if ((dev->crtcreg < 0xe) || (dev->crtcreg > 0x10)) {
                    dev->fullchange = changeframecount;
                    recalc_timings(dev);
                }
            }
            break;

        case 0x03b8:
            old = dev->ctrl;

            /* Prevent setting of bits if they are disabled in CTRL2. */
            if ((old & 0x02) && !(val & 0x02))
                dev->ctrl &= 0xfd;
            else if ((val & 0x02) && (dev->ctrl2 & 0x01))
                dev->ctrl |= 0x02;

            if ((old & 0x80) && !(val & 0x80))
                dev->ctrl &= 0x7f;
            else if ((val & 0x80) && (dev->ctrl2 & 0x02))
                dev->ctrl |= 0x80;

            dev->ctrl = (dev->ctrl & 0x82) | (val & 0x7d);

            if (old ^ val)
                recalc_timings(dev);
            break;

        case 0x03b9:
        case 0x03bb:
            dev->lp_ff = !(addr & 0x0002);
            break;

        case 0x03bf:
            old        = dev->ctrl2;
            dev->ctrl2 = val;
            /* According to the Programmer's guide to the Hercules graphics cars
               by David B. Doty from 1988, the CTRL2 modes (bits 1,0) are as follow:
               - 00: DIAG: Text mode only, only page 0 accessible;
               - 01: HALF: Graphics mode allowed, only page 0 accessible;
               - 11: FULL: Graphics mode allowed, both pages accessible. */
            if (val & 0x01)
                mem_mapping_set_exec(&dev->mapping, dev->vram);
            else
                mem_mapping_set_exec(&dev->mapping, NULL);
            if (val & 0x02)
                mem_mapping_set_addr(&dev->mapping, 0xb0000, 0x10000);
            else
                mem_mapping_set_addr(&dev->mapping, 0xb0000, 0x08000);
            if (old ^ val)
                recalc_timings(dev);
            break;

        default:
            break;
    }

    VIDEO_MONITOR_EPILOGUE()
}

static uint8_t
hercules_in(uint16_t addr, void *priv)
{
    const hercules_t *dev = (hercules_t *) priv;
    uint8_t           ret = 0xff;

    switch (addr) {
        case 0x03b0:
        case 0x03b2:
        case 0x03b4:
        case 0x03b6:
            ret = dev->crtcreg;
            break;

        case 0x03b1:
        case 0x03b3:
        case 0x03b5:
        case 0x03b7:
            if (dev->crtcreg == 0x0c)
                ret = (dev->ma >> 8) & 0x3f;
            else if (dev->crtcreg == 0x0d)
                ret = dev->ma & 0xff;
            else
                ret = dev->crtc[dev->crtcreg];
            break;

        case 0x03ba:
            ret = 0x70; /* Hercules ident */
            ret |= (dev->lp_ff ? 2 : 0);
            ret |= (dev->stat & 0x01);
            if (dev->stat & 0x08)
                ret |= 0x80;
            if ((ret & 0x81) == 0x80)
                ret |= 0x08;
            break;

        default:
            break;
    }

    return ret;
}

static void
hercules_waitstates(UNUSED(void *priv))
{
    int ws_array[16] = { 3, 4, 5, 6, 7, 8, 4, 5, 6, 7, 8, 4, 5, 6, 7, 8 };
    int ws;

    ws = ws_array[cycles & 0xf];
    cycles -= ws;
}

static void
hercules_write(uint32_t addr, uint8_t val, void *priv)
{
    hercules_t *dev = (hercules_t *) priv;

    if (dev->ctrl2 & 0x01)
        addr &= 0xffff;
    else
        addr &= 0x0fff;

    dev->vram[addr] = val;

    hercules_waitstates(dev);
}

static uint8_t
hercules_read(uint32_t addr, void *priv)
{
    hercules_t *dev = (hercules_t *) priv;
    uint8_t     ret = 0xff;

    if (dev->ctrl2 & 0x01)
        addr &= 0xffff;
    else
        addr &= 0x0fff;

    hercules_waitstates(dev);

    ret = dev->vram[addr];

    return ret;
}

static void
hercules_render_overscan_left(hercules_t *dev)
{
    uint32_t width;

    if (dev->ctrl & 0x02)
        width = (((uint32_t) dev->crtc[1]) << 4);
    else
        width = (((uint32_t) dev->crtc[1]) * 9);

    if ((dev->displine + 14) < 0)
        return;

    if (width == 0)
        return;

    for (uint8_t i = 0; i < 8; i++)
        buffer32->line[dev->displine + 14][i] = 0x00000000;
}

static void
hercules_render_overscan_right(hercules_t *dev)
{
    uint32_t width;

    if (dev->ctrl & 0x02)
        width = (((uint32_t) dev->crtc[1]) << 4);
    else
        width = (((uint32_t) dev->crtc[1]) * 9);

    if ((dev->displine + 14) < 0)
        return;

    if (width == 0)
        return;

    for (uint8_t i = 0; i < 8; i++)
        buffer32->line[dev->displine + 14][8 + width + i] = 0x00000000;
}

static void
hercules_poll(void *priv)
{
    hercules_t *dev = (hercules_t *) priv;
    uint8_t     chr;
    uint8_t     attr;
    uint16_t    ca;
    uint16_t    dat;
    uint16_t    pa;
    int         oldsc;
    int         blink;
    int         x;
    int         xx;
    int         y;
    int         yy;
    int         c;
    int         oldvc;
    int         drawcursor;
    uint32_t   *p;

    VIDEO_MONITOR_PROLOGUE()
    ca = (dev->crtc[15] | (dev->crtc[14] << 8)) & 0x3fff;

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

            hercules_render_overscan_left(dev);

            if (dev->ctrl & 0x02) {
                ca = (dev->sc & 3) * 0x2000;
                if (dev->ctrl & 0x80)
                    ca += 0x8000;

                for (x = 0; x < dev->crtc[1]; x++) {
                    if (dev->ctrl & 8)
                        dat = (dev->vram[((dev->ma << 1) & 0x1fff) + ca] << 8) | dev->vram[((dev->ma << 1) & 0x1fff) + ca + 1];
                    else
                        dat = 0;
                    dev->ma++;
                    for (c = 0; c < 16; c++)
                        buffer32->line[dev->displine + 14][(x << 4) + c + 8] = (dat & (32768 >> c)) ? 7 : 0;
                    for (c = 0; c < 16; c += 8)
                        video_blend((x << 4) + c + 8, dev->displine + 14);
                }
            } else {
                for (x = 0; x < dev->crtc[1]; x++) {
                    if (dev->ctrl & 8) {
                        /* Undocumented behavior: page 1 in text mode means characters are read
                           from page 1 and attributes from page 0. */
                        chr  = dev->charbuffer[x << 1];
                        attr = dev->charbuffer[(x << 1) + 1];
                    } else
                        chr = attr = 0;
                    drawcursor = ((dev->ma == ca) && dev->con && dev->cursoron);
                    blink      = ((dev->blink & 16) && (dev->ctrl & 0x20) && (attr & 0x80) && !drawcursor);

                    if (dev->sc == 12 && ((attr & 7) == 1)) {
                        for (c = 0; c < 9; c++)
                            buffer32->line[dev->displine + 14][(x * 9) + c + 8] = dev->cols[attr][blink][1];
                    } else {
                        for (c = 0; c < 8; c++)
                            buffer32->line[dev->displine + 14][(x * 9) + c + 8] = dev->cols[attr][blink][(fontdatm[chr][dev->sc] & (1 << (c ^ 7))) ? 1 : 0];

                        if ((chr & ~0x1f) == 0xc0)
                            buffer32->line[dev->displine + 14][(x * 9) + 8 + 8] = dev->cols[attr][blink][fontdatm[chr][dev->sc] & 1];
                        else
                            buffer32->line[dev->displine + 14][(x * 9) + 8 + 8] = dev->cols[attr][blink][0];
                    }
                    if (dev->ctrl2 & 0x01)
                        dev->ma = (dev->ma + 1) & 0x3fff;
                    else
                        dev->ma = (dev->ma + 1) & 0x7ff;

                    if (drawcursor) {
                        for (c = 0; c < 9; c++)
                            buffer32->line[dev->displine + 14][(x * 9) + c + 8] ^= dev->cols[attr][0][1];
                    }
                }
            }

            hercules_render_overscan_right(dev);

            if (dev->ctrl & 0x02)
                x = dev->crtc[1] << 4;
            else
                x = dev->crtc[1] * 9;

            video_process_8(x + 16, dev->displine + 14);
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
        } else if (((dev->crtc[8] & 3) != 3 && dev->sc == dev->crtc[9]) || ((dev->crtc[8] & 3) == 3 && dev->sc == (dev->crtc[9] >> 1))) {
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
                if (!dev->vadj) {
                    dev->dispon = 1;
                    dev->ma = dev->maback = (dev->crtc[13] | (dev->crtc[12] << 8)) & 0x3fff;
                }
                switch (dev->crtc[10] & 0x60) {
                    case 0x20:
                        dev->cursoron = 0;
                        break;
                    case 0x60:
                        dev->cursoron = dev->blink & 0x10;
                        break;
                    default:
                        dev->cursoron = dev->blink & 0x08;
                        break;
                }
            }

            if (dev->vc == dev->crtc[7]) {
                dev->dispon   = 0;
                dev->displine = 0;
                if ((dev->crtc[8] & 3) == 3)
                    dev->vsynctime = ((int32_t) dev->crtc[4] * ((dev->crtc[9] >> 1) + 1)) + dev->crtc[5] - dev->crtc[7] + 1;
                else
                    dev->vsynctime = ((int32_t) dev->crtc[4] * (dev->crtc[9] + 1)) + dev->crtc[5] - dev->crtc[7] + 1;
                if (dev->crtc[7]) {
                    if (dev->ctrl & 0x02)
                        x = dev->crtc[1] << 4;
                    else
                        x = dev->crtc[1] * 9;

                    dev->lastline++;
                    y = (dev->lastline - dev->firstline);

                    if ((dev->ctrl & 8) && x && y && ((x != xsize) || (y != ysize) || video_force_resize_get())) {
                        xsize = x;
                        ysize = y;
                        if (xsize < 64)
                            xsize = enable_overscan ? 640 : 656;
                        if (ysize < 32)
                            ysize = 200;

                        set_screen_size(xsize + (enable_overscan ? 16 : 0), ysize + (enable_overscan ? 28 : 0));

                        if (video_force_resize_get())
                            video_force_resize_set(0);
                    }

                    if ((x >= 160) && ((y + 1) >= 120)) {
                        /* Draw (overscan_size) lines of overscan on top and bottom. */
                        for (yy = 0; yy < 14; yy++) {
                            p = &(buffer32->line[(dev->firstline + yy) & 0x7ff][0]);

                            for (xx = 0; xx < (x + 16); xx++)
                                p[xx] = 0x00000000;
                        }

                        for (yy = 0; yy < 14; yy++) {
                            p = &(buffer32->line[(dev->firstline + 14 + y + yy) & 0x7ff][0]);

                            for (xx = 0; xx < (x + 16); xx++)
                                p[xx] = 0x00000000;
                        }
                    }

                    if (enable_overscan)
                        video_blit_memtoscreen(0, dev->firstline, xsize + 16, ysize + 28);
                    else
                        video_blit_memtoscreen(8, dev->firstline + 14, xsize, ysize);
                    frames++;
#if 0
                    if ((dev->ctrl & 2) && (dev->ctrl2 & 1)) {
#endif
                    if (dev->ctrl & 0x02) {
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
        if (dev->dispon && !(dev->ctrl & 0x02)) {
            for (x = 0; x < (dev->crtc[1] << 1); x++) {
                pa                 = (dev->ctrl & 0x80) ? ((x & 1) ? 0x0000 : 0x8000) : 0x0000;
                dev->charbuffer[x] = dev->vram[(((dev->ma << 1) + x) & 0x3fff) + pa];
            }
        }
    }
    VIDEO_MONITOR_EPILOGUE()
}

static void *
hercules_init(UNUSED(const device_t *info))
{
    hercules_t *dev;

    dev = (hercules_t *) malloc(sizeof(hercules_t));
    memset(dev, 0x00, sizeof(hercules_t));
    dev->monitor_index = monitor_index_global;

    overscan_x = 16;
    overscan_y = 28;

    dev->vram = (uint8_t *) malloc(0x10000);

    timer_add(&dev->timer, hercules_poll, dev, 1);

    mem_mapping_add(&dev->mapping, 0xb0000, 0x08000,
                    hercules_read, NULL, NULL, hercules_write, NULL, NULL,
                    NULL /*dev->vram*/, MEM_MAPPING_EXTERNAL, dev);

    io_sethandler(0x03b0, 16,
                  hercules_in, NULL, NULL, hercules_out, NULL, NULL, dev);

    for (uint16_t c = 0; c < 256; c++) {
        dev->cols[c][0][0] = dev->cols[c][1][0] = dev->cols[c][1][1] = 16;

        if (c & 0x08)
            dev->cols[c][0][1] = 15 + 16;
        else
            dev->cols[c][0][1] = 7 + 16;
    }
    dev->cols[0x70][0][1] = 16;
    dev->cols[0x70][0][0] = dev->cols[0x70][1][0] = dev->cols[0x70][1][1] = 16 + 15;
    dev->cols[0xF0][0][1]                                                 = 16;
    dev->cols[0xF0][0][0] = dev->cols[0xF0][1][0] = dev->cols[0xF0][1][1] = 16 + 15;
    dev->cols[0x78][0][1]                                                 = 16 + 7;
    dev->cols[0x78][0][0] = dev->cols[0x78][1][0] = dev->cols[0x78][1][1] = 16 + 15;
    dev->cols[0xF8][0][1]                                                 = 16 + 7;
    dev->cols[0xF8][0][0] = dev->cols[0xF8][1][0] = dev->cols[0xF8][1][1] = 16 + 15;
    dev->cols[0x00][0][1] = dev->cols[0x00][1][1] = 16;
    dev->cols[0x08][0][1] = dev->cols[0x08][1][1] = 16;
    dev->cols[0x80][0][1] = dev->cols[0x80][1][1] = 16;
    dev->cols[0x88][0][1] = dev->cols[0x88][1][1] = 16;

    overscan_x = overscan_y = 0;

    cga_palette = device_get_config_int("rgb_type") << 1;
    if (cga_palette > 6)
        cga_palette = 0;
    cgapal_rebuild();

    herc_blend = device_get_config_int("blend");

    video_inform(VIDEO_FLAG_TYPE_MDA, &timing_hercules);

    /* Force the LPT3 port to be enabled. */
    lpt3_setup(LPT_MDA_ADDR);

    return dev;
}

static void
hercules_close(void *priv)
{
    hercules_t *dev = (hercules_t *) priv;

    if (!dev)
        return;

    if (dev->vram)
        free(dev->vram);

    free(dev);
}

static void
speed_changed(void *priv)
{
    hercules_t *dev = (hercules_t *) priv;

    recalc_timings(dev);
}

static const device_config_t hercules_config[] = {
  // clang-format off
    {
        .name           = "rgb_type",
        .description    = "Display type",
        .type           = CONFIG_SELECTION,
        .default_string = NULL,
        .default_int    = 0,
        .file_filter    = NULL,
        .spinner        = { 0 },
        .selection      = {
            { .description = "Default", .value = 0 },
            { .description = "Green",   .value = 1 },
            { .description = "Amber",   .value = 2 },
            { .description = "Gray",    .value = 3 },
            { .description = ""                    }
        },
        .bios           = { { 0 } }
    },
    {
        .name           = "blend",
        .description    = "Blend",
        .type           = CONFIG_BINARY,
        .default_string = NULL,
        .default_int    = 1,
        .file_filter    = NULL,
        .spinner        = { 0 },
        .selection      = { { 0 } },
        .bios           = { { 0 } }
    },
    { .name = "", .description = "", .type = CONFIG_END }
  // clang-format on
};

const device_t hercules_device = {
    .name          = "Hercules",
    .internal_name = "hercules",
    .flags         = DEVICE_ISA,
    .local         = 0,
    .init          = hercules_init,
    .close         = hercules_close,
    .reset         = NULL,
    .available     = NULL,
    .speed_changed = speed_changed,
    .force_redraw  = NULL,
    .config        = hercules_config
};
