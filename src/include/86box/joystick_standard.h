/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          Definitions for the joystick driver.
 *
 *
 *
 * Authors: Miran Grca, <mgrca8@gmail.com>
 *          Sarah Walker, <https://pcem-emulator.co.uk/>
 *
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

#ifndef EMU_JOYSTICK_STANDARD_H
#define EMU_JOYSTICK_STANDARD_H

extern const joystick_if_t joystick_2axis_2button;
extern const joystick_if_t joystick_2axis_4button;
extern const joystick_if_t joystick_3axis_2button;
extern const joystick_if_t joystick_3axis_4button;
extern const joystick_if_t joystick_4axis_4button;
extern const joystick_if_t joystick_2axis_6button;
extern const joystick_if_t joystick_2axis_8button;

#endif /*EMU_JOYSTICK_STANDARD_H*/
