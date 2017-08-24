/*
 * 86Box	A hypervisor and IBM PC system emulator that specializes in
 *		running old operating systems and software designed for IBM
 *		PC systems and compatibles from 1981 through fairly recent
 *		system designs based on the PCI bus.
 *
 *		Emulation of BusLogic BT-542B ISA and BT-958D PCI SCSI
 *		controllers.
 *
 * Version:	@(#)scsi_buslogic.h	1.0.1	2017/08/23
 *
 * Authors:	TheCollector1995, <mariogplayer@gmail.com>
 *		Miran Grca, <mgrca8@gmail.com>
 *		Fred N. van Kempen, <decwiz@yahoo.com>
 *		Copyright 2016,2017 Miran Grca.
 *		Copyright 2017 Fred N. van Kempen.
 */

#ifndef SCSI_BUSLOGIC_H
# define SCSI_BUSLOGIC_H


extern device_t buslogic_device;
extern device_t buslogic_pci_device;

extern	void BuslogicDeviceReset(void *p);
  
  
#endif	/*SCSI_BUSLOGIC_H*/
