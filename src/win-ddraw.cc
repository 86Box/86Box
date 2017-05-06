/* Copyright holders: Sarah Walker, Tenshi
   see COPYING for more details
*/
#include <stdio.h>
#include <stdint.h>
#define UNICODE
#define BITMAP WINDOWS_BITMAP
#include <ddraw.h>
#undef BITMAP
#include "win-ddraw.h"
#include "win-ddraw-screenshot.h"
#include "video/video.h"
#include "win-cgapal.h"


extern "C" void fatal(const char *format, ...);
extern "C" void pclog(const char *format, ...);

extern "C" void device_force_redraw(void);

extern "C" int ddraw_init(HWND h);
extern "C" void ddraw_close(void);
 
extern "C" void video_blit_complete(void);

static void ddraw_blit_memtoscreen(int x, int y, int y1, int y2, int w, int h);
static void ddraw_blit_memtoscreen_8(int x, int y, int w, int h);

static LPDIRECTDRAW  lpdd  = NULL;
static LPDIRECTDRAW7 lpdd7 = NULL;
static LPDIRECTDRAWSURFACE7 lpdds_pri = NULL;
static LPDIRECTDRAWSURFACE7 lpdds_back = NULL;
static LPDIRECTDRAWSURFACE7 lpdds_back2 = NULL;
static LPDIRECTDRAWCLIPPER lpdd_clipper = NULL;
static DDSURFACEDESC2 ddsd;

static HWND ddraw_hwnd;

int ddraw_init(HWND h)
{
	cgapal_rebuild();

        if (FAILED(DirectDrawCreate(NULL, &lpdd, NULL)))
           return 0;
        
        if (FAILED(lpdd->QueryInterface(IID_IDirectDraw7, (LPVOID *)&lpdd7)))
           return 0;

        lpdd->Release();
        lpdd = NULL;
        
        atexit(ddraw_close);

        if (FAILED(lpdd7->SetCooperativeLevel(h, DDSCL_NORMAL)))
           return 0;
           
        memset(&ddsd, 0, sizeof(ddsd));
        ddsd.dwSize = sizeof(ddsd);
        
        ddsd.dwFlags = DDSD_CAPS;
        ddsd.ddsCaps.dwCaps = DDSCAPS_PRIMARYSURFACE;
        if (FAILED(lpdd7->CreateSurface(&ddsd, &lpdds_pri, NULL)))
           return 0;
        
        // memset(&ddsd, 0, sizeof(ddsd));
        // ddsd.dwSize = sizeof(ddsd);
        
        ddsd.dwFlags = DDSD_CAPS | DDSD_WIDTH | DDSD_HEIGHT;
        ddsd.dwWidth  = 2048;
        ddsd.dwHeight = 2048;
        ddsd.ddsCaps.dwCaps = DDSCAPS_OFFSCREENPLAIN | DDSCAPS_VIDEOMEMORY;
        if (FAILED(lpdd7->CreateSurface(&ddsd, &lpdds_back, NULL)))
           return 0;

        memset(&ddsd, 0, sizeof(ddsd));
        ddsd.dwSize = sizeof(ddsd);
        
        ddsd.dwFlags = DDSD_CAPS | DDSD_WIDTH | DDSD_HEIGHT;
        ddsd.dwWidth  = 2048;
        ddsd.dwHeight = 2048;
        ddsd.ddsCaps.dwCaps = DDSCAPS_OFFSCREENPLAIN | DDSCAPS_VIDEOMEMORY;
        if (FAILED(lpdd7->CreateSurface(&ddsd, &lpdds_back2, NULL)))
           return 0;
           
        if (FAILED(lpdd7->CreateClipper(0, &lpdd_clipper, NULL)))
           return 0;
        if (FAILED(lpdd_clipper->SetHWnd(0, h)))
           return 0;
        if (FAILED(lpdds_pri->SetClipper(lpdd_clipper)))
           return 0;

        pclog("DDRAW_INIT complete\n");
        ddraw_hwnd = h;
        video_blit_memtoscreen_func   = ddraw_blit_memtoscreen;
        video_blit_memtoscreen_8_func = ddraw_blit_memtoscreen_8;

	return 1;
}

void ddraw_close(void)
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

static void ddraw_blit_memtoscreen(int x, int y, int y1, int y2, int w, int h)
{
        RECT r_src;
        RECT r_dest;
        int xx, yy;
        POINT po;
        uint32_t *p;
        HRESULT hr;
//        pclog("Blit memtoscreen %i,%i %i %i %i,%i\n", x, y, y1, y2, w, h);

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
        if (hr == DDERR_SURFACELOST)
        {
                lpdds_back2->Restore();
                lpdds_back2->Blt(&r_src, lpdds_back, &r_src, DDBLT_WAIT, NULL);
        }

        if (readflash)
        {
                readflash = 0;
#ifdef LEGACY_READ_FLASH
		if (enable_flash)
		{
	                hr = lpdds_back2->Lock(NULL, &ddsd, DDLOCK_SURFACEMEMORYPTR | DDLOCK_WAIT, NULL);
        	        if (hr == DDERR_SURFACELOST)
                	{
                        	lpdds_back2->Restore();
	                        lpdds_back2->Lock(NULL, &ddsd, DDLOCK_SURFACEMEMORYPTR | DDLOCK_WAIT, NULL);
        	                device_force_redraw();
	                }
        	        if (!ddsd.lpSurface) return;
	                for (yy = 8; yy < 14; yy++)
	                {
				p = &(((uint32_t *) ddsd.lpSurface)[yy * ddsd.lPitch]);
                	        for (xx = (w - 40); xx < (w - 8); xx++)
                        	    p[xx] = 0xffffffff;
	                }
		}
#endif
       	}
        lpdds_back2->Unlock(NULL);
        
//        pclog("Blit from %i,%i %i,%i to %i,%i %i,%i\n", r_src.left, r_src.top, r_src.right, r_src.bottom, r_dest.left, r_dest.top, r_dest.right, r_dest.bottom);
        hr = lpdds_pri->Blt(&r_dest, lpdds_back2, &r_src, DDBLT_WAIT, NULL);
        if (hr == DDERR_SURFACELOST)
        {
                lpdds_pri->Restore();
                lpdds_pri->Blt(&r_dest, lpdds_back2, &r_src, DDBLT_WAIT, NULL);
        }
}

static void ddraw_blit_memtoscreen_8(int x, int y, int w, int h)
{
        RECT r_src;
        RECT r_dest;
        int xx, yy;
        POINT po;
        uint32_t *p;
        HRESULT hr;

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
        for (yy = 0; yy < h; yy++)
        {
                if ((y + yy) >= 0 && (y + yy) < buffer->h)
                {
                        p = (uint32_t *) &(((uint8_t *) ddsd.lpSurface)[yy * ddsd.lPitch]);
                        for (xx = 0; xx < w; xx++)
			{
                            p[xx] = pal_lookup[buffer->line[y + yy][x + xx]];
			}
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
        if (hr == DDERR_SURFACELOST)
        {
                lpdds_back2->Restore();
                lpdds_back2->Blt(&r_src, lpdds_back, &r_src, DDBLT_WAIT, NULL);
        }

        if (readflash)
        {
                readflash = 0;
		if (enable_flash)
		{
	                hr = lpdds_back2->Lock(NULL, &ddsd, DDLOCK_SURFACEMEMORYPTR | DDLOCK_WAIT, NULL);
        	        if (hr == DDERR_SURFACELOST)
	                {
        	                lpdds_back2->Restore();
                	        lpdds_back2->Lock(NULL, &ddsd, DDLOCK_SURFACEMEMORYPTR | DDLOCK_WAIT, NULL);
	                        device_force_redraw();
        	        }
                	if (!ddsd.lpSurface) return;
	                for (yy = 8; yy < 14; yy++)
        	        {
                	        p = (uint32_t *) &(((uint8_t *) ddsd.lpSurface)[yy * ddsd.lPitch]);
                        	for (xx = (w - 40); xx < (w - 8); xx++)
	                            p[xx] = 0xffffffff;
        	        }
	                lpdds_back2->Unlock(NULL);
		}
        }
        
        hr = lpdds_pri->Blt(&r_dest, lpdds_back2, &r_src, DDBLT_WAIT, NULL);
        if (hr == DDERR_SURFACELOST)
        {
                lpdds_pri->Restore();
                hr = lpdds_pri->Blt(&r_dest, lpdds_back2, &r_src, DDBLT_WAIT, NULL);
        }
}

void ddraw_take_screenshot(wchar_t *fn)
{
	ddraw_common_take_screenshot(fn, lpdds_back2);
}
