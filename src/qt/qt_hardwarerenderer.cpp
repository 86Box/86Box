#include "qt_hardwarerenderer.hpp"
#include <QApplication>
#include <QVector2D>
#include <atomic>

extern "C" {
#include <86box/86box.h>
#include <86box/plat.h>
#include <86box/video.h>
}

void HardwareRenderer::resizeGL(int w, int h)
{
    glViewport(0, 0, w, h);
}

#define PROGRAM_VERTEX_ATTRIBUTE 0
#define PROGRAM_TEXCOORD_ATTRIBUTE 1

void HardwareRenderer::initializeGL()
{
    m_context->makeCurrent(this);
    initializeOpenGLFunctions();
    m_texture = new QOpenGLTexture(image);
    m_blt = new QOpenGLTextureBlitter;
    m_blt->setRedBlueSwizzle(true);
    m_blt->create();
    QOpenGLShader *vshader = new QOpenGLShader(QOpenGLShader::Vertex, this);
    const char *vsrc =
        "attribute highp vec4 vertex;\n"
        "attribute mediump vec4 texCoord;\n"
        "varying mediump vec4 texc;\n"
        "uniform mediump mat4 matrix;\n"
        "void main(void)\n"
        "{\n"
        "    gl_Position = matrix * vertex;\n"
        "    texc = texCoord;\n"
        "}\n";
    vshader->compileSourceCode(vsrc);

    QOpenGLShader *fshader = new QOpenGLShader(QOpenGLShader::Fragment, this);
    const char *fsrc =
        "uniform sampler2D texture;\n"
        "varying mediump vec4 texc;\n"
        "void main(void)\n"
        "{\n"
        "    gl_FragColor = texture2D(texture, texc.st).bgra;\n"
        "}\n";
    fshader->compileSourceCode(fsrc);

    m_prog = new QOpenGLShaderProgram;
    m_prog->addShader(vshader);
    m_prog->addShader(fshader);
    m_prog->bindAttributeLocation("vertex", PROGRAM_VERTEX_ATTRIBUTE);
    m_prog->bindAttributeLocation("texCoord", PROGRAM_TEXCOORD_ATTRIBUTE);
    m_prog->link();

    m_prog->bind();
    m_prog->setUniformValue("texture", 0);

    pclog("OpenGL vendor: %s\n", glGetString(GL_VENDOR));
    pclog("OpenGL renderer: %s\n", glGetString(GL_RENDERER));
    pclog("OpenGL version: %s\n", glGetString(GL_VERSION));
    pclog("OpenGL shader language version: %s\n", glGetString(GL_SHADING_LANGUAGE_VERSION));
}

void HardwareRenderer::paintGL() {
    m_context->makeCurrent(this);
    QVector<QVector2D> verts, texcoords;
    QMatrix4x4 mat;
    mat.setToIdentity();
    mat.ortho(QRect(0, 0, width(), height()));
    verts.push_back(QVector2D((float)destination.x(), (float)destination.y()));
    verts.push_back(QVector2D((float)destination.x(), (float)destination.y() + destination.height()));
    verts.push_back(QVector2D((float)destination.x() + destination.width(), (float)destination.y() + destination.height()));
    verts.push_back(QVector2D((float)destination.x() + destination.width(), (float)destination.y()));
    texcoords.push_back(QVector2D((float)source.x() / 2048.f, (float)(source.y()) / 2048.f));
    texcoords.push_back(QVector2D((float)source.x() / 2048.f, (float)(source.y() + source.height()) / 2048.f));
    texcoords.push_back(QVector2D((float)(source.x() + source.width()) / 2048.f, (float)(source.y() + source.height()) / 2048.f));
    texcoords.push_back(QVector2D((float)(source.x() + source.width()) / 2048.f, (float)(source.y()) / 2048.f));

    m_prog->setUniformValue("matrix", mat);
    m_prog->enableAttributeArray(PROGRAM_VERTEX_ATTRIBUTE);
    m_prog->enableAttributeArray(PROGRAM_TEXCOORD_ATTRIBUTE);
    m_prog->setAttributeArray(PROGRAM_VERTEX_ATTRIBUTE, verts.data());
    m_prog->setAttributeArray(PROGRAM_TEXCOORD_ATTRIBUTE, texcoords.data());
    m_texture->bind();
    m_texture->setMinMagFilters(video_filter_method ? QOpenGLTexture::Linear : QOpenGLTexture::Nearest, video_filter_method ? QOpenGLTexture::Linear : QOpenGLTexture::Nearest);
    glDrawArrays(GL_TRIANGLE_FAN, 0, 4);
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
