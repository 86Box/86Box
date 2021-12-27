#ifndef QT_RENDERERCONTAINER_HPP
#define QT_RENDERERCONTAINER_HPP

#include <QStackedWidget>
#include <QKeyEvent>
#include <QEvent>
#include <memory>
#include <vector>
#include <atomic>
#include <array>

namespace Ui {
class RendererStack;
}

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

signals:
    void blitToRenderer(const std::unique_ptr<uint8_t>* img, int, int, int, int, std::atomic_flag* in_use);

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
    std::array<std::unique_ptr<uint8_t>, 2> imagebufs;

    std::unique_ptr<QWidget> current;

    /* atomic flag for each buffer to not overload the renderer */
    std::vector<std::atomic_flag> buffers_in_use;
};

#endif // QT_RENDERERCONTAINER_HPP
