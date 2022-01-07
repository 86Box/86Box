#include "qt_softwarerenderer.hpp"
#include <QApplication>

SoftwareRenderer::SoftwareRenderer(QWidget *parent) : QRasterWindow(parent->windowHandle()) { parentWidget = parent; }

void SoftwareRenderer::paintEvent(QPaintEvent *event) {
    (void) event;
    onPaint(this);
}

void SoftwareRenderer::onBlit(const std::unique_ptr<uint8_t>* img, int x, int y, int w, int h, std::atomic_flag* in_use) {
    auto tval = this;
    void* nuldata = 0;
    if (memcmp(&tval, &nuldata, sizeof(void*)) == 0) return;
    memcpy(image.bits(), img->get(), 2048 * 2048 * 4);
    in_use->clear();
    source.setRect(x, y, w, h);
    update();
}

void SoftwareRenderer::resizeEvent(QResizeEvent *event) {
    onResize(width(), height());
    QRasterWindow::resizeEvent(event);
}

bool SoftwareRenderer::event(QEvent *event)
{
    bool res = false;
    if (!eventDelegate(event, res)) return QRasterWindow::event(event);
    return res;
}
