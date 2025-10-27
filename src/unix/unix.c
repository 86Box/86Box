#ifdef __linux__
#    define _FILE_OFFSET_BITS   64
#    define _LARGEFILE64_SOURCE 1
#endif
#ifdef __HAIKU__
#include <OS.h>
#endif
#include <SDL.h>
#include <stdlib.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <sys/time.h>
#include <sys/param.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <unistd.h>
#include <errno.h>
#include <inttypes.h>
#include <dlfcn.h>
#include <wchar.h>
#include <pwd.h>
#include <stdatomic.h>

#ifdef __APPLE__
#    include "macOSXGlue.h"
#endif

#include <86box/86box.h>
#include <86box/mem.h>
#include <86box/rom.h>
#include <86box/keyboard.h>
#include <86box/mouse.h>
#include <86box/config.h>
#include <86box/path.h>
#include <86box/plat.h>
#include <86box/plat_dynld.h>
#include <86box/thread.h>
#include <86box/device.h>
#include <86box/gameport.h>
#include <86box/unix_sdl.h>
#include <86box/unix_osd.h>
#include "cpu.h"
#include <86box/timer.h>
#include <86box/nvr.h>
#include <86box/version.h>
#include <86box/video.h>
#include <86box/ui.h>
#include <86box/gdbstub.h>

#define __USE_GNU 1 /* shouldn't be done, yet it is */
#include <pthread.h>

extern SDL_Window         *sdl_win;

static int      first_use = 1;
static uint64_t StartingTime;
static uint64_t Frequency;
int             rctrl_is_lalt;
int             update_icons;
int             kbd_req_capture;
int             hide_status_bar;
int             hide_tool_bar;
int             fixed_size_x = 640;
int             fixed_size_y = 480;
extern int      title_set;
extern wchar_t  sdl_win_title[512];
plat_joystick_state_t plat_joystick_state[MAX_PLAT_JOYSTICKS];
joystick_state_t      joystick_state[GAMEPORT_MAX][MAX_JOYSTICKS];
int             joysticks_present;
SDL_mutex      *blitmtx;
SDL_threadID    eventthread;
static int      exit_event         = 0;
static int      fullscreen_pending = 0;

// Two keys to be pressed together to open the OSD, variables to make them configurable in future
static uint16_t osd_open_first_key = SDL_SCANCODE_RCTRL;
static uint16_t osd_open_second_key = SDL_SCANCODE_F11;

static const uint16_t sdl_to_xt[0x200] = {
    [SDL_SCANCODE_ESCAPE]       = 0x01,
    [SDL_SCANCODE_1]            = 0x02,
    [SDL_SCANCODE_2]            = 0x03,
    [SDL_SCANCODE_3]            = 0x04,
    [SDL_SCANCODE_4]            = 0x05,
    [SDL_SCANCODE_5]            = 0x06,
    [SDL_SCANCODE_6]            = 0x07,
    [SDL_SCANCODE_7]            = 0x08,
    [SDL_SCANCODE_8]            = 0x09,
    [SDL_SCANCODE_9]            = 0x0A,
    [SDL_SCANCODE_0]            = 0x0B,
    [SDL_SCANCODE_MINUS]        = 0x0C,
    [SDL_SCANCODE_EQUALS]       = 0x0D,
    [SDL_SCANCODE_BACKSPACE]    = 0x0E,
    [SDL_SCANCODE_TAB]          = 0x0F,
    [SDL_SCANCODE_Q]            = 0x10,
    [SDL_SCANCODE_W]            = 0x11,
    [SDL_SCANCODE_E]            = 0x12,
    [SDL_SCANCODE_R]            = 0x13,
    [SDL_SCANCODE_T]            = 0x14,
    [SDL_SCANCODE_Y]            = 0x15,
    [SDL_SCANCODE_U]            = 0x16,
    [SDL_SCANCODE_I]            = 0x17,
    [SDL_SCANCODE_O]            = 0x18,
    [SDL_SCANCODE_P]            = 0x19,
    [SDL_SCANCODE_LEFTBRACKET]  = 0x1A,
    [SDL_SCANCODE_RIGHTBRACKET] = 0x1B,
    [SDL_SCANCODE_RETURN]       = 0x1C,
    [SDL_SCANCODE_LCTRL]        = 0x1D,
    [SDL_SCANCODE_A]            = 0x1E,
    [SDL_SCANCODE_S]            = 0x1F,
    [SDL_SCANCODE_D]            = 0x20,
    [SDL_SCANCODE_F]            = 0x21,
    [SDL_SCANCODE_G]            = 0x22,
    [SDL_SCANCODE_H]            = 0x23,
    [SDL_SCANCODE_J]            = 0x24,
    [SDL_SCANCODE_K]            = 0x25,
    [SDL_SCANCODE_L]            = 0x26,
    [SDL_SCANCODE_SEMICOLON]    = 0x27,
    [SDL_SCANCODE_APOSTROPHE]   = 0x28,
    [SDL_SCANCODE_GRAVE]        = 0x29,
    [SDL_SCANCODE_LSHIFT]       = 0x2A,
    [SDL_SCANCODE_BACKSLASH]    = 0x2B,
    [SDL_SCANCODE_Z]            = 0x2C,
    [SDL_SCANCODE_X]            = 0x2D,
    [SDL_SCANCODE_C]            = 0x2E,
    [SDL_SCANCODE_V]            = 0x2F,
    [SDL_SCANCODE_B]            = 0x30,
    [SDL_SCANCODE_N]            = 0x31,
    [SDL_SCANCODE_M]            = 0x32,
    [SDL_SCANCODE_COMMA]        = 0x33,
    [SDL_SCANCODE_PERIOD]       = 0x34,
    [SDL_SCANCODE_SLASH]        = 0x35,
    [SDL_SCANCODE_RSHIFT]       = 0x36,
    [SDL_SCANCODE_KP_MULTIPLY]  = 0x37,
    [SDL_SCANCODE_LALT]         = 0x38,
    [SDL_SCANCODE_SPACE]        = 0x39,
    [SDL_SCANCODE_CAPSLOCK]     = 0x3A,
    [SDL_SCANCODE_F1]           = 0x3B,
    [SDL_SCANCODE_F2]           = 0x3C,
    [SDL_SCANCODE_F3]           = 0x3D,
    [SDL_SCANCODE_F4]           = 0x3E,
    [SDL_SCANCODE_F5]           = 0x3F,
    [SDL_SCANCODE_F6]           = 0x40,
    [SDL_SCANCODE_F7]           = 0x41,
    [SDL_SCANCODE_F8]           = 0x42,
    [SDL_SCANCODE_F9]           = 0x43,
    [SDL_SCANCODE_F10]          = 0x44,
    [SDL_SCANCODE_NUMLOCKCLEAR] = 0x45,
    [SDL_SCANCODE_SCROLLLOCK]   = 0x46,
    [SDL_SCANCODE_HOME]         = 0x147,
    [SDL_SCANCODE_UP]           = 0x148,
    [SDL_SCANCODE_PAGEUP]       = 0x149,
    [SDL_SCANCODE_KP_MINUS]     = 0x4A,
    [SDL_SCANCODE_LEFT]         = 0x14B,
    [SDL_SCANCODE_KP_5]         = 0x4C,
    [SDL_SCANCODE_RIGHT]        = 0x14D,
    [SDL_SCANCODE_KP_PLUS]      = 0x4E,
    [SDL_SCANCODE_END]          = 0x14F,
    [SDL_SCANCODE_DOWN]         = 0x150,
    [SDL_SCANCODE_PAGEDOWN]     = 0x151,
    [SDL_SCANCODE_INSERT]       = 0x152,
    [SDL_SCANCODE_DELETE]       = 0x153,
    [SDL_SCANCODE_F11]          = 0x57,
    [SDL_SCANCODE_F12]          = 0x58,

    [SDL_SCANCODE_KP_ENTER]  = 0x11c,
    [SDL_SCANCODE_RCTRL]     = 0x11d,
    [SDL_SCANCODE_KP_DIVIDE] = 0x135,
    [SDL_SCANCODE_RALT]      = 0x138,
    [SDL_SCANCODE_KP_9]      = 0x49,
    [SDL_SCANCODE_KP_8]      = 0x48,
    [SDL_SCANCODE_KP_7]      = 0x47,
    [SDL_SCANCODE_KP_6]      = 0x4D,
    [SDL_SCANCODE_KP_4]      = 0x4B,
    [SDL_SCANCODE_KP_3]      = 0x51,
    [SDL_SCANCODE_KP_2]      = 0x50,
    [SDL_SCANCODE_KP_1]      = 0x4F,
    [SDL_SCANCODE_KP_0]      = 0x52,
    [SDL_SCANCODE_KP_PERIOD] = 0x53,

    [SDL_SCANCODE_LGUI]        = 0x15B,
    [SDL_SCANCODE_RGUI]        = 0x15C,
    [SDL_SCANCODE_APPLICATION] = 0x15D,
    [SDL_SCANCODE_PRINTSCREEN] = 0x137
};

typedef struct sdl_blit_params {
    int x;
    int y;
    int w;
    int h;
} sdl_blit_params;

sdl_blit_params params  = { 0, 0, 0, 0 };
int             blitreq = 0;

void *
dynld_module(const char *name, dllimp_t *table)
{
    dllimp_t *imp;
    void     *modhandle = dlopen(name, RTLD_LAZY | RTLD_GLOBAL);

    if (modhandle) {
        for (imp = table; imp->name != NULL; imp++) {
            if ((*(void **) imp->func = dlsym(modhandle, imp->name)) == NULL) {
                dlclose(modhandle);
                return NULL;
            }
        }
    }

    return modhandle;
}

#define TMPFILE_BUFSIZE 1024  // Assumed max buffer size
void
plat_tempfile(char *bufp, char *prefix, char *suffix)
{
    struct tm     *calendertime;
    struct timeval t;
    time_t         curtime;
    size_t         used = 0;

    if (prefix != NULL)
        used = snprintf(bufp, TMPFILE_BUFSIZE, "%s-", prefix);
    else if (TMPFILE_BUFSIZE > 0)
        bufp[0] = '\0';

    gettimeofday(&t, NULL);
    curtime      = time(NULL);
    calendertime = localtime(&curtime);

    if (used < TMPFILE_BUFSIZE) {
        snprintf(bufp + used, TMPFILE_BUFSIZE - used,
                 "%d%02d%02d-%02d%02d%02d-%03" PRId32 "%s",
                 calendertime->tm_year, calendertime->tm_mon, calendertime->tm_mday,
                 calendertime->tm_hour, calendertime->tm_min, calendertime->tm_sec,
                 (int32_t)(t.tv_usec / 1000), suffix);
    }
}
#undef TMPFILE_BUFSIZE

int
plat_getcwd(char *bufp, int max)
{
    return getcwd(bufp, max) != 0;
}

int
plat_chdir(char *str)
{
    return chdir(str);
}

void
dynld_close(void *handle)
{
    dlclose(handle);
}

wchar_t *
plat_get_string(int i)
{
    switch (i) {
        case STRING_MOUSE_CAPTURE:
            return L"Click to capture mouse";
        case STRING_MOUSE_RELEASE:
            return L"Press CTRL-END to release mouse";
        case STRING_MOUSE_RELEASE_MMB:
            return L"Press CTRL-END or middle button to release mouse";
        case STRING_INVALID_CONFIG:
            return L"Invalid configuration";
        case STRING_NO_ST506_ESDI_CDROM:
            return L"MFM/RLL or ESDI CD-ROM drives never existed";
        case STRING_PCAP_ERROR_NO_DEVICES:
            return L"No PCap devices found";
        case STRING_PCAP_ERROR_INVALID_DEVICE:
            return L"Invalid PCap device";
        case STRING_GHOSTSCRIPT_ERROR_DESC:
            return L"libgs is required for automatic conversion of PostScript files to PDF.\n\nAny documents sent to the generic PostScript printer will be saved as PostScript (.ps) files.";
        case STRING_PCAP_ERROR_DESC:
            return L"Make sure libpcap is installed and that you are on a libpcap-compatible network connection.";
        case STRING_GHOSTSCRIPT_ERROR_TITLE:
            return L"Unable to initialize Ghostscript";
        case STRING_GHOSTPCL_ERROR_TITLE:
            return L"Unable to initialize GhostPCL";
        case STRING_GHOSTPCL_ERROR_DESC:
            return L"libgpcl6 is required for automatic conversion of PCL files to PDF.\n\nAny documents sent to the generic PCL printer will be saved as Printer Command Language (.pcl) files.";
        case STRING_HW_NOT_AVAILABLE_MACHINE:
            return L"Machine \"%hs\" is not available due to missing ROMs in the roms/machines directory. Switching to an available machine.";
        case STRING_HW_NOT_AVAILABLE_VIDEO:
            return L"Video card \"%hs\" is not available due to missing ROMs in the roms/video directory. Switching to an available video card.";
        case STRING_HW_NOT_AVAILABLE_TITLE:
            return L"Hardware not available";
        case STRING_MONITOR_SLEEP:
            return L"Monitor in sleep mode";
        case STRING_EDID_TOO_LARGE:
            return L"EDID file \"%ls\" is too large.";
    }
    return L"";
}

FILE *
plat_fopen(const char *path, const char *mode)
{
    return fopen(path, mode);
}

FILE *
plat_fopen64(const char *path, const char *mode)
{
    return fopen(path, mode);
}

int
path_abs(char *path)
{
    return path[0] == '/';
}

void
path_normalize(UNUSED(char *path))
{
    /* No-op. */
}

void
path_slash(char *path)
{
    if (path[strlen(path) - 1] != '/') {
        strcat(path, "/");
    }
    path_normalize(path);
}

const char *
path_get_slash(char *path)
{
    char *ret = "";

    if (path[strlen(path) - 1] != '/')
        ret =  "/";

    return ret;
}

void
plat_put_backslash(char *s)
{
    int c = strlen(s) - 1;

    if (s[c] != '/')
        s[c] = '/';
}

/* Return the last element of a pathname. */
char *
path_get_basename(const char *path)
{
    int c = (int) strlen(path);

    while (c > 0) {
        if (path[c] == '/')
            return ((char *) &path[c + 1]);
        c--;
    }

    return ((char *) path);
}
char *
path_get_filename(char *s)
{
    int c = strlen(s) - 1;

    while (c > 0) {
        if (s[c] == '/' || s[c] == '\\')
            return (&s[c + 1]);
        c--;
    }

    return s;
}

char *
path_get_extension(char *s)
{
    int c = strlen(s) - 1;

    if (c <= 0)
        return s;

    while (c && s[c] != '.')
        c--;

    if (!c)
        return (&s[strlen(s)]);

    return (&s[c + 1]);
}

void
path_append_filename(char *dest, const char *s1, const char *s2)
{
    strcpy(dest, s1);
    path_slash(dest);
    strcat(dest, s2);
}

int
plat_dir_check(char *path)
{
    struct stat stats;
    if (stat(path, &stats) < 0)
        return 0;
    return S_ISDIR(stats.st_mode);
}

int
plat_file_check(const char *path)
{
    struct stat stats;
    if (stat(path, &stats) < 0)
        return 0;
    return !S_ISDIR(stats.st_mode);
}

int
plat_dir_create(char *path)
{
    return mkdir(path, S_IRWXU);
}

void *
plat_mmap(size_t size, uint8_t executable)
{
#if defined __APPLE__ && defined MAP_JIT
    void *ret = mmap(0, size, PROT_READ | PROT_WRITE | (executable ? PROT_EXEC : 0), MAP_ANON | MAP_PRIVATE | (executable ? MAP_JIT : 0), -1, 0);
#elif defined(PROT_MPROTECT)
    void *ret = mmap(0, size, PROT_MPROTECT(PROT_READ | PROT_WRITE | (executable ? PROT_EXEC : 0)), MAP_ANON | MAP_PRIVATE, -1, 0);
#else
    void *ret = mmap(0, size, PROT_READ | PROT_WRITE | (executable ? PROT_EXEC : 0), MAP_ANON | MAP_PRIVATE, -1, 0);
#endif
    return (ret < 0) ? NULL : ret;
}

void
plat_munmap(void *ptr, size_t size)
{
    munmap(ptr, size);
}

uint64_t
plat_timer_read(void)
{
    return SDL_GetPerformanceCounter();
}

static uint64_t
plat_get_ticks_common(void)
{
    uint64_t EndingTime;
    uint64_t ElapsedMicroseconds;

    if (first_use) {
        Frequency    = SDL_GetPerformanceFrequency();
        StartingTime = SDL_GetPerformanceCounter();
        first_use    = 0;
    }
    EndingTime          = SDL_GetPerformanceCounter();
    ElapsedMicroseconds = ((EndingTime - StartingTime) * 1000000) / Frequency;

    return ElapsedMicroseconds;
}

uint32_t
plat_get_ticks(void)
{
    return (uint32_t) (plat_get_ticks_common() / 1000);
}

void
plat_remove(char *path)
{
    remove(path);
}

void ui_sb_update_icon_state(int tag, int state)
{
    osd_ui_sb_update_icon_state(tag, state);
}

void ui_sb_update_icon(int tag, int active)
{
    osd_ui_sb_update_icon(tag, active);
}

void ui_sb_update_icon_write(int tag, int active)
{
    osd_ui_sb_update_icon_write(tag, active);
}

void ui_sb_update_icon_wp(int tag, int state)
{
    osd_ui_sb_update_icon_wp(tag, state);
}

void
plat_delay_ms(uint32_t count)
{
    SDL_Delay(count);
}

void
ui_sb_update_tip(UNUSED(int arg))
{
    /* No-op. */
}

void
ui_sb_update_panes(void)
{
    /* No-op. */
}

void
ui_sb_update_text(void)
{
    /* No-op. */
}

void
path_get_dirname(char *dest, const char *path)
{
    int   c = (int) strlen(path);
    char *ptr = (char *) path;

    while (c > 0) {
        if (path[c] == '/' || path[c] == '\\') {
            ptr = (char *) &path[c];
            break;
        }
        c--;
    }

    /* Copy to destination. */
    while (path < ptr)
        *dest++ = *path++;
    *dest = '\0';
}

void
ui_sb_set_text_w(UNUSED(wchar_t *wstr))
{
    /* No-op. */
}

int
stricmp(const char *s1, const char *s2)
{
    return strcasecmp(s1, s2);
}

int
strnicmp(const char *s1, const char *s2, size_t n)
{
    return strncasecmp(s1, s2, n);
}

volatile int cpu_thread_run = 1;

void
main_thread(UNUSED(void *param))
{
    uint32_t old_time;
    uint32_t new_time;
    int      drawits;
    int      frames;

    SDL_SetThreadPriority(SDL_THREAD_PRIORITY_HIGH);
    framecountx = 0;
    // title_update = 1;
    old_time = SDL_GetTicks();
    drawits = frames = 0;
    while (!is_quit && cpu_thread_run)
    {
        /* See if it is time to run a frame of code. */
        new_time = SDL_GetTicks();

#ifdef USE_GDBSTUB
        if (gdbstub_next_asap && (drawits <= 0))
            drawits = 10;
        else
            drawits += (new_time - old_time);
#else
        drawits += (new_time - old_time);
#endif

        old_time = new_time;
        if (drawits > 0 && !dopause) {
            /* Yes, so do one frame now. */
            drawits -= force_10ms ? 10 : 1;
            if (drawits > 50)
                drawits = 0;

            /* Run a block of code. */
            pc_run();

            /* Every 200 frames we save the machine status. */
            if (++frames >= (force_10ms ? 200 : 2000) && nvr_dosave) {
                nvr_save();
                nvr_dosave = 0;
                frames     = 0;
            }
        }
        else /* Just so we dont overload the host OS. */
            SDL_Delay(1);

        /* If needed, handle a screen resize. */
        if (atomic_load(&doresize_monitors[0]) && !video_fullscreen && !is_quit) {

            if (vid_resize & 2)
                plat_resize(fixed_size_x, fixed_size_y, 0);
            else
                plat_resize(scrnsz_x, scrnsz_y, 0);

            atomic_store(&doresize_monitors[0], 1);
        }
    }

    is_quit = 1;
}

thread_t *thMain = NULL;

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
    if (SDL_ThreadID() != eventthread) {
        exit_event = 1;
        return;
    }
    if (blitreq) {
        blitreq = 0;
        video_blit_complete();
    }

    while (SDL_TryLockMutex(blitmtx) == SDL_MUTEX_TIMEDOUT) {
        if (blitreq) {
            blitreq = 0;
            video_blit_complete();
        }
    }
    startblit();

    is_quit = 1;
    sdl_close();

    pc_close(thMain);

    thMain = NULL;
}

int
ui_msgbox(int flags, void *message)
{
    return ui_msgbox_header(flags, NULL, message);
}

int
ui_msgbox_header(int flags, void *header, void *message)
{
    SDL_MessageBoxData       msgdata;
    SDL_MessageBoxButtonData msgbtn;

    if (!header) {
        if (flags & MBX_ANSI)
            header = (void *) "86Box";
        else
            header = (void *) L"86Box";
    }

    msgbtn.buttonid = 1;
    msgbtn.text     = "OK";
    msgbtn.flags    = 0;
    memset(&msgdata, 0, sizeof(SDL_MessageBoxData));
    msgdata.numbuttons = 1;
    msgdata.buttons    = &msgbtn;
    int msgflags       = 0;
    if (msgflags & MBX_FATAL)
        msgflags |= SDL_MESSAGEBOX_ERROR;
    else if (msgflags & MBX_ERROR || msgflags & MBX_WARNING)
        msgflags |= SDL_MESSAGEBOX_WARNING;
    else
        msgflags |= SDL_MESSAGEBOX_INFORMATION;
    msgdata.flags = msgflags;
    if (flags & MBX_ANSI) {
        int button      = 0;
        msgdata.title   = header;
        msgdata.message = message;
        SDL_ShowMessageBox(&msgdata, &button);
        return button;
    } else {
        int   button    = 0;
        char *res       = SDL_iconv_string("UTF-8", sizeof(wchar_t) == 2 ? "UTF-16LE" : "UTF-32LE", (char *) message, wcslen(message) * sizeof(wchar_t) + sizeof(wchar_t));
        char *res2      = SDL_iconv_string("UTF-8", sizeof(wchar_t) == 2 ? "UTF-16LE" : "UTF-32LE", (char *) header, wcslen(header) * sizeof(wchar_t) + sizeof(wchar_t));
        msgdata.message = res;
        msgdata.title   = res2;
        SDL_ShowMessageBox(&msgdata, &button);
        free(res);
        free(res2);
        return button;
    }

    return 0;
}

void
plat_get_exe_name(char *s, int size)
{
    char *basepath = SDL_GetBasePath();

    snprintf(s, size, "%s%s", basepath, basepath[strlen(basepath) - 1] == '/' ? "86box" : "/86box");
}

void
plat_power_off(void)
{
    confirm_exit_cmdl = 0;
    nvr_save();
    config_save();

    /* Deduct a sufficiently large number of cycles that no instructions will
       run before the main thread is terminated */
    cycles -= 99999999;

    cpu_thread_run = 0;
}

void
ui_sb_bugui(UNUSED(char *str))
{
    /* No-op. */
}

extern void sdl_blit(int x, int y, int w, int h);

typedef struct mouseinputdata {
    int deltax;
    int deltay;
    int deltaz;
    int mousebuttons;
} mouseinputdata;

SDL_mutex *mousemutex;
int        real_sdl_w;
int        real_sdl_h;

void
ui_sb_set_ready(UNUSED(int ready))
{
    /* No-op. */
}

// From musl.
char *
local_strsep(char **str, const char *sep)
{
    char *s = *str;
    char *end;

    if (!s)
        return NULL;
    end = s + strcspn(s, sep);
    if (*end)
        *end++ = 0;
    else
        end = 0;
    *str = end;

    return s;
}

void
plat_pause(int p)
{
    static wchar_t oldtitle[512];
    wchar_t        title[512];

    if ((!!p) == dopause)
        return;

    if ((p == 0) && (time_sync & TIME_SYNC_ENABLED))
        nvr_time_sync();

    do_pause(p);
    if (p) {
        wcsncpy(oldtitle, ui_window_title(NULL), sizeof_w(oldtitle) - 1);
        wcscpy(title, oldtitle);
        wcscat(title, L" - PAUSED");
        ui_window_title(title);
    } else {
        ui_window_title(oldtitle);
    }
}

#define TMP_PATH_BUFSIZE 1024
void
plat_init_rom_paths(void)
{
#ifndef __APPLE__
    const char *xdg_data_home = getenv("XDG_DATA_HOME");
    if (xdg_data_home) {
        char xdg_rom_path[TMP_PATH_BUFSIZE] = {0};
        size_t used = snprintf(xdg_rom_path, sizeof(xdg_rom_path), "%s/", xdg_data_home);
        if (used < sizeof(xdg_rom_path))
            used += snprintf(xdg_rom_path + used, sizeof(xdg_rom_path) - used, "86Box/");
        if (used < sizeof(xdg_rom_path) && !plat_dir_check(xdg_rom_path))
            plat_dir_create(xdg_rom_path);
        if (used < sizeof(xdg_rom_path))
            used += snprintf(xdg_rom_path + used, sizeof(xdg_rom_path) - used, "roms/");
        if (used < sizeof(xdg_rom_path) && !plat_dir_check(xdg_rom_path))
            plat_dir_create(xdg_rom_path);
        if (used < sizeof(xdg_rom_path))
            rom_add_path(xdg_rom_path);
    } else {
        const char *home = getenv("HOME");
        if (!home) {
            struct passwd *pw = getpwuid(getuid());
            if (pw)
                home = pw->pw_dir;
        }

        if (home) {
            char home_rom_path[TMP_PATH_BUFSIZE] = {0};
            size_t used = snprintf(home_rom_path, sizeof(home_rom_path),
                                   "%s/.local/share/86Box/", home);
            if (used < sizeof(home_rom_path) && !plat_dir_check(home_rom_path))
                plat_dir_create(home_rom_path);
            if (used < sizeof(home_rom_path))
                used += snprintf(home_rom_path + used,
                                 sizeof(home_rom_path) - used, "roms/");
            if (used < sizeof(home_rom_path) && !plat_dir_check(home_rom_path))
                plat_dir_create(home_rom_path);
            if (used < sizeof(home_rom_path))
                rom_add_path(home_rom_path);
        }
    }

    const char *xdg_data_dirs = getenv("XDG_DATA_DIRS");
    if (xdg_data_dirs) {
        char *xdg_rom_paths = strdup(xdg_data_dirs);
        if (xdg_rom_paths) {
            // Trim trailing colons
            size_t len = strlen(xdg_rom_paths);
            while (len > 0 && xdg_rom_paths[len - 1] == ':')
                xdg_rom_paths[--len] = '\0';

            char *saveptr = NULL;
            char *cur_xdg = strtok_r(xdg_rom_paths, ":", &saveptr);
            while (cur_xdg) {
                char real_xdg_rom_path[TMP_PATH_BUFSIZE] = {0};
                size_t used = snprintf(real_xdg_rom_path,
                                       sizeof(real_xdg_rom_path),
                                       "%s/86Box/roms/", cur_xdg);
                if (used < sizeof(real_xdg_rom_path))
                    rom_add_path(real_xdg_rom_path);
                cur_xdg = strtok_r(NULL, ":", &saveptr);
            }

            free(xdg_rom_paths);
        }
    } else {
        rom_add_path("/usr/local/share/86Box/roms/");
        rom_add_path("/usr/share/86Box/roms/");
    }
#else
    char default_rom_path[TMP_PATH_BUFSIZE] = {0};
    getDefaultROMPath(default_rom_path);
    rom_add_path(default_rom_path);
#endif
}
#undef TMP_PATH_BUFSIZE

void
plat_get_global_config_dir(char *outbuf, const size_t len)
{
    return plat_get_global_data_dir(outbuf, len);
}

void
plat_get_global_data_dir(char *outbuf, const size_t len)
{
    if (portable_mode) {
        strncpy(outbuf, exe_path, len);
    } else {
        char *prefPath = SDL_GetPrefPath(NULL, "86Box");
        strncpy(outbuf, prefPath, len);
        SDL_free(prefPath);
    }

    path_slash(outbuf);
}

void
plat_get_temp_dir(char *outbuf, uint8_t len)
{
    const char *tmpdir = getenv("TMPDIR");
    if (tmpdir == NULL) {
        tmpdir = "/tmp";
    }
    strncpy(outbuf, tmpdir, len);
    path_slash(outbuf);
}

void
plat_get_vmm_dir(char *outbuf, const size_t len)
{
    // Return empty string. SDL 86Box does not have a VM manager
    if (len > 0)
        outbuf[0] = 0;
}

bool
process_media_commands_3(uint8_t *id, char *fn, uint8_t *wp, char **xargv, int cmdargc)
{
    bool err = false;

    *id      = atoi(xargv[1]);

    if (xargv[2][0] == '\'' || xargv[2][0] == '"') {
        for (int curarg = 2; curarg < cmdargc; curarg++) {
            if (strlen(fn) + strlen(xargv[curarg]) >= PATH_MAX) {
                err = true;
                fprintf(stderr, "Path name too long.\n");
            }
            strcat(fn, xargv[curarg] + (xargv[curarg][0] == '\'' || xargv[curarg][0] == '"'));
            if (fn[strlen(fn) - 1] == '\''
                || fn[strlen(fn) - 1] == '"') {
                if (curarg + 1 < cmdargc) {
                    *wp = atoi(xargv[curarg + 1]);
                }
                break;
            }
            strcat(fn, " ");
        }
    } else {
        if (strlen(xargv[2]) < PATH_MAX) {
            strcpy(fn, xargv[2]);
            *wp = atoi(xargv[3]);
        } else {
            fprintf(stderr, "Path name too long.\n");
            err = true;
        }
    }
    if (fn[strlen(fn) - 1] == '\''
        || fn[strlen(fn) - 1] == '"')
        fn[strlen(fn) - 1] = '\0';
    return err;
}

char *(*f_readline)(const char *)          = NULL;
int (*f_add_history)(const char *)         = NULL;
void (*f_rl_callback_handler_remove)(void) = NULL;

#ifdef __APPLE__
#    define LIBEDIT_LIBRARY "libedit.dylib"
#else
#    define LIBEDIT_LIBRARY "libedit.so"
#endif

uint32_t
timer_onesec(uint32_t interval, UNUSED(void *param))
{
    pc_onesec();
    return interval;
}

void
unix_executeLine(char *line)
{
    if (line) {
        char *xargv[512];
        int   cmdargc = 0;
        char *linecpy, *linespn;

        linecpy = linespn = strdup(line);
        linecpy[strcspn(linecpy, "\r\n")] = 0;

        if (!linecpy) {
            return;
        }

        if (f_add_history)
            f_add_history(linecpy);

        memset(xargv, 0, sizeof(xargv));
        while (1) {
            xargv[cmdargc++] = local_strsep(&linespn, " ");
            if (xargv[cmdargc - 1] == NULL || cmdargc >= 512)
                break;
        }
        cmdargc--;
        if (strncasecmp(xargv[0], "help", 4) == 0) {
            printf(
                "fddload <id> <filename> <wp> - Load floppy disk image into drive <id>.\n"
                "cdload <id> <filename> - Load CD-ROM image into drive <id>.\n"
                "rdiskload <id> <filename> <wp> - Load removable disk image into removable disk drive <id>.\n"
                "cartload <id> <filename> <wp> - Load cartridge image into cartridge drive <id>.\n"
                "moload <id> <filename> <wp> - Load MO image into MO drive <id>.\n\n"
                "fddeject <id> - eject disk from floppy drive <id>.\n"
                "cdeject <id> - eject disc from CD-ROM drive <id>.\n"
                "rdiskeject <id> - eject removable disk image from removable disk drive <id>.\n"
                "carteject <id> - eject cartridge from drive <id>.\n"
                "moeject <id> - eject image from MO drive <id>.\n\n"
                "hardreset - hard reset the emulated system.\n"
                "pause - pause the the emulated system.\n"
                "fullscreen - toggle fullscreen.\n"
                "version - print version and license information.\n"
                "exit - exit 86Box.\n");
        } else if (strncasecmp(xargv[0], "exit", 4) == 0) {
            exit_event = 1;
        } else if (strncasecmp(xargv[0], "version", 7) == 0) {
#    ifndef EMU_GIT_HASH
#        define EMU_GIT_HASH "0000000"
#    endif

#    if defined(__aarch64__) || defined(_M_ARM64)
#        define ARCH_STR "arm64"
#    elif defined(__x86_64) || defined(__x86_64__) || defined(__amd64) || defined(_M_X64)
#        define ARCH_STR "x86_64"
#    else
#        define ARCH_STR "unknown arch"
#    endif

#    ifdef USE_DYNAREC
#        ifdef USE_NEW_DYNAREC
#            define DYNAREC_STR "new dynarec"
#        else
#            define DYNAREC_STR "old dynarec"
#        endif
#    else
#        define DYNAREC_STR "no dynarec"
#    endif

            printf(
                "%s v%s [%s] [%s, %s]\n\n"
                "An emulator of old computers\n"
                "Authors: Miran Grča (OBattler), RichardG867, Jasmine Iwanek, TC1995, coldbrewed, Teemu Korhonen (Manaatti), "
                "Joakim L. Gilje, Adrien Moulin (elyosh), Daniel Balsom (gloriouscow), Cacodemon345, Fred N. van Kempen (waltje), "
                "Tiseno100, reenigne, and others.\n"
                "With previous core contributions from Sarah Walker, leilei, JohnElliott, greatpsycho, and others.\n\n"
                "Released under the GNU General Public License version 2 or later. See LICENSE for more information.\n",
                EMU_NAME, EMU_VERSION_FULL, EMU_GIT_HASH, ARCH_STR, DYNAREC_STR);
        } else if (strncasecmp(xargv[0], "fullscreen", 10) == 0) {
            video_fullscreen   = video_fullscreen ? 0 : 1;
            fullscreen_pending = 1;
        } else if (strncasecmp(xargv[0], "pause", 5) == 0) {
            plat_pause(dopause ^ 1);
            printf("%s", dopause ? "Paused.\n" : "Unpaused.\n");
        } else if (strncasecmp(xargv[0], "hardreset", 9) == 0) {
            pc_reset_hard();
        } else if (strncasecmp(xargv[0], "cdload", 6) == 0 && cmdargc >= 3) {
            uint8_t id;
            bool    err = false;
            char    fn[PATH_MAX];

            if (!xargv[2] || !xargv[1]) {
                free(linecpy);
                return;
            }
            id = atoi(xargv[1]);
            memset(fn, 0, sizeof(fn));
            if (xargv[2][0] == '\'' || xargv[2][0] == '"') {
                int curarg = 2;

                for (curarg = 2; curarg < cmdargc; curarg++) {
                    if (strlen(fn) + strlen(xargv[curarg]) >= PATH_MAX) {
                        err = true;
                        fprintf(stderr, "Path name too long.\n");
                    }
                    else
                    {
                        strcat(fn, xargv[curarg] + (xargv[curarg][0] == '\'' || xargv[curarg][0] == '"'));
                        if (fn[strlen(fn) - 1] == '\''
                            || fn[strlen(fn) - 1] == '"') {
                            break;
                        }
                        strcat(fn, " ");
                    }
                }
            } else {
                if (strlen(xargv[2]) < PATH_MAX) {
                    strncpy(fn, xargv[2], PATH_MAX-1);
                } else {
                    fprintf(stderr, "Path name too long.\n");
                }
            }
            if (!err) {

                if (fn[strlen(fn) - 1] == '\''
                    || fn[strlen(fn) - 1] == '"')
                    fn[strlen(fn) - 1] = '\0';
                printf("Inserting disc into CD-ROM drive %hhu: %s\n", id, fn);
                cdrom_mount(id, fn);
            }
        } else if (strncasecmp(xargv[0], "fddeject", 8) == 0 && cmdargc >= 2) {
            floppy_eject(atoi(xargv[1]));
        } else if (strncasecmp(xargv[0], "cdeject", 8) == 0 && cmdargc >= 2) {
            cdrom_mount(atoi(xargv[1]), "");
        } else if (strncasecmp(xargv[0], "moeject", 8) == 0 && cmdargc >= 2) {
            mo_eject(atoi(xargv[1]));
        } else if (strncasecmp(xargv[0], "carteject", 8) == 0 && cmdargc >= 2) {
            cartridge_eject(atoi(xargv[1]));
        } else if (strncasecmp(xargv[0], "rdiskeject", 8) == 0 && cmdargc >= 2) {
            rdisk_eject(atoi(xargv[1]));
        } else if (strncasecmp(xargv[0], "fddload", 7) == 0 && cmdargc >= 4) {
            uint8_t id;
            uint8_t wp;
            bool    err = false;
            char    fn[PATH_MAX];

            memset(fn, 0, sizeof(fn));

            if (!xargv[2] || !xargv[1]) {
                free(linecpy);
                return;
            }
            err = process_media_commands_3(&id, fn, &wp, xargv, cmdargc);
            if (!err) {
                if (fn[strlen(fn) - 1] == '\''
                    || fn[strlen(fn) - 1] == '"')
                    fn[strlen(fn) - 1] = '\0';
                printf("Inserting disk into floppy drive %c: %s\n", id + 'A', fn);
                floppy_mount(id, fn, wp);
            }
        } else if (strncasecmp(xargv[0], "moload", 7) == 0 && cmdargc >= 4) {
            uint8_t id;
            uint8_t wp;
            bool    err = false;
            char    fn[PATH_MAX];

            memset(fn, 0, sizeof(fn));

            if (!xargv[2] || !xargv[1]) {
                free(linecpy);
                return;
            }
            err = process_media_commands_3(&id, fn, &wp, xargv, cmdargc);
            if (!err) {
                if (fn[strlen(fn) - 1] == '\''
                    || fn[strlen(fn) - 1] == '"')
                    fn[strlen(fn) - 1] = '\0';
                printf("Inserting into mo drive %hhu: %s\n", id, fn);
                mo_mount(id, fn, wp);
            }
        } else if (strncasecmp(xargv[0], "cartload", 7) == 0 && cmdargc >= 4) {
            uint8_t id;
            uint8_t wp;
            bool    err = false;
            char    fn[PATH_MAX];

            memset(fn, 0, sizeof(fn));

            if (!xargv[2] || !xargv[1]) {
                free(linecpy);
                return;
            }
            err = process_media_commands_3(&id, fn, &wp, xargv, cmdargc);
            if (!err) {
                if (fn[strlen(fn) - 1] == '\''
                    || fn[strlen(fn) - 1] == '"')
                    fn[strlen(fn) - 1] = '\0';
                printf("Inserting tape into cartridge holder %hhu: %s\n", id, fn);
                cartridge_mount(id, fn, wp);
            }
        } else if (strncasecmp(xargv[0], "rdiskload", 7) == 0 && cmdargc >= 4) {
            uint8_t id;
            uint8_t wp;
            bool    err = false;
            char    fn[PATH_MAX];

            memset(fn, 0, sizeof(fn));

            if (!xargv[2] || !xargv[1]) {
                free(linecpy);
                return;
            }
            err = process_media_commands_3(&id, fn, &wp, xargv, cmdargc);
            if (!err) {
                if (fn[strlen(fn) - 1] == '\''
                    || fn[strlen(fn) - 1] == '"')
                    fn[strlen(fn) - 1] = '\0';
                printf("Inserting disk into removable disk drive %c: %s\n", id + 'A', fn);
                rdisk_mount(id, fn, wp);
            }
        }
        free(linecpy);
    }
}

void
monitor_thread(UNUSED(void *param))
{
#ifndef USE_CLI
    if (isatty(fileno(stdin)) && isatty(fileno(stdout))) {
        char  *line = NULL;
        size_t n;

        printf("86Box monitor console.\n");
        while (!exit_event)
        {
            if (feof(stdin))
                break;

#ifdef ENABLE_READLINE
            if (f_readline)
                line = f_readline("(86Box) ");
            else {
#endif
                printf("(86Box) ");
                (void) !getline(&line, &n, stdin);
#ifdef ENABLE_READLINE
            }
#endif

            unix_executeLine(line);

            free(line);
            line = NULL;
        }
    }
#endif
}

extern int gfxcard[GFXCARD_MAX];
int
main(int argc, char **argv)
{
    SDL_Event event;
    void     *libedithandle;
    int      ret = 0;

    SDL_Init(0);
    ret = pc_init(argc, argv);
    if (ret == 0)
        return 0;
    if (!pc_init_roms()) {
        ui_msgbox_header(MBX_FATAL, L"No ROMs found.", L"86Box could not find any usable ROM images.\n\nPlease download a ROM set and extract it into the \"roms\" directory.");
        SDL_Quit();
        return 6;
    }
    pc_init_modules();

    for (uint8_t i = 1; i < GFXCARD_MAX; i++)
        gfxcard[i]  = 0;
    eventthread = SDL_ThreadID();
    blitmtx     = SDL_CreateMutex();
    if (!blitmtx) {
        fprintf(stderr, "Failed to create blit mutex: %s", SDL_GetError());
        return -1;
    }
    libedithandle = dlopen(LIBEDIT_LIBRARY, RTLD_LOCAL | RTLD_LAZY);
    if (libedithandle) {
        f_readline    = dlsym(libedithandle, "readline");
        f_add_history = dlsym(libedithandle, "add_history");
        if (!f_readline) {
            fprintf(stderr, "readline in libedit not found, line editing will be limited.\n");
        }
        f_rl_callback_handler_remove = dlsym(libedithandle, "rl_callback_handler_remove");
    } else
        fprintf(stderr, "libedit not found, line editing will be limited.\n");
    mousemutex = SDL_CreateMutex();
    sdl_initho();

    if (start_in_fullscreen) {
        video_fullscreen = 1;
        sdl_set_fs(1);
    }
    /* Fire up the machine. */
    pc_reset_hard_init();

    /* Set the PAUSE mode depending on the renderer. */
    plat_pause(0);

    /* Initialize the rendering window, or fullscreen. */
    do_start();

#ifndef USE_CLI
    thread_create(monitor_thread, NULL);
#endif

    SDL_AddTimer(1000, timer_onesec, NULL);
    while (!is_quit)
    {
        static int mouse_inside = 0;
        static int osd_first_key_pressed = 0;
        static int flag_osd_open = 0;

        while (SDL_PollEvent(&event))
        {
            if (flag_osd_open == 1)
            {
                // route almost everything to the OSD
                switch (event.type)
                {
                    case SDL_QUIT:
                    {
                        exit_event = 1;
                        break;
                    }
                    case SDL_RENDER_DEVICE_RESET:
                    case SDL_RENDER_TARGETS_RESET:
                    {
                        extern void sdl_reinit_texture(void);

                        printf("reinit tex\n");
                        sdl_reinit_texture();
                        break;
                    }
                    case SDL_WINDOWEVENT:
                    {
                        switch (event.window.event) {
                            case SDL_WINDOWEVENT_ENTER:
                                mouse_inside = 1;
                                break;
                            case SDL_WINDOWEVENT_LEAVE:
                                mouse_inside = 0;
                                break;
                        }
                        break;
                    }
                    default:
                    {
                        // route everything else
                        flag_osd_open = osd_handle(event);

                        if (flag_osd_open == 0)
                        {
                            // close it
                            osd_close(event);
                        }

                        break;
                    }
                }
            }
            else
            {
                switch (event.type)
                {
                    case SDL_QUIT:
                        exit_event = 1;
                        break;
                    case SDL_MOUSEWHEEL:
                        {
                            if (mouse_capture || video_fullscreen) {
                                if (event.wheel.direction == SDL_MOUSEWHEEL_FLIPPED) {
                                    event.wheel.x *= -1;
                                    event.wheel.y *= -1;
                                }
                                SDL_LockMutex(mousemutex);
                                mouse_set_z(event.wheel.y);
                                SDL_UnlockMutex(mousemutex);
                            }
                            break;
                        }
                    case SDL_MOUSEMOTION:
                        {
                            if (mouse_capture || video_fullscreen) {
                                SDL_LockMutex(mousemutex);
                                mouse_scale(event.motion.xrel, event.motion.yrel);
                                SDL_UnlockMutex(mousemutex);
                            }
                            break;
                        }
                    /* Touch events */
                    case SDL_FINGERDOWN:
                        {
                            // Trap these but ignore them for now
                            break;
                        }
                    case SDL_FINGERUP:
                        {
                            // Trap these but ignore them for now
                            break;
                        }
                    case SDL_FINGERMOTION:
                        {
                            // See SDL_TouchFingerEvent
                            if (mouse_capture || video_fullscreen) {
                                SDL_LockMutex(mousemutex);

                                // Real multiplier is the window size
                                int w, h;
                                SDL_GetWindowSize(sdl_win, &w, &h);

                                mouse_scale((int)(event.tfinger.dx * w), (int)(event.tfinger.dy * h));
                                SDL_UnlockMutex(mousemutex);
                            }
                            break;
                        }

                    case SDL_MOUSEBUTTONDOWN:
                    case SDL_MOUSEBUTTONUP:
                        {
                            if ((event.button.button == SDL_BUTTON_LEFT)
                                && !(mouse_capture || video_fullscreen)
                                && event.button.state == SDL_RELEASED
                                && mouse_inside) {
                                plat_mouse_capture(1);
                                break;
                            }
                            if (mouse_get_buttons() < 3 && event.button.button == SDL_BUTTON_MIDDLE && !video_fullscreen) {
                                plat_mouse_capture(0);
                                break;
                            }
                            if (mouse_capture || video_fullscreen) {
                                int buttonmask = 0;

                                switch (event.button.button) {
                                    case SDL_BUTTON_LEFT:
                                        buttonmask = 1;
                                        break;
                                    case SDL_BUTTON_RIGHT:
                                        buttonmask = 2;
                                        break;
                                    case SDL_BUTTON_MIDDLE:
                                        buttonmask = 4;
                                        break;
                                    case SDL_BUTTON_X1:
                                        buttonmask = 8;
                                        break;
                                    case SDL_BUTTON_X2:
                                        buttonmask = 16;
                                        break;
                                    default:
                                        printf("Unknown mouse button %d\n", event.button.button);
                                }
                                SDL_LockMutex(mousemutex);
                                if (event.button.state == SDL_PRESSED)
                                    mouse_set_buttons_ex(mouse_get_buttons_ex() | buttonmask);
                                else
                                    mouse_set_buttons_ex(mouse_get_buttons_ex() & ~buttonmask);
                                SDL_UnlockMutex(mousemutex);
                            }
                            break;
                        }
                    case SDL_RENDER_DEVICE_RESET:
                    case SDL_RENDER_TARGETS_RESET:
                        {
                            extern void sdl_reinit_texture(void);

                            sdl_reinit_texture();
                            break;
                        }
                    case SDL_KEYDOWN:
                    case SDL_KEYUP:
                        {
                            uint16_t xtkey = 0;

                            if (event.key.keysym.scancode == osd_open_first_key)
                            {
                                if (event.type == SDL_KEYDOWN)
                                    osd_first_key_pressed = 1;
                                else
                                    osd_first_key_pressed = 0;
                            }
                            else if (osd_first_key_pressed && event.type == SDL_KEYDOWN && event.key.keysym.scancode == osd_open_second_key)
                            {
                                // open OSD!
                                flag_osd_open = osd_open(event);

                                // we can assume alt-gr has been released, tell this also to the virtual machine
                                osd_first_key_pressed = 0;
                                keyboard_input(0, sdl_to_xt[osd_open_first_key]);
                                break;
                            }
                            else
                            {
                                // invalidate osd_first_key_pressed is something happens between its keydown and keydown for G
                                osd_first_key_pressed = 0;
                            }

                            switch (event.key.keysym.scancode) {
                                default:
                                    xtkey = sdl_to_xt[event.key.keysym.scancode];
                                    break;
                            }

                            keyboard_input(event.key.state == SDL_PRESSED, xtkey);
                            break;
                        }
                    case SDL_WINDOWEVENT:
                        {
                            switch (event.window.event) {
                                case SDL_WINDOWEVENT_ENTER:
                                    mouse_inside = 1;
                                    break;
                                case SDL_WINDOWEVENT_LEAVE:
                                    mouse_inside = 0;
                                    break;
                            }
                            break;
                        }
                    default:
                    {
                        // printf("Unhandled SDL event: %d\n", event.type);
                        break;
                    }
                }
            }
        }

        if (blitreq) {
            extern void sdl_blit(int x, int y, int w, int h);
            sdl_blit(params.x, params.y, params.w, params.h);
        }
        if (title_set) {
            extern void ui_window_title_real(void);
            ui_window_title_real();
        }
        if (video_fullscreen && keyboard_isfsexit()) {
            sdl_set_fs(0);
            video_fullscreen = 0;
        }
        if (fullscreen_pending) {
            sdl_set_fs(video_fullscreen);
            fullscreen_pending = 0;
        }
        if (exit_event) {
            do_stop();
            break;
        }
    }
    printf("\n");
    SDL_DestroyMutex(blitmtx);
    SDL_DestroyMutex(mousemutex);
    SDL_Quit();
    if (f_rl_callback_handler_remove)
        f_rl_callback_handler_remove();
    return 0;
}

char *
plat_vidapi_name(UNUSED(int i))
{
    return "default";
}

/* Converts the language code string to a numeric language ID */
int
plat_language_code(UNUSED(char *langcode))
{
    /* or maybe not */
    return 0;
}

void
plat_get_cpu_string(char *outbuf, uint8_t len) {
    char cpu_string[] = "Unknown";

    strncpy(outbuf, cpu_string, len);
}

void
plat_set_thread_name(void *thread, const char *name)
{
#ifdef __APPLE__
    if (thread) /* Apple pthread can only set self's name */
        return;
    char truncated[64];
#elif defined(__NetBSD__)
    char truncated[64];
#elif defined(__HAIKU__)
    char truncated[32];
#else
    char truncated[16];
#endif
    strncpy(truncated, name, sizeof(truncated) - 1);
#ifdef __APPLE__
    pthread_setname_np(truncated);
#elif defined(__NetBSD__)
    pthread_setname_np(thread ? *((pthread_t *) thread) : pthread_self(), truncated, "%s");
#elif defined(__HAIKU__)
    rename_thread(find_thread(NULL), truncated);
#else
    pthread_setname_np(thread ? *((pthread_t *) thread) : pthread_self(), truncated);
#endif
}

/* Converts the numeric language ID to a language code string */
void
plat_language_code_r(UNUSED(int id), UNUSED(char *outbuf), UNUSED(int len))
{
    /* or maybe not */
    return;
}

void
joystick_init(void)
{
    /* No-op. */
}

void
joystick_close(void)
{
    /* No-op. */
}

void
joystick_process(uint8_t gp)
{
    /* No-op. */
}

void
startblit(void)
{
    SDL_LockMutex(blitmtx);
}

void
endblit(void)
{
    SDL_UnlockMutex(blitmtx);
}

/* API */
void
ui_sb_mt32lcd(UNUSED(char *str))
{
    /* No-op. */
}

void
ui_hard_reset_completed(void)
{
    /* No-op. */
}
