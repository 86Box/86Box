/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          Program settings UI module.
 *
 *
 *
 * Authors: Joakim L. Gilje <jgilje@jgilje.net>
 *          Cacodemon345
 *          Teemu Korhonen
 *
 *          Copyright 2021 Joakim L. Gilje
 *          Copyright 2021-2021 Teemu Korhonen
 *          Copyright 2021-2022 Cacodemon345
 */
#include "qt_rendererstack.hpp"
#include "ui_qt_rendererstack.h"

#include "qt_hardwarerenderer.hpp"
#include "qt_openglrenderer.hpp"
#include "qt_softwarerenderer.hpp"
#include "qt_vulkanwindowrenderer.hpp"

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
#include <86box/plat.h>
#include <86box/video.h>
#include <86box/mouse.h>
}

struct mouseinputdata {
    atomic_bool         mouse_tablet_in_proximity;

    char                *mouse_type;
};
static mouseinputdata mousedata;

extern MainWindow *main_window;
RendererStack::RendererStack(QWidget *parent, int monitor_index)
    : QStackedWidget(parent)
    , ui(new Ui::RendererStack)
{
#ifdef Q_OS_WINDOWS
    int raw = 1;
#else
    int raw = 0;
#endif

    ui->setupUi(this);

    m_monitor_index = monitor_index;
#if defined __unix__ && !defined __HAIKU__
    char auto_mouse_type[16];
    mousedata.mouse_type = getenv("EMU86BOX_MOUSE");
    if (!mousedata.mouse_type || (mousedata.mouse_type[0] == '\0') || !stricmp(mousedata.mouse_type, "auto")) {
        if (QApplication::platformName().contains("wayland"))
            strcpy(auto_mouse_type, "wayland");
        else if (QApplication::platformName() == "eglfs")
            strcpy(auto_mouse_type, "evdev");
        else if (QApplication::platformName() == "xcb")
            strcpy(auto_mouse_type, "xinput2");
        else
            auto_mouse_type[0] = '\0';
        mousedata.mouse_type = auto_mouse_type;
    }

#    ifdef WAYLAND
    if (!stricmp(mousedata.mouse_type, "wayland")) {
        wl_init();
        this->mouse_capture_func   = wl_mouse_capture;
        this->mouse_uncapture_func = wl_mouse_uncapture;
    }
#    endif
#    ifdef EVDEV_INPUT
    if (!stricmp(mousedata.mouse_type, "evdev")) {
        evdev_init();
        raw = 0;
    }
#    endif
    if (!stricmp(mousedata.mouse_type, "xinput2")) {
        extern void xinput2_init();
        extern void xinput2_exit();
        xinput2_init();
        this->mouse_exit_func = xinput2_exit;
    }
#endif

    if (monitor_index == 0)
        mouse_set_raw(raw);
}

RendererStack::~RendererStack()
{
    while (QApplication::overrideCursor()) 
        QApplication::restoreOverrideCursor();
    delete ui;
}

void
qt_mouse_capture(int on)
{
    if (!on) {
        mouse_capture = 0;
        while (QApplication::overrideCursor()) QApplication::restoreOverrideCursor();
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

int ignoreNextMouseEvent = 1;
void
RendererStack::mouseReleaseEvent(QMouseEvent *event)
{
    if (!dopause && this->geometry().contains(m_monitor_index >= 1 ? event->globalPos() : event->pos()) &&
        (event->button() == Qt::LeftButton) && !mouse_capture &&
        (isMouseDown & 1) && (kbd_req_capture || (mouse_get_buttons() != 0)) &&
        (mouse_input_mode == 0)) {
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
    if (mouse_capture || (mouse_input_mode >= 1)) {
#ifdef Q_OS_WINDOWS
        if (((m_monitor_index >= 1) && (mouse_input_mode >= 1) && mousedata.mouse_tablet_in_proximity) ||
             ((m_monitor_index < 1) && (mouse_input_mode >= 1)))
#else
#ifndef __APPLE__
        if (((m_monitor_index >= 1) && (mouse_input_mode >= 1) && mousedata.mouse_tablet_in_proximity) ||
             (m_monitor_index < 1))
#else
        if ((m_monitor_index >= 1) && (mouse_input_mode >= 1) && mousedata.mouse_tablet_in_proximity)
#endif
#endif
            mouse_set_buttons_ex(mouse_get_buttons_ex() & ~event->button());
    }
    isMouseDown &= ~1;
}

void
RendererStack::mousePressEvent(QMouseEvent *event)
{
    isMouseDown |= 1;
    if (mouse_capture || (mouse_input_mode >= 1)) {
#ifdef Q_OS_WINDOWS
        if (((m_monitor_index >= 1) && (mouse_input_mode >= 1) && mousedata.mouse_tablet_in_proximity) ||
             ((m_monitor_index < 1) && (mouse_input_mode >= 1)))
#else
#ifndef __APPLE__
        if (((m_monitor_index >= 1) && (mouse_input_mode >= 1) && mousedata.mouse_tablet_in_proximity) ||
             (m_monitor_index < 1))
#else
        if ((m_monitor_index >= 1) && (mouse_input_mode >= 1) && mousedata.mouse_tablet_in_proximity)
#endif
#endif
            mouse_set_buttons_ex(mouse_get_buttons_ex() | event->button());
    }
    event->accept();
}

void
RendererStack::wheelEvent(QWheelEvent *event)
{
    mouse_set_z(event->pixelDelta().y());
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

#if defined __unix__ && !defined __HAIKU__
    if (!stricmp(mousedata.mouse_type, "wayland"))
        mouse_scale(event->pos().x() - oldPos.x(), event->pos().y() - oldPos.y());
#endif

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
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
RendererStack::enterEvent(QEnterEvent *event)
#else
RendererStack::enterEvent(QEvent *event)
#endif
{
    mousedata.mouse_tablet_in_proximity = m_monitor_index + 1;

    if (mouse_input_mode == 1)
        QApplication::setOverrideCursor(Qt::BlankCursor);
    else if (mouse_input_mode == 2)
        QApplication::setOverrideCursor(Qt::CrossCursor);
}

void
RendererStack::leaveEvent(QEvent *event)
{
    mousedata.mouse_tablet_in_proximity = 0;

    if (mouse_input_mode == 1 && QApplication::overrideCursor()) {
        while (QApplication::overrideCursor())
            QApplication::restoreOverrideCursor();
    }
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
#if QT_CONFIG(vulkan)
        case Renderer::Vulkan:
            {
                this->createWinId();
                VulkanWindowRenderer *hw = nullptr;
                try {
                    hw = new VulkanWindowRenderer(this);
                } catch (std::runtime_error &e) {
                    auto msgBox = new QMessageBox(QMessageBox::Critical, "86Box", e.what() + QString("\nFalling back to software rendering."), QMessageBox::Ok);
                    msgBox->setAttribute(Qt::WA_DeleteOnClose);
                    msgBox->show();
                    imagebufs = {};
                    QTimer::singleShot(0, this, [this]() { switchRenderer(Renderer::Software); });
                    current.reset(nullptr);
                    break;
                }
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
    if (current.get() == nullptr)
        return;
    current->setFocusPolicy(Qt::NoFocus);
    current->setFocusProxy(this);
    current->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    current->setStyleSheet("background-color: black");
    addWidget(current.get());

    this->setStyleSheet("background-color: black");

    currentBuf = 0;

    if (renderer != Renderer::OpenGL3 && renderer != Renderer::Vulkan) {
        imagebufs = rendererWindow->getBuffers();
        endblit();
        emit rendererChanged();
    }
}

// called from blitter thread
void
RendererStack::blit(int x, int y, int w, int h)
{
    if ((x < 0) || (y < 0) || (w <= 0) || (h <= 0) ||
        (w > 2048) || (h > 2048) ||
        (monitors[m_monitor_index].target_buffer == NULL) || imagebufs.empty() ||
        std::get<std::atomic_flag *>(imagebufs[currentBuf])->test_and_set()) {
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

void
RendererStack::closeEvent(QCloseEvent *event)
{
    if (cpu_thread_run == 1 || is_quit == 0) {
        event->accept();
        main_window->ui->actionShow_non_primary_monitors->setChecked(false);
        return;
    }
    event->ignore();
    main_window->close();
}

void
RendererStack::changeEvent(QEvent *event)
{
    if (m_monitor_index != 0 && isVisible()) {
        monitor_settings[m_monitor_index].mon_window_maximized = isMaximized();
        config_save();
    }
}

bool
RendererStack::event(QEvent* event)
{
    if (event->type() == QEvent::MouseMove) {
        QMouseEvent* mouse_event = (QMouseEvent*)event;

        if (m_monitor_index >= 1) {
            if (mouse_input_mode >= 1) {
                mouse_x_abs       = (mouse_event->localPos().x()) / (long double)width();
                mouse_y_abs       = (mouse_event->localPos().y()) / (long double)height();
                if (!mouse_tablet_in_proximity)
                    mouse_tablet_in_proximity = mousedata.mouse_tablet_in_proximity;
            }
            return QStackedWidget::event(event);
        }

#ifdef Q_OS_WINDOWS
        if (mouse_input_mode == 0) {
            mouse_x_abs           = (mouse_event->localPos().x()) / (long double)width();
            mouse_y_abs           = (mouse_event->localPos().y()) / (long double)height();
            return QStackedWidget::event(event);
        }
#endif

        mouse_x_abs               = (mouse_event->localPos().x()) / (long double)width();
        mouse_y_abs               = (mouse_event->localPos().y()) / (long double)height();
        mouse_tablet_in_proximity = mousedata.mouse_tablet_in_proximity;
    }

    return QStackedWidget::event(event);
}

void
RendererStack::setFocusRenderer()
{
    if (current)
        current->setFocus();
}

void
RendererStack::onResize(int width, int height)
{
    if (rendererWindow) {
        rendererWindow->r_monitor_index = m_monitor_index;
        rendererWindow->onResize(width, height);
    }
}
