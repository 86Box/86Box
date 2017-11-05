#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <stdlib.h>
#include <wchar.h>
#include "../86box.h"
#include "../device.h"
#include "../timer.h"
#include "../plat_joystick.h"
#include "gameport.h"
#include "joystick_standard.h"


static void *ch_flightstick_pro_init()
{
	return NULL;
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
		default:
		return 0;
        }
}

static void ch_flightstick_pro_a0_over(void *p)
{
}

joystick_if_t joystick_ch_flightstick_pro =
{
        "CH Flightstick Pro",
        ch_flightstick_pro_init,
        ch_flightstick_pro_close,
        ch_flightstick_pro_read,
        ch_flightstick_pro_write,
        ch_flightstick_pro_read_axis,
        ch_flightstick_pro_a0_over,
        1,
        3,
        4,
        1,
        {"X axis", "Y axis", "Throttle"},
        {"Button 1", "Button 2", "Button 3", "Button 4"},
        {"POV"}
};
