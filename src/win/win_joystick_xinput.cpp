/*
 * 86Box	A hypervisor and IBM PC system emulator that specializes in
 *		running old operating systems and software designed for IBM
 *		PC systems and compatibles from 1981 through fairly recent
 *		system designs based on the PCI bus.
 *
 *		This file is part of the 86Box distribution.
 *
 *		Xinput joystick interface.
 *
 * Version:	@(#)win_joystick_xinput.cpp	1.0.0	2019/3/19
 *
 * Authors:	Sarah Walker, <http://pcem-emulator.co.uk/>
 *		Miran Grca, <mgrca8@gmail.com>
 *              GH Cao, <driver1998.ms@outlook.com>
 *
 *		Copyright 2008-2018 Sarah Walker.
 *		Copyright 2016-2018 Miran Grca.
 *		Copyright 2019 GH Cao.
 */
#include <Xinput.h>
#include <math.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#define HAVE_STDARG_H
#include "../86box.h"
#include "../device.h"
#include "../plat.h"
#include "../game/gameport.h"
#include "win.h"

#define XINPUT_MAX_JOYSTICKS 4
#define XINPUT_NAME "Xinput compatiable controller"
#define XINPUT_NAME_LX "Left Stick X"
#define XINPUT_NAME_LY "Left Stick Y"
#define XINPUT_NAME_RX "Right Stick X"
#define XINPUT_NAME_RY "Right Stick Y"
#define XINPUT_NAME_DPAD_X "D-pad X"
#define XINPUT_NAME_DPAD_Y "D-pad Y"
#define XINPUT_NAME_LB "LB"
#define XINPUT_NAME_RB "RB"
#define XINPUT_NAME_LT "LT"
#define XINPUT_NAME_RT "RT"
#define XINPUT_NAME_A "A"
#define XINPUT_NAME_B "B"
#define XINPUT_NAME_X "X"
#define XINPUT_NAME_Y "Y"
#define XINPUT_NAME_BACK "Back/View"
#define XINPUT_NAME_START "Start/Menu"
#define XINPUT_NAME_LS "Left Stick"
#define XINPUT_NAME_RS "Right Stick"

#ifdef ENABLE_JOYSTICK_LOG
int joystick_do_log = ENABLE_JOYSTICK_LOG;


static void
joystick_log(const char *fmt, ...)
{
    va_list ap;

    if (joystick_do_log) {
	va_start(ap, fmt);
	pclog_ex(fmt, ap);
	va_end(ap);
    }
}
#else
#define joystick_log(fmt, ...)
#endif

plat_joystick_t plat_joystick_state[MAX_PLAT_JOYSTICKS];
joystick_t joystick_state[MAX_JOYSTICKS];
int joysticks_present = 0;

XINPUT_STATE controllers[XINPUT_MAX_JOYSTICKS];

void joystick_init()
{
        int c;

        atexit(joystick_close);
        
        joysticks_present = 0;
        
        memset(controllers, 0, sizeof(XINPUT_STATE) * XINPUT_MAX_JOYSTICKS);

        for (c=0; c<XINPUT_MAX_JOYSTICKS; c++) {
                int value = XInputGetState(c, &controllers[c]);
                if (value != ERROR_SUCCESS) continue;
                memcpy(plat_joystick_state[c].name, XINPUT_NAME, sizeof(XINPUT_NAME));

                plat_joystick_state[c].nr_axes = 8;

                /* analog stick */
                memcpy(plat_joystick_state[c].axis[0].name, XINPUT_NAME_LX, sizeof(XINPUT_NAME_LX));
                plat_joystick_state[c].axis[0].id = 0;  /* X axis */
                memcpy(plat_joystick_state[c].axis[1].name, XINPUT_NAME_LY, sizeof(XINPUT_NAME_LY));
                plat_joystick_state[c].axis[1].id = 1;  /* Y axis */
                memcpy(plat_joystick_state[c].axis[2].name, XINPUT_NAME_RX, sizeof(XINPUT_NAME_RX));
                plat_joystick_state[c].axis[2].id = 3;  /* RX axis */
                memcpy(plat_joystick_state[c].axis[3].name, XINPUT_NAME_RY, sizeof(XINPUT_NAME_RY));
                plat_joystick_state[c].axis[3].id = 4;  /* RY axis */

                /* d-pad, assigned to Z and RZ */
                memcpy(plat_joystick_state[c].axis[4].name, XINPUT_NAME_DPAD_X, sizeof(XINPUT_NAME_DPAD_X));
                plat_joystick_state[c].axis[4].id = 2;
                memcpy(plat_joystick_state[c].axis[5].name, XINPUT_NAME_DPAD_Y, sizeof(XINPUT_NAME_DPAD_Y));
                plat_joystick_state[c].axis[5].id = 5;

                /* Analog trigger */
                memcpy(plat_joystick_state[c].axis[6].name, XINPUT_NAME_LT, sizeof(XINPUT_NAME_LT));
                plat_joystick_state[c].axis[6].id = 6;
                memcpy(plat_joystick_state[c].axis[7].name, XINPUT_NAME_RT, sizeof(XINPUT_NAME_RT));
                plat_joystick_state[c].axis[7].id = 7;

                plat_joystick_state[c].nr_buttons = 12;
                memcpy(plat_joystick_state[c].button[0].name, XINPUT_NAME_A, sizeof(XINPUT_NAME_A));
                memcpy(plat_joystick_state[c].button[1].name, XINPUT_NAME_B, sizeof(XINPUT_NAME_B));
                memcpy(plat_joystick_state[c].button[2].name, XINPUT_NAME_X, sizeof(XINPUT_NAME_X));
                memcpy(plat_joystick_state[c].button[3].name, XINPUT_NAME_Y, sizeof(XINPUT_NAME_Y));
                memcpy(plat_joystick_state[c].button[4].name, XINPUT_NAME_LB, sizeof(XINPUT_NAME_LB));
                memcpy(plat_joystick_state[c].button[5].name, XINPUT_NAME_RB, sizeof(XINPUT_NAME_RB));
                memcpy(plat_joystick_state[c].button[6].name, XINPUT_NAME_LT, sizeof(XINPUT_NAME_LT));
                memcpy(plat_joystick_state[c].button[7].name, XINPUT_NAME_RT, sizeof(XINPUT_NAME_RT));
                memcpy(plat_joystick_state[c].button[8].name, XINPUT_NAME_BACK, sizeof(XINPUT_NAME_BACK));
                memcpy(plat_joystick_state[c].button[9].name, XINPUT_NAME_START, sizeof(XINPUT_NAME_START));
                memcpy(plat_joystick_state[c].button[10].name, XINPUT_NAME_LS, sizeof(XINPUT_NAME_LS));
                memcpy(plat_joystick_state[c].button[11].name, XINPUT_NAME_RS, sizeof(XINPUT_NAME_RS));

                plat_joystick_state[c].nr_povs = 0;

                joysticks_present++;
        }
        joystick_log("joystick_init: joysticks_present=%i\n", joysticks_present);
}

void joystick_close()
{
}

void joystick_poll(void)
{
        for (int c=0; c<joysticks_present; c++) {
                int value = XInputGetState(c, &controllers[c]);
                if (value != ERROR_SUCCESS) continue;
                        
                plat_joystick_state[c].a[0] = controllers[c].Gamepad.sThumbLX;
                plat_joystick_state[c].a[1] = - controllers[c].Gamepad.sThumbLY;
                plat_joystick_state[c].a[3] = controllers[c].Gamepad.sThumbRX;
                plat_joystick_state[c].a[4] = - controllers[c].Gamepad.sThumbRY;
                plat_joystick_state[c].a[6] = (double)controllers[c].Gamepad.bLeftTrigger / 255 * 32767;
                plat_joystick_state[c].a[7] = (double)controllers[c].Gamepad.bRightTrigger / 255 * 32767;

                plat_joystick_state[c].b[0] = (controllers[c].Gamepad.wButtons & XINPUT_GAMEPAD_A) ? 128 : 0;
                plat_joystick_state[c].b[1] = (controllers[c].Gamepad.wButtons & XINPUT_GAMEPAD_B) ? 128 : 0;
                plat_joystick_state[c].b[2] = (controllers[c].Gamepad.wButtons & XINPUT_GAMEPAD_X) ? 128 : 0;
                plat_joystick_state[c].b[3] = (controllers[c].Gamepad.wButtons & XINPUT_GAMEPAD_Y) ? 128 : 0;
                plat_joystick_state[c].b[4] = (controllers[c].Gamepad.wButtons & XINPUT_GAMEPAD_LEFT_SHOULDER) ? 128 : 0;
                plat_joystick_state[c].b[5] = (controllers[c].Gamepad.wButtons & XINPUT_GAMEPAD_RIGHT_SHOULDER) ? 128 : 0;
                plat_joystick_state[c].b[6] = (controllers[c].Gamepad.bLeftTrigger > 127) ? 128 : 0;
                plat_joystick_state[c].b[7] = (controllers[c].Gamepad.bRightTrigger > 127) ? 128 : 0;
                plat_joystick_state[c].b[8] = (controllers[c].Gamepad.wButtons & XINPUT_GAMEPAD_BACK) ? 128 : 0;
                plat_joystick_state[c].b[9] = (controllers[c].Gamepad.wButtons & XINPUT_GAMEPAD_START) ? 128 : 0;
                plat_joystick_state[c].b[10] = (controllers[c].Gamepad.wButtons & XINPUT_GAMEPAD_LEFT_THUMB) ? 128 : 0;
                plat_joystick_state[c].b[11] = (controllers[c].Gamepad.wButtons & XINPUT_GAMEPAD_RIGHT_THUMB) ? 128 : 0;
                
                int dpad_x = 0, dpad_y = 0;
                if (controllers[c].Gamepad.wButtons & XINPUT_GAMEPAD_DPAD_UP)
                        dpad_y-=32767;
                if (controllers[c].Gamepad.wButtons & XINPUT_GAMEPAD_DPAD_DOWN)
                        dpad_y+=32767;
                if (controllers[c].Gamepad.wButtons & XINPUT_GAMEPAD_DPAD_LEFT)
                        dpad_x-=32767;
                if (controllers[c].Gamepad.wButtons & XINPUT_GAMEPAD_DPAD_RIGHT)
                        dpad_x+=32767;

                plat_joystick_state[c].a[2] = dpad_x;
                plat_joystick_state[c].a[5] = dpad_y;

                for (int a=0; a<8; a++) {
                        if (plat_joystick_state[c].a[a] == -32768)
                                plat_joystick_state[c].a[a] = -32767;
                        if (plat_joystick_state[c].a[a] == 32768)
                                plat_joystick_state[c].a[a] = 32767;
                }
        }
}

static int joystick_get_axis(int joystick_nr, int mapping)
{
        if (mapping & POV_X)
        {
                int pov = plat_joystick_state[joystick_nr].p[mapping & 3];

                if (LOWORD(pov) == 0xFFFF)
                        return 0;
                else
                        return sin((2*M_PI * (double)pov) / 36000.0) * 32767;
        }
        else if (mapping & POV_Y)
        {
                int pov = plat_joystick_state[joystick_nr].p[mapping & 3];

                if (LOWORD(pov) == 0xFFFF)
                        return 0;
                else
                        return -cos((2*M_PI * (double)pov) / 36000.0) * 32767;
        }
        else
                return plat_joystick_state[joystick_nr].a[plat_joystick_state[joystick_nr].axis[mapping].id];
}

void joystick_process(void)
{
        int c, d;

	if (joystick_type == JOYSTICK_TYPE_NONE) return;

        joystick_poll();

        for (c = 0; c < joystick_get_max_joysticks(joystick_type); c++)
        {
                if (joystick_state[c].plat_joystick_nr)
                {
                        int joystick_nr = joystick_state[c].plat_joystick_nr - 1;
                        
                        for (d = 0; d < joystick_get_axis_count(joystick_type); d++)
                                joystick_state[c].axis[d] = joystick_get_axis(joystick_nr, joystick_state[c].axis_mapping[d]);
                        for (d = 0; d < joystick_get_button_count(joystick_type); d++)
                                joystick_state[c].button[d] = plat_joystick_state[joystick_nr].b[joystick_state[c].button_mapping[d]];

                        for (d = 0; d < joystick_get_pov_count(joystick_type); d++)
                        {
                                int x, y;
                                double angle, magnitude;

                                x = joystick_get_axis(joystick_nr, joystick_state[c].pov_mapping[d][0]);
                                y = joystick_get_axis(joystick_nr, joystick_state[c].pov_mapping[d][1]);
                                
                                angle = (atan2((double)y, (double)x) * 360.0) / (2*M_PI);
                                magnitude = sqrt((double)x*(double)x + (double)y*(double)y);
                                
                                if (magnitude < 16384)
                                        joystick_state[c].pov[d] = -1;
                                else
                                        joystick_state[c].pov[d] = ((int)angle + 90 + 360) % 360;
                        }
                }
                else
                {
                        for (d = 0; d < joystick_get_axis_count(joystick_type); d++)
                                joystick_state[c].axis[d] = 0;
                        for (d = 0; d < joystick_get_button_count(joystick_type); d++)
                                joystick_state[c].button[d] = 0;
                        for (d = 0; d < joystick_get_pov_count(joystick_type); d++)
                                joystick_state[c].pov[d] = -1;
                }
        }
}

