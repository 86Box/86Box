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
 * Version:	@(#)disc_imd.h	1.0.0	2017/05/30
 *
 * Author:	Miran Grca, <mgrca8@gmail.com>
 *		Copyright 2016-2017 Miran Grca.
 */

void imd_init();
void imd_load(int drive, wchar_t *fn);
void imd_close(int drive);
void imd_seek(int drive, int track);
