/* SDL glue for the shared OSD core. */
#ifdef USE_SDL_SHADER_PIPELINE
#include <GLES2/gl2.h>
#endif
#include <algorithm>
#include <cmath>
#include <SDL.h>

#include "imgui.h"
#include "imgui_impl_sdl2.h"
#ifdef USE_SDL_SHADER_PIPELINE
#include "imgui_impl_opengl3.h"
#else
#include "imgui_impl_sdlrenderer2.h"
#endif

/* SDL header redefines HAVE_STDARG_H. */
#undef HAVE_STDARG_H
#define HAVE_STDARG_H
#include <86box/86box.h>
#include <86box/plat.h>
#include <86box/video.h>

#include "sdl_osd.h"
#include "osd_core.hpp"

#ifdef USE_SDL_SHADER_PIPELINE
#include "sdl_shader.h"
#endif

/* ------------------------------------------------------------------ */
/*  Extern interface to SDL environment                                */
/* ------------------------------------------------------------------ */
extern "C" {
extern SDL_Window   *sdl_win;
extern SDL_Renderer *sdl_render;
extern char          sdl_win_title[512];
extern int           exit_event;
extern int           fullscreen_pending;
}

/* ------------------------------------------------------------------ */
/*  Glue state                                                         */
/* ------------------------------------------------------------------ */
static bool osd_inited         = false;
static bool osd_visible        = false;
static bool pending_close      = false;
static bool mouse_was_captured = false;
static int  osd_font_pixel_size = 0;

static void osd_set_scale(float scale)
{
    const int base_size  = osd_core_default_font_size();
    const float raster_scale = std::max(1.0f, scale);
    const int pixel_size = std::max(base_size, (int) std::lround((float) base_size * raster_scale));

    osd_core_set_layout_scale(scale);
    if (pixel_size == osd_font_pixel_size)
        return;

    osd_core_rebuild_default_font(pixel_size);
    osd_font_pixel_size = pixel_size;
}

/* ------------------------------------------------------------------ */
/*  ImGui platform/renderer backend                                    */
/* ------------------------------------------------------------------ */
static bool osd_backend_init(void)
{
#ifdef USE_SDL_SHADER_PIPELINE
    SDL_GLContext ctx = sdl_shader_get_context();
    if (!ctx)
        return false;

    SDL_GL_MakeCurrent(sdl_win, ctx);

    if (!ImGui_ImplSDL2_InitForOpenGL(sdl_win, ctx))
        return false;

    if (!ImGui_ImplOpenGL3_Init("#version 100")) {
        ImGui_ImplSDL2_Shutdown();
        return false;
    }
#else
    if (sdl_render == nullptr)
        return false;

    if (!ImGui_ImplSDL2_InitForSDLRenderer(sdl_win, sdl_render))
        return false;

    if (!ImGui_ImplSDLRenderer2_Init(sdl_render)) {
        ImGui_ImplSDL2_Shutdown();
        return false;
    }
#endif

    return true;
}

static void osd_backend_shutdown(void)
{
#ifdef USE_SDL_SHADER_PIPELINE
    ImGui_ImplOpenGL3_Shutdown();
#else
    ImGui_ImplSDLRenderer2_Shutdown();
#endif
    ImGui_ImplSDL2_Shutdown();
}

/* ------------------------------------------------------------------ */
/*  Frontend actions exposed to the OSD core                           */
/* ------------------------------------------------------------------ */
static void sdl_osd_toggle_fullscreen(void)
{
    video_fullscreen   = video_fullscreen ? 0 : 1;
    fullscreen_pending = 1;
}

static void sdl_osd_request_exit(void)
{
    exit_event = 1;
}

static const osd_host_t sdl_osd_host = {
    sdl_osd_toggle_fullscreen,
    sdl_osd_request_exit
};

/* ------------------------------------------------------------------ */
/*  Public API: init / deinit                                          */
/* ------------------------------------------------------------------ */
void osd_init(void)
{
    if (osd_inited)
        return;

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();

    ImGuiIO &io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    io.IniFilename = nullptr; /* don't save layout */

    osd_set_scale(1.0f);
    osd_core_setup_style();
    osd_core_set_host(&sdl_osd_host);

    if (!osd_backend_init()) {
        ImGui::DestroyContext();
        return;
    }

    osd_core_install_log_hook();
    osd_inited = true;
}

void osd_deinit(void)
{
    if (!osd_inited)
        return;

    osd_core_remove_log_hook();
    osd_backend_shutdown();
    ImGui::DestroyContext();
    osd_inited = false;
    osd_font_pixel_size = 0;
}

/* ------------------------------------------------------------------ */
/*  Public API: open / close                                           */
/* ------------------------------------------------------------------ */
int osd_open(SDL_Event event)
{
    (void)event;
    osd_visible = true;
    osd_core_set_title(sdl_win_title);
    osd_core_reset_to_menu();

    mouse_was_captured = mouse_capture;
    plat_mouse_capture(0);

    /* Prevent stale ESC from firing on re-open. */
    if (osd_inited)
        ImGui::GetIO().ClearInputKeys();

    return 1;
}

int osd_close(SDL_Event event)
{
    (void)event;
    osd_visible = false;

    /* Restore mouse capture if it was active before OSD opened. */
    plat_mouse_capture(mouse_was_captured);
    mouse_was_captured = false;

    return 1;
}

/* ------------------------------------------------------------------ */
/*  Public API: event handling                                         */
/* ------------------------------------------------------------------ */
int osd_handle(SDL_Event event)
{
    if (!osd_visible || !osd_inited)
        return 0;

    /* Handle ESC manually so keyboard navigation stays predictable. */
    if ((event.type == SDL_KEYDOWN || event.type == SDL_KEYUP)
        && event.key.keysym.scancode == SDL_SCANCODE_ESCAPE) {
        if (event.type == SDL_KEYUP) {
            if (osd_core_escape())
                return 0; /* close OSD entirely */
        }
        return 1; /* consume */
    }

    if (pending_close) {
        pending_close = false;
        return 0;
    }

    ImGui_ImplSDL2_ProcessEvent(&event);
    return 1; /* keep open */
}

/* ------------------------------------------------------------------ */
/*  Public API: present (called during blit)                           */
/* ------------------------------------------------------------------ */
void osd_present(int output_w, int output_h)
{
    if (!osd_visible || !osd_inited)
        return;

#ifdef USE_SDL_SHADER_PIPELINE
    (void) output_w;
    (void) output_h;

    SDL_GLContext ctx = sdl_shader_get_context();
    if (!ctx)
        return;

    SDL_GL_MakeCurrent(sdl_win, ctx);

    /* The OSD is drawn crisp at the window's full resolution, on top of the
     * already shaded emulator image. */
    int win_w = 0, win_h = 0;
    SDL_GetWindowSize(sdl_win, &win_w, &win_h);
    osd_set_scale(osd_core_layout_scale_for_output(win_w, win_h));

    ImGui_ImplOpenGL3_NewFrame();
    ImGui_ImplSDL2_NewFrame();
    ImGui::NewFrame();
    if (!osd_core_build_ui())
        pending_close = true;
    ImGui::Render();
    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
#else
    if (sdl_render == nullptr)
        return;

    osd_set_scale(osd_core_layout_scale_for_output(output_w, output_h));

    ImGui_ImplSDLRenderer2_NewFrame();
    ImGui_ImplSDL2_NewFrame();
    ImGui::NewFrame();
    if (!osd_core_build_ui())
        pending_close = true;
    ImGui::Render();
    ImGui_ImplSDLRenderer2_RenderDrawData(ImGui::GetDrawData(), sdl_render);
#endif
}

/* ------------------------------------------------------------------ */
/*  Stubs for status bar icons (not yet implemented)                   */
/* ------------------------------------------------------------------ */
void osd_ui_sb_update_icon_state(int tag, int state)   { (void)tag; (void)state; }
void osd_ui_sb_update_icon(int tag, int active)        { (void)tag; (void)active; }
void osd_ui_sb_update_icon_write(int tag, int active)  { (void)tag; (void)active; }
void osd_ui_sb_update_icon_wp(int tag, int state)      { (void)tag; (void)state; }
