/*
 * 86Box	A hypervisor and IBM PC system emulator that specializes in
 *		running old operating systems and software designed for IBM
 *		PC systems and compatibles from 1981 through fairly recent
 *		system designs based on the PCI bus.
 *
 *		This file is part of the 86Box distribution.
 *
 *		Chips & Technologies 82C425 display controller emulation,
 *		with support for 640x200 LCD and SMARTMAP text contrast
 *		enhancement.
 *
 *		Relevant literature:
 *
 *		[1] Chips and Technologies, Inc., 82C425 CGA LCD/CRT Controller,
 *		    Data Sheet, Revision No. 2.2, September 1991.
 *		    <https://archive.org/download/82C425/82C425.pdf>
 *
 *		[2] Pleva et al., COLOR TO MONOCHROME CONVERSION,
 *		    U.S. Patent 4,977,398, Dec. 11, 1990.
 *		    <https://pimg-fpiw.uspto.gov/fdd/98/773/049/0.pdf>
 *
 *		Based on Toshiba T1000 plasma display emulation code.
 *
 * Authors:	Fred N. van Kempen, <decwiz@yahoo.com>
 *		Miran Grca, <mgrca8@gmail.com>
 *		Sarah Walker, <tommowalker@tommowalker.co.uk>
 *		Lubomir Rintel, <lkundrak@v3.sk>
 *
 *		Copyright 2018,2019 Fred N. van Kempen.
 *		Copyright 2018,2019 Miran Grca.
 *		Copyright 2018,2019 Sarah Walker.
 *		Copyright 2021 Lubomir Rintel.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free  Software  Foundation; either  version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is  distributed in the hope that it will be useful, but
 * WITHOUT   ANY  WARRANTY;  without  even   the  implied  warranty  of
 * MERCHANTABILITY  or FITNESS	FOR A PARTICULAR  PURPOSE. See	the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the:
 *
 *   Free Software Foundation, Inc.
 *   59 Temple Place - Suite 330
 *   Boston, MA 02111-1307
 *   USA.
 */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>
#include <86box/86box.h>
#include <86box/device.h>
#include <86box/io.h>
#include <86box/mem.h>
#include <86box/timer.h>
#include "cpu.h"
#include <86box/video.h>
#include <86box/vid_cga.h>

#define F82C425_XSIZE 640
#define F82C425_YSIZE 200

/* Mapping of attributes to colours */
static uint32_t smartmap[256][2];
static uint32_t colormap[4];

static video_timings_t timing_f82c425 = {VIDEO_ISA, 8,16,32, 8,16,32};

static uint8_t st_video_options;
static uint8_t st_enabled = 1;
static int8_t st_display_internal = -1;

void f82c425_video_options_set(uint8_t options)
{
	st_video_options = options;
}

void f82c425_video_enable(uint8_t enabled)
{
	st_enabled = enabled;
}

void f82c425_display_set(uint8_t internal)
{
	st_display_internal = (int8_t)internal;
}

uint8_t f82c425_display_get()
{
	return (uint8_t)st_display_internal;
}


typedef struct f82c425_t
{
	mem_mapping_t mapping;
	cga_t cga;
	uint8_t crtcreg;

	uint64_t dispontime, dispofftime;

	int linepos, displine;
	int dispon;
	uint8_t video_options;

	uint8_t *vram;

	/* Registers specific to 82C425. */
	uint8_t ac_limit;
	uint8_t threshold;
	uint8_t shift;
	uint8_t hsync;
	uint8_t vsync_blink;
	uint8_t timing;
	uint8_t function;
} f82c425_t;


/* Convert IRGB representation to RGBI,
 * useful in SMARTMAP calculations. */
static inline uint8_t f82c425_rgbi(uint8_t irgb)
{
	return ((irgb & 0x7) << 1) | (irgb >> 3);
}

/* Convert IRGB SMARTMAP output to a RGB representation of one of 4/8 grey
 * shades we'd see on an actual V86P display: with some bias toward lighter
 * shades and a backlight with yellow/green-ish tint. */
static inline uint32_t f82c425_makecol(uint8_t rgbi, int gs4, int inv)
{
	uint8_t c;

	gs4 = 1 + !!gs4;
	if (!inv)
	{
		rgbi = 15 - rgbi;
	}
	c = 0x10 * gs4 * ((rgbi >> gs4) + 2);

#ifdef NO_BLUE
	return makecol(c, c + 0x08, c - 0x20);
#else
	return makecol(c, c + 0x08, 0x70);
#endif
}

/* Saturating/non-saturating addition for SMARTMAP(see below). */
static inline int f82c425_smartmap_add(int a, int b, int sat)
{
	int c = a + b;

	/* (SATURATING OR NON SATURATING) */
	if (sat)
	{
		if (c < 0)
			c = 0;
		else if (c > 15)
			c = 15;
	}

	return c & 0xf;
}

/* Calculate and cache mapping of CGA text color attribute to a
 * shade of gray enhanced via the SMARTMAP algorithm.
 *
 * This is a straightforward implementation of the algorithm as described
 * in U.S. Patent 4,977,398 [2]. The comments in capitals refer to portions
 * of a figure on page 4. */
static void f82c425_smartmap(f82c425_t *f82c425)
{
	int i;

	for (i = 0; i < 256; i++) {
		uint8_t bg = f82c425_rgbi(i >> 4);
		uint8_t fg = f82c425_rgbi(i & 0xf);

		/* FIG._4. */
		if (abs(bg - fg) <= (f82c425->threshold & 0x0f))
		{
			/* FOREGROUND=BACKGROUND */
			if (bg == fg)
			{
				/* SPECIAL CASE */
				if (f82c425->shift == 0xff)
				{
					/* CHECK MOST SIGNIFICANT BIT */
					if (fg & 0x8)
					{
						/* FULL WHITE */
						fg = bg = 15;
					}
					else
					{
						/* FULL BLACK */
						fg = bg = 0;
					}
				}
			}
			else
			{
				uint8_t sat = f82c425->threshold & 0x10;

				/* DETERMINE WHICH IS LIGHT */
				if (fg > bg)
				{
					fg = f82c425_smartmap_add(fg, f82c425->shift & 0x0f, sat);
					bg = f82c425_smartmap_add(bg, -(f82c425->shift >> 4), sat);
				}
				else
				{
					fg = f82c425_smartmap_add(fg, -(f82c425->shift & 0x0f), sat);
					bg = f82c425_smartmap_add(bg, f82c425->shift >> 4, sat);
				}
			}
		}

		smartmap[i][0] = f82c425_makecol(bg, f82c425->threshold & 0x20, f82c425->function & 0x80);
		smartmap[i][1] = f82c425_makecol(fg, f82c425->threshold & 0x20, f82c425->function & 0x80);
	}
}

/* Calculate mapping of 320x200 graphical mode colors. */
static void f82c425_colormap(f82c425_t *f82c425)
{
	int i;

	for (i = 0; i < 4; i++)
		colormap[i] = f82c425_makecol(5 * i, 0, f82c425->function & 0x80);
}

static void f82c425_out(uint16_t addr, uint8_t val, void *p)
{
	f82c425_t *f82c425 = (f82c425_t *)p;

	if (addr == 0x3d4)
		f82c425->crtcreg = val;

	if (((f82c425->function & 0x01) == 0) && ((f82c425->crtcreg != 0xdf) || (addr != 0x3d5)))
		return;

	if (addr != 0x3d5 || f82c425->crtcreg <= 31)
	{
		cga_out(addr, val, &f82c425->cga);
		return;
	}

	switch (f82c425->crtcreg)
	{
		case 0xd9:
			f82c425->ac_limit = val;
			break;
		case 0xda:
			f82c425->threshold = val;
			f82c425_smartmap(f82c425);
			break;
		case 0xdb:
			f82c425->shift = val;
			f82c425_smartmap(f82c425);
			break;
		case 0xdc:
			f82c425->hsync = val;
			break;
		case 0xdd:
			f82c425->vsync_blink = val;
			break;
		case 0xde:
			f82c425->timing = val;
			break;
		case 0xdf:
			f82c425->function = val;
			f82c425_smartmap(f82c425);
			f82c425_colormap(f82c425);
			break;
	}
}

static uint8_t f82c425_in(uint16_t addr, void *p)
{
	f82c425_t *f82c425 = (f82c425_t *)p;

	if ((f82c425->function & 0x01) == 0)
		return 0xff;

	if (addr == 0x3d4)
		return f82c425->crtcreg;

	if (addr != 0x3d5 || f82c425->crtcreg <= 31)
		return cga_in(addr, &f82c425->cga);

	switch (f82c425->crtcreg)
	{
		case 0xd9:
			return f82c425->ac_limit;
		case 0xda:
			return f82c425->threshold;
		case 0xdb:
			return f82c425->shift;
		case 0xdc:
			return f82c425->hsync;
		case 0xdd:
			return f82c425->vsync_blink;
		case 0xde:
			return f82c425->timing;
		case 0xdf:
			return f82c425->function;
	}

	return 0xff;
}

static void f82c425_write(uint32_t addr, uint8_t val, void *p)
{
	f82c425_t *f82c425 = (f82c425_t *)p;

	f82c425->vram[addr & 0x3fff] = val;
	cycles -= 4;
}

static uint8_t f82c425_read(uint32_t addr, void *p)
{
	f82c425_t *f82c425 = (f82c425_t *)p;
	cycles -= 4;

	return f82c425->vram[addr & 0x3fff];
}

static void f82c425_recalctimings(f82c425_t *f82c425)
{
	double disptime;
	double _dispontime, _dispofftime;

	if (f82c425->function & 0x08)
	{
		cga_recalctimings(&f82c425->cga);
		return;
	}

	disptime = 651;
	_dispontime = 640;
	_dispofftime = disptime - _dispontime;
	f82c425->dispontime = (uint64_t)(_dispontime  * xt_cpu_multi);
	f82c425->dispofftime = (uint64_t)(_dispofftime * xt_cpu_multi);
}

/* Draw a row of text. */
static void f82c425_text_row(f82c425_t *f82c425)
{
	uint32_t colors[2];
	int x, c;
	uint8_t chr, attr;
	int drawcursor;
	int cursorline;
	int blink;
	uint16_t addr;
	uint8_t sc;
	uint16_t ma = (f82c425->cga.crtc[0x0d] | (f82c425->cga.crtc[0x0c] << 8)) & 0x3fff;
	uint16_t ca = (f82c425->cga.crtc[0x0f] | (f82c425->cga.crtc[0x0e] << 8)) & 0x3fff;
	uint8_t sl = f82c425->cga.crtc[9] + 1;
	int columns = f82c425->cga.crtc[1];

	sc = (f82c425->displine) & 7;
	addr = ((ma & ~1) + (f82c425->displine >> 3) * columns) * 2;
	ma += (f82c425->displine >> 3) * columns;

	if ((f82c425->cga.crtc[0x0a] & 0x60) == 0x20)
	{
		cursorline = 0;
	}
	else
	{
		cursorline = ((f82c425->cga.crtc[0x0a] & 0x0F) <= sc) &&
			((f82c425->cga.crtc[0x0b] & 0x0F) >= sc);
	}

	for (x = 0; x < columns; x++)
	{
		chr = f82c425->vram[(addr + 2 * x) & 0x3FFF];
		attr = f82c425->vram[(addr + 2 * x + 1) & 0x3FFF];
		drawcursor = ((ma == ca) && cursorline &&
			(f82c425->cga.cgamode & 0x8) && (f82c425->cga.cgablink & 0x10));

		blink = ((f82c425->cga.cgablink & 0x10) && (f82c425->cga.cgamode & 0x20) &&
			(attr & 0x80) && !drawcursor);

		if (drawcursor)
		{
			colors[0] = smartmap[~attr & 0xff][0];
			colors[1] = smartmap[~attr & 0xff][1];
		}
		else
		{
			colors[0] = smartmap[attr][0];
			colors[1] = smartmap[attr][1];
		}

		if (blink)
			colors[1] = colors[0];

		if (f82c425->cga.cgamode & 0x01)
		{
			/* High resolution (80 cols) */
			for (c = 0; c < sl; c++)
			{
				((uint32_t *)buffer32->line[f82c425->displine])[(x << 3) + c] =
				colors[(fontdat[chr][sc] & (1 <<(c ^ 7))) ? 1 : 0];
			}
		}
		else
		{
			/* Low resolution (40 columns, stretch pixels horizontally) */
			for (c = 0; c < sl; c++)
			{
				((uint32_t *)buffer32->line[f82c425->displine])[(x << 4) + c*2] =
				((uint32_t *)buffer32->line[f82c425->displine])[(x << 4) + c*2+1] =
				colors[(fontdat[chr][sc] & (1 <<(c ^ 7))) ? 1 : 0];
			}
		}

		++ma;
	}
}

/* Draw a line in CGA 640x200 mode */
static void f82c425_cgaline6(f82c425_t *f82c425)
{
	int x, c;
	uint8_t dat;
	uint16_t addr;

	uint16_t ma = (f82c425->cga.crtc[0x0d] | (f82c425->cga.crtc[0x0c] << 8)) & 0x3fff;

	addr = ((f82c425->displine) & 1) * 0x2000 +
	       (f82c425->displine >> 1) * 80 +
	       ((ma & ~1) << 1);

	for (x = 0; x < 80; x++)
	{
		dat = f82c425->vram[addr & 0x3FFF];
		addr++;

		for (c = 0; c < 8; c++)
		{
			((uint32_t *)buffer32->line[f82c425->displine])[x*8+c] =
			colormap[dat & 0x80 ? 3 : 0];

			dat = dat << 1;
		}
	}
}

/* Draw a line in CGA 320x200 mode. */
static void f82c425_cgaline4(f82c425_t *f82c425)
{
	int x, c;
	uint8_t dat, pattern;
	uint16_t addr;

	uint16_t ma = (f82c425->cga.crtc[0x0d] | (f82c425->cga.crtc[0x0c] << 8)) & 0x3fff;
	addr = ((f82c425->displine) & 1) * 0x2000 +
	       (f82c425->displine >> 1) * 80 +
	       ((ma & ~1) << 1);

	for (x = 0; x < 80; x++)
	{
		dat = f82c425->vram[addr & 0x3FFF];
		addr++;

		for (c = 0; c < 4; c++)
		{
			pattern = (dat & 0xC0) >> 6;
			if (!(f82c425->cga.cgamode & 0x08)) pattern = 0;

			((uint32_t *)buffer32->line[f82c425->displine])[x*8+2*c] =
			((uint32_t *)buffer32->line[f82c425->displine])[x*8+2*c+1] =
			colormap[pattern & 3];

			dat = dat << 2;
		}
	}
}

static void f82c425_poll(void *p)
{
	f82c425_t *f82c425 = (f82c425_t *)p;

	if (f82c425->video_options != st_video_options ||
	    !!(f82c425->function & 1) != st_enabled)
	{
		f82c425->video_options = st_video_options;
		f82c425->function &= ~1;
		f82c425->function |= st_enabled ? 1 : 0;

		if (f82c425->function & 0x01)
			mem_mapping_enable(&f82c425->mapping);
		else
			mem_mapping_disable(&f82c425->mapping);
	}
	/* Switch between internal LCD and external CRT display. */
	if (st_display_internal != -1 && st_display_internal != !!(f82c425->function & 0x08))
	{
		if (st_display_internal)
		{
			f82c425->function &= ~0x08;
			f82c425->timing &= ~0x20;
		}
		else
		{
			f82c425->function |= 0x08;
			f82c425->timing |= 0x20;
		}
		f82c425_recalctimings(f82c425);
	}

	if (f82c425->function & 0x08)
	{
		cga_poll(&f82c425->cga);
		return;
	}

	if (!f82c425->linepos)
	{
		timer_advance_u64(&f82c425->cga.timer, f82c425->dispofftime);
		f82c425->cga.cgastat |= 1;
		f82c425->linepos = 1;
		if (f82c425->dispon)
		{
			if (f82c425->displine == 0)
			{
				video_wait_for_buffer();
			}

			switch (f82c425->cga.cgamode & 0x13)
			{
				case 0x12:
					f82c425_cgaline6(f82c425);
					break;
				case 0x02:
					f82c425_cgaline4(f82c425);
					break;
				case 0x00:
				case 0x01:
					f82c425_text_row(f82c425);
					break;
			}
		}
		f82c425->displine++;

		/* Hardcode a fixed refresh rate and VSYNC timing */
		if (f82c425->displine >= 216)
		{
			/* End of VSYNC */
			f82c425->displine = 0;
			f82c425->cga.cgastat &= ~8;
			f82c425->dispon = 1;
		}
		else
		if (f82c425->displine == (f82c425->cga.crtc[9] + 1) * f82c425->cga.crtc[6])
		{
			/* Start of VSYNC */
			f82c425->cga.cgastat |= 8;
			f82c425->dispon = 0;
		}
	}
	else
	{
		if (f82c425->dispon)
			f82c425->cga.cgastat &= ~1;
		timer_advance_u64(&f82c425->cga.timer, f82c425->dispontime);
		f82c425->linepos = 0;

		if (f82c425->displine == 200)
		{
			/* Hardcode 640x200 window size */
			if ((F82C425_XSIZE != xsize) || (F82C425_YSIZE != ysize) || video_force_resize_get())
			{
				xsize = F82C425_XSIZE;
				ysize = F82C425_YSIZE;
				set_screen_size(xsize, ysize);

				if (video_force_resize_get())
					video_force_resize_set(0);
			}
			video_blit_memtoscreen(0, 0, xsize, ysize);
			frames++;

			/* Fixed 640x200 resolution */
			video_res_x = F82C425_XSIZE;
			video_res_y = F82C425_YSIZE;

			switch (f82c425->cga.cgamode & 0x12)
			{
				case 0x12:
					video_bpp = 1;
					break;
				case 0x02:
					video_bpp = 2;
					break;
				default:
					video_bpp = 0;
			}

			f82c425->cga.cgablink++;
		}
	}
}

static void *f82c425_init(const device_t *info)
{
	f82c425_t *f82c425 = malloc(sizeof(f82c425_t));
	memset(f82c425, 0, sizeof(f82c425_t));
	cga_init(&f82c425->cga);
	video_inform(VIDEO_FLAG_TYPE_CGA, &timing_f82c425);

	/* Initialize registers that don't default to zero. */
	f82c425->hsync = 0x40;
	f82c425->vsync_blink = 0x72;

	/* 16k video RAM */
	f82c425->vram = malloc(0x4000);

	timer_set_callback(&f82c425->cga.timer, f82c425_poll);
	timer_set_p(&f82c425->cga.timer, f82c425);

	/* Occupy memory between 0xB8000 and 0xBFFFF */
	mem_mapping_add(&f82c425->mapping, 0xb8000, 0x8000, f82c425_read, NULL, NULL, f82c425_write, NULL, NULL,  NULL, 0, f82c425);
	/* Respond to CGA I/O ports */
	io_sethandler(0x03d0, 0x000c, f82c425_in, NULL, NULL, f82c425_out, NULL, NULL, f82c425);

	/* Initialize color maps for text & graphic modes */
	f82c425_smartmap(f82c425);
	f82c425_colormap(f82c425);

	/* Start off in 80x25 text mode */
	f82c425->cga.cgastat = 0xF4;
	f82c425->cga.vram = f82c425->vram;
	f82c425->video_options = 0x01;

	return f82c425;
}

static void f82c425_close(void *p)
{
	f82c425_t *f82c425 = (f82c425_t *)p;

	free(f82c425->vram);
	free(f82c425);
}

static void f82c425_speed_changed(void *p)
{
	f82c425_t *f82c425 = (f82c425_t *)p;

	f82c425_recalctimings(f82c425);
}

const device_t f82c425_video_device = {
    .name = "82C425 CGA LCD/CRT Controller",
    .internal_name = "f82c425_video",
    .flags = 0,
    .local = 0,
    .init = f82c425_init,
    .close = f82c425_close,
    .reset = NULL,
    { .available = NULL },
    .speed_changed = f82c425_speed_changed,
    .force_redraw = NULL,
    .config = NULL
};
