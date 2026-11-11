/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          Software renderer module.
 *
 * Authors: Joakim L. Gilje <jgilje@jgilje.net>
 *          Cacodemon345
 *          Teemu Korhonen
 *
 *          Copyright 2021 Joakim L. Gilje
 *          Copyright 2021-2022 Cacodemon345
 *          Copyright 2021-2022 Teemu Korhonen
 */
#include "qt_softwarerenderer.hpp"
#include <QApplication>
#include <QPainter>
#include <QResizeEvent>
#include <QScreen>
#include <QThread>
#include <QTimer>
#include <algorithm>
#include <cstring>
#include "qt_util.hpp"
#include "qt_osd.hpp"

extern "C" {
#include <86box/86box.h>
#include <86box/path.h>
#include <86box/plat.h>
#include <86box/video.h>
}

SoftwareRenderer::SoftwareRenderer(QWidget *parent)
    : QWidget(parent)
{
    RendererCommon::parentWidget = parent;

    images[0] = std::make_unique<QImage>(QSize(2048, 2048), QImage::Format_RGB32);
    images[1] = std::make_unique<QImage>(QSize(2048, 2048), QImage::Format_RGB32);

    buf_usage = std::vector<std::atomic_flag>(2);
    buf_usage[0].clear();
    buf_usage[1].clear();
    this->setMouseTracking(true);
    /* Every paint covers the whole update region, so skip Qt's background erase. */
    this->setAttribute(Qt::WA_OpaquePaintEvent);

    /* The OSD animates and reacts to input even when the machine is paused, so
     * keep refreshing while it is on screen. */
    connect(new QTimer(this), &QTimer::timeout, this, [this]() {
        if (dopause && qt_osd_needs_render())
            this->render();

        if (!qt_osd_needs_render() && was_osd_visible)
            this->render();

        was_osd_visible = qt_osd_needs_render();
    });
}

SoftwareRenderer::Worker::Worker()
    : thread(std::make_unique<QThread>())
    , ctx(std::make_unique<QObject>())
{
    ctx->moveToThread(thread.get());
    thread->setObjectName("SoftwareRenderer");
    thread->start();
}

SoftwareRenderer::Worker::~Worker()
{
    stop();
}

void
SoftwareRenderer::Worker::stop()
{
    if (!thread)
        return;
    thread->quit();
    thread->wait();
    ctx.reset();
    thread.reset();
}

void
SoftwareRenderer::finalize()
{
    worker.stop();
    qt_osd_shutdown();
}

void
SoftwareRenderer::paintEvent(QPaintEvent *event)
{
    (void) event;
    onPaint(this);
}

void
SoftwareRenderer::render()
{
#ifdef __HAIKU__
    if (!isExposed())
        return;
#endif

    QRect rect(0, 0, width(), height());
    m_backingStore->beginPaint(rect);

    QPaintDevice *device = m_backingStore->paintDevice();
    onPaint(device);

    m_backingStore->endPaint();
    m_backingStore->flush(rect);
}

void
SoftwareRenderer::exposeEvent(QExposeEvent *event)
{
    render();
}

extern void take_screenshot_clipboard_monitor(int sx, int sy, int sw, int sh, int i);

void
SoftwareRenderer::onBlit(int buf_idx, int x, int y, int w, int h)
{
    QRect origSource = wsource;
    int   prev_image = cur_image;

    cur_image = buf_idx;
    wsource.setRect(x, y, w, h);

    /* Only repaint the rows that changed since the last frame, which the other
       buffer still holds until it is released below. Every frame is blitted
       whether or not anything changed, and sending all of it to a remote X
       display can queue up enough to put the picture a second behind. */
    int top    = 0;
    int bottom = h;
    if ((wsource == origSource) && (prev_image != -1) && (prev_image != buf_idx)) {
        const QImage &cur  = *images[buf_idx];
        const QImage &last = *images[prev_image];
        auto          same = [&](int row) {
            return !memcmp(cur.constScanLine(y + row) + (x * 4), last.constScanLine(y + row) + (x * 4), w * 4);
        };
        while ((top < h) && same(top))
            top++;
        while ((bottom > top) && same(bottom - 1))
            bottom--;
    }
    buf_usage[buf_idx ^ 1].clear();

    if (wsource != origSource) {
        /* The GUI thread owns the layout; it asks for a full repaint once it has
           placed the new picture. */
        QMetaObject::invokeMethod(this, [this, src = wsource] {
            source = src;
            layoutPicture(); }, Qt::QueuedConnection);
    } else if ((top == 0) && (bottom == h)) {
        scaleFrame(QRegion(), true);
    } else if (top < bottom) {
        const QRect dest = currentLayout().dest;
        /* Filtering blends each row into its neighbours, so take a row more on either side. */
        top         = std::max(top - 1, 0);
        bottom      = std::min(bottom + 1, h);
        int dtop    = dest.y() + ((top * dest.height()) / h) - 1;
        int dbottom = dest.y() + (((bottom * dest.height()) + h - 1) / h) + 1;
        scaleFrame(QRegion(dest.x(), dtop, dest.width(), dbottom - dtop), false);
    }

    if (monitors[r_monitor_index].mon_screenshots || monitors[r_monitor_index].mon_screenshots_clipboard || monitors[r_monitor_index].mon_screenshots_raw_clipboard)
        QMetaObject::invokeMethod(this, [this, x, y, w, h] { takeScreenshots(x, y, w, h); }, Qt::QueuedConnection);
}

void
SoftwareRenderer::scaleFrame(const QRegion &dirty, bool full)
{
    if (cur_image == -1)
        return;

    const Layout l = currentLayout();
    if (l.size.isEmpty())
        return;
    /* A new layout repaints the window, borders included; a changed frame only
       repaints the picture. */
    const bool relayout = (l.gen != w_layout_gen);
    w_layout_gen        = l.gen;

    const int     back    = (frontBuffer() == 0) ? 1 : 0;
    const QRegion painted = paintBuffer(back, l, dirty, full, relayout);
    presentBuffer(back, painted);

    const QRect r     = painted.boundingRect();
    const qreal ratio = l.ratio;
    QMetaObject::invokeMethod(this, [this, r, ratio] {
        update(QRectF(r.x() / ratio, r.y() / ratio, r.width() / ratio, r.height() / ratio).toAlignedRect()); }, Qt::QueuedConnection);
}

QRegion
SoftwareRenderer::paintBuffer(int back, const Layout &l, const QRegion &dirty, bool full, bool relayout)
{
    /* Waits only while the GUI thread is still copying this buffer out. */
    std::scoped_lock lk(scaled_mutex[back]);
    QImage          &img = scaled[back];
    if (img.size() != l.size) {
        img      = QImage(l.size, QImage::Format_RGB32);
        relayout = true;
    }

    const QRect all(QPoint(0, 0), l.size);
    QRegion     painted(all);
    if (!relayout) {
        const QRegion changed = full ? QRegion(l.dest) : dirty;
        painted               = (changed | stale[back]) & all;
    }

    /* Paint in device pixels: the destination is laid out in them. */
    img.setDevicePixelRatio(1.0);
    if (!painted.isEmpty()) {
        QPainter painter(&img);
        painter.setClipRegion(painted);
        for (const QRect &r : painted)
            painter.fillRect(r, Qt::black);
        if (l.dest.intersects(painted.boundingRect())) {
            painter.setRenderHint(QPainter::SmoothPixmapTransform, video_filter_method > 0);
            /* The frame's alpha bytes are not set; adding it onto black ignores them
               and leaves the result opaque, so the GUI thread can copy it as is. */
            painter.setCompositionMode(QPainter::CompositionMode_Plus);
            painter.drawImage(QRectF(l.dest), *images[cur_image], QRectF(wsource));
        }
    }
    img.setDevicePixelRatio(l.ratio);
    return painted;
}

void
SoftwareRenderer::presentBuffer(int back, const QRegion &painted)
{
    std::scoped_lock lk(idx_mutex);
    front       = back;
    stale[back] = QRegion();
    stale[back ^ 1] |= painted;
}

SoftwareRenderer::Layout
SoftwareRenderer::currentLayout()
{
    std::scoped_lock lk(layout_mutex);
    return layout;
}

int
SoftwareRenderer::frontBuffer()
{
    std::scoped_lock lk(idx_mutex);
    return front;
}

void
SoftwareRenderer::takeScreenshots(int x, int y, int w, int h)
{
    if (monitors[r_monitor_index].mon_screenshots) {
        char path[1024];
        char fn[256];

        memset(fn, 0, sizeof(fn));
        memset(path, 0, sizeof(path));

        path_append_filename(path, usr_path, SCREENSHOT_PATH);

        if (!plat_dir_check(path))
            plat_dir_create(path);

        path_slash(path);
        strcat(path, "Monitor_");
        snprintf(&path[strlen(path)], 42, "%d_", r_monitor_index + 1);

        plat_tempfile(fn, NULL, (char *) ".png");
        strcat(path, fn);

        qreal win_scale = util::screenOfWidget(this)->devicePixelRatio();
        QSize qs = RendererCommon::parentWidget->size();
        QPixmap pixmap(RendererCommon::parentWidget->size());
        RendererCommon::parentWidget->render(&pixmap);
        QImage image;
        if (win_scale == 1.0)
            image = pixmap.toImage();
        else
            image = pixmap.toImage().scaled(qs * win_scale, Qt::IgnoreAspectRatio,
                                            Qt::SmoothTransformation);
        image.save(path, "png");
        monitors[r_monitor_index].mon_screenshots--;
    }
    if (monitors[r_monitor_index].mon_screenshots_clipboard) {
        qreal win_scale = util::screenOfWidget(this)->devicePixelRatio();
        QSize qs = RendererCommon::parentWidget->size();
        QPixmap pixmap(RendererCommon::parentWidget->size());
        RendererCommon::parentWidget->render(&pixmap);
        QImage image;
        if (win_scale == 1.0)
            image = pixmap.toImage();
        else
            image = pixmap.toImage().scaled(qs * win_scale, Qt::IgnoreAspectRatio,
                                            Qt::SmoothTransformation);
        util::copyImageToClipboard(image);
        monitors[r_monitor_index].mon_screenshots_clipboard--;
    }
    if (monitors[r_monitor_index].mon_screenshots_raw_clipboard) {
        take_screenshot_clipboard_monitor(x, y, w, h, r_monitor_index);
    }
}

void
SoftwareRenderer::layoutPicture()
{
    /* Lay out in device pixels, like the other renderers. */
    pixelRatio = devicePixelRatioF();
    onResize(width(), height());

    publishLayout();
    /* Repaint the whole picture at the new layout, even while paused. */
    if (worker.context())
        QMetaObject::invokeMethod(worker.context(), [this] { scaleFrame(QRegion(), true); }, Qt::QueuedConnection);
}

void
SoftwareRenderer::publishLayout()
{
    std::scoped_lock lk(layout_mutex);
    layout.dest  = destination;
    layout.size  = QSize(qRound(width() * pixelRatio), qRound(height() * pixelRatio));
    layout.ratio = pixelRatio;
    layout.gen++;
}

void
SoftwareRenderer::resizeEvent(QResizeEvent *event)
{
    layoutPicture();
    QWidget::resizeEvent(event);
}

bool
SoftwareRenderer::event(QEvent *event)
{
    bool res = false;
    if (!eventDelegate(event, res))
        return QWidget::event(event);
    return res;
}

void
SoftwareRenderer::onPaint(QPaintDevice *device)
{
    const bool osd   = qt_osd_needs_render();
    const int  shown = frontBuffer();
    /* Repaint when the OSD is up, or once more right after it closes so a
     * lingering overlay is cleared even while the machine is paused. */
    if (shown == -1 && !osd && !osd_drawn_last)
        return;

    QPainter painter(device);
    if ((shown == -1) || !drawFront(painter, device))
        painter.fillRect(0, 0, device->width(), device->height(), Qt::black);

    if (osd) {
        painter.setRenderHint(QPainter::SmoothPixmapTransform, false);
        qt_osd_set_layout_scale_hint(osdLayoutScaleHint());
        const QImage *frame = qt_osd_render_software(width(), height(), devicePixelRatioF());
        if (frame)
            painter.drawImage(QPointF(0, 0), *frame);
    }
    painter.end();
    osd_drawn_last = osd;

    /* The OSD animates and reacts to input even when the machine is paused, so
     * keep refreshing while it is on screen. */
    if (qt_osd_needs_render() && !dopause)
        QTimer::singleShot(16, this, [this] { update(); });
}

bool
SoftwareRenderer::drawFront(QPainter &painter, const QPaintDevice *device)
{
    /* Lock the front buffer while still holding the index, so that the worker
       cannot pick it as its back buffer in between. The worker never writes
       the front buffer, so this only waits if it is about to reuse it. */
    std::unique_lock idx(idx_mutex);
    std::scoped_lock lk(scaled_mutex[front]);
    const QImage    &img = scaled[front];
    idx.unlock();

    const qreal ratio = img.devicePixelRatio();
    if (img.size() != QSize(qRound(device->width() * ratio), qRound(device->height() * ratio)))
        return false;
    painter.setCompositionMode(QPainter::CompositionMode_Source);
    painter.drawImage(QPointF(0, 0), img);
    painter.setCompositionMode(QPainter::CompositionMode_SourceOver);
    return true;
}

std::vector<std::tuple<uint8_t *, std::atomic_flag *>>
SoftwareRenderer::getBuffers()
{
    std::vector<std::tuple<uint8_t *, std::atomic_flag *>> buffers;

    buffers.push_back(std::make_tuple(images[0]->bits(), &buf_usage[0]));
    buffers.push_back(std::make_tuple(images[1]->bits(), &buf_usage[1]));

    return buffers;
}
