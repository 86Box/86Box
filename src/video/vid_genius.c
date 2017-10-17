/*
 * 86Box	A hypervisor and IBM PC system emulator that specializes in
 *		running old operating systems and software designed for IBM
 *		PC systems and compatibles from 1981 through fairly recent
 *		system designs based on the PCI bus.
 *
 *		This file is part of the 86Box distribution.
 *
 *		MDSI Genius VHR emulation.
 *
 * Version:	@(#)vid_genius.c	1.0.1	2017/10/16
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
#include "../plat.h"
#include "video.h"
#include "vid_genius.h"


#define GENIUS_XSIZE 728
#define GENIUS_YSIZE 1008


extern uint8_t fontdat8x12[256][16];	


/* I'm at something of a disadvantage writing this emulation: I don't have an
 * MDSI Genius card, nor do I have the BIOS extension (VHRBIOS.SYS) that came 
 * with it. What I do have are the GEM and Windows 1.04 drivers, plus a driver
 * for a later MCA version of the card. The latter can be found at 
 * <http://files.mpoli.fi/hardware/DISPLAY/GENIUS/> and is necessary if you
 * want the Windows driver to work.
 *
 * This emulation appears to work correctly with:
 * The MCA drivers GMC_ANSI.SYS and INS_ANSI.SYS
 * The GEM driver SDGEN9.VGA
 * The Windows 1.04 driver GENIUS.DRV
 *
 * As far as I can see, the card uses a fixed resolution of 728x1008 pixels. 
 * It has the following modes of operation:
 * 
 * > MDA-compatible:      80x25 text, each character 9x15 pixels.
 * > CGA-compatible:      640x200 mono graphics
 * > Dual:                MDA text in the top half, CGA graphics in the bottom
 * > Native text:         80x66 text, each character 9x15 pixels.
 * > Native graphics:     728x1008 mono graphics.
 *
 * Under the covers, this seems to translate to:
 *  > Text framebuffer.     At B000:0000, 16k. Displayed if enable bit is set 
 *                         in the MDA control register.
 *  > Graphics framebuffer. In native modes goes from A000:0000 to A000:FFFF
 *                         and B800:0000 to B800:FFFF. In CGA-compatible 
 *                         mode only the section at B800:0000 to B800:7FFF
 *                         is visible. Displayed if enable bit is set in the
 *                         CGA control register.
 * 
 * Two card-specific registers control text and graphics display:
 * 
 *  03B0: Control register.
 * 	   Bit 0: Map all graphics framebuffer into memory.
 *         Bit 2: Unknown. Set by GMC /M; cleared by mode set or GMC /T.
 *         Bit 4: Set for CGA-compatible graphics, clear for native graphics.
 *         Bit 5: Set for black on white, clear for white on black.
 *
 *  03B1: Character height register.
 *        Bits 0-1: Character cell height (0 => 15, 1 => 14, 2 => 13, 3 => 12)
 *        Bit  4:   Set to double character cell height (scanlines are doubled)
 *        Bit  7:   Unknown, seems to be set for all modes except 80x66
 *
 *  Not having the card also means I don't have its font. According to the 
 *  card brochure the font is an 8x12 bitmap in a 9x15 character cell. I 
 *  therefore generated it by taking the MDA font, increasing graphics to 
 *  16 pixels in height and reducing the height of characters so they fit
 *  in an 8x12 cell if necessary.
 */

 

typedef struct genius_t
{
        mem_mapping_t mapping;

        uint8_t mda_crtc[32];	/* The 'CRTC' as the host PC sees it */
        int mda_crtcreg;	/* Current CRTC register */
	uint8_t genius_control;	/* Native control register 
				 * I think bit 0 enables the full 
				 * framebuffer. 
				 */
	uint8_t genius_charh;	/* Native character height register: 
				 * 00h => chars are 15 pixels high 
				 * 81h => chars are 14 pixels high
				 * 83h => chars are 12 pixels high 
				 * 90h => chars are 30 pixels high [15 x 2]
				 * 93h => chars are 24 pixels high [12 x 2]
				 */
	uint8_t genius_mode;	/* Current mode (see list at top of file) */
	uint8_t cga_ctrl;	/* Emulated CGA control register */
	uint8_t mda_ctrl;	/* Emulated MDA control register */
	uint8_t cga_colour;	/* Emulated CGA colour register (ignored) */

        uint8_t mda_stat;	/* MDA status (IN 0x3BA) */
        uint8_t cga_stat;	/* CGA status (IN 0x3DA) */

	int font;		/* Current font, 0 or 1 */
	int enabled;		/* Display enabled, 0 or 1 */
	int detach;		/* Detach cursor, 0 or 1 */

        int64_t dispontime, dispofftime;
        int64_t vidtime;
        
        int linepos, displine;
        int vc;
        int dispon, blink;
        int64_t vsynctime;

        uint8_t *vram;
} genius_t;

/* Mapping of attributes to colours, in MDA emulation mode */
static int mdacols[256][2][2];

void genius_recalctimings(genius_t *genius);
void genius_write(uint32_t addr, uint8_t val, void *p);
uint8_t genius_read(uint32_t addr, void *p);


void genius_out(uint16_t addr, uint8_t val, void *p)
{
        genius_t *genius = (genius_t *)p;

        switch (addr)
        {
                case 0x3b0: 	/* Command / control register */
		genius->genius_control = val;
		if (val & 1)
		{
			mem_mapping_set_addr(&genius->mapping, 0xa0000, 0x28000);
		}
		else
		{
			mem_mapping_set_addr(&genius->mapping, 0xb0000, 0x10000);
		}

		break;

		case 0x3b1:
		genius->genius_charh = val;
		break;

		/* Emulated CRTC, register select */
		case 0x3b2: case 0x3b4: case 0x3b6:
		case 0x3d0: case 0x3d2: case 0x3d4: case 0x3d6:
                genius->mda_crtcreg = val & 31;
                break;

		/* Emulated CRTC, value */
                case 0x3b3: case 0x3b5: case 0x3b7:
                case 0x3d1: case 0x3d3: case 0x3d5: case 0x3d7:
	        genius->mda_crtc[genius->mda_crtcreg] = val;
                genius_recalctimings(genius);
	        return;

		/* Emulated MDA control register */
                case 0x3b8: 
             	genius->mda_ctrl = val;
              	return;
		/* Emulated CGA control register */
		case 0x3D8:
             	genius->cga_ctrl = val;
              	return;
		/* Emulated CGA colour register */
                case 0x3D9:
               	genius->cga_colour = val;
              	return;
        }
}

uint8_t genius_in(uint16_t addr, void *p)
{
        genius_t *genius = (genius_t *)p;
        
        switch (addr)
        {
                case 0x3b0: case 0x3b2: case 0x3b4: case 0x3b6:
                case 0x3d0: case 0x3d2: case 0x3d4: case 0x3d6:
	        return genius->mda_crtcreg;
                case 0x3b1: case 0x3b3: case 0x3b5: case 0x3b7:
		case 0x3d1: case 0x3d3: case 0x3d5: case 0x3d7:
	        return genius->mda_crtc[genius->mda_crtcreg];
		case 0x3b8: 
		return genius->mda_ctrl;
		case 0x3d9:
		return genius->cga_colour;
                case 0x3ba: 
	        return genius->mda_stat;
		case 0x3d8:
		return genius->cga_ctrl;
		case 0x3da:
		return genius->cga_stat;
        }
        return 0xff;
}



void genius_write(uint32_t addr, uint8_t val, void *p)
{
        genius_t *genius = (genius_t *)p;
        egawrites++;
        
	if (genius->genius_control & 1)
	{
		addr = addr % 0x28000;
	}
	else
	/* If hi-res memory is disabled, only visible in the B000 segment */
	{
		addr = (addr & 0xFFFF) + 0x10000;
	}
       	genius->vram[addr] = val;
}



uint8_t genius_read(uint32_t addr, void *p)
{
        genius_t *genius = (genius_t *)p;
        egareads++;

	if (genius->genius_control & 1)
	{
		addr = addr % 0x28000;
	}
	else
	/* If hi-res memory is disabled, only visible in the B000 segment */
	{
		addr = (addr & 0xFFFF) + 0x10000;
	}
       	return genius->vram[addr];
}



void genius_recalctimings(genius_t *genius)
{
        double disptime;
	double _dispontime, _dispofftime;

	disptime = 0x31;
	_dispontime = 0x28;
        _dispofftime = disptime - _dispontime;
        _dispontime  *= MDACONST;
        _dispofftime *= MDACONST;
	genius->dispontime  = (int64_t)(_dispontime  * (1LL << TIMER_SHIFT));
	genius->dispofftime = (int64_t)(_dispofftime * (1LL << TIMER_SHIFT));
}


/* Draw a single line of the screen in either text mode */
void genius_textline(genius_t *genius, uint8_t background)
{
	int x;
	int w  = 80;	/* 80 characters across */
	int cw = 9;	/* Each character is 9 pixels wide */
	uint8_t chr, attr;
	uint8_t bitmap[2];
        int blink, c, row;
        int drawcursor, cursorline;
	uint16_t addr;
	uint8_t sc;
	int charh;
	uint16_t ma = (genius->mda_crtc[13] | (genius->mda_crtc[12] << 8)) & 0x3fff;
	uint16_t ca = (genius->mda_crtc[15] | (genius->mda_crtc[14] << 8)) & 0x3fff;
	unsigned char *framebuf = genius->vram + 0x10000;
	uint8_t col;

	/* Character height is 12-15 */
	charh = 15 - (genius->genius_charh & 3);
	if (genius->genius_charh & 0x10)
	{
		row = ((genius->displine >> 1) / charh);	
		sc  = ((genius->displine >> 1) % charh);	
	}
	else
	{
		row = (genius->displine / charh);	
		sc  = (genius->displine % charh);	
	}
	addr = ((ma & ~1) + row * w) * 2;

	ma += (row * w);
	
	if ((genius->mda_crtc[10] & 0x60) == 0x20)
	{
		cursorline = 0;
	}
	else
	{
		cursorline = ((genius->mda_crtc[10] & 0x1F) <= sc) &&
			     ((genius->mda_crtc[11] & 0x1F) >= sc);
	}

	for (x = 0; x < w; x++)
	{
		chr  = framebuf[(addr + 2 * x) & 0x3FFF];
		attr = framebuf[(addr + 2 * x + 1) & 0x3FFF];
		drawcursor = ((ma == ca) && cursorline && genius->enabled &&
			(genius->mda_ctrl & 8));

		switch (genius->mda_crtc[10] & 0x60)
		{
			case 0x00: drawcursor = drawcursor && (genius->blink & 16); break;
			case 0x60: drawcursor = drawcursor && (genius->blink & 32); break;
		}
		blink = ((genius->blink & 16) && 
			(genius->mda_ctrl & 0x20) && 
			(attr & 0x80) && !drawcursor);

		if (genius->mda_ctrl & 0x20) attr &= 0x7F;
		/* MDA underline */
		if (sc == charh && ((attr & 7) == 1))
		{
			col = mdacols[attr][blink][1];

			if (genius->genius_control & 0x20)
			{
				col ^= 15;
			}

			for (c = 0; c < cw; c++)
			{
				if (col != background) 
					buffer->line[genius->displine][(x * cw) + c] = col;
			}
		}
		else	/* Draw 8 pixels of character */
		{
			bitmap[0] = fontdat8x12[chr][sc];
			for (c = 0; c < 8; c++)
			{
				col = mdacols[attr][blink][(bitmap[0] & (1 << (c ^ 7))) ? 1 : 0];
				if (!(genius->enabled) || !(genius->mda_ctrl & 8))
					col = mdacols[0][0][0];
	
				if (genius->genius_control & 0x20)
				{
					col ^= 15;
				}
				if (col != background)
				{
					buffer->line[genius->displine][(x * cw) + c] = col;
				}
			}
			/* The ninth pixel column... */
			if ((chr & ~0x1f) == 0xc0) 
			{
				/* Echo column 8 for the graphics chars */
				col = buffer->line[genius->displine][(x * cw) + 7];
				if (col != background) buffer->line[genius->displine][(x * cw) + 8] = col;
			}
			else	/* Otherwise fill with background */	
			{
				col = mdacols[attr][blink][0];
				if (genius->genius_control & 0x20)
				{
					col ^= 15;
				}
				if (col != background) buffer->line[genius->displine][(x * cw) + 8] = col;
			}
                        if (drawcursor)
                        {
                        	for (c = 0; c < cw; c++)
                                	buffer->line[genius->displine][(x * cw) + c] ^= mdacols[attr][0][1];
                        }
			++ma;
		}
	}
}




/* Draw a line in the CGA 640x200 mode */
void genius_cgaline(genius_t *genius)
{
	int x, c;
	uint32_t dat;
	uint8_t ink;
	uint32_t addr;

	ink = (genius->genius_control & 0x20) ? 16 : 16+15;
	/* We draw the CGA at row 600 */
	if (genius->displine < 600) 
	{
		return;
	}
	addr = 0x18000 + 80 * ((genius->displine - 600) >> 2);
	if ((genius->displine - 600) & 2)
	{
		addr += 0x2000;
	}

	for (x = 0; x < 80; x++)
	{
		dat =  genius->vram[addr];
		addr++;

		for (c = 0; c < 8; c++)
		{
			if (dat & 0x80)
			{
				buffer->line[genius->displine][x*8 + c] = ink;
			}
			dat = dat << 1;
		}
	}
}




/* Draw a line in the native high-resolution mode */
void genius_hiresline(genius_t *genius)
{
	int x, c;
	uint32_t dat;
	uint8_t ink;
	uint32_t addr;
        
	ink = (genius->genius_control & 0x20) ? 16 : 16+15;
	/* The first 512 lines live at A0000 */
	if (genius->displine < 512) 
	{
		addr = 128 * genius->displine;
	}
	else	/* The second 496 live at B8000 */
	{
		addr = 0x18000 + 128 * (genius->displine - 512);
	}

	for (x = 0; x < 91; x++)
	{
		dat =  genius->vram[addr];
		addr++;

		for (c = 0; c < 8; c++)
		{
			if (dat & 0x80)
			{
				buffer->line[genius->displine][x*8 + c] = ink;
			}
			dat = dat << 1;
		}
	}
}




void genius_poll(void *p)
{
        genius_t *genius = (genius_t *)p;
        int x;
        uint8_t background;

        if (!genius->linepos)
        {
                genius->vidtime += genius->dispofftime;
                genius->cga_stat |= 1;
                genius->mda_stat |= 1;
                genius->linepos = 1;
                if (genius->dispon)
                {
			if (genius->genius_control & 0x20)
			{
				background = 16 + 15;
			}
			else
			{
				background = 16;
			}
                        if (genius->displine == 0)
                        {
                                video_wait_for_buffer();
                        }
			/* Start off with a blank line */
			for (x = 0; x < GENIUS_XSIZE; x++)
			{
				buffer->line[genius->displine][x] = background;
			}
			/* If graphics display enabled, draw graphics on top
			 * of the blanked line */
			if (genius->cga_ctrl & 8)
			{
				if (genius->genius_control & 8)
				{
					genius_cgaline(genius);
				}
				else
				{
					genius_hiresline(genius);
				}
			}
			/* If MDA display is enabled, draw MDA text on top
			 * of the lot */
			if (genius->mda_ctrl & 8)
			{	
				genius_textline(genius, background);
			}
                }
                genius->displine++;
		/* Hardcode a fixed refresh rate and VSYNC timing */
                if (genius->displine == 1008) /* Start of VSYNC */
                {
                        genius->cga_stat |= 8;
			genius->dispon = 0;
                }
		if (genius->displine == 1040) /* End of VSYNC */
		{
                        genius->displine = 0;
                        genius->cga_stat &= ~8;
			genius->dispon = 1;
		}
        }
        else
        {
		if (genius->dispon)
		{
                	genius->cga_stat &= ~1;
                	genius->mda_stat &= ~1;
		}
                genius->vidtime += genius->dispontime;
                genius->linepos = 0;

		if (genius->displine == 1008)
                {
/* Hardcode GENIUS_XSIZE * GENIUS_YSIZE window size */
			if (GENIUS_XSIZE != xsize || GENIUS_YSIZE != ysize)
			{
                                xsize = GENIUS_XSIZE;
                                ysize = GENIUS_YSIZE;
                                if (xsize < 64) xsize = 656;
                                if (ysize < 32) ysize = 200;
                                updatewindowsize(xsize, ysize);
                        }
                        video_blit_memtoscreen_8(0, 0, xsize, ysize);

                        frames++;
			/* Fixed 728x1008 resolution */
			video_res_x = GENIUS_XSIZE;
			video_res_y = GENIUS_YSIZE;
			video_bpp = 1;
                	genius->blink++;
                }
        }
}

void *genius_init(device_t *info)
{
        int c;
        genius_t *genius = malloc(sizeof(genius_t));
        memset(genius, 0, sizeof(genius_t));

	/* 160k video RAM */
        genius->vram = malloc(0x28000);

        timer_add(genius_poll, &genius->vidtime, TIMER_ALWAYS_ENABLED, genius);

	/* Occupy memory between 0xB0000 and 0xBFFFF (moves to 0xA0000 in
	 * high-resolution modes)  */
        mem_mapping_add(&genius->mapping, 0xb0000, 0x10000, genius_read, NULL, NULL, genius_write, NULL, NULL,  NULL, MEM_MAPPING_EXTERNAL, genius);
	/* Respond to both MDA and CGA I/O ports */
        io_sethandler(0x03b0, 0x000C, genius_in, NULL, NULL, genius_out, NULL, NULL, genius);
        io_sethandler(0x03d0, 0x0010, genius_in, NULL, NULL, genius_out, NULL, NULL, genius);

	/* MDA attributes */
	/* I don't know if the Genius's MDA emulation actually does 
	 * emulate bright / non-bright. For the time being pretend it does. */
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

/* Start off in 80x25 text mode */
        genius->cga_stat   = 0xF4;
	genius->genius_mode = 2;
	genius->enabled    = 1;
	genius->genius_charh = 0x90; /* Native character height register */
        return genius;
}

void genius_close(void *p)
{
        genius_t *genius = (genius_t *)p;

        free(genius->vram);
        free(genius);
}

static int genius_available(void)
{
        return rom_present(L"roms/video/genius/8x12.bin");
}

void genius_speed_changed(void *p)
{
        genius_t *genius = (genius_t *)p;
        
        genius_recalctimings(genius);
}

device_t genius_device =
{
        "Genius VHR",
        DEVICE_ISA, 0,
        genius_init,
        genius_close,
	NULL,
        genius_available,
        genius_speed_changed,
	NULL,
        NULL,
        NULL
};
