#ifndef QT_RENDERERCONTAINER_HPP
#define QT_RENDERERCONTAINER_HPP

#include <QStackedWidget>
#include <QKeyEvent>

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
    void keyPressEvent(QKeyEvent* event) override
    {
        event->ignore();
    }
    void keyReleaseEvent(QKeyEvent* event) override
    {
        event->ignore();
    }

signals:
    void blitToRenderer(const QImage& img, int, int, int, int);

public slots:
    void blit(int x, int y, int w, int h);
    void mousePoll();

private slots:
    void on_RendererStack_currentChanged(int arg1);

private:
    Ui::RendererStack *ui;

    struct mouseinputdata {
        int deltax, deltay, deltaz;
        int mousebuttons;
    };
    mouseinputdata mousedata;

    int x, y, w, h, sx, sy, sw, sh;

    // always have a qimage available for writing, which is _probably_ unused
    // worst case - it will just get reallocated because it's refcounter is > 1
    // when calling bits();
    int currentBuf = 0;
    QVector<QImage> imagebufs;
};

#endif // QT_RENDERERCONTAINER_HPP
