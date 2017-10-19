/*
 * 86Box	A hypervisor and IBM PC system emulator that specializes in
 *		running old operating systems and software designed for IBM
 *		PC systems and compatibles from 1981 through fairly recent
 *		system designs based on the PCI bus.
 *
 *		This file is part of the 86Box distribution.
 *
 *		Platform video API support for Win32.
 *
 * Version:	@(#)win_video.c	1.0.1	2017/10/18
 *
 * Author:	Fred N. van Kempen, <decwiz@yahoo.com>
 *
 *		Copyright 2017 Fred N. van Kempen.
 */
#define UNICODE
#define BITMAP WINDOWS_BITMAP
#include <windows.h>
#include <windowsx.h>
#undef BITMAP
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <stdlib.h>
#include <time.h>
#include <wchar.h>
#include "../86box.h"
#include "../config.h"
#include "../device.h"
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
#include "win.h"
#include "win_ddraw.h"
#include "win_d3d.h"


static struct {
    char	*name;
    int		local;
    int		(*init)(void *);
    void	(*close)(void);
    void	(*resize)(int x, int y);
    int		(*pause)(void);
} vid_apis[2][4] = {
  {
    {	"DDraw", 1, (int(*)(void*))ddraw_init, ddraw_close, NULL, ddraw_pause		},
    {	"D3D", 1, (int(*)(void*))d3d_init, d3d_close, d3d_resize, d3d_pause		},
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
    {	"DDraw", 1, (int(*)(void*))ddraw_fs_init, ddraw_fs_close, NULL, ddraw_fs_pause	},
    {	"D3D", 1, (int(*)(void*))d3d_fs_init, d3d_fs_close, NULL, d3d_fs_pause		},
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


#if 0
    /* Initialize the rendering window, or fullscreen. */
    if (start_in_fullscreen) {
	startblit();
	vid_apis[0][vid_api].close();
	video_fullscreen = 1;
	vid_apis[1][vid_api].init(hwndRender);
	leave_fullscreen_flag = 0;
	endblit();
	device_force_redraw();
    }
#endif


/* Return the VIDAPI number for the given name. */
int
plat_vidapi(char *name)
{
    int i;

    if (!strcasecmp(name, "default") || !strcasecmp(name, "system")) return(0);

    for (i=0; i<4; i++) {
	if (vid_apis[0][i].name &&
	    !strcasecmp(vid_apis[0][i].name, name)) return(i);
    }

    /* Default value. */
    return(0);
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
#ifdef USE_WX
    ui_check_menu_item(IDM_View_WX+vid_api, 0);
#endif
    vid_api = api;

    if (vid_apis[0][vid_api].local)
	ShowWindow(hwndRender, SW_SHOW);
      else
	ShowWindow(hwndRender, SW_HIDE);

    /* Initialize the (new) API. */
#ifdef USE_WX
    ui_check_menu_item(IDM_View_WX+vid_api, 1);
#endif
    i = vid_apis[0][vid_api].init((void *)hwndRender);
    endblit();
    if (! i) return(0);

    device_force_redraw();

    return(1);
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

    /* Want off and already off? */
    if (!on && !video_fullscreen) return;

    /* Want on and already on? */
    if (on && video_fullscreen) return;

    if (!on && !flag) {
	/* We want to leave FS mode. */
	flag = 1;

	return;
    }

    if (video_fullscreen_first) {
	video_fullscreen_first = 0;
	ui_msgbox(MBX_INFO, (wchar_t *)IDS_2074);
    }

    /* OK, claim the video. */
    startblit();
    video_wait_for_blit();

    mouse_close();

    /* Close the current mode, and open the new one. */
    vid_apis[video_fullscreen][vid_api].close();
    video_fullscreen = on;
    vid_apis[video_fullscreen][vid_api].init(NULL);
    flag = 0;

    mouse_init();

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

#ifdef WIN32
    wcscat(path, L"\\");
#else
    wcscat(path, L"/");
#endif

    switch(vid_api) {
	case 0:		/* ddraw */
		wcsftime(path, 128, L"%Y%m%d_%H%M%S.bmp", info);
		plat_append_filename(path, cfg_path, fn, 1024);
		if (video_fullscreen)
			ddraw_fs_take_screenshot(path);
		  else
			ddraw_take_screenshot(path);
		pclog("Screenshot: fn='%ls'\n", path);
		break;

	case 1:		/* d3d9 */
		wcsftime(fn, 128, L"%Y%m%d_%H%M%S.png", info);
		plat_append_filename(path, cfg_path, fn, 1024);
		if (video_fullscreen)
			d3d_fs_take_screenshot(path);
		  else
			d3d_take_screenshot(path);
		pclog("Screenshot: fn='%ls'\n", path);
		break;

#ifdef USE_VNC
	case 2:		/* vnc */
		wcsftime(fn, 128, L"%Y%m%d_%H%M%S.png", info);
		plat_append_filename(path, cfg_path, fn, 1024);
		vnc_take_screenshot(path);
		pclog("Screenshot: fn='%ls'\n", path);
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


/* Tell the UI and/or renderers about a new screen resolution. */
void
plat_resize(int x, int y)
{
pclog("PLAT: VID[%d,%d] resizing to %dx%d\n", video_fullscreen, vid_api, x, y);
    /* First, see if we should resize the UI window. */
    if (vid_resize) {
	/* Move the main window. */

	/* Move the status bar with it. */
	MoveWindow(hwndSBAR, 0, y+6, x, 17, TRUE);

	/* Move the render window if we have one. */
	if (vid_apis[0][vid_api].local && (hwndRender != NULL)) {
		MoveWindow(hwndRender, 0, 0, x, y, TRUE);
	}
    }

    /* Now, tell the renderer about the new screen size we want. */
    if (vid_apis[video_fullscreen][vid_api].resize) {
	startblit();
	vid_apis[video_fullscreen][vid_api].resize(x, y);
	endblit();
    }
}
