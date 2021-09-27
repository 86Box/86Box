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
 *		Michael Dr�ing, <michael@drueing.de>
 *
 *		Copyright 2018-2020 Fred N. van Kempen.
 *		Copyright 2018-2020 Michael Dr�ing.
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
#include <86box/86box.h>
#include <86box/device.h>
#include <86box/plat.h>
#include <86box/plat_dynld.h>
#include <86box/video.h>
#include <86box/ui.h>
#include <86box/win.h>
#include <86box/win_sdl.h>
#include <86box/version.h>


#define RENDERER_FULL_SCREEN	1
#define RENDERER_HARDWARE	2
#define RENDERER_OPENGL		4


static SDL_Window	*sdl_win = NULL;
static SDL_Renderer	*sdl_render = NULL;
static SDL_Texture	*sdl_tex = NULL;
static HWND		sdl_parent_hwnd = NULL;
static int		sdl_w, sdl_h;
static int		sdl_fs, sdl_flags = -1;
static int		cur_w, cur_h;
static int		cur_wx = 0, cur_wy = 0, cur_ww =0, cur_wh = 0;
static volatile int	sdl_enabled = 0;
static SDL_mutex*	sdl_mutex = NULL;


typedef struct
{
    const void *magic;
    Uint32 id;
    char *title;
    SDL_Surface *icon;
    int x, y;
    int w, h;
    int min_w, min_h;
    int max_w, max_h;
    Uint32 flags;
    Uint32 last_fullscreen_flags;

    /* Stored position and size for windowed mode */
    SDL_Rect windowed;

    SDL_DisplayMode fullscreen_mode;

    float brightness;
    Uint16 *gamma;
    Uint16 *saved_gamma;        /* (just offset into gamma) */

    SDL_Surface *surface;
    SDL_bool surface_valid;

    SDL_bool is_hiding;
    SDL_bool is_destroying;

    void *shaper;

    SDL_HitTest hit_test;
    void *hit_test_data;

    void *data;

    void *driverdata;

    SDL_Window *prev;
    SDL_Window *next;
} SDL_Window_Ex;


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
    int ret;

    if (!sdl_enabled || (x < 0) || (y < 0) || (w <= 0) || (h <= 0) || (w > 2048) || (h > 2048) || (buffer32 == NULL) || (sdl_render == NULL) || (sdl_tex == NULL)) {
	video_blit_complete();
	return;
    }

    SDL_LockMutex(sdl_mutex);

    r_src.x = x;
    r_src.y = y;
    r_src.w = w;
    r_src.h = h;
    SDL_UpdateTexture(sdl_tex, &r_src, &(buffer32->line[y][x]), (2048 + 64) * sizeof(uint32_t));
    video_blit_complete();

    SDL_RenderClear(sdl_render);

    r_src.x = x;
    r_src.y = y;
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
    ImmAssociateContext(hwndMain, NULL);
    SetFocus(hwndMain);

    if (sdl_parent_hwnd != NULL) {
	DestroyWindow(sdl_parent_hwnd);
	sdl_parent_hwnd = NULL;
    }

    /* Quit. */
    SDL_Quit();
    sdl_flags = -1;
}


static int old_capture = 0;


static void
sdl_select_best_hw_driver(void)
{
    int i;
    SDL_RendererInfo renderInfo;

    for (i = 0; i < SDL_GetNumRenderDrivers(); ++i)
    {
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
    } else
	sdl_render = SDL_CreateRenderer(sdl_win, -1, SDL_RENDERER_SOFTWARE);

    sdl_tex = SDL_CreateTexture(sdl_render, SDL_PIXELFORMAT_ARGB8888,
				SDL_TEXTUREACCESS_STREAMING, 2048, 2048);
}


static void
sdl_reinit_texture(void)
{
    if (sdl_flags == -1)
        return;

    sdl_destroy_texture();
    sdl_init_texture();
}


void
sdl_set_fs(int fs)
{
    int w = 0, h = 0, x = 0, y = 0;
    RECT rect;

    SDL_LockMutex(sdl_mutex);
    sdl_enabled = 0;

    if (fs) {
	ShowWindow(sdl_parent_hwnd, TRUE);
	SetParent(hwndRender, sdl_parent_hwnd);
	ShowWindow(hwndRender, TRUE);
	MoveWindow(sdl_parent_hwnd, 0, 0, sdl_w, sdl_h, TRUE);

	/* Show the window, make it topmost, and give it focus. */
	w = unscaled_size_x;
	h = efscrnsz_y;
	sdl_stretch(&w, &h, &x, &y);
	MoveWindow(hwndRender, x, y, w, h, TRUE);
	ImmAssociateContext(sdl_parent_hwnd, NULL);
	SetFocus(sdl_parent_hwnd);

	/* Redirect RawInput to this new window. */
	old_capture = mouse_capture;
	GetWindowRect(hwndRender, &rect);
	ClipCursor(&rect);
	mouse_capture = 1;
    } else {
	SetParent(hwndRender, hwndMain);
	ShowWindow(sdl_parent_hwnd, FALSE);
	ShowWindow(hwndRender, TRUE);
	ImmAssociateContext(hwndMain, NULL);
	SetFocus(hwndMain);
	mouse_capture = old_capture;

	if (mouse_capture) {
		GetWindowRect(hwndRender, &rect);
		ClipCursor(&rect);
	} else
		ClipCursor(&oldclip);
    }

    sdl_fs = fs;

    if (fs)
	sdl_flags |= RENDERER_FULL_SCREEN;
    else
	sdl_flags &= ~RENDERER_FULL_SCREEN;

    // sdl_reinit_texture();
    sdl_enabled = 1;
    SDL_UnlockMutex(sdl_mutex);
}


static int
sdl_init_common(int flags)
{
    wchar_t temp[128];
    SDL_version ver;

    sdl_log("SDL: init (fs=%d)\n", fs);

    /* Get and log the version of the DLL we are using. */
    SDL_GetVersion(&ver);
    sdl_log("SDL: version %d.%d.%d\n", ver.major, ver.minor, ver.patch);

    /* Initialize the SDL system. */
    if (SDL_Init(SDL_INIT_VIDEO) < 0) {
	sdl_log("SDL: initialization failed (%s)\n", sdl_GetError());
	return(0);
    }

    if (flags & RENDERER_HARDWARE) {
	if (flags & RENDERER_OPENGL)
		SDL_SetHint(SDL_HINT_RENDER_DRIVER, "OpenGL");
	else
		sdl_select_best_hw_driver();
    }

    /* Get the size of the (current) desktop. */
    sdl_w = GetSystemMetrics(SM_CXSCREEN);
    sdl_h = GetSystemMetrics(SM_CYSCREEN);

    /* Create the desktop-covering window. */
    _swprintf(temp, L"%s v%s", EMU_NAME_W, EMU_VERSION_W);
    sdl_parent_hwnd = CreateWindow(SDL_CLASS_NAME, temp, WS_POPUP, 0, 0, sdl_w, sdl_h,
				   HWND_DESKTOP, NULL, hinstance, NULL);
    ShowWindow(sdl_parent_hwnd, FALSE);

    sdl_flags = flags;

    if (sdl_win == NULL) {
	sdl_log("SDL: unable to CreateWindowFrom (%s)\n", SDL_GetError());
    }

    sdl_win = SDL_CreateWindowFrom((void *)hwndRender);
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
sdl_initho(HWND h)
{
    return sdl_init_common(RENDERER_HARDWARE | RENDERER_OPENGL);
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

    if (video_fullscreen & 2)
	return;

    if ((x == cur_w) && (y == cur_h))
	return;

    SDL_LockMutex(sdl_mutex);

    ww = x;
    wh = y;

    if (sdl_fs) {
	sdl_stretch(&ww, &wh, &wx, &wy);
	MoveWindow(hwndRender, wx, wy, ww, wh, TRUE);
    }

    cur_w = x;
    cur_h = y;

    cur_wx = wx;
    cur_wy = wy;
    cur_ww = ww;
    cur_wh = wh;

    SDL_SetWindowSize(sdl_win, cur_ww, cur_wh);
    SDL_SetWindowPosition(sdl_win, cur_wx, cur_wy);

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
