#ifndef QT_RENDERERCONTAINER_HPP
#define QT_RENDERERCONTAINER_HPP

#include <QDialog>
#include <QEvent>
#include <QKeyEvent>
#include <QLayout>
#include <QBoxLayout>
#include <QWidget>
#include <QCursor>
#include <QScreen>

#include <atomic>
#include <memory>
#include <tuple>
#include <vector>

#include "qt_renderercommon.hpp"
#include "qt_util.hpp"

#include <atomic>

namespace Ui {
class RendererStack;
}

extern "C" {
extern int vid_resize;
}

class RendererCommon;
class RendererStack : public QWidget {
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
        if (this->m_monitor_index != 0 && vid_resize != 1) {
            int newX = pos().x();
            int newY = pos().y();

            if (((frameGeometry().x() + event->size().width() + 1) > util::screenOfWidget(this)->availableGeometry().right())) {
                // move(util::screenOfWidget(this)->availableGeometry().right() - size().width() - 1, pos().y());
                newX = util::screenOfWidget(this)->availableGeometry().right() - frameGeometry().width() - 1;
                if (newX < 1)
                    newX = 1;
            }

            if (((frameGeometry().y() + event->size().height() + 1) > util::screenOfWidget(this)->availableGeometry().bottom())) {
                newY = util::screenOfWidget(this)->availableGeometry().bottom() - frameGeometry().height() - 1;
                if (newY < 1)
                    newY = 1;
            }
            move(newX, newY);
        }
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
    bool event(QEvent *event) override;

    enum class Renderer {
        Software,
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

    QWidget *currentWidget() { return current.get(); }

    void (*mouse_capture_func)(QWindow *window) = nullptr;
    void (*mouse_uncapture_func)()              = nullptr;

    void (*mouse_exit_func)() = nullptr;

signals:
    void blitToRenderer(int buf_idx, int x, int y, int w, int h);
    void rendererChanged();

public slots:
    void blit(int x, int y, int w, int h);

private:
    void createRenderer(Renderer renderer);

    QBoxLayout *boxLayout = nullptr;

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
    std::atomic_bool switchInProgress { false };

    char auto_mouse_type[16];
};

#endif // QT_RENDERERCONTAINER_HPP
