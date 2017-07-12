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
 * Version:	@(#)disc_fdi.h	1.0.0	2017/05/30
 *
 * Author:	Sarah Walker, <http://pcem-emulator.co.uk/>
 *		Miran Grca, <mgrca8@gmail.com>
 *		Copyright 2008-2017 Sarah Walker.
 *		Copyright 2016-2017 Miran Grca.
 */

void fdi_init();
void fdi_load(int drive, wchar_t *fn);
void fdi_close(int drive);
void fdi_seek(int drive, int track);
void fdi_readsector(int drive, int sector, int track, int side, int density, int sector_size);
void fdi_writesector(int drive, int sector, int track, int side, int density, int sector_size);
void fdi_comparesector(int drive, int sector, int track, int side, int density, int sector_size);
void fdi_readaddress(int drive, int sector, int side, int density);
void fdi_format(int drive, int sector, int side, int density, uint8_t fill);
int fdi_hole(int drive);
double fdi_byteperiod(int drive);
void fdi_stop();
void fdi_poll();
