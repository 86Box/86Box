/*
 * 86Box A hypervisor and IBM PC system emulator that specializes in
 *      running old operating systems and software designed for IBM
 *      PC systems and compatibles from 1981 through fairly recent
 *      system designs based on the PCI bus.
 *
 *      This file is part of the 86Box distribution.
 *
 *      Header file for Windows VM-managers native messages filter
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

#ifndef QT_WINDOWSMANAGERFILTER_HPP
#define QT_WINDOWSMANAGERFILTER_HPP

#include <QObject>
#include <QAbstractNativeEventFilter>
#include <QByteArray>
#include <QEvent>

#if QT_VERSION_MAJOR >= 6
#define result_t qintptr
#else 
#define result_t long
#endif

/*
 * Filters native events for messages from VM-manager and
 * window blocked events to notify about open modal dialogs.
 */
class WindowsManagerFilter : public QObject, public QAbstractNativeEventFilter
{
    Q_OBJECT

public:
    bool nativeEventFilter(const QByteArray &eventType, void *message, result_t *result) override;

signals:
    void pause();
    void ctrlaltdel();
    void showsettings();
    void reset();
    void shutdown();
    void dialogstatus(bool open);

protected:
    bool eventFilter(QObject *obj, QEvent *event) override;
};

#endif
