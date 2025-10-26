/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          Implementation of a standard joystick.
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

void *
joystick_standard_init(void)
{
    return NULL;
}

void
joystick_standard_close(UNUSED(void *priv))
{
    //
}

static uint8_t
joystick_paddle_read(UNUSED(void *priv))
{
    uint8_t gp  = 0;
    uint8_t ret = 0xf0;

    if ((JOYSTICK_PRESENT(gp, 0)) && (joystick_state[gp][0].button[0]))
            ret &= ~0x10;

    if ((JOYSTICK_PRESENT(gp, 2)) && (joystick_state[gp][2].button[0]))
            ret &= ~0x20;

    if ((JOYSTICK_PRESENT(gp, 1)) && (joystick_state[gp][1].button[0]))
            ret &= ~0x40;

    if ((JOYSTICK_PRESENT(gp, 3)) && (joystick_state[gp][3].button[0]))
            ret &= ~0x80;

    return ret;
}

static uint8_t
joystick_standard_read(UNUSED(void *priv))
{
    uint8_t gp  = 0;
    uint8_t ret = 0xf0;

    if (JOYSTICK_PRESENT(gp, 0)) {
        if (joystick_state[gp][0].button[0])
            ret &= ~0x10;
        if (joystick_state[gp][0].button[1])
            ret &= ~0x20;
    }

    if (JOYSTICK_PRESENT(gp, 1)) {
        if (joystick_state[gp][1].button[0])
            ret &= ~0x40;
        if (joystick_state[gp][1].button[1])
            ret &= ~0x80;
    }

    return ret;
}

uint8_t
joystick_standard_read_2button(UNUSED(void *priv))
{
    uint8_t gp  = 0;
    uint8_t ret = 0xf0;

    if (JOYSTICK_PRESENT(gp, 0)) {
        if (joystick_state[gp][0].button[0])
            ret &= ~0x10;
        if (joystick_state[gp][0].button[1])
            ret &= ~0x20;
    }

    return ret;
}

static uint8_t
joystick_standard_read_3button(UNUSED(void *priv))
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
    }

    return ret;
}

uint8_t
joystick_standard_read_4button(UNUSED(void *priv))
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
    }

    return ret;
}

void
joystick_standard_write(UNUSED(void *priv))
{
    //
}

static int
joystick_paddle_read_axis(UNUSED(void *priv), int axis)
{
    uint8_t gp = 0;

    switch (axis) {
        case 0:
            if (!JOYSTICK_PRESENT(gp, 0))
                return AXIS_NOT_PRESENT;
            return joystick_state[gp][0].axis[0];
        case 1:
            if (!JOYSTICK_PRESENT(gp, 2))
                return AXIS_NOT_PRESENT;
            return joystick_state[gp][2].axis[0];
        case 2:
            if (!JOYSTICK_PRESENT(gp, 1))
                return AXIS_NOT_PRESENT;
            return joystick_state[gp][1].axis[0];
        case 3:
            if (!JOYSTICK_PRESENT(gp, 3))
                return AXIS_NOT_PRESENT;
            return joystick_state[gp][3].axis[0];
        default:
            return 0;
    }
}

static int
joystick_standard_read_axis(UNUSED(void *priv), int axis)
{
    uint8_t gp = 0;

    switch (axis) {
        case 0:
            if (!JOYSTICK_PRESENT(gp, 0))
                return AXIS_NOT_PRESENT;
            return joystick_state[gp][0].axis[0];
        case 1:
            if (!JOYSTICK_PRESENT(gp, 0))
                return AXIS_NOT_PRESENT;
            return joystick_state[gp][0].axis[1];
        case 2:
            if (!JOYSTICK_PRESENT(gp, 1))
                return AXIS_NOT_PRESENT;
            return joystick_state[gp][1].axis[0];
        case 3:
            if (!JOYSTICK_PRESENT(gp, 1))
                return AXIS_NOT_PRESENT;
            return joystick_state[gp][1].axis[1];
        default:
            return 0;
    }
}

static int
joystick_standard_read_axis_2axis(UNUSED(void *priv), int axis)
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
        case 3:
        default:
            return 0;
    }
}

static int
joystick_standard_read_axis_2axis_t1t2wa(UNUSED(void *priv), int axis)
{
    uint8_t gp = 0;

    if (!JOYSTICK_PRESENT(gp, 0))
        return AXIS_NOT_PRESENT;

    switch (axis) {
        case 0:
            return joystick_state[gp][0].axis[0];
        case 2:
            return joystick_state[gp][0].axis[1];
        case 1:
        case 3:
        default:
            return 0;
    }
}

#if 0
// For later use
static int
joystick_standard_read_axis_with_pov(UNUSED(void *priv), int axis)
{
    uint8_t gp = 0;

    if (!JOYSTICK_PRESENT(gp, 0))
        return AXIS_NOT_PRESENT;

    switch (axis) {
        case 0: // X-axis
            return joystick_state[gp][0].axis[0];
        case 1: // Y-axis
            return joystick_state[gp][0].axis[1];
        case 2: // POV Hat
            if (joystick_state[gp][0].pov[0] == -1)
                return 32767; // Centered/No input (as per tm_fcs_rcs_read_axis example)
            if (joystick_state[gp][0].pov[0] > 315 || joystick_state[gp][0].pov[0] < 45)
                return -32768; // Up
            if (joystick_state[gp][0].pov[0] >= 45 && joystick_state[gp][0].pov[0] < 135)
                return -16384; // Up-Right (example value, matches tm_fcs_rcs_read_axis)
            if (joystick_state[gp][0].pov[0] >= 135 && joystick_state[gp][0].pov[0] < 225)
                return 0; // Right/Left (example, matches tm_fcs_rcs_read_axis)
            if (joystick_state[gp][0].pov[0] >= 225 && joystick_state[gp][0].pov[0] < 315)
                return 16384; // Down-Left (example value, matches tm_fcs_rcs_read_axis)
            return 0;
        case 3:
            return 0;
        default:
            return 0;
    }
}
#endif

static int
joystick_standard_read_axis_3axis(UNUSED(void *priv), int axis)
{
    uint8_t gp = 0;

    if (!JOYSTICK_PRESENT(gp, 0))
        return AXIS_NOT_PRESENT;

    switch (axis) {
        case 0:
            return joystick_state[gp][0].axis[0];
        case 1:
            return joystick_state[gp][0].axis[1];
        case 2: // Rudder Axis
            return joystick_state[gp][0].axis[2];
        case 3: // Throttle Axis
        default:
            return 0;
    }
}

int
joystick_standard_read_axis_3axis_throttle(UNUSED(void *priv), int axis)
{
    uint8_t gp = 0;

    if (!JOYSTICK_PRESENT(gp, 0))
        return AXIS_NOT_PRESENT;

    switch (axis) {
        case 0:
            return joystick_state[gp][0].axis[0];
        case 1:
            return joystick_state[gp][0].axis[1];
        case 3: // Throttle Axis
            return joystick_state[gp][0].axis[2];
        case 2: // Rudder Axis
        default:
            return 0;
    }
}

int
joystick_standard_read_axis_4axis(UNUSED(void *priv), int axis)
{
    uint8_t gp = 0;

    if (!JOYSTICK_PRESENT(gp, 0))
        return AXIS_NOT_PRESENT;

    switch (axis) {
        case 0:
            return joystick_state[gp][0].axis[0];
        case 1:
            return joystick_state[gp][0].axis[1];
        case 2: // Rudder Axis
            return joystick_state[gp][0].axis[3];
        case 3: // Throttle Axis
            return joystick_state[gp][0].axis[2];
        default:
            return 0;
    }
}

static int
joystick_standard_read_axis_6button(UNUSED(void *priv), int axis)
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
            return joystick_state[gp][0].button[4] ? -32767 : 32768;
        case 3:
            return joystick_state[gp][0].button[5] ? -32767 : 32768;
        default:
            return 0;
    }
}
static int
joystick_standard_read_axis_8button(UNUSED(void *priv), int axis)
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
            if (joystick_state[gp][0].button[4])
                return -32767;
            if (joystick_state[gp][0].button[6])
                return 32768;
            return 0;
        case 3:
            if (joystick_state[gp][0].button[5])
                return -32767;
            if (joystick_state[gp][0].button[7])
                return 32768;
            return 0;
        default:
            return 0;
    }
}

void
joystick_standard_a0_over(UNUSED(void *priv))
{
    //
}

const joystick_t joystick_generic_paddle = {
    .name          = "Generic paddle controller(s)",
    .internal_name = "generic_paddle",
    .init          = joystick_standard_init,
    .close         = joystick_standard_close,
    .read          = joystick_paddle_read,
    .write         = joystick_standard_write,
    .read_axis     = joystick_paddle_read_axis,
    .a0_over       = joystick_standard_a0_over,
    .axis_count    = 1,
    .button_count  = 1,
    .pov_count     = 0,
    .max_joysticks = 4,
    .axis_names    = { "X axis" },
    .button_names  = { "Button 1" },
    .pov_names     = { NULL }
};

const joystick_t joystick_2axis_1button = {
    .name          = "2-axis, 1-button joystick(s)",
    .internal_name = "2axis_1button",
    .init          = joystick_standard_init,
    .close         = joystick_standard_close,
    .read          = joystick_standard_read,
    .write         = joystick_standard_write,
    .read_axis     = joystick_standard_read_axis,
    .a0_over       = joystick_standard_a0_over,
    .axis_count    = 2,
    .button_count  = 1,
    .pov_count     = 0,
    .max_joysticks = 2,
    .axis_names    = { "X axis", "Y axis" },
    .button_names  = { "Button 1" },
    .pov_names     = { NULL }
};

const joystick_t joystick_2axis_2button = {
    .name          = "2-axis, 2-button joystick(s)",
    .internal_name = "2axis_2button",
    .init          = joystick_standard_init,
    .close         = joystick_standard_close,
    .read          = joystick_standard_read,
    .write         = joystick_standard_write,
    .read_axis     = joystick_standard_read_axis,
    .a0_over       = joystick_standard_a0_over,
    .axis_count    = 2,
    .button_count  = 2,
    .pov_count     = 0,
    .max_joysticks = 2,
    .axis_names    = { "X axis", "Y axis" },
    .button_names  = { "Button 1", "Button 2" },
    .pov_names     = { NULL }
};

const joystick_t joystick_2axis_3button = {
    .name          = "2-axis, 3-button joystick",
    .internal_name = "2axis_3button",
    .init          = joystick_standard_init,
    .close         = joystick_standard_close,
    .read          = joystick_standard_read_3button,
    .write         = joystick_standard_write,
    .read_axis     = joystick_standard_read_axis_2axis,
    .a0_over       = joystick_standard_a0_over,
    .axis_count    = 2,
    .button_count  = 3,
    .pov_count     = 0,
    .max_joysticks = 1,
    .axis_names    = { "X axis", "Y axis" },
    .button_names  = { "Button 1", "Button 2", "Button 3" },
    .pov_names     = { NULL }
};

const joystick_t joystick_2axis_4button = {
    .name          = "2-axis, 4-button joystick",
    .internal_name = "2axis_4button",
    .init          = joystick_standard_init,
    .close         = joystick_standard_close,
    .read          = joystick_standard_read_4button,
    .write         = joystick_standard_write,
    .read_axis     = joystick_standard_read_axis_2axis,
    .a0_over       = joystick_standard_a0_over,
    .axis_count    = 2,
    .button_count  = 4,
    .pov_count     = 0,
    .max_joysticks = 1,
    .axis_names    = { "X axis", "Y axis" },
    .button_names  = { "Button 1", "Button 2", "Button 3", "Button 4" },
    .pov_names     = { NULL }
};

const joystick_t joystick_2axis_6button = {
    .name          = "2-axis, 6-button joystick",
    .internal_name = "2axis_6button",
    .init          = joystick_standard_init,
    .close         = joystick_standard_close,
    .read          = joystick_standard_read_4button,
    .write         = joystick_standard_write,
    .read_axis     = joystick_standard_read_axis_6button,
    .a0_over       = joystick_standard_a0_over,
    .axis_count    = 2,
    .button_count  = 6,
    .pov_count     = 0,
    .max_joysticks = 1,
    .axis_names    = { "X axis", "Y axis" },
    .button_names  = { "Button 1", "Button 2", "Button 3", "Button 4", "Button 5", "Button 6" },
    .pov_names     = { NULL }
};

const joystick_t joystick_2axis_8button = {
    .name          = "2-axis, 8-button joystick",
    .internal_name = "2axis_8button",
    .init          = joystick_standard_init,
    .close         = joystick_standard_close,
    .read          = joystick_standard_read_4button,
    .write         = joystick_standard_write,
    .read_axis     = joystick_standard_read_axis_8button,
    .a0_over       = joystick_standard_a0_over,
    .axis_count    = 2,
    .button_count  = 8,
    .pov_count     = 0,
    .max_joysticks = 1,
    .axis_names    = { "X axis", "Y axis" },
    .button_names  = { "Button 1", "Button 2", "Button 3", "Button 4", "Button 5", "Button 6", "Button 7", "Button 8" },
    .pov_names     = { NULL }
};

const joystick_t joystick_3axis_2button = {
    .name          = "3-axis, 2-button joystick",
    .internal_name = "3axis_2button",
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
    .axis_names    = { "X axis", "Y axis", "Z axis" },
    .button_names  = { "Button 1", "Button 2" },
    .pov_names     = { NULL }
};

const joystick_t joystick_3axis_3button = {
    .name          = "3-axis, 3-button joystick",
    .internal_name = "3axis_3button",
    .init          = joystick_standard_init,
    .close         = joystick_standard_close,
    .read          = joystick_standard_read_3button,
    .write         = joystick_standard_write,
    .read_axis     = joystick_standard_read_axis_3axis_throttle,
    .a0_over       = joystick_standard_a0_over,
    .axis_count    = 3,
    .button_count  = 3,
    .pov_count     = 0,
    .max_joysticks = 1,
    .axis_names    = { "X axis", "Y axis", "Z axis" },
    .button_names  = { "Button 1", "Button 2", "Button 3" },
    .pov_names     = { NULL }
};

const joystick_t joystick_3axis_4button = {
    .name          = "3-axis, 4-button joystick",
    .internal_name = "3axis_4button",
    .init          = joystick_standard_init,
    .close         = joystick_standard_close,
    .read          = joystick_standard_read_4button,
    .write         = joystick_standard_write,
    .read_axis     = joystick_standard_read_axis_3axis_throttle,
    .a0_over       = joystick_standard_a0_over,
    .axis_count    = 3,
    .button_count  = 4,
    .pov_count     = 0,
    .max_joysticks = 1,
    .axis_names    = { "X axis", "Y axis", "Z axis" },
    .button_names  = { "Button 1", "Button 2", "Button 3", "Button 4" },
    .pov_names     = { NULL }
};

const joystick_t joystick_4axis_2button = {
    .name          = "4-axis, 2-button joystick",
    .internal_name = "4axis_2button",
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
    .axis_names    = { "X axis", "Y axis", "Z axis", "zX axis" },
    .button_names  = { "Button 1", "Button 2" },
    .pov_names     = { NULL }
};

const joystick_t joystick_4axis_3button = {
    .name          = "4-axis, 3-button joystick",
    .internal_name = "4axis_3button",
    .init          = joystick_standard_init,
    .close         = joystick_standard_close,
    .read          = joystick_standard_read_3button,
    .write         = joystick_standard_write,
    .read_axis     = joystick_standard_read_axis_4axis,
    .a0_over       = joystick_standard_a0_over,
    .axis_count    = 4,
    .button_count  = 3,
    .pov_count     = 0,
    .max_joysticks = 1,
    .axis_names    = { "X axis", "Y axis", "Z axis", "zX axis" },
    .button_names  = { "Button 1", "Button 2", "Button 3" },
    .pov_names     = { NULL }
};

const joystick_t joystick_4axis_4button = {
    .name          = "4-axis, 4-button joystick",
    .internal_name = "4axis_4button",
    .init          = joystick_standard_init,
    .close         = joystick_standard_close,
    .read          = joystick_standard_read_4button,
    .write         = joystick_standard_write,
    .read_axis     = joystick_standard_read_axis_4axis,
    .a0_over       = joystick_standard_a0_over,
    .axis_count    = 4,
    .button_count  = 4,
    .pov_count     = 0,
    .max_joysticks = 1,
    .axis_names    = { "X axis", "Y axis", "Z axis", "zX axis" },
    .button_names  = { "Button 1", "Button 2", "Button 3", "Button 4" },
    .pov_names     = { NULL }
};

const joystick_t joystick_2button_gamepad = {
    .name          = "2-button gamepad(s)",
    .internal_name = "2button_gamepad",
    .init          = joystick_standard_init,
    .close         = joystick_standard_close,
    .read          = joystick_standard_read,
    .write         = joystick_standard_write,
    .read_axis     = joystick_standard_read_axis,
    .a0_over       = joystick_standard_a0_over,
    .axis_count    = 2,
    .button_count  = 2,
    .pov_count     = 0,
    .max_joysticks = 2,
    .axis_names    = { "X axis", "Y axis" },
    .button_names  = { "Button 1", "Button 2" },
    .pov_names     = { NULL }
};

const joystick_t joystick_3button_gamepad = {
    .name          = "3-button gamepad",
    .internal_name = "3button_gamepad",
    .init          = joystick_standard_init,
    .close         = joystick_standard_close,
    .read          = joystick_standard_read_3button,
    .write         = joystick_standard_write,
    .read_axis     = joystick_standard_read_axis_2axis,
    .a0_over       = joystick_standard_a0_over,
    .axis_count    = 2,
    .button_count  = 3,
    .pov_count     = 0,
    .max_joysticks = 1,
    .axis_names    = { "X axis", "Y axis" },
    .button_names  = { "Button 1", "Button 2", "Button 3" },
    .pov_names     = { NULL }
};

const joystick_t joystick_4button_gamepad = {
    .name          = "4-button gamepad",
    .internal_name = "4button_gamepad",
    .init          = joystick_standard_init,
    .close         = joystick_standard_close,
    .read          = joystick_standard_read_4button,
    .write         = joystick_standard_write,
    .read_axis     = joystick_standard_read_axis_2axis,
    .a0_over       = joystick_standard_a0_over,
    .axis_count    = 2,
    .button_count  = 4,
    .pov_count     = 0,
    .max_joysticks = 1,
    .axis_names    = { "X axis", "Y axis" },
    .button_names  = { "Button 1", "Button 2", "Button 3", "Button 4" },
    .pov_names     = { NULL }
};

const joystick_t joystick_6button_gamepad = {
    .name          = "6-button gamepad",
    .internal_name = "6button_gamepad",
    .init          = joystick_standard_init,
    .close         = joystick_standard_close,
    .read          = joystick_standard_read_4button,
    .write         = joystick_standard_write,
    .read_axis     = joystick_standard_read_axis_6button,
    .a0_over       = joystick_standard_a0_over,
    .axis_count    = 2,
    .button_count  = 6,
    .pov_count     = 0,
    .max_joysticks = 1,
    .axis_names    = { "X axis", "Y axis" },
    .button_names  = { "Button 1", "Button 2", "Button 3", "Button 4", "Button 5", "Button 6" },
    .pov_names     = { NULL }
};

const joystick_t joystick_gravis_gamepad = {
    .name          = "Gravis PC GamePad",
    .internal_name = "gravis_gamepad",
    .init          = joystick_standard_init,
    .close         = joystick_standard_close,
    .read          = joystick_standard_read_4button,
    .write         = joystick_standard_write,
    .read_axis     = joystick_standard_read_axis_2axis,
    .a0_over       = joystick_standard_a0_over,
    .axis_count    = 2,
    .button_count  = 4,
    .pov_count     = 0,
    .max_joysticks = 1,
    .axis_names    = { "X axis", "Y axis" },
    // TODO: Check this
    .button_names  = { "Button 1 (Red)", "Button 2 (Blue)", "Button 3 (Yellow)", "Button 4 (Green)" },
    .pov_names     = { NULL }
};

const joystick_t joystick_2button_flight_yoke = {
    .name          = "2-button flight yoke",
    .internal_name = "2button_flight_yoke",
    .init          = joystick_standard_init,
    .close         = joystick_standard_close,
    .read          = joystick_standard_read,
    .write         = joystick_standard_write,
    .read_axis     = joystick_standard_read_axis,
    .a0_over       = joystick_standard_a0_over,
    .axis_count    = 2,
    .button_count  = 2,
    .pov_count     = 0,
    .max_joysticks = 2,
    .axis_names    = { "Roll axis", "Pitch axis" },
    .button_names  = { "Trigger", "Button 2" },
    .pov_names     = { NULL }
};

const joystick_t joystick_3button_flight_yoke = {
    .name          = "3-button flight yoke",
    .internal_name = "3button_flight_yoke",
    .init          = joystick_standard_init,
    .close         = joystick_standard_close,
    .read          = joystick_standard_read_3button,
    .write         = joystick_standard_write,
    .read_axis     = joystick_standard_read_axis_2axis,
    .a0_over       = joystick_standard_a0_over,
    .axis_count    = 2,
    .button_count  = 3,
    .pov_count     = 0,
    .max_joysticks = 1,
    .axis_names    = { "Roll axis", "Pitch axis" },
    .button_names  = { "Trigger", "Button 2", "Button 3" },
    .pov_names     = { NULL }
};

const joystick_t joystick_4button_flight_yoke = {
    .name          = "4-button flight yoke",
    .internal_name = "4button_flight_yoke",
    .init          = joystick_standard_init,
    .close         = joystick_standard_close,
    .read          = joystick_standard_read_4button,
    .write         = joystick_standard_write,
    .read_axis     = joystick_standard_read_axis_2axis,
    .a0_over       = joystick_standard_a0_over,
    .axis_count    = 2,
    .button_count  = 4,
    .pov_count     = 0,
    .max_joysticks = 1,
    .axis_names    = { "Roll axis", "Pitch axis" },
    .button_names  = { "Trigger", "Button 2", "Button 3", "Button 4" },
    .pov_names     = { NULL }
};

const joystick_t joystick_2button_yoke_throttle = {
    .name          = "2-button flight yoke with throttle",
    .internal_name = "2button_yoke_throttle",
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
    .axis_names    = { "Roll axis", "Pitch axis", "Throttle axis" },
    .button_names  = { "Trigger", "Button 2" },
    .pov_names     = { NULL }
};

const joystick_t joystick_3button_yoke_throttle = {
    .name          = "3-button flight yoke with throttle",
    .internal_name = "3button_yoke_throttle",
    .init          = joystick_standard_init,
    .close         = joystick_standard_close,
    .read          = joystick_standard_read_3button,
    .write         = joystick_standard_write,
    .read_axis     = joystick_standard_read_axis_3axis_throttle,
    .a0_over       = joystick_standard_a0_over,
    .axis_count    = 3,
    .button_count  = 3,
    .pov_count     = 0,
    .max_joysticks = 1,
    .axis_names    = { "Roll axis", "Pitch axis", "Throttle axis" },
    .button_names  = { "Trigger", "Button 2", "Button 3" },
    .pov_names     = { NULL }
};

const joystick_t joystick_4button_yoke_throttle = {
    .name          = "4-button flight yoke with throttle",
    .internal_name = "4button_yoke_throttle",
    .init          = joystick_standard_init,
    .close         = joystick_standard_close,
    .read          = joystick_standard_read_4button,
    .write         = joystick_standard_write,
    .read_axis     = joystick_standard_read_axis_3axis_throttle,
    .a0_over       = joystick_standard_a0_over,
    .axis_count    = 3,
    .button_count  = 4,
    .pov_count     = 0,
    .max_joysticks = 1,
    .axis_names    = { "Roll axis", "Pitch axis", "Throttle axis" },
    .button_names  = { "Trigger", "Button 2", "Button 3", "Button 4" },
    .pov_names     = { NULL }
};

const joystick_t joystick_steering_wheel_2_button = {
    .name          = "Steering wheel (3-axis, 2-button)",
    .internal_name = "steering_wheel_2_button",
    .init          = joystick_standard_init,
    .close         = joystick_standard_close,
    .read          = joystick_standard_read_2button,
    .write         = joystick_standard_write,
    .read_axis     = joystick_standard_read_axis_3axis,
    .a0_over       = joystick_standard_a0_over,
    .axis_count    = 3,
    .button_count  = 2,
    .pov_count     = 0,
    .max_joysticks = 1,
    .axis_names    = { "Steering axis", "Accelerator axis", "Brake axis" },
    .button_names  = { "Button 1", "Button 2" },
    .pov_names     = { NULL }
};

const joystick_t joystick_steering_wheel_3_button = {
    .name          = "Steering wheel (3-axis, 3-button)",
    .internal_name = "steering_wheel_3_button",
    .init          = joystick_standard_init,
    .close         = joystick_standard_close,
    .read          = joystick_standard_read_3button,
    .write         = joystick_standard_write,
    .read_axis     = joystick_standard_read_axis_3axis,
    .a0_over       = joystick_standard_a0_over,
    .axis_count    = 3,
    .button_count  = 3,
    .pov_count     = 0,
    .max_joysticks = 1,
    .axis_names    = { "Steering axis", "Accelerator axis", "Brake axis" },
    .button_names  = { "Button 1", "Button 2", "Button 3" },
    .pov_names     = { NULL }
};

const joystick_t joystick_steering_wheel_4_button = {
    .name          = "Steering wheel (3-axis, 4-button)",
    .internal_name = "steering_wheel_4_button",
    .init          = joystick_standard_init,
    .close         = joystick_standard_close,
    .read          = joystick_standard_read_4button,
    .write         = joystick_standard_write,
    .read_axis     = joystick_standard_read_axis_3axis,
    .a0_over       = joystick_standard_a0_over,
    .axis_count    = 3,
    .button_count  = 4,
    .pov_count     = 0,
    .max_joysticks = 1,
    .axis_names    = { "Steering axis", "Accelerator axis", "Brake axis" },
    .button_names  = { "Button 1", "Button 2", "Button 3", "Button 4" },
    .pov_names     = { NULL }
};

const joystick_t joystick_tm_formula_t1t2 = {
    .name          = "Thrustmaster Formula T1/T2 with adapter",
    .internal_name = "thrustmaster_formula_t1t2",
    .init          = joystick_standard_init,
    .close         = joystick_standard_close,
    .read          = joystick_standard_read_4button,
    .write         = joystick_standard_write,
    .read_axis     = joystick_standard_read_axis_2axis,
    .a0_over       = joystick_standard_a0_over,
    .axis_count    = 2,
    .button_count  = 4,
    .pov_count     = 0,
    .max_joysticks = 1,
    .axis_names    = { "Steering axis", "Accelerator/Brake axis" },
    .button_names  = { "Shifter Up", "Shifter Down", "Top Console Switch", "Bottom Console Switch" },
    .pov_names     = { NULL }
};

// TODO Validate this
const joystick_t joystick_tm_formula_t1t2wa = {
    .name          = "Thrustmaster Formula T1/T2 without adapter",
    .internal_name = "thrustmaster_formula_t1t2wa",
    .init          = joystick_standard_init,
    .close         = joystick_standard_close,
    .read          = joystick_standard_read_4button,
    .write         = joystick_standard_write,
    .read_axis     = joystick_standard_read_axis_2axis_t1t2wa,
    .a0_over       = joystick_standard_a0_over,
    .axis_count    = 2,
    .button_count  = 4,
    .pov_count     = 0,
    .max_joysticks = 1,
    .axis_names    = { "Steering axis", "Accelerator/Brake axis" },
    .button_names  = { "Shifter Up", "Shifter Down", "Top Console Switch", "Bottom Console Switch" },
    .pov_names     = { NULL }
};
