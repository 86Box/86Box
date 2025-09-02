// This is an experimental controller, and it is used for testing purposes.

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

const joystick_t joystick_ultimate_controller = {
    .name          = "Ultimate Controller (EXPERIMENTAL)",
    .internal_name = "ultimate_controller",
    .init          = joystick_standard_init,
    .close         = joystick_standard_close,
    .read          = joystick_standard_read,
    .write         = joystick_standard_write,
    .read_axis     = joystick_standard_read_axis,
    .a0_over       = joystick_standard_a0_over,
    .axis_count    = 8,
    .button_count  = 32,
    .pov_count     = 4,
    .max_joysticks = 4,
    .axis_names    = { "X Axis", "Y Axis", "Z Axis", "Z Rotation", "X Rotation", "Y Rotation", "Slider 1", "Slider 2" },
    .button_names  = {
                      "Button 1",
                      "Button 2",
                      "Button 3",
                      "Button 4",
                      "Button 5",
                      "Button 6",
                      "Button 7",
                      "Button 8",
                      "Button 9",
                      "Button 10",
                      "Button 11",
                      "Button 12",
                      "Button 13",
                      "Button 14",
                      "Button 15",
                      "Button 16",
                      "Button 17",
                      "Button 18",
                      "Button 19",
                      "Button 20",
                      "Button 21",
                      "Button 22",
                      "Button 23",
                      "Button 24",
                      "Button 25",
                      "Button 26",
                      "Button 27",
                      "Button 28",
                      "Button 29",
                      "Button 30",
                      "Button 31",
                      "Button 32"
                      },
    .pov_names     = { "POV 1", "POV 2", "POV 3", "POV 4" }
};
