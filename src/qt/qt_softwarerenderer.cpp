#include "qt_softwarerenderer.hpp"

SoftwareRenderer::SoftwareRenderer(QWidget *parent) : QWidget(parent) {}

void SoftwareRenderer::paintEvent(QPaintEvent *event) {
    (void) event;
    onPaint(this);
}

void SoftwareRenderer::onBlit(const QImage& img, int x, int y, int w, int h, std::atomic_flag* in_use) {
    image = img;
    source.setRect(x, y, w, h);
    update();
    in_use->clear();
}

void SoftwareRenderer::resizeEvent(QResizeEvent *event) {
    onResize(width(), height());
    QWidget::resizeEvent(event);
}
