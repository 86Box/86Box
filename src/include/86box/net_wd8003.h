/*
 * 86Box	A hypervisor and IBM PC system emulator that specializes in
 *		running old operating systems and software designed for IBM
 *		PC systems and compatibles from 1981 through fairly recent
 *		system designs based on the PCI bus.
 *
 *		This file is part of the 86Box distribution.
 *
 *		Implementation of the following network controllers:
 *			- SMC/WD 8003E (ISA 8-bit);
 *			- SMC/WD 8013EBT (ISA 16-bit);
 *			- SMC/WD 8013EP/A (MCA).
 *
 *
 *
 * Authors:	Fred N. van Kempen, <decwiz@yahoo.com>
 *		TheCollector1995, <mariogplayer@gmail.com>
 *		Miran Grca, <mgrca8@gmail.com>
 *		Peter Grehan, <grehan@iprg.nokia.com>
 *
 *		Copyright 2017,2018 Fred N. van Kempen.
 *		Copyright 2016-2018 Miran Grca.
 *		Portions Copyright (C) 2002  MandrakeSoft S.A.
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
#define NET_WD8003_H

enum {
    WD_NONE = 0,
    WD8003E,   /* WD8003E   :  8-bit ISA, no  interface chip */
    WD8003EB,  /* WD8003EB  :  8-bit ISA, 5x3 interface chip */
    WD8013EBT, /* WD8013EBT : 16-bit ISA, no  interface chip */
    WD8003ETA, /* WD8003ET/A: 16-bit MCA, no  interface chip */
    WD8003EA,  /* WD8003E/A : 16-bit MCA, 5x3 interface chip */
    WD8013EPA
};

extern const device_t wd8003e_device;
extern const device_t wd8003eb_device;
extern const device_t wd8013ebt_device;
extern const device_t wd8003eta_device;
extern const device_t wd8003ea_device;
extern const device_t wd8013epa_device;

#endif /*NET_WD8003_H*/
