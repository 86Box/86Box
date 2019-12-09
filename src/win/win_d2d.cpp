/*
 * 86Box	A hypervisor and IBM PC system emulator that specializes in
 *		running old operating systems and software designed for IBM
 *		PC systems and compatibles from 1981 through fairly recent
 *		system designs based on the PCI bus.
 *
 *		This file is part of the 86Box distribution.
 *
 *		Rendering module for Microsoft Direct2D.
 *
 * Version:	@(#)win_d2d.cpp	1.0.6	2019/12/10
 *
 * Authors:	David Hrdlička, <hrdlickadavid@outlook.com>
 *
 *		Copyright 2018,2019 David Hrdlička.
 */
#include <stdarg.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#define UNICODE
#define BITMAP WINDOWS_BITMAP
#include <windows.h>
#ifdef USE_D2D
#include <d2d1.h>
#include <d2d1helper.h>
#endif
#undef BITMAP

#define PNG_DEBUG 0
#include <png.h>

#define HAVE_STDARG_H
#include "../86box.h"
#include "../device.h"
#include "../video/video.h"
#include "../plat.h"
#include "../plat_dynld.h"
#include "../ui.h"
#include "win.h"
#include "win_d2d.h"


#ifdef USE_D2D
static HWND			d2d_hwnd, old_hwndMain;
static ID2D1Factory		*d2d_factory;
static ID2D1HwndRenderTarget	*d2d_target;
static ID2D1Bitmap		*d2d_buffer;
static int			d2d_width, d2d_height, d2d_screen_width, d2d_screen_height, d2d_fs;
static volatile int		d2d_enabled = 0;
#endif


/* Pointers to the real functions. */
static HRESULT (*D2D1_CreateFactory)(D2D1_FACTORY_TYPE facType,
				     REFIID riid,
				     CONST D2D1_FACTORY_OPTIONS *pFacOptions,
				     void **ppIFactory);
static dllimp_t d2d_imports[] = {
  { "D2D1CreateFactory",	&D2D1_CreateFactory		},
  { NULL,			NULL				}
};


static volatile void		*d2d_handle;	/* handle to WinPcap DLL */


#ifdef ENABLE_D2D_LOG
int d2d_do_log = ENABLE_D2D_LOG;


static void
d2d_log(const char *fmt, ...)
{
	va_list ap;

	if (d2d_do_log) {
	va_start(ap, fmt);
	pclog_ex(fmt, ap);
	va_end(ap);
	}
}
#else
#define d2d_log(fmt, ...)
#endif


#ifdef USE_D2D
static void
d2d_stretch(float *w, float *h, float *x, float *y)
{
	double dw, dh, dx, dy, temp, temp2, ratio_w, ratio_h, gsr, hsr;

	switch (video_fullscreen_scale)
	{
		case FULLSCR_SCALE_FULL:
			*w = d2d_screen_width;
			*h = d2d_screen_height;
			*x = 0;
			*y = 0;
			break;

		case FULLSCR_SCALE_43:
		case FULLSCR_SCALE_KEEPRATIO:
			dw = (double) d2d_screen_width;
			dh = (double) d2d_screen_height;
			hsr = dw / dh;
			if (video_fullscreen_scale == FULLSCR_SCALE_43)
				gsr = 4.0 / 3.0;
			else
				gsr = ((double) *w) / ((double) *h);
			if (gsr <= hsr)
			{
				temp = dh * gsr;
				dx = (dw - temp) / 2.0;
				dw = temp;
				*w = (float) dw;
				*h = (float) dh;
				*x = (float) dx;
				*y = 0;
			}
			else
			{
				temp = dw / gsr;
				dy = (dh - temp) / 2.0;
				dh = temp;
				*w = (float) dw;
				*h = (float) dh;
				*x = 0;
				*y = (float) dy;
			}
			break;

		case FULLSCR_SCALE_INT:
			dw = (double) d2d_screen_width;
			dh = (double) d2d_screen_height;
			temp = ((double) *w);
			temp2 = ((double) *h);
			ratio_w = dw / ((double) *w);
			ratio_h = dh / ((double) *h);
			if (ratio_h < ratio_w)
			{
				ratio_w = ratio_h;
			}
			dx = (dw / 2.0) - ((temp * ratio_w) / 2.0);
			dy = (dh / 2.0) - ((temp2 * ratio_h) / 2.0);
			dw -= (dx * 2.0);
			dh -= (dy * 2.0);
			*w = (float) dw;
			*h = (float) dh;
			*x = (float) dx;
			*y = (float) dy;
			break;
	}
}
#endif


#ifdef USE_D2D
static void
d2d_blit(int x, int y, int y1, int y2, int w, int h)
{
	HRESULT hr = S_OK;

	D2D1_RECT_U rectU;

	float fs_x, fs_y;
	float fs_w = w;
	float fs_h = h;	

	d2d_log("Direct2D: d2d_blit(x=%d, y=%d, y1=%d, y2=%d, w=%d, h=%d)\n", x, y, y1, y2, w, h);

	if (!d2d_enabled) {
		video_blit_complete();
		return;
	}

	if ((w != d2d_width || h != d2d_height) && !d2d_fs)
	{
		hr = d2d_target->Resize(D2D1::SizeU(w, h));

		if (SUCCEEDED(hr))
		{
			d2d_width = w;
			d2d_height = h;
		}
	}

	if (y1 == y2) {
		video_blit_complete();
		return;
	}

	if (render_buffer == NULL) {
		video_blit_complete();
		return;
	}

	/* Create a bitmap to store intermediate data */
	if (d2d_buffer == NULL) {
		if (SUCCEEDED(hr)) {
			hr = d2d_target->CreateBitmap(
				D2D1::SizeU(render_buffer->w, render_buffer->h),
				D2D1::BitmapProperties(D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_IGNORE)),	
				&d2d_buffer);
		}
	}

	/* Copy data from render_buffer */
	if (SUCCEEDED(hr)) {
		rectU = D2D1::RectU(x, y + y1, x + w, y + y2);
		hr = d2d_buffer->CopyFromMemory(
			&rectU,
			&(render_buffer->line[y + y1][x]),
			render_buffer->w << 2);
	}

	video_blit_complete();

	/* Draw! */
	if (SUCCEEDED(hr)) {
		d2d_target->BeginDraw();

		if (d2d_fs) {
			d2d_target->Clear(
				D2D1::ColorF(D2D1::ColorF::Black));

			d2d_stretch(&fs_w, &fs_h, &fs_x, &fs_y);
		}

		d2d_target->DrawBitmap(
			d2d_buffer,
			d2d_fs ? D2D1::RectF(fs_x, fs_y, fs_x + fs_w, fs_y + fs_h) : D2D1::RectF(0, 0, w, h),
			1.0f,
			D2D1_BITMAP_INTERPOLATION_MODE_LINEAR,
			D2D1::RectF(x, y, x + w, y + h));

		hr = d2d_target->EndDraw();
	}

	if (FAILED(hr))
	{
		d2d_log("Direct2D: d2d_blit: error 0x%08lx\n", hr);
	}
}
#endif


void
d2d_close(void)
{
	d2d_log("Direct2D: d2d_close()\n");

	/* Unregister our renderer! */
	video_setblit(NULL);

	if (d2d_enabled)
		d2d_enabled = 0;

#ifdef USE_D2D
	if (d2d_buffer)
	{
		d2d_buffer->Release();
		d2d_buffer = NULL;
	}
	
	if (d2d_target)
	{
		d2d_target->Release();
		d2d_target = NULL;
	}

	if (d2d_factory)
	{
		d2d_factory->Release();
		d2d_factory = NULL;
	}

	if (d2d_hwnd)
	{
		hwndMain = old_hwndMain;
		plat_set_input(hwndMain);
		DestroyWindow(d2d_hwnd);
		d2d_hwnd = NULL;
		old_hwndMain = NULL;
	}

	/* Unload the DLL if possible. */
	if (d2d_handle != NULL) {
		dynld_close((void *)d2d_handle);
		d2d_handle = NULL;
	}
#endif
}


#ifdef USE_D2D
static int
d2d_init_common(int fs)
{
	HRESULT hr = S_OK;
	WCHAR title[200];
	D2D1_HWND_RENDER_TARGET_PROPERTIES props;

	d2d_log("Direct2D: d2d_init_common(fs=%d)\n", fs);

	d2d_handle = dynld_module("d2d1.dll", d2d_imports);

	if (fs)
	{
		d2d_screen_width = GetSystemMetrics(SM_CXSCREEN);
		d2d_screen_height = GetSystemMetrics(SM_CYSCREEN);

		// Direct2D seems to lack any proper fullscreen mode,
		// therefore we just create a full screen window
		// and pass its handle to a HwndRenderTarget
		
		mbstowcs(title, emu_version, sizeof_w(title));

		d2d_hwnd = CreateWindow(
			SUB_CLASS_NAME,
			title,
			WS_POPUP,
			0, 0, d2d_screen_width, d2d_screen_height,
			HWND_DESKTOP,
			NULL,
			hinstance,
			NULL);

		old_hwndMain = hwndMain;
		hwndMain = d2d_hwnd;

		plat_set_input(d2d_hwnd);

		SetFocus(d2d_hwnd);
		SetWindowPos(d2d_hwnd, HWND_TOPMOST, 0, 0, d2d_screen_width, d2d_screen_height, SWP_SHOWWINDOW);	
	}

	hr = D2D1_CreateFactory(D2D1_FACTORY_TYPE_MULTI_THREADED, __uuidof(ID2D1Factory),
				NULL, reinterpret_cast <void **>(&d2d_factory));

	if (fs)
	{
		props = D2D1::HwndRenderTargetProperties(d2d_hwnd,
			D2D1::SizeU(d2d_screen_width, d2d_screen_height));
	}
	else
	{
		// HwndRenderTarget will get resized appropriately by d2d_blit,
		// so it's fine to let D2D imply size of 0x0 for now
		props = D2D1::HwndRenderTargetProperties(hwndRender);
	}

	if (SUCCEEDED(hr))
	{
		hr = d2d_factory->CreateHwndRenderTarget(
			D2D1::RenderTargetProperties(),
			props,
			&d2d_target);
	}	

	if (SUCCEEDED(hr)) 
	{
		d2d_fs = fs;

		d2d_width = 0;
		d2d_height = 0;

		// Make sure we get a clean exit.
		atexit(d2d_close);

		// Register our renderer!
		video_setblit(d2d_blit);
	}

	if (FAILED(hr))
	{
		d2d_log("Direct2D: d2d_init_common: error 0x%08lx\n", hr);
		d2d_close();
		return(0);
	}

	d2d_enabled = 1;

	return(1);
}
#endif


int
d2d_init(HWND h)
{
	d2d_log("Direct2D: d2d_init(h=0x%08lx)\n", h);

#ifdef USE_D2D
	return d2d_init_common(0);
#else
	return(0);
#endif
}


int
d2d_init_fs(HWND h)
{
	d2d_log("Direct2D: d2d_init_fs(h=0x%08lx)\n", h);

#ifdef USE_D2D
	return d2d_init_common(1);
#else
	return(0);
#endif
}


int
d2d_pause(void)
{
	// Not implemented in any renderer. The heck is this even for?

	d2d_log("Direct2D: d2d_pause()\n");
	return(0);
}


void
d2d_enable(int enable)
{
    d2d_enabled = enable;
}
