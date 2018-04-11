/*
 * 86Box	A hypervisor and IBM PC system emulator that specializes in
 *		running old operating systems and software designed for IBM
 *		PC systems and compatibles from 1981 through fairly recent
 *		system designs based on the PCI bus.
 *
 *		This file is part of the 86Box distribution.
 *
 *		Implementation of the CD-ROM null interface for unmounted
 *		guest CD-ROM drives.
 *
 * Version:	@(#)cdrom_null.h	1.0.3	2017/09/03
 *
 * Authors:	Sarah Walker, <http://pcem-emulator.co.uk/>
 *		Miran Grca, <mgrca8@gmail.com>
 *		Copyright 2008-2016 Sarah Walker.
 *		Copyright 2016,2017 Miran Grca.
 */
#ifndef EMU_CDROM_NULL_H
#define EMU_CDROM_NULL_H


extern int	cdrom_null_open(uint8_t id, char d);
extern void	cdrom_null_reset(uint8_t id);
extern void	null_close(uint8_t id);


#endif	/*EMU_CDROM_NULL_H*/
