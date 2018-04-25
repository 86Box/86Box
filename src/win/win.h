/*
 * 86Box	A hypervisor and IBM PC system emulator that specializes in
 *		running old operating systems and software designed for IBM
 *		PC systems and compatibles from 1981 through fairly recent
 *		system designs based on the PCI bus.
 *
 *		This file is part of the 86Box distribution.
 *
 *		Platform support defintions for Win32.
 *
 * Version:	@(#)win.h	1.0.16	2018/03/28
 *
 * Authors:	Sarah Walker, <http://pcem-emulator.co.uk/>
 *		Miran Grca, <mgrca8@gmail.com>
 *		Fred N. van Kempen, <decwiz@yahoo.com>
 *
 *		Copyright 2008-2018 Sarah Walker.
 *		Copyright 2016-2018 Miran Grca.
 *		Copyright 2017,2018 Fred N. van Kempen.
 */
#ifndef PLAT_WIN_H
# define PLAT_WIN_H

# define UNICODE
# define BITMAP WINDOWS_BITMAP
# if 0
#  ifdef _WIN32_WINNT
#   undef _WIN32_WINNT
#   define _WIN32_WINNT 0x0501
#  endif
# endif
# include <windows.h>
# include "resource.h"
# undef BITMAP


/* Class names and such. */
#define CLASS_NAME		L"86BoxMainWnd"
#define MENU_NAME		L"MainMenu"
#define ACCEL_NAME		L"MainAccel"
#define SUB_CLASS_NAME		L"86BoxSubWnd"
#define SB_CLASS_NAME		L"86BoxStatusBar"
#define SB_MENU_NAME		L"StatusBarMenu"

/* Application-specific window messages. */
#define WM_RESETD3D		WM_USER
#define WM_LEAVEFULLSCREEN	WM_USER+1
#define WM_SAVESETTINGS		0x8888
#define WM_SHOWSETTINGS		0x8889
#define WM_PAUSE		0x8890
#define WM_SENDHWND		0x8891


extern HINSTANCE	hinstance;
extern HWND		hwndMain,
			hwndRender;
extern HANDLE		ghMutex;
extern LCID		lang_id;
extern HICON		hIcon[512];

extern int		status_is_open;

extern char		openfilestring[260];
extern WCHAR		wopenfilestring[260];

extern uint8_t		filterindex;


#ifdef __cplusplus
extern "C" {
#endif

#ifdef USE_CRASHDUMP
extern void	InitCrashDump(void);
#endif

extern HICON	LoadIconEx(PCTSTR pszIconName);

/* Emulator start/stop support functions. */
extern void	do_start(void);
extern void	do_stop(void);

/* Internal platform support functions. */
extern void	set_language(int id);
extern int	get_vidpause(void);
extern void	show_cursor(int);

extern void	keyboard_getkeymap(void);
extern void	keyboard_handle(LPARAM lParam, int infocus);

extern void     win_mouse_init(void);
extern void     win_mouse_close(void);

extern LPARAM	win_get_string(int id);

extern intptr_t	fdd_type_to_icon(int type);

#ifdef EMU_DEVICE_H
extern uint8_t	deviceconfig_open(HWND hwnd, const device_t *device);
#endif
extern uint8_t	joystickconfig_open(HWND hwnd, int joy_nr, int type);

extern int	getfile(HWND hwnd, char *f, char *fn);
extern int	getsfile(HWND hwnd, char *f, char *fn);

extern void	win_settings_open(HWND hwnd);

extern void	hard_disk_add_open(HWND hwnd, int is_existing);
extern int	hard_disk_was_added(void);


/* Platform UI support functions. */
extern int	ui_init(int nCmdShow);


/* Functions in win_about.c: */
extern void	AboutDialogCreate(HWND hwnd);


/* Functions in win_snd_gain.c: */
extern void	SoundGainDialogCreate(HWND hwnd);


/* Functions in win_new_floppy.c: */
extern void	NewFloppyDialogCreate(HWND hwnd, int id, int part);


/* Functions in win_status.c: */
extern HWND	hwndStatus;
extern void	StatusWindowCreate(HWND hwnd);


/* Functions in win_stbar.c: */
extern HWND	hwndSBAR;
extern void	StatusBarCreate(HWND hwndParent, uintptr_t idStatus, HINSTANCE hInst);


/* Functions in win_dialog.c: */
extern int	file_dlg_w(HWND hwnd, WCHAR *f, WCHAR *fn, int save);
extern int	file_dlg(HWND hwnd, WCHAR *f, char *fn, int save);
extern int	file_dlg_mb(HWND hwnd, char *f, char *fn, int save);
extern int	file_dlg_w_st(HWND hwnd, int i, WCHAR *fn, int save);
extern int	file_dlg_st(HWND hwnd, int i, char *fn, int save);

extern wchar_t	*BrowseFolder(wchar_t *saved_path, wchar_t *title);


#ifdef __cplusplus
}
#endif


#endif	/*PLAT_WIN_H*/
