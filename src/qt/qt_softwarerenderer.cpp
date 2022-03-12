/*
 * 86Box	A hypervisor and IBM PC system emulator that specializes in
 *		running old operating systems and software designed for IBM
 *		PC systems and compatibles from 1981 through fairly recent
 *		system designs based on the PCI bus.
 *
 *		This file is part of the 86Box distribution.
 *
 *		Software renderer module.
 *
 *
 *
 * Authors:	Joakim L. Gilje <jgilje@jgilje.net>
 *          Cacodemon345
 *          Teemu Korhonen
 *
 *		Copyright 2021 Joakim L. Gilje
 *      Copyright 2021-2022 Cacodemon345
 *      Copyright 2021-2022 Teemu Korhonen
 */
#include "qt_softwarerenderer.hpp"
#include <QApplication>
#include <QPainter>

extern "C" {
#include <86box/86box.h>
#include <86box/video.h>
}

SoftwareRenderer::SoftwareRenderer(QWidget *parent)
#ifdef __HAIKU__
    : QWidget(parent)
#else
    : QRasterWindow(parent->windowHandle())
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

void SoftwareRenderer::paintEvent(QPaintEvent* event) {
    (void)event;
    onPaint(this);
}

void SoftwareRenderer::onBlit(int buf_idx, int x, int y, int w, int h) {
    /* TODO: should look into deleteLater() */
    auto tval = this;
    void* nuldata = 0;
    if (memcmp(&tval, &nuldata, sizeof(void*)) == 0) return;

    cur_image = buf_idx;
    buf_usage[(buf_idx + 1) % 2].clear();
    
    source.setRect(x, y, w, h);
    update();
}

void SoftwareRenderer::resizeEvent(QResizeEvent *event) {
    onResize(width(), height());
#ifdef __HAIKU__
    QWidget::resizeEvent(event);
#else
    QRasterWindow::resizeEvent(event);
#endif
}

bool SoftwareRenderer::event(QEvent *event)
{
    bool res = false;
    if (!eventDelegate(event, res))
#ifdef __HAIKU__
        return QWidget::event(event);
#else
        return QRasterWindow::event(event);
#endif
    return res;
}

void SoftwareRenderer::onPaint(QPaintDevice* device) {
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
}

std::vector<std::tuple<uint8_t*, std::atomic_flag*>> SoftwareRenderer::getBuffers()
{
    std::vector<std::tuple<uint8_t*, std::atomic_flag*>> buffers;

    buffers.push_back(std::make_tuple(images[0]->bits(), &buf_usage[0]));
    buffers.push_back(std::make_tuple(images[1]->bits(), &buf_usage[1]));

    return buffers;
}
