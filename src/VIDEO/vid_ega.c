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
 * Version:	@(#)vid_ega.c	1.0.3	2017/07/21
 *
 * Author:	Sarah Walker, <http://pcem-emulator.co.uk/>
 *		Miran Grca, <mgrca8@gmail.com>
 *		akm,
 *		Copyright 2008-2017 Sarah Walker.
 *		Copyright 2016-2017 Miran Grca.
 *		Copyright 2017-2017 akm.
 */

#include <stdint.h>
#include <stdlib.h>
#include "../ibm.h"
#include "../io.h"
#include "../mem.h"
#include "../rom.h"
#include "../timer.h"
#include "../device.h"
#include "video.h"
#include "vid_ega.h"
#include "vid_ega_render.h"


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
	uint8_t crtcreg;
        
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
                }
                ega->attrff ^= 1;
                break;
                case 0x3c2:
                egaswitchread = val & 0xc;
                ega->vres = !(val & 0x80);
                ega->pallook = ega->vres ? pallook16 : pallook64;
                ega->vidclock = val & 4; /*printf("3C2 write %02X\n",val);*/
                ega->miscout=val;
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
                ega->crtcreg = val;
                return;
		case 0x3d1:
                case 0x3d5:
#ifdef JEGA
		if ((ega->crtcreg < 0xb9) || !ega->is_jega)
#else
		if (ega->crtcreg < 0xb9)
#endif
		{
			crtcreg = ega->crtcreg & 0x1f;
	                if (crtcreg <= 7 && ega->crtc[0x11] & 0x80) return;
        	        old = ega->crtc[crtcreg];
                	ega->crtc[crtcreg] = val;
	                if (old != val)
        	        {
                	        if (crtcreg < 0xe || crtcreg > 0x10)
                        	{
                                	fullchange = changeframecount;
	                                ega_recalctimings(ega);
        	                }
                	}
		}
#ifdef JEGA
		else
		{
			switch(ega->crtcreg)
			{
				case 0xb9:	/* Mode register 1 */
					ega->RMOD1 = val;
					break;
				case 0xba:	/* Mode register 2 */
					ega->RMOD2 = val;
					break;
				case 0xbb:	/* ANK Group sel */
					ega->RDAGS = val;
					break;
				case 0xbc:	/* Font access first byte */
					if (ega->RDFFB != val)
					{
						ega->RDFFB = val;
						ega->font_index = 0;
					}
					break;
				case 0xbd:	/* Font access Second Byte */
					if (ega->RDFSB != val)
					{
						ega->RDFSB = val;
						ega->font_index = 0;
					}
					break;
				case 0xbe:	/* Font Access Pattern */
					ega->RDFAP = val;
					ega_jega_write_font(ega);
					break;
				case 0xdb:
					ega->RPSSC = val;
					break;
				case 0xd9:
					ega->RPSSU = val;
					break;
				case 0xda:
					ega->RPSSL = val;
					break;
				case 0xdc:	/* Superimposed mode (only AX-2 system, not implemented) */
					ega->RPPAJ = val;
					break;
				case 0xdd:
					ega->RCMOD = val;
					break;
				case 0xde:	/* Cursor Skew control */
					ega->RCSKW = val;
					break;
				case 0xdf:	/* Font R/W register */
					ega->RSTAT = val;
					break;
				default:
					pclog("JEGA: Write to illegal index %02X\n", ega->crtcreg);
					break;
			}
		}
#endif
                break;
        }
}

/*
 * Get the input status register 0
 *
 * Note by Tohka: Code from PCE.
 */
static uint8_t ega_get_input_status_0(ega_t *ega)
{
	unsigned bit;
	uint8_t status0 = 0;

	bit = (egaswitchread >> 2) & 3;

	if (egaswitches & (0x08 >> bit)) {
		status0 |= 0x10;
	}
	else {
		status0 &= ~0x10;
	}

	return status0;
}

uint8_t ega_in(uint16_t addr, void *p)
{
        ega_t *ega = (ega_t *)p;
	int crtcreg;

        if (((addr & 0xfff0) == 0x3d0 || (addr & 0xfff0) == 0x3b0) && !(ega->miscout & 1)) 
                addr ^= 0x60;

        switch (addr)
        {
                case 0x3c0: 
                return ega->attraddr;
                case 0x3c1: 
                return ega->attrregs[ega->attraddr];
                case 0x3c2:
		return ega_get_input_status_0(ega);
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
#ifdef JEGA
		if ((ega->crtcreg < 0xb9) || !ega->is_jega)
#else
		if (ega->crtcreg < 0xb9)
#endif
		{
			crtcreg = ega->crtcreg & 0x1f;
	                return ega->crtc[crtcreg];
		}
#ifdef JEGA
		else
		{
			switch(ega->crtcreg)
			{
				case 0xb9:
					return ega->RMOD1;
				case 0xba:
					return ega->RMOD2;
				case 0xbb:
					return ega->RDAGS;
				case 0xbc:	/* BCh RDFFB Font access First Byte */
					return ega->RDFFB;
				case 0xbd:	/* BDh RDFFB Font access Second Byte */
					return ega->RDFSB;
				case 0xbe:	/* BEh RDFAP Font Access Pattern */
					ega_jega_read_font(ega);
					return ega->RDFAP;
				case 0xdb:
					return ega->RPSSC;
				case 0xd9:
					return ega->RPSSU;
				case 0xda:
					return ega->RPSSL;
				case 0xdc:
					return ega->RPPAJ;
				case 0xdd:
					return ega->RCMOD;
				case 0xde:
					return ega->RCSKW;
				case 0xdf:
					return ega->ROMSL;
				case 0xbf:
					return 0x03;	/* The font is always readable and writable */
				default:
					pclog("JEGA: Read from illegal index %02X\n", ega->crtcreg);
					return 0x00;
			}
		}
#endif
		return 0xff;
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
	overscan_y = (ega->rowcount + 1) << 1;

        if (ega->vidclock) crtcconst = (ega->seqregs[1] & 1) ? MDACONST : (MDACONST * (9.0 / 8.0));
        else               crtcconst = (ega->seqregs[1] & 1) ? CGACONST : (CGACONST * (9.0 / 8.0));

        disptime = ega->crtc[0] + 2;
        _dispontime = ega->crtc[1] + 1;

        if (ega->seqregs[1] & 8) 
        { 
                disptime*=2; 
                _dispontime*=2;
		overscan_y <<= 1;
        }
	if (overscan_y < 16)
	{
		overscan_y = 16;
	}
        _dispofftime = disptime - _dispontime;
        _dispontime  *= crtcconst;
        _dispofftime *= crtcconst;

	ega->dispontime  = (int)(_dispontime  * (1 << TIMER_SHIFT));
	ega->dispofftime = (int)(_dispofftime * (1 << TIMER_SHIFT));
}

void ega_poll(void *p)
{
        ega_t *ega = (ega_t *)p;
        int x;
        int drawcursor = 0;
	int y_add = enable_overscan ? (overscan_y >> 1) : 0;
	int x_add = enable_overscan ? 8 : 0;
	int y_add_ex = enable_overscan ? overscan_y : 0;
	int x_add_ex = enable_overscan ? 16 : 0;
	uint32_t *q, i, j;
	int wx = 640, wy = 350;

        if (!ega->linepos)
        {
                ega->vidtime += ega->dispofftime;

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
						if (ega->seqregs[1] & 8)
						{
							ega_render_2bpp_lowres(ega);
						}
						else
						{
							ega_render_2bpp_highres(ega);
						}
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
                ega->vidtime += ega->dispontime;
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

                        if ((x != xsize || (ega->lastline - ega->firstline + 1) != ysize) || update_overscan)
                        {
                                xsize = x;
                                ysize = ega->lastline - ega->firstline + 1;
                                if (xsize < 64) xsize = 640;
                                if (ysize < 32) ysize = 200;
				y_add = enable_overscan ? 14 : 0;
				x_add = enable_overscan ? 8 : 0;
				y_add_ex = enable_overscan ? 28 : 0;
				x_add_ex = enable_overscan ? 16 : 0;

				if ((xsize > 2032) || ((ysize + y_add_ex) > 2048))
				{
					x_add = x_add_ex = 0;
					y_add = y_add_ex = 0;
					suppress_overscan = 1;
				}
				else
				{
					suppress_overscan = 0;
				}

                                if (ega->vres)
                                        updatewindowsize(xsize + x_add_ex, (ysize << 1) + y_add_ex);
                                else
                                        updatewindowsize(xsize + x_add_ex, ysize + y_add_ex);
                        }

			if (enable_overscan)
			{
				if ((x >= 160) && ((ega->lastline - ega->firstline) >= 120))
				{
					/* Draw (overscan_size - scroll size) lines of overscan on top. */
					for (i  = 0; i < (y_add - (ega->crtc[8] & 0x1f)); i++)
					{
						q = &((uint32_t *)buffer32->line[i & 0x7ff])[32];

						for (j = 0; j < (xsize + x_add_ex); j++)
						{
							q[j] = ega->pallook[ega->attrregs[0x11]];
						}
					}

					/* Draw (overscan_size + scroll size) lines of overscan on the bottom. */
					for (i  = 0; i < (y_add + (ega->crtc[8] & 0x1f)); i++)
					{
						q = &((uint32_t *)buffer32->line[(ysize + y_add + i - (ega->crtc[8] & 0x1f)) & 0x7ff])[32];

						for (j = 0; j < (xsize + x_add_ex); j++)
						{
							q[j] = ega->pallook[ega->attrregs[0x11]];
						}
					}

					for (i = (y_add - (ega->crtc[8] & 0x1f)); i < (ysize + y_add - (ega->crtc[8] & 0x1f)); i ++)
					{
						q = &((uint32_t *)buffer32->line[(i - (ega->crtc[8] & 0x1f)) & 0x7ff])[32];

						for (j = 0; j < x_add; j++)
						{
							q[j] = ega->pallook[ega->attrregs[0x11]];
							q[xsize + x_add + j] = ega->pallook[ega->attrregs[0x11]];
						}
					}
				}
			}
			else
			{
				if (ega->crtc[8] & 0x1f)
				{
					/* Draw (scroll size) lines of overscan on the bottom. */
					for (i  = 0; i < (ega->crtc[8] & 0x1f); i++)
					{
						q = &((uint32_t *)buffer32->line[(ysize + i - (ega->crtc[8] & 0x1f)) & 0x7ff])[32];

						for (j = 0; j < xsize; j++)
						{
							q[j] = ega->pallook[ega->attrregs[0x11]];
						}
					}
				}
			}
         
                        video_blit_memtoscreen(32, 0, ega->firstline, ega->lastline + 1 + y_add_ex, xsize + x_add_ex, ega->lastline - ega->firstline + 1 + y_add_ex);

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
                        ega->sc = 0;
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
        cycles -= video_timing_b;
        cycles_lost += video_timing_b;
        
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
        cycles -= video_timing_b;
        cycles_lost += video_timing_b;
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
                temp   = (ega->colournocare & 1)  ? 0xff : 0;
                temp  &= ega->la;
                temp  ^= (ega->colourcompare & 1) ? 0xff : 0;
                temp2  = (ega->colournocare & 2)  ? 0xff : 0;
                temp2 &= ega->lb;
                temp2 ^= (ega->colourcompare & 2) ? 0xff : 0;
                temp3  = (ega->colournocare & 4)  ? 0xff : 0;
                temp3 &= ega->lc;
                temp3 ^= (ega->colourcompare & 4) ? 0xff : 0;
                temp4  = (ega->colournocare & 8)  ? 0xff : 0;
                temp4 &= ega->ld;
                temp4 ^= (ega->colourcompare & 8) ? 0xff : 0;
                return ~(temp | temp2 | temp3 | temp4);
        }
        return ega->vram[addr | readplane];
}

void ega_init(ega_t *ega)
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

        for (c = 0; c < 256; c++)
        {
                pallook64[c]  = makecol32(((c >> 2) & 1) * 0xaa, ((c >> 1) & 1) * 0xaa, (c & 1) * 0xaa);
                pallook64[c] += makecol32(((c >> 5) & 1) * 0x55, ((c >> 4) & 1) * 0x55, ((c >> 3) & 1) * 0x55);
                pallook16[c]  = makecol32(((c >> 2) & 1) * 0xaa, ((c >> 1) & 1) * 0xaa, (c & 1) * 0xaa);
                pallook16[c] += makecol32(((c >> 4) & 1) * 0x55, ((c >> 4) & 1) * 0x55, ((c >> 4) & 1) * 0x55);
                if ((c & 0x17) == 6) 
                        pallook16[c] = makecol32(0xaa, 0x55, 0);
        }
        ega->pallook = pallook16;

        ega->vram_limit = 256 * 1024;
        ega->vrammask = ega->vram_limit-1;

	old_overscan_color = 0;
}

void ega_common_defaults(ega_t *ega)
{
	ega->miscout |= 0x22;
	ega->enablevram = 1;
	ega->oddeven_page = 0;

	ega->seqregs[4] |= 2;
	ega->extvram = 1;

	update_overscan = 0;

#ifdef JEGA
	ega->is_jega = 0;
#endif
}

void *ega_standalone_init()
{
        ega_t *ega = malloc(sizeof(ega_t));
        memset(ega, 0, sizeof(ega_t));
        
	overscan_x = 16;
	overscan_y = 28;

        rom_init(&ega->bios_rom, L"roms/video/ega/ibm_6277356_ega_card_u44_27128.bin", 0xc0000, 0x8000, 0x7fff, 0, MEM_MAPPING_EXTERNAL);

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

        ega->crtc[0] = 63;
        ega->dispontime = 1000 * (1 << TIMER_SHIFT);
        ega->dispofftime = 1000 * (1 << TIMER_SHIFT);
	ega->dispontime <<= 1;
	ega->dispofftime <<= 1;

        ega_init(ega);        

	ega_common_defaults(ega);

        ega->vram_limit = device_get_config_int("memory") * 1024;
        ega->vrammask = ega->vram_limit-1;

        mem_mapping_add(&ega->mapping, 0xa0000, 0x20000, ega_read, NULL, NULL, ega_write, NULL, NULL, NULL, MEM_MAPPING_EXTERNAL, ega);
        timer_add(ega_poll, &ega->vidtime, TIMER_ALWAYS_ENABLED, ega);
        io_sethandler(0x03c0, 0x0020, ega_in, NULL, NULL, ega_out, NULL, NULL, ega);
        return ega;
}

void *cpqega_standalone_init()
{
        ega_t *ega = malloc(sizeof(ega_t));
        memset(ega, 0, sizeof(ega_t));
        
	overscan_x = 16;
	overscan_y = 28;

        rom_init(&ega->bios_rom, L"roms/video/ega/108281-001.bin", 0xc0000, 0x8000, 0x7fff, 0, MEM_MAPPING_EXTERNAL);

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

        ega->crtc[0] = 63;
        ega->dispontime = 1000 * (1 << TIMER_SHIFT);
        ega->dispofftime = 1000 * (1 << TIMER_SHIFT);

        ega_init(ega);        

	ega_common_defaults(ega);

        ega->vram_limit = device_get_config_int("memory") * 1024;
        ega->vrammask = ega->vram_limit-1;

        mem_mapping_add(&ega->mapping, 0xa0000, 0x20000, ega_read, NULL, NULL, ega_write, NULL, NULL, NULL, MEM_MAPPING_EXTERNAL, ega);
        timer_add(ega_poll, &ega->vidtime, TIMER_ALWAYS_ENABLED, ega);
        io_sethandler(0x03c0, 0x0020, ega_in, NULL, NULL, ega_out, NULL, NULL, ega);
        return ega;
}

void *sega_standalone_init()
{
        ega_t *ega = malloc(sizeof(ega_t));
        memset(ega, 0, sizeof(ega_t));
        
	overscan_x = 16;
	overscan_y = 28;

        rom_init(&ega->bios_rom, L"roms/video/ega/lega.vbi", 0xc0000, 0x8000, 0x7fff, 0, MEM_MAPPING_EXTERNAL);

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

        ega->crtc[0] = 63;
        ega->dispontime = 1000 * (1 << TIMER_SHIFT);
        ega->dispofftime = 1000 * (1 << TIMER_SHIFT);

        ega_init(ega);        

	ega_common_defaults(ega);

        ega->vram_limit = device_get_config_int("memory") * 1024;
        ega->vrammask = ega->vram_limit-1;

        mem_mapping_add(&ega->mapping, 0xa0000, 0x20000, ega_read, NULL, NULL, ega_write, NULL, NULL, NULL, MEM_MAPPING_EXTERNAL, ega);
        timer_add(ega_poll, &ega->vidtime, TIMER_ALWAYS_ENABLED, ega);
        io_sethandler(0x03c0, 0x0020, ega_in, NULL, NULL, ega_out, NULL, NULL, ega);
        return ega;
}

#ifdef JEGA
uint16_t chrtosht(FILE *fp)
{
	uint16_t i, j;
	i = (uint8_t) getc(fp);
	j = (uint8_t) getc(fp) << 8;
	return (i | j);
}

unsigned int getfontx2header(FILE *fp, fontx_h *header)
{
	fread(header->id, ID_LEN, 1, fp);
	if (strncmp(header->id, "FONTX2", ID_LEN) != 0)
	{
		return 1;
	}
	fread(header->name, NAME_LEN, 1, fp);
	header->width = (uint8_t)getc(fp);
	header->height = (uint8_t)getc(fp);
	header->type = (uint8_t)getc(fp);
	return 0;
}

void readfontxtbl(fontxTbl *table, unsigned int size, FILE *fp)
{
	while (size > 0)
	{
		table->start = chrtosht(fp);
		table->end = chrtosht(fp);
		++table;
		--size;
	}
}

static void LoadFontxFile(wchar_t *fname)
{
	fontx_h head;
	fontxTbl *table;
	unsigned int code;
	uint8_t size;
	unsigned int i;

	if (!fname) return;
	if(*fname=='\0') return;
	FILE * mfile=romfopen(fname,L"rb");
	if (!mfile)
	{
		pclog("MSG: Can't open FONTX2 file: %s\n",fname);
		return;
	}
	if (getfontx2header(mfile, &head) != 0)
	{
		fclose(mfile);
		pclog("MSG: FONTX2 header is incorrect\n");
		return;
	}
	/* switch whether the font is DBCS or not */
	if (head.type == DBCS)
	{
		if (head.width == 16 && head.height == 16)
		{
			size = getc(mfile);
			table = (fontxTbl *)calloc(size, sizeof(fontxTbl));
			readfontxtbl(table, size, mfile);
			for (i = 0; i < size; i++)
			{
				for (code = table[i].start; code <= table[i].end; code++)
				{
					fread(&jfont_dbcs_16[(code * 32)], sizeof(uint8_t), 32, mfile);
				}
			}
		}
		else
		{
			fclose(mfile);
			pclog("MSG: FONTX2 DBCS font size is not correct\n");
			return;
		}
	}
	else
	{
		if (head.width == 8 && head.height == 19)
		{
			fread(jfont_sbcs_19, sizeof(uint8_t), SBCS19_LEN, mfile);
		}
		else
		{
			fclose(mfile);
			pclog("MSG: FONTX2 SBCS font size is not correct\n");
			return;
		}
	}
	fclose(mfile);
}

void *jega_standalone_init()
{
        ega_t *ega = (ega_t *) sega_standalone_init();

	LoadFontxFile(L"roms/video/ega/JPNHN19X.FNT");
	LoadFontxFile(L"roms/video/ega/JPNZN16X.FNT");

	ega->is_jega = 1;

	return ega;
}
#endif

static int ega_standalone_available()
{
        return rom_present(L"roms/video/ega/ibm_6277356_ega_card_u44_27128.bin");
}

static int cpqega_standalone_available()
{
        return rom_present(L"roms/video/ega/108281-001.bin");
}

static int sega_standalone_available()
{
        return rom_present(L"roms/video/ega/lega.vbi");
}

void ega_close(void *p)
{
        ega_t *ega = (ega_t *)p;

        free(ega->vram);
        free(ega);
}

void ega_speed_changed(void *p)
{
        ega_t *ega = (ega_t *)p;
        
        ega_recalctimings(ega);
}

static device_config_t ega_config[] =
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
                "", "", -1
        }
};

device_t ega_device =
{
        "EGA",
        0,
        ega_standalone_init,
        ega_close,
        ega_standalone_available,
        ega_speed_changed,
        NULL,
        NULL,
        ega_config
};

device_t cpqega_device =
{
        "Compaq EGA",
        0,
        cpqega_standalone_init,
        ega_close,
        cpqega_standalone_available,
        ega_speed_changed,
        NULL,
        NULL,
        ega_config
};

device_t sega_device =
{
        "SuperEGA",
        0,
        sega_standalone_init,
        ega_close,
        sega_standalone_available,
        ega_speed_changed,
        NULL,
        NULL,
        ega_config
};

#ifdef JEGA
device_t jega_device =
{
        "AX JEGA",
        0,
        jega_standalone_init,
        ega_close,
        sega_standalone_available,
        ega_speed_changed,
        NULL,
        NULL,
        ega_config
};
#endif
