/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          Definitions for the FDI floppy file format.
 *
 * Authors: Toni Wilen, <twilen@arabuusimiehet.com>
 *          and Vincent Joguin,
 *          Thomas Harte, <T.Harte@excite.co.uk>
 *
 *          Copyright 2001-2004 Toni Wilen.
 *          Copyright 2001-2004 Vincent Joguin.
 *          Copyright 2001-2016 Thomas Harte.
 */
#ifndef __FDI2RAW_H
#define __FDI2RAW_H

#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>

typedef struct fdi FDI;

#ifdef __cplusplus
extern "C" {
#endif

/*!
    Attempts to parse and return an FDI header from the file @c file.

    @parameter file the file from which to attempt to read the FDI.
    @returns a newly-allocated `FDI` if parsing succeeded; @c NULL otherwise.
*/
extern FDI *fdi2raw_header(FILE *file);

/*!
    Release all memory associated with @c file.
*/
extern void fdi2raw_header_free(FDI *file);

extern int fdi2raw_loadtrack(FDI *, uint16_t *mfmbuf, uint16_t *tracktiming, int track, int *tracklength, int *indexoffset, int *multirev, int mfm);
extern int fdi2raw_loadrevolution(FDI *, uint16_t *mfmbuf, uint16_t *tracktiming, int track, int *tracklength, int mfm);

typedef enum {
    FDI2RawDiskType8Inch = 0,
    FDI2RawDiskType5_25Inch = 1,
    FDI2RawDiskType3_5Inch = 2,
    FDI2RawDiskType3Inch = 3,
} FDI2RawDiskType;

/// @returns the disk type described by @c fdi.
extern FDI2RawDiskType fdi2raw_get_type(FDI *fdi);

/// @returns the bit rate at which @c fdi is sampled if spinning at the intended rate, in Kbit/s.
extern int fdi2raw_get_bit_rate(FDI *fdi);

/// @returns the intended rotation speed of @c fdi, in rotations per minute.
extern int fdi2raw_get_rotation(FDI *fdi);

/// @returns whether the imaged disk was write protected.
extern bool fdi2raw_get_write_protect(FDI *fdi);

/// @returns the final enumerated track represented in @c fdi.
extern int fdi2raw_get_last_track(FDI *fdi);

/// @returns the final enumerated head represented in @c fdi.
extern int fdi2raw_get_last_head(FDI *fdi);

/// @returns @c 22 if track 0 is a standard Amiga high-density; @c 11 otherwise.
extern int fdi2raw_get_num_sector(FDI *fdi);

extern int fdi2raw_get_tpi(FDI *fdi);

#ifdef __cplusplus
}
#endif

#endif
