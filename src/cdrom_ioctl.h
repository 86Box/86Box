/*
 * 86Box	A hypervisor and IBM PC system emulator that specializes in
 *		running old operating systems and software designed for IBM
 *		PC systems and compatibles from 1981 through fairly recent
 *		system designs based on the PCI bus.
 *
 *		This file is part of the 86Box distribution.
 *
 *		Implementation of the CD-ROM host drive IOCTL interface for
 *		Windows using SCSI Passthrough Direct.
 *
 * Version:	@(#)cdrom_ioctl.h	1.0.0	2017/05/30
 *
 * Author:	Sarah Walker, <http://pcem-emulator.co.uk/>
 *		Miran Grca, <mgrca8@gmail.com>
 *		Copyright 2008-2016 Sarah Walker.
 *		Copyright 2016-2017 Miran Grca.
 */

#ifndef CDROM_IOCTL_H
#define CDROM_IOCTL_H

/* this header file lists the functions provided by
   various platform specific cdrom-ioctl files */

extern uint32_t cdrom_capacity;
   
extern int ioctl_open(uint8_t id, char d);
extern void ioctl_reset(uint8_t id);

extern void ioctl_close(uint8_t id);

#endif /* ! CDROM_IOCTL_H */
