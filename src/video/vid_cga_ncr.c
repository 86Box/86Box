/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          Emulation of the NCR NGA (K511, K201) video cards.
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
#include "cpu.h"
#include <86box/timer.h>
#include <86box/mem.h>
#include <86box/pit.h>
#include <86box/rom.h>
#include <86box/device.h>
#include <86box/vid_cga.h>
#include <86box/vid_nga.h>
#include <86box/vid_cga_comp.h>
#include <86box/plat_unused.h>

#define CGA_RGB       0
#define CGA_COMPOSITE 1

#define COMPOSITE_OLD 0
#define COMPOSITE_NEW 1

static video_timings_t timing_nga = { .type = VIDEO_ISA, .write_b = 8, .write_w = 16, .write_l = 32, .read_b = 8, .read_w = 16, .read_l = 32 };

void
nga_recalctimings(nga_t *nga)
{
    double _dispontime;
    double _dispofftime;
    double disptime;

    if (nga->cga.cgamode & CGA_MODE_FLAG_HIGHRES) {
        disptime    = nga->cga.crtc[CGA_CRTC_HTOTAL] + 1;
        _dispontime = nga->cga.crtc[CGA_CRTC_HDISP];
    } else {
        disptime    = (nga->cga.crtc[CGA_CRTC_HTOTAL] + 1) << 1;
        _dispontime = nga->cga.crtc[CGA_CRTC_HDISP] << 1;
    }

    _dispofftime = disptime - _dispontime;
    _dispontime *= CGACONST / 2;
    _dispofftime *= CGACONST / 2;
    nga->cga.dispontime  = (uint64_t) (_dispontime);
    nga->cga.dispofftime = (uint64_t) (_dispofftime);
}

void
nga_out(uint16_t addr, uint8_t val, void *priv)
{
    nga_t *nga = (nga_t *) priv;

    cga_out(addr, val, &nga->cga);
}

uint8_t
nga_in(uint16_t addr, void *priv)
{
    nga_t *nga = (nga_t *) priv;

    return cga_in(addr, &nga->cga);
}

void
nga_waitstates(UNUSED(void *priv))
{
    int ws_array[16] = { 3, 4, 5, 6, 7, 8, 4, 5, 6, 7, 8, 4, 5, 6, 7, 8 };
    int ws;

    ws = ws_array[cycles & 0xf];

    if (is_nec)
        sub_cycles_vx0(ws);
    else
        sub_cycles(ws);
}

void
nga_write(uint32_t addr, uint8_t val, void *priv)
{
    nga_t *nga = (nga_t *) priv;
    int    offset;
    /* a8000-affff */
    if (!(addr & 0x10000))
        nga->vram_64k[addr & 0x7FFF] = val;
    /* b8000-bffff */
    else
        nga->cga.vram[addr & 0x7FFF] = val;

    if (nga->cga.snow_enabled) {
        /* recreate snow effect */
        offset                          = ((timer_get_remaining_u64(&nga->cga.timer) / CGACONST) * 4) & 0xfc;
        nga->cga.charbuffer[offset]     = nga->cga.vram[addr & 0x7fff];
        nga->cga.charbuffer[offset | 1] = nga->cga.vram[addr & 0x7fff];
    }
    nga_waitstates(&nga->cga);
}

uint8_t
nga_read(uint32_t addr, void *priv)
{

    nga_t  *nga = (nga_t *) priv;
    int     offset;
    uint8_t ret;
    /* a8000-affff */
    if (!(addr & 0x10000))
        ret = nga->vram_64k[addr & 0x7FFF];
    else
        ret = nga->cga.vram[addr & 0x7FFF];

    nga_waitstates(&nga->cga);

    if (nga->cga.snow_enabled) {
        /* recreate snow effect */
        offset                          = ((timer_get_remaining_u64(&nga->cga.timer) / CGACONST) * 4) & 0xfc;
        nga->cga.charbuffer[offset]     = nga->cga.vram[addr & 0x7fff];
        nga->cga.charbuffer[offset | 1] = nga->cga.vram[addr & 0x7fff];
    }

    return ret;
}

void
nga_poll(void *priv)
{
    nga_t *nga = (nga_t *) priv;
    /* set cursor position in memory */
    uint16_t cursoraddr = (nga->cga.crtc[CGA_CRTC_CURSOR_ADDR_LOW] | (nga->cga.crtc[CGA_CRTC_CURSOR_ADDR_HIGH] << 8)) & 0x3fff;
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
    int      col;
    int      scanline_old;

    /* graphic mode and not high-res modes */
    if ((nga->cga.cgamode & CGA_MODE_FLAG_GRAPHICS) && !(nga->cga.cgamode & 0x40)) {
        /* standard cga mode */
        cga_poll(&nga->cga);
        return;
    } else {
        /* high-res or text mode */
        if (!nga->cga.linepos) {
            timer_advance_u64(&nga->cga.timer, nga->cga.dispofftime);
            nga->cga.cgastat |= 1;
            nga->cga.linepos = 1;
            scanline_old            = nga->cga.scanline;
            /* if interlaced */
            if ((nga->cga.crtc[CGA_CRTC_INTERLACE] & 3) == 3)
                nga->cga.scanline = ((nga->cga.scanline << 1) + nga->cga.oddeven) & 7;
            if (nga->cga.cgadispon) {
                if (nga->cga.displine < nga->cga.firstline) {
                    nga->cga.firstline = nga->cga.displine;
                    video_wait_for_buffer();
                }
                nga->cga.lastline = nga->cga.displine;
                /* 80-col */
                if ((nga->cga.cgamode & CGA_MODE_FLAG_HIGHRES) && !(nga->cga.cgamode & CGA_MODE_FLAG_GRAPHICS)) {
                    /* for each text column */
                    for (x = 0; x < nga->cga.crtc[CGA_CRTC_HDISP]; x++) {
                        /* video output enabled */
                        if (nga->cga.cgamode & CGA_MODE_FLAG_VIDEO_ENABLE) {
                            /* character */
                            chr = nga->cga.charbuffer[x << 1];
                            /* text attributes */
                            attr = nga->cga.charbuffer[(x << 1) + 1];
                        } else
                            chr = attr = 0;
                        /* check if cursor has to be drawn */
                        drawcursor = ((nga->cga.memaddr == cursoraddr) && nga->cga.cursorvisible && nga->cga.cursoron);
                        /* set foreground */
                        cols[1] = (attr & 15) + 16;
                        /* blink active */
                        if (nga->cga.cgamode & CGA_MODE_FLAG_BLINK) {
                            cols[0] = ((attr >> 4) & 7) + 16;
                            /* attribute 7 active and not cursor */
                            if ((nga->cga.cgablink & 8) && (attr & 0x80) && !nga->cga.drawcursor) {
                                /* set blinking */
                                cols[1] = cols[0];
                            }
                        } else {
                            /* Set intensity bit */
                            cols[0] = (attr >> 4) + 16;
                        }
                        if (drawcursor) {
                            for (c = 0; c < 8; c++)
                                buffer32->line[nga->cga.displine][(x << 3) + c + 8] = cols[(fontdatm[chr][((nga->cga.scanline & 7) << 1) | nga->lineff] & (1 << (c ^ 7))) ? 1 : 0] ^ 15;
                        } else {
                            for (c = 0; c < 8; c++)
                                buffer32->line[nga->cga.displine][(x << 3) + c + 8] = cols[(fontdatm[chr][((nga->cga.scanline & 7) << 1) | nga->lineff] & (1 << (c ^ 7))) ? 1 : 0];
                        }

                        nga->cga.memaddr++;
                    }
                }
                /* 40-col */
                else if (!(nga->cga.cgamode & CGA_MODE_FLAG_GRAPHICS)) {
                    /* for each text column */
                    for (x = 0; x < nga->cga.crtc[CGA_CRTC_HDISP]; x++) {
                        if (nga->cga.cgamode & CGA_MODE_FLAG_VIDEO_ENABLE) {
                            chr  = nga->cga.vram[((nga->cga.memaddr << 1) & 0x3fff) + nga->base];
                            attr = nga->cga.vram[(((nga->cga.memaddr << 1) + 1) & 0x3fff) + nga->base];
                        } else {
                            chr = attr = 0;
                        }
                        drawcursor = ((nga->cga.memaddr == cursoraddr) && nga->cga.cursorvisible && nga->cga.cursoron);
                        /* set foreground */
                        cols[1] = (attr & 15) + 16;
                        /* blink active */
                        if (nga->cga.cgamode & CGA_MODE_FLAG_BLINK) {
                            cols[0] = ((attr >> 4) & 7) + 16;
                            if ((nga->cga.cgablink & 8) && (attr & 0x80) && !nga->cga.drawcursor) {
                                /* set blinking */
                                cols[1] = cols[0];
                            }
                        } else {
                            /* Set intensity bit */
                            cols[0] = (attr >> 4) + 16;
                        }

                        if (drawcursor) {
                            for (c = 0; c < 8; c++)
                                buffer32->line[nga->cga.displine][(x << 4) + (c << 1) + 8] = buffer32->line[nga->cga.displine][(x << 4) + (c << 1) + 1 + 8] = cols[(fontdatm[chr][((nga->cga.scanline & 7) << 1) | nga->lineff] & (1 << (c ^ 7))) ? 1 : 0] ^ 15;
                        } else {
                            for (c = 0; c < 8; c++)
                                buffer32->line[nga->cga.displine][(x << 4) + (c << 1) + 8] = buffer32->line[nga->cga.displine][(x << 4) + (c << 1) + 1 + 8] = cols[(fontdatm[chr][((nga->cga.scanline & 7) << 1) | nga->lineff] & (1 << (c ^ 7))) ? 1 : 0];
                        }

                        nga->cga.memaddr++;
                    }
                } else {
                    /* high res modes */
                    if (nga->cga.cgamode & 0x40) {
                        /* 640x400x2 mode */
                        if (nga->cga.cgamode & 0x4 || nga->cga.cgamode & CGA_MODE_FLAG_HIGHRES_GRAPHICS) {
                            /*
                             * Scanlines are read in the following order:
                             * 0b8000-0b9f3f even scans (0,4,...)
                             * 0ba000-0bbf3f odd scans (2,6,...)
                             * 0bc000-0bdf3f even scans (1,5,...)
                             * 0be000-0bff3f odd scans (3,7,...)
                             */
                            dat2    = ((nga->cga.scanline & 1) * 0x2000) | (nga->lineff * 0x4000);
                            cols[0] = 0;
                            cols[1] = 15 + 16;
                            /* 640x400x4 mode */
                        } else {
                            cols[0] = (nga->cga.cgacol & 15) | 16;
                            col     = (nga->cga.cgacol & 16) ? 24 : 16;
                            if (nga->cga.cgamode & CGA_MODE_FLAG_BW) {
                                cols[1] = col | 3; /* Cyan */
                                cols[2] = col | 4; /* Red */
                                cols[3] = col | 7; /* White */
                            } else if (nga->cga.cgacol & 32) {
                                cols[1] = col | 3; /* Cyan */
                                cols[2] = col | 5; /* Magenta */
                                cols[3] = col | 7; /* White */
                            } else {
                                cols[1] = col | 2; /* Green */
                                cols[2] = col | 4; /* Red */
                                cols[3] = col | 6; /* Yellow */
                            }
                            /*
                             * Scanlines are read in the following order:
                             * 0b8000-0bbf3f even scans (0,4,...)
                             * 0bc000-0bff3f odd scans (1,5,...)
                             * 0a8000-0abf3f even scans (2,6,...)
                             * 0ac000-0aff3f odd scans (3,7,...)
                             */
                            dat2 = (nga->cga.scanline & 1) * 0x4000;
                        }
                    } else {
                        dat2    = (nga->cga.scanline & 1) * 0x2000;
                        cols[0] = 0;
                        cols[1] = (nga->cga.cgacol & 15) + 16;
                    }

                    /* for each text column */
                    for (x = 0; x < nga->cga.crtc[CGA_CRTC_HDISP]; x++) {
                        /* video out */
                        if (nga->cga.cgamode & CGA_MODE_FLAG_VIDEO_ENABLE) {
                            /* 640x400x2 */
                            if (nga->cga.cgamode & 0x4 || nga->cga.cgamode & CGA_MODE_FLAG_HIGHRES_GRAPHICS) {
                                /* read two bytes at a time */
                                dat = (nga->cga.vram[((nga->cga.memaddr << 1) & 0x1fff) + dat2] << 8) | nga->cga.vram[((nga->cga.memaddr << 1) & 0x1fff) + dat2 + 1];
                                /* each pixel is represented by one bit, so draw 16 pixels at a time */
                                /* crtc[1] is 40 column, so 40x16=640 pixels */
                                for (c = 0; c < 16; c++) {
                                    buffer32->line[nga->cga.displine][(x << 4) + c + 8] = cols[dat >> 15];
                                    dat <<= 1;
                                }
                                /* 640x400x4 */
                            } else {
                                /* lines 2,3,6,7,etc. */
                                if (nga->cga.scanline & 2)
                                    /* read two bytes at a time */
                                    dat = (nga->vram_64k[((nga->cga.memaddr << 1) & 0x7fff) + dat2] << 8) | nga->vram_64k[((nga->cga.memaddr << 1) & 0x7fff) + dat2 + 1];
                                /* lines 0,1,4,5,etc. */
                                else
                                    /* read two bytes at a time */
                                    dat = (nga->cga.vram[((nga->cga.memaddr << 1) & 0x7fff) + dat2] << 8) | nga->cga.vram[((nga->cga.memaddr << 1) & 0x7fff) + dat2 + 1];
                                /* each pixel is represented by two bits, so draw 8 pixels at a time */
                                /* crtc[1] is 80 column, so 80x8=640 pixels */
                                for (c = 0; c < 8; c++) {
                                    buffer32->line[nga->cga.displine][(x << 3) + c + 8] = cols[dat >> 14];
                                    dat <<= 2;
                                }
                            }
                        } else {
                            dat = 0;
                        }
                        nga->cga.memaddr++;
                    }
                }
            } else {

                /* nga specific */
                cols[0] = ((nga->cga.cgamode & 0x12) == 0x12) ? 0 : (nga->cga.cgacol & 15) + 16;
                /* 80-col */
                if (nga->cga.cgamode & CGA_MODE_FLAG_HIGHRES) {
                    hline(buffer32, 0, (nga->cga.displine << 1), ((nga->cga.crtc[CGA_CRTC_HDISP] << 3) + 16) << 2, cols[0]);
                    hline(buffer32, 0, (nga->cga.displine << 1) + 1, ((nga->cga.crtc[CGA_CRTC_HDISP] << 3) + 16) << 2, cols[0]);
                } else {
                    hline(buffer32, 0, (nga->cga.displine << 1), ((nga->cga.crtc[CGA_CRTC_HDISP] << 4) + 16) << 2, cols[0]);
                    hline(buffer32, 0, (nga->cga.displine << 1) + 1, ((nga->cga.crtc[CGA_CRTC_HDISP] << 4) + 16) << 2, cols[0]);
                }
            }

            if (nga->cga.cgamode & CGA_MODE_FLAG_HIGHRES)
                /* set screen width */
                x = (nga->cga.crtc[CGA_CRTC_HDISP] << 3) + 16;
            else
                x = (nga->cga.crtc[CGA_CRTC_HDISP] << 4) + 16;

            video_process_8(x, nga->cga.displine);

            nga->cga.scanline = scanline_old;
            /* vertical sync */
            if (nga->cga.vc == nga->cga.crtc[CGA_CRTC_VSYNC] && !nga->cga.scanline)
                nga->cga.cgastat |= 8;
            nga->cga.displine++;
            if (nga->cga.displine >= 720)
                nga->cga.displine = 0;
        } else {
            timer_advance_u64(&nga->cga.timer, nga->cga.dispontime);
            if (nga->cga.cgadispon)
                nga->cga.cgastat &= ~1;
            nga->cga.linepos = 0;
            /* nga specific */
            nga->lineff ^= 1;

            /* text mode or 640x400x2 */
            if (nga->lineff && !((nga->cga.cgamode & CGA_MODE_FLAG_HIGHRES) && (nga->cga.cgamode & 0x40))) {
                nga->cga.memaddr = nga->cga.memaddr_backup;
                /* 640x400x4 */
            } else {
                if (nga->cga.vsynctime) {
                    nga->cga.vsynctime--;
                    if (!nga->cga.vsynctime)
                        nga->cga.cgastat &= ~8;
                }
                /* cursor stop scanline */
                if (nga->cga.scanline == (nga->cga.crtc[CGA_CRTC_CURSOR_END] & 31) || ((nga->cga.crtc[CGA_CRTC_INTERLACE] & 3) == 3 && nga->cga.scanline == ((nga->cga.crtc[CGA_CRTC_CURSOR_END] & 31) >> 1))) {
                    nga->cga.cursorvisible  = 0;
                }
                /* interlaced and max scanline per char reached */
                if ((nga->cga.crtc[CGA_CRTC_INTERLACE] & 3) == 3 && nga->cga.scanline == (nga->cga.crtc[CGA_CRTC_MAX_SCANLINE_ADDR] >> 1))
                    nga->cga.memaddr_backup = nga->cga.memaddr;

                if (nga->cga.vadj) {
                    nga->cga.scanline++;
                    nga->cga.scanline &= 31;
                    nga->cga.memaddr = nga->cga.memaddr_backup;
                    nga->cga.vadj--;
                    if (!nga->cga.vadj) {
                        nga->cga.cgadispon = 1;
                        /* change start of displayed page (crtc 12-13) */
                        nga->cga.memaddr = nga->cga.memaddr_backup = (nga->cga.crtc[CGA_CRTC_START_ADDR_LOW] | (nga->cga.crtc[CGA_CRTC_START_ADDR_HIGH] << 8)) & 0x7fff;
                        nga->cga.scanline                   = 0;
                    }
                    /* nga specific */
                    /* end of character line reached */
                } else if (nga->cga.scanline == nga->cga.crtc[CGA_CRTC_MAX_SCANLINE_ADDR] || ((nga->cga.crtc[CGA_CRTC_INTERLACE] & 3) == 3 && nga->cga.scanline == (nga->cga.crtc[CGA_CRTC_MAX_SCANLINE_ADDR] >> 1))) {
                    nga->cga.memaddr_backup = nga->cga.memaddr;
                    nga->cga.scanline     = 0;
                    oldvc           = nga->cga.vc;
                    nga->cga.vc++;
                    nga->cga.vc &= 127;

                    /* lines of character displayed */
                    if (nga->cga.vc == nga->cga.crtc[CGA_CRTC_VDISP])
                        nga->cga.cgadispon = 0;

                    /* total vertical lines */
                    if (oldvc == nga->cga.crtc[CGA_CRTC_VTOTAL]) {
                        nga->cga.vc = 0;
                        /* adjust vertical lines */
                        nga->cga.vadj = nga->cga.crtc[CGA_CRTC_VTOTAL_ADJUST];
                        if (!nga->cga.vadj) {
                            nga->cga.cgadispon = 1;
                            /* change start of displayed page (crtc 12-13) */
                            nga->cga.memaddr = nga->cga.memaddr_backup = (nga->cga.crtc[CGA_CRTC_START_ADDR_LOW] | (nga->cga.crtc[CGA_CRTC_START_ADDR_HIGH] << 8)) & 0x7fff;
                        }
                        /* cursor start */
                        switch (nga->cga.crtc[CGA_CRTC_CURSOR_START] & 0x60) {
                            case 0x20:
                                nga->cga.cursoron = 0;
                                break;
                            case 0x60:
                                nga->cga.cursoron = nga->cga.cgablink & 0x10;
                                break;
                            default:
                                nga->cga.cursoron = nga->cga.cgablink & 0x08;
                                break;
                        }
                    }
                    /* vertical line position */
                    if (nga->cga.vc == nga->cga.crtc[CGA_CRTC_VSYNC]) {
                        nga->cga.cgadispon = 0;
                        nga->cga.displine  = 0;
                        /* nga specific */
                        nga->cga.vsynctime = 16;
                        /* vsync pos */
                        if (nga->cga.crtc[CGA_CRTC_VSYNC]) {
                            if (nga->cga.cgamode & CGA_MODE_FLAG_HIGHRES)
                                /* set screen width */
                                x = (nga->cga.crtc[CGA_CRTC_HDISP] << 3) + 16;
                            else
                                x = (nga->cga.crtc[CGA_CRTC_HDISP] << 4) + 16;
                            nga->cga.lastline++;

                            xs_temp = x;
                            ys_temp = (nga->cga.lastline - nga->cga.firstline);

                            if ((xs_temp > 0) && (ys_temp > 0)) {
                                if (xsize < 64)
                                    xs_temp = 656;
                                /* nga specific */
                                if (ysize < 32)
                                    ys_temp = 400;
                                if (!enable_overscan)
                                    xs_temp -= 16;

                                if ((nga->cga.cgamode & CGA_MODE_FLAG_VIDEO_ENABLE) && ((xs_temp != xsize) || (ys_temp != ysize) || video_force_resize_get())) {
                                    xsize = xs_temp;
                                    ysize = ys_temp;
                                    set_screen_size(xsize, ysize + (enable_overscan ? 16 : 0));

                                    if (video_force_resize_get())
                                        video_force_resize_set(0);
                                }
                                /* nga specific */
                                if (enable_overscan) {
                                    video_blit_memtoscreen(0, (nga->cga.firstline - 8),
                                                           xsize, (nga->cga.lastline - nga->cga.firstline) + 16);
                                } else {
                                    video_blit_memtoscreen(8, nga->cga.firstline,
                                                           xsize, (nga->cga.lastline - nga->cga.firstline));
                                }
                            }
                            frames++;

                            video_res_x = xsize;
                            video_res_y = ysize;
                            /* 80-col */
                            if ((nga->cga.cgamode & CGA_MODE_FLAG_HIGHRES) && !(nga->cga.cgamode & 0x40)) {
                                video_res_x /= 8;
                                video_res_y /= (nga->cga.crtc[CGA_CRTC_MAX_SCANLINE_ADDR] + 1) * 2;
                                video_bpp = 0;
                                /* 40-col */
                            } else if (!(nga->cga.cgamode & CGA_MODE_FLAG_GRAPHICS)) {
                                video_res_x /= 16;
                                video_res_y /= (nga->cga.crtc[CGA_CRTC_MAX_SCANLINE_ADDR] + 1) * 2;
                                video_bpp = 0;
                            } else if (nga->cga.cgamode & 0x40) {
                                video_res_x /= 8;
                                video_res_y /= 2;
                                video_bpp = 1;
                            }
                        }
                        nga->cga.firstline = 1000;
                        nga->cga.lastline  = 0;
                        nga->cga.cgablink++;
                        nga->cga.oddeven ^= 1;
                    }
                } else {
                    nga->cga.scanline++;
                    nga->cga.scanline &= 31;
                    nga->cga.memaddr = nga->cga.memaddr_backup;
                }

                if (nga->cga.cgadispon)
                    nga->cga.cgastat &= ~1;

                /* enable cursor if its scanline was reached */
                if (nga->cga.scanline == (nga->cga.crtc[CGA_CRTC_CURSOR_START] & 31) || ((nga->cga.crtc[CGA_CRTC_INTERLACE] & 3) == 3 && nga->cga.scanline == ((nga->cga.crtc[CGA_CRTC_CURSOR_START] & 31) >> 1)))
                    nga->cga.cursorvisible = 1;
            }
            /* 80-columns */
            if (nga->cga.cgadispon && (nga->cga.cgamode & CGA_MODE_FLAG_HIGHRES)) {
                /* for each character per line */
                for (x = 0; x < (nga->cga.crtc[CGA_CRTC_HDISP] << 1); x++)
                    nga->cga.charbuffer[x] = nga->cga.vram[(((nga->cga.memaddr << 1) + x) & 0x3fff) + nga->base];
            }
        }
    }
}

void
nga_close(void *priv)
{
    nga_t *nga = (nga_t *) priv;
    free(nga->vram_64k);
    free(nga->cga.vram);
    free(nga);
}

void
nga_speed_changed(void *priv)
{
    nga_t *nga = (nga_t *) priv;

    nga_recalctimings(nga);
}

void *
nga_init(UNUSED(const device_t *info))
{
    int     mem;
    uint8_t charset;
    nga_t  *nga = (nga_t *) malloc(sizeof(nga_t));

    memset(nga, 0x00, sizeof(nga_t));
    video_inform(VIDEO_FLAG_TYPE_CGA, &timing_nga);

    charset = device_get_config_int("charset");

    video_load_font("roms/video/nga/ncr_nga_35122.bin", 1, 4096 * charset);

    nga->cga.composite    = 0;
    nga->cga.snow_enabled = device_get_config_int("snow_enabled");

    nga->cga.vram = malloc(0x8000);
    nga->vram_64k = malloc(0x8000);

    timer_add(&nga->cga.timer, nga_poll, nga, 1);
    mem_mapping_add(&nga->cga.mapping, 0xb8000, 0x8000,
                    nga_read, NULL, NULL,
                    nga_write, NULL, NULL, NULL, 0, nga);

    mem = device_get_config_int("memory");

    if (mem > 32) {
        /* make optional 32KB addessable */
        mem_mapping_add(&nga->mapping_64k, 0xa8000, 0x8000,
                        nga_read, NULL, NULL,
                        nga_write, NULL, NULL, NULL, 0, nga);
    }

    io_sethandler(0x03d0, 16, nga_in, NULL, NULL, nga_out, NULL, NULL, nga);

    overscan_x = overscan_y = 16;
    nga->cga.rgb_type       = device_get_config_int("rgb_type");
    cga_palette             = (nga->cga.rgb_type << 1);
    cgapal_rebuild();

    return nga;
}

const device_config_t nga_config[] = {
  // clang-format off
    {
        .name           = "rgb_type",
        .description    = "RGB type",
        .type           = CONFIG_SELECTION,
        .default_string = NULL,
        .default_int    = 0,
        .file_filter    = NULL,
        .spinner        = { 0 },
        .selection      = {
            { .description = "Color",            .value = 0 },
            { .description = "Green Monochrome", .value = 1 },
            { .description = "Amber Monochrome", .value = 2 },
            { .description = "Gray Monochrome",  .value = 3 },
            { .description = "Color (no brown)", .value = 4 },
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
    {
        .name           = "memory",
        .description    = "Memory size",
        .type           = CONFIG_SELECTION,
        .default_string = NULL,
        .default_int    = 64,
        .file_filter    = NULL,
        .spinner        = { 0 },
        .selection      = {
            { .description = "32 KB", .value = 32 },
            { .description = "64 KB", .value = 64 },
            { .description = ""                   }
        },
        .bios           = { { 0 } }
    },
    {
        .name           = "charset",
        .description    = "Character set",
        .type           = CONFIG_SELECTION,
        .default_string = NULL,
        .default_int    = 0,
        .file_filter    = NULL,
        .spinner        = { 0 },
        .selection      = {
            { .description = "U.S. English",    .value = 0 },
            { .description = "Scandinavian",    .value = 1 },
            { .description = "Other languages", .value = 2 },
            { .description = "E.F. Hutton",     .value = 3 },
            { .description = ""                            }
        },
        .bios           = { { 0 } }
    },
    { .name = "", .description = "", .type = CONFIG_END }
  // clang-format on
};

const device_t nga_device = {
    .name          = "NCR NGA",
    .internal_name = "nga",
    .flags         = DEVICE_ISA,
    .local         = 0,
    .init          = nga_init,
    .close         = nga_close,
    .reset         = NULL,
    .available     = NULL,
    .speed_changed = nga_speed_changed,
    .force_redraw  = NULL,
    .config        = nga_config
};
