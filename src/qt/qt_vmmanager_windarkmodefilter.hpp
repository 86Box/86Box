/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          Header file for Windows dark mode native messages filter
 *
 *
 *
 * Authors: Teemu Korhonen
 *
 *          Copyright 2022 Teemu Korhonen
 */

#ifndef QT_WINDOWSDARKMODEEVENTFILTER_HPP
#define QT_WINDOWSDARKMODEEVENTFILTER_HPP

#include <QObject>
#include <QAbstractNativeEventFilter>
#include <QByteArray>
#include <QEvent>
#include <QWindow>

#include "qt_vmmanager_mainwindow.hpp"

#if QT_VERSION_MAJOR >= 6
#    define result_t qintptr
#else
#    define result_t long
#endif

class WindowsDarkModeFilter : public QObject, public QAbstractNativeEventFilter {
    Q_OBJECT

public:
    WindowsDarkModeFilter() = default;
    void setWindow(VMManagerMainWindow *window);
    bool nativeEventFilter(const QByteArray &eventType, void *message, result_t *result) override;
    void reselectDarkMode();

private:
    VMManagerMainWindow *window;
};

#endif // QT_WINDOWSDARKMODEEVENTFILTER_HPP
