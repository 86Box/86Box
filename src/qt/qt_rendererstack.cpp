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

#include "qt_openglrenderer.hpp"
#include "qt_softwarerenderer.hpp"
#include "qt_vulkanwindowrenderer.hpp"

#include "qt_mainwindow.hpp"
#include "qt_util.hpp"

#include "ui_qt_mainwindow.h"

#include "evdev_mouse.hpp"

#include <cmath>

#include <atomic>
#include <stdexcept>

#include <QApplication>

#include <QScreen>
#include <QMessageBox>
#ifdef TOUCH_PR
#include <QTouchEvent>
#endif
#include <QStringBuilder>

#include <QPainter>
#include <QEvent>
#include <QKeyEvent>
#include <QWidget>

#include <QTimer>

#include <atomic>
#include <mutex>
#include <array>
#include <vector>
#include <memory>
#include <QApplication>

#ifdef WAYLAND
#    include "wl_mouse.hpp"
#endif

#ifdef __APPLE__
#    include <CoreGraphics/CoreGraphics.h>
#endif

#ifdef Q_OS_WINDOWS
#    include <windows.h>
#endif

#include <png.h>

extern "C" {
#include <86box/86box.h>
#include <86box/config.h>
#include <86box/path.h>
#include <86box/plat.h>
#include <86box/video.h>
#include <86box/mouse.h>
}

struct mouseinputdata {
    monitor_t *hovered_monitor = nullptr;

    char *mouse_type;
};
static mouseinputdata mousedata;

extern MainWindow *main_window;

#ifdef Q_OS_WINDOWS
HWND rw_hwnd;
#endif

RendererStack::RendererStack(QWidget *parent, monitor_t *monitor)
    : QWidget(parent)
    , ui(new Ui::RendererStack)
{
    m_monitor = monitor ? monitor : video_primary_monitor;
    m_monitor_index = video_monitor_id(m_monitor);

    boxLayout = new QBoxLayout(QBoxLayout::TopToBottom, this);
    boxLayout->setContentsMargins(0, 0, 0, 0);
#ifdef TOUCH_PR
    setAttribute(Qt::WA_AcceptTouchEvents, true);
#endif
#ifdef Q_OS_WINDOWS
    setAttribute(Qt::WA_NativeWindow, true);
    (void) winId();
#endif
    rendererTakesScreenshots = false;
#ifdef Q_OS_WINDOWS
    int raw = 1;
#else
    int raw = 0;
#endif

    ui->setupUi(this);

    if (m_monitor_index >= 1) {
        QTimer *frameRateTimer = new QTimer(this);
        frameRateTimer->setSingleShot(false);
        frameRateTimer->setInterval(1000);
        connect(frameRateTimer, &QTimer::timeout, this, [this] {
            if (!m_monitor)
                return;
            this->setWindowTitle(QObject::tr("86Box Monitor #%1").arg(m_monitor_index + 1) + QString(" - ") + (m_monitor->mon_dpms ? tr("Monitor in sleep mode") : tr("%1 Hz").arg(QString::number(m_monitor->mon_actualrenderedframes.load()) + (m_monitor->mon_interlace ? "i" : ""))));
        });
        frameRateTimer->start(1000);
    }
#if defined __unix__ && !defined __HAIKU__
    if (m_monitor_index != 0)
        return;

    memset(auto_mouse_type, 0, sizeof(auto_mouse_type));
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
        extern void xinput2_set_grab_widget(QWidget *widget);
        extern void xinput2_mouse_capture(QWindow *window);
        extern void xinput2_mouse_uncapture();
        xinput2_init();
        if (m_monitor_index == 0) {
            setAttribute(Qt::WA_NativeWindow, true);
            xinput2_set_grab_widget(this);
        }
        this->mouse_capture_func = xinput2_mouse_capture;
        this->mouse_uncapture_func = xinput2_mouse_uncapture;
        this->mouse_exit_func = xinput2_exit;
    }
#endif

    if (m_monitor_index == 0)
        mouse_set_raw(raw);

}

RendererStack::~RendererStack()
{
    stopRenderer();
    delete ui;
}

void
RendererStack::stopRenderer()
{
    switchInProgress = true;
    if (current) {
        disconnect(this, &RendererStack::blitToRenderer, nullptr, nullptr);
        rendererWindow->finalize();
    }
    rendererWindow = nullptr;
    current.reset();
    imagebufs.clear();

    if (m_monitor && mousedata.hovered_monitor == m_monitor) {
        mousedata.hovered_monitor = nullptr;
        while (QApplication::overrideCursor())
            QApplication::restoreOverrideCursor();
    }
    mouse_clear_input_monitor(m_monitor);
    m_monitor = nullptr;
}

bool
RendererStack::acceptsGuestInput() const
{
    return m_monitor && (m_monitor->flags & VIDEO_MONITOR_INPUT);
}

void
qt_mouse_capture(int on)
{
    if (!on) {
        mouse_capture = 0;
        while (QApplication::overrideCursor())
            QApplication::restoreOverrideCursor();
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
    if (!acceptsGuestInput()) {
        event->accept();
        return;
    }

#ifdef Q_OS_WINDOWS
    rw_hwnd = (HWND) this->winId();
#endif

    event->accept();
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
    if (!dopause && this->geometry().contains(m_monitor_index >= 1 ? event->globalPosition().toPoint() : event->position().toPoint()) &&
#else
    if (!dopause && this->geometry().contains(m_monitor_index >= 1 ? event->globalPos() : event->pos()) &&
#endif
        (event->button() == Qt::LeftButton) && !mouse_capture && (isMouseDown & 1) && (kbd_req_capture || (mouse_get_buttons() != 0)) && (mouse_input_mode == 0)) {
        plat_mouse_capture(1);
        this->setCursor(Qt::BlankCursor);
        if (!ignoreNextMouseEvent)
            ignoreNextMouseEvent++; // Avoid jumping cursor when moved.
        isMouseDown &= ~1;
        return;
    }
    if (mouse_capture && (event->button() & mouse_get_release_buttons())) {
        /*
           On Windows the raw input thread sets the button bit behind our back and
           stops processing as soon as the capture drops, so clear it here to keep it
           from staying stuck down. Elsewhere the press was already consumed above.
         */
        mouse_set_buttons_ex(mouse_get_buttons_ex() & ~event->button());
        plat_mouse_capture(0);
        this->unsetCursor();
        isMouseDown &= ~1;
        return;
    }
    if (mouse_capture || (mouse_input_mode >= 1)) {
        Qt::MouseButton button = event->button();
#ifdef __APPLE__
        if ((button == Qt::LeftButton) && control_click_active) {
            control_click_active = false;
            button = right_button_active ? Qt::NoButton : Qt::RightButton;
        } else if (button == Qt::RightButton) {
            right_button_active = false;
            button = control_click_active ? Qt::NoButton : Qt::RightButton;
        }
#endif
#ifdef Q_OS_WINDOWS
        if (((m_monitor_index >= 1) && (mouse_input_mode >= 1) && (mousedata.hovered_monitor == m_monitor)) || ((m_monitor_index < 1) && (mouse_input_mode >= 1)))
#else
#    ifdef __APPLE__
        if (((m_monitor_index >= 1) && (mouse_input_mode >= 1) && (mousedata.hovered_monitor == m_monitor)) || ((m_monitor_index < 1) && (mouse_input_mode >= 1)))
#    else
        if (((m_monitor_index >= 1) && (mouse_input_mode >= 1) && (mousedata.hovered_monitor == m_monitor)) || (m_monitor_index < 1))
#    endif
#endif
            mouse_set_buttons_ex(mouse_get_buttons_ex() & ~button);
    }
    isMouseDown &= ~1;
}

void
RendererStack::mousePressEvent(QMouseEvent *event)
{
    if (!acceptsGuestInput()) {
        event->accept();
        return;
    }

    isMouseDown |= 1;
    /* A button that releases the capture is consumed, not sent to the guest. */
    if (mouse_capture && (event->button() & mouse_get_release_buttons())) {
        event->accept();
        return;
    }
    if (mouse_capture || (mouse_input_mode >= 1)) {
        Qt::MouseButton button = event->button();
#ifdef __APPLE__
        if ((button == Qt::LeftButton) && (event->modifiers() & Qt::ControlModifier)) {
            control_click_active = true;
            button = Qt::RightButton;
        } else if (button == Qt::RightButton) {
            right_button_active = true;
        }
#endif
#ifdef Q_OS_WINDOWS
        if (((m_monitor_index >= 1) && (mouse_input_mode >= 1) && (mousedata.hovered_monitor == m_monitor)) || ((m_monitor_index < 1) && (mouse_input_mode >= 1)))
#else
#    ifdef __APPLE__
        if (((m_monitor_index >= 1) && (mouse_input_mode >= 1) && (mousedata.hovered_monitor == m_monitor)) || ((m_monitor_index < 1) && (mouse_input_mode >= 1)))
#    else
        if (((m_monitor_index >= 1) && (mouse_input_mode >= 1) && (mousedata.hovered_monitor == m_monitor)) || (m_monitor_index < 1))
#    endif
#endif
            mouse_set_buttons_ex(mouse_get_buttons_ex() | button);
    }
    event->accept();
}

void
RendererStack::wheelEvent(QWheelEvent *event)
{
    if (!acceptsGuestInput()) {
        event->accept();
        return;
    }

    if (!mouse_capture && (mouse_input_mode < 1)) {
        event->ignore();
        return;
    }

#if !defined(Q_OS_WINDOWS) && !defined(__APPLE__)
    if (event->inverted()) {
        mouse_set_z(-((short) event->angleDelta().y()));
        mouse_set_w(-((short) event->angleDelta().x()));
    } else {
        mouse_set_z((short) event->angleDelta().y());
        mouse_set_w((short) event->angleDelta().x());
    }
#else
    /* While the mouse is captured, the native input filters read the wheel.
       A tablet is never captured, so its wheel comes from Qt's angleDelta,
       120 units per notch. The core's WHEEL_DELTA is 120 on Windows and 1
       on macOS, so macOS passes whole notches and keeps the remainder. */
    if (!mouse_capture) {
#    ifdef __APPLE__
        static int wheel_rem = 0;

        wheel_rem += event->angleDelta().y();
        mouse_set_z(wheel_rem / 120);
        wheel_rem %= 120;
#    else
        mouse_set_z(event->angleDelta().y());
#    endif
    }
#endif
    event->accept();
}

void
RendererStack::mouseMoveEvent(QMouseEvent *event)
{
    if (!acceptsGuestInput()) {
        event->accept();
        return;
    }

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

#    if defined __unix__ && !defined __HAIKU__
    if (!stricmp(mousedata.mouse_type, "wayland"))
        mouse_scale(event->pos().x() - oldPos.x(), event->pos().y() - oldPos.y());
#    endif

    if (QApplication::platformName() == "eglfs") {
        leaveEvent((QEvent *) event);
        ignoreNextMouseEvent--;
    }
#    if !defined _WIN32
    QCursor::setPos(mapToGlobal(QPoint(width() / 2, height() / 2)));
#    endif
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
    if (!acceptsGuestInput())
        return;

    mousedata.hovered_monitor = m_monitor;

    if (mouse_input_mode == 1) {
        const auto *tablet = tablet_get_device(tablet_type);
        const bool hide_cursor = tablet_type && tablet && tablet != &mouse_mtouch_device &&
                                 tablet != &mouse_ibm7690_touch_device && tablet != &mouse_elo_device;
        QApplication::setOverrideCursor(hide_cursor ? Qt::BlankCursor : Qt::ArrowCursor);
    } else if (mouse_input_mode == 2)
        QApplication::setOverrideCursor(Qt::CrossCursor);
}

void
RendererStack::leaveEvent(QEvent *event)
{
    if (!acceptsGuestInput()) {
        event->accept();
        return;
    }

    if (mousedata.hovered_monitor == m_monitor)
        mousedata.hovered_monitor = nullptr;
    mouse_clear_input_monitor(m_monitor);

    if (mouse_input_mode >= 1 && QApplication::overrideCursor()) {
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
    // startblit();
    switchInProgress = true;
    if (current) {
        rendererWindow->finalize();
        boxLayout->removeWidget(current.get());
        disconnect(this, &RendererStack::blitToRenderer, nullptr, nullptr);

        /* Create new renderer only after previous is destroyed! */
        connect(current.get(), &QObject::destroyed, this, [this, renderer](QObject *) { createRenderer(renderer); });

        current.release()->deleteLater();
    } else {
        createRenderer(renderer);
    }
}

void
RendererStack::createRenderer(Renderer renderer)
{
    if (!m_monitor)
        return;

    rendererTakesScreenshots = false;
    switch (renderer) {
        default:
        case Renderer::Software:
            {
                auto sw        = new SoftwareRenderer(this, m_monitor);
                rendererWindow = sw;
                /* Delivered on the renderer's worker thread. */
                connect(this, &RendererStack::blitToRenderer, sw->blitContext(), [sw](int buf_idx, int x, int y, int w, int h) { sw->onBlit(buf_idx, x, y, w, h); }, Qt::QueuedConnection);
                current.reset(sw);
            }
            break;
        case Renderer::OpenGL3:
            {
                this->createWinId();
                this->rendererTakesScreenshots = true;
                auto hw                        = new OpenGLRenderer(this, m_monitor);
                rendererWindow                 = hw;
                connect(this, &RendererStack::blitToRenderer, hw, &OpenGLRenderer::onBlit, Qt::QueuedConnection);
                connect(hw, &OpenGLRenderer::initialized, [=]() {
                    /* Buffers are available only after initialization. */
                    imagebufs        = rendererWindow->getBuffers();
                    switchInProgress = false;
                    emit rendererChanged();
                });
                connect(hw, &OpenGLRenderer::errorInitializing, [=]() {
                    /* Renderer not could initialize, fallback to software. */
                    imagebufs = {};
                    QTimer::singleShot(0, this, [this]() { switchRenderer(Renderer::Software); });
                });
                current.reset(this->createWindowContainer(hw));
                break;
            }
#if QT_CONFIG(vulkan)
        case Renderer::Vulkan:
            {
                this->createWinId();
                VulkanWindowRenderer *hw = nullptr;
                try {
                    hw = new VulkanWindowRenderer(this, m_monitor);
                } catch (std::runtime_error &e) {
                    auto msgBox = new QMessageBox(QMessageBox::Critical, QString(), tr("Failed to initialize Vulkan renderer.") + QStringLiteral("\n") + e.what() + QStringLiteral("\n") + tr("Falling back to software rendering."), QMessageBox::Ok);
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
                    imagebufs        = rendererWindow->getBuffers();
                    switchInProgress = false;
                    emit rendererChanged();
                    if (m_monitor_index != 0 && show_second_monitors)
                        this->show();

                    QTimer::singleShot(1000, this, [this] {
                        if (m_monitor_index != 0 && show_second_monitors)
                            this->show();
                    });
                });
                connect(hw, &VulkanWindowRenderer::errorInitializing, [=]() {
                    /* Renderer could not initialize, fallback to software. */
                    imagebufs = {};
                    QTimer::singleShot(0, this, [this]() { switchRenderer(Renderer::Software); });
                });
                current.reset(this->createWindowContainer(hw));
                break;
            }
#endif
    }
    if (current.get() == nullptr) {
        return;
    }
    current->setFocusPolicy(Qt::NoFocus);
    current->setFocusProxy(this);
    current->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    current->setAttribute(Qt::WA_AlwaysStackOnTop);
    current->setStyleSheet("background-color: black");

    this->setStyleSheet("background-color: black");
    boxLayout->addWidget(current.get());
    if (m_monitor_index > 0)
        current->show();


    currentBuf = 0;

    if (renderer != Renderer::OpenGL3 && renderer != Renderer::Vulkan) {
        imagebufs        = rendererWindow->getBuffers();
        switchInProgress = false;
        emit rendererChanged();
    }
}

// called from blitter thread
void
RendererStack::blit(int x, int y, int w, int h)
{
    if ((x < 0) || (y < 0) || (w <= 0) || (h <= 0) || (x > 2048 - w) || (y > 2048 - h) || (switchInProgress) || (m_monitor->target_buffer == NULL) || imagebufs.empty() || std::get<std::atomic_flag *>(imagebufs[currentBuf])->test_and_set()) {
        video_blit_complete_monitor(m_monitor);
        return;
    }
    sx = x;
    sy = y;
    sw = this->w = w;
    sh = this->h       = h;
    uint8_t *imagebits = std::get<uint8_t *>(imagebufs[currentBuf]);
    /* Copy one texel past each edge as well (Rage 128 edge-line fix):
       the staging image is 2048x2048 and keeps whatever an earlier,
       larger mode left there, and a linear-filtered scale samples those
       neighbours. The Rage 128 paints its blanking into that ring of
       the frame buffer; for other cards the ring is what they leave. */
    int cx0 = (x > 0) ? (x - 1) : 0;
    int cx1 = ((x + w) < 2048) ? (x + w + 1) : 2048;
    int cy0 = (y > 0) ? (y - 1) : 0;
    int cy1 = ((y + h) < 2048) ? (y + h + 1) : 2048;
    for (int y1 = cy0; y1 < cy1; y1++) {
        auto scanline = imagebits + (y1 * rendererWindow->getBytesPerRow()) + (cx0 * 4);
        video_copy(scanline, &(m_monitor->target_buffer->line[y1][cx0]), (cx1 - cx0) * 4);
    }

    if (m_monitor->mon_screenshots_raw) {
        video_screenshot_monitor((uint32_t *) imagebits, x, y, rendererWindow->getBytesPerRow() / 4, m_monitor);
    }
    if (m_monitor->mon_screenshots_raw_clipboard) {
        const int stride = rendererWindow->getBytesPerRow();
        const QImage pixels(imagebits + y * stride + x * 4, w, h, stride, QImage::Format_RGB32);
        const QImage copy = pixels.copy();
        QMetaObject::invokeMethod(this, [copy] { util::copyImageToClipboard(copy); }, Qt::QueuedConnection);
        --m_monitor->mon_screenshots_raw_clipboard;
    }
    video_blit_complete_monitor(m_monitor);
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
        config_get_monitor_settings(m_monitor_index)->mon_window_maximized = isMaximized();
        config_save();
    }
    /* See MainWindow::changeEvent(). */
    if (m_monitor_index != 0 && event->type() == QEvent::WindowStateChange)
        onResize(width(), height());
}

bool
RendererStack::event(QEvent *event)
{
    if (!acceptsGuestInput() || !rendererWindow)
        return QWidget::event(event);

    if (event->type() == QEvent::MouseMove) {
        QMouseEvent *mouse_event = (QMouseEvent *) event;

        if (m_monitor_index >= 1) {
            if (mouse_input_mode >= 1) {
#ifdef TOUCH_PR
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
                mouse_x_abs = (mouse_event->position().x()) / (double) width();
                mouse_y_abs = (mouse_event->position().y()) / (double) height();
#else
                mouse_x_abs = (mouse_event->localPos().x()) / (double) width();
                mouse_y_abs = (mouse_event->localPos().y()) / (double) height();
#endif
#else
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
                mouse_x_abs       = (mouse_event->position().x()) / (long double)width();
                mouse_y_abs       = (mouse_event->position().y()) / (long double)height();
#else
                mouse_x_abs       = (mouse_event->localPos().x()) / (long double)width();
                mouse_y_abs       = (mouse_event->localPos().y()) / (long double)height();
#endif
#endif
                if (!mouse_tablet_in_proximity)
                    mouse_set_input_monitor(mousedata.hovered_monitor == m_monitor ? m_monitor : nullptr);
                mouse_x_abs -= rendererWindow->destinationF.left();
                mouse_y_abs -= rendererWindow->destinationF.top();

                if (mouse_x_abs < 0)
                    mouse_x_abs = 0;
                if (mouse_y_abs < 0)
                    mouse_y_abs = 0;

                mouse_x_abs /= rendererWindow->destinationF.width();
                mouse_y_abs /= rendererWindow->destinationF.height();

                if (mouse_x_abs > 1)
                    mouse_x_abs = 1;
                if (mouse_y_abs > 1)
                    mouse_y_abs = 1;
            }
            return QWidget::event(event);
        }

#ifdef TOUCH_PR
#ifdef Q_OS_WINDOWS
        if (mouse_input_mode == 0) {
#    if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
            mouse_x_abs = (mouse_event->position().x()) / (double) width();
            mouse_y_abs = (mouse_event->position().y()) / (double) height();
#    else
            mouse_x_abs = (mouse_event->localPos().x()) / (double) width();
            mouse_y_abs = (mouse_event->localPos().y()) / (double) height();
#    endif
            mouse_x_abs -= rendererWindow->destinationF.left();
            mouse_y_abs -= rendererWindow->destinationF.top();

            if (mouse_x_abs < 0)
                mouse_x_abs = 0;
            if (mouse_y_abs < 0)
                mouse_y_abs = 0;

            mouse_x_abs /= rendererWindow->destinationF.width();
            mouse_y_abs /= rendererWindow->destinationF.height();

            if (mouse_x_abs > 1)
                mouse_x_abs = 1;
            if (mouse_y_abs > 1)
                mouse_y_abs = 1;

            if (mouse_both_enabled())
                mouse_clear_input_monitor(m_monitor);

            return QWidget::event(event);
        }
#endif

#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
        mouse_x_abs = (mouse_event->position().x()) / (double) width();
        mouse_y_abs = (mouse_event->position().y()) / (double) height();
#else
        mouse_x_abs = (mouse_event->localPos().x()) / (double) width();
        mouse_y_abs = (mouse_event->localPos().y()) / (double) height();
#endif
#else
#ifdef Q_OS_WINDOWS
        if (mouse_input_mode == 0) {
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
            mouse_x_abs = (mouse_event->position().x()) / (long double)width();
            mouse_y_abs = (mouse_event->position().y()) / (long double)height();
#else
            mouse_x_abs = (mouse_event->localPos().x()) / (long double)width();
            mouse_y_abs = (mouse_event->localPos().y()) / (long double)height();
#endif
            mouse_x_abs -= rendererWindow->destinationF.left();
            mouse_y_abs -= rendererWindow->destinationF.top();

            if (mouse_x_abs < 0) mouse_x_abs = 0;
            if (mouse_y_abs < 0) mouse_y_abs = 0;

            mouse_x_abs /= rendererWindow->destinationF.width();
            mouse_y_abs /= rendererWindow->destinationF.height();

            if (mouse_x_abs > 1) mouse_x_abs = 1;
            if (mouse_y_abs > 1) mouse_y_abs = 1;

            if (mouse_both_enabled())
                mouse_clear_input_monitor(m_monitor);

            return QWidget::event(event);
        }
#endif

#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
        mouse_x_abs               = (mouse_event->position().x()) / (long double)width();
        mouse_y_abs               = (mouse_event->position().y()) / (long double)height();
#else
        mouse_x_abs               = (mouse_event->localPos().x()) / (long double)width();
        mouse_y_abs               = (mouse_event->localPos().y()) / (long double)height();
#endif
#endif
        mouse_x_abs              -= rendererWindow->destinationF.left();
        mouse_y_abs              -= rendererWindow->destinationF.top();

        if (mouse_x_abs < 0)
            mouse_x_abs = 0;
        if (mouse_y_abs < 0)
            mouse_y_abs = 0;

        mouse_x_abs /= rendererWindow->destinationF.width();
        mouse_y_abs /= rendererWindow->destinationF.height();

        if (mouse_x_abs > 1)
            mouse_x_abs = 1;
        if (mouse_y_abs > 1)
            mouse_y_abs = 1;
        mouse_set_input_monitor(mousedata.hovered_monitor == m_monitor ? m_monitor : nullptr);
#ifdef TOUCH_PR
    } else
        switch (event->type()) {
            case QEvent::TouchBegin:
            case QEvent::TouchUpdate:
                {
#if QT_VERSION < QT_VERSION_CHECK(6, 0, 0)
                    QTouchEvent *touchevent = (QTouchEvent *) event;
                    if (mouse_input_mode == 0)
                        break;
                    if (touchevent->touchPoints().count()) {
                        mouse_x_abs = (touchevent->touchPoints()[0].pos().x()) / (double) width();
                        mouse_y_abs = (touchevent->touchPoints()[0].pos().y()) / (double) height();
                        mouse_x_abs -= rendererWindow->destinationF.left();
                        mouse_y_abs -= rendererWindow->destinationF.top();

                        if (mouse_x_abs < 0)
                            mouse_x_abs = 0;
                        if (mouse_y_abs < 0)
                            mouse_y_abs = 0;

                        mouse_x_abs /= rendererWindow->destinationF.width();
                        mouse_y_abs /= rendererWindow->destinationF.height();

                        if (mouse_x_abs > 1)
                            mouse_x_abs = 1;
                        if (mouse_y_abs > 1)
                            mouse_y_abs = 1;
                    }
                    mouse_set_buttons_ex(mouse_get_buttons_ex() | 1);
                    touchevent->accept();
                    return true;
#else
                    QTouchEvent *touchevent = (QTouchEvent *) event;
                    if (mouse_input_mode == 0)
                        break;
                    if (touchevent->pointCount()) {
                        mouse_x_abs = (touchevent->point(0).position().x()) / (double) width();
                        mouse_y_abs = (touchevent->point(0).position().y()) / (double) height();
                        mouse_x_abs -= rendererWindow->destinationF.left();
                        mouse_y_abs -= rendererWindow->destinationF.top();

                        if (mouse_x_abs < 0)
                            mouse_x_abs = 0;
                        if (mouse_y_abs < 0)
                            mouse_y_abs = 0;

                        mouse_x_abs /= rendererWindow->destinationF.width();
                        mouse_y_abs /= rendererWindow->destinationF.height();

                        if (mouse_x_abs > 1)
                            mouse_x_abs = 1;
                        if (mouse_y_abs > 1)
                            mouse_y_abs = 1;
                    }
                    mouse_set_buttons_ex(mouse_get_buttons_ex() | 1);
                    touchevent->accept();
                    return true;
#endif
                }
            case QEvent::TouchEnd:
            case QEvent::TouchCancel:
                {
#if QT_VERSION < QT_VERSION_CHECK(6, 0, 0)
                    QTouchEvent *touchevent = (QTouchEvent *) event;
                    if (mouse_input_mode == 0)
                        break;
                    if (touchevent->touchPoints().count()) {
                        mouse_x_abs = (touchevent->touchPoints()[0].pos().x()) / (double) width();
                        mouse_y_abs = (touchevent->touchPoints()[0].pos().y()) / (double) height();
                        mouse_x_abs -= rendererWindow->destinationF.left();
                        mouse_y_abs -= rendererWindow->destinationF.top();

                        if (mouse_x_abs < 0)
                            mouse_x_abs = 0;
                        if (mouse_y_abs < 0)
                            mouse_y_abs = 0;

                        mouse_x_abs /= rendererWindow->destinationF.width();
                        mouse_y_abs /= rendererWindow->destinationF.height();

                        if (mouse_x_abs > 1)
                            mouse_x_abs = 1;
                        if (mouse_y_abs > 1)
                            mouse_y_abs = 1;
                    }
                    mouse_set_buttons_ex(mouse_get_buttons_ex() & ~1);
                    touchevent->accept();
                    return true;
#else
                    QTouchEvent *touchevent = (QTouchEvent *) event;
                    if (mouse_input_mode == 0)
                        break;
                    if (touchevent->pointCount()) {
                        mouse_x_abs = (touchevent->point(0).position().x()) / (double) width();
                        mouse_y_abs = (touchevent->point(0).position().y()) / (double) height();
                        mouse_x_abs -= rendererWindow->destinationF.left();
                        mouse_y_abs -= rendererWindow->destinationF.top();

                        if (mouse_x_abs < 0)
                            mouse_x_abs = 0;
                        if (mouse_y_abs < 0)
                            mouse_y_abs = 0;

                        mouse_x_abs /= rendererWindow->destinationF.width();
                        mouse_y_abs /= rendererWindow->destinationF.height();

                        if (mouse_x_abs > 1)
                            mouse_x_abs = 1;
                        if (mouse_y_abs > 1)
                            mouse_y_abs = 1;
                    }
                    mouse_set_buttons_ex(mouse_get_buttons_ex() & ~1);
                    touchevent->accept();
                    return true;
#endif
                }

            default:
                return QWidget::event(event);
#endif /*TOUCH_PR*/
        }

    return QWidget::event(event);
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
#ifdef Q_OS_WINDOWS
    if (m_monitor_index == 0 && mouse_capture) {
        RECT rect;
        if (GetWindowRect((HWND) this->winId(), &rect)) {
            ClipCursor(&rect);
        }
    }
#endif
    if (rendererWindow)
        rendererWindow->onResize(width, height);
}
