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
 * Version:	@(#)floppy_fdi.h	1.0.3	2017/12/14
 *
 * Authors:	Sarah Walker, <http://pcem-emulator.co.uk/>
 *		Miran Grca, <mgrca8@gmail.com>
 *
 *		Copyright 2008-2017 Sarah Walker.
 *		Copyright 2016,2017 Miran Grca.
 */
#ifndef EMU_FLOPPY_FDI_H
# define EMU_FLOPPY_FDI_H


extern void fdi_load(int drive, wchar_t *fn);
extern void fdi_close(int drive);
extern void fdi_seek(int drive, int track);
extern void fdi_readsector(int drive, int sector, int track, int side, int density, int sector_size);
extern void fdi_writesector(int drive, int sector, int track, int side, int density, int sector_size);
extern void fdi_comparesector(int drive, int sector, int track, int side, int density, int sector_size);
extern void fdi_readaddress(int drive, int sector, int side, int density);
extern void fdi_format(int drive, int sector, int side, int density, uint8_t fill);
extern int fdi_hole(int drive);
extern double fdi_byteperiod(int drive);
extern void fdi_stop(void);
extern void fdi_poll(void);


#endif	/*EMU_FLOPPY_FDI_H*/
