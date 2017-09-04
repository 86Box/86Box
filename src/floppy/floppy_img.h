/*
 * 86Box	A hypervisor and IBM PC system emulator that specializes in
 *		running old operating systems and software designed for IBM
 *		PC systems and compatibles from 1981 through fairly recent
 *		system designs based on the PCI bus.
 *
 *		This file is part of the 86Box distribution.
 *
 *		Implementation of the raw sector-based floppy image format,
 *		as well as the Japanese FDI, CopyQM, and FDF formats.
 *
 * Version:	@(#)floppy_img.h	1.0.2	2017/09/03
 *
 * Authors:	Sarah Walker, <http://pcem-emulator.co.uk/>
 *		Miran Grca, <mgrca8@gmail.com>
 *		Copyright 2008-2017 Sarah Walker.
 *		Copyright 2016,2017 Miran Grca.
 */
#ifndef EMU_FLOPPY_IMG_H
# define EMU_FLOPPY_IMG_H


extern void img_init(void);
extern void img_load(int drive, wchar_t *fn);
extern void img_close(int drive);
extern void img_seek(int drive, int track);


#endif	/*EMU_FLOPPY_IMG_H*/
