/*
 * 86Box	A hypervisor and IBM PC system emulator that specializes in
 *		running old operating systems and software designed for IBM
 *		PC systems and compatibles from 1981 through fairly recent
 *		system designs based on the PCI bus.
 *
 *		This file is part of the 86Box distribution.
 *
 *		Program settings UI module.
 *
 *
 *
 * Authors: Joakim L. Gilje <jgilje@jgilje.net>
 *          Cacodemon345
 *          Teemu Korhonen
 *
 *      Copyright 2021 Joakim L. Gilje
 *      Copyright 2021-2021 Teemu Korhonen
 *      Copyright 2021-2022 Cacodemon345
 */
#include "qt_rendererstack.hpp"
#include "ui_qt_rendererstack.h"

#include "qt_hardwarerenderer.hpp"
#include "qt_openglrenderer.hpp"
#include "qt_softwarerenderer.hpp"

#include "qt_mainwindow.hpp"
#include "qt_util.hpp"

#include "evdev_mouse.hpp"

#include <QScreen>

#ifdef __APPLE__
#    include <CoreGraphics/CoreGraphics.h>
#endif

extern "C" {
#include <86box/mouse.h>
#include <86box/plat.h>
#include <86box/video.h>
}

extern MainWindow *main_window;
RendererStack::RendererStack(QWidget *parent)
    : QStackedWidget(parent)
    , ui(new Ui::RendererStack)
{
    ui->setupUi(this);

#if defined __unix__ && !defined __HAIKU__
#    ifdef WAYLAND
    if (QApplication::platformName().contains("wayland")) {
        wl_init();
    }
#    endif
#    ifdef EVDEV_INPUT
    if (QApplication::platformName() == "eglfs") {
        evdev_init();
    }
#    endif
    if (QApplication::platformName() == "xcb") {
        extern void xinput2_init();
        xinput2_init();
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
    if (!on) {
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

void
RendererStack::mousePoll()
{
#ifdef __APPLE__
    return macos_poll_mouse();
#else /* !defined __APPLE__ */
    mouse_x          = mousedata.deltax;
    mouse_y          = mousedata.deltay;
    mouse_z          = mousedata.deltaz;
    mousedata.deltax = mousedata.deltay = mousedata.deltaz = 0;
    mouse_buttons                                          = mousedata.mousebuttons;

#    if defined __unix__ && !defined __HAIKU__
#        ifdef WAYLAND
    if (QApplication::platformName().contains("wayland"))
        wl_mouse_poll();
#        endif

#        ifdef EVDEV_INPUT
    if (QApplication::platformName() == "eglfs")
        evdev_mouse_poll();
    else
#        endif
        if (QApplication::platformName() == "xcb") {
        extern void xinput2_poll();
        xinput2_poll();
    }
#    endif /* defined __unix__ */
#endif     /* !defined __APPLE__ */
}

int ignoreNextMouseEvent = 1;
void
RendererStack::mouseReleaseEvent(QMouseEvent *event)
{
    if (this->geometry().contains(event->pos()) && event->button() == Qt::LeftButton && !mouse_capture && (isMouseDown & 1)) {
        plat_mouse_capture(1);
        this->setCursor(Qt::BlankCursor);
        if (!ignoreNextMouseEvent)
            ignoreNextMouseEvent++; // Avoid jumping cursor when moved.
        isMouseDown &= ~1;
        return;
    }
    if (mouse_capture && event->button() == Qt::MiddleButton && mouse_get_buttons() < 3) {
        plat_mouse_capture(0);
        this->setCursor(Qt::ArrowCursor);
        isMouseDown &= ~1;
        return;
    }
    if (mouse_capture) {
        mousedata.mousebuttons &= ~event->button();
    }
    isMouseDown &= ~1;
}
void
RendererStack::mousePressEvent(QMouseEvent *event)
{
    isMouseDown |= 1;
    if (mouse_capture) {
        mousedata.mousebuttons |= event->button();
    }
    event->accept();
}
void
RendererStack::wheelEvent(QWheelEvent *event)
{
    if (mouse_capture) {
        mousedata.deltaz += event->pixelDelta().y();
    }
}

void
RendererStack::mouseMoveEvent(QMouseEvent *event)
{
    if (QApplication::platformName().contains("wayland")) {
        event->accept();
        return;
    }
    if (!mouse_capture) {
        event->ignore();
        return;
    }
#ifdef __APPLE__
    event->accept();
    return;
#else
    static QPoint oldPos = QCursor::pos();
    if (ignoreNextMouseEvent) {
        oldPos = event->pos();
        ignoreNextMouseEvent--;
        event->accept();
        return;
    }
    mousedata.deltax += event->pos().x() - oldPos.x();
    mousedata.deltay += event->pos().y() - oldPos.y();
    if (QApplication::platformName() == "eglfs") {
        leaveEvent((QEvent *) event);
        ignoreNextMouseEvent--;
    }
    QCursor::setPos(mapToGlobal(QPoint(width() / 2, height() / 2)));
    ignoreNextMouseEvent = 2;
    oldPos               = event->pos();
#endif
}

void
RendererStack::leaveEvent(QEvent *event)
{
    if (QApplication::platformName().contains("wayland")) {
        event->accept();
        return;
    }
    if (!mouse_capture)
        return;
    QCursor::setPos(mapToGlobal(QPoint(width() / 2, height() / 2)));
    ignoreNextMouseEvent = 2;
    event->accept();
}

void
RendererStack::switchRenderer(Renderer renderer)
{
    startblit();
    if (current) {
        rendererWindow->finalize();
        removeWidget(current.get());
        disconnect(this, &RendererStack::blitToRenderer, nullptr, nullptr);

        /* Create new renderer only after previous is destroyed! */
        connect(current.get(), &QObject::destroyed, [this, renderer](QObject *) { createRenderer(renderer); });

        current.release()->deleteLater();
    } else {
        createRenderer(renderer);
    }
}

void
RendererStack::createRenderer(Renderer renderer)
{
    switch (renderer) {
        case Renderer::Software:
            {
                auto sw        = new SoftwareRenderer(this);
                rendererWindow = sw;
                connect(this, &RendererStack::blitToRenderer, sw, &SoftwareRenderer::onBlit, Qt::QueuedConnection);
#ifdef __HAIKU__
                current.reset(sw);
#else
                current.reset(this->createWindowContainer(sw, this));
#endif
            }
            break;
        case Renderer::OpenGL:
            {
                this->createWinId();
                auto hw        = new HardwareRenderer(this);
                rendererWindow = hw;
                connect(this, &RendererStack::blitToRenderer, hw, &HardwareRenderer::onBlit, Qt::QueuedConnection);
                current.reset(this->createWindowContainer(hw, this));
                break;
            }
        case Renderer::OpenGLES:
            {
                this->createWinId();
                auto hw        = new HardwareRenderer(this, HardwareRenderer::RenderType::OpenGLES);
                rendererWindow = hw;
                connect(this, &RendererStack::blitToRenderer, hw, &HardwareRenderer::onBlit, Qt::QueuedConnection);
                current.reset(this->createWindowContainer(hw, this));
                break;
            }
        case Renderer::OpenGL3:
            {
                this->createWinId();
                auto hw        = new OpenGLRenderer(this);
                rendererWindow = hw;
                connect(this, &RendererStack::blitToRenderer, hw, &OpenGLRenderer::onBlit, Qt::QueuedConnection);
                connect(hw, &OpenGLRenderer::initialized, [=]() {
                    /* Buffers are awailable only after initialization. */
                    imagebufs = rendererWindow->getBuffers();
                    endblit();
                    emit rendererChanged();
                });
                connect(hw, &OpenGLRenderer::errorInitializing, [=]() {
                    /* Renderer could initialize, fallback to software. */
                    imagebufs = {};
                    endblit();
                    QTimer::singleShot(0, this, [this]() { switchRenderer(Renderer::Software); });
                });
                current.reset(this->createWindowContainer(hw, this));
                break;
            }
    }

    current->setFocusPolicy(Qt::NoFocus);
    current->setFocusProxy(this);
    addWidget(current.get());

    this->setStyleSheet("background-color: black");

    currentBuf = 0;

    if (renderer != Renderer::OpenGL3) {
        imagebufs = rendererWindow->getBuffers();
        endblit();
        emit rendererChanged();
    }
}

// called from blitter thread
void
RendererStack::blit(int x, int y, int w, int h)
{
    if ((w <= 0) || (h <= 0) || (w > 2048) || (h > 2048) || (buffer32 == NULL) || imagebufs.empty() || std::get<std::atomic_flag *>(imagebufs[currentBuf])->test_and_set()) {
        video_blit_complete();
        return;
    }
    sx = x;
    sy = y;
    sw = this->w = w;
    sh = this->h       = h;
    uint8_t *imagebits = std::get<uint8_t *>(imagebufs[currentBuf]);
    for (int y1 = y; y1 < (y + h); y1++) {
        auto scanline = imagebits + (y1 * (2048) * 4) + (x * 4);
        video_copy(scanline, &(buffer32->line[y1][x]), w * 4);
    }

    if (screenshots) {
        video_screenshot((uint32_t *) imagebits, x, y, 2048);
    }
    video_blit_complete();
    emit blitToRenderer(currentBuf, sx, sy, sw, sh);
    currentBuf = (currentBuf + 1) % imagebufs.size();
}
