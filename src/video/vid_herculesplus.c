/*
 * 86Box	A hypervisor and IBM PC system emulator that specializes in
 *		running old operating systems and software designed for IBM
 *		PC systems and compatibles from 1981 through fairly recent
 *		system designs based on the PCI bus.
 *
 *		This file is part of the 86Box distribution.
 *
 *		Hercules InColor emulation.
 *
 * Version:	@(#)vid_herculesplus.c	1.0.1	2017/10/16
 *
 * Authors:	Sarah Walker, <http://pcem-emulator.co.uk/>
 *		Miran Grca, <mgrca8@gmail.com>
 *
 *		Copyright 2008-2017 Sarah Walker.
 *		Copyright 2016,2017 Miran Grca.
 */
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <stdlib.h>
#include <wchar.h>
#include "../86box.h"
#include "../ibm.h"
#include "../io.h"
#include "../mem.h"
#include "../rom.h"
#include "../timer.h"
#include "../device.h"
#include "video.h"
#include "vid_herculesplus.h"


/* extended CRTC registers */

#define HERCULESPLUS_CRTC_XMODE   20 /* xMode register */
#define HERCULESPLUS_CRTC_UNDER   21	/* Underline */
#define HERCULESPLUS_CRTC_OVER    22 /* Overstrike */

/* character width */
#define HERCULESPLUS_CW    ((herculesplus->crtc[HERCULESPLUS_CRTC_XMODE] & HERCULESPLUS_XMODE_90COL) ? 8 : 9)

/* mode control register */
#define HERCULESPLUS_CTRL_GRAPH   0x02
#define HERCULESPLUS_CTRL_ENABLE  0x08
#define HERCULESPLUS_CTRL_BLINK   0x20
#define HERCULESPLUS_CTRL_PAGE1   0x80

/* CRTC status register */
#define HERCULESPLUS_STATUS_HSYNC 0x01		/* horizontal sync */
#define HERCULESPLUS_STATUS_LIGHT 0x02
#define HERCULESPLUS_STATUS_VIDEO 0x08
#define HERCULESPLUS_STATUS_ID    0x10		/* Card identification */
#define HERCULESPLUS_STATUS_VSYNC 0x80		/* -vertical sync */

/* configuration switch register */
#define HERCULESPLUS_CTRL2_GRAPH 0x01
#define HERCULESPLUS_CTRL2_PAGE1 0x02

/* extended mode register */
#define HERCULESPLUS_XMODE_RAMFONT 0x01
#define HERCULESPLUS_XMODE_90COL   0x02

typedef struct herculesplus_t
{
        mem_mapping_t mapping;
        
        uint8_t crtc[32];
        int crtcreg;

        uint8_t ctrl, ctrl2, stat;

        int64_t dispontime, dispofftime;
        int64_t vidtime;
        
        int firstline, lastline;

        int linepos, displine;
        int vc, sc;
        uint16_t ma, maback;
        int con, coff, cursoron;
        int dispon, blink;
        int64_t vsynctime;
	int vadj;

        uint8_t *vram;
} herculesplus_t;

void herculesplus_recalctimings(herculesplus_t *herculesplus);
void herculesplus_write(uint32_t addr, uint8_t val, void *p);
uint8_t herculesplus_read(uint32_t addr, void *p);

static int mdacols[256][2][2];

void herculesplus_out(uint16_t addr, uint8_t val, void *p)
{
        herculesplus_t *herculesplus = (herculesplus_t *)p;
/*        pclog("InColor out %04X %02X\n",addr,val); */
        switch (addr)
        {
                case 0x3b0: case 0x3b2: case 0x3b4: case 0x3b6:
                herculesplus->crtcreg = val & 31;
                return;
                case 0x3b1: case 0x3b3: case 0x3b5: case 0x3b7:
		if (herculesplus->crtcreg > 22) return;
                herculesplus->crtc[herculesplus->crtcreg] = val;
                if (herculesplus->crtc[10] == 6 && herculesplus->crtc[11] == 7) /*Fix for Generic Turbo XT BIOS, which sets up cursor registers wrong*/
                {
                        herculesplus->crtc[10] = 0xb;
                        herculesplus->crtc[11] = 0xc;
                }
                herculesplus_recalctimings(herculesplus);
                return;
                case 0x3b8:
                herculesplus->ctrl = val;
                return;
                case 0x3bf:
                herculesplus->ctrl2 = val;
                if (val & 2)
                        mem_mapping_set_addr(&herculesplus->mapping, 0xb0000, 0x10000);
                else
                        mem_mapping_set_addr(&herculesplus->mapping, 0xb0000, 0x08000);
                return;
        }
}

uint8_t herculesplus_in(uint16_t addr, void *p)
{
        herculesplus_t *herculesplus = (herculesplus_t *)p;
/*        pclog("InColor in %04X %02X %04X:%04X %04X\n",addr,(herculesplus->stat & 0xF) | ((herculesplus->stat & 8) << 4),CS,pc,CX); */
        switch (addr)
        {
                case 0x3b0: case 0x3b2: case 0x3b4: case 0x3b6:
                return herculesplus->crtcreg;
                case 0x3b1: case 0x3b3: case 0x3b5: case 0x3b7:
		if (herculesplus->crtcreg > 22) return 0xff;
                return herculesplus->crtc[herculesplus->crtcreg];
                case 0x3ba:
		/* 0x50: InColor card identity */
                return (herculesplus->stat & 0xf) | ((herculesplus->stat & 8) << 4) | 0x10;
        }
        return 0xff;
}

void herculesplus_write(uint32_t addr, uint8_t val, void *p)
{
        herculesplus_t *herculesplus = (herculesplus_t *)p;

        egawrites++;

	addr &= 0xFFFF;

	herculesplus->vram[addr] = val;
}

uint8_t herculesplus_read(uint32_t addr, void *p)
{
        herculesplus_t *herculesplus = (herculesplus_t *)p;

        egareads++;

	addr &= 0xFFFF;
    return herculesplus->vram[addr];
}



void herculesplus_recalctimings(herculesplus_t *herculesplus)
{
        double disptime;
	double _dispontime, _dispofftime;
        disptime = herculesplus->crtc[0] + 1;
        _dispontime  = herculesplus->crtc[1];
        _dispofftime = disptime - _dispontime;
        _dispontime  *= MDACONST;
        _dispofftime *= MDACONST;
	herculesplus->dispontime  = (int64_t)(_dispontime  * (1 << TIMER_SHIFT));
	herculesplus->dispofftime = (int64_t)(_dispofftime * (1 << TIMER_SHIFT));
}


static void herculesplus_draw_char_rom(herculesplus_t *herculesplus, int x, uint8_t chr, uint8_t attr)
{
	unsigned            i;
	int                 elg, blk;
	unsigned            ull;
	unsigned            val;
	unsigned	    ifg, ibg;
	const unsigned char *fnt;
	int		    cw = HERCULESPLUS_CW;

	blk = 0;
	if (herculesplus->ctrl & HERCULESPLUS_CTRL_BLINK) 
	{
		if (attr & 0x80) 
		{
			blk = (herculesplus->blink & 16);
		}
		attr &= 0x7f;
	}

	/* MDA-compatible attributes */
	ibg = 0;
	ifg = 7;
	if ((attr & 0x77) == 0x70)	 /* Invert */
	{
		ifg = 0;
		ibg = 7;
	}	
	if (attr & 8) 
	{
		ifg |= 8;	/* High intensity FG */
	}
	if (attr & 0x80) 
	{
		ibg |= 8;	/* High intensity BG */
	}
	if ((attr & 0x77) == 0)	/* Blank */
	{
		ifg = ibg;
	}
	ull = ((attr & 0x07) == 1) ? 13 : 0xffff;

	if (herculesplus->crtc[HERCULESPLUS_CRTC_XMODE] & HERCULESPLUS_XMODE_90COL) 
	{
		elg = 0;
	} 
	else 
	{
		elg = ((chr >= 0xc0) && (chr <= 0xdf));
	}

	fnt = &(fontdatm[chr][herculesplus->sc]);

	if (blk)
	{
		val = 0x000;	/* Blinking, draw all background */
	}
	else if (herculesplus->sc == ull) 
	{
		val = 0x1ff;	/* Underscore, draw all foreground */
	}
	else 
	{
		val = fnt[0] << 1;
	
		if (elg) 
		{
			val |= (val >> 1) & 1;
		}
	}
	for (i = 0; i < cw; i++) 
	{
		buffer->line[herculesplus->displine][x * cw + i] = (val & 0x100) ? ifg : ibg;
		val = val << 1;
	}
}


static void herculesplus_draw_char_ram4(herculesplus_t *herculesplus, int x, uint8_t chr, uint8_t attr)
{
	unsigned            i;
	int                 elg, blk;
	unsigned            ull;
	unsigned            val;
	unsigned	    ifg, ibg, cfg;
	const unsigned char *fnt;
	int		    cw = HERCULESPLUS_CW;
	int                 blink   = herculesplus->ctrl & HERCULESPLUS_CTRL_BLINK;

	blk = 0;
	if (blink)
	{
		if (attr & 0x80) 
		{
			blk = (herculesplus->blink & 16);
		}
		attr &= 0x7f;
	}

	/* MDA-compatible attributes */
	ibg = 0;
	ifg = 7;
	if ((attr & 0x77) == 0x70)	 /* Invert */
	{
		ifg = 0;
		ibg = 7;
	}	
	if (attr & 8) 
	{
		ifg |= 8;	/* High intensity FG */
	}
	if (attr & 0x80) 
	{
		ibg |= 8;	/* High intensity BG */
	}
	if ((attr & 0x77) == 0)	/* Blank */
	{
		ifg = ibg;
	}
	ull = ((attr & 0x07) == 1) ? 13 : 0xffff;
	if (herculesplus->crtc[HERCULESPLUS_CRTC_XMODE] & HERCULESPLUS_XMODE_90COL) 
	{
		elg = 0;
	} 
	else 
	{
		elg = ((chr >= 0xc0) && (chr <= 0xdf));
	}
	fnt = herculesplus->vram + 0x4000 + 16 * chr + herculesplus->sc;

	if (blk)
	{
		/* Blinking, draw all background */
		val = 0x000;	
	}
	else if (herculesplus->sc == ull) 
	{
		/* Underscore, draw all foreground */
		val = 0x1ff;
	}
	else 
	{
		val = fnt[0x00000] << 1;
	
		if (elg) 
		{
			val |= (val >> 1) & 1;
		}
	}
	for (i = 0; i < cw; i++) 
	{
		/* Generate pixel colour */
		cfg = 0;
		/* cfg = colour of foreground pixels */
		if ((attr & 0x77) == 0) cfg = ibg; /* 'blank' attribute */
		
		buffer->line[herculesplus->displine][x * cw + i] = mdacols[attr][blink][cfg];
		val = val << 1;
	}
}


static void herculesplus_draw_char_ram48(herculesplus_t *herculesplus, int x, uint8_t chr, uint8_t attr)
{
	unsigned            i;
	int                 elg, blk, ul, ol, bld;
	unsigned            ull, oll, ulc = 0, olc = 0;
	unsigned            val;
	unsigned	    ibg, cfg;
	const unsigned char *fnt;
	int		    cw = HERCULESPLUS_CW;
	int                 blink   = herculesplus->ctrl & HERCULESPLUS_CTRL_BLINK;
	int		    font = (attr & 0x0F);

	if (font >= 12) font &= 7;

	blk = 0;
	if (blink)
	{
		if (attr & 0x40) 
		{
			blk = (herculesplus->blink & 16);
		}
		attr &= 0x7f;
	}
	/* MDA-compatible attributes */
	if (blink) 
	{
		ibg = (attr & 0x80) ? 8 : 0;
		bld = 0;
		ol  = (attr & 0x20) ? 1 : 0;
		ul  = (attr & 0x10) ? 1 : 0;
	} 
	else 
	{
		bld = (attr & 0x80) ? 1 : 0;
		ibg = (attr & 0x40) ? 0x0F : 0;
		ol  = (attr & 0x20) ? 1 : 0;
		ul  = (attr & 0x10) ? 1 : 0;
	}
	if (ul) 
	{ 
		ull = herculesplus->crtc[HERCULESPLUS_CRTC_UNDER] & 0x0F;
		ulc = (herculesplus->crtc[HERCULESPLUS_CRTC_UNDER] >> 4) & 0x0F;
		if (ulc == 0) ulc = 7;
	} 
	else 
	{
		ull = 0xFFFF;
	}
	if (ol) 
	{ 
		oll = herculesplus->crtc[HERCULESPLUS_CRTC_OVER] & 0x0F;
		olc = (herculesplus->crtc[HERCULESPLUS_CRTC_OVER] >> 4) & 0x0F;
		if (olc == 0) olc = 7;
	} 
	else 
	{
		oll = 0xFFFF;
	}

	if (herculesplus->crtc[HERCULESPLUS_CRTC_XMODE] & HERCULESPLUS_XMODE_90COL) 
	{
		elg = 0;
	} 
	else 
	{
		elg = ((chr >= 0xc0) && (chr <= 0xdf));
	}
	fnt = herculesplus->vram + 0x4000 + 16 * chr + 4096 * font + herculesplus->sc;

	if (blk)
	{
		/* Blinking, draw all background */
		val = 0x000;	
	}
	else if (herculesplus->sc == ull) 
	{
		/* Underscore, draw all foreground */
		val = 0x1ff;
	}
	else 
	{
		val = fnt[0x00000] << 1;
	
		if (elg) 
		{
			val |= (val >> 1) & 1;
		}
		if (bld) 
		{
			val |= (val >> 1);
		}
	}
	for (i = 0; i < cw; i++) 
	{
		/* Generate pixel colour */
		cfg = val & 0x100;
		if (herculesplus->sc == oll)
		{
			cfg = olc ^ ibg;	/* Strikethrough */
		}
		else if (herculesplus->sc == ull)
		{
			cfg = ulc ^ ibg;	/* Underline */
		}
		else
		{
            		cfg |= ibg;
		}
		
		buffer->line[herculesplus->displine][(x * cw) + i] = mdacols[attr][blink][cfg];
		val = val << 1;
	}
}

static void herculesplus_text_line(herculesplus_t *herculesplus, uint16_t ca)
{
        int drawcursor;
	int x, c;
        uint8_t chr, attr;
	uint32_t col;

	for (x = 0; x < herculesplus->crtc[1]; x++)
	{
		chr  = herculesplus->vram[(herculesplus->ma << 1) & 0xfff];
                attr = herculesplus->vram[((herculesplus->ma << 1) + 1) & 0xfff];

                drawcursor = ((herculesplus->ma == ca) && herculesplus->con && herculesplus->cursoron);

		switch (herculesplus->crtc[HERCULESPLUS_CRTC_XMODE] & 5)
		{
			case 0:
			case 4:	/* ROM font */
				herculesplus_draw_char_rom(herculesplus, x, chr, attr);
				break;
			case 1: /* 4k RAMfont */
				herculesplus_draw_char_ram4(herculesplus, x, chr, attr);
				break;
			case 5: /* 48k RAMfont */
				herculesplus_draw_char_ram48(herculesplus, x, chr, attr);
				break;

		}
		++herculesplus->ma;
                if (drawcursor)
                {
			int cw = HERCULESPLUS_CW;

			col = mdacols[attr][0][1];
			for (c = 0; c < cw; c++)
			{
				((uint32_t *)buffer32->line[herculesplus->displine])[x * cw + c] = col;
			}
		}
	}
}


static void herculesplus_graphics_line(herculesplus_t *herculesplus)
{
	uint16_t ca;
	int x, c, plane = 0;
	uint16_t val;

	/* Graphics mode. */
        ca = (herculesplus->sc & 3) * 0x2000;
        if ((herculesplus->ctrl & HERCULESPLUS_CTRL_PAGE1) && (herculesplus->ctrl2 & HERCULESPLUS_CTRL2_PAGE1))
		ca += 0x8000;

	for (x = 0; x < herculesplus->crtc[1]; x++)
	{
		val = (herculesplus->vram[((herculesplus->ma << 1) & 0x1fff) + ca + 0x10000 * plane] << 8)
		| herculesplus->vram[((herculesplus->ma << 1) & 0x1fff) + ca + 0x10000 * plane + 1];

		herculesplus->ma++;
		for (c = 0; c < 16; c++)
		{
			val >>= 1;

			((uint32_t *)buffer32->line[herculesplus->displine])[(x << 4) + c] = (val & 1) ? 7 : 0;
		}
	}
}

void herculesplus_poll(void *p)
{
        herculesplus_t *herculesplus = (herculesplus_t *)p;
        uint16_t ca = (herculesplus->crtc[15] | (herculesplus->crtc[14] << 8)) & 0x3fff;
        int x;
        int oldvc;
        int oldsc;

        if (!herculesplus->linepos)
        {
                herculesplus->vidtime += herculesplus->dispofftime;
                herculesplus->stat |= 1;
                herculesplus->linepos = 1;
                oldsc = herculesplus->sc;
                if ((herculesplus->crtc[8] & 3) == 3) 
                        herculesplus->sc = (herculesplus->sc << 1) & 7;
                if (herculesplus->dispon)
                {
                        if (herculesplus->displine < herculesplus->firstline)
                        {
                                herculesplus->firstline = herculesplus->displine;
                                video_wait_for_buffer();
                        }
                        herculesplus->lastline = herculesplus->displine;
                        if ((herculesplus->ctrl & HERCULESPLUS_CTRL_GRAPH) && (herculesplus->ctrl2 & HERCULESPLUS_CTRL2_GRAPH))
                        {
				herculesplus_graphics_line(herculesplus);
                        }
                        else
                        {
				herculesplus_text_line(herculesplus, ca);
                        }
                }
                herculesplus->sc = oldsc;
                if (herculesplus->vc == herculesplus->crtc[7] && !herculesplus->sc)
                {
                        herculesplus->stat |= 8;
                }
                herculesplus->displine++;
                if (herculesplus->displine >= 500) 
                        herculesplus->displine = 0;
        }
        else
        {
                herculesplus->vidtime += herculesplus->dispontime;
                if (herculesplus->dispon) 
                        herculesplus->stat &= ~1;
                herculesplus->linepos = 0;
                if (herculesplus->vsynctime)
                {
                        herculesplus->vsynctime--;
                        if (!herculesplus->vsynctime)
                        {
                                herculesplus->stat &= ~8;
                        }
                }
                if (herculesplus->sc == (herculesplus->crtc[11] & 31) || ((herculesplus->crtc[8] & 3) == 3 && herculesplus->sc == ((herculesplus->crtc[11] & 31) >> 1))) 
                { 
                        herculesplus->con = 0; 
                        herculesplus->coff = 1; 
                }
                if (herculesplus->vadj)
                {
                        herculesplus->sc++;
                        herculesplus->sc &= 31;
                        herculesplus->ma = herculesplus->maback;
                        herculesplus->vadj--;
                        if (!herculesplus->vadj)
                        {
                                herculesplus->dispon = 1;
                                herculesplus->ma = herculesplus->maback = (herculesplus->crtc[13] | (herculesplus->crtc[12] << 8)) & 0x3fff;
                                herculesplus->sc = 0;
                        }
                }
                else if (herculesplus->sc == herculesplus->crtc[9] || ((herculesplus->crtc[8] & 3) == 3 && herculesplus->sc == (herculesplus->crtc[9] >> 1)))
                {
                        herculesplus->maback = herculesplus->ma;
                        herculesplus->sc = 0;
                        oldvc = herculesplus->vc;
                        herculesplus->vc++;
                        herculesplus->vc &= 127;
                        if (herculesplus->vc == herculesplus->crtc[6]) 
                                herculesplus->dispon = 0;
                        if (oldvc == herculesplus->crtc[4])
                        {
                                herculesplus->vc = 0;
                                herculesplus->vadj = herculesplus->crtc[5];
                                if (!herculesplus->vadj) herculesplus->dispon=1;
                                if (!herculesplus->vadj) herculesplus->ma = herculesplus->maback = (herculesplus->crtc[13] | (herculesplus->crtc[12] << 8)) & 0x3fff;
                                if ((herculesplus->crtc[10] & 0x60) == 0x20) herculesplus->cursoron = 0;
                                else                                     herculesplus->cursoron = herculesplus->blink & 16;
                        }
                        if (herculesplus->vc == herculesplus->crtc[7])
                        {
                                herculesplus->dispon = 0;
                                herculesplus->displine = 0;
                                herculesplus->vsynctime = 16;
                                if (herculesplus->crtc[7])
                                {
                                        if ((herculesplus->ctrl & HERCULESPLUS_CTRL_GRAPH) && (herculesplus->ctrl2 & HERCULESPLUS_CTRL2_GRAPH)) 
					{
						x = herculesplus->crtc[1] << 4;
					}
                                        else
					{
                                               x = herculesplus->crtc[1] * 9;
					}
                                        herculesplus->lastline++;
                                        if (x != xsize || (herculesplus->lastline - herculesplus->firstline) != ysize)
                                        {
                                                xsize = x;
                                                ysize = herculesplus->lastline - herculesplus->firstline;
                                                if (xsize < 64) xsize = 656;
                                                if (ysize < 32) ysize = 200;
                                                updatewindowsize(xsize, ysize);
                                        }
					video_blit_memtoscreen(0, herculesplus->firstline, 0, herculesplus->lastline - herculesplus->firstline, xsize, herculesplus->lastline - herculesplus->firstline);
                                        frames++;
                                        if ((herculesplus->ctrl & HERCULESPLUS_CTRL_GRAPH) && (herculesplus->ctrl2 & HERCULESPLUS_CTRL2_GRAPH))
                                        {
                                                video_res_x = herculesplus->crtc[1] * 16;
                                                video_res_y = herculesplus->crtc[6] * 4;
                                                video_bpp = 1;
                                        }
                                        else
                                        {
                                                video_res_x = herculesplus->crtc[1];
                                                video_res_y = herculesplus->crtc[6];
                                                video_bpp = 0;
                                        }
                                }
                                herculesplus->firstline = 1000;
                                herculesplus->lastline = 0;
                                herculesplus->blink++;
                        }
                }
                else
                {
                        herculesplus->sc++;
                        herculesplus->sc &= 31;
                        herculesplus->ma = herculesplus->maback;
                }
                if ((herculesplus->sc == (herculesplus->crtc[10] & 31) || ((herculesplus->crtc[8] & 3) == 3 && herculesplus->sc == ((herculesplus->crtc[10] & 31) >> 1))))
                {
                        herculesplus->con = 1;
                }
        }
}

void *herculesplus_init(device_t *info)
{
        int c;
        herculesplus_t *herculesplus = malloc(sizeof(herculesplus_t));
        memset(herculesplus, 0, sizeof(herculesplus_t));

        herculesplus->vram = malloc(0x10000);	/* 64k VRAM */

        timer_add(herculesplus_poll, &herculesplus->vidtime, TIMER_ALWAYS_ENABLED, herculesplus);
        mem_mapping_add(&herculesplus->mapping, 0xb0000, 0x10000, herculesplus_read, NULL, NULL, herculesplus_write, NULL, NULL,  NULL, MEM_MAPPING_EXTERNAL, herculesplus);
        io_sethandler(0x03b0, 0x0010, herculesplus_in, NULL, NULL, herculesplus_out, NULL, NULL, herculesplus);

	for (c = 0; c < 256; c++)
        {
                mdacols[c][0][0] = mdacols[c][1][0] = mdacols[c][1][1] = 16;
                if (c & 8) mdacols[c][0][1] = 15 + 16;
                else       mdacols[c][0][1] =  7 + 16;
        }
        mdacols[0x70][0][1] = 16;
        mdacols[0x70][0][0] = mdacols[0x70][1][0] = mdacols[0x70][1][1] = 16 + 15;
        mdacols[0xF0][0][1] = 16;
        mdacols[0xF0][0][0] = mdacols[0xF0][1][0] = mdacols[0xF0][1][1] = 16 + 15;
        mdacols[0x78][0][1] = 16 + 7;
        mdacols[0x78][0][0] = mdacols[0x78][1][0] = mdacols[0x78][1][1] = 16 + 15;
        mdacols[0xF8][0][1] = 16 + 7;
        mdacols[0xF8][0][0] = mdacols[0xF8][1][0] = mdacols[0xF8][1][1] = 16 + 15;
        mdacols[0x00][0][1] = mdacols[0x00][1][1] = 16;
        mdacols[0x08][0][1] = mdacols[0x08][1][1] = 16;
        mdacols[0x80][0][1] = mdacols[0x80][1][1] = 16;
        mdacols[0x88][0][1] = mdacols[0x88][1][1] = 16;

        return herculesplus;
}

void herculesplus_close(void *p)
{
        herculesplus_t *herculesplus = (herculesplus_t *)p;

        free(herculesplus->vram);
        free(herculesplus);
}

void herculesplus_speed_changed(void *p)
{
        herculesplus_t *herculesplus = (herculesplus_t *)p;
        
        herculesplus_recalctimings(herculesplus);
}

device_t herculesplus_device =
{
        "Hercules Plus",
        DEVICE_ISA, 0,
        herculesplus_init,
        herculesplus_close,
	NULL,
        NULL,
        herculesplus_speed_changed,
        NULL,
	NULL,
        NULL
};
