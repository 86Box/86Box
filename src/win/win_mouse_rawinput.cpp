/*
 * 86Box	A hypervisor and IBM PC system emulator that specializes in
 *  		running old operating systems and software designed for IBM
 * 			PC systems and compatibles from 1981 through fairly recent
 *  		system designs based on the PCI bus.
 *
 *  		This file is part of the 86Box distribution.
 *
 *  		RawInput mouse interface.
 *
 * 			Version:	@(#)win_mouse_rawinput.cpp	1.0.0	2019/3/19
 *
 *  		Authors:	Sarah Walker, <http://pcem-emulator.co.uk/>
 *  					Miran Grca, <mgrca8@gmail.com>
 *  					GH Cao, <driver1998.ms@outlook.com>
 *
 *  		Copyright 2008-2017 Sarah Walker.
 *  		Copyright 2016,2017 Miran Grca.
 *  		Copyright 2019 GH Cao.
 */
#include <windows.h>
#include <windowsx.h>
#include <stdio.h>
#include <stdint.h>
#include "../86box.h"
#include "../mouse.h"
#include "../plat.h"
#include "win.h"

int mouse_capture;

typedef struct {
	int buttons;
	int dx;
	int dy;
} MOUSESTATE;

MOUSESTATE mousestate;

void
win_mouse_init(void)
{
    atexit(win_mouse_close);

    mouse_capture = 0;

	/* Initialize the RawInput (mouse) module. */
	RAWINPUTDEVICE ridev;
	ridev.dwFlags = 0;
	ridev.hwndTarget = NULL;
	ridev.usUsagePage = 0x01;
	ridev.usUsage = 0x02;
	if (! RegisterRawInputDevices(&ridev, 1, sizeof(ridev)))
		fatal("plat_mouse_init: RegisterRawInputDevices failed\n"); 

	memset(&mousestate, 0, sizeof(MOUSESTATE));
}

void
win_mouse_handle(LPARAM lParam, int infocus)
{
    uint32_t ri_size = 0;
    UINT size;
    RAWINPUT *raw;
 
    if (! infocus) return;

    GetRawInputData((HRAWINPUT)lParam, RID_INPUT, NULL,
		    &size, sizeof(RAWINPUTHEADER));

    raw = (RAWINPUT*)malloc(size);
    if (raw == NULL) return;

    /* Here we read the raw input data for the mouse */
    ri_size = GetRawInputData((HRAWINPUT)(lParam), RID_INPUT,
			      raw, &size, sizeof(RAWINPUTHEADER));
    if (ri_size != size) return;

    /* If the input is mouse, we process it */
    if (raw->header.dwType == RIM_TYPEMOUSE) {
		RAWMOUSE state = raw->data.mouse;

		if (state.usButtonFlags & RI_MOUSE_LEFT_BUTTON_DOWN)
			mousestate.buttons |= 1;
		else if (state.usButtonFlags & RI_MOUSE_LEFT_BUTTON_UP)
			mousestate.buttons &= ~1;
		
		if (state.usButtonFlags & RI_MOUSE_MIDDLE_BUTTON_DOWN)
			mousestate.buttons |= 4;
		else if (state.usButtonFlags & RI_MOUSE_MIDDLE_BUTTON_UP)
			mousestate.buttons &= ~4;

		if (state.usButtonFlags & RI_MOUSE_RIGHT_BUTTON_DOWN)
			mousestate.buttons |= 2;
		else if (state.usButtonFlags & RI_MOUSE_RIGHT_BUTTON_UP)
			mousestate.buttons &= ~2;

		static int x = 0, y = 0;
		if (x != state.lLastX || y != state.lLastY) {
			mousestate.dx += state.lLastX;
			mousestate.dy += state.lLastY;	
			x = state.lLastX;
			y = state.lLastY;
		}
	}
	free(raw);
}

void
win_mouse_close(void)
{
	RAWINPUTDEVICE ridev;
	ridev.dwFlags = RIDEV_REMOVE;
	ridev.hwndTarget = NULL;
	ridev.usUsagePage = 0x01;
	ridev.usUsage = 0x02;
	RegisterRawInputDevices(&ridev, 1, sizeof(ridev));
}

void
mouse_poll(void)
{
    static int b = 0;
    if (mouse_capture || video_fullscreen) {
		if (mousestate.dx != 0 || mousestate.dy != 0) {
			mouse_x += mousestate.dx;
			mouse_y += mousestate.dy;
			mouse_z = 0;

			mousestate.dx=0;
			mousestate.dy=0;
		}

		if (b != mousestate.buttons) {
			mouse_buttons = mousestate.buttons;
			b = mousestate.buttons;
		}
	}
}
