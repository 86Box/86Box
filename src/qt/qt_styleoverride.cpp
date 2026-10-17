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
#include <QPainter>

extern "C" {
#include <86box/86box.h>
#include <86box/plat.h>
}

#ifdef USE_QDARKSTYLE
#    include <dwmapi.h>
#    ifndef DWMWA_USE_IMMERSIVE_DARK_MODE
#        define DWMWA_USE_IMMERSIVE_DARK_MODE 20
#    endif
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
#ifdef USE_QDARKSTYLE
        /* Newer Qt handles the title bar color by itself */
        BOOL DarkMode = !util::isWindowsLightTheme();
        DwmSetWindowAttribute((HWND) widget->winId(), DWMWA_USE_IMMERSIVE_DARK_MODE, (LPCVOID) &DarkMode, sizeof(DarkMode));
#endif
    }

    if (qobject_cast<QComboBox *>(widget)) {
        qobject_cast<QComboBox *>(widget)->view()->setMinimumWidth(widget->minimumSizeHint().width());
    }
}

bool
StyleOverride::isWindows11Style() const
{
#if QT_VERSION >= QT_VERSION_CHECK(6, 7, 0)
    return !baseStyle()->name().compare("windows11", Qt::CaseInsensitive);
#else
    return false;
#endif
}

void
StyleOverride::drawControl(ControlElement element, const QStyleOption *option, QPainter *painter, const QWidget *widget) const
{
    /* The windows11 style draws menu bar items with a hardcoded 10pt font,
       so mark them for drawItemText() to bring the text back to 9pt. */
    if ((element == CE_MenuBarItem) && isWindows11Style()) {
        drawingMenuBarItem = true;
        QProxyStyle::drawControl(element, option, painter, widget);
        drawingMenuBarItem = false;
        return;
    }

    QProxyStyle::drawControl(element, option, painter, widget);
}

QSize
StyleOverride::sizeFromContents(ContentsType type, const QStyleOption *option, const QSize &size, const QWidget *widget) const
{
    QSize ret = QProxyStyle::sizeFromContents(type, option, size, widget);

    /* The windows11 style pads menu bar items by 17px on each side and makes
       them 32px tall, tighten that up */
    if ((type == CT_MenuBarItem) && option && !ret.isEmpty() && isWindows11Style()) {
        ret.rwidth() -= 16;
        ret.setHeight(option->fontMetrics.height() + 8);
    }

    return ret;
}

void
StyleOverride::drawItemText(QPainter *painter, const QRect &rect, int flags, const QPalette &pal, bool enabled,
                            const QString &text, QPalette::ColorRole textRole) const
{
    if (drawingMenuBarItem) {
        QFont font = painter->font();
        font.setPointSize(9);

        painter->save();
        painter->setFont(font);
        QProxyStyle::drawItemText(painter, rect, flags, pal, enabled, text, textRole);
        painter->restore();
        return;
    }

    QProxyStyle::drawItemText(painter, rect, flags, pal, enabled, text, textRole);
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
