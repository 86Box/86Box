/*
 * 86Box	A hypervisor and IBM PC system emulator that specializes in
 *		running old operating systems and software designed for IBM
 *		PC systems and compatibles from 1981 through fairly recent
 *		system designs based on the PCI bus.
 *
 *		This file is part of the 86Box distribution.
 *
 *		Voodoo Graphics, 2, Banshee, 3 emulation.
 *
 *
 *
 * Authors:	Sarah Walker, <http://pcem-emulator.co.uk/>
 *		leilei
 *
 *		Copyright 2008-2020 Sarah Walker.
 */

#ifndef VIDEO_VOODOO_DISPLAY_H
# define VIDEO_VOODOO_DISPLAY_H

void voodoo_update_ncc(voodoo_t *voodoo, int tmu);
void voodoo_pixelclock_update(voodoo_t *voodoo);
void voodoo_generate_filter_v1(voodoo_t *voodoo);
void voodoo_generate_filter_v2(voodoo_t *voodoo);
void voodoo_threshold_check(voodoo_t *voodoo);
void voodoo_callback(void *p);

#endif /*VIDEO_VOODOO_DISPLAY_H*/
