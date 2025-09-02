// This code emulates Logitech controllers

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

const joystick_t joystick_logitech_thunderpad = {
    .name          = "Logitech Thunderpad",
    .internal_name = "logitech_thunderpad",
    .init          = logitech_init,
    .close         = logitech_close,
    .read          = logitech_read,
    .write         = logitech_write,
    .read_axis     = logitech_read_axis,
    .a0_over       = logitech_a0_over,
    .axis_count    = 2,
    .button_count  = 4,
    .pov_count     = 0,
    .max_joysticks = 1,
    .axis_names    = { "X axis", "Y axis" },
    .button_names  = { "Button 1", "Button 2", "Button 3", "Button 4" },
    .pov_names     = { NULL }
};

const joystick_t joystick_logitech_wingman = {
    .name          = "Logitech WingMan",
    .internal_name = "logitech_wingman",
    .init          = logitech_init,
    .close         = logitech_close,
    .read          = logitech_read,
    .write         = logitech_write,
    .read_axis     = logitech_read_axis,
    .a0_over       = logitech_a0_over,
    .axis_count    = 3,
    .button_count  = 2,
    .pov_count     = 0,
    .max_joysticks = 1,
    .axis_names    = { "X axis", "Y axis", "Z axis" },
    .button_names  = { "Button 1", "Button 2" },
    .pov_names     = { NULL }
};

const joystick_t joystick_logitech_wingman_extreme = {
    .name          = "Logitech WingMan Extreme",
    .internal_name = "logitech_wingman_extreme",
    .init          = logitech_init,
    .close         = logitech_close,
    .read          = logitech_read,
    .write         = logitech_write,
    .read_axis     = logitech_read_axis,
    .a0_over       = logitech_a0_over,
    .axis_count    = 2,
    .button_count  = 4,
    .pov_count     = 1,
    .max_joysticks = 1,
    .axis_names    = { "X axis", "Y axis" },
    .button_names  = { "Button 1", "Button 2", "Button 3", "Button 4" },
    .pov_names     = { "POV" }
};

const joystick_t joystick_logitech_wingman_light= {
    .name          = "Logitech WingMan Light",
    .internal_name = "logitech_wingman_light",
    .init          = logitech_init,
    .close         = logitech_close,
    .read          = logitech_read,
    .write         = logitech_write,
    .read_axis     = logitech_read_axis,
    .a0_over       = logitech_a0_over,
    .axis_count    = 2,
    .button_count  = 2,
    .pov_count     = 0,
    .max_joysticks = 1,
    .axis_names    = { "X axis", "Y axis" },
    .button_names  = { "Button 1", "Button 2" },
    .pov_names     = { NULL }
};