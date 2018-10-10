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
 * Version:	@(#)win_sdl.c  	1.0.1	2018/10/10
 *
 * Authors:	Fred N. van Kempen, <decwiz@yahoo.com>
 *		Michael Drüing, <michael@drueing.de>
 *
 *		Copyright 2018 Fred N. van Kempen.
 *		Copyright 2018 Michael Drüing.
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

#define PNG_DEBUG 0
#include <png.h>

#include <stdarg.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include "../86box.h"
#include "../device.h"
#include "../plat.h"
#include "../plat_dynld.h"
#include "../video/video.h"
#include "win.h"
#include "win_sdl.h"


#define PATH_SDL_DLL	"sdl2.dll"


static void		*sdl_handle = NULL;	/* handle to libSDL2 DLL */
static SDL_Window	*sdl_win = NULL;
static SDL_Renderer	*sdl_render = NULL;
static SDL_Texture	*sdl_tex = NULL;
static HWND		sdl_parent_hwnd = NULL;
static HWND		sdl_hwnd = NULL;
static int		sdl_w, sdl_h;
static int		sdl_fs;
static int		cur_w, cur_h;

static png_structp		png_ptr;
static png_infop		info_ptr;


/* Pointers to the real functions. */
static void 		(*sdl_GetVersion)(SDL_version *ver);
static char		*const (*sdl_GetError)(void);
static int 		(*sdl_Init)(Uint32 flags);
static void	 	(*sdl_Quit)(void);
static SDL_Window	*(*sdl_CreateWindowFrom)(const void *data);
static void	 	(*sdl_DestroyWindow)(SDL_Window *window);
static SDL_Renderer	*(*sdl_CreateRenderer)(SDL_Window *window,
						int index, Uint32 flags);
static void	 	(*sdl_DestroyRenderer)(SDL_Renderer *renderer);
static SDL_Texture	*(*sdl_CreateTexture)(SDL_Renderer *renderer,
						Uint32 format, int access,
						int w, int h);
static void	 	(*sdl_DestroyTexture)(SDL_Texture *texture);
static int 		(*sdl_LockTexture)(SDL_Texture *texture,
						const SDL_Rect *rect,
						void **pixels, int *pitch);
static void	 	(*sdl_UnlockTexture)(SDL_Texture *texture);
static int 		(*sdl_RenderCopy)(SDL_Renderer *renderer,
						SDL_Texture *texture,
						const SDL_Rect *srcrect,
						const SDL_Rect *dstrect);
static void	 	(*sdl_RenderPresent)(SDL_Renderer *renderer);
static void		(*sdl_GetWindowSize)(SDL_Window* window,
					     int*        w,
					     int*        h);
static int		(*sdl_RenderReadPixels)(SDL_Renderer*   renderer,
						const SDL_Rect* rect,
						Uint32          format,
						void*           pixels,
						int             pitch);
static SDL_bool		(*sdl_SetHint)(const char* name,
				       const char* value);

static dllimp_t sdl_imports[] = {
  { "SDL_GetVersion",		&sdl_GetVersion		},
  { "SDL_GetError",		&sdl_GetError		},
  { "SDL_Init",			&sdl_Init		},
  { "SDL_Quit",			&sdl_Quit		},
  { "SDL_CreateWindowFrom",	&sdl_CreateWindowFrom	},
  { "SDL_DestroyWindow",	&sdl_DestroyWindow	},
  { "SDL_CreateRenderer",	&sdl_CreateRenderer	},
  { "SDL_DestroyRenderer",	&sdl_DestroyRenderer	},
  { "SDL_CreateTexture",	&sdl_CreateTexture	},
  { "SDL_DestroyTexture",	&sdl_DestroyTexture	},
  { "SDL_LockTexture",		&sdl_LockTexture	},
  { "SDL_UnlockTexture",	&sdl_UnlockTexture	},
  { "SDL_RenderCopy",		&sdl_RenderCopy		},
  { "SDL_RenderPresent",	&sdl_RenderPresent	},
  { "SDL_GetWindowSize",	&sdl_GetWindowSize	},
  { "SDL_RenderReadPixels",	&sdl_RenderReadPixels	},
  { "SDL_SetHint",		&sdl_SetHint		},
  { NULL,			NULL			}
};


#ifdef ENABLE_SDL_LOG
int sdl_do_log = ENABLE_SDL_LOG;
#endif


static void
sdl_log(const char *fmt, ...)
{
#ifdef ENABLE_SDL_LOG
    va_list ap;

    if (sdl_do_log) {
	va_start(ap, fmt);
	pclog_ex(fmt, ap);
	va_end(ap);
    }
#endif
}


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
		dw = (double) sdl_w;
		dh = (double) sdl_h;
		temp = (dh / 3.0) * 4.0;
		dx = (dw - temp) / 2.0;
		dw = temp;
		*w = (int) dw;
		*h = (int) dh;
		*x = (int) dx;
		*y = 0;
		break;
	case FULLSCR_SCALE_SQ:
		dw = (double) sdl_w;
		dh = (double) sdl_h;
		temp = ((double) *w);
		temp2 = ((double) *h);
		dx = (dw / 2.0) - ((dh * temp) / (temp2 * 2.0));
		dy = 0.0;
		if (dx < 0.0) {
			dx = 0.0;
			dy = (dw / 2.0) - ((dh * temp2) / (temp * 2.0));
		}
		dw -= (dx * 2.0);
		dh -= (dy * 2.0);
		*w = (int) dw;
		*h = (int) dh;
		*x = (int) dx;
		*y = (int) dy;
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
	case FULLSCR_SCALE_KEEPRATIO:
		dw = (double) sdl_w;
		dh = (double) sdl_h;
		hsr = dw / dh;
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
    }
}


static void
sdl_blit(int x, int y, int y1, int y2, int w, int h)
{
    SDL_Rect r_src;
    void *pixeldata;
    int pitch;
    int yy, ret;

    if (y1 == y2) {
	video_blit_complete();
	return;
    }

    if (buffer32 == NULL) {
	video_blit_complete();
	return;
    }

    /*
     * TODO:
     * SDL_UpdateTexture() might be better here, as it is
     * (reportedly) slightly faster.
     */
    sdl_LockTexture(sdl_tex, 0, &pixeldata, &pitch);

    for (yy = y1; yy < y2; yy++) {
       	if ((y + yy) >= 0 && (y + yy) < buffer32->h) {
		if (video_grayscale || invert_display)
			video_transform_copy((uint32_t *) &(((uint8_t *)pixeldata)[yy * pitch]), &(((uint32_t *)buffer32->line[y + yy])[x]), w);
		else
			memcpy((uint32_t *) &(((uint8_t *)pixeldata)[yy * pitch]), &(((uint32_t *)buffer32->line[y + yy])[x]), w * 4);
	}
    }

    video_blit_complete();

    sdl_UnlockTexture(sdl_tex);

    if (sdl_fs) {
	pclog("sdl_blit(%i, %i, %i, %i, %i, %i) (%i, %i)\n", x, y, y1, y2, w, h, unscaled_size_x, efscrnsz_y);
	if (w == unscaled_size_x)
		sdl_resize(w, h);
	pclog("(%08X, %08X, %08X)\n", sdl_win, sdl_render, sdl_tex);
    }

    r_src.x = 0;
    r_src.y = 0;
    r_src.w = w;
    r_src.h = h;

    ret = sdl_RenderCopy(sdl_render, sdl_tex, &r_src, 0);
    if (ret)
	sdl_log("SDL: unable to copy texture to renderer (%s)\n", sdl_GetError());

    sdl_RenderPresent(sdl_render);
}


void
sdl_close(void)
{
    /* Unregister our renderer! */
    video_setblit(NULL);

    if (sdl_tex != NULL) {
	sdl_DestroyTexture(sdl_tex);
	sdl_tex = NULL;
    }

    if (sdl_render != NULL) {
	sdl_DestroyRenderer(sdl_render);
	sdl_render = NULL;
    }

    if (sdl_win != NULL) {
	sdl_DestroyWindow(sdl_win);
	sdl_win = NULL;
    }

    if (sdl_hwnd != NULL) {
	plat_set_input(hwndMain);

	ShowWindow(hwndRender, TRUE);

	SetFocus(hwndMain);

	DestroyWindow(sdl_hwnd);
	sdl_hwnd = NULL;
    }

    if (sdl_parent_hwnd != NULL) {
	DestroyWindow(sdl_parent_hwnd);
	sdl_parent_hwnd = NULL;
    }

    /* Quit and unload the DLL if possible. */
    if (sdl_handle != NULL) {
	sdl_Quit();

	dynld_close(sdl_handle);
	sdl_handle = NULL;
    }
}


static int old_capture = 0;


static int
sdl_init_common(int fs)
{
    wchar_t temp[128];
    SDL_version ver;
    int w, h, x, y;
    RECT rect;

    sdl_log("SDL: init (fs=%d)\n", fs);

    cgapal_rebuild();

    /* Try loading the DLL. */
    sdl_handle = dynld_module(PATH_SDL_DLL, sdl_imports);
    if (sdl_handle == NULL) {
	sdl_log("SDL: unable to load '%s', SDL not available.\n", PATH_SDL_DLL);
	return(0);
    }

    /* Get and log the version of the DLL we are using. */
    sdl_GetVersion(&ver);
    sdl_log("SDL: version %d.%d.%d\n", ver.major, ver.minor, ver.patch);

    /* Initialize the SDL system. */
    if (sdl_Init(SDL_INIT_VIDEO) < 0) {
	sdl_log("SDL: initialization failed (%s)\n", sdl_GetError());
	return(0);
    }

    if (fs) {
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
	sdl_win = sdl_CreateWindowFrom((void *)sdl_hwnd);

	old_capture = mouse_capture;

	GetWindowRect(sdl_hwnd, &rect);

	ClipCursor(&rect);

	mouse_capture = 1;
    } else {
	/* Create the SDL window from the render window. */
	sdl_win = sdl_CreateWindowFrom((void *)hwndRender);

	mouse_capture = old_capture;

	if (mouse_capture) {
		GetWindowRect(hwndRender, &rect);

		ClipCursor(&rect);
	}
    }
    if (sdl_win == NULL) {
	sdl_log("SDL: unable to CreateWindowFrom (%s)\n", sdl_GetError());
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
    sdl_render = sdl_CreateRenderer(sdl_win, -1, SDL_RENDERER_SOFTWARE);
    if (sdl_render == NULL) {
	sdl_log("SDL: unable to create renderer (%s)\n", sdl_GetError());
	sdl_close();
        return(0);
    }

    /*
     * TODO:
     * Actually the source is (apparently) XRGB8888, but the alpha
     * channel seems to be set to 255 everywhere, so ARGB8888 works
     * just as well.
     */
    sdl_tex = sdl_CreateTexture(sdl_render, SDL_PIXELFORMAT_ARGB8888,
				SDL_TEXTUREACCESS_STREAMING, 2048, 2048);
    if (sdl_tex == NULL) {
	sdl_log("SDL: unable to create texture (%s)\n", sdl_GetError());
	sdl_close();
        return(0);
    }

    /* Make sure we get a clean exit. */
    atexit(sdl_close);

    /* Register our renderer! */
    video_setblit(sdl_blit);

    sdl_fs = fs;

    return(1);
}


int
sdl_init(HWND h)
{
    return sdl_init_common(0);
}


int
sdl_init_fs(HWND h)
{
    return sdl_init_common(1);
}


void
sdl_take_screenshot(const wchar_t *fn)
{
    int i, res, x, y, width = 0, height = 0;
    unsigned char* rgba = NULL;
    png_bytep *b_rgb = NULL;
    FILE *fp = NULL;

    sdl_GetWindowSize(sdl_win, &width, &height);

    /* create file */
    fp = plat_fopen((wchar_t *) fn, (wchar_t *) L"wb");
    if (!fp) {
	sdl_log("[sdl_take_screenshot] File %ls could not be opened for writing", fn);
	return;
    }

    /* initialize stuff */
    png_ptr = png_create_write_struct(PNG_LIBPNG_VER_STRING, NULL, NULL, NULL);

    if (!png_ptr) {
	sdl_log("[sdl_take_screenshot] png_create_write_struct failed");
	fclose(fp);
	return;
    }

    info_ptr = png_create_info_struct(png_ptr);
    if (!info_ptr) {
	sdl_log("[sdl_take_screenshot] png_create_info_struct failed");
	fclose(fp);
	return;
    }

    png_init_io(png_ptr, fp);

    png_set_IHDR(png_ptr, info_ptr, width, height,
	8, PNG_COLOR_TYPE_RGB, PNG_INTERLACE_NONE,
	PNG_COMPRESSION_TYPE_BASE, PNG_FILTER_TYPE_BASE);

    rgba = (unsigned char *) malloc(width * height * 4);
    if (rgba == NULL) {
	sdl_log("[sdl_take_screenshot] Unable to Allocate RGBA Bitmap Memory");
	fclose(fp);
	return;
    }

    res = sdl_RenderReadPixels(sdl_render, NULL, SDL_PIXELFORMAT_ABGR8888, rgba, width * 4);
    if (res) {
	sdl_log("[sdl_take_screenshot] Error reading render pixels\n");
	fclose(fp);
	return;
    }

    b_rgb = (png_bytep *) malloc(sizeof(png_bytep) * height);
    if (b_rgb == NULL) {
	sdl_log("[sdl_take_screenshot] Unable to Allocate RGB Bitmap Memory");
	free(rgba);
	fclose(fp);
	return;
    }

    for (y = 0; y < height; ++y) {
	b_rgb[y] = (png_byte *) malloc(png_get_rowbytes(png_ptr, info_ptr));
    	for (x = 0; x < width; ++x) {
		b_rgb[y][(x) * 3 + 0] = rgba[(y * width + x) * 4 + 0];
		b_rgb[y][(x) * 3 + 1] = rgba[(y * width + x) * 4 + 1];
		b_rgb[y][(x) * 3 + 2] = rgba[(y * width + x) * 4 + 2];
	}
    }

    png_write_info(png_ptr, info_ptr);

    png_write_image(png_ptr, b_rgb);

    png_write_end(png_ptr, NULL);

    /* cleanup heap allocation */
    for (i = 0; i < height; i++)
	if (b_rgb[i])  free(b_rgb[i]);

    if (b_rgb) free(b_rgb);

    if (rgba) free(rgba);

    if (fp) fclose(fp);
}


int
sdl_pause(void)
{
    return(0);
}


void
sdl_resize(int x, int y)
{
    int ww, wh, wx, wy;

    if ((x == cur_w) && (y == cur_h))
	return;

    pclog("sdl_resize(%i, %i)\n", x, y);
    ww = x;
    wh = y;
    sdl_stretch(&ww, &wh, &wx, &wy);

    MoveWindow(sdl_hwnd, wx, wy, ww, wh, TRUE);

    cur_w = x;
    cur_h = y;
}
