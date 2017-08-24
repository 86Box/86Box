/*
 * 86Box	A hypervisor and IBM PC system emulator that specializes in
 *		running old operating systems and software designed for IBM
 *		PC systems and compatibles from 1981 through fairly recent
 *		system designs based on the PCI bus.
 *
 *		This file is part of the 86Box distribution.
 *
 *		Implementation of the IMD floppy image format.
 *
 * Version:	@(#)disc_imd.h	1.0.1	2017/08/23
 *
 * Author:	Miran Grca, <mgrca8@gmail.com>
 *		Copyright 2016,2017 Miran Grca.
 */
#ifndef EMU_DISK_IMD_H
# define EMU_DISK_IMD_H


extern void imd_init(void);
extern void imd_load(int drive, wchar_t *fn);
extern void imd_close(int drive);
extern void imd_seek(int drive, int track);


#endif	/*EMU_DISK_IMD_H*/
