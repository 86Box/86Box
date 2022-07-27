/*
 * 86Box	A hypervisor and IBM PC system emulator that specializes in
 *		running old operating systems and software designed for IBM
 *		PC systems and compatibles from 1981 through fairly recent
 *		system designs based on the PCI bus.
 *
 *		This file is part of the 86Box distribution.
 *
 *		Voodoo Banshee and 3 specific emulation.
 *
 *
 *
 * Authors:	Sarah Walker, <http://pcem-emulator.co.uk/>
 *
 *		Copyright 2008-2020 Sarah Walker.
 */

#ifndef VIDEO_VOODOO_BANSHEE_BLITTER_H
# define VIDEO_VOODOO_BANSHEE_BLITTER_H

void voodoo_2d_reg_writel(voodoo_t *voodoo, uint32_t addr, uint32_t val);

#endif /*VIDEO_VOODOO_BANSHEE_BLITTER_H*/
