#pragma once

#include <QOpenGLFunctions>
#include <QOpenGLWidget>
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

class HardwareRenderer : public QOpenGLWidget, protected QOpenGLFunctions, public RendererCommon
{
	Q_OBJECT

private:
    bool wayland = false;
public:
    void resizeGL(int w, int h) override;
    void initializeGL() override;
    void paintGL() override;
    HardwareRenderer(QWidget* parent = nullptr)
    : QOpenGLWidget(parent), QOpenGLFunctions()
    {
        setMinimumSize(16, 16);
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
};
