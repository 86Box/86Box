/*
 * 86Box	A hypervisor and IBM PC system emulator that specializes in
 *		running old operating systems and software designed for IBM
 *		PC systems and compatibles from 1981 through fairly recent
 *		system designs based on the PCI bus.
 *
 *		This file is part of the 86Box distribution.
 *
 *		Emulation of the EGA, Chips & Technologies SuperEGA, and
 *		AX JEGA graphics cards.
 *
 * Version:	@(#)vid_ega.c	1.0.20	2019/09/26
 *
 * Authors:	Sarah Walker, <http://pcem-emulator.co.uk/>
 *		Miran Grca, <mgrca8@gmail.com>
 *		akm
 *
 *		Copyright 2008-2019 Sarah Walker.
 *		Copyright 2016-2019 Miran Grca.
 */
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <stdlib.h>
#include <wchar.h>
#include "../86box.h"
#include "../cpu/cpu.h"
#include "../io.h"
#include "../timer.h"
#include "../pit.h"
#include "../mem.h"
#include "../rom.h"
#include "../device.h"
#include "video.h"
#include "vid_ega.h"
#include "vid_ega_render.h"


#define BIOS_IBM_PATH	L"roms/video/ega/ibm_6277356_ega_card_u44_27128.bin"
#define BIOS_CPQ_PATH	L"roms/video/ega/108281-001.bin"
#define BIOS_SEGA_PATH	L"roms/video/ega/lega.vbi"


enum {
	EGA_IBM = 0,
	EGA_COMPAQ,
	EGA_SUPEREGA
};


static video_timings_t timing_ega	= {VIDEO_ISA, 8, 16, 32,   8, 16, 32};

extern uint8_t edatlookup[4][4];

static uint8_t ega_rotate[8][256];

static uint32_t pallook16[256], pallook64[256];

/*3C2 controls default mode on EGA. On VGA, it determines monitor type (mono or colour)*/
int egaswitchread,egaswitches=9; /*7=CGA mode (200 lines), 9=EGA mode (350 lines), 8=EGA mode (200 lines)*/

static int old_overscan_color = 0;

int update_overscan = 0;

#ifdef JEGA
uint8_t jfont_sbcs_19[SBCS19_LEN];	/* 256 * 19( * 8) */
uint8_t jfont_dbcs_16[DBCS16_LEN];	/* 65536 * 16 * 2 (* 8) */

typedef struct {
    char id[ID_LEN];
    char name[NAME_LEN];
    unsigned char width;
    unsigned char height;
    unsigned char type;
} fontx_h;

typedef struct {
    uint16_t start;
    uint16_t end;
} fontxTbl;

static __inline int ega_jega_enabled(ega_t *ega)
{
	if (!ega->is_jega)
	{
		return 0;
	}

	return !(ega->RMOD1 & 0x40);
}

void ega_jega_write_font(ega_t *ega)
{
	unsigned int chr = ega->RDFFB;
	unsigned int chr_2 = ega->RDFSB;

	ega->RSTAT &= ~0x02;

	/* Check if the character code is in the Wide character set of Shift-JIS */
	if (((chr >= 0x40) && (chr <= 0x7e)) || ((chr >= 0x80) && (chr <= 0xfc)))
	{
		if (ega->font_index >= 32)
		{
			ega->font_index = 0;
		}
		chr <<= 8;
		/* Fix vertical character position */
		chr |= chr_2;
		if (ega->font_index < 16)
		{
			jfont_dbcs_16[(chr * 32) + (ega->font_index * 2)] = ega->RDFAP;				/* 16x16 font */
		}
		else
		{
			jfont_dbcs_16[(chr * 32) + ((ega->font_index - 16) * 2) + 1] = ega->RDFAP;		/* 16x16 font */
		}
	}
	else
	{
		if (ega->font_index >= 19)
		{
			ega->font_index = 0;
		}
		jfont_sbcs_19[(chr * 19) + ega->font_index] = ega->RDFAP;					/* 8x19 font */
	}
	ega->font_index++;
	ega->RSTAT |= 0x02;
}

void ega_jega_read_font(ega_t *ega)
{
	unsigned int chr = ega->RDFFB;
	unsigned int chr_2 = ega->RDFSB;

	ega->RSTAT &= ~0x02;

	/* Check if the character code is in the Wide character set of Shift-JIS */
	if (((chr >= 0x40) && (chr <= 0x7e)) || ((chr >= 0x80) && (chr <= 0xfc)))
	{
		if (ega->font_index >= 32)
		{
			ega->font_index = 0;
		}
		chr <<= 8;
		/* Fix vertical character position */
		chr |= chr_2;
		if (ega->font_index < 16)
		{
			ega->RDFAP = jfont_dbcs_16[(chr * 32) + (ega->font_index * 2)];				/* 16x16 font */
		}
		else
		{
			ega->RDFAP = jfont_dbcs_16[(chr * 32) + ((ega->font_index - 16) * 2) + 1];		/* 16x16 font */
		}
	}
	else
	{
		if (ega->font_index >= 19)
		{
			ega->font_index = 0;
		}
		ega->RDFAP = jfont_sbcs_19[(chr * 19) + ega->font_index];					/* 8x19 font */
	}
	ega->font_index++;
	ega->RSTAT |= 0x02;
}
#endif


void ega_out(uint16_t addr, uint8_t val, void *p)
{
        ega_t *ega = (ega_t *)p;
        int c;
        uint8_t o, old;
        
        if (((addr & 0xfff0) == 0x3d0 || (addr & 0xfff0) == 0x3b0) && !(ega->miscout & 1)) 
                addr ^= 0x60;
        
        switch (addr)
        {
                case 0x3c0:
		case 0x3c1:
                if (!ega->attrff)
                   ega->attraddr = val & 31;
                else
                {
						o = ega->attrregs[ega->attraddr & 31];
                        ega->attrregs[ega->attraddr & 31] = val;
                        if (ega->attraddr < 16) 
                                fullchange = changeframecount;
                        if (ega->attraddr == 0x10 || ega->attraddr == 0x14 || ega->attraddr < 0x10)
                        {
                                for (c = 0; c < 16; c++)
                                {
                                        if (ega->attrregs[0x10] & 0x80) ega->egapal[c] = (ega->attrregs[c] &  0xf) | ((ega->attrregs[0x14] & 0xf) << 4);
                                        else                            ega->egapal[c] = (ega->attrregs[c] & 0x3f) | ((ega->attrregs[0x14] & 0xc) << 4);
                                }
                        }
			/* Recalculate timings on change of attribute register 0x11
			   (overscan border color) too. */
			if ((ega->attraddr == 0x10) || (ega->attraddr == 0x11)) {
				ega->overscan_color = ega->vres ? pallook16[val & 0x0f] : pallook64[val & 0x3f];
				if (o != val)
					ega_recalctimings(ega);
			}
                }
                ega->attrff ^= 1;
                break;
                case 0x3c2:
				o = ega->miscout;
                egaswitchread = (val & 0xc) >> 2;
                ega->vres = !(val & 0x80);
                ega->pallook = ega->vres ? pallook16 : pallook64;
                ega->vidclock = val & 4; /*printf("3C2 write %02X\n",val);*/
                ega->miscout=val;
				ega->overscan_color = ega->vres ? pallook16[ega->attrregs[0x11] & 0x0f] : pallook64[ega->attrregs[0x11] & 0x3f];
				if ((o ^ val) & 0x80)
					ega_recalctimings(ega);
                break;
                case 0x3c4: 
                ega->seqaddr = val; 
                break;
                case 0x3c5:
                o = ega->seqregs[ega->seqaddr & 0xf];
                ega->seqregs[ega->seqaddr & 0xf] = val;
                if (o != val && (ega->seqaddr & 0xf) == 1) 
                        ega_recalctimings(ega);
                switch (ega->seqaddr & 0xf)
                {
                        case 1: 
                        if (ega->scrblank && !(val & 0x20)) 
                                fullchange = 3; 
                        ega->scrblank = (ega->scrblank & ~0x20) | (val & 0x20); 
                        break;
                        case 2: 
                        ega->writemask = val & 0xf; 
                        break;
                        case 3:
                        ega->charsetb = (((val >> 2) & 3) * 0x10000) + 2;
                        ega->charseta = ((val & 3)        * 0x10000) + 2;
                        break;
                        case 4:
                        ega->chain2_write = !(val & 4);
                        break;
                }
                break;
                case 0x3ce: 
                ega->gdcaddr = val; 
                break;
                case 0x3cf:
                ega->gdcreg[ega->gdcaddr & 15] = val;
                switch (ega->gdcaddr & 15)
                {
                        case 2: 
                        ega->colourcompare = val; 
                        break;
                        case 4: 
                        ega->readplane = val & 3; 
                        break;
                        case 5: 
                        ega->writemode = val & 3;
                        ega->readmode = val & 8; 
                        ega->chain2_read = val & 0x10;
                        break;
                        case 6:
                        switch (val & 0xc)
                        {
                                case 0x0: /*128k at A0000*/
                                mem_mapping_set_addr(&ega->mapping, 0xa0000, 0x20000);
                                break;
                                case 0x4: /*64k at A0000*/
                                mem_mapping_set_addr(&ega->mapping, 0xa0000, 0x10000);
                                break;
                                case 0x8: /*32k at B0000*/
                                mem_mapping_set_addr(&ega->mapping, 0xb0000, 0x08000);
                                break;
                                case 0xC: /*32k at B8000*/
                                mem_mapping_set_addr(&ega->mapping, 0xb8000, 0x08000);
                                break;
                        }
                        break;
                        case 7: 
                        ega->colournocare = val; 
                        break;
                }
                break;
		case 0x3d0:
                case 0x3d4:
                ega->crtcreg = val & 31;
                return;
		case 0x3d1:
                case 0x3d5:
                if (ega->crtcreg <= 7 && ega->crtc[0x11] & 0x80) return;
                old = ega->crtc[ega->crtcreg];
                ega->crtc[ega->crtcreg] = val;
                if (old != val)
                {
                        if (ega->crtcreg < 0xe || ega->crtcreg > 0x10)
                        {
                                fullchange = changeframecount;
                                ega_recalctimings(ega);
                        }
                }
                break;
        }
}

uint8_t ega_in(uint16_t addr, void *p)
{
        ega_t *ega = (ega_t *)p;

        if (((addr & 0xfff0) == 0x3d0 || (addr & 0xfff0) == 0x3b0) && !(ega->miscout & 1)) 
                addr ^= 0x60;

        switch (addr)
        {
                case 0x3c0: 
                return ega->attraddr;
                case 0x3c1: 
                return ega->attrregs[ega->attraddr];
                case 0x3c2:
		return (egaswitches & (8 >> egaswitchread)) ? 0x10 : 0x00;
                break;
                case 0x3c4: 
                return ega->seqaddr;
                case 0x3c5:
                return ega->seqregs[ega->seqaddr & 0xf];
		case 0x3c8:
		return 2;
                case 0x3cc: 
                return ega->miscout;
                case 0x3ce: 
                return ega->gdcaddr;
                case 0x3cf:
                return ega->gdcreg[ega->gdcaddr & 0xf];
		case 0x3d0:
                case 0x3d4:
                return ega->crtcreg;
		case 0x3d1:
                case 0x3d5:
                return ega->crtc[ega->crtcreg];
                case 0x3da:
                ega->attrff = 0;
                ega->stat ^= 0x30; /*Fools IBM EGA video BIOS self-test*/
                return ega->stat;
        }
        return 0xff;
}


void ega_recalctimings(ega_t *ega)
{
	double _dispontime, _dispofftime, disptime;
        double crtcconst;

        ega->vtotal = ega->crtc[6];
        ega->dispend = ega->crtc[0x12];
        ega->vsyncstart = ega->crtc[0x10];
        ega->split = ega->crtc[0x18];

        if (ega->crtc[7] & 1)  ega->vtotal |= 0x100;
        if (ega->crtc[7] & 32) ega->vtotal |= 0x200;
        ega->vtotal += 2;

        if (ega->crtc[7] & 2)  ega->dispend |= 0x100;
        if (ega->crtc[7] & 64) ega->dispend |= 0x200;
        ega->dispend++;

        if (ega->crtc[7] & 4)   ega->vsyncstart |= 0x100;
        if (ega->crtc[7] & 128) ega->vsyncstart |= 0x200;
        ega->vsyncstart++;

        if (ega->crtc[7] & 0x10) ega->split |= 0x100;
        if (ega->crtc[9] & 0x40) ega->split |= 0x200;
        ega->split++;

        ega->hdisp = ega->crtc[1];
        ega->hdisp++;

        ega->rowoffset = ega->crtc[0x13];
        ega->rowcount = ega->crtc[9] & 0x1f;

        if (ega->vidclock) crtcconst = (ega->seqregs[1] & 1) ? MDACONST : (MDACONST * (9.0 / 8.0));
        else               crtcconst = (ega->seqregs[1] & 1) ? CGACONST : (CGACONST * (9.0 / 8.0));

	if (enable_overscan) {
		overscan_y = (ega->rowcount + 1) << 1;

	        if (ega->seqregs[1] & 8) 
			overscan_y <<= 1;
		if (overscan_y < 16)
			overscan_y = 16;
        }

        if (ega->seqregs[1] & 8) {
	        disptime = (double) ((ega->crtc[0] + 2) << 1);
        	_dispontime = (double) ((ega->crtc[1] + 1) << 1);
        } else {
        	disptime = (double) (ega->crtc[0] + 2);
	        _dispontime = (double) (ega->crtc[1] + 1);
	}
        _dispofftime = disptime - _dispontime;
        _dispontime  = _dispontime * crtcconst;
        _dispofftime = _dispofftime * crtcconst;

	ega->dispontime  = (uint64_t)(_dispontime);
	ega->dispofftime = (uint64_t)(_dispofftime);
}


void ega_poll(void *p)
{
        ega_t *ega = (ega_t *)p;
        int x;
	int xs_temp, ys_temp;
        int drawcursor = 0;
	int y_add = enable_overscan ? (overscan_y >> 1) : 0;
	int x_add = enable_overscan ? 8 : 0;
	int y_add_ex = enable_overscan ? overscan_y : 0;
	int x_add_ex = enable_overscan ? 16 : 0;
	uint32_t *q, i, j;
	int wx = 640, wy = 350;

        if (!ega->linepos)
        {
                timer_advance_u64(&ega->timer, ega->dispofftime);

                ega->stat |= 1;
                ega->linepos = 1;

                if (ega->dispon)
                {
                        if (ega->firstline == 2000) 
                        {
                                ega->firstline = ega->displine;
                                video_wait_for_buffer();
                        }

                        if (ega->scrblank)
                        {
				ega_render_blank(ega);
                        }
                        else if (!(ega->gdcreg[6] & 1))
                        {
				if (fullchange)
				{
#ifdef JEGA
					if (ega_jega_enabled(ega))
					{
						ega_render_text_jega(ega, drawcursor);
					}
					else
					{
						ega_render_text_standard(ega, drawcursor);
					}
#else
					ega_render_text_standard(ega, drawcursor);
#endif
				}
                        }
                        else
                        {
                                switch (ega->gdcreg[5] & 0x20)
                                {
                                        case 0x00:
						if (ega->seqregs[1] & 8)
						{
							ega_render_4bpp_lowres(ega);
						}
						else
						{
							ega_render_4bpp_highres(ega);
						}
						break;
                                        case 0x20:
	                                        ega_render_2bpp(ega);
						break;
                                }
                        }
                        if (ega->lastline < ega->displine) 
                                ega->lastline = ega->displine;
                }

                ega->displine++;
                if (ega->interlace) 
                        ega->displine++;
                if ((ega->stat & 8) && ((ega->displine & 15) == (ega->crtc[0x11] & 15)) && ega->vslines)
                        ega->stat &= ~8;
                ega->vslines++;
                if (ega->displine > 500) 
                        ega->displine = 0;
        }
        else
        {
                timer_advance_u64(&ega->timer, ega->dispontime);
                if (ega->dispon) 
                        ega->stat &= ~1;
                ega->linepos = 0;
                if (ega->sc == (ega->crtc[11] & 31)) 
                   ega->con = 0; 
                if (ega->dispon)
                {
                        if (ega->sc == (ega->crtc[9] & 31))
                        {
                                ega->sc = 0;
                                if (ega->sc == (ega->crtc[11] & 31))
                                        ega->con = 0;

                                ega->maback += (ega->rowoffset << 3);
                                if (ega->interlace)
                                        ega->maback += (ega->rowoffset << 3);
                                ega->maback &= ega->vrammask;
                                ega->ma = ega->maback;
                        }
                        else
                        {
                                ega->sc++;
                                ega->sc &= 31;
                                ega->ma = ega->maback;
                        }
                }
                ega->vc++;
                ega->vc &= 1023;
                if (ega->vc == ega->split)
                {
                        ega->ma = ega->maback = 0;
                        if (ega->attrregs[0x10] & 0x20)
                                ega->scrollcache = 0;
                }
                if (ega->vc == ega->dispend)
                {
                        ega->dispon=0;
                        if (ega->crtc[10] & 0x20) ega->cursoron = 0;
                        else                      ega->cursoron = ega->blink & 16;
                        if (!(ega->gdcreg[6] & 1) && !(ega->blink & 15)) 
                                fullchange = 2;
                        ega->blink++;

                        if (fullchange) 
                                fullchange--;
                }
                if (ega->vc == ega->vsyncstart)
                {
                        ega->dispon = 0;
                        ega->stat |= 8;
                        if (ega->seqregs[1] & 8) x = ega->hdisp * ((ega->seqregs[1] & 1) ? 8 : 9) * 2;
                        else                     x = ega->hdisp * ((ega->seqregs[1] & 1) ? 8 : 9);

                        if (ega->interlace && !ega->oddeven) ega->lastline++;
                        if (ega->interlace &&  ega->oddeven) ega->firstline--;

			xs_temp = x;
			ys_temp = ega->lastline - ega->firstline + 1;

			if ((xs_temp > 0) && (ys_temp > 1)) {
				x_add = enable_overscan ? 8 : 0;
				y_add = enable_overscan ? overscan_y : 0;
				x_add_ex = enable_overscan ? 16 : 0;
				y_add_ex = y_add << 1;

				if ((xsize > 2032) || ((ysize + y_add_ex) > 2048)) {
					x_add = x_add_ex = 0;
					y_add = y_add_ex = 0;
					suppress_overscan = 1;
				} else
					suppress_overscan = 0;

				xs_temp = x;
				ys_temp = ega->lastline - ega->firstline + 1;
				if (xs_temp < 64)
					xs_temp = 640;
				if (ys_temp < 32)
					ys_temp = 200;

				if ((xs_temp != xsize) || (ys_temp != ysize) || video_force_resize_get()) {
	                                if (ega->vres)
        	                                set_screen_size(xsize + x_add_ex, (ysize << 1) + y_add_ex);
                	                else
                        	                set_screen_size(xsize + x_add_ex, ysize + y_add_ex);

					if (video_force_resize_get())
						video_force_resize_set(0);
                	        }

				if (enable_overscan && !suppress_overscan) {
					if ((x >= 160) && ((ega->lastline - ega->firstline + 1) >= 120)) {
						/* Draw (overscan_size - scroll size) lines of overscan on top. */
						for (i  = 0; i < y_add; i++) {
							q = &buffer32->line[i & 0x7ff][32];

							for (j = 0; j < (xsize + x_add_ex); j++)
								q[j] = ega->overscan_color;
						}

						/* Draw (overscan_size + scroll size) lines of overscan on the bottom. */
						for (i  = 0; i < y_add_ex; i++) {
							q = &buffer32->line[(ysize + y_add + i) & 0x7ff][32];

							for (j = 0; j < (xsize + x_add_ex); j++)
								q[j] = ega->overscan_color;
						}

						for (i = y_add_ex; i < (ysize + y_add); i ++) {
							q = &buffer32->line[i & 0x7ff][32];

							for (j = 0; j < x_add; j++) {
								q[j] = ega->overscan_color;
								q[xsize + x_add + j] = ega->overscan_color;
							}
						}
					}
				}

	                        video_blit_memtoscreen(32, 0, ega->firstline, ega->lastline + 1 + y_add_ex, xsize + x_add_ex, ega->lastline - ega->firstline + 1 + y_add_ex);
			}

                        frames++;
                        
                        ega->video_res_x = wx;
                        ega->video_res_y = wy + 1;
                        if (!(ega->gdcreg[6] & 1)) /*Text mode*/
                        {
                                ega->video_res_x /= (ega->seqregs[1] & 1) ? 8 : 9;
                                ega->video_res_y /= (ega->crtc[9] & 31) + 1;
                                ega->video_bpp = 0;
                        }
                        else
                        {
                                if (ega->crtc[9] & 0x80)
                                   ega->video_res_y /= 2;
                                if (!(ega->crtc[0x17] & 1))
                                   ega->video_res_y *= 2;
                                ega->video_res_y /= (ega->crtc[9] & 31) + 1;                                   
                                if (ega->seqregs[1] & 8)
                                   ega->video_res_x /= 2;
                                ega->video_bpp = (ega->gdcreg[5] & 0x20) ? 2 : 4;
                        }

                        ega->firstline = 2000;
                        ega->lastline = 0;

                        ega->maback = ega->ma = (ega->crtc[0xc] << 8)| ega->crtc[0xd];
                        ega->ca = (ega->crtc[0xe] << 8) | ega->crtc[0xf];
                        ega->ma <<= 2;
                        ega->maback <<= 2;
                        ega->ca <<= 2;
                        changeframecount = 2;
                        ega->vslines = 0;
                }
                if (ega->vc == ega->vtotal)
                {
                        ega->vc = 0;
                        ega->sc = ega->crtc[8] & 0x1f;
                        ega->dispon = 1;
                        ega->displine = (ega->interlace && ega->oddeven) ? 1 : 0;
                        ega->scrollcache = ega->attrregs[0x13] & 7;
                }
                if (ega->sc == (ega->crtc[10] & 31)) 
                        ega->con = 1;
        }
}


void ega_write(uint32_t addr, uint8_t val, void *p)
{
        ega_t *ega = (ega_t *)p;
        uint8_t vala, valb, valc, vald;
        int writemask2 = ega->writemask;

        egawrites++;
        sub_cycles(video_timing_write_b);
        
        if (addr >= 0xB0000) addr &= 0x7fff;
        else                 addr &= 0xffff;

        if (ega->chain2_write)
        {
                writemask2 &= ~0xa;
                if (addr & 1)
                        writemask2 <<= 1;
                addr &= ~1;
                if (addr & 0x4000)
                        addr |= 1;
                addr &= ~0x4000;
        }

        addr <<= 2;

        if (addr >= ega->vram_limit)
                return;

        if (!(ega->gdcreg[6] & 1)) 
                fullchange = 2;

        switch (ega->writemode)
        {
                case 1:
                if (writemask2 & 1) ega->vram[addr]       = ega->la;
                if (writemask2 & 2) ega->vram[addr | 0x1] = ega->lb;
                if (writemask2 & 4) ega->vram[addr | 0x2] = ega->lc;
                if (writemask2 & 8) ega->vram[addr | 0x3] = ega->ld;
                break;
                case 0:
                if (ega->gdcreg[3] & 7) 
                        val = ega_rotate[ega->gdcreg[3] & 7][val];
                        
                if (ega->gdcreg[8] == 0xff && !(ega->gdcreg[3] & 0x18) && !ega->gdcreg[1])
                {
                        if (writemask2 & 1) ega->vram[addr]       = val;
                        if (writemask2 & 2) ega->vram[addr | 0x1] = val;
                        if (writemask2 & 4) ega->vram[addr | 0x2] = val;
                        if (writemask2 & 8) ega->vram[addr | 0x3] = val;
                }
                else
                {
                        if (ega->gdcreg[1] & 1) vala = (ega->gdcreg[0] & 1) ? 0xff : 0;
                        else                    vala = val;
                        if (ega->gdcreg[1] & 2) valb = (ega->gdcreg[0] & 2) ? 0xff : 0;
                        else                    valb = val;
                        if (ega->gdcreg[1] & 4) valc = (ega->gdcreg[0] & 4) ? 0xff : 0;
                        else                    valc = val;
                        if (ega->gdcreg[1] & 8) vald = (ega->gdcreg[0] & 8) ? 0xff : 0;
                        else                    vald = val;
                        switch (ega->gdcreg[3] & 0x18)
                        {
                                case 0: /*Set*/
                                if (writemask2 & 1) ega->vram[addr]       = (vala & ega->gdcreg[8]) | (ega->la & ~ega->gdcreg[8]);
                                if (writemask2 & 2) ega->vram[addr | 0x1] = (valb & ega->gdcreg[8]) | (ega->lb & ~ega->gdcreg[8]);
                                if (writemask2 & 4) ega->vram[addr | 0x2] = (valc & ega->gdcreg[8]) | (ega->lc & ~ega->gdcreg[8]);
                                if (writemask2 & 8) ega->vram[addr | 0x3] = (vald & ega->gdcreg[8]) | (ega->ld & ~ega->gdcreg[8]);
                                break;
                                case 8: /*AND*/
                                if (writemask2 & 1) ega->vram[addr]       = (vala | ~ega->gdcreg[8]) & ega->la;
                                if (writemask2 & 2) ega->vram[addr | 0x1] = (valb | ~ega->gdcreg[8]) & ega->lb;
                                if (writemask2 & 4) ega->vram[addr | 0x2] = (valc | ~ega->gdcreg[8]) & ega->lc;
                                if (writemask2 & 8) ega->vram[addr | 0x3] = (vald | ~ega->gdcreg[8]) & ega->ld;
                                break;
                                case 0x10: /*OR*/
                                if (writemask2 & 1) ega->vram[addr]       = (vala & ega->gdcreg[8]) | ega->la;
                                if (writemask2 & 2) ega->vram[addr | 0x1] = (valb & ega->gdcreg[8]) | ega->lb;
                                if (writemask2 & 4) ega->vram[addr | 0x2] = (valc & ega->gdcreg[8]) | ega->lc;
                                if (writemask2 & 8) ega->vram[addr | 0x3] = (vald & ega->gdcreg[8]) | ega->ld;
                                break;
                                case 0x18: /*XOR*/
                                if (writemask2 & 1) ega->vram[addr]       = (vala & ega->gdcreg[8]) ^ ega->la;
                                if (writemask2 & 2) ega->vram[addr | 0x1] = (valb & ega->gdcreg[8]) ^ ega->lb;
                                if (writemask2 & 4) ega->vram[addr | 0x2] = (valc & ega->gdcreg[8]) ^ ega->lc;
                                if (writemask2 & 8) ega->vram[addr | 0x3] = (vald & ega->gdcreg[8]) ^ ega->ld;
                                break;
                        }
                }
                break;
                case 2:
                if (!(ega->gdcreg[3] & 0x18) && !ega->gdcreg[1])
                {
                        if (writemask2 & 1) ega->vram[addr]       = (((val & 1) ? 0xff : 0) & ega->gdcreg[8]) | (ega->la & ~ega->gdcreg[8]);
                        if (writemask2 & 2) ega->vram[addr | 0x1] = (((val & 2) ? 0xff : 0) & ega->gdcreg[8]) | (ega->lb & ~ega->gdcreg[8]);
                        if (writemask2 & 4) ega->vram[addr | 0x2] = (((val & 4) ? 0xff : 0) & ega->gdcreg[8]) | (ega->lc & ~ega->gdcreg[8]);
                        if (writemask2 & 8) ega->vram[addr | 0x3] = (((val & 8) ? 0xff : 0) & ega->gdcreg[8]) | (ega->ld & ~ega->gdcreg[8]);
                }
                else
                {
                        vala = ((val & 1) ? 0xff : 0);
                        valb = ((val & 2) ? 0xff : 0);
                        valc = ((val & 4) ? 0xff : 0);
                        vald = ((val & 8) ? 0xff : 0);
                        switch (ega->gdcreg[3] & 0x18)
                        {
                                case 0: /*Set*/
                                if (writemask2 & 1) ega->vram[addr]       = (vala & ega->gdcreg[8]) | (ega->la & ~ega->gdcreg[8]);
                                if (writemask2 & 2) ega->vram[addr | 0x1] = (valb & ega->gdcreg[8]) | (ega->lb & ~ega->gdcreg[8]);
                                if (writemask2 & 4) ega->vram[addr | 0x2] = (valc & ega->gdcreg[8]) | (ega->lc & ~ega->gdcreg[8]);
                                if (writemask2 & 8) ega->vram[addr | 0x3] = (vald & ega->gdcreg[8]) | (ega->ld & ~ega->gdcreg[8]);
                                break;
                                case 8: /*AND*/
                                if (writemask2 & 1) ega->vram[addr]       = (vala | ~ega->gdcreg[8]) & ega->la;
                                if (writemask2 & 2) ega->vram[addr | 0x1] = (valb | ~ega->gdcreg[8]) & ega->lb;
                                if (writemask2 & 4) ega->vram[addr | 0x2] = (valc | ~ega->gdcreg[8]) & ega->lc;
                                if (writemask2 & 8) ega->vram[addr | 0x3] = (vald | ~ega->gdcreg[8]) & ega->ld;
                                break;
                                case 0x10: /*OR*/
                                if (writemask2 & 1) ega->vram[addr]       = (vala & ega->gdcreg[8]) | ega->la;
                                if (writemask2 & 2) ega->vram[addr | 0x1] = (valb & ega->gdcreg[8]) | ega->lb;
                                if (writemask2 & 4) ega->vram[addr | 0x2] = (valc & ega->gdcreg[8]) | ega->lc;
                                if (writemask2 & 8) ega->vram[addr | 0x3] = (vald & ega->gdcreg[8]) | ega->ld;
                                break;
                                case 0x18: /*XOR*/
                                if (writemask2 & 1) ega->vram[addr]       = (vala & ega->gdcreg[8]) ^ ega->la;
                                if (writemask2 & 2) ega->vram[addr | 0x1] = (valb & ega->gdcreg[8]) ^ ega->lb;
                                if (writemask2 & 4) ega->vram[addr | 0x2] = (valc & ega->gdcreg[8]) ^ ega->lc;
                                if (writemask2 & 8) ega->vram[addr | 0x3] = (vald & ega->gdcreg[8]) ^ ega->ld;
                                break;
                        }
                }
                break;
        }
}


uint8_t ega_read(uint32_t addr, void *p)
{
        ega_t *ega = (ega_t *)p;
        uint8_t temp, temp2, temp3, temp4;
        int readplane = ega->readplane;
        
        egareads++;
        sub_cycles(video_timing_read_b);
        if (addr >= 0xb0000) addr &= 0x7fff;
        else                 addr &= 0xffff;

        if (ega->chain2_read)
        {
                readplane = (readplane & 2) | (addr & 1);
                addr &= ~1;
                if (addr & 0x4000)
                        addr |= 1;
                addr &= ~0x4000;
        }

        addr <<= 2;

        if (addr >= ega->vram_limit)
                return 0xff;

        ega->la = ega->vram[addr];
        ega->lb = ega->vram[addr | 0x1];
        ega->lc = ega->vram[addr | 0x2];
        ega->ld = ega->vram[addr | 0x3];
        if (ega->readmode)
        {
                temp   = ega->la;
                temp  ^= (ega->colourcompare & 1) ? 0xff : 0;
                temp  &= (ega->colournocare & 1)  ? 0xff : 0;
                temp2  = ega->lb;
                temp2 ^= (ega->colourcompare & 2) ? 0xff : 0;
                temp2 &= (ega->colournocare & 2)  ? 0xff : 0;
                temp3  = ega->lc;
                temp3 ^= (ega->colourcompare & 4) ? 0xff : 0;
                temp3 &= (ega->colournocare & 4)  ? 0xff : 0;
                temp4  = ega->ld;
                temp4 ^= (ega->colourcompare & 8) ? 0xff : 0;
                temp4 &= (ega->colournocare & 8)  ? 0xff : 0;
                return ~(temp | temp2 | temp3 | temp4);
        }
        return ega->vram[addr | readplane];
}


void ega_init(ega_t *ega, int monitor_type, int is_mono)
{
        int c, d, e;
        
        ega->vram = malloc(0x40000);
        ega->vrammask = 0x3ffff;
        
        for (c = 0; c < 256; c++)
        {
                e = c;
                for (d = 0; d < 8; d++)
                {
                        ega_rotate[d][c] = e;
                        e = (e >> 1) | ((e & 1) ? 0x80 : 0);
                }
        }

        for (c = 0; c < 4; c++)
        {
                for (d = 0; d < 4; d++)
                {
                        edatlookup[c][d] = 0;
                        if (c & 1) edatlookup[c][d] |= 1;
                        if (d & 1) edatlookup[c][d] |= 2;
                        if (c & 2) edatlookup[c][d] |= 0x10;
                        if (d & 2) edatlookup[c][d] |= 0x20;
                }
        }

        if (is_mono)
        {
                for (c = 0; c < 256; c++)
                {
                        switch (monitor_type >> 4)
                        {
                                case DISPLAY_GREEN:
                                switch ((c >> 3) & 3)
                                {
                                        case 0:
                                        pallook64[c] = pallook16[c] = makecol32(0, 0, 0);
                                        break;
                                        case 2:
                                        pallook64[c] = pallook16[c] = makecol32(0x04, 0x8a, 0x20);
                                        break;
                                        case 1:
                                        pallook64[c] = pallook16[c] = makecol32(0x08, 0xc7, 0x2c);
                                        break;
                                        case 3:
                                        pallook64[c] = pallook16[c] = makecol32(0x34, 0xff, 0x5d);
                                        break;
                                }
                                break;
                                case DISPLAY_AMBER:
                                switch ((c >> 3) & 3)
                                {
                                        case 0:
                                        pallook64[c] = pallook16[c] = makecol32(0, 0, 0);
                                        break;
                                        case 2:
                                        pallook64[c] = pallook16[c] = makecol32(0xb2, 0x4d, 0x00);
                                        break;
                                        case 1:
                                        pallook64[c] = pallook16[c] = makecol32(0xef, 0x79, 0x00);
                                        break;
                                        case 3:
                                        pallook64[c] = pallook16[c] = makecol32(0xff, 0xe3, 0x34);
                                        break;
                                }
                                break;
                                case DISPLAY_WHITE: default:
                                switch ((c >> 3) & 3)
                                {
                                        case 0:
                                        pallook64[c] = pallook16[c] = makecol32(0, 0, 0);
                                        break;
                                        case 2:
                                        pallook64[c] = pallook16[c] = makecol32(0x7a, 0x81, 0x83);
                                        break;
                                        case 1:
                                        pallook64[c] = pallook16[c] = makecol32(0xaf, 0xb3, 0xb0);
                                        break;
                                        case 3:
                                        pallook64[c] = pallook16[c] = makecol32(0xff, 0xfd, 0xed);
                                        break;
                                }
                                break;
                        }
                }
        }
        else
        {
                for (c = 0; c < 256; c++)
                {
                        pallook64[c]  = makecol32(((c >> 2) & 1) * 0xaa, ((c >> 1) & 1) * 0xaa, (c & 1) * 0xaa);
                        pallook64[c] += makecol32(((c >> 5) & 1) * 0x55, ((c >> 4) & 1) * 0x55, ((c >> 3) & 1) * 0x55);
                        pallook16[c]  = makecol32(((c >> 2) & 1) * 0xaa, ((c >> 1) & 1) * 0xaa, (c & 1) * 0xaa);
                        pallook16[c] += makecol32(((c >> 4) & 1) * 0x55, ((c >> 4) & 1) * 0x55, ((c >> 4) & 1) * 0x55);
                        if ((c & 0x17) == 6) 
                                pallook16[c] = makecol32(0xaa, 0x55, 0);
                }
        }
        ega->pallook = pallook16;

        egaswitches = monitor_type & 0xf;

        ega->vram_limit = 256 * 1024;
        ega->vrammask = ega->vram_limit - 1;

	old_overscan_color = 0;

	ega->miscout |= 0x22;
	ega->oddeven_page = 0;

	ega->seqregs[4] |= 2;
	ega->extvram = 1;

	update_overscan = 0;

        ega->crtc[0] = 63;
        ega->crtc[6] = 255;

#ifdef JEGA
	ega->is_jega = 0;
#endif

        timer_add(&ega->timer, ega_poll, ega, 1);
}


static void *ega_standalone_init(const device_t *info)
{
        ega_t *ega = malloc(sizeof(ega_t));
        int monitor_type;

        memset(ega, 0, sizeof(ega_t));

	video_inform(VIDEO_FLAG_TYPE_SPECIAL, &timing_ega);

	overscan_x = 16;
	overscan_y = 28;

	switch(info->local) {
		case EGA_IBM:
		default:
        		rom_init(&ega->bios_rom, BIOS_IBM_PATH,
				 0xc0000, 0x8000, 0x7fff, 0, MEM_MAPPING_EXTERNAL);
			break;
		case EGA_COMPAQ:
        		rom_init(&ega->bios_rom, BIOS_CPQ_PATH,
				 0xc0000, 0x8000, 0x7fff, 0, MEM_MAPPING_EXTERNAL);
			break;
		case EGA_SUPEREGA:
        		rom_init(&ega->bios_rom, BIOS_SEGA_PATH,
				 0xc0000, 0x8000, 0x7fff, 0, MEM_MAPPING_EXTERNAL);
			break;
	}

        if (ega->bios_rom.rom[0x3ffe] == 0xaa && ega->bios_rom.rom[0x3fff] == 0x55)
        {
                int c;

                for (c = 0; c < 0x2000; c++)
                {
                        uint8_t temp = ega->bios_rom.rom[c];
                        ega->bios_rom.rom[c] = ega->bios_rom.rom[0x3fff - c];
                        ega->bios_rom.rom[0x3fff - c] = temp;
                }
        }

        monitor_type = device_get_config_int("monitor_type");
        ega_init(ega, monitor_type, (monitor_type & 0x0F) == 0x0B);

        ega->vram_limit = device_get_config_int("memory") * 1024;
        ega->vrammask = ega->vram_limit - 1;

        mem_mapping_add(&ega->mapping, 0xa0000, 0x20000, ega_read, NULL, NULL, ega_write, NULL, NULL, NULL, MEM_MAPPING_EXTERNAL, ega);
        io_sethandler(0x03a0, 0x0040, ega_in, NULL, NULL, ega_out, NULL, NULL, ega);
        return ega;
}


static int ega_standalone_available(void)
{
        return rom_present(BIOS_IBM_PATH);
}


static int cpqega_standalone_available(void)
{
        return rom_present(BIOS_CPQ_PATH);
}


static int sega_standalone_available(void)
{
        return rom_present(BIOS_SEGA_PATH);
}


static void ega_close(void *p)
{
        ega_t *ega = (ega_t *)p;

        free(ega->vram);
        free(ega);
}


static void ega_speed_changed(void *p)
{
        ega_t *ega = (ega_t *)p;
        
        ega_recalctimings(ega);
}


/* SW1 SW2 SW3 SW4
   OFF OFF  ON OFF	Monochrome			(5151)		1011	0x0B
    ON OFF OFF  ON	Color 40x25			(5153)		0110	0x06
   OFF OFF OFF  ON	Color 80x25			(5153)		0111	0x07
    ON  ON  ON OFF	Enhanced Color - Normal Mode	(5154)		1000	0x08
   OFF  ON  ON OFF	Enhanced Color - Enhanced Mode	(5154)		1001	0x09

   0 = Switch closed (ON);
   1 = Switch open   (OFF). */
static const device_config_t ega_config[] =
{
        {
                "memory", "Memory size", CONFIG_SELECTION, "", 256,
                {
                        {
                                "64 kB", 64
                        },
                        {
                                "128 kB", 128
                        },
                        {
                                "256 kB", 256
                        },
                        {
                                ""
                        }
                }
        },
        {
                .name = "monitor_type",
                .description = "Monitor type",
                .type = CONFIG_SELECTION,
                .selection =
                {
                        {
                                .description = "Monochrome (5151/MDA) (white)",
                                .value = 0x0B | (DISPLAY_WHITE << 4)
                        },
                        {
                                .description = "Monochrome (5151/MDA) (green)",
                                .value = 0x0B | (DISPLAY_GREEN << 4)
                        },
                        {
                                .description = "Monochrome (5151/MDA) (amber)",
                                .value = 0x0B | (DISPLAY_AMBER << 4)
                        },
                        {
                                .description = "Color 40x25 (5153/CGA)",
                                .value = 0x06
                        },
                        {
                                .description = "Color 80x25 (5153/CGA)",
                                .value = 0x07
                        },
                        {
                                .description = "Enhanced Color - Normal Mode (5154/ECD)",
                                .value = 0x08
                        },
                        {
                               .description = "Enhanced Color - Enhanced Mode (5154/ECD)",
                                .value = 0x09
                        },
                        {
                                .description = ""
                        }
                },
                .default_int = 9
        },
        {
                .type = -1
        }
};


const device_t ega_device =
{
        "EGA",
        DEVICE_ISA,
	EGA_IBM,
        ega_standalone_init, ega_close, NULL,
        ega_standalone_available,
        ega_speed_changed,
        NULL,
        ega_config
};

const device_t cpqega_device =
{
        "Compaq EGA",
        DEVICE_ISA,
	EGA_COMPAQ,
        ega_standalone_init, ega_close, NULL,
        cpqega_standalone_available,
        ega_speed_changed,
        NULL,
        ega_config
};

const device_t sega_device =
{
        "SuperEGA",
        DEVICE_ISA,
	EGA_SUPEREGA,
        ega_standalone_init, ega_close, NULL,
        sega_standalone_available,
        ega_speed_changed,
        NULL,
        ega_config
};

#ifdef JEGA
const device_t jega_device =
{
        "AX JEGA",
        DEVICE_ISA,
	EGA_SUPEREGA,
        ega_standalone_init, ega_close, NULL,
        sega_standalone_available,
        ega_speed_changed,
        NULL,
        ega_config
};
#endif
