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
#include <SDL2/SDL.h>
#include <SDL2/SDL_syswm.h>
#include <SDL2/SDL_system.h>
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
#include <stdbool.h>
#include <time.h>
#include <direct.h>
#include <wchar.h>
#include <io.h>
#include <86box/86box.h>
#include <86box/config.h>
#include <86box/device.h>
#include <86box/keyboard.h>
#include <86box/mouse.h>
#include <86box/timer.h>
#include <86box/nvr.h>
#include <86box/video.h>
#define GLOBAL
#include <86box/plat.h>
#include <86box/plat_midi.h>
#include <86box/ui.h>
#ifdef USE_VNC
# include <86box/vnc.h>
#endif
#include <86box/win_sdl.h>
#include <86box/win_opengl.h>
#include <86box/win.h>
#include <86box/win_imgui.h>
#include <86box/version.h>
#ifdef MTR_ENABLED
#include <minitrace/minitrace.h>
#endif

#define PATH_MAX MAX_PATH


typedef struct {
    WCHAR str[1024];
} rc_str_t;


/* Platform Public data, specific. */
HINSTANCE	hinstance;		/* application instance */
HANDLE		ghMutex;
uint32_t		lang_id, lang_sys;		/* current and system language ID */
DWORD		dwSubLangID;
int		acp_utf8;		/* Windows supports UTF-8 codepage */


/* Local data. */
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

extern bool     ImGui_ImplSDL2_Init(SDL_Window* window);
extern void     ImGui_ImplSDL2_Shutdown();
extern void     ImGui_ImplSDL2_NewFrame();
extern bool     ImGui_ImplSDL2_ProcessEvent(const SDL_Event* event);
static int	first_use = 1;
static uint64_t	StartingTime;
static uint64_t Frequency;
int rctrl_is_lalt;
int	update_icons = 1;
int	kbd_req_capture;
int hide_status_bar;
int fixed_size_x = 640;
int fixed_size_y = 480;
extern int title_set;
extern wchar_t sdl_win_title[512];
SDL_mutex *blitmtx;
SDL_threadID eventthread;
static int exit_event = 0;
int fullscreen_pending = 0;
extern float menubarheight;
static rc_str_t* lpRCstr2048,
* lpRCstr4096,
* lpRCstr4352,
* lpRCstr4608,
* lpRCstr5120,
* lpRCstr5376,
* lpRCstr5632,
* lpRCstr5888,
* lpRCstr6144,
* lpRCstr7168;

typedef struct sdl_blit_params
{
    int x, y, w, h;
} sdl_blit_params;

sdl_blit_params params = { 0, 0, 0, 0 };
int blitreq = 0;

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
	if (id == 0xFFFF)
	{
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
		SetMenu(hwndMain, menuMain);
		
		/* Re-init all the menus */
		ResetAllMenus();
		media_menu_init();
    } 
}


wchar_t*
plat_get_string(int i)
{
	LPTSTR str;

	if ((i >= 2048) && (i <= 3071))
		str = lpRCstr2048[i - 2048].str;
	else if ((i >= 4096) && (i <= 4351))
		str = lpRCstr4096[i - 4096].str;
	else if ((i >= 4352) && (i <= 4607))
		str = lpRCstr4352[i - 4352].str;
	else if ((i >= 4608) && (i <= 5119))
		str = lpRCstr4608[i - 4608].str;
	else if ((i >= 5120) && (i <= 5375))
		str = lpRCstr5120[i - 5120].str;
	else if ((i >= 5376) && (i <= 5631))
		str = lpRCstr5376[i - 5376].str;
	else if ((i >= 5632) && (i <= 5887))
		str = lpRCstr5632[i - 5632].str;
	else if ((i >= 5888) && (i <= 6143))
		str = lpRCstr5888[i - 5888].str;
	else if ((i >= 6144) && (i <= 7167))
		str = lpRCstr6144[i - 6144].str;
	else
		str = lpRCstr7168[i - 7168].str;

	return((wchar_t*)str);
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

volatile int cpu_thread_run = 1;

int stricmp(const char* s1, const char* s2)
{
    return strcasecmp(s1, s2);
}

int strnicmp(const char *s1, const char *s2, size_t n)
{
    return strncasecmp(s1, s2, n);
}

void
main_thread(void *param)
{
    uint32_t old_time, new_time;
    int drawits, frames;

    SDL_SetThreadPriority(SDL_THREAD_PRIORITY_HIGH);
    framecountx = 0;
    title_update = 1;
    old_time = SDL_GetTicks();
    drawits = frames = 0;
    while (!is_quit && cpu_thread_run) {
	/* See if it is time to run a frame of code. */
	new_time = SDL_GetTicks();
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
	if (doresize && !video_fullscreen && !is_quit) {
		if (vid_resize & 2)
			plat_resize(fixed_size_x, fixed_size_y);
		else
			plat_resize(scrnsz_x, scrnsz_y);
		doresize = 0;
	}
    }

    is_quit = 1;
}

thread_t* thMain = NULL;

void
do_start(void)
{
    /* We have not stopped yet. */
    is_quit = 0;

    /* Initialize the high-precision timer. */
    SDL_InitSubSystem(SDL_INIT_TIMER);
    timer_freq = SDL_GetPerformanceFrequency();

    /* Start the emulator, really. */
    thMain = thread_create(main_thread, NULL);
}

void
do_stop(void)
{
    if (SDL_ThreadID() != eventthread)
    {
        exit_event = 1;
        return;
    }
    if (blitreq)
    {
        blitreq = 0;
        extern void video_blit_complete();
        video_blit_complete();
    }

    while(SDL_TryLockMutex(blitmtx) == SDL_MUTEX_TIMEDOUT)
    {
        if (blitreq)
        {
            blitreq = 0;
            extern void video_blit_complete();
            video_blit_complete();
        }
    }
    startblit();

    is_quit = 1;
    sdl_close();

    pc_close(thMain);

    thMain = NULL;
}

int	ui_msgbox(int flags, void *message)
{
    return ui_msgbox_header(flags, message, NULL);
}

int	ui_msgbox_header(int flags, void *header, void* message)
{
	return ui_msgbox_ex(flags, header, message, NULL, NULL, NULL);
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

    cpu_thread_run = 0;
}

extern void     sdl_blit(int x, int y, int w, int h);

typedef struct mouseinputdata
{
    int deltax, deltay, deltaz;
    int mousebuttons;
} mouseinputdata;
SDL_mutex* mousemutex;
static mouseinputdata mousedata;
void mouse_poll()
{
    SDL_LockMutex(mousemutex);
    mouse_x = mousedata.deltax;
    mouse_y = mousedata.deltay;
    mouse_z = mousedata.deltaz;
    mousedata.deltax = mousedata.deltay = mousedata.deltaz = 0;
    mouse_buttons = mousedata.mousebuttons;
    SDL_UnlockMutex(mousemutex);
}


extern int sdl_w, sdl_h;
void ui_sb_set_ready(int ready) {}
char* xargv[512];

// From musl.
char *local_strsep(char **str, const char *sep)
{
	char *s = *str, *end;
	if (!s) return NULL;
	end = s + strcspn(s, sep);
	if (*end) *end++ = 0;
	else end = 0;
	*str = end;
	return s;
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


/* Make sure a path ends with a trailing (back)slash. */
void
plat_path_slash(char *path)
{
    if ((path[strlen(path)-1] != '\\') &&
	(path[strlen(path)-1] != '/')) {
	strcat(path, "\\");
    }
}


/* Check if the given path is absolute or not. */
int
plat_path_abs(char *path)
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
plat_get_dirname(char *dest, const char *path)
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
plat_get_filename(char *s)
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
plat_get_extension(char *s)
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
plat_append_filename(char *dest, const char *s1, const char *s2)
{
    strcpy(dest, s1);
    plat_path_slash(dest);
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
plat_munmap(void *ptr, size_t size)
{
    VirtualFree(ptr, 0, MEM_RELEASE);
}


uint64_t
plat_timer_read(void)
{
    return SDL_GetPerformanceCounter();
}

void
plat_pause(int p)
{
    static wchar_t oldtitle[512];
    wchar_t title[512];

    dopause = p;
    if (p) {
	wcsncpy(oldtitle, ui_window_title(NULL), sizeof_w(oldtitle) - 1);
	wcscpy(title, oldtitle);
	wcscat(title, L" - PAUSED -");
	ui_window_title(title);
    } else {
	ui_window_title(oldtitle);
    }
}

bool process_media_commands_3(uint8_t* id, char* fn, uint8_t* wp, int cmdargc)
{
    bool err = false;
    *id = atoi(xargv[1]);
    if (xargv[2][0] == '\'' || xargv[2][0] == '"')
    {
        int curarg = 2;
        for (curarg = 2; curarg < cmdargc; curarg++)
        {
            if (strlen(fn) + strlen(xargv[curarg]) >= PATH_MAX)
            {
                err = true;
                fprintf(stderr, "Path name too long.\n");
            }
            strcat(fn, xargv[curarg] + (xargv[curarg][0] == '\'' || xargv[curarg][0] == '"'));
            if (fn[strlen(fn) - 1] == '\''
                || fn[strlen(fn) - 1] == '"')
            {
                if (curarg + 1 < cmdargc)
                {
                    *wp = atoi(xargv[curarg + 1]);
                }
                break;
            }
            strcat(fn, " ");
        }
    }
    else
    {
        if (strlen(xargv[2]) < PATH_MAX)
        {
            strcpy(fn, xargv[2]);
            *wp = atoi(xargv[3]);
        }
        else
        {
            fprintf(stderr, "Path name too long.\n");
            err = true;
        }
    }
    if (fn[strlen(fn) - 1] == '\''
    || fn[strlen(fn) - 1] == '"') fn[strlen(fn) - 1] = '\0';
    return err;
}
char* (*f_readline)(const char*) = NULL;
int  (*f_add_history)(const char *) = NULL;
void (*f_rl_callback_handler_remove)(void) = NULL;

uint32_t timer_onesec(uint32_t interval, void* param)
{
        pc_onesec();
        return interval;
}

void monitor_thread(void* param)
{
#ifndef USE_CLI
    if (isatty(fileno(stdin)) && isatty(fileno(stdout)))
    {
        char line[256] = {0};
        printf("86Box monitor console.\n");
        while (!exit_event)
        {
            if (feof(stdin)) break;
            printf("(86Box) ");
            fgets(line, 255 + PATH_MAX, stdin);
            
			int cmdargc = 0;
			char* linecpy;
			line[strcspn(line, "\r\n")] = '\0';
			linecpy = strdup(line);
			if (f_add_history) f_add_history(line);
			memset(xargv, 0, sizeof(xargv));
			while(1) 
			{
				xargv[cmdargc++] = local_strsep(&linecpy, " ");
				if (xargv[cmdargc - 1] == NULL || cmdargc >= 512) break;
			}
			cmdargc--;
			if (strncasecmp(xargv[0], "help", 4) == 0)
			{
				printf(
					"fddload <id> <filename> <wp> - Load floppy disk image into drive <id>.\n"
					"cdload <id> <filename> - Load CD-ROM image into drive <id>.\n"
					"zipload <id> <filename> <wp> - Load ZIP image into ZIP drive <id>.\n"
					"cartload <id> <filename> <wp> - Load cartridge image into cartridge drive <id>.\n"
					"moload <id> <filename> <wp> - Load MO image into MO drive <id>.\n\n"
					"fddeject <id> - eject disk from floppy drive <id>.\n"
					"cdeject <id> - eject disc from CD-ROM drive <id>.\n"
					"zipeject <id> - eject ZIP image from ZIP drive <id>.\n"
					"carteject <id> - eject cartridge from drive <id>.\n"
					"moeject <id> - eject image from MO drive <id>.\n\n"
					"hardreset - hard reset the emulated system.\n"
					"pause - pause the the emulated system.\n"
					"fullscreen - toggle fullscreen.\n"
					"exit - exit 86Box.\n");
			}
			else if (strncasecmp(xargv[0], "exit", 4) == 0)
			{
				exit_event = 1;
			}
			else if (strncasecmp(xargv[0], "fullscreen", 10) == 0)
			{
				video_fullscreen = 1;
				fullscreen_pending = 1;
			}
			else if (strncasecmp(xargv[0], "pause", 5) == 0)
			{
				plat_pause(dopause ^ 1);
				printf("%s", dopause ? "Paused.\n" : "Unpaused.\n");
			}
			else if (strncasecmp(xargv[0], "hardreset", 9) == 0)
			{
				pc_reset_hard();
			}
			else if (strncasecmp(xargv[0], "cdload", 6) == 0 && cmdargc >= 3)
			{
				uint8_t id;
				bool err = false;
				char fn[PATH_MAX];
				
				if (!xargv[2] || !xargv[1])
				{
					free(linecpy);
					continue;
				}
				id = atoi(xargv[1]);
				memset(fn, 0, sizeof(fn));
				if (xargv[2][0] == '\'' || xargv[2][0] == '"')
				{
					int curarg = 2;
					for (curarg = 2; curarg < cmdargc; curarg++)
					{
						if (strlen(fn) + strlen(xargv[curarg]) >= PATH_MAX)
						{
							err = true;
							fprintf(stderr, "Path name too long.\n");
						}
						strcat(fn, xargv[curarg] + (xargv[curarg][0] == '\'' || xargv[curarg][0] == '"'));
						if (fn[strlen(fn) - 1] == '\''
							|| fn[strlen(fn) - 1] == '"')
						{
							break;
						}
						strcat(fn, " ");
					}
				}
				else
				{
					if (strlen(xargv[2]) < PATH_MAX)
					{
						strcpy(fn, xargv[2]);
					}
					else
					{
						fprintf(stderr, "Path name too long.\n");
					}
				}
				if (!err)
				{

					if (fn[strlen(fn) - 1] == '\''
						|| fn[strlen(fn) - 1] == '"') fn[strlen(fn) - 1] = '\0';
					printf("Inserting disc into CD-ROM drive %hhu: %s\n", id, fn);
					cdrom_mount(id, fn);
				}
			}
			else if (strncasecmp(xargv[0], "fddeject", 8) == 0 && cmdargc >= 2)
			{
				floppy_eject(atoi(xargv[1]));
			}
			else if (strncasecmp(xargv[0], "cdeject", 8) == 0 && cmdargc >= 2)
			{
				cdrom_mount(atoi(xargv[1]), "");
			}
			else if (strncasecmp(xargv[0], "moeject", 8) == 0 && cmdargc >= 2)
			{
				mo_eject(atoi(xargv[1]));
			}
			else if (strncasecmp(xargv[0], "carteject", 8) == 0 && cmdargc >= 2)
			{
				cartridge_eject(atoi(xargv[1]));
			}
			else if (strncasecmp(xargv[0], "zipeject", 8) == 0 && cmdargc >= 2)
			{
				zip_eject(atoi(xargv[1]));
			}
			else if (strncasecmp(xargv[0], "fddload", 7) == 0 && cmdargc >= 4)
			{
				uint8_t id, wp;
				bool err = false;
				char fn[PATH_MAX];
				memset(fn, 0, sizeof(fn));
				if (!xargv[2] || !xargv[1])
				{
					free(linecpy);
					continue;
				}
				err = process_media_commands_3(&id, fn, &wp, cmdargc);
				if (!err)
				{
					if (fn[strlen(fn) - 1] == '\''
						|| fn[strlen(fn) - 1] == '"') fn[strlen(fn) - 1] = '\0';
					printf("Inserting disk into floppy drive %c: %s\n", id + 'A', fn);
					floppy_mount(id, fn, wp);
				}
			}
			else if (strncasecmp(xargv[0], "moload", 7) == 0 && cmdargc >= 4)
			{
				uint8_t id, wp;
				bool err = false;
				char fn[PATH_MAX];
				memset(fn, 0, sizeof(fn));
				if (!xargv[2] || !xargv[1])
				{
					free(linecpy);
					continue;
				}
				err = process_media_commands_3(&id, fn, &wp, cmdargc);
				if (!err)
				{
					if (fn[strlen(fn) - 1] == '\''
						|| fn[strlen(fn) - 1] == '"') fn[strlen(fn) - 1] = '\0';
					printf("Inserting into mo drive %hhu: %s\n", id, fn);
					mo_mount(id, fn, wp);
				}
			}
			else if (strncasecmp(xargv[0], "cartload", 7) == 0 && cmdargc >= 4)
			{
				uint8_t id, wp;
				bool err = false;
				char fn[PATH_MAX];
				memset(fn, 0, sizeof(fn));
				if (!xargv[2] || !xargv[1])
				{
					free(linecpy);
					continue;
				}
				err = process_media_commands_3(&id, fn, &wp, cmdargc);
				if (!err)
				{
					if (fn[strlen(fn) - 1] == '\''
						|| fn[strlen(fn) - 1] == '"') fn[strlen(fn) - 1] = '\0';
					printf("Inserting tape into cartridge holder %hhu: %s\n", id, fn);
					cartridge_mount(id, fn, wp);
				}
			}
			else if (strncasecmp(xargv[0], "zipload", 7) == 0 && cmdargc >= 4)
			{
				uint8_t id, wp;
				bool err = false;
				char fn[PATH_MAX];
				memset(fn, 0, sizeof(fn));
				if (!xargv[2] || !xargv[1])
				{
					free(linecpy);
					continue;
				}
				err = process_media_commands_3(&id, fn, &wp, cmdargc);
				if (!err)
				{
					if (fn[strlen(fn) - 1] == '\''
						|| fn[strlen(fn) - 1] == '"') fn[strlen(fn) - 1] = '\0';
					printf("Inserting disk into ZIP drive %c: %s\n", id + 'A', fn);
					zip_mount(id, fn, wp);
				}
			}
			free(linecpy);
            
        }
    }
#endif
}


extern void sdl_real_blit(SDL_Rect* r_src);

void PreSDLWinMessageHook(void* userdata, void* hWnd, unsigned int message, Uint64 wParam, Sint64 lParam)
{
	switch (message)
	{
    case WM_FORCERESIZE:
    {
        extern SDL_Window* sdl_win;
        SDL_SetWindowSize(sdl_win, wParam, lParam);
        break;
    }
	case WM_INPUT:
	{
		if (infocus) {
			UINT size = 0;
			PRAWINPUT raw = NULL;

			/* Here we read the raw input data */
			GetRawInputData((HRAWINPUT)lParam, RID_INPUT, NULL, &size, sizeof(RAWINPUTHEADER));
			raw = (PRAWINPUT)malloc(size);
			if (GetRawInputData((HRAWINPUT)lParam, RID_INPUT, raw, &size, sizeof(RAWINPUTHEADER)) == size) {
				switch (raw->header.dwType)
				{
				case RIM_TYPEKEYBOARD:
					if (!ImGuiWantsKeyboardCapture()) keyboard_handle(raw);
					break;
				}
			}
			free(raw);
		}
		break;
	}
	}
}

extern SDL_Window* sdl_win;
/* For the Windows platform, this is the start of the application. */
int WINAPI
WinMain(HINSTANCE hInst, HINSTANCE hPrev, LPSTR lpszArg, int nCmdShow)
{
    SDL_Event event;

    char **argv = NULL;
    int	argc;
    wchar_t * AppID = L"86Box.86Box\0";
	RAWINPUTDEVICE ridev;
	TASKDIALOGCONFIG tdconfig;
	SDL_SysWMinfo wmInfo;

	ZeroMemory(&tdconfig, sizeof(TASKDIALOGCONFIG));
	ZeroMemory(&wmInfo, sizeof(SDL_SysWMinfo));

    SetCurrentProcessExplicitAppUserModelID(AppID);

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
    sprintf(emu_version, "%s v%s", EMU_NAME, EMU_VERSION);

    /* First, set our (default) language. */
	lang_sys = GetThreadUILanguage();
    set_language(DEFAULT_LANGUAGE);

    /* Process the command line for options. */
    argc = ProcessCommandLine(&argv);
	LoadCommonStrings();

    SDL_Init(0);
    pc_init(argc, argv);
	if (!pc_init_modules()) {
		/* Dang, no ROMs found at all! */
		tdconfig.pszMainInstruction = MAKEINTRESOURCE(IDS_2120);
		tdconfig.pszContent = MAKEINTRESOURCE(IDS_2056);
		TaskDialogIndirect(&tdconfig, NULL, NULL, NULL);
		return(6);
	}
    
    eventthread = SDL_ThreadID();
    blitmtx = SDL_CreateMutex();
    if (!blitmtx)
    {
        fprintf(stderr, "Failed to create blit mutex: %s", SDL_GetError());
        return -1;
    }
    mousemutex = SDL_CreateMutex();
    switch (vid_api)
    {
        case 0:
            sdl_inits(0);
            break;
        default:
        case 1:
            sdl_inith(0);
            break;
        case 2:
            sdl_initho(0);
            break;
    }

	/* Initialize SDL2 WM structure. */
	SDL_VERSION(&wmInfo.version);
	SDL_GetWindowWMInfo(sdl_win, &wmInfo);

	/* Initialize the RawInput (keyboard) module. */
	memset(&ridev, 0x00, sizeof(ridev));
	ridev.usUsagePage = 0x01;
	ridev.usUsage = 0x06;
	ridev.dwFlags = RIDEV_NOHOTKEYS;
	ridev.hwndTarget = wmInfo.info.win.window;	/* current focus window */
	if (!RegisterRawInputDevices(&ridev, 1, sizeof(ridev))) {
		tdconfig.pszContent = MAKEINTRESOURCE(IDS_2105);
		TaskDialogIndirect(&tdconfig, NULL, NULL, NULL);
		return(4);
	}
	keyboard_getkeymap();
	SDL_SetWindowsMessageHook(PreSDLWinMessageHook, NULL);
    if (start_in_fullscreen)
    {
        video_fullscreen = 1;
	    sdl_set_fs(1);
    }

    /* Fire up the machine. */
    pc_reset_hard_init();

    /* Set the PAUSE mode depending on the renderer. */
    //plat_pause(0);

    /* Initialize the rendering window, or fullscreen. */

    do_start();
#ifndef USE_CLI
    thread_create(monitor_thread, NULL);
#endif
    SDL_AddTimer(1000, timer_onesec, NULL);
    InitImGui();
    while (!is_quit)
    {
        while (SDL_PollEvent(&event))
	    {
            if (!mouse_capture) ImGui_ImplSDL2_ProcessEvent(&event);
            switch(event.type)
            {
                case SDL_QUIT:
                    exit_event = 1;
                    break;
                case SDL_MOUSEWHEEL:
                {
                    if (ImGuiWantsMouseCapture()) break;
                    if (mouse_capture)
                    {
                        if (event.wheel.direction == SDL_MOUSEWHEEL_FLIPPED)
                        {
                            event.wheel.x *= -1;
                            event.wheel.y *= -1;
                        }
                        SDL_LockMutex(mousemutex);
                        mousedata.deltaz = event.wheel.y;
                        SDL_UnlockMutex(mousemutex);
                    }
                    break;
                }
                case SDL_MOUSEMOTION:
                {
                    if (ImGuiWantsMouseCapture()) break;
                    if (mouse_capture)
                    {
                        SDL_LockMutex(mousemutex);
                        mousedata.deltax += event.motion.xrel;
                        mousedata.deltay += event.motion.yrel;
                        SDL_UnlockMutex(mousemutex);
                    }
                    break;
                }
                case SDL_MOUSEBUTTONDOWN:
                case SDL_MOUSEBUTTONUP:
                {
                    if (ImGuiWantsMouseCapture()) break;
                    if ((event.button.button == SDL_BUTTON_LEFT)
                    && !(mouse_capture)
                    && event.button.state == SDL_RELEASED
                    && ((event.button.x <= sdl_w && event.button.y <= sdl_h) || video_fullscreen))
                    {
                        plat_mouse_capture(1);
                        break;
                    }
                    if (mouse_get_buttons() < 3 && event.button.button == SDL_BUTTON_MIDDLE)
                    {
                        plat_mouse_capture(0);
                        break;
                    }
                    if (mouse_capture)
                    {
                        int buttonmask = 0;

                        switch(event.button.button)
                        {
                            case SDL_BUTTON_LEFT:
                                buttonmask = 1;
                                break;
                            case SDL_BUTTON_RIGHT:
                                buttonmask = 2;
                                break;
                            case SDL_BUTTON_MIDDLE:
                                buttonmask = 4;
                                break;
                        }
                        SDL_LockMutex(mousemutex);
                        if (event.button.state == SDL_PRESSED)
                        {
                            mousedata.mousebuttons |= buttonmask;
                        }
                        else mousedata.mousebuttons &= ~buttonmask;
                        SDL_UnlockMutex(mousemutex);
                    }
                    break;
                }
                case SDL_RENDER_DEVICE_RESET:
                case SDL_RENDER_TARGETS_RESET:
                    {    
                        extern void sdl_reinit_texture();
                        sdl_reinit_texture();
                        break;
                    }
                case SDL_WINDOWEVENT:
                {
                    switch (event.window.type)
                    {
					case SDL_WINDOWEVENT_FOCUS_LOST:
					{
						infocus = 0;
						break;
					}
					case SDL_WINDOWEVENT_FOCUS_GAINED:
					{
						infocus = 1;
						break;
					}
                    case SDL_WINDOWEVENT_SIZE_CHANGED:
                    {
                        sdl_w = window_w = event.window.data1;
                        sdl_h = window_h = event.window.data2;
                        if (window_remember) config_save();
                        break;
                    }
                    case SDL_WINDOWEVENT_MOVED:
                    {
                        if (strncasecmp(SDL_GetCurrentVideoDriver(), "wayland", 7) != 0)
                        {
                            window_x = event.window.data1;
                            window_y = event.window.data2;
                            if (window_remember || (vid_resize & 2)) config_save();
                        }
                        break;
                    }
                    default:
                        break;
                    }
                }
            }
	    }
        if (mouse_capture && keyboard_ismsexit())
        {
            plat_mouse_capture(0);
        }
        if (blitreq)
        {
            extern void sdl_blit(int x, int y, int w, int h);
            sdl_blit(params.x, params.y, params.w, params.h);
        }
        else
        {
            SDL_Rect srcrect;
            memcpy(&srcrect, &params, sizeof(SDL_Rect));
            sdl_real_blit(&srcrect);
        }
        if (title_set)
        {
            extern void ui_window_title_real();
            ui_window_title_real();
        }
        if (video_fullscreen && keyboard_isfsexit())
        {
            sdl_set_fs(0);
            video_fullscreen = 0;
        }
        if (!(video_fullscreen) && keyboard_isfsenter())
        {
            sdl_set_fs(1);
            video_fullscreen = 1;
        }
        if (fullscreen_pending)
        {
            sdl_set_fs(video_fullscreen);
            fullscreen_pending = 0;
        }
        if ((keyboard_recv(0x1D) || keyboard_recv(0x11D)) && keyboard_recv(0x58))
        {
            pc_send_cad();
        }
        if ((keyboard_recv(0x1D) || keyboard_recv(0x11D)) && keyboard_recv(0x57))
        {
            take_screenshot();
        }
        if (exit_event)
        {
            do_stop();
            break;
        }
    }
    printf("\n");
    SDL_DestroyMutex(blitmtx);
    SDL_DestroyMutex(mousemutex);
    ImGui_ImplSDL2_Shutdown();
    SDL_Quit();
    if (f_rl_callback_handler_remove) f_rl_callback_handler_remove();
    return 0;
}




static uint64_t
plat_get_ticks_common(void)
{
    uint64_t EndingTime, ElapsedMicroseconds;
    if (first_use) {
	Frequency = SDL_GetPerformanceFrequency();
	StartingTime = SDL_GetPerformanceCounter();
	first_use = 0;
    }
    EndingTime = SDL_GetPerformanceCounter();
    ElapsedMicroseconds = ((EndingTime - StartingTime) * 1000000) / Frequency;
    return ElapsedMicroseconds;
}

uint32_t
plat_get_ticks(void)
{
	return (uint32_t)(plat_get_ticks_common() / 1000);
}

uint32_t
plat_get_micro_ticks(void)
{
	return (uint32_t)plat_get_ticks_common();
}

void
plat_delay_ms(uint32_t count)
{
    SDL_Delay(count);
}


/* Return the VIDAPI number for the given name. */
int
plat_vidapi(char* api)
{
    if (_strnicmp(api, "sdl_software", sizeof("sdl_software") - 1) == 0) return 0;
    if (_strnicmp(api, "default", sizeof("default") - 1) == 0) return 1;
    if (_strnicmp(api, "sdl_opengl", sizeof("sdl_opengl") - 1) == 0) return 2;
    return 0;
}


/* Return the VIDAPI name for the given number. */
char*
plat_vidapi_name(int i)
{
    switch (i)
    {
        case 0:
            return "sdl_software";
        case 1:
        default:
            return "default";
        case 2:
            return "sdl_opengl";
    }
}


int
plat_setvid(int api)
{
    return(0);
}


/* Tell the renderers about a new screen resolution. */
void
plat_vidsize(int x, int y)
{
}


void
plat_vidapi_enable(int enable)
{

}


int
get_vidpause(void)
{
	return 0;
}


void
plat_setfullscreen(int on)
{
  sdl_set_fs(on);
  device_force_redraw();
}


void
plat_vid_reload_options(void)
{
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
    SDL_LockMutex(blitmtx);
}


void	/* plat_ */
endblit(void)
{
    SDL_UnlockMutex(blitmtx);
}
