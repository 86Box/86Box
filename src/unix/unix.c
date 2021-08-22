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
#include <86box/config.h>
#include <86box/plat.h>
#include <86box/plat_dynld.h>
#include <86box/device.h>
#include <86box/gameport.h>
#include <86box/unix_sdl.h>
#include <86box/timer.h>
#include <86box/nvr.h>
#include <86box/unix_sdl.h>
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

void mouse_poll()
{}

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
    return 0;
}

int	ui_msgbox_header(int flags, void *message, void* header)
{
    // Parameters that are passed will crash the program so keep these off for the time being.
    return 0;
}

void plat_get_exe_name(char *s, int size)
{
    char* basepath = SDL_GetBasePath();
    snprintf(s, size, "%s/86box", basepath);
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

void ui_window_title(wchar_t* str) {}
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
        if (SDL_PollEvent(&event))
        switch(event.type)
        {
            case SDL_QUIT:
                do_stop();
                break;
            
        }
        if (blitreq)
        {
            extern void sdl_blit(int x, int y, int y1, int y2, int w, int h);
            sdl_blit(params.x, params.y, params.y1, params.y2, params.w, params.h);
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