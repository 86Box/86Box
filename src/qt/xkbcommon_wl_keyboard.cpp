/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          xkbcommon Wayland keyboard input module.
 *
 *          Heavily inspired by libxkbcommon interactive-wayland.c
 *
 * Authors: RichardG, <richardg867@gmail.com>
 *
 *          Copyright 2023 RichardG.
 */
extern "C" {
#include <sys/mman.h>
#include <unistd.h>
#include <xkbcommon/xkbcommon.h>
#include <86box/86box.h>
};
#include "xkbcommon_keyboard.hpp"
#include <wayland-client.h>
#include <wayland-util.h>

#include <qpa/qplatformnativeinterface.h>
#include <QtDebug>
#include <QGuiApplication>

typedef struct {
    struct wl_seat *wl_seat;
    struct wl_keyboard *wl_kbd;
    uint32_t version;

    struct xkb_keymap *keymap;

    struct wl_list link;
} seat_t;

static bool wl_init_ok = false;
static struct wl_list seats;
static struct xkb_context *ctx;

static void
xkbcommon_wl_set_keymap()
{
    /* Grab keymap from the first seat with one. */
    seat_t *seat;
    seat_t *tmp;
    wl_list_for_each_safe(seat, tmp, &seats, link) {
        if (seat->keymap) {
            xkbcommon_init(seat->keymap);
            return;
        }
    }
    xkbcommon_close(); /* none found */
}

static void
kbd_keymap(void *data, struct wl_keyboard *wl_kbd, uint32_t format,
           int fd, uint32_t size)
{
    seat_t *seat = (seat_t *) data;

    char *buf = (char *) mmap(NULL, size, PROT_READ, MAP_SHARED, fd, 0);
    if (!buf) {
        qWarning() << "XKB Keyboard: Failed to mmap keymap with error" << errno;
        return;
    }

    if (seat->keymap) {
        struct xkb_keymap *keymap = seat->keymap;
        seat->keymap = NULL;
        xkbcommon_wl_set_keymap();
        xkb_keymap_unref(keymap);
    }

    seat->keymap = xkb_keymap_new_from_buffer(ctx, buf, size - 1,
                                              XKB_KEYMAP_FORMAT_TEXT_V1,
                                              XKB_KEYMAP_COMPILE_NO_FLAGS);
    munmap(buf, size);
    close(fd);
    if (!seat->keymap)
        qWarning() << "XKB Keyboard: Keymap compilation failed";

    xkbcommon_wl_set_keymap();
}

static void
kbd_enter(void *data, struct wl_keyboard *wl_kbd, uint32_t serial,
          struct wl_surface *surf, struct wl_array *keys)
{
}

static void
kbd_leave(void *data, struct wl_keyboard *wl_kbd, uint32_t serial,
          struct wl_surface *surf)
{
}

static void
kbd_key(void *data, struct wl_keyboard *wl_kbd, uint32_t serial, uint32_t time,
        uint32_t key, uint32_t state)
{
}

static void
kbd_modifiers(void *data, struct wl_keyboard *wl_kbd, uint32_t serial,
              uint32_t mods_depressed, uint32_t mods_latched,
              uint32_t mods_locked, uint32_t group)
{
}

static void
kbd_repeat_info(void *data, struct wl_keyboard *wl_kbd, int32_t rate,
                int32_t delay)
{
}

static const struct wl_keyboard_listener kbd_listener = {
    kbd_keymap,
    kbd_enter,
    kbd_leave,
    kbd_key,
    kbd_modifiers,
    kbd_repeat_info
};

static void
seat_capabilities(void *data, struct wl_seat *wl_seat, uint32_t caps)
{
    seat_t *seat = (seat_t *) data;

    if (!seat->wl_kbd && (caps & WL_SEAT_CAPABILITY_KEYBOARD)) {
        seat->wl_kbd = wl_seat_get_keyboard(seat->wl_seat);
        wl_keyboard_add_listener(seat->wl_kbd, &kbd_listener, seat);
    } else if (seat->wl_kbd && !(caps & WL_SEAT_CAPABILITY_KEYBOARD)) {
        if (seat->version >= WL_SEAT_RELEASE_SINCE_VERSION)
            wl_keyboard_release(seat->wl_kbd);
        else
            wl_keyboard_destroy(seat->wl_kbd);

        struct xkb_keymap *keymap = seat->keymap;
        seat->keymap = NULL;
        xkbcommon_wl_set_keymap();
        xkb_keymap_unref(keymap);

        seat->wl_kbd = NULL;
    }
}

static void
seat_name(void *data, struct wl_seat *wl_seat, const char *name)
{
}

static const struct wl_seat_listener seat_listener = {
    seat_capabilities,
    seat_name
};

static void
display_handle_global(void *data, struct wl_registry *wl_registry, uint32_t id,
                      const char *interface, uint32_t version)
{
    if (!strcmp(interface, "wl_seat")) {
        seat_t *seat = (seat_t *) malloc(sizeof(seat_t));
        memset(seat, 0, sizeof(seat_t));

        seat->wl_seat = (wl_seat *) wl_registry_bind(wl_registry, id, &wl_seat_interface, MIN(version, 5));
        wl_seat_add_listener(seat->wl_seat, &seat_listener, seat);
        wl_list_insert(&seats, &seat->link);
    }
}

static void
display_global_remove(void *data, struct wl_registry *wl_registry, uint32_t id)
{
    xkbcommon_close();

    seat_t *seat;
    seat_t *tmp;
    wl_list_for_each_safe(seat, tmp, &seats, link) {
        if (seat->wl_kbd) {
            if (seat->version >= WL_SEAT_RELEASE_SINCE_VERSION)
                wl_keyboard_release(seat->wl_kbd);
            else
                wl_keyboard_destroy(seat->wl_kbd);

            xkb_keymap_unref(seat->keymap);
        }

        if (seat->version >= WL_SEAT_RELEASE_SINCE_VERSION)
            wl_seat_release(seat->wl_seat);
        else
            wl_seat_destroy(seat->wl_seat);

        wl_list_remove(&seat->link);
        free(seat);
    }
}

static const struct wl_registry_listener registry_listener = {
    display_handle_global,
    display_global_remove
};

void
xkbcommon_wl_init()
{
    if (wl_init_ok)
        return;

    wl_list_init(&seats);

    ctx = xkb_context_new(XKB_CONTEXT_NO_FLAGS);
    if (!ctx) {
        qWarning() << "XKB Keyboard: XKB context creation failed";
        return;
    }

    wl_display *display = (wl_display *) QGuiApplication::platformNativeInterface()->nativeResourceForIntegration("wl_display");
    if (display) {
        auto registry = wl_display_get_registry(display);
        if (registry) {
            wl_registry_add_listener(registry, &registry_listener, nullptr);
            wl_display_roundtrip(display);
            wl_display_roundtrip(display);
        } else {
            goto err_ctx;
        }
    } else {
        goto err_ctx;
    }
    wl_init_ok = true;
    return;

err_ctx:
    xkb_context_unref(ctx);
}
