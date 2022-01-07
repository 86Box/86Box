#include "qt_renderercomon.hpp"
#include "qt_mainwindow.hpp"

#include <QPainter>
#include <QWidget>
#include <QEvent>
#include <QApplication>

#include <cmath>

extern "C" {
#include <86box/86box.h>
#include <86box/video.h>
}

RendererCommon::RendererCommon() = default;

extern MainWindow* main_window;
void RendererCommon::onPaint(QPaintDevice* device) {
    QPainter painter(device);
    painter.setRenderHint(QPainter::SmoothPixmapTransform, video_filter_method > 0 ? true : false);
    painter.fillRect(0, 0, device->width(), device->height(), QColorConstants::Black);
    painter.setCompositionMode(QPainter::CompositionMode_Plus);
    painter.drawImage(destination, image, source);
}

static void integer_scale(double *d, double *g) {
    double ratio;

    if (*d > *g) {
        ratio = std::floor(*d / *g);
        *d = *g * ratio;
    } else {
        ratio = std::ceil(*d / *g);
        *d = *g / ratio;
    }
}

void RendererCommon::onResize(int width, int height) {
    if (video_fullscreen == 0) {
        destination.setRect(0, 0, width, height);
        return;
    }
    double dx, dy, dw, dh, gsr;

    double hw = width;
    double hh = height;
    double gw = source.width();
    double gh = source.height();
    double hsr = hw / hh;

    switch (video_fullscreen_scale) {
    case FULLSCR_SCALE_INT:
        gsr = gw / gh;
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
        destination.setRect(dx, dy, dw, dh);
        break;
    case FULLSCR_SCALE_43:
    case FULLSCR_SCALE_KEEPRATIO:
        if (video_fullscreen_scale == FULLSCR_SCALE_43) {
            gsr = 4.0 / 3.0;
        } else {
            gsr = gw / gh;
        }

        if (gsr <= hsr) {
            dw = hh * gsr;
            dh = hh;
        } else {
            dw = hw;
            dh = hw / gsr;
        }
        dx = (hw - dw) / 2.0;
        dy = (hh - dh) / 2.0;
        destination.setRect(dx, dy, dw, dh);
        break;
    case FULLSCR_SCALE_FULL:
    default:
        destination.setRect(0, 0, hw, hh);
        break;
    }
}

bool RendererCommon::eventDelegate(QEvent *event, bool& result)
{
    switch (event->type())
    {
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
    return false;
}
