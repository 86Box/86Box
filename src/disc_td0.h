/*
 * 86Box	A hypervisor and IBM PC system emulator that specializes in
 *		running old operating systems and software designed for IBM
 *		PC systems and compatibles from 1981 through fairly recent
 *		system designs based on the PCI bus.
 *
 *		This file is part of the 86Box distribution.
 *
 *		Implementation of the Teledisk floppy image format.
 *
 * Version:	@(#)disc_td0.h	1.0.1	2017/08/23
 *
 * Authors:	Milodrag Milanovic,
 *		Haruhiko OKUMURA,
 *		Haruyasu YOSHIZAKI,
 *		Kenji RIKITAKE,
 *		Miran Grca, <mgrca8@gmail.com>
 *		Copyright 1988-2017 Haruhiko OKUMURA.
 *		Copyright 1988-2017 Haruyasu YOSHIZAKI.
 *		Copyright 1988-2017 Kenji RIKITAKE.
 *		Copyright 2013-2017 Milodrag Milanovic.
 *		Copyright 2016,2017 Miran Grca.
 */
#ifndef EMU_DISC_TD0_H
# define EMU_DISC_TD0_H


extern void td0_init(void);
extern void td0_load(int drive, wchar_t *fn);
extern void td0_close(int drive);
extern void td0_seek(int drive, int track);


#endif	/*EMU_DISC_TD0_H*/
