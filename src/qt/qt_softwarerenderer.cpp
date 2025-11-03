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

extern "C" {
#include <86box/86box.h>
#include <86box/video.h>
}

SoftwareRenderer::SoftwareRenderer(QWidget *parent)
#ifdef __HAIKU__
    : QWidget(parent)
#else
    : QWindow(parent->windowHandle()), m_backingStore(new QBackingStore(this))
#endif
{
    RendererCommon::parentWidget = parent;

    images[0] = std::make_unique<QImage>(QSize(2048, 2048), QImage::Format_RGB32);
    images[1] = std::make_unique<QImage>(QSize(2048, 2048), QImage::Format_RGB32);

    buf_usage = std::vector<std::atomic_flag>(2);
    buf_usage[0].clear();
    buf_usage[1].clear();
#ifdef __HAIKU__
    this->setMouseTracking(true);
#endif
}

#ifdef __HAIKU__
void
SoftwareRenderer::paintEvent(QPaintEvent *event)
{
    (void) event;
    onPaint(this);
}
#endif

void
SoftwareRenderer::render()
{
    if (!isExposed())
        return;

    QRect rect(0, 0, width(), height());
    m_backingStore->beginPaint(rect);

    QPaintDevice *device = m_backingStore->paintDevice();
    onPaint(device);

    m_backingStore->endPaint();
    m_backingStore->flush(rect);
}

void
SoftwareRenderer::exposeEvent(QExposeEvent* event)
{
    render();
}

void
SoftwareRenderer::onBlit(int buf_idx, int x, int y, int w, int h)
{
    /* TODO: should look into deleteLater() */
    auto  tval    = this;
    if ((void *) tval == nullptr)
        return;
    auto origSource = source;

    cur_image = buf_idx;
    buf_usage[buf_idx ^ 1].clear();

    source.setRect(x, y, w, h);

    if (source != origSource)
        onResize(this->width(), this->height());
#ifdef __HAIKU__
    update();
#else
    render();
#endif
}

void
SoftwareRenderer::resizeEvent(QResizeEvent *event)
{
    onResize(width(), height());
#ifdef __HAIKU__
    QWidget::resizeEvent(event);
#else
    QWindow::resizeEvent(event);
    m_backingStore->resize(event->size());
    render();
#endif
}

bool
SoftwareRenderer::event(QEvent *event)
{
    bool res = false;
    if (!eventDelegate(event, res))
#ifdef __HAIKU__
        return QWidget::event(event);
#else
        return QWindow::event(event);
#endif
    return res;
}

void
SoftwareRenderer::onPaint(QPaintDevice *device)
{
    if (cur_image == -1)
        return;

    QPainter painter(device);
    painter.setRenderHint(QPainter::SmoothPixmapTransform, video_filter_method > 0 ? true : false);
#if QT_VERSION >= QT_VERSION_CHECK(5, 14, 0)
    painter.fillRect(0, 0, device->width(), device->height(), QColorConstants::Black);
#else
    painter.fillRect(0, 0, device->width(), device->height(), Qt::black);
#endif
    painter.setCompositionMode(QPainter::CompositionMode_Plus);
    painter.drawImage(destination, *images[cur_image], source);
#ifndef __HAIKU__
    painter.end();
#endif
}

std::vector<std::tuple<uint8_t *, std::atomic_flag *>>
SoftwareRenderer::getBuffers()
{
    std::vector<std::tuple<uint8_t *, std::atomic_flag *>> buffers;

    buffers.push_back(std::make_tuple(images[0]->bits(), &buf_usage[0]));
    buffers.push_back(std::make_tuple(images[1]->bits(), &buf_usage[1]));

    return buffers;
}
