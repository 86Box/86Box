/* Copyright holders: Sarah Walker, Tenshi
   see COPYING for more details
*/
#include <stdint.h>
#define BITMAP WINDOWS_BITMAP
#include <ddraw.h>
#undef BITMAP
#include "win-ddraw-fs.h"
#include "win-ddraw-screenshot.h"
#include "video.h"

extern "C" void fatal(const char *format, ...);
extern "C" void pclog(const char *format, ...);

extern "C" void device_force_redraw();

extern "C" void ddraw_fs_init(HWND h);
extern "C" void ddraw_fs_close();
 
extern "C" void video_blit_complete();

static void ddraw_fs_blit_memtoscreen(int x, int y, int y1, int y2, int w, int h);
static void ddraw_fs_blit_memtoscreen_8(int x, int y, int w, int h);

static LPDIRECTDRAW  lpdd  = NULL;
static LPDIRECTDRAW4 lpdd4 = NULL;
static LPDIRECTDRAWSURFACE4 lpdds_pri = NULL;
static LPDIRECTDRAWSURFACE4 lpdds_back = NULL;
static LPDIRECTDRAWSURFACE4 lpdds_back2 = NULL;
static LPDIRECTDRAWCLIPPER lpdd_clipper = NULL;
static DDSURFACEDESC2 ddsd;

static HWND ddraw_hwnd;
static int ddraw_w, ddraw_h;

static PALETTE cgapal =
{
        {0,0,0},{0,42,0},{42,0,0},{42,21,0},
        {0,0,0},{0,42,42},{42,0,42},{42,42,42},
        {0,0,0},{21,63,21},{63,21,21},{63,63,21},
        {0,0,0},{21,63,63},{63,21,63},{63,63,63},

        {0,0,0},{0,0,42},{0,42,0},{0,42,42},
        {42,0,0},{42,0,42},{42,21,00},{42,42,42},
        {21,21,21},{21,21,63},{21,63,21},{21,63,63},
        {63,21,21},{63,21,63},{63,63,21},{63,63,63},

        {0,0,0},{0,21,0},{0,0,42},{0,42,42},
        {42,0,21},{21,10,21},{42,0,42},{42,0,63},
        {21,21,21},{21,63,21},{42,21,42},{21,63,63},
        {63,0,0},{42,42,0},{63,21,42},{41,41,41},
        
        {0,0,0},{0,42,42},{42,0,0},{42,42,42},
        {0,0,0},{0,42,42},{42,0,0},{42,42,42},
        {0,0,0},{0,63,63},{63,0,0},{63,63,63},
        {0,0,0},{0,63,63},{63,0,0},{63,63,63},
};

static uint32_t pal_lookup[256];
        
void ddraw_fs_init(HWND h)
{
        int c;
        
        ddraw_w = GetSystemMetrics(SM_CXSCREEN);
        ddraw_h = GetSystemMetrics(SM_CYSCREEN);
        
        for (c = 0; c < 256; c++)
            pal_lookup[c] = makecol(cgapal[c].r << 2, cgapal[c].g << 2, cgapal[c].b << 2);

        if (FAILED(DirectDrawCreate(NULL, &lpdd, NULL)))
           fatal("DirectDrawCreate failed\n");
        
        if (FAILED(lpdd->QueryInterface(IID_IDirectDraw4, (LPVOID *)&lpdd4)))
           fatal("QueryInterface failed\n");

        lpdd->Release();
        lpdd = NULL;
        
        atexit(ddraw_fs_close);

        if (FAILED(lpdd4->SetCooperativeLevel(h, DDSCL_SETFOCUSWINDOW |  
        DDSCL_CREATEDEVICEWINDOW | DDSCL_EXCLUSIVE | DDSCL_FULLSCREEN | DDSCL_ALLOWREBOOT)))
           fatal("SetCooperativeLevel failed\n");

        if (FAILED(lpdd4->SetDisplayMode(ddraw_w, ddraw_h, 32, 0 ,0)))
                fatal("SetDisplayMode failed\n");
           
        // memset(&ddsd, 0, sizeof(ddsd));
        // ddsd.dwSize = sizeof(ddsd);
        
        ddsd.dwFlags = DDSD_CAPS | DDSD_BACKBUFFERCOUNT;
        ddsd.dwBackBufferCount = 1;
        ddsd.ddsCaps.dwCaps = DDSCAPS_PRIMARYSURFACE | DDSCAPS_COMPLEX | DDSCAPS_FLIP;
        if (FAILED(lpdd4->CreateSurface(&ddsd, &lpdds_pri, NULL)))
           fatal("CreateSurface failed\n");
        
        ddsd.ddsCaps.dwCaps = DDSCAPS_BACKBUFFER;
        if (FAILED(lpdds_pri->GetAttachedSurface(&ddsd.ddsCaps, &lpdds_back2)))
           fatal("CreateSurface back failed\n");
        
        memset(&ddsd, 0, sizeof(ddsd));
        ddsd.dwSize = sizeof(ddsd);
        
        ddsd.dwFlags = DDSD_CAPS | DDSD_WIDTH | DDSD_HEIGHT;
        ddsd.dwWidth  = 2048;
        ddsd.dwHeight = 2048;
        ddsd.ddsCaps.dwCaps = DDSCAPS_OFFSCREENPLAIN | DDSCAPS_VIDEOMEMORY;
        if (FAILED(lpdd4->CreateSurface(&ddsd, &lpdds_back, NULL)))
           fatal("CreateSurface back failed\n");
           
        pclog("DDRAW_INIT complete\n");
        ddraw_hwnd = h;
        video_blit_memtoscreen_func   = ddraw_fs_blit_memtoscreen;
        video_blit_memtoscreen_8_func = ddraw_fs_blit_memtoscreen_8;
}

void ddraw_fs_close()
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
        if (lpdd4)
        {
                lpdd4->Release();
                lpdd4 = NULL;
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

static void ddraw_fs_blit_memtoscreen(int x, int y, int y1, int y2, int w, int h)
{
        RECT r_src;
        RECT r_dest;
        RECT window_rect;
        int yy;
        HRESULT hr;
        DDBLTFX ddbltfx;
                
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
        
        if (readflash && enable_flash)
        {
                RECT r;
       	        r.left   = window_rect.right - 40;
                r.right  = window_rect.right - 8;
       	        r.top    = 8;
               	r.bottom = 14;
                ddbltfx.dwFillColor = 0xffffff;
       	        lpdds_back2->Blt(&r, NULL, NULL, DDBLT_WAIT | DDBLT_COLORFILL, &ddbltfx);
        }
        
        hr = lpdds_pri->Flip(NULL, DDFLIP_NOVSYNC);        
        if (hr == DDERR_SURFACELOST)
        {
                lpdds_pri->Restore();
                lpdds_pri->Flip(NULL, DDFLIP_NOVSYNC);
        }
}

static void ddraw_fs_blit_memtoscreen_8(int x, int y, int w, int h)
{
        RECT r_src;
        RECT r_dest;
        RECT window_rect;
        int xx, yy;
        HRESULT hr;
        DDBLTFX ddbltfx;

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
                        uint32_t *p = (uint32_t *)(ddsd.lpSurface + (yy * ddsd.lPitch));
                        for (xx = 0; xx < w; xx++)
			{
                            p[xx] = pal_lookup[buffer->line[y + yy][x + xx]];
			}
                }
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
        
        if (readflash && enable_flash)
        {
                RECT r;
                r.left   = window_rect.right - 40;
                r.right  = window_rect.right - 8;
                r.top    = 8;
                r.bottom = 14;
                ddbltfx.dwFillColor = 0xffffff;
                lpdds_back2->Blt(&r, NULL, NULL, DDBLT_WAIT | DDBLT_COLORFILL, &ddbltfx);
        }
        
        lpdds_pri->Flip(NULL, DDFLIP_NOVSYNC);        
}

void ddraw_fs_take_screenshot(char *fn)
{
	ddraw_common_take_screenshot(fn, lpdds_back2);
}
