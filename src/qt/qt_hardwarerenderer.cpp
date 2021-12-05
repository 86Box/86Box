#include "qt_hardwarerenderer.hpp"

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

void HardwareRenderer::paintGL()
{
    QPainter painter(this);
    painter.setRenderHint(QPainter::SmoothPixmapTransform, video_filter_method > 0 ? true : false);
    painter.drawImage(QRect(0, 0, width(), height()), image, QRect(sx, sy, sw, sh));
    // "release" image, reducing it's refcount, so renderstack::blit()
    // won't have to reallocate
    image = QImage();
}

void HardwareRenderer::setRenderType(RenderType type) {
    QSurfaceFormat format;
    switch (type) {
    case RenderType::OpenGL:
        setTextureFormat(GL_RGB);
        format.setRenderableType(QSurfaceFormat::OpenGL);
        break;
    case RenderType::OpenGLES:
        setTextureFormat(GL_RGBA);
        format.setRenderableType(QSurfaceFormat::OpenGLES);
        break;
    }
    setFormat(format);
}

void HardwareRenderer::onBlit(const QImage& img, int x, int y, int w, int h) {
    image = img;
    sx = x;
    sy = y;
    sw = w;
    sh = h;
    update();
}
