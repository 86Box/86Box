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
#include "qt_vulkanwindowrenderer.hpp"
#ifdef Q_OS_WIN
#include "qt_d3d9renderer.hpp"
#endif

#include "qt_mainwindow.hpp"
#include "qt_util.hpp"

#include "ui_qt_mainwindow.h"

#include "evdev_mouse.hpp"

#include <atomic>
#include <stdexcept>

#include <QScreen>
#include <QMessageBox>

#ifdef __APPLE__
#    include <CoreGraphics/CoreGraphics.h>
#endif

extern "C" {
#include <86box/86box.h>
#include <86box/config.h>
#include <86box/mouse.h>
#include <86box/plat.h>
#include <86box/video.h>

double mouse_sensitivity = 1.0;
double mouse_x_error = 0.0, mouse_y_error = 0.0;
}

struct mouseinputdata {
    atomic_int deltax, deltay, deltaz;
    atomic_int mousebuttons;
};
static mouseinputdata mousedata;

extern "C" void macos_poll_mouse();
extern MainWindow *main_window;
RendererStack::RendererStack(QWidget *parent, int monitor_index)
    : QStackedWidget(parent)
    , ui(new Ui::RendererStack)
{
    ui->setupUi(this);

    m_monitor_index = monitor_index;
#if defined __unix__ && !defined __HAIKU__
    char *mouse_type = getenv("EMU86BOX_MOUSE"), auto_mouse_type[16];
    if (!mouse_type || (mouse_type[0] == '\0') || !stricmp(mouse_type, "auto")) {
        if (QApplication::platformName().contains("wayland"))
            strcpy(auto_mouse_type, "wayland");
        else if (QApplication::platformName() == "eglfs")
            strcpy(auto_mouse_type, "evdev");
        else if (QApplication::platformName() == "xcb")
            strcpy(auto_mouse_type, "xinput2");
        else
            auto_mouse_type[0] = '\0';
        mouse_type = auto_mouse_type;
    }

#    ifdef WAYLAND
    if (!stricmp(mouse_type, "wayland")) {
        wl_init();
        this->mouse_poll_func = wl_mouse_poll;
        this->mouse_capture_func = wl_mouse_capture;
        this->mouse_uncapture_func = wl_mouse_uncapture;
    }
#    endif
#    ifdef EVDEV_INPUT
    if (!stricmp(mouse_type, "evdev")) {
        evdev_init();
        this->mouse_poll_func = evdev_mouse_poll;
    }
#    endif
    if (!stricmp(mouse_type, "xinput2")) {
        extern void xinput2_init();
        extern void xinput2_poll();
        extern void xinput2_exit();
        xinput2_init();
        this->mouse_poll_func = xinput2_poll;
        this->mouse_exit_func = xinput2_exit;
    }
#endif
#ifdef __APPLE__
    this->mouse_poll_func = macos_poll_mouse;
#endif
}

RendererStack::~RendererStack()
{
    delete ui;
}

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
#ifndef __APPLE__
    mouse_x          = mousedata.deltax;
    mouse_y          = mousedata.deltay;
    mouse_z          = mousedata.deltaz;
    mousedata.deltax = mousedata.deltay = mousedata.deltaz = 0;
    mouse_buttons    = mousedata.mousebuttons;

    if (this->mouse_poll_func)
#endif
        this->mouse_poll_func();

    double scaled_x = mouse_x * mouse_sensitivity + mouse_x_error;
    double scaled_y = mouse_y * mouse_sensitivity + mouse_y_error;

    mouse_x = static_cast<int>(scaled_x);
    mouse_y = static_cast<int>(scaled_y);

    mouse_x_error = scaled_x - mouse_x;
    mouse_y_error = scaled_y - mouse_y;
}

int ignoreNextMouseEvent = 1;
void
RendererStack::mouseReleaseEvent(QMouseEvent *event)
{
    if (this->geometry().contains(event->pos()) && event->button() == Qt::LeftButton && !mouse_capture && (isMouseDown & 1) && (mouse_get_buttons() != 0)) {
        plat_mouse_capture(1);
        this->setCursor(Qt::BlankCursor);
        if (!ignoreNextMouseEvent)
            ignoreNextMouseEvent++; // Avoid jumping cursor when moved.
        isMouseDown &= ~1;
        return;
    }
    if (mouse_capture && (event->button() == Qt::MiddleButton) && (mouse_get_buttons() < 3)) {
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
#if defined __APPLE__ || defined _WIN32
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
        if ((current_vid_api == Renderer::Direct3D9 && renderer != Renderer::Direct3D9)
        || (current_vid_api != Renderer::Direct3D9 && renderer == Renderer::Direct3D9)) {
            rendererWindow->finalize();
            if (rendererWindow->hasBlitFunc()) {
                while (directBlitting) {}
                connect(this, &RendererStack::blit, this, &RendererStack::blitDummy, Qt::DirectConnection);
                disconnect(this, &RendererStack::blit, this, &RendererStack::blitRenderer);
            } else {
                connect(this, &RendererStack::blit, this, &RendererStack::blitDummy, Qt::DirectConnection);
                disconnect(this, &RendererStack::blit, this, &RendererStack::blitCommon);
            }

            removeWidget(current.get());
            disconnect(this, &RendererStack::blitToRenderer, nullptr, nullptr);

            /* Create new renderer only after previous is destroyed! */
            connect(current.get(), &QObject::destroyed, [this, renderer](QObject *) {
                createRenderer(renderer);
                disconnect(this, &RendererStack::blit, this, &RendererStack::blitDummy);
                blitDummied = false;
                QTimer::singleShot(1000, this, [this]() { blitDummied = false; } );
            });

            rendererWindow->hasBlitFunc() ? current.reset() : current.release()->deleteLater();
        } else {
            rendererWindow->finalize();
            removeWidget(current.get());
            disconnect(this, &RendererStack::blitToRenderer, nullptr, nullptr);

            /* Create new renderer only after previous is destroyed! */
            connect(current.get(), &QObject::destroyed, [this, renderer](QObject *) { createRenderer(renderer); });

            current.release()->deleteLater();
        }
    } else {
        createRenderer(renderer);
    }
}

void
RendererStack::createRenderer(Renderer renderer)
{
    current_vid_api = renderer;
    switch (renderer) {
        default:
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
                    /* Buffers are available only after initialization. */
                    imagebufs = rendererWindow->getBuffers();
                    endblit();
                    emit rendererChanged();
                });
                connect(hw, &OpenGLRenderer::errorInitializing, [=]() {
                    /* Renderer not could initialize, fallback to software. */
                    imagebufs = {};
                    QTimer::singleShot(0, this, [this]() { switchRenderer(Renderer::Software); });
                });
                current.reset(this->createWindowContainer(hw, this));
                break;
            }
#ifdef Q_OS_WIN
        case Renderer::Direct3D9:
        {
            this->createWinId();
            auto hw = new D3D9Renderer(this, m_monitor_index);
            rendererWindow = hw;
            connect(hw, &D3D9Renderer::error, this, [this](QString str)
            {
                auto msgBox = new QMessageBox(QMessageBox::Critical, "86Box", QString("Failed to initialize D3D9 renderer. Falling back to software rendering.\n\n") + str, QMessageBox::Ok);
                msgBox->setAttribute(Qt::WA_DeleteOnClose);
                msgBox->show();
                imagebufs = {};
                QTimer::singleShot(0, this, [this]() { switchRenderer(Renderer::Software); });
            });
            connect(hw, &D3D9Renderer::initialized, this, [this]()
            {
                endblit();
                emit rendererChanged();
            });
            current.reset(hw);
            break;
        }
#endif
#if QT_CONFIG(vulkan)
        case Renderer::Vulkan:
        {
            this->createWinId();
            VulkanWindowRenderer *hw = nullptr;
            try {
                hw        = new VulkanWindowRenderer(this);
            } catch(std::runtime_error& e) {
                auto msgBox = new QMessageBox(QMessageBox::Critical, "86Box", e.what() + QString("\nFalling back to software rendering."), QMessageBox::Ok);
                msgBox->setAttribute(Qt::WA_DeleteOnClose);
                msgBox->show();
                imagebufs = {};
                QTimer::singleShot(0, this, [this]() { switchRenderer(Renderer::Software); });
                current.reset(nullptr);
                break;
            };
            rendererWindow = hw;
            connect(this, &RendererStack::blitToRenderer, hw, &VulkanWindowRenderer::onBlit, Qt::QueuedConnection);
            connect(hw, &VulkanWindowRenderer::rendererInitialized, [=]() {
                /* Buffers are available only after initialization. */
                imagebufs = rendererWindow->getBuffers();
                endblit();
                emit rendererChanged();
            });
            connect(hw, &VulkanWindowRenderer::errorInitializing, [=]() {
                /* Renderer could not initialize, fallback to software. */
                auto msgBox = new QMessageBox(QMessageBox::Critical, "86Box", QString("Failed to initialize Vulkan renderer.\nFalling back to software rendering."), QMessageBox::Ok);
                msgBox->setAttribute(Qt::WA_DeleteOnClose);
                msgBox->show();
                imagebufs = {};
                QTimer::singleShot(0, this, [this]() { switchRenderer(Renderer::Software); });
            });
            current.reset(this->createWindowContainer(hw, this));
            break;
        }
#endif
    }
    if (current.get() == nullptr) return;
    current->setFocusPolicy(Qt::NoFocus);
    current->setFocusProxy(this);
    current->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    addWidget(current.get());

    this->setStyleSheet("background-color: black");

    currentBuf = 0;

    if (rendererWindow->hasBlitFunc()) {
        connect(this, &RendererStack::blit, this, &RendererStack::blitRenderer, Qt::DirectConnection);
    }
    else {
        connect(this, &RendererStack::blit, this, &RendererStack::blitCommon, Qt::DirectConnection);
    }

    if (renderer != Renderer::OpenGL3 && renderer != Renderer::Vulkan && renderer != Renderer::Direct3D9) {
        imagebufs = rendererWindow->getBuffers();
        endblit();
        emit rendererChanged();
    }
}

void
RendererStack::blitDummy(int x, int y, int w, int h)
{
    video_blit_complete_monitor(m_monitor_index);
    blitDummied = true;
}

void
RendererStack::blitRenderer(int x, int y, int w, int h)
{
    if (blitDummied) { blitDummied = false; video_blit_complete_monitor(m_monitor_index); return; }
    directBlitting = true;
    rendererWindow->blit(x, y, w, h);
    directBlitting = false;
}

// called from blitter thread
void
RendererStack::blitCommon(int x, int y, int w, int h)
{
    if (blitDummied || (x < 0) || (y < 0) || (w <= 0) || (h <= 0) || (w > 2048) || (h > 2048) || (monitors[m_monitor_index].target_buffer == NULL) || imagebufs.empty() || std::get<std::atomic_flag *>(imagebufs[currentBuf])->test_and_set()) {
        video_blit_complete_monitor(m_monitor_index);
        return;
    }
    sx = x;
    sy = y;
    sw = this->w = w;
    sh = this->h       = h;
    uint8_t *imagebits = std::get<uint8_t *>(imagebufs[currentBuf]);
    for (int y1 = y; y1 < (y + h); y1++) {
        auto scanline = imagebits + (y1 * rendererWindow->getBytesPerRow()) + (x * 4);
        video_copy(scanline, &(monitors[m_monitor_index].target_buffer->line[y1][x]), w * 4);
    }

    if (monitors[m_monitor_index].mon_screenshots) {
        video_screenshot_monitor((uint32_t *) imagebits, x, y, 2048, m_monitor_index);
    }
    video_blit_complete_monitor(m_monitor_index);
    emit blitToRenderer(currentBuf, sx, sy, sw, sh);
    currentBuf = (currentBuf + 1) % imagebufs.size();
}

void RendererStack::closeEvent(QCloseEvent* event)
{
    if (cpu_thread_run == 1 || is_quit == 0) {
        event->accept();
        main_window->ui->actionShow_non_primary_monitors->setChecked(false);
        return;
    }
    event->ignore();
    main_window->close();
}

void RendererStack::changeEvent(QEvent *event)
{
    if (m_monitor_index != 0 && isVisible()) {
        monitor_settings[m_monitor_index].mon_window_maximized = isMaximized();
        config_save();
    }
}
