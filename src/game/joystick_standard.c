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


static void *joystick_standard_init()
{
	return NULL;
}

static void joystick_standard_close(void *p)
{
}

static uint8_t joystick_standard_read(void *p)
{
        uint8_t ret = 0xf0;
        
        if (JOYSTICK_PRESENT(0))
        {
                if (joystick_state[0].button[0])
                        ret &= ~0x10;
                if (joystick_state[0].button[1])
                        ret &= ~0x20;
        }
        if (JOYSTICK_PRESENT(1))
        {
                if (joystick_state[1].button[0])
                        ret &= ~0x40;
                if (joystick_state[1].button[1])
                        ret &= ~0x80;
        }
        
        return ret;
}

static uint8_t joystick_standard_read_4button(void *p)
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

static void joystick_standard_write(void *p)
{
}

static int joystick_standard_read_axis(void *p, int axis)
{
        switch (axis)
        {
                case 0:
                if (!JOYSTICK_PRESENT(0))
                        return AXIS_NOT_PRESENT;
                return joystick_state[0].axis[0];
                case 1:
                if (!JOYSTICK_PRESENT(0))
                        return AXIS_NOT_PRESENT;
                return joystick_state[0].axis[1];
                case 2:
                if (!JOYSTICK_PRESENT(1))
                        return AXIS_NOT_PRESENT;
                return joystick_state[1].axis[0];
                case 3:
                if (!JOYSTICK_PRESENT(1))
                        return AXIS_NOT_PRESENT;
                return joystick_state[1].axis[1];
		default:
		return 0;
        }
}

static int joystick_standard_read_axis_4button(void *p, int axis)
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
                return 0;
		default:
		return 0;
        }
}
static int joystick_standard_read_axis_6button(void *p, int axis)
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
                return joystick_state[0].button[4] ? -32767 : 32768;
                case 3:
                return joystick_state[0].button[5] ? -32767 : 32768;
		default:
		return 0;
        }
}
static int joystick_standard_read_axis_8button(void *p, int axis)
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
                if (joystick_state[0].button[4])
                        return -32767;
                if (joystick_state[0].button[6])                        
                        return 32768;
                return 0;
                case 3:
                if (joystick_state[0].button[5])
                        return -32767;
                if (joystick_state[0].button[7])                        
                        return 32768;
                return 0;
		default:
		return 0;
        }
}

static void joystick_standard_a0_over(void *p)
{
}

joystick_if_t joystick_standard =
{
        "Standard 2-button joystick(s)",
        joystick_standard_init,
        joystick_standard_close,
        joystick_standard_read,
        joystick_standard_write,
        joystick_standard_read_axis,
        joystick_standard_a0_over,
        2,
        2,
        0,
        2,
        {"X axis", "Y axis"},
        {"Button 1", "Button 2"}
};
joystick_if_t joystick_standard_4button =
{
        "Standard 4-button joystick",
        joystick_standard_init,
        joystick_standard_close,
        joystick_standard_read_4button,
        joystick_standard_write,
        joystick_standard_read_axis_4button,
        joystick_standard_a0_over,
        2,
        4,
        0,
        1,
        {"X axis", "Y axis"},
        {"Button 1", "Button 2", "Button 3", "Button 4"}
};
joystick_if_t joystick_standard_6button =
{
        "Standard 6-button joystick",
        joystick_standard_init,
        joystick_standard_close,
        joystick_standard_read_4button,
        joystick_standard_write,
        joystick_standard_read_axis_6button,
        joystick_standard_a0_over,
        2,
        6,
        0,
        1,
        {"X axis", "Y axis"},
        {"Button 1", "Button 2", "Button 3", "Button 4", "Button 5", "Button 6"}
};
joystick_if_t joystick_standard_8button =
{
        "Standard 8-button joystick",
        joystick_standard_init,
        joystick_standard_close,
        joystick_standard_read_4button,
        joystick_standard_write,
        joystick_standard_read_axis_8button,
        joystick_standard_a0_over,
        2,
        8,
        0,
        1,
        {"X axis", "Y axis"},
        {"Button 1", "Button 2", "Button 3", "Button 4", "Button 5", "Button 6", "Button 7", "Button 8"}
};
