#include <stdlib.h>
#include "ibm.h"
#include "device.h"
#include "timer.h"
#include "gameport.h"
#include "joystick_standard.h"
#include "plat-joystick.h"

static void *tm_fcs_init()
{
	return NULL;
}

static void tm_fcs_close(void *p)
{
}

static uint8_t tm_fcs_read(void *p)
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
        }

        return ret;
}

static void tm_fcs_write(void *p)
{
}

static int tm_fcs_read_axis(void *p, int axis)
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
                if (joystick_state[0].pov[0] == -1)
                        return 32767;
                if (joystick_state[0].pov[0] > 315 || joystick_state[0].pov[0] < 45)
                        return -32768;
                if (joystick_state[0].pov[0] >= 45 && joystick_state[0].pov[0] < 135)
                        return -16384;
                if (joystick_state[0].pov[0] >= 135 && joystick_state[0].pov[0] < 225)
                        return 0;
                if (joystick_state[0].pov[0] >= 225 && joystick_state[0].pov[0] < 315)
                        return 16384;
                return 0;
		default:
		return 0;
        }
}

static void tm_fcs_a0_over(void *p)
{
}

joystick_if_t joystick_tm_fcs =
{
        "Thrustmaster Flight Control System",
        tm_fcs_init,
        tm_fcs_close,
        tm_fcs_read,
        tm_fcs_write,
        tm_fcs_read_axis,
        tm_fcs_a0_over,
        1,
        2,
        4,
        1,
        {"X axis", "Y axis"},
        {"Button 1", "Button 2", "Button 3", "Button 4"},
        {"POV"}
};
