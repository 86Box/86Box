/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          Emulation of the Amstrad series of PC's: PC1512, PC1640 and
 *          PC200, including their keyboard, mouse and video devices, as
 *          well as the PC2086 and PC3086 systems.
 *
 * PC1512:  The PC1512 extends CGA with a bit-planar 640x200x16 mode.
 *          Most CRTC registers are fixed.
 *
 *          The Technical Reference Manual lists the video waitstate
 *          time as between 12 and 46 cycles. We currently always use
 *          the lower number.
 *
 * PC1640:  Mostly standard EGA, but with CGA & Hercules emulation.
 *
 * PC200:  CGA with some NMI stuff. But we don't need that as it's only
 *         used for TV and LCD displays, and we're emulating a CRT.
 *
 * PPC512/640: Portable with both CGA-compatible and MDA-compatible monitors.
 *
 * TODO:   This module is not complete yet:
 *
 * All models: The internal mouse controller does not work correctly with
 *             version 7.04 of the mouse driver.
 *
 *
 *
 * Authors: Sarah Walker, <https://pcem-emulator.co.uk/>
 *          Miran Grca, <mgrca8@gmail.com>
 *          Fred N. van Kempen, <decwiz@yahoo.com>
 *          John Elliott, <jce@seasip.info>
 *
 *          Copyright 2008-2019 Sarah Walker.
 *          Copyright 2016-2019 Miran Grca.
 *          Copyright 2017-2019 Fred N. van Kempen.
 *          Copyright 2019 John Elliott.
 */
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <wchar.h>
#define HAVE_STDARG_H
#include <86box/86box.h>
#include "cpu.h"
#include <86box/timer.h>
#include <86box/io.h>
#include <86box/nmi.h>
#include <86box/pic.h>
#include <86box/pit.h>
#include <86box/ppi.h>
#include <86box/mem.h>
#include <86box/rom.h>
#include <86box/device.h>
#include <86box/nvr.h>
#include <86box/keyboard.h>
#include <86box/mouse.h>
#include <86box/gameport.h>
#include <86box/lpt.h>
#include <86box/fdd.h>
#include <86box/fdc.h>
#include <86box/sound.h>
#include <86box/snd_speaker.h>
#include <86box/video.h>
#include <86box/vid_cga.h>
#include <86box/vid_ega.h>
#include <86box/vid_mda.h>
#include <86box/machine.h>
#include <86box/m_amstrad.h>
#include <86box/plat_unused.h>

#define STAT_PARITY   0x80
#define STAT_RTIMEOUT 0x40
#define STAT_TTIMEOUT 0x20
#define STAT_LOCK     0x10
#define STAT_CD       0x08
#define STAT_SYSFLAG  0x04
#define STAT_IFULL    0x02
#define STAT_OFULL    0x01

typedef struct amsvid_t {
    rom_t      bios_rom;    /* 1640 */
    cga_t      cga;         /* 1640/200 */
    mda_t      mda;         /* 1512/200/PPC512/640*/
    ega_t      ega;         /* 1640 */
    uint8_t    emulation;   /* Which display are we emulating? */
    uint8_t    dipswitches; /* DIP switches 1-3 */
    uint8_t    crtc_index;  /* CRTC index readback
                             * Bit 7: CGA control port written
                             * Bit 6: Operation control port written
                             * Bit 5: CRTC register written
                             * Bits 0-4: Last CRTC register selected */
    uint8_t    operation_ctrl;
    uint8_t    reg_3df;
    uint8_t    type;
    uint8_t    crtc[32];
    int        crtcreg;
    int        cga_enabled; /* 1640 */
    uint8_t    cgacol;
    uint8_t    cgamode;
    uint8_t    stat;
    uint8_t    plane_write; /* 1512/200 */
    uint8_t    plane_read;  /* 1512/200 */
    uint8_t    border;      /* 1512/200 */
    uint8_t    invert;      /* 512/640 */
    int        fontbase;    /* 1512/200 */
    int        linepos;
    int        displine;
    int        sc;
    int        vc;
    int        cgadispon;
    int        con;
    int        coff;
    int        cursoron;
    int        cgablink;
    int        vsynctime;
    int        fullchange;
    int        vadj;
    uint16_t   ma;
    uint16_t   maback;
    int        dispon;
    int        blink;
    uint64_t   dispontime;  /* 1512/1640 */
    uint64_t   dispofftime; /* 1512/1640 */
    pc_timer_t timer;       /* 1512/1640 */
    int        firstline;
    int        lastline;
    uint8_t   *vram;
    void      *ams;
} amsvid_t;

typedef struct amstrad_t {
    /* Machine stuff. */
    uint8_t dead;
    uint8_t stat1;
    uint8_t stat2;
    uint8_t type;
    uint8_t language;

    /* Keyboard stuff. */
    int8_t     wantirq;
    uint8_t    key_waiting;
    uint8_t    pa;
    uint8_t    pb;
    pc_timer_t send_delay_timer;

    /* Mouse stuff. */
    int oldb;

    /* Video stuff. */
    amsvid_t *vid;
    fdc_t    *fdc;
} amstrad_t;

uint32_t amstrad_latch;

static uint8_t key_queue[16];
static int     key_queue_start = 0;
static int     key_queue_end   = 0;
static uint8_t crtc_mask[32]   = {
    0xff, 0xff, 0xff, 0xff, 0x7f, 0x1f, 0x7f, 0x7f,
    0xf3, 0x1f, 0x7f, 0x1f, 0x3f, 0xff, 0x3f, 0xff,
    0xff, 0xff, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00
};

static video_timings_t timing_pc1512 = { VIDEO_BUS, 0, 0, 0, 0, 0, 0 }; /*PC1512 video code handles waitstates itself*/
static video_timings_t timing_pc1640 = { VIDEO_ISA, 8, 16, 32, 8, 16, 32 };
static video_timings_t timing_pc200  = { VIDEO_ISA, 8, 16, 32, 8, 16, 32 };

enum {
    AMS_PC1512,
    AMS_PC1640,
    AMS_PC200,
    AMS_PPC512,
    AMS_PC2086,
    AMS_PC3086
};

#ifdef ENABLE_AMSTRAD_LOG
int amstrad_do_log = ENABLE_AMSTRAD_LOG;

static void
amstrad_log(const char *fmt, ...)
{
    va_list ap;

    if (amstrad_do_log) {
        va_start(ap, fmt);
        pclog_ex(fmt, ap);
        va_end(ap);
    }
}
#else
#    define amstrad_log(fmt, ...)
#endif

static void
recalc_timings_1512(amsvid_t *vid)
{
    double _dispontime;
    double _dispofftime;
    double disptime;

    disptime     = /*128*/ 114; /*Fixed on PC1512*/
    _dispontime  = 80;
    _dispofftime = disptime - _dispontime;
    _dispontime *= CGACONST;
    _dispofftime *= CGACONST;
    vid->dispontime  = (uint64_t) _dispontime;
    vid->dispofftime = (uint64_t) _dispofftime;
}

static void
vid_out_1512(uint16_t addr, uint8_t val, void *priv)
{
    amsvid_t *vid = (amsvid_t *) priv;
    uint8_t   old;

    if ((addr >= 0x3d0) && (addr <= 0x3d7))
        addr = (addr & 0xff9) | 0x004;

    switch (addr) {
        case 0x03d4:
            vid->crtcreg = val & 31;
            return;

        case 0x03d5:
            old                     = vid->crtc[vid->crtcreg];
            vid->crtc[vid->crtcreg] = val & crtc_mask[vid->crtcreg];
            if (old != val) {
                if (vid->crtcreg < 0xe || vid->crtcreg > 0x10) {
                    vid->fullchange = changeframecount;
                    recalc_timings_1512(vid);
                }
            }
            return;

        case 0x03d8:
            if ((val & 0x12) == 0x12 && (vid->cgamode & 0x12) != 0x12) {
                vid->plane_write = 0xf;
                vid->plane_read  = 0;
            }
            vid->cgamode = val;
            return;

        case 0x03d9:
            vid->cgacol = val;
            return;

        case 0x03dd:
            vid->plane_write = val;
            return;

        case 0x03de:
            vid->plane_read = val & 3;
            return;

        case 0x03df:
            vid->border = val;
            return;

        default:
            return;
    }
}

static uint8_t
vid_in_1512(uint16_t addr, void *priv)
{
    const amsvid_t *vid = (amsvid_t *) priv;
    uint8_t         ret = 0xff;

    if ((addr >= 0x3d0) && (addr <= 0x3d7))
        addr = (addr & 0xff9) | 0x004;

    switch (addr) {
        case 0x03d4:
            ret = vid->crtcreg;
            break;

        case 0x03d5:
            ret = vid->crtc[vid->crtcreg];
            break;

        case 0x03da:
            ret = vid->stat;
            break;

        default:
            break;
    }

    return ret;
}

static void
vid_write_1512(uint32_t addr, uint8_t val, void *priv)
{
    amsvid_t *vid = (amsvid_t *) priv;

    cycles -= 12;
    addr &= 0x3fff;

    if ((vid->cgamode & 0x12) == 0x12) {
        if (vid->plane_write & 1)
            vid->vram[addr] = val;
        if (vid->plane_write & 2)
            vid->vram[addr | 0x4000] = val;
        if (vid->plane_write & 4)
            vid->vram[addr | 0x8000] = val;
        if (vid->plane_write & 8)
            vid->vram[addr | 0xc000] = val;
    } else
        vid->vram[addr] = val;
}

static uint8_t
vid_read_1512(uint32_t addr, void *priv)
{
    const amsvid_t *vid = (amsvid_t *) priv;

    cycles -= 12;
    addr &= 0x3fff;

    if ((vid->cgamode & 0x12) == 0x12)
        return (vid->vram[addr | (vid->plane_read << 14)]);

    return (vid->vram[addr]);
}

static void
vid_poll_1512(void *priv)
{
    amsvid_t *vid = (amsvid_t *) priv;
    uint16_t  ca  = (vid->crtc[15] | (vid->crtc[14] << 8)) & 0x3fff;
    int       drawcursor;
    int       x;
    int       c;
    int       xs_temp;
    int       ys_temp;
    uint8_t   chr;
    uint8_t   attr;
    uint16_t  dat;
    uint16_t  dat2;
    uint16_t  dat3;
    uint16_t  dat4;
    int       cols[4];
    int       col;
    int       oldsc;

    if (!vid->linepos) {
        timer_advance_u64(&vid->timer, vid->dispofftime);
        vid->stat |= 1;
        vid->linepos = 1;
        oldsc        = vid->sc;
        if (vid->dispon) {
            if (vid->displine < vid->firstline) {
                vid->firstline = vid->displine;
                video_wait_for_buffer();
            }
            vid->lastline = vid->displine;
            for (c = 0; c < 8; c++) {
                if ((vid->cgamode & 0x12) == 0x12) {
                    buffer32->line[vid->displine << 1][c] = buffer32->line[(vid->displine << 1) + 1][c] = (vid->border & 15) + 16;
                    if (vid->cgamode & 1) {
                        buffer32->line[vid->displine << 1][c + (vid->crtc[1] << 3) + 8] = buffer32->line[(vid->displine << 1) + 1][c + (vid->crtc[1] << 3) + 8] = 0;
                    } else {
                        buffer32->line[vid->displine << 1][c + (vid->crtc[1] << 4) + 8] = buffer32->line[(vid->displine << 1) + 1][c + (vid->crtc[1] << 4) + 8] = 0;
                    }
                } else {
                    buffer32->line[vid->displine << 1][c] = buffer32->line[(vid->displine << 1) + 1][c] = (vid->cgacol & 15) + 16;
                    if (vid->cgamode & 1) {
                        buffer32->line[vid->displine << 1][c + (vid->crtc[1] << 3) + 8] = buffer32->line[(vid->displine << 1) + 1][c + (vid->crtc[1] << 3) + 8] = (vid->cgacol & 15) + 16;
                    } else {
                        buffer32->line[vid->displine << 1][c + (vid->crtc[1] << 4) + 8] = buffer32->line[(vid->displine << 1) + 1][c + (vid->crtc[1] << 4) + 8] = (vid->cgacol & 15) + 16;
                    }
                }
            }
            if (vid->cgamode & 1) {
                for (x = 0; x < 80; x++) {
                    chr        = vid->vram[(vid->ma << 1) & 0x3fff];
                    attr       = vid->vram[((vid->ma << 1) + 1) & 0x3fff];
                    drawcursor = ((vid->ma == ca) && vid->con && vid->cursoron);
                    if (vid->cgamode & 0x20) {
                        cols[1] = (attr & 15) + 16;
                        cols[0] = ((attr >> 4) & 7) + 16;
                        if ((vid->blink & 16) && (attr & 0x80) && !drawcursor)
                            cols[1] = cols[0];
                    } else {
                        cols[1] = (attr & 15) + 16;
                        cols[0] = (attr >> 4) + 16;
                    }
                    if (drawcursor) {
                        for (c = 0; c < 8; c++) {
                            buffer32->line[vid->displine << 1][(x << 3) + c + 8] = buffer32->line[(vid->displine << 1) + 1][(x << 3) + c + 8] = cols[(fontdat[vid->fontbase + chr][vid->sc & 7] & (1 << (c ^ 7))) ? 1 : 0] ^ 15;
                        }
                    } else {
                        for (c = 0; c < 8; c++) {
                            buffer32->line[vid->displine << 1][(x << 3) + c + 8] = buffer32->line[(vid->displine << 1) + 1][(x << 3) + c + 8] = cols[(fontdat[vid->fontbase + chr][vid->sc & 7] & (1 << (c ^ 7))) ? 1 : 0];
                        }
                    }
                    vid->ma++;
                }
            } else if (!(vid->cgamode & 2)) {
                for (x = 0; x < 40; x++) {
                    chr        = vid->vram[(vid->ma << 1) & 0x3fff];
                    attr       = vid->vram[((vid->ma << 1) + 1) & 0x3fff];
                    drawcursor = ((vid->ma == ca) && vid->con && vid->cursoron);
                    if (vid->cgamode & 0x20) {
                        cols[1] = (attr & 15) + 16;
                        cols[0] = ((attr >> 4) & 7) + 16;
                        if ((vid->blink & 16) && (attr & 0x80))
                            cols[1] = cols[0];
                    } else {
                        cols[1] = (attr & 15) + 16;
                        cols[0] = (attr >> 4) + 16;
                    }
                    vid->ma++;
                    if (drawcursor) {
                        for (c = 0; c < 8; c++) {
                            buffer32->line[vid->displine << 1][(x << 4) + (c << 1) + 8] = buffer32->line[vid->displine << 1][(x << 4) + (c << 1) + 1 + 8] = buffer32->line[(vid->displine << 1) + 1][(x << 4) + (c << 1) + 8] = buffer32->line[(vid->displine << 1) + 1][(x << 4) + (c << 1) + 1 + 8] = cols[(fontdat[vid->fontbase + chr][vid->sc & 7] & (1 << (c ^ 7))) ? 1 : 0] ^ 15;
                        }
                    } else {
                        for (c = 0; c < 8; c++) {
                            buffer32->line[vid->displine << 1][(x << 4) + (c << 1) + 8] = buffer32->line[vid->displine << 1][(x << 4) + (c << 1) + 1 + 8] = buffer32->line[(vid->displine << 1) + 1][(x << 4) + (c << 1) + 8] = buffer32->line[(vid->displine << 1) + 1][(x << 4) + (c << 1) + 1 + 8] = cols[(fontdat[vid->fontbase + chr][vid->sc & 7] & (1 << (c ^ 7))) ? 1 : 0];
                        }
                    }
                }
            } else if (!(vid->cgamode & 16)) {
                cols[0] = (vid->cgacol & 15) | 16;
                col     = (vid->cgacol & 16) ? 24 : 16;
                if (vid->cgamode & 4) {
                    cols[1] = col | 3;
                    cols[2] = col | 4;
                    cols[3] = col | 7;
                } else if (vid->cgacol & 32) {
                    cols[1] = col | 3;
                    cols[2] = col | 5;
                    cols[3] = col | 7;
                } else {
                    cols[1] = col | 2;
                    cols[2] = col | 4;
                    cols[3] = col | 6;
                }
                for (x = 0; x < 40; x++) {
                    dat = (vid->vram[((vid->ma << 1) & 0x1fff) + ((vid->sc & 1) * 0x2000)] << 8) | vid->vram[((vid->ma << 1) & 0x1fff) + ((vid->sc & 1) * 0x2000) + 1];
                    vid->ma++;
                    for (c = 0; c < 8; c++) {
                        buffer32->line[vid->displine << 1][(x << 4) + (c << 1) + 8] = buffer32->line[vid->displine << 1][(x << 4) + (c << 1) + 1 + 8] = buffer32->line[(vid->displine << 1) + 1][(x << 4) + (c << 1) + 8] = buffer32->line[(vid->displine << 1) + 1][(x << 4) + (c << 1) + 1 + 8] = cols[dat >> 14];
                        dat <<= 2;
                    }
                }
            } else {
                for (x = 0; x < 40; x++) {
                    ca   = ((vid->ma << 1) & 0x1fff) + ((vid->sc & 1) * 0x2000);
                    dat  = (vid->vram[ca] << 8) | vid->vram[ca + 1];
                    dat2 = (vid->vram[ca + 0x4000] << 8) | vid->vram[ca + 0x4001];
                    dat3 = (vid->vram[ca + 0x8000] << 8) | vid->vram[ca + 0x8001];
                    dat4 = (vid->vram[ca + 0xc000] << 8) | vid->vram[ca + 0xc001];

                    vid->ma++;
                    for (c = 0; c < 16; c++) {
                        buffer32->line[vid->displine << 1][(x << 4) + c + 8] = buffer32->line[(vid->displine << 1) + 1][(x << 4) + c + 8] = (((dat >> 15) | ((dat2 >> 15) << 1) | ((dat3 >> 15) << 2) | ((dat4 >> 15) << 3)) & (vid->cgacol & 15)) + 16;
                        dat <<= 1;
                        dat2 <<= 1;
                        dat3 <<= 1;
                        dat4 <<= 1;
                    }
                }
            }
        } else {
            cols[0] = ((vid->cgamode & 0x12) == 0x12) ? 0 : (vid->cgacol & 15) + 16;
            if (vid->cgamode & 1) {
                hline(buffer32, 0, (vid->displine << 1), (vid->crtc[1] << 3) + 16, cols[0]);
                hline(buffer32, 0, (vid->displine << 1) + 1, (vid->crtc[1] << 3) + 16, cols[0]);
            } else {
                hline(buffer32, 0, (vid->displine << 1), (vid->crtc[1] << 4) + 16, cols[0]);
                hline(buffer32, 0, (vid->displine << 1), (vid->crtc[1] << 4) + 16, cols[0]);
            }
        }

        if (vid->cgamode & 1)
            x = (vid->crtc[1] << 3) + 16;
        else
            x = (vid->crtc[1] << 4) + 16;

        video_process_8(x, vid->displine << 1);
        video_process_8(x, (vid->displine << 1) + 1);

        vid->sc = oldsc;
        if (vid->vsynctime)
            vid->stat |= 8;
        vid->displine++;
        if (vid->displine >= 360)
            vid->displine = 0;
    } else {
        timer_advance_u64(&vid->timer, vid->dispontime);
        if ((vid->lastline - vid->firstline) == 199)
            vid->dispon = 0; /*Amstrad PC1512 always displays 200 lines, regardless of CRTC settings*/
        if (vid->dispon)
            vid->stat &= ~1;
        vid->linepos = 0;
        if (vid->vsynctime) {
            vid->vsynctime--;
            if (!vid->vsynctime)
                vid->stat &= ~8;
        }
        if (vid->sc == (vid->crtc[11] & 31)) {
            vid->con  = 0;
            vid->coff = 1;
        }
        if (vid->vadj) {
            vid->sc++;
            vid->sc &= 31;
            vid->ma = vid->maback;
            vid->vadj--;
            if (!vid->vadj) {
                vid->dispon = 1;
                vid->ma = vid->maback = (vid->crtc[13] | (vid->crtc[12] << 8)) & 0x3fff;
                vid->sc               = 0;
            }
        } else if (vid->sc == vid->crtc[9]) {
            vid->maback = vid->ma;
            vid->sc     = 0;
            vid->vc++;
            vid->vc &= 127;

            if (vid->displine == 32) {
                vid->vc   = 0;
                vid->vadj = 6;
                if ((vid->crtc[10] & 0x60) == 0x20)
                    vid->cursoron = 0;
                else
                    vid->cursoron = vid->blink & 16;
            }

            if (vid->displine >= 262) {
                vid->dispon    = 0;
                vid->displine  = 0;
                vid->vsynctime = 46;

                if (vid->cgamode & 1)
                    x = (vid->crtc[1] << 3) + 16;
                else
                    x = (vid->crtc[1] << 4) + 16;
                vid->lastline++;

                xs_temp = x;
                ys_temp = (vid->lastline - vid->firstline) << 1;

                if ((xs_temp > 0) && (ys_temp > 0)) {
                    if (xs_temp < 64)
                        xs_temp = 656;
                    if (ys_temp < 32)
                        ys_temp = 400;
                    if (!enable_overscan)
                        xs_temp -= 16;

                    if ((xs_temp != xsize) || (ys_temp != ysize) || video_force_resize_get()) {
                        xsize = xs_temp;
                        ysize = ys_temp;
                        set_screen_size(xsize, ysize + (enable_overscan ? 16 : 0));

                        if (video_force_resize_get())
                            video_force_resize_set(0);
                    }

                    if (enable_overscan) {
                        video_blit_memtoscreen(0, (vid->firstline - 4) << 1,
                                               xsize, ((vid->lastline - vid->firstline) + 8) << 1);
                    } else {
                        video_blit_memtoscreen(8, vid->firstline << 1,
                                               xsize, (vid->lastline - vid->firstline) << 1);
                    }
                }

                video_res_x = xsize;
                video_res_y = ysize;
                if (vid->cgamode & 1) {
                    video_res_x /= 8;
                    video_res_y /= vid->crtc[9] + 1;
                    video_bpp = 0;
                } else if (!(vid->cgamode & 2)) {
                    video_res_x /= 16;
                    video_res_y /= vid->crtc[9] + 1;
                    video_bpp = 0;
                } else if (!(vid->cgamode & 16)) {
                    video_res_x /= 2;
                    video_bpp = 2;
                } else {
                    video_bpp = 4;
                }

                vid->firstline = 1000;
                vid->lastline  = 0;
                vid->blink++;
            }
        } else {
            vid->sc++;
            vid->sc &= 31;
            vid->ma = vid->maback;
        }
        if (vid->sc == (vid->crtc[10] & 31))
            vid->con = 1;
    }
}

static void
vid_init_1512(amstrad_t *ams)
{
    amsvid_t *vid;

    /* Allocate a video controller block. */
    vid = (amsvid_t *) calloc(1, sizeof(amsvid_t));

    video_inform(VIDEO_FLAG_TYPE_CGA, &timing_pc1512);

    vid->vram    = malloc(0x10000);
    vid->cgacol  = 7;
    vid->cgamode = 0x12;

    timer_add(&vid->timer, vid_poll_1512, vid, 1);
    mem_mapping_add(&vid->cga.mapping, 0xb8000, 0x08000,
                    vid_read_1512, NULL, NULL, vid_write_1512, NULL, NULL,
                    NULL, 0, vid);
    io_sethandler(0x03d0, 16,
                  vid_in_1512, NULL, NULL, vid_out_1512, NULL, NULL, vid);

    overscan_x = overscan_y = 16;

    vid->fontbase = (device_get_config_int("codepage") & 3) * 256;

    cga_palette = (device_get_config_int("display_type") << 1);
    cgapal_rebuild();

    ams->vid = vid;
}

static void
vid_close_1512(void *priv)
{
    amsvid_t *vid = (amsvid_t *) priv;

    free(vid->vram);

    free(vid);
}

static void
vid_speed_change_1512(void *priv)
{
    amsvid_t *vid = (amsvid_t *) priv;

    recalc_timings_1512(vid);
}

const device_config_t vid_1512_config[] = {
  // clang-format off
    {
        .name = "display_type",
        .description = "Display type",
        .type = CONFIG_SELECTION,
        .default_string = "",
        .default_int = 0,
        .file_filter = "",
        .spinner = { 0 },
        .selection = {
            { .description = "PC-CM (Colour)",     .value = 0 },
            { .description = "PC-MM (Monochrome)", .value = 3 },
            { .description = ""                               }
        }
    },
    {
        .name = "codepage",
        .description = "Hardware font",
        .type = CONFIG_SELECTION,
        .default_string = "",
        .default_int = 3,
        .file_filter = "",
        .spinner = { 0 },
        .selection = {
            { .description = "US English", .value = 3 },
            { .description = "Danish",     .value = 1 },
            { .description = "Greek",      .value = 0 },
            { .description = ""                       }
        }
    },
    {
        .name = "language",
        .description = "BIOS language",
        .type = CONFIG_SELECTION,
        .default_string = "",
        .default_int = 7,
        .file_filter = "",
        .spinner = { 0 },
        .selection = {
            { .description = "English",         .value = 7 },
            { .description = "German",          .value = 6 },
            { .description = "French",          .value = 5 },
            { .description = "Spanish",         .value = 4 },
            { .description = "Danish",          .value = 3 },
            { .description = "Swedish",         .value = 2 },
            { .description = "Italian",         .value = 1 },
            { .description = "Diagnostic mode", .value = 0 },
            { .description = ""                            }
        }
    },
    { .name = "", .description = "", .type = CONFIG_END }
  // clang-format on
};

const device_t vid_1512_device = {
    .name          = "Amstrad PC1512 (video)",
    .internal_name = "vid_1512",
    .flags         = 0,
    .local         = 0,
    .init          = NULL,
    .close         = vid_close_1512,
    .reset         = NULL,
    .available     = NULL,
    .speed_changed = vid_speed_change_1512,
    .force_redraw  = NULL,
    .config        = vid_1512_config
};

static void
recalc_timings_1640(amsvid_t *vid)
{
    cga_recalctimings(&vid->cga);
    ega_recalctimings(&vid->ega);

    if (vid->cga_enabled) {
        overscan_x = overscan_y = 16;

        vid->dispontime  = vid->cga.dispontime;
        vid->dispofftime = vid->cga.dispofftime;
    } else {
        overscan_x = 16;
        overscan_y = 28;

        vid->dispontime  = vid->ega.dispontime;
        vid->dispofftime = vid->ega.dispofftime;
    }
}

static void
vid_out_1640(uint16_t addr, uint8_t val, void *priv)
{
    amsvid_t *vid = (amsvid_t *) priv;

    switch (addr) {
        case 0x03db:
            vid->cga_enabled = val & 0x40;
            if (vid->cga_enabled) {
                timer_disable(&vid->ega.timer);
                timer_set_delay_u64(&vid->cga.timer, 0);
                mem_mapping_enable(&vid->cga.mapping);
                mem_mapping_disable(&vid->ega.mapping);
            } else {
                timer_disable(&vid->cga.timer);
                timer_set_delay_u64(&vid->ega.timer, 0);
                mem_mapping_disable(&vid->cga.mapping);
                switch (vid->ega.gdcreg[6] & 0xc) {
                    case 0x0: /*128k at A0000*/
                        mem_mapping_set_addr(&vid->ega.mapping,
                                             0xa0000, 0x20000);
                        break;

                    case 0x4: /*64k at A0000*/
                        mem_mapping_set_addr(&vid->ega.mapping,
                                             0xa0000, 0x10000);
                        break;

                    case 0x8: /*32k at B0000*/
                        mem_mapping_set_addr(&vid->ega.mapping,
                                             0xb0000, 0x08000);
                        break;

                    case 0xC: /*32k at B8000*/
                        mem_mapping_set_addr(&vid->ega.mapping,
                                             0xb8000, 0x08000);
                        break;

                    default:
                        break;
                }
            }
            return;

        default:
            break;
    }

    if (vid->cga_enabled)
        cga_out(addr, val, &vid->cga);
    else
        ega_out(addr, val, &vid->ega);
}

static uint8_t
vid_in_1640(uint16_t addr, void *priv)
{
    amsvid_t *vid = (amsvid_t *) priv;

    if (vid->cga_enabled)
        return (cga_in(addr, &vid->cga));
    else
        return (ega_in(addr, &vid->ega));
}

static void
vid_init_1640(amstrad_t *ams)
{
    amsvid_t *vid;

    /* Allocate a video controller block. */
    vid = (amsvid_t *) calloc(1, sizeof(amsvid_t));

    rom_init(&vid->bios_rom, "roms/machines/pc1640/40100",
             0xc0000, 0x8000, 0x7fff, 0, 0);

    ega_init(&vid->ega, 9, 0);
    vid->cga.vram    = vid->ega.vram;
    vid->cga_enabled = 1;
    cga_init(&vid->cga);
    timer_disable(&vid->ega.timer);

    video_inform(VIDEO_FLAG_TYPE_SPECIAL, &timing_pc1640);

    mem_mapping_add(&vid->cga.mapping, 0xb8000, 0x08000,
                    cga_read, NULL, NULL, cga_write, NULL, NULL, NULL, 0, &vid->cga);
    mem_mapping_add(&vid->ega.mapping, 0, 0,
                    ega_read, NULL, NULL, ega_write, NULL, NULL, NULL, 0, &vid->ega);
    io_sethandler(0x03a0, 64,
                  vid_in_1640, NULL, NULL, vid_out_1640, NULL, NULL, vid);

    overscan_x = overscan_y = 16;

    vid->fontbase = 768;

    cga_palette = 0;
    cgapal_rebuild();

    ams->vid = vid;
}

static void
vid_close_1640(void *priv)
{
    amsvid_t *vid = (amsvid_t *) priv;

    free(vid->ega.vram);

    free(vid);
}

static void
vid_speed_changed_1640(void *priv)
{
    amsvid_t *vid = (amsvid_t *) priv;

    recalc_timings_1640(vid);
}

const device_config_t vid_1640_config[] = {
  // clang-format off
    {
        .name = "language",
        .description = "BIOS language",
        .type = CONFIG_SELECTION,
        .default_string = "",
        .default_int = 7,
        .file_filter = "",
        .spinner = { 0 },
        .selection = {
            { .description = "English",         .value = 7 },
            { .description = "German",          .value = 6 },
            { .description = "French",          .value = 5 },
            { .description = "Spanish",         .value = 4 },
            { .description = "Danish",          .value = 3 },
            { .description = "Swedish",         .value = 2 },
            { .description = "Italian",         .value = 1 },
            { .description = "Diagnostic mode", .value = 0 },
            { .description = ""                            }
        }
    },
    { .name = "", .description = "", .type = CONFIG_END }
  // clang-format on
};

const device_t vid_1640_device = {
    .name          = "Amstrad PC1640 (video)",
    .internal_name = "vid_1640",
    .flags         = 0,
    .local         = 0,
    .init          = NULL,
    .close         = vid_close_1640,
    .reset         = NULL,
    .available     = NULL,
    .speed_changed = vid_speed_changed_1640,
    .force_redraw  = NULL,
    .config        = vid_1640_config
};

/* Display type */
#define PC200_CGA  0 /* CGA monitor */
#define PC200_MDA  1 /* MDA monitor */
#define PC200_TV   2 /* Television */
#define PC200_LCDC 3 /* PPC512 LCD as CGA*/
#define PC200_LCDM 4 /* PPC512 LCD as MDA*/

extern int nmi_mask;

static uint32_t blue;
static uint32_t green;

static uint32_t lcdcols[256][2][2];

static void
ams_inform(amsvid_t *vid)
{
    switch (vid->emulation) {
        case PC200_CGA:
        case PC200_TV:
        case PC200_LCDC:
            video_inform(VIDEO_FLAG_TYPE_CGA, &timing_pc200);
            break;
        case PC200_MDA:
        case PC200_LCDM:
            video_inform(VIDEO_FLAG_TYPE_MDA, &timing_pc200);
            break;

        default:
            break;
    }
}

static void
vid_speed_changed_200(void *priv)
{
    amsvid_t *vid = (amsvid_t *) priv;

    cga_recalctimings(&vid->cga);
    mda_recalctimings(&vid->mda);
}

/* LCD colour mappings
 *
 * 0 => solid green
 * 1 => blue on green
 * 2 => green on blue
 * 3 => solid blue
 */
static unsigned char mapping1[256] = {
    // clang-format off
/*      0  1  2  3  4  5  6  7  8  9  A  B  C  D  E  F */
/*00*/  0, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
/*10*/  2, 0, 1, 1, 1, 1, 1, 1, 2, 1, 1, 1, 1, 1, 1, 1,
/*20*/  2, 2, 0, 1, 2, 2, 1, 1, 2, 2, 1, 1, 2, 2, 1, 1,
/*30*/  2, 2, 2, 0, 2, 2, 1, 1, 2, 2, 2, 1, 2, 2, 1, 1,
/*40*/  2, 2, 1, 1, 0, 1, 1, 1, 2, 2, 1, 1, 1, 1, 1, 1,
/*50*/  2, 2, 1, 1, 2, 0, 1, 1, 2, 2, 1, 1, 2, 1, 1, 1,
/*60*/  2, 2, 2, 2, 2, 2, 0, 1, 2, 2, 2, 2, 2, 2, 1, 1,
/*70*/  2, 2, 2, 2, 2, 2, 2, 0, 2, 2, 2, 2, 2, 2, 2, 1,
/*80*/  2, 1, 1, 1, 1, 1, 1, 1, 0, 1, 1, 1, 1, 1, 1, 1,
/*90*/  2, 2, 1, 1, 1, 1, 1, 1, 2, 0, 1, 1, 1, 1, 1, 1,
/*A0*/  2, 2, 2, 1, 2, 2, 1, 1, 2, 2, 0, 1, 2, 2, 1, 1,
/*B0*/  2, 2, 2, 2, 2, 2, 1, 1, 2, 2, 2, 0, 2, 2, 1, 1,
/*C0*/  2, 2, 1, 1, 2, 1, 1, 1, 2, 2, 1, 1, 0, 1, 1, 1,
/*D0*/  2, 2, 1, 1, 2, 2, 1, 1, 2, 2, 1, 1, 2, 0, 1, 1,
/*E0*/  2, 2, 2, 2, 2, 2, 2, 1, 2, 2, 2, 2, 2, 2, 0, 1,
/*F0*/  2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 0,
    // clang-format on
};

static unsigned char mapping2[256] = {
    // clang-format off
/*      0  1  2  3  4  5  6  7  8  9  A  B  C  D  E  F */
/*00*/  0, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
/*10*/  1, 3, 2, 2, 2, 2, 2, 2, 1, 2, 2, 2, 2, 2, 2, 2,
/*20*/  1, 1, 3, 2, 1, 1, 2, 2, 1, 1, 2, 2, 1, 1, 2, 2,
/*30*/  1, 1, 1, 3, 1, 1, 2, 2, 1, 1, 1, 2, 1, 1, 2, 2,
/*40*/  1, 1, 2, 2, 3, 2, 2, 2, 1, 1, 2, 2, 2, 2, 2, 2,
/*50*/  1, 1, 2, 2, 1, 3, 2, 2, 1, 1, 2, 2, 1, 2, 2, 2,
/*60*/  1, 1, 1, 1, 1, 1, 3, 2, 1, 1, 1, 1, 1, 1, 2, 2,
/*70*/  1, 1, 1, 1, 1, 1, 1, 3, 1, 1, 1, 1, 1, 1, 1, 2,
/*80*/  2, 1, 1, 1, 1, 1, 1, 1, 0, 1, 1, 1, 1, 1, 1, 1,
/*90*/  1, 1, 2, 2, 2, 2, 2, 2, 1, 3, 2, 2, 2, 2, 2, 2,
/*A0*/  1, 1, 1, 2, 1, 1, 2, 2, 1, 1, 3, 2, 1, 1, 2, 2,
/*B0*/  1, 1, 1, 1, 1, 1, 2, 2, 1, 1, 1, 3, 1, 1, 2, 2,
/*C0*/  1, 1, 2, 2, 1, 2, 2, 2, 1, 1, 2, 2, 3, 2, 2, 2,
/*D0*/  1, 1, 2, 2, 1, 1, 2, 2, 1, 1, 2, 2, 1, 3, 2, 2,
/*E0*/  1, 1, 1, 1, 1, 1, 1, 2, 1, 1, 1, 1, 1, 1, 3, 2,
/*F0*/  1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 3,
    // clang-format on
};

static void
set_lcd_cols(uint8_t mode_reg)
{
    const unsigned char *mapping = (mode_reg & 0x80) ? mapping2 : mapping1;

    for (uint16_t c = 0; c < 256; c++) {
        switch (mapping[c]) {
            case 0:
                lcdcols[c][0][0] = lcdcols[c][1][0] = green;
                lcdcols[c][0][1] = lcdcols[c][1][1] = green;
                break;

            case 1:
                lcdcols[c][0][0] = lcdcols[c][1][0] = lcdcols[c][1][1] = green;
                lcdcols[c][0][1]                                       = blue;
                break;

            case 2:
                lcdcols[c][0][0] = lcdcols[c][1][0] = lcdcols[c][1][1] = blue;
                lcdcols[c][0][1]                                       = green;
                break;

            case 3:
                lcdcols[c][0][0] = lcdcols[c][1][0] = blue;
                lcdcols[c][0][1] = lcdcols[c][1][1] = blue;
                break;

            default:
                break;
        }
    }
}

static uint8_t
vid_in_200(uint16_t addr, void *priv)
{
    amsvid_t *vid = (amsvid_t *) priv;
    cga_t    *cga = &vid->cga;
    mda_t    *mda = &vid->mda;
    uint8_t   ret;

    switch (addr) {
        case 0x03b8:
            return (mda->ctrl);

        case 0x03d8:
            return (cga->cgamode);

        case 0x03dd:
            ret = vid->crtc_index;   /* Read NMI reason */
            vid->crtc_index &= 0x1f; /* Reset NMI reason */
            nmi = 0;                 /* And reset NMI flag */
            return ret;

        case 0x03de:
            return ((vid->operation_ctrl & 0xc7) | vid->dipswitches); /*External CGA*/

        case 0x03df:
            return (vid->reg_3df);

        default:
            break;
    }

    if (addr >= 0x3D0 && addr <= 0x3DF)
        return cga_in(addr, cga);

    if (addr >= 0x3B0 && addr <= 0x3BB)
        return mda_in(addr, mda);

    return 0xFF;
}

static void
vid_out_200(uint16_t addr, uint8_t val, void *priv)
{
    amsvid_t *vid = (amsvid_t *) priv;
    cga_t    *cga = &vid->cga;
    mda_t    *mda = &vid->mda;
    uint8_t   old;

    switch (addr) {
        /* MDA writes ============================================================== */
        case 0x3b1:
        case 0x3b3:
        case 0x3b5:
        case 0x3b7:
            /* Writes banned to CRTC registers 0-11? */
            if (!(vid->operation_ctrl & 0x40) && mda->crtcreg <= 11) {
                vid->crtc_index = 0x20 | (mda->crtcreg & 0x1f);
                if (vid->operation_ctrl & 0x80)
                    nmi_raise();
                vid->reg_3df = val;
                return;
            }
            old                     = mda->crtc[mda->crtcreg];
            mda->crtc[mda->crtcreg] = val & crtc_mask[mda->crtcreg];
            if (old != val) {
                if (mda->crtcreg < 0xe || mda->crtcreg > 0x10) {
                    vid->fullchange = changeframecount;
                    mda_recalctimings(mda);
                }
            }
            return;
        case 0x3b8:
            old       = mda->ctrl;
            mda->ctrl = val;
            if ((mda->ctrl ^ old) & 3)
                mda_recalctimings(mda);
            vid->crtc_index &= 0x1F;
            vid->crtc_index |= 0x80;
            if (vid->operation_ctrl & 0x80)
                nmi_raise();
            return;

        /* CGA writes ============================================================== */
        case 0x03d1:
        case 0x03d3:
        case 0x03d5:
        case 0x03d7:
            if (!(vid->operation_ctrl & 0x40) && cga->crtcreg <= 11) {
                vid->crtc_index = 0x20 | (cga->crtcreg & 0x1f);
                if (vid->operation_ctrl & 0x80)
                    nmi_raise();
                vid->reg_3df = val;
                return;
            }
            old                     = cga->crtc[cga->crtcreg];
            cga->crtc[cga->crtcreg] = val & crtc_mask[cga->crtcreg];
            if (old != val) {
                if (cga->crtcreg < 0xe || cga->crtcreg > 0x10) {
                    vid->fullchange = changeframecount;
                    cga_recalctimings(cga);
                }
            }
            return;

        case 0x03d8:
            old          = cga->cgamode;
            cga->cgamode = val;
            if ((cga->cgamode ^ old) & 3)
                cga_recalctimings(cga);
            vid->crtc_index &= 0x1f;
            vid->crtc_index |= 0x80;
            if (vid->operation_ctrl & 0x80)
                nmi_raise();
            else
                set_lcd_cols(val);
            return;

        /* PC200 control port writes ============================================== */
        case 0x03de:
            vid->crtc_index = 0x1f;
            /* NMI only seems to be triggered if the value being written has the high
             * bit set (enable NMI). So it only protects writes to this port if you
             * let it? */
            if (val & 0x80) {
                vid->operation_ctrl = val;
                vid->crtc_index |= 0x40;
                nmi_raise();
                return;
            }
            timer_disable(&vid->cga.timer);
            timer_disable(&vid->mda.timer);
            timer_disable(&vid->timer);
            vid->operation_ctrl = val;
            /* Bits 0 and 1 control emulation and output mode */
            amstrad_log("emulation and mode = %02X\n", val & 0x03);
            if (val & 1) /* Monitor */
                vid->emulation = (val & 2) ? PC200_MDA : PC200_CGA;
            else if (vid->type == AMS_PPC512)
                vid->emulation = (val & 2) ? PC200_LCDM : PC200_LCDC;
            else
                vid->emulation = PC200_TV;
            if (vid->emulation == PC200_CGA || vid->emulation == PC200_TV)
                timer_advance_u64(&vid->cga.timer, 1);
            else if (vid->emulation == PC200_MDA)
                timer_advance_u64(&vid->mda.timer, 1);
            else
                timer_advance_u64(&vid->timer, 1);

            /* Bit 2 disables the IDA. We don't support dynamic enabling
             * and disabling of the IDA (instead, PCEM disconnects the
             * IDA from the bus altogether) so don't implement this */

            /* Enable the appropriate memory ranges depending whether
             * the IDA is configured as MDA or CGA */
            if (vid->emulation == PC200_MDA || vid->emulation == PC200_LCDM) {
                mem_mapping_disable(&vid->cga.mapping);
                mem_mapping_enable(&vid->mda.mapping);
            } else {
                mem_mapping_disable(&vid->mda.mapping);
                mem_mapping_enable(&vid->cga.mapping);
            }
            return;

        default:
            break;
    }

    if (addr >= 0x3D0 && addr <= 0x3DF)
        cga_out(addr, val, cga);

    if (addr >= 0x3B0 && addr <= 0x3BB)
        mda_out(addr, val, mda);
}

static void
lcd_draw_char_80(amsvid_t *vid, uint32_t *buffer, uint8_t chr,
                 uint8_t attr, int drawcursor, int blink, int sc,
                 int mode160, uint8_t control)
{
    int      c;
    uint8_t  bits   = fontdat[chr + vid->cga.fontbase][sc];
    uint8_t  bright = 0;
    uint16_t mask;

    if (attr & 8) { /* bright */
        /* The brightness algorithm appears to be: replace any bit sequence 011
         * with 001 (assuming an extra 0 to the left of the byte).
         */
        bright = bits;
        for (c = 0, mask = 0x100; c < 7; c++, mask >>= 1) {
            if (((bits & mask) == 0) && ((bits & (mask >> 1)) != 0) && ((bits & (mask >> 2)) != 0))
                bright &= ~(mask >> 1);
        }
        bits = bright;
    }

    if (drawcursor)
        bits ^= 0xFF;

    for (c = 0, mask = 0x80; c < 8; c++, mask >>= 1) {
        if (mode160)
            buffer[c] = (attr & mask) ? blue : green;
        else if (control & 0x20) /* blinking */
            buffer[c] = lcdcols[attr & 0x7F][blink][(bits & mask) ? 1 : 0];
        else
            buffer[c] = lcdcols[attr][blink][(bits & mask) ? 1 : 0];
    }
}

static void
lcd_draw_char_40(amsvid_t *vid, uint32_t *buffer, uint8_t chr,
                 uint8_t attr, int drawcursor, int blink, int sc,
                 uint8_t control)
{
    uint8_t bits = fontdat[chr + vid->cga.fontbase][sc];
    uint8_t mask = 0x80;

    if (attr & 8) /* bright */
        bits = bits & (bits >> 1);
    if (drawcursor)
        bits ^= 0xFF;

    for (uint8_t c = 0; c < 8; c++, mask >>= 1) {
        if (control & 0x20) {
            buffer[c * 2] = buffer[c * 2 + 1] = lcdcols[attr & 0x7F][blink][(bits & mask) ? 1 : 0];
        } else {
            buffer[c * 2] = buffer[c * 2 + 1] = lcdcols[attr][blink][(bits & mask) ? 1 : 0];
        }
    }
}

static void
lcdm_poll(amsvid_t *vid)
{
    mda_t   *mda = &vid->mda;
    uint16_t ca  = (mda->crtc[15] | (mda->crtc[14] << 8)) & 0x3fff;
    int      drawcursor;
    int      x;
    int      oldvc;
    uint8_t  chr;
    uint8_t  attr;
    int      oldsc;
    int      blink;

    if (!mda->linepos) {
        timer_advance_u64(&vid->timer, mda->dispofftime);
        mda->stat |= 1;
        mda->linepos = 1;
        oldsc        = mda->sc;
        if ((mda->crtc[8] & 3) == 3)
            mda->sc = (mda->sc << 1) & 7;
        if (mda->dispon) {
            if (mda->displine < mda->firstline)
                mda->firstline = mda->displine;
            mda->lastline = mda->displine;
            for (x = 0; x < mda->crtc[1]; x++) {
                chr        = mda->vram[(mda->ma << 1) & 0xfff];
                attr       = mda->vram[((mda->ma << 1) + 1) & 0xfff];
                drawcursor = ((mda->ma == ca) && mda->con && mda->cursoron);
                blink      = ((mda->blink & 16) && (mda->ctrl & 0x20) && (attr & 0x80) && !drawcursor);

                lcd_draw_char_80(vid, &(buffer32->line[mda->displine])[x * 8], chr, attr, drawcursor, blink, mda->sc, 0, mda->ctrl);
                mda->ma++;
            }
        }
        mda->sc = oldsc;
        if (mda->vc == mda->crtc[7] && !mda->sc)
            mda->stat |= 8;
        mda->displine++;
        if (mda->displine >= 500)
            mda->displine = 0;
    } else {
        timer_advance_u64(&vid->timer, mda->dispontime);
        if (mda->dispon)
            mda->stat &= ~1;
        mda->linepos = 0;
        if (mda->vsynctime) {
            mda->vsynctime--;
            if (!mda->vsynctime)
                mda->stat &= ~8;
        }
        if (mda->sc == (mda->crtc[11] & 31) || ((mda->crtc[8] & 3) == 3 && mda->sc == ((mda->crtc[11] & 31) >> 1))) {
            mda->con  = 0;
            mda->coff = 1;
        }
        if (mda->vadj) {
            mda->sc++;
            mda->sc &= 31;
            mda->ma = mda->maback;
            mda->vadj--;
            if (!mda->vadj) {
                mda->dispon = 1;
                mda->ma = mda->maback = (mda->crtc[13] | (mda->crtc[12] << 8)) & 0x3fff;
                mda->sc               = 0;
            }
        } else if (mda->sc == mda->crtc[9] || ((mda->crtc[8] & 3) == 3 && mda->sc == (mda->crtc[9] >> 1))) {
            mda->maback = mda->ma;
            mda->sc     = 0;
            oldvc       = mda->vc;
            mda->vc++;
            mda->vc &= 127;
            if (mda->vc == mda->crtc[6])
                mda->dispon = 0;
            if (oldvc == mda->crtc[4]) {
                mda->vc   = 0;
                mda->vadj = mda->crtc[5];
                if (!mda->vadj)
                    mda->dispon = 1;
                if (!mda->vadj)
                    mda->ma = mda->maback = (mda->crtc[13] | (mda->crtc[12] << 8)) & 0x3fff;
                if ((mda->crtc[10] & 0x60) == 0x20)
                    mda->cursoron = 0;
                else
                    mda->cursoron = mda->blink & 16;
            }
            if (mda->vc == mda->crtc[7]) {
                mda->dispon    = 0;
                mda->displine  = 0;
                mda->vsynctime = 16;
                if (mda->crtc[7]) {
                    x = mda->crtc[1] * 8;
                    mda->lastline++;
                    if ((x != xsize) || ((mda->lastline - mda->firstline) != ysize) || video_force_resize_get()) {
                        xsize = x;
                        ysize = mda->lastline - mda->firstline;
                        if (xsize < 64)
                            xsize = 656;
                        if (ysize < 32)
                            ysize = 200;
                        set_screen_size(xsize, ysize);

                        if (video_force_resize_get())
                            video_force_resize_set(0);
                    }
                    video_blit_memtoscreen(0, mda->firstline, xsize, ysize);
                    frames++;
                    video_res_x = mda->crtc[1];
                    video_res_y = mda->crtc[6];
                    video_bpp   = 0;
                }
                mda->firstline = 1000;
                mda->lastline  = 0;
                mda->blink++;
            }
        } else {
            mda->sc++;
            mda->sc &= 31;
            mda->ma = mda->maback;
        }
        if (mda->sc == (mda->crtc[10] & 31) || ((mda->crtc[8] & 3) == 3 && mda->sc == ((mda->crtc[10] & 31) >> 1)))
            mda->con = 1;
    }
}

static void
lcdc_poll(amsvid_t *vid)
{
    cga_t   *cga = &vid->cga;
    int      drawcursor;
    int      x;
    int      xs_temp;
    int      ys_temp;
    int      oldvc;
    uint8_t  chr;
    uint8_t  attr;
    uint16_t dat;
    int      oldsc;
    uint16_t ca;
    int      blink;

    ca = (cga->crtc[15] | (cga->crtc[14] << 8)) & 0x3fff;

    if (!cga->linepos) {
        timer_advance_u64(&vid->timer, cga->dispofftime);
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

            if (cga->cgamode & 1) {
                for (x = 0; x < cga->crtc[1]; x++) {
                    chr        = cga->charbuffer[x << 1];
                    attr       = cga->charbuffer[(x << 1) + 1];
                    drawcursor = ((cga->ma == ca) && cga->con && cga->cursoron);
                    blink      = ((cga->cgablink & 16) && (cga->cgamode & 0x20) && (attr & 0x80) && !drawcursor);
                    lcd_draw_char_80(vid, &(buffer32->line[cga->displine << 1])[x * 8], chr, attr, drawcursor, blink, cga->sc, cga->cgamode & 0x40, cga->cgamode);
                    lcd_draw_char_80(vid, &(buffer32->line[(cga->displine << 1) + 1])[x * 8], chr, attr, drawcursor, blink, cga->sc, cga->cgamode & 0x40, cga->cgamode);
                    cga->ma++;
                }
            } else if (!(cga->cgamode & 2)) {
                for (x = 0; x < cga->crtc[1]; x++) {
                    chr        = cga->vram[(cga->ma << 1) & 0x3fff];
                    attr       = cga->vram[((cga->ma << 1) + 1) & 0x3fff];
                    drawcursor = ((cga->ma == ca) && cga->con && cga->cursoron);
                    blink      = ((cga->cgablink & 16) && (cga->cgamode & 0x20) && (attr & 0x80) && !drawcursor);
                    lcd_draw_char_40(vid, &(buffer32->line[cga->displine << 1])[x * 16], chr, attr, drawcursor, blink, cga->sc, cga->cgamode);
                    lcd_draw_char_40(vid, &(buffer32->line[(cga->displine << 1) + 1])[x * 16], chr, attr, drawcursor, blink, cga->sc, cga->cgamode);
                    cga->ma++;
                }
            } else { /* Graphics mode */
                for (x = 0; x < cga->crtc[1]; x++) {
                    dat = (cga->vram[((cga->ma << 1) & 0x1fff) + ((cga->sc & 1) * 0x2000)] << 8) | cga->vram[((cga->ma << 1) & 0x1fff) + ((cga->sc & 1) * 0x2000) + 1];
                    cga->ma++;
                    for (uint8_t c = 0; c < 16; c++) {
                        buffer32->line[cga->displine << 1][(x << 4) + c] = buffer32->line[(cga->displine << 1) + 1][(x << 4) + c] = (dat & 0x8000) ? blue : green;
                        dat <<= 1;
                    }
                }
            }
        } else {
            if (cga->cgamode & 1) {
                hline(buffer32, 0, (cga->displine << 1), (cga->crtc[1] << 3), green);
                hline(buffer32, 0, (cga->displine << 1) + 1, (cga->crtc[1] << 3), green);
            } else {
                hline(buffer32, 0, (cga->displine << 1), (cga->crtc[1] << 4), green);
                hline(buffer32, 0, (cga->displine << 1) + 1, (cga->crtc[1] << 4), green);
            }
        }

        if (cga->cgamode & 1)
            x = (cga->crtc[1] << 3);
        else
            x = (cga->crtc[1] << 4);

        cga->sc = oldsc;
        if (cga->vc == cga->crtc[7] && !cga->sc)
            cga->cgastat |= 8;
        cga->displine++;
        if (cga->displine >= 360)
            cga->displine = 0;
    } else {
        timer_advance_u64(&vid->timer, cga->dispontime);
        cga->linepos = 0;
        if (cga->vsynctime) {
            cga->vsynctime--;
            if (!cga->vsynctime)
                cga->cgastat &= ~8;
        }
        if (cga->sc == (cga->crtc[11] & 31) || ((cga->crtc[8] & 3) == 3 && cga->sc == ((cga->crtc[11] & 31) >> 1))) {
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
                if (!cga->vadj)
                    cga->cgadispon = 1;
                if (!cga->vadj)
                    cga->ma = cga->maback = (cga->crtc[13] | (cga->crtc[12] << 8)) & 0x3fff;
                if ((cga->crtc[10] & 0x60) == 0x20)
                    cga->cursoron = 0;
                else
                    cga->cursoron = cga->cgablink & 8;
            }

            if (cga->vc == cga->crtc[7]) {
                cga->cgadispon = 0;
                cga->displine  = 0;
                cga->vsynctime = 16;
                if (cga->crtc[7]) {
                    if (cga->cgamode & 1)
                        x = (cga->crtc[1] << 3);
                    else
                        x = (cga->crtc[1] << 4);
                    cga->lastline++;

                    xs_temp = x;
                    ys_temp = (cga->lastline - cga->firstline) << 1;

                    if ((xs_temp > 0) && (ys_temp > 0)) {
                        if (xs_temp < 64)
                            xs_temp = 640;
                        if (ys_temp < 32)
                            ys_temp = 400;

                        if ((cga->cgamode & 8) && ((xs_temp != xsize) || (ys_temp != ysize) || video_force_resize_get())) {
                            xsize = xs_temp;
                            ysize = ys_temp;
                            set_screen_size(xsize, ysize);

                            if (video_force_resize_get())
                                video_force_resize_set(0);
                        }

                        video_blit_memtoscreen(0, cga->firstline << 1,
                                               xsize, (cga->lastline - cga->firstline) << 1);
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
        if (cga->sc == (cga->crtc[10] & 31) || ((cga->crtc[8] & 3) == 3 && cga->sc == ((cga->crtc[10] & 31) >> 1)))
            cga->con = 1;
        if (cga->cgadispon && (cga->cgamode & 1)) {
            for (x = 0; x < (cga->crtc[1] << 1); x++)
                cga->charbuffer[x] = cga->vram[((cga->ma << 1) + x) & 0x3fff];
        }
    }
}

static void
vid_poll_200(void *priv)
{
    amsvid_t *vid = (amsvid_t *) priv;

    switch (vid->emulation) {
        case PC200_LCDM:
            lcdm_poll(vid);
            return;
        case PC200_LCDC:
            lcdc_poll(vid);
            return;

        default:
            break;
    }
}

static void
vid_init_200(amstrad_t *ams)
{
    amsvid_t *vid;
    cga_t    *cga;
    mda_t    *mda;

    /* Allocate a video controller block. */
    vid = (amsvid_t *) calloc(1, sizeof(amsvid_t));

    vid->emulation = device_get_config_int("video_emulation");

    /* Default to CGA */
    vid->dipswitches = 0x10;
    vid->type        = ams->type;

    if (ams->type == AMS_PC200)
        switch (vid->emulation) {
            /* DIP switches for PC200. Switches 2,3 give video emulation.
             * Switch 1 is 'swap floppy drives' (not implemented) */
            case PC200_CGA:
                vid->dipswitches = 0x10;
                break;
            case PC200_MDA:
                vid->dipswitches = 0x30;
                break;
            case PC200_TV:
                vid->dipswitches = 0x00;
                break;
                /* The other combination is 'IDA disabled' (0x20) - see
                 * m_amstrad.c */

            default:
                break;
        }
    else
        switch (vid->emulation) {
            /* DIP switches for PPC512. Switch 1 is CRT/LCD. Switch 2
             * is MDA / CGA. Switch 3 disables IDA, not implemented. */
            /* 1 = on, 0 = off
               SW1: off = crt, on = lcd;
               SW2: off = mda, on = cga;
               SW3: off = disable built-in card, on = enable */
            case PC200_CGA:
                vid->dipswitches = 0x08;
                break;
            case PC200_MDA:
                vid->dipswitches = 0x18;
                break;
            case PC200_LCDC:
                vid->dipswitches = 0x00;
                break;
            case PC200_LCDM:
                vid->dipswitches = 0x10;
                break;

            default:
                break;
        }

    cga       = &vid->cga;
    mda       = &vid->mda;
    cga->vram = mda->vram = malloc(0x4000);
    cga_init(cga);
    mda_init(mda);

    cga_palette = (device_get_config_int("display_type") << 1);
    ams_inform(vid);

    /* Attribute 8 is white on black (on a real MDA it's black on black) */
    mda_setcol(0x08, 0, 1, 15);
    mda_setcol(0x88, 0, 1, 15);
    /* Attribute 64 is black on black (on a real MDA it's white on black) */
    mda_setcol(0x40, 0, 1, 0);
    mda_setcol(0xC0, 0, 1, 0);

    cga->fontbase = (device_get_config_int("codepage") & 3) * 256;
    mda->fontbase = cga->fontbase;

    timer_add(&vid->timer, vid_poll_200, vid, 1);
    mem_mapping_add(&vid->mda.mapping, 0xb0000, 0x08000,
                    mda_read, NULL, NULL, mda_write, NULL, NULL, NULL, 0, mda);
    mem_mapping_add(&vid->cga.mapping, 0xb8000, 0x08000,
                    cga_read, NULL, NULL, cga_write, NULL, NULL, NULL, 0, cga);
    io_sethandler(0x03d0, 16, vid_in_200, NULL, NULL, vid_out_200, NULL, NULL, vid);
    io_sethandler(0x03b0, 0x000c, vid_in_200, NULL, NULL, vid_out_200, NULL, NULL, vid);

    overscan_x = overscan_y = 16;

    if (ams->type == AMS_PC200)
        vid->invert = 0;
    else
        vid->invert = device_get_config_int("invert");
    if (vid->invert) {
        blue  = makecol(0x1C, 0x71, 0x31);
        green = makecol(0x0f, 0x21, 0x3f);
    } else {
        green = makecol(0x1C, 0x71, 0x31);
        blue  = makecol(0x0f, 0x21, 0x3f);
    }
    cgapal_rebuild();
    set_lcd_cols(0);

    timer_disable(&vid->cga.timer);
    timer_disable(&vid->mda.timer);
    timer_disable(&vid->timer);
    if (vid->emulation == PC200_CGA || vid->emulation == PC200_TV)
        timer_enable(&vid->cga.timer);
    else if (vid->emulation == PC200_MDA)
        timer_enable(&vid->mda.timer);
    else
        timer_enable(&vid->timer);

    ams->vid = vid;
}

static void
vid_close_200(void *priv)
{
    amsvid_t *vid = (amsvid_t *) priv;

    if (vid->cga.vram != vid->mda.vram) {
        free(vid->cga.vram);
        free(vid->mda.vram);
    } else
        free(vid->cga.vram);

    vid->cga.vram = vid->mda.vram = NULL;

    free(vid);
}

const device_config_t vid_200_config[] = {
  /* TODO: Should have options here for:
  *
  * > Display port (TTL or RF)
  */
  // clang-format off
    {
        .name = "video_emulation",
        .description = "Display type",
        .type = CONFIG_SELECTION,
        .default_string = "",
        .default_int = PC200_CGA,
        .file_filter = "",
        .spinner = { 0 },
        .selection = {
            { .description = "CGA monitor", .value = PC200_CGA },
            { .description = "MDA monitor", .value = PC200_MDA },
            { .description = "Television",  .value = PC200_TV  },
            { .description = ""                                }
        }
    },
    {
        .name = "display_type",
        .description = "Monitor type",
        .type = CONFIG_SELECTION,
        .default_string = "",
        .default_int = 0,
        .file_filter = "",
        .spinner = { 0 },
        .selection = {
            { .description = "RGB",              .value = 0 },
            { .description = "RGB (no brown)",   .value = 4 },
            { .description = "Green Monochrome", .value = 1 },
            { .description = "Amber Monochrome", .value = 2 },
            { .description = "White Monochrome", .value = 3 },
            { .description = ""                             }
        }
    },
    {
        .name = "codepage",
        .description = "Hardware font",
        .type = CONFIG_SELECTION,
        .default_string = "",
        .default_int = 3,
        .file_filter = "",
        .spinner = { 0 },
        .selection = {
            { .description = "US English", .value = 3 },
            { .description = "Portugese",  .value = 2 },
            { .description = "Norwegian",  .value = 1 },
            { .description = "Greek",      .value = 0 },
            { .description = ""                       }
        }
    },
    {
        .name = "language",
        .description = "BIOS language",
        .type = CONFIG_SELECTION,
        .default_string = "",
        .default_int = 7,
        .file_filter = "",
        .spinner = { 0 },
        .selection = {
            { .description = "English",         .value = 7 },
            { .description = "German",          .value = 6 },
            { .description = "French",          .value = 5 },
            { .description = "Spanish",         .value = 4 },
            { .description = "Danish",          .value = 3 },
            { .description = "Swedish",         .value = 2 },
            { .description = "Italian",         .value = 1 },
            { .description = "Diagnostic mode", .value = 0 },
            { .description = ""                            }
        }
    },
    { .name = "", .description = "", .type = CONFIG_END }
  // clang-format on
};

const device_t vid_200_device = {
    .name          = "Amstrad PC200 (video)",
    .internal_name = "vid_200",
    .flags         = 0,
    .local         = 0,
    .init          = NULL,
    .close         = vid_close_200,
    .reset         = NULL,
    .available     = NULL,
    .speed_changed = vid_speed_changed_200,
    .force_redraw  = NULL,
    .config        = vid_200_config
};

const device_config_t vid_ppc512_config[] = {
  /* TODO: Should have options here for:
  *
  * > Display port (TTL or RF)
  */
  // clang-format off
    {
        .name = "video_emulation",
        .description = "Display type",
        .type = CONFIG_SELECTION,
        .default_string = "",
        .default_int = PC200_LCDC,
        .file_filter = "",
        .spinner = { 0 },
        .selection = {
            { .description = "CGA monitor",    .value = PC200_CGA  },
            { .description = "MDA monitor",    .value = PC200_MDA  },
            { .description = "LCD (CGA mode)", .value = PC200_LCDC },
            { .description = "LCD (MDA mode)", .value = PC200_LCDM },
            { .description = ""                                    }
        },
    },
    {
        .name = "display_type",
        .description = "Monitor type",
        .type = CONFIG_SELECTION,
        .default_string = "",
        .default_int = 0,
        .file_filter = "",
        .spinner = { 0 },
        .selection = {
            { .description = "RGB",              .value = 0 },
            { .description = "RGB (no brown)",   .value = 4 },
            { .description = "Green Monochrome", .value = 1 },
            { .description = "Amber Monochrome", .value = 2 },
            { .description = "White Monochrome", .value = 3 },
            { .description = ""                             }
        },
    },
    {
        .name = "codepage",
        .description = "Hardware font",
        .type = CONFIG_SELECTION,
        .default_string = "",
        .default_int = 3,
        .file_filter = "",
        .spinner = { 0 },
        .selection = {
            { .description = "US English", .value = 3 },
            { .description = "Portugese",  .value = 2 },
            { .description = "Norwegian",  .value = 1 },
            { .description = "Greek",      .value = 0 },
            { .description = ""                       }
        },
    },
    {
        .name = "language",
        .description = "BIOS language",
        .type = CONFIG_SELECTION,
        .default_string = "",
        .default_int = 7,
        .file_filter = "",
        .spinner = { 0 },
        .selection = {
            { .description = "English",         .value = 7 },
            { .description = "German",          .value = 6 },
            { .description = "French",          .value = 5 },
            { .description = "Spanish",         .value = 4 },
            { .description = "Danish",          .value = 3 },
            { .description = "Swedish",         .value = 2 },
            { .description = "Italian",         .value = 1 },
            { .description = "Diagnostic mode", .value = 0 },
            { .description = ""                            }
        }
    },
    {
        .name = "invert",
        .description = "Invert LCD colors",
        .type = CONFIG_BINARY,
        .default_string = "",
        .default_int = 0
    },
    { .name = "", .description = "", .type = CONFIG_END }
  // clang-format on
};

const device_t vid_ppc512_device = {
    .name          = "Amstrad PPC512 (video)",
    .internal_name = "vid_ppc512",
    .flags         = 0,
    .local         = 0,
    .init          = NULL,
    .close         = vid_close_200,
    .reset         = NULL,
    .available     = NULL,
    .speed_changed = vid_speed_changed_200,
    .force_redraw  = NULL,
    .config        = vid_ppc512_config
};

const device_config_t vid_pc2086_config[] = {
  // clang-format off
    {
        .name = "language",
        .description = "BIOS language",
        .type = CONFIG_SELECTION,
        .default_string = "",
        .default_int = 7,
        .file_filter = "",
        .spinner = { 0 },
        .selection = {
            { .description = "English",         .value = 7 },
            { .description = "Diagnostic mode", .value = 0 },
            { .description = ""                            }
        }
    },
    { .name = "", .description = "", .type = CONFIG_END }
  // clang-format on
};

const device_t vid_pc2086_device = {
    .name          = "Amstrad PC2086",
    .internal_name = "vid_pc2086",
    .flags         = 0,
    .local         = 0,
    .init          = NULL,
    .close         = NULL,
    .reset         = NULL,
    .available     = NULL,
    .speed_changed = NULL,
    .force_redraw  = NULL,
    .config        = vid_pc2086_config
};

const device_config_t vid_pc3086_config[] = {
  // clang-format off
    {
        .name = "language",
        .description = "BIOS language",
        .type = CONFIG_SELECTION,
        .default_string = "",
        .default_int = 7,
        .file_filter = "",
        .spinner = { 0 },
        .selection = {
            { .description = "English",         .value = 7 },
            { .description = "Diagnostic mode", .value = 3 },
            { .description = ""                            }
        }
    },
    { .name = "", .description = "", .type = CONFIG_END }
  // clang-format on
};

const device_t vid_pc3086_device = {
    .name          = "Amstrad PC3086",
    .internal_name = "vid_pc3086",
    .flags         = 0,
    .local         = 0,
    .init          = NULL,
    .close         = NULL,
    .reset         = NULL,
    .available     = NULL,
    .speed_changed = NULL,
    .force_redraw  = NULL,
    .config        = vid_pc3086_config
};

static void
ms_write(uint16_t addr, UNUSED(uint8_t val), UNUSED(void *priv))
{
    if ((addr == 0x78) || (addr == 0x79))
        mouse_clear_x();
    else
        mouse_clear_y();
}

static uint8_t
ms_read(uint16_t addr, UNUSED(void *priv))
{
    uint8_t ret;
    int     delta = 0;

    if ((addr == 0x78) || (addr == 0x79)) {
        mouse_subtract_x(&delta, NULL, -128, 127, 0);
        mouse_clear_x();
    } else {
        mouse_subtract_y(&delta, NULL, -128, 127, 1, 0);
        mouse_clear_y();
    }

    ret = (uint8_t) (int8_t) delta;

    return ret;
}

static int
ms_poll(void *priv)
{
    amstrad_t *ams = (amstrad_t *) priv;
    int b = mouse_get_buttons_ex();

    if ((b & 1) && !(ams->oldb & 1))
        keyboard_send(0x7e);
    if (!(b & 1) && (ams->oldb & 1))
        keyboard_send(0xfe);

    if ((b & 2) && !(ams->oldb & 2))
        keyboard_send(0x7d);
    if (!(b & 2) && (ams->oldb & 2))
        keyboard_send(0xfd);

    ams->oldb = b;

    return 0;
}

static void
kbd_adddata(uint16_t val)
{
    key_queue[key_queue_end] = val;
    key_queue_end            = (key_queue_end + 1) & 0xf;
}

static void
kbd_adddata_ex(uint16_t val)
{
    kbd_adddata_process(val, kbd_adddata);
}

static void
kbd_write(uint16_t port, uint8_t val, void *priv)
{
    amstrad_t *ams = (amstrad_t *) priv;

    amstrad_log("keyboard_amstrad : write %04X %02X %02X\n", port, val, ams->pb);

    switch (port) {
        case 0x61:
            /*
             * PortB - System Control.
             *
             *  7   Enable Status-1/Disable Keyboard Code on Port A.
             *  6   Enable incoming Keyboard Clock.
             *  5   Prevent external parity errors from causing NMI.
             *  4   Disable parity checking of on-board system Ram.
             *  3   Undefined (Not Connected).
             *  2   Enable Port C LSB / Disable MSB. (See 1.8.3)
             *  1   Speaker Drive.
             *  0   8253 GATE 2 (Speaker Modulate).
             *
             * This register is controlled by BIOS and/or ROS.
             */
            amstrad_log("AMSkb: write PB %02x (%02x)\n", val, ams->pb);
            if (!(ams->pb & 0x40) && (val & 0x40)) { /*Reset keyboard*/
                amstrad_log("AMSkb: reset keyboard\n");
                kbd_adddata(0xaa);
            }
            ams->pb = val;
            ppi.pb  = val;

            speaker_update();
            speaker_gated  = val & 0x01;
            speaker_enable = val & 0x02;
            if (speaker_enable)
                was_speaker_enable = 1;
            pit_devs[0].set_gate(pit_devs[0].data, 2, val & 0x01);

            if (val & 0x80) {
                /* Keyboard enabled, so enable PA reading. */
                ams->pa = 0x00;
            }
            break;

        case 0x63:
            break;

        case 0x64:
            ams->stat1 = val;
            break;

        case 0x65:
            ams->stat2 = val;
            break;

        case 0x66:
            softresetx86();
            break;

        default:
            amstrad_log("AMSkb: bad keyboard write %04X %02X\n", port, val);
    }
}

static uint8_t
kbd_read(uint16_t port, void *priv)
{
    amstrad_t *ams = (amstrad_t *) priv;
    uint8_t    ret = 0xff;

    switch (port) {
        case 0x60:
            if (ams->pb & 0x80) {
                /*
                 * PortA - System Status 1
                 *
                 *  7   Always 0                            (KBD7)
                 *  6   Second Floppy disk drive installed  (KBD6)
                 *  5   DDM1 - Default Display Mode bit 1   (KBD5)
                 *  4   DDM0 - Default Display Mode bit 0   (KBD4)
                 *  3   Always 1                            (KBD3)
                 *  2   Always 1                            (KBD2)
                 *  1   8087 NDP installed                  (KBD1)
                 *  0   Always 1                            (KBD0)
                 *
                 * DDM00
                 *    00 unknown, external color?
                 *    01 Color,alpha,40x25, bright white on black.
                 *    10 Color,alpha,80x25, bright white on black.
                 *    11 External Monochrome,80x25.
                 *
                 * Following a reset, the hardware selects VDU mode
                 * 2. The ROS then sets the initial VDU state based
                 * on the DDM value.
                 */
                ret = (ams->stat1 | 0x0d) & 0x7f;
            } else {
                ret = ams->pa;
                if (key_queue_start == key_queue_end)
                    ams->wantirq = 0;
                else {
                    ams->key_waiting = key_queue[key_queue_start];
                    key_queue_start  = (key_queue_start + 1) & 0xf;
                    ams->wantirq     = 1;
                }
            }
            break;

        case 0x61:
            ret = ams->pb;
            break;

        case 0x62:
            /*
             * PortC - System Status 2.
             *
             *  7   On-board system RAM parity error.
             *  6   External parity error (I/OCHCK from expansion bus).
             *  5   8253 PIT OUT2 output.
             *  4    Undefined (Not Connected).
             *-------------------------------------------
             *  LSB     MSB (depends on PB2)
             *-------------------------------------------
             *  3   RAM3    Undefined
             *  2   RAM2    Undefined
             *  1   RAM1    Undefined
             *  0   RAM0    RAM4
             *
             * PC7 is forced to 0 when on-board system RAM parity
             * checking is disabled by PB4.
             *
             * RAM4:0
             * 01110    512K bytes on-board.
             * 01111    544K bytes (32K external).
             * 10000    576K bytes (64K external).
             * 10001    608K bytes (96K external).
             * 10010    640K bytes (128K external or fitted on-board).
             */
            if (ams->pb & 0x04)
                ret = ams->stat2 & 0x0f;
            else
                ret = ams->stat2 >> 4;
            ret |= (ppispeakon ? 0x20 : 0);
            if (nmi)
                ret |= 0x40;
            break;

        default:
            amstrad_log("AMDkb: bad keyboard read %04X\n", port);
    }

    return ret;
}

static void
kbd_poll(void *priv)
{
    amstrad_t *ams = (amstrad_t *) priv;

    timer_advance_u64(&ams->send_delay_timer, 1000 * TIMER_USEC);

    if (ams->wantirq) {
        ams->wantirq = 0;
        ams->pa      = ams->key_waiting;
        picint(2);
    }

    if (key_queue_start != key_queue_end && !ams->pa) {
        ams->key_waiting = key_queue[key_queue_start];
        key_queue_start  = (key_queue_start + 1) & 0x0f;
        ams->wantirq     = 1;
    }
}

static void
ams_write(uint16_t port, uint8_t val, void *priv)
{
    amstrad_t *ams = (amstrad_t *) priv;

    switch (port) {
        case 0x0378:
        case 0x0379:
        case 0x037a:
            lpt_write(port, val, &lpt_ports[0]);
            break;

        case 0xdead:
            ams->dead = val;
            break;

        default:
            break;
    }
}

static uint8_t
ams_read(uint16_t port, void *priv)
{
    amstrad_t *ams = (amstrad_t *) priv;
    uint8_t    ret = 0xff;

    switch (port) {
        case 0x0378:
            ret = lpt_read(port, &lpt_ports[0]);
            break;

        case 0x0379: /* printer control, also set LK1-3.
                      * per John Elliott's site, this is xor'ed with 0x07
                      *   7 English Language.
                      *   6 German Language.
                      *   5 French Language.
                      *   4 Spanish Language.
                      *   3 Danish Language.
                      *   2 Swedish Language.
                      *   1 Italian Language.
                      *   0 Diagnostic Mode.
                      */
            ret = (lpt_read(port, &lpt_ports[0]) & 0xf8) | ams->language;
            break;

        case 0x037a: /* printer status */
            ret = lpt_read(port, &lpt_ports[0]) & 0x1f;

            switch (ams->type) {
                case AMS_PC1512:
                    ret |= 0x20;
                    break;

                case AMS_PC200:
                case AMS_PPC512:
                    if (video_is_cga())
                        ret |= 0x80;
                    else if (video_is_mda())
                        ret |= 0xc0;

                    if (fdc_read(0x037f, ams->fdc) & 0x80)
                        ret |= 0x20;
                    break;

                case AMS_PC1640:
                    if (video_is_cga())
                        ret |= 0x80;
                    else if (video_is_mda())
                        ret |= 0xc0;

                    switch (amstrad_latch & 0x7fffffff) {
                        case AMSTRAD_NOLATCH:
                            ret &= ~0x20;
                            break;
                        case AMSTRAD_SW9:
                            ret &= ~0x20;
                            break;
                        case AMSTRAD_SW10:
                            ret |= 0x20;
                            break;

                        default:
                            break;
                    }
                    break;

                default:
                    break;
            }
            break;

        case 0x03de:
            ret = 0x20;
            break;

        case 0xdead:
            ret = ams->dead;
            break;

        default:
            break;
    }

    return ret;
}

static const scancode scancode_pc200[512] = {
  // clang-format off
    { .mk = {            0 }, .brk = {                   0 } }, /* 000 */
    { .mk = {      0x01, 0 }, .brk = {             0x81, 0 } }, /* 001 */
    { .mk = {      0x02, 0 }, .brk = {             0x82, 0 } }, /* 002 */
    { .mk = {      0x03, 0 }, .brk = {             0x83, 0 } }, /* 003 */
    { .mk = {      0x04, 0 }, .brk = {             0x84, 0 } }, /* 004 */
    { .mk = {      0x05, 0 }, .brk = {             0x85, 0 } }, /* 005 */
    { .mk = {      0x06, 0 }, .brk = {             0x86, 0 } }, /* 006 */
    { .mk = {      0x07, 0 }, .brk = {             0x87, 0 } }, /* 007 */
    { .mk = {      0x08, 0 }, .brk = {             0x88, 0 } }, /* 008 */
    { .mk = {      0x09, 0 }, .brk = {             0x89, 0 } }, /* 009 */
    { .mk = {      0x0a, 0 }, .brk = {             0x8a, 0 } }, /* 00a */
    { .mk = {      0x0b, 0 }, .brk = {             0x8b, 0 } }, /* 00b */
    { .mk = {      0x0c, 0 }, .brk = {             0x8c, 0 } }, /* 00c */
    { .mk = {      0x0d, 0 }, .brk = {             0x8d, 0 } }, /* 00d */
    { .mk = {      0x0e, 0 }, .brk = {             0x8e, 0 } }, /* 00e */
    { .mk = {      0x0f, 0 }, .brk = {             0x8f, 0 } }, /* 00f */
    { .mk = {      0x10, 0 }, .brk = {             0x90, 0 } }, /* 010 */
    { .mk = {      0x11, 0 }, .brk = {             0x91, 0 } }, /* 011 */
    { .mk = {      0x12, 0 }, .brk = {             0x92, 0 } }, /* 012 */
    { .mk = {      0x13, 0 }, .brk = {             0x93, 0 } }, /* 013 */
    { .mk = {      0x14, 0 }, .brk = {             0x94, 0 } }, /* 014 */
    { .mk = {      0x15, 0 }, .brk = {             0x95, 0 } }, /* 015 */
    { .mk = {      0x16, 0 }, .brk = {             0x96, 0 } }, /* 016 */
    { .mk = {      0x17, 0 }, .brk = {             0x97, 0 } }, /* 017 */
    { .mk = {      0x18, 0 }, .brk = {             0x98, 0 } }, /* 018 */
    { .mk = {      0x19, 0 }, .brk = {             0x99, 0 } }, /* 019 */
    { .mk = {      0x1a, 0 }, .brk = {             0x9a, 0 } }, /* 01a */
    { .mk = {      0x1b, 0 }, .brk = {             0x9b, 0 } }, /* 01b */
    { .mk = {      0x1c, 0 }, .brk = {             0x9c, 0 } }, /* 01c */
    { .mk = {      0x1d, 0 }, .brk = {             0x9d, 0 } }, /* 01d */
    { .mk = {      0x1e, 0 }, .brk = {             0x9e, 0 } }, /* 01e */
    { .mk = {      0x1f, 0 }, .brk = {             0x9f, 0 } }, /* 01f */
    { .mk = {      0x20, 0 }, .brk = {             0xa0, 0 } }, /* 020 */
    { .mk = {      0x21, 0 }, .brk = {             0xa1, 0 } }, /* 021 */
    { .mk = {      0x22, 0 }, .brk = {             0xa2, 0 } }, /* 022 */
    { .mk = {      0x23, 0 }, .brk = {             0xa3, 0 } }, /* 023 */
    { .mk = {      0x24, 0 }, .brk = {             0xa4, 0 } }, /* 024 */
    { .mk = {      0x25, 0 }, .brk = {             0xa5, 0 } }, /* 025 */
    { .mk = {      0x26, 0 }, .brk = {             0xa6, 0 } }, /* 026 */
    { .mk = {      0x27, 0 }, .brk = {             0xa7, 0 } }, /* 027 */
    { .mk = {      0x28, 0 }, .brk = {             0xa8, 0 } }, /* 028 */
    { .mk = {      0x29, 0 }, .brk = {             0xa9, 0 } }, /* 029 */
    { .mk = {      0x2a, 0 }, .brk = {             0xaa, 0 } }, /* 02a */
    { .mk = {      0x2b, 0 }, .brk = {             0xab, 0 } }, /* 02b */
    { .mk = {      0x2c, 0 }, .brk = {             0xac, 0 } }, /* 02c */
    { .mk = {      0x2d, 0 }, .brk = {             0xad, 0 } }, /* 02d */
    { .mk = {      0x2e, 0 }, .brk = {             0xae, 0 } }, /* 02e */
    { .mk = {      0x2f, 0 }, .brk = {             0xaf, 0 } }, /* 02f */
    { .mk = {      0x30, 0 }, .brk = {             0xb0, 0 } }, /* 030 */
    { .mk = {      0x31, 0 }, .brk = {             0xb1, 0 } }, /* 031 */
    { .mk = {      0x32, 0 }, .brk = {             0xb2, 0 } }, /* 032 */
    { .mk = {      0x33, 0 }, .brk = {             0xb3, 0 } }, /* 033 */
    { .mk = {      0x34, 0 }, .brk = {             0xb4, 0 } }, /* 034 */
    { .mk = {      0x35, 0 }, .brk = {             0xb5, 0 } }, /* 035 */
    { .mk = {      0x36, 0 }, .brk = {             0xb6, 0 } }, /* 036 */
    { .mk = {      0x37, 0 }, .brk = {             0xb7, 0 } }, /* 037 */
    { .mk = {      0x38, 0 }, .brk = {             0xb8, 0 } }, /* 038 */
    { .mk = {      0x39, 0 }, .brk = {             0xb9, 0 } }, /* 039 */
    { .mk = {      0x3a, 0 }, .brk = {             0xba, 0 } }, /* 03a */
    { .mk = {      0x3b, 0 }, .brk = {             0xbb, 0 } }, /* 03b */
    { .mk = {      0x3c, 0 }, .brk = {             0xbc, 0 } }, /* 03c */
    { .mk = {      0x3d, 0 }, .brk = {             0xbd, 0 } }, /* 03d */
    { .mk = {      0x3e, 0 }, .brk = {             0xbe, 0 } }, /* 03e */
    { .mk = {      0x3f, 0 }, .brk = {             0xbf, 0 } }, /* 03f */
    { .mk = {      0x40, 0 }, .brk = {             0xc0, 0 } }, /* 040 */
    { .mk = {      0x41, 0 }, .brk = {             0xc1, 0 } }, /* 041 */
    { .mk = {      0x42, 0 }, .brk = {             0xc2, 0 } }, /* 042 */
    { .mk = {      0x43, 0 }, .brk = {             0xc3, 0 } }, /* 043 */
    { .mk = {      0x44, 0 }, .brk = {             0xc4, 0 } }, /* 044 */
    { .mk = {      0x45, 0 }, .brk = {             0xc5, 0 } }, /* 045 */
    { .mk = {      0x46, 0 }, .brk = {             0xc6, 0 } }, /* 046 */
    { .mk = {      0x47, 0 }, .brk = {             0xc7, 0 } }, /* 047 */
    { .mk = {      0x48, 0 }, .brk = {             0xc8, 0 } }, /* 048 */
    { .mk = {      0x49, 0 }, .brk = {             0xc9, 0 } }, /* 049 */
    { .mk = {      0x4a, 0 }, .brk = {             0xca, 0 } }, /* 04a */
    { .mk = {      0x4b, 0 }, .brk = {             0xcb, 0 } }, /* 04b */
    { .mk = {      0x4c, 0 }, .brk = {             0xcc, 0 } }, /* 04c */
    { .mk = {      0x4d, 0 }, .brk = {             0xcd, 0 } }, /* 04d */
    { .mk = {      0x4e, 0 }, .brk = {             0xce, 0 } }, /* 04e */
    { .mk = {      0x4f, 0 }, .brk = {             0xcf, 0 } }, /* 04f */
    { .mk = {      0x50, 0 }, .brk = {             0xd0, 0 } }, /* 050 */
    { .mk = {      0x51, 0 }, .brk = {             0xd1, 0 } }, /* 051 */
    { .mk = {      0x52, 0 }, .brk = {             0xd2, 0 } }, /* 052 */
    { .mk = {      0x53, 0 }, .brk = {             0xd3, 0 } }, /* 053 */
    { .mk = {      0x54, 0 }, .brk = {             0xd4, 0 } }, /* 054 */
    { .mk = {      0x55, 0 }, .brk = {             0xd5, 0 } }, /* 055 */
    { .mk = {      0x56, 0 }, .brk = {             0xd6, 0 } }, /* 056 */
    { .mk = {      0x57, 0 }, .brk = {             0xd7, 0 } }, /* 057 */
    { .mk = {      0x58, 0 }, .brk = {             0xd8, 0 } }, /* 058 */
    { .mk = {      0x59, 0 }, .brk = {             0xd9, 0 } }, /* 059 */
    { .mk = {      0x5a, 0 }, .brk = {             0xda, 0 } }, /* 05a */
    { .mk = {      0x5b, 0 }, .brk = {             0xdb, 0 } }, /* 05b */
    { .mk = {      0x5c, 0 }, .brk = {             0xdc, 0 } }, /* 05c */
    { .mk = {      0x5d, 0 }, .brk = {             0xdd, 0 } }, /* 05d */
    { .mk = {      0x5e, 0 }, .brk = {             0xde, 0 } }, /* 05e */
    { .mk = {      0x5f, 0 }, .brk = {             0xdf, 0 } }, /* 05f */
    { .mk = {      0x60, 0 }, .brk = {             0xe0, 0 } }, /* 060 */
    { .mk = {      0x61, 0 }, .brk = {             0xe1, 0 } }, /* 061 */
    { .mk = {      0x62, 0 }, .brk = {             0xe2, 0 } }, /* 062 */
    { .mk = {      0x63, 0 }, .brk = {             0xe3, 0 } }, /* 063 */
    { .mk = {      0x64, 0 }, .brk = {             0xe4, 0 } }, /* 064 */
    { .mk = {      0x65, 0 }, .brk = {             0xe5, 0 } }, /* 065 */
    { .mk = {      0x66, 0 }, .brk = {             0xe6, 0 } }, /* 066 */
    { .mk = {      0x67, 0 }, .brk = {             0xe7, 0 } }, /* 067 */
    { .mk = {      0x68, 0 }, .brk = {             0xe8, 0 } }, /* 068 */
    { .mk = {      0x69, 0 }, .brk = {             0xe9, 0 } }, /* 069 */
    { .mk = {      0x6a, 0 }, .brk = {             0xea, 0 } }, /* 06a */
    { .mk = {      0x6b, 0 }, .brk = {             0xeb, 0 } }, /* 06b */
    { .mk = {      0x6c, 0 }, .brk = {             0xec, 0 } }, /* 06c */
    { .mk = {      0x6d, 0 }, .brk = {             0xed, 0 } }, /* 06d */
    { .mk = {      0x6e, 0 }, .brk = {             0xee, 0 } }, /* 06e */
    { .mk = {      0x6f, 0 }, .brk = {             0xef, 0 } }, /* 06f */
    { .mk = {      0x70, 0 }, .brk = {             0xf0, 0 } }, /* 070 */
    { .mk = {      0x71, 0 }, .brk = {             0xf1, 0 } }, /* 071 */
    { .mk = {      0x72, 0 }, .brk = {             0xf2, 0 } }, /* 072 */
    { .mk = {      0x73, 0 }, .brk = {             0xf3, 0 } }, /* 073 */
    { .mk = {      0x74, 0 }, .brk = {             0xf4, 0 } }, /* 074 */
    { .mk = {      0x75, 0 }, .brk = {             0xf5, 0 } }, /* 075 */
    { .mk = {      0x76, 0 }, .brk = {             0xf6, 0 } }, /* 076 */
    { .mk = {      0x77, 0 }, .brk = {             0xf7, 0 } }, /* 077 */
    { .mk = {      0x78, 0 }, .brk = {             0xf8, 0 } }, /* 078 */
    { .mk = {      0x79, 0 }, .brk = {             0xf9, 0 } }, /* 079 */
    { .mk = {      0x7a, 0 }, .brk = {             0xfa, 0 } }, /* 07a */
    { .mk = {      0x7b, 0 }, .brk = {             0xfb, 0 } }, /* 07b */
    { .mk = {      0x7c, 0 }, .brk = {             0xfc, 0 } }, /* 07c */
    { .mk = {      0x7d, 0 }, .brk = {             0xfd, 0 } }, /* 07d */
    { .mk = {      0x7e, 0 }, .brk = {             0xfe, 0 } }, /* 07e */
    { .mk = {      0x7f, 0 }, .brk = {             0xff, 0 } }, /* 07f */
    { .mk = {      0x80, 0 }, .brk = {                   0 } }, /* 080 */
    { .mk = {      0x81, 0 }, .brk = {                   0 } }, /* 081 */
    { .mk = {      0x82, 0 }, .brk = {                   0 } }, /* 082 */
    { .mk = {            0 }, .brk = {                   0 } }, /* 083 */
    { .mk = {            0 }, .brk = {                   0 } }, /* 084 */
    { .mk = {      0x85, 0 }, .brk = {                   0 } }, /* 085 */
    { .mk = {      0x86, 0 }, .brk = {                   0 } }, /* 086 */
    { .mk = {      0x87, 0 }, .brk = {                   0 } }, /* 087 */
    { .mk = {      0x88, 0 }, .brk = {                   0 } }, /* 088 */
    { .mk = {      0x89, 0 }, .brk = {                   0 } }, /* 089 */
    { .mk = {      0x8a, 0 }, .brk = {                   0 } }, /* 08a */
    { .mk = {      0x8b, 0 }, .brk = {                   0 } }, /* 08b */
    { .mk = {      0x8c, 0 }, .brk = {                   0 } }, /* 08c */
    { .mk = {      0x8d, 0 }, .brk = {                   0 } }, /* 08d */
    { .mk = {      0x8e, 0 }, .brk = {                   0 } }, /* 08e */
    { .mk = {      0x8f, 0 }, .brk = {                   0 } }, /* 08f */
    { .mk = {      0x90, 0 }, .brk = {                   0 } }, /* 090 */
    { .mk = {      0x91, 0 }, .brk = {                   0 } }, /* 091 */
    { .mk = {      0x92, 0 }, .brk = {                   0 } }, /* 092 */
    { .mk = {      0x93, 0 }, .brk = {                   0 } }, /* 093 */
    { .mk = {      0x94, 0 }, .brk = {                   0 } }, /* 094 */
    { .mk = {      0x95, 0 }, .brk = {                   0 } }, /* 095 */
    { .mk = {      0x96, 0 }, .brk = {                   0 } }, /* 096 */
    { .mk = {      0x97, 0 }, .brk = {                   0 } }, /* 097 */
    { .mk = {      0x98, 0 }, .brk = {                   0 } }, /* 098 */
    { .mk = {      0x99, 0 }, .brk = {                   0 } }, /* 099 */
    { .mk = {      0x9a, 0 }, .brk = {                   0 } }, /* 09a */
    { .mk = {      0x9b, 0 }, .brk = {                   0 } }, /* 09b */
    { .mk = {      0x9c, 0 }, .brk = {                   0 } }, /* 09c */
    { .mk = {      0x9d, 0 }, .brk = {                   0 } }, /* 09d */
    { .mk = {      0x9e, 0 }, .brk = {                   0 } }, /* 09e */
    { .mk = {      0x9f, 0 }, .brk = {                   0 } }, /* 09f */
    { .mk = {      0xa0, 0 }, .brk = {                   0 } }, /* 0a0 */
    { .mk = {      0xa1, 0 }, .brk = {                   0 } }, /* 0a1 */
    { .mk = {      0xa2, 0 }, .brk = {                   0 } }, /* 0a2 */
    { .mk = {      0xa3, 0 }, .brk = {                   0 } }, /* 0a3 */
    { .mk = {      0xa4, 0 }, .brk = {                   0 } }, /* 0a4 */
    { .mk = {      0xa5, 0 }, .brk = {                   0 } }, /* 0a5 */
    { .mk = {      0xa6, 0 }, .brk = {                   0 } }, /* 0a6 */
    { .mk = {      0xa7, 0 }, .brk = {                   0 } }, /* 0a7 */
    { .mk = {      0xa8, 0 }, .brk = {                   0 } }, /* 0a8 */
    { .mk = {      0xa9, 0 }, .brk = {                   0 } }, /* 0a9 */
    { .mk = {      0xaa, 0 }, .brk = {                   0 } }, /* 0aa */
    { .mk = {      0xab, 0 }, .brk = {                   0 } }, /* 0ab */
    { .mk = {      0xac, 0 }, .brk = {                   0 } }, /* 0ac */
    { .mk = {      0xad, 0 }, .brk = {                   0 } }, /* 0ad */
    { .mk = {      0xae, 0 }, .brk = {                   0 } }, /* 0ae */
    { .mk = {      0xaf, 0 }, .brk = {                   0 } }, /* 0af */
    { .mk = {      0xb0, 0 }, .brk = {                   0 } }, /* 0b0 */
    { .mk = {      0xb1, 0 }, .brk = {                   0 } }, /* 0b1 */
    { .mk = {      0xb2, 0 }, .brk = {                   0 } }, /* 0b2 */
    { .mk = {      0xb3, 0 }, .brk = {                   0 } }, /* 0b3 */
    { .mk = {      0xb4, 0 }, .brk = {                   0 } }, /* 0b4 */
    { .mk = {      0xb5, 0 }, .brk = {                   0 } }, /* 0b5 */
    { .mk = {      0xb6, 0 }, .brk = {                   0 } }, /* 0b6 */
    { .mk = {      0xb7, 0 }, .brk = {                   0 } }, /* 0b7 */
    { .mk = {      0xb8, 0 }, .brk = {                   0 } }, /* 0b8 */
    { .mk = {      0xb9, 0 }, .brk = {                   0 } }, /* 0b9 */
    { .mk = {      0xba, 0 }, .brk = {                   0 } }, /* 0ba */
    { .mk = {      0xbb, 0 }, .brk = {                   0 } }, /* 0bb */
    { .mk = {      0xbc, 0 }, .brk = {                   0 } }, /* 0bc */
    { .mk = {      0xbd, 0 }, .brk = {                   0 } }, /* 0bd */
    { .mk = {      0xbe, 0 }, .brk = {                   0 } }, /* 0be */
    { .mk = {      0xbf, 0 }, .brk = {                   0 } }, /* 0bf */
    { .mk = {      0xc0, 0 }, .brk = {                   0 } }, /* 0c0 */
    { .mk = {      0xc1, 0 }, .brk = {                   0 } }, /* 0c1 */
    { .mk = {      0xc2, 0 }, .brk = {                   0 } }, /* 0c2 */
    { .mk = {      0xc3, 0 }, .brk = {                   0 } }, /* 0c3 */
    { .mk = {      0xc4, 0 }, .brk = {                   0 } }, /* 0c4 */
    { .mk = {      0xc5, 0 }, .brk = {                   0 } }, /* 0c5 */
    { .mk = {      0xc6, 0 }, .brk = {                   0 } }, /* 0c6 */
    { .mk = {      0xc7, 0 }, .brk = {                   0 } }, /* 0c7 */
    { .mk = {      0xc8, 0 }, .brk = {                   0 } }, /* 0c8 */
    { .mk = {      0xc9, 0 }, .brk = {                   0 } }, /* 0c9 */
    { .mk = {      0xca, 0 }, .brk = {                   0 } }, /* 0ca */
    { .mk = {      0xcb, 0 }, .brk = {                   0 } }, /* 0cb */
    { .mk = {      0xcc, 0 }, .brk = {                   0 } }, /* 0cc */
    { .mk = {      0xcd, 0 }, .brk = {                   0 } }, /* 0cd */
    { .mk = {      0xce, 0 }, .brk = {                   0 } }, /* 0ce */
    { .mk = {      0xcf, 0 }, .brk = {                   0 } }, /* 0cf */
    { .mk = {      0xd0, 0 }, .brk = {                   0 } }, /* 0d0 */
    { .mk = {      0xd1, 0 }, .brk = {                   0 } }, /* 0d1 */
    { .mk = {      0xd2, 0 }, .brk = {                   0 } }, /* 0d2 */
    { .mk = {      0xd3, 0 }, .brk = {                   0 } }, /* 0d3 */
    { .mk = {      0xd4, 0 }, .brk = {                   0 } }, /* 0d4 */
    { .mk = {      0xd5, 0 }, .brk = {                   0 } }, /* 0d5 */
    { .mk = {      0xd6, 0 }, .brk = {                   0 } }, /* 0d6 */
    { .mk = {      0xd7, 0 }, .brk = {                   0 } }, /* 0d7 */
    { .mk = {      0xd8, 0 }, .brk = {                   0 } }, /* 0d8 */
    { .mk = {      0xd9, 0 }, .brk = {                   0 } }, /* 0d9 */
    { .mk = {      0xda, 0 }, .brk = {                   0 } }, /* 0da */
    { .mk = {      0xdb, 0 }, .brk = {                   0 } }, /* 0db */
    { .mk = {      0xdc, 0 }, .brk = {                   0 } }, /* 0dc */
    { .mk = {      0xdd, 0 }, .brk = {                   0 } }, /* 0dd */
    { .mk = {      0xde, 0 }, .brk = {                   0 } }, /* 0de */
    { .mk = {      0xdf, 0 }, .brk = {                   0 } }, /* 0df */
    { .mk = {      0xe0, 0 }, .brk = {                   0 } }, /* 0e0 */
    { .mk = {      0xe1, 0 }, .brk = {                   0 } }, /* 0e1 */
    { .mk = {      0xe2, 0 }, .brk = {                   0 } }, /* 0e2 */
    { .mk = {      0xe3, 0 }, .brk = {                   0 } }, /* 0e3 */
    { .mk = {      0xe4, 0 }, .brk = {                   0 } }, /* 0e4 */
    { .mk = {      0xe5, 0 }, .brk = {                   0 } }, /* 0e5 */
    { .mk = {      0xe6, 0 }, .brk = {                   0 } }, /* 0e6 */
    { .mk = {      0xe7, 0 }, .brk = {                   0 } }, /* 0e7 */
    { .mk = {      0xe8, 0 }, .brk = {                   0 } }, /* 0e8 */
    { .mk = {      0xe9, 0 }, .brk = {                   0 } }, /* 0e9 */
    { .mk = {      0xea, 0 }, .brk = {                   0 } }, /* 0ea */
    { .mk = {      0xeb, 0 }, .brk = {                   0 } }, /* 0eb */
    { .mk = {      0xec, 0 }, .brk = {                   0 } }, /* 0ec */
    { .mk = {      0xed, 0 }, .brk = {                   0 } }, /* 0ed */
    { .mk = {      0xee, 0 }, .brk = {                   0 } }, /* 0ee */
    { .mk = {      0xef, 0 }, .brk = {                   0 } }, /* 0ef */
    { .mk = {            0 }, .brk = {                   0 } }, /* 0f0 */
    { .mk = {      0xf1, 0 }, .brk = {                   0 } }, /* 0f1 */
    { .mk = {      0xf2, 0 }, .brk = {                   0 } }, /* 0f2 */
    { .mk = {      0xf3, 0 }, .brk = {                   0 } }, /* 0f3 */
    { .mk = {      0xf4, 0 }, .brk = {                   0 } }, /* 0f4 */
    { .mk = {      0xf5, 0 }, .brk = {                   0 } }, /* 0f5 */
    { .mk = {      0xf6, 0 }, .brk = {                   0 } }, /* 0f6 */
    { .mk = {      0xf7, 0 }, .brk = {                   0 } }, /* 0f7 */
    { .mk = {      0xf8, 0 }, .brk = {                   0 } }, /* 0f8 */
    { .mk = {      0xf9, 0 }, .brk = {                   0 } }, /* 0f9 */
    { .mk = {      0xfa, 0 }, .brk = {                   0 } }, /* 0fa */
    { .mk = {      0xfb, 0 }, .brk = {                   0 } }, /* 0fb */
    { .mk = {      0xfc, 0 }, .brk = {                   0 } }, /* 0fc */
    { .mk = {      0xfd, 0 }, .brk = {                   0 } }, /* 0fd */
    { .mk = {      0xfe, 0 }, .brk = {                   0 } }, /* 0fe */
    { .mk = {      0xff, 0 }, .brk = {                   0 } }, /* 0ff */
    { .mk = {0xe1, 0x1d, 0 }, .brk = { 0xe1,       0x9d, 0 } }, /* 100 */
    { .mk = {0xe0, 0x01, 0 }, .brk = { 0xe0,       0x81, 0 } }, /* 101 */
    { .mk = {0xe0, 0x02, 0 }, .brk = { 0xe0,       0x82, 0 } }, /* 102 */
    { .mk = {0xe0, 0x03, 0 }, .brk = { 0xe0,       0x83, 0 } }, /* 103 */
    { .mk = {0xe0, 0x04, 0 }, .brk = { 0xe0,       0x84, 0 } }, /* 104 */
    { .mk = {0xe0, 0x05, 0 }, .brk = { 0xe0,       0x85, 0 } }, /* 105 */
    { .mk = {0xe0, 0x06, 0 }, .brk = { 0xe0,       0x86, 0 } }, /* 106 */
    { .mk = {0xe0, 0x07, 0 }, .brk = { 0xe0,       0x87, 0 } }, /* 107 */
    { .mk = {0xe0, 0x08, 0 }, .brk = { 0xe0,       0x88, 0 } }, /* 108 */
    { .mk = {0xe0, 0x09, 0 }, .brk = { 0xe0,       0x89, 0 } }, /* 109 */
    { .mk = {0xe0, 0x0a, 0 }, .brk = { 0xe0,       0x8a, 0 } }, /* 10a */
    { .mk = {0xe0, 0x0b, 0 }, .brk = { 0xe0,       0x8b, 0 } }, /* 10b */
    { .mk = {0xe0, 0x0c, 0 }, .brk = { 0xe0,       0x8c, 0 } }, /* 10c */
    { .mk = {            0 }, .brk = {                   0 } }, /* 10d */
    { .mk = {0xe0, 0x0e, 0 }, .brk = { 0xe0,       0x8e, 0 } }, /* 10e */
    { .mk = {0xe0, 0x0f, 0 }, .brk = { 0xe0,       0x8f, 0 } }, /* 10f */
    { .mk = {0xe0, 0x10, 0 }, .brk = { 0xe0,       0x90, 0 } }, /* 110 */
    { .mk = {0xe0, 0x11, 0 }, .brk = { 0xe0,       0x91, 0 } }, /* 111 */
    { .mk = {0xe0, 0x12, 0 }, .brk = { 0xe0,       0x92, 0 } }, /* 112 */
    { .mk = {0xe0, 0x13, 0 }, .brk = { 0xe0,       0x93, 0 } }, /* 113 */
    { .mk = {0xe0, 0x14, 0 }, .brk = { 0xe0,       0x94, 0 } }, /* 114 */
    { .mk = {0xe0, 0x15, 0 }, .brk = { 0xe0,       0x95, 0 } }, /* 115 */
    { .mk = {0xe0, 0x16, 0 }, .brk = { 0xe0,       0x96, 0 } }, /* 116 */
    { .mk = {0xe0, 0x17, 0 }, .brk = { 0xe0,       0x97, 0 } }, /* 117 */
    { .mk = {0xe0, 0x18, 0 }, .brk = { 0xe0,       0x98, 0 } }, /* 118 */
    { .mk = {0xe0, 0x19, 0 }, .brk = { 0xe0,       0x99, 0 } }, /* 119 */
    { .mk = {0xe0, 0x1a, 0 }, .brk = { 0xe0,       0x9a, 0 } }, /* 11a */
    { .mk = {0xe0, 0x1b, 0 }, .brk = { 0xe0,       0x9b, 0 } }, /* 11b */
    { .mk = {0xe0, 0x1c, 0 }, .brk = { 0xe0,       0x9c, 0 } }, /* 11c */
    { .mk = {0xe0, 0x1d, 0 }, .brk = { 0xe0,       0x9d, 0 } }, /* 11d */
    { .mk = {0xe0, 0x1e, 0 }, .brk = { 0xe0,       0x9e, 0 } }, /* 11e */
    { .mk = {0xe0, 0x1f, 0 }, .brk = { 0xe0,       0x9f, 0 } }, /* 11f */
    { .mk = {0xe0, 0x20, 0 }, .brk = { 0xe0,       0xa0, 0 } }, /* 120 */
    { .mk = {0xe0, 0x21, 0 }, .brk = { 0xe0,       0xa1, 0 } }, /* 121 */
    { .mk = {0xe0, 0x22, 0 }, .brk = { 0xe0,       0xa2, 0 } }, /* 122 */
    { .mk = {0xe0, 0x23, 0 }, .brk = { 0xe0,       0xa3, 0 } }, /* 123 */
    { .mk = {0xe0, 0x24, 0 }, .brk = { 0xe0,       0xa4, 0 } }, /* 124 */
    { .mk = {0xe0, 0x25, 0 }, .brk = { 0xe0,       0xa5, 0 } }, /* 125 */
    { .mk = {0xe0, 0x26, 0 }, .brk = { 0xe0,       0xa6, 0 } }, /* 126 */
    { .mk = {            0 }, .brk = {                   0 } }, /* 127 */
    { .mk = {            0 }, .brk = {                   0 } }, /* 128 */
    { .mk = {            0 }, .brk = {                   0 } }, /* 129 */
    { .mk = {            0 }, .brk = {                   0 } }, /* 12a */
    { .mk = {            0 }, .brk = {                   0 } }, /* 12b */
    { .mk = {0xe0, 0x2c, 0 }, .brk = { 0xe0,       0xac, 0 } }, /* 12c */
    { .mk = {0xe0, 0x2d, 0 }, .brk = { 0xe0,       0xad, 0 } }, /* 12d */
    { .mk = {0xe0, 0x2e, 0 }, .brk = { 0xe0,       0xae, 0 } }, /* 12e */
    { .mk = {0xe0, 0x2f, 0 }, .brk = { 0xe0,       0xaf, 0 } }, /* 12f */
    { .mk = {0xe0, 0x30, 0 }, .brk = { 0xe0,       0xb0, 0 } }, /* 130 */
    { .mk = {0xe0, 0x31, 0 }, .brk = { 0xe0,       0xb1, 0 } }, /* 131 */
    { .mk = {0xe0, 0x32, 0 }, .brk = { 0xe0,       0xb2, 0 } }, /* 132 */
    { .mk = {            0 }, .brk = {                   0 } }, /* 133 */
    { .mk = {0xe0, 0x34, 0 }, .brk = { 0xe0,       0xb4, 0 } }, /* 134 */
    { .mk = {0xe0, 0x35, 0 }, .brk = { 0xe0,       0xb5, 0 } }, /* 135 */
    { .mk = {            0 }, .brk = {                   0 } }, /* 136 */
    { .mk = {0xe0, 0x37, 0 }, .brk = { 0xe0,       0xb7, 0 } }, /* 137 */
    { .mk = {0xe0, 0x38, 0 }, .brk = { 0xe0,       0xb8, 0 } }, /* 138 */
    { .mk = {            0 }, .brk = {                   0 } }, /* 139 */
    { .mk = {0xe0, 0x3a, 0 }, .brk = { 0xe0,       0xba, 0 } }, /* 13a */
    { .mk = {0xe0, 0x3b, 0 }, .brk = { 0xe0,       0xbb, 0 } }, /* 13b */
    { .mk = {0xe0, 0x3c, 0 }, .brk = { 0xe0,       0xbc, 0 } }, /* 13c */
    { .mk = {0xe0, 0x3d, 0 }, .brk = { 0xe0,       0xbd, 0 } }, /* 13d */
    { .mk = {0xe0, 0x3e, 0 }, .brk = { 0xe0,       0xbe, 0 } }, /* 13e */
    { .mk = {0xe0, 0x3f, 0 }, .brk = { 0xe0,       0xbf, 0 } }, /* 13f */
    { .mk = {0xe0, 0x40, 0 }, .brk = { 0xe0,       0xc0, 0 } }, /* 140 */
    { .mk = {0xe0, 0x41, 0 }, .brk = { 0xe0,       0xc1, 0 } }, /* 141 */
    { .mk = {0xe0, 0x42, 0 }, .brk = { 0xe0,       0xc2, 0 } }, /* 142 */
    { .mk = {0xe0, 0x43, 0 }, .brk = { 0xe0,       0xc3, 0 } }, /* 143 */
    { .mk = {0xe0, 0x44, 0 }, .brk = { 0xe0,       0xc4, 0 } }, /* 144 */
    { .mk = {            0 }, .brk = {                   0 } }, /* 145 */
    { .mk = {0xe0, 0x46, 0 }, .brk = { 0xe0,       0xc6, 0 } }, /* 146 */
    { .mk = {0xe0, 0x47, 0 }, .brk = { 0xe0,       0xc7, 0 } }, /* 147 */
    { .mk = {0xe0, 0x48, 0 }, .brk = { 0xe0,       0xc8, 0 } }, /* 148 */
    { .mk = {0xe0, 0x49, 0 }, .brk = { 0xe0,       0xc9, 0 } }, /* 149 */
    { .mk = {            0 }, .brk = {                   0 } }, /* 14a */
    { .mk = {0xe0, 0x4b, 0 }, .brk = { 0xe0,       0xcb, 0 } }, /* 14b */
    { .mk = {0xe0, 0x4c, 0 }, .brk = { 0xe0,       0xcc, 0 } }, /* 14c */
    { .mk = {0xe0, 0x4d, 0 }, .brk = { 0xe0,       0xcd, 0 } }, /* 14d */
    { .mk = {0xe0, 0x4e, 0 }, .brk = { 0xe0,       0xce, 0 } }, /* 14e */
    { .mk = {0xe0, 0x4f, 0 }, .brk = { 0xe0,       0xcf, 0 } }, /* 14f */
    { .mk = {0xe0, 0x50, 0 }, .brk = { 0xe0,       0xd0, 0 } }, /* 150 */
    { .mk = {0xe0, 0x51, 0 }, .brk = { 0xe0,       0xd1, 0 } }, /* 151 */
    { .mk = {0xe0, 0x52, 0 }, .brk = { 0xe0,       0xd2, 0 } }, /* 152 */
    { .mk = {0xe0, 0x53, 0 }, .brk = { 0xe0,       0xd3, 0 } }, /* 153 */
    { .mk = {            0 }, .brk = {                   0 } }, /* 154 */
    { .mk = {0xe0, 0x55, 0 }, .brk = { 0xe0,       0xd5, 0 } }, /* 155 */
    { .mk = {            0 }, .brk = {                   0 } }, /* 156 */
    { .mk = {0xe0, 0x57, 0 }, .brk = { 0xe0,       0xd7, 0 } }, /* 157 */
    { .mk = {0xe0, 0x58, 0 }, .brk = { 0xe0,       0xd8, 0 } }, /* 158 */
    { .mk = {0xe0, 0x59, 0 }, .brk = { 0xe0,       0xd9, 0 } }, /* 159 */
    { .mk = {0xe0, 0x5a, 0 }, .brk = { 0xe0,       0xaa, 0 } }, /* 15a */
    { .mk = {0xe0, 0x5b, 0 }, .brk = { 0xe0,       0xdb, 0 } }, /* 15b */
    { .mk = {0xe0, 0x5c, 0 }, .brk = { 0xe0,       0xdc, 0 } }, /* 15c */
    { .mk = {0xe0, 0x5d, 0 }, .brk = { 0xe0,       0xdd, 0 } }, /* 15d */
    { .mk = {0xe0, 0x5e, 0 }, .brk = { 0xe0,       0xee, 0 } }, /* 15e */
    { .mk = {0xe0, 0x5f, 0 }, .brk = { 0xe0,       0xdf, 0 } }, /* 15f */
    { .mk = {            0 }, .brk = {                   0 } }, /* 160 */
    { .mk = {0xe0, 0x61, 0 }, .brk = { 0xe0,       0xe1, 0 } }, /* 161 */
    { .mk = {0xe0, 0x62, 0 }, .brk = { 0xe0,       0xe2, 0 } }, /* 162 */
    { .mk = {0xe0, 0x63, 0 }, .brk = { 0xe0,       0xe3, 0 } }, /* 163 */
    { .mk = {0xe0, 0x64, 0 }, .brk = { 0xe0,       0xe4, 0 } }, /* 164 */
    { .mk = {0xe0, 0x65, 0 }, .brk = { 0xe0,       0xe5, 0 } }, /* 165 */
    { .mk = {0xe0, 0x66, 0 }, .brk = { 0xe0,       0xe6, 0 } }, /* 166 */
    { .mk = {0xe0, 0x67, 0 }, .brk = { 0xe0,       0xe7, 0 } }, /* 167 */
    { .mk = {0xe0, 0x68, 0 }, .brk = { 0xe0,       0xe8, 0 } }, /* 168 */
    { .mk = {0xe0, 0x69, 0 }, .brk = { 0xe0,       0xe9, 0 } }, /* 169 */
    { .mk = {0xe0, 0x6a, 0 }, .brk = { 0xe0,       0xea, 0 } }, /* 16a */
    { .mk = {0xe0, 0x6b, 0 }, .brk = { 0xe0,       0xeb, 0 } }, /* 16b */
    { .mk = {0xe0, 0x6c, 0 }, .brk = { 0xe0,       0xec, 0 } }, /* 16c */
    { .mk = {0xe0, 0x6d, 0 }, .brk = { 0xe0,       0xed, 0 } }, /* 16d */
    { .mk = {0xe0, 0x6e, 0 }, .brk = { 0xe0,       0xee, 0 } }, /* 16e */
    { .mk = {            0 }, .brk = {                   0 } }, /* 16f */
    { .mk = {0xe0, 0x70, 0 }, .brk = { 0xe0,       0xf0, 0 } }, /* 170 */
    { .mk = {0xe0, 0x71, 0 }, .brk = { 0xe0,       0xf1, 0 } }, /* 171 */
    { .mk = {0xe0, 0x72, 0 }, .brk = { 0xe0,       0xf2, 0 } }, /* 172 */
    { .mk = {0xe0, 0x73, 0 }, .brk = { 0xe0,       0xf3, 0 } }, /* 173 */
    { .mk = {0xe0, 0x74, 0 }, .brk = { 0xe0,       0xf4, 0 } }, /* 174 */
    { .mk = {0xe0, 0x75, 0 }, .brk = { 0xe0,       0xf5, 0 } }, /* 175 */
    { .mk = {            0 }, .brk = {                   0 } }, /* 176 */
    { .mk = {0xe0, 0x77, 0 }, .brk = { 0xe0,       0xf7, 0 } }, /* 177 */
    { .mk = {0xe0, 0x78, 0 }, .brk = { 0xe0,       0xf8, 0 } }, /* 178 */
    { .mk = {0xe0, 0x79, 0 }, .brk = { 0xe0,       0xf9, 0 } }, /* 179 */
    { .mk = {0xe0, 0x7a, 0 }, .brk = { 0xe0,       0xfa, 0 } }, /* 17a */
    { .mk = {0xe0, 0x7b, 0 }, .brk = { 0xe0,       0xfb, 0 } }, /* 17b */
    { .mk = {0xe0, 0x7c, 0 }, .brk = { 0xe0,       0xfc, 0 } }, /* 17c */
    { .mk = {0xe0, 0x7d, 0 }, .brk = { 0xe0,       0xfd, 0 } }, /* 17d */
    { .mk = {0xe0, 0x7e, 0 }, .brk = { 0xe0,       0xfe, 0 } }, /* 17e */
    { .mk = {0xe0, 0x7f, 0 }, .brk = { 0xe0,       0xff, 0 } }, /* 17f */
    { .mk = {            0 }, .brk = {                   0 } }, /* 180 */
    { .mk = {            0 }, .brk = {                   0 } }, /* 181 */
    { .mk = {            0 }, .brk = {                   0 } }, /* 182 */
    { .mk = {            0 }, .brk = {                   0 } }, /* 183 */
    { .mk = {            0 }, .brk = {                   0 } }, /* 184 */
    { .mk = {            0 }, .brk = {                   0 } }, /* 185 */
    { .mk = {            0 }, .brk = {                   0 } }, /* 186 */
    { .mk = {            0 }, .brk = {                   0 } }, /* 187 */
    { .mk = {            0 }, .brk = {                   0 } }, /* 188 */
    { .mk = {            0 }, .brk = {                   0 } }, /* 189 */
    { .mk = {            0 }, .brk = {                   0 } }, /* 18a */
    { .mk = {            0 }, .brk = {                   0 } }, /* 18b */
    { .mk = {            0 }, .brk = {                   0 } }, /* 18c */
    { .mk = {            0 }, .brk = {                   0 } }, /* 18d */
    { .mk = {            0 }, .brk = {                   0 } }, /* 18e */
    { .mk = {            0 }, .brk = {                   0 } }, /* 18f */
    { .mk = {            0 }, .brk = {                   0 } }, /* 190 */
    { .mk = {            0 }, .brk = {                   0 } }, /* 191 */
    { .mk = {            0 }, .brk = {                   0 } }, /* 192 */
    { .mk = {            0 }, .brk = {                   0 } }, /* 193 */
    { .mk = {            0 }, .brk = {                   0 } }, /* 194 */
    { .mk = {            0 }, .brk = {                   0 } }, /* 195 */
    { .mk = {            0 }, .brk = {                   0 } }, /* 196 */
    { .mk = {            0 }, .brk = {                   0 } }, /* 197 */
    { .mk = {            0 }, .brk = {                   0 } }, /* 198 */
    { .mk = {            0 }, .brk = {                   0 } }, /* 199 */
    { .mk = {            0 }, .brk = {                   0 } }, /* 19a */
    { .mk = {            0 }, .brk = {                   0 } }, /* 19b */
    { .mk = {            0 }, .brk = {                   0 } }, /* 19c */
    { .mk = {            0 }, .brk = {                   0 } }, /* 19d */
    { .mk = {            0 }, .brk = {                   0 } }, /* 19e */
    { .mk = {            0 }, .brk = {                   0 } }, /* 19f */
    { .mk = {            0 }, .brk = {                   0 } }, /* 1a0 */
    { .mk = {            0 }, .brk = {                   0 } }, /* 1a1 */
    { .mk = {            0 }, .brk = {                   0 } }, /* 1a2 */
    { .mk = {            0 }, .brk = {                   0 } }, /* 1a3 */
    { .mk = {            0 }, .brk = {                   0 } }, /* 1a4 */
    { .mk = {            0 }, .brk = {                   0 } }, /* 1a5 */
    { .mk = {            0 }, .brk = {                   0 } }, /* 1a6 */
    { .mk = {            0 }, .brk = {                   0 } }, /* 1a7 */
    { .mk = {            0 }, .brk = {                   0 } }, /* 1a8 */
    { .mk = {            0 }, .brk = {                   0 } }, /* 1a9 */
    { .mk = {            0 }, .brk = {                   0 } }, /* 1aa */
    { .mk = {            0 }, .brk = {                   0 } }, /* 1ab */
    { .mk = {            0 }, .brk = {                   0 } }, /* 1ac */
    { .mk = {            0 }, .brk = {                   0 } }, /* 1ad */
    { .mk = {            0 }, .brk = {                   0 } }, /* 1ae */
    { .mk = {            0 }, .brk = {                   0 } }, /* 1af */
    { .mk = {            0 }, .brk = {                   0 } }, /* 1b0 */
    { .mk = {            0 }, .brk = {                   0 } }, /* 1b1 */
    { .mk = {            0 }, .brk = {                   0 } }, /* 1b2 */
    { .mk = {            0 }, .brk = {                   0 } }, /* 1b3 */
    { .mk = {            0 }, .brk = {                   0 } }, /* 1b4 */
    { .mk = {            0 }, .brk = {                   0 } }, /* 1b5 */
    { .mk = {            0 }, .brk = {                   0 } }, /* 1b6 */
    { .mk = {            0 }, .brk = {                   0 } }, /* 1b7 */
    { .mk = {            0 }, .brk = {                   0 } }, /* 1b8 */
    { .mk = {            0 }, .brk = {                   0 } }, /* 1b9 */
    { .mk = {            0 }, .brk = {                   0 } }, /* 1ba */
    { .mk = {            0 }, .brk = {                   0 } }, /* 1bb */
    { .mk = {            0 }, .brk = {                   0 } }, /* 1bc */
    { .mk = {            0 }, .brk = {                   0 } }, /* 1bd */
    { .mk = {            0 }, .brk = {                   0 } }, /* 1be */
    { .mk = {            0 }, .brk = {                   0 } }, /* 1bf */
    { .mk = {            0 }, .brk = {                   0 } }, /* 1c0 */
    { .mk = {            0 }, .brk = {                   0 } }, /* 1c1 */
    { .mk = {            0 }, .brk = {                   0 } }, /* 1c2 */
    { .mk = {            0 }, .brk = {                   0 } }, /* 1c3 */
    { .mk = {            0 }, .brk = {                   0 } }, /* 1c4 */
    { .mk = {            0 }, .brk = {                   0 } }, /* 1c5 */
    { .mk = {            0 }, .brk = {                   0 } }, /* 1c6 */
    { .mk = {            0 }, .brk = {                   0 } }, /* 1c7 */
    { .mk = {            0 }, .brk = {                   0 } }, /* 1c8 */
    { .mk = {            0 }, .brk = {                   0 } }, /* 1c9 */
    { .mk = {            0 }, .brk = {                   0 } }, /* 1ca */
    { .mk = {            0 }, .brk = {                   0 } }, /* 1cb */
    { .mk = {            0 }, .brk = {                   0 } }, /* 1cc */
    { .mk = {            0 }, .brk = {                   0 } }, /* 1cd */
    { .mk = {            0 }, .brk = {                   0 } }, /* 1ce */
    { .mk = {            0 }, .brk = {                   0 } }, /* 1cf */
    { .mk = {            0 }, .brk = {                   0 } }, /* 1d0 */
    { .mk = {            0 }, .brk = {                   0 } }, /* 1d1 */
    { .mk = {            0 }, .brk = {                   0 } }, /* 1d2 */
    { .mk = {            0 }, .brk = {                   0 } }, /* 1d3 */
    { .mk = {            0 }, .brk = {                   0 } }, /* 1d4 */
    { .mk = {            0 }, .brk = {                   0 } }, /* 1d5 */
    { .mk = {            0 }, .brk = {                   0 } }, /* 1d6 */
    { .mk = {            0 }, .brk = {                   0 } }, /* 1d7 */
    { .mk = {            0 }, .brk = {                   0 } }, /* 1d8 */
    { .mk = {            0 }, .brk = {                   0 } }, /* 1d9 */
    { .mk = {            0 }, .brk = {                   0 } }, /* 1da */
    { .mk = {            0 }, .brk = {                   0 } }, /* 1db */
    { .mk = {            0 }, .brk = {                   0 } }, /* 1dc */
    { .mk = {            0 }, .brk = {                   0 } }, /* 1dd */
    { .mk = {            0 }, .brk = {                   0 } }, /* 1de */
    { .mk = {            0 }, .brk = {                   0 } }, /* 1df */
    { .mk = {            0 }, .brk = {                   0 } }, /* 1e0 */
    { .mk = {0xe0, 0xe1, 0 }, .brk = {                   0 } }, /* 1e1 */
    { .mk = {            0 }, .brk = {                   0 } }, /* 1e2 */
    { .mk = {            0 }, .brk = {                   0 } }, /* 1e3 */
    { .mk = {            0 }, .brk = {                   0 } }, /* 1e4 */
    { .mk = {            0 }, .brk = {                   0 } }, /* 1e5 */
    { .mk = {            0 }, .brk = {                   0 } }, /* 1e6 */
    { .mk = {            0 }, .brk = {                   0 } }, /* 1e7 */
    { .mk = {            0 }, .brk = {                   0 } }, /* 1e8 */
    { .mk = {            0 }, .brk = {                   0 } }, /* 1e9 */
    { .mk = {            0 }, .brk = {                   0 } }, /* 1ea */
    { .mk = {            0 }, .brk = {                   0 } }, /* 1eb */
    { .mk = {            0 }, .brk = {                   0 } }, /* 1ec */
    { .mk = {            0 }, .brk = {                   0 } }, /* 1ed */
    { .mk = {0xe0, 0xee, 0 }, .brk = {                   0 } }, /* 1ee */
    { .mk = {            0 }, .brk = {                   0 } }, /* 1ef */
    { .mk = {            0 }, .brk = {                   0 } }, /* 1f0 */
    { .mk = {0xe0, 0xf1, 0 }, .brk = {                   0 } }, /* 1f1 */
    { .mk = {            0 }, .brk = {                   0 } }, /* 1f2 */
    { .mk = {            0 }, .brk = {                   0 } }, /* 1f3 */
    { .mk = {            0 }, .brk = {                   0 } }, /* 1f4 */
    { .mk = {            0 }, .brk = {                   0 } }, /* 1f5 */
    { .mk = {            0 }, .brk = {                   0 } }, /* 1f6 */
    { .mk = {            0 }, .brk = {                   0 } }, /* 1f7 */
    { .mk = {            0 }, .brk = {                   0 } }, /* 1f8 */
    { .mk = {            0 }, .brk = {                   0 } }, /* 1f9 */
    { .mk = {            0 }, .brk = {                   0 } }, /* 1fa */
    { .mk = {            0 }, .brk = {                   0 } }, /* 1fb */
    { .mk = {            0 }, .brk = {                   0 } }, /* 1fc */
    { .mk = {            0 }, .brk = {                   0 } }, /* 1fd */
    { .mk = {0xe0, 0xfe, 0 }, .brk = {                   0 } }, /* 1fe */
    { .mk = {0xe0, 0xff, 0 }, .brk = {                   0 } }  /* 1ff */
  // clang-format on
};

static void
machine_amstrad_init(const machine_t *model, int type)
{
    amstrad_t *ams;

    ams = (amstrad_t *) calloc(1, sizeof(amstrad_t));
    ams->type     = type;
    amstrad_latch = 0x80000000;

    switch (type) {
        case AMS_PC200:
        case AMS_PPC512:
            device_add(&amstrad_no_nmi_nvr_device);
            break;

        default:
            device_add(&amstrad_nvr_device);
            break;
    }

    machine_common_init(model);

    nmi_init();

    lpt1_remove_ams();
    lpt2_remove();

    io_sethandler(0x0378, 3,
                  ams_read, NULL, NULL, ams_write, NULL, NULL, ams);
    io_sethandler(0xdead, 1,
                  ams_read, NULL, NULL, ams_write, NULL, NULL, ams);

    switch (type) {
        case AMS_PC1512:
        case AMS_PC1640:
        case AMS_PC200:
        case AMS_PPC512:
            ams->fdc = device_add(&fdc_xt_device);
            break;

        case AMS_PC2086:
        case AMS_PC3086:
            ams->fdc = device_add(&fdc_at_actlow_device);
            break;

        default:
            break;
    }

    ams->language = 7;

    video_reset(gfxcard[0]);

    if (gfxcard[0] == VID_INTERNAL)
        switch (type) {
            case AMS_PC1512:
                loadfont("roms/machines/pc1512/40078", 8);
                device_context(&vid_1512_device);
                ams->language = device_get_config_int("language");
                vid_init_1512(ams);
                device_context_restore();
                device_add_ex(&vid_1512_device, ams->vid);
                break;

            case AMS_PPC512:
                loadfont("roms/machines/ppc512/40109", 1);
                device_context(&vid_ppc512_device);
                ams->language = device_get_config_int("language");
                vid_init_200(ams);
                device_context_restore();
                device_add_ex(&vid_ppc512_device, ams->vid);
                break;

            case AMS_PC1640:
                loadfont("roms/video/mda/mda.rom", 0);
                device_context(&vid_1640_device);
                ams->language = device_get_config_int("language");
                vid_init_1640(ams);
                device_context_restore();
                device_add_ex(&vid_1640_device, ams->vid);
                break;

            case AMS_PC200:
                loadfont("roms/machines/pc200/40109", 1);
                device_context(&vid_200_device);
                ams->language = device_get_config_int("language");
                vid_init_200(ams);
                device_context_restore();
                device_add_ex(&vid_200_device, ams->vid);
                break;

            case AMS_PC2086:
                device_context(&vid_pc2086_device);
                ams->language = device_get_config_int("language");
                device_context_restore();
                device_add(&paradise_pvga1a_pc2086_device);
                break;

            case AMS_PC3086:
                device_context(&vid_pc3086_device);
                ams->language = device_get_config_int("language");
                device_context_restore();
                device_add(&paradise_pvga1a_pc3086_device);
                break;

            default:
                break;
        }
    else if ((type == AMS_PC200) || (type == AMS_PPC512))
        io_sethandler(0x03de, 1,
                      ams_read, NULL, NULL, ams_write, NULL, NULL, ams);

    /* Initialize the (custom) keyboard/mouse interface. */
    ams->wantirq = 0;
    io_sethandler(0x0060, 7,
                  kbd_read, NULL, NULL, kbd_write, NULL, NULL, ams);
    timer_add(&ams->send_delay_timer, kbd_poll, ams, 1);
    if (type == AMS_PC1512)
        keyboard_set_table(scancode_xt);
    else
        keyboard_set_table(scancode_pc200);
    keyboard_send = kbd_adddata_ex;
    keyboard_scan = 1;
    keyboard_set_is_amstrad(((type == AMS_PC1512) || (type == AMS_PC1640)) ? 0 : 1);

    io_sethandler(0x0078, 2,
                  ms_read, NULL, NULL, ms_write, NULL, NULL, ams);
    io_sethandler(0x007a, 2,
                  ms_read, NULL, NULL, ms_write, NULL, NULL, ams);

    if (mouse_type == MOUSE_TYPE_INTERNAL) {
        /* Tell mouse driver about our internal mouse. */
        mouse_reset();
        mouse_set_buttons(2);
        mouse_set_poll(ms_poll, ams);
    }

    standalone_gameport_type = &gameport_device;
}

int
machine_pc1512_init(const machine_t *model)
{
    int ret;

    ret = bios_load_interleaved("roms/machines/pc1512/40044",
                                "roms/machines/pc1512/40043",
                                0x000fc000, 16384, 0);
    ret &= rom_present("roms/machines/pc1512/40078");

    if (bios_only || !ret)
        return ret;

    machine_amstrad_init(model, AMS_PC1512);

    return ret;
}

int
machine_pc1640_init(const machine_t *model)
{
    int ret;

    ret = bios_load_interleaved("roms/machines/pc1640/40044.v3",
                                "roms/machines/pc1640/40043.v3",
                                0x000fc000, 16384, 0);
    ret &= rom_present("roms/machines/pc1640/40100");

    if (bios_only || !ret)
        return ret;

    machine_amstrad_init(model, AMS_PC1640);

    return ret;
}

int
machine_pc200_init(const machine_t *model)
{
    int ret;

    ret = bios_load_interleaved("roms/machines/pc200/pc20v2.1",
                                "roms/machines/pc200/pc20v2.0",
                                0x000fc000, 16384, 0);
    ret &= rom_present("roms/machines/pc200/40109");

    if (bios_only || !ret)
        return ret;

    machine_amstrad_init(model, AMS_PC200);

    return ret;
}

int
machine_ppc512_init(const machine_t *model)
{
    int ret;

    ret = bios_load_interleaved("roms/machines/ppc512/40107.v2",
                                "roms/machines/ppc512/40108.v2",
                                0x000fc000, 16384, 0);
    ret &= rom_present("roms/machines/ppc512/40109");

    if (bios_only || !ret)
        return ret;

    machine_amstrad_init(model, AMS_PPC512);

    return ret;
}

int
machine_pc2086_init(const machine_t *model)
{
    int ret;

    ret = bios_load_interleavedr("roms/machines/pc2086/40179.ic129",
                                 "roms/machines/pc2086/40180.ic132",
                                 0x000fc000, 65536, 0);
    ret &= rom_present("roms/machines/pc2086/40186.ic171");

    if (bios_only || !ret)
        return ret;

    machine_amstrad_init(model, AMS_PC2086);

    return ret;
}

int
machine_pc3086_init(const machine_t *model)
{
    int ret;

    ret = bios_load_linearr("roms/machines/pc3086/fc00.bin",
                            0x000fc000, 65536, 0);
    ret &= rom_present("roms/machines/pc3086/c000.bin");

    if (bios_only || !ret)
        return ret;

    machine_amstrad_init(model, AMS_PC3086);

    return ret;
}
