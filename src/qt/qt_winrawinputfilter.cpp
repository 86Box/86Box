/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          Windows raw input native filter for Qt
 *
 * Authors: Teemu Korhonen
 *          Miran Grca, <mgrca8@gmail.com>
 *          Sam Latinga
 *          Cacodemon345
 *
 *          Copyright 2021 Teemu Korhonen
 *          Copyright 2016-2018 Miran Grca.
 *          Copyright 1997-2025 Sam Latinga
 *          Copyright 2024-2025 Cacodemon345.
 *
 * See this header for SDL3 code license:
 * https://github.com/libsdl-org/SDL/blob/8e5fe0ea61dc87b29ca9a6119324221df0113bcf/src/video/windows/SDL_windowsrawinput.c#L1
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

/* Mouse RawInput code taken from SDL3. */

#include "qt_winrawinputfilter.hpp"

#include <QMenuBar>
#include <QFile>
#include <QTextStream>
#include <QApplication>
#include <QTimer>

#include <atomic>

#include <windows.h>
#include <dwmapi.h>
#ifndef DWMWA_USE_IMMERSIVE_DARK_MODE
#    define DWMWA_USE_IMMERSIVE_DARK_MODE 20
#endif

#include <86box/keyboard.h>
#include <86box/mouse.h>
#include <86box/plat.h>
#include <86box/86box.h>
#include <86box/cdrom.h>
#include <86box/video.h>
#include <dbt.h>
#include <strsafe.h>

extern void win_keyboard_handle(uint32_t scancode, int up, int e0, int e1);

#include <array>
#include <memory>

#include "qt_rendererstack.hpp"
#include "qt_util.hpp"
#include "ui_qt_mainwindow.h"

bool NewDarkMode = FALSE;

extern MainWindow *main_window;

struct
{
    HANDLE           done_event = 0, ready_event = 0;
    std::atomic_bool done { false };

    size_t   rawinput_offset = 0, rawinput_size = 0;
    uint8_t *rawinput = nullptr;

    HANDLE thread = 0;
} win_rawinput_data;

static void
win_poll_mouse(void)
{
    // Yes, this is a thing in C++.
    auto     *data = &win_rawinput_data;
    uint32_t  size, i, count, total = 0;
    RAWINPUT *input;
    // static int64_t ms_time = plat_get_ticks();

    if (data->rawinput_offset == 0) {
        BOOL isWow64;

        data->rawinput_offset = sizeof(RAWINPUTHEADER);
        if (IsWow64Process(GetCurrentProcess(), &isWow64) && isWow64) {
            // We're going to get 64-bit data, so use the 64-bit RAWINPUTHEADER size
            data->rawinput_offset += 8;
        }
    }

    input = (RAWINPUT *) data->rawinput;
    for (;;) {
        size  = data->rawinput_size - (UINT) ((BYTE *) input - data->rawinput);
        count = GetRawInputBuffer(input, &size, sizeof(RAWINPUTHEADER));
        if (count == 0 || count == (UINT) -1) {
            if (!data->rawinput || (count == (UINT) -1 && GetLastError() == ERROR_INSUFFICIENT_BUFFER)) {
                const UINT RAWINPUT_BUFFER_SIZE_INCREMENT = 96; // 2 64-bit raw mouse packets
                BYTE      *rawinput                       = (BYTE *) realloc(data->rawinput, data->rawinput_size + RAWINPUT_BUFFER_SIZE_INCREMENT);
                if (!rawinput) {
                    break;
                }
                input          = (RAWINPUT *) (rawinput + ((BYTE *) input - data->rawinput));
                data->rawinput = rawinput;
                data->rawinput_size += RAWINPUT_BUFFER_SIZE_INCREMENT;
            } else {
                break;
            }
        } else {
            total += count;

            // Advance input to the end of the buffer
            while (count--) {
                input = NEXTRAWINPUTBLOCK(input);
            }
        }
    }

    if (total > 0) {
        for (i = 0, input = (RAWINPUT *) data->rawinput; i < total; ++i, input = NEXTRAWINPUTBLOCK(input)) {
            if (input->header.dwType == RIM_TYPEMOUSE) {
                RAWMOUSE *rawmouse = (RAWMOUSE *) ((BYTE *) input + data->rawinput_offset);
                if (mouse_capture)
                    WindowsRawInputFilter::mouse_handle(rawmouse);
            }
        }
    }

    // qDebug() << "Mouse delay: " << (plat_get_ticks() - ms_time);
    // ms_time = plat_get_ticks();
}

static DWORD
win_rawinput_thread(void *param)
{
    RAWINPUTDEVICE rid = {
        .usUsagePage = 0x01,
        .usUsage     = 0x02,
        .dwFlags     = 0,
        .hwndTarget  = nullptr
    };
    auto window = CreateWindowEx(0, TEXT("Message"), NULL, 0, 0, 0, 0, 0, HWND_MESSAGE, NULL, NULL, NULL);
    if (!window) {
        return 0;
    }

    rid.hwndTarget = window;
    if (!RegisterRawInputDevices(&rid, 1, sizeof(rid))) {
        DestroyWindow(window);
        return 0;
    }

    SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_TIME_CRITICAL);

    SetEvent(win_rawinput_data.ready_event);

    while (!win_rawinput_data.done) {
        DWORD result = MsgWaitForMultipleObjects(1, &win_rawinput_data.done_event, FALSE, INFINITE, QS_RAWINPUT);

        if (result != (WAIT_OBJECT_0 + 1)) {
            break;
        }

        // Clear the queue status so MsgWaitForMultipleObjects() will wait again
        (void) GetQueueStatus(QS_RAWINPUT);

        win_poll_mouse();
    }

    rid.dwFlags |= RIDEV_REMOVE;
    rid.hwndTarget = NULL;

    RegisterRawInputDevices(&rid, 1, sizeof(rid));
    DestroyWindow(window);
    return 0;
}

extern "C" void win_joystick_handle(PRAWINPUT);
std::unique_ptr<WindowsRawInputFilter>
WindowsRawInputFilter::Register(MainWindow *window)
{
    RAWINPUTDEVICE rid[1] = {
        { .usUsagePage = 0x01,
         .usUsage     = 0x06,
         .dwFlags     = RIDEV_NOHOTKEYS,
         .hwndTarget  = nullptr }
    };

    if (!hook_enabled) {
        RegisterRawInputDevices(rid, 1, sizeof(rid[0]));
    }

    win_rawinput_data.done_event  = CreateEvent(nullptr, FALSE, FALSE, nullptr);
    win_rawinput_data.ready_event = CreateEvent(nullptr, FALSE, FALSE, nullptr);

    if (!win_rawinput_data.done_event || !win_rawinput_data.ready_event) {
        warning("Failed to create RawInput events.");

        goto conclude;
    }

    win_rawinput_data.thread = CreateThread(nullptr, 0, win_rawinput_thread, nullptr, 0, nullptr);
    if (win_rawinput_data.thread) {
        HANDLE handles[2] = { win_rawinput_data.ready_event, win_rawinput_data.thread };

        WaitForMultipleObjects(2, handles, FALSE, INFINITE);
    } else {
        warning("Failed to create RawInput thread.");
    }

conclude:
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
    win_rawinput_data.done = true;
    if (win_rawinput_data.done_event)
        SetEvent(win_rawinput_data.done_event);
    if (win_rawinput_data.thread)
        WaitForSingleObject(win_rawinput_data.thread, INFINITE);
    RAWINPUTDEVICE rid = {
        .usUsagePage = 0x01,
        .usUsage     = 0x06,
        .dwFlags     = RIDEV_REMOVE,
        .hwndTarget  = NULL
    };

    if (!hook_enabled)
        RegisterRawInputDevices(&rid, 1, sizeof(rid));

    free(win_rawinput_data.rawinput);
}

static void
notify_drives(ULONG unitmask, int empty)
{
    if (unitmask & cdrom_assigned_letters)
        for (int i = 0; i < CDROM_NUM; i++) {
            cdrom_t *dev = &(cdrom[i]);

            if ((dev->host_letter != 0xff) && (unitmask & (1 << dev->host_letter))) {
                if (empty)
                    cdrom_set_empty(dev);
                else
                    cdrom_update_status(dev);
            }
        }
}

static void
device_change(WPARAM wParam, LPARAM lParam)
{
    PDEV_BROADCAST_HDR lpdb = (PDEV_BROADCAST_HDR) lParam;

    switch (wParam) {
        case DBT_DEVICEARRIVAL:
        case DBT_DEVICEREMOVECOMPLETE:
            /* Check whether a CD or DVD was inserted into a drive. */
            if (lpdb->dbch_devicetype == DBT_DEVTYP_VOLUME) {
                PDEV_BROADCAST_VOLUME lpdbv = (PDEV_BROADCAST_VOLUME) lpdb;

                if (lpdbv->dbcv_flags & DBTF_MEDIA)
                    notify_drives(lpdbv->dbcv_unitmask,
                                  (wParam == DBT_DEVICEREMOVECOMPLETE));
            }
            break;

        default:
            /*
               Process other WM_DEVICECHANGE notifications for other
               devices or reasons.
             */
            break;
    }
}

void
selectDarkMode()
{
    bool OldDarkMode = NewDarkMode;

    if (!util::isWindowsLightTheme()) {
        QFile f(":qdarkstyle/dark/darkstyle.qss");

        if (!f.exists())
            printf("Unable to set stylesheet, file not found\n");
        else {
            f.open(QFile::ReadOnly | QFile::Text);
            QTextStream ts(&f);
            qApp->setStyleSheet(ts.readAll());
        }
        QPalette palette(qApp->palette());
        palette.setColor(QPalette::Link, Qt::white);
        palette.setColor(QPalette::LinkVisited, Qt::lightGray);
        qApp->setPalette(palette);
        NewDarkMode = TRUE;
    } else {
        qApp->setStyleSheet("");
        QPalette palette(qApp->palette());
        palette.setColor(QPalette::Link, Qt::blue);
        palette.setColor(QPalette::LinkVisited, Qt::magenta);
        qApp->setPalette(palette);
        NewDarkMode = FALSE;
    }

    if (NewDarkMode != OldDarkMode)
        QTimer::singleShot(1000, []() {
            BOOL DarkMode = NewDarkMode;
            DwmSetWindowAttribute((HWND) main_window->winId(),
                                  DWMWA_USE_IMMERSIVE_DARK_MODE,
                                  (LPCVOID) &DarkMode,
                                  sizeof(DarkMode));

            main_window->resizeContents(monitors[0].mon_scrnsz_x,
                                        monitors[0].mon_scrnsz_y);

            for (int i = 1; i < MONITORS_NUM; i++) {
                auto mon = &(monitors[i]);

                if ((main_window->renderers[i] != nullptr) && !main_window->renderers[i]->isHidden())
                    main_window->resizeContentsMonitor(mon->mon_scrnsz_x,
                                                       mon->mon_scrnsz_y, i);
            }
        });
}

bool
WindowsRawInputFilter::nativeEventFilter(const QByteArray &eventType, void *message, result_t *result)
{
    if (eventType == "windows_generic_MSG") {
        MSG *msg = static_cast<MSG *>(message);

        if (msg != nullptr)
            switch (msg->message) {
                case WM_INPUT:
                    if (window->isActiveWindow() && (menus_open == 0))
                        handle_input((HRAWINPUT) msg->lParam);
                    else {
                        for (auto &w : window->renderers) {
                            if (w && w->isActiveWindow()) {
                                handle_input((HRAWINPUT) msg->lParam);
                                break;
                            }
                        }
                    }
                    return true;
                case WM_SETTINGCHANGE:
                    if ((((void *) msg->lParam) != nullptr) && (wcscmp(L"ImmersiveColorSet", (wchar_t *) msg->lParam) == 0) && color_scheme == 0) {

                        bool OldDarkMode = NewDarkMode;
#if 0
                    if (do_auto_pause && !dopause) {
                        auto_paused = 1;
                        plat_pause(1);
                    }
#endif

                        if (!util::isWindowsLightTheme()) {
                            QFile f(":qdarkstyle/dark/darkstyle.qss");

                            if (!f.exists())
                                printf("Unable to set stylesheet, file not found\n");
                            else {
                                f.open(QFile::ReadOnly | QFile::Text);
                                QTextStream ts(&f);
                                qApp->setStyleSheet(ts.readAll());
                            }
                            QPalette palette(qApp->palette());
                            palette.setColor(QPalette::Link, Qt::white);
                            palette.setColor(QPalette::LinkVisited, Qt::lightGray);
                            qApp->setPalette(palette);
                            NewDarkMode = TRUE;
                        } else {
                            qApp->setStyleSheet("");
                            QPalette palette(qApp->palette());
                            palette.setColor(QPalette::Link, Qt::blue);
                            palette.setColor(QPalette::LinkVisited, Qt::magenta);
                            qApp->setPalette(palette);
                            NewDarkMode = FALSE;
                        }

                        if (NewDarkMode != OldDarkMode)
                            QTimer::singleShot(1000, [this]() {
                                BOOL DarkMode = NewDarkMode;
                                DwmSetWindowAttribute((HWND) window->winId(),
                                                      DWMWA_USE_IMMERSIVE_DARK_MODE,
                                                      (LPCVOID) &DarkMode,
                                                      sizeof(DarkMode));

                                window->resizeContents(monitors[0].mon_scrnsz_x,
                                                       monitors[0].mon_scrnsz_y);

                                for (int i = 1; i < MONITORS_NUM; i++) {
                                    auto mon = &(monitors[i]);

                                    if ((window->renderers[i] != nullptr) && !window->renderers[i]->isHidden())
                                        window->resizeContentsMonitor(mon->mon_scrnsz_x,
                                                                      mon->mon_scrnsz_y, i);
                                }

#if 0
                        if (auto_paused) {
                            plat_pause(0);
                            auto_paused = 0;
                        }
#endif
                            });
                    }
                    break;
                case WM_SYSKEYDOWN:
                    /* Stop processing of Alt-F4 */
                    if (msg->wParam == 0x73)
                        return true;
                    break;
                case WM_DEVICECHANGE:
                    if (msg->hwnd == (HWND) window->winId())
                        device_change(msg->wParam, msg->lParam);
                    break;
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
WindowsRawInputFilter::mouse_handle(RAWMOUSE *raw)
{
    RAWMOUSE   state = *raw;
    static int x, delta_x;
    static int y, delta_y;
    static int b, delta_z;
    static int delta_w;

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
        delta_z = (SHORT) state.usButtonData;
        mouse_set_z(delta_z);
    } else
        delta_z = 0;

    if (state.usButtonFlags & RI_MOUSE_HWHEEL) {
        delta_w = (SHORT) state.usButtonData;
        mouse_set_w(delta_w);
    } else
        delta_w = 0;

    if (state.usFlags & MOUSE_MOVE_ABSOLUTE) {
        /* absolute mouse, i.e. RDP or VNC
         * seems to work fine for RDP on Windows 10
         * Not sure about other environments.
         */
        delta_x = (state.lLastX - x) / 25;
        delta_y = (state.lLastY - y) / 25;
        x       = state.lLastX;
        y       = state.lLastY;
    } else {
        /* relative mouse, i.e. regular mouse */
        delta_x = state.lLastX;
        delta_y = state.lLastY;
    }

    mouse_scale(delta_x, delta_y);

    /* HWND wnd = (HWND)window->winId();

    RECT rect;

    GetWindowRect(wnd, &rect);

    int left = rect.left + (rect.right - rect.left) / 2;
    int top = rect.top + (rect.bottom - rect.top) / 2;

    SetCursorPos(left, top); */
}
