/*
 * 86Box	A hypervisor and IBM PC system emulator that specializes in
 *		running old operating systems and software designed for IBM
 *		PC systems and compatibles from 1981 through fairly recent
 *		system designs based on the PCI bus.
 *
 *		Emulation of the AMD PCnet LANCE NIC controller for both the ISA
 *		and PCI buses.
 *
 * Version:	@(#)net_pcnet.c	1.0.0	2019/11/09
 *
 * Authors:	Miran Grca, <mgrca8@gmail.com>
 *		TheCollector1995, <mariogplayer@gmail.com>
 *		Antony T Curtis
 *
 *		Copyright 2004-2019 Antony T Curtis
 *		Copyright 2016-2019 Miran Grca.
 */
#ifndef NET_PCNET_H
# define NET_PCNET_H


enum {
    PCNET_NONE = 0,
    PCNET_ISA = 1,			/* 16-bit ISA */
    PCNET_PCI = 2,			/* 32-bit PCI */
	PCNET_VLB = 3			/* 32-bit VLB */
};


extern const device_t	pcnet_isa_device;
extern const device_t	pcnet_pci_device;
extern const device_t	pcnet_vlb_device;

#endif	/*NET_PCNET_H*/
