#include "qt_rendererstack.hpp"
#include "ui_qt_rendererstack.h"

#include "qt_softwarerenderer.hpp"
#include "qt_hardwarerenderer.hpp"

#include <QScreen>

#ifdef __APPLE__
#include <CoreGraphics/CoreGraphics.h>
#endif

extern "C"
{
#include <86box/mouse.h>
#include <86box/plat.h>
#include <86box/video.h>
}

RendererStack::RendererStack(QWidget *parent) :
    QStackedWidget(parent),
    ui(new Ui::RendererStack)
{
    ui->setupUi(this);
    imagebufs = QVector<QImage>(2);
    imagebufs[0] = QImage{QSize(2048 + 64, 2048 + 64), QImage::Format_RGB32};
    imagebufs[1] = QImage{QSize(2048 + 64, 2048 + 64), QImage::Format_RGB32};
}

RendererStack::~RendererStack()
{
    delete ui;
}

extern "C" void macos_poll_mouse();
void
qt_mouse_capture(int on)
{
    if (!on)
    {
        mouse_capture = 0;
        QApplication::setOverrideCursor(Qt::ArrowCursor);
#ifdef __APPLE__
        CGAssociateMouseAndMouseCursorPosition(true);
#endif
        return;
    }
    mouse_capture = 1;
    QApplication::setOverrideCursor(Qt::BlankCursor);
#ifdef __APPLE__
    CGAssociateMouseAndMouseCursorPosition(false);
#endif
    return;
}

void RendererStack::mousePoll()
{
#ifdef __APPLE__
    return macos_poll_mouse();
#else
    mouse_x = mousedata.deltax;
    mouse_y = mousedata.deltay;
    mouse_z = mousedata.deltaz;
    mousedata.deltax = mousedata.deltay = mousedata.deltaz = 0;
    mouse_buttons = mousedata.mousebuttons;
#ifdef WAYLAND
    if (QApplication::platformName().contains("wayland"))
        wl_mouse_poll();
#endif
#endif
}

void RendererStack::mouseReleaseEvent(QMouseEvent *event)
{
    if (this->geometry().contains(event->pos()) && event->button() == Qt::LeftButton && !mouse_capture)
    {
        plat_mouse_capture(1);
        this->setCursor(Qt::BlankCursor);
        return;
    }
    if (mouse_capture && event->button() == Qt::MiddleButton && mouse_get_buttons() < 3)
    {
        plat_mouse_capture(0);
        this->setCursor(Qt::ArrowCursor);
        return;
    }
    if (mouse_capture)
    {
        mousedata.mousebuttons &= ~event->button();
    }
}
void RendererStack::mousePressEvent(QMouseEvent *event)
{
    if (mouse_capture)
    {
        mousedata.mousebuttons |= event->button();
    }
    event->accept();
}
void RendererStack::wheelEvent(QWheelEvent *event)
{
    if (mouse_capture)
    {
        mousedata.deltaz += event->pixelDelta().y();
    }
}

int ignoreNextMouseEvent = 1;
void RendererStack::mouseMoveEvent(QMouseEvent *event)
{
    if (QApplication::platformName().contains("wayland"))
    {
        event->accept();
        return;
    }
    if (!mouse_capture) { event->ignore(); return; }
#ifdef __APPLE__
    event->accept();
    return;
#else
    static QPoint oldPos = QCursor::pos();
    if (ignoreNextMouseEvent) { oldPos = event->pos(); ignoreNextMouseEvent--; event->accept(); return; }
    mousedata.deltax += event->pos().x() - oldPos.x();
    mousedata.deltay += event->pos().y() - oldPos.y();
    if (event->globalPos().x() == 0 || event->globalPos().y() == 0) leaveEvent((QEvent*)event);
    if (event->globalPos().x() == screen()->geometry().width() || event->globalPos().y() == screen()->geometry().height()) leaveEvent((QEvent*)event);
    oldPos = event->pos();
#endif
}

void RendererStack::leaveEvent(QEvent* event)
{
    if (QApplication::platformName().contains("wayland"))
    {
        event->accept();
        return;
    }
    if (!mouse_capture) return;
    QCursor::setPos(mapToGlobal(QPoint(width() / 2, height() / 2)));
    ignoreNextMouseEvent = 2;
    event->accept();
}

// called from blitter thread
void RendererStack::blit(int x, int y, int w, int h)
{
    if ((w <= 0) || (h <= 0) || (w > 2048) || (h > 2048) || (buffer32 == NULL))
    {
        video_blit_complete();
        return;
    }
    sx = x;
    sy = y;
    sw = this->w = w;
    sh = this->h = h;
    auto imagebits = imagebufs[currentBuf].bits();
    video_copy(imagebits + y * ((2048 + 64) * 4) + x * 4, &(buffer32->line[y][x]), h * (2048 + 64) * sizeof(uint32_t));

    if (screenshots)
    {
        video_screenshot((uint32_t *)imagebits, 0, 0, 2048 + 64);
    }
    video_blit_complete();
    blitToRenderer(imagebufs[currentBuf], sx, sy, sw, sh);
    currentBuf = (currentBuf + 1) % 2;
}

void RendererStack::on_RendererStack_currentChanged(int arg1) {
    disconnect(this, &RendererStack::blitToRenderer, nullptr, nullptr);
    switch (arg1) {
    case 0:
        connect(this, &RendererStack::blitToRenderer, dynamic_cast<SoftwareRenderer*>(currentWidget()), &SoftwareRenderer::onBlit);
        break;
    case 1:
    case 2:
        connect(this, &RendererStack::blitToRenderer, dynamic_cast<HardwareRenderer*>(currentWidget()), &HardwareRenderer::onBlit);
        break;
    }
}
