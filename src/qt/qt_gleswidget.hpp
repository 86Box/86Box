#pragma once

#include <QOpenGLFunctions>
#include <QOpenGLWidget>
#include <QPainter>
#include <QEvent>
#include <QKeyEvent>

class GLESWidget : public QOpenGLWidget, protected QOpenGLFunctions
{
	Q_OBJECT

private:
    QImage m_image{QSize(2048 + 64, 2048 + 64), QImage::Format_RGB32};
    int x, y, w, h, sx, sy, sw, sh;
public:
    void resizeGL(int w, int h) override;
    void initializeGL() override;
    void paintGL() override;
    GLESWidget(QWidget* parent = nullptr)
    : QOpenGLWidget(parent), QOpenGLFunctions()
    {
        setMinimumSize(16, 16);
    }
    ~GLESWidget()
    {
        makeCurrent();
    }
    void mousePressEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void wheelEvent(QWheelEvent *event) override;
    void keyPressEvent(QKeyEvent* event) override
    {
        event->ignore();
    }
    void keyReleaseEvent(QKeyEvent* event) override
    {
        event->ignore();
    }
public slots:
	void qt_real_blit(int x, int y, int w, int h);
    void qt_mouse_poll();

private:
    struct mouseinputdata {
        int deltax, deltay, deltaz;
        int mousebuttons;
    };
    mouseinputdata mousedata;

};
