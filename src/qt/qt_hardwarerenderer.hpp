#pragma once

#include <QOpenGLFunctions>
#include <QOpenGLBuffer>
#include <QOpenGLWindow>
#include <QOpenGLVertexArrayObject>
#include <QOpenGLTexture>
#include <QOpenGLShader>
#include <QOpenGLShaderProgram>
#include <QOpenGLTextureBlitter>
#include <QPainter>
#include <QEvent>
#include <QKeyEvent>
#include <QWidget>

#include <atomic>
#include <mutex>
#include <array>
#include <vector>
#include <memory>
#include <QApplication>

#include "qt_renderercommon.hpp"

#ifdef WAYLAND
#include "wl_mouse.hpp"
#endif

class HardwareRenderer : public QOpenGLWindow, protected QOpenGLFunctions, public RendererCommon
{
	Q_OBJECT

private:
    bool wayland = false;
    QOpenGLContext* m_context;
    QOpenGLTexture* m_texture{nullptr};
    QOpenGLShaderProgram* m_prog{nullptr};
    QOpenGLTextureBlitter* m_blt{nullptr};
    QOpenGLBuffer m_vbo[2];
    QOpenGLVertexArrayObject m_vao;
public:
    enum class RenderType {
        OpenGL,
        OpenGLES,
        OpenGL3,
    };
    void resizeGL(int w, int h) override;
    void initializeGL() override;
    void paintGL() override;
    std::vector<std::tuple<uint8_t*, std::atomic_flag*>> getBuffers() override;
    HardwareRenderer(QWidget* parent = nullptr, RenderType rtype = RenderType::OpenGL)
    : QOpenGLWindow(QOpenGLWindow::NoPartialUpdate, parent->windowHandle()), QOpenGLFunctions()
    {
        imagebufs[0] = std::unique_ptr<uint8_t>(new uint8_t[2048 * 2048 * 4]);
        imagebufs[1] = std::unique_ptr<uint8_t>(new uint8_t[2048 * 2048 * 4]);

        buf_usage = std::vector<std::atomic_flag>(2);
        buf_usage[0].clear();
        buf_usage[1].clear();

        setMinimumSize(QSize(16, 16));
        setFlags(Qt::FramelessWindowHint);
        parentWidget = parent;
        setRenderType(rtype);

        m_context = new QOpenGLContext();
        m_context->setFormat(format());
        m_context->create();
    }
    ~HardwareRenderer()
    {
        m_context->makeCurrent(this);
        if (m_blt) m_blt->destroy();
        m_prog->release();
        delete m_prog;
        m_prog = nullptr;
        m_context->doneCurrent();
        delete m_context;
    }


    void setRenderType(RenderType type);

public slots:
    void onBlit(int buf_idx, int x, int y, int w, int h);

protected:
    std::array<std::unique_ptr<uint8_t>, 2> imagebufs;

    void resizeEvent(QResizeEvent *event) override;
    bool event(QEvent* event) override;
};
