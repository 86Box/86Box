/*
 * 86Box A hypervisor and IBM PC system emulator that specializes in
 *      running old operating systems and software designed for IBM
 *      PC systems and compatibles from 1981 through fairly recent
 *      system designs based on the PCI bus.
 *
 *      This file is part of the 86Box distribution.
 *
 *      Windows VM-managers native messages filter
 * 
 * Authors:
 *      Teemu Korhonen
 * 
 *      Copyright 2022 Teemu Korhonen
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

#include "qt_winmanagerfilter.hpp"

#include <Windows.h>
#include <86box/win.h>

bool WindowsManagerFilter::nativeEventFilter(const QByteArray &eventType, void *message, long *result)
{
    if (eventType == "windows_generic_MSG")
    {
        MSG *msg = static_cast<MSG *>(message);

        switch (msg->message)
        {
            case WM_SHOWSETTINGS:
                emit showsettings();
                return true;
            case WM_PAUSE:
                emit pause();
                return true;
            case WM_HARDRESET:
                emit reset();
                return true;
            case WM_SHUTDOWN:
                emit shutdown();
                return true;
            case WM_CTRLALTDEL:
                emit ctrlaltdel();
                return true;
        }
    }

    return false;
}

bool WindowsManagerFilter::eventFilter(QObject *obj, QEvent *event)
{
    if (event->type() == QEvent::WindowBlocked)
    {
        emit dialogstatus(1);
    }
    else if (event->type() == QEvent::WindowUnblocked)
    {
        emit dialogstatus(0);
    }

    return QObject::eventFilter(obj, event);
}