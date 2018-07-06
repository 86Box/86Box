/*
 * 86Box	A hypervisor and IBM PC system emulator that specializes in
 *		running old operating systems and software designed for IBM
 *		PC systems and compatibles from 1981 through fairly recent
 *		system designs based on the PCI bus.
 *
 *		This file is part of the 86Box distribution.
 *
 *		Platform main support module for Windows.
 *
 * Version:	@(#)win.c	1.0.49	2018/05/25
 *
 * Authors:	Sarah Walker, <http://pcem-emulator.co.uk/>
 *		Miran Grca, <mgrca8@gmail.com>
 *		Fred N. van Kempen, <decwiz@yahoo.com>
 *
 *		Copyright 2008-2018 Sarah Walker.
 *		Copyright 2016-2018 Miran Grca.
 *		Copyright 2017,2018 Fred N. van Kempen.
 */
#define UNICODE
#include <windows.h>
#include <fcntl.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <time.h>
#include <wchar.h>
#define HAVE_STDARG_H
#include "../86box.h"
#include "../config.h"
#include "../device.h"
#include "../mouse.h"
#include "../video/video.h"
#define GLOBAL
#include "../plat.h"
#include "../plat_midi.h"
#include "../ui.h"
#ifdef USE_VNC
# include "../vnc.h"
#endif
# include "win_ddraw.h"
# include "win_d3d.h"
# include "win_sdl.h"
#include "win.h"


typedef struct {
    WCHAR str[512];
} rc_str_t;


/* Platform Public data, specific. */
HINSTANCE	hinstance;		/* application instance */
HANDLE		ghMutex;
LCID		lang_id;		/* current language ID used */
DWORD		dwSubLangID;


/* Local data. */
static HANDLE	thMain;
static rc_str_t	*lpRCstr2048,
		*lpRCstr4096,
		*lpRCstr4352,
		*lpRCstr4608,
		*lpRCstr5120,
		*lpRCstr5376,
		*lpRCstr5632,
		*lpRCstr5888,
		*lpRCstr6144,
		*lpRCstr7168;
static int	vid_api_inited = 0;


static struct {
    char	*name;
    int		local;
    int		(*init)(void *);
    void	(*close)(void);
    void	(*resize)(int x, int y);
    int		(*pause)(void);
} vid_apis[2][RENDERERS_NUM] = {
  {
    {	"DDraw", 1, (int(*)(void*))ddraw_init, ddraw_close, NULL, ddraw_pause		},
    {	"D3D", 1, (int(*)(void*))d3d_init, d3d_close, d3d_resize, d3d_pause		},
#ifdef USE_VNC
    {	"SDL", 1, (int(*)(void*))sdl_init, sdl_close, NULL, sdl_pause			},
    {	"VNC", 0, vnc_init, vnc_close, vnc_resize, vnc_pause				}
#else
    {	"SDL", 1, (int(*)(void*))sdl_init, sdl_close, NULL, sdl_pause			}
#endif
  },
  {
    {	"DDraw", 1, (int(*)(void*))ddraw_init_fs, ddraw_close, NULL, ddraw_pause	},
    {	"D3D", 1, (int(*)(void*))d3d_init_fs, d3d_close, NULL, d3d_pause		},
#ifdef USE_VNC
    {	"SDL", 1, (int(*)(void*))sdl_init_fs, sdl_close, NULL, sdl_pause		},
    {	"VNC", 0, vnc_init, vnc_close, vnc_resize, vnc_pause				}
#else
    {	"SDL", 1, (int(*)(void*))sdl_init_fs, sdl_close, sdl_resize, sdl_pause		}
#endif
  },
};


#ifdef ENABLE_WIN_LOG
int win_do_log = ENABLE_WIN_LOG;
#endif


static void
win_log(const char *fmt, ...)
{
#ifdef ENABLE_WIN_LOG
    va_list ap;

    if (win_do_log) {
	va_start(ap, fmt);
	pclog_ex(fmt, ap);
	va_end(ap);
    }
#endif
}


static void
LoadCommonStrings(void)
{
    int i;

    lpRCstr2048 = (rc_str_t *)malloc(STR_NUM_2048*sizeof(rc_str_t));
    lpRCstr4096 = (rc_str_t *)malloc(STR_NUM_4096*sizeof(rc_str_t));
    lpRCstr4352 = (rc_str_t *)malloc(STR_NUM_4352*sizeof(rc_str_t));
    lpRCstr4608 = (rc_str_t *)malloc(STR_NUM_4608*sizeof(rc_str_t));
    lpRCstr5120 = (rc_str_t *)malloc(STR_NUM_5120*sizeof(rc_str_t));
    lpRCstr5376 = (rc_str_t *)malloc(STR_NUM_5376*sizeof(rc_str_t));
    lpRCstr5632 = (rc_str_t *)malloc(STR_NUM_5632*sizeof(rc_str_t));
    lpRCstr5888 = (rc_str_t *)malloc(STR_NUM_5888*sizeof(rc_str_t));
    lpRCstr6144 = (rc_str_t *)malloc(STR_NUM_6144*sizeof(rc_str_t));
    lpRCstr7168 = (rc_str_t *)malloc(STR_NUM_7168*sizeof(rc_str_t));

    for (i=0; i<STR_NUM_2048; i++)
	LoadString(hinstance, 2048+i, lpRCstr2048[i].str, 512);

    for (i=0; i<STR_NUM_4096; i++)
	LoadString(hinstance, 4096+i, lpRCstr4096[i].str, 512);

    for (i=0; i<STR_NUM_4352; i++)
	LoadString(hinstance, 4352+i, lpRCstr4352[i].str, 512);

    for (i=0; i<STR_NUM_4608; i++)
	LoadString(hinstance, 4608+i, lpRCstr4608[i].str, 512);

    for (i=0; i<STR_NUM_5120; i++)
	LoadString(hinstance, 5120+i, lpRCstr5120[i].str, 512);

    for (i=0; i<STR_NUM_5376; i++) {
	if ((i == 0) || (i > 3))
		LoadString(hinstance, 5376+i, lpRCstr5376[i].str, 512);
    }

    for (i=0; i<STR_NUM_5632; i++) {
	if ((i == 0) || (i > 3))
		LoadString(hinstance, 5632+i, lpRCstr5632[i].str, 512);
    }

    for (i=0; i<STR_NUM_5888; i++)
	LoadString(hinstance, 5888+i, lpRCstr5888[i].str, 512);

    for (i=0; i<STR_NUM_6144; i++)
	LoadString(hinstance, 6144+i, lpRCstr6144[i].str, 512);

    for (i=0; i<STR_NUM_7168; i++)
	LoadString(hinstance, 7168+i, lpRCstr7168[i].str, 512);
}


/* Set (or re-set) the language for the application. */
void
set_language(int id)
{
    LCID lcidNew = MAKELCID(id, dwSubLangID);

    if (lang_id != lcidNew) {
	/* Set our new language ID. */
	lang_id = lcidNew;

	SetThreadLocale(lang_id);

	/* Load the strings table for this ID. */
	LoadCommonStrings();

#if 0
	/* Update the menus for this ID. */
	MenuUpdate();
#endif
    }
}


wchar_t *
plat_get_string(int i)
{
    LPTSTR str;

    if ((i >= 2048) && (i <= 3071))
	str = lpRCstr2048[i-2048].str;
    else if ((i >= 4096) && (i <= 4351))
	str = lpRCstr4096[i-4096].str;
    else if ((i >= 4352) && (i <= 4607))
	str = lpRCstr4352[i-4352].str;
    else if ((i >= 4608) && (i <= 5119))
	str = lpRCstr4608[i-4608].str;
    else if ((i >= 5120) && (i <= 5375))
	str = lpRCstr5120[i-5120].str;
    else if ((i >= 5376) && (i <= 5631))
	str = lpRCstr5376[i-5376].str;
    else if ((i >= 5632) && (i <= 5887))
	str = lpRCstr5632[i-5632].str;
    else if ((i >= 5888) && (i <= 6143))
	str = lpRCstr5888[i-5888].str;
    else if ((i >= 6144) && (i <= 7167))
	str = lpRCstr6144[i-6144].str;
    else
	str = lpRCstr7168[i-7168].str;

    return((wchar_t *)str);
}


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
	if (! AllocConsole()) {
		/* Cannot create console, just give up. */
		return;
	}
    }
    fp = NULL;
    if ((h = GetStdHandle(STD_OUTPUT_HANDLE)) != NULL) {
	/* We got the handle, now open a file descriptor. */
	if ((i = _open_osfhandle((intptr_t)h, _O_TEXT)) != -1) {
		/* We got a file descriptor, now allocate a new stream. */
		if ((fp = _fdopen(i, "w")) != NULL) {
			/* Got the stream, re-initialize stdout without it. */
			(void)freopen("CONOUT$", "w", stdout);
			setvbuf(stdout, NULL, _IONBF, 0);
			fflush(stdout);
		}
	}
    }
}


/* Process the commandline, and create standard argc/argv array. */
static int
ProcessCommandLine(wchar_t ***argw)
{
    WCHAR *cmdline;
    wchar_t *argbuf;
    wchar_t **args;
    int argc_max;
    int i, q, argc;

    cmdline = GetCommandLine();
    i = wcslen(cmdline) + 1;
    argbuf = (wchar_t *)malloc(sizeof(wchar_t)*i);
    wcscpy(argbuf, cmdline);

    argc = 0;
    argc_max = 64;
    args = (wchar_t **)malloc(sizeof(wchar_t *) * argc_max);
    if (args == NULL) {
	free(argbuf);
	return(0);
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

		args[argc++] = &argbuf[i];

		if (argc >= argc_max) {
			argc_max += 64;
			args = realloc(args, sizeof(wchar_t *)*argc_max);
			if (args == NULL) {
				free(argbuf);
				return(0);
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

    args[argc] = NULL;
    *argw = args;

    return(argc);
}


/* For the Windows platform, this is the start of the application. */
int WINAPI
WinMain(HINSTANCE hInst, HINSTANCE hPrev, LPSTR lpszArg, int nCmdShow)
{
    wchar_t **argw = NULL;
    int	argc, i;

    /* Set this to the default value (windowed mode). */
    video_fullscreen = 0;

    /* We need this later. */
    hinstance = hInst;

    /* Set the application version ID string. */
    sprintf(emu_version, "%s v%s", EMU_NAME, EMU_VERSION);

#ifdef USE_CRASHDUMP
    /* Enable crash dump services. */
    InitCrashDump();
#endif

    /* First, set our (default) language. */
    set_language(0x0409);

    /* Create console window. */
    CreateConsole(1);

    /* Process the command line for options. */
    argc = ProcessCommandLine(&argw);

    /* Pre-initialize the system, this loads the config file. */
    if (! pc_init(argc, argw)) {
	/* Detach from console. */
	CreateConsole(0);
	return(1);
    }

    /* Cleanup: we may no longer need the console. */
    if (! force_debug)
	CreateConsole(0);

    /* Handle our GUI. */
    i = ui_init(nCmdShow);

    return(i);
}


/*
 * We do this here since there is platform-specific stuff
 * going on here, and we do it in a function separate from
 * main() so we can call it from the UI module as well.
 */
void
do_start(void)
{
    LARGE_INTEGER qpc;

    /* We have not stopped yet. */
    quited = 0;

    /* Initialize the high-precision timer. */
    timeBeginPeriod(1);
    QueryPerformanceFrequency(&qpc);
    timer_freq = qpc.QuadPart;
    win_log("Main timer precision: %llu\n", timer_freq);

    /* Start the emulator, really. */
    thMain = thread_create(pc_thread, &quited);
    SetThreadPriority(thMain, THREAD_PRIORITY_HIGHEST);
}


/* Cleanly stop the emulator. */
void
do_stop(void)
{
    quited = 1;

    plat_delay_ms(100);

    pc_close(thMain);

    thMain = NULL;
}


void
plat_get_exe_name(wchar_t *s, int size)
{
    GetModuleFileName(hinstance, s, size);
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


/* Make sure a path ends with a trailing (back)slash. */
void
plat_path_slash(wchar_t *path)
{
    if ((path[wcslen(path)-1] != L'\\') &&
	(path[wcslen(path)-1] != L'/')) {
	wcscat(path, L"\\");
    }
}


/* Check if the given path is absolute or not. */
int
plat_path_abs(wchar_t *path)
{
    if ((path[1] == L':') || (path[0] == L'\\') || (path[0] == L'/'))
	return(1);

    return(0);
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
plat_append_filename(wchar_t *dest, wchar_t *s1, wchar_t *s2)
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


/* Return the VIDAPI number for the given name. */
int
plat_vidapi(char *name)
{
    int i;

    if (!strcasecmp(name, "default") || !strcasecmp(name, "system")) return(0);

    for (i = 0; i < RENDERERS_NUM; i++) {
	if (vid_apis[0][i].name &&
	    !strcasecmp(vid_apis[0][i].name, name)) return(i);
    }

    /* Default value. */
    return(0);
}


/* Return the VIDAPI name for the given number. */
char *
plat_vidapi_name(int api)
{
    char *name = "default";

    switch(api) {
	case 0:
#if 0
		/* DirectDraw is default. */
		name = "ddraw";
#endif
		break;

	case 1:
		name = "d3d";
		break;

	case 2:
		name = "sdl";
		break;

#ifdef USE_VNC
	case 3:
		name = "vnc";
		break;

#endif
    }

    return(name);
}


int
plat_setvid(int api)
{
    int i;

    win_log("Initializing VIDAPI: api=%d\n", api);
    startblit();
    video_wait_for_blit();

    /* Close the (old) API. */
    vid_apis[0][vid_api].close();
    vid_api = api;

    if (vid_apis[0][vid_api].local)
	ShowWindow(hwndRender, SW_SHOW);
      else
	ShowWindow(hwndRender, SW_HIDE);

    /* Initialize the (new) API. */
    i = vid_apis[0][vid_api].init((void *)hwndRender);
    endblit();
    if (! i) return(0);

    device_force_redraw();

    vid_api_inited = 1;

    return(1);
}


/* Tell the renderers about a new screen resolution. */
void
plat_vidsize(int x, int y)
{
    if (!vid_api_inited || !vid_apis[video_fullscreen][vid_api].resize) return;

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
    HWND *hw;

    /* Want off and already off? */
    if (!on && !video_fullscreen) return;

    /* Want on and already on? */
    if (on && video_fullscreen) return;

    if (on && video_fullscreen_first) {
	video_fullscreen_first = 0;
	ui_msgbox(MBX_INFO, (wchar_t *)IDS_2052);
    }

    /* OK, claim the video. */
    startblit();
    video_wait_for_blit();

    win_mouse_close();

    /* Close the current mode, and open the new one. */
    vid_apis[video_fullscreen][vid_api].close();
    video_fullscreen = on;
    hw = (video_fullscreen) ? &hwndMain : &hwndRender;
    vid_apis[video_fullscreen][vid_api].init((void *) *hw);

    win_mouse_init();

    /* Release video and make it redraw the screen. */
    endblit();
    device_force_redraw();

    /* Finally, handle the host's mouse cursor. */
    /* win_log("%s full screen, %s cursor\n", on ? "enter" : "leave", on ? "hide" : "show"); */
    show_cursor(video_fullscreen ? 0 : -1);
}


void
take_screenshot(void)
{
    wchar_t path[1024], fn[128];
    struct tm *info;
    time_t now;

    win_log("Screenshot: video API is: %i\n", vid_api);
    if ((vid_api < 0) || (vid_api > 2)) return;

    memset(fn, 0, sizeof(fn));
    memset(path, 0, sizeof(path));

    (void)time(&now);
    info = localtime(&now);

    plat_append_filename(path, usr_path, SCREENSHOT_PATH);

    if (! plat_dir_check(path))
	plat_dir_create(path);

    wcscat(path, L"\\");

    wcsftime(fn, 128, L"%Y%m%d_%H%M%S.png", info);
    wcscat(path, fn);

    switch(vid_api) {
	case 0:		/* ddraw */
		ddraw_take_screenshot(path);
		break;

	case 1:		/* d3d9 */
		d3d_take_screenshot(path);
		break;

	case 2:		/* sdl */
		sdl_take_screenshot(path);
		break;

#ifdef USE_VNC
	case 3:		/* vnc */
		vnc_take_screenshot(path);
		break;
#endif
    }
}


/* LPARAM interface to plat_get_string(). */
LPARAM win_get_string(int id)
{
    wchar_t *ret;

    ret = plat_get_string(id);
    return ((LPARAM) ret);
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
