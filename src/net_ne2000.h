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
 * Version:	@(#)net_ne2000.h	1.0.2	2017/05/11
 *
 * Author:	Fred N. van Kempen, <decwiz@yahoo.com>
 */
#ifndef NET_NE2000_H
# define NET_NE2000_H


#define NE2K_NE1000	1			/* 8bit ISA NE1000 */
#define NE2K_NE2000	2			/* 16bit ISA NE2000 */
#define NE2K_RTL8029AS	3			/* 32bi PCI Realtek 8029AS */


extern device_t	ne1000_device;
extern device_t	ne2000_device;
extern device_t	rtl8029as_device;


extern void	ne2000_generate_maclocal(uint32_t mac);
extern uint32_t	ne2000_get_maclocal(void);

extern void	ne2000_generate_maclocal_pci(uint32_t mac);
extern uint32_t	ne2000_get_maclocal_pci(void);


#endif	/*NET_NE2000_H*/
