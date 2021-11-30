#include <SDL.h>
#include <QApplication>
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

typedef struct mouseinputdata
{
    int deltax, deltay, deltaz;
    int mousebuttons;
} mouseinputdata;

mouseinputdata mousedata;
SDL_mutex* mousemutex;

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

void qt_mouse_poll()
{
    SDL_LockMutex(mousemutex);
    mouse_x = mousedata.deltax;
    mouse_y = mousedata.deltay;
    mouse_z = mousedata.deltaz;
    mousedata.deltax = mousedata.deltay = mousedata.deltaz = 0;
    mouse_buttons = mousedata.mousebuttons;
    SDL_UnlockMutex(mousemutex);
}

void GLESWidget::resizeGL(int w, int h)
{
    glViewport(0, 0, w, h);
}

void GLESWidget::initializeGL()
{
    initializeOpenGLFunctions();
}
void GLESWidget::paintGL()
{
    QPainter painter(this);
    //painter.fillRect(rect, QColor(0, 0, 0));
    painter.drawImage(QRect(0, 0, width(), height()), m_image.convertToFormat(QImage::Format_RGBA8888), QRect(sx, sy, sw, sh));
    painter.end();
    update();
}

void GLESWidget::mouseReleaseEvent(QMouseEvent *event)
{
    if (this->geometry().contains(event->pos()) && event->button() == Qt::LeftButton && !mouse_capture)
    {
        this->grabMouse();
        return;
    }
    if (mouse_capture && event->button() == Qt::MiddleButton && mouse_get_buttons() < 3)
    {
        this->releaseMouse();
        return;
    }
    if (mouse_capture)
    {
        SDL_LockMutex(mousemutex);
        mousedata.mousebuttons &= ~event->button();
        SDL_UnlockMutex(mousemutex);
    }
}
void GLESWidget::mousePressEvent(QMouseEvent *event)
{
    if (mouse_capture)
    {
        SDL_LockMutex(mousemutex);
        mousedata.mousebuttons |= event->button();
        SDL_UnlockMutex(mousemutex);
    }
}
void GLESWidget::wheelEvent(QWheelEvent *event)
{
    if (mouse_capture)
    {
        SDL_LockMutex(mousemutex);
        mousedata.deltay += event->pixelDelta().y();
        SDL_UnlockMutex(mousemutex);
    }
}

int ignoreNextMouseEvent = 0;
void GLESWidget::mouseMoveEvent(QMouseEvent *event)
{
    if (!mouse_capture) { event->ignore(); return; }
#ifdef __APPLE__
    event->accept();
    return;
#else
    static QPoint oldPos = QCursor::pos();
    if (ignoreNextMouseEvent) { oldPos = event->pos(); ignoreNextMouseEvent--; event->accept(); return; }
    SDL_LockMutex(mousemutex);
    mousedata.deltax += event->pos().x() - oldPos.x();
    mousedata.deltay += event->pos().y() - oldPos.y();
    SDL_UnlockMutex(mousemutex);
    QCursor::setPos(mapToGlobal(QPoint(width() / 2, height() / 2)));
    oldPos = event->pos();
    ignoreNextMouseEvent = 1;
#endif
}

void GLESWidget::qt_real_blit(int x, int y, int w, int h)
{
    // printf("Offpainter thread ID: %X\n", SDL_ThreadID());
    if ((w <= 0) || (h <= 0) || (w > 2048) || (h > 2048) || (buffer32 == NULL))
    {
        video_blit_complete();
        return;
    }
    sx = x;
    sy = y;
    sw = this->w = w;
    sh = this->h = h;
    auto imagebits = m_image.bits();
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
}