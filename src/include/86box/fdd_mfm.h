/*
 * 86Box	A hypervisor and IBM PC system emulator that specializes in
 *		running old operating systems and software designed for IBM
 *		PC systems and compatibles from 1981 through fairly recent
 *		system designs based on the PCI bus.
 *
 *		This file is part of the 86Box distribution.
 *
 *		Implementation of the HxC MFM image format.
 *
 *
 *
 * Authors:	Miran Grca, <mgrca8@gmail.com>
 *
 *		Copyright 2018 Miran Grca.
 */
#ifndef EMU_FLOPPY_MFM_H
#define EMU_FLOPPY_MFM_H

extern void mfm_seek(int drive, int track);
extern void mfm_load(int drive, char *fn);
extern void mfm_close(int drive);

#endif /*EMU_FLOPPY_MFM_H*/
