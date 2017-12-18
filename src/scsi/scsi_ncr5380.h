/*
 * 86Box	A hypervisor and IBM PC system emulator that specializes in
 *		running old operating systems and software designed for IBM
 *		PC systems and compatibles from 1981 through fairly recent
 *		system designs based on the PCI bus.
 *
 *		This file is part of the 86Box distribution.
 *
 *		Implementation of the NCR 5380 series of SCSI Host Adapters
 *		made by NCR. These controllers were designed for
 *		the ISA bus.
 *
 * Version:	@(#)scsi_ncr5380.c	1.0.1	2017/12/16
 *
 * Authors:	TheCollector1995, <mariogplayer@gmail.com>
 *		Fred N. van Kempen, <decwiz@yahoo.com>
 */
#ifndef SCSI_NCR5380_H
# define SCSI_NCR5380_H


extern device_t scsi_lcs6821n_device;
extern device_t scsi_rt1000b_device;
extern device_t scsi_t130b_device;
extern device_t scsi_scsiat_device;

  
#endif	/*SCSI_NCR5380_H*/
