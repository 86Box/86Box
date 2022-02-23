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
 *
 *
 * Authors:	Sarah Walker, <http://pcem-emulator.co.uk/>
 *		Miran Grca, <mgrca8@gmail.com>
 *		Fred N. van Kempen, <decwiz@yahoo.com>
 *
 *		Copyright 2008-2019 Sarah Walker.
 *		Copyright 2016-2019 Miran Grca.
 *		Copyright 2017-2019 Fred N. van Kempen.
 *		Copyright 2021 Laci bá'
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

/* DPI Awareness Context, copied from MinGW-w64 windef.h */
#ifndef _DPI_AWARENESS_CONTEXTS_
DECLARE_HANDLE(DPI_AWARENESS_CONTEXT);
#define DPI_AWARENESS_CONTEXT_UNAWARE              ((DPI_AWARENESS_CONTEXT)-1)
#define DPI_AWARENESS_CONTEXT_SYSTEM_AWARE         ((DPI_AWARENESS_CONTEXT)-2)
#define DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE    ((DPI_AWARENESS_CONTEXT)-3)
#define DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2 ((DPI_AWARENESS_CONTEXT)-4)
#define DPI_AWARENESS_CONTEXT_UNAWARE_GDISCALED    ((DPI_AWARENESS_CONTEXT)-5)
#endif

#ifndef WM_DPICHANGED_AFTERPARENT
#define WM_DPICHANGED_AFTERPARENT 0x02E3
#endif

/* Class names and such. */
#define CLASS_NAME		L"86BoxMainWnd"
#define MENU_NAME		L"MainMenu"
#define ACCEL_NAME		L"MainAccel"
#define SUB_CLASS_NAME		L"86BoxSubWnd"
#define SB_CLASS_NAME		L"86BoxStatusBar"
#define SB_MENU_NAME		L"StatusBarMenu"
#define FS_CLASS_NAME		L"86BoxFullScreen"
#define SDL_CLASS_NAME		L"86BoxSDLWnd"
#define SDL_SUB_CLASS_NAME	L"86BoxSDLSubWnd"

#define CASSETTE_SUBMENU_NAME	L"CassetteSubmenu"
#define CARTRIDGE_SUBMENU_NAME	L"CartridgeSubmenu"
#define FLOPPY_SUBMENU_NAME	L"FloppySubmenu"
#define CDROM_SUBMENU_NAME	L"CdromSubmenu"
#define ZIP_SUBMENU_NAME	L"ZIPSubmenu"
#define MO_SUBMENU_NAME		L"MOSubmenu"

#define VID_GL_SUBMENU		L"VidGLSubMenu"

/* Application-specific window messages.

   A dialog sends 0x8895 with WPARAM = 1 followed by 0x8896 with WPARAM = 1 on open,
   and 0x8895 with WPARAM = <previous pause status> followed by 0x8896 with WPARAM = 0.

   All shutdowns will send an 0x8897. */
#define WM_LEAVEFULLSCREEN	WM_USER
#define WM_SAVESETTINGS		0x8888
#define WM_SHOWSETTINGS		0x8889
#define WM_PAUSE		0x8890
#define WM_SENDHWND		0x8891
#define WM_HARDRESET		0x8892
#define WM_SHUTDOWN		0x8893
#define WM_CTRLALTDEL		0x8894
/* Pause/resume status: WPARAM = 1 for paused, 0 for resumed. */
#define WM_SENDSTATUS		0x8895
/* Dialog (Settings or message box) status: WPARAM = 1 for open, 0 for closed. */
#define WM_SENDDLGSTATUS	0x8896
/* The emulator has shut down. */
#define WM_HAS_SHUTDOWN		0x8897

#ifdef USE_VNC
#define RENDERERS_NUM		5
#else
#define RENDERERS_NUM		4
#endif


#ifdef __cplusplus
extern "C" {
#endif

extern HINSTANCE	hinstance;
extern HWND		hwndMain,
			hwndRender;
extern HANDLE		ghMutex;
extern HICON		hIcon[256];
extern int		dpi;
extern RECT		oldclip;
extern int		sbar_height, tbar_height, user_resize;
extern int		acp_utf8;

// extern int		status_is_open;

extern char		openfilestring[512];
extern WCHAR		wopenfilestring[512];

extern uint8_t		filterindex;


extern void	ResizeWindowByClientArea(HWND hwnd, int width, int height);
extern void	InitCrashDump(void);

/* Emulator start/stop support functions. */
extern void	do_start(void);
extern void	do_stop(void);

/* Internal platform support functions. */
extern int	has_language_changed(uint32_t id);
extern void	set_language(uint32_t id);
extern int	get_vidpause(void);
extern void	show_cursor(int);

extern void	keyboard_getkeymap(void);
extern void	keyboard_handle(PRAWINPUT raw);

extern void     win_mouse_init(void);
extern void     win_mouse_close(void);
extern void     win_mouse_handle(PRAWINPUT raw);

extern void     win_joystick_handle(PRAWINPUT raw);

extern void     win_notify_dlg_open(void);
extern void     win_notify_dlg_closed(void);
extern int      win_get_dpi(HWND hwnd);
extern int      win_get_system_metrics(int i, int dpi);

extern LPARAM	win_get_string(int id);

extern void     win_clear_icon_set();
extern void     win_system_icon_set();
extern void     win_load_icon_set();
extern void     win_get_icons_path(char* path_root);

extern intptr_t	fdd_type_to_icon(int type);

#ifdef EMU_DEVICE_H
extern uint8_t	deviceconfig_open(HWND hwnd, const device_t *device);
extern uint8_t	deviceconfig_inst_open(HWND hwnd, const device_t *device, int inst);
#endif
extern uint8_t	joystickconfig_open(HWND hwnd, int joy_nr, int type);

extern int	getfile(HWND hwnd, char *f, char *fn);
extern int	getsfile(HWND hwnd, char *f, char *fn);

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


/* Functions in win_specify_dim.c: */
extern void	SpecifyDimensionsDialogCreate(HWND hwnd);

/* Functions in win_preferences.c: */
extern void	PreferencesDlgCreate(HWND hwnd);


/* Functions in win_settings.c: */
#define SETTINGS_PAGE_MACHINE			0
#define SETTINGS_PAGE_VIDEO			1
#define SETTINGS_PAGE_INPUT			2
#define SETTINGS_PAGE_SOUND			3
#define SETTINGS_PAGE_NETWORK			4
#define SETTINGS_PAGE_PORTS			5
#define SETTINGS_PAGE_STORAGE			6
#define SETTINGS_PAGE_HARD_DISKS		7
#define SETTINGS_PAGE_FLOPPY_AND_CDROM_DRIVES	8
#define SETTINGS_PAGE_OTHER_REMOVABLE_DEVICES	9
#define SETTINGS_PAGE_PERIPHERALS		10

extern void	win_settings_open(HWND hwnd);
extern void	win_settings_open_ex(HWND hwnd, int category);


/* Functions in win_stbar.c: */
extern HWND	hwndSBAR;
extern void	StatusBarCreate(HWND hwndParent, uintptr_t idStatus, HINSTANCE hInst);
extern int	MediaMenuHandler(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam);


/* Functions in win_toolbar.c */
extern HWND	hwndRebar;
extern void	ToolBarCreate(HWND hwndParent, HINSTANCE hInst);
extern void	ToolBarLoadIcons();
extern void	ToolBarUpdatePause(int paused);


/* Functions in win_dialog.c: */
/* Pass NULL in the title param to use the default title. */
extern int	file_dlg_w(HWND hwnd, WCHAR *f, WCHAR *fn, WCHAR *title, int save);
extern int	file_dlg(HWND hwnd, WCHAR *f, char *fn, char *title, int save);
extern int	file_dlg_mb(HWND hwnd, char *f, char *fn, char *title, int save);
extern int	file_dlg_w_st(HWND hwnd, int i, WCHAR *fn, char *title, int save);
extern int	file_dlg_st(HWND hwnd, int i, char *fn, char *title, int save);

extern wchar_t	*BrowseFolder(wchar_t *saved_path, wchar_t *title);


/* Functions in win_media_menu.c */
extern void	media_menu_init();
extern void	media_menu_reset();
extern int	media_menu_proc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam);
extern HMENU	media_menu_get_cassette(void);
extern HMENU	media_menu_get_cartridge(int id);
extern HMENU	media_menu_get_floppy(int id);
extern HMENU	media_menu_get_cdrom(int id);
extern HMENU	media_menu_get_zip(int id);
extern HMENU	media_menu_get_mo(int id);
extern void	media_menu_update_cassette(void);
extern void	media_menu_update_cartridge(int id);
extern void	media_menu_update_floppy(int id);
extern void	media_menu_update_cdrom(int id);
extern void	media_menu_update_zip(int id);
extern void	media_menu_update_mo(int id);

/* Functions in win_ui.c */
extern HMENU	menuMain;
extern void	ResetAllMenus();

#ifdef __cplusplus
}
#endif

#endif	/*PLAT_WIN_H*/
