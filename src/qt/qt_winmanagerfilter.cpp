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
 */

#include "qt_winmanagerfilter.hpp"

#include <Windows.h>
#include <86box/win.h>

bool WindowsManagerFilter::nativeEventFilter(const QByteArray &eventType, void *message, result_t *result)
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
                if (msg->wParam == 1)
                    emit force_shutdown();
                else
                    emit request_shutdown();
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
