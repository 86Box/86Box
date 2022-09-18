/*
 * 86Box	A hypervisor and IBM PC system emulator that specializes in
 *		running old operating systems and software designed for IBM
 *		PC systems and compatibles from 1981 through fairly recent
 *		system designs based on the PCI bus.
 *
 *		This file is part of the 86Box distribution.
 *
 *		Implementation of the FDI floppy stream image format
 *		interface to the FDI2RAW module.
 *
 *
 *
 * Authors:	Sarah Walker, <tommowalker@tommowalker.co.uk>
 *		Miran Grca, <mgrca8@gmail.com>
 *		Fred N. van Kempen, <decwiz@yahoo.com>
 *
 *		Copyright 2008-2018 Sarah Walker.
 *		Copyright 2016-2018 Miran Grca.
 *		Copyright 2018 Fred N. van Kempen.
 */
#ifndef EMU_FLOPPY_FDI_H
#define EMU_FLOPPY_FDI_H

extern void fdi_seek(int drive, int track);
extern void fdi_load(int drive, char *fn);
extern void fdi_close(int drive);

#endif /*EMU_FLOPPY_FDI_H*/
