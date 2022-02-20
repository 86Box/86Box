/*
 * VARCem	Virtual ARchaeological Computer EMulator.
 *		An emulator of (mostly) x86-based PC systems and devices,
 *		using the ISA,EISA,VLB,MCA  and PCI system buses, roughly
 *		spanning the era between 1981 and 1995.
 *
 *		This file is part of the VARCem Project.
 *
 *		Definitions for the NE2000 ethernet controller.
 *
 *
 *
 * Authors:	Fred N. van Kempen, <decwiz@yahoo.com>
 *
 *		Copyright 2017,2018 Fred N. van Kempen.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free  Software  Foundation; either  version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is  distributed in the hope that it will be useful, but
 * WITHOUT   ANY  WARRANTY;  without  even   the  implied  warranty  of
 * MERCHANTABILITY  or FITNESS  FOR A PARTICULAR  PURPOSE. See  the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the:
 *
 *   Free Software Foundation, Inc.
 *   59 Temple Place - Suite 330
 *   Boston, MA 02111-1307
 *   USA.
 */

#ifndef NET_NE2000_H
# define NET_NE2000_H


enum {
    NE2K_NONE = 0,
    NE2K_NE1000 = 1,			/* 8-bit ISA NE1000 */
    NE2K_NE2000 = 2,			/* 16-bit ISA NE2000 */
    NE2K_ETHERNEXT_MC = 3,		/* 16-bit MCA EtherNext/MC */
    NE2K_RTL8019AS = 4,			/* 16-bit ISA PnP Realtek 8019AS */
    NE2K_RTL8029AS = 5			/* 32-bit PCI Realtek 8029AS */
};


extern const device_t	ne1000_device;
extern const device_t	ne2000_device;
extern const device_t	ethernext_mc_device;
extern const device_t	rtl8019as_device;
extern const device_t	rtl8029as_device;


#endif	/*NET_NE2000_H*/
