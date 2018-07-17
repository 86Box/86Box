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
 * TODO:	This module is not complete yet:
 *
 * PC1512:	The BIOS assumes 512K RAM, because I cannot figure out how to
 *		read the status of the LK4 jumper on the mainboard, which is
 *		somehow linked to the bus gate array on the NDMACS line...
 *
 * PC1612:	EGA mode does not seem to work in the PC1640; it works fine
 *		in alpha mode, but in highres ("ECD350") mode, it displays
 *		some semi-random junk. Video-memory pointer maybe?
 *
 * Version:	@(#)m_amstrad.c	1.0.14	2018/04/29
 *
 * Authors:	Sarah Walker, <http://pcem-emulator.co.uk/>
 *		Miran Grca, <mgrca8@gmail.com>
 *		Fred N. van Kempen, <decwiz@yahoo.com>
 *
 *		Copyright 2008-2018 Sarah Walker.
 *		Copyright 2016-2018 Miran Grca.
 *		Copyright 2017,2018 Fred N. van Kempen.
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
#include "../io.h"
#include "../nmi.h"
#include "../pic.h"
#include "../pit.h"
#include "../ppi.h"
#include "../mem.h"
#include "../rom.h"
#include "../timer.h"
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
#include "../video/vid_paradise.h"
#include "machine.h"


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
    ega_t	ega;			/* 1640 */
    uint8_t	crtc[32];
    int		crtcreg;
    int		cga_enabled;		/* 1640 */
    uint8_t	cgacol,
		cgamode,
		stat;
    uint8_t	plane_write,		/* 1512/200 */
		plane_read,		/* 1512/200 */
		border;			/* 1512/200 */
    int		linepos,
		displine;
    int		sc, vc;
    int		cgadispon;
    int		con, coff,
		cursoron,
		cgablink;
    int64_t	vsynctime;
    int		vadj;
    uint16_t	ma, maback;
    int		dispon;
    int		blink;
    int64_t	dispontime,		/* 1512/1640 */
		dispofftime;		/* 1512/1640 */
    int64_t	vidtime;		/* 1512/1640 */
    int		firstline,
		lastline;
    uint8_t	*vram;
} amsvid_t;

typedef struct {
    /* Machine stuff. */
    uint8_t	dead;
    uint8_t	stat1,
		stat2;

    /* Keyboard stuff. */
    int8_t	wantirq;
    uint8_t	key_waiting;
    uint8_t	pa;
    uint8_t	pb;

    /* Mouse stuff. */
    uint8_t	mousex,
		mousey;
    int		oldb;

    /* Video stuff. */
    amsvid_t	*vid;
} amstrad_t;


static uint8_t	key_queue[16];
static int	key_queue_start = 0,
		key_queue_end = 0;
static uint8_t	crtc_mask[32] = {
    0xff, 0xff, 0xff, 0xff, 0x7f, 0x1f, 0x7f, 0x7f,
    0xf3, 0x1f, 0x7f, 0x1f, 0x3f, 0xff, 0x3f, 0xff,
    0xff, 0xff, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00
};


#ifdef ENABLE_AMSTRAD_LOG
int amstrad_do_log = ENABLE_AMSTRAD_LOG;
#endif


static void
amstrad_log(const char *fmt, ...)
{
#ifdef ENABLE_AMSTRAD_LOG
   va_list ap;

   if (amstrad_do_log)
   {
	va_start(ap, fmt);
	pclog_ex(fmt, ap);
	va_end(ap);
   }
#endif
}


static void
recalc_timings_1512(amsvid_t *vid)
{
    double _dispontime, _dispofftime, disptime;

    disptime = 128; /*Fixed on PC1512*/
    _dispontime = 80;
    _dispofftime = disptime - _dispontime;
    _dispontime  *= CGACONST;
    _dispofftime *= CGACONST;
    vid->dispontime  = (int64_t)(_dispontime * (1 << TIMER_SHIFT));
    vid->dispofftime = (int64_t)(_dispofftime * (1 << TIMER_SHIFT));
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

	case 0x03d5:
		ret = vid->crtc[vid->crtcreg];

	case 0x03da:
		ret = vid->stat;
    }

    return(ret);
}


static void
vid_write_1512(uint32_t addr, uint8_t val, void *priv)
{
    amsvid_t *vid = (amsvid_t *)priv;

    egawrites++;
    cycles -= 12;
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
    cycles -= 12;
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
    int x, c;
    uint8_t chr, attr;
    uint16_t dat, dat2, dat3, dat4;
    int cols[4];
    int col;
    int oldsc;

    if (! vid->linepos) {
	vid->vidtime += vid->dispofftime;
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
				buffer->line[vid->displine][c] = (vid->border & 15) + 16;
				if (vid->cgamode & 1)
					buffer->line[vid->displine][c + (vid->crtc[1] << 3) + 8] = 0;
				  else
					buffer->line[vid->displine][c + (vid->crtc[1] << 4) + 8] = 0;
			} else {
				buffer->line[vid->displine][c] = (vid->cgacol & 15) + 16;
				if (vid->cgamode & 1)
					buffer->line[vid->displine][c + (vid->crtc[1] << 3) + 8] = (vid->cgacol & 15) + 16;
				  else
					buffer->line[vid->displine][c + (vid->crtc[1] << 4) + 8] = (vid->cgacol & 15) + 16;
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
					for (c = 0; c < 8; c++)
					    buffer->line[vid->displine][(x << 3) + c + 8] = cols[(fontdat[chr][vid->sc & 7] & (1 << (c ^ 7))) ? 1 : 0] ^ 15;
				} else {
					for (c = 0; c < 8; c++)
					    buffer->line[vid->displine][(x << 3) + c + 8] = cols[(fontdat[chr][vid->sc & 7] & (1 << (c ^ 7))) ? 1 : 0];
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
					for (c = 0; c < 8; c++)
					    buffer->line[vid->displine][(x << 4) + (c << 1) + 8] = 
					    	buffer->line[vid->displine][(x << 4) + (c << 1) + 1 + 8] = cols[(fontdat[chr][vid->sc & 7] & (1 << (c ^ 7))) ? 1 : 0] ^ 15;
				} else {
					for (c = 0; c < 8; c++)
					    buffer->line[vid->displine][(x << 4) + (c << 1) + 8] = 
					    	buffer->line[vid->displine][(x << 4) + (c << 1) + 1 + 8] = cols[(fontdat[chr][vid->sc & 7] & (1 << (c ^ 7))) ? 1 : 0];
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
					buffer->line[vid->displine][(x << 4) + (c << 1) + 8] =
						buffer->line[vid->displine][(x << 4) + (c << 1) + 1 + 8] = cols[dat >> 14];
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
					buffer->line[vid->displine][(x << 4) + c + 8] = (((dat >> 15) | ((dat2 >> 15) << 1) | ((dat3 >> 15) << 2) | ((dat4 >> 15) << 3)) & (vid->cgacol & 15)) + 16;
					dat  <<= 1;
					dat2 <<= 1;
					dat3 <<= 1;
					dat4 <<= 1;
				}
			}
		}
	} else {
		cols[0] = ((vid->cgamode & 0x12) == 0x12) ? 0 : (vid->cgacol & 15) + 16;
		if (vid->cgamode & 1)
			hline(buffer, 0, vid->displine, (vid->crtc[1] << 3) + 16, cols[0]);
		else
			hline(buffer, 0, vid->displine, (vid->crtc[1] << 4) + 16, cols[0]);
	}

	vid->sc = oldsc;
	if (vid->vsynctime)
	   vid->stat |= 8;
	vid->displine++;
	if (vid->displine >= 360) 
		vid->displine = 0;
    } else {
	vid->vidtime += vid->dispontime;
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

			if ((x != xsize) || ((vid->lastline - vid->firstline) != ysize) || video_force_resize_get()) {
				xsize = x;
				ysize = vid->lastline - vid->firstline;
				if (xsize < 64) xsize = 656;
				if (ysize < 32) ysize = 200;
				set_screen_size(xsize, (ysize << 1) + 16);

				if (video_force_resize_get())
					video_force_resize_set(0);
			}

			video_blit_memtoscreen_8(0, vid->firstline - 4, 0, (vid->lastline - vid->firstline) + 8, xsize, (vid->lastline - vid->firstline) + 8);

			video_res_x = xsize - 16;
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

    vid->vram = malloc(0x10000);
    vid->cgacol = 7;
    vid->cgamode = 0x12;

    timer_add(vid_poll_1512, &vid->vidtime, TIMER_ALWAYS_ENABLED, vid);
    mem_mapping_add(&vid->cga.mapping, 0xb8000, 0x08000,
		    vid_read_1512, NULL, NULL, vid_write_1512, NULL, NULL,
		    NULL, 0, vid);
    io_sethandler(0x03d0, 16,
		  vid_in_1512, NULL, NULL, vid_out_1512, NULL, NULL, vid);

    overscan_x = overscan_y = 16;

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


static const device_t vid_1512_device = {
    "Amstrad PC1512 (video)",
    0, 0,
    NULL, vid_close_1512, NULL,
    NULL,
    vid_speed_change_1512,
    NULL
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
			mem_mapping_enable(&vid->cga.mapping);
			mem_mapping_disable(&vid->ega.mapping);
		} else {
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

    switch (addr) {
    }

    if (vid->cga_enabled)
	return(cga_in(addr, &vid->cga));
      else
	return(ega_in(addr, &vid->ega));
}


static void
vid_poll_1640(void *priv)
{
    amsvid_t *vid = (amsvid_t *)priv;

    if (vid->cga_enabled) {
	overscan_x = overscan_y = 16;

	vid->cga.vidtime = vid->vidtime;
	cga_poll(&vid->cga);
	vid->vidtime = vid->cga.vidtime;
    } else {
	overscan_x = 16; overscan_y = 28;

	vid->ega.vidtime = vid->vidtime;
	ega_poll(&vid->ega);
	vid->vidtime = vid->ega.vidtime;
    }
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

    mem_mapping_add(&vid->cga.mapping, 0xb8000, 0x08000,
	cga_read, NULL, NULL, cga_write, NULL, NULL, NULL, 0, &vid->cga);
    mem_mapping_add(&vid->ega.mapping, 0,       0,
	ega_read, NULL, NULL, ega_write, NULL, NULL, NULL, 0, &vid->ega);
    io_sethandler(0x03a0, 64,
		  vid_in_1640, NULL, NULL, vid_out_1640, NULL, NULL, vid);

    timer_add(vid_poll_1640, &vid->vidtime, TIMER_ALWAYS_ENABLED, vid);

    overscan_x = overscan_y = 16;

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


static const device_t vid_1640_device = {
    "Amstrad PC1640 (video)",
    0, 0,
    NULL, vid_close_1640, NULL,
    NULL,
    vid_speed_changed_1640,
    NULL
};


static void
vid_out_200(uint16_t addr, uint8_t val, void *priv)
{
    amsvid_t *vid = (amsvid_t *)priv;
    cga_t *cga = &vid->cga;
    uint8_t old;

    switch (addr) {
	case 0x03d5:
		if (!(vid->plane_read & 0x40) && cga->crtcreg <= 11) {
			if (vid->plane_read & 0x80) 
				nmi = 1;

			vid->plane_write = 0x20 | (cga->crtcreg & 0x1f);
			vid->border = val;
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
		vid->plane_write |= 0x80;
		if (vid->plane_read & 0x80)
			nmi = 1;
		return;

	case 0x03de:
		vid->plane_read = val;
		vid->plane_write = 0x1f;
		if (val & 0x80) 
			vid->plane_write |= 0x40;
		return;
    }

    cga_out(addr, val, cga);
}


static uint8_t
vid_in_200(uint16_t addr, void *priv)
{
    amsvid_t *vid = (amsvid_t *)priv;
    cga_t *cga = &vid->cga;
    uint8_t ret;

    switch (addr) {
	case 0x03d8:
		return(cga->cgamode);

	case 0x03dd:
		ret = vid->plane_write;
		vid->plane_write &= 0x1f;
		nmi = 0;
		return(ret);

	case 0x03de:
		return((vid->plane_read & 0xc7) | 0x10); /*External CGA*/

	case 0x03df:
		return(vid->border);
    }

    return(cga_in(addr, cga));
}


static void
vid_init_200(amstrad_t *ams)
{
    amsvid_t *vid;
    cga_t *cga;

    /* Allocate a video controller block. */
    vid = (amsvid_t *)malloc(sizeof(amsvid_t));
    memset(vid, 0x00, sizeof(amsvid_t));

    cga = &vid->cga;
    cga->vram = malloc(0x4000);
    cga_init(cga);

    mem_mapping_add(&vid->cga.mapping, 0xb8000, 0x08000,
	cga_read, NULL, NULL, cga_write, NULL, NULL, NULL, 0, cga);
    io_sethandler(0x03d0, 16,
		  vid_in_200, NULL, NULL, vid_out_200, NULL, NULL, vid);

    timer_add(cga_poll, &cga->vidtime, TIMER_ALWAYS_ENABLED, cga);

    overscan_x = overscan_y = 16;

    ams->vid = vid;
}


static void
vid_close_200(void *priv)
{
    amsvid_t *vid = (amsvid_t *)priv;

    free(vid->cga.vram);

    free(vid);
}


static void
vid_speed_changed_200(void *priv)
{
    amsvid_t *vid = (amsvid_t *)priv;

    cga_recalctimings(&vid->cga);
}


static const device_t vid_200_device = {
    "Amstrad PC200 (video)",
    0, 0,
    NULL, vid_close_200, NULL,
    NULL,
    vid_speed_changed_200,
    NULL
};


static void
ms_write(uint16_t addr, uint8_t val, void *priv)
{
    amstrad_t *ams = (amstrad_t *)priv;

    if (addr == 0x78)
	ams->mousex = 0;
      else
	ams->mousey = 0;
}


static uint8_t
ms_read(uint16_t addr, void *priv)
{
    amstrad_t *ams = (amstrad_t *)priv;

    if (addr == 0x78)
	return(ams->mousex);

    return(ams->mousey);
}


static int
ms_poll(int x, int y, int z, int b, void *priv)
{
    amstrad_t *ams = (amstrad_t *)priv;

    ams->mousex += x;
    ams->mousey -= y;

    if ((b & 1) && !(ams->oldb & 1))
	keyboard_send(0x7e);
    if ((b & 2) && !(ams->oldb & 2))
	keyboard_send(0x7d);
    if (!(b & 1) && (ams->oldb & 1))
	keyboard_send(0xfe);
    if (!(b & 2) && (ams->oldb & 2))
	keyboard_send(0xfd);

    ams->oldb = b;

    return(0);
}


static void
kbd_adddata(uint16_t val)
{
    key_queue[key_queue_end] = val;
    amstrad_log("keyboard_amstrad : %02X added to key queue at %i\n",
					val, key_queue_end);
    key_queue_end = (key_queue_end + 1) & 0xf;
}


static void
kbd_adddata_ex(uint16_t val)
{
    kbd_adddata_process(val, kbd_adddata);
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

		timer_process();
		timer_update_outstanding();

		speaker_update();
		speaker_gated = val & 0x01;
		speaker_enable = val & 0x02;
		if (speaker_enable) 
			was_speaker_enable = 1;
		pit_set_gate(&pit, 2, val & 0x01);

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
		pc_reset(1);
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
			ret = (0x0d | ams->stat1) & 0x7f;
		} else {
			ret = ams->pa;
			if (key_queue_start == key_queue_end) {
				ams->wantirq = 0;
			} else {
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

    keyboard_delay += (1000 * TIMER_USEC);

    if (ams->wantirq) {
	ams->wantirq = 0;
	ams->pa = ams->key_waiting;
	picint(2);
	amstrad_log("keyboard_amstrad : take IRQ\n");
    }

    if (key_queue_start != key_queue_end && !ams->pa) {
	ams->key_waiting = key_queue[key_queue_start];
	amstrad_log("Reading %02X from the key queue at %i\n",
			ams->key_waiting, key_queue_start);
	key_queue_start = (key_queue_start + 1) & 0xf;
	ams->wantirq = 1;
    }
}


static void
ams_write(uint16_t port, uint8_t val, void *priv)
{
    amstrad_t *ams = (amstrad_t *)priv;

    switch (port) {
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
	case 0x0379:	/* printer control, also set LK1-3.
			 *   0	English Language.
			 *   1	German Language.
			 *   2	French Language.
			 *   3	Spanish Language.
			 *   4	Danish Language.
			 *   5	Swedish Language.
			 *   6	Italian Language.
			 *   7	Diagnostic Mode.
			 */
		ret = 0x02;	/* ENGLISH. no Diags mode */
		break;

	case 0x037a:	/* printer status */
		switch(romset) {
			case ROM_PC1512:
				ret = 0x20;
				break;

			case ROM_PC200:
				ret = 0x80;
				break;

			default:
				ret = 0x00;
		}
		break;

	case 0xdead:
		ret = ams->dead;
		break;
    }

    return(ret);
}


void
machine_amstrad_init(const machine_t *model)
{
    amstrad_t *ams;

    ams = (amstrad_t *)malloc(sizeof(amstrad_t));
    memset(ams, 0x00, sizeof(amstrad_t));

    device_add(&amstrad_nvr_device);

    machine_common_init(model);

    nmi_init();

    lpt2_remove_ams();

    io_sethandler(0x0379, 2,
		  ams_read, NULL, NULL, NULL, NULL, NULL, ams);

    io_sethandler(0xdead, 1,
		  ams_read, NULL, NULL, ams_write, NULL, NULL, ams);

    io_sethandler(0x0078, 1,
		  ms_read, NULL, NULL, ms_write, NULL, NULL, ams);

    io_sethandler(0x007a, 1,
		  ms_read, NULL, NULL, ms_write, NULL, NULL, ams);

// 		device_add(&fdc_at_actlow_device);

    switch(romset) {
	case ROM_PC1512:
		device_add(&fdc_xt_device);
		break;

	case ROM_PC1640:
		device_add(&fdc_xt_device);
		break;

	case ROM_PC200:
		device_add(&fdc_xt_device);
		break;

	case ROM_PC2086:
		device_add(&fdc_at_actlow_device);
		break;

	case ROM_PC3086:
		device_add(&fdc_at_actlow_device);
		break;

	case ROM_MEGAPC:
		device_add(&fdc_at_actlow_device);
		break;
    }

    if (gfxcard == GFX_INTERNAL) switch(romset) {
	case ROM_PC1512:
                loadfont(L"roms/machines/pc1512/40078", 2);
		vid_init_1512(ams);
		device_add_ex(&vid_1512_device, ams->vid);
		break;

	case ROM_PC1640:
		vid_init_1640(ams);
		device_add_ex(&vid_1640_device, ams->vid);
		break;

	case ROM_PC200:
		loadfont(L"roms/machines/pc200/40109.bin", 1);
		vid_init_200(ams);
		device_add_ex(&vid_200_device, ams->vid);
		break;

	case ROM_PC2086:
		device_add(&paradise_pvga1a_pc2086_device);
		break;

	case ROM_PC3086:
		device_add(&paradise_pvga1a_pc3086_device);
		break;

	case ROM_MEGAPC:
		device_add(&paradise_wd90c11_megapc_device);
		break;
    }

    /* Initialize the (custom) keyboard/mouse interface. */
    ams->wantirq = 0;
    io_sethandler(0x0060, 7,
		  kbd_read, NULL, NULL, kbd_write, NULL, NULL, ams);
    timer_add(kbd_poll, &keyboard_delay, TIMER_ALWAYS_ENABLED, ams);
    keyboard_set_table(scancode_xt);
    keyboard_send = kbd_adddata_ex;
    keyboard_scan = 1;

    /* Tell mouse driver about our internal mouse. */
    mouse_reset();
    mouse_set_poll(ms_poll, ams);

    if (joystick_type != 7)
	device_add(&gameport_device);
}
