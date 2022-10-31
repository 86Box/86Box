/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          Definitions for the Toshiba T1000/T1200 machines.
 *
 *
 *
 * Authors: Fred N. van Kempen, <decwiz@yahoo.com>
 *          Miran Grca, <mgrca8@gmail.com>
 *          Sarah Walker, <tommowalker@tommowalker.co.uk>
 *
 *          Copyright 2017,2018 Fred N. van Kempen.
 *          Copyright 2016-2018 Miran Grca.
 *          Copyright 2008-2018 Sarah Walker.
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

#ifndef MACHINE_T1000_H
#define MACHINE_T1000_H

extern const device_t t1000_video_device;
extern const device_t t1200_video_device;

extern void t1000_video_options_set(uint8_t options);
extern void t1000_video_enable(uint8_t enabled);
extern void t1000_display_set(uint8_t internal);

extern void t1000_syskey(uint8_t amask, uint8_t omask, uint8_t xmask);

extern void t1000_nvr_load(void);
extern void t1000_nvr_save(void);

extern void t1200_nvr_load(void);
extern void t1200_nvr_save(void);

#endif /*MACHINE_T1000_H*/
