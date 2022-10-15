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
    explicit RendererStack(QWidget *parent = nullptr, int monitor_index = 0);
    ~RendererStack();

    void mousePressEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void wheelEvent(QWheelEvent *event) override;
    void leaveEvent(QEvent *event) override;
    void closeEvent(QCloseEvent *event) override;
    void changeEvent(QEvent* event) override;
    void resizeEvent(QResizeEvent *event) override
    {
        onResize(event->size().width(), event->size().height());
    }
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
        OpenGL3,
        Vulkan,
        Direct3D9,
        None = -1
    };
    void switchRenderer(Renderer renderer);

    /* Does current renderer implement options dialog */
    bool hasOptions() const { return rendererWindow ? rendererWindow->hasOptions() : false; }
    /* Reloads options of current renderer */
    void reloadOptions() const { return rendererWindow->reloadOptions(); }
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

    void (*mouse_poll_func)() = nullptr;
    void (*mouse_capture_func)(QWindow *window) = nullptr;
    void (*mouse_uncapture_func)() = nullptr;
    void (*mouse_exit_func)() = nullptr;

signals:
    void blitToRenderer(int buf_idx, int x, int y, int w, int h);
    void blit(int x, int y, int w, int h);
    void rendererChanged();

public slots:
    void blitCommon(int x, int y, int w, int h);
    void blitRenderer(int x, int y, int w, int h);
    void blitDummy(int x, int y, int w, int h);
    void mousePoll();

private:
    void createRenderer(Renderer renderer);

    Ui::RendererStack *ui;

    int x, y, w, h, sx, sy, sw, sh;

    int currentBuf  = 0;
    int isMouseDown = 0;
    int m_monitor_index = 0;

    Renderer current_vid_api = Renderer::None;

    std::vector<std::tuple<uint8_t *, std::atomic_flag *>> imagebufs;

    RendererCommon          *rendererWindow { nullptr };
    std::unique_ptr<QWidget> current;
    std::atomic<bool> directBlitting{false};
};

#endif // QT_RENDERERCONTAINER_HPP
