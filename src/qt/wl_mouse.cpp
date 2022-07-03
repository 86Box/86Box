/*
 * 86Box	A hypervisor and IBM PC system emulator that specializes in
 *		running old operating systems and software designed for IBM
 *		PC systems and compatibles from 1981 through fairly recent
 *		system designs based on the PCI bus.
 *
 *		This file is part of the 86Box distribution.
 *
 *		Wayland mouse input module.
 *
 *
 *
 * Authors:	Cacodemon345
 *
 *		Copyright 2021-2022 Cacodemon345
 */
#include "wl_mouse.hpp"
#include <QGuiApplication>
#include <wayland-client-core.h>
#include <wayland-client-protocol.h>
#include <wayland-relative-pointer-unstable-v1-client-protocol.h>
#include <wayland-pointer-constraints-unstable-v1-client-protocol.h>

#include <qpa/qplatformnativeinterface.h>
#include <QWindow>
#include <QGuiApplication>

extern "C"
{
#include <86box/plat.h>
}

static zwp_relative_pointer_manager_v1* rel_manager = nullptr;
static zwp_relative_pointer_v1* rel_pointer = nullptr;
static zwp_pointer_constraints_v1* conf_pointer_interface = nullptr;
static zwp_locked_pointer_v1* conf_pointer = nullptr;

static int rel_mouse_x = 0, rel_mouse_y = 0;
static bool wl_init_ok = false;

void rel_mouse_event(void* data, zwp_relative_pointer_v1* zwp_relative_pointer_v1, uint32_t tstmp, uint32_t tstmpl, wl_fixed_t dx, wl_fixed_t dy, wl_fixed_t dx_real, wl_fixed_t dy_real)
{
    rel_mouse_x += wl_fixed_to_int(dx_real);
    rel_mouse_y += wl_fixed_to_int(dy_real);
}

extern "C"
{
    extern int mouse_x, mouse_y;
}

void wl_mouse_poll()
{
    mouse_x = rel_mouse_x;
    mouse_y = rel_mouse_y;
    rel_mouse_x = 0;
    rel_mouse_y = 0;
}

static struct zwp_relative_pointer_v1_listener rel_listener =
{
    rel_mouse_event
};

static void
display_handle_global(void *data, struct wl_registry *registry, uint32_t id,
                      const char *interface, uint32_t version)
{
    if (!strcmp(interface, "zwp_relative_pointer_manager_v1"))
    {
        rel_manager = (zwp_relative_pointer_manager_v1*)wl_registry_bind(registry, id, &zwp_relative_pointer_manager_v1_interface, version);
    }
    if (!strcmp(interface, "zwp_pointer_constraints_v1"))
    {
        conf_pointer_interface = (zwp_pointer_constraints_v1*)wl_registry_bind(registry, id, &zwp_pointer_constraints_v1_interface, version);
    }
}

static void
display_global_remove(void *data, struct wl_registry *wl_registry, uint32_t name)
{
    plat_mouse_capture(0);
    zwp_relative_pointer_manager_v1_destroy(rel_manager);
    zwp_pointer_constraints_v1_destroy(conf_pointer_interface);
    rel_manager = nullptr;
    conf_pointer_interface = nullptr;
}

static const struct wl_registry_listener registry_listener = {
    display_handle_global,
    display_global_remove
};

void wl_init()
{
    if (!wl_init_ok) {
        wl_display* display = (wl_display*)QGuiApplication::platformNativeInterface()->nativeResourceForIntegration("wl_display");
        if (display)
        {
            auto registry = wl_display_get_registry(display);
            if (registry)
            {
                wl_registry_add_listener(registry, &registry_listener, nullptr);
                wl_display_roundtrip(display);
            }
        }
        wl_init_ok = true;
    }
}

void wl_mouse_capture(QWindow *window)
{
    if (rel_manager) {
        rel_pointer = zwp_relative_pointer_manager_v1_get_relative_pointer(rel_manager, (wl_pointer*)QGuiApplication::platformNativeInterface()->nativeResourceForIntegration("wl_pointer"));
        zwp_relative_pointer_v1_add_listener(rel_pointer, &rel_listener, nullptr);
    }
    if (conf_pointer_interface) conf_pointer = zwp_pointer_constraints_v1_lock_pointer(conf_pointer_interface, (wl_surface*)QGuiApplication::platformNativeInterface()->nativeResourceForWindow("surface", window), (wl_pointer*)QGuiApplication::platformNativeInterface()->nativeResourceForIntegration("wl_pointer"), nullptr, ZWP_POINTER_CONSTRAINTS_V1_LIFETIME_PERSISTENT);
}

void wl_mouse_uncapture()
{
    if (conf_pointer) zwp_locked_pointer_v1_destroy(conf_pointer);
    if (rel_pointer) zwp_relative_pointer_v1_destroy(rel_pointer);
    rel_pointer = nullptr;
    conf_pointer = nullptr;
}
