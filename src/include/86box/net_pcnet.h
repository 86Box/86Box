/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          Emulation of the AMD PCnet LANCE NIC controller for both the ISA
 *          and PCI buses.
 *
 * Authors: Miran Grca, <mgrca8@gmail.com>
 *          TheCollector1995, <mariogplayer@gmail.com>
 *          Antony T Curtis
 *
 *          Copyright 2004-2019 Antony T Curtis
 *          Copyright 2016-2019 Miran Grca.
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
    DEV_AM79C973     = 6, /* PCnet-FAST III (PCI, 10/100 Mbps) */
    DEV_AM79C970     = 7,  /* PCnet-PCI (PCI, 10 Mbps, the 1994 original) */
    DEV_AM79C971     = 8,  /* PCnet-FAST (PCI, 10/100 Mbps, external MII PHY) */
    DEV_AM79C972     = 9,  /* PCnet-FAST+ (PCI, 10/100 Mbps, external MII PHY, OnNow) */
    DEV_AM79C975     = 10, /* PCnet-FAST III Am79C975 (the Am79C973 with the SMIU) */
    DEV_AM79C974     = 11, /* PCnet-SCSI's Ethernet (PCI, 10 Mbps; the SCSI is scsi_pcscsi.c's) */
    DEV_AM79C961A    = 12  /* PCnet-ISA II (ISA, 10 Mbps, full duplex, Plug and Play) */
};

#endif /*NET_PCNET_H*/
