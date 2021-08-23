#ifdef __linux__
#define _FILE_OFFSET_BITS 64
#define _LARGEFILE64_SOURCE 1
#endif
#include <stdlib.h>
#include <stdio.h>
#include <time.h>
#include <sys/time.h>
#include <sys/param.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <errno.h>
#include <inttypes.h>
#include <dlfcn.h>
#include <SDL2/SDL.h>
#include <86box/86box.h>
#include <86box/keyboard.h>
#include <86box/mouse.h>
#include <86box/config.h>
#include <86box/plat.h>
#include <86box/plat_dynld.h>
#include <86box/device.h>
#include <86box/gameport.h>
#include <86box/unix_sdl.h>
#include <86box/timer.h>
#include <86box/nvr.h>

static int	first_use = 1;
static uint64_t	StartingTime;
static uint64_t Frequency;
int rctrl_is_lalt;
int	update_icons;
int	kbd_req_capture;
int hide_status_bar;
int fixed_size_x = 640;
int fixed_size_y = 480;
plat_joystick_t	plat_joystick_state[MAX_PLAT_JOYSTICKS];
joystick_t	joystick_state[MAX_JOYSTICKS];
int		joysticks_present;
SDL_mutex *blitmtx;

static const uint16_t sdl_to_xt[0x200] =
{
    [SDL_SCANCODE_ESCAPE] = 0x01,
    [SDL_SCANCODE_1] = 0x02,
    [SDL_SCANCODE_2] = 0x03,
    [SDL_SCANCODE_3] = 0x04,
    [SDL_SCANCODE_4] = 0x05,
    [SDL_SCANCODE_5] = 0x06,
    [SDL_SCANCODE_6] = 0x07,
    [SDL_SCANCODE_7] = 0x08,
    [SDL_SCANCODE_8] = 0x09,
    [SDL_SCANCODE_9] = 0x0A,
    [SDL_SCANCODE_0] = 0x0B,
    [SDL_SCANCODE_MINUS] = 0x0C,
    [SDL_SCANCODE_EQUALS] = 0x0D,
    [SDL_SCANCODE_BACKSPACE] = 0x0E,
    [SDL_SCANCODE_TAB] = 0x0F,
    [SDL_SCANCODE_Q] = 0x10,
    [SDL_SCANCODE_W] = 0x11,
    [SDL_SCANCODE_E] = 0x12,
    [SDL_SCANCODE_R] = 0x13,
    [SDL_SCANCODE_T] = 0x14,
    [SDL_SCANCODE_Y] = 0x15,
    [SDL_SCANCODE_U] = 0x16,
    [SDL_SCANCODE_I] = 0x17,
    [SDL_SCANCODE_O] = 0x18,
    [SDL_SCANCODE_P] = 0x19,
    [SDL_SCANCODE_LEFTBRACKET] = 0x1A,
    [SDL_SCANCODE_RIGHTBRACKET] = 0x1B,
    [SDL_SCANCODE_RETURN] = 0x1C,
    [SDL_SCANCODE_LCTRL] = 0x1D,
    [SDL_SCANCODE_A] = 0x1E,
    [SDL_SCANCODE_S] = 0x1F,
    [SDL_SCANCODE_D] = 0x20,
    [SDL_SCANCODE_F] = 0x21,
    [SDL_SCANCODE_G] = 0x22,
    [SDL_SCANCODE_H] = 0x23,
    [SDL_SCANCODE_J] = 0x24,
    [SDL_SCANCODE_K] = 0x25,
    [SDL_SCANCODE_L] = 0x26,
    [SDL_SCANCODE_SEMICOLON] = 0x27,
    [SDL_SCANCODE_APOSTROPHE] = 0x28,
    [SDL_SCANCODE_GRAVE] = 0x29,
    [SDL_SCANCODE_LSHIFT] = 0x2A,
    [SDL_SCANCODE_BACKSLASH] = 0x2B,
    [SDL_SCANCODE_Z] = 0x2C,
    [SDL_SCANCODE_X] = 0x2D,
    [SDL_SCANCODE_C] = 0x2E,
    [SDL_SCANCODE_V] = 0x2F,
    [SDL_SCANCODE_B] = 0x30,
    [SDL_SCANCODE_N] = 0x31,
    [SDL_SCANCODE_M] = 0x32,
    [SDL_SCANCODE_COMMA] = 0x33,
    [SDL_SCANCODE_PERIOD] = 0x34,
    [SDL_SCANCODE_SLASH] = 0x35,
    [SDL_SCANCODE_RSHIFT] = 0x36,
    [SDL_SCANCODE_KP_MULTIPLY] = 0x37,
    [SDL_SCANCODE_LALT] = 0x38,
    [SDL_SCANCODE_SPACE] = 0x39,
    [SDL_SCANCODE_CAPSLOCK] = 0x3A,
    [SDL_SCANCODE_F1] = 0x3B,
    [SDL_SCANCODE_F2] = 0x3C,
    [SDL_SCANCODE_F3] = 0x3D,
    [SDL_SCANCODE_F4] = 0x3E,
    [SDL_SCANCODE_F5] = 0x3F,
    [SDL_SCANCODE_F6] = 0x40,
    [SDL_SCANCODE_F7] = 0x41,
    [SDL_SCANCODE_F8] = 0x42,
    [SDL_SCANCODE_F9] = 0x43,
    [SDL_SCANCODE_F10] = 0x44,
    [SDL_SCANCODE_NUMLOCKCLEAR] = 0x45,
    [SDL_SCANCODE_SCROLLLOCK] = 0x46,
    [SDL_SCANCODE_HOME] = 0x47,
    [SDL_SCANCODE_UP] = 0x48,
    [SDL_SCANCODE_PAGEUP] = 0x49,
    [SDL_SCANCODE_KP_MINUS] = 0x4A,
    [SDL_SCANCODE_LEFT] = 0x4B,
    [SDL_SCANCODE_KP_5] = 0x4C,
    [SDL_SCANCODE_RIGHT] = 0x4D,
    [SDL_SCANCODE_KP_PLUS] = 0x4E,
    [SDL_SCANCODE_END] = 0x4F,
    [SDL_SCANCODE_DOWN] = 0x50,
    [SDL_SCANCODE_PAGEDOWN] = 0x51,
    [SDL_SCANCODE_INSERT] = 0x52,
    [SDL_SCANCODE_DELETE] = 0x53,
    [SDL_SCANCODE_F11] = 0x57,
    [SDL_SCANCODE_F12] = 0x58,

    [SDL_SCANCODE_KP_ENTER] = 0x11c,
    [SDL_SCANCODE_RCTRL] = 0x11d,
    [SDL_SCANCODE_KP_DIVIDE] = 0x135,
    [SDL_SCANCODE_RALT] = 0x138,
    [SDL_SCANCODE_KP_9] = 0x49,
    [SDL_SCANCODE_KP_8] = 0x48,
    [SDL_SCANCODE_KP_7] = 0x47,
    [SDL_SCANCODE_KP_6] = 0x4D,
    [SDL_SCANCODE_KP_4] = 0x4B,
    [SDL_SCANCODE_KP_3] = 0x51,
    [SDL_SCANCODE_KP_2] = 0x50,
    [SDL_SCANCODE_KP_1] = 0x4F
};

typedef struct sdl_blit_params
{
    int x, y, y1, y2, w, h;
} sdl_blit_params;

sdl_blit_params params = { 0, 0, 0, 0, 0, 0 };
int blitreq = 0;

void* dynld_module(const char *name, dllimp_t *table)
{
    dllimp_t* imp;
    void* modhandle = dlopen(name, RTLD_LAZY | RTLD_GLOBAL);
    if (modhandle)
    {
        for (imp = table; imp->name != NULL; imp++)
        {
            if ((*(void**)imp->func = dlsym(modhandle, imp->name)) == NULL)
            {
                dlclose(modhandle);
                return NULL;
            }
        }
    }
    return modhandle;
}

void
plat_tempfile(char *bufp, char *prefix, char *suffix)
{
    struct tm* calendertime;
    struct timeval t;
    time_t curtime;

    if (prefix != NULL)
	sprintf(bufp, "%s-", prefix);
      else
	strcpy(bufp, "");
    gettimeofday(&t, NULL);
    curtime = time(NULL);
    calendertime = localtime(&curtime);
    sprintf(&bufp[strlen(bufp)], "%d%02d%02d-%02d%02d%02d-%03d%s", calendertime->tm_year, calendertime->tm_mon, calendertime->tm_mday, calendertime->tm_hour, calendertime->tm_min, calendertime->tm_sec, t.tv_usec / 1000, suffix);
}

int
plat_getcwd(char *bufp, int max)
{
    return getcwd(bufp, max) != 0;
}

int
plat_chdir(char* str)
{
    return chdir(str);
}

void dynld_close(void *handle)
{
	dlclose(handle);
}

wchar_t* plat_get_string(int i)
{
    switch (i)
    {
        case IDS_2077:
            return L"Click to capture mouse.";
        case IDS_2078:
            return L"Press CTRL-END to release mouse";
        case IDS_2079:
            return L"Press CTRL-END or middle button to release mouse";
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
plat_path_abs(char *path)
{
    return path[0] == '/';
}

void
plat_path_slash(char *path)
{
    if ((path[strlen(path)-1] != '/')) {
	strcat(path, "/");
    }
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
plat_get_basename(const char *path)
{
    int c = (int)strlen(path);

    while (c > 0) {
	if (path[c] == '/')
	   return((char *)&path[c]);
       c--;
    }

    return((char *)path);
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

int
plat_dir_check(char *path)
{
    struct stat dummy;
    if (stat(path, &dummy) < 0)
    {
        return 0;
    }
    return S_ISDIR(dummy.st_mode);
}

int
plat_dir_create(char *path)
{
    return mkdir(path, S_IRWXU);
}

uint64_t
plat_timer_read(void)
{
    return SDL_GetPerformanceCounter();
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

void plat_remove(char* path)
{
    remove(path);
}

void
ui_sb_update_icon_state(int tag, int state)
{

}

void
ui_sb_update_icon(int tag, int active)
{

}

void
plat_delay_ms(uint32_t count)
{
    SDL_Delay(count);
}

void
ui_sb_update_tip(int arg)
{

}

void
ui_sb_update_panes()
{

}

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

    framecountx = 0;
    //title_update = 1;
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
    /* Claim the video blitter. */
    startblit();

    sdl_close();

    pc_close(thMain);

    thMain = NULL;
}

int	ui_msgbox(int flags, void *message)
{
    fprintf(stderr, "Got msgbox request. Flags: 0x%X, msgid: %llu\n", flags, (uint64_t) message);
    return 0;
}

int	ui_msgbox_header(int flags, void *message, void* header)
{
    // Parameters that are passed will crash the program so keep these off for the time being.
    fprintf(stderr, "Got msgbox request. Flags: 0x%X, msgid: %llu, hdrid: %llu\n", flags, (uint64_t) message, (uint64_t) header);
    return 0;
}

void plat_get_exe_name(char *s, int size)
{
    char* basepath = SDL_GetBasePath();
    snprintf(s, size, "%s%s", basepath, basepath[strlen(basepath) - 1] == '/' ? "86box" : "/86box");
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

wchar_t* ui_sb_bugui(wchar_t *str)
{
    return str;
}

extern void     sdl_blit(int x, int y, int y1, int y2, int w, int h);
int numlock = 0;

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


extern int real_sdl_w, real_sdl_h;
void ui_sb_set_ready(int ready) {}
int main(int argc, char** argv)
{
    SDL_Event event;

    SDL_Init(0);
    pc_init(argc, argv);
    if (! pc_init_modules()) {
        fprintf(stderr, "No ROMs found.\n");
        SDL_Quit();
        return 6;
    }
    blitmtx = SDL_CreateMutex();
    if (!blitmtx)
    {
        fprintf(stderr, "Failed to create blit mutex: %s", SDL_GetError());
        return -1;
    }

    sdl_initho();

    if (start_in_fullscreen)
	sdl_set_fs(1);

    /* Fire up the machine. */
    pc_reset_hard_init();

    /* Set the PAUSE mode depending on the renderer. */
    //plat_pause(0);

    /* Initialize the rendering window, or fullscreen. */

    do_start();
    while (!is_quit)
    {
        static int onesec_tic = 0;
        if (SDL_PollEvent(&event))
        switch(event.type)
        {
            case SDL_QUIT:
                do_stop();
                break;
            case SDL_MOUSEWHEEL:
            {
                if (mouse_capture || video_fullscreen)
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
            }
            case SDL_MOUSEMOTION:
            {
                if (mouse_capture || video_fullscreen)
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
                if ((event.button.button == SDL_BUTTON_LEFT)
                && !(mouse_capture || video_fullscreen)
                && event.button.state == SDL_RELEASED
                && event.button.x <= real_sdl_w && event.button.y <= real_sdl_h)
                {
                    plat_mouse_capture(1);
                    break;
                }
                if (mouse_get_buttons() < 3 && event.button.button == SDL_BUTTON_MIDDLE && !video_fullscreen)
                {
                    plat_mouse_capture(0);
                    break;
                }
                if (mouse_capture || video_fullscreen)
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
            case SDL_KEYDOWN:
            case SDL_KEYUP:
            {
                uint16_t xtkey = 0;
                switch(event.key.keysym.scancode)
                {
                    case SDL_SCANCODE_KP_1 ... SDL_SCANCODE_KP_0:
                    {
                        if (numlock)
                        {
                            xtkey = (event.key.keysym.scancode - SDL_SCANCODE_KP_1) + 2;
                        }
                        else xtkey = sdl_to_xt[event.key.keysym.scancode];
                        break;
                    }
                    case SDL_SCANCODE_NUMLOCKCLEAR:
                    {
                        if (event.type == SDL_KEYDOWN) numlock ^= 1;
                    }
                    default:
                        xtkey = sdl_to_xt[event.key.keysym.scancode];
                        break;
                }
                keyboard_input(event.key.state == SDL_PRESSED, xtkey);
            }
        }
        if (mouse_capture && keyboard_ismsexit())
        {
            plat_mouse_capture(0);
        }
        if (blitreq)
        {
            extern void sdl_blit(int x, int y, int y1, int y2, int w, int h);
            sdl_blit(params.x, params.y, params.y1, params.y2, params.w, params.h);
        }
        if (SDL_GetTicks() - onesec_tic >= 1000)
        {
            onesec_tic = SDL_GetTicks();
            pc_onesec();
        }
    }

    SDL_DestroyMutex(blitmtx);
    SDL_Quit();
    return 0;
}
char* plat_vidapi_name(int i)
{
    return "default";
}
void joystick_init(void) {}
void joystick_close(void) {}
void joystick_process(void) {}
void startblit()
{
    SDL_LockMutex(blitmtx);
}

void endblit()
{
    SDL_UnlockMutex(blitmtx);
}