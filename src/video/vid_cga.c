/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          Emulation of the old and new IBM CGA graphics cards.
 *
 *
 *
 * Authors: Sarah Walker, <https://pcem-emulator.co.uk/>
 *          Miran Grca, <mgrca8@gmail.com>
 *          W. M. Martinez, <anikom15@outlook.com>
 *
 *          Copyright 2008-2019 Sarah Walker.
 *          Copyright 2016-2019 Miran Grca.
 *          Copyright 2023 W. M. Martinez
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
#include <86box/video.h>
#include <86box/vid_cga.h>
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

typedef union
{
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

static video_timings_t timing_cga = { .type = VIDEO_ISA, .write_b = 8, .write_w = 16, .write_l = 32, .read_b = 8, .read_w = 16, .read_l = 32 };

void cga_recalctimings(cga_t *cga);

static void
cga_update_latch(cga_t *cga)
{
    uint32_t lp_latch = cga->displine * cga->crtc[1];

    cga->crtc[0x10] = (lp_latch >> 8) & 0x3f;
    cga->crtc[0x11] = lp_latch & 0xff;
}

void
cga_out(uint16_t addr, uint8_t val, void *priv)
{
    cga_t  *cga = (cga_t *) priv;
    uint8_t old;

    if ((addr >= 0x3d0) && (addr <= 0x3d7))
        addr = (addr & 0xff9) | 0x004;

    switch (addr) {
        case 0x3D4:
            cga->crtcreg = val & 31;
            return;
        case 0x3D5:
            old                     = cga->crtc[cga->crtcreg];
            cga->crtc[cga->crtcreg] = val & crtcmask[cga->crtcreg];
            if (old != val) {
                if ((cga->crtcreg < 0xe) || (cga->crtcreg > 0x11)) {
                    cga->fullchange = changeframecount;
                    cga_recalctimings(cga);
                }
            }
            return;
        case 0x3D8:
            old          = cga->cgamode;
            cga->cgamode = val;

            if (old ^ val) {
                if ((old ^ val) & 0x07)
                    update_cga16_color(val);

                cga_recalctimings(cga);
            }
            return;
        case 0x3D9:
            old         = cga->cgacol;
            cga->cgacol = val;
            if (old ^ val)
                cga_recalctimings(cga);
            return;

        case 0x3DB:
            if (cga->lp_strobe == 1)
                cga->lp_strobe = 0;
            return;
        case 0x3DC:
            if (cga->lp_strobe == 0) {
                cga->lp_strobe = 1;
                cga_update_latch(cga);
            }
            return;

        default:
            break;
    }
}

uint8_t
cga_in(uint16_t addr, void *priv)
{
    cga_t  *cga = (cga_t *) priv;
    uint8_t ret = 0xff;

    if ((addr >= 0x3d0) && (addr <= 0x3d7))
        addr = (addr & 0xff9) | 0x004;

    switch (addr) {
        case 0x3D4:
            ret = cga->crtcreg;
            break;
        case 0x3D5:
            ret = cga->crtc[cga->crtcreg];
            break;
        case 0x3DA:
            ret = cga->cgastat;
            break;

        case 0x3DB:
            if (cga->lp_strobe == 1)
                cga->lp_strobe = 0;
            break;
        case 0x3DC:
            if (cga->lp_strobe == 0) {
                cga->lp_strobe = 1;
                cga_update_latch(cga);
            }
            break;

        default:
            break;
    }

    return ret;
}

void
cga_pravetz_out(UNUSED(uint16_t addr), uint8_t val, void *priv)
{
    cga_t *cga = (cga_t *) priv;

    cga->fontbase = (((unsigned int) val) << 8);
}

uint8_t
cga_pravetz_in(UNUSED(uint16_t addr), void *priv)
{
    const cga_t *cga = (cga_t *) priv;

    return (cga->fontbase >> 8);
}

void
cga_waitstates(UNUSED(void *priv))
{
    int ws_array[16] = { 3, 4, 5, 6, 7, 8, 4, 5, 6, 7, 8, 4, 5, 6, 7, 8 };
    int ws;

    ws = ws_array[cycles & 0xf];
    cycles -= ws;
}

void
cga_write(uint32_t addr, uint8_t val, void *priv)
{
    cga_t *cga = (cga_t *) priv;

    cga->vram[addr & 0x3fff] = val;
    if (cga->snow_enabled) {
        int offset                  = ((timer_get_remaining_u64(&cga->timer) / CGACONST) * 2) & 0xfc;
        cga->charbuffer[offset]     = cga->vram[addr & 0x3fff];
        cga->charbuffer[offset | 1] = cga->vram[addr & 0x3fff];
    }
    cga_waitstates(cga);
}

uint8_t
cga_read(uint32_t addr, void *priv)
{
    cga_t *cga = (cga_t *) priv;

    cga_waitstates(cga);
    if (cga->snow_enabled) {
        int offset                  = ((timer_get_remaining_u64(&cga->timer) / CGACONST) * 2) & 0xfc;
        cga->charbuffer[offset]     = cga->vram[addr & 0x3fff];
        cga->charbuffer[offset | 1] = cga->vram[addr & 0x3fff];
    }
    return cga->vram[addr & 0x3fff];
}

void
cga_recalctimings(cga_t *cga)
{
    double disptime;
    double _dispontime;
    double _dispofftime;

    if (cga->cgamode & 1) {
        disptime    = (double) (cga->crtc[0] + 1);
        _dispontime = (double) cga->crtc[1];
    } else {
        disptime    = (double) ((cga->crtc[0] + 1) << 1);
        _dispontime = (double) (cga->crtc[1] << 1);
    }
    _dispofftime     = disptime - _dispontime;
    _dispontime      = _dispontime * CGACONST;
    _dispofftime     = _dispofftime * CGACONST;
    cga->dispontime  = (uint64_t) (_dispontime);
    cga->dispofftime = (uint64_t) (_dispofftime);
}

static void
cga_render(cga_t *cga, int line)
{
    uint16_t ca  = (cga->crtc[15] | (cga->crtc[14] << 8)) & 0x3fff;
    int      drawcursor;
    int      x;
    int      c;
    uint8_t  chr;
    uint8_t  attr;
    uint16_t dat;
    int      cols[4];
    int      col;

    if ((cga->cgamode & 0x12) == 0x12) {
        for (c = 0; c < 8; ++c) {
            buffer32->line[line][c] = 0;
            if (cga->cgamode & 1)
                buffer32->line[line][c + (cga->crtc[1] << 3) + 8] = 0;
            else
                buffer32->line[line][c + (cga->crtc[1] << 4) + 8] = 0;
        }
    } else {
        for (c = 0; c < 8; ++c) {
            buffer32->line[line][c] = (cga->cgacol & 15) + 16;
            if (cga->cgamode & 1)
                buffer32->line[line][c + (cga->crtc[1] << 3) + 8] = (cga->cgacol & 15) + 16;
            else
                buffer32->line[line][c + (cga->crtc[1] << 4) + 8] = (cga->cgacol & 15) + 16;
        }
    }
    if (cga->cgamode & 1) {
        for (x = 0; x < cga->crtc[1]; x++) {
            if (cga->cgamode & 8) {
                chr  = cga->charbuffer[x << 1];
                attr = cga->charbuffer[(x << 1) + 1];
            } else
                chr = attr = 0;
            drawcursor = ((cga->ma == ca) && cga->con && cga->cursoron);
            cols[1]    = (attr & 15) + 16;
            if (cga->cgamode & 0x20) {
                cols[0] = ((attr >> 4) & 7) + 16;
                if ((cga->cgablink & 8) && (attr & 0x80) && !cga->drawcursor)
                    cols[1] = cols[0];
            } else
                cols[0] = (attr >> 4) + 16;
            if (drawcursor) {
                for (c = 0; c < 8; c++) {
                    buffer32->line[line][(x << 3) + c + 8]
                        = cols[(fontdat[chr + cga->fontbase][cga->sc & 7] & (1 << (c ^ 7))) ? 1 : 0] ^ 15;
                }
            } else {
                for (c = 0; c < 8; c++) {
                    buffer32->line[line][(x << 3) + c + 8]
                        = cols[(fontdat[chr + cga->fontbase][cga->sc & 7] & (1 << (c ^ 7))) ? 1 : 0];
                }
            }
            cga->ma++;
        }
    } else if (!(cga->cgamode & 2)) {
        for (x = 0; x < cga->crtc[1]; x++) {
            if (cga->cgamode & 8) {
                chr  = cga->vram[(cga->ma << 1) & 0x3fff];
                attr = cga->vram[((cga->ma << 1) + 1) & 0x3fff];
            } else
                chr = attr = 0;
            drawcursor = ((cga->ma == ca) && cga->con && cga->cursoron);
            cols[1]    = (attr & 15) + 16;
            if (cga->cgamode & 0x20) {
                cols[0] = ((attr >> 4) & 7) + 16;
                if ((cga->cgablink & 8) && (attr & 0x80))
                    cols[1] = cols[0];
            } else
                cols[0] = (attr >> 4) + 16;
            cga->ma++;
            if (drawcursor) {
                for (c = 0; c < 8; c++) {
                    buffer32->line[line][(x << 4) + (c << 1) + 8]
                        = buffer32->line[line][(x << 4) + (c << 1) + 9]
                        = cols[(fontdat[chr + cga->fontbase][cga->sc & 7] & (1 << (c ^ 7))) ? 1 : 0] ^ 15;
                }
            } else {
                for (c = 0; c < 8; c++) {
                    buffer32->line[line][(x << 4) + (c << 1) + 8]
                        = buffer32->line[line][(x << 4) + (c << 1) + 9] 
                        = cols[(fontdat[chr + cga->fontbase][cga->sc & 7] & (1 << (c ^ 7))) ? 1 : 0];
                }
            }
        }
    } else if (!(cga->cgamode & 16)) {
        cols[0] = (cga->cgacol & 15) | 16;
        col     = (cga->cgacol & 16) ? 24 : 16;
        if (cga->cgamode & 4) {
            cols[1] = col | 3; /* Cyan */
            cols[2] = col | 4; /* Red */
            cols[3] = col | 7; /* White */
        } else if (cga->cgacol & 32) {
            cols[1] = col | 3; /* Cyan */
            cols[2] = col | 5; /* Magenta */
            cols[3] = col | 7; /* White */
        } else {
            cols[1] = col | 2; /* Green */
            cols[2] = col | 4; /* Red */
            cols[3] = col | 6; /* Yellow */
        }
        for (x = 0; x < cga->crtc[1]; x++) {
            if (cga->cgamode & 8)
                dat = (cga->vram[((cga->ma << 1) & 0x1fff) + ((cga->sc & 1) * 0x2000)] << 8) |
                      cga->vram[((cga->ma << 1) & 0x1fff) + ((cga->sc & 1) * 0x2000) + 1];
            else
                dat = 0;
            cga->ma++;
            for (c = 0; c < 8; c++) {
                buffer32->line[line][(x << 4) + (c << 1) + 8]
                    = buffer32->line[line][(x << 4) + (c << 1) + 9]
                    = cols[dat >> 14];
                dat <<= 2;
            }
        }
    } else {
        cols[0] = 0;
        cols[1] = (cga->cgacol & 15) + 16;
        for (x = 0; x < cga->crtc[1]; x++) {
            if (cga->cgamode & 8)
                dat = (cga->vram[((cga->ma << 1) & 0x1fff) + ((cga->sc & 1) * 0x2000)] << 8) |
                      cga->vram[((cga->ma << 1) & 0x1fff) + ((cga->sc & 1) * 0x2000) + 1];
            else
                dat = 0;
            cga->ma++;
            for (c = 0; c < 16; c++) {
                buffer32->line[line][(x << 4) + c + 8] = cols[dat >> 15];
                dat <<= 1;
            }
        }
    }
}

static void
cga_render_blank(cga_t *cga, int line)
{
    int col = ((cga->cgamode & 0x12) == 0x12) ? 0 : (cga->cgacol & 15) + 16;

    if (cga->cgamode & 1)
        hline(buffer32, 0, line, (cga->crtc[1] << 3) + 16, col);
    else
        hline(buffer32, 0, line, (cga->crtc[1] << 4) + 16, col);
}

static void
cga_render_process(cga_t *cga, int line)
{
    int      x;
    uint8_t  border;

    if (cga->cgamode & 1)
        x = (cga->crtc[1] << 3) + 16;
    else
        x = (cga->crtc[1] << 4) + 16;

    if (cga->composite) {
        border = ((cga->cgamode & 0x12) == 0x12) ? 0 : (cga->cgacol & 15);

        Composite_Process(cga->cgamode, border, x >> 2, buffer32->line[line]);
    } else
        video_process_8(x, line);
}

static uint8_t
cga_interpolate_srgb(uint8_t co1, uint8_t co2, double fraction)
{
    uint8_t ret = ((co2 - co1) * fraction + co1);

    return ret;
}

static uint8_t
cga_interpolate_linear(uint8_t co1, uint8_t co2, double fraction)
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
    ret = (uint8_t) (r2 * 255.0);

    return ret;
}

static color_t
cga_interpolate_lookup(cga_t *cga, color_t color1, color_t color2, UNUSED(double fraction))
{
    color_t ret;
    uint8_t dt = cga->double_type - DOUBLE_INTERPOLATE_SRGB;

    ret.a = 0x00;
    ret.r = interp_lut[dt][color1.r][color2.r];
    ret.g = interp_lut[dt][color1.g][color2.g];
    ret.b = interp_lut[dt][color1.b][color2.b];

    return ret;
}

static void
cga_interpolate(cga_t *cga, int x, int y, int w, int h)
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

            interim_1 = cga_interpolate_lookup(cga, prev_color, black, quotient);
            interim_2 = cga_interpolate_lookup(cga, black, next_color, quotient);
            final = cga_interpolate_lookup(cga, interim_1, interim_2, quotient);

            buffer32->line[i][j] = final.color;
        }
    }
}

static void
cga_blit_memtoscreen(cga_t *cga, int x, int y, int w, int h)
{
    if (cga->double_type > DOUBLE_SIMPLE)
        cga_interpolate(cga, x, y, w, h);

    video_blit_memtoscreen(x, y, w, h);
}

void
cga_poll(void *priv)
{
    cga_t   *cga = (cga_t *) priv;
    int      x;
    int      oldsc;
    int      oldvc;
    int      xs_temp;
    int      ys_temp;
    int      old_ma;

    if (!cga->linepos) {
        timer_advance_u64(&cga->timer, cga->dispofftime);
        cga->cgastat |= 1;
        cga->linepos = 1;
        oldsc        = cga->sc;
        if ((cga->crtc[8] & 3) == 3)
            cga->sc = ((cga->sc << 1) + cga->oddeven) & 7;
        if (cga->cgadispon) {
            if (cga->displine < cga->firstline) {
                cga->firstline = cga->displine;
                video_wait_for_buffer();
            }
            cga->lastline = cga->displine;
            switch (cga->double_type) {
                default:
                    cga_render(cga, cga->displine << 1);
                    cga_render_blank(cga, (cga->displine << 1) + 1);
                    break;
                case DOUBLE_NONE:
                    cga_render(cga, cga->displine);
                    break;
                case DOUBLE_SIMPLE:
                    old_ma = cga->ma;
                    cga_render(cga, cga->displine << 1);
                    cga->ma = old_ma;
                    cga_render(cga, (cga->displine << 1) + 1);
                    break;
            }
        } else {
            switch (cga->double_type) {
                default:
                    cga_render_blank(cga, cga->displine << 1);
                    break;
                case DOUBLE_NONE:
                    cga_render_blank(cga, cga->displine);
                    break;
                case DOUBLE_SIMPLE:
                    cga_render_blank(cga, cga->displine << 1);
                    cga_render_blank(cga, (cga->displine << 1) + 1);
                    break;
            }
        }

        switch (cga->double_type) {
            default:
                cga_render_process(cga, cga->displine << 1);
                cga_render_process(cga, (cga->displine << 1) + 1);
                break;
            case DOUBLE_NONE:
                cga_render_process(cga, cga->displine);
                break;
        }

        cga->sc = oldsc;
        if (cga->vc == cga->crtc[7] && !cga->sc)
            cga->cgastat |= 8;
        cga->displine++;
        if (cga->displine >= 360)
            cga->displine = 0;
    } else {
        timer_advance_u64(&cga->timer, cga->dispontime);
        cga->linepos = 0;
        if (cga->vsynctime) {
            cga->vsynctime--;
            if (!cga->vsynctime)
                cga->cgastat &= ~8;
        }
        if (cga->sc == (cga->crtc[11] & 31) || ((cga->crtc[8] & 3) == 3 &&
            cga->sc == ((cga->crtc[11] & 31) >> 1))) {
            cga->con  = 0;
            cga->coff = 1;
        }
        if ((cga->crtc[8] & 3) == 3 && cga->sc == (cga->crtc[9] >> 1))
            cga->maback = cga->ma;
        if (cga->vadj) {
            cga->sc++;
            cga->sc &= 31;
            cga->ma = cga->maback;
            cga->vadj--;
            if (!cga->vadj) {
                cga->cgadispon = 1;
                cga->ma = cga->maback = (cga->crtc[13] | (cga->crtc[12] << 8)) & 0x3fff;
                cga->sc               = 0;
            }
        } else if (cga->sc == cga->crtc[9]) {
            cga->maback = cga->ma;
            cga->sc     = 0;
            oldvc       = cga->vc;
            cga->vc++;
            cga->vc &= 127;

            if (cga->vc == cga->crtc[6])
                cga->cgadispon = 0;

            if (oldvc == cga->crtc[4]) {
                cga->vc   = 0;
                cga->vadj = cga->crtc[5];
                if (!cga->vadj) {
                    cga->cgadispon = 1;
                    cga->ma = cga->maback = (cga->crtc[13] | (cga->crtc[12] << 8)) & 0x3fff;
                }
                switch (cga->crtc[10] & 0x60) {
                    case 0x20:
                        cga->cursoron = 0;
                        break;
                    case 0x60:
                        cga->cursoron = cga->cgablink & 0x10;
                        break;
                    default:
                        cga->cursoron = cga->cgablink & 0x08;
                        break;
                }
            }

            if (cga->vc == cga->crtc[7]) {
                cga->cgadispon = 0;
                cga->displine  = 0;
                cga->vsynctime = 16;
                if (cga->crtc[7]) {
                    if (cga->cgamode & 1)
                        x = (cga->crtc[1] << 3) + 16;
                    else
                        x = (cga->crtc[1] << 4) + 16;
                    cga->lastline++;

                    xs_temp = x;
                    ys_temp = cga->lastline - cga->firstline;
                    if (cga->double_type > DOUBLE_NONE)
                        ys_temp <<= 1;

                    if ((xs_temp > 0) && (ys_temp > 0)) {
                        if (xs_temp < 64)
                            xs_temp = 656;
                        if (ys_temp < 32)
                            ys_temp = 200;
                        if (!enable_overscan)
                            xs_temp -= 16;

                        if ((cga->cgamode & 8) && ((xs_temp != xsize) ||
                            (ys_temp != ysize) || video_force_resize_get())) {
                            xsize = xs_temp;
                            ysize = ys_temp;
                            if (cga->double_type > DOUBLE_NONE)
                                set_screen_size(xsize, ysize + (enable_overscan ? 16 : 0));
                            else
                                set_screen_size(xsize, ysize + (enable_overscan ? 8 : 0));

                            if (video_force_resize_get())
                                video_force_resize_set(0);
                        }

                        if (cga->double_type > DOUBLE_NONE) {
                            if (enable_overscan)
                                cga_blit_memtoscreen(cga, 0, (cga->firstline - 4) << 1,
                                                     xsize, ((cga->lastline - cga->firstline) << 1) + 16);
                            else
                                cga_blit_memtoscreen(cga, 8, cga->firstline << 1,
                                                     xsize, (cga->lastline - cga->firstline) << 1);
                        } else {
                            if (enable_overscan)
                                video_blit_memtoscreen(0, cga->firstline - 4,
                                                       xsize, (cga->lastline - cga->firstline) + 8);
                            else
                                video_blit_memtoscreen(8, cga->firstline,
                                                       xsize, cga->lastline - cga->firstline);
                        }
                    }

                    frames++;

                    video_res_x = xsize;
                    video_res_y = ysize;
                    if (cga->cgamode & 1) {
                        video_res_x /= 8;
                        video_res_y /= cga->crtc[9] + 1;
                        video_bpp = 0;
                    } else if (!(cga->cgamode & 2)) {
                        video_res_x /= 16;
                        video_res_y /= cga->crtc[9] + 1;
                        video_bpp = 0;
                    } else if (!(cga->cgamode & 16)) {
                        video_res_x /= 2;
                        video_bpp = 2;
                    } else
                        video_bpp = 1;
                }
                cga->firstline = 1000;
                cga->lastline  = 0;
                cga->cgablink++;
                cga->oddeven ^= 1;
            }
        } else {
            cga->sc++;
            cga->sc &= 31;
            cga->ma = cga->maback;
        }
        if (cga->cgadispon)
            cga->cgastat &= ~1;
        if (cga->sc == (cga->crtc[10] & 31) || ((cga->crtc[8] & 3) == 3 &&
            cga->sc == ((cga->crtc[10] & 31) >> 1)))
            cga->con = 1;
        if (cga->cgadispon && (cga->cgamode & 1)) {
            for (x = 0; x < (cga->crtc[1] << 1); x++)
                cga->charbuffer[x] = cga->vram[((cga->ma << 1) + x) & 0x3fff];
        }
    }
}

void
cga_init(cga_t *cga)
{
    timer_add(&cga->timer, cga_poll, cga, 1);
    cga->composite = 0;
}

void *
cga_standalone_init(UNUSED(const device_t *info))
{
    int    display_type;
    cga_t *cga = malloc(sizeof(cga_t));

    memset(cga, 0, sizeof(cga_t));
    video_inform(VIDEO_FLAG_TYPE_CGA, &timing_cga);

    display_type      = device_get_config_int("display_type");
    cga->composite    = (display_type != CGA_RGB);
    cga->revision     = device_get_config_int("composite_type");
    cga->snow_enabled = device_get_config_int("snow_enabled");

    cga->vram = malloc(0x4000);

    cga_comp_init(cga->revision);
    timer_add(&cga->timer, cga_poll, cga, 1);
    mem_mapping_add(&cga->mapping, 0xb8000, 0x08000, cga_read, NULL, NULL, cga_write, NULL, NULL, NULL /*cga->vram*/, MEM_MAPPING_EXTERNAL, cga);
    io_sethandler(0x03d0, 0x0010, cga_in, NULL, NULL, cga_out, NULL, NULL, cga);

    overscan_x = overscan_y = 16;

    cga->rgb_type = device_get_config_int("rgb_type");
    cga_palette   = (cga->rgb_type << 1);
    cgapal_rebuild();
    update_cga16_color(cga->cgamode);

    cga->double_type = device_get_config_int("double_type");

    for (uint16_t i = 0; i < 256; i++) {
        for (uint16_t j = 0; j < 256; j++) {
            interp_lut[0][i][j] = cga_interpolate_srgb(i, j, 0.5);
            interp_lut[1][i][j] = cga_interpolate_linear(i, j, 0.5);
        }
    }

    return cga;
}

void *
cga_pravetz_init(const device_t *info)
{
    cga_t *cga = cga_standalone_init(info);

    loadfont("roms/video/cga/PRAVETZ-VDC2.BIN", 10);

    io_removehandler(0x03dd, 0x0001, cga_in, NULL, NULL, cga_out, NULL, NULL, cga);
    io_sethandler(0x03dd, 0x0001, cga_pravetz_in, NULL, NULL, cga_pravetz_out, NULL, NULL, cga);

    cga->fontbase = 0x0300;

    return cga;
}

void
cga_close(void *priv)
{
    cga_t *cga = (cga_t *) priv;

    free(cga->vram);
    free(cga);
}

void
cga_speed_changed(void *priv)
{
    cga_t *cga = (cga_t *) priv;

    cga_recalctimings(cga);
}

// clang-format off
const device_config_t cga_config[] = {
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
};
// clang-format on

const device_t cga_device = {
    .name          = "IBM CGA",
    .internal_name = "cga",
    .flags         = DEVICE_ISA,
    .local         = 0,
    .init          = cga_standalone_init,
    .close         = cga_close,
    .reset         = NULL,
    .available     = NULL,
    .speed_changed = cga_speed_changed,
    .force_redraw  = NULL,
    .config        = cga_config
};

const device_t cga_pravetz_device = {
    .name          = "Pravetz VDC-2",
    .internal_name = "cga_pravetz",
    .flags         = DEVICE_ISA,
    .local         = 0,
    .init          = cga_pravetz_init,
    .close         = cga_close,
    .reset         = NULL,
    .available     = NULL,
    .speed_changed = cga_speed_changed,
    .force_redraw  = NULL,
    .config        = cga_config
};
