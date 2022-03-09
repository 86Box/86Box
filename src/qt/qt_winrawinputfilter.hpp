/*
 * 86Box A hypervisor and IBM PC system emulator that specializes in
 *      running old operating systems and software designed for IBM
 *      PC systems and compatibles from 1981 through fairly recent
 *      system designs based on the PCI bus.
 *
 *      This file is part of the 86Box distribution.
 *
 *      Header file for windows raw input native filter for QT
 *
 * Authors:
 *      Teemu Korhonen
 *
 *      Copyright 2021 Teemu Korhonen
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

#ifndef QT_WINDOWSRAWINPUTFILTER_HPP
#define QT_WINDOWSRAWINPUTFILTER_HPP

#include <QObject>
#include <QMainWindow>
#include <QAbstractNativeEventFilter>
#include <QByteArray>

#include <Windows.h>

#include <memory>

#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
#define result_t qintptr
#else
#define result_t long
#endif

class WindowsRawInputFilter : public QObject, public QAbstractNativeEventFilter
{
    Q_OBJECT

public:
    static std::unique_ptr<WindowsRawInputFilter> Register(QMainWindow *window);

    bool nativeEventFilter(const QByteArray &eventType, void *message, result_t *result) override;

    ~WindowsRawInputFilter();

public slots:
    void mousePoll();

private:
    QMainWindow *window;
    uint16_t scancode_map[768];
    int buttons = 0;
	int dx = 0;
	int dy = 0;
	int dwheel = 0;
    int menus_open = 0;

    WindowsRawInputFilter(QMainWindow *window);

    void handle_input(HRAWINPUT input);
    void keyboard_handle(PRAWINPUT raw);
    void mouse_handle(PRAWINPUT raw);
    static UINT16 convert_scan_code(UINT16 scan_code);
    void keyboard_getkeymap();
};

#endif
