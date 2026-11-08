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
#include <mutex>
#include "qt_renderercommon.hpp"

class QThread;

class SoftwareRenderer :
    public QWidget,
    public RendererCommon {
    Q_OBJECT
public:
    explicit SoftwareRenderer(QWidget *parent = nullptr);
    ~SoftwareRenderer() override;
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
    QObject *blitContext() const { return worker_ctx; }

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
    void stopWorker();
    /* Worker thread: scale the current frame into the back buffer, where dirty
       is the part of the window (device pixels) that changed. */
    void scaleFrame(const QRegion &dirty, bool full);
    void takeScreenshots(int x, int y, int w, int h);

    QThread *worker     = nullptr;
    QObject *worker_ctx = nullptr;

    /* Owned by the worker thread. */
    int   cur_image = -1;
    QRect wsource { 0, 0, 0, 0 };
    int   w_layout_gen = -1;

    /* Layout published by the GUI thread for the worker. */
    std::mutex layout_mutex;
    QRect      l_dest;
    QSize      l_size;
    qreal      l_ratio = 1.0;
    int        l_gen   = 0;

    /* The scaled picture, double buffered. The worker writes the back buffer
       while the GUI thread copies the front one to the window; each buffer
       remembers what it missed while the other was in front. */
    std::mutex              idx_mutex;
    std::array<std::mutex, 2> scaled_mutex;
    std::array<QImage, 2>   scaled;
    std::array<QRegion, 2>  stale;
    int                     front = -1;
};

#endif // SOFTWARERENDERER_HPP
