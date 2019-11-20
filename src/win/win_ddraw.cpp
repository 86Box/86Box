/*
 * 86Box	A hypervisor and IBM PC system emulator that specializes in
 *		running old operating systems and software designed for IBM
 *		PC systems and compatibles from 1981 through fairly recent
 *		system designs based on the PCI bus.
 *
 *		This file is part of the 86Box distribution.
 *
 *		Rendering module for Microsoft DirectDraw 9.
 *
 * NOTES:	This code should be re-merged into a single init() with a
 *		'fullscreen' argument, indicating FS mode is requested.
 *
 * Version:	@(#)win_ddraw.cpp	1.0.16	2019/11/01
 *
 * Authors:	Sarah Walker, <http://pcem-emulator.co.uk/>
 *		Miran Grca, <mgrca8@gmail.com>
 *		Fred N. van Kempen, <decwiz@yahoo.com>
 *
 *		Copyright 2008-2019 Sarah Walker.
 *		Copyright 2016-2019 Miran Grca.
 *		Copyright 2017-2019 Fred N. van Kempen.
 */
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#define UNICODE
#define BITMAP WINDOWS_BITMAP
#include <windows.h>
#undef BITMAP

#define HAVE_STDARG_H
#include "../86box.h"
#include "../device.h"
#include "../video/video.h"
#include "../plat.h"
#include "../ui.h"
#include "win_ddraw.h"
#include "win.h"


static LPDIRECTDRAW		lpdd = NULL;
static LPDIRECTDRAW4		lpdd4 = NULL;
static LPDIRECTDRAWSURFACE4	lpdds_pri = NULL,
				lpdds_back = NULL,
				lpdds_back2 = NULL;
static LPDIRECTDRAWCLIPPER	lpdd_clipper = NULL;
static DDSURFACEDESC2		ddsd;
static HWND			ddraw_hwnd;
static int			ddraw_w, ddraw_h;
static int			ddraw_fs;
static volatile int		ddraw_enabled = 0;


#ifdef ENABLE_DDRAW_LOG
int ddraw_do_log = ENABLE_DDRAW_LOG;


static void
ddraw_log(const char *fmt, ...)
{
    va_list ap;

    if (ddraw_do_log) {
	va_start(ap, fmt);
	pclog_ex(fmt, ap);
	va_end(ap);
    }
}
#else
#define ddraw_log(fmt, ...)
#endif


static void
ddraw_fs_size_default(RECT w_rect, RECT *r_dest)
{
	r_dest->left   = 0;
	r_dest->top    = 0;
	r_dest->right  = (w_rect.right  - w_rect.left) - 1;
	r_dest->bottom = (w_rect.bottom - w_rect.top) - 1;
}


static void
ddraw_fs_size(RECT w_rect, RECT *r_dest, int w, int h)
{
    int ratio_w, ratio_h;
    double hsr, gsr, d, sh, sw, wh, ww, mh, mw;

    ddraw_log("video_fullscreen_scale = %i\n", video_fullscreen_scale);

    sh = (double) (w_rect.bottom - w_rect.top);
    sw = (double) (w_rect.right - w_rect.left);
    wh = (double) h;
    ww = (double) w;

    switch (video_fullscreen_scale) {
	case FULLSCR_SCALE_FULL:
		ddraw_fs_size_default(w_rect, r_dest);
		break;

	case FULLSCR_SCALE_43:
	case FULLSCR_SCALE_KEEPRATIO:
		if (video_fullscreen_scale == FULLSCR_SCALE_43) {
			mw = 4.0;
			mh = 3.0;
		} else {
			mw = ww;
			mh = wh;
		}

		hsr = sw / sh;
		gsr = mw / mh;

		if (hsr > gsr) {
			/* Host ratio is bigger than guest ratio. */
			d = (sw - (mw * (sh / mh))) / 2.0;

			r_dest->left   = (int) d;
			r_dest->right  = (int) (sw - d - 1.0);
			r_dest->top    = 0;
			r_dest->bottom = (int) (sh - 1.0);
		} else if (hsr < gsr) {
			/* Host ratio is smaller or rqual than guest ratio. */
			d = (sh - (mh * (sw / mw))) / 2.0;

			r_dest->left   = 0;
			r_dest->right  = (int) (sw - 1.0);
			r_dest->top    = (int) d;
			r_dest->bottom = (int) (sh - d - 1.0);
		} else {
			/* Host ratio is equal to guest ratio. */
			ddraw_fs_size_default(w_rect, r_dest);
		}
		break;

	case FULLSCR_SCALE_INT:
		ratio_w = (w_rect.right  - w_rect.left) / w;
		ratio_h = (w_rect.bottom - w_rect.top)  / h;
		if (ratio_h < ratio_w)
			ratio_w = ratio_h;
		r_dest->left   = ((w_rect.right  - w_rect.left) / 2) - ((w * ratio_w) / 2);
		r_dest->right  = ((w_rect.right  - w_rect.left) / 2) + ((w * ratio_w) / 2) - 1;
		r_dest->top    = ((w_rect.bottom - w_rect.top)  / 2) - ((h * ratio_w) / 2);
		r_dest->bottom = ((w_rect.bottom - w_rect.top)  / 2) + ((h * ratio_w) / 2) - 1;
		break;
    }
}


static void
ddraw_blit(int x, int y, int y1, int y2, int w, int h)
{
    RECT r_src;
    RECT r_dest;
    POINT po;
    RECT w_rect;
    int yy;
    HRESULT hr;
    DDBLTFX ddbltfx;
    RECT *r_tgt = ddraw_fs ? &r_dest : &r_src;

    if (!ddraw_enabled) {
	video_blit_complete();
	return;
    }

    if (lpdds_back == NULL) {
	video_blit_complete();
	return; /*Nothing to do*/
    }

    if ((y1 == y2) || (h <= 0)) {
	video_blit_complete();
	return;
    }

    memset(&ddsd, 0, sizeof(ddsd));
    ddsd.dwSize = sizeof(ddsd);

    hr = lpdds_back->Lock(NULL, &ddsd,
			  DDLOCK_SURFACEMEMORYPTR | DDLOCK_WAIT, NULL);
    if (hr == DDERR_SURFACELOST) {
	lpdds_back->Restore();
	lpdds_back->Lock(NULL, &ddsd,
			 DDLOCK_SURFACEMEMORYPTR | DDLOCK_WAIT, NULL);
	device_force_redraw();
    }

    if (! ddsd.lpSurface) {
	video_blit_complete();
	return;
    }

    for (yy = y1; yy < y2; yy++) {
	if (buffer32) {
		if ((y + yy) >= 0 && (y + yy) < buffer32->h) {
			if (video_grayscale || invert_display)
				video_transform_copy((uint32_t *) &(((uint8_t *) ddsd.lpSurface)[yy * ddsd.lPitch]), &(buffer32->line[y + yy][x]), w);
			else
				memcpy((uint32_t *) &(((uint8_t *) ddsd.lpSurface)[yy * ddsd.lPitch]), &(buffer32->line[y + yy][x]), w * 4);
		}
	}
    }

    video_blit_complete();
    lpdds_back->Unlock(NULL);

    if (ddraw_fs) {
	w_rect.left = 0;
	w_rect.top = 0;
	w_rect.right = ddraw_w;
	w_rect.bottom = ddraw_h;
	ddraw_fs_size(w_rect, &r_dest, w, h);
    } else {
	po.x = po.y = 0;

	ClientToScreen(ddraw_hwnd, &po);
	GetClientRect(ddraw_hwnd, &r_dest);
	OffsetRect(&r_dest, po.x, po.y);	
    }

    r_src.left   = 0;
    r_src.top    = 0;       
    r_src.right  = w;
    r_src.bottom = h;

    if (ddraw_fs) {
	ddbltfx.dwSize = sizeof(ddbltfx);
	ddbltfx.dwFillColor = 0;

	lpdds_back2->Blt(&w_rect, NULL, NULL,
			 DDBLT_WAIT | DDBLT_COLORFILL, &ddbltfx);
    }

    hr = lpdds_back2->Blt(r_tgt, lpdds_back, &r_src, DDBLT_WAIT, NULL);
    if (hr == DDERR_SURFACELOST) {
	lpdds_back2->Restore();
	lpdds_back2->Blt(r_tgt, lpdds_back, &r_src, DDBLT_WAIT, NULL);
    }

    if (ddraw_fs)
	hr = lpdds_pri->Flip(NULL, DDFLIP_NOVSYNC);	
    else {
	lpdds_back2->Unlock(NULL);

	hr = lpdds_pri->Blt(&r_dest, lpdds_back2, &r_src, DDBLT_WAIT, NULL);
    }

    if (hr == DDERR_SURFACELOST) {
	lpdds_pri->Restore();
	if (ddraw_fs)
		lpdds_pri->Flip(NULL, DDFLIP_NOVSYNC);
	else
		lpdds_pri->Blt(&r_dest, lpdds_back2, &r_src, DDBLT_WAIT, NULL);
    }
}


int
ddraw_init(HWND h)
{
    if (FAILED(DirectDrawCreate(NULL, &lpdd, NULL))) return(0);

    if (FAILED(lpdd->QueryInterface(IID_IDirectDraw4, (LPVOID *)&lpdd4)))
					return(0);

    lpdd->Release();
    lpdd = NULL;

    atexit(ddraw_close);

    if (FAILED(lpdd4->SetCooperativeLevel(h, DDSCL_NORMAL))) return(0);

    memset(&ddsd, 0, sizeof(ddsd));
    ddsd.dwSize = sizeof(ddsd);

    ddsd.dwFlags = DDSD_CAPS;
    ddsd.ddsCaps.dwCaps = DDSCAPS_PRIMARYSURFACE;
    if (FAILED(lpdd4->CreateSurface(&ddsd, &lpdds_pri, NULL))) return(0);

    ddsd.dwFlags = DDSD_CAPS | DDSD_WIDTH | DDSD_HEIGHT;
    ddsd.dwWidth  = 2048;
    ddsd.dwHeight = 2048;
    ddsd.ddsCaps.dwCaps = DDSCAPS_OFFSCREENPLAIN | DDSCAPS_VIDEOMEMORY;
    if (FAILED(lpdd4->CreateSurface(&ddsd, &lpdds_back, NULL))) {
	ddsd.dwFlags = DDSD_CAPS | DDSD_WIDTH | DDSD_HEIGHT;
	ddsd.dwWidth  = 2048;
	ddsd.dwHeight = 2048;
	ddsd.ddsCaps.dwCaps = DDSCAPS_OFFSCREENPLAIN | DDSCAPS_SYSTEMMEMORY;
	if (FAILED(lpdd4->CreateSurface(&ddsd, &lpdds_back, NULL)))
				fatal("CreateSurface back failed\n");
    }

    memset(&ddsd, 0, sizeof(ddsd));
    ddsd.dwSize = sizeof(ddsd);
    ddsd.dwFlags = DDSD_CAPS | DDSD_WIDTH | DDSD_HEIGHT;
    ddsd.dwWidth  = 2048;
    ddsd.dwHeight = 2048;
    ddsd.ddsCaps.dwCaps = DDSCAPS_OFFSCREENPLAIN | DDSCAPS_VIDEOMEMORY;
    if (FAILED(lpdd4->CreateSurface(&ddsd, &lpdds_back2, NULL))) {
	ddsd.dwFlags = DDSD_CAPS | DDSD_WIDTH | DDSD_HEIGHT;
	ddsd.dwWidth  = 2048;
	ddsd.dwHeight = 2048;
	ddsd.ddsCaps.dwCaps = DDSCAPS_OFFSCREENPLAIN | DDSCAPS_SYSTEMMEMORY;
	if (FAILED(lpdd4->CreateSurface(&ddsd, &lpdds_back2, NULL)))
				fatal("CreateSurface back failed\n");
    }

    if (FAILED(lpdd4->CreateClipper(0, &lpdd_clipper, NULL))) return(0);

    if (FAILED(lpdd_clipper->SetHWnd(0, h))) return(0);

    if (FAILED(lpdds_pri->SetClipper(lpdd_clipper))) return(0);

    ddraw_hwnd = h;

    video_setblit(ddraw_blit);

    ddraw_fs = 0;
    ddraw_enabled = 1;

    return(1);
}


int
ddraw_init_fs(HWND h)
{
    ddraw_w = GetSystemMetrics(SM_CXSCREEN);
    ddraw_h = GetSystemMetrics(SM_CYSCREEN);

    if (FAILED(DirectDrawCreate(NULL, &lpdd, NULL))) return 0;

    if (FAILED(lpdd->QueryInterface(IID_IDirectDraw4, (LPVOID *)&lpdd4))) return 0;

    lpdd->Release();
    lpdd = NULL;

    atexit(ddraw_close);

    if (FAILED(lpdd4->SetCooperativeLevel(h,
					  DDSCL_SETFOCUSWINDOW | \
					  DDSCL_CREATEDEVICEWINDOW | \
					  DDSCL_EXCLUSIVE | \
					  DDSCL_FULLSCREEN | \
					  DDSCL_ALLOWREBOOT))) return 0;

    if (FAILED(lpdd4->SetDisplayMode(ddraw_w, ddraw_h, 32, 0 ,0))) return 0;

    memset(&ddsd, 0, sizeof(ddsd));
    ddsd.dwSize = sizeof(ddsd);
    ddsd.dwFlags = DDSD_CAPS | DDSD_BACKBUFFERCOUNT;
    ddsd.dwBackBufferCount = 1;
    ddsd.ddsCaps.dwCaps = DDSCAPS_PRIMARYSURFACE | DDSCAPS_COMPLEX | DDSCAPS_FLIP;
    if (FAILED(lpdd4->CreateSurface(&ddsd, &lpdds_pri, NULL))) return 0;

    ddsd.ddsCaps.dwCaps = DDSCAPS_BACKBUFFER;
    if (FAILED(lpdds_pri->GetAttachedSurface(&ddsd.ddsCaps, &lpdds_back2))) return 0;

    memset(&ddsd, 0, sizeof(ddsd));
    ddsd.dwSize = sizeof(ddsd);
    ddsd.dwFlags = DDSD_CAPS | DDSD_WIDTH | DDSD_HEIGHT;
    ddsd.dwWidth  = 2048;
    ddsd.dwHeight = 2048;
    ddsd.ddsCaps.dwCaps = DDSCAPS_OFFSCREENPLAIN | DDSCAPS_VIDEOMEMORY;
    if (FAILED(lpdd4->CreateSurface(&ddsd, &lpdds_back, NULL))) {
	ddsd.dwFlags = DDSD_CAPS | DDSD_WIDTH | DDSD_HEIGHT;
	ddsd.dwWidth  = 2048;
	ddsd.dwHeight = 2048;
	ddsd.ddsCaps.dwCaps = DDSCAPS_OFFSCREENPLAIN | DDSCAPS_SYSTEMMEMORY;
	if (FAILED(lpdd4->CreateSurface(&ddsd, &lpdds_back, NULL))) return 0;
    }

    ddraw_hwnd = h;

    video_setblit(ddraw_blit);

    ddraw_fs = 1;
    ddraw_enabled = 1;

    return(1);
}


void
ddraw_close(void)
{
    video_setblit(NULL);

    if (ddraw_enabled)
	ddraw_enabled = 0;

    if (lpdds_back2) {
	lpdds_back2->Release();
	lpdds_back2 = NULL;
    }
    if (lpdds_back) {
	lpdds_back->Release();
	lpdds_back = NULL;
    }
    if (lpdds_pri) {
	lpdds_pri->Release();
	lpdds_pri = NULL;
    }
    if (lpdd_clipper) {
	lpdd_clipper->Release();
	lpdd_clipper = NULL;
    }
    if (lpdd4) {
	lpdd4->Release();
	lpdd4 = NULL;
    }
}


int
ddraw_pause(void)
{
    return(0);
}


void
ddraw_enable(int enable)
{
    ddraw_enabled = enable;
}
