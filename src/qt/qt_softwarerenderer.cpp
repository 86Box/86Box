#include "qt_softwarerenderer.hpp"

SoftwareRenderer::SoftwareRenderer(QWidget *parent) : QWidget(parent) {}

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
    QWidget::resizeEvent(event);
}
