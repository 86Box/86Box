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
 * Version:	@(#)vid_ega_render.h	1.0.3	2019/10/03
 *
 * Author:	Sarah Walker, <http://pcem-emulator.co.uk/>
 *		Miran Grca, <mgrca8@gmail.com>
 *		Copyright 2008-2019 Sarah Walker.
 *		Copyright 2016-2019 Miran Grca.
 */

extern int firstline_draw, lastline_draw;
extern int displine;
extern int sc;

extern uint32_t ma, ca;
extern int con, cursoron, cgablink;

extern int scrollcache;

extern uint8_t edatlookup[4][4];

void ega_render_blank(ega_t *ega);

void ega_render_overscan_left(ega_t *ega);
void ega_render_overscan_right(ega_t *ega);

void ega_render_text_40(ega_t *ega);
void ega_render_text_80(ega_t *ega);

void ega_render_2bpp(ega_t *ega);

void ega_render_4bpp_lowres(ega_t *ega);
void ega_render_4bpp_highres(ega_t *ega);
