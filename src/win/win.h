/*
 * 86Box	A hypervisor and IBM PC system emulator that specializes in
 *		running old operating systems and software designed for IBM
 *		PC systems and compatibles from 1981 through fairly recent
 *		system designs based on the PCI bus.
 *
 *		This file is part of the 86Box distribution.
 *
 *		This file should contain things only used by the platform
 *		support modules for Windows.  Generic definitions for UI and
 *		platform go into ../plat*.h.
 *
 * Version:	@(#)win.h	1.0.4	2017/10/09
 *
 * Authors:	Sarah Walker, <http://pcem-emulator.co.uk/>
 *		Miran Grca, <mgrca8@gmail.com>
 *		Fred N. van Kempen, <decwiz@yahoo.com>
 *
 *		Copyright 2008-2017 Sarah Walker.
 *		Copyright 2016,2017 Miran Grca.
 *		Copyright 2017 Fred N. van Kempen.
 */
#ifndef BOX_WIN_H
# define BOX_WIN_H

# ifndef NO_UNICODE
#  define UNICODE
# endif
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
#define RENDER_NAME		L"RenderWindow"

/* Application-specific window messages. */
#define WM_RESETD3D		WM_USER
#define WM_LEAVEFULLSCREEN	WM_USER+1
#define WM_SAVESETTINGS		0x8888


extern int		pause;
extern int		status_is_open;
extern int		mousecapture;
extern LCID		dwLanguage;

extern HINSTANCE	hinstance;
extern HWND		hwndMain;
extern HICON		hIcon[512];

extern char		openfilestring[260];
extern WCHAR		wopenfilestring[260];


#ifdef __cplusplus
extern "C" {
#endif

extern HICON	LoadIconEx(PCTSTR pszIconName);

extern void	win_language_set(void);
extern void	win_language_update(void);
extern void	win_language_check(void);

#ifdef EMU_DEVICE_H
extern void	deviceconfig_open(HWND hwnd, device_t *device);
#endif
extern void	joystickconfig_open(HWND hwnd, int joy_nr, int type);

extern int	getfile(HWND hwnd, char *f, char *fn);
extern int	getsfile(HWND hwnd, char *f, char *fn);

extern void	win_settings_open(HWND hwnd);

extern void	hard_disk_add_open(HWND hwnd, int is_existing);
extern int	hard_disk_was_added(void);

extern void	get_registry_key_map(void);
extern void	process_raw_input(LPARAM lParam, int infocus);

extern int	fdd_type_to_icon(int type);


/* Functions in win_about.c: */
extern void	AboutDialogCreate(HWND hwnd);


/* Functions in win_status.c: */
extern HWND	hwndStatus;
extern void	StatusWindowCreate(HWND hwnd);


/* Functions in win_stbar.c: */
extern HWND	hwndSBAR;
extern void	StatusBarCreate(HWND hwndParent, int idStatus, HINSTANCE hInst);


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


#endif	/*BOX_WIN_H*/
