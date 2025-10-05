/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          xkbcommon-x11 keyboard input module.
 *
 *          Heavily inspired by libxkbcommon interactive-x11.c
 *
 * Authors: RichardG, <richardg867@gmail.com>
 *
 *          Copyright 2023 RichardG.
 */
extern "C" {
/* xkb.h has identifiers named "explicit", which is a C++ keyword now... */
#ifdef __clang__
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wkeyword-macro"
#endif
#define explicit explicit_
#ifdef __clang__
#pragma clang diagnostic pop
#endif
#include <xcb/xkb.h>
#undef explicit

#include <xkbcommon/xkbcommon-x11.h>
};
#include "xkbcommon_keyboard.hpp"

#include <qpa/qplatformnativeinterface.h>
#include <QtDebug>
#include <QGuiApplication>

void
xkbcommon_x11_init()
{
    xcb_connection_t *conn;
    struct xkb_context *ctx;
    int32_t core_kbd_device_id;
    struct xkb_keymap *keymap;

    conn = (xcb_connection_t *) QGuiApplication::platformNativeInterface()->nativeResourceForIntegration("connection");
    if (!conn) {
        qWarning() << "XKB Keyboard: X server connection failed";
        return;
    }

    int ret = xkb_x11_setup_xkb_extension(conn,
                                          XKB_X11_MIN_MAJOR_XKB_VERSION, XKB_X11_MIN_MINOR_XKB_VERSION,
                                          XKB_X11_SETUP_XKB_EXTENSION_NO_FLAGS,
                                          NULL, NULL, NULL, NULL);
    if (!ret) {
        qWarning() << "XKB Keyboard: XKB extension setup failed";
        return;
    }

    ctx = xkb_context_new(XKB_CONTEXT_NO_FLAGS);
    if (!ctx) {
        qWarning() << "XKB Keyboard: XKB context creation failed";
        return;
    }

    core_kbd_device_id = xkb_x11_get_core_keyboard_device_id(conn);
    if (core_kbd_device_id == -1) {
        qWarning() << "XKB Keyboard: Core keyboard device not found";
        goto err_ctx;
    }

    keymap = xkb_x11_keymap_new_from_device(ctx, conn, core_kbd_device_id, XKB_KEYMAP_COMPILE_NO_FLAGS);
    if (!keymap) {
        qWarning() << "XKB Keyboard: Keymap loading failed";
        goto err_ctx;
    }

    xkbcommon_init(keymap);
    return;

err_ctx:
    xkb_context_unref(ctx);
}
