#include <QApplication>
#include <QImage>
#include <QGuiApplication>
#include <qnamespace.h>
#include "qt_gleswidget.hpp"
#ifdef __APPLE__
#include <CoreGraphics/CoreGraphics.h>
#endif

extern "C"
{
#include <86box/mouse.h>
#include <86box/plat.h>
#include <86box/video.h>
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

void GLESWidget::qt_mouse_poll()
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
    if (wayland)
        wl_mouse_poll();
#endif
#endif
}

void GLESWidget::resizeGL(int w, int h)
{
    glViewport(0, 0, w, h);
}

void GLESWidget::initializeGL()
{
    initializeOpenGLFunctions();
    connect(this, &GLESWidget::reqUpdate, this, &GLESWidget::reqUpdate_);
}

void GLESWidget::paintGL()
{
    QPainter painter(this);
    painter.drawImage(QRect(0, 0, width(), height()), m_image.convertToFormat(QImage::Format_RGBX8888), QRect(sx, sy, sw, sh));
    painter.end();
    firstupdate = true;
}

void GLESWidget::reqUpdate_()
{
    update();
}

void GLESWidget::mouseReleaseEvent(QMouseEvent *event)
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
void GLESWidget::mousePressEvent(QMouseEvent *event)
{
    if (mouse_capture)
    {
        mousedata.mousebuttons |= event->button();
    }
    event->accept();
}
void GLESWidget::wheelEvent(QWheelEvent *event)
{
    if (mouse_capture)
    {
        mousedata.deltay += event->pixelDelta().y();
    }
}

int ignoreNextMouseEvent = 0;
void GLESWidget::mouseMoveEvent(QMouseEvent *event)
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
    QCursor::setPos(mapToGlobal(QPoint(width() / 2, height() / 2)));
    oldPos = event->pos();
    ignoreNextMouseEvent = 1;
#endif
}

void GLESWidget::qt_real_blit(int x, int y, int w, int h)
{
    if ((w <= 0) || (h <= 0) || (w > 2048) || (h > 2048) || (buffer32 == NULL) || !firstupdate)
    {
        video_blit_complete();
        return;
    }
    sx = x;
    sy = y;
    sw = this->w = w;
    sh = this->h = h;
    static auto imagebits = m_image.bits();
    for (int y1 = y; y1 < (y + h - 1); y1++)
    {
        auto scanline = imagebits + (y1 * (2048 + 64) * 4);
        video_copy(scanline + (x * 4), &(buffer32->line[y1][x]), w * 4);
    }
    if (screenshots)
    {
        video_screenshot((uint32_t *)imagebits, 0, 0, 2048 + 64);
    }
    video_blit_complete();
    this->reqUpdate();
}
