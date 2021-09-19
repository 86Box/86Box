/*
 * VARCem	Virtual ARchaeological Computer EMulator.
 *		An emulator of (mostly) x86-based PC systems and devices,
 *		using the ISA,EISA,VLB,MCA  and PCI system buses, roughly
 *		spanning the era between 1981 and 1995.
 *
 *		This file is part of the VARCem Project.
 *
 *		Implementation of the Toshiba 3100e plasma display.
 *		This display has a fixed 640x400 resolution.
 *
 *		T3100e CRTC regs (from the ROM):
 *
 *		Selecting a character height of 3 seems to be sufficient to
 *		convert the 640x200 graphics mode to 640x400 (and, by
 *		analogy, 320x200 to 320x400).
 * 
 *		Horiz-----> Vert------>  I ch
 *		38 28 2D 0A 1F 06 19 1C 02 07 06 07   CO40
 *		71 50 5A 0A 1F 06 19 1C 02 07 06 07   CO80
 *		38 28 2D 0A 7F 06 64 70 02 01 06 07   Graphics
 *		61 50 52 0F 19 06 19 19 02 0D 0B 0C   MONO
 *		2D 28 22 0A 67 00 64 67 02 03 06 07   640x400
 *
 *
 *
 * Authors:	Fred N. van Kempen, <decwiz@yahoo.com>
 *		Miran Grca, <mgrca8@gmail.com>
 *		Sarah Walker, <tommowalker@tommowalker.co.uk>
 *
 *		Copyright 2017-2019 Fred N. van Kempen.
 *		Copyright 2016-2019 Miran Grca.
 *		Copyright 2008-2019 Sarah Walker.
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
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>
#define HAVE_STDARG_H
#include <86box/86box.h>
#include <86box/device.h>
#include <86box/io.h>
#include <86box/mem.h>
#include <86box/timer.h>
#include "cpu.h"
#include <86box/video.h>
#include <86box/vid_cga.h>
#include <86box/m_at_t3100e.h>


#define T3100E_XSIZE 640
#define T3100E_YSIZE 400

/*Very rough estimate*/
#define VID_CLOCK (double)(651 * 416 * 60)


/* Mapping of attributes to colours */
static uint32_t amber, black;
static uint8_t  boldcols[256];		/* Which attributes use the bold font */
static uint32_t blinkcols[256][2];
static uint32_t normcols[256][2];

/* Video options set by the motherboard; they will be picked up by the card
 * on the next poll.
 *
 * Bit  3:   Disable built-in video (for add-on card)
 * Bit  2:   Thin font
 * Bits 0,1: Font set (not currently implemented) 
 */
static uint8_t st_video_options;
static int8_t st_display_internal = -1;

void t3100e_video_options_set(uint8_t options)
{
	st_video_options = options;
}

void t3100e_display_set(uint8_t internal)
{
	st_display_internal = internal;
}

uint8_t t3100e_display_get()
{
	return st_display_internal;
}


typedef struct t3100e_t
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

        uint8_t *vram;
} t3100e_t;

static video_timings_t timing_t3100e   = {VIDEO_ISA, 8,16,32, 8,16,32};


void t3100e_recalctimings(t3100e_t *t3100e);
void t3100e_write(uint32_t addr, uint8_t val, void *p);
uint8_t t3100e_read(uint32_t addr, void *p);
void t3100e_recalcattrs(t3100e_t *t3100e);


void t3100e_out(uint16_t addr, uint8_t val, void *p)
{
        t3100e_t *t3100e = (t3100e_t *)p;
        switch (addr)
        {
		/* Emulated CRTC, register select */
		case 0x3d0: case 0x3d2: case 0x3d4: case 0x3d6:
		cga_out(addr, val, &t3100e->cga);
                break;

		/* Emulated CRTC, value */
                case 0x3d1: case 0x3d3: case 0x3d5: case 0x3d7:
		/* Register 0x12 controls the attribute mappings for the
		 * plasma screen. */
		if (t3100e->cga.crtcreg == 0x12) 
		{
			t3100e->attrmap = val;	
			t3100e_recalcattrs(t3100e);
			return;
		}	
		cga_out(addr, val, &t3100e->cga);

                t3100e_recalctimings(t3100e);
	        return;

		/* CGA control register */
                case 0x3D8:
		cga_out(addr, val, &t3100e->cga);
              	return;
		/* CGA colour register */
                case 0x3D9:
		cga_out(addr, val, &t3100e->cga);
              	return;
        }
}

uint8_t t3100e_in(uint16_t addr, void *p)
{
        t3100e_t *t3100e = (t3100e_t *)p;
	uint8_t val;

        switch (addr)
        {
                case 0x3d1: case 0x3d3: case 0x3d5: case 0x3d7:
		if (t3100e->cga.crtcreg == 0x12)
		{
			val = t3100e->attrmap & 0x0F;
			if (t3100e->internal) val |= 0x30; /* Plasma / CRT */
			return val;
		}
	}
	
	return cga_in(addr, &t3100e->cga);
}




void t3100e_write(uint32_t addr, uint8_t val, void *p)
{
        t3100e_t *t3100e = (t3100e_t *)p;

        t3100e->vram[addr & 0x7fff] = val;
        cycles -= 4;
}
	


uint8_t t3100e_read(uint32_t addr, void *p)
{
        t3100e_t *t3100e = (t3100e_t *)p;
	cycles -= 4;

        return t3100e->vram[addr & 0x7fff];
}



void t3100e_recalctimings(t3100e_t *t3100e)
{
        double disptime;
	double _dispontime, _dispofftime;

	if (!t3100e->internal)
	{
		cga_recalctimings(&t3100e->cga);
		return;
	}
	disptime = 651;
	_dispontime = 640;
        _dispofftime = disptime - _dispontime;
	t3100e->dispontime  = (uint64_t)(_dispontime * (cpuclock / VID_CLOCK) * (double)(1ull << 32));
	t3100e->dispofftime = (uint64_t)(_dispofftime * (cpuclock / VID_CLOCK) * (double)(1ull << 32));
}


/* Draw a row of text in 80-column mode */
void t3100e_text_row80(t3100e_t *t3100e)
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
	uint16_t ma = (t3100e->cga.crtc[13] | (t3100e->cga.crtc[12] << 8)) & 0x7fff;
	uint16_t ca = (t3100e->cga.crtc[15] | (t3100e->cga.crtc[14] << 8)) & 0x7fff;

	sc = (t3100e->displine) & 15;
	addr = ((ma & ~1) + (t3100e->displine >> 4) * 80) * 2;
	ma += (t3100e->displine >> 4) * 80;

	if ((t3100e->cga.crtc[10] & 0x60) == 0x20)
	{
		cursorline = 0;
	}
	else
	{
		cursorline = ((t3100e->cga.crtc[10] & 0x0F)*2 <= sc) &&
			     ((t3100e->cga.crtc[11] & 0x0F)*2 >= sc);
	}
	for (x = 0; x < 80; x++)
        {
		chr  = t3100e->vram[(addr + 2 * x) & 0x7FFF];
		attr = t3100e->vram[(addr + 2 * x + 1) & 0x7FFF];
                drawcursor = ((ma == ca) && cursorline &&
			(t3100e->cga.cgamode & 8) && (t3100e->cga.cgablink & 16));

		blink = ((t3100e->cga.cgablink & 16) && (t3100e->cga.cgamode & 0x20) &&
			(attr & 0x80) && !drawcursor);

		if (t3100e->video_options & 4)
			bold = boldcols[attr] ? chr + 256 : chr;
		else
                        bold = boldcols[attr] ? chr : chr + 256;
		bold += 512 * (t3100e->video_options & 3);

                if (t3100e->cga.cgamode & 0x20)	/* Blink */
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
                       		((uint32_t *)buffer32->line[t3100e->displine])[(x << 3) + c] = cols[(fontdatm[bold][sc] & (1 << (c ^ 7))) ? 1 : 0] ^ (amber ^ black);
			}
		}
                else
                {
                	for (c = 0; c < 8; c++)
				((uint32_t *)buffer32->line[t3100e->displine])[(x << 3) + c] = cols[(fontdatm[bold][sc] & (1 << (c ^ 7))) ? 1 : 0];
                }
		++ma;
	}
}

/* Draw a row of text in 40-column mode */
void t3100e_text_row40(t3100e_t *t3100e)
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
	uint16_t ma = (t3100e->cga.crtc[13] | (t3100e->cga.crtc[12] << 8)) & 0x7fff;
	uint16_t ca = (t3100e->cga.crtc[15] | (t3100e->cga.crtc[14] << 8)) & 0x7fff;

	sc = (t3100e->displine) & 15;
	addr = ((ma & ~1) + (t3100e->displine >> 4) * 40) * 2;
	ma += (t3100e->displine >> 4) * 40;

	if ((t3100e->cga.crtc[10] & 0x60) == 0x20)
	{
		cursorline = 0;
	}
	else
	{
		cursorline = ((t3100e->cga.crtc[10] & 0x0F)*2 <= sc) &&
			     ((t3100e->cga.crtc[11] & 0x0F)*2 >= sc);
	}
	for (x = 0; x < 40; x++)
        {
		chr  = t3100e->vram[(addr + 2 * x) & 0x7FFF];
		attr = t3100e->vram[(addr + 2 * x + 1) & 0x7FFF];
                drawcursor = ((ma == ca) && cursorline &&
			(t3100e->cga.cgamode & 8) && (t3100e->cga.cgablink & 16));

		blink = ((t3100e->cga.cgablink & 16) && (t3100e->cga.cgamode & 0x20) &&
			(attr & 0x80) && !drawcursor);

		if (t3100e->video_options & 4)
			bold = boldcols[attr] ? chr + 256 : chr;
		else	bold = boldcols[attr] ? chr : chr + 256;
		bold += 512 * (t3100e->video_options & 3);

                if (t3100e->cga.cgamode & 0x20)	/* Blink */
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
                       		((uint32_t *)buffer32->line[t3100e->displine])[(x << 4) + c*2] = 
                       		((uint32_t *)buffer32->line[t3100e->displine])[(x << 4) + c*2 + 1] = cols[(fontdatm[bold][sc] & (1 << (c ^ 7))) ? 1 : 0] ^ (amber ^ black);
			}
		}
                else
                {
                	for (c = 0; c < 8; c++)
			{
				((uint32_t *)buffer32->line[t3100e->displine])[(x << 4) + c*2] = 
				((uint32_t *)buffer32->line[t3100e->displine])[(x << 4) + c*2+1] = cols[(fontdatm[bold][sc] & (1 << (c ^ 7))) ? 1 : 0];
			}
                }
		++ma;
	}
}




/* Draw a line in CGA 640x200 or T3100e 640x400 mode */
void t3100e_cgaline6(t3100e_t *t3100e)
{
	int x, c;
	uint8_t dat;
	uint32_t ink = 0;
	uint16_t addr;
	uint32_t fg = (t3100e->cga.cgacol & 0x0F) ? amber : black;
	uint32_t bg = black;

	uint16_t ma = (t3100e->cga.crtc[13] | (t3100e->cga.crtc[12] << 8)) & 0x7fff;

	if (t3100e->cga.crtc[9] == 3)	/* 640*400 */
	{
		addr = ((t3100e->displine) & 1) * 0x2000 +
		       ((t3100e->displine >> 1) & 1) * 0x4000 +
		       (t3100e->displine >> 2) * 80 +
		       ((ma & ~1) << 1);
	}
	else
	{
		addr = ((t3100e->displine >> 1) & 1) * 0x2000 +
		       (t3100e->displine >> 2) * 80 +
		       ((ma & ~1) << 1);
	}
	for (x = 0; x < 80; x++)
	{
		dat = t3100e->vram[addr & 0x7FFF];
		addr++;

		for (c = 0; c < 8; c++)
		{
			ink = (dat & 0x80) ? fg : bg;
			if (!(t3100e->cga.cgamode & 8)) ink = black;
			((uint32_t *)buffer32->line[t3100e->displine])[x*8+c] = ink;
			dat = dat << 1;
		}
	}
}


/* Draw a line in CGA 320x200 mode. Here the CGA colours are converted to
 * dither patterns: colour 1 to 25% grey, colour 2 to 50% grey */
void t3100e_cgaline4(t3100e_t *t3100e)
{
	int x, c;
	uint8_t dat, pattern;
	uint32_t ink0 = 0, ink1 = 0;
	uint16_t addr;

	uint16_t ma = (t3100e->cga.crtc[13] | (t3100e->cga.crtc[12] << 8)) & 0x7fff;
	if (t3100e->cga.crtc[9] == 3)	/* 320*400 undocumented */
	{
		addr = ((t3100e->displine) & 1) * 0x2000 +
		       ((t3100e->displine >> 1) & 1) * 0x4000 +
		       (t3100e->displine >> 2) * 80 +
		       ((ma & ~1) << 1);
	}
	else	/* 320*200 */
	{
		addr = ((t3100e->displine >> 1) & 1) * 0x2000 +
		       (t3100e->displine >> 2) * 80 +
		       ((ma & ~1) << 1);
	}
	for (x = 0; x < 80; x++)
	{
		dat = t3100e->vram[addr & 0x7FFF];
		addr++;

		for (c = 0; c < 4; c++)
		{
			pattern = (dat & 0xC0) >> 6;
			if (!(t3100e->cga.cgamode & 8)) pattern = 0;

			switch (pattern & 3)
			{
				case 0: ink0 = ink1 = black; break;
				case 1: if (t3100e->displine & 1) 
					{
						ink0 = black; ink1 = black;
					}
					else
					{
						ink0 = amber; ink1 = black;
					}
					break;
				case 2: if (t3100e->displine & 1) 
					{
						ink0 = black; ink1 = amber;
					}
					else
					{
						ink0 = amber; ink1 = black;
					}
					break;
				case 3: ink0 = ink1 = amber; break;

			}
			((uint32_t *)buffer32->line[t3100e->displine])[x*8+2*c] = ink0;
			((uint32_t *)buffer32->line[t3100e->displine])[x*8+2*c+1] = ink1;
			dat = dat << 2;
		}
	}
}






void t3100e_poll(void *p)
{
        t3100e_t *t3100e = (t3100e_t *)p;

	if (t3100e->video_options != st_video_options)
	{
		t3100e->video_options = st_video_options;

		if (t3100e->video_options & 8) /* Disable internal CGA */
			mem_mapping_disable(&t3100e->mapping);
		else	mem_mapping_enable(&t3100e->mapping);

		/* Set the font used for the external display */
		t3100e->cga.fontbase = (512 * (t3100e->video_options & 3))
				     + ((t3100e->video_options & 4) ? 256 : 0);

	}
	/* Switch between internal plasma and external CRT display. */
	if (st_display_internal != -1 && st_display_internal != t3100e->internal)
	{
		t3100e->internal = st_display_internal;
                t3100e_recalctimings(t3100e);
	}
	if (!t3100e->internal)
	{
		cga_poll(&t3100e->cga);
		return;
	}


        if (!t3100e->linepos)
        {
                timer_advance_u64(&t3100e->cga.timer, t3100e->dispofftime);
                t3100e->cga.cgastat |= 1;
                t3100e->linepos = 1;
                if (t3100e->dispon)
                {
                        if (t3100e->displine == 0)
                        {
                                video_wait_for_buffer();
                        }

			/* Graphics */
			if (t3100e->cga.cgamode & 0x02)	
			{
				if (t3100e->cga.cgamode & 0x10)
					t3100e_cgaline6(t3100e);
				else	t3100e_cgaline4(t3100e);
			}
			else	
			if (t3100e->cga.cgamode & 0x01) /* High-res text */
			{
				t3100e_text_row80(t3100e); 
			}
			else
			{
				t3100e_text_row40(t3100e); 
			}
                }
                t3100e->displine++;
		/* Hardcode a fixed refresh rate and VSYNC timing */
                if (t3100e->displine == 400) /* Start of VSYNC */
                {
                        t3100e->cga.cgastat |= 8;
			t3100e->dispon = 0;
                }
		if (t3100e->displine == 416) /* End of VSYNC */
		{
                        t3100e->displine = 0;
                        t3100e->cga.cgastat &= ~8;
			t3100e->dispon = 1;
		}
        }
        else
        {
		if (t3100e->dispon)
		{
                	t3100e->cga.cgastat &= ~1;
		}
                timer_advance_u64(&t3100e->cga.timer, t3100e->dispontime);
                t3100e->linepos = 0;

		if (t3100e->displine == 400)
                {
/* Hardcode 640x400 window size */
			if ((T3100E_XSIZE != xsize) || (T3100E_YSIZE != ysize) || video_force_resize_get())
			{
                                xsize = T3100E_XSIZE;
                                ysize = T3100E_YSIZE;
                                if (xsize < 64) xsize = 656;
                                if (ysize < 32) ysize = 200;
                                set_screen_size(xsize, ysize);

				if (video_force_resize_get())
					video_force_resize_set(0);
                        }
                        video_blit_memtoscreen(0, 0, xsize, ysize);

                        frames++;
			/* Fixed 640x400 resolution */
			video_res_x = T3100E_XSIZE;
			video_res_y = T3100E_YSIZE;

			if (t3100e->cga.cgamode & 0x02)	
			{
				if (t3100e->cga.cgamode & 0x10)
					video_bpp = 1;
				else	video_bpp = 2;

			}
			else	 video_bpp = 0;
                	t3100e->cga.cgablink++;
                }
        }
}



void t3100e_recalcattrs(t3100e_t *t3100e)
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
	amber = makecol(0xf7, 0x7C, 0x34);
	black = makecol(0x17, 0x0C, 0x00);

	/* Initialise the attribute mapping. Start by defaulting everything
	 * to black on amber, and with bold set by bit 3 */
	for (n = 0; n < 256; n++)
	{
		boldcols[n] = (n & 8) != 0;
		blinkcols[n][0] = normcols[n][0] = amber; 
		blinkcols[n][1] = normcols[n][1] = black;
	}

	/* Colours 0x11-0xFF are controlled by bits 2 and 3 of the 
	 * passed value. Exclude x0 and x8, which are always black on 
	 * amber. */
	for (n = 0x11; n <= 0xFF; n++)
	{
		if ((n & 7) == 0) continue;
		if (t3100e->attrmap & 4)	/* Inverse */
		{
			blinkcols[n][0] = normcols[n][0] = amber;
			blinkcols[n][1] = normcols[n][1] = black;
		}
		else				/* Normal */
		{
			blinkcols[n][0] = normcols[n][0] = black;
			blinkcols[n][1] = normcols[n][1] = amber;
		}
		if (t3100e->attrmap & 8) boldcols[n] = 1;	/* Bold */
	}
	/* Set up the 01-0E range, controlled by bits 0 and 1 of the 
	 * passed value. When blinking is enabled this also affects 81-8E. */
	for (n = 0x01; n <= 0x0E; n++)
	{
		if (n == 7) continue;
		if (t3100e->attrmap & 1)
		{
			blinkcols[n][0] = normcols[n][0] = amber;
			blinkcols[n][1] = normcols[n][1] = black;
			blinkcols[n+128][0] = amber;
			blinkcols[n+128][1] = black;
		}
		else
		{
			blinkcols[n][0] = normcols[n][0] = black;
			blinkcols[n][1] = normcols[n][1] = amber;
			blinkcols[n+128][0] = black;
			blinkcols[n+128][1] = amber;
		}
		if (t3100e->attrmap & 2) boldcols[n] = 1;
	}
	/* Colours 07 and 0F are always amber on black. If blinking is 
	 * enabled so are 87 and 8F. */
	for (n = 0x07; n <= 0x0F; n += 8)
	{
		blinkcols[n][0] = normcols[n][0] = black;
		blinkcols[n][1] = normcols[n][1] = amber;
		blinkcols[n+128][0] = black;
		blinkcols[n+128][1] = amber;
	}
	/* When not blinking, colours 81-8F are always amber on black. */
	for (n = 0x81; n <= 0x8F; n ++)
	{
		normcols[n][0] = black;
		normcols[n][1] = amber;
		boldcols[n] = (n & 0x08) != 0;
	}


	/* Finally do the ones which are solid black. These differ between
	 * the normal and blinking mappings */
	for (n = 0; n <= 0xFF; n += 0x11)
	{
		normcols[n][0] = normcols[n][1] = black;
	}
	/* In the blinking range, 00 11 22 .. 77 and 80 91 A2 .. F7 are black */
	for (n = 0; n <= 0x77; n += 0x11)
	{
		blinkcols[n][0] = blinkcols[n][1] = black;
		blinkcols[n+128][0] = blinkcols[n+128][1] = black;
	}
}


void *t3100e_init(const device_t *info)
{
        t3100e_t *t3100e = malloc(sizeof(t3100e_t));
        memset(t3100e, 0, sizeof(t3100e_t));
	loadfont("roms/machines/t3100e/t3100e_font.bin", 5);
	cga_init(&t3100e->cga);
	video_inform(VIDEO_FLAG_TYPE_CGA, &timing_t3100e);

	t3100e->internal = 1;

	/* 32k video RAM */
        t3100e->vram = malloc(0x8000);

        timer_set_callback(&t3100e->cga.timer, t3100e_poll);
        timer_set_p(&t3100e->cga.timer, t3100e);

	/* Occupy memory between 0xB8000 and 0xBFFFF */
        mem_mapping_add(&t3100e->mapping, 0xb8000, 0x8000, t3100e_read, NULL, NULL, t3100e_write, NULL, NULL,  NULL, 0, t3100e);
	/* Respond to CGA I/O ports */
        io_sethandler(0x03d0, 0x000c, t3100e_in, NULL, NULL, t3100e_out, NULL, NULL, t3100e);

	/* Default attribute mapping is 4 */
	t3100e->attrmap = 4;
	t3100e_recalcattrs(t3100e);

/* Start off in 80x25 text mode */
        t3100e->cga.cgastat   = 0xF4;
	t3100e->cga.vram = t3100e->vram;
	t3100e->enabled    = 1;
	t3100e->video_options = 0xFF;
        return t3100e;
}

void t3100e_close(void *p)
{
        t3100e_t *t3100e = (t3100e_t *)p;

        free(t3100e->vram);
        free(t3100e);
}

void t3100e_speed_changed(void *p)
{
        t3100e_t *t3100e = (t3100e_t *)p;
        
        t3100e_recalctimings(t3100e);
}

const device_t t3100e_device =
{
        "Toshiba T3100e",
        0,
        0,
        t3100e_init,
        t3100e_close,
        NULL,
        { NULL },
        t3100e_speed_changed,
        NULL
};
