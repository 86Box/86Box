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
#define UNICODE
#define NTDDI_VERSION 0x06010000
#include <windows.h>
#include <commctrl.h>
#include <shlobj.h>
#include <shobjidl.h>
#include <fcntl.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <time.h>
#include <direct.h>
#include <wchar.h>
#include <io.h>
#include <stdatomic.h>
#define HAVE_STDARG_H
#include <86box/86box.h>
#include <86box/config.h>
#include <86box/device.h>
#include <86box/keyboard.h>
#include <86box/mouse.h>
#include <86box/timer.h>
#include <86box/nvr.h>
#include <86box/video.h>
#include <86box/mem.h>
#include <86box/rom.h>
#include <86box/path.h>
#define GLOBAL
#include <86box/plat.h>
#include <86box/thread.h>
#include <86box/ui.h>
#ifdef USE_VNC
# include <86box/vnc.h>
#endif
#include <86box/win_sdl.h>
#include <86box/win_opengl.h>
#include <86box/win.h>
#include <86box/version.h>
#include <86box/gdbstub.h>
#ifdef MTR_ENABLED
#include <minitrace/minitrace.h>
#endif

typedef struct {
    WCHAR str[1024];
} rc_str_t;


/* Platform Public data, specific. */
HINSTANCE	hinstance;		/* application instance */
HANDLE		ghMutex;
uint32_t		lang_id, lang_sys;		/* current and system language ID */
DWORD		dwSubLangID;
int		acp_utf8;		/* Windows supports UTF-8 codepage */
volatile int	cpu_thread_run = 1;


/* Local data. */
static HANDLE	thMain;
static rc_str_t	*lpRCstr2048 = NULL,
		*lpRCstr4096 = NULL,
		*lpRCstr4352 = NULL,
		*lpRCstr4608 = NULL,
		*lpRCstr5120 = NULL,
		*lpRCstr5376 = NULL,
		*lpRCstr5632 = NULL,
		*lpRCstr5888 = NULL,
		*lpRCstr6144 = NULL,
		*lpRCstr7168 = NULL;
static int	vid_api_inited = 0;
static char	*argbuf;
static int	first_use = 1;
static LARGE_INTEGER	StartingTime;
static LARGE_INTEGER	Frequency;


static const struct {
    const char	*name;
    int		local;
    int		(*init)(void *);
    void	(*close)(void);
    void	(*resize)(int x, int y);
    int		(*pause)(void);
    void	(*enable)(int enable);
    void	(*set_fs)(int fs);
    void	(*reload)(void);
} vid_apis[RENDERERS_NUM] = {
  {	"SDL_Software", 1, (int(*)(void*))sdl_inits, sdl_close, NULL, sdl_pause, sdl_enable, sdl_set_fs, sdl_reload	},
  {	"SDL_Hardware", 1, (int(*)(void*))sdl_inith, sdl_close, NULL, sdl_pause, sdl_enable, sdl_set_fs, sdl_reload	},
  {	"SDL_OpenGL", 1, (int(*)(void*))sdl_initho, sdl_close, NULL, sdl_pause, sdl_enable, sdl_set_fs, sdl_reload	}
 ,{	"OpenGL_Core", 1, (int(*)(void*))opengl_init, opengl_close, opengl_resize, opengl_pause, NULL, opengl_set_fs, opengl_reload}
#ifdef USE_VNC
 ,{	"VNC", 0, vnc_init, vnc_close, vnc_resize, vnc_pause, NULL, NULL						}
#endif
};


extern int title_update;


#ifdef ENABLE_WIN_LOG
int win_do_log = ENABLE_WIN_LOG;


static void
win_log(const char *fmt, ...)
{
    va_list ap;

    if (win_do_log) {
	va_start(ap, fmt);
	pclog_ex(fmt, ap);
	va_end(ap);
    }
}
#else
#define win_log(fmt, ...)
#endif

void
free_string(rc_str_t **str)
{
    if (*str != NULL) {
	free(*str);
	*str = NULL;
    }
}


static void
LoadCommonStrings(void)
{
    int i;

    free_string(&lpRCstr7168);
    free_string(&lpRCstr6144);
    free_string(&lpRCstr5888);
    free_string(&lpRCstr5632);
    free_string(&lpRCstr5376);
    free_string(&lpRCstr5120);
    free_string(&lpRCstr4608);
    free_string(&lpRCstr4352);
    free_string(&lpRCstr4096);
    free_string(&lpRCstr2048);

    lpRCstr2048 = calloc(STR_NUM_2048, sizeof(rc_str_t));
    lpRCstr4096 = calloc(STR_NUM_4096, sizeof(rc_str_t));
    lpRCstr4352 = calloc(STR_NUM_4352, sizeof(rc_str_t));
    lpRCstr4608 = calloc(STR_NUM_4608, sizeof(rc_str_t));
    lpRCstr5120 = calloc(STR_NUM_5120, sizeof(rc_str_t));
    lpRCstr5376 = calloc(STR_NUM_5376, sizeof(rc_str_t));
    lpRCstr5632 = calloc(STR_NUM_5632, sizeof(rc_str_t));
    lpRCstr5888 = calloc(STR_NUM_5888, sizeof(rc_str_t));
    lpRCstr6144 = calloc(STR_NUM_6144, sizeof(rc_str_t));
    lpRCstr7168 = calloc(STR_NUM_7168, sizeof(rc_str_t));

    for (i=0; i<STR_NUM_2048; i++)
	LoadString(hinstance, 2048+i, lpRCstr2048[i].str, 1024);

    for (i=0; i<STR_NUM_4096; i++)
	LoadString(hinstance, 4096+i, lpRCstr4096[i].str, 1024);

    for (i=0; i<STR_NUM_4352; i++)
	LoadString(hinstance, 4352+i, lpRCstr4352[i].str, 1024);

    for (i=0; i<STR_NUM_4608; i++)
	LoadString(hinstance, 4608+i, lpRCstr4608[i].str, 1024);

    for (i=0; i<STR_NUM_5120; i++)
	LoadString(hinstance, 5120+i, lpRCstr5120[i].str, 1024);

    for (i=0; i<STR_NUM_5376; i++) {
	if ((i == 0) || (i > 3))
		LoadString(hinstance, 5376+i, lpRCstr5376[i].str, 1024);
    }

    for (i=0; i<STR_NUM_5632; i++) {
	if ((i == 0) || (i > 3))
		LoadString(hinstance, 5632+i, lpRCstr5632[i].str, 1024);
    }

    for (i=0; i<STR_NUM_5888; i++)
	LoadString(hinstance, 5888+i, lpRCstr5888[i].str, 1024);

    for (i=0; i<STR_NUM_6144; i++)
	LoadString(hinstance, 6144+i, lpRCstr6144[i].str, 1024);

    for (i=0; i<STR_NUM_7168; i++)
	LoadString(hinstance, 7168+i, lpRCstr7168[i].str, 1024);
}


size_t mbstoc16s(uint16_t dst[], const char src[], int len)
{
    if (src == NULL) return 0;
    if (len < 0) return 0;

    size_t ret = MultiByteToWideChar(CP_UTF8, 0, src, -1, dst, dst == NULL ? 0 : len);

    if (!ret) {
	return -1;
    }

    return ret;
}


size_t c16stombs(char dst[], const uint16_t src[], int len)
{
    if (src == NULL) return 0;
    if (len < 0) return 0;

    size_t ret = WideCharToMultiByte(CP_UTF8, 0, src, -1, dst, dst == NULL ? 0 : len, NULL, NULL);

    if (!ret) {
	return -1;
    }

    return ret;
}


int
has_language_changed(uint32_t id)
{
    return (lang_id != id);
}


/* Set (or re-set) the language for the application. */
void
set_language(uint32_t id)
{
    if (id == 0xFFFF) {
	set_language(lang_sys);
	lang_id = id;
	return;
    }

    if (lang_id != id) {
	/* Set our new language ID. */
	lang_id = id;
	SetThreadUILanguage(lang_id);

	/* Load the strings table for this ID. */
	LoadCommonStrings();

	/* Reload main menu */
	menuMain = LoadMenu(hinstance, L"MainMenu");
	if (hwndMain != NULL)
		SetMenu(hwndMain, menuMain);

	/* Re-init all the menus */
	ResetAllMenus();
	media_menu_init();
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

#ifdef MTR_ENABLED
void
init_trace(void)
{
    mtr_init("trace.json");
    mtr_start();
}

void
shutdown_trace(void)
{
    mtr_stop();
    mtr_shutdown();
}
#endif

/* Create a console if we don't already have one. */
static void
CreateConsole(int init)
{
    HANDLE h;
    FILE *fp;
    fpos_t p;
    int i;

    if (! init) {
	if (force_debug)
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

    if (fp != NULL) {
	fclose(fp);
	fp = NULL;
    }
}


static void
CloseConsole(void)
{
    CreateConsole(0);
}


/* Process the commandline, and create standard argc/argv array. */
static int
ProcessCommandLine(char ***argv)
{
    char **args;
    int argc_max;
    int i, q, argc;

    if (acp_utf8) {
	i = strlen(GetCommandLineA()) + 1;
	argbuf = (char *)malloc(i);
	strcpy(argbuf, GetCommandLineA());
    } else {
	i = c16stombs(NULL, GetCommandLineW(), 0) + 1;
	argbuf = (char *)malloc(i);
	c16stombs(argbuf, GetCommandLineW(), i);
    }

    argc = 0;
    argc_max = 64;
    args = (char **)malloc(sizeof(char *) * argc_max);
    if (args == NULL) {
	free(argbuf);
	return(0);
    }

    /* parse commandline into argc/argv format */
    i = 0;
    while (argbuf[i]) {
	while (argbuf[i] == ' ')
		  i++;

	if (argbuf[i]) {
		if ((argbuf[i] == '\'') || (argbuf[i] == '"')) {
			q = argbuf[i++];
			if (!argbuf[i])
				break;
		} else
			q = 0;

		args[argc++] = &argbuf[i];

		if (argc >= argc_max) {
			argc_max += 64;
			args = realloc(args, sizeof(char *)*argc_max);
			if (args == NULL) {
				free(argbuf);
				return(0);
			}
		}

		while ((argbuf[i]) && ((q)
			? (argbuf[i]!=q) : (argbuf[i]!=' '))) i++;

		if (argbuf[i]) {
			argbuf[i] = 0;
			i++;
		}
	}
    }

    args[argc] = NULL;
    *argv = args;

    return(argc);
}


/* For the Windows platform, this is the start of the application. */
int WINAPI
WinMain(HINSTANCE hInst, HINSTANCE hPrev, LPSTR lpszArg, int nCmdShow)
{
    char **argv = NULL;
    int	argc, i;

    /* Initialize the COM library for the main thread. */
    CoInitializeEx(NULL, COINIT_MULTITHREADED);

    /* Check if Windows supports UTF-8 */
    if (GetACP() == CP_UTF8)
	acp_utf8 = 1;
    else
	acp_utf8 = 0;

    /* Set this to the default value (windowed mode). */
    video_fullscreen = 0;

    /* We need this later. */
    hinstance = hInst;

    /* Set the application version ID string. */
    sprintf(emu_version, "%s v%s", EMU_NAME, EMU_VERSION_FULL);

	/* First, set our (default) language. */
	lang_sys = GetThreadUILanguage();
    set_language(DEFAULT_LANGUAGE);

    /* Process the command line for options. */
    argc = ProcessCommandLine(&argv);

    /* Pre-initialize the system, this loads the config file. */
    if (! pc_init(argc, argv)) {
	/* Detach from console. */
	if (force_debug)
		CreateConsole(0);

	if (source_hwnd)
		PostMessage((HWND) (uintptr_t) source_hwnd, WM_HAS_SHUTDOWN, (WPARAM) 0, (LPARAM) hwndMain);

	free(argbuf);
	free(argv);
	return(1);
    }

    /* Create console window. */
    if (force_debug) {
	CreateConsole(1);
	atexit(CloseConsole);
}

    /* Handle our GUI. */
    i = ui_init(nCmdShow);

    /* Uninitialize COM before exit. */
    CoUninitialize();

    free(argbuf);
    free(argv);
    return(i);
}


void
main_thread(void *param)
{
    uint32_t old_time, new_time;
    int drawits, frames;

    framecountx = 0;
    title_update = 1;
    old_time = GetTickCount();
    drawits = frames = 0;
    while (!is_quit && cpu_thread_run) {
	/* See if it is time to run a frame of code. */
	new_time = GetTickCount();
#ifdef USE_GDBSTUB
	if (gdbstub_next_asap && (drawits <= 0))
		drawits = 10;
	else
#endif
	drawits += (new_time - old_time);
	old_time = new_time;
	if (drawits > 0 && !dopause) {
		/* Yes, so do one frame now. */
		drawits -= 10;
		if (drawits > 50)
			drawits = 0;

		/* Run a block of code. */
		pc_run();

		/* Every 200 frames we save the machine status. */
		if (++frames >= 200 && nvr_dosave) {
			nvr_save();
			nvr_dosave = 0;
			frames = 0;
		}
	} else	/* Just so we dont overload the host OS. */
		Sleep((drawits < -1) ? 1 : 0);
		// Sleep(1);

	/* If needed, handle a screen resize. */
	if (!atomic_flag_test_and_set(&doresize) && !video_fullscreen && !is_quit) {
		if (vid_resize & 2)
			plat_resize(fixed_size_x, fixed_size_y);
		else
			plat_resize(scrnsz_x, scrnsz_y);
	}
    }

    is_quit = 1;
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
    is_quit = 0;

    /* Initialize the high-precision timer. */
    timeBeginPeriod(1);
    QueryPerformanceFrequency(&qpc);
    timer_freq = qpc.QuadPart;
    win_log("Main timer precision: %llu\n", timer_freq);

    /* Start the emulator, really. */
    thMain = thread_create(main_thread, NULL);
    SetThreadPriority(thMain, THREAD_PRIORITY_HIGHEST);
}


/* Cleanly stop the emulator. */
void
do_stop(void)
{
    /* Claim the video blitter. */
    startblit();

    vid_apis[vid_api].close();

    pc_close(thMain);

    thMain = NULL;

    if (source_hwnd)
	PostMessage((HWND) (uintptr_t) source_hwnd, WM_HAS_SHUTDOWN, (WPARAM) 0, (LPARAM) hwndMain);
}


void
plat_get_exe_name(char *s, int size)
{
    wchar_t *temp;

    if (acp_utf8)
	GetModuleFileNameA(hinstance, s, size);
    else {
	temp = malloc(size * sizeof(wchar_t));
	GetModuleFileNameW(hinstance, temp, size);
	c16stombs(s, temp, size);
	free(temp);
    }
}


void
plat_tempfile(char *bufp, char *prefix, char *suffix)
{
    SYSTEMTIME SystemTime;

    if (prefix != NULL)
	sprintf(bufp, "%s-", prefix);
      else
	strcpy(bufp, "");

    GetSystemTime(&SystemTime);
    sprintf(&bufp[strlen(bufp)], "%d%02d%02d-%02d%02d%02d-%03d%s",
        SystemTime.wYear, SystemTime.wMonth, SystemTime.wDay,
	SystemTime.wHour, SystemTime.wMinute, SystemTime.wSecond,
	SystemTime.wMilliseconds,
	suffix);
}


int
plat_getcwd(char *bufp, int max)
{
    wchar_t *temp;

    if (acp_utf8)
	(void)_getcwd(bufp, max);
    else {
	temp = malloc(max * sizeof(wchar_t));
	(void)_wgetcwd(temp, max);
	c16stombs(bufp, temp, max);
	free(temp);
    }

    return(0);
}


int
plat_chdir(char *path)
{
    wchar_t *temp;
    int len, ret;

    if (acp_utf8)
	return(_chdir(path));
    else {
	len = mbstoc16s(NULL, path, 0) + 1;
	temp = malloc(len * sizeof(wchar_t));
	mbstoc16s(temp, path, len);

	ret = _wchdir(temp);

	free(temp);
	return ret;
    }
}


FILE *
plat_fopen(const char *path, const char *mode)
{
    wchar_t *pathw, *modew;
    int len;
    FILE *fp;

    if (acp_utf8)
	return fopen(path, mode);
    else {
	len = mbstoc16s(NULL, path, 0) + 1;
	pathw = malloc(sizeof(wchar_t) * len);
	mbstoc16s(pathw, path, len);

	len = mbstoc16s(NULL, mode, 0) + 1;
	modew = malloc(sizeof(wchar_t) * len);
	mbstoc16s(modew, mode, len);

	fp = _wfopen(pathw, modew);

	free(pathw);
	free(modew);

	return fp;
    }
}


/* Open a file, using Unicode pathname, with 64bit pointers. */
FILE *
plat_fopen64(const char *path, const char *mode)
{
    return plat_fopen(path, mode);
}


void
plat_remove(char *path)
{
    wchar_t *temp;
    int len;

    if (acp_utf8)
	remove(path);
    else {
	len = mbstoc16s(NULL, path, 0) + 1;
	temp = malloc(len * sizeof(wchar_t));
	mbstoc16s(temp, path, len);

	_wremove(temp);

	free(temp);
    }
}

void
path_normalize(char* path)
{
    /* No-op */
}

/* Make sure a path ends with a trailing (back)slash. */
void
path_slash(char *path)
{
    if ((path[strlen(path)-1] != '\\') &&
	(path[strlen(path)-1] != '/')) {
	strcat(path, "\\");
    }
}


/* Check if the given path is absolute or not. */
int
path_abs(char *path)
{
    if ((path[1] == ':') || (path[0] == '\\') || (path[0] == '/'))
	return(1);

    return(0);
}


/* Return the last element of a pathname. */
char *
plat_get_basename(const char *path)
{
    int c = (int)strlen(path);

    while (c > 0) {
	if (path[c] == '/' || path[c] == '\\')
	   return((char *)&path[c + 1]);
       c--;
    }

    return((char *)path);
}


/* Return the 'directory' element of a pathname. */
void
path_get_dirname(char *dest, const char *path)
{
    int c = (int)strlen(path);
    char *ptr;

    ptr = (char *)path;

    while (c > 0) {
	if (path[c] == '/' || path[c] == '\\') {
		ptr = (char *)&path[c];
		break;
	}
 	c--;
    }

    /* Copy to destination. */
    while (path < ptr)
	*dest++ = *path++;
    *dest = '\0';
}


char *
path_get_filename(char *s)
{
    int c = strlen(s) - 1;

    while (c > 0) {
	if (s[c] == '/' || s[c] == '\\')
	   return(&s[c+1]);
       c--;
    }

    return(s);
}


char *
path_get_extension(char *s)
{
    int c = strlen(s) - 1;

    if (c <= 0)
	return(s);

    while (c && s[c] != '.')
		c--;

    if (!c)
	return(&s[strlen(s)]);

    return(&s[c+1]);
}


void
path_append_filename(char *dest, const char *s1, const char *s2)
{
    strcpy(dest, s1);
    path_slash(dest);
    strcat(dest, s2);
}


void
plat_put_backslash(char *s)
{
    int c = strlen(s) - 1;

    if (s[c] != '/' && s[c] != '\\')
	   s[c] = '/';
}


int
plat_dir_check(char *path)
{
    DWORD dwAttrib;
    int len;
    wchar_t *temp;

    if (acp_utf8)
	dwAttrib = GetFileAttributesA(path);
    else {
	len = mbstoc16s(NULL, path, 0) + 1;
	temp = malloc(len * sizeof(wchar_t));
	mbstoc16s(temp, path, len);

	dwAttrib = GetFileAttributesW(temp);

	free(temp);
    }

    return(((dwAttrib != INVALID_FILE_ATTRIBUTES &&
	   (dwAttrib & FILE_ATTRIBUTE_DIRECTORY))) ? 1 : 0);
}


int
plat_dir_create(char *path)
{
    int ret, len;
    wchar_t *temp;

    if (acp_utf8)
	return (int)SHCreateDirectoryExA(NULL, path, NULL);
    else {
	len = mbstoc16s(NULL, path, 0) + 1;
	temp = malloc(len * sizeof(wchar_t));
	mbstoc16s(temp, path, len);

	ret = (int)SHCreateDirectoryExW(NULL, temp, NULL);

	free(temp);

	return ret;
    }
}


void *
plat_mmap(size_t size, uint8_t executable)
{
    return VirtualAlloc(NULL, size, MEM_COMMIT, executable ? PAGE_EXECUTE_READWRITE : PAGE_READWRITE);
}


void
plat_init_rom_paths()
{
    wchar_t appdata_dir[1024] = { L'\0' };

    if (_wgetenv(L"LOCALAPPDATA") && _wgetenv(L"LOCALAPPDATA")[0] != L'\0') {
        char appdata_dir_a[1024] = { '\0' };
        size_t len = 0;
        wcsncpy(appdata_dir, _wgetenv(L"LOCALAPPDATA"), 1024);
        len = wcslen(appdata_dir);
        if (appdata_dir[len - 1] != L'\\') {
            appdata_dir[len] = L'\\';
            appdata_dir[len + 1] = L'\0';
        }
        wcscat(appdata_dir, L"86box");
        CreateDirectoryW(appdata_dir, NULL);
        wcscat(appdata_dir, L"\\roms");
        CreateDirectoryW(appdata_dir, NULL);
        wcscat(appdata_dir, L"\\");
        c16stombs(appdata_dir_a, appdata_dir, 1024);
        rom_add_path(appdata_dir_a);
    }
}

void
plat_munmap(void *ptr, size_t size)
{
    VirtualFree(ptr, 0, MEM_RELEASE);
}


uint64_t
plat_timer_read(void)
{
    LARGE_INTEGER li;

    QueryPerformanceCounter(&li);

    return(li.QuadPart);
}

static LARGE_INTEGER
plat_get_ticks_common(void)
{
    LARGE_INTEGER EndingTime, ElapsedMicroseconds;

    if (first_use) {
	QueryPerformanceFrequency(&Frequency);
	QueryPerformanceCounter(&StartingTime);
	first_use = 0;
    }

    QueryPerformanceCounter(&EndingTime);
    ElapsedMicroseconds.QuadPart = EndingTime.QuadPart - StartingTime.QuadPart;

    /* We now have the elapsed number of ticks, along with the
       number of ticks-per-second. We use these values
       to convert to the number of elapsed microseconds.
       To guard against loss-of-precision, we convert
       to microseconds *before* dividing by ticks-per-second. */
    ElapsedMicroseconds.QuadPart *= 1000000;
    ElapsedMicroseconds.QuadPart /= Frequency.QuadPart;

    return ElapsedMicroseconds;
}

uint32_t
plat_get_ticks(void)
{
	return (uint32_t)(plat_get_ticks_common().QuadPart / 1000);
}

uint32_t
plat_get_micro_ticks(void)
{
	return (uint32_t)plat_get_ticks_common().QuadPart;
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

    /* Default/System is SDL Hardware. */
    if (!strcasecmp(name, "default") || !strcasecmp(name, "system")) return(1);

    /* If DirectDraw or plain SDL was specified, return SDL Software. */
    if (!strcasecmp(name, "ddraw") || !strcasecmp(name, "sdl")) return(1);

    for (i = 0; i < RENDERERS_NUM; i++) {
	if (vid_apis[i].name &&
	    !strcasecmp(vid_apis[i].name, name)) return(i);
    }

    /* Default value. */
    return(1);
}


/* Return the VIDAPI name for the given number. */
char *
plat_vidapi_name(int api)
{
    char *name = "default";

    switch(api) {
	case 0:
		name = "sdl_software";
		break;
	case 1:
		break;
	case 2:
		name = "sdl_opengl";
		break;
	case 3:
		name = "opengl_core";
		break;
#ifdef USE_VNC
	case 4:
		name = "vnc";
		break;
#endif
	default:
		fatal("Unknown renderer: %i\n", api);
		break;
    }

    return(name);
}


int
plat_setvid(int api)
{
    int i;

    win_log("Initializing VIDAPI: api=%d\n", api);
    startblit();

    /* Close the (old) API. */
    vid_apis[vid_api].close();
    vid_api = api;

    if (vid_apis[vid_api].local)
	ShowWindow(hwndRender, SW_SHOW);
      else
	ShowWindow(hwndRender, SW_HIDE);

    /* Initialize the (new) API. */
    i = vid_apis[vid_api].init((void *)hwndRender);
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
    if (!vid_api_inited || !vid_apis[vid_api].resize) return;

    startblit();
    vid_apis[vid_api].resize(x, y);
    endblit();
}


void
plat_vidapi_enable(int enable)
{
    int i = 1;

    if (!vid_api_inited || !vid_apis[vid_api].enable)
	return;

    vid_apis[vid_api].enable(enable != 0);

    if (! i)
	return;

    if (enable)
	device_force_redraw();
}


int
get_vidpause(void)
{
    return(vid_apis[vid_api].pause());
}


void
plat_setfullscreen(int on)
{
    RECT rect;
    int temp_x, temp_y;
    int dpi = win_get_dpi(hwndMain);

    /* Are we changing from the same state to the same state? */
    if ((!!(on & 1)) == (!!video_fullscreen))
	return;

    if (on && video_fullscreen_first) {
	video_fullscreen |= 2;
	if (ui_msgbox_header(MBX_INFO | MBX_DONTASK, (wchar_t *) IDS_2134, (wchar_t *) IDS_2052) == 10) {
		video_fullscreen_first = 0;
		config_save();
	}
	video_fullscreen &= 1;
    }

    /* OK, claim the video. */
    if (!(on & 2))
	win_mouse_close();

    /* Close the current mode, and open the new one. */
    video_fullscreen = (on & 1) | 2;
    if (vid_apis[vid_api].set_fs)
	vid_apis[vid_api].set_fs(on & 1);
    if (!(on & 1)) {
	plat_resize(scrnsz_x, scrnsz_y);
	if (vid_resize) {
		/* scale the screen base on DPI */
		if (!(vid_resize & 2) && window_remember) {
			MoveWindow(hwndMain, window_x, window_y, window_w, window_h, TRUE);
			GetClientRect(hwndMain, &rect);

			temp_x = rect.right - rect.left + 1;
			temp_y = rect.bottom - rect.top + 1 - (hide_status_bar ? 0 : sbar_height) - (hide_tool_bar ? 0 : tbar_height);
		} else {
			if (dpi_scale) {
				temp_x = MulDiv((vid_resize & 2) ? fixed_size_x : unscaled_size_x, dpi, 96);
				temp_y = MulDiv((vid_resize & 2) ? fixed_size_y : unscaled_size_y, dpi, 96);
			} else {
				temp_x = (vid_resize & 2) ? fixed_size_x : unscaled_size_x;
				temp_y = (vid_resize & 2) ? fixed_size_y : unscaled_size_y;
			}

			/* Main Window. */
			if (vid_resize >= 2)
				MoveWindow(hwndMain, window_x, window_y, window_w, window_h, TRUE);

			ResizeWindowByClientArea(hwndMain, temp_x, temp_y + (hide_status_bar ? 0 : sbar_height) + (hide_tool_bar ? 0 : tbar_height));
		}

		/* Toolbar. */
		MoveWindow(hwndRebar, 0, 0, temp_x, tbar_height, TRUE);

		/* Render window. */
		MoveWindow(hwndRender, 0, hide_tool_bar ? 0 : tbar_height, temp_x, temp_y, TRUE);

		/* Status bar. */
		GetClientRect(hwndMain, &rect);
		MoveWindow(hwndSBAR, 0, rect.bottom - sbar_height, temp_x, sbar_height, TRUE);

		if (mouse_capture)
			ClipCursor(&rect);

		scrnsz_x = (vid_resize & 2) ? fixed_size_x : unscaled_size_x;
		scrnsz_y = (vid_resize & 2) ? fixed_size_y : unscaled_size_y;
	}
    }
    video_fullscreen &= 1;
    video_force_resize_set(1);
    if (!(on & 1))
        atomic_flag_clear(&doresize);

    win_mouse_init();

    if (!(on & 2)) {
	/* Release video and make it redraw the screen. */
	device_force_redraw();

	/* Send a CTRL break code so CTRL does not get stuck. */
	keyboard_input(0, 0x01D);
    }

    /* Finally, handle the host's mouse cursor. */
    /* win_log("%s full screen, %s cursor\n", on ? "enter" : "leave", on ? "hide" : "show"); */
    show_cursor(video_fullscreen ? 0 : -1);

    if (!(on & 2)) {
	/* This is needed for OpenGL. */
	plat_vidapi_enable(0);
	plat_vidapi_enable(1);
    }
}


void
plat_vid_reload_options(void)
{
	if (!vid_api_inited || !vid_apis[vid_api].reload)
		return;

	vid_apis[vid_api].reload();
}


void
plat_vidapi_reload(void)
{
    vid_apis[vid_api].reload();
}


/* Sets up the program language before initialization. */
uint32_t
plat_language_code(char* langcode)
{
	if (!strcmp(langcode, "system"))
		return 0xFFFF;

	int len = mbstoc16s(NULL, langcode, 0) + 1;
	wchar_t *temp = malloc(len * sizeof(wchar_t));
	mbstoc16s(temp, langcode, len);

	LCID lcid = LocaleNameToLCID((LPWSTR)temp, 0);

	free(temp);
	return lcid;
}

/* Converts back the language code to LCID */
void
plat_language_code_r(uint32_t lcid, char* outbuf, int len)
{
	if (lcid == 0xFFFF)
	{
		strcpy(outbuf, "system");
		return;
	}

	wchar_t buffer[LOCALE_NAME_MAX_LENGTH + 1];
	LCIDToLocaleName(lcid, buffer, LOCALE_NAME_MAX_LENGTH, 0);

	c16stombs(outbuf, buffer, len);
}

void
take_screenshot(void)
{
    startblit();
    screenshots++;
    endblit();
    device_force_redraw();
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
