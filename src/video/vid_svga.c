/*
 * 86Box	A hypervisor and IBM PC system emulator that specializes in
 *		running old operating systems and software designed for IBM
 *		PC systems and compatibles from 1981 through fairly recent
 *		system designs based on the PCI bus.
 *
 *		This file is part of the 86Box distribution.
 *
 *		Generic SVGA handling.
 *
 *		This is intended to be used by another SVGA driver,
 *		and not as a card in it's own right.
 *
 * Version:	@(#)vid_svga.c	1.0.35	2018/10/21
 *
 * Authors:	Sarah Walker, <http://pcem-emulator.co.uk/>
 *		Miran Grca, <mgrca8@gmail.com>
 *
 *		Copyright 2008-2018 Sarah Walker.
 *		Copyright 2016-2018 Miran Grca.
 */
#include <inttypes.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <stdlib.h>
#include <wchar.h>
#include "../86box.h"
#include "../cpu/cpu.h"
#include "../machine/machine.h"
#include "../timer.h"
#include "../io.h"
#include "../pit.h"
#include "../mem.h"
#include "../rom.h"
#include "video.h"
#include "vid_svga.h"
#include "vid_svga_render.h"


#define svga_output 0

void svga_doblit(int y1, int y2, int wx, int wy, svga_t *svga);

extern int	cyc_total;
extern uint8_t	edatlookup[4][4];

uint8_t		svga_rotate[8][256];

static const uint32_t mask16[16] = {
    0x00000000, 0x000000ff, 0x0000ff00, 0x0000ffff,
    0x00ff0000, 0x00ff00ff, 0x00ffff00, 0x00ffffff,
    0xff000000, 0xff0000ff, 0xff00ff00, 0xff00ffff,
    0xffff0000, 0xffff00ff, 0xffffff00, 0xffffffff
};

/*Primary SVGA device. As multiple video cards are not yet supported this is the
  only SVGA device.*/
static svga_t	*svga_pri;


svga_t
*svga_get_pri()
{
    return svga_pri;
}


void
svga_set_override(svga_t *svga, int val)
{
    if (svga->override && !val)
	svga->fullchange = changeframecount;
    svga->override = val;
}


void
svga_out(uint16_t addr, uint8_t val, void *p)
{
    svga_t *svga = (svga_t *)p;
    int c;
    uint8_t o, index;

    switch (addr) {
	case 0x3c0:
	case 0x3c1:
		if (!svga->attrff) {
			svga->attraddr = val & 31;
			if ((val & 0x20) != svga->attr_palette_enable) {
				svga->fullchange = 3;
				svga->attr_palette_enable = val & 0x20;
				svga_recalctimings(svga);
			}
		} else {
			if ((svga->attraddr == 0x13) && (svga->attrregs[0x13] != val))
				svga->fullchange = changeframecount;
			o = svga->attrregs[svga->attraddr & 31];
			svga->attrregs[svga->attraddr & 31] = val;
			if (svga->attraddr < 16) 
				svga->fullchange = changeframecount;
			if (svga->attraddr == 0x10 || svga->attraddr == 0x14 || svga->attraddr < 0x10) {
				for (c = 0; c < 16; c++) {
					if (svga->attrregs[0x10] & 0x80) {
						svga->egapal[c] = (svga->attrregs[c] &  0xf) |
								  ((svga->attrregs[0x14] & 0xf) << 4);
					} else {
						svga->egapal[c] = (svga->attrregs[c] & 0x3f) |
								  ((svga->attrregs[0x14] & 0xc) << 4);
					}
				}
			}
			/* Recalculate timings on change of attribute register 0x11
			   (overscan border color) too. */
			if (svga->attraddr == 0x10) {
				if (o != val)
					svga_recalctimings(svga);
			} else if (svga->attraddr == 0x11) {
				svga->overscan_color = svga->pallook[svga->attrregs[0x11]];
				if (o != val)
					svga_recalctimings(svga);
			} else if (svga->attraddr == 0x12) {
				if ((val & 0xf) != svga->plane_mask)
					svga->fullchange = changeframecount;
				svga->plane_mask = val & 0xf;
			}
		}
		svga->attrff ^= 1;
		break;
	case 0x3c2:
		svga->miscout = val;
		svga->vidclock = val & 4;
		io_removehandler(0x03a0, 0x0020, svga->video_in, NULL, NULL, svga->video_out, NULL, NULL, svga->p);
		if (!(val & 1))
			io_sethandler(0x03a0, 0x0020, svga->video_in, NULL, NULL, svga->video_out, NULL, NULL, svga->p);
		svga_recalctimings(svga);
		break;
	case 0x3c4: 
		svga->seqaddr = val; 
		break;
	case 0x3c5:
		if (svga->seqaddr > 0xf)
			return;
		o = svga->seqregs[svga->seqaddr & 0xf];
		svga->seqregs[svga->seqaddr & 0xf] = val;
		if (o != val && (svga->seqaddr & 0xf) == 1)
			svga_recalctimings(svga);
		switch (svga->seqaddr & 0xf) {
			case 1:
				if (svga->scrblank && !(val & 0x20)) 
					svga->fullchange = 3; 
				svga->scrblank = (svga->scrblank & ~0x20) | (val & 0x20); 
				svga_recalctimings(svga);
				break;
			case 2:
				svga->writemask = val & 0xf; 
				break;
			case 3:
				svga->charsetb = (((val >> 2) & 3) * 0x10000) + 2;
				svga->charseta = ((val & 3)  * 0x10000) + 2;
				if (val & 0x10)
					svga->charseta += 0x8000;
				if (val & 0x20)
					svga->charsetb += 0x8000;
				break;
			case 4: 
				svga->chain2_write = !(val & 4);
				svga->chain4 = val & 8;
				svga->fast = (svga->gdcreg[8] == 0xff && !(svga->gdcreg[3] & 0x18) &&
					      !svga->gdcreg[1]) && svga->chain4;
				break;
		}
		break;
	case 0x3c6: 
		svga->dac_mask = val; 
		break;
	case 0x3c7:
	case 0x3c8:
		svga->dac_pos = 0;
		svga->dac_status = addr & 0x03;
		svga->dac_addr = (val + (addr & 0x01)) & 255;
		break;
	case 0x3c9:
		svga->fullchange = changeframecount;
		switch (svga->dac_pos) {
			case 0:
				svga->dac_r = val;
				svga->dac_pos++; 
				break;
			case 1:
				svga->dac_g = val;
				svga->dac_pos++; 
				break;
                        case 2:
				index = svga->dac_addr & 255;
				svga->vgapal[index].r = svga->dac_r;
				svga->vgapal[index].g = svga->dac_g;
				svga->vgapal[index].b = val; 
				if (svga->ramdac_type == RAMDAC_8BIT)
					svga->pallook[index] = makecol32(svga->vgapal[index].r, svga->vgapal[index].g, svga->vgapal[index].b);
				else
					svga->pallook[index] = makecol32(video_6to8[svga->vgapal[index].r & 0x3f], video_6to8[svga->vgapal[index].g & 0x3f], video_6to8[svga->vgapal[index].b & 0x3f]);
				svga->dac_pos = 0; 
				svga->dac_addr = (svga->dac_addr + 1) & 255; 
				break;
		}
		break;
	case 0x3ce:
		svga->gdcaddr = val; 
		break;
	case 0x3cf:
		o = svga->gdcreg[svga->gdcaddr & 15];
		switch (svga->gdcaddr & 15) {
			case 2:
				svga->colourcompare=val;
				break;
			case 4:
				svga->readplane = val & 3;
				break;
			case 5:
				svga->writemode = val & 3;
				svga->readmode = val & 8;
				svga->chain2_read = val & 0x10;
				break;
			case 6:
				if ((svga->gdcreg[6] & 0xc) != (val & 0xc)) {
					switch (val&0xC) {
						case 0x0: /*128k at A0000*/
							mem_mapping_set_addr(&svga->mapping, 0xa0000, 0x20000);
							svga->banked_mask = 0xffff;
							break;
						case 0x4: /*64k at A0000*/
							mem_mapping_set_addr(&svga->mapping, 0xa0000, 0x10000);
							svga->banked_mask = 0xffff;
							break;
						case 0x8: /*32k at B0000*/
							mem_mapping_set_addr(&svga->mapping, 0xb0000, 0x08000);
							svga->banked_mask = 0x7fff;
							break;
						case 0xC: /*32k at B8000*/
							mem_mapping_set_addr(&svga->mapping, 0xb8000, 0x08000);
							svga->banked_mask = 0x7fff;
							break;
					}
				}
				break;
			case 7:
				svga->colournocare=val;
				break;
		}
		svga->gdcreg[svga->gdcaddr & 15] = val;                
		svga->fast = (svga->gdcreg[8] == 0xff && !(svga->gdcreg[3] & 0x18) &&
			     !svga->gdcreg[1]) && svga->chain4;
		if (((svga->gdcaddr & 15) == 5  && (val ^ o) & 0x70) ||
		    ((svga->gdcaddr & 15) == 6 && (val ^ o) & 1))
			svga_recalctimings(svga);
		break;
    }
}


uint8_t
svga_in(uint16_t addr, void *p)
{
    svga_t *svga = (svga_t *)p;
    uint8_t index, ret = 0xff;

    switch (addr) {
	case 0x3c0:
		ret = svga->attraddr | svga->attr_palette_enable;
		break;
	case 0x3c1:
		ret = svga->attrregs[svga->attraddr];
		break;
	case 0x3c2:
		if ((svga->vgapal[0].r + svga->vgapal[0].g + svga->vgapal[0].b) >= 0x4e)
			ret = 0;
		else
			ret = 0x10;
		break;
	case 0x3c4:
		ret = svga->seqaddr;
		break;
	case 0x3c5:
		ret = svga->seqregs[svga->seqaddr & 0x0f];
		break;
	case 0x3c6:
		ret = svga->dac_mask;
		break;
	case 0x3c7:
		ret = svga->dac_status;
		break;
	case 0x3c8:
		ret = svga->dac_addr;
		break;
	case 0x3c9:
		index = (svga->dac_addr - 1) & 255;
		switch (svga->dac_pos) {
			case 0:
				svga->dac_pos++; 
				if (svga->ramdac_type == RAMDAC_8BIT)
					ret = svga->vgapal[index].r;
				else
					ret = svga->vgapal[index].r & 0x3f;
				break;
			case 1:
				svga->dac_pos++; 
				if (svga->ramdac_type == RAMDAC_8BIT)
					ret = svga->vgapal[index].g;
				else
					ret = svga->vgapal[index].g & 0x3f;
				break;
			case 2:
				svga->dac_pos=0; 
				svga->dac_addr = (svga->dac_addr + 1) & 255; 
				if (svga->ramdac_type == RAMDAC_8BIT)
					ret = svga->vgapal[index].b;
				else
					ret = svga->vgapal[index].b & 0x3f;
				break;
		}
		break;
	case 0x3cc:
		ret = svga->miscout;
		break;
	case 0x3ce:
		ret = svga->gdcaddr;
		break;
	case 0x3cf:
		/* The spec says GDC addresses 0xF8 to 0xFB return the latch. */
		switch(svga->gdcaddr) {
			case 0xf8:
				ret = (svga->latch & 0xFF);
				break;
			case 0xf9:
				ret = ((svga->latch & 0xFF00) >> 8);
				break;
			case 0xfa:
				ret = ((svga->latch & 0xFF0000) >> 16);
				break;
			case 0xfb:
				ret = ((svga->latch & 0xFF000000) >> 24);
				break;
			default:
                		ret = svga->gdcreg[svga->gdcaddr & 0xf];
				break;
		}
		break;
	case 0x3da:
		svga->attrff = 0;

		if (svga->cgastat & 0x01)
			svga->cgastat &= ~0x30;
		else
			svga->cgastat ^= 0x30;
		ret = svga->cgastat;
		break;
    }

    return(ret);
}


void
svga_set_ramdac_type(svga_t *svga, int type)
{
    int c;

    if (svga->ramdac_type != type) {
	svga->ramdac_type = type;

	for (c = 0; c < 256; c++) {
		if (svga->ramdac_type == RAMDAC_8BIT)
			svga->pallook[c] = makecol32(svga->vgapal[c].r, svga->vgapal[c].g, svga->vgapal[c].b);
		else
			svga->pallook[c] = makecol32((svga->vgapal[c].r & 0x3f) * 4,
						     (svga->vgapal[c].g & 0x3f) * 4,
						     (svga->vgapal[c].b & 0x3f) * 4);
	}
    }
}


void
svga_recalctimings(svga_t *svga)
{
    double crtcconst, _dispontime, _dispofftime, disptime;

    svga->vtotal = svga->crtc[6];
    svga->dispend = svga->crtc[0x12];
    svga->vsyncstart = svga->crtc[0x10];
    svga->split = svga->crtc[0x18];
    svga->vblankstart = svga->crtc[0x15];

    if (svga->crtc[7] & 1)
	svga->vtotal |= 0x100;
    if (svga->crtc[7] & 32)
	svga->vtotal |= 0x200;
    svga->vtotal += 2;

    if (svga->crtc[7] & 2)
	svga->dispend |= 0x100;
    if (svga->crtc[7] & 64)
	svga->dispend |= 0x200;
    svga->dispend++;

    if (svga->crtc[7] & 4)
	svga->vsyncstart |= 0x100;
    if (svga->crtc[7] & 128)
	svga->vsyncstart |= 0x200;
    svga->vsyncstart++;

    if (svga->crtc[7] & 0x10)
	svga->split|=0x100;
    if (svga->crtc[9] & 0x40)
	svga->split|=0x200;
    svga->split++;

    if (svga->crtc[7] & 0x08)
	svga->vblankstart |= 0x100;
    if (svga->crtc[9] & 0x20)
	svga->vblankstart |= 0x200;
    svga->vblankstart++;

    svga->hdisp = svga->crtc[1];
    svga->hdisp++;

    svga->htotal = svga->crtc[0];
    svga->htotal += 6;	/*+6 is required for Tyrian*/

    svga->rowoffset = svga->crtc[0x13];

    svga->clock = (svga->vidclock) ? VGACONST2 : VGACONST1;

    svga->lowres = svga->attrregs[0x10] & 0x40;

    svga->interlace = 0;

    svga->ma_latch = (svga->crtc[0xc] << 8) | svga->crtc[0xd];

    svga->hdisp_time = svga->hdisp;
    svga->render = svga_render_blank;
    if (!svga->scrblank && svga->attr_palette_enable) {
	if (!(svga->gdcreg[6] & 1) && !(svga->attrregs[0x10] & 1)) { /*Text mode*/
		if (svga->seqregs[1] & 8) /*40 column*/ {
			svga->render = svga_render_text_40;
			svga->hdisp *= (svga->seqregs[1] & 1) ? 16 : 18;
		} else {
			svga->render = svga_render_text_80;
			svga->hdisp *= (svga->seqregs[1] & 1) ? 8 : 9;
		}
		svga->hdisp_old = svga->hdisp;
	} else {
		svga->hdisp *= (svga->seqregs[1] & 8) ? 16 : 8;
		svga->hdisp_old = svga->hdisp;                        

		switch (svga->gdcreg[5] & 0x60) {
			case 0x00: 
				if (svga->seqregs[1] & 8) /*Low res (320)*/
					svga->render = svga_render_4bpp_lowres;
				else
					svga->render = svga_render_4bpp_highres;
				break;
			case 0x20:		/*4 colours*/
				if (svga->seqregs[1] & 8) /*Low res (320)*/
					svga->render = svga_render_2bpp_lowres;
				else
					svga->render = svga_render_2bpp_highres;
				break;
			case 0x40: case 0x60:	/*256+ colours*/
				switch (svga->bpp) {
					case 8:
						if (svga->lowres)
							svga->render = svga_render_8bpp_lowres;
						else
							svga->render = svga_render_8bpp_highres;
						break;
					case 15:
						if (svga->lowres)
							svga->render = svga_render_15bpp_lowres;
						else
							svga->render = svga_render_15bpp_highres;
						break;
					case 16:
						if (svga->lowres)
							svga->render = svga_render_16bpp_lowres;
						else
							svga->render = svga_render_16bpp_highres;
						break;
					case 24:
						if (svga->lowres)
							svga->render = svga_render_24bpp_lowres;
						else
							svga->render = svga_render_24bpp_highres;
						break;
					case 32:
						if (svga->lowres)
							svga->render = svga_render_32bpp_lowres;
						else
							svga->render = svga_render_32bpp_highres;
						break;
				}
				break;
		}
	}
    }

    svga->linedbl = svga->crtc[9] & 0x80;
    svga->rowcount = svga->crtc[9] & 31;
    if (enable_overscan) {
	overscan_y = (svga->rowcount + 1) << 1;
	if (svga->seqregs[1] & 8)	/*Low res (320)*/
		overscan_y <<= 1;
	if (overscan_y < 16)
		overscan_y = 16;
    }
    if (svga->recalctimings_ex) 
	svga->recalctimings_ex(svga);

    if (svga->vblankstart < svga->dispend)
	svga->dispend = svga->vblankstart;

    crtcconst = (svga->seqregs[1] & 1) ? (svga->clock * 8.0) : (svga->clock * 9.0);

    disptime  = svga->htotal;
    _dispontime = svga->hdisp_time;

    if (svga->seqregs[1] & 8) {
	disptime *= 2;
	_dispontime *= 2;
    }
    _dispofftime = disptime - _dispontime;
    _dispontime *= crtcconst;
    _dispofftime *= crtcconst;

    svga->dispontime = (uint64_t)(_dispontime);
    svga->dispofftime = (uint64_t)(_dispofftime);
    if (svga->dispontime < TIMER_USEC)
	svga->dispontime = TIMER_USEC;
    if (svga->dispofftime < TIMER_USEC)
	svga->dispofftime = TIMER_USEC;
}


void
svga_poll(void *p)
{
    svga_t *svga = (svga_t *)p;
    uint32_t x;
    int wx, wy;

    if (!svga->linepos) {
	if (svga->displine == svga->hwcursor_latch.y && svga->hwcursor_latch.ena) {
		svga->hwcursor_on = 64 - svga->hwcursor_latch.yoff;
		svga->hwcursor_oddeven = 0;
	}

	if (svga->displine == (svga->hwcursor_latch.y + 1) && svga->hwcursor_latch.ena &&
			       svga->interlace) {
		svga->hwcursor_on = 64 - (svga->hwcursor_latch.yoff + 1);
		svga->hwcursor_oddeven = 1;
	}

	if (svga->displine == svga->dac_hwcursor_latch.y && svga->dac_hwcursor_latch.ena) {
		svga->dac_hwcursor_on = 64 - svga->dac_hwcursor_latch.yoff;
		svga->dac_hwcursor_oddeven = 0;
	}

	if (svga->displine == (svga->dac_hwcursor_latch.y + 1) && svga->dac_hwcursor_latch.ena &&
			       svga->interlace) {
		svga->dac_hwcursor_on = 64 - (svga->dac_hwcursor_latch.yoff + 1);
		svga->dac_hwcursor_oddeven = 1;
	}

	if (svga->displine == svga->overlay_latch.y && svga->overlay_latch.ena) {
		svga->overlay_on = svga->overlay_latch.ysize - svga->overlay_latch.yoff;
		svga->overlay_oddeven = 0;
	}

	if (svga->displine == svga->overlay_latch.y+1 && svga->overlay_latch.ena && svga->interlace) {
		svga->overlay_on = svga->overlay_latch.ysize - svga->overlay_latch.yoff;
		svga->overlay_oddeven = 1;
	}

	timer_advance_u64(&svga->timer, svga->dispofftime);
	svga->cgastat |= 1;
	svga->linepos = 1;

	if (svga->dispon) {
		svga->hdisp_on=1;

		svga->ma &= svga->vram_display_mask;
		if (svga->firstline == 2000) {
			svga->firstline = svga->displine;
			video_wait_for_buffer();
		}

		if (svga->hwcursor_on || svga->dac_hwcursor_on || svga->overlay_on) {
			svga->changedvram[svga->ma >> 12] = svga->changedvram[(svga->ma >> 12) + 1] =
							    svga->interlace ? 3 : 2;
		}

		if (!svga->override)
			svga->render(svga);

		if (svga->overlay_on) {
			if (!svga->override && svga->overlay_draw)
				svga->overlay_draw(svga, svga->displine);
			svga->overlay_on--;
			if (svga->overlay_on && svga->interlace)
				svga->overlay_on--;
		}

		if (svga->dac_hwcursor_on) {
			if (!svga->override && svga->dac_hwcursor_draw)
				svga->dac_hwcursor_draw(svga, svga->displine);
			svga->dac_hwcursor_on--;
			if (svga->dac_hwcursor_on && svga->interlace)
				svga->dac_hwcursor_on--;
		}

		if (svga->hwcursor_on) {
			if (!svga->override && svga->hwcursor_draw)
				svga->hwcursor_draw(svga, svga->displine);
			svga->hwcursor_on--;
			if (svga->hwcursor_on && svga->interlace)
				svga->hwcursor_on--;
		}

		if (svga->lastline < svga->displine) 
			svga->lastline = svga->displine;
	}

	svga->displine++;
	if (svga->interlace) 
		svga->displine++;
	if ((svga->cgastat & 8) && ((svga->displine & 15) == (svga->crtc[0x11] & 15)) && svga->vslines)
		svga->cgastat &= ~8;
	svga->vslines++;
	if (svga->displine > 1500)
		svga->displine = 0;
    } else {
	timer_advance_u64(&svga->timer, svga->dispontime);

	if (svga->dispon) 
		svga->cgastat &= ~1;
	svga->hdisp_on = 0;

	svga->linepos = 0;
	if (svga->sc == (svga->crtc[11] & 31)) 
		svga->con = 0;
	if (svga->dispon) {
		if (svga->linedbl && !svga->linecountff) {
			svga->linecountff = 1;
			svga->ma = svga->maback;
		} else if (svga->sc == svga->rowcount) {
			svga->linecountff = 0;
			svga->sc = 0;

			svga->maback += (svga->rowoffset << 3);
			if (svga->interlace)
				svga->maback += (svga->rowoffset << 3);
			svga->maback &= svga->vram_display_mask;
			svga->ma = svga->maback;
		} else {
			svga->linecountff = 0;
			svga->sc++;
			svga->sc &= 31;
			svga->ma = svga->maback;
		}
	}
	svga->vc++;
	svga->vc &= 2047;

	if (svga->vc == svga->split) {
		svga->ma = svga->maback = 0;
		svga->sc = 0;
		if (svga->attrregs[0x10] & 0x20) 
			svga->scrollcache = 0;
	}
	if (svga->vc == svga->dispend) {
		if (svga->vblank_start)
			svga->vblank_start(svga);
		svga->dispon=0;
		if (svga->crtc[10] & 0x20)
			svga->cursoron = 0;
		else
			svga->cursoron = svga->blink & 16;

		if (!(svga->gdcreg[6] & 1) && !(svga->blink & 15)) 
			svga->fullchange = 2;
		svga->blink++;

		for (x = 0; x < ((svga->vram_mask + 1) >> 12); x++) {
			if (svga->changedvram[x]) 
				svga->changedvram[x]--;
		}
		if (svga->fullchange) 
			svga->fullchange--;
	}
	if (svga->vc == svga->vsyncstart) {
		svga->dispon = 0;
		svga->cgastat |= 8;
		x = svga->hdisp;

		if (svga->interlace && !svga->oddeven)
			svga->lastline++;
		if (svga->interlace &&  svga->oddeven)
			svga->firstline--;

		wx = x;
		wy = svga->lastline - svga->firstline;

		if (!svga->override)
			svga_doblit(svga->firstline_draw, svga->lastline_draw + 1, wx, wy, svga);

		svga->firstline = 2000;
		svga->lastline = 0;

		svga->firstline_draw = 2000;
		svga->lastline_draw = 0;

		svga->oddeven ^= 1;

		changeframecount = svga->interlace ? 3 : 2;
		svga->vslines = 0;

		if (svga->interlace && svga->oddeven)
			svga->ma = svga->maback = svga->ma_latch + (svga->rowoffset << 1);
		else
			svga->ma = svga->maback = svga->ma_latch;
		svga->ca = (svga->crtc[0xe] << 8) | svga->crtc[0xf];

		svga->ma <<= 2;
		svga->maback <<= 2;
		svga->ca <<= 2;
	}
	if (svga->vc == svga->vtotal) {
		svga->vc = 0;
		svga->sc = svga->crtc[8] & 0x1f;
		svga->dispon = 1;
		svga->displine = (svga->interlace && svga->oddeven) ? 1 : 0;
		svga->scrollcache = svga->attrregs[0x13] & 7;
		svga->linecountff = 0;

		svga->hwcursor_on = 0;
		svga->hwcursor_latch = svga->hwcursor;

		svga->dac_hwcursor_on = 0;
		svga->dac_hwcursor_latch = svga->dac_hwcursor;

		svga->overlay_on = 0;
		svga->overlay_latch = svga->overlay;
	}
	if (svga->sc == (svga->crtc[10] & 31)) 
		svga->con = 1;
    }
}


int
svga_init(svga_t *svga, void *p, int memsize, 
	  void (*recalctimings_ex)(struct svga_t *svga),
	  uint8_t (*video_in) (uint16_t addr, void *p),
	  void    (*video_out)(uint16_t addr, uint8_t val, void *p),
	  void (*hwcursor_draw)(struct svga_t *svga, int displine),
	  void (*overlay_draw)(struct svga_t *svga, int displine))
{
    int c, d, e;

    svga->p = p;

    for (c = 0; c < 256; c++) {
	e = c;
	for (d = 0; d < 8; d++) {
		svga_rotate[d][c] = e;
		e = (e >> 1) | ((e & 1) ? 0x80 : 0);
	}
    }
    svga->readmode = 0;

    svga->attrregs[0x11] = 0;
    svga->overscan_color = 0x000000;

    overscan_x = 16;
    overscan_y = 32;

    svga->crtc[0] = 63;
    svga->crtc[6] = 255;
    svga->dispontime = 1000ull << 32;
    svga->dispofftime = 1000ull << 32;
    svga->bpp = 8;
    svga->vram = malloc(memsize);
    svga->vram_max = memsize;
    svga->vram_display_mask = svga->vram_mask = memsize - 1;
    svga->decode_mask = 0x7fffff;
    svga->changedvram = malloc(memsize >> 12);
    svga->recalctimings_ex = recalctimings_ex;
    svga->video_in  = video_in;
    svga->video_out = video_out;
    svga->hwcursor_draw = hwcursor_draw;
    svga->overlay_draw = overlay_draw;

    svga->hwcursor.xsize = svga->hwcursor.ysize = 32;
    svga->hwcursor.yoff = 32;

    svga->dac_hwcursor.xsize = svga->dac_hwcursor.ysize = 32;
    svga->dac_hwcursor.yoff = 32;

    mem_mapping_add(&svga->mapping, 0xa0000, 0x20000,
		    svga_read, svga_readw, svga_readl,
		    svga_write, svga_writew, svga_writel,
		    NULL, MEM_MAPPING_EXTERNAL, svga);

    timer_add(&svga->timer, svga_poll, svga, 1);

    svga_pri = svga;

    svga->ramdac_type = RAMDAC_6BIT;

    return 0;
}


void
svga_close(svga_t *svga)
{
    free(svga->changedvram);
    free(svga->vram);

    svga_pri = NULL;
}


void
svga_write_common(uint32_t addr, uint8_t val, uint8_t linear, void *p)
{
    svga_t *svga = (svga_t *)p;

    int func_select, writemask2 = svga->writemask;
    int memory_map_mode;
    uint32_t write_mask, bit_mask, set_mask, val32 = (uint32_t) val;

    egawrites++;

    sub_cycles(video_timing_write_b);

    if (!linear) {
	memory_map_mode = (svga->gdcreg[6] >> 2) & 3;

	addr &= 0x1ffff;

	switch (memory_map_mode) {
		case 0:
			break;
		case 1:
			if (addr >= 0x10000)
				return;
			addr += svga->write_bank;
			break;
		case 2:
			addr -= 0x10000;
			if (addr >= 0x8000)
				return;
			break;
		default:
		case 3:
			addr -= 0x18000;
			if (addr >= 0x8000)
				return;
			break;
	}
    }

    if (!(svga->gdcreg[6] & 1))
	svga->fullchange = 2;

    if ((svga->chain4 || svga->fb_only) && (svga->writemode < 4)) {
	writemask2 = 1 << (addr & 3);
	addr &= ~3;
    } else if (svga->chain2_write) {
	writemask2 &= ~0xa;
	if (addr & 1)
		writemask2 <<= 1;
	addr &= ~1;
	addr <<= 2;
    } else
	addr <<= 2;
    addr &= svga->decode_mask;

    if (addr >= svga->vram_max)
	return;

    addr &= svga->vram_mask;

    svga->changedvram[addr >> 12] = changeframecount;

    /* standard VGA latched access */
    func_select = (svga->gdcreg[3] >> 3) & 3;

    switch (svga->writemode) {
	case 0:
		/* rotate */
                if (svga->gdcreg[3] & 7)
                        val32 = svga_rotate[svga->gdcreg[3] & 7][val32];

		/* apply set/reset mask */
		bit_mask = svga->gdcreg[8];

		val32 |= (val32 << 8);
		val32 |= (val32 << 16);

		if (svga->gdcreg[8] == 0xff && !(svga->gdcreg[3] & 0x18) &&
		    (!svga->gdcreg[1] || svga->set_reset_disabled)) {
			/* mask data according to sr[2] */
			write_mask = mask16[writemask2 & 0x0f];
			addr >>= 2;

			((uint32_t *)(svga->vram))[addr] &= ~write_mask;
			((uint32_t *)(svga->vram))[addr] |= (val32 & write_mask);
			return;
		}

		set_mask = mask16[svga->gdcreg[1] & 0x0f];
		val32 = (val32 & ~set_mask) | (mask16[svga->gdcreg[0] & 0x0f] & set_mask);
		break;
	case 1:
		val32 = svga->latch;

		/* mask data according to sr[2] */
		write_mask = mask16[writemask2 & 0x0f];
		addr >>= 2;

		((uint32_t *)(svga->vram))[addr] &= ~write_mask;
		((uint32_t *)(svga->vram))[addr] |= (val32 & write_mask);
		return;
	case 2:
		val32 = mask16[val32 & 0x0f];
		bit_mask = svga->gdcreg[8];

                if (!(svga->gdcreg[3] & 0x18) && (!svga->gdcreg[1] || svga->set_reset_disabled))
			func_select = 0;
		break;
	case 3:
		/* rotate */
                if (svga->gdcreg[3] & 7)
                        val32 = svga_rotate[svga->gdcreg[3] & 7][val];

		bit_mask = svga->gdcreg[8] & val32;
		val32 = mask16[svga->gdcreg[0] & 0x0f];
		break;
	default:
		if (svga->ven_write)
			svga->ven_write(svga, val, addr);
		return;
    }

    /* apply bit mask */
    bit_mask |= bit_mask << 8;
    bit_mask |= bit_mask << 16;

    /* apply logical operation */
    switch(func_select) {
	case 0:
	default:
		/* set */
		val32 &= bit_mask;
		val32 |= (svga->latch & ~bit_mask);
		break;
	case 1:
		/* and */
		val32 |= ~bit_mask;
		val32 &= svga->latch;
		break;
	case 2:
		/* or */
		val32 &= bit_mask;
		val32 |= svga->latch;
		break;
	case 3:
		/* xor */
		val32 &= bit_mask;
		val32 ^= svga->latch;
		break;
    }

    /* mask data according to sr[2] */
    write_mask = mask16[writemask2 & 0x0f];
    addr >>= 2;

    ((uint32_t *)(svga->vram))[addr] = (((uint32_t *)(svga->vram))[addr] & ~write_mask) | (val32 & write_mask);
}


uint8_t
svga_read_common(uint32_t addr, uint8_t linear, void *p)
{
    svga_t *svga = (svga_t *)p;
    uint32_t latch_addr = 0, ret;
    int memory_map_mode, readplane = svga->readplane;

    sub_cycles(video_timing_read_b);

    egareads++;

    if (!linear) {
	memory_map_mode = (svga->gdcreg[6] >> 2) & 3;

	addr &= 0x1ffff;

	switch(memory_map_mode) {
		case 0:
			break;
		case 1:
			if (addr >= 0x10000)
				return 0xff;
			addr += svga->read_bank;
			break;
		case 2:
			addr -= 0x10000;
			if (addr >= 0x8000)
				return 0xff;
			break;
		default:
		case 3:
			addr -= 0x18000;
			if (addr >= 0x8000)
				return 0xff;
			break;
	}

	latch_addr = (addr << 2) & svga->decode_mask;
    }

    if (svga->chain4 || svga->fb_only)  {
	addr &= svga->decode_mask;
	if (addr >= svga->vram_max)
		return 0xff;
	return svga->vram[addr & svga->vram_mask];
    } else if (svga->chain2_read) {
	readplane = (readplane & 2) | (addr & 1);
	addr &= ~1;
	addr <<= 2;
	addr |= readplane;
	addr &= svga->decode_mask;
	if (addr >= svga->vram_max)
		return 0xff;
	addr &= svga->vram_mask;
	return svga->vram[addr];
    } else
	addr <<= 2;

    addr &= svga->decode_mask;

    /* standard VGA latched access */
    if (linear) {
	if (addr >= svga->vram_max)
		return 0xff;

	addr &= svga->vram_mask;

	svga->latch = ((uint32_t *)(svga->vram))[addr >> 2];
    } else {
	if (latch_addr > svga->vram_max)
		svga->latch = 0xffffffff;
	else {
		latch_addr &= svga->vram_mask;
		svga->latch = ((uint32_t *)(svga->vram))[latch_addr >> 2];
	}

    	if (addr >= svga->vram_max)
		return 0xff;

	    addr &= svga->vram_mask;
    }

    if (!(svga->gdcreg[5] & 8)) {
	/* read mode 0 */
	return (svga->latch >> (readplane * 8)) & 0xff;
    } else {
	/* read mode 1 */
	ret = (svga->latch ^ mask16[svga->colourcompare & 0x0f]) & mask16[svga->colournocare & 0x0f];
	ret |= ret >> 16;
	ret |= ret >> 8;
	ret = (~ret) & 0xff;
	return(ret);
    }
}


void
svga_write(uint32_t addr, uint8_t val, void *p)
{
    svga_write_common(addr, val, 0, p);
}


void
svga_write_linear(uint32_t addr, uint8_t val, void *p)
{
    svga_write_common(addr, val, 1, p);
}


uint8_t
svga_read(uint32_t addr, void *p)
{
    return svga_read_common(addr, 0, p);
}


uint8_t
svga_read_linear(uint32_t addr, void *p)
{
    return svga_read_common(addr, 1, p);
}


void
svga_doblit(int y1, int y2, int wx, int wy, svga_t *svga)
{
    int y_add = (enable_overscan) ? overscan_y : 0;
    int x_add = (enable_overscan) ? 16 : 0;
    uint32_t *p;
    int i, j;

    if ((xsize > 2032) || (ysize > 2032)) {
	x_add = 0;
	y_add = 0;
	suppress_overscan = 1;
    } else
	suppress_overscan = 0;

    if (y1 > y2) {
	video_blit_memtoscreen(32, 0, 0, 0, xsize + x_add, ysize + y_add);
	return;
    }

    if ((wx != xsize) || ((wy + 1) != ysize) || video_force_resize_get()) {
	/* Screen res has changed.. fix up, and let them know. */
	xsize = wx;
	ysize = wy + 1;
	if (xsize < 64)
		xsize = 640;
	if (ysize < 32)
		ysize = 200;

	set_screen_size(xsize+x_add,ysize+y_add);

	if (video_force_resize_get())
		video_force_resize_set(0);
    }

    if (enable_overscan && !suppress_overscan) {
	if ((wx >= 160) && ((wy + 1) >= 120)) {
		/* Draw (overscan_size - scroll size) lines of overscan on top. */
		for (i  = 0; i < (y_add >> 1); i++) {
			p = &buffer32->line[i & 0x7ff][32];

			for (j = 0; j < (xsize + x_add); j++)
				p[j] = svga->overscan_color;
		}

		/* Draw (overscan_size + scroll size) lines of overscan on the bottom. */
		for (i  = 0; i < (y_add >> 1); i++) {
			p = &buffer32->line[(ysize + (y_add >> 1) + i) & 0x7ff][32];

			for (j = 0; j < (xsize + x_add); j++)
				p[j] = svga->overscan_color;
		}

		for (i = (y_add >> 1); i < (ysize + (y_add >> 1)); i ++) {
			p = &buffer32->line[i & 0x7ff][32];

			for (j = 0; j < 8; j++) {
				p[j] = svga->overscan_color;
				p[xsize + (x_add >> 1) + j] = svga->overscan_color;
			}
		}
	}
    }

    video_blit_memtoscreen(32, 0, y1, y2 + y_add, xsize + x_add, ysize + y_add);
}


void
svga_writeb_linear(uint32_t addr, uint8_t val, void *p)
{
    svga_t *svga = (svga_t *)p;

    if (!svga->fast) {
	svga_write_linear(addr, val, p);
	return;
    }

    egawrites++;

    addr &= svga->decode_mask;
    if (addr >= svga->vram_max)
	return;
    addr &= svga->vram_mask;
    svga->changedvram[addr >> 12] = changeframecount;
    *(uint8_t *)&svga->vram[addr] = val;
}


void
svga_writew_common(uint32_t addr, uint16_t val, uint8_t linear, void *p)
{
    svga_t *svga = (svga_t *)p;

    if (!svga->fast) {
	svga_write_common(addr, val, linear, p);
	svga_write_common(addr + 1, val >> 8, linear, p);
	return;
    }

    egawrites += 2;

    sub_cycles(video_timing_write_w);

    if (!linear)
	addr = (addr & svga->banked_mask) + svga->write_bank;
    addr &= svga->decode_mask;
    if (addr >= svga->vram_max)
	return;
    addr &= svga->vram_mask;

    svga->changedvram[addr >> 12] = changeframecount;
    *(uint16_t *)&svga->vram[addr] = val;
}


void
svga_writew(uint32_t addr, uint16_t val, void *p)
{
    svga_writew_common(addr, val, 0, p);
}


void
svga_writew_linear(uint32_t addr, uint16_t val, void *p)
{
    svga_writew_common(addr, val, 1, p);
}


void
svga_writel_common(uint32_t addr, uint32_t val, uint8_t linear, void *p)
{
    svga_t *svga = (svga_t *)p;

    if (!svga->fast) {
	svga_write_common(addr, val, linear, p);
	svga_write_common(addr + 1, val >> 8, linear, p);
	svga_write_common(addr + 2, val >> 16, linear, p);
	svga_write_common(addr + 3, val >> 24, linear, p);
	return;
    }

    egawrites += 4;

    sub_cycles(video_timing_write_l);

    if (!linear)
	addr = (addr & svga->banked_mask) + svga->write_bank;
    addr &= svga->decode_mask;
    if (addr >= svga->vram_max)
	return;
    addr &= svga->vram_mask;

    svga->changedvram[addr >> 12] = changeframecount;
    *(uint32_t *)&svga->vram[addr] = val;
}


void
svga_writel(uint32_t addr, uint32_t val, void *p)
{
    svga_writel_common(addr, val, 0, p);
}


void
svga_writel_linear(uint32_t addr, uint32_t val, void *p)
{
    svga_writel_common(addr, val, 1, p);
}


uint8_t
svga_readb_linear(uint32_t addr, void *p)
{
    svga_t *svga = (svga_t *)p;

    if (!svga->fast)
	return svga_read_linear(addr, p);

    egareads++;

    addr &= svga->decode_mask;
    if (addr >= svga->vram_max)
	return 0xff;

    return *(uint8_t *)&svga->vram[addr & svga->vram_mask];
}


uint16_t
svga_readw_common(uint32_t addr, uint8_t linear, void *p)
{
    svga_t *svga = (svga_t *)p;

    if (!svga->fast)
	return svga_read_common(addr, linear, p) | (svga_read_common(addr + 1, linear, p) << 8);

    egareads += 2;

    sub_cycles(video_timing_read_w);

    if (!linear)
	addr = (addr & svga->banked_mask) + svga->read_bank;
    addr &= svga->decode_mask;
    if (addr >= svga->vram_max)
	return 0xffff;

    return *(uint16_t *)&svga->vram[addr & svga->vram_mask];
}


uint16_t
svga_readw(uint32_t addr, void *p)
{
    return svga_readw_common(addr, 0, p);
}


uint16_t
svga_readw_linear(uint32_t addr, void *p)
{
    return svga_readw_common(addr, 1, p);
}


uint32_t
svga_readl_common(uint32_t addr, uint8_t linear, void *p)
{
    svga_t *svga = (svga_t *)p;

    if (!svga->fast) {
	return svga_read_common(addr, linear, p) | (svga_read_common(addr + 1, linear, p) << 8) |
	       (svga_read_common(addr + 2, linear, p) << 16) | (svga_read_common(addr + 3, linear, p) << 24);
    }

    egareads += 4;

    sub_cycles(video_timing_read_l);

    if (!linear)
	addr = (addr & svga->banked_mask) + svga->read_bank;
    addr &= svga->decode_mask;
    if (addr >= svga->vram_max)
	return 0xffffffff;

    return *(uint32_t *)&svga->vram[addr & svga->vram_mask];
}


uint32_t
svga_readl(uint32_t addr, void *p)
{
    return svga_readl_common(addr, 0, p);
}


uint32_t
svga_readl_linear(uint32_t addr, void *p)
{
    return svga_readl_common(addr, 1, p);
}
