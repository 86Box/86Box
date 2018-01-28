/*
 * 86Box	A hypervisor and IBM PC system emulator that specializes in
 *		running old operating systems and software designed for IBM
 *		PC systems and compatibles from 1981 through fairly recent
 *		system designs based on the PCI bus.
 *
 *		This file is part of the 86Box distribution.
 *
 *		Definitions for the NE2000 ethernet controller.
 *
 * Version:	@(#)net_ne2000.h	1.0.5	2018/01/28
 *
 * Author:	Fred N. van Kempen, <decwiz@yahoo.com>
 */
#ifndef NET_NE2000_H
# define NET_NE2000_H


enum {
	NE2K_NONE = 0,
	NE2K_NE1000,			/* 8-bit ISA NE1000 */
	NE2K_NE2000,			/* 16-bit ISA NE2000 */
	NE2K_RTL8019AS,			/* 16-bit ISA PnP Realtek 8019AS */
	NE2K_RTL8029AS			/* 32-bit PCI Realtek 8029AS */
};


extern device_t	ne1000_device;
extern device_t	ne2000_device;
extern device_t	rtl8019as_device;
extern device_t	rtl8029as_device;


#endif	/*NET_NE2000_H*/
