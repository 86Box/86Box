#ifndef QT_RENDERERCONTAINER_HPP
#define QT_RENDERERCONTAINER_HPP

#include <QDialog>
#include <QEvent>
#include <QKeyEvent>
#include <QStackedWidget>
#include <QWidget>
#include <QCursor>

#include <atomic>
#include <memory>
#include <tuple>
#include <vector>

#include "qt_renderercommon.hpp"

#include <atomic>

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
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
    void enterEvent(QEnterEvent *event) override;
#else
    void enterEvent(QEvent *event) override;
#endif
    void leaveEvent(QEvent *event) override;
    void closeEvent(QCloseEvent *event) override;
    void changeEvent(QEvent *event) override;
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
    bool event(QEvent* event) override;

    enum class Renderer {
        Software,
        OpenGL,
        OpenGLES,
        OpenGL3,
        Vulkan,
        None = -1
    };
    void switchRenderer(Renderer renderer);

    /* Does current renderer implement options dialog */
    bool hasOptions() const { return rendererWindow ? rendererWindow->hasOptions() : false; }
    /* Reloads options of current renderer */
    void reloadOptions() const { return rendererWindow->reloadOptions(); }
    /* Returns options dialog for current renderer */
    QDialog *getOptions(QWidget *parent) { return rendererWindow ? rendererWindow->getOptions(parent) : nullptr; }
    /* Reload the renderer itself */
    bool reloadRendererOption() { return rendererWindow ? rendererWindow->reloadRendererOption() : false; }

    void setFocusRenderer();
    void onResize(int width, int height);

    void (*mouse_capture_func)(QWindow *window) = nullptr;
    void (*mouse_uncapture_func)()              = nullptr;

    void (*mouse_exit_func)()                   = nullptr;

signals:
    void blitToRenderer(int buf_idx, int x, int y, int w, int h);
    void rendererChanged();

public slots:
    void blit(int x, int y, int w, int h);

private:
    void createRenderer(Renderer renderer);

    Ui::RendererStack *ui;

    int x;
    int y;
    int w;
    int h;
    int sx;
    int sy;
    int sw;
    int sh;

    int currentBuf      = 0;
    int isMouseDown     = 0;
    int m_monitor_index = 0;

    std::vector<std::tuple<uint8_t *, std::atomic_flag *>> imagebufs;

    RendererCommon          *rendererWindow { nullptr };
    std::unique_ptr<QWidget> current;

    std::atomic_bool rendererTakesScreenshots;
    std::atomic_bool switchInProgress{false};
};

#endif // QT_RENDERERCONTAINER_HPP
