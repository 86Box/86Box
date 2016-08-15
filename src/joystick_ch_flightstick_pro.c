/* Copyright holders: Sarah Walker
   see COPYING for more details
*/
#include <stdlib.h>
#include "ibm.h"
#include "device.h"
#include "timer.h"
#include "gameport.h"
#include "joystick_standard.h"
#include "plat-joystick.h"

static void *ch_flightstick_pro_init()
{
}

static void ch_flightstick_pro_close(void *p)
{
}

static uint8_t ch_flightstick_pro_read(void *p)
{
        uint8_t ret = 0xf0;
        
        if (JOYSTICK_PRESENT(0))
        {
                if (joystick_state[0].button[0])
                        ret &= ~0x10;
                if (joystick_state[0].button[1])
                        ret &= ~0x20;
                if (joystick_state[0].button[2])
                        ret &= ~0x40;
                if (joystick_state[0].button[3])
                        ret &= ~0x80;
                if (joystick_state[0].pov[0] != -1)
                {
                        if (joystick_state[0].pov[0] > 315 || joystick_state[0].pov[0] < 45)
                                ret &= ~0xf0;
                        else if (joystick_state[0].pov[0] >= 45 && joystick_state[0].pov[0] < 135)
                                ret &= ~0xb0;
                        else if (joystick_state[0].pov[0] >= 135 && joystick_state[0].pov[0] < 225)
                                ret &= ~0x70;
                        else if (joystick_state[0].pov[0] >= 225 && joystick_state[0].pov[0] < 315)
                                ret &= ~0x30;
                }
        }

        return ret;
}

static void ch_flightstick_pro_write(void *p)
{
}

static int ch_flightstick_pro_read_axis(void *p, int axis)
{
        if (!JOYSTICK_PRESENT(0))
                return AXIS_NOT_PRESENT;

        switch (axis)
        {
                case 0:
                return joystick_state[0].axis[0];
                case 1:
                return joystick_state[0].axis[1];
                case 2:
                return 0;
                case 3:
                return joystick_state[0].axis[2];
        }
}

static void ch_flightstick_pro_a0_over(void *p)
{
}

joystick_if_t joystick_ch_flightstick_pro =
{
        .name      = "CH Flightstick Pro",
        .init      = ch_flightstick_pro_init,
        .close     = ch_flightstick_pro_close,
        .read      = ch_flightstick_pro_read,
        .write     = ch_flightstick_pro_write,
        .read_axis = ch_flightstick_pro_read_axis,
        .a0_over   = ch_flightstick_pro_a0_over,
        .max_joysticks = 1,
        .axis_count = 3,
        .button_count = 4,
        .pov_count = 1,
        .axis_names = {"X axis", "Y axis", "Throttle"},
        .button_names = {"Button 1", "Button 2", "Button 3", "Button 4"},
        .pov_names = {"POV"}
};
