/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          SDL2 joystick interface.
 *
 *
 *
 * Authors: Sarah Walker, <https://pcem-emulator.co.uk/>
 *          Joakim L. Gilje <jgilje@jgilje.net>
 *
 *          Copyright 2017-2021 Sarah Walker
 *          Copyright 2021 Joakim L. Gilje
 */
#include <SDL2/SDL.h>

#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define _USE_MATH_DEFINES
#include <math.h>
/* This #undef is needed because a SDL include header redefines HAVE_STDARG_H. */
#undef HAVE_STDARG_H
#define HAVE_STDARG_H
#include <86box/86box.h>
#include <86box/device.h>
#include <86box/gameport.h>
#include <86box/plat_unused.h>

int                  joysticks_present;
joystick_t           joystick_state[MAX_JOYSTICKS];
plat_joystick_t      plat_joystick_state[MAX_PLAT_JOYSTICKS];
static SDL_Joystick *sdl_joy[MAX_PLAT_JOYSTICKS];

#ifndef M_PI
#    define M_PI 3.14159265358979323846
#endif

void
joystick_init(void)
{
    /* This is needed for SDL's Windows raw input backend to work properly without SDL video. */
    SDL_SetHint(SDL_HINT_JOYSTICK_THREAD, "1");

    if (SDL_InitSubSystem(SDL_INIT_JOYSTICK) != 0) {
        return;
    }
    joysticks_present = SDL_NumJoysticks();

    memset(sdl_joy, 0, sizeof(sdl_joy));
    for (int c = 0; c < joysticks_present; c++) {
        sdl_joy[c] = SDL_JoystickOpen(c);

        if (sdl_joy[c]) {
            int d;

            strncpy(plat_joystick_state[c].name, SDL_JoystickNameForIndex(c), 64);
            plat_joystick_state[c].nr_axes    = MIN(SDL_JoystickNumAxes(sdl_joy[c]), MAX_JOY_AXES);
            plat_joystick_state[c].nr_buttons = MIN(SDL_JoystickNumButtons(sdl_joy[c]), MAX_JOY_BUTTONS);
            plat_joystick_state[c].nr_povs    = MIN(SDL_JoystickNumHats(sdl_joy[c]), MAX_JOY_POVS);

            for (d = 0; d < plat_joystick_state[c].nr_axes; d++) {
                snprintf(plat_joystick_state[c].axis[d].name, sizeof(plat_joystick_state[c].axis[d].name), "Axis %i", d);
                plat_joystick_state[c].axis[d].id = d;
            }
            for (d = 0; d < plat_joystick_state[c].nr_buttons; d++) {
                snprintf(plat_joystick_state[c].button[d].name, sizeof(plat_joystick_state[c].button[d].name), "Button %i", d);
                plat_joystick_state[c].button[d].id = d;
            }
            for (d = 0; d < plat_joystick_state[c].nr_povs; d++) {
                snprintf(plat_joystick_state[c].pov[d].name, sizeof(plat_joystick_state[c].pov[d].name), "POV %i", d);
                plat_joystick_state[c].pov[d].id = d;
            }
        }
    }
}

void
joystick_close(void)
{
    int c;

    for (c = 0; c < joysticks_present; c++) {
        if (sdl_joy[c])
            SDL_JoystickClose(sdl_joy[c]);
    }
}

static int
joystick_get_axis(int joystick_nr, int mapping)
{
    if (mapping & POV_X) {
        switch (plat_joystick_state[joystick_nr].p[mapping & 3]) {
            case SDL_HAT_LEFTUP:
            case SDL_HAT_LEFT:
            case SDL_HAT_LEFTDOWN:
                return -32767;

            case SDL_HAT_RIGHTUP:
            case SDL_HAT_RIGHT:
            case SDL_HAT_RIGHTDOWN:
                return 32767;

            default:
                return 0;
        }
    } else if (mapping & POV_Y) {
        switch (plat_joystick_state[joystick_nr].p[mapping & 3]) {
            case SDL_HAT_LEFTUP:
            case SDL_HAT_UP:
            case SDL_HAT_RIGHTUP:
                return -32767;

            case SDL_HAT_LEFTDOWN:
            case SDL_HAT_DOWN:
            case SDL_HAT_RIGHTDOWN:
                return 32767;

            default:
                return 0;
        }
    } else
        return plat_joystick_state[joystick_nr].a[plat_joystick_state[joystick_nr].axis[mapping].id];
}

void
joystick_process(void)
{
    int c;
    int d;

    if (!joystick_type)
        return;

    SDL_JoystickUpdate();
    for (c = 0; c < joysticks_present; c++) {
        int b;

        for (b = 0; b < plat_joystick_state[c].nr_axes; b++)
            plat_joystick_state[c].a[b] = SDL_JoystickGetAxis(sdl_joy[c], b);

        for (b = 0; b < plat_joystick_state[c].nr_buttons; b++)
            plat_joystick_state[c].b[b] = SDL_JoystickGetButton(sdl_joy[c], b);

        for (b = 0; b < plat_joystick_state[c].nr_povs; b++)
            plat_joystick_state[c].p[b] = SDL_JoystickGetHat(sdl_joy[c], b);
        //                pclog("joystick %i - x=%i y=%i b[0]=%i b[1]=%i  %i\n", c, joystick_state[c].x, joystick_state[c].y, joystick_state[c].b[0], joystick_state[c].b[1], joysticks_present);
    }

    for (c = 0; c < joystick_get_max_joysticks(joystick_type); c++) {
        if (joystick_state[c].plat_joystick_nr) {
            int joystick_nr = joystick_state[c].plat_joystick_nr - 1;

            for (d = 0; d < joystick_get_axis_count(joystick_type); d++)
                joystick_state[c].axis[d] = joystick_get_axis(joystick_nr, joystick_state[c].axis_mapping[d]);
            for (d = 0; d < joystick_get_button_count(joystick_type); d++)
                joystick_state[c].button[d] = plat_joystick_state[joystick_nr].b[joystick_state[c].button_mapping[d]];
            for (d = 0; d < joystick_get_pov_count(joystick_type); d++) {
                int    x, y;
                double angle, magnitude;

                x = joystick_get_axis(joystick_nr, joystick_state[c].pov_mapping[d][0]);
                y = joystick_get_axis(joystick_nr, joystick_state[c].pov_mapping[d][1]);

                angle     = (atan2((double) y, (double) x) * 360.0) / (2 * M_PI);
                magnitude = sqrt((double) x * (double) x + (double) y * (double) y);

                if (magnitude < 16384)
                    joystick_state[c].pov[d] = -1;
                else
                    joystick_state[c].pov[d] = ((int) angle + 90 + 360) % 360;
            }
        } else {
            for (d = 0; d < joystick_get_axis_count(joystick_type); d++)
                joystick_state[c].axis[d] = 0;
            for (d = 0; d < joystick_get_button_count(joystick_type); d++)
                joystick_state[c].button[d] = 0;
            for (d = 0; d < joystick_get_pov_count(joystick_type); d++)
                joystick_state[c].pov[d] = -1;
        }
    }
}

#ifdef _WIN32
void
win_joystick_handle(UNUSED(void *raw))
{
    /* Nothing to be done here, atleast currently */
}
#endif
