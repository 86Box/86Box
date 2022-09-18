/*
 * 86Box	A hypervisor and IBM PC system emulator that specializes in
 *		running old operating systems and software designed for IBM
 *		PC systems and compatibles from 1981 through fairly recent
 *		system designs based on the PCI bus.
 *
 *		This file is part of the 86Box distribution.
 *
 *		Implementation of the ISA Bus (de)Bugger expansion card
 *		sold as a DIY kit in the late 1980's in The Netherlands.
 *		This card was a assemble-yourself 8bit ISA addon card for
 *		PC and AT systems that had several tools to aid in low-
 *		level debugging (mostly for faulty BIOSes, bootloaders
 *		and system kernels...)
 *
 *		Definitions for the BUGGER card.
 *
 *
 *
 * Author:	Fred N. van Kempen, <decwiz@yahoo.com>
 *
 *		Copyright 1989-2018 Fred N. van Kempen.
 */
#ifndef BUGGER_H
#define BUGGER_H

/* I/O port range used. */
#define BUGGER_ADDR    0x007a
#define BUGGER_ADDRLEN 4

#ifdef __cplusplus
extern "C" {
#endif

/* Global variables. */
extern const device_t bugger_device;

/* Functions. */

#ifdef __cplusplus
}
#endif

#endif /*BUGGER_H*/
