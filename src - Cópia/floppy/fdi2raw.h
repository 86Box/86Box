/*
 * VARCem	Virtual ARchaeological Computer EMulator.
 *		An emulator of (mostly) x86-based PC systems and devices,
 *		using the ISA,EISA,VLB,MCA  and PCI system buses, roughly
 *		spanning the era between 1981 and 1995.
 *
 *		This file is part of the VARCem Project.
 *
 *		Definitions for the FDI floppy file format.
 *
 * Version:	@(#)fdi2raw.h	1.0.1	2018/02/14
 *
 * Authors:	Toni Wilen, <twilen@arabuusimiehet.com>
 *		and Vincent Joguin,
 *		Thomas Harte, <T.Harte@excite.co.uk>
 *
 *		Copyright 2001-2004 Toni Wilen.
 *		Copyright 2001-2004 Vincent Joguin.
 *		Copyright 2001 Thomas Harte.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free  Software  Foundation; either  version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is  distributed in the hope that it will be useful, but
 * WITHOUT   ANY  WARRANTY;  without  even   the  implied  warranty  of
 * MERCHANTABILITY  or FITNESS  FOR A PARTICULAR  PURPOSE. See  the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the:
 *
 *   Free Software Foundation, Inc.
 *   59 Temple Place - Suite 330
 *   Boston, MA 02111-1307
 *   USA.
 */
#ifndef __FDI2RAW_H
#define __FDI2RAW_H

#define uae_u8 uint8_t
#define uae_u16 uint16_t
#define uae_u32 uint32_t

#include <stdio.h>
typedef struct fdi FDI;

#ifdef __cplusplus
extern "C" {
#endif

extern int fdi2raw_loadtrack (FDI*, uae_u16 *mfmbuf, uae_u16 *tracktiming, int track, int *tracklength, int *indexoffset, int *multirev, int mfm);

extern int fdi2raw_loadrevolution (FDI*, uae_u16 *mfmbuf, uae_u16 *tracktiming, int track, int *tracklength, int mfm);

extern FDI *fdi2raw_header(FILE *f);
extern void fdi2raw_header_free (FDI *);
extern int fdi2raw_get_last_track(FDI *);
extern int fdi2raw_get_num_sector (FDI *);
extern int fdi2raw_get_last_head(FDI *);
extern int fdi2raw_get_type (FDI *);
extern int fdi2raw_get_bit_rate (FDI *);
extern int fdi2raw_get_rotation (FDI *);
extern int fdi2raw_get_write_protect (FDI *);
extern int fdi2raw_get_tpi (FDI *);

#ifdef __cplusplus
}
#endif

#endif
