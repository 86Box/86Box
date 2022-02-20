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
 * Author:	Sarah Walker, <http://pcem-emulator.co.uk/>
 *		Miran Grca, <mgrca8@gmail.com>
 *		Copyright 2008-2018 Sarah Walker.
 *		Copyright 2016-2018 Miran Grca.
 */

#ifndef VIDEO_SVGA_RENDER_H
# define VIDEO_SVGA_RENDER_H

extern int firstline_draw, lastline_draw;
extern int displine;
extern int sc;

extern uint32_t ma, ca;
extern int con, cursoron, cgablink;

extern int scrollcache;

extern uint8_t edatlookup[4][4];

void svga_recalc_remap_func(svga_t *svga);

void svga_render_null(svga_t *svga);
void svga_render_blank(svga_t *svga);
void svga_render_overscan_left(svga_t *svga);
void svga_render_overscan_right(svga_t *svga);
void svga_render_text_40(svga_t *svga);
void svga_render_text_80(svga_t *svga);
void svga_render_text_80_ksc5601(svga_t *svga);

void svga_render_2bpp_lowres(svga_t *svga);
void svga_render_2bpp_highres(svga_t *svga);
void svga_render_2bpp_headland_highres(svga_t *svga);
void svga_render_4bpp_lowres(svga_t *svga);
void svga_render_4bpp_highres(svga_t *svga);
void svga_render_8bpp_lowres(svga_t *svga);
void svga_render_8bpp_highres(svga_t *svga);
void svga_render_8bpp_tseng_lowres(svga_t *svga);
void svga_render_8bpp_tseng_highres(svga_t *svga);
void svga_render_8bpp_gs_lowres(svga_t *svga);
void svga_render_8bpp_gs_highres(svga_t *svga);
void svga_render_8bpp_rgb_lowres(svga_t *svga);
void svga_render_8bpp_rgb_highres(svga_t *svga);
void svga_render_15bpp_lowres(svga_t *svga);
void svga_render_15bpp_highres(svga_t *svga);
void svga_render_15bpp_mix_lowres(svga_t *svga);
void svga_render_15bpp_mix_highres(svga_t *svga);
void svga_render_16bpp_lowres(svga_t *svga);
void svga_render_16bpp_highres(svga_t *svga);
void svga_render_24bpp_lowres(svga_t *svga);
void svga_render_24bpp_highres(svga_t *svga);
void svga_render_32bpp_lowres(svga_t *svga);
void svga_render_32bpp_highres(svga_t *svga);
void svga_render_ABGR8888_lowres(svga_t *svga);
void svga_render_ABGR8888_highres(svga_t *svga);
void svga_render_RGBA8888_lowres(svga_t *svga);
void svga_render_RGBA8888_highres(svga_t *svga);

extern void (*svga_render)(svga_t *svga);

#endif /*VID_SVGA_RENDER_H*/
