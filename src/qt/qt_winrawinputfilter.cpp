/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          Windows raw input native filter for QT
 *
 *
 *
 * Authors: Teemu Korhonen
 *          Miran Grca, <mgrca8@gmail.com>
 *
 *          Copyright 2021 Teemu Korhonen
 *          Copyright 2016-2018 Miran Grca.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 */

#include "qt_winrawinputfilter.hpp"

#include <QMenuBar>

#include <atomic>

#include <windows.h>

#include <86box/keyboard.h>
#include <86box/mouse.h>
#include <86box/plat.h>
#include <86box/86box.h>

extern void    win_keyboard_handle(uint32_t scancode, int up, int e0, int e1);

#include <array>
#include <memory>

#include "qt_rendererstack.hpp"

extern "C" void win_joystick_handle(PRAWINPUT);
std::unique_ptr<WindowsRawInputFilter>
WindowsRawInputFilter::Register(MainWindow *window)
{
    RAWINPUTDEVICE rid[2] = {
        {.usUsagePage = 0x01,
         .usUsage     = 0x06,
         .dwFlags     = RIDEV_NOHOTKEYS,
         .hwndTarget  = nullptr},
        { .usUsagePage = 0x01,
         .usUsage     = 0x02,
         .dwFlags     = 0,
         .hwndTarget  = nullptr}
    };

    if (hook_enabled && (RegisterRawInputDevices(&(rid[1]), 1, sizeof(rid[0])) == FALSE))
            return std::unique_ptr<WindowsRawInputFilter>(nullptr);
    else if (!hook_enabled && (RegisterRawInputDevices(rid, 2, sizeof(rid[0])) == FALSE))
            return std::unique_ptr<WindowsRawInputFilter>(nullptr);

    std::unique_ptr<WindowsRawInputFilter> inputfilter(new WindowsRawInputFilter(window));

    return inputfilter;
}

WindowsRawInputFilter::WindowsRawInputFilter(MainWindow *window)
{
    this->window = window;

    for (auto menu : window->findChildren<QMenu *>()) {
        connect(menu, &QMenu::aboutToShow, this, [=]() { menus_open++; });
        connect(menu, &QMenu::aboutToHide, this, [=]() { menus_open--; });
    }
}

WindowsRawInputFilter::~WindowsRawInputFilter()
{
    RAWINPUTDEVICE rid[2] = {
        {.usUsagePage = 0x01,
         .usUsage     = 0x06,
         .dwFlags     = RIDEV_REMOVE,
         .hwndTarget  = NULL},
        { .usUsagePage = 0x01,
         .usUsage     = 0x02,
         .dwFlags     = RIDEV_REMOVE,
         .hwndTarget  = NULL}
    };

    if (hook_enabled)
        RegisterRawInputDevices(&(rid[1]), 1, sizeof(rid[0]));
    else
        RegisterRawInputDevices(rid, 2, sizeof(rid[0]));
}

bool
WindowsRawInputFilter::nativeEventFilter(const QByteArray &eventType, void *message, result_t *result)
{
    if (eventType == "windows_generic_MSG") {
        MSG *msg = static_cast<MSG *>(message);

        if (msg->message == WM_INPUT) {

            if (window->isActiveWindow() && menus_open == 0)
                handle_input((HRAWINPUT) msg->lParam);
            else
            {
                for (auto &w : window->renderers) {
                    if (w && w->isActiveWindow()) {
                        handle_input((HRAWINPUT) msg->lParam);
                        break;
                    }
                }
            }

            return true;
        }

        /* Stop processing of Alt-F4 */
        if (msg->message == WM_SYSKEYDOWN) {
            if (msg->wParam == 0x73) {
                return true;
            }
        }
    }

    return false;
}

void
WindowsRawInputFilter::handle_input(HRAWINPUT input)
{
    UINT size = 0;

    GetRawInputData(input, RID_INPUT, NULL, &size, sizeof(RAWINPUTHEADER));

    std::vector<BYTE> buf(size);

    if (GetRawInputData(input, RID_INPUT, buf.data(), &size, sizeof(RAWINPUTHEADER)) == size) {
        PRAWINPUT raw = (PRAWINPUT) buf.data();

        switch (raw->header.dwType) {
            case RIM_TYPEKEYBOARD:
                keyboard_handle(raw);
                break;
            case RIM_TYPEMOUSE:
                if (mouse_capture)
                    mouse_handle(raw);
                break;
            case RIM_TYPEHID:
                win_joystick_handle(raw);
                break;
        }
    }
}

/* The following is more or less a direct copy of the old WIN32 implementation */

void
WindowsRawInputFilter::keyboard_handle(PRAWINPUT raw)
{
    RAWKEYBOARD rawKB = raw->data.keyboard;

    win_keyboard_handle(rawKB.MakeCode, (rawKB.Flags & RI_KEY_BREAK),
                        (rawKB.Flags & RI_KEY_E0), (rawKB.Flags & RI_KEY_E1));
}

void
WindowsRawInputFilter::mouse_handle(PRAWINPUT raw)
{
    RAWMOUSE   state = raw->data.mouse;
    static int x, delta_x;
    static int y, delta_y;
    static int b, delta_z;

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

    HWND wnd = (HWND)window->winId();

    RECT rect;

    GetWindowRect(wnd, &rect);

    int left = rect.left + (rect.right - rect.left) / 2;
    int top = rect.top + (rect.bottom - rect.top) / 2;

    SetCursorPos(left, top);
}
