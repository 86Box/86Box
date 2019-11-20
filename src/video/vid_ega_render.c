/*
 * 86Box	A hypervisor and IBM PC system emulator that specializes in
 *		running old operating systems and software designed for IBM
 *		PC systems and compatibles from 1981 through fairly recent
 *		system designs based on the PCI bus.
 *
 *		This file is part of the 86Box distribution.
 *
 *		EGA renderers.
 *
 * Version:	@(#)vid_ega_render.c	1.0.7	2019/11/19
 *
 * Author:	Sarah Walker, <http://pcem-emulator.co.uk/>
 *		Miran Grca, <mgrca8@gmail.com>
 *
 *		Copyright 2008-2019 Sarah Walker.
 *		Copyright 2016-2019 Miran Grca.
 */
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <wchar.h>
#include "../86box.h"
#include "../device.h"
#include "../timer.h"
#include "../mem.h"
#include "../rom.h"
#include "video.h"
#include "vid_ega.h"
#include "vid_ega_render.h"


int
ega_display_line(ega_t *ega)
{
    int y_add = (enable_overscan) ? (overscan_y >> 1) : 0;
    unsigned int dl = ega->displine;

    if (ega->crtc[9] & 0x1f)
	dl -= (ega->crtc[8] & 0x1f);

    dl += y_add;
    dl &= 0x7ff;
    return dl;
}


void
ega_render_blank(ega_t *ega)
{
    int x, xx;

    if ((ega->displine + ega->y_add) < 0)
	return;

    for (x = 0; x < (ega->hdisp + ega->scrollcache); x++) {
	switch (ega->seqregs[1] & 9) {
		case 0:
			for (xx = 0; xx < 9; xx++)  buffer32->line[ega->displine + ega->y_add][ega->x_add + (x * 9) + xx] = 0;
			break;
		case 1:
			for (xx = 0; xx < 8; xx++)  buffer32->line[ega->displine + ega->y_add][ega->x_add + (x * 8) + xx] = 0;
			break;
		case 8:
			for (xx = 0; xx < 18; xx++) buffer32->line[ega->displine + ega->y_add][ega->x_add + (x * 18) + xx] = 0;
			break;
		case 9:
			for (xx = 0; xx < 16; xx++) buffer32->line[ega->displine + ega->y_add][ega->x_add + (x * 16) + xx] = 0;
			break;
	}
    }
}


void
ega_render_overscan_left(ega_t *ega)
{
    int i;

    if (ega->scrblank || (ega->hdisp == 0))
	return;

    for (i = 0; i < ega->x_add; i++)
	buffer32->line[ega->displine + ega->y_add][i] = ega->overscan_color;
}


void
ega_render_overscan_right(ega_t *ega)
{
    int i, right;

    if (ega->scrblank || (ega->hdisp == 0))
	return;

    right = (overscan_x >> 1) + ega->scrollcache;
    for (i = 0; i < right; i++)
	buffer32->line[ega->displine + ega->y_add][ega->x_add + ega->hdisp + i] = ega->overscan_color;
}


void
ega_render_text_40(ega_t *ega)
{     
    uint32_t *p;
    int x, xx;
    int drawcursor, xinc;
    uint8_t chr, attr, dat;
    uint32_t charaddr;
    int fg, bg;

    if ((ega->displine + ega->y_add) < 0)
	return;

    if (ega->firstline_draw == 2000) 
	ega->firstline_draw = ega->displine;
    ega->lastline_draw = ega->displine;

    if (fullchange) {
	p = &buffer32->line[ega->displine + ega->y_add][ega->x_add];
	xinc = (ega->seqregs[1] & 1) ? 16 : 18;

	for (x = 0; x < (ega->hdisp + ega->scrollcache); x += xinc) {
		drawcursor = ((ega->ma == ega->ca) && ega->con && ega->cursoron);
		chr  = ega->vram[(ega->ma << 1) & ega->vrammask];
		attr = ega->vram[((ega->ma << 1) + 1) & ega->vrammask];

		if (attr & 8)	charaddr = ega->charsetb + (chr * 128);
		else		charaddr = ega->charseta + (chr * 128);

		if (drawcursor) {
			bg = ega->pallook[ega->egapal[attr & 15]]; 
			fg = ega->pallook[ega->egapal[attr >> 4]]; 
		} else {
			fg = ega->pallook[ega->egapal[attr & 15]];
			bg = ega->pallook[ega->egapal[attr >> 4]];

			if (attr & 0x80 && ega->attrregs[0x10] & 8) {
				bg = ega->pallook[ega->egapal[(attr >> 4) & 7]];
				if (ega->blink & 16) 
					fg = bg;
			}
		}

		dat = ega->vram[charaddr + (ega->sc << 2)];
		if (ega->seqregs[1] & 1) {
			for (xx = 0; xx < 16; xx += 2) 
				p[xx] = p[xx + 1] = (dat & (0x80 >> (xx >> 1))) ? fg : bg;
		} else {
			for (xx = 0; xx < 16; xx += 2)
				p[xx] = p[xx + 1] = (dat & (0x80 >> (xx >> 1))) ? fg : bg;
			if ((chr & ~0x1f) != 0xc0 || !(ega->attrregs[0x10] & 4)) 
				p[16] = p[17] = bg;
			else
				p[16] = p[17] = (dat & 1) ? fg : bg;
		}
		ega->ma += 4; 
		p += xinc;
	}
	ega->ma &= ega->vrammask;
    }
}


void
ega_render_text_80(ega_t *ega)
{
    uint32_t *p;
    int x, xx;
    int drawcursor, xinc;
    uint8_t chr, attr, dat;
    uint32_t charaddr;
    int fg, bg;

    if ((ega->displine + ega->y_add) < 0)
	return;

    if (ega->firstline_draw == 2000) 
	ega->firstline_draw = ega->displine;
    ega->lastline_draw = ega->displine;

    if (fullchange) {
	p = &buffer32->line[ega->displine + ega->y_add][ega->x_add];
	xinc = (ega->seqregs[1] & 1) ? 8 : 9;

	for (x = 0; x < (ega->hdisp + ega->scrollcache); x += xinc) {
		drawcursor = ((ega->ma == ega->ca) && ega->con && ega->cursoron);
		chr  = ega->vram[(ega->ma << 1) & ega->vrammask];
		attr = ega->vram[((ega->ma << 1) + 1) & ega->vrammask];

		if (attr & 8)	charaddr = ega->charsetb + (chr * 128);
		else		charaddr = ega->charseta + (chr * 128);

		if (drawcursor) {
			bg = ega->pallook[ega->egapal[attr & 15]]; 
			fg = ega->pallook[ega->egapal[attr >> 4]]; 
		} else {
			fg = ega->pallook[ega->egapal[attr & 15]];
			bg = ega->pallook[ega->egapal[attr >> 4]];
			if (attr & 0x80 && ega->attrregs[0x10] & 8) {
				bg = ega->pallook[ega->egapal[(attr >> 4) & 7]];
				if (ega->blink & 16) 
					fg = bg;
			}
		}

		dat = ega->vram[charaddr + (ega->sc << 2)];
		if (ega->seqregs[1] & 1)  { 
			for (xx = 0; xx < 8; xx++) 
				p[xx] = (dat & (0x80 >> xx)) ? fg : bg;
		} else {
			for (xx = 0; xx < 8; xx++) 
				p[xx] = (dat & (0x80 >> xx)) ? fg : bg;
			if ((chr & ~0x1F) != 0xC0 || !(ega->attrregs[0x10] & 4)) 
				p[8] = bg;
			else                  
				p[8] = (dat & 1) ? fg : bg;
		}
		ega->ma += 4; 
		p += xinc;
	}
	ega->ma &= ega->vrammask;
    }
}


void
ega_render_2bpp_lowres(ega_t *ega)
{
    int x;
    uint8_t dat[2];
    uint32_t addr, *p;

    if ((ega->displine + ega->y_add) < 0)
	return;

    p = &buffer32->line[ega->displine + ega->y_add][ega->x_add];

    if (ega->firstline_draw == 2000)
	ega->firstline_draw = ega->displine;
    ega->lastline_draw = ega->displine;

    for (x = 0; x <= (ega->hdisp + ega->scrollcache); x += 16) {
	addr = ega->ma;

	if (!(ega->crtc[0x17] & 0x40)) {
		addr = (addr << 1) & ega->vrammask;
		addr &= ~7;

		if ((ega->crtc[0x17] & 0x20) && (ega->ma & 0x20000))
			addr |= 4;

		if (!(ega->crtc[0x17] & 0x20) && (ega->ma & 0x8000))
			addr |= 4;
	}

	if (!(ega->crtc[0x17] & 0x01))
		addr = (addr & ~0x8000) | ((ega->sc & 1) ? 0x8000 : 0);

	if (!(ega->crtc[0x17] & 0x02))
		addr = (addr & ~0x10000) | ((ega->sc & 2) ? 0x10000 : 0);

	dat[0] = ega->vram[addr];
	dat[1] = ega->vram[addr | 0x1];
	if (ega->seqregs[1] & 4)
		ega->ma += 2;
	else
		ega->ma += 4;

	ega->ma &= ega->vrammask;

	p[0]  = p[1]  = ega->pallook[ega->egapal[(dat[0] >> 6) & 3]];
	p[2]  = p[3]  = ega->pallook[ega->egapal[(dat[0] >> 4) & 3]];
	p[4]  = p[5]  = ega->pallook[ega->egapal[(dat[0] >> 2) & 3]];
	p[6]  = p[7]  = ega->pallook[ega->egapal[dat[0] & 3]];
	p[8]  = p[9]  = ega->pallook[ega->egapal[(dat[1] >> 6) & 3]];
	p[10] = p[11] = ega->pallook[ega->egapal[(dat[1] >> 4) & 3]];
	p[12] = p[13] = ega->pallook[ega->egapal[(dat[1] >> 2) & 3]];
	p[14] = p[15] = ega->pallook[ega->egapal[dat[1] & 3]];

	p += 16;
    }
}


void
ega_render_2bpp_highres(ega_t *ega)
{
    int x;
    uint8_t dat[2];
    uint32_t addr, *p;

    if ((ega->displine + ega->y_add) < 0)
	return;

    p = &buffer32->line[ega->displine + ega->y_add][ega->x_add];

    if (ega->firstline_draw == 2000)
	ega->firstline_draw = ega->displine;
    ega->lastline_draw = ega->displine;

    for (x = 0; x <= (ega->hdisp + ega->scrollcache); x += 8) {
	addr = ega->ma;

	if (!(ega->crtc[0x17] & 0x40)) {
		addr = (addr << 1) & ega->vrammask;
		addr &= ~7;

		if ((ega->crtc[0x17] & 0x20) && (ega->ma & 0x20000))
			addr |= 4;

		if (!(ega->crtc[0x17] & 0x20) && (ega->ma & 0x8000))
			addr |= 4;
	}

	if (!(ega->crtc[0x17] & 0x01))
		addr = (addr & ~0x8000) | ((ega->sc & 1) ? 0x8000 : 0);

	if (!(ega->crtc[0x17] & 0x02))
		addr = (addr & ~0x10000) | ((ega->sc & 2) ? 0x10000 : 0);

	dat[0] = ega->vram[addr];
	dat[1] = ega->vram[addr | 0x1];
	if (ega->seqregs[1] & 4)
		ega->ma += 2;
	else
		ega->ma += 4;

	ega->ma &= ega->vrammask;

	p[0]  = ega->pallook[ega->egapal[(dat[0] >> 6) & 3]];
	p[1]  = ega->pallook[ega->egapal[(dat[0] >> 4) & 3]];
	p[2]  = ega->pallook[ega->egapal[(dat[0] >> 2) & 3]];
	p[3]  = ega->pallook[ega->egapal[dat[0] & 3]];
	p[4]  = ega->pallook[ega->egapal[(dat[1] >> 6) & 3]];
	p[5] = ega->pallook[ega->egapal[(dat[1] >> 4) & 3]];
	p[6] = ega->pallook[ega->egapal[(dat[1] >> 2) & 3]];
	p[7] = ega->pallook[ega->egapal[dat[1] & 3]];

	p += 8;
    }
}


void
ega_render_4bpp_lowres(ega_t *ega)
{
    int x, oddeven;
    uint8_t dat, edat[4];
    uint32_t addr, *p;

    if ((ega->displine + ega->y_add) < 0)
	return;

    p = &buffer32->line[ega->displine + ega->y_add][ega->x_add];

    if (ega->firstline_draw == 2000)
	ega->firstline_draw = ega->displine;
    ega->lastline_draw = ega->displine;

    for (x = 0; x <= (ega->hdisp + ega->scrollcache); x += 16) {
	addr = ega->ma;
	oddeven = 0;

	if (!(ega->crtc[0x17] & 0x40)) {
		addr = (addr << 1) & ega->vrammask;

		if (ega->seqregs[1] & 4)
			oddeven = (addr & 4) ? 1 : 0;

		addr &= ~7;

		if ((ega->crtc[0x17] & 0x20) && (ega->ma & 0x20000))
			addr |= 4;
		if (!(ega->crtc[0x17] & 0x20) && (ega->ma & 0x8000))
			addr |= 4;
	}

	if (!(ega->crtc[0x17] & 0x01))
		addr = (addr & ~0x8000) | ((ega->sc & 1) ? 0x8000 : 0);

	if (!(ega->crtc[0x17] & 0x02))
		addr = (addr & ~0x10000) | ((ega->sc & 2) ? 0x10000 : 0);

	if (ega->seqregs[1] & 4) {
		edat[0] = ega->vram[addr | oddeven];
		edat[2] = ega->vram[addr | oddeven | 0x2];
		edat[1] = edat[3] = 0;
		ega->ma += 2;
	} else {
		edat[0] = ega->vram[addr];
		edat[1] = ega->vram[addr | 0x1];
		edat[2] = ega->vram[addr | 0x2];
		edat[3] = ega->vram[addr | 0x3];
		ega->ma += 4;
	}

	ega->ma &= ega->vrammask;

	dat = edatlookup[edat[0] >> 6][edat[1] >> 6] | (edatlookup[edat[2] >> 6][edat[3] >> 6] << 2);
	p[0]  = p[1]  = ega->pallook[ega->egapal[(dat >> 4) & ega->plane_mask]];
	p[2]  = p[3]  = ega->pallook[ega->egapal[dat & ega->plane_mask]];
	dat = edatlookup[(edat[0] >> 4) & 3][(edat[1] >> 4) & 3] | (edatlookup[(edat[2] >> 4) & 3][(edat[3] >> 4) & 3] << 2);
	p[4]  = p[5]  = ega->pallook[ega->egapal[(dat >> 4) & ega->plane_mask]];
	p[6]  = p[7]  = ega->pallook[ega->egapal[dat & ega->plane_mask]];
	dat = edatlookup[(edat[0] >> 2) & 3][(edat[1] >> 2) & 3] | (edatlookup[(edat[2] >> 2) & 3][(edat[3] >> 2) & 3] << 2);
	p[8]  = p[9]  = ega->pallook[ega->egapal[(dat >> 4) & ega->plane_mask]];
	p[10] = p[11] = ega->pallook[ega->egapal[dat & ega->plane_mask]];
	dat = edatlookup[edat[0] & 3][edat[1] & 3] | (edatlookup[edat[2] & 3][edat[3] & 3] << 2);
	p[12] = p[13] = ega->pallook[ega->egapal[(dat >> 4) & ega->plane_mask]];
	p[14] = p[15] = ega->pallook[ega->egapal[dat & ega->plane_mask]];

	p += 16;
    }
}


void
ega_render_4bpp_highres(ega_t *ega)
{
    int x, oddeven;
    uint8_t dat, edat[4];
    uint32_t addr, *p;

    if ((ega->displine + ega->y_add) < 0)
	return;

    p = &buffer32->line[ega->displine + ega->y_add][ega->x_add];

    if (ega->firstline_draw == 2000)
	ega->firstline_draw = ega->displine;
    ega->lastline_draw = ega->displine;

    for (x = 0; x <= (ega->hdisp + ega->scrollcache); x += 8) {
	addr = ega->ma;
	oddeven = 0;

	if (!(ega->crtc[0x17] & 0x40)) {
		addr = (addr << 1) & ega->vrammask;

		if (ega->seqregs[1] & 4)
			oddeven = (addr & 4) ? 1 : 0;

		addr &= ~7;

		if ((ega->crtc[0x17] & 0x20) && (ega->ma & 0x20000))
			addr |= 4;
		if (!(ega->crtc[0x17] & 0x20) && (ega->ma & 0x8000))
			addr |= 4;
	}

	if (!(ega->crtc[0x17] & 0x01))
		addr = (addr & ~0x8000) | ((ega->sc & 1) ? 0x8000 : 0);
	if (!(ega->crtc[0x17] & 0x02))
		addr = (addr & ~0x10000) | ((ega->sc & 2) ? 0x10000 : 0);

	if (ega->seqregs[1] & 4) {
		edat[0] = ega->vram[addr | oddeven];
		edat[2] = ega->vram[addr | oddeven | 0x2];
		edat[1] = edat[3] = 0;
		ega->ma += 2;
	} else {
		*(uint32_t *)(&edat[0]) = *(uint32_t *)(&ega->vram[addr]);
		ega->ma += 4;
	}
	ega->ma &= ega->vrammask;

	dat = edatlookup[edat[0] >> 6][edat[1] >> 6] | (edatlookup[edat[2] >> 6][edat[3] >> 6] << 2);
	p[0] = ega->pallook[ega->egapal[(dat >> 4) & ega->plane_mask]];
	p[1] = ega->pallook[ega->egapal[dat & ega->plane_mask]];
	dat = edatlookup[(edat[0] >> 4) & 3][(edat[1] >> 4) & 3] | (edatlookup[(edat[2] >> 4) & 3][(edat[3] >> 4) & 3] << 2);
	p[2] = ega->pallook[ega->egapal[(dat >> 4) & ega->plane_mask]];
	p[3] = ega->pallook[ega->egapal[dat & ega->plane_mask]];
	dat = edatlookup[(edat[0] >> 2) & 3][(edat[1] >> 2) & 3] | (edatlookup[(edat[2] >> 2) & 3][(edat[3] >> 2) & 3] << 2);
	p[4] = ega->pallook[ega->egapal[(dat >> 4) & ega->plane_mask]];
	p[5] = ega->pallook[ega->egapal[dat & ega->plane_mask]];
	dat = edatlookup[edat[0] & 3][edat[1] & 3] | (edatlookup[edat[2] & 3][edat[3] & 3] << 2);
	p[6] = ega->pallook[ega->egapal[(dat >> 4) & ega->plane_mask]];
	p[7] = ega->pallook[ega->egapal[dat & ega->plane_mask]];

	p += 8;
    }
}
