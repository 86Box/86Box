/*
 * 86Box	A hypervisor and IBM PC system emulator that specializes in
 *		running old operating systems and software designed for IBM
 *		PC systems and compatibles from 1981 through fairly recent
 *		system designs based on the PCI bus.
 *
 *		This file is part of the 86Box distribution.
 *
 *		3DFX Voodoo emulation.
 *
 *
 *
 * Authors:	Sarah Walker, <http://pcem-emulator.co.uk/>
 *
 *		Copyright 2008-2020 Sarah Walker.
 */

#ifndef VIDEO_VOODOO_BLITTER_H
#define VIDEO_VOODOO_BLITTER_H

void voodoo_v2_blit_start(voodoo_t *voodoo);
void voodoo_v2_blit_data(voodoo_t *voodoo, uint32_t data);
void voodoo_fastfill(voodoo_t *voodoo, voodoo_params_t *params);

#endif /*VIDEO_VOODOO_BLITTER_H*/
