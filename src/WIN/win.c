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
 * Version:	@(#)win.c	1.0.3	2017/06/12
 *
 * Authors:	Sarah Walker, <http://pcem-emulator.co.uk/>
 *		Miran Grca, <mgrca8@gmail.com>
 *		Fred N. van Kempen, <decwiz@yahoo.com>
 *		Copyright 2008-2017 Sarah Walker.
 *		Copyright 2016-2017 Miran Grca.
 */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../86box.h"
#include "../device.h"
#include "../disc.h"
#include "../fdd.h"
#include "../hdd.h"
#include "../ibm.h"
#include "../cpu/cpu.h"
#include "../mem.h"
#include "../rom.h"
#include "../nvr.h"
#include "../config.h"
#include "../model.h"
#include "../ide.h"
#include "../cdrom.h"
#include "../cdrom_null.h"
#include "../cdrom_ioctl.h"
#include "../cdrom_image.h"
#include "../scsi.h"
#include "../scsi_disk.h"
#include "../video/video.h"
#include "../video/vid_ega.h"
#include "../mouse.h"
#include "../sound/sound.h"
#include "../sound/snd_dbopl.h"
#include "plat_keyboard.h"
#include "plat_iodev.h"
#include "plat_mouse.h"
#include "plat_midi.h"
#include "plat_thread.h"
#include "plat_ticks.h"
#include "plat_ui.h"

#include "win.h"
#include "win_cgapal.h"
#include "win_ddraw.h"
#include "win_d3d.h"
#include "win_language.h"

#include <windowsx.h>
#include <commctrl.h>
#include <commdlg.h>
#include <process.h>


#ifndef MAPVK_VK_TO_VSC
#define MAPVK_VK_TO_VSC 0
#endif

/*  Declare Windows procedure  */
LRESULT CALLBACK WindowProcedure(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam);
LRESULT CALLBACK subWindowProcedure(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam);

LRESULT CALLBACK StatusBarProcedure(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam);

#define TIMER_1SEC 1

extern int	updatestatus;


typedef struct win_event_t
{
        HANDLE handle;
} win_event_t;

LONG_PTR	OriginalStatusBarProcedure;
HWND		ghwnd;
HINSTANCE	hinstance;
HMENU		menu;
int		pause = 0;
int		scale = 0;
HWND		hwndRender, hwndStatus;
uint64_t	timer_freq;
int		winsizex=640, winsizey=480;
int		efwinsizey=480;
int		gfx_present[GFX_MAX];
HANDLE		ghMutex;
HANDLE		mainthreadh;
int		infocus=1;
int		drawits=0;
int		romspresent[ROM_MAX];
int		quited=0;
RECT		oldclip;
int		mousecapture=0;
int		recv_key[272];
HMENU		*sb_menu_handles;
uint64_t	main_time;


static struct
{
        int (*init)(HWND h);
        void (*close)();
        void (*resize)(int x, int y);
} vid_apis[2][2] =
{	{	{	ddraw_init, ddraw_close, NULL		},
		{	d3d_init, d3d_close, d3d_resize		}	},
	{	{	ddraw_fs_init, ddraw_fs_close, NULL	},
		{	d3d_fs_init, d3d_fs_close, NULL		}	}	};

static int	save_window_pos = 0;

static RAWINPUTDEVICE	device;

static int	win_doresize = 0;

static int	leave_fullscreen_flag = 0;

static int	unscaled_size_x = 0;
static int	unscaled_size_y = 0;

static uint64_t	start_time;
static uint64_t	end_time;

HMENU		smenu;

static uint8_t	host_cdrom_drive_available[26];

static uint8_t	host_cdrom_drive_available_num = 0;

static wchar_t	**argv;
static int	argc;
static wchar_t	*argbuf;

static HANDLE	hinstAcc;

static HICON	hIcon[512];

static int	*iStatusWidths;
static int	*sb_icon_flags;
static int	*sb_part_meanings;
static int	*sb_part_icons;
static WCHAR	**sbTips;

static int	sb_parts = 0;
static int	sb_ready = 0;


void updatewindowsize(int x, int y)
{
	int owsx = winsizex;
	int owsy = winsizey;

	int temp_overscan_x = overscan_x;
	int temp_overscan_y = overscan_y;

        if (vid_resize) return;

	if (x < 160)  x = 160;
	if (y < 100)  y = 100;

	if (x > 2048)  x = 2048;
	if (y > 2048)  y = 2048;

	if (suppress_overscan)
	{
		temp_overscan_x = temp_overscan_y = 0;
	}

        unscaled_size_x=x; efwinsizey=y;

	if (force_43)
	{
		/* Account for possible overscan. */
		if (temp_overscan_y == 16)
		{
			/* CGA */
			unscaled_size_y = ((int) (((double) (x - temp_overscan_x) / 4.0) * 3.0)) + temp_overscan_y;
		}
		else if (temp_overscan_y < 16)
		{
			/* MDA/Hercules */
			unscaled_size_y = ((int) (((double) (x) / 4.0) * 3.0));
		}
		else
		{
			if (enable_overscan)
			{
				/* EGA/(S)VGA with overscan */
				unscaled_size_y = ((int) (((double) (x - temp_overscan_x) / 4.0) * 3.0)) + temp_overscan_y;
			}
			else
			{
				/* EGA/(S)VGA without overscan */
				unscaled_size_y = ((int) (((double) (x) / 4.0) * 3.0));
			}
		}
	}
	else
	{
		unscaled_size_y = efwinsizey;
	}

	switch(scale)
	{
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
	{
	        win_doresize = 1;
	}
	else
	{
		win_doresize = 0;
	}
}

void uws_natural(void)
{
	updatewindowsize(unscaled_size_x, efwinsizey);
}

void releasemouse(void)
{
        if (mousecapture) 
        {
                ClipCursor(&oldclip);
                ShowCursor(TRUE);
                mousecapture = 0;
        }
}

void startblit(void)
{
        WaitForSingleObject(ghMutex, INFINITE);
}

void endblit(void)
{
        ReleaseMutex(ghMutex);
}

void leave_fullscreen(void)
{
        leave_fullscreen_flag = 1;
}

uint32_t get_ticks(void)
{
	return GetTickCount();
}

void delay_ms(uint32_t count)
{
	Sleep(count);
}

void mainthread(LPVOID param)
{
        int frames = 0;
        DWORD old_time, new_time;

	RECT r;
	int sb_borders[3];

        drawits=0;
        old_time = GetTickCount();
        while (!quited)
        {
		if (updatestatus)
		{
			updatestatus = 0;
			if (status_is_open)
			{
				SendMessage(status_hwnd, WM_USER, 0, 0);
			}
		}
                new_time = GetTickCount();
                drawits += new_time - old_time;
                old_time = new_time;
                if (drawits > 0 && !pause)
                {
			start_time = timer_read();
                        drawits-=10;        if (drawits>50) drawits=0;
                        runpc();
                        frames++;
                        if (frames >= 200 && nvr_dosave)
                        {
                                frames = 0;
                                nvr_dosave = 0;
                                savenvr();
                        }
                        end_time = timer_read();
                        main_time += end_time - start_time;
                }
                else
                        Sleep(1);

                if (!video_fullscreen && win_doresize && (winsizex > 0) && (winsizey > 0))
                {
                        video_wait_for_blit();
			SendMessage(hwndStatus, SB_GETBORDERS, 0, (LPARAM) sb_borders);
                        GetWindowRect(ghwnd, &r);
                        MoveWindow(hwndRender, 0, 0, winsizex, winsizey, TRUE);
                        GetWindowRect(hwndRender, &r);
                        MoveWindow(hwndStatus, 0, r.bottom + GetSystemMetrics(SM_CYEDGE), winsizex, 17, TRUE);
                        GetWindowRect(ghwnd, &r);

                        MoveWindow(ghwnd, r.left, r.top,
                                winsizex + (GetSystemMetrics(vid_resize ? SM_CXSIZEFRAME : SM_CXFIXEDFRAME) * 2),
                                winsizey + (GetSystemMetrics(SM_CYEDGE) * 2) + (GetSystemMetrics(vid_resize ? SM_CYSIZEFRAME : SM_CYFIXEDFRAME) * 2) + GetSystemMetrics(SM_CYMENUSIZE) + GetSystemMetrics(SM_CYCAPTION) + 17 + sb_borders[1] + 1,
                                TRUE);

			if (mousecapture)
			{
				GetWindowRect(hwndRender, &r);
				ClipCursor(&r);
			}

                        win_doresize = 0;
                }

                if (leave_fullscreen_flag)
                {
                        leave_fullscreen_flag = 0;
                        SendMessage(ghwnd, WM_LEAVEFULLSCREEN, 0, 0);
                }
                if (video_fullscreen && infocus)
                {
                        SetCursorPos(9999, 9999);
                }
        }
}

void *thread_create(void (*thread_rout)(void *param), void *param)
{
        return (void *)_beginthread(thread_rout, 0, param);
}

void thread_kill(void *handle)
{
        TerminateThread(handle, 0);
}

void thread_sleep(int t)
{
        Sleep(t);
}

event_t *thread_create_event(void)
{
        win_event_t *event = malloc(sizeof(win_event_t));
        
        event->handle = CreateEvent(NULL, FALSE, FALSE, NULL);
        
        return (event_t *)event;
}

void thread_set_event(event_t *_event)
{
        win_event_t *event = (win_event_t *)_event;
        
        SetEvent(event->handle);
}

void thread_reset_event(event_t *_event)
{
        win_event_t *event = (win_event_t *)_event;
        
        ResetEvent(event->handle);
}

int thread_wait_event(event_t *_event, int timeout)
{
        win_event_t *event = (win_event_t *)_event;
        
        if (timeout == -1)
                timeout = INFINITE;
        
        if (WaitForSingleObject(event->handle, timeout))
                return 1;
        return 0;
}

void thread_destroy_event(event_t *_event)
{
        win_event_t *event = (win_event_t *)_event;
        
        CloseHandle(event->handle);
        
        free(event);
}

static void init_cdrom_host_drives(void)
{
	int i = 0;
        WCHAR s[64];

	host_cdrom_drive_available_num = 0;

	for (i='A'; i<='Z'; i++)
	{
		_swprintf(s, L"%c:\\", i);

		if (GetDriveType(s)==DRIVE_CDROM)
		{
			host_cdrom_drive_available[i - 'A'] = 1;

			host_cdrom_drive_available_num++;
		}
		else
		{
			host_cdrom_drive_available[i - 'A'] = 0;
		}
        }
}


HMENU create_popup_menu(int part)
{
	HMENU newHandle;
	newHandle = CreatePopupMenu();
	AppendMenu(smenu, MF_POPUP, (UINT_PTR) newHandle, 0);
	return newHandle;
}


void create_floppy_submenu(HMENU m, int id)
{
	AppendMenu(m, MF_STRING, IDM_FLOPPY_IMAGE_NEW | id, win_language_get_string_from_id(2211));
	AppendMenu(m, MF_SEPARATOR, 0, 0);
	AppendMenu(m, MF_STRING, IDM_FLOPPY_IMAGE_EXISTING | id, win_language_get_string_from_id(2212));
	AppendMenu(m, MF_STRING, IDM_FLOPPY_IMAGE_EXISTING_WP | id, win_language_get_string_from_id(2213));
	AppendMenu(m, MF_SEPARATOR, 0, 0);
	AppendMenu(m, MF_STRING, IDM_FLOPPY_EJECT | id, win_language_get_string_from_id(2214));
}

void create_cdrom_submenu(HMENU m, int id)
{
	int i = 0;
        WCHAR s[64];

	AppendMenu(m, MF_STRING, IDM_CDROM_MUTE | id, win_language_get_string_from_id(2215));
	AppendMenu(m, MF_SEPARATOR, 0, 0);
	AppendMenu(m, MF_STRING, IDM_CDROM_EMPTY | id, win_language_get_string_from_id(2216));
	AppendMenu(m, MF_STRING, IDM_CDROM_RELOAD | id, win_language_get_string_from_id(2217));
	AppendMenu(m, MF_SEPARATOR, 0, 0);
	AppendMenu(m, MF_STRING, IDM_CDROM_IMAGE | id, win_language_get_string_from_id(2218));

	if (host_cdrom_drive_available_num == 0)
	{
		if ((cdrom_drives[id].host_drive >= 'A') && (cdrom_drives[id].host_drive <= 'Z'))
		{
			cdrom_drives[id].host_drive = 0;
		}

		goto check_menu_items;
	}
	else
	{
		if ((cdrom_drives[id].host_drive >= 'A') && (cdrom_drives[id].host_drive <= 'Z'))
		{
			if (!host_cdrom_drive_available[cdrom_drives[id].host_drive - 'A'])
			{
				cdrom_drives[id].host_drive = 0;
			}
		}
	}

	AppendMenu(m, MF_SEPARATOR, 0, 0);

	for (i = 0; i < 26; i++)
	{
		_swprintf(s, L"Host CD/DVD Drive (%c:)", i + 0x41);
		if (host_cdrom_drive_available[i])
		{
			AppendMenu(m, MF_STRING, IDM_CDROM_HOST_DRIVE | (i << 3) | id, s);
		}
	}

check_menu_items:
	if (!cdrom_drives[id].sound_on)
	{
		CheckMenuItem(m, IDM_CDROM_MUTE | id, MF_CHECKED);
	}

	if (cdrom_drives[id].host_drive == 200)
	{
		CheckMenuItem(m, IDM_CDROM_IMAGE | id, MF_CHECKED);
	}
	else if ((cdrom_drives[id].host_drive >= 'A') && (cdrom_drives[id].host_drive <= 'Z'))
	{
		CheckMenuItem(m, IDM_CDROM_HOST_DRIVE | id | ((cdrom_drives[id].host_drive - 'A') << 3), MF_CHECKED);
	}
	else
	{
		cdrom_drives[id].host_drive = 0;
		CheckMenuItem(m, IDM_CDROM_EMPTY | id, MF_CHECKED);
	}
}

void create_removable_disk_submenu(HMENU m, int id)
{
	AppendMenu(m, MF_STRING, IDM_RDISK_EJECT | id, win_language_get_string_from_id(2216));
	AppendMenu(m, MF_STRING, IDM_RDISK_RELOAD | id, win_language_get_string_from_id(2217));
	AppendMenu(m, MF_SEPARATOR, 0, 0);
	AppendMenu(m, MF_STRING, IDM_RDISK_SEND_CHANGE | id, win_language_get_string_from_id(2201));
	AppendMenu(m, MF_SEPARATOR, 0, 0);
	AppendMenu(m, MF_STRING, IDM_RDISK_IMAGE | id, win_language_get_string_from_id(2218));
	AppendMenu(m, MF_STRING, IDM_RDISK_IMAGE_WP | id, win_language_get_string_from_id(2220));
}

void get_executable_name(wchar_t *s, int size)
{
        GetModuleFileName(hinstance, s, size);
}

void set_window_title(wchar_t *s)
{
        if (video_fullscreen)
                return;
        SetWindowText(ghwnd, s);
}

uint64_t timer_read(void)
{
        LARGE_INTEGER qpc_time;
        QueryPerformanceCounter(&qpc_time);
        return qpc_time.QuadPart;
}

static void process_command_line(void)
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
        if (!argv)
        {
                free(argbuf);
                return;
        }

        i = 0;

        /* parse commandline into argc/argv format */
        while (argbuf[i])
        {
                while (argbuf[i] == L' ')
                        i++;

                if (argbuf[i])
                {
                        if ((argbuf[i] == L'\'') || (argbuf[i] == L'"'))
                        {
                                q = argbuf[i++];
                                if (!argbuf[i])
                                        break;
                        }
                        else
                                q = 0;

                        argv[argc++] = &argbuf[i];

                        if (argc >= argc_max)
                        {
                                argc_max += 64;
                                argv = realloc(argv, sizeof(wchar_t *) * argc_max);
                                if (!argv)
                                {
                                        free(argbuf);
                                        return;
                                }
                        }

                        while ((argbuf[i]) && ((q) ? (argbuf[i] != q) : (argbuf[i] != L' ')))
                                i++;

                        if (argbuf[i])
                        {
                                argbuf[i] = 0;
                                i++;
                        }
                }
        }

        argv[argc] = NULL;
}

int find_in_array(int *array, int val, int len, int menu_base)
{
	int i = 0;
	int temp = 0;
	for (i = 0; i < len; i++)
	{
	        CheckMenuItem(menu, menu_base + array[i], MF_UNCHECKED);
		if (array[i] == val)
		{
			temp = 1;
		}
	}
	return temp;
}

HICON LoadIconEx(PCTSTR pszIconName)
{
	return (HICON) LoadImage(hinstance, pszIconName, IMAGE_ICON, 16, 16, LR_SHARED);
}

HICON LoadIconBig(PCTSTR pszIconName)
{
	return (HICON) LoadImage(hinstance, pszIconName, IMAGE_ICON, 64, 64, 0);
}

int fdd_type_to_icon(int type)
{
	switch(type)
	{
		default:
		case 0:
			return 512;
		case 1:
			return 128;
		case 2:
			return 130;
		case 3:
			return 132;
		case 4:
		case 5:
		case 6:
			return 134;
		case 7:
			return 144;
		case 8:
			return 146;
		case 9:
		case 10:
		case 11:
		case 12:
			return 150;
		case 13:
			return 152;
	}
}

int count_hard_disks(int bus)
{
	int i = 0;

	int c = 0;

	for (i = 0; i < HDC_NUM; i++)
	{
		if (hdc[i].bus == bus)
		{
			c++;
		}
	}

	return c;
}

int find_status_bar_part(int tag)
{
	int i = 0;
	int found = -1;

	if (!sb_ready || (sb_parts == 0) || (sb_part_meanings == NULL))
	{
		return -1;
	}

	for (i = 0; i < sb_parts; i++)
	{
		if (sb_part_meanings[i] == tag)
		{
			found = i;
			break;
		}
	}

	return found;
}

/* This is for the disk activity indicator. */
void update_status_bar_icon(int tag, int active)
{
	int found = -1;
	int temp_flags = 0;

	if (((tag & 0xf0) >= SB_TEXT) || !sb_ready || (sb_parts == 0) || (sb_icon_flags == NULL) || (sb_part_icons == NULL))
	{
		return;
	}

	temp_flags |= active;

	found = find_status_bar_part(tag);

	if (found != -1)
	{
		if (temp_flags != (sb_icon_flags[found] & 1))
		{
			sb_icon_flags[found] &= ~1;
			sb_icon_flags[found] |= active;

			sb_part_icons[found] &= ~257;
			sb_part_icons[found] |= sb_icon_flags[found];

			SendMessage(hwndStatus, SB_SETICON, found, (LPARAM) hIcon[sb_part_icons[found]]);
		}
	}
}

/* This is for the drive state indicator. */
void update_status_bar_icon_state(int tag, int state)
{
	int found = -1;

	if (((tag & 0xf0) >= SB_HDD) || !sb_ready || (sb_parts == 0) || (sb_icon_flags == NULL) || (sb_part_icons == NULL))
	{
		return;
	}

	found = find_status_bar_part(tag);

	if (found != -1)
	{
		sb_icon_flags[found] &= ~256;
		sb_icon_flags[found] |= state ? 256 : 0;

		sb_part_icons[found] &= ~257;
		sb_part_icons[found] |= sb_icon_flags[found];

		SendMessage(hwndStatus, SB_SETICON, found, (LPARAM) hIcon[sb_part_icons[found]]);
	}
}

void create_floppy_tip(int part)
{
	WCHAR wtext[512];
	WCHAR tempTip[512];

	int drive = sb_part_meanings[part] & 0xf;

	mbstowcs(wtext, fdd_getname(fdd_get_type(drive)), strlen(fdd_getname(fdd_get_type(drive))) + 1);
	if (wcslen(discfns[drive]) == 0)
	{
		_swprintf(tempTip,  win_language_get_string_from_id(2179), drive + 1, wtext, win_language_get_string_from_id(2185));
	}
	else
	{
		_swprintf(tempTip,  win_language_get_string_from_id(2179), drive + 1, wtext, discfns[drive]);
	}

	if (sbTips[part] != NULL)
	{
		free(sbTips[part]);
	}
	sbTips[part] = (WCHAR *) malloc((wcslen(tempTip) << 1) + 2);
	wcscpy(sbTips[part], tempTip);
}

void create_cdrom_tip(int part)
{
	WCHAR wtext[512];
	WCHAR tempTip[512];

	int drive = sb_part_meanings[part] & 0xf;

	if (cdrom_drives[drive].host_drive == 200)
	{
		if (wcslen(cdrom_image[drive].image_path) == 0)
		{
			_swprintf(tempTip, win_language_get_string_from_id(2180), drive + 1, win_language_get_string_from_id(2185));
		}
		else
		{
			_swprintf(tempTip, win_language_get_string_from_id(2180), drive + 1, cdrom_image[drive].image_path);
		}
	}
	else if (cdrom_drives[drive].host_drive < 0x41)
	{
		_swprintf(tempTip, win_language_get_string_from_id(2180), drive + 1, win_language_get_string_from_id(2185));
	}
	else
	{
		_swprintf(wtext, win_language_get_string_from_id(2186), cdrom_drives[drive].host_drive & ~0x20);
		_swprintf(tempTip, win_language_get_string_from_id(2180), drive + 1, wtext);
	}

	if (sbTips[part] != NULL)
	{
		free(sbTips[part]);
	}
	sbTips[part] = (WCHAR *) malloc((wcslen(tempTip) << 1) + 2);
	wcscpy(sbTips[part], tempTip);
}

void create_removable_hd_tip(int part)
{
	WCHAR tempTip[512];

	int drive = sb_part_meanings[part] & 0x1f;

	if (wcslen(hdc[drive].fn) == 0)
	{
		_swprintf(tempTip,  win_language_get_string_from_id(2198), drive, win_language_get_string_from_id(2185));
	}
	else
	{
		_swprintf(tempTip,  win_language_get_string_from_id(2198), drive, hdc[drive].fn);
	}

	if (sbTips[part] != NULL)
	{
		free(sbTips[part]);
	}
	sbTips[part] = (WCHAR *) malloc((wcslen(tempTip) << 1) + 2);
	wcscpy(sbTips[part], tempTip);
}

void create_hd_tip(int part)
{
	WCHAR *szText;
	int id = 2181;

	int bus = sb_part_meanings[part] & 0xf;

	switch(bus)
	{
		case HDD_BUS_MFM:
			id = 2181;
			break;
		case HDD_BUS_RLL:
			id = 2207;
			break;
		case HDD_BUS_XTIDE:
			id = 2208;
			break;
		case HDD_BUS_IDE_PIO_ONLY:
			id = 2182;
			break;
		case HDD_BUS_IDE_PIO_AND_DMA:
			id = 2183;
			break;
		case HDD_BUS_SCSI:
			id = 2184;
			break;
	}

	szText = (WCHAR *) win_language_get_string_from_id(id);

	if (sbTips[part] != NULL)
	{
		free(sbTips[part]);
	}
	sbTips[part] = (WCHAR *) malloc((wcslen(szText) << 1) + 2);
	wcscpy(sbTips[part], szText);
}

void update_tip(int meaning)
{
	int i = 0;
	int part = -1;

	if (!sb_ready || (sb_parts == 0) || (sb_part_meanings == NULL))
	{
		return;
	}

	for (i = 0; i < sb_parts; i++)
	{
		if (sb_part_meanings[i] == meaning)
		{
			part = i;
		}
	}

	if (part != -1)
	{
		switch(meaning & 0xf0)
		{
			case SB_FLOPPY:
				create_floppy_tip(part);
				break;
			case SB_CDROM:
				create_cdrom_tip(part);
				break;
			case SB_RDISK:
				create_removable_hd_tip(part);
				break;
			case SB_HDD:
				create_hd_tip(part);
				break;
			default:
				break;
		}

		SendMessage(hwndStatus, SB_SETTIPTEXT, part, (LPARAM) sbTips[part]);
	}
}

void status_settextw(wchar_t *wstr)
{
	int i = 0;
	int part = -1;

	if (!sb_ready || (sb_parts == 0) || (sb_part_meanings == NULL))
	{
		return;
	}

	for (i = 0; i < sb_parts; i++)
	{
		if (sb_part_meanings[i] == SB_TEXT)
		{
			part = i;
		}
	}

	if (part != -1)
	{
		SendMessage(hwndStatus, SB_SETTEXT, part | SBT_NOBORDERS, (LPARAM) wstr);		
	}
}

static wchar_t cwstr[512];

void status_settext(char *str)
{
	memset(cwstr, 0, 1024);
	mbstowcs(cwstr, str, strlen(str) + 1);
	status_settextw(cwstr);
}

void destroy_menu_handles(void)
{
	int i = 0;

	if (sb_parts == 0)
	{
		return;
	}

	for (i = 0; i < sb_parts; i++)
	{
		DestroyMenu(sb_menu_handles[i]);
	}

	free(sb_menu_handles);
}

void destroy_tips(void)
{
	int i = 0;

	if (sb_parts == 0)
	{
		return;
	}

	for (i = 0; i < sb_parts; i++)
	{
		free(sbTips[i]);
	}

	free(sbTips);
}

void update_status_bar_panes(HWND hwnds)
{
	int i, j, id;
	int edge = 0;

	int c_mfm = 0;
	int c_rll = 0;
	int c_xtide = 0;
	int c_ide_pio = 0;
	int c_ide_dma = 0;
	int c_scsi = 0;

	sb_ready = 0;

	c_mfm = count_hard_disks(HDD_BUS_MFM);
	c_rll = count_hard_disks(HDD_BUS_RLL);
	c_xtide = count_hard_disks(HDD_BUS_XTIDE);
	c_ide_pio = count_hard_disks(HDD_BUS_IDE_PIO_ONLY);
	c_ide_dma = count_hard_disks(HDD_BUS_IDE_PIO_AND_DMA);
	c_scsi = count_hard_disks(HDD_BUS_SCSI);

	if (sb_parts > 0)
	{
		for (i = 0; i < sb_parts; i++)
		{
			SendMessage(hwnds, SB_SETICON, i, (LPARAM) NULL);
		}

		sb_parts = 0;

		free(iStatusWidths);
		free(sb_part_meanings);
		free(sb_part_icons);
		free(sb_icon_flags);
		destroy_menu_handles();
		destroy_tips();
	}

	for (i = 0; i < FDD_NUM; i++)
	{
		if (fdd_get_type(i) != 0)
		{
			/* pclog("update_status_bar_panes(): Found floppy drive %c:, type %i\n", 65 + i, fdd_get_type(i)); */
			sb_parts++;
		}
	}
	for (i = 0; i < CDROM_NUM; i++)
	{
		if (cdrom_drives[i].bus_type != 0)
		{
			sb_parts++;
		}
	}
	for (i = 0; i < HDC_NUM; i++)
	{
		if (hdc[i].bus == HDD_BUS_SCSI_REMOVABLE)
		{
			sb_parts++;
		}
	}
	if (c_mfm && !(models[model].flags & MODEL_HAS_IDE) && !!memcmp(hdd_controller_name, "none", 4) && !!memcmp(hdd_controller_name, "xtide", 5) && !!memcmp(hdd_controller_name, "esdi", 4))
	{
		sb_parts++;
	}
	if (c_rll && !memcmp(hdd_controller_name, "esdi", 4))
	{
		sb_parts++;
	}
	if (c_xtide && !memcmp(hdd_controller_name, "xtide", 5))
	{
		sb_parts++;
	}
	if (c_ide_pio && (models[model].flags & MODEL_HAS_IDE))
	{
		sb_parts++;
	}
	if (c_ide_dma && (models[model].flags & MODEL_HAS_IDE))
	{
		sb_parts++;
	}
	if (c_scsi && (scsi_card_current != 0))
	{
		sb_parts++;
	}
	sb_parts++;

	iStatusWidths = (int *) malloc(sb_parts * sizeof(int));
	sb_part_meanings = (int *) malloc(sb_parts * sizeof(int));
	sb_part_icons = (int *) malloc(sb_parts * sizeof(int));
	sb_icon_flags = (int *) malloc(sb_parts * sizeof(int));
	sb_menu_handles = (HMENU *) malloc(sb_parts * sizeof(HMENU));
	sbTips = (WCHAR **) malloc(sb_parts * sizeof(WCHAR *));

	memset(iStatusWidths, 0, sb_parts * sizeof(int));
	memset(sb_part_meanings, 0, sb_parts * sizeof(int));
	memset(sb_part_icons, 0, sb_parts * sizeof(int));
	memset(sb_icon_flags, 0, sb_parts * sizeof(int));
	memset(sb_menu_handles, 0, sb_parts * sizeof(HMENU));
	memset(sbTips, 0, sb_parts * sizeof(WCHAR *));

	sb_parts = 0;

	for (i = 0; i < FDD_NUM; i++)
	{
		if (fdd_get_type(i) != 0)
		{
			/* pclog("update_status_bar_panes(): Found floppy drive %c:, type %i\n", 65 + i, fdd_get_type(i)); */
			edge += SB_ICON_WIDTH;
			iStatusWidths[sb_parts] = edge;
			sb_part_meanings[sb_parts] = SB_FLOPPY | i;
			sb_parts++;
		}
	}
	for (i = 0; i < CDROM_NUM; i++)
	{
		if (cdrom_drives[i].bus_type != 0)
		{
			edge += SB_ICON_WIDTH;
			iStatusWidths[sb_parts] = edge;
			sb_part_meanings[sb_parts] = SB_CDROM | i;
			sb_parts++;
		}
	}
	for (i = 0; i < HDC_NUM; i++)
	{
		if (hdc[i].bus == HDD_BUS_SCSI_REMOVABLE)
		{
			edge += SB_ICON_WIDTH;
			iStatusWidths[sb_parts] = edge;
			sb_part_meanings[sb_parts] = SB_RDISK | i;
			sb_parts++;
		}
	}
	if (c_mfm && !(models[model].flags & MODEL_HAS_IDE) && !!memcmp(hdd_controller_name, "none", 4) && !!memcmp(hdd_controller_name, "xtide", 5) && !!memcmp(hdd_controller_name, "esdi", 4))
	{
		edge += SB_ICON_WIDTH;
		iStatusWidths[sb_parts] = edge;
		sb_part_meanings[sb_parts] = SB_HDD | HDD_BUS_MFM;
		sb_parts++;
	}
	if (c_rll && !memcmp(hdd_controller_name, "esdi", 4))
	{
		edge += SB_ICON_WIDTH;
		iStatusWidths[sb_parts] = edge;
		sb_part_meanings[sb_parts] = SB_HDD | HDD_BUS_RLL;
		sb_parts++;
	}
	if (c_xtide && !memcmp(hdd_controller_name, "xtide", 5))
	{
		edge += SB_ICON_WIDTH;
		iStatusWidths[sb_parts] = edge;
		sb_part_meanings[sb_parts] = SB_HDD | HDD_BUS_XTIDE;
		sb_parts++;
	}
	if (c_ide_pio && (models[model].flags & MODEL_HAS_IDE))
	{
		edge += SB_ICON_WIDTH;
		iStatusWidths[sb_parts] = edge;
		sb_part_meanings[sb_parts] = SB_HDD | HDD_BUS_IDE_PIO_ONLY;
		sb_parts++;
	}
	if (c_ide_dma && (models[model].flags & MODEL_HAS_IDE))
	{
		edge += SB_ICON_WIDTH;
		iStatusWidths[sb_parts] = edge;
		sb_part_meanings[sb_parts] = SB_HDD | HDD_BUS_IDE_PIO_AND_DMA;
		sb_parts++;
	}
	if (c_scsi)
	{
		edge += SB_ICON_WIDTH;
		iStatusWidths[sb_parts] = edge;
		sb_part_meanings[sb_parts] = SB_HDD | HDD_BUS_SCSI;
		sb_parts++;
	}
	if (sb_parts)
	{
		iStatusWidths[sb_parts - 1] += (24 - SB_ICON_WIDTH);
	}
	iStatusWidths[sb_parts] = -1;
	sb_part_meanings[sb_parts] = SB_TEXT;
	sb_parts++;

	SendMessage(hwnds, SB_SETPARTS, (WPARAM) sb_parts, (LPARAM) iStatusWidths);

	for (i = 0; i < sb_parts; i++)
	{
		switch (sb_part_meanings[i] & 0xf0)
		{
			case SB_FLOPPY:
				/* Floppy */
				sb_icon_flags[i] = (wcslen(discfns[sb_part_meanings[i] & 0xf]) == 0) ? 256 : 0;
				sb_part_icons[i] = fdd_type_to_icon(fdd_get_type(sb_part_meanings[i] & 0xf)) | sb_icon_flags[i];
				sb_menu_handles[i] = create_popup_menu(i);
				create_floppy_submenu(sb_menu_handles[i], sb_part_meanings[i] & 0xf);
				EnableMenuItem(sb_menu_handles[i], IDM_FLOPPY_EJECT | (sb_part_meanings[i] & 0xf), MF_BYCOMMAND | ((sb_icon_flags[i] & 256) ? MF_GRAYED : MF_ENABLED));
				create_floppy_tip(i);
				break;
			case SB_CDROM:
				/* CD-ROM */
				id = sb_part_meanings[i] & 0xf;
				if (cdrom_drives[id].host_drive == 200)
				{
					sb_icon_flags[i] = (wcslen(cdrom_image[id].image_path) == 0) ? 256 : 0;
				}
				else if ((cdrom_drives[id].host_drive >= 'A') && (cdrom_drives[id].host_drive <= 'Z'))
				{
					sb_icon_flags[i] = 0;
				}
				else
				{
					sb_icon_flags[i] = 256;
				}
				if (cdrom_drives[id].bus_type == CDROM_BUS_SCSI)
				{
					j = 164;
				}
				else if (cdrom_drives[id].bus_type == CDROM_BUS_ATAPI_PIO_AND_DMA)
				{
					j = 162;
				}
				else
				{
					j = 160;
				}
				sb_part_icons[i] = j | sb_icon_flags[i];
				sb_menu_handles[i] = create_popup_menu(i);
				create_cdrom_submenu(sb_menu_handles[i], sb_part_meanings[i] & 0xf);
				EnableMenuItem(sb_menu_handles[i], IDM_CDROM_RELOAD | (sb_part_meanings[i] & 0xf), MF_BYCOMMAND | MF_GRAYED);
				create_cdrom_tip(i);
				break;
			case SB_RDISK:
				/* Removable hard disk */
				sb_icon_flags[i] = (wcslen(hdc[sb_part_meanings[i] & 0x1f].fn) == 0) ? 256 : 0;
				sb_part_icons[i] = 176 + sb_icon_flags[i];
				sb_menu_handles[i] = create_popup_menu(i);
				create_removable_disk_submenu(sb_menu_handles[i], sb_part_meanings[i] & 0x1f);
				EnableMenuItem(sb_menu_handles[i], IDM_RDISK_EJECT | (sb_part_meanings[i] & 0x1f), MF_BYCOMMAND | ((sb_icon_flags[i] & 256) ? MF_GRAYED : MF_ENABLED));
				EnableMenuItem(sb_menu_handles[i], IDM_RDISK_RELOAD | (sb_part_meanings[i] & 0x1f), MF_BYCOMMAND | MF_GRAYED);
				EnableMenuItem(sb_menu_handles[i], IDM_RDISK_SEND_CHANGE | (sb_part_meanings[i] & 0x1f), MF_BYCOMMAND | ((sb_icon_flags[i] & 256) ? MF_GRAYED : MF_ENABLED));
				create_removable_hd_tip(i);
				break;
			case SB_HDD:
				/* Hard disk */
				sb_part_icons[i] = 192 + (((sb_part_meanings[i] & 0xf) - 1) << 1);
				create_hd_tip(i);
				break;
			case SB_TEXT:
				/* Status text */
				SendMessage(hwnds, SB_SETTEXT, i | SBT_NOBORDERS, (LPARAM) L"");
				sb_part_icons[i] = -1;
				break;
		}
		if (sb_part_icons[i] != -1)
		{
			SendMessage(hwnds, SB_SETTEXT, i | SBT_NOBORDERS, (LPARAM) "");
			SendMessage(hwnds, SB_SETICON, i, (LPARAM) hIcon[sb_part_icons[i]]);
			SendMessage(hwnds, SB_SETTIPTEXT, i, (LPARAM) sbTips[i]);
			/* pclog("Status bar part found: %02X (%i)\n", sb_part_meanings[i], sb_part_icons[i]); */
		}
		else
		{
			SendMessage(hwnds, SB_SETICON, i, (LPARAM) NULL);
		}
	}

	sb_ready = 1;
}

HWND EmulatorStatusBar(HWND hwndParent, int idStatus, HINSTANCE hinst)
{
	HWND hwndStatus;
	int i;
	RECT rectDialog;
	int dw, dh;

	for (i = 128; i < 136; i++)
	{
		hIcon[i] = LoadIconEx((PCTSTR) i);
	}

	for (i = 144; i < 148; i++)
	{
		hIcon[i] = LoadIconEx((PCTSTR) i);
	}

	for (i = 150; i < 154; i++)
	{
		hIcon[i] = LoadIconEx((PCTSTR) i);
	}

	for (i = 160; i < 166; i++)
	{
		hIcon[i] = LoadIconEx((PCTSTR) i);
	}

	for (i = 176; i < 178; i++)
	{
		hIcon[i] = LoadIconEx((PCTSTR) i);
	}

	for (i = 192; i < 204; i++)
	{
		hIcon[i] = LoadIconEx((PCTSTR) i);
	}

	for (i = 384; i < 392; i++)
	{
		hIcon[i] = LoadIconEx((PCTSTR) i);
	}

	for (i = 400; i < 404; i++)
	{
		hIcon[i] = LoadIconEx((PCTSTR) i);
	}

	for (i = 406; i < 410; i++)
	{
		hIcon[i] = LoadIconEx((PCTSTR) i);
	}

	for (i = 416; i < 422; i++)
	{
		hIcon[i] = LoadIconEx((PCTSTR) i);
	}

	for (i = 432; i < 434; i++)
	{
		hIcon[i] = LoadIconEx((PCTSTR) i);
	}

	GetWindowRect(hwndParent, &rectDialog);
	dw = rectDialog.right - rectDialog.left;
	dh = rectDialog.bottom - rectDialog.top;

	InitCommonControls();

	hwndStatus = CreateWindowEx(0, STATUSCLASSNAME, (PCTSTR) NULL, SBARS_SIZEGRIP | WS_CHILD | WS_VISIBLE | SBT_TOOLTIPS, 0, dh - 17, dw, 17, hwndParent,
				    (HMENU) idStatus, hinst, NULL);

	GetWindowRect(hwndStatus, &rectDialog);

	SetWindowPos(hwndStatus, HWND_TOPMOST, rectDialog.left, rectDialog.top, rectDialog.right - rectDialog.left, rectDialog.bottom - rectDialog.top, SWP_SHOWWINDOW);

	SendMessage(hwndStatus, SB_SETMINHEIGHT, (WPARAM) 17, (LPARAM) 0);

	sb_parts = 0;

	update_status_bar_panes(hwndStatus);

	return hwndStatus;
}

void win_menu_update(void)
{
#if 0
        menu = LoadMenu(hThisInstance, TEXT("MainMenu"));

	smenu = LoadMenu(hThisInstance, TEXT("StatusBarMenu"));
        initmenu();

	SetMenu(ghwnd, menu);

	win_title_update = 1;
#endif
}

int WINAPI WinMain (HINSTANCE hThisInstance, HINSTANCE hPrevInstance, LPSTR lpszArgument, int nFunsterStil)
{
	HWND hwnd;					/* This is the handle for our window */
	MSG messages;					/* Here messages to the application are saved */
	WNDCLASSEX wincl;				/* Data structure for the windowclass */
	int c, d, bRet;
	WCHAR emulator_title[200];
	LARGE_INTEGER qpc_freq;
	HACCEL haccel;					/* Handle to accelerator table */

	memset(recv_key, 0, sizeof(recv_key));

	process_command_line();

	win_language_load_common_strings();

	hinstance=hThisInstance;
	/* The Window structure */
	wincl.hInstance = hThisInstance;
	wincl.lpszClassName = szClassName;
	wincl.lpfnWndProc = WindowProcedure;		/* This function is called by windows */
	wincl.style = CS_DBLCLKS;			/* Catch double-clicks */
	wincl.cbSize = sizeof (WNDCLASSEX);

	/* Use default icon and mouse-pointer */
	wincl.hIcon = LoadIcon(hinstance, (LPCTSTR) 100);
	wincl.hIconSm = LoadIcon(hinstance, (LPCTSTR) 100);
	wincl.hCursor = NULL;
	wincl.lpszMenuName = NULL;			/* No menu */
	wincl.cbClsExtra = 0;				/* No extra bytes after the window class */
	wincl.cbWndExtra = 0;				/* structure or the window instance */
	/* Use Windows's default color as the background of the window */
	wincl.hbrBackground = (HBRUSH) COLOR_BACKGROUND;

	/* Register the window class, and if it fails quit the program */
	if (!RegisterClassEx(&wincl))
	{
		return 0;
	}

	wincl.lpszClassName = szSubClassName;
	wincl.lpfnWndProc = subWindowProcedure;		/* This function is called by windows */

	if (!RegisterClassEx(&wincl))
	{
		return 0;
	}

	menu = LoadMenu(hThisInstance, TEXT("MainMenu"));

	_swprintf(emulator_title, L"%s v%s", EMU_NAME_W, EMU_VERSION_W);

	/* The class is registered, let's create the program*/
	hwnd = CreateWindowEx (
		0,                   /* Extended possibilites for variation */
		szClassName,         /* Classname */
		emulator_title,      /* Title Text */
		(WS_OVERLAPPEDWINDOW & ~WS_SIZEBOX)/* | DS_3DLOOK*/, /* default window */
		CW_USEDEFAULT,       /* Windows decides the position */
		CW_USEDEFAULT,       /* where the window ends up on the screen */
		640+(GetSystemMetrics(SM_CXFIXEDFRAME)*2),                 /* The programs width */
		480+(GetSystemMetrics(SM_CYFIXEDFRAME)*2)+GetSystemMetrics(SM_CYMENUSIZE)+GetSystemMetrics(SM_CYCAPTION)+1,                 /* and height in pixels */
		HWND_DESKTOP,        /* The window is a child-window to desktop */
		menu,                /* Menu */
		hThisInstance,       /* Program Instance handler */
		NULL                 /* No Window Creation data */
        );

	/* Make the window visible on the screen */
	ShowWindow (hwnd, nFunsterStil);

	/* Load the accelerator table */
	haccel = LoadAccelerators(hinstAcc, L"MainAccel");
	if (haccel == NULL)
	{
		fatal("haccel is null\n");
	}

	device.usUsagePage = 0x01;
	device.usUsage = 0x06;
	device.dwFlags = RIDEV_NOHOTKEYS;
	device.hwndTarget = hwnd;
	
	if (RegisterRawInputDevices(&device, 1, sizeof(device)))
	{
		pclog("Raw input registered!\n");
	}
	else
	{
		pclog("Raw input registration failed!\n");
	}

	get_registry_key_map();

        ghwnd=hwnd;

	hwndRender = CreateWindow(L"STATIC", NULL, WS_VISIBLE | WS_CHILD | SS_BITMAP, 0, 0, 1, 1, ghwnd, NULL, hinstance, NULL);

        initpc(argc, argv);

        init_cdrom_host_drives();

	hwndStatus = EmulatorStatusBar(hwnd, IDC_STATUS, hThisInstance);

	OriginalStatusBarProcedure = GetWindowLongPtr(hwndStatus, GWLP_WNDPROC);
	SetWindowLongPtr(hwndStatus, GWL_WNDPROC, (LONG_PTR) &StatusBarProcedure);

	smenu = LoadMenu(hThisInstance, TEXT("StatusBarMenu"));

	initmodules();

	if (vid_apis[0][vid_api].init(hwndRender) == 0)
	{
		if (vid_apis[0][vid_api ^ 1].init(hwndRender) == 0)
		{
			fatal("Both DirectDraw and Direct3D renderers failed to initialize\n");
		}
		else
		{
			vid_api ^= 1;
		}
	}

        if (vid_resize) SetWindowLongPtr(hwnd, GWL_STYLE, (WS_OVERLAPPEDWINDOW&~WS_MINIMIZEBOX)|WS_VISIBLE);
        else            SetWindowLongPtr(hwnd, GWL_STYLE, (WS_OVERLAPPEDWINDOW&~WS_SIZEBOX&~WS_THICKFRAME&~WS_MAXIMIZEBOX&~WS_MINIMIZEBOX)|WS_VISIBLE);

#ifdef ENABLE_LOG_TOGGLES
# ifdef ENABLE_BUSLOGIC_LOG
	CheckMenuItem(menu, IDM_LOG_BUSLOGIC, buslogic_do_log ? MF_CHECKED : MF_UNCHECKED);
# endif
# ifdef ENABLE_CDROM_LOG
	CheckMenuItem(menu, IDM_LOG_CDROM, cdrom_do_log ? MF_CHECKED : MF_UNCHECKED);
# endif
# ifdef ENABLE_D86F_LOG
	CheckMenuItem(menu, IDM_LOG_D86F, d86f_do_log ? MF_CHECKED : MF_UNCHECKED);
# endif
# ifdef ENABLE_FDC_LOG
	CheckMenuItem(menu, IDM_LOG_FDC, fdc_do_log ? MF_CHECKED : MF_UNCHECKED);
# endif
# ifdef ENABLE_IDE_LOG
	CheckMenuItem(menu, IDM_LOG_IDE, ide_do_log ? MF_CHECKED : MF_UNCHECKED);
# endif
# ifdef ENABLE_SERIAL_LOG
	CheckMenuItem(menu, IDM_LOG_SERIAL, serial_do_log ? MF_CHECKED : MF_UNCHECKED);
# endif
# ifdef ENABLE_NIC_LOG
	/*FIXME: should be network_setlog(1:0) */
	CheckMenuItem(menu, IDM_LOG_NIC, nic_do_log ? MF_CHECKED : MF_UNCHECKED);
# endif
#endif

	CheckMenuItem(menu, IDM_VID_FORCE43, force_43 ? MF_CHECKED : MF_UNCHECKED);
	CheckMenuItem(menu, IDM_VID_OVERSCAN, enable_overscan ? MF_CHECKED : MF_UNCHECKED);
	CheckMenuItem(menu, IDM_VID_INVERT, invert_display ? MF_CHECKED : MF_UNCHECKED);

        if (vid_resize) CheckMenuItem(menu, IDM_VID_RESIZE, MF_CHECKED);
        CheckMenuItem(menu, IDM_VID_DDRAW + vid_api, MF_CHECKED);
        CheckMenuItem(menu, IDM_VID_FS_FULL + video_fullscreen_scale, MF_CHECKED);
        CheckMenuItem(menu, IDM_VID_REMEMBER, window_remember ? MF_CHECKED : MF_UNCHECKED);
	CheckMenuItem(menu, IDM_VID_SCALE_1X + scale, MF_CHECKED);

	CheckMenuItem(menu, IDM_VID_CGACON, vid_cga_contrast ? MF_CHECKED : MF_UNCHECKED);
	CheckMenuItem(menu, IDM_VID_GRAYCT_601 + video_graytype, MF_CHECKED);
	CheckMenuItem(menu, IDM_VID_GRAY_RGB + video_grayscale, MF_CHECKED);

        d=romset;
        for (c=0;c<ROM_MAX;c++)
        {
                romset=c;
                romspresent[c]=loadbios();
                pclog("romset %i - %i\n", c, romspresent[c]);
        }
        
        for (c = 0; c < ROM_MAX; c++)
        {
                if (romspresent[c])
                   break;
        }
        if (c == ROM_MAX)
        {
		msgbox_critical(ghwnd, IDS_2062);
                return 0;
        }

        romset=d;
        c=loadbios();

        if (!c)
        {
                if (romset!=-1)
		{
			msgbox_info(ghwnd, IDS_2063);
		}
                for (c=0;c<ROM_MAX;c++)
                {
                        if (romspresent[c])
                        {
                                romset = c;
                                model = model_getmodel(romset);
                                saveconfig();
                                resetpchard();
                                break;
                        }
                }
        }
        
	for (c = 0; c < GFX_MAX; c++)
	{
		gfx_present[c] = video_card_available(video_old_to_new(c));
	}

        if (!video_card_available(video_old_to_new(gfxcard)))
        {
                if (romset!=-1)
		{
			msgbox_info(ghwnd, IDS_2064);
		}
                for (c = GFX_MAX-1; c >= 0; c--)
                {
                        if (gfx_present[c])
                        {
                                gfxcard = c;
                                saveconfig();
                                resetpchard();
                                break;
                        }
                }
        }

        loadbios();
        resetpchard();
        
        timeBeginPeriod(1);
        
        atexit(releasemouse);

        ghMutex = CreateMutex(NULL, FALSE, NULL);
        mainthreadh=(HANDLE)_beginthread(mainthread,0,NULL);
        SetThreadPriority(mainthreadh, THREAD_PRIORITY_HIGHEST);

        updatewindowsize(640, 480);

        QueryPerformanceFrequency(&qpc_freq);
        timer_freq = qpc_freq.QuadPart;

        if (start_in_fullscreen)
        {
                startblit();
                mouse_close();
                vid_apis[0][vid_api].close();
                video_fullscreen = 1;
		vid_apis[1][vid_api].init(hwndRender);
                mouse_init();
                leave_fullscreen_flag = 0;
                endblit();
                device_force_redraw();
        }
        if (window_remember)
        {
                MoveWindow(hwnd, window_x, window_y,
                        window_w,
                        window_h,
                        TRUE);
        }
	else
	{
		MoveWindow(hwndRender, 0, 0,
			winsizex,
			winsizey,
			TRUE);
		MoveWindow(hwndStatus, 0, winsizey + 6,
			winsizex,
			17,
			TRUE);
	}
                        
        /* Run the message loop. It will run until GetMessage() returns 0 */
        while (!quited)
        {
                while (((bRet = GetMessage(&messages,NULL,0,0)) != 0) && !quited)
                {
			if (bRet == -1)
			{
				fatal("bRet is -1\n");
			}

                        if (messages.message==WM_QUIT) quited=1;
			if (!TranslateAccelerator(hwnd, haccel, &messages))
			{
	                        TranslateMessage(&messages);
	                        DispatchMessage(&messages);
			}

	                if (recv_key[0x58] && recv_key[0x42] && mousecapture)
	                {
	                        ClipCursor(&oldclip);
	                        ShowCursor(TRUE);
	                        mousecapture=0;
	                }

		         if ((recv_key[0x1D] || recv_key[0x9D]) && (recv_key[0x38] || recv_key[0xB8]) && (recv_key[0x51] || recv_key[0xD1]) &&
		              video_fullscreen)
			{
				leave_fullscreen();
	                }
		}

                quited=1;
        }
        
        startblit();
        Sleep(200);
        TerminateThread(mainthreadh,0);
        savenvr();
	saveconfig();
        closepc();

        vid_apis[video_fullscreen][vid_api].close();
        
        timeEndPeriod(1);
        if (mousecapture) 
        {
                ClipCursor(&oldclip);
                ShowCursor(TRUE);
        }
        
        UnregisterClass(szSubClassName, hinstance);
        UnregisterClass(szClassName, hinstance);

        return messages.wParam;
}

HHOOK hKeyboardHook;
int hook_enabled = 0;

LRESULT CALLBACK LowLevelKeyboardProc(int nCode, WPARAM wParam, LPARAM lParam)
{
	BOOL bControlKeyDown;
	KBDLLHOOKSTRUCT* p;

	if (nCode < 0 || nCode != HC_ACTION)
	{
		return CallNextHookEx( hKeyboardHook, nCode, wParam, lParam);
	}
	
	p = (KBDLLHOOKSTRUCT*)lParam;

        if (p->vkCode == VK_TAB && p->flags & LLKHF_ALTDOWN) return 1; /* disable alt-tab */
        if (p->vkCode == VK_SPACE && p->flags & LLKHF_ALTDOWN) return 1; /* disable alt-tab */
	if((p->vkCode == VK_LWIN) || (p->vkCode == VK_RWIN)) return 1; /* disable windows keys */
	if (p->vkCode == VK_ESCAPE && p->flags & LLKHF_ALTDOWN) return 1; /* disable alt-escape */
	bControlKeyDown = GetAsyncKeyState (VK_CONTROL) >> ((sizeof(SHORT) * 8) - 1); /* checks ctrl key pressed */
	if (p->vkCode == VK_ESCAPE && bControlKeyDown) return 1; /* disable ctrl-escape */

	return CallNextHookEx( hKeyboardHook, nCode, wParam, lParam );
}

void cdrom_close(uint8_t id)
{
	switch (cdrom_drives[id].host_drive)
	{
		case 0:
			null_close(id);
			break;
		default:
			ioctl_close(id);
			break;
		case 200:
			image_close(id);
			break;
	}
}

static BOOL CALLBACK about_dlgproc(HWND hdlg, UINT message, WPARAM wParam, LPARAM lParam)
{
	HWND h;

        switch (message)
        {
		case WM_INITDIALOG:
			pause = 1;
                        h = GetDlgItem(hdlg, IDC_ABOUT_ICON);
			SendMessage(h, STM_SETIMAGE, (WPARAM) IMAGE_ICON, (LPARAM) LoadIconBig((PCTSTR) 100));
			break;

		case WM_COMMAND:
	                switch (LOWORD(wParam))
        	        {
	                        case IDOK:
					EndDialog(hdlg, 0);
                                        pause = 0;
					return TRUE;
				default:
					break;
			}
			break;
	}

	return FALSE;
}

void about_open(HWND hwnd)
{
	DialogBox(hinstance, (LPCTSTR)DLG_ABOUT, hwnd, about_dlgproc);
}

static void win_pc_reset(int hard)
{
	pause=1;
	Sleep(100);
	savenvr();
	saveconfig();
	if (hard)
	{
		resetpchard();
	}
	else
	{
		resetpc_cad();
	}
	pause=0;
}

void video_toggle_option(HMENU hmenu, int *val, int id)
{
	startblit();
	video_wait_for_blit();
	*val ^= 1;
	CheckMenuItem(hmenu, id, *val ? MF_CHECKED : MF_UNCHECKED);
	endblit();
	saveconfig();
	device_force_redraw();
}

LRESULT CALLBACK WindowProcedure (HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam)
{
	HMENU hmenu;
	RECT rect;
	int i = 0;

	switch (message)
	{
		case WM_CREATE:
			SetTimer(hwnd, TIMER_1SEC, 1000, NULL);
			hKeyboardHook = SetWindowsHookEx( WH_KEYBOARD_LL,  LowLevelKeyboardProc, GetModuleHandle(NULL), 0 );
			hook_enabled = 1;
			break;

		case WM_COMMAND:
			hmenu=GetMenu(hwnd);
			switch (LOWORD(wParam))
			{
				case IDM_ACTION_SCREENSHOT:
					take_screenshot();
					break;

				case IDM_ACTION_HRESET:
					win_pc_reset(1);
					break;

				case IDM_ACTION_RESET_CAD:
					win_pc_reset(0);
					break;

				case IDM_ACTION_EXIT:
					PostQuitMessage (0);       /* send a WM_QUIT to the message queue */
					break;

				case IDM_ACTION_CTRL_ALT_ESC:
					ctrl_alt_esc();
					break;
					
				case IDM_CONFIG:
					win_settings_open(hwnd);
					break;

				case IDM_ABOUT:
					about_open(hwnd);
					break;

				case IDM_STATUS:
					status_open(hwnd);
					break;

				case IDM_VID_RESIZE:
					vid_resize = !vid_resize;
					CheckMenuItem(hmenu, IDM_VID_RESIZE, (vid_resize)? MF_CHECKED : MF_UNCHECKED);
					if (vid_resize)	SetWindowLongPtr(hwnd, GWL_STYLE, (WS_OVERLAPPEDWINDOW & ~WS_MINIMIZEBOX) | WS_VISIBLE);
					else		SetWindowLongPtr(hwnd, GWL_STYLE, (WS_OVERLAPPEDWINDOW & ~WS_SIZEBOX & ~WS_THICKFRAME & ~WS_MAXIMIZEBOX & ~WS_MINIMIZEBOX) | WS_VISIBLE);
					GetWindowRect(hwnd, &rect);
					SetWindowPos(hwnd, 0, rect.left, rect.top, rect.right - rect.left, rect.bottom - rect.top, SWP_NOZORDER | SWP_FRAMECHANGED);
					GetWindowRect(hwndStatus,&rect);
					SetWindowPos(hwndStatus, 0, rect.left, rect.top, rect.right - rect.left, rect.bottom - rect.top, SWP_NOZORDER | SWP_FRAMECHANGED);
					if (vid_resize)
					{
						CheckMenuItem(hmenu, IDM_VID_SCALE_1X + scale, MF_UNCHECKED);
						CheckMenuItem(hmenu, IDM_VID_SCALE_2X, MF_CHECKED);
						scale = 1;
					}
					EnableMenuItem(hmenu, IDM_VID_SCALE_1X, vid_resize ? MF_GRAYED : MF_ENABLED);
					EnableMenuItem(hmenu, IDM_VID_SCALE_2X, vid_resize ? MF_GRAYED : MF_ENABLED);
					EnableMenuItem(hmenu, IDM_VID_SCALE_3X, vid_resize ? MF_GRAYED : MF_ENABLED);
					EnableMenuItem(hmenu, IDM_VID_SCALE_4X, vid_resize ? MF_GRAYED : MF_ENABLED);
					win_doresize = 1;
					saveconfig();
					break;

				case IDM_VID_REMEMBER:
					window_remember = !window_remember;
					CheckMenuItem(hmenu, IDM_VID_REMEMBER, window_remember ? MF_CHECKED : MF_UNCHECKED);
					GetWindowRect(hwnd, &rect);
					if (window_remember)
					{
						window_x = rect.left;
						window_y = rect.top;
						window_w = rect.right - rect.left;
						window_h = rect.bottom - rect.top;
					}
					saveconfig();
					break;

				case IDM_VID_DDRAW:
				case IDM_VID_D3D:
					startblit();
					video_wait_for_blit();
					CheckMenuItem(hmenu, IDM_VID_DDRAW + vid_api, MF_UNCHECKED);
					vid_apis[0][vid_api].close();
					vid_api = LOWORD(wParam) - IDM_VID_DDRAW;
					CheckMenuItem(hmenu, IDM_VID_DDRAW + vid_api, MF_CHECKED);
					vid_apis[0][vid_api].init(hwndRender);
					endblit();
					saveconfig();
					device_force_redraw();
					cgapal_rebuild();
					break;

				case IDM_VID_FULLSCREEN:
					if(video_fullscreen != 1)
					{
						if (video_fullscreen_first)
						{
							video_fullscreen_first = 0;
							msgbox_info(ghwnd, IDS_2193);
						}

						startblit();
						video_wait_for_blit();
						mouse_close();
						vid_apis[0][vid_api].close();
						video_fullscreen = 1;
						vid_apis[1][vid_api].init(ghwnd);
						mouse_init();
						leave_fullscreen_flag = 0;
						endblit();
						saveconfig();
						device_force_redraw();
						cgapal_rebuild();
					}
					break;

				case IDM_VID_FS_FULL:
				case IDM_VID_FS_43:
				case IDM_VID_FS_SQ:                                
				case IDM_VID_FS_INT:
					CheckMenuItem(hmenu, IDM_VID_FS_FULL + video_fullscreen_scale, MF_UNCHECKED);
					video_fullscreen_scale = LOWORD(wParam) - IDM_VID_FS_FULL;
					CheckMenuItem(hmenu, IDM_VID_FS_FULL + video_fullscreen_scale, MF_CHECKED);
					saveconfig();
					device_force_redraw();
					break;

				case IDM_VID_SCALE_1X:
				case IDM_VID_SCALE_2X:
				case IDM_VID_SCALE_3X:
				case IDM_VID_SCALE_4X:
					CheckMenuItem(hmenu, IDM_VID_SCALE_1X + scale, MF_UNCHECKED);
					scale = LOWORD(wParam) - IDM_VID_SCALE_1X;
					CheckMenuItem(hmenu, IDM_VID_SCALE_1X + scale, MF_CHECKED);
					saveconfig();
					device_force_redraw();
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
					vid_cga_contrast = !vid_cga_contrast;
					CheckMenuItem(menu, IDM_VID_CGACON, vid_cga_contrast ? MF_CHECKED : MF_UNCHECKED);
					cgapal_rebuild();
					saveconfig();
					break;

				case IDM_VID_GRAYCT_601:
				case IDM_VID_GRAYCT_709:
				case IDM_VID_GRAYCT_AVE:
					CheckMenuItem(hmenu, IDM_VID_GRAYCT_601 + video_graytype, MF_UNCHECKED);
					video_graytype = LOWORD(wParam) - IDM_VID_GRAYCT_601;
					CheckMenuItem(hmenu, IDM_VID_GRAYCT_601 + video_graytype, MF_CHECKED);
					saveconfig();
					device_force_redraw();
					break;

				case IDM_VID_GRAY_RGB:
				case IDM_VID_GRAY_MONO:
				case IDM_VID_GRAY_AMBER:
				case IDM_VID_GRAY_GREEN:
				case IDM_VID_GRAY_WHITE:
					CheckMenuItem(hmenu, IDM_VID_GRAY_RGB + video_grayscale, MF_UNCHECKED);
					video_grayscale = LOWORD(wParam) - IDM_VID_GRAY_RGB;
					CheckMenuItem(hmenu, IDM_VID_GRAY_RGB + video_grayscale, MF_CHECKED);
					saveconfig();
					device_force_redraw();
					break;

#ifdef ENABLE_LOG_TOGGLES
#ifdef ENABLE_BUSLOGIC_LOG
				case IDM_LOG_BUSLOGIC:
					buslogic_do_log ^= 1;
					CheckMenuItem(hmenu, IDM_LOG_BUSLOGIC, buslogic_do_log ? MF_CHECKED : MF_UNCHECKED);
					break;
#endif

#ifdef ENABLE_CDROM_LOG
				case IDM_LOG_CDROM:
					cdrom_do_log ^= 1;
					CheckMenuItem(hmenu, IDM_LOG_CDROM, cdrom_do_log ? MF_CHECKED : MF_UNCHECKED);
					break;
#endif

#ifdef ENABLE_D86F_LOG
				case IDM_LOG_D86F:
					d86f_do_log ^= 1;
					CheckMenuItem(hmenu, IDM_LOG_D86F, d86f_do_log ? MF_CHECKED : MF_UNCHECKED);
					break;
#endif

#ifdef ENABLE_FDC_LOG
				case IDM_LOG_FDC:
					fdc_do_log ^= 1;
					CheckMenuItem(hmenu, IDM_LOG_FDC, fdc_do_log ? MF_CHECKED : MF_UNCHECKED);
					break;
#endif

#ifdef ENABLE_IDE_LOG
				case IDM_LOG_IDE:
					ide_do_log ^= 1;
					CheckMenuItem(hmenu, IDM_LOG_IDE, ide_do_log ? MF_CHECKED : MF_UNCHECKED);
					break;
#endif

#ifdef ENABLE_SERIAL_LOG
				case IDM_LOG_SERIAL:
					serial_do_log ^= 1;
					CheckMenuItem(hmenu, IDM_LOG_SERIAL, serial_do_log ? MF_CHECKED : MF_UNCHECKED);
					break;
#endif

#ifdef ENABLE_NIC_LOG
				case IDM_LOG_NIC:
					/*FIXME: should be network_setlog() */
					nic_do_log ^= 1;
					CheckMenuItem(hmenu, IDM_LOG_NIC, nic_do_log ? MF_CHECKED : MF_UNCHECKED);
					break;
#endif
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
					pause = 1;
					if (!file_dlg_st(hwnd, IDS_2174, "", 0))
					{
						if (msgbox_reset_yn(ghwnd) == IDYES)
						{
							config_save(config_file_default);
							for (i = 0; i < FDD_NUM; i++)
							{
								disc_close(i);
							}
							for (i = 0; i < CDROM_NUM; i++)
							{
								cdrom_drives[i].handler->exit(i);
								if (cdrom_drives[i].host_drive == 200)
								{
									image_close(i);
								}
								else if ((cdrom_drives[i].host_drive >= 'A') && (cdrom_drives[i].host_drive <= 'Z'))
								{
									ioctl_close(i);
								}
								else
								{
									null_close(i);
								}
							}
							resetpchard_close();
							loadconfig(wopenfilestring);
							for (i = 0; i < CDROM_NUM; i++)
							{
								if (cdrom_drives[i].bus_type)
								{
									SCSIReset(cdrom_drives[i].scsi_device_id, cdrom_drives[i].scsi_device_lun);
								}

								if (cdrom_drives[i].host_drive == 200)
								{
									image_open(i, cdrom_image[i].image_path);
								}
								else if ((cdrom_drives[i].host_drive >= 'A') && (cdrom_drives[i].host_drive <= 'Z'))
								{
									ioctl_open(i, cdrom_drives[i].host_drive);
								}
								else	
								{
								        cdrom_null_open(i, cdrom_drives[i].host_drive);
								}
							}

							disc_load(0, discfns[0]);
							disc_load(1, discfns[1]);
							disc_load(2, discfns[2]);
							disc_load(3, discfns[3]);

							/* pclog_w(L"NVR path: %s\n", nvr_path); */
							mem_resize();
							loadbios();
							update_status_bar_panes(hwndStatus);
							resetpchard_init();
						}
					}
					pause = 0;
					break;                        

				case IDM_CONFIG_SAVE:
					pause = 1;
					if (!file_dlg_st(hwnd, IDS_2174, "", 1))
					{
						config_save(wopenfilestring);
					}
					pause = 0;
					break;                                                
			}
			return 0;		

		case WM_INPUT:
			process_raw_input(lParam, infocus);
			break;

		case WM_SETFOCUS:
			infocus=1;
			if (!hook_enabled)
			{
				hKeyboardHook = SetWindowsHookEx( WH_KEYBOARD_LL,  LowLevelKeyboardProc, GetModuleHandle(NULL), 0 );
				hook_enabled = 1;
			}
			break;

		case WM_KILLFOCUS:
			infocus=0;
			if (mousecapture)
			{
				ClipCursor(&oldclip);
				ShowCursor(TRUE);
				mousecapture=0;
			}
			memset(recv_key, 0, sizeof(recv_key));
			if (video_fullscreen)
			{
				leave_fullscreen_flag = 1;
			}
			if (hook_enabled)
			{
				UnhookWindowsHookEx(hKeyboardHook);
				hook_enabled = 0;
			}
			break;

		case WM_LBUTTONUP:
			if (!mousecapture && !video_fullscreen)
			{
				GetClipCursor(&oldclip);
				GetWindowRect(hwndRender, &rect);

				ClipCursor(&rect);
				mousecapture = 1;
				while (1)
				{
					if (ShowCursor(FALSE) < 0)
					{
						break;
					}
				}
			}
			break;

		case WM_MBUTTONUP:
			if (!(mouse_get_type(mouse_type) & MOUSE_TYPE_3BUTTON))
			{
				releasemouse();
			}
			break;

		case WM_ENTERMENULOOP:
			break;

		case WM_SIZE:
			winsizex = (lParam & 0xFFFF);
			winsizey = (lParam >> 16) - (17 + 6);

			if (winsizey < 0)
			{
				winsizey = 0;
			}

			MoveWindow(hwndRender, 0, 0, winsizex, winsizey, TRUE);

			if (vid_apis[video_fullscreen][vid_api].resize)
			{
				startblit();
				video_wait_for_blit();
				vid_apis[video_fullscreen][vid_api].resize(winsizex, winsizey);
				endblit();
			}

			MoveWindow(hwndStatus, 0, winsizey + 6, winsizex, 17, TRUE);

			if (mousecapture)
			{
				GetWindowRect(hwndRender, &rect);

				ClipCursor(&rect);
			}

			if (window_remember)
			{
				GetWindowRect(hwnd, &rect);
				window_x = rect.left;
				window_y = rect.top;
				window_w = rect.right - rect.left;
				window_h = rect.bottom - rect.top;
				save_window_pos = 1;
			}

			saveconfig();
			break;

		case WM_MOVE:
			if (window_remember)
			{
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
			{
				onesec();
			}
			break;

		case WM_RESETD3D:
			startblit();
			if (video_fullscreen)
			{
				d3d_fs_reset();
			}
			else
			{
				d3d_reset();
			}
			endblit();
			break;

		case WM_LEAVEFULLSCREEN:
			startblit();
			mouse_close();
			vid_apis[1][vid_api].close();
			video_fullscreen = 0;
			saveconfig();
			vid_apis[0][vid_api].init(hwndRender);
			mouse_init();
			endblit();
			device_force_redraw();
			cgapal_rebuild();
			break;

		case WM_KEYDOWN:
		case WM_SYSKEYDOWN:
		case WM_KEYUP:
		case WM_SYSKEYUP:
			return 0;

		case WM_DESTROY:
			UnhookWindowsHookEx(hKeyboardHook);
			KillTimer(hwnd, TIMER_1SEC);
			PostQuitMessage (0);		/* send a WM_QUIT to the message queue */
			break;

		case WM_SYSCOMMAND:
			/* Disable ALT key *ALWAYS*, I don't think there's any use for reaching the menu that way. */
			if (wParam == SC_KEYMENU && HIWORD(lParam) <= 0)
			{
				return 0; /*disable ALT key for menu*/
			}

		default:
			return DefWindowProc (hwnd, message, wParam, lParam);
	}
	return 0;
}

LRESULT CALLBACK subWindowProcedure(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam)
{
	switch (message)
	{
		default:
			return DefWindowProc(hwnd, message, wParam, lParam);
	}
	return 0;
}

VOID APIENTRY HandlePopupMenu(HWND hwnd, POINT pt, int id)
{
	if (id >= (sb_parts - 1))
	{
		return;
	}
	pt.x = id * SB_ICON_WIDTH;	/* Justify to the left. */
	pt.y = 0;			/* Justify to the top. */
	ClientToScreen(hwnd, (LPPOINT) &pt);
	TrackPopupMenu(sb_menu_handles[id], TPM_LEFTALIGN | TPM_BOTTOMALIGN | TPM_LEFTBUTTON, pt.x, pt.y, 0, hwndStatus, NULL);
}

LRESULT CALLBACK StatusBarProcedure(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam)
{
	RECT rc;
	POINT pt;

	WCHAR temp_image_path[1024];
	int new_cdrom_drive;
	int ret = 0;
	int item_id = 0;
	int item_params = 0;
	int id = 0;
	int part = 0;
	int letter = 0;

	HMENU hmenu;

	switch (message)
	{
		case WM_COMMAND:
			item_id = LOWORD(wParam) & 0xff00;	/* Mask out the low 8 bits for item ID. */
			item_params = LOWORD(wParam) & 0x00ff;	/* Mask out the high 8 bits for item parameter. */

	                switch (item_id)
        	        {
	                        case IDM_FLOPPY_IMAGE_EXISTING:
        	                case IDM_FLOPPY_IMAGE_EXISTING_WP:
					id = item_params & 0x0003;
					part = find_status_bar_part(SB_FLOPPY | id);
					if ((part == -1) || (sb_menu_handles == NULL))
					{
						break;
					}

					ret = file_dlg_w_st(hwnd, IDS_2173, discfns[id], 0);
					if (!ret)
					{
						disc_close(id);
						ui_writeprot[id] = (item_id == IDM_FLOPPY_IMAGE_EXISTING_WP) ? 1 : 0;
						disc_load(id, wopenfilestring);
						update_status_bar_icon_state(SB_FLOPPY | id, wcslen(discfns[id]) ? 0 : 1);
						EnableMenuItem(sb_menu_handles[part], IDM_FLOPPY_EJECT | id, MF_BYCOMMAND | (wcslen(discfns[id]) ? MF_ENABLED : MF_GRAYED));
						update_tip(SB_FLOPPY | id);
						saveconfig();
					}
					break;

				case IDM_FLOPPY_EJECT:
					id = item_params & 0x0003;
					part = find_status_bar_part(SB_FLOPPY | id);
					if ((part == -1) || (sb_menu_handles == NULL))
					{
						break;
					}

					disc_close(id);
					update_status_bar_icon_state(SB_FLOPPY | id, 1);
					EnableMenuItem(sb_menu_handles[part], IDM_FLOPPY_EJECT | id, MF_BYCOMMAND | MF_GRAYED);
					update_tip(SB_FLOPPY | id);
					saveconfig();
					break;

				case IDM_CDROM_MUTE:
					id = item_params & 0x0007;
					hmenu = GetSubMenu(smenu, id + 4);
					Sleep(100);
					cdrom_drives[id].sound_on ^= 1;                                             
					CheckMenuItem(hmenu, IDM_CDROM_MUTE | id, cdrom_drives[id].sound_on ? MF_UNCHECKED : MF_CHECKED);
					saveconfig();
					sound_cd_thread_reset();
					break;

				case IDM_CDROM_EMPTY:
					id = item_params & 0x0007;
					cdrom_eject(id);
					break;

				case IDM_CDROM_RELOAD:
					id = item_params & 0x0007;
					cdrom_reload(id);
					break;

				case IDM_CDROM_IMAGE:
					id = item_params & 0x0007;
					part = find_status_bar_part(SB_CDROM | id);
					if ((part == -1) || (sb_menu_handles == NULL))
					{
						break;
					}

					if (!file_dlg_w_st(hwnd, IDS_2175, cdrom_image[id].image_path, 0))
					{
						cdrom_drives[id].prev_host_drive = cdrom_drives[id].host_drive;
						wcscpy(temp_image_path, wopenfilestring);
						if ((wcscmp(cdrom_image[id].image_path, temp_image_path) == 0) && (cdrom_drives[id].host_drive == 200))
						{
							/* Switching from image to the same image. Do nothing. */
							break;
						}
						cdrom_drives[id].handler->exit(id);
						cdrom_close(id);
						image_open(id, temp_image_path);
						/* Signal disc change to the emulated machine. */
						cdrom_insert(id);
						CheckMenuItem(sb_menu_handles[part], IDM_CDROM_EMPTY | id, MF_UNCHECKED);
						if ((cdrom_drives[id].host_drive >= 'A') && (cdrom_drives[id].host_drive <= 'Z'))
						{
							CheckMenuItem(sb_menu_handles[part], IDM_CDROM_HOST_DRIVE | id | ((cdrom_drives[id].host_drive - 'A') << 3), MF_UNCHECKED);
						}
						cdrom_drives[id].host_drive = (wcslen(cdrom_image[id].image_path) == 0) ? 0 : 200;
						if (cdrom_drives[id].host_drive == 200)
						{
							CheckMenuItem(sb_menu_handles[part], IDM_CDROM_IMAGE | id, MF_CHECKED);
							update_status_bar_icon_state(SB_CDROM | id, 0);
						}
						else
						{
							CheckMenuItem(sb_menu_handles[part], IDM_CDROM_IMAGE | id, MF_UNCHECKED);
							CheckMenuItem(sb_menu_handles[part], IDM_CDROM_EMPTY | id, MF_UNCHECKED);
							update_status_bar_icon_state(SB_CDROM | id, 1);
						}
						update_tip(SB_CDROM | id);
						saveconfig();
					}
					break;

	                        case IDM_CDROM_HOST_DRIVE:
					id = item_params & 0x0007;
					letter = ((item_params >> 3) & 0x001f) + 'A';
					part = find_status_bar_part(SB_CDROM | id);
					if ((part == -1) || (sb_menu_handles == NULL))
					{
						break;
					}

					new_cdrom_drive = letter;
					if (cdrom_drives[id].host_drive == new_cdrom_drive)
					{
						/* Switching to the same drive. Do nothing. */
						break;
					}
					cdrom_drives[id].prev_host_drive = cdrom_drives[id].host_drive;
					cdrom_drives[id].handler->exit(id);
					cdrom_close(id);
                                	ioctl_open(id, new_cdrom_drive);
					/* Signal disc change to the emulated machine. */
					cdrom_insert(id);
                	                CheckMenuItem(sb_menu_handles[part], IDM_CDROM_EMPTY | id, MF_UNCHECKED);
					if ((cdrom_drives[id].host_drive >= 'A') && (cdrom_drives[id].host_drive <= 'Z'))
					{
	                                	CheckMenuItem(sb_menu_handles[part], IDM_CDROM_HOST_DRIVE | id | ((cdrom_drives[id].host_drive - 'A') << 3), MF_UNCHECKED);
					}
        	                        CheckMenuItem(sb_menu_handles[part], IDM_CDROM_IMAGE | id, MF_UNCHECKED);
                	                cdrom_drives[id].host_drive = new_cdrom_drive;
	                                CheckMenuItem(sb_menu_handles[part], IDM_CDROM_HOST_DRIVE | id | ((cdrom_drives[id].host_drive - 'A') << 3), MF_CHECKED);
					EnableMenuItem(sb_menu_handles[part], IDM_CDROM_RELOAD | id, MF_BYCOMMAND | MF_GRAYED);
					update_status_bar_icon_state(SB_CDROM | id, 0);
					update_tip(SB_CDROM | id);
                	                saveconfig();
					break;

				case IDM_RDISK_EJECT:
					id = item_params & 0x001f;
					removable_disk_eject(id);
					break;

				case IDM_RDISK_RELOAD:
					id = item_params & 0x001f;
					removable_disk_reload(id);
					break;

				case IDM_RDISK_SEND_CHANGE:
					id = item_params & 0x001f;
					scsi_disk_insert(id);
					break;

				case IDM_RDISK_IMAGE:
				case IDM_RDISK_IMAGE_WP:
					id = item_params & 0x001f;
					ret = file_dlg_w_st(hwnd, IDS_2172, hdc[id].fn, id);
					if (!ret)
					{
						removable_disk_unload(id);
						memset(hdc[id].fn, 0, sizeof(hdc[id].fn));
						wcscpy(hdc[id].fn, wopenfilestring);
						hdc[id].wp = (item_id == IDM_RDISK_IMAGE_WP) ? 1 : 0;
						scsi_loadhd(hdc[id].scsi_id, hdc[id].scsi_lun, id);
						scsi_disk_insert(id);
						if (wcslen(hdc[id].fn) > 0)
						{
							update_status_bar_icon_state(SB_RDISK | id, 0);
							EnableMenuItem(sb_menu_handles[part], IDM_RDISK_EJECT | id, MF_BYCOMMAND | MF_ENABLED);
							EnableMenuItem(sb_menu_handles[part], IDM_RDISK_RELOAD | id, MF_BYCOMMAND | MF_GRAYED);
							EnableMenuItem(sb_menu_handles[part], IDM_RDISK_SEND_CHANGE | id, MF_BYCOMMAND | MF_ENABLED);
						}
						else
						{
							update_status_bar_icon_state(SB_RDISK | id, 1);
							EnableMenuItem(sb_menu_handles[part], IDM_RDISK_EJECT | id, MF_BYCOMMAND | MF_GRAYED);
							EnableMenuItem(sb_menu_handles[part], IDM_RDISK_RELOAD | id, MF_BYCOMMAND | MF_GRAYED);
							EnableMenuItem(sb_menu_handles[part], IDM_RDISK_SEND_CHANGE | id, MF_BYCOMMAND | MF_GRAYED);
						}
						update_tip(SB_RDISK | id);
						saveconfig();
					}
					break;

				default:
					break;
			}
			return 0;

		case WM_LBUTTONDOWN:
		case WM_RBUTTONDOWN:
			GetClientRect(hwnd, (LPRECT)& rc);
			pt.x = GET_X_LPARAM(lParam);
			pt.y = GET_Y_LPARAM(lParam);
			if (PtInRect((LPRECT) &rc, pt))
			{
				HandlePopupMenu(hwnd, pt, (pt.x / SB_ICON_WIDTH));
			}
			break;

		default:
	                return CallWindowProc((WNDPROC) OriginalStatusBarProcedure, hwnd, message, wParam, lParam);
	}
	return 0;
}
