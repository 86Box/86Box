#pragma once

#include <QOpenGLFunctions>
#include <QOpenGLWidget>
#include <QOpenGLWindow>
#include <QPainter>
#include <QEvent>
#include <QKeyEvent>

#include <atomic>
#include <mutex>
#include <QApplication>

#include "qt_renderercomon.hpp"

#ifdef WAYLAND
#include "wl_mouse.hpp"
#endif

class HardwareRenderer : public QOpenGLWindow, protected QOpenGLFunctions, public RendererCommon
{
	Q_OBJECT

private:
    bool wayland = false;
    QWidget* parentWidget{nullptr};
public:
    void resizeGL(int w, int h) override;
    void initializeGL() override;
    void paintGL() override;
    HardwareRenderer(QWidget* parent = nullptr)
    : QOpenGLWindow(QOpenGLWindow::NoPartialUpdate, parent->windowHandle()), QOpenGLFunctions()
    {
        setMinimumSize(QSize(16, 16));
        setFlags(Qt::FramelessWindowHint);
        parentWidget = parent;
    }
    ~HardwareRenderer()
    {
        makeCurrent();
    }

    enum class RenderType {
        OpenGL,
        OpenGLES,
    };
    void setRenderType(RenderType type);

public slots:
    void onBlit(const QImage& img, int, int, int, int, std::atomic_flag* in_use);

protected:
    void resizeEvent(QResizeEvent *event) override;
    bool event(QEvent* event) override;
};
