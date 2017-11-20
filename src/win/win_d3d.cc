/*
 * 86Box	A hypervisor and IBM PC system emulator that specializes in
 *		running old operating systems and software designed for IBM
 *		PC systems and compatibles from 1981 through fairly recent
 *		system designs based on the PCI bus.
 *
 *		This file is part of the 86Box distribution.
 *
 *		Rendering module for Microsoft Direct3D 9.
 *
 * Version:	@(#)win_d3d.cc	1.0.6	2017/11/12
 *
 * Authors:	Sarah Walker, <http://pcem-emulator.co.uk/>
 *		Miran Grca, <mgrca8@gmail.com>
 *		Fred N. van Kempen, <decwiz@yahoo.com>
 *
 *		Copyright 2008-2017 Sarah Walker.
 *		Copyright 2016,2017 Miran Grca.
 *		Copyright 2017 Fred N. van Kempen.
 */
#include <stdio.h>
#include <stdint.h>
#include "../86box.h"
#include "../device.h"
#include "../video/video.h"
#include "../plat.h"
#include "win.h"
#include "win_d3d.h"


struct CUSTOMVERTEX {
    FLOAT	x, y, z, rhw;    // from the D3DFVF_XYZRHW flag
    DWORD	color;
    FLOAT	tu, tv;
};


static LPDIRECT3D9		d3d = NULL;
static LPDIRECT3DDEVICE9	d3ddev = NULL; 
static LPDIRECT3DVERTEXBUFFER9	v_buffer = NULL;
static LPDIRECT3DTEXTURE9	d3dTexture = NULL;
static D3DPRESENT_PARAMETERS	d3dpp;
static HWND			d3d_hwnd;
static HWND			d3d_device_window;
static int			d3d_w,
				d3d_h;

static CUSTOMVERTEX d3d_verts[] = {
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


static void
d3d_size(RECT w_rect, double *l, double *t, double *r, double *b, int w, int h)
{
    int ratio_w, ratio_h;

    switch (video_fullscreen_scale) {
	case FULLSCR_SCALE_FULL:
		*l = -0.5;
		*t = -0.5;
		*r = (w_rect.right  - w_rect.left) - 0.5;
		*b = (w_rect.bottom - w_rect.top) - 0.5;
		break;

	case FULLSCR_SCALE_43:
		*t = -0.5;
		*b = (w_rect.bottom - w_rect.top) - 0.5;
		*l = ((w_rect.right  - w_rect.left) / 2) - (((w_rect.bottom - w_rect.top) * 4) / (3 * 2)) - 0.5;
		*r = ((w_rect.right  - w_rect.left) / 2) + (((w_rect.bottom - w_rect.top) * 4) / (3 * 2)) - 0.5;
		if (*l < -0.5) {
			*l = -0.5;
			*r = (w_rect.right  - w_rect.left) - 0.5;
			*t = ((w_rect.bottom - w_rect.top) / 2) - (((w_rect.right - w_rect.left) * 3) / (4 * 2)) - 0.5;
			*b = ((w_rect.bottom - w_rect.top) / 2) + (((w_rect.right - w_rect.left) * 3) / (4 * 2)) - 0.5;
		}
		break;

	case FULLSCR_SCALE_SQ:
		*t = -0.5;
		*b = (w_rect.bottom - w_rect.top) - 0.5;
		*l = ((w_rect.right  - w_rect.left) / 2) - (((w_rect.bottom - w_rect.top) * w) / (h * 2)) - 0.5;
		*r = ((w_rect.right  - w_rect.left) / 2) + (((w_rect.bottom - w_rect.top) * w) / (h * 2)) - 0.5;
		if (*l < -0.5) {
			*l = -0.5;
			*r = (w_rect.right  - w_rect.left) - 0.5;
			*t = ((w_rect.bottom - w_rect.top) / 2) - (((w_rect.right - w_rect.left) * h) / (w * 2)) - 0.5;
			*b = ((w_rect.bottom - w_rect.top) / 2) + (((w_rect.right - w_rect.left) * h) / (w * 2)) - 0.5;
		}
		break;

	case FULLSCR_SCALE_INT:
		ratio_w = (w_rect.right  - w_rect.left) / w;
		ratio_h = (w_rect.bottom - w_rect.top)  / h;
		if (ratio_h < ratio_w)
			ratio_w = ratio_h;
		*l = ((w_rect.right  - w_rect.left) / 2) - ((w * ratio_w) / 2) - 0.5;
		*r = ((w_rect.right  - w_rect.left) / 2) + ((w * ratio_w) / 2) - 0.5;
		*t = ((w_rect.bottom - w_rect.top)  / 2) - ((h * ratio_w) / 2) - 0.5;
		*b = ((w_rect.bottom - w_rect.top)  / 2) + ((h * ratio_w) / 2) - 0.5;
		break;
    }
}


static void
d3d_blit_fs(int x, int y, int y1, int y2, int w, int h)
{
    HRESULT hr = D3D_OK;
    HRESULT hbsr = D3D_OK;
    VOID* pVoid;
    D3DLOCKED_RECT dr;
    RECT w_rect;
    int yy;
    double l = 0, t = 0, r = 0, b = 0;

    if ((y1 == y2) || (h <= 0)) {
	video_blit_complete();
	return; /*Nothing to do*/
    }

    if (hr == D3D_OK && !(y1 == 0 && y2 == 0)) {
	RECT lock_rect;

	lock_rect.top    = y1;
	lock_rect.left   = 0;
	lock_rect.bottom = y2;
	lock_rect.right  = 2047;

	hr = d3dTexture->LockRect(0, &dr, &lock_rect, 0);
	if (hr == D3D_OK) {
		for (yy = y1; yy < y2; yy++)
			memcpy((void *)((uintptr_t)dr.pBits + ((yy - y1) * dr.Pitch)), &(((uint32_t *)buffer32->line[yy + y])[x]), w * 4);

		video_blit_complete();
		d3dTexture->UnlockRect(0);
	} else {
		video_blit_complete();
		return;
	}
    } else
	video_blit_complete();

    d3d_verts[0].tu = d3d_verts[2].tu = d3d_verts[3].tu = 0;
    d3d_verts[0].tv = d3d_verts[3].tv = d3d_verts[4].tv = 0;
    d3d_verts[1].tu = d3d_verts[4].tu = d3d_verts[5].tu = (float)w / 2048.0;
    d3d_verts[1].tv = d3d_verts[2].tv = d3d_verts[5].tv = (float)h / 2048.0;
    d3d_verts[0].color = d3d_verts[1].color = d3d_verts[2].color =
    d3d_verts[3].color = d3d_verts[4].color = d3d_verts[5].color =
    d3d_verts[6].color = d3d_verts[7].color = d3d_verts[8].color =
    d3d_verts[9].color = d3d_verts[10].color = d3d_verts[11].color = 0xffffff;

    GetClientRect(d3d_device_window, &w_rect);
    d3d_size(w_rect, &l, &t, &r, &b, w, h);

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
	hbsr = hr = d3ddev->BeginScene();

    if (hr == D3D_OK) {
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
    }

    if (hbsr == D3D_OK)
	hr = d3ddev->EndScene();

    if (hr == D3D_OK)
	hr = d3ddev->Present(NULL, NULL, d3d_device_window, NULL);

    if (hr == D3DERR_DEVICELOST || hr == D3DERR_INVALIDCALL)
	PostMessage(hwndMain, WM_RESETD3D, 0, 0);
}


static void
d3d_blit(int x, int y, int y1, int y2, int w, int h)
{
    HRESULT hr = D3D_OK;
    HRESULT hbsr = D3D_OK;
    VOID* pVoid;
    D3DLOCKED_RECT dr;
    RECT r;
    int yy;

    if ((y1 == y2) || (h <= 0)) {
	video_blit_complete();
	return; /*Nothing to do*/
    }

    r.top    = y1;
    r.left   = 0;
    r.bottom = y2;
    r.right  = 2047;

    hr = d3dTexture->LockRect(0, &dr, &r, 0);
    if (hr == D3D_OK) {	
	for (yy = y1; yy < y2; yy++) {
		if ((y + yy) >= 0 && (y + yy) < buffer32->h)
			memcpy((void *)((uintptr_t)dr.pBits + ((yy - y1) * dr.Pitch)), &(((uint32_t *)buffer32->line[yy + y])[x]), w * 4);
	}

	video_blit_complete();
	d3dTexture->UnlockRect(0);
    } else {
	video_blit_complete();
	return;
    }

    d3d_verts[0].tu = d3d_verts[2].tu = d3d_verts[3].tu = 0;//0.5 / 2048.0;
    d3d_verts[0].tv = d3d_verts[3].tv = d3d_verts[4].tv = 0;//0.5 / 2048.0;
    d3d_verts[1].tu = d3d_verts[4].tu = d3d_verts[5].tu = (float)w / 2048.0;
    d3d_verts[1].tv = d3d_verts[2].tv = d3d_verts[5].tv = (float)h / 2048.0;
    d3d_verts[0].color = d3d_verts[1].color = d3d_verts[2].color =
    d3d_verts[3].color = d3d_verts[4].color = d3d_verts[5].color =
    d3d_verts[6].color = d3d_verts[7].color = d3d_verts[8].color =
    d3d_verts[9].color = d3d_verts[10].color = d3d_verts[11].color = 0xffffff;

    GetClientRect(d3d_hwnd, &r);
    d3d_verts[0].x = d3d_verts[2].x = d3d_verts[3].x = -0.5;
    d3d_verts[0].y = d3d_verts[3].y = d3d_verts[4].y = -0.5;
    d3d_verts[1].x = d3d_verts[4].x = d3d_verts[5].x = (r.right-r.left)-0.5;
    d3d_verts[1].y = d3d_verts[2].y = d3d_verts[5].y = (r.bottom-r.top)-0.5;
    d3d_verts[6].x = d3d_verts[8].x = d3d_verts[9].x = (r.right-r.left)-40.5;
    d3d_verts[6].y = d3d_verts[9].y = d3d_verts[10].y = 8.5;
    d3d_verts[7].x = d3d_verts[10].x = d3d_verts[11].x = (r.right-r.left)-8.5;
    d3d_verts[7].y = d3d_verts[8].y = d3d_verts[11].y = 14.5;

    if (hr == D3D_OK)
	hr = v_buffer->Lock(0, 0, (void**)&pVoid, 0);    // lock the vertex buffer
    if (hr == D3D_OK)
	memcpy(pVoid, d3d_verts, sizeof(d3d_verts));    // copy the vertices to the locked buffer
    if (hr == D3D_OK)
	hr = v_buffer->Unlock();    // unlock the vertex buffer

    if (hr == D3D_OK)	
	hbsr = hr = d3ddev->BeginScene();

    if (hr == D3D_OK) {
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
    }

    if (hbsr == D3D_OK)
	hr = d3ddev->EndScene();

    if (hr == D3D_OK)
	hr = d3ddev->Present(NULL, NULL, d3d_hwnd, NULL);

    if (hr == D3DERR_DEVICELOST || hr == D3DERR_INVALIDCALL)
		PostMessage(d3d_hwnd, WM_RESETD3D, 0, 0);
}


static void
d3d_init_objects(void)
{
    D3DLOCKED_RECT dr;
    RECT r;
    int y;

    if (FAILED(d3ddev->CreateVertexBuffer(12*sizeof(CUSTOMVERTEX),
		   0,
		   D3DFVF_XYZRHW | D3DFVF_DIFFUSE | D3DFVF_TEX1,
		   D3DPOOL_MANAGED,
		   &v_buffer,
		   NULL)))
	fatal("CreateVertexBuffer failed\n");

    if (FAILED(d3ddev->CreateTexture(2048, 2048, 1, 0,
		D3DFMT_X8R8G8B8, D3DPOOL_MANAGED, &d3dTexture, NULL)))
	fatal("CreateTexture failed\n");

    r.top    = r.left  = 0;
    r.bottom = r.right = 2047;

    if (FAILED(d3dTexture->LockRect(0, &dr, &r, 0)))
			fatal("LockRect failed\n");

    for (y = 0; y < 2048; y++) {
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


int
d3d_init(HWND h)
{
    int c;

    for (c = 0; c < 256; c++)
	pal_lookup[c] = makecol(cgapal[c].r << 2,
				cgapal[c].g << 2, cgapal[c].b << 2);

    d3d_hwnd = h;

    d3d = Direct3DCreate9(D3D_SDK_VERSION);

    memset(&d3dpp, 0, sizeof(d3dpp));      
    d3dpp.Flags = 0;
    d3dpp.SwapEffect = D3DSWAPEFFECT_DISCARD;
    d3dpp.hDeviceWindow = h;
    d3dpp.BackBufferCount = 1;
    d3dpp.MultiSampleType = D3DMULTISAMPLE_NONE;
    d3dpp.MultiSampleQuality = 0;
    d3dpp.EnableAutoDepthStencil = false;
    d3dpp.AutoDepthStencilFormat = D3DFMT_UNKNOWN;
    d3dpp.PresentationInterval = D3DPRESENT_INTERVAL_IMMEDIATE;
    d3dpp.Windowed = true;
    d3dpp.BackBufferFormat = D3DFMT_UNKNOWN;
    d3dpp.BackBufferWidth = 0;
    d3dpp.BackBufferHeight = 0;

    if (FAILED(d3d->CreateDevice(D3DADAPTER_DEFAULT,
				 D3DDEVTYPE_HAL, h,
				 D3DCREATE_SOFTWARE_VERTEXPROCESSING,
				 &d3dpp, &d3ddev)))
	fatal("CreateDevice failed\n");

    d3d_init_objects();

    video_setblit(d3d_blit);

    return(1);
}


int
d3d_init_fs(HWND h)
{
    WCHAR title[200];
    int c;

    d3d_w = GetSystemMetrics(SM_CXSCREEN);
    d3d_h = GetSystemMetrics(SM_CYSCREEN);

    d3d_hwnd = h;
    for (c = 0; c < 256; c++)
	pal_lookup[c] = makecol(cgapal[c].r << 2,
				cgapal[c].g << 2, cgapal[c].b << 2);

    /*FIXME: should be done once, in win.c */
    _swprintf(title, L"%s v%s", EMU_NAME_W, EMU_VERSION_W);
    d3d_device_window = CreateWindowEx (
		0,
		SUB_CLASS_NAME,
		title,
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
    d3dpp.Flags = 0;
    d3dpp.SwapEffect = D3DSWAPEFFECT_DISCARD;
    d3dpp.hDeviceWindow = d3d_device_window;
    d3dpp.BackBufferCount = 1;
    d3dpp.MultiSampleType = D3DMULTISAMPLE_NONE;
    d3dpp.MultiSampleQuality = 0;
    d3dpp.EnableAutoDepthStencil = false;
    d3dpp.AutoDepthStencilFormat = D3DFMT_UNKNOWN;
    d3dpp.PresentationInterval = D3DPRESENT_INTERVAL_IMMEDIATE;
    d3dpp.Windowed = false;
    d3dpp.BackBufferFormat = D3DFMT_X8R8G8B8;
    d3dpp.BackBufferWidth = d3d_w;
    d3dpp.BackBufferHeight = d3d_h;

    if (FAILED(d3d->CreateDevice(D3DADAPTER_DEFAULT,
				 D3DDEVTYPE_HAL, h,
				 D3DCREATE_SOFTWARE_VERTEXPROCESSING,
				 &d3dpp, &d3ddev)))
	fatal("CreateDevice failed\n");
	
    d3d_init_objects();

    video_setblit(d3d_blit_fs);

    return(1);
}


static void
d3d_close_objects(void)
{
    if (d3dTexture) {
	d3dTexture->Release();
	d3dTexture = NULL;
    }
    if (v_buffer) {
	v_buffer->Release();
	v_buffer = NULL;
    }
}


void
d3d_close(void)
{       
    d3d_close_objects();

    if (d3ddev) {
	d3ddev->Release();
	d3ddev = NULL;
    }
    if (d3d) {
	d3d->Release();
	d3d = NULL;
    }

    if (d3d_device_window != NULL) {
	DestroyWindow(d3d_device_window);
	d3d_device_window = NULL;
    }
}


void
d3d_reset(void)
{
    HRESULT hr;

    if (! d3ddev) return;

    memset(&d3dpp, 0, sizeof(d3dpp));      

    d3dpp.Flags = 0;
    d3dpp.SwapEffect = D3DSWAPEFFECT_DISCARD;
    d3dpp.hDeviceWindow = d3d_hwnd;
    d3dpp.BackBufferCount = 1;
    d3dpp.MultiSampleType = D3DMULTISAMPLE_NONE;
    d3dpp.MultiSampleQuality = 0;
    d3dpp.EnableAutoDepthStencil = false;
    d3dpp.AutoDepthStencilFormat = D3DFMT_UNKNOWN;
    d3dpp.PresentationInterval = D3DPRESENT_INTERVAL_IMMEDIATE;
    d3dpp.Windowed = true;
    d3dpp.BackBufferFormat = D3DFMT_UNKNOWN;
    d3dpp.BackBufferWidth = 0;
    d3dpp.BackBufferHeight = 0;

    hr = d3ddev->Reset(&d3dpp);

    if (hr == D3DERR_DEVICELOST) return;

    d3ddev->SetTextureStageState(0,D3DTSS_COLOROP,   D3DTOP_SELECTARG1);
    d3ddev->SetTextureStageState(0,D3DTSS_COLORARG1, D3DTA_TEXTURE);
    d3ddev->SetTextureStageState(0,D3DTSS_ALPHAOP,   D3DTOP_DISABLE);

    d3ddev->SetSamplerState(0, D3DSAMP_MAGFILTER, D3DTEXF_LINEAR);
    d3ddev->SetSamplerState(0, D3DSAMP_MINFILTER, D3DTEXF_LINEAR);

    device_force_redraw();
}


void
d3d_reset_fs(void)
{
    HRESULT hr;

    memset(&d3dpp, 0, sizeof(d3dpp));      
    d3dpp.Flags = 0;
    d3dpp.SwapEffect = D3DSWAPEFFECT_DISCARD;
    d3dpp.hDeviceWindow = d3d_device_window;
    d3dpp.BackBufferCount = 1;
    d3dpp.MultiSampleType = D3DMULTISAMPLE_NONE;
    d3dpp.MultiSampleQuality = 0;
    d3dpp.EnableAutoDepthStencil = false;
    d3dpp.AutoDepthStencilFormat = D3DFMT_UNKNOWN;
    d3dpp.PresentationInterval = D3DPRESENT_INTERVAL_IMMEDIATE;
    d3dpp.Windowed = false;
    d3dpp.BackBufferFormat = D3DFMT_X8R8G8B8;
    d3dpp.BackBufferWidth = d3d_w;
    d3dpp.BackBufferHeight = d3d_h;

    hr = d3ddev->Reset(&d3dpp);
    if (hr == D3DERR_DEVICELOST) return;

    d3ddev->SetTextureStageState(0,D3DTSS_COLOROP,   D3DTOP_SELECTARG1);
    d3ddev->SetTextureStageState(0,D3DTSS_COLORARG1, D3DTA_TEXTURE);
    d3ddev->SetTextureStageState(0,D3DTSS_ALPHAOP,   D3DTOP_DISABLE);

    d3ddev->SetSamplerState(0, D3DSAMP_MAGFILTER, D3DTEXF_LINEAR);
    d3ddev->SetSamplerState(0, D3DSAMP_MINFILTER, D3DTEXF_LINEAR);

    device_force_redraw();
}


void
d3d_resize(int x, int y)
{
    d3dpp.BackBufferWidth  = x;
    d3dpp.BackBufferHeight = y;

    d3d_reset();
}


int
d3d_pause(void)
{
    return(0);
}


void
d3d_take_screenshot(wchar_t *fn)
{
    LPDIRECT3DSURFACE9 d3dSurface = NULL;

    if (! d3dTexture) return;

    d3ddev->GetBackBuffer(0, 0, D3DBACKBUFFER_TYPE_MONO, &d3dSurface);
    D3DXSaveSurfaceToFile(fn, D3DXIFF_PNG, d3dSurface, NULL, NULL);

    d3dSurface->Release();
    d3dSurface = NULL;
}
