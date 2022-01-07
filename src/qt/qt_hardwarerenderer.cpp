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
    glViewport(0, 0, w * devicePixelRatio(), h * devicePixelRatio());
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
        "attribute highp vec4 VertexCoord;\n"
        "attribute mediump vec4 TexCoord;\n"
        "varying mediump vec4 texc;\n"
        "uniform mediump mat4 MVPMatrix;\n"
        "void main(void)\n"
        "{\n"
        "    gl_Position = MVPMatrix * VertexCoord;\n"
        "    texc = TexCoord;\n"
        "}\n";
    QString vsrccore =
        "in highp vec4 VertexCoord;\n"
        "in mediump vec4 TexCoord;\n"
        "out mediump vec4 texc;\n"
        "uniform mediump mat4 MVPMatrix;\n"
        "void main(void)\n"
        "{\n"
        "    gl_Position = MVPMatrix * VertexCoord;\n"
        "    texc = TexCoord;\n"
        "}\n";
    if (m_context->isOpenGLES() && m_context->format().version() >= qMakePair(3, 0))
    {
        vsrccore.prepend("#version 300 es\n");
        vshader->compileSourceCode(vsrccore);
    }
    else if (m_context->format().version() >= qMakePair(3, 0) && m_context->format().profile() == QSurfaceFormat::CoreProfile)
    {
        vsrccore.prepend("#version 130\n");
        vshader->compileSourceCode(vsrccore);
    }
    else vshader->compileSourceCode(vsrc);

    QOpenGLShader *fshader = new QOpenGLShader(QOpenGLShader::Fragment, this);
    const char *fsrc =
        "uniform sampler2D texture;\n"
        "varying mediump vec4 texc;\n"
        "void main(void)\n"
        "{\n"
        "    gl_FragColor = texture2D(texture, texc.st).bgra;\n"
        "}\n";
    QString fsrccore =
        "uniform sampler2D texture;\n"
        "in mediump vec4 texc;\n"
        "out highp vec4 FragColor;\n"
        "void main(void)\n"
        "{\n"
        "    FragColor = texture2D(texture, texc.st).bgra;\n"
        "}\n";
    if (m_context->isOpenGLES() && m_context->format().version() >= qMakePair(3, 0))
    {
        fsrccore.prepend("#version 300 es\n");
        fshader->compileSourceCode(fsrccore);
    }
    else if (m_context->format().version() >= qMakePair(3, 0) && m_context->format().profile() == QSurfaceFormat::CoreProfile)
    {
        fsrccore.prepend("#version 130\n");
        fshader->compileSourceCode(fsrccore);
    }
    else fshader->compileSourceCode(fsrc);

    m_prog = new QOpenGLShaderProgram;
    m_prog->addShader(vshader);
    m_prog->addShader(fshader);
    m_prog->bindAttributeLocation("VertexCoord", PROGRAM_VERTEX_ATTRIBUTE);
    m_prog->bindAttributeLocation("TexCoord", PROGRAM_TEXCOORD_ATTRIBUTE);
    m_prog->link();

    m_prog->bind();
    m_prog->setUniformValue("texture", 0);

    if (m_context->format().version() >= qMakePair(3, 0) && m_vao.create()) {
        m_vao.bind();
    }

    m_vbo[PROGRAM_VERTEX_ATTRIBUTE].create();
    m_vbo[PROGRAM_VERTEX_ATTRIBUTE].bind();
    m_vbo[PROGRAM_VERTEX_ATTRIBUTE].allocate(sizeof(QVector2D) * 4);
    m_vbo[PROGRAM_TEXCOORD_ATTRIBUTE].create();
    m_vbo[PROGRAM_TEXCOORD_ATTRIBUTE].bind();
    m_vbo[PROGRAM_TEXCOORD_ATTRIBUTE].allocate(sizeof(QVector2D) * 4);

    pclog("OpenGL vendor: %s\n", glGetString(GL_VENDOR));
    pclog("OpenGL renderer: %s\n", glGetString(GL_RENDERER));
    pclog("OpenGL version: %s\n", glGetString(GL_VERSION));
    pclog("OpenGL shader language version: %s\n", glGetString(GL_SHADING_LANGUAGE_VERSION));
    glClearColor(0, 0, 0, 1);
}

void HardwareRenderer::paintGL() {
    m_context->makeCurrent(this);
    glClear(GL_COLOR_BUFFER_BIT);
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
    m_vbo[PROGRAM_VERTEX_ATTRIBUTE].bind(); m_vbo[PROGRAM_VERTEX_ATTRIBUTE].write(0, verts.data(), sizeof(QVector2D) * 4); m_vbo[PROGRAM_VERTEX_ATTRIBUTE].release();
    m_vbo[PROGRAM_TEXCOORD_ATTRIBUTE].bind(); m_vbo[PROGRAM_TEXCOORD_ATTRIBUTE].write(0, texcoords.data(), sizeof(QVector2D) * 4);  m_vbo[PROGRAM_TEXCOORD_ATTRIBUTE].release();

    m_prog->setUniformValue("MVPMatrix", mat);
    m_prog->enableAttributeArray(PROGRAM_VERTEX_ATTRIBUTE);
    m_prog->enableAttributeArray(PROGRAM_TEXCOORD_ATTRIBUTE);

    m_vbo[PROGRAM_VERTEX_ATTRIBUTE].bind(); m_prog->setAttributeBuffer(PROGRAM_VERTEX_ATTRIBUTE, GL_FLOAT, 0, 2, 0); m_vbo[PROGRAM_VERTEX_ATTRIBUTE].release();
    m_vbo[PROGRAM_TEXCOORD_ATTRIBUTE].bind(); m_prog->setAttributeBuffer(PROGRAM_TEXCOORD_ATTRIBUTE, GL_FLOAT, 0, 2, 0); m_vbo[PROGRAM_TEXCOORD_ATTRIBUTE].release();
    m_texture->bind();
    m_texture->setMinMagFilters(video_filter_method ? QOpenGLTexture::Linear : QOpenGLTexture::Nearest, video_filter_method ? QOpenGLTexture::Linear : QOpenGLTexture::Nearest);
    glDrawArrays(GL_TRIANGLE_FAN, 0, 4);
}

void HardwareRenderer::setRenderType(RenderType type) {
    QSurfaceFormat format;
    switch (type) {
    case RenderType::OpenGL3:
        format.setVersion(3, 0);
        format.setProfile(QSurfaceFormat::CoreProfile);
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
    if (!m_texture || !img || !img->get() || (m_texture && !m_texture->isCreated()))
    {
        in_use->clear();
        source.setRect(x, y, w, h);
        return;
    }
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
    bool res = false;
    if (!eventDelegate(event, res)) return QOpenGLWindow::event(event);
    return res;
}
