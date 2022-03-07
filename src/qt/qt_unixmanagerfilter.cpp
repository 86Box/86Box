/*
 * 86Box A hypervisor and IBM PC system emulator that specializes in
 *      running old operating systems and software designed for IBM
 *      PC systems and compatibles from 1981 through fairly recent
 *      system designs based on the PCI bus.
 *
 *      This file is part of the 86Box distribution.
 *
 *      Source file for Unix VM-managers (client-side)
 *
 * Authors:
 *      Teemu Korhonen
    Cacodemon345
 *
 *      Copyright 2022 Teemu Korhonen
 *      Copyright 2022 Cacodemon345
 */

#include "qt_unixmanagerfilter.hpp"

UnixManagerSocket::UnixManagerSocket(QObject* obj)
    : QLocalSocket(obj)
{
    connect(this, &QLocalSocket::readyRead, this, &UnixManagerSocket::readyToRead);
}

void UnixManagerSocket::readyToRead()
{
    if (canReadLine())
    {
        QByteArray line = readLine();
        if (line.size())
        {
            line.resize(line.size() - 1);
            if (line == "showsettings")
            {
                emit showsettings();
            }
            else if (line == "pause")
            {
                emit pause();
            }
            else if (line == "cad")
            {
                emit ctrlaltdel();
            }
            else if (line == "reset")
            {
                emit resetVM();
            }
            else if (line == "shutdownnoprompt")
            {
                emit force_shutdown();
            }
            else if (line == "shutdown")
            {
                emit request_shutdown();
            }
        }
    }
}

bool UnixManagerSocket::eventFilter(QObject *obj, QEvent *event)
{
    if (state() == QLocalSocket::ConnectedState)
    {
        if (event->type() == QEvent::WindowBlocked)
        {
            write(QByteArray{"1"});
        }
        else if (event->type() == QEvent::WindowUnblocked)
        {
            write(QByteArray{"0"});
        }
    }

    return QObject::eventFilter(obj, event);
}
