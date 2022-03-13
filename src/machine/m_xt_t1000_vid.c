/*
 * VARCem	Virtual ARchaeological Computer EMulator.
 *		An emulator of (mostly) x86-based PC systems and devices,
 *		using the ISA,EISA,VLB,MCA  and PCI system buses, roughly
 *		spanning the era between 1981 and 1995.
 *
 *		This file is part of the VARCem Project.
 *
 *		Implementation of the Toshiba T1000 plasma display, which
 *		has a fixed resolution of 640x200 pixels.
 *
 *
 *
 * Authors:	Fred N. van Kempen, <decwiz@yahoo.com>
 *		Miran Grca, <mgrca8@gmail.com>
 *		Sarah Walker, <tommowalker@tommowalker.co.uk>
 *
 *		Copyright 2018,2019 Fred N. van Kempen.
 *		Copyright 2018,2019 Miran Grca.
 *		Copyright 2018,2019 Sarah Walker.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free  Software  Foundation; either  version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is  distributed in the hope that it will be useful, but
 * WITHOUT   ANY  WARRANTY;  without  even   the  implied  warranty  of
 * MERCHANTABILITY  or FITNESS  FOR A PARTICULAR  PURPOSE. See  the GNU
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
#include <86box/m_xt_t1000.h>


#define T1000_XSIZE 640
#define T1000_YSIZE 200


/* Mapping of attributes to colours */
static uint32_t blue, grey;
static uint8_t  boldcols[256];		/* Which attributes use the bold font */
static uint32_t blinkcols[256][2];
static uint32_t normcols[256][2];
static uint8_t  language;

static video_timings_t timing_t1000    = {VIDEO_ISA, 8,16,32, 8,16,32};


/* Video options set by the motherboard; they will be picked up by the card
 * on the next poll.
 *
 * Bit  1:   Danish
 * Bit  0:   Thin font
 */
static uint8_t st_video_options;
static uint8_t st_enabled = 1;
static int8_t st_display_internal = -1;

void t1000_video_options_set(uint8_t options)
{
	st_video_options = options & 1;
	st_video_options |= language;
}

void t1000_video_enable(uint8_t enabled)
{
	st_enabled = enabled;
}

void t1000_display_set(uint8_t internal)
{
	st_display_internal = (int8_t)internal;
}

uint8_t t1000_display_get()
{
	return (uint8_t)st_display_internal;
}


typedef struct t1000_t
{
        mem_mapping_t mapping;

	cga_t cga;		/* The CGA is used for the external
				 * display; most of its registers are
				 * ignored by the plasma display. */

	int font;		/* Current font, 0-3 */
	int enabled;		/* Hardware enabled, 0 or 1 */
	int internal;		/* Using internal display? */
	uint8_t	attrmap;	/* Attribute mapping register */

        uint64_t dispontime, dispofftime;

        int linepos, displine;
        int vc;
        int dispon;
        int vsynctime;
	uint8_t video_options;
	uint8_t backlight, invert;

        uint8_t *vram;
} t1000_t;


static void t1000_recalctimings(t1000_t *t1000);
static void t1000_write(uint32_t addr, uint8_t val, void *p);
static uint8_t t1000_read(uint32_t addr, void *p);
static void t1000_recalcattrs(t1000_t *t1000);


static void t1000_out(uint16_t addr, uint8_t val, void *p)
{
        t1000_t *t1000 = (t1000_t *)p;
        switch (addr)
        {
		/* Emulated CRTC, register select */
		case 0x3d0: case 0x3d2: case 0x3d4: case 0x3d6:
		cga_out(addr, val, &t1000->cga);
                break;

		/* Emulated CRTC, value */
                case 0x3d1: case 0x3d3: case 0x3d5: case 0x3d7:
		/* Register 0x12 controls the attribute mappings for the
		 * LCD screen. */
		if (t1000->cga.crtcreg == 0x12)
		{
			t1000->attrmap = val;
			t1000_recalcattrs(t1000);
			return;
		}
		cga_out(addr, val, &t1000->cga);

                t1000_recalctimings(t1000);
	        return;

		/* CGA control register */
                case 0x3D8:
		cga_out(addr, val, &t1000->cga);
              	return;
		/* CGA colour register */
                case 0x3D9:
		cga_out(addr, val, &t1000->cga);
              	return;
        }
}

static uint8_t t1000_in(uint16_t addr, void *p)
{
        t1000_t *t1000 = (t1000_t *)p;
	uint8_t val;

        switch (addr)
        {
                case 0x3d1: case 0x3d3: case 0x3d5: case 0x3d7:
		if (t1000->cga.crtcreg == 0x12)
		{
			val = t1000->attrmap & 0x0F;
			if (t1000->internal) val |= 0x20; /* LCD / CRT */
			return val;
		}
	}

	return cga_in(addr, &t1000->cga);
}




static void t1000_write(uint32_t addr, uint8_t val, void *p)
{
        t1000_t *t1000 = (t1000_t *)p;

        t1000->vram[addr & 0x3fff] = val;
        cycles -= 4;
}

static uint8_t t1000_read(uint32_t addr, void *p)
{
        t1000_t *t1000 = (t1000_t *)p;
	cycles -= 4;

        return t1000->vram[addr & 0x3fff];
}



static void t1000_recalctimings(t1000_t *t1000)
{
        double disptime;
	double _dispontime, _dispofftime;

	if (!t1000->internal)
	{
		cga_recalctimings(&t1000->cga);
		return;
	}
	disptime = 651;
	_dispontime = 640;
        _dispofftime = disptime - _dispontime;
	t1000->dispontime  = (uint64_t)(_dispontime  * xt_cpu_multi);
	t1000->dispofftime = (uint64_t)(_dispofftime * xt_cpu_multi);
}

/* Draw a row of text in 80-column mode */
static void t1000_text_row80(t1000_t *t1000)
{
	uint32_t cols[2];
	int x, c;
        uint8_t chr, attr;
        int drawcursor;
	int cursorline;
	int bold;
	int blink;
	uint16_t addr;
	uint8_t sc;
	uint16_t ma = (t1000->cga.crtc[13] | (t1000->cga.crtc[12] << 8)) & 0x3fff;
	uint16_t ca = (t1000->cga.crtc[15] | (t1000->cga.crtc[14] << 8)) & 0x3fff;

	sc = (t1000->displine) & 7;
	addr = ((ma & ~1) + (t1000->displine >> 3) * 80) * 2;
	ma += (t1000->displine >> 3) * 80;

	if ((t1000->cga.crtc[10] & 0x60) == 0x20)
	{
		cursorline = 0;
	}
	else
	{
		cursorline = ((t1000->cga.crtc[10] & 0x0F) <= sc) &&
			     ((t1000->cga.crtc[11] & 0x0F) >= sc);
	}
	for (x = 0; x < 80; x++)
        {
		chr  = t1000->vram[(addr + 2 * x) & 0x3FFF];
		attr = t1000->vram[(addr + 2 * x + 1) & 0x3FFF];
                drawcursor = ((ma == ca) && cursorline &&
			(t1000->cga.cgamode & 8) && (t1000->cga.cgablink & 16));

		blink = ((t1000->cga.cgablink & 16) && (t1000->cga.cgamode & 0x20) &&
			(attr & 0x80) && !drawcursor);

		if (t1000->video_options & 1)
                        bold = boldcols[attr] ? chr : chr + 256;
		else
			bold = boldcols[attr] ? chr + 256 : chr;
		if (t1000->video_options & 2)
                        bold += 512;

                if (t1000->cga.cgamode & 0x20)	/* Blink */
                {
			cols[1] = blinkcols[attr][1];
			cols[0] = blinkcols[attr][0];
                        if (blink) cols[1] = cols[0];
		}
		else
		{
			cols[1] = normcols[attr][1];
			cols[0] = normcols[attr][0];
		}
                if (drawcursor)
                {
                	for (c = 0; c < 8; c++)
			{
                       		((uint32_t *)buffer32->line[t1000->displine])[(x << 3) + c] = cols[(fontdat[bold][sc] & (1 << (c ^ 7))) ? 1 : 0] ^ (blue ^ grey);
			}
		}
                else
                {
                	for (c = 0; c < 8; c++)
				((uint32_t *)buffer32->line[t1000->displine])[(x << 3) + c] = cols[(fontdat[bold][sc] & (1 << (c ^ 7))) ? 1 : 0];
                }
		++ma;
	}
}

/* Draw a row of text in 40-column mode */
static void t1000_text_row40(t1000_t *t1000)
{
	uint32_t cols[2];
	int x, c;
        uint8_t chr, attr;
        int drawcursor;
	int cursorline;
	int bold;
	int blink;
	uint16_t addr;
	uint8_t sc;
	uint16_t ma = (t1000->cga.crtc[13] | (t1000->cga.crtc[12] << 8)) & 0x3fff;
	uint16_t ca = (t1000->cga.crtc[15] | (t1000->cga.crtc[14] << 8)) & 0x3fff;

	sc = (t1000->displine) & 7;
	addr = ((ma & ~1) + (t1000->displine >> 3) * 40) * 2;
	ma += (t1000->displine >> 3) * 40;

	if ((t1000->cga.crtc[10] & 0x60) == 0x20)
	{
		cursorline = 0;
	}
	else
	{
		cursorline = ((t1000->cga.crtc[10] & 0x0F) <= sc) &&
			     ((t1000->cga.crtc[11] & 0x0F) >= sc);
	}
	for (x = 0; x < 40; x++)
        {
		chr  = t1000->vram[(addr + 2 * x) & 0x3FFF];
		attr = t1000->vram[(addr + 2 * x + 1) & 0x3FFF];
                drawcursor = ((ma == ca) && cursorline &&
			(t1000->cga.cgamode & 8) && (t1000->cga.cgablink & 16));

		blink = ((t1000->cga.cgablink & 16) && (t1000->cga.cgamode & 0x20) &&
			(attr & 0x80) && !drawcursor);

		if (t1000->video_options & 1)
			bold = boldcols[attr] ? chr : chr + 256;
		else
                        bold = boldcols[attr] ? chr + 256 : chr;
		if (t1000->video_options & 2)
                        bold += 512;

                if (t1000->cga.cgamode & 0x20)	/* Blink */
                {
			cols[1] = blinkcols[attr][1];
			cols[0] = blinkcols[attr][0];
                        if (blink) cols[1] = cols[0];
		}
		else
		{
			cols[1] = normcols[attr][1];
			cols[0] = normcols[attr][0];
		}
                if (drawcursor)
                {
                	for (c = 0; c < 8; c++)
			{
                       		((uint32_t *)buffer32->line[t1000->displine])[(x << 4) + c*2] =
                       		((uint32_t *)buffer32->line[t1000->displine])[(x << 4) + c*2 + 1] = cols[(fontdat[bold][sc] & (1 << (c ^ 7))) ? 1 : 0] ^ (blue ^ grey);
			}
		}
                else
                {
                	for (c = 0; c < 8; c++)
			{
				((uint32_t *)buffer32->line[t1000->displine])[(x << 4) + c*2] =
				((uint32_t *)buffer32->line[t1000->displine])[(x << 4) + c*2+1] = cols[(fontdat[bold][sc] & (1 << (c ^ 7))) ? 1 : 0];
			}
                }
		++ma;
	}
}

/* Draw a line in CGA 640x200 mode */
static void t1000_cgaline6(t1000_t *t1000)
{
	int x, c;
	uint8_t dat;
	uint32_t ink = 0;
	uint16_t addr;
	uint32_t fg = (t1000->cga.cgacol & 0x0F) ? blue : grey;
	uint32_t bg = grey;

	uint16_t ma = (t1000->cga.crtc[13] | (t1000->cga.crtc[12] << 8)) & 0x3fff;

	addr = ((t1000->displine) & 1) * 0x2000 +
	       (t1000->displine >> 1) * 80 +
	       ((ma & ~1) << 1);

	for (x = 0; x < 80; x++)
	{
		dat = t1000->vram[addr & 0x3FFF];
		addr++;

		for (c = 0; c < 8; c++)
		{
			ink = (dat & 0x80) ? fg : bg;
			if (!(t1000->cga.cgamode & 8))
                                ink = grey;
			((uint32_t *)buffer32->line[t1000->displine])[x*8+c] = ink;
			dat = dat << 1;
		}
	}
}

/* Draw a line in CGA 320x200 mode. Here the CGA colours are converted to
 * dither patterns: colour 1 to 25% grey, colour 2 to 50% grey */
static void t1000_cgaline4(t1000_t *t1000)
{
	int x, c;
	uint8_t dat, pattern;
	uint32_t ink0, ink1;
	uint16_t addr;

	uint16_t ma = (t1000->cga.crtc[13] | (t1000->cga.crtc[12] << 8)) & 0x3fff;
	addr = ((t1000->displine) & 1) * 0x2000 +
	       (t1000->displine >> 1) * 80 +
	       ((ma & ~1) << 1);

	for (x = 0; x < 80; x++)
	{
		dat = t1000->vram[addr & 0x3FFF];
		addr++;

		for (c = 0; c < 4; c++)
		{
			pattern = (dat & 0xC0) >> 6;
			if (!(t1000->cga.cgamode & 8)) pattern = 0;

			switch (pattern & 3)
			{
				default:
				case 0: ink0 = ink1 = grey; break;
				case 1: if (t1000->displine & 1)
					{
						ink0 = grey; ink1 = grey;
					}
					else
					{
						ink0 = blue; ink1 = grey;
					}
					break;
				case 2: if (t1000->displine & 1)
					{
						ink0 = grey; ink1 = blue;
					}
					else
					{
						ink0 = blue; ink1 = grey;
					}
					break;
				case 3: ink0 = ink1 = blue; break;

			}
			((uint32_t *)buffer32->line[t1000->displine])[x*8+2*c] = ink0;
			((uint32_t *)buffer32->line[t1000->displine])[x*8+2*c+1] = ink1;
			dat = dat << 2;
		}
	}
}

static void t1000_poll(void *p)
{
        t1000_t *t1000 = (t1000_t *)p;

	if (t1000->video_options != st_video_options ||
	    t1000->enabled != st_enabled)
	{
		t1000->video_options = st_video_options;
		t1000->enabled = st_enabled;

		/* Set the font used for the external display */
		t1000->cga.fontbase = ((t1000->video_options & 3) * 256);

		if (t1000->enabled) /* Disable internal chipset */
			mem_mapping_enable(&t1000->mapping);
		else
			mem_mapping_disable(&t1000->mapping);
	}
	/* Switch between internal plasma and external CRT display. */
	if (st_display_internal != -1 && st_display_internal != t1000->internal)
	{
		t1000->internal = st_display_internal;
                t1000_recalctimings(t1000);
	}
	if (!t1000->internal)
	{
		cga_poll(&t1000->cga);
		return;
	}

        if (!t1000->linepos)
        {
		timer_advance_u64(&t1000->cga.timer, t1000->dispofftime);
                t1000->cga.cgastat |= 1;
                t1000->linepos = 1;
                if (t1000->dispon)
                {
                        if (t1000->displine == 0)
                        {
                                video_wait_for_buffer();
                        }

			/* Graphics */
			if (t1000->cga.cgamode & 0x02)
			{
				if (t1000->cga.cgamode & 0x10)
					t1000_cgaline6(t1000);
				else	t1000_cgaline4(t1000);
			}
			else
			if (t1000->cga.cgamode & 0x01) /* High-res text */
			{
				t1000_text_row80(t1000);
			}
			else
			{
				t1000_text_row40(t1000);
			}
                }
                t1000->displine++;
		/* Hardcode a fixed refresh rate and VSYNC timing */
                if (t1000->displine == 200) /* Start of VSYNC */
                {
                        t1000->cga.cgastat |= 8;
			t1000->dispon = 0;
                }
		if (t1000->displine == 216) /* End of VSYNC */
		{
                        t1000->displine = 0;
                        t1000->cga.cgastat &= ~8;
			t1000->dispon = 1;
		}
        }
        else
        {
		if (t1000->dispon)
		{
                	t1000->cga.cgastat &= ~1;
		}
                timer_advance_u64(&t1000->cga.timer, t1000->dispontime);
                t1000->linepos = 0;

		if (t1000->displine == 200)
                {
                        /* Hardcode 640x200 window size */
			if ((T1000_XSIZE != xsize) || (T1000_YSIZE != ysize) || video_force_resize_get())
			{
                                xsize = T1000_XSIZE;
                                ysize = T1000_YSIZE;
                                if (xsize < 64) xsize = 656;
                                if (ysize < 32) ysize = 200;
                                set_screen_size(xsize, ysize);

				if (video_force_resize_get())
					video_force_resize_set(0);
                        }
                        video_blit_memtoscreen(0, 0, xsize, ysize);

                        frames++;
			/* Fixed 640x200 resolution */
			video_res_x = T1000_XSIZE;
			video_res_y = T1000_YSIZE;

			if (t1000->cga.cgamode & 0x02)
			{
				if (t1000->cga.cgamode & 0x10)
					video_bpp = 1;
				else	video_bpp = 2;

			}
			else	 video_bpp = 0;
                	t1000->cga.cgablink++;
                }
        }
}

static void t1000_recalcattrs(t1000_t *t1000)
{
	int n;

	/* val behaves as follows:
	 *     Bit 0: Attributes 01-06, 08-0E are inverse video
	 *     Bit 1: Attributes 01-06, 08-0E are bold
	 *     Bit 2: Attributes 11-16, 18-1F, 21-26, 28-2F ... F1-F6, F8-FF
	 * 	      are inverse video
	 *     Bit 3: Attributes 11-16, 18-1F, 21-26, 28-2F ... F1-F6, F8-FF
	 * 	      are bold */

	/* Set up colours */
	if (t1000->invert) {
		if (t1000->backlight) {
			grey = makecol(0x2D, 0x39, 0x5A);
			blue = makecol(0x85, 0xa0, 0xD6);
		} else {
			grey = makecol(0x0f, 0x21, 0x3f);
			blue = makecol(0x1C, 0x71, 0x31);
		}
	} else {
		if (t1000->backlight) {
			blue = makecol(0x2D, 0x39, 0x5A);
			grey = makecol(0x85, 0xa0, 0xD6);
		} else {
			blue = makecol(0x0f, 0x21, 0x3f);
			grey = makecol(0x1C, 0x71, 0x31);
		}
	}

	/* Initialise the attribute mapping. Start by defaulting everything
	 * to grey on blue, and with bold set by bit 3 */
	for (n = 0; n < 256; n++)
	{
		boldcols[n] = (n & 8) != 0;
		blinkcols[n][0] = normcols[n][0] = blue;
		blinkcols[n][1] = normcols[n][1] = grey;
	}

	/* Colours 0x11-0xFF are controlled by bits 2 and 3 of the
	 * passed value. Exclude x0 and x8, which are always grey on
	 * blue. */
	for (n = 0x11; n <= 0xFF; n++)
	{
		if ((n & 7) == 0) continue;
		if (t1000->attrmap & 4)	/* Inverse */
		{
			blinkcols[n][0] = normcols[n][0] = blue;
			blinkcols[n][1] = normcols[n][1] = grey;
		}
		else				/* Normal */
		{
			blinkcols[n][0] = normcols[n][0] = grey;
			blinkcols[n][1] = normcols[n][1] = blue;
		}
		if (t1000->attrmap & 8) boldcols[n] = 1;	/* Bold */
	}
	/* Set up the 01-0E range, controlled by bits 0 and 1 of the
	 * passed value. When blinking is enabled this also affects 81-8E. */
	for (n = 0x01; n <= 0x0E; n++)
	{
		if (n == 7) continue;
		if (t1000->attrmap & 1)
		{
			blinkcols[n][0] = normcols[n][0] = blue;
			blinkcols[n][1] = normcols[n][1] = grey;
			blinkcols[n+128][0] = blue;
			blinkcols[n+128][1] = grey;
		}
		else
		{
			blinkcols[n][0] = normcols[n][0] = grey;
			blinkcols[n][1] = normcols[n][1] = blue;
			blinkcols[n+128][0] = grey;
			blinkcols[n+128][1] = blue;
		}
		if (t1000->attrmap & 2) boldcols[n] = 1;
	}
	/* Colours 07 and 0F are always blue on grey. If blinking is
	 * enabled so are 87 and 8F. */
	for (n = 0x07; n <= 0x0F; n += 8)
	{
		blinkcols[n][0] = normcols[n][0] = grey;
		blinkcols[n][1] = normcols[n][1] = blue;
		blinkcols[n+128][0] = grey;
		blinkcols[n+128][1] = blue;
	}
	/* When not blinking, colours 81-8F are always blue on grey. */
	for (n = 0x81; n <= 0x8F; n ++)
	{
		normcols[n][0] = grey;
		normcols[n][1] = blue;
		boldcols[n] = (n & 0x08) != 0;
	}


	/* Finally do the ones which are solid grey. These differ between
	 * the normal and blinking mappings */
	for (n = 0; n <= 0xFF; n += 0x11)
	{
		normcols[n][0] = normcols[n][1] = grey;
	}
	/* In the blinking range, 00 11 22 .. 77 and 80 91 A2 .. F7 are grey */
	for (n = 0; n <= 0x77; n += 0x11)
	{
		blinkcols[n][0] = blinkcols[n][1] = grey;
		blinkcols[n+128][0] = blinkcols[n+128][1] = grey;
	}
}


static void *t1000_init(const device_t *info)
{
        t1000_t *t1000 = malloc(sizeof(t1000_t));
        memset(t1000, 0, sizeof(t1000_t));
	loadfont("roms/machines/t1000/t1000font.bin", 8);
	cga_init(&t1000->cga);
	video_inform(VIDEO_FLAG_TYPE_CGA, &timing_t1000);

	t1000->internal = 1;

	t1000->backlight = device_get_config_int("backlight");
	t1000->invert = device_get_config_int("invert");

	/* 16k video RAM */
        t1000->vram = malloc(0x4000);

        timer_set_callback(&t1000->cga.timer, t1000_poll);
        timer_set_p(&t1000->cga.timer, t1000);

	/* Occupy memory between 0xB8000 and 0xBFFFF */
        mem_mapping_add(&t1000->mapping, 0xb8000, 0x8000, t1000_read, NULL, NULL, t1000_write, NULL, NULL,  NULL, 0, t1000);
	/* Respond to CGA I/O ports */
        io_sethandler(0x03d0, 0x000c, t1000_in, NULL, NULL, t1000_out, NULL, NULL, t1000);

	/* Default attribute mapping is 4 */
	t1000->attrmap = 4;
	t1000_recalcattrs(t1000);

        /* Start off in 80x25 text mode */
        t1000->cga.cgastat   = 0xF4;
	t1000->cga.vram = t1000->vram;
	t1000->enabled    = 1;
	t1000->video_options = 0x01;
	language = device_get_config_int("display_language") ? 2 : 0;
	return t1000;
}

static void t1000_close(void *p)
{
        t1000_t *t1000 = (t1000_t *)p;

        free(t1000->vram);
        free(t1000);
}

static void t1000_speed_changed(void *p)
{
        t1000_t *t1000 = (t1000_t *)p;

        t1000_recalctimings(t1000);
}

static const device_config_t t1000_config[] = {
    {
        .name = "display_language",
        .description = "Language",
        .type = CONFIG_SELECTION,
        .selection =
        {
            {
                .description = "USA",
                .value = 0
            },
            {
                .description = "Danish",
                .value = 1
            }
        },
        .default_int = 0
    },
    {
        .name = "backlight",
        .description = "Enable backlight",
        .type = CONFIG_BINARY,
        .default_string = "",
        .default_int = 1 },
    {
        .name = "invert",
        .description = "Invert colors",
        .type = CONFIG_BINARY,
        .default_string = "",
        .default_int = 0
    },
    { .type = -1 }
};

const device_t t1000_video_device = {
    .name = "Toshiba T1000 Video",
    .internal_name = "t1000_video",
    .flags = 0,
    .local = 0,
    .init = t1000_init,
    .close = t1000_close,
    .reset = NULL,
    { .available = NULL },
    .speed_changed = t1000_speed_changed,
    .force_redraw = NULL,
    .config = t1000_config
};

const device_t t1200_video_device = {
    .name = "Toshiba T1200 Video",
    .internal_name = "t1200_video",
    .flags = 0,
    .local = 0,
    .init = t1000_init,
    .close = t1000_close,
    .reset = NULL,
    { .available = NULL },
    .speed_changed = t1000_speed_changed,
    .force_redraw = NULL,
    .config = t1000_config
};
