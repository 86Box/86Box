/* Copyright holders: Sarah Walker, Tenshi
   see COPYING for more details
*/
#include <stdint.h>
#define BITMAP WINDOWS_BITMAP
#include <d3d9.h>
#undef BITMAP
#include <D3dx9tex.h>
#include "resources.h"
#include "win-d3d.h"
#include "video.h"

extern "C" void fatal(const char *format, ...);
extern "C" void pclog(const char *format, ...);

extern "C" void device_force_redraw();
extern "C" void video_blit_complete();

void d3d_init_objects();
void d3d_close_objects();
void d3d_blit_memtoscreen(int x, int y, int y1, int y2, int w, int h);
void d3d_blit_memtoscreen_8(int x, int y, int w, int h);

static LPDIRECT3D9             d3d        = NULL;
static LPDIRECT3DDEVICE9       d3ddev     = NULL; 
static LPDIRECT3DVERTEXBUFFER9 v_buffer   = NULL;
static LPDIRECT3DTEXTURE9      d3dTexture = NULL;
static D3DPRESENT_PARAMETERS d3dpp;

static HWND d3d_hwnd;

struct CUSTOMVERTEX
{
     FLOAT x, y, z, rhw;    // from the D3DFVF_XYZRHW flag
     FLOAT tu, tv;
};

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

static CUSTOMVERTEX d3d_verts[] =
{
     {   0.0f,    0.0f, 1.0f, 1.0f, 0.0f, 0.0f},
     {2048.0f, 2048.0f, 1.0f, 1.0f, 1.0f, 1.0f},
     {   0.0f, 2048.0f, 1.0f, 1.0f, 0.0f, 1.0f},

     {   0.0f,    0.0f, 1.0f, 1.0f, 0.0f, 0.0f},
     {2048.0f,    0.0f, 1.0f, 1.0f, 1.0f, 0.0f},
     {2048.0f, 2048.0f, 1.0f, 1.0f, 1.0f, 1.0f},
};
  
void d3d_init(HWND h)
{
        int c;
        HRESULT hr;
        
        for (c = 0; c < 256; c++)
            pal_lookup[c] = makecol(cgapal[c].r << 2, cgapal[c].g << 2, cgapal[c].b << 2);

        d3d_hwnd = h;
        
        d3d = Direct3DCreate9(D3D_SDK_VERSION);

        memset(&d3dpp, 0, sizeof(d3dpp));      

        d3dpp.Flags                  = 0;
        d3dpp.SwapEffect             = D3DSWAPEFFECT_DISCARD;
        d3dpp.hDeviceWindow          = h;
        d3dpp.BackBufferCount        = 1;
        d3dpp.MultiSampleType        = D3DMULTISAMPLE_NONE;
        d3dpp.MultiSampleQuality     = 0;
        d3dpp.EnableAutoDepthStencil = false;
        d3dpp.AutoDepthStencilFormat = D3DFMT_UNKNOWN;
        d3dpp.PresentationInterval   = D3DPRESENT_INTERVAL_IMMEDIATE;
        d3dpp.Windowed               = true;
        d3dpp.BackBufferFormat       = D3DFMT_UNKNOWN;
        d3dpp.BackBufferWidth        = 0;
        d3dpp.BackBufferHeight       = 0;

        hr = d3d->CreateDevice(D3DADAPTER_DEFAULT, D3DDEVTYPE_HAL, h, D3DCREATE_SOFTWARE_VERTEXPROCESSING, &d3dpp, &d3ddev);
        
        d3d_init_objects();
        
        video_blit_memtoscreen_func = d3d_blit_memtoscreen;
        video_blit_memtoscreen_8_func = d3d_blit_memtoscreen_8;
}

void d3d_close_objects()
{
        if (d3dTexture)
        {
                d3dTexture->Release();
                d3dTexture = NULL;
        }
        if (v_buffer)
        {
                v_buffer->Release();
                v_buffer = NULL;
        }
}

void d3d_init_objects()
{
        HRESULT hr;
        D3DLOCKED_RECT dr;
        int y;
        RECT r;

        hr = d3ddev->CreateVertexBuffer(6*sizeof(CUSTOMVERTEX),
                                   0,
                                   D3DFVF_XYZRHW | D3DFVF_TEX1,
                                   D3DPOOL_MANAGED,
                                   &v_buffer,
                                   NULL);

        d3ddev->CreateTexture(2048, 2048, 1, 0, D3DFMT_X8R8G8B8, D3DPOOL_MANAGED, &d3dTexture, NULL);
     
        // r.top    = r.left  = 0;
        r.bottom = r.right = 2047;

        if (FAILED(d3dTexture->LockRect(0, &dr, &r, 0)))
           fatal("LockRect failed\n");
        
        /* for (y = 0; y < 2048; y++)
        {
                uint32_t *p = (uint32_t *)(dr.pBits + (y * dr.Pitch));
                memset(p, 0, 2048 * 4);
        } */

        d3dTexture->UnlockRect(0);

        d3ddev->SetTextureStageState(0,D3DTSS_COLOROP,   D3DTOP_SELECTARG1);
        d3ddev->SetTextureStageState(0,D3DTSS_COLORARG1, D3DTA_TEXTURE);
        d3ddev->SetTextureStageState(0,D3DTSS_ALPHAOP,   D3DTOP_DISABLE);

        d3ddev->SetSamplerState(0, D3DSAMP_MAGFILTER, D3DTEXF_LINEAR);
        d3ddev->SetSamplerState(0, D3DSAMP_MINFILTER, D3DTEXF_LINEAR);
}

void d3d_resize(int x, int y)
{
        HRESULT hr;

        d3dpp.BackBufferWidth  = x;
        d3dpp.BackBufferHeight = y;

        d3d_reset();
}
        
void d3d_reset()
{
        HRESULT hr;
        
        memset(&d3dpp, 0, sizeof(d3dpp));      

        d3dpp.Flags                  = 0;
        d3dpp.SwapEffect             = D3DSWAPEFFECT_DISCARD;
        d3dpp.hDeviceWindow          = d3d_hwnd;
        d3dpp.BackBufferCount        = 1;
        d3dpp.MultiSampleType        = D3DMULTISAMPLE_NONE;
        d3dpp.MultiSampleQuality     = 0;
        d3dpp.EnableAutoDepthStencil = false;
        d3dpp.AutoDepthStencilFormat = D3DFMT_UNKNOWN;
        d3dpp.PresentationInterval   = D3DPRESENT_INTERVAL_IMMEDIATE;
        d3dpp.Windowed               = true;
        d3dpp.BackBufferFormat       = D3DFMT_UNKNOWN;
        d3dpp.BackBufferWidth        = 0;
        d3dpp.BackBufferHeight       = 0;

        hr = d3ddev->Reset(&d3dpp);

        if (hr == D3DERR_DEVICELOST)
                return;

        d3ddev->SetTextureStageState(0,D3DTSS_COLOROP,   D3DTOP_SELECTARG1);
        d3ddev->SetTextureStageState(0,D3DTSS_COLORARG1, D3DTA_TEXTURE);
        d3ddev->SetTextureStageState(0,D3DTSS_ALPHAOP,   D3DTOP_DISABLE);

        d3ddev->SetSamplerState(0, D3DSAMP_MAGFILTER, D3DTEXF_LINEAR);
        d3ddev->SetSamplerState(0, D3DSAMP_MINFILTER, D3DTEXF_LINEAR);

        device_force_redraw();
}

void d3d_close()
{       
        if (d3dTexture)
        {
                d3dTexture->Release();
                d3dTexture = NULL;
        }
        if (v_buffer)
        {
                v_buffer->Release();
                v_buffer = NULL;
        }
        if (d3ddev)
        {
                d3ddev->Release();
                d3ddev = NULL;
        }
        if (d3d)
        {
                d3d->Release();
                d3d = NULL;
        }
}

void d3d_blit_memtoscreen(int x, int y, int y1, int y2, int w, int h)
{
        HRESULT hr = D3D_OK;
        VOID* pVoid;
        D3DLOCKED_RECT dr;
        RECT r;
        uint32_t *p, *src;
        int yy;

        if (y1 == y2)
        {
                video_blit_complete();
        	return; /*Nothing to do*/
        }

        r.top    = y1;
        r.left   = 0;
        r.bottom = y2;
        r.right  = 2047;

        if (hr == D3D_OK)
        {        
                if (FAILED(d3dTexture->LockRect(0, &dr, &r, 0)))
                   fatal("LockRect failed\n");
        
                for (yy = y1; yy < y2; yy++)
                        memcpy(dr.pBits + ((yy - y1) * dr.Pitch), &(((uint32_t *)buffer32->line[yy + y])[x]), w * 4);

                video_blit_complete();
                d3dTexture->UnlockRect(0);
        }
        else
                video_blit_complete();

        d3d_verts[0].tu = d3d_verts[2].tu = d3d_verts[3].tu = 0;//0.5 / 2048.0;
        d3d_verts[0].tv = d3d_verts[3].tv = d3d_verts[4].tv = 0;//0.5 / 2048.0;
        d3d_verts[1].tu = d3d_verts[4].tu = d3d_verts[5].tu = (float)w / 2048.0;
        d3d_verts[1].tv = d3d_verts[2].tv = d3d_verts[5].tv = (float)h / 2048.0;

        GetClientRect(d3d_hwnd, &r);
        d3d_verts[0].x = d3d_verts[2].x = d3d_verts[3].x = -0.5;
        d3d_verts[0].y = d3d_verts[3].y = d3d_verts[4].y = -0.5;
        d3d_verts[1].x = d3d_verts[4].x = d3d_verts[5].x = (r.right  - r.left) - 0.5;
        d3d_verts[1].y = d3d_verts[2].y = d3d_verts[5].y = (r.bottom - r.top) - 0.5;

        if (hr == D3D_OK)
                hr = v_buffer->Lock(0, 0, (void**)&pVoid, 0);    // lock the vertex buffer
        if (hr == D3D_OK)
                memcpy(pVoid, d3d_verts, sizeof(d3d_verts));    // copy the vertices to the locked buffer
        if (hr == D3D_OK)
                hr = v_buffer->Unlock();    // unlock the vertex buffer

        if (hr == D3D_OK)        
                hr = d3ddev->BeginScene();

        if (hr == D3D_OK)
        {
                if (hr == D3D_OK)
                        hr = d3ddev->SetTexture(0, d3dTexture);

                if (hr == D3D_OK)
                        hr = d3ddev->SetFVF(D3DFVF_XYZRHW | D3DFVF_TEX1);

                if (hr == D3D_OK)
                        hr = d3ddev->SetStreamSource(0, v_buffer, 0, sizeof(CUSTOMVERTEX));

                if (hr == D3D_OK)
                        hr = d3ddev->DrawPrimitive(D3DPT_TRIANGLELIST, 0, 2);

                if (hr == D3D_OK)
                        hr = d3ddev->SetTexture(0, NULL);

                if (hr == D3D_OK)
                        hr = d3ddev->EndScene();
        }

        if (hr == D3D_OK)
                hr = d3ddev->Present(NULL, NULL, d3d_hwnd, NULL);
        
        if (hr == D3DERR_DEVICELOST || hr == D3DERR_INVALIDCALL)
                PostMessage(d3d_hwnd, WM_RESETD3D, 0, 0);
}

void d3d_blit_memtoscreen_8(int x, int y, int w, int h)
{
        VOID* pVoid;
        D3DLOCKED_RECT dr;
        RECT r;
        uint32_t *p, *src;
        int yy, xx;
        HRESULT hr = D3D_OK;

	if (h == 0)
	{
                video_blit_complete();
		return; /*Nothing to do*/
        }

        r.top    = 0;
        r.left   = 0;
        r.bottom = h;
        r.right  = 2047;
        
        if (hr == D3D_OK)
        {
                if (FAILED(d3dTexture->LockRect(0, &dr, &r, 0)))
                        fatal("LockRect failed\n");
        
                for (yy = 0; yy < h; yy++)
                {
                        uint32_t *p = (uint32_t *)(dr.pBits + (yy * dr.Pitch));
                        if ((y + yy) >= 0 && (y + yy) < buffer->h)
                        {
                                for (xx = 0; xx < w; xx++)
                                        p[xx] = pal_lookup[buffer->line[y + yy][x + xx]];
                        }
                }
                video_blit_complete();

                d3dTexture->UnlockRect(0);
        }
        else
                video_blit_complete();
      
        d3d_verts[0].tu = d3d_verts[2].tu = d3d_verts[3].tu = 0;//0.5 / 2048.0;
        d3d_verts[0].tv = d3d_verts[3].tv = d3d_verts[4].tv = 0;//0.5 / 2048.0;
        d3d_verts[1].tu = d3d_verts[4].tu = d3d_verts[5].tu = (float)w / 2048.0;
        d3d_verts[1].tv = d3d_verts[2].tv = d3d_verts[5].tv = (float)h / 2048.0;

        GetClientRect(d3d_hwnd, &r);
        d3d_verts[0].x = d3d_verts[2].x = d3d_verts[3].x = -0.5;
        d3d_verts[0].y = d3d_verts[3].y = d3d_verts[4].y = -0.5;
        d3d_verts[1].x = d3d_verts[4].x = d3d_verts[5].x = (r.right  - r.left) - 0.5;
        d3d_verts[1].y = d3d_verts[2].y = d3d_verts[5].y = (r.bottom - r.top) - 0.5;

        if (hr == D3D_OK)
                hr = v_buffer->Lock(0, 0, (void**)&pVoid, 0);    // lock the vertex buffer
        if (hr == D3D_OK)
                memcpy(pVoid, d3d_verts, sizeof(d3d_verts));    // copy the vertices to the locked buffer
        if (hr == D3D_OK)
                hr = v_buffer->Unlock();    // unlock the vertex buffer

        if (hr == D3D_OK)        
                hr = d3ddev->BeginScene();

        if (hr == D3D_OK)
        {
                if (hr == D3D_OK)
                        hr = d3ddev->SetTexture(0, d3dTexture);

                if (hr == D3D_OK)
                        hr = d3ddev->SetFVF(D3DFVF_XYZRHW | D3DFVF_TEX1);

                if (hr == D3D_OK)
                        hr = d3ddev->SetStreamSource(0, v_buffer, 0, sizeof(CUSTOMVERTEX));

                if (hr == D3D_OK)
                        hr = d3ddev->DrawPrimitive(D3DPT_TRIANGLELIST, 0, 2);

                if (hr == D3D_OK)
                        hr = d3ddev->SetTexture(0, NULL);

                if (hr == D3D_OK)
                        hr = d3ddev->EndScene();
        }

        if (hr == D3D_OK)
                hr = d3ddev->Present(NULL, NULL, d3d_hwnd, NULL);
        
        if (hr == D3DERR_DEVICELOST || hr == D3DERR_INVALIDCALL)
                PostMessage(d3d_hwnd, WM_RESETD3D, 0, 0);
}

void d3d_take_screenshot(char *fn)
{
        HRESULT hr = D3D_OK;
	LPDIRECT3DSURFACE9 d3dSurface = NULL;

	if (!d3dTexture)  return;

	hr = d3ddev->GetBackBuffer(0, 0, D3DBACKBUFFER_TYPE_MONO, &d3dSurface);
	hr = D3DXSaveSurfaceToFile(fn, D3DXIFF_PNG, d3dSurface, NULL, NULL);

	d3dSurface->Release();
	d3dSurface = NULL;
}
