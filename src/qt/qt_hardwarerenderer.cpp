#include "qt_hardwarerenderer.hpp"
#include <QApplication>
#include <atomic>

extern "C" {
#include <86box/86box.h>
#include <86box/plat.h>
}

void HardwareRenderer::resizeGL(int w, int h)
{
    glViewport(0, 0, w, h);
}

void HardwareRenderer::initializeGL()
{
    initializeOpenGLFunctions();
}

void HardwareRenderer::paintGL() {
    onPaint(this);
}

void HardwareRenderer::setRenderType(RenderType type) {
    QSurfaceFormat format;
    switch (type) {
    case RenderType::OpenGL:
        format.setRenderableType(QSurfaceFormat::OpenGL);
        break;
    case RenderType::OpenGLES:
        format.setRenderableType(QSurfaceFormat::OpenGLES);
        break;
    }
    format.setSwapInterval(0);
    setFormat(format);
}

void HardwareRenderer::onBlit(const QImage& img, int x, int y, int w, int h, std::atomic_flag* in_use) {
    image = img;
    source.setRect(x, y, w, h);
    update();
    in_use->clear();
}

void HardwareRenderer::resizeEvent(QResizeEvent *event) {
    onResize(width(), height());
    QOpenGLWindow::resizeEvent(event);
}

bool HardwareRenderer::event(QEvent *event)
{
    switch (event->type())
    {
        default:
            return QOpenGLWindow::event(event);
        case QEvent::MouseButtonPress:
        case QEvent::MouseMove:
        case QEvent::MouseButtonRelease:
        case QEvent::KeyPress:
        case QEvent::KeyRelease:
        case QEvent::Wheel:
        case QEvent::Enter:
        case QEvent::Leave:
            return QApplication::sendEvent(parentWidget, event);
    }
    return false;
}
