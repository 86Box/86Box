/*
 * 86Box	A hypervisor and IBM PC system emulator that specializes in
 *		running old operating systems and software designed for IBM
 *		PC systems and compatibles from 1981 through fairly recent
 *		system designs based on the PCI bus.
 *
 *		This file is part of the 86Box distribution.
 *
 *		Utility functions.
 *
 *
 *
 * Authors:	Teemu Korhonen
 *
 *		Copyright 2022 Teemu Korhonen
 */
#include <QStringBuilder>
#include <QStringList>
#include <QWidget>
#include <QApplication>
#if QT_VERSION <= QT_VERSION_CHECK(5, 14, 0)
#include <QDesktopWidget>
#endif
#include "qt_util.hpp"

namespace util
{
    QScreen* screenOfWidget(QWidget* widget)
    {
#if QT_VERSION <= QT_VERSION_CHECK(5, 14, 0)
        return QApplication::screens()[QApplication::desktop()->screenNumber(widget) == -1 ? 0 : QApplication::desktop()->screenNumber(widget)];
#else
        return widget->screen();
#endif
    }

    QString DlgFilter(std::initializer_list<QString> extensions, bool last)
    {
        QStringList temp;

        for (auto ext : extensions)
        {
#ifdef Q_OS_UNIX
            if (ext == "*")
            {
                temp.append("*");
                continue;
            }
            temp.append("*." % ext.toUpper());
#endif
            temp.append("*." % ext);
        }

#ifdef Q_OS_UNIX
        temp.removeDuplicates();
#endif
        return " (" % temp.join(' ') % ")" % (!last ? ";;" : "");
    }

}
