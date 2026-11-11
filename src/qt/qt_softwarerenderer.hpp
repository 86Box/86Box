#ifndef SOFTWARERENDERER_HPP
#define SOFTWARERENDERER_HPP

#include <QWidget>
#include <QWindow>
#include <QPaintDevice>
#include <QRegion>
#include <QScopedPointer>
#include <QBackingStore>
#include <array>
#include <atomic>
#include <memory>
#include <mutex>
#include "qt_renderercommon.hpp"

class QPainter;
class QThread;

class SoftwareRenderer :
    public QWidget,
    public RendererCommon {
    Q_OBJECT
public:
    explicit SoftwareRenderer(QWidget *parent = nullptr);
    void finalize() override final;

    void paintEvent(QPaintEvent *event) override;

#ifdef __HAIKU__
    void exposeEvent(QExposeEvent *event) override;
#else
    void exposeEvent(QExposeEvent *event);
#endif

    std::vector<std::tuple<uint8_t *, std::atomic_flag *>> getBuffers() override;

    /* Frames are scaled on a worker thread so that a large picture does not
       hold up the GUI thread, and with it the host's input. Connect the
       blit signal to this object, not to the renderer. */
    QObject *blitContext() const { return worker.context(); }

public slots:
    /* Runs on the worker thread. */
    void onBlit(int buf_idx, int x, int y, int w, int h);

protected:
    std::array<std::unique_ptr<QImage>, 2> images;
    bool                                   osd_drawn_last = false;

    void onPaint(QPaintDevice *device);
    void layoutPicture();
    void resizeEvent(QResizeEvent *event) override;
    bool event(QEvent *event) override;

    bool was_osd_visible = false;

    void render();

    QScopedPointer<QBackingStore> m_backingStore;

private:
    /* A thread with an event loop and an object living on it, to deliver work to.
       Stopping it waits for the work in progress to finish. */
    class Worker {
    public:
        Worker();
        ~Worker();
        Worker(const Worker &)            = delete;
        Worker &operator=(const Worker &) = delete;
        Worker(Worker &&)                 = delete;
        Worker &operator=(Worker &&)      = delete;

        QObject *context() const { return ctx.get(); }
        void     stop();

    private:
        std::unique_ptr<QThread> thread;
        std::unique_ptr<QObject> ctx;
    };

    /* Layout published by the GUI thread for the worker, in device pixels. */
    struct Layout {
        QRect dest;
        QSize size;
        qreal ratio = 1.0;
        int   gen   = 0;
    };

    /* Worker thread: scale the current frame into the back buffer, where dirty
       is the part of the window (device pixels) that changed. */
    void    scaleFrame(const QRegion &dirty, bool full);
    QRegion paintBuffer(int back, const Layout &l, const QRegion &dirty, bool full, bool relayout);
    void    presentBuffer(int back, const QRegion &painted);
    void    takeScreenshots(int x, int y, int w, int h);

    Layout currentLayout();
    void   publishLayout();
    int    frontBuffer();
    bool   drawFront(QPainter &painter, const QPaintDevice *device);

    /* Owned by the worker thread. */
    int   cur_image = -1;
    QRect wsource { 0, 0, 0, 0 };
    int   w_layout_gen = -1;

    std::mutex layout_mutex;
    Layout     layout;

    /* The scaled picture, double buffered. The worker writes the back buffer
       while the GUI thread copies the front one to the window; each buffer
       remembers what it missed while the other was in front. */
    std::mutex              idx_mutex;
    std::array<std::mutex, 2> scaled_mutex;
    std::array<QImage, 2>   scaled;
    std::array<QRegion, 2>  stale;
    int                     front = -1;

    /* Declared last, so that it stops before anything it uses is destroyed. */
    Worker worker;
};

#endif // SOFTWARERENDERER_HPP
