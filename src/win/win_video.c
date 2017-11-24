/*
 * 86Box	A hypervisor and IBM PC system emulator that specializes in
 *		running old operating systems and software designed for IBM
 *		PC systems and compatibles from 1981 through fairly recent
 *		system designs based on the PCI bus.
 *
 *		This file is part of the 86Box distribution.
 *
 *		Video API platform support for Windows.
 *
 * Version:	@(#)win_video.c	1.0.8	2017/11/18
 *
 * Author:	Fred N. van Kempen, <decwiz@yahoo.com>
 *
 *		Copyright 2017 Fred N. van Kempen.
 */
#define UNICODE
#include <windows.h>
#include <windowsx.h>
#include <commctrl.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <stdlib.h>
#include <time.h>
#include <wchar.h>
#include "../86box.h"
#include "../config.h"
#include "../device.h"
#include "../mouse.h"
#include "../video/video.h"
#include "../plat.h"
#include "../plat_mouse.h"
#include "../ui.h"
#ifdef USE_VNC
# include "../vnc.h"
#endif
#ifdef USE_RDP
# include "../rdp.h"
#endif
#ifdef USE_WX
# include "../wx/wx_ui.h"
#else
# include "win_ddraw.h"
# include "win_d3d.h"
#endif
#include "win.h"


static struct {
    char	*name;
    int		local;
    int		(*init)(void *);
    void	(*close)(void);
    void	(*resize)(int x, int y);
    int		(*pause)(void);
} vid_apis[2][4] = {
  {
#ifdef USE_WX
    {	"WxWidgets", 1, wx_init, wx_close, NULL, wx_pause		},
    {	"WxWidgets", 1, wx_init, wx_close, NULL, wx_pause		},
#else
    {	"DDraw", 1, (int(*)(void*))ddraw_init, ddraw_close, NULL, ddraw_pause		},
    {	"D3D", 1, (int(*)(void*))d3d_init, d3d_close, d3d_resize, d3d_pause		},
#endif
#ifdef USE_VNC
    {	"VNC", 0, vnc_init, vnc_close, vnc_resize, vnc_pause		},
#else
    {	NULL, 0, NULL, NULL, NULL, NULL					},
#endif
#ifdef USE_RDP
    {	"RDP", 0, rdp_init, rdp_close, rdp_resize, rdp_pause		}
#else
    {	NULL, 0, NULL, NULL, NULL, NULL					}
#endif
  },
  {
#ifdef USE_WX
    {	"WxWidgets", 1, wx_init, wx_close, NULL, wx_pause		},
    {	"WxWidgets", 1, wx_init, wx_close, NULL, wx_pause		},
#else
    {	"DDraw", 1, (int(*)(void*))ddraw_init_fs, ddraw_close, NULL, ddraw_pause	},
    {	"D3D", 1, (int(*)(void*))d3d_init_fs, d3d_close, NULL, d3d_pause		},
#endif
#ifdef USE_VNC
    {	"VNC", 0, vnc_init, vnc_close, vnc_resize, vnc_pause		},
#else
    {	NULL, 0, NULL, NULL, NULL, NULL					},
#endif
#ifdef USE_RDP
    {	"RDP", 0, rdp_init, rdp_close, rdp_resize, rdp_pause		}
#else
    {	NULL, 0, NULL, NULL, NULL, NULL					}
#endif
  },
};


/* Return the VIDAPI number for the given name. */
int
plat_vidapi(char *name)
{
    int i;

    if (!strcasecmp(name, "default") || !strcasecmp(name, "system")) return(1);

    for (i=0; i<4; i++) {
	if (vid_apis[0][i].name &&
	    !strcasecmp(vid_apis[0][i].name, name)) return(i);
    }

    /* Default value. */
    return(1);
}


/* Return the VIDAPI name for the given number. */
char *
plat_vidapi_name(int api)
{
    char *name = "default";

    switch(api) {
#if USE_WX
	case 0:
		break;

	case 1:
		name = "wxwidgets";
		break;
#else
	case 0:
		name = "ddraw";
		break;

	case 1:
#if 0
		/* Direct3D is default. */
		name = "d3d";
#endif
		break;
#endif

#ifdef USE_VNC
	case 2:
		name = "vnc";
		break;

#endif
#ifdef USE_RDP
	case 3:
		name = "rdp";
		break;
#endif
    }

    return(name);
}


int
plat_setvid(int api)
{
    int i;

    pclog("Initializing VIDAPI: api=%d\n", api);
    startblit();
    video_wait_for_blit();

    /* Close the (old) API. */
    vid_apis[0][vid_api].close();
//#ifdef USE_WX
//    ui_check_menu_item(IDM_View_WX+vid_api, 0);
//#endif
    vid_api = api;

#ifndef USE_WX
    if (vid_apis[0][vid_api].local)
	ShowWindow(hwndRender, SW_SHOW);
      else
	ShowWindow(hwndRender, SW_HIDE);
#endif

    /* Initialize the (new) API. */
#ifdef USE_WX
//    ui_check_menu_item(IDM_View_WX+vid_api, 1);
    i = vid_apis[0][vid_api].init(NULL);
#else
    i = vid_apis[0][vid_api].init((void *)hwndRender);
#endif
    endblit();
    if (! i) return(0);

    device_force_redraw();

    return(1);
}


/* Tell the renderers about a new screen resolution. */
void
plat_vidsize(int x, int y)
{
    if (! vid_apis[video_fullscreen][vid_api].resize) return;

    startblit();
    video_wait_for_blit();
    vid_apis[video_fullscreen][vid_api].resize(x, y);
    endblit();
}


int
get_vidpause(void)
{
    return(vid_apis[video_fullscreen][vid_api].pause());
}


void
plat_setfullscreen(int on)
{
    static int flag = 0;
    HWND *hw;

    /* Want off and already off? */
    if (!on && !video_fullscreen) return;

    /* Want on and already on? */
    if (on && video_fullscreen) return;

    if (!on && !flag) {
	/* We want to leave FS mode. */
	flag = 1;

#ifdef USE_WX
	goto doit;
#endif
	return;
    }

    if (video_fullscreen_first) {
	video_fullscreen_first = 0;
	ui_msgbox(MBX_INFO, (wchar_t *)IDS_2074);
    }

    /* OK, claim the video. */
#ifdef USE_WX
doit:
#endif
    startblit();
    video_wait_for_blit();

    mouse_close();

    /* Close the current mode, and open the new one. */
    vid_apis[video_fullscreen][vid_api].close();
    video_fullscreen = on;
    hw = (video_fullscreen) ? &hwndMain : &hwndRender;
    vid_apis[video_fullscreen][vid_api].init((void *) *hw);
    flag = 0;

#ifdef USE_WX
    wx_set_fullscreen(on);
#endif

    mouse_init();
    leave_fullscreen_flag = 0;

    /* Release video and make it redraw the screen. */
    endblit();
    device_force_redraw();
}


void
take_screenshot(void)
{
    wchar_t path[1024], fn[128];
    struct tm *info;
    time_t now;

    pclog("Screenshot: video API is: %i\n", vid_api);
    if ((vid_api < 0) || (vid_api > 1)) return;

    memset(fn, 0, sizeof(fn));
    memset(path, 0, sizeof(path));

    (void)time(&now);
    info = localtime(&now);

    plat_append_filename(path, cfg_path, SCREENSHOT_PATH, sizeof(path)-2);

    if (! plat_dir_check(path))
	plat_dir_create(path);

    wcscat(path, L"\\");

    switch(vid_api) {
#ifdef USE_WX
	case 0:
	case 1:
		wcsftime(fn, 128, L"%Y%m%d_%H%M%S.png", info);
		wcscat(path, fn);
		wx_screenshot(path);
		break;
#else
	case 0:		/* ddraw */
		wcsftime(fn, 128, L"%Y%m%d_%H%M%S.bmp", info);
		wcscat(path, fn);
		ddraw_take_screenshot(path);
		break;

	case 1:		/* d3d9 */
		wcsftime(fn, 128, L"%Y%m%d_%H%M%S.png", info);
		wcscat(path, fn);
		d3d_take_screenshot(path);
		break;
#endif

#ifdef USE_VNC
	case 2:		/* vnc */
		wcsftime(fn, 128, L"%Y%m%d_%H%M%S.png", info);
		wcscat(path, fn);
		vnc_take_screenshot(path);
		break;
#endif
    }
}


void	/* plat_ */
startblit(void)
{
    WaitForSingleObject(ghMutex, INFINITE);
}


void	/* plat_ */
endblit(void)
{
    ReleaseMutex(ghMutex);
}


#if 0
/* Tell the UI and/or renderers about a new screen resolution. */
void
plat_resize(int x, int y)
{
pclog("PLAT: VID[%d,%d] resizing to %dx%d\n", video_fullscreen, vid_api, x, y);

    /* Do not accept these sizes. */
    if (x==0 || x<320 || y==0 || y<200) return;

    /* First, see if we should resize the UI window. */
    if (! vid_resize)
#ifdef USE_WX
	wx_resize(x, y);
#else
	ui_resize(x, y);
#endif

#if 0
    /* Now, tell the renderer about the new screen size we want. */
    if (vid_apis[video_fullscreen][vid_api].resize) {
	startblit();
	vid_apis[video_fullscreen][vid_api].resize(x, y);
	endblit();
    }
#endif
}


void
plat_mouse_capture(int on)
{
    if (on && !mouse_capture) {
	/* Enable the in-app mouse. */
#ifdef USE_WX
	wx_capture_mouse(1);
#endif

	mouse_capture = 1;
    } else if (!on && mouse_capture) {
	/* Disable the in-app mouse. */
#ifdef USE_WX
	wx_capture_mouse(0);
#endif

	mouse_capture = 0;
    }
}
#endif
