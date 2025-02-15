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

static void *
ch_flightstick_pro_init(void)
{
    return NULL;
}

static void
ch_flightstick_pro_close(UNUSED(void *priv))
{
    //
}

static uint8_t
ch_flightstick_pro_read(UNUSED(void *priv))
{
    uint8_t ret = 0xf0;

    if (JOYSTICK_PRESENT(0, 0)) {
        if (joystick_state[0][0].button[0])
            ret &= ~0x10;
        if (joystick_state[0][0].button[1])
            ret &= ~0x20;
        if (joystick_state[0][0].button[2])
            ret &= ~0x40;
        if (joystick_state[0][0].button[3])
            ret &= ~0x80;
        if (joystick_state[0][0].pov[0] != -1) {
            if (joystick_state[0][0].pov[0] > 315 || joystick_state[0][0].pov[0] < 45)
                ret &= ~0xf0;
            else if (joystick_state[0][0].pov[0] >= 45 && joystick_state[0][0].pov[0] < 135)
                ret &= ~0xb0;
            else if (joystick_state[0][0].pov[0] >= 135 && joystick_state[0][0].pov[0] < 225)
                ret &= ~0x70;
            else if (joystick_state[0][0].pov[0] >= 225 && joystick_state[0][0].pov[0] < 315)
                ret &= ~0x30;
        }
    }

    return ret;
}

static void
ch_flightstick_pro_write(UNUSED(void *priv))
{
    //
}

static int
ch_flightstick_pro_read_axis(UNUSED(void *priv), int axis)
{
    if (!JOYSTICK_PRESENT(0, 0))
        return AXIS_NOT_PRESENT;

    switch (axis) {
        case 0:
            return joystick_state[0][0].axis[0];
        case 1:
            return joystick_state[0][0].axis[1];
        case 2:
            return 0;
        case 3:
            return joystick_state[0][0].axis[2];
        default:
            return 0;
    }
}

static void
ch_flightstick_pro_a0_over(UNUSED(void *priv))
{
    //
}

const joystick_if_t joystick_ch_flightstick_pro = {
    .name          = "CH Flightstick Pro",
    .internal_name = "ch_flightstick_pro",
    .init          = ch_flightstick_pro_init,
    .close         = ch_flightstick_pro_close,
    .read          = ch_flightstick_pro_read,
    .write         = ch_flightstick_pro_write,
    .read_axis     = ch_flightstick_pro_read_axis,
    .a0_over       = ch_flightstick_pro_a0_over,
    .axis_count    = 3,
    .button_count  = 4,
    .pov_count     = 1,
    .max_joysticks = 1,
    .axis_names    = { "X axis", "Y axis", "Throttle" },
    .button_names  = { "Button 1", "Button 2", "Button 3", "Button 4" },
    .pov_names     = { "POV" }
};
