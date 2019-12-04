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
 * Version:	@(#)win_sdl.c  	1.0.9	2019/12/05
 *
 * Authors:	Fred N. van Kempen, <decwiz@yahoo.com>
 *		Michael Drüing, <michael@drueing.de>
 *
 *		Copyright 2018,2019 Fred N. van Kempen.
 *		Copyright 2018,2019 Michael Drüing.
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
#define UNICODE
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <SDL2/SDL.h>

#include <stdarg.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
/* This #undef is needed because a SDL include header redefines HAVE_STDARG_H. */
#undef HAVE_STDARG_H
#define HAVE_STDARG_H
#include "../86box.h"
#include "../device.h"
#include "../plat.h"
#include "../plat_dynld.h"
#include "../video/video.h"
#include "../ui.h"
#include "win.h"
#include "win_sdl.h"


#define RENDERER_FULL_SCREEN	1
#define RENDERER_HARDWARE	2


static SDL_Window	*sdl_win = NULL;
static SDL_Renderer	*sdl_render = NULL;
static SDL_Texture	*sdl_tex = NULL;
static HWND		sdl_parent_hwnd = NULL;
static HWND		sdl_hwnd = NULL;
static int		sdl_w, sdl_h;
static int		sdl_fs;
static int		cur_w, cur_h;
static volatile int	sdl_enabled = 0;
static SDL_mutex*	sdl_mutex = NULL;


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
sdl_stretch(int *w, int *h, int *x, int *y)
{
    double dw, dh, dx, dy, temp, temp2, ratio_w, ratio_h, gsr, hsr;

    switch (video_fullscreen_scale) {
	case FULLSCR_SCALE_FULL:
		*w = sdl_w;
		*h = sdl_h;
		*x = 0;
		*y = 0;
		break;
	case FULLSCR_SCALE_43:
	case FULLSCR_SCALE_KEEPRATIO:
		dw = (double) sdl_w;
		dh = (double) sdl_h;
		hsr = dw / dh;
		if (video_fullscreen_scale == FULLSCR_SCALE_43)
			gsr = 4.0 / 3.0;
		else
			gsr = ((double) *w) / ((double) *h);
		if (gsr <= hsr) {
			temp = dh * gsr;
			dx = (dw - temp) / 2.0;
			dw = temp;
			*w = (int) dw;
			*h = (int) dh;
			*x = (int) dx;
			*y = 0;
		} else {
			temp = dw / gsr;
			dy = (dh - temp) / 2.0;
			dh = temp;
			*w = (int) dw;
			*h = (int) dh;
			*x = 0;
			*y = (int) dy;
		}
		break;
	case FULLSCR_SCALE_INT:
		dw = (double) sdl_w;
		dh = (double) sdl_h;
		temp = ((double) *w);
		temp2 = ((double) *h);
		ratio_w = dw / ((double) *w);
		ratio_h = dh / ((double) *h);
		if (ratio_h < ratio_w)
			ratio_w = ratio_h;
		dx = (dw / 2.0) - ((temp * ratio_w) / 2.0);
		dy = (dh / 2.0) - ((temp2 * ratio_h) / 2.0);
		dw -= (dx * 2.0);
		dh -= (dy * 2.0);
		*w = (int) dw;
		*h = (int) dh;
		*x = (int) dx;
		*y = (int) dy;
		break;
    }
}


static void
sdl_blit(int x, int y, int y1, int y2, int w, int h)
{
    SDL_Rect r_src;
    void *pixeldata;
    int pitch;
    int yy, ret;

    if (!sdl_enabled) {
	video_blit_complete();
	return;
    }

    if ((y1 == y2) || (h <= 0)) {
	video_blit_complete();
	return;
    }

    if (buffer32 == NULL) {
	video_blit_complete();
	return;
    }

    SDL_LockMutex(sdl_mutex);

    /*
     * TODO:
     * SDL_UpdateTexture() might be better here, as it is
     * (reportedly) slightly faster.
     */
    SDL_LockTexture(sdl_tex, 0, &pixeldata, &pitch);

    for (yy = y1; yy < y2; yy++) {
       	if ((y + yy) >= 0 && (y + yy) < buffer32->h) {
		if (video_grayscale || invert_display)
			video_transform_copy((uint32_t *) &(((uint8_t *)pixeldata)[yy * pitch]), &(buffer32->line[y + yy][x]), w);
		else
			memcpy((uint32_t *) &(((uint8_t *)pixeldata)[yy * pitch]), &(buffer32->line[y + yy][x]), w * 4);
	}
    }

    video_blit_complete();

    SDL_UnlockTexture(sdl_tex);

    if (sdl_fs) {
	sdl_log("sdl_blit(%i, %i, %i, %i, %i, %i) (%i, %i)\n", x, y, y1, y2, w, h, unscaled_size_x, efscrnsz_y);
	if (w == unscaled_size_x)
		sdl_resize(w, h);
	sdl_log("(%08X, %08X, %08X)\n", sdl_win, sdl_render, sdl_tex);
    }

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


void
sdl_close(void)
{
    /* Unregister our renderer! */
    video_setblit(NULL);

    if (sdl_enabled)
	sdl_enabled = 0;

    if (sdl_mutex != NULL) {
	SDL_DestroyMutex(sdl_mutex);
	sdl_mutex = NULL;
    }

    if (sdl_tex != NULL) {
	SDL_DestroyTexture(sdl_tex);
	sdl_tex = NULL;
    }

    if (sdl_render != NULL) {
	SDL_DestroyRenderer(sdl_render);
	sdl_render = NULL;
    }

    if (sdl_win != NULL) {
	SDL_DestroyWindow(sdl_win);
	sdl_win = NULL;
    }

    if (sdl_hwnd != NULL) {
	plat_set_input(hwndMain);

	ShowWindow(hwndRender, TRUE);

	SetFocus(hwndMain);

	if (sdl_fs)
		DestroyWindow(sdl_hwnd);
	sdl_hwnd = NULL;
    }

    if (sdl_parent_hwnd != NULL) {
	DestroyWindow(sdl_parent_hwnd);
	sdl_parent_hwnd = NULL;
    }

    /* Quit. */
    SDL_Quit();
}


static int old_capture = 0;


static int
sdl_init_common(int flags)
{
    wchar_t temp[128];
    SDL_version ver;
    int w = 0, h = 0, x = 0, y = 0;
    RECT rect;

    sdl_log("SDL: init (fs=%d)\n", fs);

    /* Get and log the version of the DLL we are using. */
    SDL_GetVersion(&ver);
    sdl_log("SDL: version %d.%d.%d\n", ver.major, ver.minor, ver.patch);

    /* Initialize the SDL system. */
    if (SDL_Init(SDL_INIT_VIDEO) < 0) {
	sdl_log("SDL: initialization failed (%s)\n", sdl_GetError());
	return(0);
    }

    if (flags & RENDERER_HARDWARE)
	SDL_SetHint(SDL_HINT_RENDER_DRIVER, "direct3d");
	/* TODO: why is this necessary to avoid black screen on Win7/8/10? */

    if (flags & RENDERER_FULL_SCREEN) {
	/* Get the size of the (current) desktop. */
	sdl_w = GetSystemMetrics(SM_CXSCREEN);
	sdl_h = GetSystemMetrics(SM_CYSCREEN);

	/* Create the desktop-covering window. */
	_swprintf(temp, L"%s v%s", EMU_NAME_W, EMU_VERSION_W);
        sdl_parent_hwnd = CreateWindow(SUB_CLASS_NAME,
				temp,
				WS_POPUP,
				0, 0, sdl_w, sdl_h,
				HWND_DESKTOP,
				NULL,
				hinstance,
				NULL);

	SetWindowPos(sdl_parent_hwnd, HWND_TOPMOST,
		     0, 0, sdl_w, sdl_h, SWP_SHOWWINDOW);

	/* Create the actual rendering window. */
	_swprintf(temp, L"%s v%s", EMU_NAME_W, EMU_VERSION_W);
        sdl_hwnd = CreateWindow(SUB_CLASS_NAME,
				temp,
				WS_POPUP,
				0, 0, sdl_w, sdl_h,
				sdl_parent_hwnd,
				NULL,
				hinstance,
				NULL);
	sdl_log("SDL: FS %dx%d window at %08lx\n", sdl_w, sdl_h, sdl_hwnd);

	/* Redirect RawInput to this new window. */
	plat_set_input(sdl_hwnd);

	SetFocus(sdl_hwnd);

	/* Show the window, make it topmost, and give it focus. */
	w = unscaled_size_x;
	h = efscrnsz_y;
	sdl_stretch(&w, &h, &x, &y);
	SetWindowPos(sdl_hwnd, sdl_parent_hwnd,
		     x, y, w, h, SWP_SHOWWINDOW);

	/* Now create the SDL window from that. */
	sdl_win = SDL_CreateWindowFrom((void *)sdl_hwnd);

	old_capture = mouse_capture;

	GetWindowRect(sdl_hwnd, &rect);

	ClipCursor(&rect);

	mouse_capture = 1;
    } else {
	/* Create the SDL window from the render window. */
	sdl_win = SDL_CreateWindowFrom((void *)hwndRender);

	mouse_capture = old_capture;

	if (mouse_capture) {
		GetWindowRect(hwndRender, &rect);

		ClipCursor(&rect);
	}
    }
    if (sdl_win == NULL) {
	sdl_log("SDL: unable to CreateWindowFrom (%s)\n", SDL_GetError());
	sdl_close();
	return(0);
    }

    /*
     * TODO:
     * SDL_RENDERER_SOFTWARE, because SDL tries to do funky stuff
     * otherwise (it turns off Win7 Aero and it looks like it's
     * trying to switch to fullscreen even though the window is
     * not a fullscreen window?)
     */
    if (flags & RENDERER_HARDWARE) {
	sdl_render = SDL_CreateRenderer(sdl_win, -1, SDL_RENDERER_ACCELERATED);
	SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "1");
    } else
	sdl_render = SDL_CreateRenderer(sdl_win, -1, SDL_RENDERER_SOFTWARE);

    if (sdl_render == NULL) {
	sdl_log("SDL: unable to create renderer (%s)\n", SDL_GetError());
	sdl_close();
        return(0);
    }

    /*
     * TODO:
     * Actually the source is (apparently) XRGB8888, but the alpha
     * channel seems to be set to 255 everywhere, so ARGB8888 works
     * just as well.
     */
    sdl_tex = SDL_CreateTexture(sdl_render, SDL_PIXELFORMAT_ARGB8888,
				SDL_TEXTUREACCESS_STREAMING, 2048, 2048);
    if (sdl_tex == NULL) {
	sdl_log("SDL: unable to create texture (%s)\n", SDL_GetError());
	sdl_close();
        return(0);
    }

    /* Make sure we get a clean exit. */
    atexit(sdl_close);

    /* Register our renderer! */
    video_setblit(sdl_blit);

    sdl_fs = !!(flags & RENDERER_FULL_SCREEN);

    sdl_enabled = 1;

    sdl_mutex = SDL_CreateMutex();

    return(1);
}


int
sdl_inits(HWND h)
{
    return sdl_init_common(0);
}


int
sdl_inith(HWND h)
{
    return sdl_init_common(RENDERER_HARDWARE);
}


int
sdl_inits_fs(HWND h)
{
    return sdl_init_common(RENDERER_FULL_SCREEN);
}


int
sdl_inith_fs(HWND h)
{
    return sdl_init_common(RENDERER_FULL_SCREEN | RENDERER_HARDWARE);
}


int
sdl_pause(void)
{
    return(0);
}


void
sdl_resize(int x, int y)
{
    int ww = 0, wh = 0, wx = 0, wy = 0;

    if ((x == cur_w) && (y == cur_h))
	return;

    ww = x;
    wh = y;

    if (sdl_fs) {
	sdl_stretch(&ww, &wh, &wx, &wy);
	MoveWindow(sdl_hwnd, wx, wy, ww, wh, TRUE);
    }

    cur_w = x;
    cur_h = y;
}


void
sdl_enable(int enable)
{
    sdl_enabled = enable;
}
