#define DIRECTINPUT_VERSION 0x0800
#include <dinput.h>
#include "plat-mouse.h"
#include "win.h"

extern "C" int video_fullscreen;

extern "C" void fatal(const char *format, ...);
extern "C" void pclog(const char *format, ...);

extern "C" void mouse_init();
extern "C" void mouse_close();
extern "C" void mouse_poll_host();
extern "C" void mouse_get_mickeys(int *x, int *y);

static LPDIRECTINPUT8 lpdi;
static LPDIRECTINPUTDEVICE8 lpdi_mouse = NULL;
static DIMOUSESTATE mousestate;
static int mouse_x = 0, mouse_y = 0;
int mouse_buttons = 0;

void mouse_init()
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

void mouse_close()
{
        if (lpdi_mouse)
        {
                lpdi_mouse->Release();
                lpdi_mouse = NULL;
        }
}

void mouse_poll_host()
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
        if (!mousecapture && !video_fullscreen)
           mouse_x = mouse_y = mouse_buttons = 0;
}

void mouse_get_mickeys(int *x, int *y)
{
        *x = mouse_x;
        *y = mouse_y;
        mouse_x = mouse_y = 0;
}
