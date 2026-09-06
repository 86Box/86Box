/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          Wayland mouse input module.
 *
 * Authors: Cacodemon345
 *
 *          Copyright 2021-2022 Cacodemon345
 */
#include "wl_mouse.hpp"
#include <QGuiApplication>
#include <wayland-client-core.h>
#include <wayland-client-protocol.h>
#include <wayland-relative-pointer-unstable-v1-client-protocol.h>
#include <wayland-pointer-constraints-unstable-v1-client-protocol.h>
#include <wayland-keyboard-shortcuts-inhibit-unstable-v1-client-protocol.h>

#include <qpa/qplatformnativeinterface.h>
#include <QWindow>
#include <QGuiApplication>

extern "C" {
#include <86box/mouse.h>
#include <86box/plat.h>
}

static zwp_relative_pointer_manager_v1           *rel_manager            = nullptr;
static zwp_relative_pointer_v1                   *rel_pointer            = nullptr;
static zwp_pointer_constraints_v1                *conf_pointer_interface = nullptr;
static zwp_locked_pointer_v1                     *conf_pointer           = nullptr;
static zwp_locked_pointer_v1                     *conf_pointer_toplevel  = nullptr;
static zwp_keyboard_shortcuts_inhibit_manager_v1 *kbd_manager            = nullptr;
static zwp_keyboard_shortcuts_inhibitor_v1       *kbd_inhibitor          = nullptr;

static bool wl_init_ok     = false;
static bool pointer_locked = false;

static void
locked_pointer_locked(void *data, zwp_locked_pointer_v1 *locked_pointer)
{
    pointer_locked = true;
}

static void
locked_pointer_unlocked(void *data, zwp_locked_pointer_v1 *locked_pointer)
{
    pointer_locked = false;
}

static const struct zwp_locked_pointer_v1_listener locked_pointer_listener = {
    locked_pointer_locked,
    locked_pointer_unlocked
};

static wl_surface *
surface_for(QWindow *window)
{
    if (!window)
        return nullptr;
    return (wl_surface *) QGuiApplication::platformNativeInterface()->nativeResourceForWindow("surface", window);
}

static zwp_locked_pointer_v1 *
lock_pointer_to(wl_surface *surface, wl_pointer *pointer)
{
    if (!surface)
        return nullptr;

    auto *lock = zwp_pointer_constraints_v1_lock_pointer(conf_pointer_interface, surface, pointer,
                                                         nullptr, ZWP_POINTER_CONSTRAINTS_V1_LIFETIME_PERSISTENT);
    if (lock)
        zwp_locked_pointer_v1_add_listener(lock, &locked_pointer_listener, nullptr);
    return lock;
}

void
rel_mouse_event(void *data, zwp_relative_pointer_v1 *zwp_relative_pointer_v1, uint32_t tstmp, uint32_t tstmpl, wl_fixed_t dx, wl_fixed_t dy, wl_fixed_t dx_real, wl_fixed_t dy_real)
{
    mouse_scale(wl_fixed_to_int(dx_real), wl_fixed_to_int(dy_real));
}

static struct zwp_relative_pointer_v1_listener rel_listener = {
    rel_mouse_event
};

static void
display_handle_global(void *data, struct wl_registry *registry, uint32_t id,
                      const char *interface, uint32_t version)
{
    if (!strcmp(interface, "zwp_relative_pointer_manager_v1")) {
        rel_manager = (zwp_relative_pointer_manager_v1 *) wl_registry_bind(registry, id, &zwp_relative_pointer_manager_v1_interface, version);
    }
    if (!strcmp(interface, "zwp_pointer_constraints_v1")) {
        conf_pointer_interface = (zwp_pointer_constraints_v1 *) wl_registry_bind(registry, id, &zwp_pointer_constraints_v1_interface, version);
    }
    if (!strcmp(interface, "zwp_keyboard_shortcuts_inhibit_manager_v1")) {
        kbd_manager = (zwp_keyboard_shortcuts_inhibit_manager_v1 *) wl_registry_bind(registry, id, &zwp_keyboard_shortcuts_inhibit_manager_v1_interface, version);
    }
}

static void
display_global_remove(void *data, struct wl_registry *wl_registry, uint32_t name)
{
    plat_mouse_capture(0);
    if (kbd_inhibitor) {
        zwp_keyboard_shortcuts_inhibitor_v1_destroy(kbd_inhibitor);
        kbd_inhibitor = nullptr;
    }
    if (kbd_manager) {
        zwp_keyboard_shortcuts_inhibit_manager_v1_destroy(kbd_manager);
        kbd_manager = nullptr;
    }
    if (rel_manager) {
        zwp_relative_pointer_manager_v1_destroy(rel_manager);
        rel_manager = nullptr;
    }
    if (conf_pointer_interface) {
        zwp_pointer_constraints_v1_destroy(conf_pointer_interface);
        conf_pointer_interface = nullptr;
    }
}

static const struct wl_registry_listener registry_listener = {
    display_handle_global,
    display_global_remove
};

void
wl_init()
{
    if (!wl_init_ok) {
        wl_display *display = (wl_display *) QGuiApplication::platformNativeInterface()->nativeResourceForIntegration("wl_display");
        if (display) {
            auto registry = wl_display_get_registry(display);
            if (registry) {
                wl_registry_add_listener(registry, &registry_listener, nullptr);
                wl_display_roundtrip(display);
            }
        }
        wl_init_ok = true;
    }
}

void
wl_keyboard_grab(QWindow *window)
{
    if (!kbd_inhibitor && kbd_manager) {
        kbd_inhibitor = zwp_keyboard_shortcuts_inhibit_manager_v1_inhibit_shortcuts(kbd_manager, (wl_surface *) QGuiApplication::platformNativeInterface()->nativeResourceForWindow("surface", window), (wl_seat *) QGuiApplication::platformNativeInterface()->nativeResourceForIntegration("wl_seat"));
    }
}

void
wl_mouse_capture(QWindow *window)
{
    if (!kbd_inhibitor && kbd_manager) {
        kbd_inhibitor = zwp_keyboard_shortcuts_inhibit_manager_v1_inhibit_shortcuts(kbd_manager, (wl_surface *) QGuiApplication::platformNativeInterface()->nativeResourceForWindow("surface", window), (wl_seat *) QGuiApplication::platformNativeInterface()->nativeResourceForIntegration("wl_seat"));
    }
    if (rel_manager) {
        rel_pointer = zwp_relative_pointer_manager_v1_get_relative_pointer(rel_manager, (wl_pointer *) QGuiApplication::platformNativeInterface()->nativeResourceForIntegration("wl_pointer"));
        zwp_relative_pointer_v1_add_listener(rel_pointer, &rel_listener, nullptr);
    }
    if (conf_pointer_interface) {
        auto *pointer = (wl_pointer *) QGuiApplication::platformNativeInterface()->nativeResourceForIntegration("wl_pointer");

        QWindow *toplevel = window;
        while (toplevel && toplevel->parent())
            toplevel = toplevel->parent();

        wl_surface *surface          = surface_for(window);
        wl_surface *toplevel_surface = surface_for(toplevel);

        /* Compositors disagree on which surface a constraint is resolved
         * against: KWin uses the window's root surface, while Weston, Mutter
         * and wlroots use the focused (sub)surface. With the OpenGL and Vulkan
         * renderers those differ, so lock both and let the compositor activate
         * whichever one it recognises. */
        if (pointer) {
            conf_pointer = lock_pointer_to(surface, pointer);
            if (toplevel_surface != surface)
                conf_pointer_toplevel = lock_pointer_to(toplevel_surface, pointer);
        }
    }
}

void
wl_mouse_uncapture()
{
    if (conf_pointer_interface && !pointer_locked)
        qWarning() << "Wayland: the compositor never activated the pointer lock";

    if (conf_pointer)
        zwp_locked_pointer_v1_destroy(conf_pointer);
    if (conf_pointer_toplevel)
        zwp_locked_pointer_v1_destroy(conf_pointer_toplevel);
    if (rel_pointer)
        zwp_relative_pointer_v1_destroy(rel_pointer);
    rel_pointer           = nullptr;
    conf_pointer          = nullptr;
    conf_pointer_toplevel = nullptr;
    pointer_locked        = false;
}
