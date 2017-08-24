/*
 * 86Box	A hypervisor and IBM PC system emulator that specializes in
 *		running old operating systems and software designed for IBM
 *		PC systems and compatibles from 1981 through fairly recent
 *		system designs based on the PCI bus.
 *
 *		This file is part of the 86Box distribution.
 *
 *		Direct3D 9 full screen rendererer and screenshots taking.
 *
 * Version:	@(#)win_d3d_fs.cc	1.0.2	2017/08/23
 *
 * Authors:	Sarah Walker, <http://pcem-emulator.co.uk/>
 *		Miran Grca, <mgrca8@gmail.com>
 *		Copyright 2008-2017 Sarah Walker.
 *		Copyright 2016,2017 Miran Grca.
 */
#include <stdint.h>
#include <stdio.h>
#include "../86box.h"
#include "../VIDEO/video.h"
#include "win.h"
#include "win_d3d.h"
#include "win_cgapal.h"


extern "C" void fatal(const char *format, ...);
extern "C" void pclog(const char *format, ...);

extern "C" void device_force_redraw(void);

static void d3d_fs_init_objects(void);
static void d3d_fs_close_objects(void);
static void d3d_fs_blit_memtoscreen(int x, int y, int y1, int y2, int w, int h);
static void d3d_fs_blit_memtoscreen_8(int x, int y, int w, int h);

extern "C" void video_blit_complete(void);


static LPDIRECT3D9             d3d        = NULL;
static LPDIRECT3DDEVICE9       d3ddev     = NULL; 
static LPDIRECT3DVERTEXBUFFER9 v_buffer   = NULL;
static LPDIRECT3DTEXTURE9      d3dTexture = NULL;
static D3DPRESENT_PARAMETERS d3dpp;

static HWND d3d_hwnd;
static HWND d3d_device_window;

static int d3d_fs_w, d3d_fs_h;

struct CUSTOMVERTEX
{
     FLOAT x, y, z, rhw;    // from the D3DFVF_XYZRHW flag
     DWORD color;
     FLOAT tu, tv;
};

PALETTE cgapal =
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

PALETTE cgapal_mono[6] =
{
	{ // 0 - green, 4-color-optimized contrast
		{0x00,0x00,0x00},{0x00,0x0d,0x03},{0x01,0x17,0x05},{0x01,0x1a,0x06},{0x02,0x28,0x09},{0x02,0x2c,0x0a},{0x03,0x39,0x0d},{0x03,0x3c,0x0e},
		{0x00,0x07,0x01},{0x01,0x13,0x04},{0x01,0x1f,0x07},{0x01,0x23,0x08},{0x02,0x31,0x0b},{0x02,0x35,0x0c},{0x05,0x3f,0x11},{0x0d,0x3f,0x17},
	},
	{ // 1 - green, 16-color-optimized contrast
		{0x00,0x00,0x00},{0x00,0x0d,0x03},{0x01,0x15,0x05},{0x01,0x17,0x05},{0x01,0x21,0x08},{0x01,0x24,0x08},{0x02,0x2e,0x0b},{0x02,0x31,0x0b},
		{0x01,0x22,0x08},{0x02,0x28,0x09},{0x02,0x30,0x0b},{0x02,0x32,0x0c},{0x03,0x39,0x0d},{0x03,0x3b,0x0e},{0x09,0x3f,0x14},{0x0d,0x3f,0x17},
	},
	{ // 2 - amber, 4-color-optimized contrast
		{0x00,0x00,0x00},{0x15,0x05,0x00},{0x20,0x0b,0x00},{0x24,0x0d,0x00},{0x33,0x18,0x00},{0x37,0x1b,0x00},{0x3f,0x26,0x01},{0x3f,0x2b,0x06},
		{0x0b,0x02,0x00},{0x1b,0x08,0x00},{0x29,0x11,0x00},{0x2e,0x14,0x00},{0x3b,0x1e,0x00},{0x3e,0x21,0x00},{0x3f,0x32,0x0a},{0x3f,0x38,0x0d},
	},
	{ // 3 - amber, 16-color-optimized contrast
		{0x00,0x00,0x00},{0x15,0x05,0x00},{0x1e,0x09,0x00},{0x21,0x0b,0x00},{0x2b,0x12,0x00},{0x2f,0x15,0x00},{0x38,0x1c,0x00},{0x3b,0x1e,0x00},
		{0x2c,0x13,0x00},{0x32,0x17,0x00},{0x3a,0x1e,0x00},{0x3c,0x1f,0x00},{0x3f,0x27,0x01},{0x3f,0x2a,0x04},{0x3f,0x36,0x0c},{0x3f,0x38,0x0d},
	},
	{ // 4 - grey, 4-color-optimized contrast
		{0x00,0x00,0x00},{0x0e,0x0f,0x10},{0x15,0x17,0x18},{0x18,0x1a,0x1b},{0x24,0x25,0x25},{0x27,0x28,0x28},{0x33,0x34,0x32},{0x37,0x38,0x35},
		{0x09,0x0a,0x0b},{0x11,0x12,0x13},{0x1c,0x1e,0x1e},{0x20,0x22,0x22},{0x2c,0x2d,0x2c},{0x2f,0x30,0x2f},{0x3c,0x3c,0x38},{0x3f,0x3f,0x3b},
	},
	{ // 5 - grey, 16-color-optimized contrast
		{0x00,0x00,0x00},{0x0e,0x0f,0x10},{0x13,0x14,0x15},{0x15,0x17,0x18},{0x1e,0x20,0x20},{0x20,0x22,0x22},{0x29,0x2a,0x2a},{0x2c,0x2d,0x2c},
		{0x1f,0x21,0x21},{0x23,0x25,0x25},{0x2b,0x2c,0x2b},{0x2d,0x2e,0x2d},{0x34,0x35,0x33},{0x37,0x37,0x34},{0x3e,0x3e,0x3a},{0x3f,0x3f,0x3b},
	}
};

uint32_t pal_lookup[256];

static CUSTOMVERTEX d3d_verts[] =
{
     {   0.0f,    0.0f, 1.0f, 1.0f, 0xffffff, 0.0f, 0.0f},
     {2048.0f, 2048.0f, 1.0f, 1.0f, 0xffffff, 1.0f, 1.0f},
     {   0.0f, 2048.0f, 1.0f, 1.0f, 0xffffff, 0.0f, 1.0f},

     {   0.0f,    0.0f, 1.0f, 1.0f, 0xffffff, 0.0f, 0.0f},
     {2048.0f,    0.0f, 1.0f, 1.0f, 0xffffff, 1.0f, 0.0f},
     {2048.0f, 2048.0f, 1.0f, 1.0f, 0xffffff, 1.0f, 1.0f},

     {   0.0f,    0.0f, 1.0f, 1.0f, 0xffffff, 0.0f, 0.0f},
     {2048.0f, 2048.0f, 1.0f, 1.0f, 0xffffff, 1.0f, 1.0f},
     {   0.0f, 2048.0f, 1.0f, 1.0f, 0xffffff, 0.0f, 1.0f},

     {   0.0f,    0.0f, 1.0f, 1.0f, 0xffffff, 0.0f, 0.0f},
     {2048.0f,    0.0f, 1.0f, 1.0f, 0xffffff, 1.0f, 0.0f},
     {2048.0f, 2048.0f, 1.0f, 1.0f, 0xffffff, 1.0f, 1.0f}
};


void cgapal_rebuild(void)
{
        int c;
        for (c = 0; c < 256; c++)
	{
		pal_lookup[c] = makecol(video_6to8[cgapal[c].r], video_6to8[cgapal[c].g], video_6to8[cgapal[c].b]);
	}
	if ((cga_palette > 1) && (cga_palette < 8))
	{
		if (vid_cga_contrast != 0)
		{
                        for (c = 0; c < 16; c++)
                        {
                                pal_lookup[c] = makecol(video_6to8[cgapal_mono[cga_palette - 2][c].r], video_6to8[cgapal_mono[cga_palette - 2][c].g], video_6to8[cgapal_mono[cga_palette - 2][c].b]);
                                pal_lookup[c + 16] = makecol(video_6to8[cgapal_mono[cga_palette - 2][c].r], video_6to8[cgapal_mono[cga_palette - 2][c].g], video_6to8[cgapal_mono[cga_palette - 2][c].b]);
                                pal_lookup[c + 32] = makecol(video_6to8[cgapal_mono[cga_palette - 2][c].r], video_6to8[cgapal_mono[cga_palette - 2][c].g], video_6to8[cgapal_mono[cga_palette - 2][c].b]);
                                pal_lookup[c + 48] = makecol(video_6to8[cgapal_mono[cga_palette - 2][c].r], video_6to8[cgapal_mono[cga_palette - 2][c].g], video_6to8[cgapal_mono[cga_palette - 2][c].b]);
                        }
                }
                else
                {
                        for (c = 0; c < 16; c++)
                        {
                                pal_lookup[c] = makecol(video_6to8[cgapal_mono[cga_palette - 1][c].r], video_6to8[cgapal_mono[cga_palette - 1][c].g], video_6to8[cgapal_mono[cga_palette - 1][c].b]);
                                pal_lookup[c + 16] = makecol(video_6to8[cgapal_mono[cga_palette - 1][c].r], video_6to8[cgapal_mono[cga_palette - 1][c].g], video_6to8[cgapal_mono[cga_palette - 1][c].b]);
                                pal_lookup[c + 32] = makecol(video_6to8[cgapal_mono[cga_palette - 1][c].r], video_6to8[cgapal_mono[cga_palette - 1][c].g], video_6to8[cgapal_mono[cga_palette - 1][c].b]);
                                pal_lookup[c + 48] = makecol(video_6to8[cgapal_mono[cga_palette - 1][c].r], video_6to8[cgapal_mono[cga_palette - 1][c].g], video_6to8[cgapal_mono[cga_palette - 1][c].b]);
                        }
                }
        }

	if (cga_palette == 8)
	{
		pal_lookup[0x16] = makecol(video_6to8[42], video_6to8[42], video_6to8[0]);
	}
}

int d3d_fs_init(HWND h)
{
        int c;
	WCHAR emulator_title[200];

        d3d_fs_w = GetSystemMetrics(SM_CXSCREEN);
        d3d_fs_h = GetSystemMetrics(SM_CYSCREEN);
        
        for (c = 0; c < 256; c++)
            pal_lookup[c] = makecol(cgapal[c].r << 2, cgapal[c].g << 2, cgapal[c].b << 2);

        d3d_hwnd = h;

	/*FIXME: should be done once, in win.c */
	_swprintf(emulator_title, L"%s v%s", EMU_NAME_W, EMU_VERSION_W);
        d3d_device_window = CreateWindowEx (
                0,
                szSubClassName,
                emulator_title,
                WS_POPUP,
                CW_USEDEFAULT,
                CW_USEDEFAULT,
                640,
                480,
                HWND_DESKTOP,
                NULL,
                NULL,
                NULL 
        );
        
        d3d = Direct3DCreate9(D3D_SDK_VERSION);

        memset(&d3dpp, 0, sizeof(d3dpp));      

        d3dpp.Flags                  = 0;
        d3dpp.SwapEffect             = D3DSWAPEFFECT_DISCARD;
        d3dpp.hDeviceWindow          = d3d_device_window;
        d3dpp.BackBufferCount        = 1;
        d3dpp.MultiSampleType        = D3DMULTISAMPLE_NONE;
        d3dpp.MultiSampleQuality     = 0;
        d3dpp.EnableAutoDepthStencil = false;
        d3dpp.AutoDepthStencilFormat = D3DFMT_UNKNOWN;
        d3dpp.PresentationInterval   = D3DPRESENT_INTERVAL_IMMEDIATE;
        d3dpp.Windowed               = false;
        d3dpp.BackBufferFormat       = D3DFMT_X8R8G8B8;
        d3dpp.BackBufferWidth        = d3d_fs_w;
        d3dpp.BackBufferHeight       = d3d_fs_h;

        if (FAILED(d3d->CreateDevice(D3DADAPTER_DEFAULT, D3DDEVTYPE_HAL, h, D3DCREATE_SOFTWARE_VERTEXPROCESSING, &d3dpp, &d3ddev)))
           fatal("CreateDevice failed\n");
        
        d3d_fs_init_objects();
        
        video_blit_memtoscreen_func   = d3d_fs_blit_memtoscreen;
        video_blit_memtoscreen_8_func = d3d_fs_blit_memtoscreen_8;

	return 1;
}

static void d3d_fs_close_objects()
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

static void d3d_fs_init_objects()
{
        D3DLOCKED_RECT dr;
        int y;
        RECT r;

        if (FAILED(d3ddev->CreateVertexBuffer(12*sizeof(CUSTOMVERTEX),
                                   0,
                                   D3DFVF_XYZRHW | D3DFVF_DIFFUSE | D3DFVF_TEX1,
                                   D3DPOOL_MANAGED,
                                   &v_buffer,
                                   NULL)))
           fatal("CreateVertexBuffer failed\n");

        if (FAILED(d3ddev->CreateTexture(2048, 2048, 1, 0, D3DFMT_X8R8G8B8, D3DPOOL_MANAGED, &d3dTexture, NULL)))
           fatal("CreateTexture failed\n");
     
        r.top    = r.left  = 0;
        r.bottom = r.right = 2047;

        if (FAILED(d3dTexture->LockRect(0, &dr, &r, 0)))
           fatal("LockRect failed\n");
        
        for (y = 0; y < 2048; y++)
        {
                uint32_t *p = (uint32_t *)((uintptr_t)dr.pBits + (y * dr.Pitch));
                memset(p, 0, 2048 * 4);
        }

        d3dTexture->UnlockRect(0);

        d3ddev->SetTextureStageState(0,D3DTSS_COLOROP,   D3DTOP_SELECTARG1);
        d3ddev->SetTextureStageState(0,D3DTSS_COLORARG1, D3DTA_TEXTURE);
        d3ddev->SetTextureStageState(0,D3DTSS_ALPHAOP,   D3DTOP_DISABLE);

        d3ddev->SetSamplerState(0, D3DSAMP_MAGFILTER, D3DTEXF_LINEAR);
        d3ddev->SetSamplerState(0, D3DSAMP_MINFILTER, D3DTEXF_LINEAR);
}

/*void d3d_resize(int x, int y)
{
        HRESULT hr;

        d3dpp.BackBufferWidth  = x;
        d3dpp.BackBufferHeight = y;

        d3d_reset();
}*/
        
void d3d_fs_reset()
{
        HRESULT hr;

        memset(&d3dpp, 0, sizeof(d3dpp));      

        d3dpp.Flags                  = 0;
        d3dpp.SwapEffect             = D3DSWAPEFFECT_DISCARD;
        d3dpp.hDeviceWindow          = d3d_device_window;
        d3dpp.BackBufferCount        = 1;
        d3dpp.MultiSampleType        = D3DMULTISAMPLE_NONE;
        d3dpp.MultiSampleQuality     = 0;
        d3dpp.EnableAutoDepthStencil = false;
        d3dpp.AutoDepthStencilFormat = D3DFMT_UNKNOWN;
        d3dpp.PresentationInterval   = D3DPRESENT_INTERVAL_IMMEDIATE;
        d3dpp.Windowed               = false;
        d3dpp.BackBufferFormat       = D3DFMT_X8R8G8B8;
        d3dpp.BackBufferWidth        = d3d_fs_w;
        d3dpp.BackBufferHeight       = d3d_fs_h;

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

void d3d_fs_close()
{       
        d3d_fs_close_objects();
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
        DestroyWindow(d3d_device_window);
}

static void d3d_fs_size(RECT window_rect, double *l, double *t, double *r, double *b, int w, int h)
{
        int ratio_w, ratio_h;
        switch (video_fullscreen_scale)
        {
                case FULLSCR_SCALE_FULL:
                *l = -0.5;
                *t = -0.5;
                *r = (window_rect.right  - window_rect.left) - 0.5;
                *b = (window_rect.bottom - window_rect.top) - 0.5;
                break;
                case FULLSCR_SCALE_43:
                *t = -0.5;
                *b = (window_rect.bottom - window_rect.top) - 0.5;
                *l = ((window_rect.right  - window_rect.left) / 2) - (((window_rect.bottom - window_rect.top) * 4) / (3 * 2)) - 0.5;
                *r = ((window_rect.right  - window_rect.left) / 2) + (((window_rect.bottom - window_rect.top) * 4) / (3 * 2)) - 0.5;
                if (*l < -0.5)
                {
                        *l = -0.5;
                        *r = (window_rect.right  - window_rect.left) - 0.5;
                        *t = ((window_rect.bottom - window_rect.top) / 2) - (((window_rect.right - window_rect.left) * 3) / (4 * 2)) - 0.5;
                        *b = ((window_rect.bottom - window_rect.top) / 2) + (((window_rect.right - window_rect.left) * 3) / (4 * 2)) - 0.5;
                }
                break;
                case FULLSCR_SCALE_SQ:
                *t = -0.5;
                *b = (window_rect.bottom - window_rect.top) - 0.5;
                *l = ((window_rect.right  - window_rect.left) / 2) - (((window_rect.bottom - window_rect.top) * w) / (h * 2)) - 0.5;
                *r = ((window_rect.right  - window_rect.left) / 2) + (((window_rect.bottom - window_rect.top) * w) / (h * 2)) - 0.5;
                if (*l < -0.5)
                {
                        *l = -0.5;
                        *r = (window_rect.right  - window_rect.left) - 0.5;
                        *t = ((window_rect.bottom - window_rect.top) / 2) - (((window_rect.right - window_rect.left) * h) / (w * 2)) - 0.5;
                        *b = ((window_rect.bottom - window_rect.top) / 2) + (((window_rect.right - window_rect.left) * h) / (w * 2)) - 0.5;
                }
                break;
                case FULLSCR_SCALE_INT:
                ratio_w = (window_rect.right  - window_rect.left) / w;
                ratio_h = (window_rect.bottom - window_rect.top)  / h;
                if (ratio_h < ratio_w)
                        ratio_w = ratio_h;
                *l = ((window_rect.right  - window_rect.left) / 2) - ((w * ratio_w) / 2) - 0.5;
                *r = ((window_rect.right  - window_rect.left) / 2) + ((w * ratio_w) / 2) - 0.5;
                *t = ((window_rect.bottom - window_rect.top)  / 2) - ((h * ratio_w) / 2) - 0.5;
                *b = ((window_rect.bottom - window_rect.top)  / 2) + ((h * ratio_w) / 2) - 0.5;
                break;
        }
}

static void d3d_fs_blit_memtoscreen(int x, int y, int y1, int y2, int w, int h)
{
        HRESULT hr = D3D_OK;
        VOID* pVoid;
        D3DLOCKED_RECT dr;
        RECT window_rect;
        int yy;
        double l = 0, t = 0, r = 0, b = 0;

        if (y1 == y2)
	{
                video_blit_complete();
		return; /*Nothing to do*/
        }

        if (hr == D3D_OK && !(y1 == 0 && y2 == 0))
        {
                RECT lock_rect;
                
                lock_rect.top    = y1;
                lock_rect.left   = 0;
                lock_rect.bottom = y2;
                lock_rect.right  = 2047;

                hr = d3dTexture->LockRect(0, &dr, &lock_rect, 0);
                if (hr == D3D_OK)
                {
                        for (yy = y1; yy < y2; yy++)
                                memcpy((void *)((uintptr_t)dr.pBits + ((yy - y1) * dr.Pitch)), &(((uint32_t *)buffer32->line[yy + y])[x]), w * 4);

                        video_blit_complete();
                        d3dTexture->UnlockRect(0);
                }
                else
                {
                        video_blit_complete();
                        return;
                }
        }
        else
                video_blit_complete();

        d3d_verts[0].tu = d3d_verts[2].tu = d3d_verts[3].tu = 0;
        d3d_verts[0].tv = d3d_verts[3].tv = d3d_verts[4].tv = 0;
        d3d_verts[1].tu = d3d_verts[4].tu = d3d_verts[5].tu = (float)w / 2048.0;
        d3d_verts[1].tv = d3d_verts[2].tv = d3d_verts[5].tv = (float)h / 2048.0;
        d3d_verts[0].color = d3d_verts[1].color = d3d_verts[2].color =
        d3d_verts[3].color = d3d_verts[4].color = d3d_verts[5].color =
        d3d_verts[6].color = d3d_verts[7].color = d3d_verts[8].color =
        d3d_verts[9].color = d3d_verts[10].color = d3d_verts[11].color = 0xffffff;

        GetClientRect(d3d_device_window, &window_rect);
        d3d_fs_size(window_rect, &l, &t, &r, &b, w, h);
        
        d3d_verts[0].x = l;
        d3d_verts[0].y = t;
        d3d_verts[1].x = r;
        d3d_verts[1].y = b;
        d3d_verts[2].x = l;
        d3d_verts[2].y = b;
        d3d_verts[3].x = l;
        d3d_verts[3].y = t;
        d3d_verts[4].x = r;
        d3d_verts[4].y = t;
        d3d_verts[5].x = r;
        d3d_verts[5].y = b;
        d3d_verts[6].x = d3d_verts[8].x = d3d_verts[9].x = r - 40.5;
        d3d_verts[6].y = d3d_verts[9].y = d3d_verts[10].y = t + 8.5;
        d3d_verts[7].x = d3d_verts[10].x = d3d_verts[11].x = r - 8.5;
        d3d_verts[7].y = d3d_verts[8].y = d3d_verts[11].y = t + 14.5;

        if (hr == D3D_OK)
                hr = v_buffer->Lock(0, 0, (void**)&pVoid, 0);
        if (hr == D3D_OK)
                memcpy(pVoid, d3d_verts, sizeof(d3d_verts));
        if (hr == D3D_OK)
                hr = v_buffer->Unlock();

        if (hr == D3D_OK)        
                hr = d3ddev->BeginScene();

        if (hr == D3D_OK)
        {
                if (hr == D3D_OK)
                        d3ddev->Clear(0, NULL, D3DCLEAR_TARGET, D3DCOLOR_XRGB(0, 0, 0), 1.0, 0);

                if (hr == D3D_OK)
                        hr = d3ddev->SetTexture(0, d3dTexture);

                if (hr == D3D_OK)
                        hr = d3ddev->SetFVF(D3DFVF_XYZRHW | D3DFVF_DIFFUSE | D3DFVF_TEX1);

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
                hr = d3ddev->Present(NULL, NULL, d3d_device_window, NULL);
        
        if (hr == D3DERR_DEVICELOST || hr == D3DERR_INVALIDCALL)
                PostMessage(ghwnd, WM_RESETD3D, 0, 0);
}

static void d3d_fs_blit_memtoscreen_8(int x, int y, int w, int h)
{
        HRESULT hr = D3D_OK;
        VOID* pVoid;
        D3DLOCKED_RECT dr;
        RECT window_rect;
        int xx, yy;
        double l = 0, t = 0, r = 0, b = 0;

	if (!h)
	{
                video_blit_complete();
		return; /*Nothing to do*/
        }

        if (hr == D3D_OK)
        {
                RECT lock_rect;
                
                lock_rect.top    = 0;
                lock_rect.left   = 0;
                lock_rect.bottom = 2047;
                lock_rect.right  = 2047;

                hr = d3dTexture->LockRect(0, &dr, &lock_rect, 0);
                if (hr == D3D_OK)
                {
                        for (yy = 0; yy < h; yy++)
                        {
                                uint32_t *p = (uint32_t *)((uintptr_t)dr.pBits + (yy * dr.Pitch));
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
                {
                        video_blit_complete();
                        return;
                }
	}
        else
                video_blit_complete();
      
        d3d_verts[0].tu = d3d_verts[2].tu = d3d_verts[3].tu = 0;
        d3d_verts[0].tv = d3d_verts[3].tv = d3d_verts[4].tv = 0;
        d3d_verts[1].tu = d3d_verts[4].tu = d3d_verts[5].tu = (float)w / 2048.0;
        d3d_verts[1].tv = d3d_verts[2].tv = d3d_verts[5].tv = (float)h / 2048.0;
        d3d_verts[0].color = d3d_verts[1].color = d3d_verts[2].color =
        d3d_verts[3].color = d3d_verts[4].color = d3d_verts[5].color =
        d3d_verts[6].color = d3d_verts[7].color = d3d_verts[8].color =
        d3d_verts[9].color = d3d_verts[10].color = d3d_verts[11].color = 0xffffff;

        GetClientRect(d3d_device_window, &window_rect);
        d3d_fs_size(window_rect, &l, &t, &r, &b, w, h);
        
        d3d_verts[0].x = l;
        d3d_verts[0].y = t;
        d3d_verts[1].x = r;
        d3d_verts[1].y = b;
        d3d_verts[2].x = l;
        d3d_verts[2].y = b;
        d3d_verts[3].x = l;
        d3d_verts[3].y = t;
        d3d_verts[4].x = r;
        d3d_verts[4].y = t;
        d3d_verts[5].x = r;
        d3d_verts[5].y = b;
        d3d_verts[6].x = d3d_verts[8].x = d3d_verts[9].x = r - 40.5;
        d3d_verts[6].y = d3d_verts[9].y = d3d_verts[10].y = t + 8.5;
        d3d_verts[7].x = d3d_verts[10].x = d3d_verts[11].x = r - 8.5;
        d3d_verts[7].y = d3d_verts[8].y = d3d_verts[11].y = t + 14.5;

        if (hr == D3D_OK)
                hr = v_buffer->Lock(0, 0, (void**)&pVoid, 0);
        if (hr == D3D_OK)
                memcpy(pVoid, d3d_verts, sizeof(d3d_verts));
        if (hr == D3D_OK)
                hr = v_buffer->Unlock();

        if (hr == D3D_OK)        
                hr = d3ddev->BeginScene();

        if (hr == D3D_OK)
        {
                if (hr == D3D_OK)
                        d3ddev->Clear(0, NULL, D3DCLEAR_TARGET, D3DCOLOR_XRGB(0, 0, 0), 1.0, 0);

                if (hr == D3D_OK)
                        hr = d3ddev->SetTexture(0, d3dTexture);

                if (hr == D3D_OK)
                        hr = d3ddev->SetFVF(D3DFVF_XYZRHW | D3DFVF_DIFFUSE | D3DFVF_TEX1);

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
                hr = d3ddev->Present(NULL, NULL, d3d_device_window, NULL);
        
        if (hr == D3DERR_DEVICELOST || hr == D3DERR_INVALIDCALL)
                PostMessage(ghwnd, WM_RESETD3D, 0, 0);
}


void d3d_fs_take_screenshot(wchar_t *fn)
{
	LPDIRECT3DSURFACE9 d3dSurface = NULL;

	if (!d3dTexture)  return;

	d3ddev->GetBackBuffer(0, 0, D3DBACKBUFFER_TYPE_MONO, &d3dSurface);
	D3DXSaveSurfaceToFile(fn, D3DXIFF_PNG, d3dSurface, NULL, NULL);

	d3dSurface->Release();
	d3dSurface = NULL;
}
