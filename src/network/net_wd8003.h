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
 * Version:	@(#)net_ne2000.h	1.0.2	2018/03/15
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
#ifndef NET_WD8003_H
# define NET_WD8003_H

enum {
    WD_NONE = 0,
	WD8003E = 1,				/* 8-bit ISA WD8003E */
	WD8013EBT = 2,				/* 16-bit ISA WD8013EBT */
	WD8013EPA = 3				/* MCA WD8013EP/A */
};

extern const device_t 	wd8003e_device;
extern const device_t 	wd8013ebt_device;
extern const device_t 	wd8013epa_device;

#endif	/*NET_WD8003_H*/