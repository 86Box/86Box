/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          Generic Windows native event filter for dark mode handling
 *
 * Authors: Teemu Korhonen
 *          Cacodemon345
 *
 *          Copyright 2021 Teemu Korhonen
 *          Copyright 2024-2025 Cacodemon345.
 */
#include "qt_vmmanager_windarkmodefilter.hpp"

#include <QDebug>
#include <QTextStream>
#include <QFile>
#include <QApplication>
#include <QTimer>

#include <windows.h>
#include <dwmapi.h>
#ifndef DWMWA_USE_IMMERSIVE_DARK_MODE
#    define DWMWA_USE_IMMERSIVE_DARK_MODE 20
#endif

#include <86box/86box.h>
#include <86box/plat.h>

#include "qt_util.hpp"

static bool NewDarkMode = FALSE;

void
WindowsDarkModeFilter::reselectDarkMode()
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
        window->resize(window->size());

        NewDarkMode = TRUE;
    } else {
        qApp->setStyleSheet("");
        QPalette palette(qApp->palette());
        palette.setColor(QPalette::Link, Qt::blue);
        palette.setColor(QPalette::LinkVisited, Qt::magenta);
        qApp->setPalette(palette);
        window->resize(window->size());
        NewDarkMode = FALSE;
    }
    window->updateDarkMode();

    if (NewDarkMode != OldDarkMode)
        QTimer::singleShot(1000, [this]() {
            BOOL DarkMode = NewDarkMode;
            DwmSetWindowAttribute((HWND) window->winId(),
                                  DWMWA_USE_IMMERSIVE_DARK_MODE,
                                  (LPCVOID) &DarkMode,
                                  sizeof(DarkMode));
        });
}

void
WindowsDarkModeFilter::setWindow(VMManagerMainWindow *window)
{
    this->window = window;
}

bool
WindowsDarkModeFilter::nativeEventFilter(const QByteArray &eventType, void *message, result_t *result)
{
    if ((window != nullptr) && (eventType == "windows_generic_MSG")) {
        MSG *msg = static_cast<MSG *>(message);

        if ((msg != nullptr) && (msg->message == WM_SETTINGCHANGE)) {
            if ((((void *) msg->lParam) != nullptr) && (wcscmp(L"ImmersiveColorSet", (wchar_t *) msg->lParam) == 0) && color_scheme == 0) 
                reselectDarkMode();
        }
    }

    return false;
}
