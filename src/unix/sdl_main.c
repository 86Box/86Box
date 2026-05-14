#include <stdbool.h>
#include <stdint.h>
#include <wchar.h>

#include <SDL.h>

#ifdef _WIN32
#    include <windows.h>
#endif

#include <86box/86box.h>
#include <86box/keyboard.h>
#include <86box/mouse.h>
#include <86box/plat.h>
#include <86box/thread.h>
#include <86box/unix_sdl.h>
#include <86box/unix_osd.h>
#include <86box/timer.h>
#include <86box/nvr.h>
#include <86box/version.h>
#include <86box/video.h>
#include <86box/ui.h>
#include <86box/gdbstub.h>

#include "sdl_monitor.h"

extern SDL_Window         *sdl_win;

int             rctrl_is_lalt;
int             update_icons;
int             kbd_req_capture;
int             hide_status_bar;
int             hide_tool_bar;
bool            fast_forward = false;
int             fixed_size_x = 640;
int             fixed_size_y = 480;
extern int      title_set;
extern wchar_t  sdl_win_title[512];
SDL_mutex      *blitmtx;
SDL_threadID    eventthread;
int      exit_event         = 0;
int      fullscreen_pending = 0;

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
        if ((drawits > 0 || fast_forward) && !dopause) {
            /* Yes, so do one frame now. */
            drawits -= force_10ms ? 10 : 1;
            if (drawits > 50 || fast_forward)
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

uint32_t
timer_onesec(uint32_t interval, UNUSED(void *param))
{
    pc_onesec();
    return interval;
}

extern int gfxcard[GFXCARD_MAX];
int
main(int argc, char **argv)
{
    SDL_Event event;
    void     *libedithandle = NULL;
    int      ret = 0;

    SDL_Init(0);
    ret = pc_init(argc, argv);
    if (ret == 0)
        return 0;
    if (!pc_init_roms()) {
        ui_msgbox_header(MBX_FATAL, L"No ROMs found.", EMU_NAME_W L" could not find any usable ROM images.\n\nPlease download a ROM set and extract it into the \"roms\" directory.");
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

    monitor_init();

    mousemutex = SDL_CreateMutex();

    if (start_in_fullscreen)
        video_fullscreen = 1;

    sdl_initho();

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

        if (!SDL_WaitEventTimeout(&event, 1))
            goto check_flags;
        do {
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
        } while (SDL_PollEvent(&event));

check_flags:
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
    monitor_close();
    return 0;
}

#ifdef _WIN32
int WINAPI
WinMain(UNUSED(HINSTANCE instance), UNUSED(HINSTANCE previous), UNUSED(LPSTR command_line), UNUSED(int show))
{
    return main(__argc, __argv); // :')
}
#endif

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
