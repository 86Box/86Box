/* Copyright holders: Sarah Walker, Tenshi
   see COPYING for more details
*/
#include <stdint.h>
#include "../video/video.h"
#include "win_ddraw.h"


static LPDIRECTDRAW  lpdd  = NULL;
static LPDIRECTDRAW7 lpdd7 = NULL;
static LPDIRECTDRAWSURFACE7 lpdds_pri = NULL;
static LPDIRECTDRAWSURFACE7 lpdds_back = NULL;
static LPDIRECTDRAWSURFACE7 lpdds_back2 = NULL;
static LPDIRECTDRAWCLIPPER lpdd_clipper = NULL;
static DDSURFACEDESC2 ddsd;
static HWND ddraw_hwnd;
static int ddraw_w, ddraw_h;


extern "C" void fatal(const char *format, ...);
extern "C" void pclog(const char *format, ...);

extern "C" void device_force_redraw(void);

extern "C" int ddraw_fs_init(HWND h);
extern "C" void ddraw_fs_close(void);
extern "C" int ddraw_fs_pause(void);
extern "C" void ddraw_fs_take_screenshot(wchar_t *fn);
 
extern void ddraw_common_take_screenshot(wchar_t *fn, IDirectDrawSurface7 *pDDSurface);


static void blit_memtoscreen(int, int, int, int, int, int);


int ddraw_fs_init(HWND h)
{
        ddraw_w = GetSystemMetrics(SM_CXSCREEN);
        ddraw_h = GetSystemMetrics(SM_CYSCREEN);
        
	cgapal_rebuild();

        if (FAILED(DirectDrawCreate(NULL, &lpdd, NULL)))
           return 0;
        
        if (FAILED(lpdd->QueryInterface(IID_IDirectDraw7, (LPVOID *)&lpdd7)))
           return 0;

        lpdd->Release();
        lpdd = NULL;
        
        atexit(ddraw_fs_close);

        if (FAILED(lpdd7->SetCooperativeLevel(h, DDSCL_SETFOCUSWINDOW |  
        DDSCL_CREATEDEVICEWINDOW | DDSCL_EXCLUSIVE | DDSCL_FULLSCREEN | DDSCL_ALLOWREBOOT)))
           return 0;

        if (FAILED(lpdd7->SetDisplayMode(ddraw_w, ddraw_h, 32, 0 ,0)))
                return 0;
           
        // memset(&ddsd, 0, sizeof(ddsd));
        // ddsd.dwSize = sizeof(ddsd);
        
        ddsd.dwFlags = DDSD_CAPS | DDSD_BACKBUFFERCOUNT;
        ddsd.dwBackBufferCount = 1;
        ddsd.ddsCaps.dwCaps = DDSCAPS_PRIMARYSURFACE | DDSCAPS_COMPLEX | DDSCAPS_FLIP;
        if (FAILED(lpdd7->CreateSurface(&ddsd, &lpdds_pri, NULL)))
           return 0;
        
        ddsd.ddsCaps.dwCaps = DDSCAPS_BACKBUFFER;
        if (FAILED(lpdds_pri->GetAttachedSurface(&ddsd.ddsCaps, &lpdds_back2)))
           return 0;
        
        memset(&ddsd, 0, sizeof(ddsd));
        ddsd.dwSize = sizeof(ddsd);
        
        ddsd.dwFlags = DDSD_CAPS | DDSD_WIDTH | DDSD_HEIGHT;
        ddsd.dwWidth  = 2048;
        ddsd.dwHeight = 2048;
        ddsd.ddsCaps.dwCaps = DDSCAPS_OFFSCREENPLAIN | DDSCAPS_VIDEOMEMORY;
        if (FAILED(lpdd7->CreateSurface(&ddsd, &lpdds_back, NULL)))
        {
                ddsd.dwFlags = DDSD_CAPS | DDSD_WIDTH | DDSD_HEIGHT;
                ddsd.dwWidth  = 2048;
                ddsd.dwHeight = 2048;
                ddsd.ddsCaps.dwCaps = DDSCAPS_OFFSCREENPLAIN | DDSCAPS_SYSTEMMEMORY;
                if (FAILED(lpdd7->CreateSurface(&ddsd, &lpdds_back, NULL)))
                        return 0;
        }
           
        pclog("DDRAW_INIT complete\n");
        ddraw_hwnd = h;

        video_setblit(blit_memtoscreen);

	return 1;
}

void ddraw_fs_close(void)
{
        if (lpdds_back2)
        {
                lpdds_back2->Release();
                lpdds_back2 = NULL;
        }
        if (lpdds_back)
        {
                lpdds_back->Release();
                lpdds_back = NULL;
        }
        if (lpdds_pri)
        {
                lpdds_pri->Release();
                lpdds_pri = NULL;
        }
        if (lpdd_clipper)
        {
                lpdd_clipper->Release();
                lpdd_clipper = NULL;
        }
        if (lpdd7)
        {
                lpdd7->Release();
                lpdd7 = NULL;
        }
}

static void ddraw_fs_size(RECT window_rect, RECT *r_dest, int w, int h)
{
        int ratio_w, ratio_h;
        switch (video_fullscreen_scale)
        {
                case FULLSCR_SCALE_FULL:
                r_dest->left   = 0;
                r_dest->top    = 0;
                r_dest->right  = (window_rect.right  - window_rect.left) - 1;
                r_dest->bottom = (window_rect.bottom - window_rect.top) - 1;
                break;
                case FULLSCR_SCALE_43:
                r_dest->top    = 0;
                r_dest->bottom = (window_rect.bottom - window_rect.top) - 1;
                r_dest->left   = ((window_rect.right  - window_rect.left) / 2) - (((window_rect.bottom - window_rect.top) * 4) / (3 * 2));
                r_dest->right  = ((window_rect.right  - window_rect.left) / 2) + (((window_rect.bottom - window_rect.top) * 4) / (3 * 2)) - 1;
                if (r_dest->left < 0)
                {
                        r_dest->left   = 0;
                        r_dest->right  = (window_rect.right  - window_rect.left) - 1;
                        r_dest->top    = ((window_rect.bottom - window_rect.top) / 2) - (((window_rect.right - window_rect.left) * 3) / (4 * 2));
                        r_dest->bottom = ((window_rect.bottom - window_rect.top) / 2) + (((window_rect.right - window_rect.left) * 3) / (4 * 2)) - 1;
                }
                break;
                case FULLSCR_SCALE_SQ:
                r_dest->top    = 0;
                r_dest->bottom = (window_rect.bottom - window_rect.top) - 1;
                r_dest->left   = ((window_rect.right  - window_rect.left) / 2) - (((window_rect.bottom - window_rect.top) * w) / (h * 2));
                r_dest->right  = ((window_rect.right  - window_rect.left) / 2) + (((window_rect.bottom - window_rect.top) * w) / (h * 2)) - 1;
                if (r_dest->left < 0)
                {
                        r_dest->left   = 0;
                        r_dest->right  = (window_rect.right  - window_rect.left) - 1;
                        r_dest->top    = ((window_rect.bottom - window_rect.top) / 2) - (((window_rect.right - window_rect.left) * h) / (w * 2));
                        r_dest->bottom = ((window_rect.bottom - window_rect.top) / 2) + (((window_rect.right - window_rect.left) * h) / (w * 2)) - 1;
                }
                break;
                case FULLSCR_SCALE_INT:
                ratio_w = (window_rect.right  - window_rect.left) / w;
                ratio_h = (window_rect.bottom - window_rect.top)  / h;
                if (ratio_h < ratio_w)
                        ratio_w = ratio_h;
                r_dest->left   = ((window_rect.right  - window_rect.left) / 2) - ((w * ratio_w) / 2);
                r_dest->right  = ((window_rect.right  - window_rect.left) / 2) + ((w * ratio_w) / 2) - 1;
                r_dest->top    = ((window_rect.bottom - window_rect.top)  / 2) - ((h * ratio_w) / 2);
                r_dest->bottom = ((window_rect.bottom - window_rect.top)  / 2) + ((h * ratio_w) / 2) - 1;
                break;
        }
}

static void blit_memtoscreen(int x, int y, int y1, int y2, int w, int h)
{
        RECT r_src;
        RECT r_dest;
        RECT window_rect;
        int yy;
        HRESULT hr;
        DDBLTFX ddbltfx;
                
	if (lpdds_back == NULL)
	{
                video_blit_complete();
		return; /*Nothing to do*/
        }

        memset(&ddsd, 0, sizeof(ddsd));
        ddsd.dwSize = sizeof(ddsd);

        hr = lpdds_back->Lock(NULL, &ddsd, DDLOCK_SURFACEMEMORYPTR | DDLOCK_WAIT, NULL);
        if (hr == DDERR_SURFACELOST)
        {
                lpdds_back->Restore();
                lpdds_back->Lock(NULL, &ddsd, DDLOCK_SURFACEMEMORYPTR | DDLOCK_WAIT, NULL);
                device_force_redraw();
        }
        if (!ddsd.lpSurface)
        {
                video_blit_complete();
                return;
        }
	for (yy = y1; yy < y2; yy++)
	{
		if ((y + yy) >= 0)  memcpy((unsigned char*)ddsd.lpSurface + (yy * ddsd.lPitch), &(((uint32_t *)buffer32->line[y + yy])[x]), w * 4);
	}
        video_blit_complete();
        lpdds_back->Unlock(NULL);
        
        window_rect.left = 0;
        window_rect.top = 0;
        window_rect.right = ddraw_w;
        window_rect.bottom = ddraw_h;
        ddraw_fs_size(window_rect, &r_dest, w, h);
        
        r_src.left   = 0;
        r_src.top    = 0;       
        r_src.right  = w;
        r_src.bottom = h;

        ddbltfx.dwSize = sizeof(ddbltfx);
        ddbltfx.dwFillColor = 0;
        
        lpdds_back2->Blt(&window_rect, NULL, NULL, DDBLT_WAIT | DDBLT_COLORFILL, &ddbltfx);
        
        hr = lpdds_back2->Blt(&r_dest, lpdds_back, &r_src, DDBLT_WAIT, NULL);
        if (hr == DDERR_SURFACELOST)
        {
                lpdds_back2->Restore();
                lpdds_back2->Blt(&r_dest, lpdds_back, &r_src, DDBLT_WAIT, NULL);
        }
        
        hr = lpdds_pri->Flip(NULL, DDFLIP_NOVSYNC);        
        if (hr == DDERR_SURFACELOST)
        {
                lpdds_pri->Restore();
                lpdds_pri->Flip(NULL, DDFLIP_NOVSYNC);
        }
}


int
ddraw_fs_pause(void)
{
    return(0);
}


void ddraw_fs_take_screenshot(wchar_t *fn)
{
	ddraw_common_take_screenshot(fn, lpdds_back2);
}
