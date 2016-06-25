#include <stdio.h>
#include <stdint.h>
#define BITMAP WINDOWS_BITMAP
#include <ddraw.h>
#undef BITMAP
#include "win-ddraw.h"
#include "win-ddraw-screenshot.h"
#include "video.h"

extern "C" void fatal(const char *format, ...);
extern "C" void pclog(const char *format, ...);

extern "C" void device_force_redraw();

extern "C" void ddraw_init(HWND h);
extern "C" void ddraw_close();

static void ddraw_blit_memtoscreen(int x, int y, int y1, int y2, int w, int h);
static void ddraw_blit_memtoscreen_8(int x, int y, int w, int h);

static LPDIRECTDRAW  lpdd  = NULL;
static LPDIRECTDRAW4 lpdd4 = NULL;
static LPDIRECTDRAWSURFACE4 lpdds_pri = NULL;
static LPDIRECTDRAWSURFACE4 lpdds_back = NULL;
static LPDIRECTDRAWSURFACE4 lpdds_back2 = NULL;
static LPDIRECTDRAWCLIPPER lpdd_clipper = NULL;
static DDSURFACEDESC2 ddsd;

static HWND ddraw_hwnd;

static PALETTE cgapal=
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
        
void ddraw_init(HWND h)
{
        int c;
        
        for (c = 0; c < 256; c++)
            pal_lookup[c] = makecol(cgapal[c].r << 2, cgapal[c].g << 2, cgapal[c].b << 2);

        if (FAILED(DirectDrawCreate(NULL, &lpdd, NULL)))
           fatal("DirectDrawCreate failed\n");
        
        if (FAILED(lpdd->QueryInterface(IID_IDirectDraw4, (LPVOID *)&lpdd4)))
           fatal("QueryInterface failed\n");

        lpdd->Release();
        lpdd = NULL;
        
        atexit(ddraw_close);

        if (FAILED(lpdd4->SetCooperativeLevel(h, DDSCL_NORMAL)))
           fatal("SetCooperativeLevel failed\n");
           
        memset(&ddsd, 0, sizeof(ddsd));
        ddsd.dwSize = sizeof(ddsd);
        
        ddsd.dwFlags = DDSD_CAPS;
        ddsd.ddsCaps.dwCaps = DDSCAPS_PRIMARYSURFACE;
        if (FAILED(lpdd4->CreateSurface(&ddsd, &lpdds_pri, NULL)))
           fatal("CreateSurface failed\n");
        
        memset(&ddsd, 0, sizeof(ddsd));
        ddsd.dwSize = sizeof(ddsd);
        
        ddsd.dwFlags = DDSD_CAPS | DDSD_WIDTH | DDSD_HEIGHT;
        ddsd.dwWidth  = 2080;
        ddsd.dwHeight = 2080;
        ddsd.ddsCaps.dwCaps = DDSCAPS_OFFSCREENPLAIN | DDSCAPS_VIDEOMEMORY;
        if (FAILED(lpdd4->CreateSurface(&ddsd, &lpdds_back, NULL)))
           fatal("CreateSurface back failed\n");

        memset(&ddsd, 0, sizeof(ddsd));
        ddsd.dwSize = sizeof(ddsd);
        
        ddsd.dwFlags = DDSD_CAPS | DDSD_WIDTH | DDSD_HEIGHT;
        ddsd.dwWidth  = 2080;
        ddsd.dwHeight = 2080;
        ddsd.ddsCaps.dwCaps = DDSCAPS_OFFSCREENPLAIN | DDSCAPS_VIDEOMEMORY;
        if (FAILED(lpdd4->CreateSurface(&ddsd, &lpdds_back2, NULL)))
           fatal("CreateSurface back failed\n");
           
        if (FAILED(lpdd4->CreateClipper(0, &lpdd_clipper, NULL)))
           fatal("CreateClipper failed\n");
        if (FAILED(lpdd_clipper->SetHWnd(0, h)))
           fatal("SetHWnd failed\n");
        if (FAILED(lpdds_pri->SetClipper(lpdd_clipper)))
           fatal("SetClipper failed\n");

        pclog("DDRAW_INIT complete\n");
        ddraw_hwnd = h;
        video_blit_memtoscreen   = ddraw_blit_memtoscreen;
        video_blit_memtoscreen_8 = ddraw_blit_memtoscreen_8;
}

void ddraw_close()
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

static void ddraw_blit_memtoscreen(int x, int y, int y1, int y2, int w, int h)
{
        RECT r_src;
        RECT r_dest;
        int xx, yy;
        POINT po;
        uint32_t *p;
        HRESULT hr;
        
//        pclog("Blit memtoscreen %i,%i %i %i %i,%i\n", x, y, y1, y2, w, h);

        memset(&ddsd, 0, sizeof(ddsd));
        ddsd.dwSize = sizeof(ddsd);

        hr = lpdds_back->Lock(NULL, &ddsd, DDLOCK_SURFACEMEMORYPTR | DDLOCK_WAIT, NULL);
        if (hr == DDERR_SURFACELOST)
        {
                lpdds_back->Restore();
                lpdds_back->Lock(NULL, &ddsd, DDLOCK_SURFACEMEMORYPTR | DDLOCK_WAIT, NULL);
                device_force_redraw();
        }
        if (!ddsd.lpSurface) return;
        for (yy = y1; yy < y2; yy++)
        {
                if ((y + yy) >= 0 && (y + yy) < buffer->h)
                        memcpy(ddsd.lpSurface + (yy * ddsd.lPitch), &(((uint32_t *)buffer32->line[y + yy])[x]), w * 4);
        }
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
        	                p = (uint32_t *)(ddsd.lpSurface + (yy * ddsd.lPitch));                        
                	        for (xx = (w - 40); xx < (w - 8); xx++)
                        	    p[xx] = 0xffffffff;
	                }
		}
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

        memset(&ddsd, 0, sizeof(ddsd));
        ddsd.dwSize = sizeof(ddsd);

        hr = lpdds_back->Lock(NULL, &ddsd, DDLOCK_SURFACEMEMORYPTR | DDLOCK_WAIT, NULL);

        if (hr == DDERR_SURFACELOST)
        {
                lpdds_back->Restore();
                lpdds_back->Lock(NULL, &ddsd, DDLOCK_SURFACEMEMORYPTR | DDLOCK_WAIT, NULL);
                device_force_redraw();
        }
        if (!ddsd.lpSurface) return;
        for (yy = 0; yy < h; yy++)
        {
                if ((y + yy) >= 0 && (y + yy) < buffer->h)
                {
                        p = (uint32_t *)(ddsd.lpSurface + (yy * ddsd.lPitch));
                        for (xx = 0; xx < w; xx++)
			{
                            p[xx] = pal_lookup[buffer->line[y + yy][x + xx]];
			    /* If brown circuity is disabled, double the green component. */
			    if ((buffer->line[y + yy][x + xx] == 0x16) && !cga_brown)  p[xx] += (p[xx] & 0xff00);
			}
                }
        }
        p = (uint32_t *)(ddsd.lpSurface + (4 * ddsd.lPitch));
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
                	        p = (uint32_t *)(ddsd.lpSurface + (yy * ddsd.lPitch));                        
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

void ddraw_take_screenshot(char *fn)
{
	ddraw_common_take_screenshot(fn, lpdds_back2);
}
