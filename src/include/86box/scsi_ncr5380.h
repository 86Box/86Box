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
 *
 *
 * Authors:	Sarah Walker, <http://pcem-emulator.co.uk/>
 *		TheCollector1995, <mariogplayer@gmail.com>
 *		Fred N. van Kempen, <decwiz@yahoo.com>
 *
 *		Copyright 2017-2018 Sarah Walker.
 *		Copyright 2017-2018 TheCollector1995.
 *		Copyright 2017,2018 Fred N. van Kempen.
 */

#ifndef SCSI_NCR5380_H
# define SCSI_NCR5380_H

extern const device_t scsi_lcs6821n_device;
extern const device_t scsi_rt1000b_device;
extern const device_t scsi_t128_device;
extern const device_t scsi_t130b_device;
extern const device_t scsi_ls2000_device;
#if defined(DEV_BRANCH) && defined(USE_SUMO)
extern const device_t scsi_scsiat_device;
#endif

#endif	/*SCSI_NCR5380_H*/
