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
    WCHAR str[512];
} rc_str_t;


/* Platform Public data, specific. */
HINSTANCE	hinstance;		/* application instance */
HANDLE		ghMutex;
LCID		lang_id;		/* current language ID used */
DWORD		dwSubLangID;
int		acp_utf8;		/* Windows supports UTF-8 codepage */


/* Local data. */
//static int	vid_api_inited = 0;
static char	*argbuf;

extern bool     ImGui_ImplSDL2_Init(SDL_Window* window);
extern void     ImGui_ImplSDL2_Shutdown();
extern void     ImGui_ImplSDL2_NewFrame();
extern bool     ImGui_ImplSDL2_ProcessEvent(const SDL_Event* event);
static int	first_use = 1;
static uint64_t	StartingTime;
static uint64_t Frequency;
int rctrl_is_lalt;
int	update_icons;
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


/* Set (or re-set) the language for the application. */
void
set_language(int id)
{

}


wchar_t *
plat_get_string(int i)
{
    switch (i)
    {
        case IDS_2077:
            return L"Click to capture mouse.";
        case IDS_2078:
            return L"Press CTRL-END to release mouse";
        case IDS_2079:
            return L"Press CTRL-END or middle button to release mouse";
        case IDS_2080:
            return L"Failed to initialize FluidSynth";
        case IDS_4099:
            return L"MFM/RLL or ESDI CD-ROM drives never existed";
        case IDS_2093:
            return L"Failed to set up PCap";
        case IDS_2094:
            return L"No PCap devices found";
        case IDS_2110:
            return L"Unable to initialize FreeType";
        case IDS_2111:
            return L"Unable to initialize SDL, libsdl2 is required";
        case IDS_2131:
            return L"libfreetype is required for ESC/P printer emulation.";
        case IDS_2132:
            return L"libgs is required for automatic conversion of PostScript files to PDF.\n\nAny documents sent to the generic PostScript printer will be saved as PostScript (.ps) files.";
        case IDS_2129:
            return L"Make sure libpcap is installed and that you are on a libpcap-compatible network connection.";
        case IDS_2114:
            return L"Unable to initialize Ghostscript";
    }
    return L"";
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
void ui_sb_set_text_w(wchar_t *wstr)
{

}

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
		SDL_Delay(1);

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

int	ui_msgbox_header(int flags, void *message, void* header)
{
    if (!header) header = L"86Box";
    fwprintf(stderr, L"%s\n", header);
    fwprintf(stderr, L"==========================\n%s\n", plat_get_string((int)message));
    return 0;
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

void ui_sb_bugui(char *str)
{
    
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
	   return((char *)&path[c]);
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
    set_language(0x0409);

    /* Process the command line for options. */
    argc = ProcessCommandLine(&argv);

    SDL_Init(0);
    pc_init(argc, argv);
    if (! pc_init_modules()) {
        fprintf(stderr, "No ROMs found.\n");
        SDL_Quit();
        return 6;
    }
    
    eventthread = SDL_ThreadID();
    blitmtx = SDL_CreateMutex();
    if (!blitmtx)
    {
        fprintf(stderr, "Failed to create blit mutex: %s", SDL_GetError());
        return -1;
    }
    mousemutex = SDL_CreateMutex();
    sdl_initho(0);

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
                case SDL_KEYDOWN:
                case SDL_KEYUP:
                {
                    if (ImGuiWantsKeyboardCapture()) break;
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
                            if (window_remember) config_save();
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
plat_vidapi(char *name)
{
    /* Default value. */
    return(0);
}


/* Return the VIDAPI name for the given number. */
char *
plat_vidapi_name(int api)
{
     return "default";
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

}

void
plat_vid_reload_options(void)
{
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
