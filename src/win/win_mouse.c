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
#include <86box/86box.h>
#include <86box/mouse.h>
#include <86box/plat.h>
#include <86box/win.h>

int mouse_capture;

typedef struct {
	int buttons;
	int dx;
	int dy;
	int dwheel;
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
win_mouse_handle(PRAWINPUT raw)
{
    RAWMOUSE state = raw->data.mouse;
    static int x, y;

	/* read mouse buttons and wheel */
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

	if (state.usButtonFlags & RI_MOUSE_WHEEL) {
		mousestate.dwheel += (SHORT)state.usButtonData / 120;
	}


    if (state.usFlags & MOUSE_MOVE_ABSOLUTE) {
		/* absolute mouse, i.e. RDP or VNC
		 * seems to work fine for RDP on Windows 10
		 * Not sure about other environments.
		 */
		mousestate.dx += (state.lLastX - x)/25;
		mousestate.dy += (state.lLastY - y)/25;
		x=state.lLastX;
		y=state.lLastY;
	} else {
		/* relative mouse, i.e. regular mouse */
		mousestate.dx += state.lLastX;
		mousestate.dy += state.lLastY;
	}
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
		if (mousestate.dx != 0 || mousestate.dy != 0 || mousestate.dwheel != 0) {
			mouse_x += mousestate.dx;
			mouse_y += mousestate.dy;
			mouse_z = mousestate.dwheel;

			mousestate.dx=0;
			mousestate.dy=0;
			mousestate.dwheel=0;

			//pclog("dx=%d, dy=%d, dwheel=%d\n", mouse_x, mouse_y, mouse_z);
		}

		if (b != mousestate.buttons) {
			mouse_buttons = mousestate.buttons;
			b = mousestate.buttons;
		}
	}
}
