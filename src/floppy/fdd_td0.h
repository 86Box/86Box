/*
 * 86Box	A hypervisor and IBM PC system emulator that specializes in
 *		running old operating systems and software designed for IBM
 *		PC systems and compatibles from 1981 through fairly recent
 *		system designs based on the PCI bus.
 *
 *		This file is part of the 86Box distribution.
 *
 *		Definitions for the Teledisk floppy image format.
 *
 * Version:	@(#)floppy_td0.h	1.0.2	2018/03/17
 *
 * Authors:	Miran Grca, <mgrca8@gmail.com>
 *		Fred N. van Kempen, <decwiz@yahoo.com>
 *
 *		Copyright 2016-2018 Miran Grca.
 *		Copyright 2017,2018 Fred N. van Kempen.
 */
#ifndef EMU_FLOPPY_TD0_H
# define EMU_FLOPPY_TD0_H


extern void td0_init(void);
extern void td0_load(int drive, wchar_t *fn);
extern void td0_close(int drive);


#endif	/*EMU_FLOPPY_TD0_H*/
