/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          Implementation of Thrust Master Flight Control System.
 *
 * Authors: Miran Grca, <mgrca8@gmail.com>
 *          Sarah Walker, <https://pcem-emulator.co.uk/>
 *          Jasmine Iwanek, <jriwanek@gmail.com>
 *
 *          Copyright 2016-2018 Miran Grca.
 *          Copyright 2008-2018 Sarah Walker.
 *          Copyright 2021-2025 Jasmine IWanek.
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
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <stdlib.h>
#include <wchar.h>
#include <86box/86box.h>
#include <86box/device.h>
#include <86box/timer.h>
#include <86box/gameport.h>
#include <86box/plat_unused.h>
#include <86box/joystick.h>

static int
tm_fcs_read_axis(UNUSED(void *priv), int axis)
{
    uint8_t gp= 0;

    if (!JOYSTICK_PRESENT(gp, 0))
        return AXIS_NOT_PRESENT;

    switch (axis) {
        case 0:
            return joystick_state[gp][0].axis[0];
        case 1:
            return joystick_state[gp][0].axis[1];
        case 3:
            if (joystick_state[gp][0].pov[0] == -1)
                return 32767;
            if (joystick_state[gp][0].pov[0] > 315 || joystick_state[gp][0].pov[0] < 45)
                return -32768;
            if (joystick_state[gp][0].pov[0] >= 45 && joystick_state[gp][0].pov[0] < 135)
                return -16384;
            if (joystick_state[gp][0].pov[0] >= 135 && joystick_state[gp][0].pov[0] < 225)
                return 0;
            if (joystick_state[gp][0].pov[0] >= 225 && joystick_state[gp][0].pov[0] < 315)
                return 16384;
            return 0;
        case 2:
        default:
            return 0;
    }
}

static int
tm_fcs_rcs_read_axis(UNUSED(void *priv), int axis)
{
    uint8_t gp = 0;

    if (!JOYSTICK_PRESENT(gp, 0))
        return AXIS_NOT_PRESENT;

    switch (axis) {
        case 0:
            return joystick_state[gp][0].axis[0];
        case 1:
            return joystick_state[gp][0].axis[1];
        case 2:
            return joystick_state[gp][0].axis[2];
        case 3:
            if (joystick_state[gp][0].pov[0] == -1)
                return 32767;
            if (joystick_state[gp][0].pov[0] > 315 || joystick_state[gp][0].pov[0] < 45)
                return -32768;
            if (joystick_state[gp][0].pov[0] >= 45 && joystick_state[gp][0].pov[0] < 135)
                return -16384;
            if (joystick_state[gp][0].pov[0] >= 135 && joystick_state[gp][0].pov[0] < 225)
                return 0;
            if (joystick_state[gp][0].pov[0] >= 225 && joystick_state[gp][0].pov[0] < 315)
                return 16384;
            return 0;
        default:
            return 0;
    }
}

const joystick_t joystick_tm_fcs = {
    .name          = "Thrustmaster Flight Control System",
    .internal_name = "thrustmaster_fcs",
    .init          = joystick_standard_init,
    .close         = joystick_standard_close,
    .read          = joystick_standard_read_4button,
    .write         = joystick_standard_write,
    .read_axis     = tm_fcs_read_axis,
    .a0_over       = joystick_standard_a0_over,
    .axis_count    = 2,
    .button_count  = 4,
    .pov_count     = 1,
    .max_joysticks = 1,
    .axis_names    = { "X axis", "Y axis" },
    .button_names  = { "Trigger", "Button 2", "Button 3", "Button 4" },
    .pov_names     = { "POV" }
};

const joystick_t joystick_tm_fcs_rcs = {
    .name          = "Thrustmaster FCS + Rudder Control System",
    .internal_name = "thrustmaster_fcs_rcs",
    .init          = joystick_standard_init,
    .close         = joystick_standard_close,
    .read          = joystick_standard_read_4button,
    .write         = joystick_standard_write,
    .read_axis     = tm_fcs_rcs_read_axis,
    .a0_over       = joystick_standard_a0_over,
    .axis_count    = 3,
    .button_count  = 4,
    .pov_count     = 1,
    .max_joysticks = 1,
    .axis_names    = { "X axis", "Y axis", "Rudder" },
    .button_names  = { "Trigger", "Button 2", "Button 3", "Button 4" },
    .pov_names     = { "POV" }
};
