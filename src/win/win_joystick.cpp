/*
 * 86Box	A hypervisor and IBM PC system emulator that specializes in
 *		running old operating systems and software designed for IBM
 *		PC systems and compatibles from 1981 through fairly recent
 *		system designs based on the PCI bus.
 *
 *		This file is part of the 86Box distribution.
 *
 *		Joystick interface to host device.
 *
 * Version:	@(#)win_joystick.cpp	1.0.12	2019/10/23
 *
 * Authors:	Sarah Walker, <http://pcem-emulator.co.uk/>
 *		Miran Grca, <mgrca8@gmail.com>
 *
 *		Copyright 2008-2019 Sarah Walker.
 *		Copyright 2016-2019 Miran Grca.
 */
#define DIRECTINPUT_VERSION 0x0800
#include <dinput.h>
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


plat_joystick_t plat_joystick_state[MAX_PLAT_JOYSTICKS];
joystick_t joystick_state[MAX_JOYSTICKS];
int joysticks_present = 0;


static LPDIRECTINPUT8 lpdi;
static LPDIRECTINPUTDEVICE8 lpdi_joystick[2] = {NULL, NULL};
static GUID joystick_guids[MAX_JOYSTICKS];


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


static BOOL CALLBACK joystick_enum_callback(LPCDIDEVICEINSTANCE lpddi, UNUSED(LPVOID data))
{
        if (joysticks_present >= MAX_JOYSTICKS)
                return DIENUM_STOP;
        
        joystick_log("joystick_enum_callback : found joystick %i : %s\n", joysticks_present, lpddi->tszProductName);
        
        joystick_guids[joysticks_present++] = lpddi->guidInstance;

        if (joysticks_present >= MAX_JOYSTICKS)
                return DIENUM_STOP;
        
        return DIENUM_CONTINUE;
}

BOOL CALLBACK DIEnumDeviceObjectsCallback( 
                      LPCDIDEVICEOBJECTINSTANCE lpddoi,
                      LPVOID pvRef)
{
        plat_joystick_t *state = (plat_joystick_t *)pvRef;
        
        if (lpddoi->guidType == GUID_XAxis  || lpddoi->guidType == GUID_YAxis  || lpddoi->guidType == GUID_ZAxis ||
            lpddoi->guidType == GUID_RxAxis || lpddoi->guidType == GUID_RyAxis || lpddoi->guidType == GUID_RzAxis)
        {
                if (state->nr_axes < 8)
						{memcpy(state->axis[state->nr_axes].name, lpddoi->tszName, strlen(lpddoi->tszName) + 1);
						joystick_log("Axis %i : %s  %x %x\n", state->nr_axes, state->axis[state->nr_axes].name, lpddoi->dwOfs, lpddoi->dwType);
						if (lpddoi->guidType == GUID_XAxis)
								state->axis[state->nr_axes].id = 0;
						else if (lpddoi->guidType == GUID_YAxis)
								state->axis[state->nr_axes].id = 1;
						else if (lpddoi->guidType == GUID_ZAxis)
								state->axis[state->nr_axes].id = 2;
						else if (lpddoi->guidType == GUID_RxAxis)
								state->axis[state->nr_axes].id = 3;
						else if (lpddoi->guidType == GUID_RyAxis)
								state->axis[state->nr_axes].id = 4;
						else if (lpddoi->guidType == GUID_RzAxis)
								state->axis[state->nr_axes].id = 5;
						state->nr_axes++;
				}
        }
        else if (lpddoi->guidType == GUID_Button)
        {
                if (state->nr_buttons < 32)
                {
                        memcpy(state->button[state->nr_buttons].name, lpddoi->tszName, strlen(lpddoi->tszName) + 1);
                        joystick_log("Button %i : %s  %x %x\n", state->nr_buttons, state->button[state->nr_buttons].name, lpddoi->dwOfs, lpddoi->dwType);
                        state->nr_buttons++;
                }
        }
        else if (lpddoi->guidType == GUID_POV)
        {
                if (state->nr_povs < 4)
                {
                        memcpy(state->pov[state->nr_povs].name, lpddoi->tszName, strlen(lpddoi->tszName) + 1);
                        joystick_log("POV %i : %s  %x %x\n", state->nr_povs, state->pov[state->nr_povs].name, lpddoi->dwOfs, lpddoi->dwType);
                        state->nr_povs++;
                }
        }  
		else if (lpddoi->guidType == GUID_Slider)
        {
                if (state->nr_sliders < 2)
                {
                        memcpy(state->slider[state->nr_sliders].name, lpddoi->tszName, strlen(lpddoi->tszName) + 1);
                        state->slider[state->nr_sliders].id = state->nr_sliders | SLIDER;
                        joystick_log("Slider %i : %s  %x %x\n", state->nr_sliders, state->slider[state->nr_sliders].name, lpddoi->dwOfs, lpddoi->dwType);
                        state->nr_sliders++;
                }
        }
        
        return DIENUM_CONTINUE;
}

void joystick_init()
{
        int c;

        atexit(joystick_close);
        
        joysticks_present = 0;
        
        if (FAILED(DirectInput8Create(hinstance, DIRECTINPUT_VERSION, IID_IDirectInput8A, (void **) &lpdi, NULL)))
                fatal("joystick_init : DirectInputCreate failed\n"); 

        if (FAILED(lpdi->EnumDevices(DIDEVTYPE_JOYSTICK, joystick_enum_callback, NULL, DIEDFL_ATTACHEDONLY)))
                fatal("joystick_init : EnumDevices failed\n");

        joystick_log("joystick_init: joysticks_present=%i\n", joysticks_present);
        
        for (c = 0; c < joysticks_present; c++)
        {                
                LPDIRECTINPUTDEVICE8 lpdi_joystick_temp = NULL;
                DIPROPRANGE joy_axis_range;
                DIDEVICEINSTANCE device_instance;
                DIDEVCAPS devcaps;
                
                if (FAILED(lpdi->CreateDevice(joystick_guids[c], &lpdi_joystick_temp, NULL)))
                        fatal("joystick_init : CreateDevice failed\n");
                if (FAILED(lpdi_joystick_temp->QueryInterface(IID_IDirectInputDevice8, (void **)&lpdi_joystick[c])))
                        fatal("joystick_init : CreateDevice failed\n");
                lpdi_joystick_temp->Release();
                
                memset(&device_instance, 0, sizeof(device_instance));
                device_instance.dwSize = sizeof(device_instance);
                if (FAILED(lpdi_joystick[c]->GetDeviceInfo(&device_instance)))
                        fatal("joystick_init : GetDeviceInfo failed\n");
                joystick_log("Joystick %i :\n", c);
                joystick_log(" tszInstanceName = %s\n", device_instance.tszInstanceName);
                joystick_log(" tszProductName = %s\n", device_instance.tszProductName);
                memcpy(plat_joystick_state[c].name, device_instance.tszInstanceName, strlen(device_instance.tszInstanceName) + 1);

                memset(&devcaps, 0, sizeof(devcaps));
                devcaps.dwSize = sizeof(devcaps);
                if (FAILED(lpdi_joystick[c]->GetCapabilities(&devcaps)))
                        fatal("joystick_init : GetCapabilities failed\n");
                joystick_log(" Axes = %i\n", devcaps.dwAxes);
                joystick_log(" Buttons = %i\n", devcaps.dwButtons);
                joystick_log(" POVs = %i\n", devcaps.dwPOVs);

                lpdi_joystick[c]->EnumObjects(DIEnumDeviceObjectsCallback, &plat_joystick_state[c], DIDFT_ALL); 
                
                if (FAILED(lpdi_joystick[c]->SetCooperativeLevel(hwndMain, DISCL_BACKGROUND | DISCL_NONEXCLUSIVE)))
                        fatal("joystick_init : SetCooperativeLevel failed\n");
                if (FAILED(lpdi_joystick[c]->SetDataFormat(&c_dfDIJoystick)))
                        fatal("joystick_init : SetDataFormat failed\n");

                joy_axis_range.lMin = -32768;
                joy_axis_range.lMax =  32767;
                joy_axis_range.diph.dwSize = sizeof(DIPROPRANGE);
                joy_axis_range.diph.dwHeaderSize = sizeof(DIPROPHEADER);
                joy_axis_range.diph.dwHow = DIPH_BYOFFSET;
                joy_axis_range.diph.dwObj = DIJOFS_X;
                lpdi_joystick[c]->SetProperty(DIPROP_RANGE, &joy_axis_range.diph);
                joy_axis_range.diph.dwObj = DIJOFS_Y;
                lpdi_joystick[c]->SetProperty(DIPROP_RANGE, &joy_axis_range.diph);
                joy_axis_range.diph.dwObj = DIJOFS_Z;
                lpdi_joystick[c]->SetProperty(DIPROP_RANGE, &joy_axis_range.diph);
                joy_axis_range.diph.dwObj = DIJOFS_RX;
                lpdi_joystick[c]->SetProperty(DIPROP_RANGE, &joy_axis_range.diph);
                joy_axis_range.diph.dwObj = DIJOFS_RY;
                lpdi_joystick[c]->SetProperty(DIPROP_RANGE, &joy_axis_range.diph);
                joy_axis_range.diph.dwObj = DIJOFS_RZ;
                lpdi_joystick[c]->SetProperty(DIPROP_RANGE, &joy_axis_range.diph);
				joy_axis_range.diph.dwObj = DIJOFS_SLIDER(0);
                lpdi_joystick[c]->SetProperty(DIPROP_RANGE, &joy_axis_range.diph);
                joy_axis_range.diph.dwObj = DIJOFS_SLIDER(1);
                lpdi_joystick[c]->SetProperty(DIPROP_RANGE, &joy_axis_range.diph);
				
                if (FAILED(lpdi_joystick[c]->Acquire()))
                        fatal("joystick_init : Acquire failed\n");
        }
}

void joystick_close()
{
        if (lpdi_joystick[1])
        {
                lpdi_joystick[1]->Release();
                lpdi_joystick[1] = NULL;
        }
        if (lpdi_joystick[0])
        {
                lpdi_joystick[0]->Release();
                lpdi_joystick[0] = NULL;
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
		else if (mapping & SLIDER)
        {
                return plat_joystick_state[joystick_nr].s[mapping & 3];
        }
        else
                return plat_joystick_state[joystick_nr].a[plat_joystick_state[joystick_nr].axis[mapping].id];
}

void joystick_process(void)
{
        int c, d;

	if (joystick_type == JOYSTICK_TYPE_NONE) return;

        for (c = 0; c < joysticks_present; c++)
        {                
                DIJOYSTATE joystate;
                int b;
                
                if (FAILED(lpdi_joystick[c]->Poll()))
                {
                        lpdi_joystick[c]->Acquire();
                        lpdi_joystick[c]->Poll();
                }
                if (FAILED(lpdi_joystick[c]->GetDeviceState(sizeof(DIJOYSTATE), (LPVOID)&joystate)))
                {
                        lpdi_joystick[c]->Acquire();
                        lpdi_joystick[c]->Poll();
                        lpdi_joystick[c]->GetDeviceState(sizeof(DIJOYSTATE), (LPVOID)&joystate);
                }
                
                plat_joystick_state[c].a[0] = joystate.lX;
                plat_joystick_state[c].a[1] = joystate.lY;
                plat_joystick_state[c].a[2] = joystate.lZ;
                plat_joystick_state[c].a[3] = joystate.lRx;
                plat_joystick_state[c].a[4] = joystate.lRy;
                plat_joystick_state[c].a[5] = joystate.lRz;
				plat_joystick_state[c].s[0] = joystate.rglSlider[0];
                plat_joystick_state[c].s[1] = joystate.rglSlider[1];
                
                for (b = 0; b < 16; b++)
                        plat_joystick_state[c].b[b] = joystate.rgbButtons[b] & 0x80;

                for (b = 0; b < 4; b++)
                        plat_joystick_state[c].p[b] = joystate.rgdwPOV[b];
//                joystick_log("joystick %i - x=%i y=%i b[0]=%i b[1]=%i  %i\n", c, joystick_state[c].x, joystick_state[c].y, joystick_state[c].b[0], joystick_state[c].b[1], joysticks_present);
        }
        
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

