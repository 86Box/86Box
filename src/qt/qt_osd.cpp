/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          Qt glue for the shared OSD core.
 */
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>

#include <QImage>
#include <QKeyEvent>
#include <QMetaObject>

#include "imgui.h"
#include "imgui_impl_opengl3.h"

#include "osd_core.hpp"
#include "qt_osd.hpp"
#include "qt_osd_raster.hpp"
#include "qt_mainwindow.hpp"

extern "C" {
#include <86box/86box.h>
#include <86box/plat.h>
#include <86box/keyboard.h>
}

extern MainWindow *main_window;

namespace {

using Clock = std::chrono::steady_clock;

bool                 g_visible      = false;
bool                 g_ctx_ready    = false;
bool                 g_gl_ready     = false;
bool                 g_fonts_ready  = false;
bool                 g_mouse_was_captured = false;
int                  g_font_pixel_size = 0;
Clock::time_point    g_last_frame;
QImage               g_software_surface;

/* ------------------------------------------------------------------ */
/*  Frontend actions exposed to the OSD core                           */
/* ------------------------------------------------------------------ */
void
qt_osd_toggle_fullscreen(void)
{
    /* Queue the action so we do not re-enter Qt widget code from ImGui. */
    QMetaObject::invokeMethod(main_window, "on_actionFullscreen_triggered", Qt::QueuedConnection);
}

void
qt_osd_request_exit(void)
{
    g_visible = false;
    g_mouse_was_captured = false;
    plat_mouse_capture(0);
    QMetaObject::invokeMethod(main_window, "on_actionExit_triggered", Qt::QueuedConnection);
}

const osd_host_t qt_osd_host = {
    qt_osd_toggle_fullscreen,
    qt_osd_request_exit
};

void
ensure_font_size(float scale)
{
    const float raster_scale = std::max(1.0f, scale);

    const int base_size  = osd_core_default_font_size();
    const int pixel_size = std::max(base_size, (int) std::lround((float) base_size * raster_scale));

    if (pixel_size == g_font_pixel_size)
        return;

    osd_core_rebuild_default_font(pixel_size);
    g_font_pixel_size = pixel_size;
    g_fonts_ready     = false;
}

/* ------------------------------------------------------------------ */
/*  Lazy init                                                          */
/* ------------------------------------------------------------------ */
void
ensure_context(void)
{
    if (g_ctx_ready)
        return;

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();

    ImGuiIO &io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    io.IniFilename  = nullptr; /* don't save layout */

    osd_core_set_layout_scale(1.0f);
    osd_core_setup_style();
    ensure_font_size(1.0f);
    osd_core_set_host(&qt_osd_host);
    osd_core_install_log_hook();

    g_ctx_ready = true;
}

void
ensure_gl(void)
{
    if (g_gl_ready)
        return;

    /* Qt requests a 3.2 (4.1 on macOS) core profile, so GLSL 150 fits. */
    if (ImGui_ImplOpenGL3_Init("#version 150"))
        g_gl_ready = true;
}

/* Build the font atlas for the software path. */
void
ensure_fonts(void)
{
    if (g_fonts_ready)
        return;

    ImGuiIO &io = ImGui::GetIO();
    unsigned char *px = nullptr;
    int w = 0, h = 0;
    io.Fonts->GetTexDataAsRGBA32(&px, &w, &h);
    io.Fonts->SetTexID((ImTextureID) (intptr_t) 1);
    g_fonts_ready = true;
}

/* Feed ImGui the time since the previous frame so animations advance. */
void
advance_frame_time(ImGuiIO &io)
{
    const auto  now = Clock::now();
    const float dt  = std::chrono::duration<float>(now - g_last_frame).count();
    g_last_frame = now;
    io.DeltaTime = (dt > 0.0f) ? dt : (1.0f / 60.0f);
}

/* ------------------------------------------------------------------ */
/*  Qt key -> ImGui key                                                */
/* ------------------------------------------------------------------ */
ImGuiKey
qt_key_to_imgui(int key)
{
    switch (key) {
        case Qt::Key_Tab:       return ImGuiKey_Tab;
        case Qt::Key_Left:      return ImGuiKey_LeftArrow;
        case Qt::Key_Right:     return ImGuiKey_RightArrow;
        case Qt::Key_Up:        return ImGuiKey_UpArrow;
        case Qt::Key_Down:      return ImGuiKey_DownArrow;
        case Qt::Key_PageUp:    return ImGuiKey_PageUp;
        case Qt::Key_PageDown:  return ImGuiKey_PageDown;
        case Qt::Key_Home:      return ImGuiKey_Home;
        case Qt::Key_End:       return ImGuiKey_End;
        case Qt::Key_Insert:    return ImGuiKey_Insert;
        case Qt::Key_Delete:    return ImGuiKey_Delete;
        case Qt::Key_Backspace: return ImGuiKey_Backspace;
        case Qt::Key_Space:     return ImGuiKey_Space;
        case Qt::Key_Return:    return ImGuiKey_Enter;
        case Qt::Key_Enter:     return ImGuiKey_KeypadEnter;
        default:                return ImGuiKey_None;
    }
}

int
qt_button_to_imgui(int button)
{
    switch (button) {
        case Qt::LeftButton:   return 0;
        case Qt::RightButton:  return 1;
        case Qt::MiddleButton: return 2;
        default:               return -1;
    }
}

void
osd_open(void)
{
    ensure_context();
    g_visible = true;
    osd_core_set_title(main_window ? main_window->getTitle().toUtf8().constData() : "");
    osd_core_reset_to_menu();
    ImGui::GetIO().ClearInputKeys();
    g_last_frame = Clock::now();

    /* Free the pointer so the user can drive the menu. */
    g_mouse_was_captured = (mouse_capture != 0);
    if (g_mouse_was_captured)
        plat_mouse_capture(0);

    /* Release any guest-held keys so the toggle modifiers do not stay stuck. */
    keyboard_all_up();
}

void
osd_close(void)
{
    g_visible = false;
    if (g_mouse_was_captured)
        plat_mouse_capture(1);
    g_mouse_was_captured = false;
}

} /* namespace */

/* ------------------------------------------------------------------ */
/*  Public API                                                         */
/* ------------------------------------------------------------------ */
bool
qt_osd_is_visible(void)
{
    return g_visible;
}

void
qt_osd_toggle(void)
{
    if (g_visible)
        osd_close();
    else
        osd_open();
}

void
qt_osd_set_layout_scale_hint(float scale)
{
    ensure_context();
    if (scale <= 0.0f)
        scale = 1.0f;
    osd_core_set_layout_scale(scale);
    ensure_font_size(scale);
}

void
qt_osd_shutdown(void)
{
    if (g_gl_ready) {
        ImGui_ImplOpenGL3_Shutdown();
        g_gl_ready = false;
    }
    if (g_ctx_ready) {
        osd_core_remove_log_hook();
        ImGui::DestroyContext();
        g_ctx_ready = false;
    }
    g_fonts_ready = false;
    g_font_pixel_size = 0;
    g_software_surface = QImage();
    g_visible = false;
}

void
qt_osd_render(int output_w, int output_h, float dpr)
{
    /* OpenGL path for the GL renderer. */
    if (!g_visible)
        return;

    ensure_context();
    ensure_gl();
    if (!g_gl_ready)
        return;

    ImGuiIO &io = ImGui::GetIO();
    io.DisplaySize             = ImVec2((float) output_w, (float) output_h);
    io.DisplayFramebufferScale = ImVec2(dpr, dpr);

    advance_frame_time(io);

    ImGui_ImplOpenGL3_NewFrame();
    ImGui::NewFrame();

    if (!osd_core_build_ui())
        osd_close();

    ImGui::Render();
    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
}

const QImage *
qt_osd_render_software(int logical_w, int logical_h, float dpr)
{
    if (!g_visible)
        return nullptr;

    ensure_context();
    ensure_fonts();

    const int dw = std::max(1, (int) std::lround(logical_w * dpr));
    const int dh = std::max(1, (int) std::lround(logical_h * dpr));
    if (g_software_surface.width() != dw || g_software_surface.height() != dh)
        g_software_surface = QImage(dw, dh, QImage::Format_ARGB32_Premultiplied);
    g_software_surface.setDevicePixelRatio(dpr);
    g_software_surface.fill(Qt::transparent);

    ImGuiIO &io = ImGui::GetIO();
    io.DisplaySize             = ImVec2((float) logical_w, (float) logical_h);
    io.DisplayFramebufferScale = ImVec2(1.0f, 1.0f);

    advance_frame_time(io);

    ImGui::NewFrame();
    if (!osd_core_build_ui())
        osd_close();
    ImGui::Render();

    osd_raster_render(ImGui::GetDrawData(), g_software_surface, dpr);
    return g_visible ? &g_software_surface : nullptr;
}

bool
qt_osd_key(int qt_key, int qt_modifiers, bool down, bool repeat)
{
    if (!g_visible)
        return false;

    /* Escape steps back a view or closes the OSD. */
    if (qt_key == Qt::Key_Escape) {
        if (!down && osd_core_escape())
            osd_close();
        return true;
    }

    ImGuiKey ik = qt_key_to_imgui(qt_key);
    if (ik != ImGuiKey_None)
        ImGui::GetIO().AddKeyEvent(ik, down);

    return true; /* swallow all input while the OSD is open */
}

void
qt_osd_mouse_pos(float x, float y)
{
    if (g_visible)
        ImGui::GetIO().AddMousePosEvent(x, y);
}

void
qt_osd_mouse_button(int qt_button, bool down)
{
    if (!g_visible)
        return;

    int b = qt_button_to_imgui(qt_button);
    if (b >= 0)
        ImGui::GetIO().AddMouseButtonEvent(b, down);
}

void
qt_osd_mouse_wheel(float dx, float dy)
{
    if (g_visible)
        ImGui::GetIO().AddMouseWheelEvent(dx, dy);
}
