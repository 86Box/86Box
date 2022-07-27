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
    void request_shutdown();
    void force_shutdown();
    void dialogstatus(bool open);

protected:
    bool eventFilter(QObject *obj, QEvent *event) override;
};

#endif
