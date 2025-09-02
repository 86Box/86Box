// This code emulates Gravis controllers

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

const joystick_t joystick_gravis_analog_joystick = {
    .name          = "Gravis Analog Joystick",
    .internal_name = "gravis_analog_joystick",
    .init          = gravis_init,
    .close         = gravis_close,
    .read          = gravis_read,
    .write         = gravis_write,
    .read_axis     = gravis_read_axis,
    .a0_over       = gravis_a0_over,
    .axis_count    = 2,
    .button_count  = 2,
    .pov_count     = 0,
    .max_joysticks = 1,
    .axis_names    = { "X axis", "Y axis" },
    .button_names  = { "Button 1", "Button 2" },
    .pov_names     = { NULL }
};

const joystick_t joystick_gravis_analog_pro_joystick = {
    .name          = "Gravis Analog Pro Joystick",
    .internal_name = "gravis_analog_pro_joystick",
    .init          = gravis_init,
    .close         = gravis_close,
    .read          = gravis_read,
    .write         = gravis_write,
    .read_axis     = gravis_read_axis,
    .a0_over       = gravis_a0_over,
    .axis_count    = 3,
    .button_count  = 4,
    .pov_count     = 0,
    .max_joysticks = 1,
    .axis_names    = { "X axis", "Y axis", "Z axis" },
    .button_names  = { "Button 1", "Button 2", "Button 3", "Button 4" },
    .pov_names     = { NULL }
};

const joystick_t joystick_gravis_gamepad = {
    .name          = "Gravis Gamepad",
    .internal_name = "gravis_gamepad",
    .init          = gravis_init,
    .close         = gravis_close,
    .read          = gravis_read,
    .write         = gravis_write,
    .read_axis     = gravis_read_axis,
    .a0_over       = gravis_a0_over,
    .axis_count    = 2,
    .button_count  = 4,
    .pov_count     = 0,
    .max_joysticks = 1,
    .axis_names    = { "X axis", "Y axis" },
    .button_names  = { "Button 1", "Button 2", "Button 3", "Button 4" },
    .pov_names     = { NULL }
};