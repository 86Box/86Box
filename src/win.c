/* Copyright holders: Sarah Walker, Tenshi
   see COPYING for more details
*/
#define UNICODE
#define  _WIN32_WINNT 0x0501
#define BITMAP WINDOWS_BITMAP
#include <windows.h>
#include <windowsx.h>
#undef BITMAP

#include <commctrl.h>
#include <commdlg.h>
#include <process.h>
#include <string.h>
#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>
#include <stdint.h>
#include "86box.h"
#include "device.h"
#include "disc.h"
#include "fdd.h"
#include "hdd.h"
#include "ibm.h"
#include "cpu/cpu.h"
#include "mem.h"
#include "rom.h"
#include "nvr.h"
#include "thread.h"
#include "config.h"
#include "model.h"
#include "ide.h"
#include "cdrom.h"
#include "cdrom-null.h"
#include "cdrom-ioctl.h"
#include "cdrom-image.h"
#include "video/video.h"
#include "video/vid_ega.h"
#include "plat-keyboard.h"
#include "plat-mouse.h"
#include "plat-midi.h"
#include "mouse.h"
#include "sound/sound.h"
#include "sound/snd_dbopl.h"

#include "win.h"
#include "win-ddraw.h"
#include "win-ddraw-fs.h"
#include "win-d3d.h"
#include "win-d3d-fs.h"
#include "win-language.h"
#include "resource.h"


#ifndef MAPVK_VK_TO_VSC
#define MAPVK_VK_TO_VSC 0
#endif

static int save_window_pos = 0;
uint64_t timer_freq;

int rawinputkey[272];

static RAWINPUTDEVICE device;
static uint16_t scancode_map[65536];

static struct
{
        int (*init)(HWND h);
        void (*close)();
        void (*resize)(int x, int y);
} vid_apis[2][2] =
{
        {
                ddraw_init, ddraw_close, NULL,
                d3d_init, d3d_close, d3d_resize
        },
        {
                ddraw_fs_init, ddraw_fs_close, NULL,
                d3d_fs_init, d3d_fs_close, NULL
        },
};

#define TIMER_1SEC 1

int winsizex=640,winsizey=480;
int efwinsizey=480;
int gfx_present[GFX_MAX];

HANDLE ghMutex;

HANDLE mainthreadh;

int infocus=1;

int drawits=0;

int romspresent[ROM_MAX];
int quited=0;

RECT oldclip;
int mousecapture=0;

/*  Declare Windows procedure  */
LRESULT CALLBACK WindowProcedure(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam);
LRESULT CALLBACK subWindowProcedure(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam);

LRESULT CALLBACK StatusBarProcedure(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam);

LONG OriginalStatusBarProcedure;

HWND ghwnd;

HINSTANCE hinstance;

HMENU menu;

extern int updatestatus;

int pause=0;

static int win_doresize = 0;

static int leave_fullscreen_flag = 0;

static int unscaled_size_x = 0;
static int unscaled_size_y = 0;

int scale = 0;

HWND hwndRender, hwndStatus;

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

void uws_natural()
{
	updatewindowsize(unscaled_size_x, efwinsizey);
}

void releasemouse()
{
        if (mousecapture) 
        {
                ClipCursor(&oldclip);
                ShowCursor(TRUE);
                mousecapture = 0;
        }
}

void startblit()
{
        WaitForSingleObject(ghMutex, INFINITE);
}

void endblit()
{
        ReleaseMutex(ghMutex);
}

void leave_fullscreen()
{
        leave_fullscreen_flag = 1;
}

uint64_t main_time;

uint64_t start_time;
uint64_t end_time;

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
                                SendMessage(status_hwnd, WM_USER, 0, 0);
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
                        MoveWindow(hwndRender, 0, 0,
                                winsizex,
                                winsizey,
                                TRUE);
                        GetWindowRect(hwndRender, &r);
                        MoveWindow(hwndStatus, 0, r.bottom + GetSystemMetrics(SM_CYEDGE),
       	                        winsizex,
               	                17,
                       	        TRUE);
                        GetWindowRect(ghwnd, &r);

                        MoveWindow(ghwnd, r.left, r.top,
                                winsizex + (GetSystemMetrics(vid_resize ? SM_CXSIZEFRAME : SM_CXFIXEDFRAME) * 2),
                                winsizey + (GetSystemMetrics(SM_CYEDGE) * 2) + (GetSystemMetrics(vid_resize ? SM_CYSIZEFRAME : SM_CYFIXEDFRAME) * 2) + GetSystemMetrics(SM_CYMENUSIZE) + GetSystemMetrics(SM_CYCAPTION) + 17 + sb_borders[1] + 1,
                                TRUE);

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

typedef struct win_event_t
{
        HANDLE handle;
} win_event_t;

event_t *thread_create_event()
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

HMENU smenu;

static void initmenu(void)
{
        int i, c;
        HMENU m;
        WCHAR s[64];

	for (i = 0; i < CDROM_NUM; i++)
	{
	        m=GetSubMenu(smenu, i + 4); /*CD-ROM*/

	        /* Loop through each Windows drive letter and test to see if
	           it's a CDROM */
	        for (c='A';c<='Z';c++)
	        {
        	        _swprintf(s,L"%c:\\",c);
	                if (GetDriveType(s)==DRIVE_CDROM)
	                {
        	                _swprintf(s, win_language_get_string_from_id(2076), c);
	                        AppendMenu(m,MF_STRING,IDM_CDROM_1_REAL+(c << 2)+i,s);
	                }
	        }
	}
}

void get_executable_name(WCHAR *s, int size)
{
        GetModuleFileName(hinstance, s, size);
}

void set_window_title(WCHAR *s)
{
        if (video_fullscreen)
                return;
        SetWindowText(ghwnd, s);
}

uint64_t timer_read()
{
        LARGE_INTEGER qpc_time;
        QueryPerformanceCounter(&qpc_time);
        return qpc_time.QuadPart;
}

/* This is so we can disambiguate scan codes that would otherwise conflict and get
   passed on incorrectly. */
UINT16 convert_scan_code(UINT16 scan_code)
{
	switch (scan_code)
        {
		case 0xE001:
		return 0xF001;
		case 0xE002:
		return 0xF002;
		case 0xE0AA:
		return 0xF003;
		case 0xE005:
		return 0xF005;
		case 0xE006:
		return 0xF006;
		case 0xE007:
		return 0xF007;
		case 0xE071:
		return 0xF008;
		case 0xE072:
		return 0xF009;
		case 0xE07F:
		return 0xF00A;
		case 0xE0E1:
		return 0xF00B;
		case 0xE0EE:
		return 0xF00C;
		case 0xE0F1:
		return 0xF00D;
		case 0xE0FE:
		return 0xF00E;
		case 0xE0EF:
		return 0xF00F;

		default:
		return scan_code;
	}
}

void get_registry_key_map()
{
	WCHAR *keyName = L"SYSTEM\\CurrentControlSet\\Control\\Keyboard Layout";
	WCHAR *valueName = L"Scancode Map";
	unsigned char buf[32768];
	DWORD bufSize;
	HKEY hKey;
	int j;

 	/* First, prepare the default scan code map list which is 1:1.
 	   Remappings will be inserted directly into it.
 	   65536 bytes so scan codes fit in easily and it's easy to find what each maps too,
 	   since each array element is a scan code and provides for E0, etc. ones too. */
	for (j = 0; j < 65536; j++)
		scancode_map[j] = convert_scan_code(j);

	bufSize = 32768;
 	/* Get the scan code remappings from:
 	   HKEY_LOCAL_MACHINE\SYSTEM\CurrentControlSet\Control\Keyboard Layout */
	if (RegOpenKeyEx(HKEY_LOCAL_MACHINE, keyName, 0, 1, &hKey) == ERROR_SUCCESS)
        {
		if(RegQueryValueEx(hKey, valueName, NULL, NULL, buf, &bufSize) == ERROR_SUCCESS)
                {
			UINT32 *bufEx2 = (UINT32 *) buf;
			int scMapCount = bufEx2[2];
			if ((bufSize != 0) && (scMapCount != 0))
                        {
				UINT16 *bufEx = (UINT16 *) (buf + 12);
				for (j = 0; j < scMapCount*2; j += 2)
 				{
 					/* Each scan code is 32-bit: 16 bits of remapped scan code,
 					   and 16 bits of original scan code. */
  					int scancode_unmapped = bufEx[j + 1];
  					int scancode_mapped = bufEx[j];

  					scancode_mapped = convert_scan_code(scancode_mapped);

					/* Fixes scan code map logging. */
  					scancode_map[scancode_unmapped] = scancode_mapped;
  				}
			}
		}
		RegCloseKey(hKey);
	}
}

static wchar_t **argv;
static int argc;
static wchar_t *argbuf;

static void process_command_line()
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

int valid_models[2] = { 0, 1 };
int valid_bases[6] = { 0x130, 0x134, 0x230, 0x234, 0x330, 0x334 };
int valid_irqs[6] = { 9, 10, 11, 12, 14, 15 };
int valid_dma_channels[3] = { 5, 6, 7 };
int valid_ide_channels[8] = { 0, 1, 2, 3, 4, 5, 6, 7 };
int valid_scsi_ids[15] = { 0, 1, 2, 3, 4, 5, 6, 8, 9, 10, 11, 12, 13, 14, 15 };
int valid_scsi_luns[8] = { 0, 1, 2, 3, 4, 5, 6, 7 };

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

HANDLE hinstAcc;

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

int sb_parts = 10;

int sb_part_meanings[12];
int sb_part_icons[12];

int sb_icon_width = 24;

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

HICON hIcon[512];

int iStatusWidths[] = { 18, 36, 54, 72, 90, 108, 126, 144, 168, 192, 210, -1 };

#define SBI_FLAG_ACTIVE		1
#define SBI_FLAG_EMPTY		256

int sb_icon_flags[512];

/* This is for the disk activity indicator. */
void update_status_bar_icon(int tag, int active)
{
	int i = 0;
	int found = -1;
	int temp_flags = 0;

	if ((tag & 0xf0) >= 0x30)
	{
		return;
	}

	temp_flags |= active;

	for (i = 0; i < 12; i++)
	{
		if (sb_part_meanings[i] == tag)
		{
			found = i;
			break;
		}
	}

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
	int i = 0;
	int found = -1;

	if ((tag & 0xf0) >= 0x20)
	{
		return;
	}

	for (i = 0; i < 12; i++)
	{
		if (sb_part_meanings[i] == tag)
		{
			found = i;
			break;
		}
	}

	if (found != -1)
	{
		sb_icon_flags[found] &= ~256;
		sb_icon_flags[found] |= state ? 256 : 0;

		sb_part_icons[found] &= ~257;
		sb_part_icons[found] |= sb_icon_flags[found];

		SendMessage(hwndStatus, SB_SETICON, found, (LPARAM) hIcon[sb_part_icons[found]]);
	}
}

WCHAR sbTips[24][512];

void create_floppy_tip(int part)
{
	WCHAR *szText;
	WCHAR wtext[512];

	int drive = sb_part_meanings[part] & 0xf;

	mbstowcs(wtext, fdd_getname(fdd_get_type(drive)), strlen(fdd_getname(fdd_get_type(drive))) + 1);
	if (wcslen(discfns[drive]) == 0)
	{
		_swprintf(sbTips[part],  win_language_get_string_from_id(2179), drive + 1, wtext, win_language_get_string_from_id(2185));
	}
	else
	{
		_swprintf(sbTips[part],  win_language_get_string_from_id(2179), drive + 1, wtext, discfns[drive]);
	}
}

void create_cdrom_tip(int part)
{
	WCHAR *szText;
	char ansi_text[3][512];
	WCHAR wtext[512];

	int drive = sb_part_meanings[part] & 0xf;

	if (cdrom_drives[drive].host_drive == 200)
	{
		if (wcslen(cdrom_image[drive].image_path) == 0)
		{
			_swprintf(sbTips[part], win_language_get_string_from_id(2180), drive + 1, win_language_get_string_from_id(2185));
		}
		else
		{
			_swprintf(sbTips[part], win_language_get_string_from_id(2180), drive + 1, cdrom_image[drive].image_path);
		}
	}
	else if (cdrom_drives[drive].host_drive < 0x41)
	{
		_swprintf(sbTips[part], win_language_get_string_from_id(2180), drive + 1, win_language_get_string_from_id(2185));
	}
	else
	{
		_swprintf(wtext, win_language_get_string_from_id(2186), cdrom_drives[drive].host_drive & ~0x20);
		_swprintf(sbTips[part], win_language_get_string_from_id(2180), drive + 1, wtext);
	}
}

void create_hd_tip(int part)
{
	WCHAR *szText;

	int bus = sb_part_meanings[part] & 0xf;
	szText = (WCHAR *) win_language_get_string_from_id(2181 + bus);
	memcpy(sbTips[part], szText, (wcslen(szText) << 1) + 2);
}

void update_tip(int meaning)
{
	int i = 0;
	int part = -1;

	for (i = 0; i < sb_parts; i++)
	{
		if (sb_part_meanings[i] == meaning)
		{
			part = i;
		}
	}

	if (part != -1)
	{
		switch(meaning & 0x30)
		{
			case 0x00:
				create_floppy_tip(part);
				break;
			case 0x10:
				create_cdrom_tip(part);
				break;
			case 0x20:
				create_hd_tip(part);
				break;
			default:
				break;
		}

		SendMessage(hwndStatus, SB_SETTIPTEXT, part, (LPARAM) sbTips[part]);
	}
}

static int get_floppy_state(int id)
{
	return (wcslen(discfns[id]) == 0) ? 1 : 0;
}

static int get_cd_state(int id)
{
	if (cdrom_drives[id].host_drive < 0x41)
	{
		return 1;
	}
	else
	{
		if (cdrom_drives[id].host_drive == 0x200)
		{
			return (wcslen(cdrom_image[id].image_path) == 0) ? 1 : 0;
		}
		else
		{
			return 0;
		}
	}
}

void status_settextw(wchar_t *wstr)
{
	int i = 0;
	int part = -1;

	for (i = 0; i < sb_parts; i++)
	{
		if (sb_part_meanings[i] == 0x30)
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

void update_status_bar_panes(HWND hwnds)
{
	int i, j, id;
	int edge = 0;

	int c_rll = 0;
	int c_mfm = 0;
	int c_ide_pio = 0;
	int c_ide_dma = 0;
	int c_scsi = 0;

	c_mfm = count_hard_disks(1);
	c_ide_pio = count_hard_disks(2);
	c_ide_dma = count_hard_disks(3);
	c_scsi = count_hard_disks(4);

	sb_parts = 0;
	memset(sb_part_meanings, 0, 40);
	for (i = 0; i < 4; i++)
	{
		if (fdd_get_type(i) != 0)
		{
			/* pclog("update_status_bar_panes(): Found floppy drive %c:, type %i\n", 65 + i, fdd_get_type(i)); */
			edge += sb_icon_width;
			iStatusWidths[sb_parts] = edge;
			sb_part_meanings[sb_parts] = 0x00 | i;
			sb_parts++;
		}
	}
	for (i = 0; i < 4; i++)
	{
		if (cdrom_drives[i].enabled != 0)
		{
			edge += sb_icon_width;
			iStatusWidths[sb_parts] = edge;
			sb_part_meanings[sb_parts] = 0x10 | i;
			sb_parts++;
		}
	}
	if (c_mfm && !(models[model].flags & MODEL_HAS_IDE) && !!memcmp(hdd_controller_name, "none", 4) && !!memcmp(hdd_controller_name, "xtide", 5))
	{
		edge += sb_icon_width;
		iStatusWidths[sb_parts] = edge;
		sb_part_meanings[sb_parts] = 0x20;
		sb_parts++;
	}
	if (c_ide_pio && (models[model].flags & MODEL_HAS_IDE) || !memcmp(hdd_controller_name, "xtide", 5))
	{
		edge += sb_icon_width;
		iStatusWidths[sb_parts] = edge;
		sb_part_meanings[sb_parts] = 0x21;
		sb_parts++;
	}
	if (c_ide_dma && (models[model].flags & MODEL_HAS_IDE) || !memcmp(hdd_controller_name, "xtide", 5))
	{
		edge += sb_icon_width;
		iStatusWidths[sb_parts] = edge;
		sb_part_meanings[sb_parts] = 0x22;
		sb_parts++;
	}
	if (c_scsi)
	{
		edge += sb_icon_width;
		iStatusWidths[sb_parts] = edge;
		sb_part_meanings[sb_parts] = 0x23;
		sb_parts++;
	}
	if (sb_parts)
	{
		iStatusWidths[sb_parts - 1] += (24 - sb_icon_width);
	}
	iStatusWidths[sb_parts] = -1;
	sb_part_meanings[sb_parts] = 0x30;
	sb_parts++;

	SendMessage(hwnds, SB_SETPARTS, (WPARAM) sb_parts, (LPARAM) iStatusWidths);

	for (i = 0; i < sb_parts; i++)
	{
		switch (sb_part_meanings[i] & 0x30)
		{
			case 0x00:
				/* Floppy */
				sb_icon_flags[i] = (wcslen(discfns[sb_part_meanings[i] & 0xf]) == 0) ? 256 : 0;
				sb_part_icons[i] = fdd_type_to_icon(fdd_get_type(sb_part_meanings[i] & 0xf)) | sb_icon_flags[i];
				create_floppy_tip(i);
				break;
			case 0x10:
				/* CD-ROM */
				id = sb_part_meanings[i] & 0xf;
				if (cdrom_drives[id].host_drive < 0x41)
				{
					sb_icon_flags[i] = 256;
				}
				else
				{
					if (cdrom_drives[id].host_drive == 0x200)
					{
						sb_icon_flags[i] = (wcslen(cdrom_image[id].image_path) == 0) ? 256 : 0;
					}
					else
					{
						sb_icon_flags[i] = 0;
					}
				}
				if (cdrom_drives[id].bus_type == 1)
				{
					j = 164;
				}
				else
				{
					j = (cdrom_drives[id].atapi_dma) ? 162 : 160;
				}
				sb_part_icons[i] = j | sb_icon_flags[i];
				create_cdrom_tip(i);
				break;
			case 0x20:
				/* Hard disk */
				sb_part_icons[i] = 176 + ((sb_part_meanings[i] & 0xf) << 1);
				create_hd_tip(i);
				break;
			case 0x30:
				/* Status text */
				SendMessage(hwnds, SB_SETTEXT, i | SBT_NOBORDERS, (LPARAM) L"Welcome to Unicode 86Box! :p");
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

	for (i = 176; i < 184; i++)
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

	GetWindowRect(hwndParent, &rectDialog);
	dw = rectDialog.right - rectDialog.left;
	dh = rectDialog.bottom - rectDialog.top;

	InitCommonControls();

	hwndStatus = CreateWindowEx(
		0,
		STATUSCLASSNAME,
		(PCTSTR) NULL,
		SBARS_SIZEGRIP | WS_CHILD | WS_VISIBLE | SBT_TOOLTIPS,
		0, dh - 17, dw, 17,
		hwndParent,
		(HMENU) idStatus,
		hinst,
		NULL);

	GetWindowRect(hwndStatus, &rectDialog);

	SetWindowPos(
		hwndStatus,
		HWND_TOPMOST,
		rectDialog.left,
		rectDialog.top,
		rectDialog.right - rectDialog.left,
		rectDialog.bottom - rectDialog.top,
		SWP_SHOWWINDOW);

	SendMessage(hwndStatus, SB_SETMINHEIGHT, (WPARAM) 17, (LPARAM) 0);

	update_status_bar_panes(hwndStatus);

	return hwndStatus;
}

void win_menu_update()
{
#if 0
        menu = LoadMenu(hThisInstance, TEXT("MainMenu"));

	smenu = LoadMenu(hThisInstance, TEXT("StatusBarMenu"));
        initmenu();

	SetMenu(ghwnd, menu);

	win_title_update = 1;
#endif
}

int recv_key[272];

int WINAPI WinMain (HINSTANCE hThisInstance,
                    HINSTANCE hPrevInstance,
                    LPSTR lpszArgument,
                    int nFunsterStil)

{
        HWND hwnd;               /* This is the handle for our window */
        MSG messages;            /* Here messages to the application are saved */
        WNDCLASSEX wincl;        /* Data structure for the windowclass */
        int c, d, e, bRet;
	WCHAR emulator_title[200];
        LARGE_INTEGER qpc_freq;
        HACCEL haccel;           /* Handle to accelerator table */

	memset(recv_key, 0, sizeof(recv_key));

        process_command_line();

	win_language_load_common_strings();
        
        hinstance=hThisInstance;
        /* The Window structure */
        wincl.hInstance = hThisInstance;
        wincl.lpszClassName = szClassName;
        wincl.lpfnWndProc = WindowProcedure;      /* This function is called by windows */
        wincl.style = CS_DBLCLKS;                 /* Catch double-clicks */
        wincl.cbSize = sizeof (WNDCLASSEX);

        /* Use default icon and mouse-pointer */
        wincl.hIcon = LoadIcon(hinstance, (LPCTSTR) 100);
        wincl.hIconSm = LoadIcon(hinstance, (LPCTSTR) 100);
        wincl.hCursor = NULL;
        wincl.lpszMenuName = NULL;                 /* No menu */
        wincl.cbClsExtra = 0;                      /* No extra bytes after the window class */
        wincl.cbWndExtra = 0;                      /* structure or the window instance */
        /* Use Windows's default color as the background of the window */
        wincl.hbrBackground = (HBRUSH) COLOR_BACKGROUND;

        /* Register the window class, and if it fails quit the program */
        if (!RegisterClassEx(&wincl))
                return 0;

        wincl.lpszClassName = szSubClassName;
        wincl.lpfnWndProc = subWindowProcedure;      /* This function is called by windows */

        if (!RegisterClassEx(&wincl))
                return 0;

        menu = LoadMenu(hThisInstance, TEXT("MainMenu"));
        
		_swprintf(emulator_title, L"86Box v%s", emulator_version_w);

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
                fatal("haccel is null\n");

        memset(rawinputkey, 0, sizeof(rawinputkey));
	device.usUsagePage = 0x01;
	device.usUsage = 0x06;
	device.dwFlags = RIDEV_NOHOTKEYS;
	device.hwndTarget = hwnd;
	
	if (RegisterRawInputDevices(&device, 1, sizeof(device)))
		pclog("Raw input registered!\n");
	else
		pclog("Raw input registration failed!\n");

	get_registry_key_map();

        ghwnd=hwnd;

        initpc(argc, argv);

	hwndRender = CreateWindow(L"STATIC", NULL, WS_VISIBLE | WS_CHILD | SS_BITMAP, 0, 0, 1, 1, ghwnd, NULL, hinstance, NULL);

	hwndStatus = EmulatorStatusBar(hwnd, IDC_STATUS, hThisInstance);

	OriginalStatusBarProcedure = GetWindowLong(hwndStatus, GWL_WNDPROC);
	SetWindowLong(hwndStatus, GWL_WNDPROC, (LONG) &StatusBarProcedure);

	smenu = LoadMenu(hThisInstance, TEXT("StatusBarMenu"));
        initmenu();

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

        if (vid_resize) SetWindowLong(hwnd, GWL_STYLE, WS_OVERLAPPEDWINDOW|WS_VISIBLE);
        else            SetWindowLong(hwnd, GWL_STYLE, (WS_OVERLAPPEDWINDOW&~WS_SIZEBOX&~WS_THICKFRAME&~WS_MAXIMIZEBOX)|WS_VISIBLE);

	SetWindowLong(hwnd, GWL_STYLE, GetWindowLong(hwnd, GWL_STYLE) & ~WS_MINIMIZEBOX);

        /* Note by Kiririn: I've redone this since the CD-ROM can be disabled but still have something inside it. */
	for (e = 0; e < CDROM_NUM; e++)
	{
		if (!cdrom_drives[e].sound_on)
		{
			CheckMenuItem(smenu, IDM_CDROM_1_MUTE + e, MF_CHECKED);
		}

		if (cdrom_drives[e].host_drive == 200)
		{
			CheckMenuItem(smenu, IDM_CDROM_1_IMAGE + e, MF_CHECKED);
		}
		else if (cdrom_drives[e].host_drive >= 65)
		{
			CheckMenuItem(smenu, IDM_CDROM_1_REAL + e + (cdrom_drives[e].host_drive << 2), MF_CHECKED);
		}
		else
		{
			CheckMenuItem(smenu, IDM_CDROM_1_EMPTY + e, MF_CHECKED);
		}
	}

#ifdef ENABLE_LOG_TOGGLES
#ifdef ENABLE_BUSLOGIC_LOG
	CheckMenuItem(menu, IDM_LOG_BUSLOGIC, buslogic_do_log ? MF_CHECKED : MF_UNCHECKED);
#endif
#ifdef ENABLE_CDROM_LOG
	CheckMenuItem(menu, IDM_LOG_CDROM, cdrom_do_log ? MF_CHECKED : MF_UNCHECKED);
#endif
#ifdef ENABLE_D86F_LOG
	CheckMenuItem(menu, IDM_LOG_D86F, d86f_do_log ? MF_CHECKED : MF_UNCHECKED);
#endif
#ifdef ENABLE_FDC_LOG
	CheckMenuItem(menu, IDM_LOG_FDC, fdc_do_log ? MF_CHECKED : MF_UNCHECKED);
#endif
#ifdef ENABLE_IDE_LOG
	CheckMenuItem(menu, IDM_LOG_IDE, ide_do_log ? MF_CHECKED : MF_UNCHECKED);
#endif
#ifdef ENABLE_NE2000_LOG
	CheckMenuItem(menu, IDM_LOG_NE2000, ne2000_do_log ? MF_CHECKED : MF_UNCHECKED);
#endif
#endif

	CheckMenuItem(menu, IDM_VID_FORCE43, force_43 ? MF_CHECKED : MF_UNCHECKED);
	CheckMenuItem(menu, IDM_VID_OVERSCAN, enable_overscan ? MF_CHECKED : MF_UNCHECKED);
	CheckMenuItem(menu, IDM_VID_INVERT, invert_display ? MF_CHECKED : MF_UNCHECKED);

        if (vid_resize) CheckMenuItem(menu, IDM_VID_RESIZE, MF_CHECKED);
        CheckMenuItem(menu, IDM_VID_DDRAW + vid_api, MF_CHECKED);
        CheckMenuItem(menu, IDM_VID_FS_FULL + video_fullscreen_scale, MF_CHECKED);
        CheckMenuItem(menu, IDM_VID_REMEMBER, window_remember ? MF_CHECKED : MF_UNCHECKED);
	CheckMenuItem(menu, IDM_VID_SCALE_1X + scale, MF_CHECKED);

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
		msgbox_critical(ghwnd, 2062);
                return 0;
        }

        romset=d;
        c=loadbios();

        if (!c)
        {
                if (romset!=-1)
		{
			msgbox_info(ghwnd, 2063);
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
                gfx_present[c] = video_card_available(video_old_to_new(c));

        if (!video_card_available(video_old_to_new(gfxcard)))
        {
                if (romset!=-1)
		{
			msgbox_info(ghwnd, 2064);
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

		         if ((recv_key[0x1D] || recv_key[0x9D]) && 
		             (recv_key[0x38] || recv_key[0xB8]) && 
		             (recv_key[0x51] || recv_key[0xD1]) &&
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

LRESULT CALLBACK LowLevelKeyboardProc( int nCode, WPARAM wParam, LPARAM lParam )
{
	BOOL bControlKeyDown;
	KBDLLHOOKSTRUCT* p;

        if (nCode < 0 || nCode != HC_ACTION)
                return CallNextHookEx( hKeyboardHook, nCode, wParam, lParam); 
	
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

int ide_ter_set_irq(HMENU hmenu, int irq, int id)
{
	if (ide_irq[2] == irq)
	{
		return 0;
	}
        if (msgbox_reset_yn(ghwnd) != IDYES)
	{
		return 0;
	}
	pause = 1;
	Sleep(100);
	ide_irq[2] = irq;
	CheckMenuItem(hmenu, IDM_IDE_TER_IRQ9, MF_UNCHECKED);
	CheckMenuItem(hmenu, IDM_IDE_TER_IRQ10, MF_UNCHECKED);
	CheckMenuItem(hmenu, IDM_IDE_TER_IRQ11, MF_UNCHECKED);
	CheckMenuItem(hmenu, IDM_IDE_TER_IRQ12, MF_UNCHECKED);
	CheckMenuItem(hmenu, IDM_IDE_TER_IRQ14, MF_UNCHECKED);
	CheckMenuItem(hmenu, IDM_IDE_TER_IRQ15, MF_UNCHECKED);
	CheckMenuItem(hmenu, id, MF_CHECKED);
	saveconfig();
	resetpchard();
	pause = 0;
	return 1;
}

int ide_qua_set_irq(HMENU hmenu, int irq, int id)
{
	if (ide_irq[3] == irq)
	{
		return 0;
	}
        if (msgbox_reset_yn(ghwnd) != IDYES)
	{
		return 0;
	}
	pause = 1;
	Sleep(100);
	ide_irq[3] = irq;
	CheckMenuItem(hmenu, IDM_IDE_QUA_IRQ9, MF_UNCHECKED);
	CheckMenuItem(hmenu, IDM_IDE_QUA_IRQ10, MF_UNCHECKED);
	CheckMenuItem(hmenu, IDM_IDE_QUA_IRQ11, MF_UNCHECKED);
	CheckMenuItem(hmenu, IDM_IDE_QUA_IRQ12, MF_UNCHECKED);
	CheckMenuItem(hmenu, IDM_IDE_QUA_IRQ14, MF_UNCHECKED);
	CheckMenuItem(hmenu, IDM_IDE_QUA_IRQ15, MF_UNCHECKED);
	CheckMenuItem(hmenu, id, MF_CHECKED);
	saveconfig();
	resetpchard();
	pause = 0;
	return 1;
}

void video_toggle_option(HMENU hmenu, int *val, int id)
{
	*val ^= 1;
	CheckMenuItem(hmenu, id, *val ? MF_CHECKED : MF_UNCHECKED);
	saveconfig();
}

void win_cdrom_eject(uint8_t id)
{
        HMENU hmenu;
	hmenu = GetSubMenu(smenu, id + 4);
	if (cdrom_drives[id].host_drive == 0)
	{
		/* Switch from empty to empty. Do nothing. */
		return;
	}
	cdrom_drives[id].handler->exit(id);
	cdrom_close(id);
	cdrom_null_open(id, 0);
	if (cdrom_drives[id].enabled)
	{
		/* Signal disc change to the emulated machine. */
		cdrom_insert(id);
	}
	if (cdrom_drives[id].host_drive == 200)
	{
		CheckMenuItem(hmenu, IDM_CDROM_1_IMAGE + id,		           MF_UNCHECKED);
	}
	else
	{
		CheckMenuItem(hmenu, IDM_CDROM_1_REAL + id + (cdrom_drive << 2), MF_UNCHECKED);
	}
	cdrom_drives[id].prev_host_drive = cdrom_drives[id].host_drive;
	cdrom_drives[id].host_drive=0;
	CheckMenuItem(hmenu, IDM_CDROM_1_EMPTY + id, MF_CHECKED);
	update_status_bar_icon_state(0x10 | id, get_cd_state(id));
	update_tip(0x10 | id);
	saveconfig();
}

void win_cdrom_reload(uint8_t id)
{
        HMENU hmenu;
	hmenu = GetSubMenu(smenu, id + 4);
	int new_cdrom_drive;
	if ((cdrom_drives[id].host_drive == cdrom_drives[id].prev_host_drive) || (cdrom_drives[id].prev_host_drive == 0) || (cdrom_drives[id].host_drive != 0))
	{
		/* Switch from empty to empty. Do nothing. */
		return;
	}
	cdrom_close(id);
	if (cdrom_drives[id].prev_host_drive == 200)
	{
		image_open(id, cdrom_image[id].image_path);
		if (cdrom_drives[id].enabled)
		{
			/* Signal disc change to the emulated machine. */
			cdrom_insert(id);
		}
		CheckMenuItem(hmenu, IDM_CDROM_1_EMPTY + id,		           MF_UNCHECKED);
		cdrom_drives[id].host_drive = 200;
		CheckMenuItem(hmenu, IDM_CDROM_1_IMAGE + id,		           MF_CHECKED);
	}
	else 
	{
		new_cdrom_drive = cdrom_drives[id].prev_host_drive;
		ioctl_open(id, new_cdrom_drive);
		if (cdrom_drives[id].enabled)
		{
			/* Signal disc change to the emulated machine. */
			cdrom_insert(id);
		}
		CheckMenuItem(hmenu, IDM_CDROM_1_EMPTY + id,		           MF_UNCHECKED);
		cdrom_drive = new_cdrom_drive;
		CheckMenuItem(hmenu, IDM_CDROM_1_REAL + id + (cdrom_drives[id].host_drive << 2), MF_CHECKED);
	}
	update_status_bar_icon_state(0x10 | id, get_cd_state(id));
	update_tip(0x10 | id);
	saveconfig();
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
        DialogBox(hinstance, (LPCTSTR) ABOUTDLG, hwnd, about_dlgproc);
}

LRESULT CALLBACK WindowProcedure (HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam)
{
        HMENU hmenu;
        RECT rect;
	uint32_t ri_size = 0;
	int edgex, edgey;
	int sb_borders[3];

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
                        case IDM_FILE_HRESET:
                        pause=1;
                        Sleep(100);
                        savenvr();
			saveconfig();
                        resetpchard();
                        pause=0;
                        break;
                        case IDM_FILE_RESET_CAD:
                        pause=1;
                        Sleep(100);
                        savenvr();
			saveconfig();
                        resetpc_cad();
                        pause=0;
                        break;
                        case IDM_FILE_EXIT:
                        PostQuitMessage (0);       /* send a WM_QUIT to the message queue */
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
                        vid_resize=!vid_resize;
                        CheckMenuItem(hmenu, IDM_VID_RESIZE, (vid_resize)?MF_CHECKED:MF_UNCHECKED);
                        if (vid_resize) SetWindowLong(hwnd, GWL_STYLE, WS_OVERLAPPEDWINDOW|WS_VISIBLE);
                        else            SetWindowLong(hwnd, GWL_STYLE, (WS_OVERLAPPEDWINDOW&~WS_SIZEBOX&~WS_THICKFRAME&~WS_MAXIMIZEBOX)|WS_VISIBLE);
			SetWindowLong(hwnd, GWL_STYLE, GetWindowLong(hwnd, GWL_STYLE) & ~WS_MINIMIZEBOX);
                        GetWindowRect(hwnd,&rect);
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

                        case IDM_VID_DDRAW: case IDM_VID_D3D:
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
                        break;

                        case IDM_VID_FULLSCREEN:
                        if (video_fullscreen_first)
                        {
                                video_fullscreen_first = 0;
				msgbox_info(ghwnd, 2193);
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
                        device_force_redraw();
                        break;

                        case IDM_VID_FS_FULL:
                        case IDM_VID_FS_43:
                        case IDM_VID_FS_SQ:                                
                        case IDM_VID_FS_INT:
                        CheckMenuItem(hmenu, IDM_VID_FS_FULL + video_fullscreen_scale, MF_UNCHECKED);
                        video_fullscreen_scale = LOWORD(wParam) - IDM_VID_FS_FULL;
                        CheckMenuItem(hmenu, IDM_VID_FS_FULL + video_fullscreen_scale, MF_CHECKED);
                        saveconfig();
                        break;

                        case IDM_VID_SCALE_1X:
                        case IDM_VID_SCALE_2X:
                        case IDM_VID_SCALE_3X:
                        case IDM_VID_SCALE_4X:
                        CheckMenuItem(hmenu, IDM_VID_SCALE_1X + scale, MF_UNCHECKED);
			scale = LOWORD(wParam) - IDM_VID_SCALE_1X;
                        CheckMenuItem(hmenu, IDM_VID_SCALE_1X + scale, MF_CHECKED);
                        saveconfig();
			break;

			case IDM_VID_FORCE43:
			video_toggle_option(hmenu, &force_43, IDM_VID_FORCE43);
			break;

			case IDM_VID_INVERT:
			video_toggle_option(hmenu, &invert_display, IDM_VID_INVERT);
			break;

			case IDM_VID_OVERSCAN:
			video_toggle_option(hmenu, &enable_overscan, IDM_VID_OVERSCAN);
			update_overscan = 1;
			break;

			case IDM_VID_FLASH:
			video_toggle_option(hmenu, &enable_flash, IDM_VID_FLASH);
			break;

			case IDM_VID_SCREENSHOT:
			take_screenshot();
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

#ifdef ENABLE_NE2000_LOG
			case IDM_LOG_NE2000:
			ne2000_do_log ^= 1;
			CheckMenuItem(hmenu, IDM_LOG_NE2000, ne2000_do_log ? MF_CHECKED : MF_UNCHECKED);
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
                        if (!file_dlg_st(hwnd, 2174, "", 0))
                        {
                                if (msgbox_reset_yn(ghwnd) == IDYES)
                                {
                                        config_save(config_file_default);
                                        loadconfig(wopenfilestring);
					pclog_w(L"NVR path: %s\n", nvr_path);
                                        mem_resize();
                                        loadbios();
                                        resetpchard();
                                }
                        }
                        pause = 0;
                        break;                        
                        
                        case IDM_CONFIG_SAVE:
                        pause = 1;
                        if (!file_dlg_st(hwnd, 2174, "", 1))
                                config_save(wopenfilestring);
                        pause = 0;
                        break;                                                
                }
                return 0;
                
		case WM_INPUT:
                {
                        UINT size;
                        RAWINPUT *raw;

                        if (!infocus)
                                break;

                        GetRawInputData((HRAWINPUT)lParam, RID_INPUT, NULL, &size, sizeof(RAWINPUTHEADER));

                        raw = malloc(size);

			if (raw == NULL)
			{
				return 0;
			}

        		/* Here we read the raw input data for the keyboard */
        		ri_size = GetRawInputData((HRAWINPUT)(lParam), RID_INPUT, raw, &size, sizeof(RAWINPUTHEADER));

			if(ri_size != size)
			{
				return 0;
			}

        		/* If the input is keyboard, we process it */
        		if (raw->header.dwType == RIM_TYPEKEYBOARD)
        		{
        			const RAWKEYBOARD rawKB = raw->data.keyboard;
                                USHORT scancode = rawKB.MakeCode;

        			/* If it's not a scan code that starts with 0xE1 */
        			if (!(rawKB.Flags & RI_KEY_E1))
        			{
        				if (rawKB.Flags & RI_KEY_E0)
                                                scancode |= (0xE0 << 8);

        				/* Remap it according to the list from the Registry */
        				scancode = scancode_map[scancode];

        				if ((scancode >> 8) == 0xF0)
        					scancode |= 0x100; /* Extended key code in disambiguated format */
        				else if ((scancode >> 8) == 0xE0)
        					scancode |= 0x80; /* Normal extended key code */

        				/* If it's not 0 (therefore not 0xE1, 0xE2, etc),
        				   then pass it on to the rawinputkey array */
        				if (!(scancode & 0xf00))
					{
                                                rawinputkey[scancode & 0x1ff] = !(rawKB.Flags & RI_KEY_BREAK);
						recv_key[scancode & 0x1ff] = rawinputkey[scancode & 0x1ff];
					}
        			}
				else
				{
					if (rawKB.MakeCode == 0x1D)
						scancode = 0xFF;
        				if (!(scancode & 0xf00))
					{
                                                rawinputkey[scancode & 0x1ff] = !(rawKB.Flags & RI_KEY_BREAK);
						recv_key[scancode & 0x1ff] = rawinputkey[scancode & 0x1ff];
					}
				}
                        }
                        free(raw);

		}
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
                memset(rawinputkey, 0, sizeof(rawinputkey));
                if (video_fullscreen)
                        leave_fullscreen_flag = 1;
		if (hook_enabled)
		{
			UnhookWindowsHookEx(hKeyboardHook);
			hook_enabled = 0;
		}
                break;

                case WM_LBUTTONUP:
                if (!mousecapture && !video_fullscreen)
                {
                        RECT pcclip;

                        GetClipCursor(&oldclip);
                        GetWindowRect(hwnd, &pcclip);
                        pcclip.left   += GetSystemMetrics(SM_CXFIXEDFRAME) + 10;
                        pcclip.right  -= GetSystemMetrics(SM_CXFIXEDFRAME) + 10;
                        pcclip.top    += GetSystemMetrics(SM_CXFIXEDFRAME) + GetSystemMetrics(SM_CYMENUSIZE) + GetSystemMetrics(SM_CYCAPTION) + 10;
                        pcclip.bottom -= GetSystemMetrics(SM_CXFIXEDFRAME) + 10;
                        ClipCursor(&pcclip);
                        mousecapture = 1;
                        while (1)
                        {
                                if (ShowCursor(FALSE) < 0) break;
                        }
                }
                break;

                case WM_MBUTTONUP:
                if (!(mouse_get_type(mouse_type) & MOUSE_TYPE_3BUTTON))
                        releasemouse();
                break;

                case WM_ENTERMENULOOP:
                break;

                case WM_SIZE:
		winsizex = (lParam & 0xFFFF);
		winsizey = (lParam >> 16) - (17 + 6);

		MoveWindow(hwndRender, 0, 0,
			winsizex,
			winsizey,
			TRUE);

                if (vid_apis[video_fullscreen][vid_api].resize)
                {
                        startblit();
                        video_wait_for_blit();
                        vid_apis[video_fullscreen][vid_api].resize(winsizex, winsizey);
                        endblit();
                }

		MoveWindow(hwndStatus, 0, winsizey + 6,
			winsizex,
			17,
			TRUE);

                if (mousecapture)
                {
                        RECT pcclip;

                        GetWindowRect(hwnd, &pcclip);
                        pcclip.left   += GetSystemMetrics(SM_CXFIXEDFRAME) + 10;
                        pcclip.right  -= GetSystemMetrics(SM_CXFIXEDFRAME) + 10;
                        pcclip.top    += GetSystemMetrics(SM_CXFIXEDFRAME) + GetSystemMetrics(SM_CYMENUSIZE) + GetSystemMetrics(SM_CYCAPTION) + 10;
                        pcclip.bottom -= GetSystemMetrics(SM_CXFIXEDFRAME) + 10;
                        ClipCursor(&pcclip);
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
                        onesec();
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
                vid_apis[0][vid_api].init(ghwnd);
                mouse_init();
                endblit();
                device_force_redraw();
                break;

                case WM_KEYDOWN:
                case WM_SYSKEYDOWN:
                case WM_KEYUP:
                case WM_SYSKEYUP:
                   return 0;

                case WM_DESTROY:
                UnhookWindowsHookEx( hKeyboardHook );
                KillTimer(hwnd, TIMER_1SEC);
                PostQuitMessage (0);       /* send a WM_QUIT to the message queue */
                break;

                case WM_SYSCOMMAND:
		/* Disable ALT key *ALWAYS*, I don't think there's any use for reaching the menu that way. */
                if (wParam == SC_KEYMENU && HIWORD(lParam) <= 0)
                        return 0; /*disable ALT key for menu*/

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
	HMENU pmenu;
	int menu_id = -1;
	if (id >= (sb_parts - 1))
	{
		return;
	}
	pt.x = id * sb_icon_width;	/* Justify to the left. */
	pt.y = 0;			/* Justify to the top. */
	ClientToScreen(hwnd, (LPPOINT) &pt);
	if ((sb_part_meanings[id] & 0x30) == 0x00)
	{
		menu_id = sb_part_meanings[id] & 0xf;
	}
	else if ((sb_part_meanings[id] & 0x30) == 0x10)
	{
		menu_id = (sb_part_meanings[id] & 0xf) + 4;
	}
	if (menu_id != -1)
	{
		pmenu = GetSubMenu(smenu, menu_id);
		TrackPopupMenu(pmenu, TPM_LEFTALIGN | TPM_BOTTOMALIGN | TPM_LEFTBUTTON, pt.x, pt.y, 0, hwndStatus, NULL);
	}
}

LRESULT CALLBACK StatusBarProcedure(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam)
{
	RECT rc;
	POINT pt;

	WCHAR temp_image_path[1024];
	int new_cdrom_drive;
	int cdrom_id = 0;
	int menu_sub_param = 0;
	int menu_super_param = 0;

        HMENU hmenu;

        switch (message)
        {
		case WM_COMMAND:
                switch (LOWORD(wParam))
                {
                        case IDM_DISC_1:
                        case IDM_DISC_1_WP:
			if (!file_dlg_w_st(hwnd, 2173, discfns[0], 0))
                        {
                                disc_close(0);
				ui_writeprot[0] = (LOWORD(wParam) == IDM_DISC_1_WP) ? 1 : 0;
                                disc_load(0, wopenfilestring);
				update_status_bar_icon_state(0x00, 0);
				update_tip(0x00);
                                saveconfig();
                        }
                        break;
                        case IDM_DISC_2:
                        case IDM_DISC_2_WP:
			if (!file_dlg_w_st(hwnd, 2173, discfns[1], 0))
                        {
                                disc_close(1);
				ui_writeprot[1] = (LOWORD(wParam) == IDM_DISC_2_WP) ? 1 : 0;
                                disc_load(1, wopenfilestring);
				update_status_bar_icon_state(0x01, 0);
				update_tip(0x01);
                                saveconfig();
                        }
                        break;
                        case IDM_DISC_3:
                        case IDM_DISC_3_WP:
			if (!file_dlg_w_st(hwnd, 2173, discfns[2], 0))
                        {
                                disc_close(2);
				ui_writeprot[2] = (LOWORD(wParam) == IDM_DISC_3_WP) ? 1 : 0;
                                disc_load(2, wopenfilestring);
				update_status_bar_icon_state(0x02, 0);
				update_tip(0x02);
                                saveconfig();
                        }
                        break;
                        case IDM_DISC_4:
                        case IDM_DISC_4_WP:
			if (!file_dlg_w_st(hwnd, 2173, discfns[3], 0))
                        {
                                disc_close(3);
				ui_writeprot[3] = (LOWORD(wParam) == IDM_DISC_4_WP) ? 1 : 0;
                                disc_load(3, wopenfilestring);
				update_status_bar_icon_state(0x03, 0);
				update_tip(0x03);
                                saveconfig();
                        }
                        break;
                        case IDM_EJECT_1:
                        disc_close(0);
			update_status_bar_icon_state(0x00, 1);
			update_tip(0x00);
                        saveconfig();
                        break;
                        case IDM_EJECT_2:
                        disc_close(1);
			update_status_bar_icon_state(0x01, 1);
			update_tip(0x01);
                        saveconfig();
                        break;
                        case IDM_EJECT_3:
                        disc_close(2);
			update_status_bar_icon_state(0x02, 1);
			update_tip(0x02);
                        saveconfig();
                        break;
                        case IDM_EJECT_4:
                        disc_close(3);
			update_status_bar_icon_state(0x03, 1);
			update_tip(0x03);
                        saveconfig();
                        break;

			case IDM_CDROM_1_MUTE:
			case IDM_CDROM_2_MUTE:
			case IDM_CDROM_3_MUTE:
			case IDM_CDROM_4_MUTE:
			cdrom_id = LOWORD(wParam) & 3;
			hmenu = GetSubMenu(smenu, cdrom_id + 4);
			Sleep(100);
			cdrom_drives[cdrom_id].sound_on ^= 1;                                             
			CheckMenuItem(hmenu, IDM_CDROM_1_MUTE + (cdrom_id * 1000), cdrom_drives[cdrom_id].sound_on ? MF_UNCHECKED : MF_CHECKED);
			saveconfig();
			sound_cd_thread_reset();
			break;

			case IDM_CDROM_1_EMPTY:
			case IDM_CDROM_2_EMPTY:
			case IDM_CDROM_3_EMPTY:
			case IDM_CDROM_4_EMPTY:
			cdrom_id = LOWORD(wParam) & 3;
			hmenu = GetSubMenu(smenu, cdrom_id + 4);
			win_cdrom_eject(cdrom_id);
                        break;

			case IDM_CDROM_1_RELOAD:
			case IDM_CDROM_2_RELOAD:
			case IDM_CDROM_3_RELOAD:
			case IDM_CDROM_4_RELOAD:
			cdrom_id = LOWORD(wParam) & 3;
			hmenu = GetSubMenu(smenu, cdrom_id + 4);
			win_cdrom_reload(cdrom_id);
			break;

			case IDM_CDROM_1_IMAGE:
			case IDM_CDROM_2_IMAGE:
			case IDM_CDROM_3_IMAGE:
			case IDM_CDROM_4_IMAGE:
			cdrom_id = LOWORD(wParam) & 3;
			hmenu = GetSubMenu(smenu, cdrom_id + 4);
                        if (!file_dlg_w_st(hwnd, 2175, cdrom_image[cdrom_id].image_path, 0))
                        {
				cdrom_drives[cdrom_id].prev_host_drive = cdrom_drives[cdrom_id].host_drive;
				wcscpy(temp_image_path, wopenfilestring);
				if ((wcscmp(cdrom_image[cdrom_id].image_path, temp_image_path) == 0) && (cdrom_drives[cdrom_id].host_drive == 200))
				{
					/* Switching from image to the same image. Do nothing. */
					break;
				}
				cdrom_drives[cdrom_id].handler->exit(cdrom_id);
				cdrom_close(cdrom_id);
				image_open(cdrom_id, temp_image_path);
				if (cdrom_drives[cdrom_id].enabled)
				{
					/* Signal disc change to the emulated machine. */
					cdrom_insert(cdrom_id);
				}
                                CheckMenuItem(hmenu, IDM_CDROM_1_EMPTY + cdrom_id,           MF_UNCHECKED);
				if ((cdrom_drives[cdrom_id].host_drive != 0) && (cdrom_drives[cdrom_id].host_drive != 200))
				{
	                                CheckMenuItem(hmenu, IDM_CDROM_1_REAL + cdrom_id + (cdrom_drives[cdrom_id].host_drive << 2), MF_UNCHECKED);
				}
				cdrom_drives[cdrom_id].host_drive = 200;
                                CheckMenuItem(hmenu, IDM_CDROM_1_IMAGE + cdrom_id,		           MF_CHECKED);
				update_tip(0x10 | cdrom_id);
                                saveconfig();
                        }
			break;

                        default:
			cdrom_id = LOWORD(wParam) & 3;
			hmenu = GetSubMenu(smenu, cdrom_id + 4);
			menu_sub_param = ((LOWORD(wParam) - IDM_CDROM_1_REAL) - cdrom_id) >> 2;
			/* pclog("[%04X] Guest drive %c [%i]: -> Host drive %c [%i]:\n", LOWORD(wParam), 0x4b + cdrom_id, cdrom_id, menu_sub_param, menu_sub_param); */
			if (((LOWORD(wParam) & ~3) >= (IDM_CDROM_1_REAL + ('A' << 2))) && ((LOWORD(wParam) & ~3) <= (IDM_CDROM_1_REAL + ('Z' << 2))))
                        {
				new_cdrom_drive = menu_sub_param;
				if (cdrom_drives[cdrom_id].host_drive == new_cdrom_drive)
				{
					/* Switching to the same drive. Do nothing. */
					break;
				}
				cdrom_drives[cdrom_id].prev_host_drive = cdrom_drives[cdrom_id].host_drive;
				cdrom_drives[cdrom_id].handler->exit(cdrom_id);
				cdrom_close(cdrom_id);
                                ioctl_open(cdrom_id, new_cdrom_drive);
				if (cdrom_drives[cdrom_id].enabled)
				{
					/* Signal disc change to the emulated machine. */
					cdrom_insert(cdrom_id);
				}
                                CheckMenuItem(hmenu, IDM_CDROM_1_EMPTY + cdrom_id,           MF_UNCHECKED);
				if ((cdrom_drives[cdrom_id].host_drive != 0) && (cdrom_drives[cdrom_id].host_drive != 200))
				{
	                                CheckMenuItem(hmenu, IDM_CDROM_1_REAL + cdrom_id + (cdrom_drives[cdrom_id].host_drive << 2), MF_UNCHECKED);
				}
                                CheckMenuItem(hmenu, IDM_CDROM_1_IMAGE + cdrom_id,		           MF_UNCHECKED);
                                cdrom_drives[cdrom_id].host_drive = new_cdrom_drive;
                                CheckMenuItem(hmenu, IDM_CDROM_1_REAL + cdrom_id + (cdrom_drives[cdrom_id].host_drive << 2), MF_CHECKED);
				update_tip(0x10 | cdrom_id);
                                saveconfig();
                        }
                        break;
		}
		return 0;

		case WM_LBUTTONDOWN:
		GetClientRect(hwnd, (LPRECT)& rc);
		pt.x = GET_X_LPARAM(lParam);
		pt.y = GET_Y_LPARAM(lParam);
		if (PtInRect((LPRECT) &rc, pt))
		{
			HandlePopupMenu(hwnd, pt, (pt.x / sb_icon_width));
		}
		break;

		default:
                return CallWindowProc((WNDPROC) OriginalStatusBarProcedure, hwnd, message, wParam, lParam);
        }
        return 0;
}
