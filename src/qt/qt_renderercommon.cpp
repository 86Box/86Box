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
 * Authors: Joakim L. Gilje <jgilje@jgilje.net>
 *
 *          Copyright 2021 Joakim L. Gilje
 */
#include "qt_renderercommon.hpp"
#include "qt_mainwindow.hpp"
#include "qt_osd.hpp"
#include "osd_core.hpp"

#include <QPainter>
#include <QWidget>
#include <QEvent>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QWheelEvent>
#include <QApplication>

#include <cmath>

extern "C" {
#include <86box/86box.h>
#include <86box/plat.h>
#include <86box/video.h>
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
standalone_scale(QRect &destination, int width, int height, QRect source, int scalemode)
{
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

    switch (scalemode) {
        case FULLSCR_SCALE_INT:
        case FULLSCR_SCALE_INT43:
            gsr = gw / gh;

            if (scalemode == FULLSCR_SCALE_INT43) {
                gh = gw / r43;

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
            if (scalemode == FULLSCR_SCALE_43)
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

void
RendererCommon::onResize(int width, int height)
{
    /* The stretch mode applies in fullscreen and, if enabled, when the window
       holding this renderer (the main window or a monitor window) is maximized. */
    bool apply_scale = video_fullscreen ||
                       (video_fullscreen_scale_maximized && parentWidget->window()->isMaximized());

    width  = round(pixelRatio * width);
    height = round(pixelRatio * height);

    output_size = QSize(width, height);

    const auto &monitor = monitors[r_monitor_index];
    if (!force_43 && force_device_aspect && monitor.mon_device_aspect_x > 0 && monitor.mon_device_aspect_y > 0) {
        int dw = width;
        int dh = qRound((double) width * monitor.mon_device_aspect_y / monitor.mon_device_aspect_x);
        if (dh > height) {
            dh = height;
            dw = qRound((double) height * monitor.mon_device_aspect_x / monitor.mon_device_aspect_y);
        }
        destination.setRect((width - dw) / 2, (height - dh) / 2, dw, dh);
    } else if (!apply_scale && !(force_43 && vid_resize))
        destination.setRect(0, 0, width, height);
    else {
        auto   temp_fullscreen_scale = video_fullscreen_scale;
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

        if (force_43 && vid_resize && !apply_scale)
            temp_fullscreen_scale = FULLSCR_SCALE_43;

        switch (temp_fullscreen_scale) {
            case FULLSCR_SCALE_INT:
            case FULLSCR_SCALE_INT43:
                gsr = gw / gh;

                if (temp_fullscreen_scale == FULLSCR_SCALE_INT43) {
                    gh = gw / r43;
                    // gw = gw;

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
                if (temp_fullscreen_scale == FULLSCR_SCALE_43)
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
            case FULLSCR_SCALE_CENTER:
            case FULLSCR_SCALE_CENTER43:
                /* Like 4:3 integer scale, keep the width and make the height match it. */
                if (temp_fullscreen_scale == FULLSCR_SCALE_CENTER43)
                    gh = std::round(gw / r43);

                /* An image larger than the window extends past its edges and gets cut off. */
                dx = std::floor((hw - gw) / 2.0);
                dy = std::floor((hh - gh) / 2.0);
                destination.setRect((int) dx, (int) dy, (int) gw, (int) gh);
                break;
            case FULLSCR_SCALE_FULL:
            default:
                destination.setRect(0, 0, (int) hw, (int) hh);
                break;
        }
    }

    if (destination.width() == 0) destination.setWidth(256);
    if (destination.height() == 0) destination.setHeight(256);

    monitors[r_monitor_index].mon_res_x = (double) destination.width();
    monitors[r_monitor_index].mon_res_y = (double) destination.height();

    destinationF.setRect((double) destination.x() / (double) width, (double) destination.y() / (double) height,
                         (double) destination.width() / (double) width, (double) destination.height() / (double) height);
}

void
RendererCommon::visibleRects(int out_w, int out_h, QRect &visible_source, QRect &visible_destination) const
{
    visible_destination = destination.intersected(QRect(0, 0, out_w, out_h));

    if (visible_destination == destination) {
        visible_source = source;
        return;
    }

    if (visible_destination.isEmpty() || destination.isEmpty()) {
        visible_source = QRect(source.x(), source.y(), 0, 0);
        return;
    }

    /* Crop the source by the same fraction the destination was cut by. */
    const double sx = (double) source.width() / (double) destination.width();
    const double sy = (double) source.height() / (double) destination.height();

    const int x0 = source.x() + (int) std::lround((visible_destination.left() - destination.left()) * sx);
    const int y0 = source.y() + (int) std::lround((visible_destination.top() - destination.top()) * sy);
    const int x1 = source.x() + (int) std::lround((visible_destination.left() + visible_destination.width() - destination.left()) * sx);
    const int y1 = source.y() + (int) std::lround((visible_destination.top() + visible_destination.height() - destination.top()) * sy);

    visible_source.setRect(x0, y0, x1 - x0, y1 - y0);
}

float
RendererCommon::osdLayoutScaleHint() const
{
    const double dpr = std::max(1.0, pixelRatio);
    const QRect  visible   = destination.intersected(QRect(QPoint(0, 0), output_size));
    const int    logical_w = std::max(1, (int) std::lround((double) visible.width() / dpr));
    const int    logical_h = std::max(1, (int) std::lround((double) visible.height() / dpr));
    return osd_core_layout_scale_for_output(logical_w, logical_h);
}

bool
RendererCommon::eventDelegate(QEvent *event, bool &result)
{
    switch (event->type()) {
        default:
            return false;
        case QEvent::KeyPress:
        case QEvent::KeyRelease:
            /* Keyboard for the OSD is intercepted centrally in
             * MainWindow::eventFilter (the render window has no focus), so here
             * we only forward to the machine as usual. */
            result = QApplication::sendEvent(main_window, event);
            return true;
        case QEvent::MouseButtonPress:
        case QEvent::MouseMove:
        case QEvent::MouseButtonRelease:
            if (qt_osd_is_visible()) {
                auto *me = static_cast<QMouseEvent *>(event);
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
                qt_osd_mouse_pos((float) me->position().x(), (float) me->position().y());
#else
                qt_osd_mouse_pos((float) me->x(), (float) me->y());
#endif
                if (event->type() == QEvent::MouseButtonPress)
                    qt_osd_mouse_button(me->button(), true);
                else if (event->type() == QEvent::MouseButtonRelease)
                    qt_osd_mouse_button(me->button(), false);
                result = true;
                return true;
            }
            result = QApplication::sendEvent(parentWidget, event);
            return true;
        case QEvent::Wheel:
            if (qt_osd_is_visible()) {
                auto *we = static_cast<QWheelEvent *>(event);
                qt_osd_mouse_wheel((float) we->angleDelta().x() / 120.0f,
                                   (float) we->angleDelta().y() / 120.0f);
                result = true;
                return true;
            }
            result = QApplication::sendEvent(parentWidget, event);
            return true;
#ifdef TOUCH_PR
        case QEvent::TouchBegin:
        case QEvent::TouchEnd:
        case QEvent::TouchCancel:
        case QEvent::TouchUpdate:
#endif
        case QEvent::Enter:
        case QEvent::Leave:
            result = QApplication::sendEvent(parentWidget, event);
            return true;
    }
}
