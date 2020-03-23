/*
 * 86Box	A hypervisor and IBM PC system emulator that specializes in
 *		running old operating systems and software designed for IBM
 *		PC systems and compatibles from 1981 through fairly recent
 *		system designs based on the PCI bus.
 *
 *		This file is part of the 86Box distribution.
 *
 *		Implementation of the NCR 53C810 and 53C875 SCSI Host
 *		Adapters made by NCR and later Symbios and LSI. These
 *		controllers were designed for the PCI bus.
 *
 * Version:	@(#)scsi_ncr53c8xx.c	1.0.2	2018/10/30
 *
 * Authors:	TheCollector1995, <mariogplayer@gmail.com>
 *		Miran Grca, <mgrca8@gmail.com>
 *		Paul Brook (QEMU),
 *		Artyom Tarasenko (QEMU),
 *
 *		Copyright 2006-2018 Paul Brook.
 *		Copyright 2009-2018 Artyom Tarasenko.
 *		Copyright 2017,2018 Miran Grca.
 */
#ifndef SCSI_NCR53C8XX_H
# define SCSI_NCR53C8XX_H


extern const device_t ncr53c810_pci_device;
extern const device_t ncr53c825a_pci_device;
extern const device_t ncr53c860_pci_device;
extern const device_t ncr53c875_pci_device;

  
#endif	/*SCSI_NCR53C8XX_H*/
