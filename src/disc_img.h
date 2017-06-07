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
 * Version:	@(#)disc_img.h	1.0.0	2017/05/30
 *
 * Author:	Sarah Walker, <http://pcem-emulator.co.uk/>
 *		Miran Grca, <mgrca8@gmail.com>
 *		Copyright 2008-2017 Sarah Walker.
 *		Copyright 2016-2017 Miran Grca.
 */

void img_init();
void img_load(int drive, wchar_t *fn);
void img_close(int drive);
void img_seek(int drive, int track);
