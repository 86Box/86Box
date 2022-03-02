/*
 * 86Box	A hypervisor and IBM PC system emulator that specializes in
 *		running old operating systems and software designed for IBM
 *		PC systems and compatibles from 1981 through fairly recent
 *		system designs based on the PCI bus.
 *
 *		This file is part of the 86Box distribution.
 *
 *		SVGA renderers.
 *
 *
 *
 * Authors:	Sarah Walker, <http://pcem-emulator.co.uk/>
 *		Miran Grca, <mgrca8@gmail.com>
 *
 *		Copyright 2008-2019 Sarah Walker.
 *		Copyright 2016-2019 Miran Grca.
 */
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <wchar.h>
#include <86box/86box.h>
#include <86box/device.h>
#include <86box/mem.h>
#include <86box/timer.h>
#include <86box/video.h>
#include <86box/vid_svga.h>
#include <86box/vid_svga_render.h>
#include <86box/vid_svga_render_remap.h>

void
svga_render_null(svga_t *svga)
{
    if ((svga->displine + svga->y_add) < 0)
	return;

    if (svga->firstline_draw == 2000)
	svga->firstline_draw = svga->displine;
    svga->lastline_draw = svga->displine;
}

void
svga_render_blank(svga_t *svga)
{
    int x, xx;

    if ((svga->displine + svga->y_add) < 0)
	return;

    if (svga->firstline_draw == 2000)
	svga->firstline_draw = svga->displine;
    svga->lastline_draw = svga->displine;

    for (x = 0; x < (svga->hdisp + svga->scrollcache); x++) {
	switch (svga->seqregs[1] & 9) {
		case 0:
			for (xx = 0; xx < 9; xx++)
				buffer32->line[svga->displine + svga->y_add][svga->x_add + (x * 9) + xx] = 0x00000000;
			break;
		case 1:
			for (xx = 0; xx < 8; xx++)
				buffer32->line[svga->displine + svga->y_add][svga->x_add + (x * 8) + xx] = 0x00000000;
			break;
		case 8:
			for (xx = 0; xx < 18; xx++)
				buffer32->line[svga->displine + svga->y_add][svga->x_add + (x * 18) + xx] = 0x00000000;
			break;
		case 9:
			for (xx = 0; xx < 16; xx++)
				buffer32->line[svga->displine + svga->y_add][svga->x_add + (x * 16) + xx] = 0x00000000;
			break;
	}
    }
}


void
svga_render_overscan_left(svga_t *svga)
{
    int i;

    if ((svga->displine + svga->y_add) < 0)
	return;

    if (svga->scrblank || (svga->hdisp == 0))
	return;

    for (i = 0; i < svga->x_add; i++)
	buffer32->line[svga->displine + svga->y_add][i] = svga->overscan_color;
}


void
svga_render_overscan_right(svga_t *svga)
{
    int i, right;

    if ((svga->displine + svga->y_add) < 0)
	return;

    if (svga->scrblank || (svga->hdisp == 0))
	return;

    right = (overscan_x >> 1);
    for (i = 0; i < right; i++)
	buffer32->line[svga->displine + svga->y_add][svga->x_add + svga->hdisp + i] = svga->overscan_color;
}


void
svga_render_text_40(svga_t *svga)
{
    uint32_t *p;
    int x, xx;
    int drawcursor, xinc;
    uint8_t chr, attr, dat;
    uint32_t charaddr;
    int fg, bg;
	uint32_t addr = 0;

    if ((svga->displine + svga->y_add) < 0)
	return;

    if (svga->firstline_draw == 2000)
	svga->firstline_draw = svga->displine;
    svga->lastline_draw = svga->displine;

    if (svga->fullchange) {
	p = &buffer32->line[svga->displine + svga->y_add][svga->x_add];
	xinc = (svga->seqregs[1] & 1) ? 16 : 18;

	for (x = 0; x < (svga->hdisp + svga->scrollcache); x += xinc) {
		if (!svga->force_old_addr)
			addr = svga->remap_func(svga, svga->ma) & svga->vram_display_mask;

		drawcursor = ((svga->ma == svga->ca) && svga->con && svga->cursoron);

		if (svga->crtc[0x17] & 0x80) {
			if (svga->force_old_addr) {
				chr  = svga->vram[(svga->ma << 1) & svga->vram_display_mask];
				attr = svga->vram[((svga->ma << 1) + 1) & svga->vram_display_mask];
			} else {
				chr  = svga->vram[addr];
				attr = svga->vram[addr+1];
			}
		} else
			chr = attr = 0;

		if (attr & 8)	charaddr = svga->charsetb + (chr * 128);
		else		charaddr = svga->charseta + (chr * 128);

		if (drawcursor) {
			bg = svga->pallook[svga->egapal[attr & 15]];
			fg = svga->pallook[svga->egapal[attr >> 4]];
		} else {
			fg = svga->pallook[svga->egapal[attr & 15]];
			bg = svga->pallook[svga->egapal[attr >> 4]];

			if (attr & 0x80 && svga->attrregs[0x10] & 8) {
				bg = svga->pallook[svga->egapal[(attr >> 4) & 7]];
				if (svga->blink & 16)
					fg = bg;
			}
		}

		dat = svga->vram[charaddr + (svga->sc << 2)];
		if (svga->seqregs[1] & 1) {
			for (xx = 0; xx < 16; xx += 2)
				p[xx] = p[xx + 1] = (dat & (0x80 >> (xx >> 1))) ? fg : bg;
		} else {
			for (xx = 0; xx < 16; xx += 2)
				p[xx] = p[xx + 1] = (dat & (0x80 >> (xx >> 1))) ? fg : bg;
			if ((chr & ~0x1f) != 0xc0 || !(svga->attrregs[0x10] & 4))
				p[16] = p[17] = bg;
			else
				p[16] = p[17] = (dat & 1) ? fg : bg;
		}
		svga->ma += 4;
		p += xinc;
	}
	svga->ma &= svga->vram_display_mask;
    }
}


void
svga_render_text_80(svga_t *svga)
{
    uint32_t *p;
    int x, xx;
    int drawcursor, xinc;
    uint8_t chr, attr, dat;
    uint32_t charaddr;
    int fg, bg;
	uint32_t addr = 0;

    if ((svga->displine + svga->y_add) < 0)
	return;

    if (svga->firstline_draw == 2000)
	svga->firstline_draw = svga->displine;
    svga->lastline_draw = svga->displine;

    if (svga->fullchange) {
	p = &buffer32->line[svga->displine + svga->y_add][svga->x_add];
	xinc = (svga->seqregs[1] & 1) ? 8 : 9;

	for (x = 0; x < (svga->hdisp + svga->scrollcache); x += xinc) {
		if (!svga->force_old_addr)
			addr = svga->remap_func(svga, svga->ma) & svga->vram_display_mask;

		drawcursor = ((svga->ma == svga->ca) && svga->con && svga->cursoron);

		if (svga->crtc[0x17] & 0x80) {
			if (svga->force_old_addr) {
				chr  = svga->vram[(svga->ma << 1) & svga->vram_display_mask];
				attr = svga->vram[((svga->ma << 1) + 1) & svga->vram_display_mask];
			} else {
				chr  = svga->vram[addr];
				attr = svga->vram[addr+1];
			}
		} else
			chr = attr = 0;

		if (attr & 8)	charaddr = svga->charsetb + (chr * 128);
		else		charaddr = svga->charseta + (chr * 128);

		if (drawcursor) {
			bg = svga->pallook[svga->egapal[attr & 15]];
			fg = svga->pallook[svga->egapal[attr >> 4]];
		} else {
			fg = svga->pallook[svga->egapal[attr & 15]];
			bg = svga->pallook[svga->egapal[attr >> 4]];
			if (attr & 0x80 && svga->attrregs[0x10] & 8) {
				bg = svga->pallook[svga->egapal[(attr >> 4) & 7]];
				if (svga->blink & 16)
					fg = bg;
			}
		}

		dat = svga->vram[charaddr + (svga->sc << 2)];
		if (svga->seqregs[1] & 1)  {
			for (xx = 0; xx < 8; xx++)
				p[xx] = (dat & (0x80 >> xx)) ? fg : bg;
		} else {
			for (xx = 0; xx < 8; xx++)
				p[xx] = (dat & (0x80 >> xx)) ? fg : bg;
			if ((chr & ~0x1F) != 0xC0 || !(svga->attrregs[0x10] & 4))
				p[8] = bg;
			else
				p[8] = (dat & 1) ? fg : bg;
		}
		svga->ma += 4;
		p += xinc;
	}
	svga->ma &= svga->vram_display_mask;
    }
}

/*Not available on most generic cards.*/
void
svga_render_text_80_ksc5601(svga_t *svga)
{
    uint32_t *p;
    int x, xx;
    int drawcursor, xinc;
    uint8_t chr, attr, dat, nextchr;
    uint32_t charaddr;
    int fg, bg;

    if ((svga->displine + svga->y_add) < 0)
	return;

    if (svga->firstline_draw == 2000)
	svga->firstline_draw = svga->displine;
    svga->lastline_draw = svga->displine;

    if (svga->fullchange) {
	p = &buffer32->line[svga->displine + svga->y_add][svga->x_add];

	xinc = (svga->seqregs[1] & 1) ? 8 : 9;

	for (x = 0; x < (svga->hdisp + svga->scrollcache); x += xinc) {
		uint32_t addr = svga->remap_func(svga, svga->ma) & svga->vram_display_mask;
		drawcursor = ((svga->ma == svga->ca) && svga->con && svga->cursoron);
		chr  = svga->vram[addr];
		nextchr = svga->vram[addr + 8];
		if (svga->crtc[0x17] & 0x80)
			attr = svga->vram[addr + 1];
		else
			attr = 0;

		if (drawcursor) {
			bg = svga->pallook[svga->egapal[attr & 15]];
			fg = svga->pallook[svga->egapal[attr >> 4]];
		} else {
			fg = svga->pallook[svga->egapal[attr & 15]];
			bg = svga->pallook[svga->egapal[attr >> 4]];
			if (attr & 0x80 && svga->attrregs[0x10] & 8) {
				bg = svga->pallook[svga->egapal[(attr >> 4) & 7]];
				if (svga->blink & 16)
					fg = bg;
			}
		}

		if ((x + xinc) < svga->hdisp && (chr & (nextchr | svga->ksc5601_sbyte_mask) & 0x80)) {
			if ((chr == svga->ksc5601_udc_area_msb[0] || chr == svga->ksc5601_udc_area_msb[1]) && (nextchr > 0xa0 && nextchr < 0xff))
				dat = fontdatksc5601_user[(chr == svga->ksc5601_udc_area_msb[1] ? 96 : 0) + (nextchr & 0x7F) - 0x20].chr[svga->sc];
			else if (nextchr & 0x80) {
				if (svga->ksc5601_swap_mode == 1 && (nextchr > 0xa0 && nextchr < 0xff)) {
					if(chr >= 0x80 && chr < 0x99) chr += 0x30;
					else if(chr >= 0xB0 && chr < 0xC9) chr -= 0x30;
				}
				dat = fontdatksc5601[((chr & 0x7F) << 7) | (nextchr & 0x7F)].chr[svga->sc];
			} else
				dat = 0xff;
		} else {
			if (attr & 8)	charaddr = svga->charsetb + (chr * 128);
			else		charaddr = svga->charseta + (chr * 128);

			if ((svga->ksc5601_english_font_type >> 8) == 1)
				dat = fontdatksc5601[((svga->ksc5601_english_font_type & 0x7F) << 7) | (chr >> 1)].chr[((chr & 1) << 4) | svga->sc];
			else
				dat = svga->vram[charaddr + (svga->sc << 2)];
		}

		if (svga->seqregs[1] & 1) {
			for (xx = 0; xx < 8; xx++)
			p[xx] = (dat & (0x80 >> xx)) ? fg : bg;
		} else {
			for (xx = 0; xx < 8; xx++)
				p[xx] = (dat & (0x80 >> xx)) ? fg : bg;
			if (((chr & ~0x1f) != 0xc0) || !(svga->attrregs[0x10] & 4))
				p[8] = bg;
			else
				p[8] = (dat & 1) ? fg : bg;
		}
		svga->ma += 4;
		p += xinc;

		if ((x + xinc) < svga->hdisp && (chr & (nextchr | svga->ksc5601_sbyte_mask) & 0x80)) {
			attr = svga->vram[((svga->ma << 1) + 1) & svga->vram_display_mask];

			if (drawcursor)  {
				bg = svga->pallook[svga->egapal[attr & 15]];
				fg = svga->pallook[svga->egapal[attr >> 4]];
			} else {
				fg = svga->pallook[svga->egapal[attr & 15]];
				bg = svga->pallook[svga->egapal[attr >> 4]];
				if (attr & 0x80 && svga->attrregs[0x10] & 8) {
					bg = svga->pallook[svga->egapal[(attr >> 4) & 7]];
					if (svga->blink & 16)
						fg = bg;
				}
			}

			if ((chr == svga->ksc5601_udc_area_msb[0] || chr == svga->ksc5601_udc_area_msb[1]) && (nextchr > 0xa0 && nextchr < 0xff))
				dat = fontdatksc5601_user[(chr == svga->ksc5601_udc_area_msb[1] ? 96 : 0) + (nextchr & 0x7F) - 0x20].chr[svga->sc + 16];
			else if(nextchr & 0x80)
				dat = fontdatksc5601[((chr & 0x7f) << 7) | (nextchr & 0x7F)].chr[svga->sc + 16];
			else
				dat = 0xff;

			if (svga->seqregs[1] & 1) {
				for (xx = 0; xx < 8; xx++)
					p[xx] = (dat & (0x80 >> xx)) ? fg : bg;
			} else {
				for (xx = 0; xx < 8; xx++)
					p[xx] = (dat & (0x80 >> xx)) ? fg : bg;
				if (((chr & ~0x1f) != 0xc0) || !(svga->attrregs[0x10] & 4))
					p[8] = bg;
				else
					p[8] = (dat & 1) ? fg : bg;
			}

			svga->ma += 4;
			p += xinc;
			x += xinc;
		}
	}
	svga->ma &= svga->vram_display_mask;
    }
}


void
svga_render_2bpp_lowres(svga_t *svga)
{
	int changed_offset;
    int x;
    uint8_t dat[2];
    uint32_t addr, *p;
	uint32_t changed_addr;

    if ((svga->displine + svga->y_add) < 0)
	return;

	if (svga->force_old_addr) {
		changed_offset = ((svga->ma << 1) + (svga->sc & ~svga->crtc[0x17] & 3) * 0x8000) >> 12;
		
		if (svga->changedvram[changed_offset] || svga->changedvram[changed_offset + 1] || svga->fullchange) {
			p = &buffer32->line[svga->displine + svga->y_add][svga->x_add];

			if (svga->firstline_draw == 2000)
				svga->firstline_draw = svga->displine;
			svga->lastline_draw = svga->displine;

			for (x = 0; x <= (svga->hdisp + svga->scrollcache); x += 16) {
				addr = svga->ma;

				if (!(svga->crtc[0x17] & 0x40)) {
					addr = (addr << 1) & svga->vram_mask;
					addr &= ~7;

					if ((svga->crtc[0x17] & 0x20) && (svga->ma & 0x20000))
						addr |= 4;

					if (!(svga->crtc[0x17] & 0x20) && (svga->ma & 0x8000))
						addr |= 4;
				}

				if (!(svga->crtc[0x17] & 0x01))
					addr = (addr & ~0x8000) | ((svga->sc & 1) ? 0x8000 : 0);

				if (!(svga->crtc[0x17] & 0x02))
					addr = (addr & ~0x10000) | ((svga->sc & 2) ? 0x10000 : 0);

				dat[0] = svga->vram[addr];
				dat[1] = svga->vram[addr | 0x1];
				if (svga->seqregs[1] & 4)
					svga->ma += 2;
				else
					svga->ma += 4;
				svga->ma &= svga->vram_mask;
				if (svga->crtc[0x17] & 0x80) {
					p[0]  = p[1]  = svga->pallook[svga->egapal[(dat[0] >> 6) & 3]];
					p[2]  = p[3]  = svga->pallook[svga->egapal[(dat[0] >> 4) & 3]];
					p[4]  = p[5]  = svga->pallook[svga->egapal[(dat[0] >> 2) & 3]];
					p[6]  = p[7]  = svga->pallook[svga->egapal[dat[0] & 3]];
					p[8]  = p[9]  = svga->pallook[svga->egapal[(dat[1] >> 6) & 3]];
					p[10] = p[11] = svga->pallook[svga->egapal[(dat[1] >> 4) & 3]];
					p[12] = p[13] = svga->pallook[svga->egapal[(dat[1] >> 2) & 3]];
					p[14] = p[15] = svga->pallook[svga->egapal[dat[1] & 3]];
				} else
					memset(p, 0x00, 16 * sizeof(uint32_t));
				p += 16;
			}
		}
	} else {
		changed_addr = svga->remap_func(svga, svga->ma);

		if (svga->changedvram[changed_addr >> 12] || svga->changedvram[(changed_addr >> 12) + 1] || svga->fullchange) {
			p = &buffer32->line[svga->displine + svga->y_add][svga->x_add];

			if (svga->firstline_draw == 2000)
				svga->firstline_draw = svga->displine;
			svga->lastline_draw = svga->displine;

			for (x = 0; x <= (svga->hdisp + svga->scrollcache); x += 16) {
				addr = svga->remap_func(svga, svga->ma);

				dat[0] = svga->vram[addr];
				dat[1] = svga->vram[addr | 0x1];
				if (svga->seqregs[1] & 4)
					svga->ma += 2;
				else
					svga->ma += 4;

				svga->ma &= svga->vram_mask;

				if (svga->crtc[0x17] & 0x80) {
					p[0]  = p[1]  = svga->pallook[svga->egapal[(dat[0] >> 6) & 3]];
					p[2]  = p[3]  = svga->pallook[svga->egapal[(dat[0] >> 4) & 3]];
					p[4]  = p[5]  = svga->pallook[svga->egapal[(dat[0] >> 2) & 3]];
					p[6]  = p[7]  = svga->pallook[svga->egapal[dat[0] & 3]];
					p[8]  = p[9]  = svga->pallook[svga->egapal[(dat[1] >> 6) & 3]];
					p[10] = p[11] = svga->pallook[svga->egapal[(dat[1] >> 4) & 3]];
					p[12] = p[13] = svga->pallook[svga->egapal[(dat[1] >> 2) & 3]];
					p[14] = p[15] = svga->pallook[svga->egapal[dat[1] & 3]];
				} else
					memset(p, 0x00, 16 * sizeof(uint32_t));

				p += 16;
			}
		}
    }
}


void
svga_render_2bpp_highres(svga_t *svga)
{
	int changed_offset;
    int x;
    uint8_t dat[2];
    uint32_t addr, *p;
	uint32_t changed_addr;

    if ((svga->displine + svga->y_add) < 0)
	return;

	if (svga->force_old_addr) {
		changed_offset = ((svga->ma << 1) + (svga->sc & ~svga->crtc[0x17] & 3) * 0x8000) >> 12;

		if (svga->changedvram[changed_offset] || svga->changedvram[changed_offset + 1] || svga->fullchange) {
		p = &buffer32->line[svga->displine + svga->y_add][svga->x_add];

		if (svga->firstline_draw == 2000)
			svga->firstline_draw = svga->displine;
		svga->lastline_draw = svga->displine;

		for (x = 0; x <= (svga->hdisp + svga->scrollcache); x += 8) {
			addr = svga->ma;

			if (!(svga->crtc[0x17] & 0x40)) {
				addr = (addr << 1) & svga->vram_mask;
				addr &= ~7;

				if ((svga->crtc[0x17] & 0x20) && (svga->ma & 0x20000))
					addr |= 4;

				if (!(svga->crtc[0x17] & 0x20) && (svga->ma & 0x8000))
					addr |= 4;
			}

			if (!(svga->crtc[0x17] & 0x01))
				addr = (addr & ~0x8000) | ((svga->sc & 1) ? 0x8000 : 0);

			if (!(svga->crtc[0x17] & 0x02))
				addr = (addr & ~0x10000) | ((svga->sc & 2) ? 0x10000 : 0);

			dat[0] = svga->vram[addr];
			dat[1] = svga->vram[addr | 0x1];
			if (svga->seqregs[1] & 4)
				svga->ma += 2;
			else
				svga->ma += 4;
			svga->ma &= svga->vram_mask;
			if (svga->crtc[0x17] & 0x80) {
				p[0] = svga->pallook[svga->egapal[(dat[0] >> 6) & 3]];
				p[1] = svga->pallook[svga->egapal[(dat[0] >> 4) & 3]];
				p[2] = svga->pallook[svga->egapal[(dat[0] >> 2) & 3]];
				p[3] = svga->pallook[svga->egapal[dat[0] & 3]];
				p[4] = svga->pallook[svga->egapal[(dat[1] >> 6) & 3]];
				p[5] = svga->pallook[svga->egapal[(dat[1] >> 4) & 3]];
				p[6] = svga->pallook[svga->egapal[(dat[1] >> 2) & 3]];
				p[7] = svga->pallook[svga->egapal[dat[1] & 3]];
			} else
				memset(p, 0x00, 8 * sizeof(uint32_t));
			p += 8;
		}
		}
	} else {
		changed_addr = svga->remap_func(svga, svga->ma);

		if (svga->changedvram[changed_addr >> 12] || svga->changedvram[(changed_addr >> 12) + 1] || svga->fullchange) {
		p = &buffer32->line[svga->displine + svga->y_add][svga->x_add];

		if (svga->firstline_draw == 2000)
			svga->firstline_draw = svga->displine;
		svga->lastline_draw = svga->displine;

		for (x = 0; x <= (svga->hdisp + svga->scrollcache); x += 8) {
			addr = svga->remap_func(svga, svga->ma);

			dat[0] = svga->vram[addr];
			dat[1] = svga->vram[addr | 0x1];
			if (svga->seqregs[1] & 4)
				svga->ma += 2;
			else
				svga->ma += 4;

			svga->ma &= svga->vram_mask;

			if (svga->crtc[0x17] & 0x80) {
				p[0] = svga->pallook[svga->egapal[(dat[0] >> 6) & 3]];
				p[1] = svga->pallook[svga->egapal[(dat[0] >> 4) & 3]];
				p[2] = svga->pallook[svga->egapal[(dat[0] >> 2) & 3]];
				p[3] = svga->pallook[svga->egapal[dat[0] & 3]];
				p[4] = svga->pallook[svga->egapal[(dat[1] >> 6) & 3]];
				p[5] = svga->pallook[svga->egapal[(dat[1] >> 4) & 3]];
				p[6] = svga->pallook[svga->egapal[(dat[1] >> 2) & 3]];
				p[7] = svga->pallook[svga->egapal[dat[1] & 3]];
			} else
				memset(p, 0x00, 8 * sizeof(uint32_t));

			p += 8;
		}
		}	
	}
}


void
svga_render_2bpp_headland_highres(svga_t *svga)
{
    int x;
    int oddeven;
    uint32_t addr, *p;
    uint8_t edat[4];
    uint8_t dat;
	uint32_t changed_addr;

    if ((svga->displine + svga->y_add) < 0)
	return;

    changed_addr = svga->remap_func(svga, svga->ma);

     if (svga->changedvram[changed_addr >> 12] || svga->changedvram[(changed_addr >> 12) + 1] || svga->fullchange) {
	p = &buffer32->line[svga->displine + svga->y_add][svga->x_add];

	if (svga->firstline_draw == 2000)
		svga->firstline_draw = svga->displine;
	svga->lastline_draw = svga->displine;

	for (x = 0; x <= (svga->hdisp + svga->scrollcache); x += 8) {
		addr = svga->remap_func(svga, svga->ma);
		oddeven = 0;

		if (svga->seqregs[1] & 4) {
			oddeven = (addr & 4) ? 1 : 0;
			edat[0] = svga->vram[addr | oddeven];
			edat[2] = svga->vram[addr | oddeven | 0x2];
			edat[1] = edat[3] = 0;
		} else {
			*(uint32_t *)(&edat[0]) = *(uint32_t *)(&svga->vram[addr]);
		}
		svga->ma += 4;
		svga->ma &= svga->vram_mask;

		if (svga->crtc[0x17] & 0x80) {
			dat = edatlookup[edat[0] >> 6][edat[1] >> 6] | (edatlookup[edat[2] >> 6][edat[3] >> 6] << 2);
			p[0] = svga->pallook[svga->egapal[(dat >> 4) & svga->plane_mask]];
			p[1] = svga->pallook[svga->egapal[dat & svga->plane_mask]];
			dat = edatlookup[(edat[0] >> 4) & 3][(edat[1] >> 4) & 3] | (edatlookup[(edat[2] >> 4) & 3][(edat[3] >> 4) & 3] << 2);
			p[2] = svga->pallook[svga->egapal[(dat >> 4) & svga->plane_mask]];
			p[3] = svga->pallook[svga->egapal[dat & svga->plane_mask]];
			dat = edatlookup[(edat[0] >> 2) & 3][(edat[1] >> 2) & 3] | (edatlookup[(edat[2] >> 2) & 3][(edat[3] >> 2) & 3] << 2);
			p[4] = svga->pallook[svga->egapal[(dat >> 4) & svga->plane_mask]];
			p[5] = svga->pallook[svga->egapal[dat & svga->plane_mask]];
			dat = edatlookup[edat[0] & 3][edat[1] & 3] | (edatlookup[edat[2] & 3][edat[3] & 3] << 2);
			p[6] = svga->pallook[svga->egapal[(dat >> 4) & svga->plane_mask]];
			p[7] = svga->pallook[svga->egapal[dat & svga->plane_mask]];
		} else
			memset(p, 0x00, 8 * sizeof(uint32_t));

		p += 8;
	}
    }
}

void
svga_render_4bpp_lowres(svga_t *svga)
{
    int x, oddeven;
    uint32_t addr, *p;
    uint8_t edat[4];
    uint8_t dat;
	uint32_t changed_addr;

    if ((svga->displine + svga->y_add) < 0)
		return;

	if (svga->force_old_addr) {
		if (svga->changedvram[svga->ma >> 12] || svga->changedvram[(svga->ma >> 12) + 1] || svga->fullchange) {
		p = &buffer32->line[svga->displine + svga->y_add][svga->x_add];

		if (svga->firstline_draw == 2000) 
			svga->firstline_draw = svga->displine;
		svga->lastline_draw = svga->displine;

		for (x = 0; x <= (svga->hdisp + svga->scrollcache); x += 16) {
			addr = svga->ma;
			oddeven = 0;

			if (!(svga->crtc[0x17] & 0x40)) {
				addr = (addr << 1) & svga->vram_mask;

				if (svga->seqregs[1] & 4)
					oddeven = (addr & 4) ? 1 : 0;

				addr &= ~7;

				if ((svga->crtc[0x17] & 0x20) && (svga->ma & 0x20000))
					addr |= 4;
				if (!(svga->crtc[0x17] & 0x20) && (svga->ma & 0x8000))
					addr |= 4;
			}

			if (!(svga->crtc[0x17] & 0x01))
				addr = (addr & ~0x8000) | ((svga->sc & 1) ? 0x8000 : 0);
			if (!(svga->crtc[0x17] & 0x02))
				addr = (addr & ~0x10000) | ((svga->sc & 2) ? 0x10000 : 0);

			if (svga->seqregs[1] & 4) {
				edat[0] = svga->vram[addr | oddeven];
				edat[2] = svga->vram[addr | oddeven | 0x2];
					edat[1] = edat[3] = 0;
				svga->ma += 2;
			} else {
				*(uint32_t *)(&edat[0]) = *(uint32_t *)(&svga->vram[addr]);
				svga->ma += 4;
			}
			svga->ma &= svga->vram_mask;

			if (svga->crtc[0x17] & 0x80) {
				dat = edatlookup[edat[0] >> 6][edat[1] >> 6] | (edatlookup[edat[2] >> 6][edat[3] >> 6] << 2);
				p[0]  = p[1]  = svga->pallook[svga->egapal[(dat >> 4) & svga->plane_mask]];
				p[2]  = p[3]  = svga->pallook[svga->egapal[dat & svga->plane_mask]];
				dat = edatlookup[(edat[0] >> 4) & 3][(edat[1] >> 4) & 3] | (edatlookup[(edat[2] >> 4) & 3][(edat[3] >> 4) & 3] << 2);
				p[4]  = p[5]  = svga->pallook[svga->egapal[(dat >> 4) & svga->plane_mask]];
				p[6]  = p[7]  = svga->pallook[svga->egapal[dat & svga->plane_mask]];
				dat = edatlookup[(edat[0] >> 2) & 3][(edat[1] >> 2) & 3] | (edatlookup[(edat[2] >> 2) & 3][(edat[3] >> 2) & 3] << 2);
				p[8]  = p[9]  = svga->pallook[svga->egapal[(dat >> 4) & svga->plane_mask]];
				p[10] = p[11] = svga->pallook[svga->egapal[dat & svga->plane_mask]];
				dat = edatlookup[edat[0] & 3][edat[1] & 3] | (edatlookup[edat[2] & 3][edat[3] & 3] << 2);
				p[12] = p[13] = svga->pallook[svga->egapal[(dat >> 4) & svga->plane_mask]];
				p[14] = p[15] = svga->pallook[svga->egapal[dat & svga->plane_mask]];
			} else
				memset(p, 0x00, 16 * sizeof(uint32_t));

			p += 16;
		}
		}	
	} else {
		changed_addr = svga->remap_func(svga, svga->ma);

		if (svga->changedvram[changed_addr >> 12] || svga->changedvram[(changed_addr >> 12) + 1] || svga->fullchange) {
		p = &buffer32->line[svga->displine + svga->y_add][svga->x_add];

		if (svga->firstline_draw == 2000)
			svga->firstline_draw = svga->displine;
		svga->lastline_draw = svga->displine;

		for (x = 0; x <= (svga->hdisp + svga->scrollcache); x += 16) {
			addr = svga->remap_func(svga, svga->ma);
			oddeven = 0;

			if (svga->seqregs[1] & 4) {
				oddeven = (addr & 4) ? 1 : 0;
				edat[0] = svga->vram[addr | oddeven];
				edat[2] = svga->vram[addr | oddeven | 0x2];
					edat[1] = edat[3] = 0;
				svga->ma += 2;
			} else {
				*(uint32_t *)(&edat[0]) = *(uint32_t *)(&svga->vram[addr]);
				svga->ma += 4;
			}
			svga->ma &= svga->vram_mask;

			if (svga->crtc[0x17] & 0x80) {
				dat = edatlookup[edat[0] >> 6][edat[1] >> 6] | (edatlookup[edat[2] >> 6][edat[3] >> 6] << 2);
				p[0]  = p[1]  = svga->pallook[svga->egapal[(dat >> 4) & svga->plane_mask]];
				p[2]  = p[3]  = svga->pallook[svga->egapal[dat & svga->plane_mask]];
				dat = edatlookup[(edat[0] >> 4) & 3][(edat[1] >> 4) & 3] | (edatlookup[(edat[2] >> 4) & 3][(edat[3] >> 4) & 3] << 2);
				p[4]  = p[5]  = svga->pallook[svga->egapal[(dat >> 4) & svga->plane_mask]];
				p[6]  = p[7]  = svga->pallook[svga->egapal[dat & svga->plane_mask]];
				dat = edatlookup[(edat[0] >> 2) & 3][(edat[1] >> 2) & 3] | (edatlookup[(edat[2] >> 2) & 3][(edat[3] >> 2) & 3] << 2);
				p[8]  = p[9]  = svga->pallook[svga->egapal[(dat >> 4) & svga->plane_mask]];
				p[10] = p[11] = svga->pallook[svga->egapal[dat & svga->plane_mask]];
				dat = edatlookup[edat[0] & 3][edat[1] & 3] | (edatlookup[edat[2] & 3][edat[3] & 3] << 2);
				p[12] = p[13] = svga->pallook[svga->egapal[(dat >> 4) & svga->plane_mask]];
				p[14] = p[15] = svga->pallook[svga->egapal[dat & svga->plane_mask]];
			} else
				memset(p, 0x00, 16 * sizeof(uint32_t));

			p += 16;
		}
		}	
	}
}


void
svga_render_4bpp_highres(svga_t *svga)
{
	int changed_offset;
    int x, oddeven;
    uint32_t addr, *p;
    uint8_t edat[4];
    uint8_t dat;
	uint32_t changed_addr;

    if ((svga->displine + svga->y_add) < 0)
		return;
	
	if (svga->force_old_addr) {
		changed_offset = (svga->ma + (svga->sc & ~svga->crtc[0x17] & 3) * 0x8000) >> 12;

		if (svga->changedvram[changed_offset] || svga->changedvram[changed_offset + 1] || svga->fullchange) {
		p = &buffer32->line[svga->displine + svga->y_add][svga->x_add];

		if (svga->firstline_draw == 2000) 
			svga->firstline_draw = svga->displine;
		svga->lastline_draw = svga->displine;

		for (x = 0; x <= (svga->hdisp + svga->scrollcache); x += 8) {
			addr = svga->ma;
			oddeven = 0;

			if (!(svga->crtc[0x17] & 0x40)) {
				addr = (addr << 1) & svga->vram_mask;

				if (svga->seqregs[1] & 4)
					oddeven = (addr & 4) ? 1 : 0;

				addr &= ~7;

				if ((svga->crtc[0x17] & 0x20) && (svga->ma & 0x20000))
					addr |= 4;
				if (!(svga->crtc[0x17] & 0x20) && (svga->ma & 0x8000))
					addr |= 4;
			}

			if (!(svga->crtc[0x17] & 0x01))
				addr = (addr & ~0x8000) | ((svga->sc & 1) ? 0x8000 : 0);
			if (!(svga->crtc[0x17] & 0x02))
				addr = (addr & ~0x10000) | ((svga->sc & 2) ? 0x10000 : 0);

			if (svga->seqregs[1] & 4) {
				edat[0] = svga->vram[addr | oddeven];
				edat[2] = svga->vram[addr | oddeven | 0x2];
					edat[1] = edat[3] = 0;
				svga->ma += 2;
			} else {
				*(uint32_t *)(&edat[0]) = *(uint32_t *)(&svga->vram[addr]);
				svga->ma += 4;
			}
			svga->ma &= svga->vram_mask;

			if (svga->crtc[0x17] & 0x80) {
				dat = edatlookup[edat[0] >> 6][edat[1] >> 6] | (edatlookup[edat[2] >> 6][edat[3] >> 6] << 2);
				p[0] = svga->pallook[svga->egapal[(dat >> 4) & svga->plane_mask]];
				p[1] = svga->pallook[svga->egapal[dat & svga->plane_mask]];
				dat = edatlookup[(edat[0] >> 4) & 3][(edat[1] >> 4) & 3] | (edatlookup[(edat[2] >> 4) & 3][(edat[3] >> 4) & 3] << 2);
				p[2] = svga->pallook[svga->egapal[(dat >> 4) & svga->plane_mask]];
				p[3] = svga->pallook[svga->egapal[dat & svga->plane_mask]];
				dat = edatlookup[(edat[0] >> 2) & 3][(edat[1] >> 2) & 3] | (edatlookup[(edat[2] >> 2) & 3][(edat[3] >> 2) & 3] << 2);
				p[4] = svga->pallook[svga->egapal[(dat >> 4) & svga->plane_mask]];
				p[5] = svga->pallook[svga->egapal[dat & svga->plane_mask]];
				dat = edatlookup[edat[0] & 3][edat[1] & 3] | (edatlookup[edat[2] & 3][edat[3] & 3] << 2);
				p[6] = svga->pallook[svga->egapal[(dat >> 4) & svga->plane_mask]];
				p[7] = svga->pallook[svga->egapal[dat & svga->plane_mask]];
			} else
				memset(p, 0x00, 8 * sizeof(uint32_t));

			p += 8;
		}
		}	
	} else {
		changed_addr = svga->remap_func(svga, svga->ma);

		if (svga->changedvram[changed_addr >> 12] || svga->changedvram[(changed_addr >> 12) + 1] || svga->fullchange) {
		p = &buffer32->line[svga->displine + svga->y_add][svga->x_add];

		if (svga->firstline_draw == 2000)
			svga->firstline_draw = svga->displine;
		svga->lastline_draw = svga->displine;

		for (x = 0; x <= (svga->hdisp + svga->scrollcache); x += 8) {
			addr = svga->remap_func(svga, svga->ma);
			oddeven = 0;

			if (svga->seqregs[1] & 4) {
				oddeven = (addr & 4) ? 1 : 0;
				edat[0] = svga->vram[addr | oddeven];
				edat[2] = svga->vram[addr | oddeven | 0x2];
					edat[1] = edat[3] = 0;
				svga->ma += 2;
			} else {
				*(uint32_t *)(&edat[0]) = *(uint32_t *)(&svga->vram[addr]);
				svga->ma += 4;
			}
			svga->ma &= svga->vram_mask;

			if (svga->crtc[0x17] & 0x80) {
				dat = edatlookup[edat[0] >> 6][edat[1] >> 6] | (edatlookup[edat[2] >> 6][edat[3] >> 6] << 2);
				p[0] = svga->pallook[svga->egapal[(dat >> 4) & svga->plane_mask]];
				p[1] = svga->pallook[svga->egapal[dat & svga->plane_mask]];
				dat = edatlookup[(edat[0] >> 4) & 3][(edat[1] >> 4) & 3] | (edatlookup[(edat[2] >> 4) & 3][(edat[3] >> 4) & 3] << 2);
				p[2] = svga->pallook[svga->egapal[(dat >> 4) & svga->plane_mask]];
				p[3] = svga->pallook[svga->egapal[dat & svga->plane_mask]];
				dat = edatlookup[(edat[0] >> 2) & 3][(edat[1] >> 2) & 3] | (edatlookup[(edat[2] >> 2) & 3][(edat[3] >> 2) & 3] << 2);
				p[4] = svga->pallook[svga->egapal[(dat >> 4) & svga->plane_mask]];
				p[5] = svga->pallook[svga->egapal[dat & svga->plane_mask]];
				dat = edatlookup[edat[0] & 3][edat[1] & 3] | (edatlookup[edat[2] & 3][edat[3] & 3] << 2);
				p[6] = svga->pallook[svga->egapal[(dat >> 4) & svga->plane_mask]];
				p[7] = svga->pallook[svga->egapal[dat & svga->plane_mask]];
			} else
				memset(p, 0x00, 8 * sizeof(uint32_t));

			p += 8;
		}
		}
	}
}


void
svga_render_8bpp_lowres(svga_t *svga)
{
    int x;
    uint32_t *p;
    uint32_t dat;
	uint32_t changed_addr;
	uint32_t addr;

    if ((svga->displine + svga->y_add) < 0)
		return;
	
	if (svga->force_old_addr) {
		if (svga->changedvram[svga->ma >> 12] || svga->changedvram[(svga->ma >> 12) + 1] || svga->fullchange) {
		p = &buffer32->line[svga->displine + svga->y_add][svga->x_add];

		if (svga->firstline_draw == 2000) 
			svga->firstline_draw = svga->displine;
		svga->lastline_draw = svga->displine;

		for (x = 0; x <= (svga->hdisp + svga->scrollcache); x += 8) {
			dat = *(uint32_t *)(&svga->vram[svga->ma & svga->vram_display_mask]);

			p[0] = p[1] = svga->map8[dat & 0xff];
			p[2] = p[3] = svga->map8[(dat >> 8) & 0xff];
			p[4] = p[5] = svga->map8[(dat >> 16) & 0xff];
			p[6] = p[7] = svga->map8[(dat >> 24) & 0xff];

			svga->ma += 4;
			p += 8;
		}
		svga->ma &= svga->vram_display_mask;
		}
	} else {
		changed_addr = svga->remap_func(svga, svga->ma);

		if (svga->changedvram[changed_addr >> 12] || svga->changedvram[(changed_addr >> 12) + 1] || svga->fullchange) {
		p = &buffer32->line[svga->displine + svga->y_add][svga->x_add];

		if (svga->firstline_draw == 2000)
			svga->firstline_draw = svga->displine;
		svga->lastline_draw = svga->displine;

		if (!svga->remap_required) {
			for (x = 0; x <= (svga->hdisp + svga->scrollcache); x += 8) {
				dat = *(uint32_t *)(&svga->vram[svga->ma & svga->vram_display_mask]);
				p[0] = p[1] = svga->map8[dat & 0xff];
				p[2] = p[3] = svga->map8[(dat >> 8) & 0xff];
				p[4] = p[5] = svga->map8[(dat >> 16) & 0xff];
				p[6] = p[7] = svga->map8[(dat >> 24) & 0xff];

				svga->ma += 4;
				p += 8;
			}
		} else {
			for (x = 0; x <= (svga->hdisp + svga->scrollcache); x += 8) {
				addr = svga->remap_func(svga, svga->ma);
				dat = *(uint32_t *)(&svga->vram[addr & svga->vram_display_mask]);
				p[0] = p[1] = svga->map8[dat & 0xff];
				p[2] = p[3] = svga->map8[(dat >> 8) & 0xff];
				p[4] = p[5] = svga->map8[(dat >> 16) & 0xff];
				p[6] = p[7] = svga->map8[(dat >> 24) & 0xff];

				svga->ma += 4;
				p += 8;
			}
		}
		svga->ma &= svga->vram_display_mask;
		}
	}
}


void
svga_render_8bpp_highres(svga_t *svga)
{
    int x;
    uint32_t *p;
    uint32_t dat;
	uint32_t changed_addr;
	uint32_t addr;

    if ((svga->displine + svga->y_add) < 0)
		return;
	
	if (svga->force_old_addr) {
		if (svga->changedvram[svga->ma >> 12] || svga->changedvram[(svga->ma >> 12) + 1] || svga->fullchange) {
		p = &buffer32->line[svga->displine + svga->y_add][svga->x_add];

		if (svga->firstline_draw == 2000) 
			svga->firstline_draw = svga->displine;
		svga->lastline_draw = svga->displine;

		for (x = 0; x <= (svga->hdisp/* + svga->scrollcache*/); x += 8) {
			dat = *(uint32_t *)(&svga->vram[svga->ma & svga->vram_display_mask]);
			p[0] = svga->map8[dat & 0xff];
			p[1] = svga->map8[(dat >> 8) & 0xff];
			p[2] = svga->map8[(dat >> 16) & 0xff];
			p[3] = svga->map8[(dat >> 24) & 0xff];

			dat = *(uint32_t *)(&svga->vram[(svga->ma + 4) & svga->vram_display_mask]);
			p[4] = svga->map8[dat & 0xff];
			p[5] = svga->map8[(dat >> 8) & 0xff];
			p[6] = svga->map8[(dat >> 16) & 0xff];
			p[7] = svga->map8[(dat >> 24) & 0xff];

			svga->ma += 8;
			p += 8;
		}
		svga->ma &= svga->vram_display_mask;
		}
	} else {
		changed_addr = svga->remap_func(svga, svga->ma);

		if (svga->changedvram[changed_addr >> 12] || svga->changedvram[(changed_addr >> 12) + 1] || svga->fullchange) {
		p = &buffer32->line[svga->displine + svga->y_add][svga->x_add];

		if (svga->firstline_draw == 2000)
			svga->firstline_draw = svga->displine;
		svga->lastline_draw = svga->displine;

		if (!svga->remap_required) {
			for (x = 0; x <= (svga->hdisp/* + svga->scrollcache*/); x += 8) {
				dat = *(uint32_t *)(&svga->vram[svga->ma & svga->vram_display_mask]);
				p[0] = svga->map8[dat & 0xff];
				p[1] = svga->map8[(dat >> 8) & 0xff];
				p[2] = svga->map8[(dat >> 16) & 0xff];
				p[3] = svga->map8[(dat >> 24) & 0xff];

				dat = *(uint32_t *)(&svga->vram[(svga->ma + 4) & svga->vram_display_mask]);
				p[4] = svga->map8[dat & 0xff];
				p[5] = svga->map8[(dat >> 8) & 0xff];
				p[6] = svga->map8[(dat >> 16) & 0xff];
				p[7] = svga->map8[(dat >> 24) & 0xff];

				svga->ma += 8;
				p += 8;
			}
		} else {
			for (x = 0; x <= (svga->hdisp/* + svga->scrollcache*/); x += 4) {
				addr = svga->remap_func(svga, svga->ma);
				dat = *(uint32_t *)(&svga->vram[addr & svga->vram_display_mask]);
				p[0] = svga->map8[dat & 0xff];
				p[1] = svga->map8[(dat >> 8) & 0xff];
				p[2] = svga->map8[(dat >> 16) & 0xff];
				p[3] = svga->map8[(dat >> 24) & 0xff];

				svga->ma += 4;
				p += 4;
			}
		}
		svga->ma &= svga->vram_display_mask;
		}
	}
}

void
svga_render_8bpp_tseng_lowres(svga_t *svga)
{
    int x;
    uint32_t *p;
    uint32_t dat;

    if ((svga->displine + svga->y_add) < 0)
	return;

    if (svga->changedvram[svga->ma >> 12] || svga->changedvram[(svga->ma >> 12) + 1] || svga->fullchange) {
	p = &buffer32->line[svga->displine + svga->y_add][svga->x_add];

	if (svga->firstline_draw == 2000)
		svga->firstline_draw = svga->displine;
	svga->lastline_draw = svga->displine;

	for (x = 0; x <= (svga->hdisp + svga->scrollcache); x += 8) {
		if (svga->crtc[0x17] & 0x80) {
			dat = *(uint32_t *)(&svga->vram[svga->ma & svga->vram_display_mask]);
			if (svga->attrregs[0x10] & 0x80)
				dat = (dat & ~0xf0) | ((svga->attrregs[0x14] & 0x0f) << 4);
			p[0] = p[1] = svga->map8[dat & 0xff];
			dat >>= 8;
			if (svga->attrregs[0x10] & 0x80)
				dat = (dat & ~0xf0) | ((svga->attrregs[0x14] & 0x0f) << 4);
			p[2] = p[3] = svga->map8[dat & 0xff];
			dat >>= 8;
			if (svga->attrregs[0x10] & 0x80)
				dat = (dat & ~0xf0) | ((svga->attrregs[0x14] & 0x0f) << 4);
			p[4] = p[5] = svga->map8[dat & 0xff];
			dat >>= 8;
			if (svga->attrregs[0x10] & 0x80)
				dat = (dat & ~0xf0) | ((svga->attrregs[0x14] & 0x0f) << 4);
			p[6] = p[7] = svga->map8[dat & 0xff];
		} else
			memset(p, 0x00, 8 * sizeof(uint32_t));

		svga->ma += 4;
		p += 8;
	}
	svga->ma &= svga->vram_display_mask;
    }
}


void
svga_render_8bpp_tseng_highres(svga_t *svga)
{
    int x;
    uint32_t *p;
    uint32_t dat;

    if ((svga->displine + svga->y_add) < 0)
	return;

    if (svga->changedvram[svga->ma >> 12] || svga->changedvram[(svga->ma >> 12) + 1] || svga->fullchange) {
	p = &buffer32->line[svga->displine + svga->y_add][svga->x_add];

	if (svga->firstline_draw == 2000)
		svga->firstline_draw = svga->displine;
	svga->lastline_draw = svga->displine;

	for (x = 0; x <= (svga->hdisp/* + svga->scrollcache*/); x += 8) {
		if (svga->crtc[0x17] & 0x80) {
			dat = *(uint32_t *)(&svga->vram[svga->ma & svga->vram_display_mask]);
			if (svga->attrregs[0x10] & 0x80)
				dat = (dat & ~0xf0) | ((svga->attrregs[0x14] & 0x0f) << 4);
			p[0] = svga->map8[dat & 0xff];
			dat >>= 8;
			if (svga->attrregs[0x10] & 0x80)
				dat = (dat & ~0xf0) | ((svga->attrregs[0x14] & 0x0f) << 4);
			p[1] = svga->map8[dat & 0xff];
			dat >>= 8;
			if (svga->attrregs[0x10] & 0x80)
				dat = (dat & ~0xf0) | ((svga->attrregs[0x14] & 0x0f) << 4);
			p[2] = svga->map8[dat & 0xff];
			dat >>= 8;
			if (svga->attrregs[0x10] & 0x80)
				dat = (dat & ~0xf0) | ((svga->attrregs[0x14] & 0x0f) << 4);
			p[3] = svga->map8[dat & 0xff];

			dat = *(uint32_t *)(&svga->vram[(svga->ma + 4) & svga->vram_display_mask]);
			if (svga->attrregs[0x10] & 0x80)
				dat = (dat & ~0xf0) | ((svga->attrregs[0x14] & 0x0f) << 4);
			p[4] = svga->map8[dat & 0xff];
			dat >>= 8;
			if (svga->attrregs[0x10] & 0x80)
				dat = (dat & ~0xf0) | ((svga->attrregs[0x14] & 0x0f) << 4);
			p[5] = svga->map8[dat & 0xff];
			dat >>= 8;
			if (svga->attrregs[0x10] & 0x80)
				dat = (dat & ~0xf0) | ((svga->attrregs[0x14] & 0x0f) << 4);
			p[6] = svga->map8[dat & 0xff];
			dat >>= 8;
			if (svga->attrregs[0x10] & 0x80)
				dat = (dat & ~0xf0) | ((svga->attrregs[0x14] & 0x0f) << 4);
			p[7] = svga->map8[dat & 0xff];
		} else
			memset(p, 0x00, 8 * sizeof(uint32_t));

		svga->ma += 8;
		p += 8;
	}
	svga->ma &= svga->vram_display_mask;
    }
}


void
svga_render_15bpp_lowres(svga_t *svga)
{
    int x;
    uint32_t *p;
    uint32_t dat;
	uint32_t changed_addr, addr;

    if ((svga->displine + svga->y_add) < 0)
		return;
	
	if (svga->force_old_addr) {
		if (svga->changedvram[svga->ma >> 12] || svga->changedvram[(svga->ma >> 12) + 1] || svga->fullchange) {
		p = &buffer32->line[svga->displine + svga->y_add][svga->x_add];

		if (svga->firstline_draw == 2000) 
			svga->firstline_draw = svga->displine;
		svga->lastline_draw = svga->displine;

		for (x = 0; x <= (svga->hdisp + svga->scrollcache); x += 4) {
			if (svga->crtc[0x17] & 0x80) {
				dat = *(uint32_t *)(&svga->vram[(svga->ma + (x << 1)) & svga->vram_display_mask]);

				p[(x << 1)]     = p[(x << 1) + 1] = video_15to32[dat & 0xffff];
				p[(x << 1) + 2] = p[(x << 1) + 3] = video_15to32[dat >> 16];

				dat = *(uint32_t *)(&svga->vram[(svga->ma + (x << 1) + 4) & svga->vram_display_mask]);

				p[(x << 1) + 4] = p[(x << 1) + 5] = video_15to32[dat & 0xffff];
				p[(x << 1) + 6] = p[(x << 1) + 7] = video_15to32[dat >> 16];
			} else
				memset(&(p[(x << 1)]), 0x00, 8 * sizeof(uint32_t));
		}
		svga->ma += x << 1;
		svga->ma &= svga->vram_display_mask;
		}
	} else {
		changed_addr = svga->remap_func(svga, svga->ma);

		if (svga->changedvram[changed_addr >> 12] || svga->changedvram[(changed_addr >> 12) + 1] || svga->fullchange) {
		p = &buffer32->line[svga->displine + svga->y_add][svga->x_add];

		if (svga->firstline_draw == 2000)
			svga->firstline_draw = svga->displine;
		svga->lastline_draw = svga->displine;

		if (!svga->remap_required) {
			for (x = 0; x <= (svga->hdisp + svga->scrollcache); x += 4) {
				if (svga->crtc[0x17] & 0x80) {
					dat = *(uint32_t *)(&svga->vram[(svga->ma + (x << 1)) & svga->vram_display_mask]);

					*p++ = video_15to32[dat & 0xffff];
					*p++ = video_15to32[dat >> 16];

					dat = *(uint32_t *)(&svga->vram[(svga->ma + (x << 1) + 4) & svga->vram_display_mask]);

					*p++ = video_15to32[dat & 0xffff];
					*p++ = video_15to32[dat >> 16];
				} else
					memset(&(p[(x << 1)]), 0x00, 8 * sizeof(uint32_t));
			}
			svga->ma += x << 1;
		} else {
			for (x = 0; x <= (svga->hdisp + svga->scrollcache); x += 2) {
				if (svga->crtc[0x17] & 0x80) {
					addr = svga->remap_func(svga, svga->ma);
					dat = *(uint32_t *)(&svga->vram[addr & svga->vram_display_mask]);

					*p++ = video_15to32[dat & 0xffff];
					*p++ = video_15to32[dat >> 16];
				} else
					memset(&(p[x]), 0x00, 2 * sizeof(uint32_t));
				svga->ma += 4;
			}
		}
		svga->ma &= svga->vram_display_mask;
		}
	}
}


void
svga_render_15bpp_highres(svga_t *svga)
{
    int x;
    uint32_t *p;
    uint32_t dat;
	uint32_t changed_addr, addr;

    if ((svga->displine + svga->y_add) < 0)
		return;
	
	if (svga->force_old_addr) {
		if (svga->changedvram[svga->ma >> 12] || svga->changedvram[(svga->ma >> 12) + 1] || svga->fullchange) {
		p = &buffer32->line[svga->displine + svga->y_add][svga->x_add];

		if (svga->firstline_draw == 2000) 
			svga->firstline_draw = svga->displine;
		svga->lastline_draw = svga->displine;

		for (x = 0; x <= (svga->hdisp + svga->scrollcache); x += 8) {
			if (svga->crtc[0x17] & 0x80) {
				dat = *(uint32_t *)(&svga->vram[(svga->ma + (x << 1)) & svga->vram_display_mask]);
				p[x]     = video_15to32[dat & 0xffff];
				p[x + 1] = video_15to32[dat >> 16];

				dat = *(uint32_t *)(&svga->vram[(svga->ma + (x << 1) + 4) & svga->vram_display_mask]);
				p[x + 2] = video_15to32[dat & 0xffff];
				p[x + 3] = video_15to32[dat >> 16];

				dat = *(uint32_t *)(&svga->vram[(svga->ma + (x << 1) + 8) & svga->vram_display_mask]);
				p[x + 4] = video_15to32[dat & 0xffff];
				p[x + 5] = video_15to32[dat >> 16];

				dat = *(uint32_t *)(&svga->vram[(svga->ma + (x << 1) + 12) & svga->vram_display_mask]);
				p[x + 6] = video_15to32[dat & 0xffff];
				p[x + 7] = video_15to32[dat >> 16];
			} else
				memset(&(p[x]), 0x00, 8 * sizeof(uint32_t));
		}
		svga->ma += x << 1; 
		svga->ma &= svga->vram_display_mask;
		}
	} else {
		changed_addr = svga->remap_func(svga, svga->ma);

		if (svga->changedvram[changed_addr >> 12] || svga->changedvram[(changed_addr >> 12) + 1] || svga->fullchange) {
		p = &buffer32->line[svga->displine + svga->y_add][svga->x_add];

		if (svga->firstline_draw == 2000)
			svga->firstline_draw = svga->displine;
		svga->lastline_draw = svga->displine;

		if (!svga->remap_required) {
			for (x = 0; x <= (svga->hdisp + svga->scrollcache); x += 8) {
				if (svga->crtc[0x17] & 0x80) {
					dat = *(uint32_t *)(&svga->vram[(svga->ma + (x << 1)) & svga->vram_display_mask]);
					*p++ = video_15to32[dat & 0xffff];
					*p++ = video_15to32[dat >> 16];

					dat = *(uint32_t *)(&svga->vram[(svga->ma + (x << 1) + 4) & svga->vram_display_mask]);
					*p++ = video_15to32[dat & 0xffff];
					*p++ = video_15to32[dat >> 16];

					dat = *(uint32_t *)(&svga->vram[(svga->ma + (x << 1) + 8) & svga->vram_display_mask]);
					*p++ = video_15to32[dat & 0xffff];
					*p++ = video_15to32[dat >> 16];

					dat = *(uint32_t *)(&svga->vram[(svga->ma + (x << 1) + 12) & svga->vram_display_mask]);
					*p++ = video_15to32[dat & 0xffff];
					*p++ = video_15to32[dat >> 16];
				} else
					memset(&(p[x]), 0x00, 8 * sizeof(uint32_t));
			}
			svga->ma += x << 1;
		} else {
			for (x = 0; x <= (svga->hdisp + svga->scrollcache); x += 2) {
				if (svga->crtc[0x17] & 0x80) {
					addr = svga->remap_func(svga, svga->ma);
					dat = *(uint32_t *)(&svga->vram[addr & svga->vram_display_mask]);

					*p++ = video_15to32[dat & 0xffff];
					*p++ = video_15to32[dat >> 16];
				} else
					memset(&(p[x]), 0x00, 2 * sizeof(uint32_t));
				svga->ma += 4;
			}
		}
		svga->ma &= svga->vram_display_mask;
		}
	}
}


void
svga_render_15bpp_mix_lowres(svga_t *svga)
{
    int x;
    uint32_t *p;
    uint32_t dat;

    if ((svga->displine + svga->y_add) < 0)
	return;

    if (svga->changedvram[svga->ma >> 12] || svga->changedvram[(svga->ma >> 12) + 1] || svga->fullchange) {
	p = &buffer32->line[svga->displine + svga->y_add][svga->x_add];

	if (svga->firstline_draw == 2000)
		svga->firstline_draw = svga->displine;
	svga->lastline_draw = svga->displine;

	for (x = 0; x <= (svga->hdisp + svga->scrollcache); x += 4) {
		if (svga->crtc[0x17] & 0x80) {
			dat = *(uint32_t *)(&svga->vram[(svga->ma + (x << 1)) & svga->vram_display_mask]);
			p[(x << 1)]     = p[(x << 1) + 1] = (dat & 0x00008000) ? svga->pallook[dat & 0xff] : video_15to32[dat & 0xffff];

			dat >>= 16;
			p[(x << 1) + 2] = p[(x << 1) + 3] = (dat & 0x00008000) ? svga->pallook[dat & 0xff] : video_15to32[dat & 0xffff];

			dat = *(uint32_t *)(&svga->vram[(svga->ma + (x << 1) + 4) & svga->vram_display_mask]);
			p[(x << 1) + 4] = p[(x << 1) + 5] = (dat & 0x00008000) ? svga->pallook[dat & 0xff] : video_15to32[dat & 0xffff];

			dat >>= 16;
			p[(x << 1) + 6] = p[(x << 1) + 7] = (dat & 0x00008000) ? svga->pallook[dat & 0xff] : video_15to32[dat & 0xffff];
		} else
			memset(&(p[(x << 1)]), 0x00, 8 * sizeof(uint32_t));
	}
	svga->ma += x << 1;
	svga->ma &= svga->vram_display_mask;
    }
}


void
svga_render_15bpp_mix_highres(svga_t *svga)
{
    int x;
    uint32_t *p;
    uint32_t dat;

    if ((svga->displine + svga->y_add) < 0)
	return;

    if (svga->changedvram[svga->ma >> 12] || svga->changedvram[(svga->ma >> 12) + 1] || svga->fullchange) {
	p = &buffer32->line[svga->displine + svga->y_add][svga->x_add];

	if (svga->firstline_draw == 2000)
		svga->firstline_draw = svga->displine;
	svga->lastline_draw = svga->displine;

	for (x = 0; x <= (svga->hdisp + svga->scrollcache); x += 8) {
		if (svga->crtc[0x17] & 0x80) {
			dat = *(uint32_t *)(&svga->vram[(svga->ma + (x << 1)) & svga->vram_display_mask]);
			p[x]     = (dat & 0x00008000) ? svga->pallook[dat & 0xff] : video_15to32[dat & 0xffff];
			dat >>= 16;
			p[x + 1] = (dat & 0x00008000) ? svga->pallook[dat & 0xff] : video_15to32[dat & 0xffff];

			dat = *(uint32_t *)(&svga->vram[(svga->ma + (x << 1) + 4) & svga->vram_display_mask]);
			p[x + 2] = (dat & 0x00008000) ? svga->pallook[dat & 0xff] : video_15to32[dat & 0xffff];
			dat >>= 16;
			p[x + 3] = (dat & 0x00008000) ? svga->pallook[dat & 0xff] : video_15to32[dat & 0xffff];

			dat = *(uint32_t *)(&svga->vram[(svga->ma + (x << 1) + 8) & svga->vram_display_mask]);
			p[x + 4] = (dat & 0x00008000) ? svga->pallook[dat & 0xff] : video_15to32[dat & 0xffff];
			dat >>= 16;
			p[x + 5] = (dat & 0x00008000) ? svga->pallook[dat & 0xff] : video_15to32[dat & 0xffff];

			dat = *(uint32_t *)(&svga->vram[(svga->ma + (x << 1) + 12) & svga->vram_display_mask]);
			p[x + 6] = (dat & 0x00008000) ? svga->pallook[dat & 0xff] : video_15to32[dat & 0xffff];
			dat >>= 16;
			p[x + 7] = (dat & 0x00008000) ? svga->pallook[dat & 0xff] : video_15to32[dat & 0xffff];
		} else
			memset(&(p[x]), 0x00, 8 * sizeof(uint32_t));
	}
	svga->ma += x << 1;
	svga->ma &= svga->vram_display_mask;
    }
}


void
svga_render_16bpp_lowres(svga_t *svga)
{
    int x;
    uint32_t *p;
    uint32_t dat;
	uint32_t changed_addr, addr;

    if ((svga->displine + svga->y_add) < 0)
		return;

	if (svga->force_old_addr) {
		if (svga->changedvram[svga->ma >> 12] || svga->changedvram[(svga->ma >> 12) + 1] || svga->fullchange) {
		p = &buffer32->line[svga->displine + svga->y_add][svga->x_add];

		if (svga->firstline_draw == 2000)
			svga->firstline_draw = svga->displine;
		svga->lastline_draw = svga->displine;

		for (x = 0; x <= (svga->hdisp + svga->scrollcache); x += 4) {
			if (svga->crtc[0x17] & 0x80) {
				dat = *(uint32_t *)(&svga->vram[(svga->ma + (x << 1)) & svga->vram_display_mask]);
				p[(x << 1)]     = p[(x << 1) + 1] = video_16to32[dat & 0xffff];
				p[(x << 1) + 2] = p[(x << 1) + 3] = video_16to32[dat >> 16];

				dat = *(uint32_t *)(&svga->vram[(svga->ma + (x << 1) + 4) & svga->vram_display_mask]);
				p[(x << 1) + 4] = p[(x << 1) + 5] = video_16to32[dat & 0xffff];
				p[(x << 1) + 6] = p[(x << 1) + 7] = video_16to32[dat >> 16];
			} else
				memset(&(p[(x << 1)]), 0x00, 8 * sizeof(uint32_t));
		}
		svga->ma += x << 1; 
		svga->ma &= svga->vram_display_mask;
		}
	} else {
		changed_addr = svga->remap_func(svga, svga->ma);

		if (svga->changedvram[changed_addr >> 12] || svga->changedvram[(changed_addr >> 12) + 1] || svga->fullchange) {
		p = &buffer32->line[svga->displine + svga->y_add][svga->x_add];

		if (svga->firstline_draw == 2000)
			svga->firstline_draw = svga->displine;
		svga->lastline_draw = svga->displine;

		if (!svga->remap_required) {
			for (x = 0; x <= (svga->hdisp + svga->scrollcache); x += 4) {
				if (svga->crtc[0x17] & 0x80) {
					dat = *(uint32_t *)(&svga->vram[(svga->ma + (x << 1)) & svga->vram_display_mask]);

					*p++ = video_16to32[dat & 0xffff];
					*p++ = video_16to32[dat >> 16];

					dat = *(uint32_t *)(&svga->vram[(svga->ma + (x << 1) + 4) & svga->vram_display_mask]);

					*p++ = video_16to32[dat & 0xffff];
					*p++ = video_16to32[dat >> 16];
				} else
					memset(&(p[(x << 1)]), 0x00, 8 * sizeof(uint32_t));
			}
			svga->ma += x << 1;
		} else {
			for (x = 0; x <= (svga->hdisp + svga->scrollcache); x += 2) {
				if (svga->crtc[0x17] & 0x80) {
				addr = svga->remap_func(svga, svga->ma);
				dat = *(uint32_t *)(&svga->vram[addr & svga->vram_display_mask]);

				*p++ = video_16to32[dat & 0xffff];
				*p++ = video_16to32[dat >> 16];
				} else
					memset(&(p[x]), 0x00, 2 * sizeof(uint32_t));
			}
			svga->ma += 4;
		}
		svga->ma &= svga->vram_display_mask;
		}	
	}
}


void
svga_render_16bpp_highres(svga_t *svga)
{
    int x;
    uint32_t *p;
	uint32_t dat;
	uint32_t changed_addr, addr;

    if ((svga->displine + svga->y_add) < 0)
		return;
	
	if (svga->force_old_addr) {
    if (svga->changedvram[svga->ma >> 12] || svga->changedvram[(svga->ma >> 12) + 1] || svga->fullchange) {
	p = &buffer32->line[svga->displine + svga->y_add][svga->x_add];

	if (svga->firstline_draw == 2000) 
		svga->firstline_draw = svga->displine;
	svga->lastline_draw = svga->displine;

	for (x = 0; x <= (svga->hdisp + svga->scrollcache); x += 8) {
		if (svga->crtc[0x17] & 0x80) {
			uint32_t dat = *(uint32_t *)(&svga->vram[(svga->ma + (x << 1)) & svga->vram_display_mask]);
			p[x]     = video_16to32[dat & 0xffff];
			p[x + 1] = video_16to32[dat >> 16];

			dat = *(uint32_t *)(&svga->vram[(svga->ma + (x << 1) + 4) & svga->vram_display_mask]);
			p[x + 2] = video_16to32[dat & 0xffff];
			p[x + 3] = video_16to32[dat >> 16];

			dat = *(uint32_t *)(&svga->vram[(svga->ma + (x << 1) + 8) & svga->vram_display_mask]);
			p[x + 4] = video_16to32[dat & 0xffff];
			p[x + 5] = video_16to32[dat >> 16];

			dat = *(uint32_t *)(&svga->vram[(svga->ma + (x << 1) + 12) & svga->vram_display_mask]);
			p[x + 6] = video_16to32[dat & 0xffff];
			p[x + 7] = video_16to32[dat >> 16];
		} else
			memset(&(p[x]), 0x00, 8 * sizeof(uint32_t));
	}
	svga->ma += x << 1; 
	svga->ma &= svga->vram_display_mask;
    }	
	} else {
		changed_addr = svga->remap_func(svga, svga->ma);

		if (svga->changedvram[changed_addr >> 12] || svga->changedvram[(changed_addr >> 12) + 1] || svga->fullchange) {
		p = &buffer32->line[svga->displine + svga->y_add][svga->x_add];

		if (svga->firstline_draw == 2000)
			svga->firstline_draw = svga->displine;
		svga->lastline_draw = svga->displine;

		if (!svga->remap_required) {
			for (x = 0; x <= (svga->hdisp + svga->scrollcache); x += 8) {
				if (svga->crtc[0x17] & 0x80) {
					dat = *(uint32_t *)(&svga->vram[(svga->ma + (x << 1)) & svga->vram_display_mask]);
					*p++ = video_16to32[dat & 0xffff];
					*p++ = video_16to32[dat >> 16];

					dat = *(uint32_t *)(&svga->vram[(svga->ma + (x << 1) + 4) & svga->vram_display_mask]);
					*p++ = video_16to32[dat & 0xffff];
					*p++ = video_16to32[dat >> 16];

					dat = *(uint32_t *)(&svga->vram[(svga->ma + (x << 1) + 8) & svga->vram_display_mask]);
					*p++ = video_16to32[dat & 0xffff];
					*p++ = video_16to32[dat >> 16];

					dat = *(uint32_t *)(&svga->vram[(svga->ma + (x << 1) + 12) & svga->vram_display_mask]);
					*p++ = video_16to32[dat & 0xffff];
					*p++ = video_16to32[dat >> 16];
				} else
					memset(&(p[x]), 0x00, 8 * sizeof(uint32_t));
			}
			svga->ma += x << 1;
		} else {
			for (x = 0; x <= (svga->hdisp + svga->scrollcache); x += 2) {
				if (svga->crtc[0x17] & 0x80) {
					addr = svga->remap_func(svga, svga->ma);
					dat = *(uint32_t *)(&svga->vram[addr & svga->vram_display_mask]);

					*p++ = video_16to32[dat & 0xffff];
					*p++ = video_16to32[dat >> 16];
				} else
					memset(&(p[x]), 0x00, 2 * sizeof(uint32_t));

				svga->ma += 4;
			}
		}
		svga->ma &= svga->vram_display_mask;
		}	
	}
}


void
svga_render_24bpp_lowres(svga_t *svga)
{
    int x;
    uint32_t *p;
	uint32_t changed_addr, addr;
	uint32_t dat0, dat1, dat2;
	uint32_t fg;

    if ((svga->displine + svga->y_add) < 0)
		return;
	
	if (svga->force_old_addr) {
		if ((svga->displine + svga->y_add) < 0)
		return;

		if (svga->changedvram[svga->ma >> 12] || svga->changedvram[(svga->ma >> 12) + 1] || svga->fullchange) {
		if (svga->firstline_draw == 2000) 
			svga->firstline_draw = svga->displine;
		svga->lastline_draw = svga->displine;

		for (x = 0; x <= (svga->hdisp + svga->scrollcache); x++) {
			if (svga->crtc[0x17] & 0x80)
				fg = svga->vram[svga->ma] | (svga->vram[svga->ma + 1] << 8) | (svga->vram[svga->ma + 2] << 16);
			else
				fg = 0x00000000;
			svga->ma += 3; 
			svga->ma &= svga->vram_display_mask;
			buffer32->line[svga->displine + svga->y_add][(x << 1) + svga->x_add] =
			buffer32->line[svga->displine + svga->y_add][(x << 1) + 1 + svga->x_add] = fg;
		}
		}
	} else {
		changed_addr = svga->remap_func(svga, svga->ma);

		if (svga->changedvram[changed_addr >> 12] || svga->changedvram[(changed_addr >> 12) + 1] || svga->fullchange) {
		p = &buffer32->line[svga->displine + svga->y_add][svga->x_add];

		if (svga->firstline_draw == 2000)
			svga->firstline_draw = svga->displine;
		svga->lastline_draw = svga->displine;

		if (!svga->remap_required) {
			for (x = 0; x <= (svga->hdisp + svga->scrollcache); x++) {
				if (svga->crtc[0x17] & 0x80) {
					dat0 = *(uint32_t *)(&svga->vram[svga->ma & svga->vram_display_mask]);
					dat1 = *(uint32_t *)(&svga->vram[(svga->ma + 4) & svga->vram_display_mask]);
					dat2 = *(uint32_t *)(&svga->vram[(svga->ma + 8) & svga->vram_display_mask]);
				} else
					dat0 = dat1 = dat2 = 0x00000000;

				p[0] = p[1] = dat0 & 0xffffff;
				p[2] = p[3] = (dat0 >> 24) | ((dat1 & 0xffff) << 8);
				p[4] = p[5] = (dat1 >> 16) | ((dat2 & 0xff) << 16);
				p[6] = p[7] = dat2 >> 8;

				svga->ma += 12;
			}
		} else {
			for (x = 0; x <= (svga->hdisp + svga->scrollcache); x += 4) {
				if (svga->crtc[0x17] & 0x80) {
					addr = svga->remap_func(svga, svga->ma);
					dat0 = *(uint32_t *)(&svga->vram[addr & svga->vram_display_mask]);
					addr = svga->remap_func(svga, svga->ma + 4);
					dat1 = *(uint32_t *)(&svga->vram[addr & svga->vram_display_mask]);
					addr = svga->remap_func(svga, svga->ma + 8);
					dat2 = *(uint32_t *)(&svga->vram[addr & svga->vram_display_mask]);
				} else
					dat0 = dat1 = dat2 = 0x00000000;

				p[0] = p[1] = dat0 & 0xffffff;
				p[2] = p[3] = (dat0 >> 24) | ((dat1 & 0xffff) << 8);
				p[4] = p[5] = (dat1 >> 16) | ((dat2 & 0xff) << 16);
				p[6] = p[7] = dat2 >> 8;

				svga->ma += 12;
			}
		}
		svga->ma &= svga->vram_display_mask;
		}
	}
}


void
svga_render_24bpp_highres(svga_t *svga)
{
    int x;
    uint32_t *p;
	uint32_t changed_addr, addr;
	uint32_t dat0, dat1, dat2;
	uint32_t dat;

    if ((svga->displine + svga->y_add) < 0)
		return;
	
	if (svga->force_old_addr) {
		if (svga->changedvram[svga->ma >> 12] || svga->changedvram[(svga->ma >> 12) + 1] || svga->fullchange) {
		p = &buffer32->line[svga->displine + svga->y_add][svga->x_add];

		if (svga->firstline_draw == 2000) 
			svga->firstline_draw = svga->displine;
		svga->lastline_draw = svga->displine;

		for (x = 0; x <= (svga->hdisp + svga->scrollcache); x += 4) {
			if (svga->crtc[0x17] & 0x80) {
				dat = *(uint32_t *)(&svga->vram[svga->ma & svga->vram_display_mask]);
				p[x] = dat & 0xffffff;

				dat = *(uint32_t *)(&svga->vram[(svga->ma + 3) & svga->vram_display_mask]);
				p[x + 1] = dat & 0xffffff;

				dat = *(uint32_t *)(&svga->vram[(svga->ma + 6) & svga->vram_display_mask]);
				p[x + 2] = dat & 0xffffff;

				dat = *(uint32_t *)(&svga->vram[(svga->ma + 9) & svga->vram_display_mask]);
				p[x + 3] = dat & 0xffffff;
			} else
				memset(&(p[x]), 0x0, 4 * sizeof(uint32_t));

			svga->ma += 12;
		}
		svga->ma &= svga->vram_display_mask;
		}		
	} else {
		changed_addr = svga->remap_func(svga, svga->ma);

		if (svga->changedvram[changed_addr >> 12] || svga->changedvram[(changed_addr >> 12) + 1] || svga->fullchange) {
		p = &buffer32->line[svga->displine + svga->y_add][svga->x_add];

		if (svga->firstline_draw == 2000)
			svga->firstline_draw = svga->displine;
		svga->lastline_draw = svga->displine;

		if (!svga->remap_required) {
			for (x = 0; x <= (svga->hdisp + svga->scrollcache); x += 4) {
				if (svga->crtc[0x17] & 0x80) {
					dat0 = *(uint32_t *)(&svga->vram[svga->ma & svga->vram_display_mask]);
					dat1 = *(uint32_t *)(&svga->vram[(svga->ma + 4) & svga->vram_display_mask]);
					dat2 = *(uint32_t *)(&svga->vram[(svga->ma + 8) & svga->vram_display_mask]);

					*p++ = dat0 & 0xffffff;
					*p++ = (dat0 >> 24) | ((dat1 & 0xffff) << 8);
					*p++ = (dat1 >> 16) | ((dat2 & 0xff) << 16);
					*p++ = dat2 >> 8;
				} else
					memset(&(p[x]), 0x0, 4 * sizeof(uint32_t));

				svga->ma += 12;
			}
		} else {
			for (x = 0; x <= (svga->hdisp + svga->scrollcache); x += 4) {
				if (svga->crtc[0x17] & 0x80) {
					addr = svga->remap_func(svga, svga->ma);
					dat0 = *(uint32_t *)(&svga->vram[addr & svga->vram_display_mask]);
					addr = svga->remap_func(svga, svga->ma + 4);
					dat1 = *(uint32_t *)(&svga->vram[addr & svga->vram_display_mask]);
					addr = svga->remap_func(svga, svga->ma + 8);
					dat2 = *(uint32_t *)(&svga->vram[addr & svga->vram_display_mask]);

					*p++ = dat0 & 0xffffff;
					*p++ = (dat0 >> 24) | ((dat1 & 0xffff) << 8);
					*p++ = (dat1 >> 16) | ((dat2 & 0xff) << 16);
					*p++ = dat2 >> 8;
				} else
					memset(&(p[x]), 0x0, 4 * sizeof(uint32_t));

				svga->ma += 12;
			}
		}
		svga->ma &= svga->vram_display_mask;
		}	
	}
}


void
svga_render_32bpp_lowres(svga_t *svga)
{
    int x;
	uint32_t *p;
    uint32_t dat;
	uint32_t changed_addr, addr;

    if ((svga->displine + svga->y_add) < 0)
		return;
	
	if (svga->force_old_addr) {
		if (svga->changedvram[svga->ma >> 12] || svga->changedvram[(svga->ma >> 12) + 1] || svga->fullchange) {
		if (svga->firstline_draw == 2000) 
			svga->firstline_draw = svga->displine;
		svga->lastline_draw = svga->displine;

		for (x = 0; x <= (svga->hdisp + svga->scrollcache); x++) {
			if (svga->crtc[0x17] & 0x80)
				dat = svga->vram[svga->ma] | (svga->vram[svga->ma + 1] << 8) | (svga->vram[svga->ma + 2] << 16);
			else
				dat = 0x00000000;
			svga->ma += 4;
			svga->ma &= svga->vram_display_mask;
			buffer32->line[svga->displine + svga->y_add][(x << 1) + svga->x_add] =
			buffer32->line[svga->displine + svga->y_add][(x << 1) + 1 + svga->x_add] = dat;
		}
		}
	} else {
		changed_addr = svga->remap_func(svga, svga->ma);

		if (svga->changedvram[changed_addr >> 12] || svga->changedvram[(changed_addr >> 12) + 1] || svga->fullchange) {
		p = &buffer32->line[svga->displine + svga->y_add][svga->x_add];

		if (svga->firstline_draw == 2000)
			svga->firstline_draw = svga->displine;
		svga->lastline_draw = svga->displine;

		if (!svga->remap_required) {
			for (x = 0; x <= (svga->hdisp + svga->scrollcache); x++) {
				if (svga->crtc[0x17] & 0x80)
					dat = *(uint32_t *)(&svga->vram[(svga->ma + (x << 2)) & svga->vram_display_mask]);
				else
					dat = 0x00000000;
				*p++ = dat & 0xffffff;
				*p++ = dat & 0xffffff;
			}
			svga->ma += (x * 4);
		} else {
			for (x = 0; x <= (svga->hdisp + svga->scrollcache); x++) {
				if (svga->crtc[0x17] & 0x80) {
					addr = svga->remap_func(svga, svga->ma);
					dat = *(uint32_t *)(&svga->vram[addr & svga->vram_display_mask]);
				} else
					dat = 0x00000000;
				*p++ = dat & 0xffffff;
				*p++ = dat & 0xffffff;
				svga->ma += 4;
			}
			svga->ma &= svga->vram_display_mask;
		}
		}
	}
}


void
svga_render_32bpp_highres(svga_t *svga)
{
    int x;
    uint32_t *p;
    uint32_t dat;
	uint32_t changed_addr, addr;

    if ((svga->displine + svga->y_add) < 0)
		return;
	
	if (svga->force_old_addr) {
		if (svga->changedvram[svga->ma >> 12] ||  svga->changedvram[(svga->ma >> 12) + 1] || svga->changedvram[(svga->ma >> 12) + 2] || svga->fullchange) {
		p = &buffer32->line[svga->displine + svga->y_add][svga->x_add];
			
		if (svga->firstline_draw == 2000) 
			svga->firstline_draw = svga->displine;
		svga->lastline_draw = svga->displine;

		for (x = 0; x <= (svga->hdisp + svga->scrollcache); x++) {
			if (svga->crtc[0x17] & 0x80)
				dat = *(uint32_t *)(&svga->vram[(svga->ma + (x << 2)) & svga->vram_display_mask]);
			else
				dat = 0x00000000;
			p[x] = dat & 0xffffff;
		}
		svga->ma += 4; 
		svga->ma &= svga->vram_display_mask;
		}
	} else {
		changed_addr = svga->remap_func(svga, svga->ma);

		if (svga->changedvram[changed_addr >> 12] || svga->changedvram[(changed_addr >> 12) + 1] || svga->fullchange) {
		p = &buffer32->line[svga->displine + svga->y_add][svga->x_add];

		if (svga->firstline_draw == 2000)
			svga->firstline_draw = svga->displine;
		svga->lastline_draw = svga->displine;

		if (!svga->remap_required) {
			for (x = 0; x <= (svga->hdisp + svga->scrollcache); x++) {
				if (svga->crtc[0x17] & 0x80) {
					dat = *(uint32_t *)(&svga->vram[(svga->ma + (x << 2)) & svga->vram_display_mask]);
					*p++ = dat & 0xffffff;
				} else
					memset(&(p[x]), 0x0, 1 * sizeof(uint32_t));
			}
			svga->ma += (x * 4);
		} else {
			for (x = 0; x <= (svga->hdisp + svga->scrollcache); x++) {
				if (svga->crtc[0x17] & 0x80) {
					addr = svga->remap_func(svga, svga->ma);
					dat = *(uint32_t *)(&svga->vram[addr & svga->vram_display_mask]);
					*p++ = dat & 0xffffff;
				} else
					memset(&(p[x]), 0x0, 1 * sizeof(uint32_t));

				svga->ma += 4;
			}
		}
		svga->ma &= svga->vram_display_mask;
		}
	}
}


void
svga_render_ABGR8888_highres(svga_t *svga)
{
    int x;
    uint32_t *p;
    uint32_t dat;
	uint32_t changed_addr, addr;

    if ((svga->displine + svga->y_add) < 0)
		return;

	changed_addr = svga->remap_func(svga, svga->ma);

    if (svga->changedvram[changed_addr >> 12] || svga->changedvram[(changed_addr >> 12) + 1] || svga->fullchange) {
	p = &buffer32->line[svga->displine + svga->y_add][svga->x_add];

	if (svga->firstline_draw == 2000)
		svga->firstline_draw = svga->displine;
	svga->lastline_draw = svga->displine;

	if (!svga->remap_required) {
		for (x = 0; x <= (svga->hdisp + svga->scrollcache); x++) {
			if (svga->crtc[0x17] & 0x80) {
				dat = *(uint32_t *)(&svga->vram[(svga->ma + (x << 2)) & svga->vram_display_mask]);
				*p++ = ((dat & 0xff0000) >> 16) | (dat & 0x00ff00) | ((dat & 0x0000ff) << 16);
			} else
				memset(&(p[x]), 0x0, 1 * sizeof(uint32_t));
		}
		svga->ma += x*4;
	} else {
		for (x = 0; x <= (svga->hdisp + svga->scrollcache); x++) {
			if (svga->crtc[0x17] & 0x80) {
				addr = svga->remap_func(svga, svga->ma);
				dat = *(uint32_t *)(&svga->vram[addr & svga->vram_display_mask]);
				*p++ = ((dat & 0xff0000) >> 16) | (dat & 0x00ff00) | ((dat & 0x0000ff) << 16);
			} else
				memset(&(p[x]), 0x0, 1 * sizeof(uint32_t));

			svga->ma += 4;
		}
	}
	svga->ma &= svga->vram_display_mask;
    }
}


void
svga_render_RGBA8888_highres(svga_t *svga)
{
    int x;
    uint32_t *p;
    uint32_t dat;
	uint32_t changed_addr, addr;

    if ((svga->displine + svga->y_add) < 0)
		return;

	changed_addr = svga->remap_func(svga, svga->ma);

    if (svga->changedvram[changed_addr >> 12] || svga->changedvram[(changed_addr >> 12) + 1] || svga->fullchange) {
	p = &buffer32->line[svga->displine + svga->y_add][svga->x_add];

	if (svga->firstline_draw == 2000)
		svga->firstline_draw = svga->displine;
	svga->lastline_draw = svga->displine;

	if (!svga->remap_required) {
		for (x = 0; x <= (svga->hdisp + svga->scrollcache); x++) {
			if (svga->crtc[0x17] & 0x80) {
				dat = *(uint32_t *)(&svga->vram[(svga->ma + (x << 2)) & svga->vram_display_mask]);
				*p++ = dat >> 8;
			} else
				memset(&(p[x]), 0x0, 1 * sizeof(uint32_t));
		}
		svga->ma += (x * 4);
	} else {
		for (x = 0; x <= (svga->hdisp + svga->scrollcache); x++) {
			if (svga->crtc[0x17] & 0x80) {
				addr = svga->remap_func(svga, svga->ma);
				dat = *(uint32_t *)(&svga->vram[addr & svga->vram_display_mask]);
				*p++ = dat >> 8;
			} else
				memset(&(p[x]), 0x0, 1 * sizeof(uint32_t));

			svga->ma += 4;
		}
	}
	svga->ma &= svga->vram_display_mask;
    }
}
