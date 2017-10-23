/*
 * 86Box	A hypervisor and IBM PC system emulator that specializes in
 *		running old operating systems and software designed for IBM
 *		PC systems and compatibles from 1981 through fairly recent
 *		system designs based on the PCI bus.
 *
 *		This file is part of the 86Box distribution.
 *
 *		Mouse interface to host device.
 *
 * Version:	@(#)win_mouse.cc	1.0.5	2017/10/22
 *
 * Authors:	Sarah Walker, <http://pcem-emulator.co.uk/>
 *		Miran Grca, <mgrca8@gmail.com>
 *
 *		Copyright 2008-2017 Sarah Walker.
 *		Copyright 2016,2017 Miran Grca.
 */
#define DIRECTINPUT_VERSION 0x0800
#include <dinput.h>
#include <stdio.h>
#include <stdint.h>
#include "../86Box.h"
#include "../mouse.h"
#include "../plat.h"
#include "../plat_mouse.h"
#include "win.h"


int mouse_capture;


static LPDIRECTINPUT8 lpdi;
static LPDIRECTINPUTDEVICE8 lpdi_mouse = NULL;
static DIMOUSESTATE mousestate;


void
mouse_init(void)
{
    atexit(mouse_close);
        
    mouse_capture = 0;

    if (FAILED(DirectInput8Create(hinstance, DIRECTINPUT_VERSION,
	       IID_IDirectInput8A, (void **) &lpdi, NULL)))
	fatal("mouse_init : DirectInputCreate failed\n"); 

    if (FAILED(lpdi->CreateDevice(GUID_SysMouse, &lpdi_mouse, NULL)))
	fatal("mouse_init : CreateDevice failed\n");

    if (FAILED(lpdi_mouse->SetCooperativeLevel(hwndMain,
	       DISCL_FOREGROUND | (video_fullscreen ? DISCL_EXCLUSIVE : DISCL_NONEXCLUSIVE))))
	fatal("mouse_init : SetCooperativeLevel failed\n");

    if (FAILED(lpdi_mouse->SetDataFormat(&c_dfDIMouse)))
	fatal("mouse_init : SetDataFormat failed\n");
}


void
mouse_close(void)
{
    if (lpdi_mouse) {
	lpdi_mouse->Release();
	lpdi_mouse = NULL;
    }
}


void
mouse_poll_host(void)
{
    static int buttons = 0;
    static int x = 0, y = 0, z = 0;
    int b;

    if (FAILED(lpdi_mouse->GetDeviceState(sizeof(DIMOUSESTATE),
				(LPVOID)&mousestate))) {
	lpdi_mouse->Acquire();
	lpdi_mouse->GetDeviceState(sizeof(DIMOUSESTATE), (LPVOID)&mousestate);
    }                

    if (mouse_capture || video_fullscreen) {
	if (x != mousestate.lX || y != mousestate.lY || z != mousestate.lZ) {
		mouse_x += mousestate.lX;
		mouse_y += mousestate.lY;
		mouse_z += mousestate.lZ/120;

		x = mousestate.lX;
		y = mousestate.lY;        
		z = mousestate.lZ/120;
	}

	b = 0;
	if (mousestate.rgbButtons[0] & 0x80) b |= 1;
	if (mousestate.rgbButtons[1] & 0x80) b |= 2;
	if (mousestate.rgbButtons[2] & 0x80) b |= 4;
	if (buttons != b) {
		mouse_buttons = b;
		buttons = b;
	}
    }
}
