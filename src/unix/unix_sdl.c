#include <SDL.h>
#include <SDL_messagebox.h>

#include <stdarg.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <sys/param.h>
/* This #undef is needed because a SDL include header redefines HAVE_STDARG_H. */
#undef HAVE_STDARG_H
#define HAVE_STDARG_H
#include <86box/86box.h>
#include <86box/device.h>
#include <86box/plat.h>
#include <86box/plat_dynld.h>
#include <86box/video.h>
#include <86box/ui.h>
#include <86box/version.h>
#include <86box/ini.h>
#include <86box/config.h>
#include <86box/unix_osd.h>
#include <86box/unix_sdl.h>
#include "unix_sdl_shader.h"

#define RENDERER_FULL_SCREEN 1
#define RENDERER_HARDWARE    2
#define RENDERER_OPENGL      4

typedef struct sdl_blit_params {
    int x;
    int y;
    int w;
    int h;
} sdl_blit_params;
extern sdl_blit_params params;
extern int             blitreq;

SDL_Window         *sdl_win     = NULL;
SDL_Renderer       *sdl_render  = NULL;
static SDL_Texture *sdl_tex     = NULL;
static SDL_Texture *sdl_osd_tex = NULL;
int                 sdl_w       = SCREEN_RES_X;
int                 sdl_h       = SCREEN_RES_Y;
static int          sdl_fs;
static int          sdl_flags   = -1;
static int          cur_w;
static int          cur_h;
static int          cur_wx      = 0;
static int          cur_wy      = 0;
static int          cur_ww      = 0;
static int          cur_wh      = 0;
static volatile int sdl_enabled = 1;
SDL_mutex          *sdl_mutex   = NULL;
int                 mouse_capture;
int                 title_set         = 0;
int                 resize_pending    = 0;
int                 resize_w          = 0;
int                 resize_h          = 0;
static void        *pixeldata         = NULL;

void sdl_reinit_texture(void);

static void
sdl_integer_scale(double *d, double *g)
{
    double ratio;

    if (*d > *g) {
        ratio = floor(*d / *g);
        *d    = *g * ratio;
    } else {
        ratio = ceil(*d / *g);
        *d    = *g / ratio;
    }
}

static void
sdl_get_output_size(int *w, int *h)
{
#if USE_SDL_SHADER_PIPELINE
    SDL_GL_GetDrawableSize(sdl_win, w, h);
#else
    if (sdl_render && SDL_GetRendererOutputSize(sdl_render, w, h) == 0)
        return;
    SDL_GetWindowSize(sdl_win, w, h);
#endif
}

static void
sdl_stretch(int *w, int *h, int *x, int *y)
{
    double hw;
    double gw;
    double hh;
    double gh;
    double dx;
    double dy;
    double dw;
    double dh;
    double gsr;
    double hsr;
    int    real_sdl_w;
    int    real_sdl_h;
    int    scale_mode;

    sdl_get_output_size(&real_sdl_w, &real_sdl_h);

    hw  = (double) real_sdl_w;
    hh  = (double) real_sdl_h;
    gw  = (double) *w;
    gh  = (double) *h;
    hsr = hw / hh;
    scale_mode = video_gl_input_scale_mode;

    switch (scale_mode) {
        case FULLSCR_SCALE_FULL:
        default:
            *w = real_sdl_w;
            *h = real_sdl_h;
            *x = 0;
            *y = 0;
            break;
        case FULLSCR_SCALE_43:
        case FULLSCR_SCALE_KEEPRATIO:
            if (scale_mode == FULLSCR_SCALE_43)
                gsr = 4.0 / 3.0;
            else
                gsr = gw / gh;
            if (gsr <= hsr) {
                dw = hh * gsr;
                dh = hh;
            } else {
                dw = hw;
                dh = hw / gsr;
            }
            dx = (hw - dw) / 2.0;
            dy = (hh - dh) / 2.0;
            *w = (int) dw;
            *h = (int) dh;
            *x = (int) dx;
            *y = (int) dy;
            break;
        case FULLSCR_SCALE_INT:
        case FULLSCR_SCALE_INT43:
            if (scale_mode == FULLSCR_SCALE_INT43)
                gsr = 4.0 / 3.0;
            else
                gsr = gw / gh;
            if (gsr <= hsr) {
                dw = hh * gsr;
                dh = hh;
            } else {
                dw = hw;
                dh = hw / gsr;
            }
            sdl_integer_scale(&dw, &gw);
            sdl_integer_scale(&dh, &gh);
            dx = (hw - dw) / 2.0;
            dy = (hh - dh) / 2.0;
            *w = (int) dw;
            *h = (int) dh;
            *x = (int) dx;
            *y = (int) dy;
            break;
    }
}

static int
sdl_blit_invalid(int x, int y, int w, int h)
{
    if (!sdl_enabled || (x < 0) || (y < 0) || (w <= 0) || (h <= 0) || (w > 2048) || (h > 2048) || (buffer32 == NULL))
        return 1;

#if USE_SDL_SHADER_PIPELINE
    return !sdl_shader_active();
#else
    return (sdl_render == NULL) || (sdl_tex == NULL);
#endif
}

void
sdl_blit_shim(int x, int y, int w, int h, int monitor_index)
{
    params.x = x;
    params.y = y;
    params.w = w;
    params.h = h;

    if (!sdl_blit_invalid(x, y, w, h) || (monitor_index >= 1))
        for (int row = 0; row < h; ++row)
            video_copy(&(((uint8_t *) pixeldata)[row * 2048 * sizeof(uint32_t)]), &(buffer32->line[y + row][x]), w * sizeof(uint32_t));

    if (monitors[monitor_index].mon_screenshots_raw)
        video_screenshot((uint32_t *) pixeldata, 0, 0, 2048);
    blitreq = 1;

    video_blit_complete_monitor(monitor_index);
}

void ui_window_title_real(void);

void
sdl_real_blit(SDL_Rect *r_src)
{
    SDL_Rect r_dst;
    int      winx;
    int      winy;

    sdl_get_output_size(&winx, &winy);

    r_dst   = *r_src;
    r_dst.x = r_dst.y = 0;

    if (sdl_fs) {
        sdl_stretch(&r_dst.w, &r_dst.h, &r_dst.x, &r_dst.y);
    } else {
        r_dst.w *= ((float) winx / (float) r_dst.w);
        r_dst.h *= ((float) winy / (float) r_dst.h);
    }

#if USE_SDL_SHADER_PIPELINE
    sdl_shader_blit(sdl_win, pixeldata, r_src->w, r_src->h,
                    r_dst.x, r_dst.y, r_dst.w, r_dst.h);
#else
    SDL_Rect src_rect = { 0, 0, r_src->w, r_src->h };

    if (sdl_render == NULL || sdl_tex == NULL || sdl_osd_tex == NULL)
        return;

    if (SDL_UpdateTexture(sdl_tex, &src_rect, pixeldata, 2048 * (int) sizeof(uint32_t)) < 0)
        return;

    osd_present(r_src->w, r_src->h);

    SDL_SetRenderDrawColor(sdl_render, 0, 0, 0, 255);
    SDL_RenderClear(sdl_render);
    SDL_RenderCopy(sdl_render, sdl_tex, &src_rect, &r_dst);

    if (osd_is_visible()) {
        SDL_Surface *osd_surface = osd_get_surface();

        if (osd_surface && osd_surface->w == r_src->w && osd_surface->h == r_src->h) {
            if (SDL_UpdateTexture(sdl_osd_tex, &src_rect, osd_surface->pixels, osd_surface->pitch) == 0)
                SDL_RenderCopy(sdl_render, sdl_osd_tex, &src_rect, &r_dst);
        }
    }

    SDL_RenderPresent(sdl_render);
#endif
}

void
sdl_blit(int x, int y, int w, int h)
{
    SDL_Rect r_src;

    if (sdl_blit_invalid(x, y, w, h)) {
        r_src.x = x;
        r_src.y = y;
        r_src.w = w;
        r_src.h = h;
        sdl_real_blit(&r_src);
        blitreq = 0;
        return;
    }

    SDL_LockMutex(sdl_mutex);

    if (resize_pending) {
        if (!video_fullscreen)
            sdl_resize(resize_w, resize_h);
        resize_pending = 0;
    }

    r_src.x = x;
    r_src.y = y;
    r_src.w = w;
    r_src.h = h;
    blitreq = 0;

    sdl_real_blit(&r_src);
    SDL_UnlockMutex(sdl_mutex);
}

static void
sdl_destroy_window(void)
{
    if (sdl_win != NULL) {
        if (window_remember) {
            SDL_GetWindowSize(sdl_win, &window_w, &window_h);
            if (strncasecmp(SDL_GetCurrentVideoDriver(), "wayland", 7) != 0) {
                SDL_GetWindowPosition(sdl_win, &window_x, &window_y);
            }
        }
        SDL_DestroyWindow(sdl_win);
        sdl_win = NULL;
    }
}

static void
sdl_destroy_texture(void)
{
#if USE_SDL_SHADER_PIPELINE
    sdl_shader_close();
#else
    if (sdl_osd_tex != NULL) {
        SDL_DestroyTexture(sdl_osd_tex);
        sdl_osd_tex = NULL;
    }
    if (sdl_tex != NULL) {
        SDL_DestroyTexture(sdl_tex);
        sdl_tex = NULL;
    }
    if (sdl_render != NULL) {
        SDL_DestroyRenderer(sdl_render);
        sdl_render = NULL;
    }
#endif
}

void
sdl_close(void)
{
    if (sdl_mutex != NULL)
        SDL_LockMutex(sdl_mutex);

    /* Unregister our renderer! */
    video_setblit(NULL);

    if (sdl_enabled)
        sdl_enabled = 0;

    if (sdl_mutex != NULL) {
        SDL_DestroyMutex(sdl_mutex);
        sdl_mutex = NULL;
    }

    sdl_destroy_texture();
    sdl_destroy_window();

    if (pixeldata != NULL) {
        free(pixeldata);
        pixeldata = NULL;
    }

    /* Quit. */
    SDL_Quit();
    sdl_flags = -1;
}

void
sdl_enable(int enable)
{
    if (sdl_flags == -1)
        return;

    SDL_LockMutex(sdl_mutex);
    sdl_enabled = !!enable;

    if (enable == 1) {
        SDL_SetWindowSize(sdl_win, cur_ww, cur_wh);
        sdl_reinit_texture();
    }

    SDL_UnlockMutex(sdl_mutex);
}

static void
sdl_select_best_hw_driver(void)
{
    SDL_RendererInfo renderInfo;

    for (int i = 0; i < SDL_GetNumRenderDrivers(); ++i) {
        SDL_GetRenderDriverInfo(i, &renderInfo);
        if (renderInfo.flags & SDL_RENDERER_ACCELERATED) {
            SDL_SetHint(SDL_HINT_RENDER_DRIVER, renderInfo.name);
            return;
        }
    }
}

void
sdl_reinit_texture(void)
{
#if USE_SDL_SHADER_PIPELINE
    /* Always-GL: nothing to reinit, shader context persists. */
#else
    if (pixeldata == NULL)
        return;

    sdl_destroy_texture();

    sdl_render = SDL_CreateRenderer(sdl_win, -1, SDL_RENDERER_ACCELERATED);
    if (sdl_render == NULL) {
        SDL_SetHint(SDL_HINT_RENDER_DRIVER, "");
        sdl_render = SDL_CreateRenderer(sdl_win, -1, SDL_RENDERER_SOFTWARE);
    }
    if (sdl_render == NULL)
        return;

    sdl_tex = SDL_CreateTexture(sdl_render, SDL_PIXELFORMAT_ARGB8888,
                                SDL_TEXTUREACCESS_STREAMING, 2048, 2048);
    sdl_osd_tex = SDL_CreateTexture(sdl_render, SDL_PIXELFORMAT_ABGR8888,
                                    SDL_TEXTUREACCESS_STREAMING, 2048, 2048);
    if (sdl_tex == NULL || sdl_osd_tex == NULL) {
        sdl_destroy_texture();
        return;
    }

    SDL_SetTextureBlendMode(sdl_osd_tex, SDL_BLENDMODE_BLEND);
#endif
}

void
sdl_set_fs(int fs)
{
    SDL_LockMutex(sdl_mutex);
#if USE_SDL_SHADER_PIPELINE
    SDL_SetWindowFullscreen(sdl_win, fs ? SDL_WINDOW_FULLSCREEN : 0);
#else
    SDL_SetWindowFullscreen(sdl_win, fs ? SDL_WINDOW_FULLSCREEN_DESKTOP : 0);
#endif
    SDL_SetRelativeMouseMode((SDL_bool) mouse_capture);

    sdl_fs = fs;

    if (fs)
        sdl_flags |= RENDERER_FULL_SCREEN;
    else
        sdl_flags &= ~RENDERER_FULL_SCREEN;

    sdl_reinit_texture();
    SDL_UnlockMutex(sdl_mutex);
}

void
sdl_resize(int x, int y)
{
    int ww = 0;
    int wh = 0;
    int wx = 0;
    int wy = 0;

    if (video_fullscreen & 2)
        return;

    if ((x == cur_w) && (y == cur_h))
        return;

    SDL_LockMutex(sdl_mutex);

    ww = x;
    wh = y;

    cur_w = x;
    cur_h = y;

    cur_wx = wx;
    cur_wy = wy;
    cur_ww = ww;
    cur_wh = wh;

    SDL_SetWindowSize(sdl_win, cur_ww, cur_wh);
    sdl_get_output_size(&sdl_w, &sdl_h);

    sdl_reinit_texture();

    SDL_UnlockMutex(sdl_mutex);
}
void
sdl_reload(void)
{
    if (sdl_flags & RENDERER_HARDWARE) {
        SDL_LockMutex(sdl_mutex);

        SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, video_filter_method ? "1" : "0");
        sdl_reinit_texture();

        SDL_UnlockMutex(sdl_mutex);
    }
}

int
plat_vidapi(UNUSED(const char *api))
{
    return 0;
}

static int
sdl_init_common(int flags)
{
    SDL_version ver;
    Uint32      window_flags = (vid_resize & 1 ? SDL_WINDOW_RESIZABLE : 0);

#if USE_SDL_SHADER_PIPELINE
    window_flags |= SDL_WINDOW_OPENGL;
#endif

    /* Get and log the version of the DLL we are using. */
    SDL_GetVersion(&ver);
    fprintf(stderr, "SDL: version %d.%d.%d\n", ver.major, ver.minor, ver.patch);

    /* Initialize the SDL system. */
    if (SDL_Init(SDL_INIT_VIDEO) < 0) {
        fprintf(stderr, "SDL: initialization failed (%s)\n", SDL_GetError());
        return (0);
    }

    // Ensure mouse and touchpads behaves the same for us, dunno if these really do something
    SDL_SetHint(SDL_HINT_TOUCH_MOUSE_EVENTS, "1");
    SDL_SetHint(SDL_HINT_MOUSE_TOUCH_EVENTS, "1");

    if (flags & RENDERER_HARDWARE) {
#if USE_SDL_SHADER_PIPELINE
        if (flags & RENDERER_OPENGL) {
            SDL_SetHint(SDL_HINT_RENDER_DRIVER, "OpenGL");
        } else
#endif
            sdl_select_best_hw_driver();
    }

    sdl_mutex = SDL_CreateMutex();
    sdl_win   = SDL_CreateWindow("86Box", strncasecmp(SDL_GetCurrentVideoDriver(), "wayland", 7) != 0 && window_remember ? window_x : SDL_WINDOWPOS_CENTERED, strncasecmp(SDL_GetCurrentVideoDriver(), "wayland", 7) != 0 && window_remember ? window_y : SDL_WINDOWPOS_CENTERED, scrnsz_x, scrnsz_y, window_flags);
    sdl_set_fs(video_fullscreen);
    if (!(video_fullscreen & 1)) {
        if (vid_resize & 2)
            plat_resize(fixed_size_x, fixed_size_y, 0);
        else
            plat_resize(scrnsz_x, scrnsz_y, 0);
    }
    if ((vid_resize < 2) && window_remember) {
        SDL_SetWindowSize(sdl_win, window_w, window_h);
    }

#if USE_SDL_SHADER_PIPELINE
    {
        const char *sp = config_get_string("GL3 Shaders", "shader0", "");
        if (sp && sp[0])
            sdl_shader_init(sdl_win, sp);
        /* Always need a GL context. Passthrough if no shader loaded. */
        if (!sdl_shader_active())
            sdl_shader_init_passthrough(sdl_win);
    }
#endif
    SDL_ShowCursor(SDL_FALSE);
    osd_init();

    /* Make sure we get a clean exit. */
    atexit(sdl_close);

    pixeldata = calloc(1, 2048 * 2048 * 4);
    sdl_reinit_texture();

    /* Register our renderer! */
    video_setblit(sdl_blit_shim);

    sdl_enabled = 1;

    return (1);
}

int
sdl_inits(void)
{
    return sdl_init_common(0);
}

int
sdl_inith(void)
{
    return sdl_init_common(RENDERER_HARDWARE);
}

int
sdl_initho(void)
{
    return sdl_init_common(RENDERER_HARDWARE | RENDERER_OPENGL);
}

int
sdl_pause(void)
{
    return 0;
}

void
plat_mouse_capture(int on)
{
    SDL_LockMutex(sdl_mutex);
    SDL_SetRelativeMouseMode((SDL_bool) on);
    mouse_capture = on;
    SDL_UnlockMutex(sdl_mutex);
}

void
plat_resize(int w, int h, UNUSED(int monitor_index))
{
    SDL_LockMutex(sdl_mutex);
    resize_w       = w;
    resize_h       = h;
    resize_pending = 1;
    SDL_UnlockMutex(sdl_mutex);
}

wchar_t    sdl_win_title[512] = { L'8', L'6', L'B', L'o', L'x', 0 };
SDL_mutex *titlemtx           = NULL;

void
ui_window_title_real(void)
{
    char *res;
    if (sizeof(wchar_t) == 1) {
        SDL_SetWindowTitle(sdl_win, (char *) sdl_win_title);
        return;
    }
    res = SDL_iconv_string("UTF-8", sizeof(wchar_t) == 2 ? "UTF-16LE" : "UTF-32LE", (char *) sdl_win_title, wcslen(sdl_win_title) * sizeof(wchar_t) + sizeof(wchar_t));
    if (res) {
        SDL_SetWindowTitle(sdl_win, res);
        SDL_free((void *) res);
    }
    title_set = 0;
}
extern SDL_threadID eventthread;

/* Only activate threading path on macOS, otherwise it will softlock Xorg.
   Wayland doesn't seem to have this issue. */
wchar_t *
ui_window_title(wchar_t *str)
{
    if (!str)
        return sdl_win_title;
#ifdef __APPLE__
    if (eventthread == SDL_ThreadID())
#endif
    {
        memset(sdl_win_title, 0, sizeof(sdl_win_title));
        wcsncpy(sdl_win_title, str, 512);
        ui_window_title_real();
        return str;
    }
#ifdef __APPLE__
    memset(sdl_win_title, 0, sizeof(sdl_win_title));
    wcsncpy(sdl_win_title, str, 512);
    title_set = 1;
#endif
    return str;
}

void
ui_init_monitor(UNUSED(int monitor_index))
{
    /* No-op. */
}

void
ui_deinit_monitor(UNUSED(int monitor_index))
{
    /* No-op. */
}

void
plat_resize_request(UNUSED(int w), UNUSED(int h), int monitor_index)
{
    atomic_store((&doresize_monitors[monitor_index]), 1);
}
