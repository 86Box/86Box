/*
 * 86Box	A hypervisor and IBM PC system emulator that specializes in
 *		running old operating systems and software designed for IBM
 *		PC systems and compatibles from 1981 through fairly recent
 *		system designs based on the PCI bus.
 *
 *		This file is part of the 86Box distribution.
 *
 *		Implementation of the PCjs JSON floppy image format.
 *
 * Version:	@(#)floppy_json.h	1.0.1	2017/09/06
 *
 * Author:	Fred N. van Kempen, <decwiz@yahoo.com>
 *		Copyright 2017 Fred N. van Kempen.
 */
#ifndef EMU_FLOPPY_JSON_H
# define EMU_FLOPPY_JSON_H


//extern void	json_init(void);
extern void	json_load(int drive, wchar_t *fn);
extern void	json_close(int drive);
//extern void	json_seek(int drive, int track);


#endif	/*EMU_FLOPPY_JSON_H*/
