/* Copyright holders: Sarah Walker, Tenshi
   see COPYING for more details
*/
#include <stdio.h>
#include <stdint.h>
#define UNICODE
#define BITMAP WINDOWS_BITMAP
#include <windows.h>
#undef BITMAP
#include "../video/video.h"
#include "win_ddraw.h"
#include "win_cgapal.h"
#include "win.h"
#include "win_language.h"


extern "C" void fatal(const char *format, ...);
extern "C" void pclog(const char *format, ...);

extern "C" void device_force_redraw(void);

extern "C" int ddraw_init(HWND h);
extern "C" void ddraw_close(void);
 
extern "C" void video_blit_complete(void);


static LPDIRECTDRAW  lpdd  = NULL;
static LPDIRECTDRAW7 lpdd7 = NULL;
static LPDIRECTDRAWSURFACE7 lpdds_pri = NULL;
static LPDIRECTDRAWSURFACE7 lpdds_back = NULL;
static LPDIRECTDRAWSURFACE7 lpdds_back2 = NULL;
static LPDIRECTDRAWCLIPPER lpdd_clipper = NULL;
static DDSURFACEDESC2 ddsd;
static HWND ddraw_hwnd;
static HBITMAP hbitmap;
static int xs, ys, ys2;


static void
CopySurface(IDirectDrawSurface7 *pDDSurface)
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
			  win_language_get_string_from_id(IDS_2088), szFilename);
		msgbox_error_wstr(hwndMain, szMessage);
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


void
ddraw_common_take_screenshot(wchar_t *fn, IDirectDrawSurface7 *pDDSurface)
{
	xs = xsize;
	ys = ys2 = ysize;
	/* For EGA/(S)VGA, the size is NOT adjusted for overscan. */
	if ((overscan_y > 16) && enable_overscan)
	{
		xs += overscan_x;
		ys += overscan_y;
	}
	/* For CGA, the width is adjusted for overscan, but the height is not. */
	if (overscan_y == 16)
	{
		if (ys2 <= 250)
			ys += (overscan_y >> 1);
		else
			ys += overscan_y;
	}
	CopySurface(pDDSurface);
	SaveBitmap(fn, hbitmap);
}


static void
ddraw_blit_memtoscreen(int x, int y, int y1, int y2, int w, int h)
{
    RECT r_src;
    RECT r_dest;
    int yy;
    POINT po;
    HRESULT hr;

//    pclog("Blit memtoscreen %i,%i %i %i %i,%i\n", x, y, y1, y2, w, h);

    if (lpdds_back == NULL) {
	video_blit_complete();
	return; /*Nothing to do*/
    }

    if (h <= 0) {
	video_blit_complete();
	return;
    }

    memset(&ddsd, 0, sizeof(ddsd));
    ddsd.dwSize = sizeof(ddsd);

    hr = lpdds_back->Lock(NULL, &ddsd, DDLOCK_SURFACEMEMORYPTR | DDLOCK_WAIT, NULL);
    if (hr == DDERR_SURFACELOST) {
	lpdds_back->Restore();
	lpdds_back->Lock(NULL, &ddsd, DDLOCK_SURFACEMEMORYPTR | DDLOCK_WAIT, NULL);
	device_force_redraw();
    }
    if (!ddsd.lpSurface) {
	video_blit_complete();
	return;
    }

    for (yy = y1; yy < y2; yy++) {
	if ((y + yy) >= 0 && (y + yy) < buffer->h)
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
	
//    pclog("Blit from %i,%i %i,%i to %i,%i %i,%i\n", r_src.left, r_src.top, r_src.right, r_src.bottom, r_dest.left, r_dest.top, r_dest.right, r_dest.bottom);
    hr = lpdds_pri->Blt(&r_dest, lpdds_back2, &r_src, DDBLT_WAIT, NULL);
    if (hr == DDERR_SURFACELOST) {
	lpdds_pri->Restore();
	lpdds_pri->Blt(&r_dest, lpdds_back2, &r_src, DDBLT_WAIT, NULL);
    }
}


static void
ddraw_blit_memtoscreen_8(int x, int y, int w, int h)
{
    RECT r_src;
    RECT r_dest;
    int xx, yy;
    POINT po;
    uint32_t *p;
    HRESULT hr;

    if (lpdds_back == NULL) {
	video_blit_complete();
	return; /*Nothing to do*/
    }

    memset(&ddsd, 0, sizeof(ddsd));
    ddsd.dwSize = sizeof(ddsd);

    hr = lpdds_back->Lock(NULL, &ddsd, DDLOCK_SURFACEMEMORYPTR | DDLOCK_WAIT, NULL);
    if (hr == DDERR_SURFACELOST) {
	lpdds_back->Restore();
	lpdds_back->Lock(NULL, &ddsd, DDLOCK_SURFACEMEMORYPTR | DDLOCK_WAIT, NULL);
	device_force_redraw();
    }
    if (!ddsd.lpSurface) {
	video_blit_complete();
	return;
    }

    for (yy = 0; yy < h; yy++) {
	if ((y + yy) >= 0 && (y + yy) < buffer->h) {
		p = (uint32_t *) &(((uint8_t *) ddsd.lpSurface)[yy * ddsd.lPitch]);
		for (xx = 0; xx < w; xx++)
			p[xx] = pal_lookup[buffer->line[y + yy][x + xx]];
	}
    }
    p = &(((uint32_t *) ddsd.lpSurface)[4 * ddsd.lPitch]);
    lpdds_back->Unlock(NULL);
    video_blit_complete();
	
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

    hr = lpdds_pri->Blt(&r_dest, lpdds_back2, &r_src, DDBLT_WAIT, NULL);
    if (hr == DDERR_SURFACELOST) {
	lpdds_pri->Restore();
	hr = lpdds_pri->Blt(&r_dest, lpdds_back2, &r_src, DDBLT_WAIT, NULL);
    }
}


int
ddraw_init(HWND h)
{
#if NO_THIS_CRASHES_NOW
    cgapal_rebuild();
#endif

    if (FAILED(DirectDrawCreate(NULL, &lpdd, NULL))) return(0);

    if (FAILED(lpdd->QueryInterface(IID_IDirectDraw7, (LPVOID *)&lpdd7)))
					return(0);

    lpdd->Release();
    lpdd = NULL;

    atexit(ddraw_close);

    if (FAILED(lpdd7->SetCooperativeLevel(h, DDSCL_NORMAL))) return(0);

    memset(&ddsd, 0, sizeof(ddsd));
    ddsd.dwSize = sizeof(ddsd);

    ddsd.dwFlags = DDSD_CAPS;
    ddsd.ddsCaps.dwCaps = DDSCAPS_PRIMARYSURFACE;
    if (FAILED(lpdd7->CreateSurface(&ddsd, &lpdds_pri, NULL))) return(0);

    // memset(&ddsd, 0, sizeof(ddsd));
    // ddsd.dwSize = sizeof(ddsd);
    ddsd.dwFlags = DDSD_CAPS | DDSD_WIDTH | DDSD_HEIGHT;
    ddsd.dwWidth  = 2048;
    ddsd.dwHeight = 2048;
    ddsd.ddsCaps.dwCaps = DDSCAPS_OFFSCREENPLAIN | DDSCAPS_VIDEOMEMORY;
    if (FAILED(lpdd7->CreateSurface(&ddsd, &lpdds_back, NULL))) {
	ddsd.dwFlags = DDSD_CAPS | DDSD_WIDTH | DDSD_HEIGHT;
	ddsd.dwWidth  = 2048;
	ddsd.dwHeight = 2048;
	ddsd.ddsCaps.dwCaps = DDSCAPS_OFFSCREENPLAIN | DDSCAPS_SYSTEMMEMORY;
	if (FAILED(lpdd7->CreateSurface(&ddsd, &lpdds_back, NULL)))
				fatal("CreateSurface back failed\n");
    }

    memset(&ddsd, 0, sizeof(ddsd));
    ddsd.dwSize = sizeof(ddsd);

    ddsd.dwFlags = DDSD_CAPS | DDSD_WIDTH | DDSD_HEIGHT;
    ddsd.dwWidth  = 2048;
    ddsd.dwHeight = 2048;
    ddsd.ddsCaps.dwCaps = DDSCAPS_OFFSCREENPLAIN | DDSCAPS_VIDEOMEMORY;
    if (FAILED(lpdd7->CreateSurface(&ddsd, &lpdds_back2, NULL))) {
	ddsd.dwFlags = DDSD_CAPS | DDSD_WIDTH | DDSD_HEIGHT;
	ddsd.dwWidth  = 2048;
	ddsd.dwHeight = 2048;
	ddsd.ddsCaps.dwCaps = DDSCAPS_OFFSCREENPLAIN | DDSCAPS_SYSTEMMEMORY;
	if (FAILED(lpdd7->CreateSurface(&ddsd, &lpdds_back2, NULL)))
				fatal("CreateSurface back failed\n");
    }

    if (FAILED(lpdd7->CreateClipper(0, &lpdd_clipper, NULL))) return(0);

    if (FAILED(lpdd_clipper->SetHWnd(0, h))) return(0);

    if (FAILED(lpdds_pri->SetClipper(lpdd_clipper))) return(0);

    ddraw_hwnd = h;
    video_blit_memtoscreen_func   = ddraw_blit_memtoscreen;
    video_blit_memtoscreen_8_func = ddraw_blit_memtoscreen_8;

    return(1);
}


void
ddraw_close(void)
{
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
    if (lpdd7) {
	lpdd7->Release();
	lpdd7 = NULL;
    }
}


void
ddraw_take_screenshot(wchar_t *fn)
{
    ddraw_common_take_screenshot(fn, lpdds_back2);
}
