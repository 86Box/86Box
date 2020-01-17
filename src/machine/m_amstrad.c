/*
 * 86Box	A hypervisor and IBM PC system emulator that specializes in
 *		running old operating systems and software designed for IBM
 *		PC systems and compatibles from 1981 through fairly recent
 *		system designs based on the PCI bus.
 *
 *		This file is part of the 86Box distribution.
 *
 *		Emulation of the Amstrad series of PC's: PC1512, PC1640 and
 *		PC200, including their keyboard, mouse and video devices, as
 *		well as the PC2086 and PC3086 systems.
 *
 * PC1512:	The PC1512 extends CGA with a bit-planar 640x200x16 mode.
 *		Most CRTC registers are fixed.
 *
 *		The Technical Reference Manual lists the video waitstate
 *		time as between 12 and 46 cycles. We currently always use
 *		the lower number.
 *
 * PC1640:	Mostly standard EGA, but with CGA & Hercules emulation.
 *
 * PC200:	CGA with some NMI stuff. But we don't need that as it's only
 *		used for TV and LCD displays, and we're emulating a CRT.
 *
 * PPC512/640:	Portable with both CGA-compatible and MDA-compatible monitors.
 *
 * TODO:	This module is not complete yet:
 *
 * All models:	The internal mouse controller does not work correctly with
 *		version 7.04 of the mouse driver.
 *
 * Version:	@(#)m_amstrad.c	1.0.21	2019/11/15
 *
 * Authors:	Sarah Walker, <http://pcem-emulator.co.uk/>
 *		Miran Grca, <mgrca8@gmail.com>
 *		Fred N. van Kempen, <decwiz@yahoo.com>
 *		John Elliott, <jce@seasip.info>
 *
 *		Copyright 2008-2019 Sarah Walker.
 *		Copyright 2016-2019 Miran Grca.
 *		Copyright 2017-2019 Fred N. van Kempen.
 *		Copyright 2019 John Elliott.
 */
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <wchar.h>
#define HAVE_STDARG_H
#include "../86box.h"
#include "../cpu/cpu.h"
#include "../timer.h"
#include "../io.h"
#include "../nmi.h"
#include "../pic.h"
#include "../pit.h"
#include "../ppi.h"
#include "../mem.h"
#include "../rom.h"
#include "../device.h"
#include "../nvr.h"
#include "../keyboard.h"
#include "../mouse.h"
#include "../game/gameport.h"
#include "../lpt.h"
#include "../floppy/fdd.h"
#include "../floppy/fdc.h"
#include "../sound/sound.h"
#include "../sound/snd_speaker.h"
#include "../video/video.h"
#include "../video/vid_cga.h"
#include "../video/vid_ega.h"
#include "../video/vid_mda.h"
#include "../video/vid_paradise.h"
#include "machine.h"
#include "m_amstrad.h"


#define STAT_PARITY     0x80
#define STAT_RTIMEOUT   0x40
#define STAT_TTIMEOUT   0x20
#define STAT_LOCK       0x10
#define STAT_CD         0x08
#define STAT_SYSFLAG    0x04
#define STAT_IFULL      0x02
#define STAT_OFULL      0x01


typedef struct {
    rom_t	bios_rom;		/* 1640 */
    cga_t	cga;			/* 1640/200 */
    mda_t	mda;			/* 1512/200/PPC512/640*/
    ega_t	ega;			/* 1640 */
    uint8_t	emulation;		/* Which display are we emulating? */
    uint8_t	dipswitches;		/* DIP switches 1-3 */
    uint8_t	crtc_index;		/* CRTC index readback
					 * Bit 7: CGA control port written
					 * Bit 6: Operation control port written
					 * Bit 5: CRTC register written
					 * Bits 0-4: Last CRTC register selected */
    uint8_t	operation_ctrl;
    uint8_t	reg_3df, type;
    uint8_t	crtc[32];
    int		crtcreg;
    int		cga_enabled;		/* 1640 */
    uint8_t	cgacol,
		cgamode,
		stat;
    uint8_t	plane_write,		/* 1512/200 */
		plane_read,		/* 1512/200 */
		border;			/* 1512/200 */
    int		fontbase;		/* 1512/200 */
    int		linepos,
		displine;
    int		sc, vc;
    int		cgadispon;
    int		con, coff,
		cursoron,
		cgablink;
    int	vsynctime;
    int		vadj;
    uint16_t	ma, maback;
    int		dispon;
    int		blink;
    uint64_t	dispontime,		/* 1512/1640 */
		dispofftime;		/* 1512/1640 */
    pc_timer_t	timer;			/* 1512/1640 */
    int		firstline,
		lastline;
    uint8_t	*vram;
    void	*ams;
} amsvid_t;

typedef struct {
    /* Machine stuff. */
    uint8_t	dead;
    uint8_t	stat1,
		stat2;
    uint8_t	type,
		language;

    /* Keyboard stuff. */
    int8_t	wantirq;
    uint8_t	key_waiting;
    uint8_t	pa;
    uint8_t	pb;
    pc_timer_t	send_delay_timer;

    /* Mouse stuff. */
    uint8_t	mousex,
		mousey;
    int		oldb;

    /* Video stuff. */
    amsvid_t	*vid;
    fdc_t	*fdc;
} amstrad_t;


int		amstrad_latch;


static uint8_t	key_queue[16];
static int	key_queue_start = 0,
		key_queue_end = 0;
static uint8_t	crtc_mask[32] = {
    0xff, 0xff, 0xff, 0xff, 0x7f, 0x1f, 0x7f, 0x7f,
    0xf3, 0x1f, 0x7f, 0x1f, 0x3f, 0xff, 0x3f, 0xff,
    0xff, 0xff, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00
};

static video_timings_t timing_pc1512   = {VIDEO_BUS, 0,0,0, 0,0,0}; /*PC1512 video code handles waitstates itself*/
static video_timings_t timing_pc1640   = {VIDEO_ISA, 8,16,32, 8,16,32};
static video_timings_t timing_pc200    = {VIDEO_ISA, 8,16,32, 8,16,32};


enum
{
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

   if (amstrad_do_log)
   {
	va_start(ap, fmt);
	pclog_ex(fmt, ap);
	va_end(ap);
   }
}
#else
#define amstrad_log(fmt, ...)
#endif


static void
recalc_timings_1512(amsvid_t *vid)
{
    double _dispontime, _dispofftime, disptime;

    disptime = /*128*/ 114; /*Fixed on PC1512*/
    _dispontime = 80;
    _dispofftime = disptime - _dispontime;
    _dispontime  *= CGACONST;
    _dispofftime *= CGACONST;
    vid->dispontime  = (uint64_t)_dispontime;
    vid->dispofftime = (uint64_t)_dispofftime;
}


static void
vid_out_1512(uint16_t addr, uint8_t val, void *priv)
{
    amsvid_t *vid = (amsvid_t *)priv;
    uint8_t old;

    switch (addr) {
	case 0x03d4:
		vid->crtcreg = val & 31;
		return;

	case 0x03d5:
		old = vid->crtc[vid->crtcreg];
		vid->crtc[vid->crtcreg] = val & crtc_mask[vid->crtcreg];
		if (old != val) {
			if (vid->crtcreg < 0xe || vid->crtcreg > 0x10) {
				fullchange = changeframecount;
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
    }
}


static uint8_t
vid_in_1512(uint16_t addr, void *priv)
{
    amsvid_t *vid = (amsvid_t *)priv;
    uint8_t ret = 0xff;

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
    }

    return(ret);
}


static void
vid_write_1512(uint32_t addr, uint8_t val, void *priv)
{
    amsvid_t *vid = (amsvid_t *)priv;

    egawrites++;
    sub_cycles(12);
    addr &= 0x3fff;

    if ((vid->cgamode & 0x12) == 0x12) {
	if (vid->plane_write & 1) vid->vram[addr] = val;
	if (vid->plane_write & 2) vid->vram[addr | 0x4000] = val;
	if (vid->plane_write & 4) vid->vram[addr | 0x8000] = val;
	if (vid->plane_write & 8) vid->vram[addr | 0xc000] = val;
    } else
	vid->vram[addr] = val;
}


static uint8_t
vid_read_1512(uint32_t addr, void *priv)
{
    amsvid_t *vid = (amsvid_t *)priv;

    egareads++;
    sub_cycles(12);
    addr &= 0x3fff;

    if ((vid->cgamode & 0x12) == 0x12)
	return(vid->vram[addr | (vid->plane_read << 14)]);

    return(vid->vram[addr]);
}


static void
vid_poll_1512(void *priv)
{
    amsvid_t *vid = (amsvid_t *)priv;
    uint16_t ca = (vid->crtc[15] | (vid->crtc[14] << 8)) & 0x3fff;
    int drawcursor;
    int x, c, xs_temp, ys_temp;
    uint8_t chr, attr;
    uint16_t dat, dat2, dat3, dat4;
    int cols[4];
    int col;
    int oldsc;

    if (! vid->linepos) {
	timer_advance_u64(&vid->timer, vid->dispofftime);
	vid->stat |= 1;
	vid->linepos = 1;
	oldsc = vid->sc;
	if (vid->dispon) {
		if (vid->displine < vid->firstline) {
			vid->firstline = vid->displine;
			video_wait_for_buffer();
		}
		vid->lastline = vid->displine;
		for (c = 0; c < 8; c++) {
			if ((vid->cgamode & 0x12) == 0x12) {
				buffer32->line[(vid->displine << 1)][c] = buffer32->line[(vid->displine << 1) + 1][c] = (vid->border & 15) + 16;
				if (vid->cgamode & 1) {
					buffer32->line[(vid->displine << 1)][c + (vid->crtc[1] << 3) + 8] =
					buffer32->line[(vid->displine << 1) + 1][c + (vid->crtc[1] << 3) + 8] = 0;
				} else {
					buffer32->line[(vid->displine << 1)][c + (vid->crtc[1] << 4) + 8] =
					buffer32->line[(vid->displine << 1)+ 1][c + (vid->crtc[1] << 4) + 8] = 0;
				}
			} else {
				buffer32->line[(vid->displine << 1)][c] = buffer32->line[(vid->displine << 1) + 1][c] = (vid->cgacol & 15) + 16;
				if (vid->cgamode & 1) {
					buffer32->line[(vid->displine << 1)][c + (vid->crtc[1] << 3) + 8] =
					buffer32->line[(vid->displine << 1) + 1][c + (vid->crtc[1] << 3) + 8] = (vid->cgacol & 15) + 16;
				} else {
					buffer32->line[(vid->displine << 1)][c + (vid->crtc[1] << 4) + 8] =
					buffer32->line[(vid->displine << 1) + 1][c + (vid->crtc[1] << 4) + 8] = (vid->cgacol & 15) + 16;
				}
			}
		}
		if (vid->cgamode & 1) {
			for (x = 0; x < 80; x++) {
				chr  = vid->vram[ ((vid->ma << 1) & 0x3fff)];
				attr = vid->vram[(((vid->ma << 1) + 1) & 0x3fff)];
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
						buffer32->line[(vid->displine << 1)][(x << 3) + c + 8] =
						buffer32->line[(vid->displine << 1) + 1][(x << 3) + c + 8] =
							cols[(fontdat[vid->fontbase + chr][vid->sc & 7] & (1 << (c ^ 7))) ? 1 : 0] ^ 15;
					}
				} else {
					for (c = 0; c < 8; c++) {
						buffer32->line[(vid->displine << 1)][(x << 3) + c + 8] =
						buffer32->line[(vid->displine << 1) + 1][(x << 3) + c + 8] =
							cols[(fontdat[vid->fontbase + chr][vid->sc & 7] & (1 << (c ^ 7))) ? 1 : 0];
					}
				}
				vid->ma++;
			}
		} else if (! (vid->cgamode & 2)) {
			for (x = 0; x < 40; x++) {
				chr = vid->vram[((vid->ma << 1) & 0x3fff)];
				attr = vid->vram[(((vid->ma << 1) + 1) & 0x3fff)];
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
						buffer32->line[(vid->displine << 1)][(x << 4) + (c << 1) + 8] =
						buffer32->line[(vid->displine << 1)][(x << 4) + (c << 1) + 1 + 8] =
						buffer32->line[(vid->displine << 1) + 1][(x << 4) + (c << 1) + 8] =
						buffer32->line[(vid->displine << 1) + 1][(x << 4) + (c << 1) + 1 + 8] =
							cols[(fontdat[vid->fontbase + chr][vid->sc & 7] & (1 << (c ^ 7))) ? 1 : 0] ^ 15;
						}
				} else {
					for (c = 0; c < 8; c++) {
						buffer32->line[(vid->displine << 1)][(x << 4) + (c << 1) + 8] = 
						buffer32->line[(vid->displine << 1)][(x << 4) + (c << 1) + 1 + 8] =
						buffer32->line[(vid->displine << 1) + 1][(x << 4) + (c << 1) + 8] = 
						buffer32->line[(vid->displine << 1) + 1][(x << 4) + (c << 1) + 1 + 8] =
							cols[(fontdat[vid->fontbase + chr][vid->sc & 7] & (1 << (c ^ 7))) ? 1 : 0];
					}
				}
			}
		} else if (! (vid->cgamode & 16)) {
			cols[0] = (vid->cgacol & 15) | 16;
			col = (vid->cgacol & 16) ? 24 : 16;
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
					buffer32->line[(vid->displine << 1)][(x << 4) + (c << 1) + 8] =
					buffer32->line[(vid->displine << 1)][(x << 4) + (c << 1) + 1 + 8] =
					buffer32->line[(vid->displine << 1) + 1][(x << 4) + (c << 1) + 8] =
					buffer32->line[(vid->displine << 1) + 1][(x << 4) + (c << 1) + 1 + 8] =
						cols[dat >> 14];
					dat <<= 2;
				}
			}
		} else {
			for (x = 0; x < 40; x++) {
				ca = ((vid->ma << 1) & 0x1fff) + ((vid->sc & 1) * 0x2000);
				dat  = (vid->vram[ca] << 8) | vid->vram[ca + 1];
				dat2 = (vid->vram[ca + 0x4000] << 8) | vid->vram[ca + 0x4001];
				dat3 = (vid->vram[ca + 0x8000] << 8) | vid->vram[ca + 0x8001];
				dat4 = (vid->vram[ca + 0xc000] << 8) | vid->vram[ca + 0xc001];

				vid->ma++;
				for (c = 0; c < 16; c++) {
					buffer32->line[(vid->displine << 1)][(x << 4) + c + 8] = buffer32->line[(vid->displine << 1) + 1][(x << 4) + c + 8] =
						(((dat >> 15) | ((dat2 >> 15) << 1) | ((dat3 >> 15) << 2) | ((dat4 >> 15) << 3)) & (vid->cgacol & 15)) + 16;
					dat  <<= 1;
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
		if (! vid->vsynctime)
		   vid->stat &= ~8;
	}
	if (vid->sc == (vid->crtc[11] & 31)) { 
		vid->con = 0; 
		vid->coff = 1; 
	}
	if (vid->vadj) {
		vid->sc++;
		vid->sc &= 31;
		vid->ma = vid->maback;
		vid->vadj--;
		if (! vid->vadj) {
			vid->dispon = 1;
			vid->ma = vid->maback = (vid->crtc[13] | (vid->crtc[12] << 8)) & 0x3fff;
			vid->sc = 0;
		}
	} else if (vid->sc == vid->crtc[9]) {
		vid->maback = vid->ma;
		vid->sc = 0;
		vid->vc++;
		vid->vc &= 127;

		if (vid->displine == 32) {
			vid->vc = 0;
			vid->vadj = 6;
			if ((vid->crtc[10] & 0x60) == 0x20)
				vid->cursoron = 0;
			  else
				vid->cursoron = vid->blink & 16;
		}

		if (vid->displine >= 262) {
			vid->dispon = 0;
			vid->displine = 0;
			vid->vsynctime = 46;

			if (vid->cgamode&1)
				x = (vid->crtc[1] << 3) + 16;
			  else
				x = (vid->crtc[1] << 4) + 16;
			vid->lastline++;

			xs_temp = x;
			ys_temp = (vid->lastline - vid->firstline) << 1;

			if ((xs_temp > 0) && (ys_temp > 0)) {
				if (xs_temp < 64) xs_temp = 656;
				if (ys_temp < 32) ys_temp = 400;
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
					video_blit_memtoscreen_8(0, (vid->firstline - 4) << 1, 0, ((vid->lastline - vid->firstline) + 8) << 1,
								 xsize, ((vid->lastline - vid->firstline) + 8) << 1);
				} else {
					video_blit_memtoscreen_8(8, vid->firstline << 1, 0, (vid->lastline - vid->firstline) << 1,
								 xsize, (vid->lastline - vid->firstline) << 1);
				}
			}

			video_res_x = xsize;
			video_res_y = ysize;
			if (vid->cgamode & 1) {
			video_res_x /= 8;
				video_res_y /= vid->crtc[9] + 1;
				video_bpp = 0;
			} else if (! (vid->cgamode & 2)) {
				video_res_x /= 16;
				video_res_y /= vid->crtc[9] + 1;
				video_bpp = 0;
			} else if (! (vid->cgamode & 16)) {
				video_res_x /= 2;
				video_bpp = 2;
			} else {
				video_bpp = 4;
			}

			vid->firstline = 1000;
			vid->lastline = 0;
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
    vid = (amsvid_t *)malloc(sizeof(amsvid_t));
    memset(vid, 0x00, sizeof(amsvid_t));

    video_inform(VIDEO_FLAG_TYPE_CGA, &timing_pc1512);

    vid->vram = malloc(0x10000);
    vid->cgacol = 7;
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
    amsvid_t *vid = (amsvid_t *)priv;

    free(vid->vram);

    free(vid);
}


static void
vid_speed_change_1512(void *priv)
{
    amsvid_t *vid = (amsvid_t *)priv;

    recalc_timings_1512(vid);
}


device_config_t vid_1512_config[] =
{
	{
		"display_type", "Display type", CONFIG_SELECTION, "", 0,
		{
			{
				"PC-CM (Colour)", 0
			},
			{
				"PC-MM (Monochrome)", 3
			},
			{
				""
			}
		}
	},
	{
        "codepage", "Hardware font", CONFIG_SELECTION, "", 3,
		{
			{
				"US English", 3
			},
			{
				"Danish", 1
			},
			{
				"Greek", 0
			},
			{
                ""
			}
		}
	},
	{
        "language", "BIOS language", CONFIG_SELECTION, "", 7,
		{
			{
				"English", 7
			},
			{
				"German", 6
			},
			{
				"French", 5
			},
			{
				"Spanish", 4
			},
			{
				"Danish", 3
			},
			{
				"Swedish", 2
			},
			{
				"Italian", 1
			},
			{
				"Diagnostic mode", 0
			},
			{
                ""
			}
		}
	},
	{
			"", "", -1
	}
};


static const device_t vid_1512_device = {
    "Amstrad PC1512 (video)",
    0, 0,
    NULL, vid_close_1512, NULL,
    NULL,
    vid_speed_change_1512,
    NULL,
    vid_1512_config
};


const device_t *
pc1512_get_device(void)
{
    return(&vid_1512_device);
}


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
	overscan_x = 16; overscan_y = 28;

	vid->dispontime  = vid->ega.dispontime;
	vid->dispofftime = vid->ega.dispofftime;
    }
}


static void
vid_out_1640(uint16_t addr, uint8_t val, void *priv)
{
    amsvid_t *vid = (amsvid_t *)priv;

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
			}
		}
		return;
    }

    if (vid->cga_enabled)
	cga_out(addr, val, &vid->cga);
      else
	ega_out(addr, val, &vid->ega);
}


static uint8_t
vid_in_1640(uint16_t addr, void *priv)
{
    amsvid_t *vid = (amsvid_t *)priv;

    if (vid->cga_enabled)
	return(cga_in(addr, &vid->cga));
      else
	return(ega_in(addr, &vid->ega));
}


static void
vid_init_1640(amstrad_t *ams)
{
    amsvid_t *vid;

    /* Allocate a video controller block. */
    vid = (amsvid_t *)malloc(sizeof(amsvid_t));
    memset(vid, 0x00, sizeof(amsvid_t));

    rom_init(&vid->bios_rom, L"roms/machines/pc1640/40100",
	     0xc0000, 0x8000, 0x7fff, 0, 0);

    ega_init(&vid->ega, 9, 0);
    vid->cga.vram = vid->ega.vram;
    vid->cga_enabled = 1;
    cga_init(&vid->cga);
    timer_disable(&vid->ega.timer);

    video_inform(VIDEO_FLAG_TYPE_SPECIAL, &timing_pc1640);

    mem_mapping_add(&vid->cga.mapping, 0xb8000, 0x08000,
	cga_read, NULL, NULL, cga_write, NULL, NULL, NULL, 0, &vid->cga);
    mem_mapping_add(&vid->ega.mapping, 0,       0,
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
    amsvid_t *vid = (amsvid_t *)priv;

    free(vid->ega.vram);

    free(vid);
}


static void
vid_speed_changed_1640(void *priv)
{
    amsvid_t *vid = (amsvid_t *)priv;

    recalc_timings_1640(vid);
}


device_config_t vid_1640_config[] =
{
	{
        "language", "BIOS language", CONFIG_SELECTION, "", 7,
		{
			{
				"English", 7
			},
			{
				"German", 6
			},
			{
				"French", 5
			},
			{
				"Spanish", 4
			},
			{
				"Danish", 3
			},
			{
				"Swedish", 2
			},
			{
				"Italian", 1
			},
			{
				"Diagnostic mode", 0
			},
			{
                ""
			}
		}
	},
        {
                "", "", -1
        }
};

static const device_t vid_1640_device = {
    "Amstrad PC1640 (video)",
    0, 0,
    NULL, vid_close_1640, NULL,
    NULL,
    vid_speed_changed_1640,
    NULL,
    vid_1640_config
};

const device_t *
pc1640_get_device(void)
{
    return(&vid_1640_device);
}

/* Display type */
#define PC200_CGA  0	/* CGA monitor */
#define PC200_MDA  1	/* MDA monitor */
#define PC200_TV   2	/* Television */
#define PC200_LCDC 3	/* PPC512 LCD as CGA*/
#define PC200_LCDM 4	/* PPC512 LCD as MDA*/

extern int nmi_mask;

static uint32_t blue, green;

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
    }
}


static void
vid_speed_changed_200(void *priv)
{
    amsvid_t *vid = (amsvid_t *)priv;

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
static unsigned char mapping1[256] =
{
/*	0  1  2  3  4  5  6  7  8  9  A  B  C  D  E  F */
/*00*/	0, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
/*10*/	2, 0, 1, 1, 1, 1, 1, 1, 2, 1, 1, 1, 1, 1, 1, 1,
/*20*/	2, 2, 0, 1, 2, 2, 1, 1, 2, 2, 1, 1, 2, 2, 1, 1,
/*30*/	2, 2, 2, 0, 2, 2, 1, 1, 2, 2, 2, 1, 2, 2, 1, 1,
/*40*/	2, 2, 1, 1, 0, 1, 1, 1, 2, 2, 1, 1, 1, 1, 1, 1,
/*50*/	2, 2, 1, 1, 2, 0, 1, 1, 2, 2, 1, 1, 2, 1, 1, 1,
/*60*/	2, 2, 2, 2, 2, 2, 0, 1, 2, 2, 2, 2, 2, 2, 1, 1,
/*70*/	2, 2, 2, 2, 2, 2, 2, 0, 2, 2, 2, 2, 2, 2, 2, 1,
/*80*/	2, 1, 1, 1, 1, 1, 1, 1, 0, 1, 1, 1, 1, 1, 1, 1,
/*90*/	2, 2, 1, 1, 1, 1, 1, 1, 2, 0, 1, 1, 1, 1, 1, 1,
/*A0*/	2, 2, 2, 1, 2, 2, 1, 1, 2, 2, 0, 1, 2, 2, 1, 1,
/*B0*/	2, 2, 2, 2, 2, 2, 1, 1, 2, 2, 2, 0, 2, 2, 1, 1,
/*C0*/	2, 2, 1, 1, 2, 1, 1, 1, 2, 2, 1, 1, 0, 1, 1, 1,
/*D0*/	2, 2, 1, 1, 2, 2, 1, 1, 2, 2, 1, 1, 2, 0, 1, 1,
/*E0*/	2, 2, 2, 2, 2, 2, 2, 1, 2, 2, 2, 2, 2, 2, 0, 1,
/*F0*/	2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 0,
};

static unsigned char mapping2[256] =
{
/*	0  1  2  3  4  5  6  7  8  9  A  B  C  D  E  F */
/*00*/	0, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
/*10*/	1, 3, 2, 2, 2, 2, 2, 2, 1, 2, 2, 2, 2, 2, 2, 2,
/*20*/	1, 1, 3, 2, 1, 1, 2, 2, 1, 1, 2, 2, 1, 1, 2, 2,
/*30*/	1, 1, 1, 3, 1, 1, 2, 2, 1, 1, 1, 2, 1, 1, 2, 2,
/*40*/	1, 1, 2, 2, 3, 2, 2, 2, 1, 1, 2, 2, 2, 2, 2, 2,
/*50*/	1, 1, 2, 2, 1, 3, 2, 2, 1, 1, 2, 2, 1, 2, 2, 2,
/*60*/	1, 1, 1, 1, 1, 1, 3, 2, 1, 1, 1, 1, 1, 1, 2, 2,
/*70*/	1, 1, 1, 1, 1, 1, 1, 3, 1, 1, 1, 1, 1, 1, 1, 2,
/*80*/	2, 1, 1, 1, 1, 1, 1, 1, 0, 1, 1, 1, 1, 1, 1, 1,
/*90*/	1, 1, 2, 2, 2, 2, 2, 2, 1, 3, 2, 2, 2, 2, 2, 2,
/*A0*/	1, 1, 1, 2, 1, 1, 2, 2, 1, 1, 3, 2, 1, 1, 2, 2,
/*B0*/	1, 1, 1, 1, 1, 1, 2, 2, 1, 1, 1, 3, 1, 1, 2, 2,
/*C0*/	1, 1, 2, 2, 1, 2, 2, 2, 1, 1, 2, 2, 3, 2, 2, 2,
/*D0*/	1, 1, 2, 2, 1, 1, 2, 2, 1, 1, 2, 2, 1, 3, 2, 2,
/*E0*/	1, 1, 1, 1, 1, 1, 1, 2, 1, 1, 1, 1, 1, 1, 3, 2,
/*F0*/	1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 3,
};


static void set_lcd_cols(uint8_t mode_reg)
{
    unsigned char *mapping = (mode_reg & 0x80) ? mapping2 : mapping1;
    int c;

    for (c = 0; c < 256; c++) {
	switch (mapping[c]) {
		case 0:
			lcdcols[c][0][0] = lcdcols[c][1][0] = green;
			lcdcols[c][0][1] = lcdcols[c][1][1] = green;
			break;

		case 1:
			lcdcols[c][0][0] = lcdcols[c][1][0] = 
					   lcdcols[c][1][1] = green;
			lcdcols[c][0][1] = blue;
			break;

		case 2:
			lcdcols[c][0][0] = lcdcols[c][1][0] = 
					   lcdcols[c][1][1] = blue;
			lcdcols[c][0][1] = green;
			break;

		case 3:
			lcdcols[c][0][0] = lcdcols[c][1][0] = blue;
			lcdcols[c][0][1] = lcdcols[c][1][1] = blue;
			break;
	}
    }
}


static uint8_t
vid_in_200(uint16_t addr, void *priv)
{
    amsvid_t *vid = (amsvid_t *)priv;
    cga_t *cga = &vid->cga;
    mda_t *mda = &vid->mda;
    uint8_t ret;

    switch (addr) {
	case 0x03b8:
		return(mda->ctrl);
		
	case 0x03d8:
		return(cga->cgamode);

	case 0x03dd:
		ret = vid->crtc_index;		/* Read NMI reason */
		vid->crtc_index &= 0x1f;	/* Reset NMI reason */
		nmi = 0;			/* And reset NMI flag */
		return(ret);

	case 0x03de:
		return((vid->operation_ctrl & 0xc7) | vid->dipswitches); /*External CGA*/

	case 0x03df:
		return(vid->reg_3df);
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
    amsvid_t *vid = (amsvid_t *)priv;
    cga_t *cga = &vid->cga;
    mda_t *mda = &vid->mda;
    uint8_t old;

    switch (addr) {
/* 	MDA writes ============================================================== */
	case 0x3b1:
	case 0x3b3:
	case 0x3b5:
	case 0x3b7:
		/* Writes banned to CRTC registers 0-11? */
		if (!(vid->operation_ctrl & 0x40) && mda->crtcreg <= 11) {
			vid->crtc_index = 0x20 | (mda->crtcreg & 0x1f);
			if (vid->operation_ctrl & 0x80)
				nmi = 1;
			vid->reg_3df = val;
			return;
		}
		old = mda->crtc[mda->crtcreg];
		mda->crtc[mda->crtcreg] = val & crtc_mask[mda->crtcreg];
		if (old != val) {
			if (mda->crtcreg < 0xe || mda->crtcreg > 0x10) {
				fullchange = changeframecount;
				mda_recalctimings(mda);
			}
		}
		return;
	case 0x3b8:
		old = mda->ctrl;
		mda->ctrl = val;
		if ((mda->ctrl ^ old) & 3)
			mda_recalctimings(mda);
		vid->crtc_index &= 0x1F;
		vid->crtc_index |= 0x80;
		if (vid->operation_ctrl & 0x80)
			nmi = 1;
		return;

/* 	CGA writes ============================================================== */	
	case 0x03d1:
	case 0x03d3:
	case 0x03d5:
	case 0x03d7:
		if (!(vid->operation_ctrl & 0x40) && cga->crtcreg <= 11) {
			vid->crtc_index = 0x20 | (cga->crtcreg & 0x1f);
			if (vid->operation_ctrl & 0x80) 
				nmi = 1;
			vid->reg_3df = val;
			return;
		}
		old = cga->crtc[cga->crtcreg];
		cga->crtc[cga->crtcreg] = val & crtc_mask[cga->crtcreg];
		if (old != val) {
			if (cga->crtcreg < 0xe || cga->crtcreg > 0x10) {
				fullchange = changeframecount;
				cga_recalctimings(cga);
			}
		}
		return;

	case 0x03d8:
		old = cga->cgamode;
		cga->cgamode = val;
		if ((cga->cgamode ^ old) & 3)
			cga_recalctimings(cga);
		vid->crtc_index &= 0x1f;
		vid->crtc_index |= 0x80;
		if (vid->operation_ctrl & 0x80)
			nmi = 1;
		else
			set_lcd_cols(val);
		return;

/* 	PC200 control port writes ============================================== */
	case 0x03de:
		vid->crtc_index = 0x1f;
	/* 	NMI only seems to be triggered if the value being written has the high
	 * 	bit set (enable NMI). So it only protects writes to this port if you 
	 * 	let it? */
		if (val & 0x80) {
			vid->operation_ctrl = val;
			vid->crtc_index |= 0x40;
			nmi = 1;
			return;
		}
                timer_disable(&vid->cga.timer);
                timer_disable(&vid->mda.timer);
                timer_disable(&vid->timer);
		vid->operation_ctrl = val;
		/* Bits 0 and 1 control emulation and output mode */
		amstrad_log("emulation and mode = %02X\n", val & 0x03);
		if (val & 1)	/* Monitor */
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
		if (vid->emulation == PC200_MDA || 
		    vid->emulation == PC200_LCDM) {
			mem_mapping_disable(&vid->cga.mapping);
			mem_mapping_enable(&vid->mda.mapping);
		}
		else {
			mem_mapping_disable(&vid->mda.mapping);
			mem_mapping_enable(&vid->cga.mapping);
		}
		return;
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
    int c;
    uint8_t bits = fontdat[chr + vid->cga.fontbase][sc];
    uint8_t bright = 0;
    uint16_t mask;

    if (attr & 8) {	/* bright */
	/* The brightness algorithm appears to be: replace any bit sequence 011 
	 * with 001 (assuming an extra 0 to the left of the byte). 
	 */
	bright = bits;
	for (c = 0, mask = 0x100; c < 7; c++, mask >>= 1) {
		if (((bits & mask) == 0) && ((bits & (mask >> 1)) != 0) &&
		    ((bits & (mask >> 2)) != 0))
			bright &= ~(mask >> 1);
	}
	bits = bright;
    }

    if (drawcursor) bits ^= 0xFF;

    for (c = 0, mask = 0x80; c < 8; c++, mask >>= 1) {
	if (mode160) buffer[c] = (attr & mask) ? blue : green;
	else if (control & 0x20) /* blinking */
		buffer[c] = lcdcols[attr & 0x7F][blink][(bits & mask) ? 1 : 0];
	else	buffer[c] = lcdcols[attr][blink][(bits & mask) ? 1 : 0];
    }
}


static void 
lcd_draw_char_40(amsvid_t *vid, uint32_t *buffer, uint8_t chr, 
	uint8_t attr, int drawcursor, int blink, int sc,
	uint8_t control)
{
    int c;
    uint8_t bits = fontdat[chr + vid->cga.fontbase][sc];
    uint8_t mask = 0x80;

    if (attr & 8)	/* bright */
	bits = bits & (bits >> 1);
    if (drawcursor) bits ^= 0xFF;

    for (c = 0; c < 8; c++, mask >>= 1) {
	if (control & 0x20) {
		buffer[c*2] = buffer[c*2+1] =
			      lcdcols[attr & 0x7F][blink][(bits & mask) ? 1 : 0];
	} else {
		buffer[c*2] = buffer[c*2+1] =
			      lcdcols[attr][blink][(bits & mask) ? 1 : 0];
	}
    }
}


static void 
lcdm_poll(amsvid_t *vid)
{
    mda_t *mda = &vid->mda;
    uint16_t ca = (mda->crtc[15] | (mda->crtc[14] << 8)) & 0x3fff;
    int drawcursor;
    int x;
    int oldvc;
    uint8_t chr, attr;
    int oldsc;
    int blink;

    if (!mda->linepos) {
	timer_advance_u64(&vid->timer, mda->dispofftime);
	mda->stat |= 1;
	mda->linepos = 1;
	oldsc = mda->sc;
	if ((mda->crtc[8] & 3) == 3) 
		mda->sc = (mda->sc << 1) & 7;
	if (mda->dispon) {
		if (mda->displine < mda->firstline)
			mda->firstline = mda->displine;
		mda->lastline = mda->displine;
		for (x = 0; x < mda->crtc[1]; x++) {
			chr  = mda->vram[(mda->ma << 1) & 0xfff];
			attr = mda->vram[((mda->ma << 1) + 1) & 0xfff];
			drawcursor = ((mda->ma == ca) && mda->con && mda->cursoron);
			blink = ((mda->blink & 16) && (mda->ctrl & 0x20) && (attr & 0x80) && !drawcursor);

			lcd_draw_char_80(vid, &((uint32_t *)(buffer32->line[mda->displine]))[x * 8], chr, attr, drawcursor, blink, mda->sc, 0, mda->ctrl);
			mda->ma++;
		}
	}
	mda->sc = oldsc;
	if (mda->vc == mda->crtc[7] && !mda->sc)
		mda->stat |= 8;
	mda->displine++;
	if (mda->displine >= 500) 
		mda->displine=0;
    } else {
	timer_advance_u64(&vid->timer, mda->dispontime);
	if (mda->dispon) mda->stat&=~1;
	mda->linepos=0;
	if (mda->vsynctime) {
		mda->vsynctime--;
		if (!mda->vsynctime)
			mda->stat&=~8;
	}
	if (mda->sc == (mda->crtc[11] & 31) || ((mda->crtc[8] & 3) == 3 && mda->sc == ((mda->crtc[11] & 31) >> 1))) {
		mda->con = 0; 
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
			mda->sc = 0;
		}
	} else if (mda->sc == mda->crtc[9] || ((mda->crtc[8] & 3) == 3 && mda->sc == (mda->crtc[9] >> 1))) {
		mda->maback = mda->ma;
		mda->sc = 0;
		oldvc = mda->vc;
		mda->vc++;
		mda->vc &= 127;
		if (mda->vc == mda->crtc[6]) 
		mda->dispon=0;
		if (oldvc == mda->crtc[4]) {
			mda->vc = 0;
			mda->vadj = mda->crtc[5];
			if (!mda->vadj) mda->dispon = 1;
			if (!mda->vadj) mda->ma = mda->maback = (mda->crtc[13] | (mda->crtc[12] << 8)) & 0x3fff;
			if ((mda->crtc[10] & 0x60) == 0x20) mda->cursoron = 0;
			else                                mda->cursoron = mda->blink & 16;
		}
		if (mda->vc == mda->crtc[7]) {
			mda->dispon = 0;
			mda->displine = 0;
			mda->vsynctime = 16;
			if (mda->crtc[7]) {
				x = mda->crtc[1] * 8;
				mda->lastline++;
				if ((x != xsize) || ((mda->lastline - mda->firstline) != ysize) || video_force_resize_get()) {
					xsize = x;
					ysize = mda->lastline - mda->firstline;
					if (xsize < 64) xsize = 656;
					if (ysize < 32) ysize = 200;
					set_screen_size(xsize, ysize);

					if (video_force_resize_get())
						video_force_resize_set(0);
				}
				video_blit_memtoscreen(0, mda->firstline, 0, ysize, xsize, ysize);
				frames++;
				video_res_x = mda->crtc[1];
				video_res_y = mda->crtc[6];
				video_bpp = 0;
			}
			mda->firstline = 1000;
			mda->lastline = 0;
			mda->blink++;
		}
	} else {
		mda->sc++;
		mda->sc &= 31;
		mda->ma = mda->maback;
	}
	if ((mda->sc == (mda->crtc[10] & 31) || ((mda->crtc[8] & 3) == 3 && mda->sc == ((mda->crtc[10] & 31) >> 1))))
		mda->con = 1;
    }
}


static void 
lcdc_poll(amsvid_t *vid)
{
    cga_t *cga = &vid->cga;
    int drawcursor;
    int x, c, xs_temp, ys_temp;
    int oldvc;
    uint8_t chr, attr;
    uint16_t dat;
    int oldsc;
    uint16_t ca;
    int blink;

    ca = (cga->crtc[15] | (cga->crtc[14] << 8)) & 0x3fff;

    if (!cga->linepos) {
	timer_advance_u64(&vid->timer, cga->dispofftime);
	cga->cgastat |= 1;
	cga->linepos = 1;
	oldsc = cga->sc;
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
				chr = cga->charbuffer[x << 1];
				attr = cga->charbuffer[(x << 1) + 1];
				drawcursor = ((cga->ma == ca) && cga->con && cga->cursoron);
				blink = ((cga->cgablink & 16) && (cga->cgamode & 0x20) && (attr & 0x80) && !drawcursor);
				lcd_draw_char_80(vid, &(buffer32->line[(cga->displine << 1)])[x * 8], chr, attr, drawcursor, blink, cga->sc, cga->cgamode & 0x40, cga->cgamode);
				lcd_draw_char_80(vid, &(buffer32->line[(cga->displine << 1) + 1])[x * 8], chr, attr, drawcursor, blink, cga->sc, cga->cgamode & 0x40, cga->cgamode);
				cga->ma++;
			}
		} else if (!(cga->cgamode & 2)) {
			for (x = 0; x < cga->crtc[1]; x++) {
				chr  = cga->vram[((cga->ma << 1) & 0x3fff)];
				attr = cga->vram[(((cga->ma << 1) + 1) & 0x3fff)];
				drawcursor = ((cga->ma == ca) && cga->con && cga->cursoron);
				blink = ((cga->cgablink & 16) && (cga->cgamode & 0x20) && (attr & 0x80) && !drawcursor);
				lcd_draw_char_40(vid, &(buffer32->line[(cga->displine << 1)])[x * 16], chr, attr, drawcursor, blink, cga->sc, cga->cgamode);
				lcd_draw_char_40(vid, &(buffer32->line[(cga->displine << 1) + 1])[x * 16], chr, attr, drawcursor, blink, cga->sc, cga->cgamode);
				cga->ma++;
			}
		} else {	/* Graphics mode */
			for (x = 0; x < cga->crtc[1]; x++) {
				dat = (cga->vram[((cga->ma << 1) & 0x1fff) + ((cga->sc & 1) * 0x2000)] << 8) | cga->vram[((cga->ma << 1) & 0x1fff) + ((cga->sc & 1) * 0x2000) + 1];
				cga->ma++;
				for (c = 0; c < 16; c++) {
					buffer32->line[(cga->displine << 1)][(x << 4) + c] = buffer32->line[(cga->displine << 1) + 1][(x << 4) + c] =
						(dat & 0x8000) ? blue : green;
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

	if (cga->cgamode & 1) x = (cga->crtc[1] << 3);
	else                  x = (cga->crtc[1] << 4);

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
		cga->con = 0; 
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
			cga->sc = 0;
		}
	} else if (cga->sc == cga->crtc[9]) {
		cga->maback = cga->ma;
		cga->sc = 0;
		oldvc = cga->vc;
		cga->vc++;
		cga->vc &= 127;

		if (cga->vc == cga->crtc[6]) 
			cga->cgadispon = 0;

		if (oldvc == cga->crtc[4]) {
			cga->vc = 0;
			cga->vadj = cga->crtc[5];
			if (!cga->vadj) cga->cgadispon = 1;
			if (!cga->vadj) cga->ma = cga->maback = (cga->crtc[13] | (cga->crtc[12] << 8)) & 0x3fff;
			if ((cga->crtc[10] & 0x60) == 0x20) cga->cursoron = 0;
			else                                cga->cursoron = cga->cgablink & 8;
		}

		if (cga->vc == cga->crtc[7]) {
			cga->cgadispon = 0;
			cga->displine = 0;
			cga->vsynctime = 16;
			if (cga->crtc[7]) {
				if (cga->cgamode & 1) x = (cga->crtc[1] << 3);
				else                  x = (cga->crtc[1] << 4);
				cga->lastline++;

				xs_temp = x;
				ys_temp = (cga->lastline - cga->firstline) << 1;

				if ((xs_temp > 0) && (ys_temp > 0)) {
					if (xs_temp < 64) xs_temp = 640;
					if (ys_temp < 32) ys_temp = 400;

					if ((cga->cgamode & 8) && ((xs_temp != xsize) || (ys_temp != ysize) || video_force_resize_get())) {
						xsize = xs_temp;
						ysize = ys_temp;
						set_screen_size(xsize, ysize);

						if (video_force_resize_get())
							video_force_resize_set(0);
					}

					video_blit_memtoscreen(0, cga->firstline << 1, 0, (cga->lastline - cga->firstline) << 1,
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
			cga->lastline = 0;
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
	if ((cga->sc == (cga->crtc[10] & 31) || ((cga->crtc[8] & 3) == 3 && cga->sc == ((cga->crtc[10] & 31) >> 1)))) 
		cga->con = 1;
	if (cga->cgadispon && (cga->cgamode & 1)) {
		for (x = 0; x < (cga->crtc[1] << 1); x++)
			cga->charbuffer[x] = cga->vram[(((cga->ma << 1) + x) & 0x3fff)];
	}
    }
}


static void 
vid_poll_200(void *p)
{
    amsvid_t *vid = (amsvid_t *)p;

    switch (vid->emulation) {
	case PC200_LCDM:
		lcdm_poll(vid);
		return;
	case PC200_LCDC:	
		lcdc_poll(vid);
		return;
    }
}


static void
vid_init_200(amstrad_t *ams)
{
    amsvid_t *vid;
    cga_t *cga;
    mda_t *mda;

    /* Allocate a video controller block. */
    vid = (amsvid_t *)malloc(sizeof(amsvid_t));
    memset(vid, 0x00, sizeof(amsvid_t));

    vid->emulation = device_get_config_int("video_emulation");
    cga_palette = (device_get_config_int("display_type") << 1);
    ams_inform(vid);

    /* Default to CGA */
    vid->dipswitches = 0x10;	
    vid->type = ams->type;

    if (ams->type == AMS_PC200) switch (vid->emulation) {
	/* DIP switches for PC200. Switches 2,3 give video emulation.
	 * Switch 1 is 'swap floppy drives' (not implemented) */
	case PC200_CGA:  vid->dipswitches = 0x10; break;
	case PC200_MDA:  vid->dipswitches = 0x30; break;
	case PC200_TV:   vid->dipswitches = 0x00; break;
	/* The other combination is 'IDA disabled' (0x20) - see
	 * m_amstrad.c */
    } else switch (vid->emulation) {
	/* DIP switches for PPC512. Switch 1 is CRT/LCD. Switch 2
	 * is MDA / CGA. Switch 3 disables IDA, not implemented. */
	/* 1 = on, 0 = off
	   SW1: off = crt, on = lcd;
	   SW2: off = mda, on = cga;
	   SW3: off = disable built-in card, on = enable */
	case PC200_CGA:  vid->dipswitches = 0x08; break;
	case PC200_MDA:  vid->dipswitches = 0x18; break;
	case PC200_LCDC: vid->dipswitches = 0x00; break;
	case PC200_LCDM: vid->dipswitches = 0x10; break;
    }

    cga = &vid->cga;
    mda = &vid->mda;
    cga->vram = mda->vram = malloc(0x4000);
    cga_init(cga);
    mda_init(mda);

    /* Attribute 8 is white on black (on a real MDA it's black on black) */
    mda_setcol(0x08, 0, 1, 15);
    mda_setcol(0x88, 0, 1, 15);
    /* Attribute 64 is black on black (on a real MDA it's white on black) */
    mda_setcol(0x40, 0, 1, 0);
    mda_setcol(0xC0, 0, 1, 0);	

    cga->fontbase = (device_get_config_int("codepage") & 3) * 256;

    timer_add(&vid->timer, vid_poll_200, vid, 1);
    mem_mapping_add(&vid->mda.mapping, 0xb0000, 0x08000, 
		    mda_read, NULL, NULL, mda_write, NULL, NULL, NULL, 0, mda);
    mem_mapping_add(&vid->cga.mapping, 0xb8000, 0x08000,
		    cga_read, NULL, NULL, cga_write, NULL, NULL, NULL, 0, cga);
    io_sethandler(0x03d0, 16, vid_in_200, NULL, NULL, vid_out_200, NULL, NULL, vid);
    io_sethandler(0x03b0, 0x000c, vid_in_200, NULL, NULL, vid_out_200, NULL, NULL, vid);

    overscan_x = overscan_y = 16;

    green = makecol(0x1C, 0x71, 0x31);
    blue = makecol(0x0f, 0x21, 0x3f);	
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
    amsvid_t *vid = (amsvid_t *)priv;

    free(vid->cga.vram);
    free(vid->mda.vram);

    free(vid);
}


device_config_t vid_200_config[] =
{
	/* TODO: Should have options here for:
	*
	*	> Display port (TTL or RF)
	*/
	{
		"video_emulation", "Display type", CONFIG_SELECTION, "", PC200_CGA,
		{
			{
				"CGA monitor", PC200_CGA
			},
			{
				"MDA monitor", PC200_MDA
			},
			{
				"Television", PC200_TV
			},
			{
				""
			}
		}
	},
        {
		"display_type", "Monitor type", CONFIG_SELECTION, "", 0,
		{
			{
					"RGB", 0
			},
			{
					"RGB (no brown)", 4
			},
			{
					"Green Monochrome", 1
			},
			{
					"Amber Monochrome", 2
			},
			{
					"White Monochrome", 3
			},
			{
				   ""
			}
		}
        },
        {
		"codepage", "Hardware font", CONFIG_SELECTION, "", 3,
		{
			{
				"US English", 3
			},
			{
				"Portugese", 2
			},
			{
				"Norwegian", 1
			},
			{
				"Greek", 0
			},
			{
				""
			}
		}
	},
	{
        "language", "BIOS language", CONFIG_SELECTION, "", 7,
		{
			{
				"English", 7
			},
			{
				"German", 6
			},
			{
				"French", 5
			},
			{
				"Spanish", 4
			},
			{
				"Danish", 3
			},
			{
				"Swedish", 2
			},
			{
				"Italian", 1
			},
			{
				"Diagnostic mode", 0
			},
			{
                ""
			}
		}
	},
        {
                "", "", -1
        }
};


static const device_t vid_200_device = {
    "Amstrad PC200 (video)",
    0, 0,
    NULL, vid_close_200, NULL,
    NULL,
    vid_speed_changed_200,
    NULL,
    vid_200_config
};


const device_t *
pc200_get_device(void)
{
    return(&vid_200_device);
}


device_config_t vid_ppc512_config[] =
{
	/* TODO: Should have options here for:
	*
	*	> Display port (TTL or RF)
	*/
	{
		"video_emulation", "Display type", CONFIG_SELECTION, "", PC200_LCDC,
		{
			{
				"CGA monitor", PC200_CGA
			},
			{
				"MDA monitor", PC200_MDA
			},
			{
				"LCD (CGA mode)", PC200_LCDC
			},
			{
				"LCD (MDA mode)", PC200_LCDM
			},
		{
				""
			}
		},
	},
        {
                "display_type", "Monitor type", CONFIG_SELECTION, "", 0,
                {
                        {
                                "RGB", 0
                        },
                        {
                                "RGB (no brown)", 4
                        },
                        {
                                "Green Monochrome", 1
                        },
                        {
                                "Amber Monochrome", 2
                        },
                        {
                                "White Monochrome", 3
                        },
                        {
                                ""
                        }
                },
        },
        {
                "codepage", "Hardware font", CONFIG_SELECTION, "", 3,
				{
					{
						"US English", 3
					},
					{
						"Portugese", 2
					},
					{
						"Norwegian",1
					},
					{
						"Greek", 0
					},
					{
						""
					}
				},
        },
	{
        "language", "BIOS language", CONFIG_SELECTION, "", 7,
		{
			{
				"English", 7
			},
			{
				"German", 6
			},
			{
				"French", 5
			},
			{
				"Spanish", 4
			},
			{
				"Danish", 3
			},
			{
				"Swedish", 2
			},
			{
				"Italian", 1
			},
			{
				"Diagnostic mode", 0
			},
			{
                ""
			}
		}
	},
        {
                "", "", -1
        }
};

static const device_t vid_ppc512_device = {
    "Amstrad PPC512 (video)",
    0, 0,
    NULL, vid_close_200, NULL,
    NULL,
    vid_speed_changed_200,
    NULL,
    vid_ppc512_config
};


const device_t *
ppc512_get_device(void)
{
    return(&vid_ppc512_device);
}


device_config_t vid_pc2086_config[] =
{
	{
        "language", "BIOS language", CONFIG_SELECTION, "", 7,
		{
			{
				"English", 7
			},
			{
				"Diagnostic mode", 0
			},
			{
                ""
			}
		}
	},
        {
                "", "", -1
        }
};

static const device_t vid_pc2086_device = {
    "Amstrad PC2086",
    0, 0,
    NULL, NULL, NULL,
    NULL,
    NULL,
    NULL,
    vid_pc2086_config
};


const device_t *
pc2086_get_device(void)
{
    return(&vid_pc2086_device);
}


device_config_t vid_pc3086_config[] =
{
	{
        "language", "BIOS language", CONFIG_SELECTION, "", 7,
		{
			{
				"English", 7
			},
			{
				"Diagnostic mode", 3
			},
			{
                ""
			}
		}
	},
        {
                "", "", -1
        }
};

static const device_t vid_pc3086_device = {
    "Amstrad PC3086",
    0, 0,
    NULL, NULL, NULL,
    NULL,
    NULL,
    NULL,
    vid_pc3086_config
};


const device_t *
pc3086_get_device(void)
{
    return(&vid_pc3086_device);
}


static void
ms_write(uint16_t addr, uint8_t val, void *priv)
{
    amstrad_t *ams = (amstrad_t *)priv;

    if ((addr == 0x78) || (addr == 0x79))
	ams->mousex = 0;
    else
	ams->mousey = 0;
}


static uint8_t
ms_read(uint16_t addr, void *priv)
{
    amstrad_t *ams = (amstrad_t *)priv;
    uint8_t ret;

    if ((addr == 0x78) || (addr == 0x79)) {
	ret = ams->mousex;
	ams->mousex = 0;
    } else {
	ret = ams->mousey;
	ams->mousey = 0;
    }

    return(ret);
}


static int
ms_poll(int x, int y, int z, int b, void *priv)
{
    amstrad_t *ams = (amstrad_t *)priv;

    ams->mousex += x;
    ams->mousey -= y;

    if ((b & 1) && !(ams->oldb & 1))
	keyboard_send(0x7e);
    if (!(b & 1) && (ams->oldb & 1))
	keyboard_send(0xfe);

    if ((b & 2) && !(ams->oldb & 2))
	keyboard_send(0x7d);
    if (!(b & 2) && (ams->oldb & 2))
	keyboard_send(0xfd);

    ams->oldb = b;

    return(0);
}


static void
kbd_adddata(uint16_t val)
{
    key_queue[key_queue_end] = val;
    key_queue_end = (key_queue_end + 1) & 0xf;
}


static void
kbd_adddata_ex(uint16_t val)
{
    kbd_adddata(val);
    // kbd_adddata_process(val, kbd_adddata);
}


static void
kbd_write(uint16_t port, uint8_t val, void *priv)
{
    amstrad_t *ams = (amstrad_t *)priv;

    amstrad_log("keyboard_amstrad : write %04X %02X %02X\n", port, val, ams->pb);

    switch (port) {
	case 0x61:
		/*
		 * PortB - System Control.
		 *
		 *  7	Enable Status-1/Disable Keyboard Code on Port A.
		 *  6	Enable incoming Keyboard Clock.
		 *  5	Prevent external parity errors from causing NMI.
		 *  4	Disable parity checking of on-board system Ram.
		 *  3	Undefined (Not Connected).
		 *  2	Enable Port C LSB / Disable MSB. (See 1.8.3)
		 *  1	Speaker Drive.
		 *  0	8253 GATE 2 (Speaker Modulate).
		 *
		 * This register is controlled by BIOS and/or ROS.
		 */
		amstrad_log("AMSkb: write PB %02x (%02x)\n", val, ams->pb);
		if (!(ams->pb & 0x40) && (val & 0x40)) { /*Reset keyboard*/
			amstrad_log("AMSkb: reset keyboard\n");
			kbd_adddata(0xaa);
		}
		ams->pb = val;
		ppi.pb = val;

		speaker_update();
		speaker_gated = val & 0x01;
		speaker_enable = val & 0x02;
		if (speaker_enable) 
			was_speaker_enable = 1;
		pit_ctr_set_gate(&pit->counters[2], val & 0x01);

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
		cpu_set_edx();
		break;

	default:
		amstrad_log("AMSkb: bad keyboard write %04X %02X\n", port, val);
    }
}


static uint8_t
kbd_read(uint16_t port, void *priv)
{
    amstrad_t *ams = (amstrad_t *)priv;
    uint8_t ret = 0xff;

    switch (port) {
	case 0x60:
		if (ams->pb & 0x80) {
			/*
			 * PortA - System Status 1
			 *
			 *  7	Always 0			    (KBD7)
			 *  6	Second Floppy disk drive installed  (KBD6)
			 *  5	DDM1 - Default Display Mode bit 1   (KBD5)
			 *  4	DDM0 - Default Display Mode bit 0   (KBD4)
			 *  3	Always 1			    (KBD3)
			 *  2	Always 1			    (KBD2)
			 *  1	8087 NDP installed		    (KBD1)
			 *  0	Always 1			    (KBD0)
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
				key_queue_start = (key_queue_start + 1) & 0xf;
				ams->wantirq = 1;	
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
		 *  7	On-board system RAM parity error.
		 *  6	External parity error (I/OCHCK from expansion bus).
		 *  5	8253 PIT OUT2 output.
		 *  4 	Undefined (Not Connected).
		 *-------------------------------------------
		 *	LSB 	MSB (depends on PB2)
		 *-------------------------------------------
		 *  3	RAM3	Undefined
		 *  2	RAM2	Undefined
		 *  1	RAM1	Undefined
		 *  0	RAM0	RAM4
		 *
		 * PC7 is forced to 0 when on-board system RAM parity
		 * checking is disabled by PB4.
		 *
		 * RAM4:0
		 * 01110	512K bytes on-board.
		 * 01111	544K bytes (32K external).
		 * 10000	576K bytes (64K external).
		 * 10001	608K bytes (96K external).
		 * 10010	640K bytes (128K external or fitted on-board).
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

    return(ret);
}


static void
kbd_poll(void *priv)
{
    amstrad_t *ams = (amstrad_t *)priv;

    timer_advance_u64(&ams->send_delay_timer, 1000 * TIMER_USEC);

    if (ams->wantirq) {
	ams->wantirq = 0;
	ams->pa = ams->key_waiting;
	picint(2);
    }

    if (key_queue_start != key_queue_end && !ams->pa) {
	ams->key_waiting = key_queue[key_queue_start];
	key_queue_start = (key_queue_start + 1) & 0x0f;
	ams->wantirq = 1;
    }
}


static void
ams_write(uint16_t port, uint8_t val, void *priv)
{
    amstrad_t *ams = (amstrad_t *)priv;

    switch (port) {
	case 0x0378:
	case 0x0379:
	case 0x037a:
		lpt_write(port, val, &lpt_ports[0]);
		break;

	case 0xdead:
		ams->dead = val;
		break;
    }
}


static uint8_t
ams_read(uint16_t port, void *priv)
{
    amstrad_t *ams = (amstrad_t *)priv;
    uint8_t ret = 0xff;

    switch (port) {
	case 0x0378:
		ret = lpt_read(port, &lpt_ports[0]);
		break;

	case 0x0379:	/* printer control, also set LK1-3.
			 * per John Elliott's site, this is xor'ed with 0x07
			 *   7	English Language.
			 *   6	German Language.
			 *   5	French Language.
			 *   4	Spanish Language.
			 *   3	Danish Language.
			 *   2	Swedish Language.
			 *   1	Italian Language.
			 *   0	Diagnostic Mode.
			 */
		ret = (lpt_read(port, &lpt_ports[0]) & 0xf8) | ams->language;
		break;

	case 0x037a:	/* printer status */
		ret = lpt_read(port, &lpt_ports[0]) & 0x1f;

		switch(ams->type) {
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

				switch (amstrad_latch) {
					case AMSTRAD_NOLATCH:
						ret &= ~0x20;
						break;
					case AMSTRAD_SW9:
						ret &= ~0x20;
						break;
					case AMSTRAD_SW10:
						ret |= 0x20;
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
    }

    return(ret);
}


static void
machine_amstrad_init(const machine_t *model, int type)
{
    amstrad_t *ams;

    ams = (amstrad_t *)malloc(sizeof(amstrad_t));
    memset(ams, 0x00, sizeof(amstrad_t));
    ams->type = type;

    device_add(&amstrad_nvr_device);

    machine_common_init(model);

    nmi_init();

    lpt1_remove_ams();
    lpt2_remove();

    io_sethandler(0x0378, 3,
		  ams_read, NULL, NULL, ams_write, NULL, NULL, ams);
    io_sethandler(0xdead, 1,
		  ams_read, NULL, NULL, ams_write, NULL, NULL, ams);

    switch(type) {
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
    }

    ams->language = 7;

    if (gfxcard == VID_INTERNAL) switch(type) {
	case AMS_PC1512:
		loadfont(L"roms/machines/pc1512/40078", 8);
		device_context(&vid_1512_device);
		ams->language = device_get_config_int("language");
		vid_init_1512(ams);
		device_context_restore();
		device_add_ex(&vid_1512_device, ams->vid);
		break;
	
	case AMS_PPC512:
		loadfont(L"roms/machines/ppc512/40109", 1);
		device_context(&vid_ppc512_device);
		ams->language = device_get_config_int("language");
		vid_init_200(ams);
		device_context_restore();
		device_add_ex(&vid_ppc512_device, ams->vid);
		break;
	
	case AMS_PC1640:
		loadfont(L"roms/video/mda/mda.rom", 0);
		device_context(&vid_1640_device);
		ams->language = device_get_config_int("language");
		vid_init_1640(ams);
		device_context_restore();
		device_add_ex(&vid_1640_device, ams->vid);
		break;

	case AMS_PC200:
		loadfont(L"roms/machines/pc200/40109", 1);
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
    } else if ((type == AMS_PC200) || (type == AMS_PPC512))
	io_sethandler(0x03de, 1,
		      ams_read, NULL, NULL, ams_write, NULL, NULL, ams);

    /* Initialize the (custom) keyboard/mouse interface. */
    ams->wantirq = 0;
    io_sethandler(0x0060, 7,
		  kbd_read, NULL, NULL, kbd_write, NULL, NULL, ams);
    timer_add(&ams->send_delay_timer, kbd_poll, ams, 1);
    keyboard_set_table(scancode_xt);
    keyboard_send = kbd_adddata_ex;
    keyboard_scan = 1;

    io_sethandler(0x0078, 2,
		  ms_read, NULL, NULL, ms_write, NULL, NULL, ams);
    io_sethandler(0x007a, 2,
		  ms_read, NULL, NULL, ms_write, NULL, NULL, ams);

    if (mouse_type == MOUSE_TYPE_INTERNAL) {
	/* Tell mouse driver about our internal mouse. */
	mouse_reset();
	mouse_set_poll(ms_poll, ams);
    }

    if (joystick_type != JOYSTICK_TYPE_NONE)
	device_add(&gameport_device);
}


int
machine_pc1512_init(const machine_t *model)
{
    int ret;

    ret = bios_load_interleaved(L"roms/machines/pc1512/40044",
				L"roms/machines/pc1512/40043",
				0x000fc000, 16384, 0);
    ret &= rom_present(L"roms/machines/pc1512/40078");

    if (bios_only || !ret)
	return ret;

    machine_amstrad_init(model, AMS_PC1512);

    return ret;
}


int
machine_pc1640_init(const machine_t *model)
{
    int ret;

    ret = bios_load_interleaved(L"roms/machines/pc1640/40044.v3",
				L"roms/machines/pc1640/40043.v3",
				0x000fc000, 16384, 0);
    ret &= rom_present(L"roms/machines/pc1640/40100");

    if (bios_only || !ret)
	return ret;

    machine_amstrad_init(model, AMS_PC1640);

    return ret;
}


int
machine_pc200_init(const machine_t *model)
{
    int ret;

    ret = bios_load_interleaved(L"roms/machines/pc200/pc20v2.1",
				L"roms/machines/pc200/pc20v2.0",
				0x000fc000, 16384, 0);
    ret &= rom_present(L"roms/machines/pc200/40109");

    if (bios_only || !ret)
	return ret;

    machine_amstrad_init(model, AMS_PC200);

    return ret;
}


int
machine_ppc512_init(const machine_t *model)
{
    int ret;

    ret = bios_load_interleaved(L"roms/machines/ppc512/40107.v2",
				L"roms/machines/ppc512/40108.v2",
				0x000fc000, 16384, 0);
    ret &= rom_present(L"roms/machines/ppc512/40109");

    if (bios_only || !ret)
	return ret;

    machine_amstrad_init(model, AMS_PPC512);

    return ret;
}


int
machine_pc2086_init(const machine_t *model)
{
    int ret;

    ret = bios_load_interleavedr(L"roms/machines/pc2086/40179.ic129",
				 L"roms/machines/pc2086/40180.ic132",
				 0x000fc000, 65536, 0);
    ret &= rom_present(L"roms/machines/pc2086/40186.ic171");

    if (bios_only || !ret)
	return ret;

    machine_amstrad_init(model, AMS_PC2086);

    return ret;
}


int
machine_pc3086_init(const machine_t *model)
{
    int ret;

    ret = bios_load_linearr(L"roms/machines/pc3086/fc00.bin",
			    0x000fc000, 65536, 0);
    ret &= rom_present(L"roms/machines/pc3086/c000.bin");

    if (bios_only || !ret)
	return ret;

    machine_amstrad_init(model, AMS_PC3086);

    return ret;
}
