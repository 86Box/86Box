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
 *
 *
 * Authors:	Sarah Walker, <http://pcem-emulator.co.uk/>
 *		Miran Grca, <mgrca8@gmail.com>
 *		Fred N. van Kempen, <decwiz@yahoo.com>
 *
 *		Copyright 2008-2020 Sarah Walker.
 *		Copyright 2016-2020 Miran Grca.
 *		Copyright 2017-2020 Fred N. van Kempen.
 *		Copyright 2019,2020 GH Cao.
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
#include <86box/plat.h>
#include <86box/86box.h>
#include <86box/config.h>
#include "../cpu/cpu.h"
#include <86box/device.h>
#include <86box/keyboard.h>
#include <86box/mouse.h>
#include <86box/timer.h>
#include <86box/nvr.h>
#include <86box/video.h>
#include <86box/vid_ega.h>		// for update_overscan
#include <86box/plat_midi.h>
#include <86box/plat_dynld.h>
#include <86box/ui.h>
#include <86box/win.h>
#include <86box/version.h>
#ifdef USE_DISCORD
# include <86box/win_discord.h>
#endif

#ifdef MTR_ENABLED
#include <minitrace/minitrace.h>
#endif

#define TIMER_1SEC	1		/* ID of the one-second timer */


/* Platform Public data, specific. */
HWND		hwndMain,		/* application main window */
		hwndRender;		/* machine render window */
HMENU		menuMain;		/* application main menu */
HICON		hIcon[256];		/* icon data loaded from resources */
RECT		oldclip;		/* mouse rect */
int		sbar_height = 23;	/* statusbar height */
int		minimized = 0;
int		infocus = 1, button_down = 0;
int		rctrl_is_lalt = 0;
int		user_resize = 0;
int		fixed_size_x = 0, fixed_size_y = 0;
int		kbd_req_capture = 0;
int		hide_status_bar = 0;

extern char	openfilestring[512];
extern WCHAR	wopenfilestring[512];


/* Local data. */
static wchar_t	wTitle[512];
static int	manager_wm = 0;
static int	save_window_pos = 0, pause_state = 0;
static int	dpi = 96;
static int	padded_frame = 0;
static int	vis = -1;

/* Per Monitor DPI Aware v2 APIs, Windows 10 v1703+ */
void* user32_handle = NULL;
static UINT  (WINAPI *pGetDpiForWindow)(HWND);
static UINT (WINAPI *pGetSystemMetricsForDpi)(int i, UINT dpi);
static DPI_AWARENESS_CONTEXT (WINAPI *pGetWindowDpiAwarenessContext)(HWND);
static BOOL (WINAPI *pAreDpiAwarenessContextsEqual)(DPI_AWARENESS_CONTEXT A, DPI_AWARENESS_CONTEXT B);
static dllimp_t user32_imports[] = {
{ "GetDpiForWindow",	&pGetDpiForWindow },
{ "GetSystemMetricsForDpi", &pGetSystemMetricsForDpi },
{ "GetWindowDpiAwarenessContext", &pGetWindowDpiAwarenessContext },
{ "AreDpiAwarenessContextsEqual", &pAreDpiAwarenessContextsEqual },
{ NULL,		NULL		}
};

int
win_get_dpi(HWND hwnd) {
    if (user32_handle != NULL) {
        return pGetDpiForWindow(hwnd);
    } else {
        HDC dc = GetDC(hwnd);
        UINT dpi = GetDeviceCaps(dc, LOGPIXELSX);
        ReleaseDC(hwnd, dc);
        return dpi;
    }
}

int win_get_system_metrics(int index, int dpi) {
    if (user32_handle != NULL) {
        /* Only call GetSystemMetricsForDpi when we are using PMv2 */
        DPI_AWARENESS_CONTEXT c = pGetWindowDpiAwarenessContext(hwndMain);
        if (pAreDpiAwarenessContextsEqual(c, DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2))
            return pGetSystemMetricsForDpi(index, dpi);
    }

    return GetSystemMetrics(index);
}

void
ResizeWindowByClientArea(HWND hwnd, int width, int height)
{
	if ((vid_resize == 1) || padded_frame) {
		int padding = win_get_system_metrics(SM_CXPADDEDBORDER, dpi);
		width  += (win_get_system_metrics(SM_CXFRAME, dpi) + padding) * 2;
		height += (win_get_system_metrics(SM_CYFRAME, dpi) + padding) * 2;
	} else {
		width  += win_get_system_metrics(SM_CXFIXEDFRAME, dpi) * 2;
		height += win_get_system_metrics(SM_CYFIXEDFRAME, dpi) * 2;
	}

	height += win_get_system_metrics(SM_CYCAPTION, dpi);
	height += win_get_system_metrics(SM_CYBORDER, dpi) + win_get_system_metrics(SM_CYMENUSIZE, dpi);

	SetWindowPos(hwnd, NULL, 0, 0, width, height, SWP_NOMOVE);
}

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
    *val ^= 1;
    CheckMenuItem(h, id, *val ? MF_CHECKED : MF_UNCHECKED);
    endblit();
    config_save();
    device_force_redraw();
}

#if defined(DEV_BRANCH) && defined(USE_OPENGL)
/* Recursively finds and deletes target submenu */
static int
delete_submenu(HMENU parent, HMENU target)
{
	for (int i = 0; i < GetMenuItemCount(parent); i++)
	{
		MENUITEMINFO mii;
		mii.cbSize = sizeof(mii);
		mii.fMask = MIIM_SUBMENU;

		if (GetMenuItemInfo(parent, i, TRUE, &mii) != 0)
		{
			if (mii.hSubMenu == target)
			{
				DeleteMenu(parent, i, MF_BYPOSITION);
				return 1;
			}
			else if (mii.hSubMenu != NULL)
			{
				if (delete_submenu(mii.hSubMenu, target))
					return 1;
			}
		}
	}

	return 0;
}
#endif

static void
show_render_options_menu()
{
#if defined(DEV_BRANCH) && defined(USE_OPENGL)
	static int menu_vidapi = -1;
	static HMENU cur_menu = NULL;
	
	if (vid_api == menu_vidapi)
		return;

	if (cur_menu != NULL)
	{
		if (delete_submenu(menuMain, cur_menu))
			cur_menu = NULL;
	}

	if (cur_menu == NULL)
	{
		switch (IDM_VID_SDL_SW + vid_api)
		{
		case IDM_VID_OPENGL_CORE:
			cur_menu = LoadMenu(hinstance, VID_GL_SUBMENU);
			InsertMenu(GetSubMenu(menuMain, 1), 4, MF_BYPOSITION | MF_STRING | MF_POPUP, (UINT_PTR)cur_menu, plat_get_string(IDS_2144));
			CheckMenuItem(menuMain, IDM_VID_GL_FPS_BLITTER, video_framerate == -1 ? MF_CHECKED : MF_UNCHECKED);
			CheckMenuItem(menuMain, IDM_VID_GL_FPS_25, video_framerate == 25 ? MF_CHECKED : MF_UNCHECKED);
			CheckMenuItem(menuMain, IDM_VID_GL_FPS_30, video_framerate == 30 ? MF_CHECKED : MF_UNCHECKED);
			CheckMenuItem(menuMain, IDM_VID_GL_FPS_50, video_framerate == 50 ? MF_CHECKED : MF_UNCHECKED);
			CheckMenuItem(menuMain, IDM_VID_GL_FPS_60, video_framerate == 60 ? MF_CHECKED : MF_UNCHECKED);
			CheckMenuItem(menuMain, IDM_VID_GL_FPS_75, video_framerate == 75 ? MF_CHECKED : MF_UNCHECKED);
			CheckMenuItem(menuMain, IDM_VID_GL_VSYNC, video_vsync ? MF_CHECKED : MF_UNCHECKED);
			EnableMenuItem(menuMain, IDM_VID_GL_NOSHADER, strlen(video_shader) > 0 ? MF_ENABLED : MF_DISABLED);
			break;
		}
	}

	menu_vidapi = vid_api;
#endif
}

static void
video_set_filter_menu(HMENU menu) 
{
	CheckMenuItem(menu, IDM_VID_FILTER_NEAREST, vid_api == 0 || video_filter_method == 0 ? MF_CHECKED : MF_UNCHECKED);
	CheckMenuItem(menu, IDM_VID_FILTER_LINEAR, vid_api != 0 && video_filter_method == 1 ? MF_CHECKED : MF_UNCHECKED);
	EnableMenuItem(menu, IDM_VID_FILTER_NEAREST, vid_api == 0 ? MF_GRAYED : MF_ENABLED);
	EnableMenuItem(menu, IDM_VID_FILTER_LINEAR, vid_api == 0 ? MF_GRAYED : MF_ENABLED);
}

static void
ResetAllMenus(void)
{
    CheckMenuItem(menuMain, IDM_ACTION_RCTRL_IS_LALT, MF_UNCHECKED);
    CheckMenuItem(menuMain, IDM_ACTION_KBD_REQ_CAPTURE, MF_UNCHECKED);

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

    CheckMenuItem(menuMain, IDM_VID_HIDE_STATUS_BAR, MF_UNCHECKED);
    CheckMenuItem(menuMain, IDM_VID_FORCE43, MF_UNCHECKED);
    CheckMenuItem(menuMain, IDM_VID_OVERSCAN, MF_UNCHECKED);
    CheckMenuItem(menuMain, IDM_VID_INVERT, MF_UNCHECKED);

    CheckMenuItem(menuMain, IDM_VID_RESIZE, MF_UNCHECKED);
    CheckMenuItem(menuMain, IDM_VID_SDL_SW, MF_UNCHECKED);
    CheckMenuItem(menuMain, IDM_VID_SDL_HW, MF_UNCHECKED);
    CheckMenuItem(menuMain, IDM_VID_SDL_OPENGL, MF_UNCHECKED);
#if defined(DEV_BRANCH) && defined(USE_OPENGL)
    CheckMenuItem(menuMain, IDM_VID_OPENGL_CORE, MF_UNCHECKED);
    show_render_options_menu();
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
	CheckMenuItem(menuMain, IDM_VID_HIDPI, MF_UNCHECKED);

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
    CheckMenuItem(menuMain, IDM_ACTION_KBD_REQ_CAPTURE, kbd_req_capture ? MF_CHECKED : MF_UNCHECKED);

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

    CheckMenuItem(menuMain, IDM_VID_HIDE_STATUS_BAR, hide_status_bar ? MF_CHECKED : MF_UNCHECKED);
    CheckMenuItem(menuMain, IDM_VID_FORCE43, force_43?MF_CHECKED:MF_UNCHECKED);
    CheckMenuItem(menuMain, IDM_VID_OVERSCAN, enable_overscan?MF_CHECKED:MF_UNCHECKED);
    CheckMenuItem(menuMain, IDM_VID_INVERT, invert_display ? MF_CHECKED : MF_UNCHECKED);

    if (vid_resize == 1)
	CheckMenuItem(menuMain, IDM_VID_RESIZE, MF_CHECKED);
    CheckMenuItem(menuMain, IDM_VID_SDL_SW+vid_api, MF_CHECKED);
    CheckMenuItem(menuMain, IDM_VID_FS_FULL+video_fullscreen_scale, MF_CHECKED);
    CheckMenuItem(menuMain, IDM_VID_REMEMBER, window_remember?MF_CHECKED:MF_UNCHECKED);
    CheckMenuItem(menuMain, IDM_VID_SCALE_1X+scale, MF_CHECKED);
    CheckMenuItem(menuMain, IDM_VID_HIDPI, dpi_scale?MF_CHECKED:MF_UNCHECKED);

    CheckMenuItem(menuMain, IDM_VID_CGACON, vid_cga_contrast?MF_CHECKED:MF_UNCHECKED);
    CheckMenuItem(menuMain, IDM_VID_GRAYCT_601+video_graytype, MF_CHECKED);
    CheckMenuItem(menuMain, IDM_VID_GRAY_RGB+video_grayscale, MF_CHECKED);

    video_set_filter_menu(menuMain);

#ifdef USE_DISCORD
    if (discord_loaded)
	CheckMenuItem(menuMain, IDM_DISCORD, enable_discord ? MF_CHECKED : MF_UNCHECKED);
    else
	EnableMenuItem(menuMain, IDM_DISCORD, MF_DISABLED);
#endif
#ifdef MTR_ENABLED
    EnableMenuItem(menuMain, IDM_ACTION_END_TRACE, MF_DISABLED);
#endif

    if (vid_resize) {
	if (vid_resize >= 2) {
		CheckMenuItem(menuMain, IDM_VID_RESIZE, MF_UNCHECKED);
		EnableMenuItem(menuMain, IDM_VID_RESIZE, MF_GRAYED);
	}

	CheckMenuItem(menuMain, IDM_VID_SCALE_1X + scale, MF_UNCHECKED);
	CheckMenuItem(menuMain, IDM_VID_SCALE_2X, MF_CHECKED);
	EnableMenuItem(menuMain, IDM_VID_SCALE_1X, MF_GRAYED);
	EnableMenuItem(menuMain, IDM_VID_SCALE_2X, MF_GRAYED);
	EnableMenuItem(menuMain, IDM_VID_SCALE_3X, MF_GRAYED);
	EnableMenuItem(menuMain, IDM_VID_SCALE_4X, MF_GRAYED);
    }
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


void
plat_power_off(void)
{
    confirm_exit = 0;
    nvr_save();
    config_save();

    /* Deduct a sufficiently large number of cycles that no instructions will
       run before the main thread is terminated */
    cycles -= 99999999;

    KillTimer(hwndMain, TIMER_1SEC);
    PostQuitMessage(0);

    /* Cleanly terminate all of the emulator's components so as
       to avoid things like threads getting stuck. */
    // do_stop();
    cpu_thread_run = 0;

    // exit(-1);
}

#ifdef MTR_ENABLED
static void
handle_trace(HMENU hmenu, int trace)
{
    EnableMenuItem(hmenu, IDM_ACTION_BEGIN_TRACE, trace? MF_GRAYED : MF_ENABLED);
    EnableMenuItem(hmenu, IDM_ACTION_END_TRACE, trace? MF_ENABLED : MF_GRAYED);
    if (trace) {
        init_trace();
    } else {
        shutdown_trace();
    }
}
#endif

/* Catch WM_INPUT messages for 'current focus' window. */
#if defined(__amd64__) || defined(__aarch64__)
static LRESULT CALLBACK
#else
static BOOL CALLBACK
#endif
input_proc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam)
{
    switch (message) {
	case WM_INPUT:
		if (infocus) {
			UINT size = 0;
			PRAWINPUT raw = NULL;

			/* Here we read the raw input data */
			GetRawInputData((HRAWINPUT)lParam, RID_INPUT, NULL, &size, sizeof(RAWINPUTHEADER));
			raw = (PRAWINPUT)malloc(size);
			if (GetRawInputData((HRAWINPUT)lParam, RID_INPUT, raw, &size, sizeof(RAWINPUTHEADER)) == size) {
				switch(raw->header.dwType)
				{
					case RIM_TYPEKEYBOARD:
						keyboard_handle(raw);
						break;
					case RIM_TYPEMOUSE:
						win_mouse_handle(raw);
						break;
					case RIM_TYPEHID:
						win_joystick_handle(raw);
						break;
				}
			}
			free(raw);
		}
		break;
	case WM_SETFOCUS:
		infocus = 1;
		break;

	case WM_KILLFOCUS:
		infocus = 0;
		plat_mouse_capture(0);
		break;

	case WM_LBUTTONDOWN:
		button_down |= 1;
		break;

	case WM_LBUTTONUP:
		if ((button_down & 1) && !video_fullscreen)
			plat_mouse_capture(1);
		button_down &= ~1;
		break;

	case WM_MBUTTONUP:
		if (mouse_get_buttons() < 3)
			plat_mouse_capture(0);
		break;

	default:
		return(1);
		/* return(CallWindowProc((WNDPROC)input_orig_proc,
				      hwnd, message, wParam, lParam)); */
    }

    return(0);
}


static LRESULT CALLBACK
MainWindowProcedure(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam)
{
    HMENU hmenu;

    int i;
    RECT rect, *rect_p;

    WINDOWPOS *pos;

    int temp_x, temp_y;

    if (input_proc(hwnd, message, wParam, lParam) == 0)
	return(0);

    switch (message) {
	case WM_CREATE:
		SetTimer(hwnd, TIMER_1SEC, 1000, NULL);
		break;

	case WM_COMMAND:
		hmenu = GetMenu(hwnd);
		switch (LOWORD(wParam)) {
			case IDM_ACTION_SCREENSHOT:
				take_screenshot();
				break;

#ifdef MTR_ENABLED
		case IDM_ACTION_BEGIN_TRACE:
		case IDM_ACTION_END_TRACE:
		case IDM_ACTION_TRACE:
			tracing_on = !tracing_on;
			handle_trace(hmenu, tracing_on);
			break;
#endif

			case IDM_ACTION_HRESET:
				win_notify_dlg_open();
				if (confirm_reset)
					i = ui_msgbox_ex(MBX_QUESTION_YN | MBX_DONTASK, (wchar_t *) IDS_2112, NULL, (wchar_t *) IDS_2137, (wchar_t *) IDS_2138, NULL);
				else
					i = 0;
				if ((i % 10) == 0) {
					pc_reset_hard();
					if (i == 10) {
						confirm_reset = 0;
						nvr_save();
						config_save();
					}
				}
				win_notify_dlg_closed();
				break;

			case IDM_ACTION_RESET_CAD:
				pc_send_cad();
				break;

			case IDM_ACTION_EXIT:
				win_notify_dlg_open();
				if (confirm_exit && confirm_exit_cmdl)
					i = ui_msgbox_ex(MBX_QUESTION_YN | MBX_DONTASK, (wchar_t *) IDS_2113, NULL, (wchar_t *) IDS_2119, (wchar_t *) IDS_2136, NULL);
				else
					i = 0;
				if ((i % 10) == 0) {
					if (i == 10) {
						confirm_exit = 0;
						nvr_save();
						config_save();
					}
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

			case IDM_ACTION_KBD_REQ_CAPTURE:
				kbd_req_capture ^= 1;
				CheckMenuItem(hmenu, IDM_ACTION_KBD_REQ_CAPTURE, kbd_req_capture ? MF_CHECKED : MF_UNCHECKED);
				config_save();
				break;

			case IDM_ACTION_PAUSE:
				plat_pause(dopause ^ 1);
				CheckMenuItem(menuMain, IDM_ACTION_PAUSE, dopause ? MF_CHECKED : MF_UNCHECKED);
				break;

			case IDM_CONFIG:
				win_settings_open(hwnd);
				break;

			case IDM_SND_GAIN:
				SoundGainDialogCreate(hwnd);
				break;

			case IDM_ABOUT:
				AboutDialogCreate(hwnd);
				break;

			case IDM_DOCS:
				ShellExecute(hwnd, L"open", EMU_DOCS_URL, NULL, NULL, SW_SHOW);
				break;

			case IDM_UPDATE_ICONS:
				update_icons ^= 1;
				CheckMenuItem(hmenu, IDM_UPDATE_ICONS, update_icons ? MF_CHECKED : MF_UNCHECKED);
				config_save();
				break;

			case IDM_VID_HIDE_STATUS_BAR:
				hide_status_bar ^= 1;
				CheckMenuItem(hmenu, IDM_VID_HIDE_STATUS_BAR, hide_status_bar ? MF_CHECKED : MF_UNCHECKED);
				ShowWindow(hwndSBAR, hide_status_bar ? SW_HIDE : SW_SHOW);
				GetWindowRect(hwnd, &rect);
				if (hide_status_bar)
					MoveWindow(hwnd, rect.left, rect.top, rect.right - rect.left, rect.bottom - rect.top - sbar_height, TRUE);
				else
					MoveWindow(hwnd, rect.left, rect.top, rect.right - rect.left, rect.bottom - rect.top + sbar_height, TRUE);
				config_save();
				break;

			case IDM_VID_RESIZE:
				vid_resize ^= 1;
				CheckMenuItem(hmenu, IDM_VID_RESIZE, (vid_resize & 1) ? MF_CHECKED : MF_UNCHECKED);

				if (vid_resize == 1)
					SetWindowLongPtr(hwnd, GWL_STYLE, (WS_OVERLAPPEDWINDOW) | WS_VISIBLE);
				else
					SetWindowLongPtr(hwnd, GWL_STYLE, (WS_OVERLAPPEDWINDOW & ~WS_SIZEBOX & ~WS_MAXIMIZEBOX) | WS_VISIBLE);

				/* scale the screen base on DPI */
				if (dpi_scale) {
					temp_x = MulDiv(unscaled_size_x, dpi, 96);
					temp_y = MulDiv(unscaled_size_y, dpi, 96);
				} else {
					temp_x = unscaled_size_x;
					temp_y = unscaled_size_y;
				}

				if (hide_status_bar)
					ResizeWindowByClientArea(hwnd, temp_x, temp_y);
				else
					ResizeWindowByClientArea(hwnd, temp_x, temp_y + sbar_height);

				if (mouse_capture) {
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

				scrnsz_x = unscaled_size_x;
				scrnsz_y = unscaled_size_y;
				doresize = 1;
				config_save();
				break;

			case IDM_VID_REMEMBER:
				window_remember = !window_remember;
				CheckMenuItem(hmenu, IDM_VID_REMEMBER, window_remember ? MF_CHECKED : MF_UNCHECKED);
				GetWindowRect(hwnd, &rect);
				if (!(vid_resize & 2) && window_remember) {
					window_x = rect.left;
					window_y = rect.top;
					window_w = rect.right - rect.left;
					window_h = rect.bottom - rect.top;
				}
				config_save();
				break;

			case IDM_VID_SDL_SW:
			case IDM_VID_SDL_HW:
			case IDM_VID_SDL_OPENGL:
#if defined(DEV_BRANCH) && defined(USE_OPENGL)
			case IDM_VID_OPENGL_CORE:
#endif
#ifdef USE_VNC
			case IDM_VID_VNC:
#endif
				CheckMenuItem(hmenu, IDM_VID_SDL_SW + vid_api, MF_UNCHECKED);
				plat_setvid(LOWORD(wParam) - IDM_VID_SDL_SW);
				CheckMenuItem(hmenu, IDM_VID_SDL_SW + vid_api, MF_CHECKED);
				video_set_filter_menu(hmenu);
				config_save();
				show_render_options_menu();
				break;

#if defined(DEV_BRANCH) && defined(USE_OPENGL)
			case IDM_VID_GL_FPS_BLITTER:
			case IDM_VID_GL_FPS_25:
			case IDM_VID_GL_FPS_30:
			case IDM_VID_GL_FPS_50:
			case IDM_VID_GL_FPS_60:
			case IDM_VID_GL_FPS_75:
			{
				static const int fps[] = { -1, 25, 30, 50, 60, 75 };
				int idx = 0;
				for (; fps[idx] != video_framerate; idx++);
				CheckMenuItem(hmenu, IDM_VID_GL_FPS_BLITTER + idx, MF_UNCHECKED);
				video_framerate = fps[LOWORD(wParam) - IDM_VID_GL_FPS_BLITTER];
				CheckMenuItem(hmenu, LOWORD(wParam), MF_CHECKED);
				plat_vid_reload_options();
				config_save();
				break;
			}
			case IDM_VID_GL_VSYNC:
				video_vsync = !video_vsync;
				CheckMenuItem(hmenu, IDM_VID_GL_VSYNC, video_vsync ? MF_CHECKED : MF_UNCHECKED);
				plat_vid_reload_options();
				config_save();
				break;
			case IDM_VID_GL_SHADER:
				win_notify_dlg_open();
				if (file_dlg_st(hwnd, IDS_2143, video_shader, NULL, 0) == 0)
				{
					strcpy_s(video_shader, sizeof(video_shader), openfilestring);
					EnableMenuItem(menuMain, IDM_VID_GL_NOSHADER, strlen(video_shader) > 0 ? MF_ENABLED : MF_DISABLED);
				}
				win_notify_dlg_closed();
				plat_vid_reload_options();
				break;
			case IDM_VID_GL_NOSHADER:
				video_shader[0] = '\0';
				EnableMenuItem(menuMain, IDM_VID_GL_NOSHADER, MF_DISABLED);
				plat_vid_reload_options();
				break;
#endif

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
				reset_screen_size();
				device_force_redraw();
				video_force_resize_set(1);
				doresize = 1;
				config_save();
				break;

			case IDM_VID_FILTER_NEAREST:
			case IDM_VID_FILTER_LINEAR:				
				video_filter_method = LOWORD(wParam) - IDM_VID_FILTER_NEAREST;
				video_set_filter_menu(hmenu);
				plat_vid_reload_options();
				config_save();
				break;

			case IDM_VID_HIDPI:
				dpi_scale = !dpi_scale;
				CheckMenuItem(hmenu, IDM_VID_HIDPI, dpi_scale ? MF_CHECKED : MF_UNCHECKED);
				doresize = 1;
				config_save();
				break;

			case IDM_VID_SPECIFY_DIM:
				SpecifyDimensionsDialogCreate(hwnd);
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
			default:
				media_menu_proc(hwnd, message, wParam, lParam);
				break;
		}
		return(0);

	case WM_ENTERMENULOOP:
		break;

	case WM_DPICHANGED:
		dpi = HIWORD(wParam);
		GetWindowRect(hwndSBAR, &rect);
		sbar_height = rect.bottom - rect.top;
		rect_p = (RECT*)lParam;
		if (vid_resize == 1)
			MoveWindow(hwnd, rect_p->left, rect_p->top, rect_p->right - rect_p->left, rect_p->bottom - rect_p->top, TRUE);
		else if (vid_resize >= 2) {
			temp_x = fixed_size_x;
			temp_y = fixed_size_y;
			if (dpi_scale) {
				temp_x = MulDiv(temp_x, dpi, 96);
				temp_y = MulDiv(temp_y, dpi, 96);
			}

			/* Main Window. */
			if (hide_status_bar)
				ResizeWindowByClientArea(hwndMain, temp_x, temp_y);
			else
				ResizeWindowByClientArea(hwndMain, temp_x, temp_y + sbar_height);
		} else if (!user_resize)
			doresize = 1;
		break;

	case WM_WINDOWPOSCHANGED:
		pos = (WINDOWPOS*)lParam;
		GetClientRect(hwndMain, &rect);

		if (IsIconic(hwndMain)) {
			plat_vidapi_enable(0);
			minimized = 1;
			return(0);
		} else if (minimized) {
			minimized = 0;
			video_force_resize_set(1);
		}

		if (window_remember) {
			window_x = pos->x;
			window_y = pos->y;
			window_w = pos->cx;
			window_h = pos->cy;
			save_window_pos = 1;
			config_save();
		}

		if (!(pos->flags & SWP_NOSIZE) || !user_resize) {
			plat_vidapi_enable(0);

			if (hide_status_bar)
				MoveWindow(hwndRender, 0, 0, rect.right, rect.bottom, TRUE);
			else {
				MoveWindow(hwndSBAR, 0, rect.bottom - sbar_height, sbar_height, rect.right, TRUE);
				MoveWindow(hwndRender, 0, 0, rect.right, rect.bottom - sbar_height, TRUE);
			}

			GetClientRect(hwndRender, &rect);
			if (dpi_scale) {
				temp_x = MulDiv(rect.right, 96, dpi);
				temp_y = MulDiv(rect.bottom, 96, dpi);

				if (temp_x != scrnsz_x || temp_y != scrnsz_y) {
					scrnsz_x = temp_x;
					scrnsz_y = temp_y;
					doresize = 1;
				}
			} else {
				if (rect.right != scrnsz_x || rect.bottom != scrnsz_y) {
					scrnsz_x = rect.right;
					scrnsz_y = rect.bottom;
					doresize = 1;
				}
			}

			plat_vidsize(rect.right, rect.bottom);

			if (mouse_capture) {
				GetWindowRect(hwndRender, &rect);
				ClipCursor(&rect);
			}

			plat_vidapi_enable(2);
		}

		return(0);

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
		if (confirm_exit && confirm_exit_cmdl)
			i = ui_msgbox_ex(MBX_QUESTION_YN | MBX_DONTASK, (wchar_t *) IDS_2113, NULL, (wchar_t *) IDS_2119, (wchar_t *) IDS_2136, NULL);
		else
			i = 0;
		if ((i % 10) == 0) {
			if (i == 10) {
				confirm_exit = 0;
				nvr_save();
				config_save();
			}
			KillTimer(hwnd, TIMER_1SEC);
			PostQuitMessage(0);
		}
		win_notify_dlg_closed();
		break;

	case WM_DESTROY:
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
		if (confirm_reset)
			i = ui_msgbox_ex(MBX_QUESTION_YN | MBX_DONTASK, (wchar_t *) IDS_2112, NULL, (wchar_t *) IDS_2137, (wchar_t *) IDS_2138, NULL);
		else
			i = 0;
		if ((i % 10) == 0) {
			pc_reset_hard();
			if (i == 10) {
				confirm_reset = 0;
				nvr_save();
				config_save();
			}
		}
		win_notify_dlg_closed();
		break;

	case WM_SHUTDOWN:
		if (manager_wm)
			break;
		win_notify_dlg_open();
		if (confirm_exit && confirm_exit_cmdl)
			i = ui_msgbox_ex(MBX_QUESTION_YN | MBX_DONTASK, (wchar_t *) IDS_2113, NULL, (wchar_t *) IDS_2119, (wchar_t *) IDS_2136, NULL);
		else
			i = 0;
		if ((i % 10) == 0) {
			if (i == 10) {
				confirm_exit = 0;
				nvr_save();
				config_save();
			}
			KillTimer(hwnd, TIMER_1SEC);
			PostQuitMessage(0);
		}
		win_notify_dlg_closed();
		break;

	case WM_CTRLALTDEL:
		if (manager_wm)
			break;
		manager_wm = 1;
		pc_send_cad();
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
		break;

	case WM_KILLFOCUS:
		infocus = 0;
		plat_mouse_capture(0);
		break;

	case WM_ACTIVATE:
		if ((wParam != WA_INACTIVE) && !(video_fullscreen & 2)) {
			video_force_resize_set(1);
			plat_vidapi_enable(0);
			plat_vidapi_enable(1);
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
    switch (message) {
	case WM_LBUTTONDOWN:
		button_down |= 2;
		break;

	case WM_LBUTTONUP:
		if ((button_down & 2) && !video_fullscreen)
			plat_mouse_capture(1);
		button_down &= ~2;
		break;

	case WM_MBUTTONUP:
		if (mouse_get_buttons() < 3)
			plat_mouse_capture(0);
		break;

	default:
		return(DefWindowProc(hwnd, message, wParam, lParam));
    }

    return(0);
}


static LRESULT CALLBACK
SDLMainWindowProcedure(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam)
{
    if (input_proc(hwnd, message, wParam, lParam) == 0)
	return(0);

    return(DefWindowProc(hwnd, message, wParam, lParam));
}


static LRESULT CALLBACK
SDLSubWindowProcedure(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam)
{
    return(DefWindowProc(hwnd, message, wParam, lParam));
}


static HRESULT CALLBACK
TaskDialogProcedure(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam, LONG_PTR lpRefData)
{
    switch (message) {
	case TDN_HYPERLINK_CLICKED:
		/* open linked URL */
		ShellExecute(hwnd, L"open", (LPCWSTR) lParam, NULL, NULL, SW_SHOW);
		break;
    }

    return S_OK;
}


int
ui_init(int nCmdShow)
{
    WCHAR title[200];
    WNDCLASSEX wincl;			/* buffer for main window's class */
    RAWINPUTDEVICE ridev;		/* RawInput device */
    MSG messages = {0};			/* received-messages buffer */
    HWND hwnd = NULL;			/* handle for our window */
    HACCEL haccel;			/* handle to accelerator table */
    RECT sbar_rect;			/* RECT of the status bar */
    int bRet;
    TASKDIALOGCONFIG tdconfig = {0};
    TASKDIALOG_BUTTON tdbuttons[] = {{IDCANCEL, MAKEINTRESOURCE(IDS_2119)}};

    /* Load DPI related Windows 10 APIs */
    user32_handle = dynld_module("user32.dll", user32_imports);

    /* Set up TaskDialog configuration. */
    tdconfig.cbSize = sizeof(tdconfig);
    tdconfig.dwFlags = TDF_ENABLE_HYPERLINKS;
    tdconfig.dwCommonButtons = 0;
    tdconfig.pszWindowTitle = MAKEINTRESOURCE(IDS_STRINGS);
    tdconfig.pszMainIcon = TD_ERROR_ICON;
    tdconfig.pszMainInstruction = MAKEINTRESOURCE(IDS_2050);
    tdconfig.cButtons = ARRAYSIZE(tdbuttons);
    tdconfig.pButtons = tdbuttons;
    tdconfig.pfCallback = TaskDialogProcedure;

    /* Start settings-only mode if requested. */
    if (settings_only) {
	if (! pc_init_modules()) {
		/* Dang, no ROMs found at all! */
		tdconfig.pszMainInstruction = MAKEINTRESOURCE(IDS_2120);
		tdconfig.pszContent = MAKEINTRESOURCE(IDS_2056);
		TaskDialogIndirect(&tdconfig, NULL, NULL, NULL);
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
    wincl.lpszClassName = SDL_CLASS_NAME;
    wincl.lpfnWndProc = SDLMainWindowProcedure;
    if (! RegisterClassEx(&wincl))
			return(2);
    wincl.lpszClassName = SDL_SUB_CLASS_NAME;
    wincl.lpfnWndProc = SDLSubWindowProcedure;
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
    hwndMain = tdconfig.hwndParent = hwnd;

    ui_window_title(title);

    /* Get the current DPI */
    dpi = win_get_dpi(hwndMain);

    /* Check if we have a padded window frame */
    padded_frame = (GetSystemMetrics(SM_CXPADDEDBORDER) != 0);

    /* Create the status bar window. */
    StatusBarCreate(hwndMain, IDC_STATUS, hinstance);

    /* Get the actual height of the status bar */
    GetWindowRect(hwndSBAR, &sbar_rect);
    sbar_height = sbar_rect.bottom - sbar_rect.top;
    if (hide_status_bar)
	ShowWindow(hwndSBAR, SW_HIDE);

    /* Set up main window for resizing if configured. */
    if (vid_resize == 1)
	SetWindowLongPtr(hwnd, GWL_STYLE,
			(WS_OVERLAPPEDWINDOW));
    else
	SetWindowLongPtr(hwnd, GWL_STYLE,
			(WS_OVERLAPPEDWINDOW&~WS_SIZEBOX&~WS_THICKFRAME&~WS_MAXIMIZEBOX));

    /* Create the Machine Rendering window. */
    hwndRender = CreateWindow(/*L"STATIC"*/ SUB_CLASS_NAME, NULL, WS_CHILD|SS_BITMAP,
			      0, 0, 1, 1, hwnd, NULL, hinstance, NULL);

    /* Initiate a resize in order to properly arrange all controls.
       Move to the last-saved position if needed. */
    if ((vid_resize < 2) && window_remember)
	MoveWindow(hwnd, window_x, window_y, window_w, window_h, TRUE);
    else {
	if (vid_resize >= 2) {
		scrnsz_x = fixed_size_x;
		scrnsz_y = fixed_size_y;
	}
	if (hide_status_bar)
		ResizeWindowByClientArea(hwndMain, scrnsz_x, scrnsz_y);
	else
		ResizeWindowByClientArea(hwndMain, scrnsz_x, scrnsz_y + sbar_height);
    }

    /* Reset all menus to their defaults. */
    ResetAllMenus();
    media_menu_init();

    /* Make the window visible on the screen. */
    ShowWindow(hwnd, nCmdShow);

    /* Warn the user about unsupported configs. */
    if (cpu_override && ui_msgbox_ex(MBX_WARNING | MBX_QUESTION_OK, (void*)IDS_2145, (void*)IDS_2146, (void*)IDS_2147, (void*)IDS_2119, NULL))
    {
	    DestroyWindow(hwnd);
	    return(0);
    }

    GetClipCursor(&oldclip);

    /* Initialize the RawInput (keyboard) module. */
    memset(&ridev, 0x00, sizeof(ridev));
    ridev.usUsagePage = 0x01;
    ridev.usUsage = 0x06;
    ridev.dwFlags = RIDEV_NOHOTKEYS;
    ridev.hwndTarget = NULL;	/* current focus window */
    if (! RegisterRawInputDevices(&ridev, 1, sizeof(ridev))) {
	tdconfig.pszContent = MAKEINTRESOURCE(IDS_2105);
	TaskDialogIndirect(&tdconfig, NULL, NULL, NULL);
	return(4);
    }
    keyboard_getkeymap();

    /* Load the accelerator table */
    haccel = LoadAccelerators(hinstance, ACCEL_NAME);
    if (haccel == NULL) {
	tdconfig.pszContent = MAKEINTRESOURCE(IDS_2104);
	TaskDialogIndirect(&tdconfig, NULL, NULL, NULL);
	return(3);
    }

    /* Initialize the mouse module. */
    win_mouse_init();

    /*
     * Before we can create the Render window, we first have
     * to prepare some other things that it depends on.
     */
    ghMutex = CreateMutex(NULL, FALSE, NULL);

    /* All done, fire up the actual emulated machine. */
    if (! pc_init_modules()) {
	/* Dang, no ROMs found at all! */
	tdconfig.pszMainInstruction = MAKEINTRESOURCE(IDS_2120);
	tdconfig.pszContent = MAKEINTRESOURCE(IDS_2056);
	TaskDialogIndirect(&tdconfig, NULL, NULL, NULL);
	return(6);
    }

    /* Initialize the configured Video API. */
    if (! plat_setvid(vid_api)) {
	tdconfig.pszContent = MAKEINTRESOURCE(IDS_2089);
	TaskDialogIndirect(&tdconfig, NULL, NULL, NULL);
	return(5);
    }

    /* Set up the current window size. */
    if (vid_resize & 2)
	plat_resize(fixed_size_x, fixed_size_y);
    else
	plat_resize(scrnsz_x, scrnsz_y);

    /* Fire up the machine. */
    pc_reset_hard_init();

    /* Set the PAUSE mode depending on the renderer. */
    plat_pause(0);

    /* Initialize the rendering window, or fullscreen. */
    if (start_in_fullscreen)
	plat_setfullscreen(1);

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
    while (! is_quit) {
	bRet = GetMessage(&messages, NULL, 0, 0);
	if ((bRet == 0) || is_quit) break;

	if (bRet == -1) {
		fatal("bRet is -1\n");
	}

	/* On WM_QUIT, tell the CPU thread to stop running. That will then tell us
	   to stop running as well. */
	if (messages.message == WM_QUIT)
		cpu_thread_run = 0;

	if (! TranslateAccelerator(hwnd, haccel, &messages))
	{
		/* Don't process other keypresses. */
		if (messages.message == WM_SYSKEYDOWN ||
			messages.message == WM_SYSKEYUP ||
			messages.message == WM_KEYDOWN ||
			messages.message == WM_KEYUP)
			continue;

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

    UnregisterClass(SDL_SUB_CLASS_NAME, hinstance);
    UnregisterClass(SDL_CLASS_NAME, hinstance);
    UnregisterClass(SUB_CLASS_NAME, hinstance);
    UnregisterClass(CLASS_NAME, hinstance);

    win_mouse_close();

#ifdef USE_DISCORD
    /* Shut down the Discord integration */
    discord_close();
#endif

	if (user32_handle != NULL)
    	dynld_close(user32_handle);

    return(messages.wParam);
}


wchar_t *
ui_window_title(wchar_t *s)
{
    if (! video_fullscreen) {
	if (s != NULL) {
		wcsncpy(wTitle, s, sizeof_w(wTitle) - 1);
	} else
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
	wcsncpy(oldtitle, ui_window_title(NULL), sizeof_w(oldtitle) - 1);
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
    /* First, see if we should resize the UI window. */
    if (!vid_resize) {
	/* scale the screen base on DPI */
	if (dpi_scale) {
		x = MulDiv(x, dpi, 96);
		y = MulDiv(y, dpi, 96);
	}
	if (hide_status_bar)
		ResizeWindowByClientArea(hwndMain, x, y);
	else
		ResizeWindowByClientArea(hwndMain, x, y + sbar_height);
    }
}


void
plat_mouse_capture(int on)
{
    RECT rect;

    if (!kbd_req_capture && (mouse_type == MOUSE_TYPE_NONE))
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
