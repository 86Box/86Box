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

#ifndef VIDEO_VOODOO_REG_H
#define VIDEO_VOODOO_REG_H

void voodoo_reg_writel(uint32_t addr, uint32_t val, void *p);

#endif /*VIDEO_VOODOO_REG_H*/
