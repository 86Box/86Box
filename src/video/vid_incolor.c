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
 * Version:	@(#)vid_incolor.c	1.0.1	2017/10/10
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
#include "../ibm.h"
#include "../io.h"
#include "../mem.h"
#include "../rom.h"
#include "../timer.h"
#include "../device.h"
#include "video.h"
#include "vid_incolor.h"


/* extended CRTC registers */
#define INCOLOR_CRTC_XMODE   20 /* xMode register */
#define INCOLOR_CRTC_UNDER   21	/* Underline */
#define INCOLOR_CRTC_OVER    22 /* Overstrike */
#define INCOLOR_CRTC_EXCEPT  23 /* Exception */
#define INCOLOR_CRTC_MASK    24 /* Plane display mask & write mask */
#define INCOLOR_CRTC_RWCTRL  25 /* Read/write control */
#define INCOLOR_CRTC_RWCOL   26 /* Read/write colour */
#define INCOLOR_CRTC_PROTECT 27 /* Latch protect */
#define INCOLOR_CRTC_PALETTE 28 /* Palette */

/* character width */
#define INCOLOR_CW    ((incolor->crtc[INCOLOR_CRTC_XMODE] & INCOLOR_XMODE_90COL) ? 8 : 9)

/* mode control register */
#define INCOLOR_CTRL_GRAPH   0x02
#define INCOLOR_CTRL_ENABLE  0x08
#define INCOLOR_CTRL_BLINK   0x20
#define INCOLOR_CTRL_PAGE1   0x80

/* CRTC status register */
#define INCOLOR_STATUS_HSYNC 0x01		/* horizontal sync */
#define INCOLOR_STATUS_LIGHT 0x02
#define INCOLOR_STATUS_VIDEO 0x08
#define INCOLOR_STATUS_ID    0x50		/* Card identification */
#define INCOLOR_STATUS_VSYNC 0x80		/* -vertical sync */

/* configuration switch register */
#define INCOLOR_CTRL2_GRAPH 0x01
#define INCOLOR_CTRL2_PAGE1 0x02

/* extended mode register */
#define INCOLOR_XMODE_RAMFONT 0x01
#define INCOLOR_XMODE_90COL   0x02


/* Read/write control */
#define INCOLOR_RWCTRL_WRMODE   0x30
#define INCOLOR_RWCTRL_POLARITY 0x40

/* exception register */
#define INCOLOR_EXCEPT_CURSOR  0x0F		/* Cursor colour */
#define INCOLOR_EXCEPT_PALETTE 0x10		/* Enable palette register */
#define INCOLOR_EXCEPT_ALTATTR 0x20		/* Use alternate attributes */



/* Default palette */
static unsigned char defpal[16] = 
{
	0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07,
	0x38, 0x39, 0x3A, 0x3B, 0x3C, 0x3D, 0x3E, 0x3F
};

static uint32_t incolor_rgb[64];

/* Mapping of inks to RGB */
static unsigned char init_rgb[64][3] =
{
				/* rgbRGB */
        { 0x00, 0x00, 0x00 },	/* 000000 */
        { 0x00, 0x00, 0xaa },	/* 000001 */
        { 0x00, 0xaa, 0x00 },	/* 000010 */
        { 0x00, 0xaa, 0xaa },	/* 000011 */
        { 0xaa, 0x00, 0x00 },	/* 000100 */
        { 0xaa, 0x00, 0xaa },	/* 000101 */
        { 0xaa, 0xaa, 0x00 },	/* 000110 */
        { 0xaa, 0xaa, 0xaa },	/* 000111 */
        { 0x00, 0x00, 0x55 },	/* 001000 */
        { 0x00, 0x00, 0xff },	/* 001001 */
        { 0x00, 0xaa, 0x55 },	/* 001010 */
        { 0x00, 0xaa, 0xff },	/* 001011 */
        { 0xaa, 0x00, 0x55 },	/* 001100 */
        { 0xaa, 0x00, 0xff },	/* 001101 */
        { 0xaa, 0xaa, 0x55 },	/* 001110 */
        { 0xaa, 0xaa, 0xff },	/* 001111 */
        { 0x00, 0x55, 0x00 },	/* 010000 */
        { 0x00, 0x55, 0xaa },	/* 010001 */
        { 0x00, 0xff, 0x00 },	/* 010010 */
        { 0x00, 0xff, 0xaa },	/* 010011 */
        { 0xaa, 0x55, 0x00 },	/* 010100 */
        { 0xaa, 0x55, 0xaa },	/* 010101 */
        { 0xaa, 0xff, 0x00 },	/* 010110 */
        { 0xaa, 0xff, 0xaa },	/* 010111 */
        { 0x00, 0x55, 0x55 },	/* 011000 */
        { 0x00, 0x55, 0xff },	/* 011001 */
        { 0x00, 0xff, 0x55 },	/* 011010 */
        { 0x00, 0xff, 0xff },	/* 011011 */
        { 0xaa, 0x55, 0x55 },	/* 011100 */
        { 0xaa, 0x55, 0xff },	/* 011101 */
        { 0xaa, 0xff, 0x55 },	/* 011110 */
        { 0xaa, 0xff, 0xff },	/* 011111 */
        { 0x55, 0x00, 0x00 },	/* 100000 */
        { 0x55, 0x00, 0xaa },	/* 100001 */
        { 0x55, 0xaa, 0x00 },	/* 100010 */
        { 0x55, 0xaa, 0xaa },	/* 100011 */
        { 0xff, 0x00, 0x00 },	/* 100100 */
        { 0xff, 0x00, 0xaa },	/* 100101 */
        { 0xff, 0xaa, 0x00 },	/* 100110 */
        { 0xff, 0xaa, 0xaa },	/* 100111 */
        { 0x55, 0x00, 0x55 },	/* 101000 */
        { 0x55, 0x00, 0xff },	/* 101001 */
        { 0x55, 0xaa, 0x55 },	/* 101010 */
        { 0x55, 0xaa, 0xff },	/* 101011 */
        { 0xff, 0x00, 0x55 },	/* 101100 */
        { 0xff, 0x00, 0xff },	/* 101101 */
        { 0xff, 0xaa, 0x55 },	/* 101110 */
        { 0xff, 0xaa, 0xff },	/* 101111 */
        { 0x55, 0x55, 0x00 },	/* 110000 */
        { 0x55, 0x55, 0xaa },	/* 110001 */
        { 0x55, 0xff, 0x00 },	/* 110010 */
        { 0x55, 0xff, 0xaa },	/* 110011 */
        { 0xff, 0x55, 0x00 },	/* 110100 */
        { 0xff, 0x55, 0xaa },	/* 110101 */
        { 0xff, 0xff, 0x00 },	/* 110110 */
        { 0xff, 0xff, 0xaa },	/* 110111 */
        { 0x55, 0x55, 0x55 },	/* 111000 */
        { 0x55, 0x55, 0xff },	/* 111001 */
        { 0x55, 0xff, 0x55 },	/* 111010 */
        { 0x55, 0xff, 0xff },	/* 111011 */
        { 0xff, 0x55, 0x55 },	/* 111100 */
        { 0xff, 0x55, 0xff },	/* 111101 */
        { 0xff, 0xff, 0x55 },	/* 111110 */
        { 0xff, 0xff, 0xff },	/* 111111 */
};



typedef struct incolor_t
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

	uint8_t palette[16];	/* EGA-style 16 -> 64 palette registers */
	uint8_t palette_idx;	/* Palette write index */
	uint8_t latch[4];	/* Memory read/write latches */
        uint8_t *vram;
} incolor_t;

void incolor_recalctimings(incolor_t *incolor);
void incolor_write(uint32_t addr, uint8_t val, void *p);
uint8_t incolor_read(uint32_t addr, void *p);


void incolor_out(uint16_t addr, uint8_t val, void *p)
{
        incolor_t *incolor = (incolor_t *)p;
/*        pclog("InColor out %04X %02X\n",addr,val); */
        switch (addr)
        {
                case 0x3b0: case 0x3b2: case 0x3b4: case 0x3b6:
                incolor->crtcreg = val & 31;
                return;
                case 0x3b1: case 0x3b3: case 0x3b5: case 0x3b7:
		if (incolor->crtcreg > 28) return;
		/* Palette load register */
		if (incolor->crtcreg == INCOLOR_CRTC_PALETTE)	
		{
			incolor->palette[incolor->palette_idx % 16] = val;
			++incolor->palette_idx;
		} 
                incolor->crtc[incolor->crtcreg] = val;
                if (incolor->crtc[10] == 6 && incolor->crtc[11] == 7) /*Fix for Generic Turbo XT BIOS, which sets up cursor registers wrong*/
                {
                        incolor->crtc[10] = 0xb;
                        incolor->crtc[11] = 0xc;
                }
                incolor_recalctimings(incolor);
                return;
                case 0x3b8:
                incolor->ctrl = val;
                return;
                case 0x3bf:
                incolor->ctrl2 = val;
                if (val & 2)
                        mem_mapping_set_addr(&incolor->mapping, 0xb0000, 0x10000);
                else
                        mem_mapping_set_addr(&incolor->mapping, 0xb0000, 0x08000);
                return;
        }
}

uint8_t incolor_in(uint16_t addr, void *p)
{
        incolor_t *incolor = (incolor_t *)p;
/*        pclog("InColor in %04X %02X %04X:%04X %04X\n",addr,(incolor->stat & 0xF) | ((incolor->stat & 8) << 4),CS,pc,CX); */
        switch (addr)
        {
                case 0x3b0: case 0x3b2: case 0x3b4: case 0x3b6:
                return incolor->crtcreg;
                case 0x3b1: case 0x3b3: case 0x3b5: case 0x3b7:
		if (incolor->crtcreg > 28) return 0xff;
		incolor->palette_idx = 0;	/* Read resets the palette index */
                return incolor->crtc[incolor->crtcreg];
                case 0x3ba:
		/* 0x50: InColor card identity */
                return (incolor->stat & 0xf) | ((incolor->stat & 8) << 4) | 0x50;
        }
        return 0xff;
}

void incolor_write(uint32_t addr, uint8_t val, void *p)
{
        incolor_t *incolor = (incolor_t *)p;

	int plane;

	unsigned char wmask = incolor->crtc[INCOLOR_CRTC_MASK];
	unsigned char wmode = incolor->crtc[INCOLOR_CRTC_RWCTRL] & INCOLOR_RWCTRL_WRMODE;
	unsigned char fg    = incolor->crtc[INCOLOR_CRTC_RWCOL] & 0x0F;
	unsigned char bg    = (incolor->crtc[INCOLOR_CRTC_RWCOL] >> 4)&0x0F;
	unsigned char w = 0;
	unsigned char vmask;	/* Mask of bit within byte */
	unsigned char pmask;	/* Mask of plane within colour value */
	unsigned char latch;

        egawrites++;

	addr &= 0xFFFF;

	/* In text mode, writes to the bottom 16k always touch all 4 planes */
	if (!(incolor->ctrl & INCOLOR_CTRL_GRAPH) && addr < 0x4000)
	{
		incolor->vram[addr] = val;
		return;
	}

	/* There are four write modes:
	 * 0: 1 => foreground,    0 => background
	 * 1: 1 => foreground,    0 => source latch
 	 * 2: 1 => source latch,  0 => background
	 * 3: 1 => source latch,  0 => ~source latch 
	 */
	pmask = 1;
	for (plane = 0; plane < 4; pmask <<= 1, wmask >>= 1, addr += 0x10000,
		plane++)
	{
		if (wmask & 0x10) /* Ignore writes to selected plane */
		{
			continue;
		}
		latch = incolor->latch[plane];
		for (vmask = 0x80; vmask != 0; vmask >>= 1) 
		{
			switch (wmode) 
			{
				case 0x00:
					if (val & vmask) w = (fg & pmask);
					else		 w = (bg & pmask);
					break;
				case 0x10:
					if (val & vmask) w = (fg    & pmask);
					else		 w = (latch & vmask);
					break;
				case 0x20:
					if (val & vmask) w = (latch & vmask);
					else		 w = (bg    & pmask);
					break;
				case 0x30:
					if (val & vmask) w = (latch    & vmask);
					else		 w = ((~latch) & vmask);
					break;
			}
		/* w is nonzero to write a 1, zero to write a 0 */
			if (w)	incolor->vram[addr] |= vmask;
			else	incolor->vram[addr] &= ~vmask; 	
		}
	}
}

uint8_t incolor_read(uint32_t addr, void *p)
{
        incolor_t *incolor = (incolor_t *)p;
	unsigned plane;
	unsigned char lp    = incolor->crtc[INCOLOR_CRTC_PROTECT];
	unsigned char value = 0;
	unsigned char dc;	/* "don't care" register */
	unsigned char bg;	/* background colour */
	unsigned char fg;
	unsigned char mask, pmask;

        egareads++;

	addr &= 0xFFFF;
	/* Read the four planes into latches */
	for (plane = 0; plane < 4; plane++, addr += 0x10000) 
	{
		incolor->latch[plane] &= lp;
		incolor->latch[plane] |= (incolor->vram[addr] & ~lp);
	}
	addr &= 0xFFFF;
	/* In text mode, reads from the bottom 16k assume all planes have
	 * the same contents */
	if (!(incolor->ctrl & INCOLOR_CTRL_GRAPH) && addr < 0x4000)
	{
		return incolor->latch[0];
	}
	/* For each pixel, work out if its colour matches the background */
	for (mask = 0x80; mask != 0; mask >>= 1) 
	{
		fg = 0;
    		dc = incolor->crtc[INCOLOR_CRTC_RWCTRL] & 0x0F;
    		bg = (incolor->crtc[INCOLOR_CRTC_RWCOL] >> 4) & 0x0F;
		for (plane = 0, pmask = 1; plane < 4; plane++, pmask <<= 1) 
		{
			if (dc & pmask) 
			{
				fg |= (bg & pmask); 
			} 
			else if (incolor->latch[plane] & mask) 
			{
				fg |= pmask;
			}
		}		
		if (bg == fg) value |= mask;
	}	
	if (incolor->crtc[INCOLOR_CRTC_RWCTRL] & INCOLOR_RWCTRL_POLARITY) 
	{
		value = ~value;
	}
	return value;
}



void incolor_recalctimings(incolor_t *incolor)
{
        double disptime;
	double _dispontime, _dispofftime;
        disptime = incolor->crtc[0] + 1;
        _dispontime  = incolor->crtc[1];
        _dispofftime = disptime - _dispontime;
        _dispontime  *= MDACONST;
        _dispofftime *= MDACONST;
	incolor->dispontime  = (int64_t)(_dispontime  * (1 << TIMER_SHIFT));
	incolor->dispofftime = (int64_t)(_dispofftime * (1 << TIMER_SHIFT));
}


static void incolor_draw_char_rom(incolor_t *incolor, int x, uint8_t chr, uint8_t attr)
{
	unsigned            i;
	int                 elg, blk;
	unsigned            ull;
	unsigned            val;
	unsigned	    ifg, ibg;
	const unsigned char *fnt;
	uint32_t	    fg, bg;
	int		    cw = INCOLOR_CW;

	blk = 0;
	if (incolor->ctrl & INCOLOR_CTRL_BLINK) 
	{
		if (attr & 0x80) 
		{
			blk = (incolor->blink & 16);
		}
		attr &= 0x7f;
	}

	if (incolor->crtc[INCOLOR_CRTC_EXCEPT] & INCOLOR_EXCEPT_ALTATTR) 
	{
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
	} 
	else 
	{
		/* CGA-compatible attributes */
		ull = 0xffff;
		ifg = attr & 0x0F;
		ibg = (attr >> 4) & 0x0F;
	}
	if (incolor->crtc[INCOLOR_CRTC_EXCEPT] & INCOLOR_EXCEPT_PALETTE) 
	{
		fg = incolor_rgb[incolor->palette[ifg]];
		bg = incolor_rgb[incolor->palette[ibg]];
	} 
	else 
	{
		fg = incolor_rgb[defpal[ifg]];
		bg = incolor_rgb[defpal[ibg]];
	}

	/* ELG set to stretch 8px character to 9px */
	if (incolor->crtc[INCOLOR_CRTC_XMODE] & INCOLOR_XMODE_90COL) 
	{
		elg = 0;
	} 
	else 
	{
		elg = ((chr >= 0xc0) && (chr <= 0xdf));
	}

	fnt = &(fontdatm[chr][incolor->sc]);

	if (blk)
	{
		val = 0x000;	/* Blinking, draw all background */
	}
	else if (incolor->sc == ull) 
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
		((uint32_t *)buffer32->line[incolor->displine])[x * cw + i] = (val & 0x100) ? fg : bg;
		val = val << 1;
	}
}


static void incolor_draw_char_ram4(incolor_t *incolor, int x, uint8_t chr, uint8_t attr)
{
	unsigned            i;
	int                 elg, blk;
	unsigned            ull;
	unsigned            val[4];
	unsigned	    ifg, ibg, cfg, pmask, plane;
	const unsigned char *fnt;
	uint32_t	    fg;
	int		    cw = INCOLOR_CW;
	int                 blink   = incolor->ctrl & INCOLOR_CTRL_BLINK;
	int                 altattr = incolor->crtc[INCOLOR_CRTC_EXCEPT] & INCOLOR_EXCEPT_ALTATTR;
	int                 palette = incolor->crtc[INCOLOR_CRTC_EXCEPT] & INCOLOR_EXCEPT_PALETTE;

	blk = 0;
	if (blink)
	{
		if (attr & 0x80) 
		{
			blk = (incolor->blink & 16);
		}
		attr &= 0x7f;
	}

	if (altattr)
	{
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
	} 
	else 
	{
		/* CGA-compatible attributes */
		ull = 0xffff;
		ifg = attr & 0x0F;
		ibg = (attr >> 4) & 0x0F;
	}
	if (incolor->crtc[INCOLOR_CRTC_XMODE] & INCOLOR_XMODE_90COL) 
	{
		elg = 0;
	} 
	else 
	{
		elg = ((chr >= 0xc0) && (chr <= 0xdf));
	}
	fnt = incolor->vram + 0x4000 + 16 * chr + incolor->sc;

	if (blk)
	{
		/* Blinking, draw all background */
		val[0] = val[1] = val[2] = val[3] = 0x000;	
	}
	else if (incolor->sc == ull) 
	{
		/* Underscore, draw all foreground */
		val[0] = val[1] = val[2] = val[3] = 0x1ff;
	}
	else 
	{
		val[0] = fnt[0x00000] << 1;
		val[1] = fnt[0x10000] << 1;
		val[2] = fnt[0x20000] << 1;
		val[3] = fnt[0x30000] << 1;
	
		if (elg) 
		{
			val[0] |= (val[0] >> 1) & 1;
			val[1] |= (val[1] >> 1) & 1;
			val[2] |= (val[2] >> 1) & 1;
			val[3] |= (val[3] >> 1) & 1;
		}
	}
	for (i = 0; i < cw; i++) 
	{
		/* Generate pixel colour */
		cfg = 0;
		pmask = 1;
		for (plane = 0; plane < 4; plane++, pmask = pmask << 1)
		{
			if (val[plane] & 0x100) cfg |= (ifg & pmask);
			else			cfg |= (ibg & pmask);
		}
		/* cfg = colour of foreground pixels */
		if (altattr && (attr & 0x77) == 0) cfg = ibg; /* 'blank' attribute */
		if (palette)
		{
			fg = incolor_rgb[incolor->palette[cfg]];
		} 
		else 
		{
			fg = incolor_rgb[defpal[cfg]];
		}
		
		((uint32_t *)buffer32->line[incolor->displine])[x * cw + i] = fg;
		val[0] = val[0] << 1;
		val[1] = val[1] << 1;
		val[2] = val[2] << 1;
		val[3] = val[3] << 1;
	}
}


static void incolor_draw_char_ram48(incolor_t *incolor, int x, uint8_t chr, uint8_t attr)
{
	unsigned            i;
	int                 elg, blk, ul, ol, bld;
	unsigned            ull, oll, ulc = 0, olc = 0;
	unsigned            val[4];
	unsigned	    ifg = 0, ibg, cfg, pmask, plane;
	const unsigned char *fnt;
	uint32_t	    fg;
	int		    cw = INCOLOR_CW;
	int                 blink   = incolor->ctrl & INCOLOR_CTRL_BLINK;
	int                 altattr = incolor->crtc[INCOLOR_CRTC_EXCEPT] & INCOLOR_EXCEPT_ALTATTR;
	int                 palette = incolor->crtc[INCOLOR_CRTC_EXCEPT] & INCOLOR_EXCEPT_PALETTE;
	int		    font = (attr & 0x0F);

	if (font >= 12) font &= 7;

	blk = 0;
	if (blink && altattr)
	{
		if (attr & 0x40) 
		{
			blk = (incolor->blink & 16);
		}
		attr &= 0x7f;
	}
	if (altattr) 
	{
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
	} 
	else 
	{
		/* CGA-compatible attributes */
		ibg = 0;
		ifg = (attr >> 4) & 0x0F;
		ol  = 0;
		ul  = 0;
		bld = 0;
	}
	if (ul) 
	{ 
		ull = incolor->crtc[INCOLOR_CRTC_UNDER] & 0x0F;
		ulc = (incolor->crtc[INCOLOR_CRTC_UNDER] >> 4) & 0x0F;
		if (ulc == 0) ulc = 7;
	} 
	else 
	{
		ull = 0xFFFF;
	}
	if (ol) 
	{ 
		oll = incolor->crtc[INCOLOR_CRTC_OVER] & 0x0F;
		olc = (incolor->crtc[INCOLOR_CRTC_OVER] >> 4) & 0x0F;
		if (olc == 0) olc = 7;
	} 
	else 
	{
		oll = 0xFFFF;
	}

	if (incolor->crtc[INCOLOR_CRTC_XMODE] & INCOLOR_XMODE_90COL) 
	{
		elg = 0;
	} 
	else 
	{
		elg = ((chr >= 0xc0) && (chr <= 0xdf));
	}
	fnt = incolor->vram + 0x4000 + 16 * chr + 4096 * font + incolor->sc;

	if (blk)
	{
		/* Blinking, draw all background */
		val[0] = val[1] = val[2] = val[3] = 0x000;	
	}
	else if (incolor->sc == ull) 
	{
		/* Underscore, draw all foreground */
		val[0] = val[1] = val[2] = val[3] = 0x1ff;
	}
	else 
	{
		val[0] = fnt[0x00000] << 1;
		val[1] = fnt[0x10000] << 1;
		val[2] = fnt[0x20000] << 1;
		val[3] = fnt[0x30000] << 1;
	
		if (elg) 
		{
			val[0] |= (val[0] >> 1) & 1;
			val[1] |= (val[1] >> 1) & 1;
			val[2] |= (val[2] >> 1) & 1;
			val[3] |= (val[3] >> 1) & 1;
		}
		if (bld) 
		{
			val[0] |= (val[0] >> 1);
			val[1] |= (val[1] >> 1);
			val[2] |= (val[2] >> 1);
			val[3] |= (val[3] >> 1);
		}
	}
	for (i = 0; i < cw; i++) 
	{
		/* Generate pixel colour */
		cfg = 0;
		pmask = 1;
		if (incolor->sc == oll)
		{
			cfg = olc ^ ibg;	/* Strikethrough */
		}
		else if (incolor->sc == ull)
		{
			cfg = ulc ^ ibg;	/* Underline */
		}
		else
		{
			for (plane = 0; plane < 4; plane++, pmask = pmask << 1)
			{
				if (val[plane] & 0x100)
				{
					if (altattr)	cfg |= ((~ibg) & pmask);
					else		cfg |= ((~ifg) & pmask);
				}
				else if (altattr)	cfg |= (ibg & pmask);
			}
		}
		if (palette)
		{
			fg = incolor_rgb[incolor->palette[cfg]];
		} 
		else 
		{
			fg = incolor_rgb[defpal[cfg]];
		}
		
		((uint32_t *)buffer32->line[incolor->displine])[x * cw + i] = fg;
		val[0] = val[0] << 1;
		val[1] = val[1] << 1;
		val[2] = val[2] << 1;
		val[3] = val[3] << 1;
	}
}






static void incolor_text_line(incolor_t *incolor, uint16_t ca)
{
        int drawcursor;
	int x, c;
        uint8_t chr, attr;
	uint32_t col;

	for (x = 0; x < incolor->crtc[1]; x++)
	{
		chr  = incolor->vram[(incolor->ma << 1) & 0xfff];
                attr = incolor->vram[((incolor->ma << 1) + 1) & 0xfff];

                drawcursor = ((incolor->ma == ca) && incolor->con && incolor->cursoron);

		switch (incolor->crtc[INCOLOR_CRTC_XMODE] & 5)
		{
			case 0:
			case 4:	/* ROM font */
				incolor_draw_char_rom(incolor, x, chr, attr);
				break;
			case 1: /* 4k RAMfont */
				incolor_draw_char_ram4(incolor, x, chr, attr);
				break;
			case 5: /* 48k RAMfont */
				incolor_draw_char_ram48(incolor, x, chr, attr);
				break;

		}
		++incolor->ma;
                if (drawcursor)
                {
			int cw = INCOLOR_CW;
			uint8_t ink = incolor->crtc[INCOLOR_CRTC_EXCEPT] & INCOLOR_EXCEPT_CURSOR;
			if (ink == 0) ink = (attr & 0x08) | 7;

			/* In MDA-compatible mode, cursor brightness comes from 
			 * background */
			if (incolor->crtc[INCOLOR_CRTC_EXCEPT] & INCOLOR_EXCEPT_ALTATTR) 
			{
				ink = (attr & 0x08) | (ink & 7);
			}
			if (incolor->crtc[INCOLOR_CRTC_EXCEPT] & INCOLOR_EXCEPT_PALETTE) 
			{
				col = incolor_rgb[incolor->palette[ink]];
			} 
			else 
			{
				col = incolor_rgb[defpal[ink]];
			}
			for (c = 0; c < cw; c++)
			{
				((uint32_t *)buffer32->line[incolor->displine])[x * cw + c] = col;
			}
		}
	}
}


static void incolor_graphics_line(incolor_t *incolor)
{
	uint8_t mask;
	uint16_t ca;
	int x, c, plane, col;
	uint8_t ink;
	uint16_t val[4];

	/* Graphics mode. */
        ca = (incolor->sc & 3) * 0x2000;
        if ((incolor->ctrl & INCOLOR_CTRL_PAGE1) && (incolor->ctrl2 & INCOLOR_CTRL2_PAGE1))
		ca += 0x8000;

	for (x = 0; x < incolor->crtc[1]; x++)
	{
		mask = incolor->crtc[INCOLOR_CRTC_MASK];	/* Planes to display */
		for (plane = 0; plane < 4; plane++, mask = mask >> 1)
		{
			if (mask & 1) 
				val[plane] = (incolor->vram[((incolor->ma << 1) & 0x1fff) + ca + 0x10000 * plane] << 8) | 
					      incolor->vram[((incolor->ma << 1) & 0x1fff) + ca + 0x10000 * plane + 1];
			else	val[plane] = 0;
		}
		incolor->ma++;
		for (c = 0; c < 16; c++)
		{
			ink = 0;
			for (plane = 0; plane < 4; plane++)
			{
				ink = ink >> 1;
				if (val[plane] & 0x8000) ink |= 8;
				val[plane] = val[plane] << 1;
			}
			/* Is palette in use? */
			if (incolor->crtc[INCOLOR_CRTC_EXCEPT] & INCOLOR_EXCEPT_PALETTE)
				col = incolor->palette[ink];
			else	col = defpal[ink];

			((uint32_t *)buffer32->line[incolor->displine])[(x << 4) + c] = incolor_rgb[col];
		}
	}
}

void incolor_poll(void *p)
{
        incolor_t *incolor = (incolor_t *)p;
        uint16_t ca = (incolor->crtc[15] | (incolor->crtc[14] << 8)) & 0x3fff;
        int x;
        int oldvc;
        int oldsc;

        if (!incolor->linepos)
        {
                incolor->vidtime += incolor->dispofftime;
                incolor->stat |= 1;
                incolor->linepos = 1;
                oldsc = incolor->sc;
                if ((incolor->crtc[8] & 3) == 3) 
                        incolor->sc = (incolor->sc << 1) & 7;
                if (incolor->dispon)
                {
                        if (incolor->displine < incolor->firstline)
                        {
                                incolor->firstline = incolor->displine;
                                video_wait_for_buffer();
                        }
                        incolor->lastline = incolor->displine;
                        if ((incolor->ctrl & INCOLOR_CTRL_GRAPH) && (incolor->ctrl2 & INCOLOR_CTRL2_GRAPH))
                        {
				incolor_graphics_line(incolor);
                        }
                        else
                        {
				incolor_text_line(incolor, ca);
                        }
                }
                incolor->sc = oldsc;
                if (incolor->vc == incolor->crtc[7] && !incolor->sc)
                {
                        incolor->stat |= 8;
                }
                incolor->displine++;
                if (incolor->displine >= 500) 
                        incolor->displine = 0;
        }
        else
        {
                incolor->vidtime += incolor->dispontime;
                if (incolor->dispon) 
                        incolor->stat &= ~1;
                incolor->linepos = 0;
                if (incolor->vsynctime)
                {
                        incolor->vsynctime--;
                        if (!incolor->vsynctime)
                        {
                                incolor->stat &= ~8;
                        }
                }
                if (incolor->sc == (incolor->crtc[11] & 31) || ((incolor->crtc[8] & 3) == 3 && incolor->sc == ((incolor->crtc[11] & 31) >> 1))) 
                { 
                        incolor->con = 0; 
                        incolor->coff = 1; 
                }
                if (incolor->vadj)
                {
                        incolor->sc++;
                        incolor->sc &= 31;
                        incolor->ma = incolor->maback;
                        incolor->vadj--;
                        if (!incolor->vadj)
                        {
                                incolor->dispon = 1;
                                incolor->ma = incolor->maback = (incolor->crtc[13] | (incolor->crtc[12] << 8)) & 0x3fff;
                                incolor->sc = 0;
                        }
                }
                else if (incolor->sc == incolor->crtc[9] || ((incolor->crtc[8] & 3) == 3 && incolor->sc == (incolor->crtc[9] >> 1)))
                {
                        incolor->maback = incolor->ma;
                        incolor->sc = 0;
                        oldvc = incolor->vc;
                        incolor->vc++;
                        incolor->vc &= 127;
                        if (incolor->vc == incolor->crtc[6]) 
                                incolor->dispon = 0;
                        if (oldvc == incolor->crtc[4])
                        {
                                incolor->vc = 0;
                                incolor->vadj = incolor->crtc[5];
                                if (!incolor->vadj) incolor->dispon=1;
                                if (!incolor->vadj) incolor->ma = incolor->maback = (incolor->crtc[13] | (incolor->crtc[12] << 8)) & 0x3fff;
                                if ((incolor->crtc[10] & 0x60) == 0x20) incolor->cursoron = 0;
                                else                                     incolor->cursoron = incolor->blink & 16;
                        }
                        if (incolor->vc == incolor->crtc[7])
                        {
                                incolor->dispon = 0;
                                incolor->displine = 0;
                                incolor->vsynctime = 16;
                                if (incolor->crtc[7])
                                {
                                        if ((incolor->ctrl & INCOLOR_CTRL_GRAPH) && (incolor->ctrl2 & INCOLOR_CTRL2_GRAPH)) 
					{
						x = incolor->crtc[1] << 4;
					}
                                        else
					{
                                               x = incolor->crtc[1] * 9;
					}
                                        incolor->lastline++;
                                        if (x != xsize || (incolor->lastline - incolor->firstline) != ysize)
                                        {
                                                xsize = x;
                                                ysize = incolor->lastline - incolor->firstline;
                                                if (xsize < 64) xsize = 656;
                                                if (ysize < 32) ysize = 200;
                                                updatewindowsize(xsize, ysize);
                                        }
					video_blit_memtoscreen(0, incolor->firstline, 0, incolor->lastline - incolor->firstline, xsize, incolor->lastline - incolor->firstline);
                                        frames++;
                                        if ((incolor->ctrl & INCOLOR_CTRL_GRAPH) && (incolor->ctrl2 & INCOLOR_CTRL2_GRAPH))
                                        {
                                                video_res_x = incolor->crtc[1] * 16;
                                                video_res_y = incolor->crtc[6] * 4;
                                                video_bpp = 1;
                                        }
                                        else
                                        {
                                                video_res_x = incolor->crtc[1];
                                                video_res_y = incolor->crtc[6];
                                                video_bpp = 0;
                                        }
                                }
                                incolor->firstline = 1000;
                                incolor->lastline = 0;
                                incolor->blink++;
                        }
                }
                else
                {
                        incolor->sc++;
                        incolor->sc &= 31;
                        incolor->ma = incolor->maback;
                }
                if ((incolor->sc == (incolor->crtc[10] & 31) || ((incolor->crtc[8] & 3) == 3 && incolor->sc == ((incolor->crtc[10] & 31) >> 1))))
                {
                        incolor->con = 1;
                }
        }
}

void *incolor_init(device_t *info)
{
        int c;
        incolor_t *incolor = malloc(sizeof(incolor_t));
        memset(incolor, 0, sizeof(incolor_t));

        incolor->vram = malloc(0x40000);	/* 4 planes of 64k */

        timer_add(incolor_poll, &incolor->vidtime, TIMER_ALWAYS_ENABLED, incolor);
        mem_mapping_add(&incolor->mapping, 0xb0000, 0x08000, incolor_read, NULL, NULL, incolor_write, NULL, NULL,  NULL, MEM_MAPPING_EXTERNAL, incolor);
        io_sethandler(0x03b0, 0x0010, incolor_in, NULL, NULL, incolor_out, NULL, NULL, incolor);

	for (c = 0; c < 64; c++)
	{
		incolor_rgb[c] = makecol32(init_rgb[c][0], init_rgb[c][1], init_rgb[c][2]);
	}

/* Initialise CRTC regs to safe values */
	incolor->crtc[INCOLOR_CRTC_MASK  ] = 0x0F; /* All planes displayed */
	incolor->crtc[INCOLOR_CRTC_RWCTRL] = INCOLOR_RWCTRL_POLARITY;
	incolor->crtc[INCOLOR_CRTC_RWCOL ] = 0x0F; /* White on black */
	incolor->crtc[INCOLOR_CRTC_EXCEPT] = INCOLOR_EXCEPT_ALTATTR;
	for (c = 0; c < 16; c++) 
	{
		incolor->palette[c] = defpal[c];
	}
	incolor->palette_idx = 0;



        return incolor;
}

void incolor_close(void *p)
{
        incolor_t *incolor = (incolor_t *)p;

        free(incolor->vram);
        free(incolor);
}

void incolor_speed_changed(void *p)
{
        incolor_t *incolor = (incolor_t *)p;
        
        incolor_recalctimings(incolor);
}

device_t incolor_device =
{
        "Hercules InColor",
        DEVICE_ISA, 0,
        incolor_init,
        incolor_close,
	NULL,
        NULL,
        incolor_speed_changed,
	NULL,
	NULL,
        NULL
};
