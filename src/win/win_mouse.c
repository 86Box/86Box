/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          RawInput mouse interface.
 *
 *
 *
 * Authors: Miran Grca, <mgrca8@gmail.com>
 *          GH Cao, <driver1998.ms@outlook.com>
 *          Jasmine Iwanek,
 *
 *          Copyright 2016-2017 Miran Grca.
 *          Copyright 2019 GH Cao.
 *          Copyright 2021-2023 Jasmine Iwanek.
 */
#include <windows.h>
#include <windowsx.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdint.h>
#include <86box/86box.h>
#include <86box/mouse.h>
#include <86box/pic.h>
#include <86box/plat.h>
#include <86box/win.h>

int    mouse_capture;

void
win_mouse_init(void)
{
    atexit(win_mouse_close);

    mouse_capture = 0;

    /* Initialize the RawInput (mouse) module. */
    RAWINPUTDEVICE ridev;
    ridev.dwFlags     = 0;
    ridev.hwndTarget  = NULL;
    ridev.usUsagePage = 0x01;
    ridev.usUsage     = 0x02;
    if (!RegisterRawInputDevices(&ridev, 1, sizeof(ridev)))
        fatal("plat_mouse_init: RegisterRawInputDevices failed\n");
}

void
win_mouse_handle(PRAWINPUT raw)
{
    RAWMOUSE   state = raw->data.mouse;
    static int x;
    static int delta_x;
    static int y;
    static int delta_y;
    static int b;
    static int delta_z;

    b = mouse_get_buttons_ex();

    /* read mouse buttons and wheel */
    if (state.usButtonFlags & RI_MOUSE_LEFT_BUTTON_DOWN)
        b |= 1;
    else if (state.usButtonFlags & RI_MOUSE_LEFT_BUTTON_UP)
        b &= ~1;

    if (state.usButtonFlags & RI_MOUSE_MIDDLE_BUTTON_DOWN)
        b |= 4;
    else if (state.usButtonFlags & RI_MOUSE_MIDDLE_BUTTON_UP)
        b &= ~4;

    if (state.usButtonFlags & RI_MOUSE_RIGHT_BUTTON_DOWN)
        b |= 2;
    else if (state.usButtonFlags & RI_MOUSE_RIGHT_BUTTON_UP)
        b &= ~2;

    if (state.usButtonFlags & RI_MOUSE_BUTTON_4_DOWN)
        b |= 8;
    else if (state.usButtonFlags & RI_MOUSE_BUTTON_4_UP)
        b &= ~8;

    if (state.usButtonFlags & RI_MOUSE_BUTTON_5_DOWN)
        b |= 16;
    else if (state.usButtonFlags & RI_MOUSE_BUTTON_5_UP)
        b &= ~16;

    mouse_set_buttons_ex(b);

    if (state.usButtonFlags & RI_MOUSE_WHEEL) {
        delta_z = (SHORT) state.usButtonData / 120;
        mouse_set_z(delta_z);
    } else
        delta_z = 0;

    if (state.usFlags & MOUSE_MOVE_ABSOLUTE) {
        /* absolute mouse, i.e. RDP or VNC
         * seems to work fine for RDP on Windows 10
         * Not sure about other environments.
         */
        delta_x = (state.lLastX - x) / 25;
        delta_y = (state.lLastY - y) / 25;
        x = state.lLastX;
        y = state.lLastY;
    } else {
        /* relative mouse, i.e. regular mouse */
        delta_x = state.lLastX;
        delta_y = state.lLastY;
    }

    mouse_scale(delta_x, delta_y);
}

void
win_mouse_close(void)
{
    RAWINPUTDEVICE ridev;
    ridev.dwFlags     = RIDEV_REMOVE;
    ridev.hwndTarget  = NULL;
    ridev.usUsagePage = 0x01;
    ridev.usUsage     = 0x02;
    RegisterRawInputDevices(&ridev, 1, sizeof(ridev));
}
