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
 * Version:	@(#)win_mouse.cc	1.0.1	2017/06/21
 *
 * Authors:	Sarah Walker, <http://pcem-emulator.co.uk/>
 *		Miran Grca, <mgrca8@gmail.com>
 *		Copyright 2008-2017 Sarah Walker.
 *		Copyright 2016-2017 Miran Grca.
 */
#define DIRECTINPUT_VERSION 0x0800
#include <dinput.h>
#include <stdint.h>
#include "plat_mouse.h"
#include "win.h"


extern "C" int video_fullscreen;

extern "C" void fatal(const char *format, ...);
extern "C" void pclog(const char *format, ...);

extern "C" void mouse_init(void);
extern "C" void mouse_close(void);
extern "C" void mouse_poll_host(void);
extern "C" void mouse_get_mickeys(int *x, int *y, int *z);


static LPDIRECTINPUT8 lpdi;
static LPDIRECTINPUTDEVICE8 lpdi_mouse = NULL;
static DIMOUSESTATE mousestate;
static int mouse_x = 0, mouse_y = 0, mouse_z = 0;
int mouse_buttons = 0;


void mouse_init(void)
{
        atexit(mouse_close);
        
        if (FAILED(DirectInput8Create(hinstance, DIRECTINPUT_VERSION, IID_IDirectInput8A, (void **) &lpdi, NULL)))
                fatal("mouse_init : DirectInputCreate failed\n"); 
        if (FAILED(lpdi->CreateDevice(GUID_SysMouse, &lpdi_mouse, NULL)))
           fatal("mouse_init : CreateDevice failed\n");
        if (FAILED(lpdi_mouse->SetCooperativeLevel(ghwnd, DISCL_FOREGROUND | (video_fullscreen ? DISCL_EXCLUSIVE : DISCL_NONEXCLUSIVE))))
           fatal("mouse_init : SetCooperativeLevel failed\n");
        if (FAILED(lpdi_mouse->SetDataFormat(&c_dfDIMouse)))
           fatal("mouse_init : SetDataFormat failed\n");
}


void mouse_close(void)
{
        if (lpdi_mouse)
        {
                lpdi_mouse->Release();
                lpdi_mouse = NULL;
        }
}


void mouse_poll_host(void)
{
        if (FAILED(lpdi_mouse->GetDeviceState(sizeof(DIMOUSESTATE), (LPVOID)&mousestate)))
        {
                lpdi_mouse->Acquire();
                lpdi_mouse->GetDeviceState(sizeof(DIMOUSESTATE), (LPVOID)&mousestate);
        }                
        mouse_buttons = 0;
        if (mousestate.rgbButtons[0] & 0x80)
           mouse_buttons |= 1;
        if (mousestate.rgbButtons[1] & 0x80)
           mouse_buttons |= 2;
        if (mousestate.rgbButtons[2] & 0x80)
           mouse_buttons |= 4;
        mouse_x += mousestate.lX;
        mouse_y += mousestate.lY;        
        mouse_z += mousestate.lZ/120;
        if (!mousecapture && !video_fullscreen)
           mouse_x = mouse_y = mouse_buttons = 0;
}


void mouse_get_mickeys(int *x, int *y, int *z)
{
        *x = mouse_x;
        *y = mouse_y;
        *z = mouse_z;
        mouse_x = mouse_y = mouse_z = 0;
}
