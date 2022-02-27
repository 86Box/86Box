/*
 * 86Box A hypervisor and IBM PC system emulator that specializes in
 *      running old operating systems and software designed for IBM
 *      PC systems and compatibles from 1981 through fairly recent
 *      system designs based on the PCI bus.
 *
 *      This file is part of the 86Box distribution.
 *
 *      Header file for Unix VM-managers (client-side)
 *
 * Authors:
 *      Teemu Korhonen
	Cacodemon345
 *
 *      Copyright 2022 Teemu Korhonen
 *      Copyright 2022 Cacodemon345
 */

#ifndef QT_UNIXMANAGERFILTER_HPP
#define QT_UNIXMANAGERFILTER_HPP

#include <QObject>
#include <QLocalSocket>
#include <QEvent>

/*
 * Filters messages from VM-manager and
 * window blocked events to notify about open modal dialogs.
 */
class UnixManagerSocket : public QLocalSocket
{
    Q_OBJECT
public:
    UnixManagerSocket(QObject* object = nullptr);
signals:
    void pause();
    void ctrlaltdel();
    void showsettings();
    void resetVM();
    void request_shutdown();
    void force_shutdown();
    void dialogstatus(bool open);

protected:
    bool eventFilter(QObject *obj, QEvent *event) override;
protected slots:
    void readyToRead();
};

#endif
