/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          Definitions for the NE2000 ethernet controller.
 *
 *
 *
 * Authors: Fred N. van Kempen, <decwiz@yahoo.com>
 *
 *          Copyright 2017-2018 Fred N. van Kempen.
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
#define NET_NE2000_H

enum {
    NE2K_NONE          = 0,
    NE2K_NE1000        = 1, /* 8-bit ISA NE1000 */
    NE2K_NE1000_COMPAT = 2, /* 8-bit ISA NE1000-Compatible */
    NE2K_NE2000        = 3, /* 16-bit ISA NE2000 */
    NE2K_NE2000_COMPAT = 4, /* 16-bit ISA NE2000-Compatible */
    NE2K_ETHERNEXT_MC  = 5, /* 16-bit MCA EtherNext/MC */
    NE2K_RTL8019AS     = 6, /* 16-bit ISA PnP Realtek 8019AS */
    NE2K_DE220P        = 7, /* 16-bit ISA PnP D-Link DE-220P */
    NE2K_RTL8029AS     = 8  /* 32-bit PCI Realtek 8029AS */
};

#endif /*NET_NE2000_H*/
