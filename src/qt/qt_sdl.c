/*
 * 86Box	A hypervisor and IBM PC system emulator that specializes in
 *		running old operating systems and software designed for IBM
 *		PC systems and compatibles from 1981 through fairly recent
 *		system designs based on the PCI bus.
 *
 *		This file is part of the 86Box distribution.
 *
 *		Rendering module for libSDL2
 *
 * NOTE:	Given all the problems reported with FULLSCREEN use of SDL,
 *		we will not use that, but, instead, use a new window which
 *		coverrs the entire desktop.
 *
 *
 *
 * Authors:	Fred N. van Kempen, <decwiz@yahoo.com>
 *		Michael Drüing, <michael@drueing.de>
 *
 *		Copyright 2018-2020 Fred N. van Kempen.
 *		Copyright 2018-2020 Michael Drüing.
 *
 *		Redistribution and  use  in source  and binary forms, with
 *		or  without modification, are permitted  provided that the
 *		following conditions are met:
 *
 *		1. Redistributions of  source  code must retain the entire
 *		   above notice, this list of conditions and the following
 *		   disclaimer.
 *
 *		2. Redistributions in binary form must reproduce the above
 *		   copyright  notice,  this list  of  conditions  and  the
 *		   following disclaimer in  the documentation and/or other
 *		   materials provided with the distribution.
 *
 *		3. Neither the  name of the copyright holder nor the names
 *		   of  its  contributors may be used to endorse or promote
 *		   products  derived from  this  software without specific
 *		   prior written permission.
 *
 * THIS SOFTWARE  IS  PROVIDED BY THE  COPYRIGHT  HOLDERS AND CONTRIBUTORS
 * "AS IS" AND  ANY EXPRESS  OR  IMPLIED  WARRANTIES,  INCLUDING, BUT  NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A
 * PARTICULAR PURPOSE  ARE  DISCLAIMED. IN  NO  EVENT  SHALL THE COPYRIGHT
 * HOLDER OR  CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL,  EXEMPLARY,  OR  CONSEQUENTIAL  DAMAGES  (INCLUDING,  BUT  NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE  GOODS OR SERVICES;  LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED  AND ON  ANY
 * THEORY OF  LIABILITY, WHETHER IN  CONTRACT, STRICT  LIABILITY, OR  TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING  IN ANY  WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */
#include <SDL2/SDL.h>

#include <stdarg.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
/* This #undef is needed because a SDL include header redefines HAVE_STDARG_H. */
#undef HAVE_STDARG_H
#define HAVE_STDARG_H
#include <86box/86box.h>
#include <86box/mouse.h>
#include <86box/keyboard.h>
#include <86box/device.h>
#include <86box/plat.h>
#include <86box/plat_dynld.h>
#include <86box/video.h>
#include <86box/ui.h>
#include <86box/version.h>

#include "qt_sdl.h"

#define RENDERER_FULL_SCREEN	1
#define RENDERER_HARDWARE	2
#define RENDERER_OPENGL		4


static SDL_Window	*sdl_win = NULL;
static SDL_Renderer	*sdl_render = NULL;
static SDL_Texture	*sdl_tex = NULL;
static int		sdl_w, sdl_h;
static int		sdl_fs, sdl_flags = -1;
static int		cur_w, cur_h;
static int		cur_ww = 0, cur_wh = 0;
static volatile int	sdl_enabled = 0;
static SDL_mutex*	sdl_mutex = NULL;

static const uint16_t sdl_to_xt[0x200] =
    {
        [SDL_SCANCODE_ESCAPE] = 0x01,
        [SDL_SCANCODE_1] = 0x02,
        [SDL_SCANCODE_2] = 0x03,
        [SDL_SCANCODE_3] = 0x04,
        [SDL_SCANCODE_4] = 0x05,
        [SDL_SCANCODE_5] = 0x06,
        [SDL_SCANCODE_6] = 0x07,
        [SDL_SCANCODE_7] = 0x08,
        [SDL_SCANCODE_8] = 0x09,
        [SDL_SCANCODE_9] = 0x0A,
        [SDL_SCANCODE_0] = 0x0B,
        [SDL_SCANCODE_MINUS] = 0x0C,
        [SDL_SCANCODE_EQUALS] = 0x0D,
        [SDL_SCANCODE_BACKSPACE] = 0x0E,
        [SDL_SCANCODE_TAB] = 0x0F,
        [SDL_SCANCODE_Q] = 0x10,
        [SDL_SCANCODE_W] = 0x11,
        [SDL_SCANCODE_E] = 0x12,
        [SDL_SCANCODE_R] = 0x13,
        [SDL_SCANCODE_T] = 0x14,
        [SDL_SCANCODE_Y] = 0x15,
        [SDL_SCANCODE_U] = 0x16,
        [SDL_SCANCODE_I] = 0x17,
        [SDL_SCANCODE_O] = 0x18,
        [SDL_SCANCODE_P] = 0x19,
        [SDL_SCANCODE_LEFTBRACKET] = 0x1A,
        [SDL_SCANCODE_RIGHTBRACKET] = 0x1B,
        [SDL_SCANCODE_RETURN] = 0x1C,
        [SDL_SCANCODE_LCTRL] = 0x1D,
        [SDL_SCANCODE_A] = 0x1E,
        [SDL_SCANCODE_S] = 0x1F,
        [SDL_SCANCODE_D] = 0x20,
        [SDL_SCANCODE_F] = 0x21,
        [SDL_SCANCODE_G] = 0x22,
        [SDL_SCANCODE_H] = 0x23,
        [SDL_SCANCODE_J] = 0x24,
        [SDL_SCANCODE_K] = 0x25,
        [SDL_SCANCODE_L] = 0x26,
        [SDL_SCANCODE_SEMICOLON] = 0x27,
        [SDL_SCANCODE_APOSTROPHE] = 0x28,
        [SDL_SCANCODE_GRAVE] = 0x29,
        [SDL_SCANCODE_LSHIFT] = 0x2A,
        [SDL_SCANCODE_BACKSLASH] = 0x2B,
        [SDL_SCANCODE_Z] = 0x2C,
        [SDL_SCANCODE_X] = 0x2D,
        [SDL_SCANCODE_C] = 0x2E,
        [SDL_SCANCODE_V] = 0x2F,
        [SDL_SCANCODE_B] = 0x30,
        [SDL_SCANCODE_N] = 0x31,
        [SDL_SCANCODE_M] = 0x32,
        [SDL_SCANCODE_COMMA] = 0x33,
        [SDL_SCANCODE_PERIOD] = 0x34,
        [SDL_SCANCODE_SLASH] = 0x35,
        [SDL_SCANCODE_RSHIFT] = 0x36,
        [SDL_SCANCODE_KP_MULTIPLY] = 0x37,
        [SDL_SCANCODE_LALT] = 0x38,
        [SDL_SCANCODE_SPACE] = 0x39,
        [SDL_SCANCODE_CAPSLOCK] = 0x3A,
        [SDL_SCANCODE_F1] = 0x3B,
        [SDL_SCANCODE_F2] = 0x3C,
        [SDL_SCANCODE_F3] = 0x3D,
        [SDL_SCANCODE_F4] = 0x3E,
        [SDL_SCANCODE_F5] = 0x3F,
        [SDL_SCANCODE_F6] = 0x40,
        [SDL_SCANCODE_F7] = 0x41,
        [SDL_SCANCODE_F8] = 0x42,
        [SDL_SCANCODE_F9] = 0x43,
        [SDL_SCANCODE_F10] = 0x44,
        [SDL_SCANCODE_NUMLOCKCLEAR] = 0x45,
        [SDL_SCANCODE_SCROLLLOCK] = 0x46,
        [SDL_SCANCODE_HOME] = 0x147,
        [SDL_SCANCODE_UP] = 0x148,
        [SDL_SCANCODE_PAGEUP] = 0x149,
        [SDL_SCANCODE_KP_MINUS] = 0x4A,
        [SDL_SCANCODE_LEFT] = 0x14B,
        [SDL_SCANCODE_KP_5] = 0x4C,
        [SDL_SCANCODE_RIGHT] = 0x14D,
        [SDL_SCANCODE_KP_PLUS] = 0x4E,
        [SDL_SCANCODE_END] = 0x14F,
        [SDL_SCANCODE_DOWN] = 0x150,
        [SDL_SCANCODE_PAGEDOWN] = 0x151,
        [SDL_SCANCODE_INSERT] = 0x152,
        [SDL_SCANCODE_DELETE] = 0x153,
        [SDL_SCANCODE_F11] = 0x57,
        [SDL_SCANCODE_F12] = 0x58,

        [SDL_SCANCODE_KP_ENTER] = 0x11c,
        [SDL_SCANCODE_RCTRL] = 0x11d,
        [SDL_SCANCODE_KP_DIVIDE] = 0x135,
        [SDL_SCANCODE_RALT] = 0x138,
        [SDL_SCANCODE_KP_9] = 0x49,
        [SDL_SCANCODE_KP_8] = 0x48,
        [SDL_SCANCODE_KP_7] = 0x47,
        [SDL_SCANCODE_KP_6] = 0x4D,
        [SDL_SCANCODE_KP_4] = 0x4B,
        [SDL_SCANCODE_KP_3] = 0x51,
        [SDL_SCANCODE_KP_2] = 0x50,
        [SDL_SCANCODE_KP_1] = 0x4F,
        [SDL_SCANCODE_KP_0] = 0x52,
        [SDL_SCANCODE_KP_PERIOD] = 0x53,

        [SDL_SCANCODE_LGUI] = 0x15B,
        [SDL_SCANCODE_RGUI] = 0x15C,
        [SDL_SCANCODE_APPLICATION] = 0x15D,
        [SDL_SCANCODE_PRINTSCREEN] = 0x137,
        [SDL_SCANCODE_NONUSBACKSLASH] = 0x56,
};

typedef struct mouseinputdata
{
    int deltax, deltay, deltaz;
    int mousebuttons;
} mouseinputdata;
static mouseinputdata mousedata;

// #define ENABLE_SDL_LOG 3
#ifdef ENABLE_SDL_LOG
int sdl_do_log = ENABLE_SDL_LOG;

static void
sdl_log(const char *fmt, ...)
{
    va_list ap;

    if (sdl_do_log) {
	va_start(ap, fmt);
	pclog_ex(fmt, ap);
	va_end(ap);
    }
}
#else
#define sdl_log(fmt, ...)
#endif


static void
sdl_integer_scale(double *d, double *g)
{
    double ratio;

    if (*d > *g) {
	ratio = floor(*d / *g);
	*d = *g * ratio;
    } else {
	ratio = ceil(*d / *g);
	*d = *g / ratio;
    }
}


static void
sdl_stretch(int *w, int *h, int *x, int *y)
{
    double hw, gw, hh, gh, dx, dy, dw, dh, gsr, hsr;

    hw = (double) sdl_w;
    hh = (double) sdl_h;
    gw = (double) *w;
    gh = (double) *h;
    hsr = hw / hh;

    switch (video_fullscreen_scale) {
	case FULLSCR_SCALE_FULL:
	default:
		*w = sdl_w;
		*h = sdl_h;
		*x = 0;
		*y = 0;
		break;
	case FULLSCR_SCALE_43:
	case FULLSCR_SCALE_KEEPRATIO:
		if (video_fullscreen_scale == FULLSCR_SCALE_43)
			gsr = 4.0 / 3.0;
		else
			gsr = gw / gh;
		if (gsr <= hsr) {
			dw = hh * gsr;
			dh = hh;
		} else {
			dw = hw;
			dh = hw / gsr;
		}
		dx = (hw - dw) / 2.0;
		dy = (hh - dh) / 2.0;
		*w = (int) dw;
		*h = (int) dh;
		*x = (int) dx;
		*y = (int) dy;
		break;
	case FULLSCR_SCALE_INT:
		gsr = gw / gh;
		if (gsr <= hsr) {
			dw = hh * gsr;
			dh = hh;
		} else {
			dw = hw;
			dh = hw / gsr;
		}
		sdl_integer_scale(&dw, &gw);
		sdl_integer_scale(&dh, &gh);
		dx = (hw - dw) / 2.0;
		dy = (hh - dh) / 2.0;
		*w = (int) dw;
		*h = (int) dh;
		*x = (int) dx;
		*y = (int) dy;
		break;
    }
}

static void
sdl_blit(int x, int y, int w, int h)
{
    SDL_Rect r_src;
    void *pixeldata;
    int ret, pitch;

    if (!sdl_enabled || (x < 0) || (y < 0) || (w <= 0) || (h <= 0) || (w > 2048) || (h > 2048) || (buffer32 == NULL) || (sdl_render == NULL) || (sdl_tex == NULL)) {
        video_blit_complete();
        return;
    }

    SDL_LockMutex(sdl_mutex);
    SDL_LockTexture(sdl_tex, 0, &pixeldata, &pitch);

    video_copy(pixeldata, &(buffer32->line[y][x]), h * (2048) * sizeof(uint32_t));

    if (screenshots)
        video_screenshot((uint32_t *) pixeldata, 0, 0, (2048));

    SDL_UnlockTexture(sdl_tex);

    video_blit_complete();

    SDL_RenderClear(sdl_render);

    r_src.x = 0;
    r_src.y = 0;
    r_src.w = w;
    r_src.h = h;

    ret = SDL_RenderCopy(sdl_render, sdl_tex, &r_src, 0);
    if (ret)
        sdl_log("SDL: unable to copy texture to renderer (%s)\n", sdl_GetError());

    SDL_RenderPresent(sdl_render);

    SDL_UnlockMutex(sdl_mutex);
}


static void
sdl_destroy_window(void)
{
    if (sdl_win != NULL) {
	SDL_DestroyWindow(sdl_win);
	sdl_win = NULL;
    }
}


static void
sdl_destroy_texture(void)
{
    if (sdl_tex != NULL) {
	SDL_DestroyTexture(sdl_tex);
	sdl_tex = NULL;
    }

    /* SDL_DestroyRenderer also automatically destroys all associated textures. */
    if (sdl_render != NULL) {
	SDL_DestroyRenderer(sdl_render);
	sdl_render = NULL;
    }
}


void
sdl_close(void)
{
    if (sdl_mutex != NULL)
	SDL_LockMutex(sdl_mutex);

    /* Unregister our renderer! */
    video_setblit(NULL);

    if (sdl_enabled)
	sdl_enabled = 0;

    if (sdl_mutex != NULL) {
	SDL_DestroyMutex(sdl_mutex);
	sdl_mutex = NULL;
    }

    sdl_destroy_texture();
    sdl_destroy_window();

    /* Quit. */
    SDL_Quit();
    sdl_flags = -1;
}

static void sdl_select_best_hw_driver(void) {
    int i;
    SDL_RendererInfo renderInfo;

    for (i = 0; i < SDL_GetNumRenderDrivers(); ++i) {
        SDL_GetRenderDriverInfo(i, &renderInfo);
        if (renderInfo.flags & SDL_RENDERER_ACCELERATED) {
            SDL_SetHint(SDL_HINT_RENDER_DRIVER, renderInfo.name);
            return;
        }
    }
}

static void
sdl_init_texture(void)
{
    if (sdl_flags & RENDERER_HARDWARE) {
        sdl_render = SDL_CreateRenderer(sdl_win, -1, SDL_RENDERER_ACCELERATED);
        SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, video_filter_method ? "1" : "0");
    } else {
        sdl_render = SDL_CreateRenderer(sdl_win, -1, SDL_RENDERER_SOFTWARE);
    }

    sdl_tex = SDL_CreateTexture(sdl_render, SDL_PIXELFORMAT_ARGB8888,
				SDL_TEXTUREACCESS_STREAMING, (2048), (2048));

    if (sdl_render == NULL) {
        sdl_log("SDL: unable to SDL_CreateRenderer (%s)\n", SDL_GetError());
    }
    if (sdl_tex == NULL) {
        sdl_log("SDL: unable to SDL_CreateTexture (%s)\n", SDL_GetError());
    }
}


static void
sdl_reinit_texture(void)
{
    if (sdl_flags == -1)
        return;

    sdl_destroy_texture();
    sdl_init_texture();
}


void sdl_set_fs(int fs) {
    SDL_SetWindowFullscreen(sdl_win, fs ? SDL_WINDOW_FULLSCREEN_DESKTOP : 0);
    SDL_SetRelativeMouseMode((SDL_bool)mouse_capture);

    sdl_fs = fs;

    if (fs) {
        sdl_flags |= RENDERER_FULL_SCREEN;
    } else {
        sdl_flags &= ~RENDERER_FULL_SCREEN;
    }

    sdl_reinit_texture();
}


static int
sdl_init_common(void* win, int flags)
{
    wchar_t temp[128];
    SDL_version ver;

    sdl_log("SDL: init (fs=%d)\n", 0);

    /* Get and log the version of the DLL we are using. */
    SDL_GetVersion(&ver);
    sdl_log("SDL: version %d.%d.%d\n", ver.major, ver.minor, ver.patch);

    /* Initialize the SDL system. */
    if (SDL_Init(SDL_INIT_VIDEO) < 0) {
        sdl_log("SDL: initialization failed (%s)\n", SDL_GetError());
        return(0);
    }

    if (flags & RENDERER_HARDWARE) {
        if (flags & RENDERER_OPENGL)
            SDL_SetHint(SDL_HINT_RENDER_DRIVER, "OpenGL");
        else
            sdl_select_best_hw_driver();
    }

    /* Get the size of the (current) desktop. */
    SDL_DisplayMode dm;
    if (SDL_GetDesktopDisplayMode(0, &dm) != 0) {
        sdl_log("SDL: SDL_GetDesktopDisplayMode failed (%s)\n", SDL_GetError());
        return(0);
    }
    sdl_w = dm.w;
    sdl_h = dm.h;
    sdl_flags = flags;

    sdl_win = SDL_CreateWindow("86Box renderer", 640, 480, 100, 100, sdl_flags);
    if (sdl_win == NULL) {
        sdl_log("SDL: unable to CreateWindowFrom (%s)\n", SDL_GetError());
    }
    sdl_init_texture();
    sdl_set_fs(video_fullscreen & 1);

    /* Make sure we get a clean exit. */
    atexit(sdl_close);

    /* Register our renderer! */
    video_setblit(sdl_blit);

    sdl_enabled = 1;
    sdl_mutex = SDL_CreateMutex();

    return(1);
}


int
sdl_inits(void* win)
{
    return sdl_init_common(win, 0);
}


int
sdl_inith(void* win)
{
    return sdl_init_common(win, RENDERER_HARDWARE);
}


int
sdl_initho(void* win)
{
    return sdl_init_common(win, RENDERER_HARDWARE | RENDERER_OPENGL);
}


int
sdl_pause(void)
{
    return(0);
}


void
sdl_resize(int w, int h)
{
    int ww = 0, wh = 0;

    if (video_fullscreen & 2)
	return;

    if ((w == cur_w) && (h == cur_h))
	return;

    SDL_LockMutex(sdl_mutex);

    ww = w;
    wh = h;

    if (sdl_fs) {
//        sdl_stretch(&ww, &wh, &wx, &wy);
//        MoveWindow(hwndRender, wx, wy, ww, wh, TRUE);
    }

    cur_w = w;
    cur_h = h;

    cur_ww = ww;
    cur_wh = wh;

    SDL_SetWindowSize(sdl_win, cur_ww, cur_wh);
    sdl_reinit_texture();

    SDL_UnlockMutex(sdl_mutex);
}


void
sdl_enable(int enable)
{
    if (sdl_flags == -1)
	return;

    SDL_LockMutex(sdl_mutex);
    sdl_enabled = !!enable;

    if (enable == 1) {
        SDL_SetWindowSize(sdl_win, cur_ww, cur_wh);
        sdl_reinit_texture();
    }

    SDL_UnlockMutex(sdl_mutex);
}


void
sdl_reload(void)
{
    if (sdl_flags & RENDERER_HARDWARE) {
	SDL_LockMutex(sdl_mutex);

	SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, video_filter_method ? "1" : "0");
	sdl_reinit_texture();

	SDL_UnlockMutex(sdl_mutex);
    }
}

static int mouse_inside = 0;
enum sdl_main_status sdl_main() {
    int ret = SdlMainOk;
    SDL_Rect r_src;
    SDL_Event event;

    while (SDL_PollEvent(&event))
    {
        switch(event.type)
        {
        case SDL_QUIT:
            ret = SdlMainQuit;
            break;
        case SDL_MOUSEWHEEL:
        {
            if (mouse_capture || video_fullscreen)
            {
                if (event.wheel.direction == SDL_MOUSEWHEEL_FLIPPED)
                {
                    event.wheel.x *= -1;
                    event.wheel.y *= -1;
                }
                mousedata.deltaz = event.wheel.y;
            }
            break;
        }
        case SDL_MOUSEMOTION:
        {
            if (mouse_capture || video_fullscreen)
            {
                mousedata.deltax += event.motion.xrel;
                mousedata.deltay += event.motion.yrel;
            }
            break;
        }
        case SDL_MOUSEBUTTONDOWN:
        case SDL_MOUSEBUTTONUP:
        {
            if ((event.button.button == SDL_BUTTON_LEFT)
                && !(mouse_capture || video_fullscreen)
                && event.button.state == SDL_RELEASED
                && mouse_inside)
            {
                plat_mouse_capture(1);
                break;
            }
            if (mouse_get_buttons() < 3 && event.button.button == SDL_BUTTON_MIDDLE && !video_fullscreen)
            {
                plat_mouse_capture(0);
                break;
            }
            if (mouse_capture || video_fullscreen)
            {
                int buttonmask = 0;

                switch(event.button.button)
                {
                case SDL_BUTTON_LEFT:
                    buttonmask = 1;
                    break;
                case SDL_BUTTON_RIGHT:
                    buttonmask = 2;
                    break;
                case SDL_BUTTON_MIDDLE:
                    buttonmask = 4;
                    break;
                }
                if (event.button.state == SDL_PRESSED)
                {
                    mousedata.mousebuttons |= buttonmask;
                }
                else mousedata.mousebuttons &= ~buttonmask;
            }
            break;
        }
        case SDL_RENDER_DEVICE_RESET:
        case SDL_RENDER_TARGETS_RESET:
        {
            sdl_reinit_texture();
            break;
        }
        case SDL_KEYDOWN:
        case SDL_KEYUP:
        {
            uint16_t xtkey = 0;
            switch(event.key.keysym.scancode)
            {
            default:
                xtkey = sdl_to_xt[event.key.keysym.scancode];
                break;
            }
            keyboard_input(event.key.state == SDL_PRESSED, xtkey);
        }
        break;
        case SDL_WINDOWEVENT:
        {
            switch (event.window.event)
            {
            case SDL_WINDOWEVENT_ENTER:
                mouse_inside = 1;
                break;
            case SDL_WINDOWEVENT_LEAVE:
                mouse_inside = 0;
                break;
            }
        }
        }
    }

    if (mouse_capture && keyboard_ismsexit()) {
        plat_mouse_capture(0);
    }
    if (video_fullscreen && keyboard_isfsexit()) {
        plat_setfullscreen(0);
    }

    return ret;
}

void sdl_mouse_capture(int on) {
    SDL_SetRelativeMouseMode((SDL_bool)on);
}

void sdl_mouse_poll() {
    mouse_x = mousedata.deltax;
    mouse_y = mousedata.deltay;
    mouse_z = mousedata.deltaz;
    mousedata.deltax = mousedata.deltay = mousedata.deltaz = 0;
    mouse_buttons = mousedata.mousebuttons;
}
