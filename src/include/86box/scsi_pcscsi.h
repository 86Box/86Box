/*
 * 86Box	A hypervisor and IBM PC system emulator that specializes in
 *		running old operating systems and software designed for IBM
 *		PC systems and compatibles from 1981 through fairly recent
 *		system designs based on the PCI bus.
 *
 *		This file is part of the 86Box distribution.
 *
 *		Implementation of the AMD PCscsi and Tekram DC-390 SCSI
 *		controllers using the NCR 53c9x series of chips.
 *
 *
 *
 *
 * Authors:	Fabrice Bellard (QEMU)
 *		Herve Poussineau (QEMU)
 *		TheCollector1995, <mariogplayer@gmail.com>
 *		Miran Grca, <mgrca8@gmail.com>
 *
 *		Copyright 2005-2018 Fabrice Bellard.
 *		Copyright 2012-2018 Herve Poussineau.
 *		Copyright 2017,2018 Miran Grca.
 */

#ifndef SCSI_PCSCSI_H
#define SCSI_PCSCSI_H

extern const device_t dc390_pci_device;
extern const device_t ncr53c90_mca_device;

#endif /*SCSI_BUSLOGIC_H*/
