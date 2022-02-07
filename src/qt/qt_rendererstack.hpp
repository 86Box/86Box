#ifndef QT_RENDERERCONTAINER_HPP
#define QT_RENDERERCONTAINER_HPP

#include <QStackedWidget>
#include <QKeyEvent>
#include <QEvent>
#include <memory>
#include <vector>
#include <atomic>
#include <tuple>

namespace Ui {
class RendererStack;
}

class RendererCommon;
class RendererStack : public QStackedWidget
{
    Q_OBJECT

public:
    explicit RendererStack(QWidget *parent = nullptr);
    ~RendererStack();

    void mousePressEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void wheelEvent(QWheelEvent *event) override;
    void leaveEvent(QEvent *event) override;
    void keyPressEvent(QKeyEvent* event) override
    {
        event->ignore();
    }
    void keyReleaseEvent(QKeyEvent* event) override
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

    RendererCommon* rendererWindow{nullptr};
signals:
    void blitToRenderer(int buf_idx, int x, int y, int w, int h);

public slots:
    void blit(int x, int y, int w, int h);
    void mousePoll();

private:
    Ui::RendererStack *ui;

    struct mouseinputdata {
        int deltax, deltay, deltaz;
        int mousebuttons;
    };
    mouseinputdata mousedata;

    int x, y, w, h, sx, sy, sw, sh;

    int currentBuf = 0;
    int isMouseDown = 0;
    std::vector<std::tuple<uint8_t*, std::atomic_flag*>> imagebufs;

    std::unique_ptr<QWidget> current;

    friend class MainWindow;
};

#endif // QT_RENDERERCONTAINER_HPP
