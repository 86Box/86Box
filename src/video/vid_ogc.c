/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          Emulation of the Olivetti OGC 8-bit ISA (GO708) and
 *          M21/M24/M28 16-bit bus (GO317/318/380/709) video cards.
 *
 *
 *
 * Authors: Sarah Walker, <https://pcem-emulator.co.uk/>
 *          Miran Grca, <mgrca8@gmail.com>
 *          Fred N. van Kempen, <decwiz@yahoo.com>
 *          EngiNerd, <webmaster.crrc@yahoo.it>
 *
 *          Copyright 2008-2019 Sarah Walker.
 *          Copyright 2016-2019 Miran Grca.
 *          Copyright 2017-2019 Fred N. van Kempen.
 *          Copyright 2020      EngiNerd.
 */

#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <stdlib.h>
#include <stdarg.h>
#include <wchar.h>
#include <86box/io.h>
#include <86box/video.h>
#include <86box/86box.h>
#include <86box/timer.h>
#include <86box/mem.h>
#include <86box/pit.h>
#include <86box/rom.h>
#include <86box/device.h>
#include <86box/vid_cga.h>
#include <86box/vid_ogc.h>
#include <86box/vid_cga_comp.h>
#include <86box/plat_unused.h>

/*
 * Current bugs:
 * - Olivetti diagnostics fail with errors: 6845 crtc write / read error out 0000 in 00ff
 * - Dark blue (almost black) picture in composite mode
 */

#define CGA_RGB       0
#define CGA_COMPOSITE 1

#define COMPOSITE_OLD 0
#define COMPOSITE_NEW 1

static video_timings_t timing_ogc = { .type = VIDEO_ISA, .write_b = 8, .write_w = 16, .write_l = 32, .read_b = 8, .read_w = 16, .read_l = 32 };

static uint8_t mdaattr[256][2][2];

void
ogc_recalctimings(ogc_t *ogc)
{
    double _dispontime;
    double _dispofftime;
    double disptime;

    if (ogc->cga.cgamode & 1) {
        disptime    = ogc->cga.crtc[0] + 1;
        _dispontime = ogc->cga.crtc[1];
    } else {
        disptime    = (ogc->cga.crtc[0] + 1) << 1;
        _dispontime = ogc->cga.crtc[1] << 1;
    }

    _dispofftime = disptime - _dispontime;
    _dispontime *= CGACONST / 2;
    _dispofftime *= CGACONST / 2;
    ogc->cga.dispontime  = (uint64_t) (_dispontime);
    ogc->cga.dispofftime = (uint64_t) (_dispofftime);
}

void
ogc_out(uint16_t addr, uint8_t val, void *priv)
{
    ogc_t *ogc = (ogc_t *) priv;

#if 0
    if (addr >= 0x3c0 && addr <= 0x3cf)
        addr = addr + 16;
#endif

    switch (addr) {
        case 0x3d4:
        case 0x3d5:
        case 0x3d8:
        case 0x3d9:
            cga_out(addr, val, &ogc->cga);
            break;

        case 0x3db:
        case 0x3de:
            if (addr == ogc->ctrl_addr) {
                /* set control register */
                ogc->ctrl_3de = val;
                /* select 1st or 2nd 16k vram block to be used */
                ogc->base = (val & 0x08) ? 0x4000 : 0;
            }
            break;

        default:
            break;
    }
}

uint8_t
ogc_in(uint16_t addr, void *priv)
{
    ogc_t *ogc = (ogc_t *) priv;

#if 0
    if (addr >= 0x3c0 && addr <= 0x3cf)
        addr = addr + 16;
#endif

    uint8_t ret = 0xff;

    switch (addr) {
        case 0x3d4:
        case 0x3d5:
        case 0x3da:
            /*
             * bits 6-7: 3 = no DEB expansion board installed
             * bits 4-5: 2 color, 3 mono
             * bit 3: high during 1st half of vertical retrace in character mode (CCA standard)
             * bit 2: lightpen switch (CGA standard)
             * bit 1: lightpen strobe (CGA standard)
             * bit 0: high during retrace (CGA standard)
             */
            ret = cga_in(addr, &ogc->cga);
            if (addr == 0x3da) {
                ret = ret | 0xe0;
                if (ogc->mono_display)
                    ret = ret | 0x10;
            }
            break;

        default:
            break;
    }

    return ret;
}

void
ogc_waitstates(UNUSED(void *priv))
{
    int ws_array[16] = { 3, 4, 5, 6, 7, 8, 4, 5, 6, 7, 8, 4, 5, 6, 7, 8 };
    int ws;

    ws = ws_array[cycles & 0xf];
    sub_cycles(ws);
}

void
ogc_write(uint32_t addr, uint8_t val, void *priv)
{
    ogc_t *ogc = (ogc_t *) priv;
    int    offset;

    ogc->cga.vram[addr & 0x7FFF] = val;
    if (ogc->cga.snow_enabled) {
        /* recreate snow effect */
        offset                          = ((timer_get_remaining_u64(&ogc->cga.timer) / CGACONST) * 4) & 0xfc;
        ogc->cga.charbuffer[offset]     = ogc->cga.vram[addr & 0x7fff];
        ogc->cga.charbuffer[offset | 1] = ogc->cga.vram[addr & 0x7fff];
    }
    ogc_waitstates(&ogc->cga);
}

uint8_t
ogc_read(uint32_t addr, void *priv)
{

    ogc_t *ogc = (ogc_t *) priv;
    int    offset;

    ogc_waitstates(&ogc->cga);

    if (ogc->cga.snow_enabled) {
        /* recreate snow effect */
        offset                          = ((timer_get_remaining_u64(&ogc->cga.timer) / CGACONST) * 4) & 0xfc;
        ogc->cga.charbuffer[offset]     = ogc->cga.vram[addr & 0x7fff];
        ogc->cga.charbuffer[offset | 1] = ogc->cga.vram[addr & 0x7fff];
    }

    return (ogc->cga.vram[addr & 0x7FFF]);
}

void
ogc_poll(void *priv)
{
    ogc_t   *ogc = (ogc_t *) priv;
    uint16_t ca  = (ogc->cga.crtc[15] | (ogc->cga.crtc[14] << 8)) & 0x3fff;
    int      drawcursor;
    int      x;
    int      c;
    int      xs_temp;
    int      ys_temp;
    int      oldvc;
    uint8_t  chr;
    uint8_t  attr;
    uint16_t dat;
    uint16_t dat2;
    int      cols[4];
    int      oldsc;
    int      blink     = 0;
    int      underline = 0;

    // composito colore appare blu scuro

    /* graphic mode and not mode 40h */
    if (!(ogc->ctrl_3de & 0x1 || !(ogc->cga.cgamode & 2))) {
        /* standard cga mode */
        cga_poll(&ogc->cga);
        return;
    } else {
        /* mode 40h or text mode */
        if (!ogc->cga.linepos) {
            timer_advance_u64(&ogc->cga.timer, ogc->cga.dispofftime);
            ogc->cga.cgastat |= 1;
            ogc->cga.linepos = 1;
            oldsc            = ogc->cga.sc;
            if ((ogc->cga.crtc[8] & 3) == 3)
                ogc->cga.sc = ((ogc->cga.sc << 1) + ogc->cga.oddeven) & 7;
            if (ogc->cga.cgadispon) {
                if (ogc->cga.displine < ogc->cga.firstline) {
                    ogc->cga.firstline = ogc->cga.displine;
                    video_wait_for_buffer();
                }
                ogc->cga.lastline = ogc->cga.displine;
                /* 80-col */
                if (ogc->cga.cgamode & 1) {
                    /* for each text column */
                    for (x = 0; x < ogc->cga.crtc[1]; x++) {
                        /* video output enabled */
                        if (ogc->cga.cgamode & 8) {
                            /* character */
                            chr = ogc->cga.charbuffer[x << 1];
                            /* text attributes */
                            attr = ogc->cga.charbuffer[(x << 1) + 1];
                        } else
                            chr = attr = 0;
                        /* check if cursor has to be drawn */
                        drawcursor = ((ogc->cga.ma == ca) && ogc->cga.con && ogc->cga.cursoron);
                        /* check if character underline mode should be set */
                        underline = ((ogc->ctrl_3de & 0x40) && (attr & 0x1) && !(attr & 0x6));
                        if (underline) {
                            /* set forecolor to white */
                            attr = attr | 0x7;
                        }
                        blink = 0;
                        /* set foreground */
                        cols[1] = (attr & 15) + 16;
                        /* blink active */
                        if (ogc->cga.cgamode & 0x20) {
                            cols[0] = ((attr >> 4) & 7) + 16;
                            /* attribute 7 active and not cursor */
                            if ((ogc->cga.cgablink & 8) && (attr & 0x80) && !ogc->cga.drawcursor) {
                                /* set blinking */
                                cols[1] = cols[0];
                                blink   = 1;
                            }
                        } else {
                            /* Set intensity bit */
                            cols[0] = (attr >> 4) + 16;
                            blink   = (attr & 0x80) * 8 + 7 + 16;
                        }
                        /* character underline active and 7th row of pixels in character height being drawn */
                        if (underline && (ogc->cga.sc == 7)) {
                            /* for each pixel in character width */
                            for (c = 0; c < 8; c++)
                                buffer32->line[ogc->cga.displine][(x << 3) + c + 8] = mdaattr[attr][blink][1];
                        } else if (drawcursor) {
                            for (c = 0; c < 8; c++)
                                buffer32->line[ogc->cga.displine][(x << 3) + c + 8] = cols[(fontdatm[chr][((ogc->cga.sc & 7) << 1) | ogc->lineff] & (1 << (c ^ 7))) ? 1 : 0] ^ 15;
                        } else {
                            for (c = 0; c < 8; c++)
                                buffer32->line[ogc->cga.displine][(x << 3) + c + 8] = cols[(fontdatm[chr][((ogc->cga.sc & 7) << 1) | ogc->lineff] & (1 << (c ^ 7))) ? 1 : 0];
                        }

                        ogc->cga.ma++;
                    }
                }
                /* 40-col */
                else if (!(ogc->cga.cgamode & 2)) {
                    for (x = 0; x < ogc->cga.crtc[1]; x++) {
                        if (ogc->cga.cgamode & 8) {
                            chr  = ogc->cga.vram[((ogc->cga.ma << 1) & 0x3fff) + ogc->base];
                            attr = ogc->cga.vram[(((ogc->cga.ma << 1) + 1) & 0x3fff) + ogc->base];
                        } else {
                            chr = attr = 0;
                        }
                        drawcursor = ((ogc->cga.ma == ca) && ogc->cga.con && ogc->cga.cursoron);
                        /* check if character underline mode should be set */
                        underline = ((ogc->ctrl_3de & 0x40) && (attr & 0x1) && !(attr & 0x6));
                        if (underline) {
                            /* set forecolor to white */
                            attr = attr | 0x7;
                        }
                        blink = 0;
                        /* set foreground */
                        cols[1] = (attr & 15) + 16;
                        /* blink active */
                        if (ogc->cga.cgamode & 0x20) {
                            cols[0] = ((attr >> 4) & 7) + 16;
                            if ((ogc->cga.cgablink & 8) && (attr & 0x80) && !ogc->cga.drawcursor) {
                                /* set blinking */
                                cols[1] = cols[0];
                                blink   = 1;
                            }
                        } else {
                            /* Set intensity bit */
                            cols[0] = (attr >> 4) + 16;
                            blink   = (attr & 0x80) * 8 + 7 + 16;
                        }

                        /* character underline active and 7th row of pixels in character height being drawn */
                        if (underline && (ogc->cga.sc == 7)) {
                            /* for each pixel in character width */
                            for (c = 0; c < 8; c++)
                                buffer32->line[ogc->cga.displine][(x << 4) + (c << 1) + 8] = buffer32->line[ogc->cga.displine][(x << 4) + (c << 1) + 1 + 8] = mdaattr[attr][blink][1];
                        } else if (drawcursor) {
                            for (c = 0; c < 8; c++)
                                buffer32->line[ogc->cga.displine][(x << 4) + (c << 1) + 8] = buffer32->line[ogc->cga.displine][(x << 4) + (c << 1) + 1 + 8] = cols[(fontdatm[chr][((ogc->cga.sc & 7) << 1) | ogc->lineff] & (1 << (c ^ 7))) ? 1 : 0] ^ 15;
                        } else {
                            for (c = 0; c < 8; c++)
                                buffer32->line[ogc->cga.displine][(x << 4) + (c << 1) + 8] = buffer32->line[ogc->cga.displine][(x << 4) + (c << 1) + 1 + 8] = cols[(fontdatm[chr][((ogc->cga.sc & 7) << 1) | ogc->lineff] & (1 << (c ^ 7))) ? 1 : 0];
                        }

                        ogc->cga.ma++;
                    }
                } else {
                    /* 640x400 mode */
                    if (ogc->ctrl_3de & 1) {
                        dat2    = ((ogc->cga.sc & 1) * 0x4000) | (ogc->lineff * 0x2000);
                        cols[0] = 0;
                        cols[1] = 15 + 16;
                    } else {
                        dat2    = (ogc->cga.sc & 1) * 0x2000;
                        cols[0] = 0;
                        cols[1] = (ogc->cga.cgacol & 15) + 16;
                    }

                    for (x = 0; x < ogc->cga.crtc[1]; x++) {
                        /* video out */
                        if (ogc->cga.cgamode & 8) {
                            dat = (ogc->cga.vram[((ogc->cga.ma << 1) & 0x1fff) + dat2] << 8) | ogc->cga.vram[((ogc->cga.ma << 1) & 0x1fff) + dat2 + 1];
                        } else {
                            dat = 0;
                        }
                        ogc->cga.ma++;

                        for (c = 0; c < 16; c++) {
                            buffer32->line[ogc->cga.displine][(x << 4) + c + 8] = cols[dat >> 15];
                            dat <<= 1;
                        }
                    }
                }
            } else {
                /* ogc specific */
                cols[0] = ((ogc->cga.cgamode & 0x12) == 0x12) ? 0 : (ogc->cga.cgacol & 15) + 16;
                if (ogc->cga.cgamode & 1)
                    hline(buffer32, 0, ogc->cga.displine, ((ogc->cga.crtc[1] << 3) + 16) << 2, cols[0]);
                else
                    hline(buffer32, 0, ogc->cga.displine, ((ogc->cga.crtc[1] << 4) + 16) << 2, cols[0]);
            }

            /* 80 columns */
            if (ogc->cga.cgamode & 1)
                x = (ogc->cga.crtc[1] << 3) + 16;
            else
                x = (ogc->cga.crtc[1] << 4) + 16;

            video_process_8(x, ogc->cga.displine);

            ogc->cga.sc = oldsc;
            if (ogc->cga.vc == ogc->cga.crtc[7] && !ogc->cga.sc)
                ogc->cga.cgastat |= 8;
            ogc->cga.displine++;
            if (ogc->cga.displine >= 720)
                ogc->cga.displine = 0;
        } else {
            timer_advance_u64(&ogc->cga.timer, ogc->cga.dispontime);
            if (ogc->cga.cgadispon)
                ogc->cga.cgastat &= ~1;
            ogc->cga.linepos = 0;
            /* ogc specific */
            ogc->lineff ^= 1;
            if (ogc->lineff) {
                ogc->cga.ma = ogc->cga.maback;
            } else {
                if (ogc->cga.vsynctime) {
                    ogc->cga.vsynctime--;
                    if (!ogc->cga.vsynctime)
                        ogc->cga.cgastat &= ~8;
                }
                if (ogc->cga.sc == (ogc->cga.crtc[11] & 31) || ((ogc->cga.crtc[8] & 3) == 3 && ogc->cga.sc == ((ogc->cga.crtc[11] & 31) >> 1))) {
                    ogc->cga.con  = 0;
                    ogc->cga.coff = 1;
                }
                if ((ogc->cga.crtc[8] & 3) == 3 && ogc->cga.sc == (ogc->cga.crtc[9] >> 1))
                    ogc->cga.maback = ogc->cga.ma;
                if (ogc->cga.vadj) {
                    ogc->cga.sc++;
                    ogc->cga.sc &= 31;
                    ogc->cga.ma = ogc->cga.maback;
                    ogc->cga.vadj--;
                    if (!ogc->cga.vadj) {
                        ogc->cga.cgadispon = 1;
                        ogc->cga.ma = ogc->cga.maback = (ogc->cga.crtc[13] | (ogc->cga.crtc[12] << 8)) & 0x3fff;
                        ogc->cga.sc                   = 0;
                    }
                    // potrebbe dare problemi con composito
                } else if (ogc->cga.sc == ogc->cga.crtc[9] || ((ogc->cga.crtc[8] & 3) == 3 && ogc->cga.sc == (ogc->cga.crtc[9] >> 1))) {
                    ogc->cga.maback = ogc->cga.ma;
                    ogc->cga.sc     = 0;
                    oldvc           = ogc->cga.vc;
                    ogc->cga.vc++;
                    ogc->cga.vc &= 127;

                    if (ogc->cga.vc == ogc->cga.crtc[6])
                        ogc->cga.cgadispon = 0;

                    if (oldvc == ogc->cga.crtc[4]) {
                        ogc->cga.vc   = 0;
                        ogc->cga.vadj = ogc->cga.crtc[5];
                        if (!ogc->cga.vadj) {
                            ogc->cga.cgadispon = 1;
                            ogc->cga.ma = ogc->cga.maback = (ogc->cga.crtc[13] | (ogc->cga.crtc[12] << 8)) & 0x3fff;
                        }
                        switch (ogc->cga.crtc[10] & 0x60) {
                            case 0x20:
                                ogc->cga.cursoron = 0;
                                break;
                            case 0x60:
                                ogc->cga.cursoron = ogc->cga.cgablink & 0x10;
                                break;
                            default:
                                ogc->cga.cursoron = ogc->cga.cgablink & 0x08;
                                break;
                        }
                    }
                    if (ogc->cga.vc == ogc->cga.crtc[7]) {
                        ogc->cga.cgadispon = 0;
                        ogc->cga.displine  = 0;
                        /* ogc specific */
                        ogc->cga.vsynctime = (ogc->cga.crtc[3] >> 4) + 1;
                        if (ogc->cga.crtc[7]) {
                            if (ogc->cga.cgamode & 1)
                                x = (ogc->cga.crtc[1] << 3) + 16;
                            else
                                x = (ogc->cga.crtc[1] << 4) + 16;
                            ogc->cga.lastline++;

                            xs_temp = x;
                            ys_temp = (ogc->cga.lastline - ogc->cga.firstline);

                            if ((xs_temp > 0) && (ys_temp > 0)) {
                                if (xsize < 64)
                                    xs_temp = 656;
                                /* ogc specific */
                                if (ysize < 32)
                                    ys_temp = 200;
                                if (!enable_overscan)
                                    xs_temp -= 16;

                                if ((ogc->cga.cgamode & 8) && ((xs_temp != xsize) || (ys_temp != ysize) || video_force_resize_get())) {
                                    xsize = xs_temp;
                                    ysize = ys_temp;
                                    set_screen_size(xsize, ysize + (enable_overscan ? 16 : 0));

                                    if (video_force_resize_get())
                                        video_force_resize_set(0);
                                }
                                /* ogc specific */
                                if (enable_overscan) {
                                    video_blit_memtoscreen(0, (ogc->cga.firstline - 8),
                                                           xsize, (ogc->cga.lastline - ogc->cga.firstline) + 16);
                                } else {
                                    video_blit_memtoscreen(8, ogc->cga.firstline,
                                                           xsize, (ogc->cga.lastline - ogc->cga.firstline));
                                }
                            }
                            frames++;

                            video_res_x = xsize;
                            video_res_y = ysize;
                            /* 80-col */
                            if (ogc->cga.cgamode & 1) {
                                video_res_x /= 8;
                                video_res_y /= (ogc->cga.crtc[9] + 1) * 2;
                                video_bpp = 0;
                                /* 40-col */
                            } else if (!(ogc->cga.cgamode & 2)) {
                                video_res_x /= 16;
                                video_res_y /= (ogc->cga.crtc[9] + 1) * 2;
                                video_bpp = 0;
                            } else if (!(ogc->ctrl_3de & 1)) {
                                video_res_y /= 2;
                                video_bpp = 1;
                            }
                        }
                        ogc->cga.firstline = 1000;
                        ogc->cga.lastline  = 0;
                        ogc->cga.cgablink++;
                        ogc->cga.oddeven ^= 1;
                    }
                } else {
                    ogc->cga.sc++;
                    ogc->cga.sc &= 31;
                    ogc->cga.ma = ogc->cga.maback;
                }

                if (ogc->cga.cgadispon)
                    ogc->cga.cgastat &= ~1;

                if (ogc->cga.sc == (ogc->cga.crtc[10] & 31) || ((ogc->cga.crtc[8] & 3) == 3 && ogc->cga.sc == ((ogc->cga.crtc[10] & 31) >> 1)))
                    ogc->cga.con = 1;
            }
            /* 80-columns */
            if (ogc->cga.cgadispon && (ogc->cga.cgamode & 1)) {
                for (x = 0; x < (ogc->cga.crtc[1] << 1); x++)
                    ogc->cga.charbuffer[x] = ogc->cga.vram[(((ogc->cga.ma << 1) + x) & 0x3fff) + ogc->base];
            }
        }
    }
}

void
ogc_close(void *priv)
{
    ogc_t *ogc = (ogc_t *) priv;

    free(ogc->cga.vram);
    free(ogc);
}

void
ogc_speed_changed(void *priv)
{
    ogc_t *ogc = (ogc_t *) priv;

    ogc_recalctimings(ogc);
}

void
ogc_mdaattr_rebuild(void)
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

/*
 * Missing features
 * - Composite video mode not working
 * - Optional EGC expansion board (which handles 640x400x16) not implemented
 */
void *
ogc_init(UNUSED(const device_t *info))
{
#if 0
    int display_type;
#endif
    ogc_t *ogc = (ogc_t *) malloc(sizeof(ogc_t));

    memset(ogc, 0x00, sizeof(ogc_t));
    video_inform(VIDEO_FLAG_TYPE_CGA, &timing_ogc);

    loadfont("roms/video/ogc/ogc graphics board go380 258 pqbq.bin", 1);

    /* FIXME: composite is not working yet */
#if 0
    display_type = device_get_config_int("display_type");
#endif
    ogc->cga.composite    = 0; // (display_type != CGA_RGB);
    ogc->cga.revision     = device_get_config_int("composite_type");
    ogc->cga.snow_enabled = device_get_config_int("snow_enabled");

    ogc->cga.vram = malloc(0x8000);

    cga_comp_init(ogc->cga.revision);
    timer_add(&ogc->cga.timer, ogc_poll, ogc, 1);
    mem_mapping_add(&ogc->cga.mapping, 0xb8000, 0x08000,
                    ogc_read, NULL, NULL,
                    ogc_write, NULL, NULL, NULL, 0, ogc);
    io_sethandler(0x03d0, 16, ogc_in, NULL, NULL, ogc_out, NULL, NULL, ogc);

    overscan_x = overscan_y = 16;
    ogc->cga.rgb_type       = device_get_config_int("rgb_type");
    cga_palette             = (ogc->cga.rgb_type << 1);
    cgapal_rebuild();
    ogc_mdaattr_rebuild();

    /* color display */
    if (device_get_config_int("rgb_type") == 0 || device_get_config_int("rgb_type") == 4)
        ogc->mono_display = 0;
    else
        ogc->mono_display = 1;

    ogc->ctrl_addr = 0x3de;

    return ogc;
}

const device_config_t ogc_m24_config[] = {
  // clang-format off
    {
        /* Olivetti / ATT compatible displays */
        .name           = "rgb_type",
        .description    = "RGB type",
        .type           = CONFIG_SELECTION,
        .default_string = NULL,
        .default_int    = CGA_RGB,
        .file_filter    = NULL,
        .spinner        = { 0 },
        .selection      = {
            { .description = "Color",            .value = 0 },
            { .description = "Green Monochrome", .value = 1 },
            { .description = "Amber Monochrome", .value = 2 },
            { .description = "Gray Monochrome",  .value = 3 },
            { .description = ""                             }
        },
        .bios           = { { 0 } }
    },
    {
        .name           = "snow_enabled",
        .description    = "Snow emulation",
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

const device_t ogc_m24_device = {
    .name          = "Olivetti M21/M24/M28 (GO317/318/380/709) video card",
    .internal_name = "ogc_m24",
    .flags         = DEVICE_ISA,
    .local         = 0,
    .init          = ogc_init,
    .close         = ogc_close,
    .reset         = NULL,
    .available     = NULL,
    .speed_changed = ogc_speed_changed,
    .force_redraw  = NULL,
    .config        = ogc_m24_config
};

const device_t ogc_device = {
    .name          = "Olivetti OGC (GO708)",
    .internal_name = "ogc",
    .flags         = DEVICE_ISA,
    .local         = 0,
    .init          = ogc_init,
    .close         = ogc_close,
    .reset         = NULL,
    .available     = NULL,
    .speed_changed = ogc_speed_changed,
    .force_redraw  = NULL,
    .config        = cga_config
};
