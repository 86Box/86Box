#include "qt_hardwarerenderer.hpp"
#include <QApplication>
#include <atomic>

extern "C" {
#include <86box/86box.h>
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
    //onPaint(this);
}

void HardwareRenderer::paintUnderGL() {
    glClearColor(0.f, 0.f, 0.f, 1.f);
    glClear(GL_COLOR_BUFFER_BIT);
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
