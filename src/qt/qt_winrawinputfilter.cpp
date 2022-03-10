/*
 * 86Box A hypervisor and IBM PC system emulator that specializes in
 *      running old operating systems and software designed for IBM
 *      PC systems and compatibles from 1981 through fairly recent
 *      system designs based on the PCI bus.
 *
 *      This file is part of the 86Box distribution.
 *
 *      Windows raw input native filter for QT
 *
 * Authors:
 *      Teemu Korhonen
 *      Miran Grca, <mgrca8@gmail.com>
 *
 *      Copyright 2021 Teemu Korhonen
 *      Copyright 2016-2018 Miran Grca.
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

#include <Windows.h>

#include <86box/keyboard.h>
#include <86box/mouse.h>
#include <86box/plat.h>
#include <86box/86box.h>

#include <array>
#include <memory>

extern "C" void win_joystick_handle(PRAWINPUT);
std::unique_ptr<WindowsRawInputFilter> WindowsRawInputFilter::Register(QMainWindow *window)
{
    HWND wnd = (HWND)window->winId();

    RAWINPUTDEVICE rid[2] =
    {
        {
            .usUsagePage = 0x01,
            .usUsage = 0x06,
            .dwFlags = RIDEV_NOHOTKEYS,
            .hwndTarget = wnd
        },
        {
            .usUsagePage = 0x01,
            .usUsage = 0x02,
            .dwFlags = 0,
            .hwndTarget = wnd
        }
    };

    if (RegisterRawInputDevices(rid, 2, sizeof(rid[0])) == FALSE)
        return std::unique_ptr<WindowsRawInputFilter>(nullptr);

    std::unique_ptr<WindowsRawInputFilter> inputfilter(new WindowsRawInputFilter(window));

    return inputfilter;
}

WindowsRawInputFilter::WindowsRawInputFilter(QMainWindow *window)
{
    this->window = window;

    for (auto menu : window->findChildren<QMenu*>())
    {
        connect(menu, &QMenu::aboutToShow, this, [=]() { menus_open++; });
        connect(menu, &QMenu::aboutToHide, this, [=]() { menus_open--; });
    }

    for (size_t i = 0; i < sizeof(scancode_map) / sizeof(scancode_map[0]); i++)
        scancode_map[i] = i;

    keyboard_getkeymap();
}

WindowsRawInputFilter::~WindowsRawInputFilter()
{
    RAWINPUTDEVICE rid[2] =
    {
        {
            .usUsagePage = 0x01,
            .usUsage = 0x06,
            .dwFlags = RIDEV_REMOVE,
            .hwndTarget = NULL
        },
        {
            .usUsagePage = 0x01,
            .usUsage = 0x02,
            .dwFlags = RIDEV_REMOVE,
            .hwndTarget = NULL
        }
    };

    RegisterRawInputDevices(rid, 2, sizeof(rid[0]));
}

bool WindowsRawInputFilter::nativeEventFilter(const QByteArray &eventType, void *message, result_t *result)
{
    if (eventType == "windows_generic_MSG")
    {
        MSG *msg = static_cast<MSG *>(message);

        if (msg->message == WM_INPUT) {
            if (window->isActiveWindow() && menus_open == 0)
                handle_input((HRAWINPUT) msg->lParam);

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

void WindowsRawInputFilter::handle_input(HRAWINPUT input)
{
    UINT size = 0;

    GetRawInputData(input, RID_INPUT, NULL, &size, sizeof(RAWINPUTHEADER));

    std::vector<BYTE> buf(size);

    if (GetRawInputData(input, RID_INPUT, buf.data(), &size, sizeof(RAWINPUTHEADER)) == size)
    {
        PRAWINPUT raw = (PRAWINPUT)buf.data();

        switch(raw->header.dwType)
        {
            case RIM_TYPEKEYBOARD:
                keyboard_handle(raw);
                break;
            case RIM_TYPEMOUSE:
                if (mouse_capture)
                    mouse_handle(raw);
                break;
            case RIM_TYPEHID:
            {
                win_joystick_handle(raw);
                break;
            }
        }
    }
}

/* The following is more or less a direct copy of the old WIN32 implementation */

void WindowsRawInputFilter::keyboard_handle(PRAWINPUT raw)
{
    USHORT scancode;
    static int recv_lalt = 0, recv_ralt = 0, recv_tab = 0;

    RAWKEYBOARD rawKB = raw->data.keyboard;
    scancode = rawKB.MakeCode;

    if (kbd_req_capture && !mouse_capture && !video_fullscreen)
        return;

    /* If it's not a scan code that starts with 0xE1 */
    if (!(rawKB.Flags & RI_KEY_E1))
    {
        if (rawKB.Flags & RI_KEY_E0)
            scancode |= 0x100;

        /* Translate the scan code to 9-bit */
        scancode = convert_scan_code(scancode);

        /* Remap it according to the list from the Registry */
        if (scancode != scancode_map[scancode])
            pclog("Scan code remap: %03X -> %03X\n", scancode, scancode);
        scancode = scancode_map[scancode];

        /* If it's not 0xFFFF, send it to the emulated
       keyboard.
       We use scan code 0xFFFF to mean a mapping that
       has a prefix other than E0 and that is not E1 1D,
       which is, for our purposes, invalid. */
        if ((scancode == 0x00F) &&
            !(rawKB.Flags & RI_KEY_BREAK) &&
            (recv_lalt || recv_ralt) &&
            !mouse_capture)
        {
            /* We received a TAB while ALT was pressed, while the mouse
           is not captured, suppress the TAB and send an ALT key up. */
            if (recv_lalt)
            {
                keyboard_input(0, 0x038);
                /* Extra key press and release so the guest is not stuck in the
               menu bar. */
                keyboard_input(1, 0x038);
                keyboard_input(0, 0x038);
                recv_lalt = 0;
            }
            if (recv_ralt)
            {
                keyboard_input(0, 0x138);
                /* Extra key press and release so the guest is not stuck in the
               menu bar. */
                keyboard_input(1, 0x138);
                keyboard_input(0, 0x138);
                recv_ralt = 0;
            }
        }
        else if (((scancode == 0x038) || (scancode == 0x138)) &&
                 !(rawKB.Flags & RI_KEY_BREAK) &&
                 recv_tab &&
                 !mouse_capture)
        {
            /* We received an ALT while TAB was pressed, while the mouse
           is not captured, suppress the ALT and send a TAB key up. */
            keyboard_input(0, 0x00F);
            recv_tab = 0;
        }
        else
        {
            switch (scancode)
            {
            case 0x00F:
                recv_tab = !(rawKB.Flags & RI_KEY_BREAK);
                break;
            case 0x038:
                recv_lalt = !(rawKB.Flags & RI_KEY_BREAK);
                break;
            case 0x138:
                recv_ralt = !(rawKB.Flags & RI_KEY_BREAK);
                break;
            }

            /* Translate right CTRL to left ALT if the user has so
           chosen. */
            if ((scancode == 0x11D) && rctrl_is_lalt)
                scancode = 0x038;

            /* Normal scan code pass through, pass it through as is if
           it's not an invalid scan code. */
            if (scancode != 0xFFFF)
                keyboard_input(!(rawKB.Flags & RI_KEY_BREAK), scancode);
        }
    }
    else
    {
        if (rawKB.MakeCode == 0x1D)
        {
            scancode = scancode_map[0x100]; /* Translate E1 1D to 0x100 (which would
                           otherwise be E0 00 but that is invalid
                           anyway).
                           Also, take a potential mapping into
                           account. */
        }
        else
            scancode = 0xFFFF;
        if (scancode != 0xFFFF)
            keyboard_input(!(rawKB.Flags & RI_KEY_BREAK), scancode);
    }
}

/* This is so we can disambiguate scan codes that would otherwise conflict and get
   passed on incorrectly. */
UINT16 WindowsRawInputFilter::convert_scan_code(UINT16 scan_code)
{
    if ((scan_code & 0xff00) == 0xe000)
        scan_code = (scan_code & 0xff) | 0x0100;

    if (scan_code == 0xE11D)
        scan_code = 0x0100;
    /* E0 00 is sent by some USB keyboards for their special keys, as it is an
       invalid scan code (it has no untranslated set 2 equivalent), we mark it
       appropriately so it does not get passed through. */
    else if ((scan_code > 0x01FF) || (scan_code == 0x0100))
        scan_code = 0xFFFF;

    return scan_code;
}

void WindowsRawInputFilter::keyboard_getkeymap()
{
    const LPCSTR keyName = "SYSTEM\\CurrentControlSet\\Control\\Keyboard Layout";
    const LPCSTR valueName = "Scancode Map";
    unsigned char buf[32768];
    DWORD bufSize;
    HKEY hKey;
    int j;
    UINT32 *bufEx2;
    int scMapCount;
    UINT16 *bufEx;
    int scancode_unmapped;
    int scancode_mapped;

    /* First, prepare the default scan code map list which is 1:1.
     * Remappings will be inserted directly into it.
     * 512 bytes so this takes less memory, bit 9 set means E0
     * prefix.
     */
    for (j = 0; j < 512; j++)
        scancode_map[j] = j;

    /* Get the scan code remappings from:
    HKEY_LOCAL_MACHINE\SYSTEM\CurrentControlSet\Control\Keyboard Layout */
    bufSize = 32768;
    if (RegOpenKeyExA(HKEY_LOCAL_MACHINE, keyName, 0, 1, &hKey) == ERROR_SUCCESS)
    {
        if (RegQueryValueExA(hKey, valueName, NULL, NULL, buf, &bufSize) == ERROR_SUCCESS)
        {
            bufEx2 = (UINT32 *)buf;
            scMapCount = bufEx2[2];
            if ((bufSize != 0) && (scMapCount != 0))
            {
                bufEx = (UINT16 *)(buf + 12);
                for (j = 0; j < scMapCount * 2; j += 2)
                {
                    /* Each scan code is 32-bit: 16 bits of remapped scan code,
                    and 16 bits of original scan code. */
                    scancode_unmapped = bufEx[j + 1];
                    scancode_mapped = bufEx[j];

                    scancode_unmapped = convert_scan_code(scancode_unmapped);
                    scancode_mapped = convert_scan_code(scancode_mapped);

                    /* Ignore source scan codes with prefixes other than E1
                   that are not E1 1D. */
                    if (scancode_unmapped != 0xFFFF)
                        scancode_map[scancode_unmapped] = scancode_mapped;
                }
            }
        }
        RegCloseKey(hKey);
    }
}

void WindowsRawInputFilter::mouse_handle(PRAWINPUT raw)
{
    RAWMOUSE state = raw->data.mouse;
    static int x, y;

    /* read mouse buttons and wheel */
    if (state.usButtonFlags & RI_MOUSE_LEFT_BUTTON_DOWN)
        buttons |= 1;
    else if (state.usButtonFlags & RI_MOUSE_LEFT_BUTTON_UP)
        buttons &= ~1;

    if (state.usButtonFlags & RI_MOUSE_MIDDLE_BUTTON_DOWN)
        buttons |= 4;
    else if (state.usButtonFlags & RI_MOUSE_MIDDLE_BUTTON_UP)
        buttons &= ~4;

    if (state.usButtonFlags & RI_MOUSE_RIGHT_BUTTON_DOWN)
        buttons |= 2;
    else if (state.usButtonFlags & RI_MOUSE_RIGHT_BUTTON_UP)
        buttons &= ~2;

    if (state.usButtonFlags & RI_MOUSE_WHEEL)
    {
        dwheel += (SHORT)state.usButtonData / 120;
    }

    if (state.usFlags & MOUSE_MOVE_ABSOLUTE)
    {
        /* absolute mouse, i.e. RDP or VNC
         * seems to work fine for RDP on Windows 10
         * Not sure about other environments.
         */
        dx += (state.lLastX - x) / 25;
        dy += (state.lLastY - y) / 25;
        x = state.lLastX;
        y = state.lLastY;
    }
    else
    {
        /* relative mouse, i.e. regular mouse */
        dx += state.lLastX;
        dy += state.lLastY;
    }
    HWND wnd = (HWND)window->winId();

    RECT rect;

    GetWindowRect(wnd, &rect);

    int left = rect.left + (rect.right - rect.left) / 2;
    int top = rect.top + (rect.bottom - rect.top) / 2;

    SetCursorPos(left, top);
}

void WindowsRawInputFilter::mousePoll()
{
    if (mouse_capture || video_fullscreen)
    {
        static int b = 0;

        if (dx != 0 || dy != 0 || dwheel != 0)
        {
            mouse_x += dx;
            mouse_y += dy;
            mouse_z = dwheel;

            dx = 0;
            dy = 0;
            dwheel = 0;
        }

        if (b != buttons)
        {
            mouse_buttons = buttons;
            b = buttons;
        }
    }
}
