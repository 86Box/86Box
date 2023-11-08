/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          Program settings UI module.
 *
 *
 *
 * Authors: Joakim L. Gilje <jgilje@jgilje.net>
 *
 *          Copyright 2021 Joakim L. Gilje
 */

#include "qt_renderercommon.hpp"
#include "qt_mainwindow.hpp"
#include "qt_machinestatus.hpp"

#include <QPainter>
#include <QWidget>
#include <QEvent>
#include <QApplication>
#include <QFontMetrics>
#include <QStatusBar>
#include <QLayout>

#include <cmath>

extern "C" {
#include <86box/86box.h>
#include <86box/plat.h>
#include <86box/video.h>

int status_icons_fullscreen = 0;
}

RendererCommon::RendererCommon() = default;

extern MainWindow *main_window;

static void
integer_scale(double *d, double *g)
{
    double ratio;

    if (*d > *g) {
        ratio = std::floor(*d / *g);
        *d    = *g * ratio;
    } else {
        ratio = std::ceil(*d / *g);
        *d    = *g / ratio;
    }
}

void
RendererCommon::onResize(int width, int height)
{
    /* This is needed so that the if below does not take like, 5 lines. */
    bool is_fs = (video_fullscreen == 0);
    bool parent_max = (parentWidget->isMaximized() == false);
    bool main_is_ancestor = main_window->isAncestorOf(parentWidget);
    bool main_max = main_window->isMaximized();
    bool main_is_max = (main_is_ancestor && main_max == false);

    if (is_fs && (video_fullscreen_scale_maximized ? (parent_max && main_is_max) : 1))
        destination.setRect(0, 0, width, height);
    else {
        double dx;
        double dy;
        double dw;
        double dh;
        double gsr;

        double hw  = width;
        double hh  = height;
        double gw  = source.width();
        double gh  = source.height();
        double hsr = hw / hh;
        double r43 = 4.0 / 3.0;

        switch (video_fullscreen_scale) {
            case FULLSCR_SCALE_INT:
            case FULLSCR_SCALE_INT43:
                gsr = gw / gh;

                if (video_fullscreen_scale == FULLSCR_SCALE_INT43) {
                    gh = gw / r43;
                    gw = gw;

                    gsr = r43;
                }

                if (gsr <= hsr) {
                    dw = hh * gsr;
                    dh = hh;
                } else {
                    dw = hw;
                    dh = hw / gsr;
                }

                integer_scale(&dw, &gw);
                integer_scale(&dh, &gh);

                dx = (hw - dw) / 2.0;
                dy = (hh - dh) / 2.0;
                destination.setRect((int) dx, (int) dy, (int) dw, (int) dh);
                break;
            case FULLSCR_SCALE_43:
            case FULLSCR_SCALE_KEEPRATIO:
                if (video_fullscreen_scale == FULLSCR_SCALE_43)
                    gsr = r43;
                else
                    gsr = gw / gh;

                if (gsr <= hsr) {
                    dw = hh * gsr;
                    dh = hh;
                } else {
                    dw = hw;
                    dh = hw / gsr;
                }
                dx = (hw - dw) / 2.0;
                dy = (hh - dh) / 2.0;
                destination.setRect((int) dx, (int) dy, (int) dw, (int) dh);
                break;
            case FULLSCR_SCALE_FULL:
            default:
                destination.setRect(0, 0, (int) hw, (int) hh);
                break;
        }
    }

    monitors[r_monitor_index].mon_res_x = (double) destination.width();
    monitors[r_monitor_index].mon_res_y = (double) destination.height();
}

void RendererCommon::drawStatusBarIcons(QPainter* painter)
{
    uint32_t x = 0;
    auto prevcompositionMode = painter->compositionMode();
    painter->setCompositionMode(QPainter::CompositionMode::CompositionMode_SourceOver);
    for (int i = 0; i < main_window->statusBar()->children().count(); i++) {
        QLabel* label = qobject_cast<QLabel*>(main_window->statusBar()->children()[i]);
        if (label) {
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
            const QPixmap pixmap = label->pixmap();
#else
            const QPixmap pixmap = label->pixmap() ? *label->pixmap() : QPixmap();
#endif
            if (!pixmap.isNull()) {
                painter->setBrush(QColor::fromRgbF(0, 0, 0, 1.));
                painter->fillRect(x, painter->device()->height() - pixmap.height() - 5,
                                  pixmap.width(), pixmap.height() + 5, QColor::fromRgbF(0, 0, 0, .5));
                painter->drawPixmap(x + main_window->statusBar()->layout()->spacing() / 2,
                                    painter->device()->height() - pixmap.height() - 3, pixmap);
                x += pixmap.width();
                if (i <= main_window->statusBar()->children().count() - 3) {
                    painter->fillRect(x, painter->device()->height() - pixmap.height() - 5,
                                      main_window->statusBar()->layout()->spacing(), pixmap.height() + 5,
                                      QColor::fromRgbF(0, 0, 0, .5));
                    x += main_window->statusBar()->layout()->spacing();
                } else
                    painter->fillRect(x, painter->device()->height() - pixmap.height() - 4, 4,
                                      pixmap.height() + 4, QColor::fromRgbF(0, 0, 0, .5));
            }
        }
    }
    if (main_window->status->getMessage().isEmpty() == false) {
        auto curStatusMsg = main_window->status->getMessage();
        auto textSize = painter->fontMetrics().size(Qt::TextSingleLine, QChar(' ') + curStatusMsg + QChar(' '));
        painter->setPen(QColor(0, 0, 0, 127));
        painter->fillRect(painter->device()->width() - textSize.width(), painter->device()->height() - textSize.height(),
                          textSize.width(), textSize.height(), QColor(0, 0, 0, 127));
        painter->setPen(QColor(255, 255, 255, 255));
        painter->drawText(QRectF(painter->device()->width() - textSize.width(), painter->device()->height() - textSize.height(),
                                 textSize.width(), textSize.height()), Qt::TextSingleLine, QChar(' ') + curStatusMsg + QChar(' '));
    }
    painter->setCompositionMode(prevcompositionMode);
}

bool
RendererCommon::eventDelegate(QEvent *event, bool &result)
{
    switch (event->type()) {
        default:
            return false;
        case QEvent::KeyPress:
        case QEvent::KeyRelease:
            result = QApplication::sendEvent(main_window, event);
            return true;
        case QEvent::MouseButtonPress:
        case QEvent::MouseMove:
        case QEvent::MouseButtonRelease:
        case QEvent::Wheel:
        case QEvent::Enter:
        case QEvent::Leave:
            result = QApplication::sendEvent(parentWidget, event);
            return true;
    }
}
