/*
 * 86Box	A hypervisor and IBM PC system emulator that specializes in
 *		running old operating systems and software designed for IBM
 *		PC systems and compatibles from 1981 through fairly recent
 *		system designs based on the PCI bus.
 *
 *		This file is part of the 86Box distribution.
 *
 *		user Interface module for WinAPI on Windows.
 *
 * Version:	@(#)win_ui.c	1.0.45	2019/12/05
 *
 * Authors:	Sarah Walker, <http://pcem-emulator.co.uk/>
 *		Miran Grca, <mgrca8@gmail.com>
 *		Fred N. van Kempen, <decwiz@yahoo.com>
 *
 *		Copyright 2008-2019 Sarah Walker.
 *		Copyright 2016-2019 Miran Grca.
 *		Copyright 2017-2019 Fred N. van Kempen.
 *		Copyright 2019 GH Cao.
 */
#define UNICODE
#include <windows.h>
#include <commctrl.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <time.h>
#include <wchar.h>
#include "../86box.h"
#include "../config.h"
#include "../device.h"
#include "../keyboard.h"
#include "../mouse.h"
#include "../video/video.h"
#include "../video/vid_ega.h"		// for update_overscan
#include "../plat.h"
#include "../plat_midi.h"
#include "../ui.h"
#include "win.h"
#ifdef USE_DISCORD
# include "win_discord.h"
#endif


#define TIMER_1SEC	1		/* ID of the one-second timer */


/* Platform Public data, specific. */
HWND		hwndMain,		/* application main window */
		hwndRender;		/* machine render window */
HMENU		menuMain;		/* application main menu */
HICON		hIcon[256];		/* icon data loaded from resources */
RECT		oldclip;		/* mouse rect */
int		sbar_height = 23;     /* statusbar height */
int		minimized = 0;
int		infocus = 1;
int		rctrl_is_lalt = 0;
int		user_resize = 0;

char		openfilestring[260];
WCHAR		wopenfilestring[260];


/* Local data. */
static wchar_t	wTitle[512];
static HHOOK	hKeyboardHook;
static int	hook_enabled = 0, manager_wm = 0;
static int	save_window_pos = 0, pause_state = 0;


static int vis = -1;

/* Set host cursor visible or not. */
void
show_cursor(int val)
{
    if (val == vis)
	return;

    if (val == 0) {
    	while (1)
		if (ShowCursor(FALSE) < 0) break;
    } else
	ShowCursor(TRUE);

    vis = val;
}


HICON
LoadIconEx(PCTSTR pszIconName)
{
    return((HICON)LoadImage(hinstance, pszIconName, IMAGE_ICON,
						16, 16, LR_SHARED));
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


static void
ResetAllMenus(void)
{
#ifndef DEV_BRANCH
    /* FIXME: until we fix these.. --FvK */
    EnableMenuItem(menuMain, IDM_CONFIG_LOAD, MF_DISABLED);
    EnableMenuItem(menuMain, IDM_CONFIG_SAVE, MF_DISABLED);
#endif

#ifdef USE_D2D
    /* Disable Direct2D menu option on NT5 */
    if(LOBYTE(LOWORD(GetVersion())) < 6)
        EnableMenuItem(menuMain, IDM_VID_D2D, MF_GRAYED);
#endif

    CheckMenuItem(menuMain, IDM_ACTION_RCTRL_IS_LALT, MF_UNCHECKED);

    CheckMenuItem(menuMain, IDM_UPDATE_ICONS, MF_UNCHECKED);

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
    CheckMenuItem(menuMain, IDM_LOG_NIC, MF_UNCHECKED);
# endif
#endif

    CheckMenuItem(menuMain, IDM_VID_FORCE43, MF_UNCHECKED);
    CheckMenuItem(menuMain, IDM_VID_OVERSCAN, MF_UNCHECKED);
    CheckMenuItem(menuMain, IDM_VID_INVERT, MF_UNCHECKED);

    CheckMenuItem(menuMain, IDM_VID_RESIZE, MF_UNCHECKED);
    CheckMenuItem(menuMain, IDM_VID_SDL_SW, MF_UNCHECKED);
    CheckMenuItem(menuMain, IDM_VID_SDL_HW, MF_UNCHECKED);
#ifdef USE_D2D
    CheckMenuItem(menuMain, IDM_VID_D2D, MF_UNCHECKED);
#endif
#ifdef USE_VNC
    CheckMenuItem(menuMain, IDM_VID_VNC, MF_UNCHECKED);
#endif
    CheckMenuItem(menuMain, IDM_VID_FS_FULL+0, MF_UNCHECKED);
    CheckMenuItem(menuMain, IDM_VID_FS_FULL+1, MF_UNCHECKED);
    CheckMenuItem(menuMain, IDM_VID_FS_FULL+2, MF_UNCHECKED);
    CheckMenuItem(menuMain, IDM_VID_FS_FULL+3, MF_UNCHECKED);
    CheckMenuItem(menuMain, IDM_VID_FS_FULL+4, MF_UNCHECKED);
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

    CheckMenuItem(menuMain, IDM_ACTION_RCTRL_IS_LALT, rctrl_is_lalt ? MF_CHECKED : MF_UNCHECKED);

    CheckMenuItem(menuMain, IDM_UPDATE_ICONS, update_icons ? MF_CHECKED : MF_UNCHECKED);

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
    CheckMenuItem(menuMain, IDM_VID_SDL_SW+vid_api, MF_CHECKED);
    CheckMenuItem(menuMain, IDM_VID_FS_FULL+video_fullscreen_scale, MF_CHECKED);
    CheckMenuItem(menuMain, IDM_VID_REMEMBER, window_remember?MF_CHECKED:MF_UNCHECKED);
    CheckMenuItem(menuMain, IDM_VID_SCALE_1X+scale, MF_CHECKED);

    CheckMenuItem(menuMain, IDM_VID_CGACON, vid_cga_contrast?MF_CHECKED:MF_UNCHECKED);
    CheckMenuItem(menuMain, IDM_VID_GRAYCT_601+video_graytype, MF_CHECKED);
    CheckMenuItem(menuMain, IDM_VID_GRAY_RGB+video_grayscale, MF_CHECKED);

#ifdef USE_DISCORD
    if (discord_loaded)
	CheckMenuItem(menuMain, IDM_DISCORD, enable_discord ? MF_CHECKED : MF_UNCHECKED);
    else
	EnableMenuItem(menuMain, IDM_DISCORD, MF_DISABLED);
#endif
}


static LRESULT CALLBACK
LowLevelKeyboardProc(int nCode, WPARAM wParam, LPARAM lParam)
{
    BOOL bControlKeyDown;
    KBDLLHOOKSTRUCT *p;

    if (nCode < 0 || nCode != HC_ACTION || (!mouse_capture && !video_fullscreen))
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


void
win_notify_dlg_open(void)
{
    manager_wm = 1;
    pause_state = dopause;
    plat_pause(1);
    if (source_hwnd)
	PostMessage((HWND) (uintptr_t) source_hwnd, WM_SENDDLGSTATUS, (WPARAM) 1, (LPARAM) hwndMain);
}


void
win_notify_dlg_closed(void)
{
    if (source_hwnd)
	PostMessage((HWND) (uintptr_t) source_hwnd, WM_SENDDLGSTATUS, (WPARAM) 0, (LPARAM) hwndMain);
    plat_pause(pause_state);
    manager_wm = 0;
}


static LRESULT CALLBACK
MainWindowProcedure(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam)
{
    HMENU hmenu;

    int i, sb_borders[3];
    RECT rect;

    int temp_x, temp_y;

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
				win_notify_dlg_open();
				i = ui_msgbox(MBX_QUESTION_YN, (wchar_t *)IDS_2121);
				if (i == 0)
					pc_reset(1);
				win_notify_dlg_closed();
				break;

			case IDM_ACTION_RESET_CAD:
				pc_reset(0);
				break;

			case IDM_ACTION_EXIT:
				win_notify_dlg_open();
				i = ui_msgbox(MBX_QUESTION_YN, (wchar_t *)IDS_2122);
				if (i == 0) {
					UnhookWindowsHookEx(hKeyboardHook);
					KillTimer(hwnd, TIMER_1SEC);
					PostQuitMessage(0);
				}
				win_notify_dlg_closed();
				break;

			case IDM_ACTION_CTRL_ALT_ESC:
				pc_send_cae();
				break;

			case IDM_ACTION_RCTRL_IS_LALT:
				rctrl_is_lalt ^= 1;
				CheckMenuItem(hmenu, IDM_ACTION_RCTRL_IS_LALT, rctrl_is_lalt ? MF_CHECKED : MF_UNCHECKED);
				config_save();
				break;

			case IDM_ACTION_PAUSE:
				plat_pause(dopause ^ 1);
				CheckMenuItem(menuMain, IDM_ACTION_PAUSE, dopause ? MF_CHECKED : MF_UNCHECKED);
				break;

			case IDM_CONFIG:
				win_settings_open(hwnd);
				break;

			case IDM_ABOUT:
				AboutDialogCreate(hwnd);
				break;

			case IDM_UPDATE_ICONS:
				update_icons ^= 1;
				CheckMenuItem(hmenu, IDM_UPDATE_ICONS, update_icons ? MF_CHECKED : MF_UNCHECKED);
				config_save();
				break;

			case IDM_VID_RESIZE:
				vid_resize = !vid_resize;
				CheckMenuItem(hmenu, IDM_VID_RESIZE, (vid_resize)? MF_CHECKED : MF_UNCHECKED);

				if (vid_resize)
					SetWindowLongPtr(hwnd, GWL_STYLE, (WS_OVERLAPPEDWINDOW) | WS_VISIBLE);
				  else
					SetWindowLongPtr(hwnd, GWL_STYLE, (WS_OVERLAPPEDWINDOW & ~WS_SIZEBOX & ~WS_THICKFRAME & ~WS_MAXIMIZEBOX) | WS_VISIBLE);

				SendMessage(hwndSBAR, SB_GETBORDERS, 0, (LPARAM) sb_borders);

				GetWindowRect(hwnd, &rect);

				/* Main Window. */
				if (GetSystemMetrics(SM_CXPADDEDBORDER) == 0) {
					/* For platforms that subsystem version < 6.0 (default on mingw/msys2) */
					/* In this case, border sizes are different between resizable and non-resizable window */
					MoveWindow(hwnd, rect.left, rect.top,
						unscaled_size_x + (GetSystemMetrics(vid_resize ? SM_CXSIZEFRAME : SM_CXFIXEDFRAME) * 2),
						unscaled_size_y + (GetSystemMetrics(vid_resize ? SM_CYSIZEFRAME : SM_CYFIXEDFRAME) * 2) + (GetSystemMetrics(SM_CYBORDER) + GetSystemMetrics(SM_CYMENUSIZE)) + GetSystemMetrics(SM_CYCAPTION) + sbar_height,
						TRUE);
				} else {
					/* For platforms that subsystem version >= 6.0 (default on llvm-mingw, mainly for Windows/ARM) */
					/* In this case, border sizes are the same between resizable and non-resizable window */
					MoveWindow(hwnd, rect.left, rect.top,
						unscaled_size_x + ((GetSystemMetrics(SM_CXFRAME) + GetSystemMetrics(SM_CXPADDEDBORDER)) * 2),
						unscaled_size_y + ((GetSystemMetrics(SM_CYFRAME) + GetSystemMetrics(SM_CXPADDEDBORDER)) * 2) + (GetSystemMetrics(SM_CYBORDER) + GetSystemMetrics(SM_CYMENUSIZE)) + GetSystemMetrics(SM_CYCAPTION) + sbar_height,
						TRUE);
				}

				/* Render window. */
				MoveWindow(hwndRender, 0, 0, unscaled_size_x, unscaled_size_y, TRUE);
				GetWindowRect(hwndRender, &rect);

				/* Status bar. */
				MoveWindow(hwndSBAR,
					   0, rect.bottom + GetSystemMetrics(SM_CYEDGE),
					   unscaled_size_x, 17, TRUE);

				if (mouse_capture) {
					GetWindowRect(hwndRender, &rect);

					ClipCursor(&rect);
				}

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

			case IDM_VID_SDL_SW:
			case IDM_VID_SDL_HW:
#ifdef USE_D2D
			case IDM_VID_D2D:
#endif
#ifdef USE_VNC
			case IDM_VID_VNC:
#endif
				CheckMenuItem(hmenu, IDM_VID_SDL_SW + vid_api, MF_UNCHECKED);
				plat_setvid(LOWORD(wParam) - IDM_VID_SDL_SW);
				CheckMenuItem(hmenu, IDM_VID_SDL_SW + vid_api, MF_CHECKED);
				config_save();
				break;

			case IDM_VID_FULLSCREEN:
				plat_setfullscreen(1);
				config_save();
				break;

			case IDM_VID_FS_FULL:
			case IDM_VID_FS_43:
			case IDM_VID_FS_KEEPRATIO:                              
			case IDM_VID_FS_INT:
				CheckMenuItem(hmenu, IDM_VID_FS_FULL+video_fullscreen_scale, MF_UNCHECKED);
				video_fullscreen_scale = LOWORD(wParam) - IDM_VID_FS_FULL;
				CheckMenuItem(hmenu, IDM_VID_FS_FULL+video_fullscreen_scale, MF_CHECKED);
				device_force_redraw();
				config_save();
				break;

			case IDM_VID_SCALE_1X:
			case IDM_VID_SCALE_2X:
			case IDM_VID_SCALE_3X:
			case IDM_VID_SCALE_4X:
				CheckMenuItem(hmenu, IDM_VID_SCALE_1X+scale, MF_UNCHECKED);
				scale = LOWORD(wParam) - IDM_VID_SCALE_1X;
				CheckMenuItem(hmenu, IDM_VID_SCALE_1X+scale, MF_CHECKED);
				device_force_redraw();
				video_force_resize_set(1);
				config_save();
				break;

			case IDM_VID_FORCE43:
				video_toggle_option(hmenu, &force_43, IDM_VID_FORCE43);
				video_force_resize_set(1);
				break;

			case IDM_VID_INVERT:
				video_toggle_option(hmenu, &invert_display, IDM_VID_INVERT);
				break;

			case IDM_VID_OVERSCAN:
				update_overscan = 1;
				video_toggle_option(hmenu, &enable_overscan, IDM_VID_OVERSCAN);
				video_force_resize_set(1);
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
				CheckMenuItem(hmenu, IDM_VID_GRAYCT_601+video_graytype, MF_UNCHECKED);
				video_graytype = LOWORD(wParam) - IDM_VID_GRAYCT_601;
				CheckMenuItem(hmenu, IDM_VID_GRAYCT_601+video_graytype, MF_CHECKED);
				device_force_redraw();
				config_save();
				break;

			case IDM_VID_GRAY_RGB:
			case IDM_VID_GRAY_MONO:
			case IDM_VID_GRAY_AMBER:
			case IDM_VID_GRAY_GREEN:
			case IDM_VID_GRAY_WHITE:
				CheckMenuItem(hmenu, IDM_VID_GRAY_RGB+video_grayscale, MF_UNCHECKED);
				video_grayscale = LOWORD(wParam) - IDM_VID_GRAY_RGB;
				CheckMenuItem(hmenu, IDM_VID_GRAY_RGB+video_grayscale, MF_CHECKED);
				device_force_redraw();
				config_save();
				break;

#ifdef USE_DISCORD
			case IDM_DISCORD:
				if (! discord_loaded) break;
				enable_discord ^= 1;
				CheckMenuItem(hmenu, IDM_DISCORD, enable_discord ? MF_CHECKED : MF_UNCHECKED);
				if(enable_discord) {
					discord_init();
					discord_update_activity(dopause);
				} else
					discord_close();
				break;
#endif

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
		}
		return(0);

	case WM_ENTERMENULOOP:
		break;

	case WM_SIZE:
		if (user_resize && !vid_resize)
			break;

		SendMessage(hwndSBAR, SB_GETBORDERS, 0, (LPARAM) sb_borders);

		temp_x = (lParam & 0xFFFF);
		temp_y = (lParam >> 16);

		if ((temp_x <= 0) || (temp_y <= 0)) {
			minimized = 1;
			break;
		} else if (minimized == 1) {
			minimized = 0;
			video_force_resize_set(1);
		}

		plat_vidapi_enable(0);
		temp_y -= sbar_height;
		if (temp_x < 1)
			temp_x = 1;
		if (temp_y < 1)
			temp_y = 1;

		if ((temp_x != scrnsz_x) || (temp_y != scrnsz_y))
			doresize = 1;

		scrnsz_x = temp_x;
		scrnsz_y = temp_y;

		MoveWindow(hwndRender, 0, 0, scrnsz_x, scrnsz_y, TRUE);

		GetWindowRect(hwndRender, &rect);

		/* Status bar. */
		MoveWindow(hwndSBAR,
			   0, rect.bottom + GetSystemMetrics(SM_CYEDGE),
			   scrnsz_x, 17, TRUE);

		plat_vidsize(scrnsz_x, scrnsz_y);

		if (mouse_capture) {
			GetWindowRect(hwndRender, &rect);

			ClipCursor(&rect);
		}

		if (window_remember) {
			GetWindowRect(hwnd, &rect);
			window_x = rect.left;
			window_y = rect.top;
			window_w = rect.right - rect.left;
			window_h = rect.bottom - rect.top;
			save_window_pos = 1;
		}
		plat_vidapi_enable(1);

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
		if (wParam == TIMER_1SEC)
			pc_onesec();
		else if ((wParam >= 0x8000) && (wParam <= 0x80ff))
			ui_sb_timer_callback(wParam & 0xff);
		break;
		
	case WM_LEAVEFULLSCREEN:
		plat_setfullscreen(0);
		config_save();
		break;

	case WM_KEYDOWN:
	case WM_KEYUP:
	case WM_SYSKEYDOWN:
	case WM_SYSKEYUP:
		return(0);

	case WM_CLOSE:
		win_notify_dlg_open();
		i = ui_msgbox(MBX_QUESTION_YN, (wchar_t *)IDS_2122);
		if (i == 0) {
			UnhookWindowsHookEx(hKeyboardHook);
			KillTimer(hwnd, TIMER_1SEC);
			PostQuitMessage(0);
		}
		win_notify_dlg_closed();
		break;

	case WM_DESTROY:
		UnhookWindowsHookEx(hKeyboardHook);
		KillTimer(hwnd, TIMER_1SEC);
		PostQuitMessage(0);
		break;

	case WM_SHOWSETTINGS:
		if (manager_wm)
			break;
		manager_wm = 1;
		win_settings_open(hwnd);
		manager_wm = 0;
		break;

	case WM_PAUSE:
		if (manager_wm)
			break;
		manager_wm = 1;
		plat_pause(dopause ^ 1);
		CheckMenuItem(menuMain, IDM_ACTION_PAUSE, dopause ? MF_CHECKED : MF_UNCHECKED);
		manager_wm = 0;
		break;

	case WM_HARDRESET:
		if (manager_wm)
			break;
		win_notify_dlg_open();
		i = ui_msgbox(MBX_QUESTION_YN, (wchar_t *)IDS_2121);
		if (i == 0)
			pc_reset(1);
		win_notify_dlg_closed();
		break;

	case WM_SHUTDOWN:
		if (manager_wm)
			break;
		win_notify_dlg_open();
		i = ui_msgbox(MBX_QUESTION_YN, (wchar_t *)IDS_2122);
		if (i == 0) {
			UnhookWindowsHookEx(hKeyboardHook);
			KillTimer(hwnd, TIMER_1SEC);
			PostQuitMessage(0);
		}
		win_notify_dlg_closed();
		break;

	case WM_CTRLALTDEL:
		if (manager_wm)
			break;
		manager_wm = 1;
		pc_reset(0);
		manager_wm = 0;
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
		plat_mouse_capture(0);
		if (hook_enabled) {
			UnhookWindowsHookEx(hKeyboardHook);
			hook_enabled = 0;
		}
		break;

	case WM_ENTERSIZEMOVE:
		user_resize = 1;
		break;

	case WM_EXITSIZEMOVE:
		user_resize = 0;

		/* If window is not resizable, then tell the main thread to
		   resize it, as sometimes, moves can mess up the window size. */
		if (!vid_resize)
			doresize = 1;
		break;
    }

    return(0);
}


static LRESULT CALLBACK
SubWindowProcedure(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam)
{
    return(DefWindowProc(hwnd, message, wParam, lParam));
}


int
ui_init(int nCmdShow)
{
    WCHAR title[200];
    WNDCLASSEX wincl;			/* buffer for main window's class */
    RAWINPUTDEVICE ridev;		/* RawInput device */
    MSG messages;			/* received-messages buffer */
    HWND hwnd = NULL;				/* handle for our window */
    HACCEL haccel;			/* handle to accelerator table */
	RECT sbar_rect;         /* RECT of the status bar */
    int bRet;

    if (settings_only) {
	if (! pc_init_modules()) {
		/* Dang, no ROMs found at all! */
		MessageBox(hwnd,
			   plat_get_string(IDS_2056),
			   plat_get_string(IDS_2050),
			   MB_OK | MB_ICONERROR);
		return(6);
	}

	win_settings_open(NULL);
	return(0);
    }

#ifdef USE_DISCORD
    if(! discord_load()) {
	enable_discord = 0;
    } else if (enable_discord) {
	/* Initialize the Discord API */
	discord_init();

	/* Update Discord status */
	discord_update_activity(dopause);
    }
#endif

    /* Create our main window's class and register it. */
    wincl.hInstance = hinstance;
    wincl.lpszClassName = CLASS_NAME;
    wincl.lpfnWndProc = MainWindowProcedure;
    wincl.style = CS_DBLCLKS;		/* Catch double-clicks */
    wincl.cbSize = sizeof(WNDCLASSEX);
    wincl.hIcon = LoadIcon(hinstance, (LPCTSTR)10);
    wincl.hIconSm = LoadIcon(hinstance, (LPCTSTR)10);
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
    menuMain = LoadMenu(hinstance, MENU_NAME);

    /* Now create our main window. */
    mbstowcs(title, emu_version, sizeof_w(title));
    hwnd = CreateWindowEx (
		0,			/* no extended possibilites */
		CLASS_NAME,		/* class name */
		title,			/* Title Text */
		(WS_OVERLAPPEDWINDOW & ~WS_SIZEBOX) | DS_3DLOOK,
		CW_USEDEFAULT,		/* Windows decides the position */
		CW_USEDEFAULT,		/* where window ends up on the screen */
		scrnsz_x+(GetSystemMetrics(SM_CXFIXEDFRAME)*2),	/* width */
		scrnsz_y+(GetSystemMetrics(SM_CYFIXEDFRAME)*2)+GetSystemMetrics(SM_CYMENUSIZE)+GetSystemMetrics(SM_CYCAPTION)+1,	/* and height in pixels */
		HWND_DESKTOP,		/* window is a child to desktop */
		menuMain,		/* menu */
		hinstance,		/* Program Instance handler */
		NULL);			/* no Window Creation data */
    hwndMain = hwnd;

    ui_window_title(title);

    /* Set up main window for resizing if configured. */
    if (vid_resize)
	SetWindowLongPtr(hwnd, GWL_STYLE,
			(WS_OVERLAPPEDWINDOW));
      else
	SetWindowLongPtr(hwnd, GWL_STYLE,
			(WS_OVERLAPPEDWINDOW&~WS_SIZEBOX&~WS_THICKFRAME&~WS_MAXIMIZEBOX));

    /* Move to the last-saved position if needed. */
    if (window_remember)
	MoveWindow(hwnd, window_x, window_y, window_w, window_h, TRUE);

    /* Reset all menus to their defaults. */
    ResetAllMenus();

    /* Make the window visible on the screen. */
    ShowWindow(hwnd, nCmdShow);

    /* Initialize the RawInput (keyboard) module. */
    memset(&ridev, 0x00, sizeof(ridev));
    ridev.usUsagePage = 0x01;
    ridev.usUsage = 0x06;
    ridev.dwFlags = RIDEV_NOHOTKEYS;
    ridev.hwndTarget = NULL;	/* current focus window */
    if (! RegisterRawInputDevices(&ridev, 1, sizeof(ridev))) {
	MessageBox(hwndMain,
		   plat_get_string(IDS_2114),
		   plat_get_string(IDS_2050),
		   MB_OK | MB_ICONERROR);
	return(4);
    }
    keyboard_getkeymap();

    /* Set up the main window for RawInput. */
    plat_set_input(hwndMain);

    /* Load the accelerator table */
    haccel = LoadAccelerators(hinstance, ACCEL_NAME);
    if (haccel == NULL) {
	MessageBox(hwndMain,
		   plat_get_string(IDS_2113),
		   plat_get_string(IDS_2050),
		   MB_OK | MB_ICONERROR);
	return(3);
    }

    /* Initialize the mouse module. */
    win_mouse_init();

    /* Create the status bar window. */
    StatusBarCreate(hwndMain, IDC_STATUS, hinstance);

	/* Get the actual height of the statusbar, 
	 * since that is not something we can change.
	 */
	GetWindowRect(hwndSBAR, &sbar_rect);
	sbar_height = sbar_rect.bottom - sbar_rect.top;

    /*
     * Before we can create the Render window, we first have
     * to prepare some other things that it depends on.
     */
    ghMutex = CreateMutex(NULL, FALSE, L"86Box.BlitMutex");

    /* Create the Machine Rendering window. */
    hwndRender = CreateWindow(L"STATIC", NULL, WS_CHILD|SS_BITMAP,
			      0, 0, 1, 1, hwnd, NULL, hinstance, NULL);
    MoveWindow(hwndRender, 0, 0, scrnsz_x, scrnsz_y, TRUE);

    /* All done, fire up the actual emulated machine. */
    if (! pc_init_modules()) {
	/* Dang, no ROMs found at all! */
	MessageBox(hwnd,
		   plat_get_string(IDS_2056),
		   plat_get_string(IDS_2050),
		   MB_OK | MB_ICONERROR);
	return(6);
    }

    /* Initialize the configured Video API. */
    if (! plat_setvid(vid_api)) {
	MessageBox(hwnd,
		   plat_get_string(IDS_2095),
		   plat_get_string(IDS_2050),
		   MB_OK | MB_ICONERROR);
	return(5);
    }

    /* Initialize the rendering window, or fullscreen. */
    if (start_in_fullscreen)
	plat_setfullscreen(1);

    /* Set up the current window size. */
    plat_resize(scrnsz_x, scrnsz_y);

    /* Fire up the machine. */
    pc_reset_hard_init();

    /* Set the PAUSE mode depending on the renderer. */
    plat_pause(0);

    /* If so requested via the command line, inform the
     * application that started us of our HWND, using the
     * the hWnd and unique ID the application has given
     * us. */
    if (source_hwnd)
	PostMessage((HWND) (uintptr_t) source_hwnd, WM_SENDHWND, (WPARAM) unique_id, (LPARAM) hwndMain);

    /*
     * Everything has been configured, and all seems to work,
     * so now it is time to start the main thread to do some
     * real work, and we will hang in here, dealing with the
     * UI until we're done.
     */
    do_start();

    /* Run the message loop. It will run until GetMessage() returns 0 */
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

	if (mouse_capture && keyboard_ismsexit()) {
		/* Release the in-app mouse. */
		plat_mouse_capture(0);
        }

	if (video_fullscreen && keyboard_isfsexit()) {
		/* Signal "exit fullscreen mode". */
		plat_setfullscreen(0);
	}

#ifdef USE_DISCORD
	/* Run Discord API callbacks */
	if (enable_discord)
		discord_run_callbacks();
#endif
    }

    timeEndPeriod(1);

    if (mouse_capture)
	plat_mouse_capture(0);

    /* Close down the emulator. */
    do_stop();

    UnregisterClass(SUB_CLASS_NAME, hinstance);
    UnregisterClass(CLASS_NAME, hinstance);

    win_mouse_close();

#ifdef USE_DISCORD
    /* Shut down the Discord integration */
    discord_close();
#endif

    return(messages.wParam);
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
    } else {
	if (s == NULL)
		s = wTitle;
    }

    return(s);
}


/* We should have the language ID as a parameter. */
void
plat_pause(int p)
{
    static wchar_t oldtitle[512];
    wchar_t title[512];

    /* If un-pausing, as the renderer if that's OK. */
    if (p == 0)
	p = get_vidpause();

    /* If already so, done. */
    if (dopause == p) {
	/* Send the WM to a manager if needed. */
	if (source_hwnd)
		PostMessage((HWND) (uintptr_t) source_hwnd, WM_SENDSTATUS, (WPARAM) !!dopause, (LPARAM) hwndMain);

	return;
    }

    if (p) {
	wcscpy(oldtitle, ui_window_title(NULL));
	wcscpy(title, oldtitle);
	wcscat(title, L" - PAUSED -");
	ui_window_title(title);
    } else {
	ui_window_title(oldtitle);
    }

    dopause = p;

    /* Update the actual menu. */
    CheckMenuItem(menuMain, IDM_ACTION_PAUSE,
		  (dopause) ? MF_CHECKED : MF_UNCHECKED);

#if USE_DISCORD
    /* Update Discord status */
    if(enable_discord)
	discord_update_activity(dopause);
#endif

    /* Send the WM to a manager if needed. */
    if (source_hwnd)
	PostMessage((HWND) (uintptr_t) source_hwnd, WM_SENDSTATUS, (WPARAM) !!dopause, (LPARAM) hwndMain);
}


/* Tell the UI about a new screen resolution. */
void
plat_resize(int x, int y)
{
    int sb_borders[3];
    RECT r;

    /* First, see if we should resize the UI window. */
    if (!vid_resize) {
	video_wait_for_blit();

	SendMessage(hwndSBAR, SB_GETBORDERS, 0, (LPARAM) sb_borders);

	GetWindowRect(hwndMain, &r);

	if (GetSystemMetrics(SM_CXPADDEDBORDER) == 0) {
		/* For platforms that subsystem version < 6.0 (gcc on mingw/msys2) */
		/* In this case, border sizes are different between resizable and non-resizable window */
		MoveWindow(hwndMain, r.left, r.top,
			x + (GetSystemMetrics(vid_resize ? SM_CXSIZEFRAME : SM_CXFIXEDFRAME) * 2),
			y + (GetSystemMetrics(vid_resize ? SM_CYSIZEFRAME : SM_CYFIXEDFRAME) * 2) + (GetSystemMetrics(SM_CYBORDER) + GetSystemMetrics(SM_CYMENUSIZE)) + GetSystemMetrics(SM_CYCAPTION) + sbar_height,
			TRUE);
	} else {
		/* For platforms that subsystem version >= 6.0 (clang/llvm on llvm-mingw, mainly for Windows/ARM) */
		/* In this case, border sizes are the same between resizable and non-resizable window */
		MoveWindow(hwndMain, r.left, r.top,
			x + ((GetSystemMetrics(SM_CXFRAME) + GetSystemMetrics(SM_CXPADDEDBORDER)) * 2),
			y + ((GetSystemMetrics(SM_CYFRAME) + GetSystemMetrics(SM_CXPADDEDBORDER)) * 2) + (GetSystemMetrics(SM_CYBORDER) + GetSystemMetrics(SM_CYMENUSIZE)) + GetSystemMetrics(SM_CYCAPTION) + sbar_height,
			TRUE);
	}

	MoveWindow(hwndRender, 0, 0, x, y, TRUE);
	GetWindowRect(hwndRender, &r);

	MoveWindow(hwndSBAR,
		   0, r.bottom + GetSystemMetrics(SM_CYEDGE),
		   x, 17, TRUE);

	if (mouse_capture) {
		GetWindowRect(hwndRender, &r);
		ClipCursor(&r);
	}
    }
}


void
plat_mouse_capture(int on)
{
    RECT rect;

    if (mouse_type == MOUSE_TYPE_NONE)
	return;

    if (on && !mouse_capture) {
	/* Enable the in-app mouse. */
	GetClipCursor(&oldclip);
	GetWindowRect(hwndRender, &rect);
	ClipCursor(&rect);
	show_cursor(0);
	mouse_capture = 1;
    } else if (!on && mouse_capture) {
	/* Disable the in-app mouse. */
	ClipCursor(&oldclip);
	show_cursor(-1);

	mouse_capture = 0;
    }
}


/* Catch WM_INPUT messages for 'current focus' window. */
static LONG_PTR	input_orig_proc;
static HWND input_orig_hwnd = NULL;
#if defined(__amd64__) || defined(__aarch64__)
static LRESULT CALLBACK
#else
static BOOL CALLBACK
#endif
input_proc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam)
{
    switch (message) {
	case WM_INPUT:
		keyboard_handle(lParam, infocus);
#ifndef USE_DINPUT
		win_mouse_handle(lParam, infocus);
#endif
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
		plat_mouse_capture(0);
		if (hook_enabled) {
			UnhookWindowsHookEx(hKeyboardHook);
			hook_enabled = 0;
		}
		break;

	case WM_LBUTTONUP:
		if (! video_fullscreen)
			plat_mouse_capture(1);
		break;

	case WM_MBUTTONUP:
		if (mouse_get_buttons() < 3)
			plat_mouse_capture(0);
		break;

	default:
		return(CallWindowProc((WNDPROC)input_orig_proc,
				      hwnd, message, wParam, lParam));
    }

    return(0);
}


/* Set up a handler for the 'currently active' window. */
void
plat_set_input(HWND h)
{
    /* If needed, rest the old one first. */
    if (input_orig_hwnd != NULL) {
	SetWindowLongPtr(input_orig_hwnd, GWLP_WNDPROC,
			 (LONG_PTR)input_orig_proc);
    }

    /* Redirect the window procedure so we can catch WM_INPUT. */
    input_orig_proc = GetWindowLongPtr(h, GWLP_WNDPROC);
    input_orig_hwnd = h;
    SetWindowLongPtr(h, GWLP_WNDPROC, (LONG_PTR)&input_proc);
}
