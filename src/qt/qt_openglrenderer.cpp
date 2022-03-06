/*
 * 86Box A hypervisor and IBM PC system emulator that specializes in
 *      running old operating systems and software designed for IBM
 *      PC systems and compatibles from 1981 through fairly recent
 *      system designs based on the PCI bus.
 *
 *      This file is part of the 86Box distribution.
 *
 *      OpenGL renderer for Qt
 *
 * Authors:
 *      Teemu Korhonen
 *
 *      Copyright 2022 Teemu Korhonen
 */

#include <QCoreApplication>
#include <QMessageBox>
#include <QOpenGLShaderProgram>
#include <QOpenGLTexture>
#include <QStringBuilder>
#include <QSurfaceFormat>

#include <cmath>

#include "qt_opengloptionsdialog.hpp"
#include "qt_openglrenderer.hpp"

OpenGLRenderer::OpenGLRenderer(QWidget *parent)
    : QWindow(parent->windowHandle())
    , renderTimer(new QTimer(this))
{
    renderTimer->setTimerType(Qt::PreciseTimer);
    /* TODO: need's more accuracy, maybe target 1ms earlier and spin yield */
    connect(renderTimer, &QTimer::timeout, this, &OpenGLRenderer::render);

    buf_usage = std::vector<std::atomic_flag>(BUFFERCOUNT);
    for (auto &flag : buf_usage)
        flag.clear();

    setSurfaceType(QWindow::OpenGLSurface);

    QSurfaceFormat format;

#ifdef Q_OS_MACOS
    format.setVersion(4, 1);
#else
    format.setVersion(3, 2);
#endif
    format.setProfile(QSurfaceFormat::OpenGLContextProfile::CoreProfile);

    if (QOpenGLContext::openGLModuleType() == QOpenGLContext::LibGLES)
        format.setRenderableType(QSurfaceFormat::OpenGLES);

    setFormat(format);

    parentWidget = parent;

    source.setRect(0, 0, INIT_WIDTH, INIT_HEIGHT);
}

OpenGLRenderer::~OpenGLRenderer()
{
    finalize();
}

void
OpenGLRenderer::exposeEvent(QExposeEvent *event)
{
    Q_UNUSED(event);

    if (!isInitialized)
        initialize();
}

void
OpenGLRenderer::resizeEvent(QResizeEvent *event)
{
    Q_UNUSED(event);

    onResize(event->size().width(), event->size().height());

    if (notReady())
        return;

    context->makeCurrent(this);

    glViewport(
        destination.x(),
        destination.y(),
        destination.width() * devicePixelRatio(),
        destination.height() * devicePixelRatio());
}

bool
OpenGLRenderer::event(QEvent *event)
{
    Q_UNUSED(event);

    bool res = false;
    if (!eventDelegate(event, res))
        return QWindow::event(event);
    return res;
}

void
OpenGLRenderer::initialize()
{
    try {
        context = new QOpenGLContext(this);

        context->setFormat(format());

        if (!context->create())
            throw opengl_init_error(tr("Couldn't create OpenGL context."));

        if (!context->makeCurrent(this))
            throw opengl_init_error(tr("Couldn't switch to OpenGL context."));

        auto version = context->format().version();

        if (version.first < 3)
            throw opengl_init_error(tr("OpenGL version 3.0 or greater is required. Current version is %1.%2").arg(version.first).arg(version.second));

        initializeOpenGLFunctions();

        /* Prepare the shader version string */
        glslVersion = reinterpret_cast<const char *>(glGetString(GL_SHADING_LANGUAGE_VERSION));
        glslVersion.truncate(4);
        glslVersion.remove('.');
        glslVersion.prepend("#version ");
        if (QOpenGLContext::openGLModuleType() == QOpenGLContext::LibGLES)
            glslVersion.append(" es");
        else if (context->format().profile() == QSurfaceFormat::CoreProfile)
            glslVersion.append(" core");

        initializeExtensions();

        initializeBuffers();

        /* Vertex, texture 2d coordinates and color (white) making a quad as triangle strip */
        const GLfloat surface[] = {
            -1.f, 1.f, 0.f, 0.f, 1.f, 1.f, 1.f, 1.f,
            1.f, 1.f, 1.f, 0.f, 1.f, 1.f, 1.f, 1.f,
            -1.f, -1.f, 0.f, 1.f, 1.f, 1.f, 1.f, 1.f,
            1.f, -1.f, 1.f, 1.f, 1.f, 1.f, 1.f, 1.f
        };

        glGenVertexArrays(1, &vertexArrayID);

        glBindVertexArray(vertexArrayID);

        glGenBuffers(1, &vertexBufferID);
        glBindBuffer(GL_ARRAY_BUFFER, vertexBufferID);
        glBufferData(GL_ARRAY_BUFFER, sizeof(surface), surface, GL_STATIC_DRAW);

        glGenTextures(1, &textureID);
        glBindTexture(GL_TEXTURE_2D, textureID);

        const GLfloat border_color[] = { 0.f, 0.f, 0.f, 1.f };

        glTexParameterfv(GL_TEXTURE_2D, GL_TEXTURE_BORDER_COLOR, border_color);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_BORDER);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_BORDER);

        glTexImage2D(GL_TEXTURE_2D, 0, QOpenGLTexture::RGBA8_UNorm, INIT_WIDTH, INIT_HEIGHT, 0, QOpenGLTexture::BGRA, QOpenGLTexture::UInt32_RGBA8_Rev, NULL);

        options = new OpenGLOptions(this, true, glslVersion);

        applyOptions();

        glClearColor(0.f, 0.f, 0.f, 1.f);

        glViewport(
            destination.x(),
            destination.y(),
            destination.width() * devicePixelRatio(),
            destination.height() * devicePixelRatio());

        GLenum error = glGetError();
        if (error != GL_NO_ERROR)
            throw opengl_init_error(tr("OpenGL initialization failed. Error %1.").arg(error));

        isInitialized = true;

        emit initialized();

    } catch (const opengl_init_error &e) {
        /* Mark all buffers as in use */
        for (auto &flag : buf_usage)
            flag.test_and_set();

        QMessageBox::critical((QWidget *) qApp->findChild<QWindow *>(), tr("Error initializing OpenGL"), e.what() % tr("\nFalling back to software rendering."));

        context->doneCurrent();
        isFinalized   = true;
        isInitialized = true;

        emit errorInitializing();
    }
}

void
OpenGLRenderer::finalize()
{
    if (isFinalized)
        return;

    renderTimer->stop();

    context->makeCurrent(this);

    if (hasBufferStorage)
        glUnmapBuffer(GL_PIXEL_UNPACK_BUFFER);

    glDeleteBuffers(1, &unpackBufferID);
    glDeleteTextures(1, &textureID);
    glDeleteBuffers(1, &vertexBufferID);
    glDeleteVertexArrays(1, &vertexArrayID);

    if (!hasBufferStorage && unpackBuffer)
        free(unpackBuffer);

    context->doneCurrent();

    isFinalized = true;
}

QDialog *
OpenGLRenderer::getOptions(QWidget *parent)
{
    auto dialog = new OpenGLOptionsDialog(parent, *options, [this]() { return new OpenGLOptions(this, false, glslVersion); });

    connect(dialog, &OpenGLOptionsDialog::optionsChanged, this, &OpenGLRenderer::updateOptions);

    return dialog;
}

void
OpenGLRenderer::initializeExtensions()
{
#ifndef NO_BUFFER_STORAGE
    if (context->hasExtension("GL_ARB_buffer_storage")) {
        hasBufferStorage = true;

        glBufferStorage = (PFNGLBUFFERSTORAGEPROC) context->getProcAddress("glBufferStorage");
    }
#endif
}

void
OpenGLRenderer::initializeBuffers()
{
    glGenBuffers(1, &unpackBufferID);

    glBindBuffer(GL_PIXEL_UNPACK_BUFFER, unpackBufferID);

    if (hasBufferStorage) {
#ifndef NO_BUFFER_STORAGE
        /* Create persistent buffer for pixel transfer. */
        glBufferStorage(GL_PIXEL_UNPACK_BUFFER, BUFFERBYTES * BUFFERCOUNT, NULL, GL_MAP_WRITE_BIT | GL_MAP_PERSISTENT_BIT | GL_MAP_COHERENT_BIT);

        unpackBuffer = glMapBufferRange(GL_PIXEL_UNPACK_BUFFER, 0, BUFFERBYTES * BUFFERCOUNT, GL_MAP_WRITE_BIT | GL_MAP_PERSISTENT_BIT | GL_MAP_COHERENT_BIT);
#endif
    } else {
        /* Fallback; create our own buffer. */
        unpackBuffer = malloc(BUFFERBYTES * BUFFERCOUNT);

        if (unpackBuffer == nullptr)
            throw opengl_init_error(tr("Allocating memory for unpack buffer failed."));

        glBufferData(GL_PIXEL_UNPACK_BUFFER, BUFFERBYTES * BUFFERCOUNT, NULL, GL_STREAM_DRAW);
    }
}

void
OpenGLRenderer::applyOptions()
{
    /* TODO: change detection in options */

    if (options->framerate() > 0) {
        int interval = (int) ceilf(1000.f / (float) options->framerate());
        renderTimer->setInterval(std::chrono::milliseconds(interval));
    }

    if (options->renderBehavior() == OpenGLOptions::TargetFramerate)
        renderTimer->start();
    else
        renderTimer->stop();

    auto format   = this->format();
    int  interval = options->vSync() ? 1 : 0;

    if (format.swapInterval() != interval) {
        format.setSwapInterval(interval);
        setFormat(format);
        context->setFormat(format);
    }

    GLint filter = options->filter() == OpenGLOptions::Linear ? GL_LINEAR : GL_NEAREST;
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, filter);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, filter);

    currentFilter = options->filter();
}

void
OpenGLRenderer::applyShader(const OpenGLShaderPass &shader)
{
    if (!shader.bind())
        return;

    if (shader.vertex_coord() != -1) {
        glEnableVertexAttribArray(shader.vertex_coord());
        glVertexAttribPointer(shader.vertex_coord(), 2, GL_FLOAT, GL_FALSE, 8 * sizeof(GLfloat), 0);
    }

    if (shader.tex_coord() != -1) {
        glEnableVertexAttribArray(shader.tex_coord());
        glVertexAttribPointer(shader.tex_coord(), 2, GL_FLOAT, GL_FALSE, 8 * sizeof(GLfloat), (void *) (2 * sizeof(GLfloat)));
    }

    if (shader.color() != -1) {
        glEnableVertexAttribArray(shader.color());
        glVertexAttribPointer(shader.color(), 4, GL_FLOAT, GL_FALSE, 8 * sizeof(GLfloat), (void *) (4 * sizeof(GLfloat)));
    }

    if (shader.mvp_matrix() != -1) {
        static const GLfloat mvp[] = {
            1.f, 0.f, 0.f, 0.f,
            0.f, 1.f, 0.f, 0.f,
            0.f, 0.f, 1.f, 0.f,
            0.f, 0.f, 0.f, 1.f
        };
        glUniformMatrix4fv(shader.mvp_matrix(), 1, GL_FALSE, mvp);
    }

    if (shader.output_size() != -1)
        glUniform2f(shader.output_size(), destination.width(), destination.height());

    if (shader.input_size() != -1)
        glUniform2f(shader.input_size(), source.width(), source.height());

    if (shader.texture_size() != -1)
        glUniform2f(shader.texture_size(), source.width(), source.height());

    if (shader.frame_count() != -1)
        glUniform1i(shader.frame_count(), frameCounter);
}

void
OpenGLRenderer::render()
{
    context->makeCurrent(this);

    if (options->filter() != currentFilter)
        applyOptions();

    /* TODO: multiple shader passes */
    applyShader(options->shaders().first());

    glClear(GL_COLOR_BUFFER_BIT);
    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);

    context->swapBuffers(this);

    frameCounter = (frameCounter + 1) & 1023;
}

void
OpenGLRenderer::updateOptions(OpenGLOptions *newOptions)
{
    context->makeCurrent(this);

    glUseProgram(0);

    delete options;

    options = newOptions;

    options->setParent(this);

    applyOptions();
}

std::vector<std::tuple<uint8_t *, std::atomic_flag *>>
OpenGLRenderer::getBuffers()
{
    std::vector<std::tuple<uint8_t *, std::atomic_flag *>> buffers;

    if (notReady() || !unpackBuffer)
        return buffers;

    /* Split the buffer area */
    for (int i = 0; i < BUFFERCOUNT; i++) {
        buffers.push_back(std::make_tuple((uint8_t *) unpackBuffer + BUFFERBYTES * i, &buf_usage[i]));
    }

    return buffers;
}

void
OpenGLRenderer::onBlit(int buf_idx, int x, int y, int w, int h)
{
    if (notReady())
        return;

    context->makeCurrent(this);

    if (source.width() != w || source.height() != h) {
        source.setRect(0, 0, w, h);

        /* Resize the texture */
        glBindBuffer(GL_PIXEL_UNPACK_BUFFER, 0);
        glTexImage2D(GL_TEXTURE_2D, 0, (GLenum) QOpenGLTexture::RGBA8_UNorm, source.width(), source.height(), 0, (GLenum) QOpenGLTexture::BGRA, (GLenum) QOpenGLTexture::UInt32_RGBA8_Rev, NULL);
        glBindBuffer(GL_PIXEL_UNPACK_BUFFER, unpackBufferID);
    }

    if (!hasBufferStorage)
        glBufferSubData(GL_PIXEL_UNPACK_BUFFER, BUFFERBYTES * buf_idx, h * ROW_LENGTH * sizeof(uint32_t), (uint8_t *) unpackBuffer + BUFFERBYTES * buf_idx);

    glPixelStorei(GL_UNPACK_SKIP_PIXELS, BUFFERPIXELS * buf_idx + y * ROW_LENGTH + x);
    glPixelStorei(GL_UNPACK_ROW_LENGTH, ROW_LENGTH);
    glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, w, h, (GLenum) QOpenGLTexture::BGRA, (GLenum) QOpenGLTexture::UInt32_RGBA8_Rev, NULL);

    /* TODO: check if fence sync is implementable here and still has any benefit. */
    glFinish();

    buf_usage[buf_idx].clear();

    if (options->renderBehavior() == OpenGLOptions::SyncWithVideo)
        render();
}
