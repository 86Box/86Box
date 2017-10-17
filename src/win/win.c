/*
 * 86Box	A hypervisor and IBM PC system emulator that specializes in
 *		running old operating systems and software designed for IBM
 *		PC systems and compatibles from 1981 through fairly recent
 *		system designs based on the PCI bus.
 *
 *		This file is part of the 86Box distribution.
 *
 *		The Emulator's Windows core.
 *
 * Version:	@(#)win.c	1.0.25	2017/10/16
 *
 * Authors:	Sarah Walker, <http://pcem-emulator.co.uk/>
 *		Miran Grca, <mgrca8@gmail.com>
 *		Fred N. van Kempen, <decwiz@yahoo.com>
 *
 *		Copyright 2008-2017 Sarah Walker.
 *		Copyright 2016,2017 Miran Grca.
 *		Copyright 2017 Fred N. van Kempen.
 */
#define UNICODE
#define BITMAP WINDOWS_BITMAP
#include <windows.h>
#include <windowsx.h>
#include <commctrl.h>
#include <commdlg.h>
#include <fcntl.h>
#include <io.h>
#include <process.h>
#include <shlobj.h>
#undef BITMAP
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <stdlib.h>
#include <time.h>
#include <wchar.h>
#include "../86box.h"
#include "../config.h"
#include "../ibm.h"
#include "../mem.h"			// because of load_config
#include "../rom.h"			// because of load_config
#include "../device.h"
#include "../nvr.h"
#include "../mouse.h"
#include "../cdrom/cdrom.h"
#include "../cdrom/cdrom_image.h"
#include "../cdrom/cdrom_null.h"
#include "../floppy/floppy.h"
#include "../scsi/scsi.h"
#include "../network/network.h"
#include "../video/video.h"
#include "../video/vid_ega.h"		// for update_overscan
#include "../sound/sound.h"
#define GLOBAL
#include "../plat.h"
#include "../plat_keyboard.h"
#include "../plat_mouse.h"
#include "../plat_midi.h"
#include "../ui.h"
#include "win.h"
#include "win_ddraw.h"
#include "win_d3d.h"
#ifdef USE_VNC
# include "win_vnc.h"
#endif


#define TIMER_1SEC	1


typedef struct {
    WCHAR str[512];
} rc_str_t;


extern int	status_update_needed;


/* Public data, more or less non-specific to platform. */
int		scale = 0;
uint64_t	timer_freq;
int		winsizex = 640,
		winsizey = 480;
int		efwinsizey = 480;
int		gfx_present[GFX_MAX];
int		drawits = 0;
int		mousecapture = 0;
uint64_t	main_time;

/* Public data, specific to platform. */
HWND		hwndMain;
HMENU		menuMain;
HANDLE		ghMutex;
HINSTANCE	hinstance;
HICON		hIcon[512];
RECT		oldclip;
LCID		dwLanguage;
int		infocus = 1;
int		recv_key[272];
uint32_t	dwLangID,
		dwSubLangID;

char		openfilestring[260];
WCHAR		wopenfilestring[260];


/* Local data. */
static HANDLE	thMain;
static HWND	hwndRender;		/* machine render window */
static wchar_t	wTitle[512];
static RAWINPUTDEVICE	device;
static HHOOK	hKeyboardHook;
static int	hook_enabled = 0;
static int	save_window_pos = 0;
static int	doresize = 0;
static int	quited;
static int	leave_fullscreen_flag = 0;
static int	unscaled_size_x = 0;
static int	unscaled_size_y = 0;
static uint64_t	start_time;
static uint64_t	end_time;
static wchar_t	**argv;
static int	argc;
static wchar_t	*argbuf;
static rc_str_t	*lpRCstr2048,
		*lpRCstr3072,
		*lpRCstr4096,
		*lpRCstr4352,
		*lpRCstr4608,
		*lpRCstr5120,
		*lpRCstr5376,
		*lpRCstr5632,
		*lpRCstr6144;
static struct {
    int		local;
    int		(*init)(HWND h);
    void	(*close)(void);
    void	(*resize)(int x, int y);
    int		(*pause)(void);
} vid_apis[2][4] = {
  {
    {	1, ddraw_init, ddraw_close, NULL, ddraw_pause		},
    {	1, d3d_init, d3d_close, d3d_resize, d3d_pause		},
#ifdef USE_VNC
    {	0, vnc_init, vnc_close, vnc_resize, vnc_pause		},
#else
    {	0, NULL, NULL, NULL, NULL				},
#endif
#ifdef USE_RDP
    {	0, rdp_init, rdp_close, rdp_resize, rdp_pause		}
#else
    {	0, NULL, NULL, NULL, NULL				}
#endif
  },
  {
    {	1, ddraw_fs_init, ddraw_fs_close, NULL, ddraw_fs_pause	},
    {	1, d3d_fs_init, d3d_fs_close, NULL, d3d_fs_pause	},
#ifdef USE_VNC
    {	0, vnc_init, vnc_close, vnc_resize, vnc_pause		},
#else
    {	0, NULL, NULL, NULL, NULL				},
#endif
#ifdef USE_RDP
    {	0, rdp_init, rdp_close, rdp_resize, rdp_pause		}
#else
    {	0, NULL, NULL, NULL, NULL				}
#endif
  }
};


HICON
LoadIconEx(PCTSTR pszIconName)
{
    return((HICON)LoadImage(hinstance, pszIconName, IMAGE_ICON,
						16, 16, LR_SHARED));
}


#if 0
static void
win_menu_update(void)
{
    menuMain = LoadMenu(hinstance, L"MainMenu"));

    menuSBAR = LoadMenu(hinstance, L"StatusBarMenu");

    initmenu();

    SetMenu(hwndMain, menu);

    win_title_update = 1;
}
#endif


static void
releasemouse(void)
{
    if (mousecapture) {
	ClipCursor(&oldclip);
	ShowCursor(TRUE);
	mousecapture = 0;
    }
}


static void
video_toggle_option(HMENU h, int *val, int id)
{
    startblit();
    video_wait_for_blit();
    *val ^= 1;
    CheckMenuItem(h, id, *val ? MF_CHECKED : MF_UNCHECKED);
    endblit();
    config_save();
    device_force_redraw();
}


/* The main thread runs the actual emulator code. */
static void
MainThread(LPVOID param)
{
    int sb_borders[3];
    DWORD old_time, new_time;
    int frames = 0;
    RECT r;

    drawits = 0;
    old_time = plat_get_ticks();
    while (! quited) {
	if (status_update_needed) {
		if (hwndStatus != NULL)
			SendMessage(hwndStatus, WM_USER, 0, 0);
		status_update_needed = 0;
	}

	new_time = plat_get_ticks();
	drawits += new_time - old_time;
	old_time = new_time;
	if (drawits > 0 && !dopause) {
		start_time = plat_timer_read();
		drawits -= 10;
		if (drawits > 50) drawits = 0;
		pc_run();

		if (++frames >= 200 && nvr_dosave) {
			frames = 0;
			nvr_save();
			nvr_dosave = 0;
		}

		end_time = plat_timer_read();
		main_time += end_time - start_time;
	} else
		plat_delay_ms(1);

	if (!video_fullscreen && vid_apis[0][vid_api].local &&
	    doresize && (winsizex>0) && (winsizey>0)) {
		SendMessage(hwndSBAR, SB_GETBORDERS, 0, (LPARAM) sb_borders);
		GetWindowRect(hwndMain, &r);
		MoveWindow(hwndRender, 0, 0, winsizex, winsizey, TRUE);
		GetWindowRect(hwndRender, &r);
		MoveWindow(hwndSBAR,
			   0, r.bottom + GetSystemMetrics(SM_CYEDGE),
			   winsizex, 17, TRUE);
		GetWindowRect(hwndMain, &r);

		MoveWindow(hwndMain, r.left, r.top,
			   winsizex + (GetSystemMetrics(vid_resize ? SM_CXSIZEFRAME : SM_CXFIXEDFRAME) * 2),
			   winsizey + (GetSystemMetrics(SM_CYEDGE) * 2) + (GetSystemMetrics(vid_resize ? SM_CYSIZEFRAME : SM_CYFIXEDFRAME) * 2) + GetSystemMetrics(SM_CYMENUSIZE) + GetSystemMetrics(SM_CYCAPTION) + 17 + sb_borders[1] + 1,
			   TRUE);

		if (mousecapture) {
			GetWindowRect(hwndRender, &r);
			ClipCursor(&r);
		}

		doresize = 0;
	}

	if (leave_fullscreen_flag) {
		leave_fullscreen_flag = 0;

		SendMessage(hwndMain, WM_LEAVEFULLSCREEN, 0, 0);
	}

	if (video_fullscreen && infocus)
		SetCursorPos(9999, 9999);
    }
}


static void
ResetAllMenus(void)
{
#ifdef ENABLE_LOG_TOGGLES
# ifdef ENABLE_BUSLOGIC_LOG
    CheckMenuItem(menuMain, IDM_LOG_BUSLOGIC, MF_UNCHECKED);
# endif
# ifdef ENABLE_CDROM_LOG
    CheckMenuItem(menuMain, IDM_LOG_CDROM, MF_UNCHECKED);
# endif
# ifdef ENABLE_D86F_LOG
    CheckMenuItem(menuMain, IDM_LOG_D86F, MF_UNCHECKED);
# endif
# ifdef ENABLE_FDC_LOG
    CheckMenuItem(menuMain, IDM_LOG_FDC, MF_UNCHECKED);
# endif
# ifdef ENABLE_IDE_LOG
    CheckMenuItem(menuMain, IDM_LOG_IDE, MF_UNCHECKED);
# endif
# ifdef ENABLE_SERIAL_LOG
    CheckMenuItem(menuMain, IDM_LOG_SERIAL, MF_UNCHECKED);
# endif
# ifdef ENABLE_NIC_LOG
    /*FIXME: should be network_setlog(1:0) */
    CheckMenuItem(menuMain, IDM_LOG_NIC, MF_UNCHECKED);
# endif
#endif

    CheckMenuItem(menuMain, IDM_VID_FORCE43, MF_UNCHECKED);
    CheckMenuItem(menuMain, IDM_VID_OVERSCAN, MF_UNCHECKED);
    CheckMenuItem(menuMain, IDM_VID_INVERT, MF_UNCHECKED);

    CheckMenuItem(menuMain, IDM_VID_RESIZE, MF_UNCHECKED);
    CheckMenuItem(menuMain, IDM_VID_DDRAW+0, MF_UNCHECKED);
    CheckMenuItem(menuMain, IDM_VID_DDRAW+1, MF_UNCHECKED);
#ifdef USE_VNC
    CheckMenuItem(menuMain, IDM_VID_DDRAW+2, MF_UNCHECKED);
#endif
#ifdef USE_VNC
    CheckMenuItem(menuMain, IDM_VID_DDRAW+3, MF_UNCHECKED);
#endif
    CheckMenuItem(menuMain, IDM_VID_FS_FULL+0, MF_UNCHECKED);
    CheckMenuItem(menuMain, IDM_VID_FS_FULL+1, MF_UNCHECKED);
    CheckMenuItem(menuMain, IDM_VID_FS_FULL+2, MF_UNCHECKED);
    CheckMenuItem(menuMain, IDM_VID_FS_FULL+3, MF_UNCHECKED);
    CheckMenuItem(menuMain, IDM_VID_REMEMBER, MF_UNCHECKED);
    CheckMenuItem(menuMain, IDM_VID_SCALE_1X+0, MF_UNCHECKED);
    CheckMenuItem(menuMain, IDM_VID_SCALE_1X+1, MF_UNCHECKED);
    CheckMenuItem(menuMain, IDM_VID_SCALE_1X+2, MF_UNCHECKED);
    CheckMenuItem(menuMain, IDM_VID_SCALE_1X+3, MF_UNCHECKED);

    CheckMenuItem(menuMain, IDM_VID_CGACON, MF_UNCHECKED);
    CheckMenuItem(menuMain, IDM_VID_GRAYCT_601+0, MF_UNCHECKED);
    CheckMenuItem(menuMain, IDM_VID_GRAYCT_601+1, MF_UNCHECKED);
    CheckMenuItem(menuMain, IDM_VID_GRAYCT_601+2, MF_UNCHECKED);
    CheckMenuItem(menuMain, IDM_VID_GRAY_RGB+0, MF_UNCHECKED);
    CheckMenuItem(menuMain, IDM_VID_GRAY_RGB+1, MF_UNCHECKED);
    CheckMenuItem(menuMain, IDM_VID_GRAY_RGB+2, MF_UNCHECKED);
    CheckMenuItem(menuMain, IDM_VID_GRAY_RGB+3, MF_UNCHECKED);
    CheckMenuItem(menuMain, IDM_VID_GRAY_RGB+4, MF_UNCHECKED);

#ifdef ENABLE_LOG_TOGGLES
# ifdef ENABLE_BUSLOGIC_LOG
    CheckMenuItem(menuMain, IDM_LOG_BUSLOGIC, buslogic_do_log?MF_CHECKED:MF_UNCHECKED);
# endif
# ifdef ENABLE_CDROM_LOG
    CheckMenuItem(menuMain, IDM_LOG_CDROM, cdrom_do_log?MF_CHECKED:MF_UNCHECKED);
# endif
# ifdef ENABLE_D86F_LOG
    CheckMenuItem(menuMain, IDM_LOG_D86F, d86f_do_log?MF_CHECKED:MF_UNCHECKED);
# endif
# ifdef ENABLE_FDC_LOG
    CheckMenuItem(menuMain, IDM_LOG_FDC, fdc_do_log?MF_CHECKED:MF_UNCHECKED);
# endif
# ifdef ENABLE_IDE_LOG
    CheckMenuItem(menuMain, IDM_LOG_IDE, ide_do_log?MF_CHECKED:MF_UNCHECKED);
# endif
# ifdef ENABLE_SERIAL_LOG
    CheckMenuItem(menuMain, IDM_LOG_SERIAL, serial_do_log?MF_CHECKED:MF_UNCHECKED);
# endif
# ifdef ENABLE_NIC_LOG
    CheckMenuItem(menuMain, IDM_LOG_NIC, nic_do_log?MF_CHECKED:MF_UNCHECKED);
# endif
#endif

    CheckMenuItem(menuMain, IDM_VID_FORCE43, force_43?MF_CHECKED:MF_UNCHECKED);
    CheckMenuItem(menuMain, IDM_VID_OVERSCAN, enable_overscan?MF_CHECKED:MF_UNCHECKED);
    CheckMenuItem(menuMain, IDM_VID_INVERT, invert_display ? MF_CHECKED : MF_UNCHECKED);

    if (vid_resize)
	CheckMenuItem(menuMain, IDM_VID_RESIZE, MF_CHECKED);
    CheckMenuItem(menuMain, IDM_VID_DDRAW+vid_api, MF_CHECKED);
    CheckMenuItem(menuMain, IDM_VID_FS_FULL+video_fullscreen_scale, MF_CHECKED);
    CheckMenuItem(menuMain, IDM_VID_REMEMBER, window_remember?MF_CHECKED:MF_UNCHECKED);
    CheckMenuItem(menuMain, IDM_VID_SCALE_1X+scale, MF_CHECKED);

    CheckMenuItem(menuMain, IDM_VID_CGACON, vid_cga_contrast?MF_CHECKED:MF_UNCHECKED);
    CheckMenuItem(menuMain, IDM_VID_GRAYCT_601+video_graytype, MF_CHECKED);
    CheckMenuItem(menuMain, IDM_VID_GRAY_RGB+video_grayscale, MF_CHECKED);
}


static LRESULT CALLBACK
LowLevelKeyboardProc(int nCode, WPARAM wParam, LPARAM lParam)
{
    BOOL bControlKeyDown;
    KBDLLHOOKSTRUCT* p;

    if (nCode < 0 || nCode != HC_ACTION)
	return(CallNextHookEx(hKeyboardHook, nCode, wParam, lParam));
	
    p = (KBDLLHOOKSTRUCT*)lParam;

    /* disable alt-tab */
    if (p->vkCode == VK_TAB && p->flags & LLKHF_ALTDOWN) return(1);

    /* disable alt-space */
    if (p->vkCode == VK_SPACE && p->flags & LLKHF_ALTDOWN) return(1);

    /* disable alt-escape */
    if (p->vkCode == VK_ESCAPE && p->flags & LLKHF_ALTDOWN) return(1);

    /* disable windows keys */
    if((p->vkCode == VK_LWIN) || (p->vkCode == VK_RWIN)) return(1);

    /* checks ctrl key pressed */
    bControlKeyDown = GetAsyncKeyState(VK_CONTROL)>>((sizeof(SHORT)*8)-1);

    /* disable ctrl-escape */
    if (p->vkCode == VK_ESCAPE && bControlKeyDown) return(1);

    return(CallNextHookEx(hKeyboardHook, nCode, wParam, lParam));
}


static LRESULT CALLBACK
MainWindowProcedure(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam)
{
    HMENU hmenu;
    RECT rect;
    int i = 0;

    switch (message) {
	case WM_CREATE:
		SetTimer(hwnd, TIMER_1SEC, 1000, NULL);
		hKeyboardHook = SetWindowsHookEx(WH_KEYBOARD_LL,
						 LowLevelKeyboardProc,
						 GetModuleHandle(NULL), 0);
		hook_enabled = 1;
		break;

	case WM_COMMAND:
		hmenu = GetMenu(hwnd);
		switch (LOWORD(wParam)) {
			case IDM_ACTION_SCREENSHOT:
				take_screenshot();
				break;

			case IDM_ACTION_HRESET:
				pc_reset(1);
				break;

			case IDM_ACTION_RESET_CAD:
				pc_reset(0);
				break;

			case IDM_ACTION_EXIT:
				PostQuitMessage(0);
				break;

			case IDM_ACTION_CTRL_ALT_ESC:
				pc_send_cae();
				break;

			case IDM_ACTION_PAUSE:
				plat_pause(dopause ^ 1);
				break;

			case IDM_CONFIG:
				win_settings_open(hwnd);
				break;

			case IDM_ABOUT:
				AboutDialogCreate(hwnd);
				break;

			case IDM_STATUS:
				StatusWindowCreate(hwnd);
				break;

			case IDM_VID_RESIZE:
				vid_resize = !vid_resize;
				CheckMenuItem(hmenu, IDM_VID_RESIZE, (vid_resize)? MF_CHECKED : MF_UNCHECKED);
				if (vid_resize)
					SetWindowLongPtr(hwnd, GWL_STYLE, (WS_OVERLAPPEDWINDOW & ~WS_MINIMIZEBOX) | WS_VISIBLE);
				  else
					SetWindowLongPtr(hwnd, GWL_STYLE, (WS_OVERLAPPEDWINDOW & ~WS_SIZEBOX & ~WS_THICKFRAME & ~WS_MAXIMIZEBOX & ~WS_MINIMIZEBOX) | WS_VISIBLE);
				GetWindowRect(hwnd, &rect);
				SetWindowPos(hwnd, 0, rect.left, rect.top, rect.right - rect.left, rect.bottom - rect.top, SWP_NOZORDER | SWP_FRAMECHANGED);
				GetWindowRect(hwndSBAR,&rect);
				SetWindowPos(hwndSBAR, 0, rect.left, rect.top, rect.right - rect.left, rect.bottom - rect.top, SWP_NOZORDER | SWP_FRAMECHANGED);
				if (vid_resize) {
					CheckMenuItem(hmenu, IDM_VID_SCALE_1X + scale, MF_UNCHECKED);
					CheckMenuItem(hmenu, IDM_VID_SCALE_2X, MF_CHECKED);
					scale = 1;
				}
				EnableMenuItem(hmenu, IDM_VID_SCALE_1X, vid_resize ? MF_GRAYED : MF_ENABLED);
				EnableMenuItem(hmenu, IDM_VID_SCALE_2X, vid_resize ? MF_GRAYED : MF_ENABLED);
				EnableMenuItem(hmenu, IDM_VID_SCALE_3X, vid_resize ? MF_GRAYED : MF_ENABLED);
				EnableMenuItem(hmenu, IDM_VID_SCALE_4X, vid_resize ? MF_GRAYED : MF_ENABLED);
				doresize = 1;
				config_save();
				break;

			case IDM_VID_REMEMBER:
				window_remember = !window_remember;
				CheckMenuItem(hmenu, IDM_VID_REMEMBER, window_remember ? MF_CHECKED : MF_UNCHECKED);
				GetWindowRect(hwnd, &rect);
				if (window_remember) {
					window_x = rect.left;
					window_y = rect.top;
					window_w = rect.right - rect.left;
					window_h = rect.bottom - rect.top;
				}
				config_save();
				break;

			case IDM_VID_DDRAW:
			case IDM_VID_D3D:
#ifdef USE_VNC
			case IDM_VID_VNC:
#endif
#ifdef USE_RDP
			case IDM_VID_RDP:
#endif
				startblit();
				video_wait_for_blit();
				CheckMenuItem(hmenu, IDM_VID_DDRAW+vid_api, MF_UNCHECKED);
				vid_apis[0][vid_api].close();
				vid_api = LOWORD(wParam) - IDM_VID_DDRAW;
				if (vid_apis[0][vid_api].local)
					ShowWindow(hwndRender, SW_SHOW);
				  else
					ShowWindow(hwndRender, SW_HIDE);
				CheckMenuItem(hmenu, IDM_VID_DDRAW+vid_api, MF_CHECKED);
				vid_apis[0][vid_api].init(hwndRender);
				endblit();
				device_force_redraw();
				cgapal_rebuild();
				config_save();
				break;

			case IDM_VID_FULLSCREEN:
				if (video_fullscreen == 1) break;

				if (video_fullscreen_first) {
					video_fullscreen_first = 0;
					ui_msgbox(MBX_INFO, (wchar_t *)IDS_2074);
				}

				startblit();
				video_wait_for_blit();
				mouse_close();
				vid_apis[0][vid_api].close();
				video_fullscreen = 1;
				vid_apis[1][vid_api].init(hwndMain);
				mouse_init();
				leave_fullscreen_flag = 0;
				endblit();
				device_force_redraw();
				cgapal_rebuild();
				config_save();
				break;

			case IDM_VID_FS_FULL:
			case IDM_VID_FS_43:
			case IDM_VID_FS_SQ:                                
			case IDM_VID_FS_INT:
				CheckMenuItem(hmenu, IDM_VID_FS_FULL + video_fullscreen_scale, MF_UNCHECKED);
				video_fullscreen_scale = LOWORD(wParam) - IDM_VID_FS_FULL;
				CheckMenuItem(hmenu, IDM_VID_FS_FULL + video_fullscreen_scale, MF_CHECKED);
				device_force_redraw();
				config_save();
				break;

			case IDM_VID_SCALE_1X:
			case IDM_VID_SCALE_2X:
			case IDM_VID_SCALE_3X:
			case IDM_VID_SCALE_4X:
				CheckMenuItem(hmenu, IDM_VID_SCALE_1X + scale, MF_UNCHECKED);
				scale = LOWORD(wParam) - IDM_VID_SCALE_1X;
				CheckMenuItem(hmenu, IDM_VID_SCALE_1X + scale, MF_CHECKED);
				device_force_redraw();
				config_save();
				break;

			case IDM_VID_FORCE43:
				video_toggle_option(hmenu, &force_43, IDM_VID_FORCE43);
				break;

			case IDM_VID_INVERT:
				video_toggle_option(hmenu, &invert_display, IDM_VID_INVERT);
				break;

			case IDM_VID_OVERSCAN:
				update_overscan = 1;
				video_toggle_option(hmenu, &enable_overscan, IDM_VID_OVERSCAN);
				break;

			case IDM_VID_CGACON:
				vid_cga_contrast ^= 1;
				CheckMenuItem(hmenu, IDM_VID_CGACON, vid_cga_contrast ? MF_CHECKED : MF_UNCHECKED);
				cgapal_rebuild();
				config_save();
				break;

			case IDM_VID_GRAYCT_601:
			case IDM_VID_GRAYCT_709:
			case IDM_VID_GRAYCT_AVE:
				CheckMenuItem(hmenu, IDM_VID_GRAYCT_601 + video_graytype, MF_UNCHECKED);
				video_graytype = LOWORD(wParam) - IDM_VID_GRAYCT_601;
				CheckMenuItem(hmenu, IDM_VID_GRAYCT_601 + video_graytype, MF_CHECKED);
				device_force_redraw();
				config_save();
				break;

			case IDM_VID_GRAY_RGB:
			case IDM_VID_GRAY_MONO:
			case IDM_VID_GRAY_AMBER:
			case IDM_VID_GRAY_GREEN:
			case IDM_VID_GRAY_WHITE:
				CheckMenuItem(hmenu, IDM_VID_GRAY_RGB + video_grayscale, MF_UNCHECKED);
				video_grayscale = LOWORD(wParam) - IDM_VID_GRAY_RGB;
				CheckMenuItem(hmenu, IDM_VID_GRAY_RGB + video_grayscale, MF_CHECKED);
				device_force_redraw();
				config_save();
				break;

#ifdef ENABLE_LOG_TOGGLES
# ifdef ENABLE_BUSLOGIC_LOG
			case IDM_LOG_BUSLOGIC:
				buslogic_do_log ^= 1;
				CheckMenuItem(hmenu, IDM_LOG_BUSLOGIC, buslogic_do_log ? MF_CHECKED : MF_UNCHECKED);
				break;
# endif

# ifdef ENABLE_CDROM_LOG
			case IDM_LOG_CDROM:
				cdrom_do_log ^= 1;
				CheckMenuItem(hmenu, IDM_LOG_CDROM, cdrom_do_log ? MF_CHECKED : MF_UNCHECKED);
				break;
# endif

# ifdef ENABLE_D86F_LOG
			case IDM_LOG_D86F:
				d86f_do_log ^= 1;
				CheckMenuItem(hmenu, IDM_LOG_D86F, d86f_do_log ? MF_CHECKED : MF_UNCHECKED);
				break;
# endif

# ifdef ENABLE_FDC_LOG
			case IDM_LOG_FDC:
				fdc_do_log ^= 1;
				CheckMenuItem(hmenu, IDM_LOG_FDC, fdc_do_log ? MF_CHECKED : MF_UNCHECKED);
				break;
# endif

# ifdef ENABLE_IDE_LOG
			case IDM_LOG_IDE:
				ide_do_log ^= 1;
				CheckMenuItem(hmenu, IDM_LOG_IDE, ide_do_log ? MF_CHECKED : MF_UNCHECKED);
				break;
# endif

# ifdef ENABLE_SERIAL_LOG
			case IDM_LOG_SERIAL:
				serial_do_log ^= 1;
				CheckMenuItem(hmenu, IDM_LOG_SERIAL, serial_do_log ? MF_CHECKED : MF_UNCHECKED);
				break;
# endif

# ifdef ENABLE_NIC_LOG
			case IDM_LOG_NIC:
				nic_do_log ^= 1;
				CheckMenuItem(hmenu, IDM_LOG_NIC, nic_do_log ? MF_CHECKED : MF_UNCHECKED);
				break;
# endif
#endif

#ifdef ENABLE_LOG_BREAKPOINT
			case IDM_LOG_BREAKPOINT:
				pclog("---- LOG BREAKPOINT ----\n");
				break;
#endif

#ifdef ENABLE_VRAM_DUMP
			case IDM_DUMP_VRAM:
				svga_dump_vram();
				break;
#endif

			case IDM_CONFIG_LOAD:
				plat_pause(1);
				if (! file_dlg_st(hwnd, IDS_2160, "", 0)) {
					if (ui_msgbox(MBX_QUESTION, (wchar_t *)IDS_2051) == IDYES) {
						config_write(config_file_default);
						for (i = 0; i < FDD_NUM; i++)
							floppy_close(i);
						for (i = 0; i < CDROM_NUM; i++)
						{
							cdrom_drives[i].handler->exit(i);
							if (cdrom_drives[i].host_drive == 200)
								image_close(i);
							else if ((cdrom_drives[i].host_drive >= 'A') && (cdrom_drives[i].host_drive <= 'Z'))
								ioctl_close(i);
							else
								null_close(i);
						}
						pc_reset_hard_close();
						config_load(wopenfilestring);
						for (i = 0; i < CDROM_NUM; i++)
						{
							if (cdrom_drives[i].bus_type)
								SCSIReset(cdrom_drives[i].scsi_device_id, cdrom_drives[i].scsi_device_lun);

							if (cdrom_drives[i].host_drive == 200)
								image_open(i, cdrom_image[i].image_path);
							else if ((cdrom_drives[i].host_drive >= 'A') && (cdrom_drives[i].host_drive <= 'Z'))
								ioctl_open(i, cdrom_drives[i].host_drive);
							else	
							        cdrom_null_open(i, cdrom_drives[i].host_drive);
						}

						floppy_load(0, floppyfns[0]);
						floppy_load(1, floppyfns[1]);
						floppy_load(2, floppyfns[2]);
						floppy_load(3, floppyfns[3]);

						mem_resize();
						rom_load_bios(romset);
						network_init();
						ResetAllMenus();
						ui_sb_update_panes();
						pc_reset_hard_init();
					}
				}
				plat_pause(0);
				break;                        

			case IDM_CONFIG_SAVE:
				plat_pause(1);
				if (! file_dlg_st(hwnd, IDS_2160, "", 1)) {
					config_write(wopenfilestring);
				}
				plat_pause(0);
				break;                                                
		}
		return(0);

	case WM_INPUT:
		process_raw_input(lParam, infocus);
		break;

	case WM_SETFOCUS:
		infocus = 1;
		if (! hook_enabled) {
			hKeyboardHook = SetWindowsHookEx(WH_KEYBOARD_LL,
							 LowLevelKeyboardProc,
							 GetModuleHandle(NULL),
							 0);
			hook_enabled = 1;
		}
		break;

	case WM_KILLFOCUS:
		infocus = 0;
		releasemouse();
		memset(recv_key, 0, sizeof(recv_key));
		if (video_fullscreen)
			leave_fullscreen_flag = 1;
		if (hook_enabled) {
			UnhookWindowsHookEx(hKeyboardHook);
			hook_enabled = 0;
		}
		break;

	case WM_LBUTTONUP:
		if (!mousecapture && !video_fullscreen) {
			GetClipCursor(&oldclip);
			GetWindowRect(hwndRender, &rect);

			ClipCursor(&rect);
			mousecapture = 1;
			while (1) {
				if (ShowCursor(FALSE) < 0) break;
			}
		}
		break;

	case WM_MBUTTONUP:
		if (!(mouse_get_type(mouse_type) & MOUSE_TYPE_3BUTTON)) {
			releasemouse();
		}
		break;

	case WM_ENTERMENULOOP:
		break;

	case WM_SIZE:
		winsizex = (lParam & 0xFFFF);
		winsizey = (lParam >> 16) - (17 + 6);
		if (winsizey < 0)
			winsizey = 0;

		MoveWindow(hwndSBAR, 0, winsizey + 6, winsizex, 17, TRUE);

		if (vid_apis[0][vid_api].local && (hwndRender != NULL)) {
			MoveWindow(hwndRender, 0, 0, winsizex, winsizey, TRUE);

			if (vid_apis[video_fullscreen][vid_api].resize) {
				startblit();
				video_wait_for_blit();
				vid_apis[video_fullscreen][vid_api].resize(winsizex, winsizey);
				endblit();
			}

			if (mousecapture) {
				GetWindowRect(hwndRender, &rect);

				ClipCursor(&rect);
			}
		}

		if (window_remember) {
			GetWindowRect(hwnd, &rect);
			window_x = rect.left;
			window_y = rect.top;
			window_w = rect.right - rect.left;
			window_h = rect.bottom - rect.top;
			save_window_pos = 1;
		}

		config_save();
		break;

	case WM_MOVE:
		if (window_remember) {
			GetWindowRect(hwnd, &rect);
			window_x = rect.left;
			window_y = rect.top;
			window_w = rect.right - rect.left;
			window_h = rect.bottom - rect.top;
			save_window_pos = 1;
		}
		break;
                
	case WM_TIMER:
		if (wParam == TIMER_1SEC) {
			onesec();
		}
		break;

	case WM_RESETD3D:
		startblit();
		if (video_fullscreen)
			d3d_fs_reset();
		  else
			d3d_reset();
		endblit();
		break;

	case WM_LEAVEFULLSCREEN:
		startblit();
		mouse_close();
		vid_apis[1][vid_api].close();
		video_fullscreen = 0;
		config_save();
		vid_apis[0][vid_api].init(hwndRender);
		mouse_init();
		endblit();
		device_force_redraw();
		cgapal_rebuild();
		break;

	case WM_KEYDOWN:
	case WM_KEYUP:
	case WM_SYSKEYDOWN:
	case WM_SYSKEYUP:
		return(0);

	case WM_DESTROY:
		UnhookWindowsHookEx(hKeyboardHook);
		KillTimer(hwnd, TIMER_1SEC);
		PostQuitMessage(0);
		break;

	case WM_SYSCOMMAND:
		/*
		 * Disable ALT key *ALWAYS*,
		 * I don't think there's any use for
		 * reaching the menu that way.
		 */
		if (wParam == SC_KEYMENU && HIWORD(lParam) <= 0) {
			return 0; /*disable ALT key for menu*/
		}

	default:
		return(DefWindowProc(hwnd, message, wParam, lParam));
    }

    return(0);
}


static LRESULT CALLBACK
SubWindowProcedure(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam)
{
    return(DefWindowProc(hwnd, message, wParam, lParam));
}


static void
LoadCommonStrings(void)
{
    int i;

    lpRCstr2048 = (rc_str_t *)malloc(STRINGS_NUM_2048*sizeof(rc_str_t));
    lpRCstr3072 = (rc_str_t *)malloc(STRINGS_NUM_3072*sizeof(rc_str_t));
    lpRCstr4096 = (rc_str_t *)malloc(STRINGS_NUM_4096*sizeof(rc_str_t));
    lpRCstr4352 = (rc_str_t *)malloc(STRINGS_NUM_4352*sizeof(rc_str_t));
    lpRCstr4608 = (rc_str_t *)malloc(STRINGS_NUM_4608*sizeof(rc_str_t));
    lpRCstr5120 = (rc_str_t *)malloc(STRINGS_NUM_5120*sizeof(rc_str_t));
    lpRCstr5376 = (rc_str_t *)malloc(STRINGS_NUM_5376*sizeof(rc_str_t));
    lpRCstr5632 = (rc_str_t *)malloc(STRINGS_NUM_5632*sizeof(rc_str_t));
    lpRCstr6144 = (rc_str_t *)malloc(STRINGS_NUM_6144*sizeof(rc_str_t));

    for (i=0; i<STRINGS_NUM_2048; i++)
	LoadString(hinstance, 2048+i, lpRCstr2048[i].str, 512);

    for (i=0; i<STRINGS_NUM_3072; i++)
	LoadString(hinstance, 3072+i, lpRCstr3072[i].str, 512);

    for (i=0; i<STRINGS_NUM_4096; i++)
	LoadString(hinstance, 4096+i, lpRCstr4096[i].str, 512);

    for (i=0; i<STRINGS_NUM_4352; i++)
	LoadString(hinstance, 4352+i, lpRCstr4352[i].str, 512);

    for (i=0; i<STRINGS_NUM_4608; i++)
	LoadString(hinstance, 4608+i, lpRCstr4608[i].str, 512);

    for (i=0; i<STRINGS_NUM_5120; i++)
	LoadString(hinstance, 5120+i, lpRCstr5120[i].str, 512);

    for (i=0; i<STRINGS_NUM_5376; i++)
	LoadString(hinstance, 5376+i, lpRCstr5376[i].str, 512);

    for (i=0; i<STRINGS_NUM_5632; i++)
	LoadString(hinstance, 5632+i, lpRCstr5632[i].str, 512);

    for (i=0; i<STRINGS_NUM_6144; i++)
	LoadString(hinstance, 6144+i, lpRCstr6144[i].str, 512);
}


#ifdef USE_CONSOLE
/* Create a console if we don't already have one. */
static void
CreateConsole(int init)
{
    HANDLE h;
    FILE *fp;
    fpos_t p;
    int i;

    if (! init) {
	FreeConsole();
	return;
    }

    /* Are we logging to a file? */
    p = 0;
    (void)fgetpos(stdout, &p);
    if (p != -1) return;

    /* Not logging to file, attach to console. */
    if (! AttachConsole(ATTACH_PARENT_PROCESS)) {
	/* Parent has no console, create one. */
	AllocConsole();
    }

    h = GetStdHandle(STD_OUTPUT_HANDLE);
    i = _open_osfhandle((long)h, _O_TEXT);
    fp = _fdopen(i, "w");
    setvbuf(fp, NULL, _IONBF, 1);
    *stdout = *fp;

    h = GetStdHandle(STD_ERROR_HANDLE);
    i = _open_osfhandle((long)h, _O_TEXT);
    fp = _fdopen(i, "w");
    setvbuf(fp, NULL, _IONBF, 1);
    *stderr = *fp;

#if 0
    /* Set up stdin as well. */
    h = GetStdHandle(STD_INPUT_HANDLE);
    i = _open_osfhandle((long)h, _O_TEXT);
    fp = _fdopen(i, "r");
    setvbuf(fp, NULL, _IONBF, 128);
    *stdin = *fp;
#endif
}
#endif


/* Process the commandline, and create standard argc/argv array. */
static void
ProcessCommandLine(void)
{
    WCHAR *cmdline;
    int argc_max;
    int i, q;

    cmdline = GetCommandLine();
    i = wcslen(cmdline) + 1;
    argbuf = malloc(i * 2);
    memcpy(argbuf, cmdline, i * 2);

    argc = 0;
    argc_max = 64;
    argv = malloc(sizeof(wchar_t *) * argc_max);
    if (argv == NULL) {
	free(argbuf);
	return;
    }

    /* parse commandline into argc/argv format */
    i = 0;
    while (argbuf[i]) {
	while (argbuf[i] == L' ')
		  i++;

	if (argbuf[i]) {
		if ((argbuf[i] == L'\'') || (argbuf[i] == L'"')) {
			q = argbuf[i++];
			if (!argbuf[i])
				break;
		} else
			q = 0;

		argv[argc++] = &argbuf[i];

		if (argc >= argc_max) {
			argc_max += 64;
			argv = realloc(argv, sizeof(wchar_t *) * argc_max);
			if (argv == NULL) {
				free(argbuf);
				return;
			}
		}

		while ((argbuf[i]) && ((q)
			? (argbuf[i]!=q) : (argbuf[i]!=L' '))) i++;

		if (argbuf[i]) {
			argbuf[i] = 0;
			i++;
		}
	}
    }

    argv[argc] = NULL;
}


/* @@@
 * For the Windows platform, this is the start of the application.
 */
int WINAPI
WinMain(HINSTANCE hInst, HINSTANCE hPrev, LPSTR lpszArg, int nFunsterStil)
{
    WCHAR title[200];
    WNDCLASSEX wincl;			/* buffer for main window's class */
    MSG messages;			/* received-messages buffer */
    HWND hwnd;				/* handle for our window */
    HACCEL haccel;			/* handle to accelerator table */
    int bRet;
    LARGE_INTEGER qpc_freq;

#ifdef USE_CONSOLE
    /* Create console window. */
    CreateConsole(1);
#endif

    /* Process the command line for options. */
    ProcessCommandLine();

    /* Pre-initialize the system, this loads the config file. */
    if (! pc_init(argc, argv)) {
#ifdef USE_CONSOLE
	/* Detach from console. */
	CreateConsole(0);
#endif
	return(1);
    }

    /* We need this later. */
    hinstance = hInst;

    /* Load common strings from the resource file. */
    LoadCommonStrings();

    /* Create our main window's class and register it. */
    wincl.hInstance = hInst;
    wincl.lpszClassName = CLASS_NAME;
    wincl.lpfnWndProc = MainWindowProcedure;
    wincl.style = CS_DBLCLKS;		/* Catch double-clicks */
    wincl.cbSize = sizeof(WNDCLASSEX);
    wincl.hIcon = LoadIcon(hInst, (LPCTSTR)100);
    wincl.hIconSm = LoadIcon(hInst, (LPCTSTR)100);
    wincl.hCursor = NULL;
    wincl.lpszMenuName = NULL;
    wincl.cbClsExtra = 0;
    wincl.cbWndExtra = 0;
    wincl.hbrBackground = CreateSolidBrush(RGB(0,0,0));
    if (! RegisterClassEx(&wincl))
			return(2);
    wincl.lpszClassName = SUB_CLASS_NAME;
    wincl.lpfnWndProc = SubWindowProcedure;
    if (! RegisterClassEx(&wincl))
			return(2);

    /* Load the Window Menu(s) from the resources. */
    menuMain = LoadMenu(hInst, MENU_NAME);

    /* Set the initial title for the program's main window. */
    _swprintf(title, L"%s v%s", EMU_NAME_W, EMU_VERSION_W);

    /* Now create our main window. */
    hwnd = CreateWindowEx (
		0,			/* no extended possibilites */
		CLASS_NAME,		/* class name */
		title,			/* Title Text */
		(WS_OVERLAPPEDWINDOW & ~WS_SIZEBOX) | DS_3DLOOK,
		CW_USEDEFAULT,		/* Windows decides the position */
		CW_USEDEFAULT,		/* where window ends up on the screen */
		640+(GetSystemMetrics(SM_CXFIXEDFRAME)*2),	/* width */
		480+(GetSystemMetrics(SM_CYFIXEDFRAME)*2)+GetSystemMetrics(SM_CYMENUSIZE)+GetSystemMetrics(SM_CYCAPTION)+1,	/* and height in pixels */
		HWND_DESKTOP,		/* window is a child to desktop */
		menuMain,		/* menu */
		hInst,			/* Program Instance handler */
		NULL);			/* no Window Creation data */
    hwndMain = hwnd;

    ui_window_title(title);

    /* Resize the window if needed. */
    if (vid_resize)
	SetWindowLongPtr(hwnd, GWL_STYLE,
			(WS_OVERLAPPEDWINDOW&~WS_MINIMIZEBOX));
      else
	SetWindowLongPtr(hwnd, GWL_STYLE,
			(WS_OVERLAPPEDWINDOW&~WS_SIZEBOX&~WS_THICKFRAME&~WS_MAXIMIZEBOX&~WS_MINIMIZEBOX));

    if (window_remember)
	MoveWindow(hwnd, window_x, window_y, window_w, window_h, TRUE);

    /* Reset all menus to their defaults. */
    ResetAllMenus();

    /* Make the window visible on the screen. */
    ShowWindow(hwnd, nFunsterStil);

    /* Load the accelerator table */
    haccel = LoadAccelerators(hInst, ACCEL_NAME);
    if (haccel == NULL) {
	MessageBox(hwndMain,
		   plat_get_string(IDS_2053),
		   plat_get_string(IDS_2050),
		   MB_OK | MB_ICONERROR);
	return(3);
    }

    /* Initialize the input (keyboard, mouse, game) module. */
    memset(recv_key, 0x00, sizeof(recv_key));
    device.usUsagePage = 0x01;
    device.usUsage = 0x06;
    device.dwFlags = RIDEV_NOHOTKEYS;
    device.hwndTarget = hwnd;
    if (! RegisterRawInputDevices(&device, 1, sizeof(device))) {
	MessageBox(hwndMain,
		   plat_get_string(IDS_2054),
		   plat_get_string(IDS_2050),
		   MB_OK | MB_ICONERROR);
	return(4);
    }
    get_registry_key_map();

    /* Create the status bar window. */
    StatusBarCreate(hwndMain, IDC_STATUS, hInst);

    /*
     * Before we can create the Render window, we first have
     * to prepare some other things that it depends on.
     */
    ghMutex = CreateMutex(NULL, FALSE, L"86Box.BlitMutex");

    /* Create the Machine Rendering window. */
    hwndRender = CreateWindow(L"STATIC", NULL, WS_CHILD|SS_BITMAP,
			      0, 0, 1, 1, hwnd, NULL, hInst, NULL);
    MoveWindow(hwndRender, 0, 0, winsizex, winsizey, TRUE);

    /* If this is a local renderer, enable it. */
    if (vid_apis[0][vid_api].local)
	ShowWindow(hwndRender, SW_SHOW);

    /* Select the best system renderer available. */
    if (! vid_apis[0][vid_api].init(hwndRender)) {
	vid_api ^= 1;
	if (! vid_apis[0][vid_api].init(hwndRender)) {
		MessageBox(hwnd,
			   plat_get_string(IDS_2095),
			   plat_get_string(IDS_2050),
			   MB_OK | MB_ICONERROR);
		return(5);
	}
    }

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
    if (vid_apis[video_fullscreen][vid_api].resize) {
	startblit();
	vid_apis[video_fullscreen][vid_api].resize(winsizex, winsizey);
	endblit();
    }

    /* All done, fire up the actual emulated machine. */
    if (! pc_init_modules()) {
	/* Dang, no ROMs found at all! */
	MessageBox(hwnd,
		   plat_get_string(IDS_2056),
		   plat_get_string(IDS_2050),
		   MB_OK | MB_ICONERROR);
	return(6);
    }

    /* Fire up the machine. */
    pc_reset_hard();

    /* Set the PAUSE mode depending on the renderer. */
    plat_pause(0);

    /*
     * Everything has been configured, and all seems to work,
     * so now it is time to start the main thread to do some
     * real work, and we will hang in here, dealing with the
     * UI until we're done.
     */
    timeBeginPeriod(1);
    QueryPerformanceFrequency(&qpc_freq);
    timer_freq = qpc_freq.QuadPart;

    atexit(releasemouse);

    thMain = (HANDLE)_beginthread(MainThread, 0, NULL);
    SetThreadPriority(thMain, THREAD_PRIORITY_HIGHEST);

    /* Run the message loop. It will run until GetMessage() returns 0 */
    quited = 0;
    while (! quited) {
	bRet = GetMessage(&messages, NULL, 0, 0);
	if ((bRet == 0) || quited) break;

	if (bRet == -1) {
		fatal("bRet is -1\n");
	}

	if (messages.message == WM_QUIT) {
		quited = 1;
		break;
	}

	if (! TranslateAccelerator(hwnd, haccel, &messages)) {
                TranslateMessage(&messages);
                DispatchMessage(&messages);
	}

	if (recv_key[0x58] && recv_key[0x42] && mousecapture) {
		ClipCursor(&oldclip);
		ShowCursor(TRUE);
		mousecapture = 0;
        }

         if ((recv_key[0x1D] || recv_key[0x9D]) &&
	     (recv_key[0x38] || recv_key[0xB8]) &&
	     (recv_key[0x51] || recv_key[0xD1]) && video_fullscreen) {
		leave_fullscreen();
	}
    }

    /* Why start??? --FvK */
    startblit();

    Sleep(200);
    TerminateThread(thMain, 0);

    nvr_save();

    config_save();

    pc_close();

    vid_apis[video_fullscreen][vid_api].close();
        
    timeEndPeriod(1);

    if (mousecapture) {
	ClipCursor(&oldclip);
	ShowCursor(TRUE);
    }
        
    UnregisterClass(SUB_CLASS_NAME, hInst);
    UnregisterClass(CLASS_NAME, hInst);

    return(messages.wParam);
}


FILE *
plat_fopen(wchar_t *path, wchar_t *mode)
{
    return(_wfopen(path, mode));
}


void
plat_remove(wchar_t *path)
{
    _wremove(path);
}


int
plat_getcwd(wchar_t *bufp, int max)
{
    (void)_wgetcwd(bufp, max);

    return(0);
}


int
plat_chdir(wchar_t *path)
{
    return(_wchdir(path));
}


void
plat_get_exe_name(wchar_t *s, int size)
{
    GetModuleFileName(hinstance, s, size);
}


wchar_t *
plat_get_filename(wchar_t *s)
{
    int c = wcslen(s) - 1;

    while (c > 0) {
	if (s[c] == L'/' || s[c] == L'\\')
	   return(&s[c+1]);
       c--;
    }

    return(s);
}


wchar_t *
plat_get_extension(wchar_t *s)
{
    int c = wcslen(s) - 1;

    if (c <= 0)
	return(s);

    while (c && s[c] != L'.')
		c--;

    if (!c)
	return(&s[wcslen(s)]);

    return(&s[c+1]);
}


void
plat_append_filename(wchar_t *dest, wchar_t *s1, wchar_t *s2, int size)
{
    wcscat(dest, s1);
    wcscat(dest, s2);
}


void
plat_put_backslash(wchar_t *s)
{
    int c = wcslen(s) - 1;

    if (s[c] != L'/' && s[c] != L'\\')
	   s[c] = L'/';
}


wchar_t *
ui_window_title(wchar_t *s)
{
    if (! video_fullscreen) {
	if (s != NULL)
		wcscpy(wTitle, s);
	  else
		s = wTitle;

       	SetWindowText(hwndMain, s);
    }

    return(s);
}


int
plat_dir_check(wchar_t *path)
{
    DWORD dwAttrib = GetFileAttributes(path);

    return(((dwAttrib != INVALID_FILE_ATTRIBUTES &&
	   (dwAttrib & FILE_ATTRIBUTE_DIRECTORY))) ? 1 : 0);
}


int
plat_dir_create(wchar_t *path)
{
    return((int)CreateDirectory(path, NULL));
}


uint64_t
plat_timer_read(void)
{
    LARGE_INTEGER li;

    QueryPerformanceCounter(&li);

    return(li.QuadPart);
}


uint32_t
plat_get_ticks(void)
{
    return(GetTickCount());
}


void
plat_delay_ms(uint32_t count)
{
    Sleep(count);
}


/* We should have the language ID as a parameter. */
void
win_set_language(void)
{
    SetThreadLocale(dwLanguage);
}


/* Update the global language used. This needs a parameter.. */
void
win_language_update(void)
{
    win_set_language();
#if 0
    MenuUpdate();
#endif
    LoadCommonStrings();
}


void
win_language_check(void)
{
    LCID dwLanguageNew = MAKELCID(dwLangID, dwSubLangID);

    if (dwLanguageNew != dwLanguage) {
	dwLanguage = dwLanguageNew;

	win_language_update();
    }
}


void
plat_pause(int p)
{
    static wchar_t oldtitle[512];

    /* If un-pausing, as the renderer if that's OK. */
    if (p == 0)
	p = vid_apis[video_fullscreen][vid_api].pause();

    /* If already so, done. */
    if (dopause == p) return;

    if (p) {
	wcscpy(oldtitle, wTitle);
	wcscat(wTitle, L" - PAUSED -");
	ui_window_title(NULL);
    } else {
	ui_window_title(oldtitle);
    }

    dopause = p;

    /* Update the actual menu. */
    CheckMenuItem(menuMain, IDM_ACTION_PAUSE, (dopause)?MF_CHECKED:MF_UNCHECKED);
}


wchar_t *
plat_get_string(int i)
{
    LPTSTR str;

    if ((i >= 2048) && (i <= 3071)) {
	str = lpRCstr2048[i-2048].str;
    } else if ((i >= 3072) && (i <= 4095)) {
	str = lpRCstr3072[i-3072].str;
    } else if ((i >= 4096) && (i <= 4351)) {
	str = lpRCstr4096[i-4096].str;
    } else if ((i >= 4352) && (i <= 4607)) {
	str = lpRCstr4352[i-4352].str;
    } else if ((i >= 4608) && (i <= 5119)) {
	str = lpRCstr4608[i-4608].str;
    } else if ((i >= 5120) && (i <= 5375)) {
	str = lpRCstr5120[i-5120].str;
    } else if ((i >= 5376) && (i <= 5631)) {
	str = lpRCstr5376[i-5376].str;
    } else if ((i >= 5632) && (i <= 6143)) {
	str = lpRCstr5632[i-5632].str;
    } else {
	str = lpRCstr6144[i-6144].str;
    }

    return((wchar_t *)str);
}


wchar_t *
plat_get_string_from_string(char *str)
{
    return(plat_get_string(atoi(str)));
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


void
startblit(void)
{
    WaitForSingleObject(ghMutex, INFINITE);
}


void
endblit(void)
{
    ReleaseMutex(ghMutex);
}


void
updatewindowsize(int x, int y)
{
    int owsx = winsizex;
    int owsy = winsizey;
    int temp_overscan_x = overscan_x;
    int temp_overscan_y = overscan_y;
    double dx, dy, dtx, dty;

    if (vid_resize) return;

    if (x < 160)  x = 160;
    if (y < 100)  y = 100;
    if (x > 2048) x = 2048;
    if (y > 2048) y = 2048;

    if (suppress_overscan)
	temp_overscan_x = temp_overscan_y = 0;

    unscaled_size_x=x; efwinsizey=y;

    if (force_43) {
	dx = (double) x;
	dtx = (double) temp_overscan_x;

	dy = (double) y;
	dty = (double) temp_overscan_y;

	/* Account for possible overscan. */
	if (temp_overscan_y == 16) {
		/* CGA */
		dy = (((dx - dtx) / 4.0) * 3.0) + dty;
	} else if (temp_overscan_y < 16) {
		/* MDA/Hercules */
		dy = (x / 4.0) * 3.0;
	} else {
		if (enable_overscan) {
			/* EGA/(S)VGA with overscan */
			dy = (((dx - dtx) / 4.0) * 3.0) + dty;
		} else {
			/* EGA/(S)VGA without overscan */
			dy = (x / 4.0) * 3.0;
		}
	}
	unscaled_size_y = (int) dy;
    } else {
	unscaled_size_y = efwinsizey;
    }

    switch(scale) {
	case 0:
		winsizex = unscaled_size_x >> 1;
		winsizey = unscaled_size_y >> 1;
		break;

	case 1:
		winsizex = unscaled_size_x;
		winsizey = unscaled_size_y;
		break;

	case 2:
		winsizex = (unscaled_size_x * 3) >> 1;
		winsizey = (unscaled_size_y * 3) >> 1;
		break;

	case 3:
		winsizex = unscaled_size_x << 1;
		winsizey = unscaled_size_y << 1;
		break;
    }

    if ((owsx != winsizex) || (owsy != winsizey))
	doresize = 1;
      else
	doresize = 0;
}


void
uws_natural(void)
{
    updatewindowsize(unscaled_size_x, efwinsizey);
}


void
leave_fullscreen(void)
{
    leave_fullscreen_flag = 1;
}
