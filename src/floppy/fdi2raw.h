/*
 * 86Box	A hypervisor and IBM PC system emulator that specializes in
 *		running old operating systems and software designed for IBM
 *		PC systems and compatibles from 1981 through fairly recent
 *		system designs based on the PCI bus.
 *
 *		This file is part of the 86Box distribution.
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
