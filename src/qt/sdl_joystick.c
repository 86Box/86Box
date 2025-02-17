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
 * Authors: Sarah Walker, <https://pcem-emulator.co.uk/>
 *          Joakim L. Gilje, <jgilje@jgilje.net>
 *          Jasmine Iwanek, jriwanek@gmail.com>
 *
 *          Copyright 2017-2021 Sarah Walker.
 *          Copyright 2021 Joakim L. Gilje.
 *          Copyright 2021-2025 Jasmine Iwanek.
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

int                  joysticks_present = 0;
joystick_t           joystick_state[GAMEPORT_MAX][MAX_JOYSTICKS];
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
    for (int js = 0; js < joysticks_present; js++) {
        sdl_joy[js] = SDL_JoystickOpen(js);

        if (sdl_joy[js]) {
            strncpy(plat_joystick_state[js].name, SDL_JoystickNameForIndex(js), 64);
            plat_joystick_state[js].nr_axes    = MIN(SDL_JoystickNumAxes(sdl_joy[js]), MAX_JOY_AXES);
            plat_joystick_state[js].nr_buttons = MIN(SDL_JoystickNumButtons(sdl_joy[js]), MAX_JOY_BUTTONS);
            plat_joystick_state[js].nr_povs    = MIN(SDL_JoystickNumHats(sdl_joy[js]), MAX_JOY_POVS);

            for (int axis_nr = 0; axis_nr < plat_joystick_state[js].nr_axes; axis_nr++) {
                snprintf(plat_joystick_state[js].axis[axis_nr].name, sizeof(plat_joystick_state[js].axis[axis_nr].name), "Axis %i", axis_nr);
                plat_joystick_state[js].axis[axis_nr].id = axis_nr;
            }
            for (int button_nr = 0; button_nr < plat_joystick_state[js].nr_buttons; button_nr++) {
                snprintf(plat_joystick_state[js].button[button_nr].name, sizeof(plat_joystick_state[js].button[button_nr].name), "Button %i", button_nr);
                plat_joystick_state[js].button[button_nr].id = button_nr;
            }
            for (int pov_nr = 0; pov_nr < plat_joystick_state[js].nr_povs; pov_nr++) {
                snprintf(plat_joystick_state[js].pov[pov_nr].name, sizeof(plat_joystick_state[js].pov[pov_nr].name), "POV %i", pov_nr);
                plat_joystick_state[js].pov[pov_nr].id = pov_nr;
            }
        }
    }
}

void
joystick_close(void)
{
    for (int js = 0; js < joysticks_present; js++) {
        if (sdl_joy[js])
            SDL_JoystickClose(sdl_joy[js]);
    }
}

static int
joystick_get_axis(int gameport, int joystick_nr, int mapping)
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
    if (!joystick_type)
        return;

    SDL_JoystickUpdate();
    for (int js = 0; js < joysticks_present; js++) {
        for (int axis_nr = 0; axis_nr < plat_joystick_state[js].nr_axes; axis_nr++)
            plat_joystick_state[js].a[axis_nr] = SDL_JoystickGetAxis(sdl_joy[js], axis_nr);

        for (int button_nr = 0; button_nr < plat_joystick_state[js].nr_buttons; button_nr++)
            plat_joystick_state[js].b[button_nr] = SDL_JoystickGetButton(sdl_joy[js], button_nr);

        for (int pov_nr = 0; pov_nr < plat_joystick_state[js].nr_povs; pov_nr++)
            plat_joystick_state[js].p[pov_nr] = SDL_JoystickGetHat(sdl_joy[js], pov_nr);

#if 0
        pclog("joystick %i - x=%i y=%i b[0]=%i b[1]=%i  %i\n", js,
              joystick_state[0][js].x,
              joystick_state[0][js].y,
              joystick_state[0][js].b[0],
              joystick_state[0][js].b[1],
              joysticks_present);
#endif
    }

    for (int js = 0; js < joystick_get_max_joysticks(joystick_type); js++) {
        if (joystick_state[0][js].plat_joystick_nr) {
            int joystick_nr = joystick_state[0][js].plat_joystick_nr - 1;

            for (int axis_nr = 0; axis_nr < joystick_get_axis_count(joystick_type); axis_nr++)
                joystick_state[0][js].axis[axis_nr] = joystick_get_axis(0, joystick_nr, joystick_state[0][js].axis_mapping[axis_nr]);

            for (int button_nr = 0; button_nr < joystick_get_button_count(joystick_type); button_nr++)
                joystick_state[0][js].button[button_nr] = plat_joystick_state[joystick_nr].b[joystick_state[0][js].button_mapping[button_nr]];

            for (int pov_nr = 0; pov_nr < joystick_get_pov_count(joystick_type); pov_nr++) {
                int    x         = joystick_get_axis(0, joystick_nr, joystick_state[0][js].pov_mapping[pov_nr][0]);
                int    y         = joystick_get_axis(0, joystick_nr, joystick_state[0][js].pov_mapping[pov_nr][1]);
                double angle     = (atan2((double) y, (double) x) * 360.0) / (2 * M_PI);
                double magnitude = sqrt((double) x * (double) x + (double) y * (double) y);

                if (magnitude < 16384)
                    joystick_state[0][js].pov[pov_nr] = -1;
                else
                    joystick_state[0][js].pov[pov_nr] = ((int) angle + 90 + 360) % 360;
            }
        } else {
            for (int axis_nr = 0; axis_nr < joystick_get_axis_count(joystick_type); axis_nr++)
                joystick_state[0][js].axis[axis_nr] = 0;

            for (int button_nr = 0; button_nr < joystick_get_button_count(joystick_type); button_nr++)
                joystick_state[0][js].button[button_nr] = 0;

            for (int pov_nr = 0; pov_nr < joystick_get_pov_count(joystick_type); pov_nr++)
                joystick_state[0][js].pov[pov_nr] = -1;
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
