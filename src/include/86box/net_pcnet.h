/*
 * 86Box	A hypervisor and IBM PC system emulator that specializes in
 *		running old operating systems and software designed for IBM
 *		PC systems and compatibles from 1981 through fairly recent
 *		system designs based on the PCI bus.
 *
 *		Emulation of the AMD PCnet LANCE NIC controller for both the ISA
 *		and PCI buses.
 *
 *
 *
 * Authors:	Miran Grca, <mgrca8@gmail.com>
 *		TheCollector1995, <mariogplayer@gmail.com>
 *		Antony T Curtis
 *
 *		Copyright 2004-2019 Antony T Curtis
 *		Copyright 2016-2019 Miran Grca.
 */

#ifndef NET_PCNET_H
#define NET_PCNET_H

enum {
    DEV_NONE         = 0,
    DEV_AM79C960     = 1, /* PCnet-ISA (ISA, 10 Mbps, NE2100/NE1500T compatible) */
    DEV_AM79C960_EB  = 2, /* PCnet-ISA (ISA, 10 Mbps, Racal InterLan EtherBlaster compatible) */
    DEV_AM79C960_VLB = 3, /* PCnet-VLB (VLB, 10 Mbps, NE2100/NE1500T compatible) */
    DEV_AM79C961     = 4, /* PCnet-ISA+ (ISA, 10 Mbps, NE2100/NE1500T compatible, Plug and Play) */
    DEV_AM79C970A    = 5, /* PCnet-PCI II (PCI, 10 Mbps) */
    DEV_AM79C973     = 6  /* PCnet-FAST III (PCI, 10/100 Mbps) */
};

extern const device_t pcnet_am79c960_device;
extern const device_t pcnet_am79c960_eb_device;
extern const device_t pcnet_am79c960_vlb_device;
extern const device_t pcnet_am79c961_device;
extern const device_t pcnet_am79c970a_device;
extern const device_t pcnet_am79c973_device;

#endif /*NET_PCNET_H*/
