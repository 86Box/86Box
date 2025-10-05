/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          Style override class.
 *
 * Authors: Teemu Korhonen
 *
 *          Copyright 2022 Teemu Korhonen
 */
#include "qt_styleoverride.hpp"
#include "qt_util.hpp"

#include <QComboBox>
#include <QAbstractItemView>
#include <QPixmap>
#include <QIcon>
#include <QStyleOption>
#include <QMainWindow>

extern "C" {
#include <86box/86box.h>
#include <86box/plat.h>
}

#ifdef Q_OS_WINDOWS
#include <dwmapi.h>
#ifndef DWMWA_USE_IMMERSIVE_DARK_MODE
#define DWMWA_USE_IMMERSIVE_DARK_MODE 20
#endif
#endif

int
StyleOverride::styleHint(
    StyleHint           hint,
    const QStyleOption *option,
    const QWidget      *widget,
    QStyleHintReturn   *returnData) const
{
    /* Disable using menu with alt key */
    if (!start_vmm && (!kbd_req_capture || mouse_capture) && (hint == QStyle::SH_MenuBar_AltKeyNavigation))
        return 0;

    return QProxyStyle::styleHint(hint, option, widget, returnData);
}

void
StyleOverride::polish(QWidget *widget)
{
    QProxyStyle::polish(widget);
    /* Disable title bar context help buttons globally as they are unused. */
    if (widget->isWindow()) {
        if (widget->layout() && widget->minimumSize() == widget->maximumSize()) {
            if (widget->minimumSize().width() < widget->minimumSizeHint().width()
                || widget->minimumSize().height() < widget->minimumSizeHint().height()) {
                widget->setFixedSize(QSize(QWIDGETSIZE_MAX, QWIDGETSIZE_MAX));
                widget->layout()->setSizeConstraint(QLayout::SetFixedSize);
            }
            if (!qobject_cast<QMainWindow *>(widget)) {
                widget->setWindowFlag(Qt::MSWindowsFixedSizeDialogHint, true);
            }

            if (qobject_cast<QMainWindow *>(widget)) {
                widget->setWindowFlag(Qt::MSWindowsFixedSizeDialogHint, vid_resize != 1);
                widget->setWindowFlag(Qt::WindowMaximizeButtonHint, vid_resize == 1);
            }
        }
        widget->setWindowFlag(Qt::WindowContextHelpButtonHint, false);
#ifdef Q_OS_WINDOWS
        BOOL DarkMode = !util::isWindowsLightTheme();
        DwmSetWindowAttribute((HWND)widget->winId(), DWMWA_USE_IMMERSIVE_DARK_MODE, (LPCVOID)&DarkMode, sizeof(DarkMode));
#endif
    }

    if (qobject_cast<QComboBox *>(widget)) {
        qobject_cast<QComboBox *>(widget)->view()->setMinimumWidth(widget->minimumSizeHint().width());
    }
}

QPixmap
StyleOverride::generatedIconPixmap(QIcon::Mode iconMode, const QPixmap &pixmap, const QStyleOption *option) const
{
    if (iconMode != QIcon::Disabled) {
        return QProxyStyle::generatedIconPixmap(iconMode, pixmap, option);
    }

    auto image = pixmap.toImage();

    for (int y = 0; y < image.height(); y++) {
        for (int x = 0; x < image.width(); x++) {
            // checkerboard transparency
            if (((x ^ y) & 1) == 0) {
                image.setPixelColor(x, y, Qt::transparent);
                continue;
            }

            auto color = image.pixelColor(x, y);

            // convert to grayscale using the NTSC formula
            auto avg = 0.0;
            avg += color.blueF() * 0.114;
            avg += color.greenF() * 0.587;
            avg += color.redF() * 0.299;

            color.setRedF(avg);
            color.setGreenF(avg);
            color.setBlueF(avg);

            image.setPixelColor(x, y, color);

        }
    }

    return QPixmap::fromImage(image);
}
