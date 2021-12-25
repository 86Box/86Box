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
    m_context->makeCurrent(this);
    initializeOpenGLFunctions();
    m_texture = new QOpenGLTexture(image);
    m_blt = new QOpenGLTextureBlitter;
    m_blt->setRedBlueSwizzle(true);
    m_blt->create();
}

void HardwareRenderer::paintGL() {
    m_context->makeCurrent(this);
    m_blt->bind();
    QMatrix4x4 target = QOpenGLTextureBlitter::targetTransform(QRect(0, 0, 2048, 2048), source);
    m_blt->blit(m_texture->textureId(), target, QOpenGLTextureBlitter::Origin::OriginTopLeft);
    m_blt->release();
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

void HardwareRenderer::onBlit(const std::unique_ptr<uint8_t>* img, int x, int y, int w, int h, std::atomic_flag* in_use) {
    auto tval = this;
    void* nuldata = 0;
    if (memcmp(&tval, &nuldata, sizeof(void*)) == 0) return;
    m_context->makeCurrent(this);
    m_texture->setData(QOpenGLTexture::PixelFormat::RGBA, QOpenGLTexture::PixelType::UInt8, (const void*)img->get());
    in_use->clear();
    source.setRect(x, y, w, h);
    update();
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
