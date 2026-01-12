/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          Implementation of the Flight Stick Pro.
 *
 * Authors: Miran Grca, <mgrca8@gmail.com>
 *          Sarah Walker, <https://pcem-emulator.co.uk/>
 *          Jasmine Iwanek, <jriwanek@gmail.com>
 *
 *          Copyright 2016-2018 Miran Grca.
 *          Copyright 2008-2018 Sarah Walker.
 *          Copyright 2021-2025 Jasmine Iwanek.
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

static uint8_t
ch_flightstick_pro_read(UNUSED(void *priv))
{
    uint8_t gp  = 0;
    uint8_t ret = 0xf0;

    if (JOYSTICK_PRESENT(gp, 0)) {
        if (joystick_state[gp][0].button[0])
            ret &= ~0x10;
        if (joystick_state[gp][0].button[1])
            ret &= ~0x20;
        if (joystick_state[gp][0].button[2])
            ret &= ~0x40;
        if (joystick_state[gp][0].button[3])
            ret &= ~0x80;

        // POV Hat
        if (joystick_state[gp][0].pov[0] != -1) {
            // POV Up
            if ((joystick_state[gp][0].pov[0] > 315) || (joystick_state[gp][0].pov[0] < 45))
                ret &= ~0xf0; // 1, 2, 3, 4
            // POV Right
            else if ((joystick_state[gp][0].pov[0] >= 45) && (joystick_state[gp][0].pov[0] < 135))
                ret &= ~0xb0; // 1, 2, 4
            // POV Down
            else if ((joystick_state[gp][0].pov[0] >= 135) && (joystick_state[gp][0].pov[0] < 225))
                ret &= ~0x70; // 1, 2, 3
            // POV Left
            else if ((joystick_state[gp][0].pov[0] >= 225) && (joystick_state[gp][0].pov[0] < 315))
                ret &= ~0x30; // 1, 2
        }
    }

    return ret;
}

static uint8_t
ch_virtual_pilot_pro_read(UNUSED(void *priv))
{
    uint8_t gp  = 0;
    uint8_t ret = 0xf0;

    if (JOYSTICK_PRESENT(gp, 0)) {
        if (joystick_state[gp][0].button[0]) // 1
            ret &= ~0x10;
        if (joystick_state[gp][0].button[1]) // 2
            ret &= ~0x20;
        if (joystick_state[gp][0].button[2]) // 3
            ret &= ~0x40;
        if (joystick_state[gp][0].button[3]) // 4
            ret &= ~0x80;
        if (joystick_state[gp][0].button[4]) // 1, 3
            ret &= ~0x50;
        if (joystick_state[gp][0].button[5]) // 1, 4
            ret &= ~0x90;

        // Right POV Hat
        uint8_t pov_id = 0;

        if (joystick_state[gp][0].pov[pov_id] != -1) {
            // POV Up
            if ((joystick_state[gp][0].pov[pov_id] > 315) || (joystick_state[gp][0].pov[pov_id] < 45))
                ret &= ~0xf0; // 1, 2, 3, 4
            // POV Right
            else if ((joystick_state[gp][0].pov[pov_id] >= 45) && (joystick_state[gp][0].pov[pov_id] < 135))
                ret &= ~0xb0; // 1, 2, 4
            // POV Down
            else if ((joystick_state[gp][0].pov[pov_id] >= 135) && (joystick_state[gp][0].pov[pov_id] < 225))
                ret &= ~0x70; // 1, 2, 3
            // POV Left
            else if ((joystick_state[gp][0].pov[pov_id] >= 225) && (joystick_state[gp][0].pov[pov_id] < 315))
                ret &= ~0x30; // 1, 2
        }

        // Left POV Hat
        pov_id = 1;

        if (joystick_state[gp][0].pov[pov_id] != -1) {
            // POV Up
            if ((joystick_state[gp][0].pov[pov_id] > 315) || (joystick_state[gp][0].pov[pov_id] < 45))
                ret &= ~0xe0; // 2, 3, 4
            // POV Right
            else if ((joystick_state[gp][0].pov[pov_id] >= 45) && (joystick_state[gp][0].pov[pov_id] < 135))
                ret &= ~0xa0; // 2, 4
            // POV Down
            else if ((joystick_state[gp][0].pov[pov_id] >= 135) && (joystick_state[gp][0].pov[pov_id] < 225))
                ret &= ~0x60; // 2, 3
            // POV Left
            else if ((joystick_state[gp][0].pov[pov_id] >= 225) && (joystick_state[gp][0].pov[pov_id] < 315))
                ret &= ~0xc0; // 3, 4
        }
    }

    return ret;
}

const joystick_t joystick_ch_flightstick = {
    .name          = "CH Flightstick",
    .internal_name = "ch_flightstick",
    .init          = joystick_standard_init,
    .close         = joystick_standard_close,
    .read          = joystick_standard_read_2button,
    .write         = joystick_standard_write,
    .read_axis     = joystick_standard_read_axis_3axis_throttle,
    .a0_over       = joystick_standard_a0_over,
    .axis_count    = 3,
    .button_count  = 2,
    .pov_count     = 0,
    .max_joysticks = 1,
    .axis_names    = { "X axis (Roll)", "Y axis (Pitch)", "Throttle" },
    .button_names  = { "Trigger", "Button 2" },
    .pov_names     = { NULL }
};

const joystick_t joystick_ch_flightstick_ch_pedals = {
    .name          = "CH Flightstick + CH Pedals",
    .internal_name = "ch_flightstick_ch_pedals",
    .init          = joystick_standard_init,
    .close         = joystick_standard_close,
    .read          = joystick_standard_read_2button,
    .write         = joystick_standard_write,
    .read_axis     = joystick_standard_read_axis_4axis,
    .a0_over       = joystick_standard_a0_over,
    .axis_count    = 4,
    .button_count  = 2,
    .pov_count     = 0,
    .max_joysticks = 1,
    .axis_names    = { "X axis (Roll)", "Y axis (Pitch)", "Throttle", "Rudder (Yaw)" },
    .button_names  = { "Trigger", "Button 2" },
    .pov_names     = { NULL }
};

const joystick_t joystick_ch_flightstick_ch_pedals_pro = {
    .name          = "CH Flightstick + CH Pedals Pro",
    .internal_name = "ch_flightstick_ch_pedals_pro",
    .init          = joystick_standard_init,
    .close         = joystick_standard_close,
    .read          = joystick_standard_read_2button,
    .write         = joystick_standard_write,
    .read_axis     = joystick_standard_read_axis_4axis,
    .a0_over       = joystick_standard_a0_over,
    .axis_count    = 4,
    .button_count  = 2,
    .pov_count     = 0,
    .max_joysticks = 1,
    .axis_names    = { "X axis (Roll)", "Y axis (Pitch)", "Left Pedal", "Right Pedal" },
    .button_names  = { "Trigger", "Button 2" },
    .pov_names     = { NULL }
};

const joystick_t joystick_ch_flightstick_pro = {
    .name          = "CH Flightstick Pro",
    .internal_name = "ch_flightstick_pro",
    .init          = joystick_standard_init,
    .close         = joystick_standard_close,
    .read          = ch_flightstick_pro_read,
    .write         = joystick_standard_write,
    .read_axis     = joystick_standard_read_axis_3axis_throttle,
    .a0_over       = joystick_standard_a0_over,
    .axis_count    = 3,
    .button_count  = 4,
    .pov_count     = 1,
    .max_joysticks = 1,
    .axis_names    = { "X axis (Roll)", "Y axis (Pitch)", "Throttle" },
    .button_names  = { "Trigger", "Button 2", "Button 3", "Button 4" },
    .pov_names     = { "POV" }
};

const joystick_t joystick_ch_flightstick_pro_ch_pedals = {
    .name          = "CH Flightstick Pro + CH Pedals",
    .internal_name = "ch_flightstick_pro_ch_pedals",
    .init          = joystick_standard_init,
    .close         = joystick_standard_close,
    .read          = ch_flightstick_pro_read,
    .write         = joystick_standard_write,
    .read_axis     = joystick_standard_read_axis_4axis,
    .a0_over       = joystick_standard_a0_over,
    .axis_count    = 4,
    .button_count  = 4,
    .pov_count     = 1,
    .max_joysticks = 1,
    .axis_names    = { "X axis (Roll)", "Y axis (Pitch)", "Throttle", "Rudder (Yaw)" },
    .button_names  = { "Trigger", "Button 2", "Button 3", "Button 4" },
    .pov_names     = { "POV" }
};

const joystick_t joystick_ch_flightstick_pro_ch_pedals_pro = {
    .name          = "CH Flightstick Pro + CH Pedals Pro",
    .internal_name = "ch_flightstick_pro_ch_pedals_pro",
    .init          = joystick_standard_init,
    .close         = joystick_standard_close,
    .read          = ch_flightstick_pro_read,
    .write         = joystick_standard_write,
    .read_axis     = joystick_standard_read_axis_4axis,
    .a0_over       = joystick_standard_a0_over,
    .axis_count    = 4,
    .button_count  = 4,
    .pov_count     = 1,
    .max_joysticks = 1,
    .axis_names    = { "X axis (Roll)", "Y axis (Pitch)", "Left Pedal", "Right Pedal" },
    .button_names  = { "Trigger", "Button 2", "Button 3", "Button 4" },
    .pov_names     = { "POV" }
};

const joystick_t joystick_ch_virtual_pilot = {
    .name          = "CH Virtual Pilot",
    .internal_name = "ch_virtual_pilot",
    .init          = joystick_standard_init,
    .close         = joystick_standard_close,
    .read          = joystick_standard_read_2button,
    .write         = joystick_standard_write,
    .read_axis     = joystick_standard_read_axis_3axis_throttle,
    .a0_over       = joystick_standard_a0_over,
    .axis_count    = 3,
    .button_count  = 2,
    .pov_count     = 0,
    .max_joysticks = 1,
    .axis_names    = { "X axis (Roll)", "Y axis (Pitch)", "Throttle" },
    .button_names  = { "Button 1", "Button 2" },
    .pov_names     = { NULL }
};

const joystick_t joystick_ch_virtual_pilot_ch_pedals = {
    .name          = "CH Virtual Pilot + CH Pedals",
    .internal_name = "ch_virtual_pilot_ch_pedals",
    .init          = joystick_standard_init,
    .close         = joystick_standard_close,
    .read          = joystick_standard_read_2button,
    .write         = joystick_standard_write,
    .read_axis     = joystick_standard_read_axis_4axis,
    .a0_over       = joystick_standard_a0_over,
    .axis_count    = 4,
    .button_count  = 2,
    .pov_count     = 0,
    .max_joysticks = 1,
    .axis_names    = { "X axis (Roll)", "Y axis (Pitch)", "Throttle", "Rudder (Yaw)" },
    .button_names  = { "Button 1", "Button 2" },
    .pov_names     = { NULL }
};

const joystick_t joystick_ch_virtual_pilot_ch_pedals_pro = {
    .name          = "CH Virtual Pilot + CH Pedals Pro",
    .internal_name = "ch_virtual_pilot_ch_pedals_pro",
    .init          = joystick_standard_init,
    .close         = joystick_standard_close,
    .read          = joystick_standard_read_2button,
    .write         = joystick_standard_write,
    .read_axis     = joystick_standard_read_axis_4axis,
    .a0_over       = joystick_standard_a0_over,
    .axis_count    = 4,
    .button_count  = 2,
    .pov_count     = 0,
    .max_joysticks = 1,
    .axis_names    = { "X axis (Roll)", "Y axis (Pitch)", "Left Pedal", "Right Pedal" },
    .button_names  = { "Button 1", "Button 2" },
    .pov_names     = { NULL }
};

const joystick_t joystick_ch_virtual_pilot_pro = {
    .name          = "CH Virtual Pilot Pro",
    .internal_name = "ch_virtual_pilot_pro",
    .init          = joystick_standard_init,
    .close         = joystick_standard_close,
    .read          = ch_virtual_pilot_pro_read,
    .write         = joystick_standard_write,
    .read_axis     = joystick_standard_read_axis_3axis_throttle,
    .a0_over       = joystick_standard_a0_over,
    .axis_count    = 3,
    .button_count  = 6,
    .pov_count     = 2,
    .max_joysticks = 1,
    .axis_names    = { "X axis (Roll)", "Y axis (Pitch)", "Throttle" },
    .button_names  = { "Button 1", "Button 2", "Button 3", "Button 4", "Button 5", "Button 6" },
    .pov_names     = { "Right POV", "Left POV" }
};

const joystick_t joystick_ch_virtual_pilot_pro_ch_pedals = {
    .name          = "CH Virtual Pilot Pro + CH Pedals",
    .internal_name = "ch_virtual_pilot_pro_ch_pedals",
    .init          = joystick_standard_init,
    .close         = joystick_standard_close,
    .read          = ch_virtual_pilot_pro_read,
    .write         = joystick_standard_write,
    .read_axis     = joystick_standard_read_axis_4axis,
    .a0_over       = joystick_standard_a0_over,
    .axis_count    = 4,
    .button_count  = 6,
    .pov_count     = 2,
    .max_joysticks = 1,
    .axis_names    = { "X axis (Roll)", "Y axis (Pitch)", "Throttle", "Rudder (Yaw)" },
    .button_names  = { "Button 1", "Button 2", "Button 3", "Button 4", "Button 5", "Button 6" },
    .pov_names     = { "Right POV", "Left POV" }
};

const joystick_t joystick_ch_virtual_pilot_pro_ch_pedals_pro = {
    .name          = "CH Virtual Pilot Pro + CH Pedals Pro",
    .internal_name = "ch_virtual_pilot_pro_ch_pedals_pro",
    .init          = joystick_standard_init,
    .close         = joystick_standard_close,
    .read          = ch_virtual_pilot_pro_read,
    .write         = joystick_standard_write,
    .read_axis     = joystick_standard_read_axis_4axis,
    .a0_over       = joystick_standard_a0_over,
    .axis_count    = 4,
    .button_count  = 6,
    .pov_count     = 2,
    .max_joysticks = 1,
    .axis_names    = { "X axis (Roll)", "Y axis (Pitch)", "Left Pedal", "Right Pedal" },
    .button_names  = { "Button 1", "Button 2", "Button 3", "Button 4", "Button 5", "Button 6" },
    .pov_names     = { "Right POV", "Left POV" }
};
