/*
 * VARCem	Virtual ARchaeological Computer EMulator.
 *		An emulator of (mostly) x86-based PC systems and devices,
 *		using the ISA,EISA,VLB,MCA  and PCI system buses, roughly
 *		spanning the era between 1981 and 1995.
 *
 *		This file is part of the VARCem Project.
 *
 *		Definitions for the Toshiba T3100e system.
 *
 *
 *
 * Authors:	Fred N. van Kempen, <decwiz@yahoo.com>
 *		Miran Grca, <mgrca8@gmail.com>
 *		Sarah Walker, <tommowalker@tommowalker.co.uk>
 *
 *		Copyright 2017,2018 Fred N. van Kempen.
 *		Copyright 2016-2018 Miran Grca.
 *		Copyright 2008-2018 Sarah Walker.
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

#ifndef MACHINE_T3100E_H
# define MACHINE_T3100E_H


extern const device_t t3100e_device;


extern void	t3100e_notify_set(uint8_t value);
extern void	t3100e_display_set(uint8_t value);
extern uint8_t	t3100e_display_get(void);
extern uint8_t	t3100e_config_get(void);
extern void	t3100e_turbo_set(uint8_t value);
extern uint8_t	t3100e_mono_get(void);
extern void	t3100e_mono_set(uint8_t value);

extern void	t3100e_video_options_set(uint8_t options);
extern void	t3100e_display_set(uint8_t internal);


#endif	/*MACHINE_T3100E_H*/
