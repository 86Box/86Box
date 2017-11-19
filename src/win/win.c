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
 * Version:	@(#)win.c	1.0.36	2017/11/18
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
#include <windows.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <stdlib.h>
#include <time.h>
#include <wchar.h>
#include "../86box.h"
#include "../config.h"
#define GLOBAL
#include "../plat.h"
#include "../plat_mouse.h"
#include "../plat_midi.h"
#include "../ui.h"
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
		*lpRCstr3072,
		*lpRCstr4096,
		*lpRCstr4352,
		*lpRCstr4608,
		*lpRCstr5120,
		*lpRCstr5376,
		*lpRCstr5632,
		*lpRCstr6144;


static void
LoadCommonStrings(void)
{
    int i;

    lpRCstr2048 = (rc_str_t *)malloc(STR_NUM_2048*sizeof(rc_str_t));
    lpRCstr3072 = (rc_str_t *)malloc(STR_NUM_3072*sizeof(rc_str_t));
    lpRCstr4096 = (rc_str_t *)malloc(STR_NUM_4096*sizeof(rc_str_t));
    lpRCstr4352 = (rc_str_t *)malloc(STR_NUM_4352*sizeof(rc_str_t));
    lpRCstr4608 = (rc_str_t *)malloc(STR_NUM_4608*sizeof(rc_str_t));
    lpRCstr5120 = (rc_str_t *)malloc(STR_NUM_5120*sizeof(rc_str_t));
    lpRCstr5376 = (rc_str_t *)malloc(STR_NUM_5376*sizeof(rc_str_t));
    lpRCstr5632 = (rc_str_t *)malloc(STR_NUM_5632*sizeof(rc_str_t));
    lpRCstr6144 = (rc_str_t *)malloc(STR_NUM_6144*sizeof(rc_str_t));

    for (i=0; i<STR_NUM_2048; i++)
	LoadString(hinstance, 2048+i, lpRCstr2048[i].str, 512);

    for (i=0; i<STR_NUM_3072; i++)
	LoadString(hinstance, 3072+i, lpRCstr3072[i].str, 512);

    for (i=0; i<STR_NUM_4096; i++)
	LoadString(hinstance, 4096+i, lpRCstr4096[i].str, 512);

    for (i=0; i<STR_NUM_4352; i++)
	LoadString(hinstance, 4352+i, lpRCstr4352[i].str, 512);

    for (i=0; i<STR_NUM_4608; i++)
	LoadString(hinstance, 4608+i, lpRCstr4608[i].str, 512);

    for (i=0; i<STR_NUM_5120; i++)
	LoadString(hinstance, 5120+i, lpRCstr5120[i].str, 512);

    for (i=0; i<STR_NUM_5376; i++)
	LoadString(hinstance, 5376+i, lpRCstr5376[i].str, 512);

    for (i=0; i<STR_NUM_5632; i++)
	LoadString(hinstance, 5632+i, lpRCstr5632[i].str, 512);

    for (i=0; i<STR_NUM_6144; i++)
	LoadString(hinstance, 6144+i, lpRCstr6144[i].str, 512);
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


#ifndef USE_WX
# ifdef USE_CONSOLE
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
# endif


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


/* @@@
 * For the Windows platform, this is the start of the application.
 */
int WINAPI
WinMain(HINSTANCE hInst, HINSTANCE hPrev, LPSTR lpszArg, int nFunsterStil)
{
    wchar_t **argw = NULL;
    int	argc, i;

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

#ifdef USE_CONSOLE
    /* Create console window. */
    CreateConsole(1);
#endif

    /* Process the command line for options. */
    argc = ProcessCommandLine(&argw);

    /* Pre-initialize the system, this loads the config file. */
    if (! pc_init(argc, argw)) {
#ifdef USE_CONSOLE
	/* Detach from console. */
	CreateConsole(0);
#endif
	return(1);
    }

    /* Cleanup: we no longer need the commandline arguments. */
    free(argw[0]);
    free(argw);

    /* Handle our GUI. */
    i = ui_init(nFunsterStil);

    return(i);
}
#endif	/*USE_WX*/


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
    pclog("Main timer precision: %llu\n", timer_freq);

#if 0
    /* We should have an application-wide at_exit catcher. */
    atexit(plat_mouse_capture);
#endif

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
