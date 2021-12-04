#include "qt_softwarerenderer.hpp"

#include <QPainter>

SoftwareRenderer::SoftwareRenderer(QWidget *parent) : QWidget(parent) {}

void SoftwareRenderer::paintEvent(QPaintEvent *event) {
    (void) event;

    QPainter painter(this);
    painter.drawImage(QRect(0, 0, width(), height()), image, QRect(sx, sy, sw, sh));
    image = QImage();
}

void SoftwareRenderer::onBlit(const QImage& img, int x, int y, int w, int h) {
    image = img;
    sx = x;
    sy = y;
    sw = w;
    sh = h;
    update();
}
