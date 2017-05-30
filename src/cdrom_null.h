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
 * Version:	@(#)cdrom_null.h	1.0.0	2017/05/30
 *
 * Author:	Sarah Walker, <http://pcem-emulator.co.uk/>
 *		Miran Grca, <mgrca8@gmail.com>
 *		Copyright 2008-2016 Sarah Walker.
 *		Copyright 2016-2017 Miran Grca.
 */

#ifndef CDROM_NULL_H
#define CDROM_NULL_H

/* this header file lists the functions provided by
   various platform specific cdrom-ioctl files */

int cdrom_null_open(uint8_t id, char d);
void cdrom_null_reset(uint8_t id);
void null_close(uint8_t id);

#endif /* ! CDROM_NULL_H */
