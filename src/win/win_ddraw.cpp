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
 * Version:	@(#)win_ddraw.cpp	1.0.5	2018/01/15
 *
 * Authors:	Sarah Walker, <http://pcem-emulator.co.uk/>
 *		Miran Grca, <mgrca8@gmail.com>
 *		Fred N. van Kempen, <decwiz@yahoo.com>
 *
 *		Copyright 2008-2018 Sarah Walker.
 *		Copyright 2016-2018 Miran Grca.
 *		Copyright 2017,2018 Fred N. van Kempen.
 */
#include <stdio.h>
#include <stdint.h>
#define UNICODE
#define BITMAP WINDOWS_BITMAP
#include <windows.h>
#undef BITMAP
#include "../86box.h"
#include "../device.h"
#include "../video/video.h"
#include "../plat.h"
#include "../ui.h"
#include "win_ddraw.h"
#include "win_png.h"
#include "win.h"


static LPDIRECTDRAW		lpdd = NULL;
static LPDIRECTDRAW4		lpdd4 = NULL;
static LPDIRECTDRAWSURFACE4	lpdds_pri = NULL,
				lpdds_back = NULL,
				lpdds_back2 = NULL;
static LPDIRECTDRAWCLIPPER	lpdd_clipper = NULL;
static DDSURFACEDESC2		ddsd;
static HWND			ddraw_hwnd;
static HBITMAP			hbitmap;
static int			ddraw_w, ddraw_h,
				xs, ys, ys2;


static void
CopySurface(IDirectDrawSurface4 *pDDSurface)
{ 
    HDC hdc, hmemdc;
    HBITMAP hprevbitmap;
    DDSURFACEDESC2 ddsd2;

    pDDSurface->GetDC(&hdc);
    hmemdc = CreateCompatibleDC(hdc); 
    ZeroMemory(&ddsd2 ,sizeof( ddsd2 )); // better to clear before using
    ddsd2.dwSize = sizeof( ddsd2 ); //initialize with size 
    pDDSurface->GetSurfaceDesc(&ddsd2);
    hbitmap = CreateCompatibleBitmap( hdc ,xs ,ys);
    hprevbitmap = (HBITMAP) SelectObject( hmemdc, hbitmap );
    BitBlt(hmemdc,0 ,0 ,xs ,ys ,hdc ,0 ,0,SRCCOPY);    
    SelectObject(hmemdc,hprevbitmap); // restore the old bitmap 
    DeleteDC(hmemdc);
    pDDSurface->ReleaseDC(hdc);
}


static void
DoubleLines(uint8_t *dst, uint8_t *src)
{
    int i = 0;

    for (i = 0; i < ys; i++) {
	memcpy(dst + (i * xs * 8), src + (i * xs * 4), xs * 4);
	memcpy(dst + ((i * xs * 8) + (xs * 4)), src + (i * xs * 4), xs * 4);
    }
}


static void
SaveBitmap(wchar_t *szFilename, HBITMAP hBitmap)
{
    static WCHAR szMessage[512];
    BITMAPFILEHEADER bmpFileHeader; 
    BITMAPINFO bmpInfo;
    HDC hdc;
    FILE *fp = NULL;
    LPVOID pBuf = NULL;
    LPVOID pBuf2 = NULL;

    do { 
	hdc = GetDC(NULL);

	ZeroMemory(&bmpInfo, sizeof(BITMAPINFO));
	bmpInfo.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);

	GetDIBits(hdc, hBitmap, 0, 0, NULL, &bmpInfo, DIB_RGB_COLORS); 

	if (bmpInfo.bmiHeader.biSizeImage <= 0)
		bmpInfo.bmiHeader.biSizeImage =
			bmpInfo.bmiHeader.biWidth*abs(bmpInfo.bmiHeader.biHeight)*(bmpInfo.bmiHeader.biBitCount+7)/8;

	if ((pBuf = malloc(bmpInfo.bmiHeader.biSizeImage)) == NULL) {
//		pclog("ERROR: Unable to Allocate Bitmap Memory");
		break;
	}

	if (ys2 <= 250)
		pBuf2 = malloc(bmpInfo.bmiHeader.biSizeImage * 2);

	bmpInfo.bmiHeader.biCompression = BI_RGB;

	GetDIBits(hdc, hBitmap, 0, bmpInfo.bmiHeader.biHeight, pBuf, &bmpInfo, DIB_RGB_COLORS);

	if ((fp = _wfopen(szFilename, L"wb")) == NULL) {
		_swprintf(szMessage,
			  plat_get_string(IDS_2088), szFilename);
		ui_msgbox(MBX_ERROR, szMessage);
		break;
	} 

	bmpFileHeader.bfReserved1 = 0;
	bmpFileHeader.bfReserved2 = 0;
	if (pBuf2) {
		bmpInfo.bmiHeader.biSizeImage <<= 1;
		bmpInfo.bmiHeader.biHeight <<= 1;
	}
	bmpFileHeader.bfSize=sizeof(BITMAPFILEHEADER)+sizeof(BITMAPINFOHEADER)+bmpInfo.bmiHeader.biSizeImage;
	bmpFileHeader.bfType=0x4D42;
	bmpFileHeader.bfOffBits=sizeof(BITMAPFILEHEADER)+sizeof(BITMAPINFOHEADER); 

	(void)fwrite(&bmpFileHeader,sizeof(BITMAPFILEHEADER),1,fp);
	(void)fwrite(&bmpInfo.bmiHeader,sizeof(BITMAPINFOHEADER),1,fp);
	if (pBuf2) {
		DoubleLines((uint8_t *) pBuf2, (uint8_t *) pBuf);
		(void)fwrite(pBuf2,bmpInfo.bmiHeader.biSizeImage,1,fp); 
	} else {
		(void)fwrite(pBuf,bmpInfo.bmiHeader.biSizeImage,1,fp); 
	}
    } while(false); 

    if (hdc) ReleaseDC(NULL,hdc); 

    if (pBuf2) free(pBuf2); 

    if (pBuf) free(pBuf); 

    if (fp) fclose(fp);
}


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
    double hsr, gsr, ra, d;

    pclog("video_fullscreen_scale = %i\n", video_fullscreen_scale);

    switch (video_fullscreen_scale) {
	case FULLSCR_SCALE_FULL:
		ddraw_fs_size_default(w_rect, r_dest);
		break;

	case FULLSCR_SCALE_43:
		r_dest->top    = 0;
		r_dest->bottom = (w_rect.bottom - w_rect.top) - 1;
		r_dest->left   = ((w_rect.right  - w_rect.left) / 2) - (((w_rect.bottom - w_rect.top) * 4) / (3 * 2));
		r_dest->right  = ((w_rect.right  - w_rect.left) / 2) + (((w_rect.bottom - w_rect.top) * 4) / (3 * 2)) - 1;
		if (r_dest->left < 0) {
			r_dest->left   = 0;
			r_dest->right  = (w_rect.right  - w_rect.left) - 1;
			r_dest->top    = ((w_rect.bottom - w_rect.top) / 2) - (((w_rect.right - w_rect.left) * 3) / (4 * 2));
			r_dest->bottom = ((w_rect.bottom - w_rect.top) / 2) + (((w_rect.right - w_rect.left) * 3) / (4 * 2)) - 1;
		}
		break;

	case FULLSCR_SCALE_SQ:
		r_dest->top    = 0;
		r_dest->bottom = (w_rect.bottom - w_rect.top) - 1;
		r_dest->left   = ((w_rect.right  - w_rect.left) / 2) - (((w_rect.bottom - w_rect.top) * w) / (h * 2));
		r_dest->right  = ((w_rect.right  - w_rect.left) / 2) + (((w_rect.bottom - w_rect.top) * w) / (h * 2)) - 1;
		if (r_dest->left < 0) {
			r_dest->left   = 0;
			r_dest->right  = (w_rect.right  - w_rect.left) - 1;
			r_dest->top    = ((w_rect.bottom - w_rect.top) / 2) - (((w_rect.right - w_rect.left) * h) / (w * 2));
			r_dest->bottom = ((w_rect.bottom - w_rect.top) / 2) + (((w_rect.right - w_rect.left) * h) / (w * 2)) - 1;
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

	case FULLSCR_SCALE_KEEPRATIO:
		hsr = ((double) (w_rect.right  - w_rect.left)) / ((double) (w_rect.bottom - w_rect.top));
		gsr = ((double) w) / ((double) h);

		if (hsr > gsr) {
			/* Host ratio is bigger than guest ratio. */
			ra = ((double) (w_rect.bottom - w_rect.top)) / ((double) h);

			d = ((double) w) * ra;
			d = (((double) (w_rect.right  - w_rect.left)) - d) / 2.0;

			r_dest->left   = ((int) d);
			r_dest->right  = (w_rect.right  - w_rect.left) - ((int) d) - 1;
			r_dest->top    = 0;
			r_dest->bottom = (w_rect.bottom - w_rect.top)  - 1;
		} else if (hsr < gsr) {
			/* Host ratio is smaller or rqual than guest ratio. */
			ra = ((double) (w_rect.right  - w_rect.left)) / ((double) w);

			d = ((double) h) * ra;
			d = (((double) (w_rect.bottom - w_rect.top)) - d) / 2.0;

			r_dest->left   = 0;
			r_dest->right  = (w_rect.right  - w_rect.left) - 1;
			r_dest->top    = ((int) d);
			r_dest->bottom = (w_rect.bottom - w_rect.top)  - ((int) d) - 1;
		} else {
			/* Host ratio is equal to guest ratio. */
			ddraw_fs_size_default(w_rect, r_dest);
		}
		break;
    }
}


static void
ddraw_blit_fs(int x, int y, int y1, int y2, int w, int h)
{
    RECT r_src;
    RECT r_dest;
    RECT w_rect;
    int yy;
    HRESULT hr;
    DDBLTFX ddbltfx;

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

    for (yy = y1; yy < y2; yy++)
	if (buffer32)  memcpy((void *)((uintptr_t)ddsd.lpSurface + (yy * ddsd.lPitch)), &(((uint32_t *)buffer32->line[y + yy])[x]), w * 4);
    video_blit_complete();
    lpdds_back->Unlock(NULL);

    w_rect.left = 0;
    w_rect.top = 0;
    w_rect.right = ddraw_w;
    w_rect.bottom = ddraw_h;
    ddraw_fs_size(w_rect, &r_dest, w, h);

    r_src.left   = 0;
    r_src.top    = 0;       
    r_src.right  = w;
    r_src.bottom = h;

    ddbltfx.dwSize = sizeof(ddbltfx);
    ddbltfx.dwFillColor = 0;

    lpdds_back2->Blt(&w_rect, NULL, NULL,
		     DDBLT_WAIT | DDBLT_COLORFILL, &ddbltfx);

    hr = lpdds_back2->Blt(&r_dest, lpdds_back, &r_src, DDBLT_WAIT, NULL);
    if (hr == DDERR_SURFACELOST) {
	lpdds_back2->Restore();
	lpdds_back2->Blt(&r_dest, lpdds_back, &r_src, DDBLT_WAIT, NULL);
    }
	
    hr = lpdds_pri->Flip(NULL, DDFLIP_NOVSYNC);	
    if (hr == DDERR_SURFACELOST) {
	lpdds_pri->Restore();
	lpdds_pri->Flip(NULL, DDFLIP_NOVSYNC);
    }
}


static void
ddraw_blit(int x, int y, int y1, int y2, int w, int h)
{
    RECT r_src;
    RECT r_dest;
    POINT po;
    HRESULT hr;
    int yy;

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
	if (buffer32)
		if ((y + yy) >= 0 && (y + yy) < buffer32->h)
			memcpy((uint32_t *) &(((uint8_t *) ddsd.lpSurface)[yy * ddsd.lPitch]), &(((uint32_t *)buffer32->line[y + yy])[x]), w * 4);
    }
    video_blit_complete();
    lpdds_back->Unlock(NULL);

    po.x = po.y = 0;
	
    ClientToScreen(ddraw_hwnd, &po);
    GetClientRect(ddraw_hwnd, &r_dest);
    OffsetRect(&r_dest, po.x, po.y);	
	
    r_src.left   = 0;
    r_src.top    = 0;       
    r_src.right  = w;
    r_src.bottom = h;

    hr = lpdds_back2->Blt(&r_src, lpdds_back, &r_src, DDBLT_WAIT, NULL);
    if (hr == DDERR_SURFACELOST) {
	lpdds_back2->Restore();
	lpdds_back2->Blt(&r_src, lpdds_back, &r_src, DDBLT_WAIT, NULL);
    }

    lpdds_back2->Unlock(NULL);
	
    hr = lpdds_pri->Blt(&r_dest, lpdds_back2, &r_src, DDBLT_WAIT, NULL);
    if (hr == DDERR_SURFACELOST) {
	lpdds_pri->Restore();
	lpdds_pri->Blt(&r_dest, lpdds_back2, &r_src, DDBLT_WAIT, NULL);
    }
}


void
ddraw_take_screenshot(wchar_t *fn)
{
#if 0
    xs = xsize;
    ys = ys2 = ysize;

    /* For EGA/(S)VGA, the size is NOT adjusted for overscan. */
    if ((overscan_y > 16) && enable_overscan) {
	xs += overscan_x;
	ys += overscan_y;
    }

    /* For CGA, the width is adjusted for overscan, but the height is not. */
    if (overscan_y == 16) {
	if (ys2 <= 250)
		ys += (overscan_y >> 1);
	  else
		ys += overscan_y;
    }
#endif

    xs = get_actual_size_x();
    ys = ys2 = get_actual_size_y();

    if (ysize <= 250) {
	ys >>= 1;
	ys2 >>= 1;
    }

    CopySurface(lpdds_back2);

    SaveBitmap(fn, hbitmap);
}


int
ddraw_init(HWND h)
{
    cgapal_rebuild();

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

    return(1);
}


int
ddraw_init_fs(HWND h)
{
    ddraw_w = GetSystemMetrics(SM_CXSCREEN);
    ddraw_h = GetSystemMetrics(SM_CYSCREEN);

    cgapal_rebuild();

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

    video_setblit(ddraw_blit_fs);

    return(1);
}


void
ddraw_close(void)
{
    video_setblit(NULL);

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
