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
 * Version:	@(#)disc_td0.h	1.0.0	2017/05/30
 *
 * Author:	Milodrag Milanovic,
 *		Haruhiko OKUMURA,
 *		Haruyasu YOSHIZAKI,
 *		Kenji RIKITAKE,
 *		Miran Grca, <mgrca8@gmail.com>
 *		Copyright 1988-2017 Haruhiko OKUMURA.
 *		Copyright 1988-2017 Haruyasu YOSHIZAKI.
 *		Copyright 1988-2017 Kenji RIKITAKE.
 *		Copyright 2013-2017 Milodrag Milanovic.
 *		Copyright 2016-2017 Miran Grca.
 */

void td0_init();
void td0_load(int drive, wchar_t *fn);
void td0_close(int drive);
void td0_seek(int drive, int track);
