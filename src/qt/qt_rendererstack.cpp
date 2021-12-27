#include "qt_rendererstack.hpp"
#include "ui_qt_rendererstack.h"

#include "qt_softwarerenderer.hpp"
#include "qt_hardwarerenderer.hpp"

#include "evdev_mouse.hpp"

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
    imagebufs[0].reset(new uint8_t[2048 * 2048 * 4]);
    imagebufs[1].reset(new uint8_t[2048 * 2048 * 4]);

    buffers_in_use = std::vector<std::atomic_flag>(2);
    buffers_in_use[0].clear();
    buffers_in_use[1].clear();

#ifdef WAYLAND
    if (QApplication::platformName().contains("wayland")) {
        wl_init();
    }
#endif
#ifdef EVDEV_INPUT
    if (QApplication::platformName() == "xcb" || QApplication::platformName() == "eglfs") {
        evdev_init();
    }
#endif
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
#ifdef EVDEV_INPUT
    evdev_mouse_poll();
#endif
#endif
}

int ignoreNextMouseEvent = 1;
void RendererStack::mouseReleaseEvent(QMouseEvent *event)
{
    if (this->geometry().contains(event->pos()) && event->button() == Qt::LeftButton && !mouse_capture)
    {
        plat_mouse_capture(1);
        this->setCursor(Qt::BlankCursor);
        if (!ignoreNextMouseEvent) ignoreNextMouseEvent++; // Avoid jumping cursor when moved.
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
    if (QApplication::platformName() == "eglfs")
    {
        leaveEvent((QEvent*)event);
        ignoreNextMouseEvent--;
    }
    else if (event->globalPos().x() == 0 || event->globalPos().y() == 0) leaveEvent((QEvent*)event);
    else if (event->globalPos().x() == (screen()->geometry().width() - 1) || event->globalPos().y() == (screen()->geometry().height() - 1)) leaveEvent((QEvent*)event);
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

void RendererStack::switchRenderer(Renderer renderer) {
    startblit();
    if (current) {
        removeWidget(current.get());
    }

    switch (renderer) {
    case Renderer::Software:
    {
        auto sw = new SoftwareRenderer(this);
        connect(this, &RendererStack::blitToRenderer, sw, &SoftwareRenderer::onBlit, Qt::QueuedConnection);
        current.reset(sw);
    }
        break;
    case Renderer::OpenGL:
    {
        this->createWinId();
        auto hw = new HardwareRenderer(this);
        connect(this, &RendererStack::blitToRenderer, hw, &HardwareRenderer::onBlit, Qt::QueuedConnection);
        current.reset(this->createWindowContainer(hw, this));
        break;
    }
    case Renderer::OpenGLES:
    {
        this->createWinId();
        auto hw = new HardwareRenderer(this, HardwareRenderer::RenderType::OpenGLES);
        connect(this, &RendererStack::blitToRenderer, hw, &HardwareRenderer::onBlit, Qt::QueuedConnection);
        current.reset(this->createWindowContainer(hw, this));
        break;
    }
    case Renderer::OpenGL3:
    {
        this->createWinId();
        auto hw = new HardwareRenderer(this, HardwareRenderer::RenderType::OpenGL3);
        connect(this, &RendererStack::blitToRenderer, hw, &HardwareRenderer::onBlit, Qt::QueuedConnection);
        current.reset(this->createWindowContainer(hw, this));
        break;
    }
    }
    current->setFocusPolicy(Qt::NoFocus);
    current->setFocusProxy(this);
    addWidget(current.get());

    this->setStyleSheet("background-color: black");
    for (auto& in_use : buffers_in_use)
        in_use.clear();

    endblit();
}

// called from blitter thread
void RendererStack::blit(int x, int y, int w, int h)
{
    if ((w <= 0) || (h <= 0) || (w > 2048) || (h > 2048) || (buffer32 == NULL) || buffers_in_use[currentBuf].test_and_set())
    {
        video_blit_complete();
        return;
    }
    sx = x;
    sy = y;
    sw = this->w = w;
    sh = this->h = h;
    auto imagebits = imagebufs[currentBuf].get();
    video_copy(imagebits + y * (2048 * 4) + x * 4, &(buffer32->line[y][x]), h * 2048 * sizeof(uint32_t));

    if (screenshots)
    {
        video_screenshot((uint32_t *)imagebits, x, y, 2048);
    }
    video_blit_complete();
    blitToRenderer(&imagebufs[currentBuf], sx, sy, sw, sh, &buffers_in_use[currentBuf]);
    currentBuf = (currentBuf + 1) % 2;
}
