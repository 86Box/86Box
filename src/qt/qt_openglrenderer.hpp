/*
 * 86Box A hypervisor and IBM PC system emulator that specializes in
 *      running old operating systems and software designed for IBM
 *      PC systems and compatibles from 1981 through fairly recent
 *      system designs based on the PCI bus.
 *
 *      This file is part of the 86Box distribution.
 *
 *      Header file for OpenGL renderer
 *
 * Authors:
 *      Teemu Korhonen
 *
 *      Copyright 2022 Teemu Korhonen
 */

#ifndef QT_OPENGLRENDERER_HPP
#define QT_OPENGLRENDERER_HPP

#if defined Q_OS_MACOS || __arm__
#    define NO_BUFFER_STORAGE
#endif

#include <QOpenGLContext>
#include <QOpenGLExtraFunctions>
#include <QResizeEvent>
#include <QTimer>
#include <QWidget>
#include <QWindow>
#if !defined NO_BUFFER_STORAGE && !(QT_VERSION >= QT_VERSION_CHECK(6, 0, 0))
#    include <QtOpenGLExtensions/QOpenGLExtensions>
#endif

#include <atomic>
#include <stdexcept>
#include <tuple>
#include <vector>

#include "qt_opengloptions.hpp"
#include "qt_renderercommon.hpp"

class OpenGLRenderer : public QWindow, protected QOpenGLExtraFunctions, public RendererCommon {
    Q_OBJECT

public:
    QOpenGLContext *context;

    OpenGLRenderer(QWidget *parent = nullptr);
    ~OpenGLRenderer();

    std::vector<std::tuple<uint8_t *, std::atomic_flag *>> getBuffers() override;

    void     finalize() override final;
    bool     hasOptions() const override { return true; }
    QDialog *getOptions(QWidget *parent) override;

signals:
    void initialized();
    void errorInitializing();

public slots:
    void onBlit(int buf_idx, int x, int y, int w, int h);

protected:
    void exposeEvent(QExposeEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;
    bool event(QEvent *event) override;

private:
    static constexpr int INIT_WIDTH   = 640;
    static constexpr int INIT_HEIGHT  = 400;
    static constexpr int ROW_LENGTH   = 2048;
    static constexpr int BUFFERPIXELS = 4194304;
    static constexpr int BUFFERBYTES  = 16777216; /* Pixel is 4 bytes. */
    static constexpr int BUFFERCOUNT  = 3;        /* How many buffers to use for pixel transfer (2-3 is commonly recommended). */

    OpenGLOptions *options;
    QTimer        *renderTimer;

    QString glslVersion;

    bool isInitialized = false;
    bool isFinalized   = false;

    GLuint unpackBufferID = 0;
    GLuint vertexArrayID  = 0;
    GLuint vertexBufferID = 0;
    GLuint textureID      = 0;
    int    frameCounter   = 0;

    OpenGLOptions::FilterType currentFilter;

    void *unpackBuffer = nullptr;

    void initialize();
    void initializeExtensions();
    void initializeBuffers();
    void applyOptions();
    void applyShader(const OpenGLShaderPass &shader);
    bool notReady() const { return !isInitialized || isFinalized; }

    /* GL_ARB_buffer_storage */
    bool hasBufferStorage = false;
#ifndef NO_BUFFER_STORAGE
    PFNGLBUFFERSTORAGEPROC glBufferStorage = nullptr;
#endif

private slots:
    void render();
    void updateOptions(OpenGLOptions *newOptions);
};

class opengl_init_error : public std::runtime_error {
public:
    opengl_init_error(const QString &what)
        : std::runtime_error(what.toStdString())
    {
    }
};

#endif
