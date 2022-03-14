#ifndef QT_RENDERERCONTAINER_HPP
#define QT_RENDERERCONTAINER_HPP

#include <QDialog>
#include <QEvent>
#include <QKeyEvent>
#include <QStackedWidget>
#include <QWidget>

#include <atomic>
#include <memory>
#include <tuple>
#include <vector>

#include "qt_renderercommon.hpp"

namespace Ui {
class RendererStack;
}

class RendererCommon;
class RendererStack : public QStackedWidget {
    Q_OBJECT

public:
    explicit RendererStack(QWidget *parent = nullptr);
    ~RendererStack();

    void mousePressEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void wheelEvent(QWheelEvent *event) override;
    void leaveEvent(QEvent *event) override;
    void keyPressEvent(QKeyEvent *event) override
    {
        event->ignore();
    }
    void keyReleaseEvent(QKeyEvent *event) override
    {
        event->ignore();
    }

    enum class Renderer {
        Software,
        OpenGL,
        OpenGLES,
        OpenGL3
    };
    void switchRenderer(Renderer renderer);

    /* Does current renderer implement options dialog */
    bool hasOptions() const { return rendererWindow ? rendererWindow->hasOptions() : false; }
    /* Returns options dialog for current renderer */
    QDialog *getOptions(QWidget *parent) { return rendererWindow ? rendererWindow->getOptions(parent) : nullptr; }

    void setFocusRenderer()
    {
        if (current)
            current->setFocus();
    }
    void onResize(int width, int height)
    {
        if (rendererWindow)
            rendererWindow->onResize(width, height);
    }

signals:
    void blitToRenderer(int buf_idx, int x, int y, int w, int h);
    void rendererChanged();

public slots:
    void blit(int x, int y, int w, int h);
    void mousePoll();

private:
    void createRenderer(Renderer renderer);

    Ui::RendererStack *ui;

    struct mouseinputdata {
        int deltax, deltay, deltaz;
        int mousebuttons;
    };
    mouseinputdata mousedata;

    int x, y, w, h, sx, sy, sw, sh;

    int currentBuf  = 0;
    int isMouseDown = 0;

    std::vector<std::tuple<uint8_t *, std::atomic_flag *>> imagebufs;

    RendererCommon          *rendererWindow { nullptr };
    std::unique_ptr<QWidget> current;
};

#endif // QT_RENDERERCONTAINER_HPP
