/*
 * 86Box	A hypervisor and IBM PC system emulator that specializes in
 *		running old operating systems and software designed for IBM
 *		PC systems and compatibles from 1981 through fairly recent
 *		system designs based on the PCI bus.
 *
 *		This file is part of the 86Box distribution.
 *
 *		The Windows CGA palette handler header.
 *
 * Version:	@(#)win_cgapal.h	1.0.0	2017/05/30
 *
 * Author:	Miran Grca, <mgrca8@gmail.com>
 *		Copyright 2016-2017 Miran Grca.
 */

extern PALETTE cgapal;
extern PALETTE cgapal_mono[6];

extern uint32_t pal_lookup[256];

#ifdef __cplusplus
extern "C" {
#endif
void cgapal_rebuild();
void destroy_bitmap(BITMAP *b);
#ifdef __cplusplus
}
#endif
