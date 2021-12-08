#include "qt_mainwindow.hpp"
#include "ui_qt_mainwindow.h"

extern "C" {
#include <86box/86box.h>
#include <86box/config.h>
#include <86box/keyboard.h>
#include <86box/plat.h>
#include <86box/video.h>
};

#include <QGuiApplication>
#include <QWindow>
#include <QTimer>
#include <QThread>
#include <QKeyEvent>
#include <QMessageBox>
#include <QFocusEvent>

#include <array>
#include <unordered_map>

#include "qt_settings.hpp"
#include "qt_machinestatus.hpp"
#include "qt_mediamenu.hpp"

#ifdef __unix__
#include <X11/Xlib.h>
#include <X11/keysym.h>
#undef KeyPress
#undef KeyRelease
#endif

extern void qt_mouse_capture(int);
extern "C" void qt_blit(int x, int y, int w, int h);

MainWindow::MainWindow(QWidget *parent) :
    QMainWindow(parent),
    ui(new Ui::MainWindow)
{
    Q_INIT_RESOURCE(qt_resources);
    status = std::make_unique<MachineStatus>(this);
    mm = std::make_shared<MediaMenu>(this);
    MediaMenu::ptr = mm;

    ui->setupUi(this);
    ui->stackedWidget->setMouseTracking(true);
    ui->ogl->setRenderType(HardwareRenderer::RenderType::OpenGL);
    ui->gles->setRenderType(HardwareRenderer::RenderType::OpenGLES);

    connect(this, &MainWindow::showMessageForNonQtThread, this, &MainWindow::showMessage_, Qt::BlockingQueuedConnection);

    connect(this, &MainWindow::setTitleForNonQtThread, this, &MainWindow::setTitle_, Qt::BlockingQueuedConnection);
    connect(this, &MainWindow::getTitleForNonQtThread, this, &MainWindow::getTitle_, Qt::BlockingQueuedConnection);

    connect(this, &MainWindow::pollMouse, ui->stackedWidget, &RendererStack::mousePoll);

    connect(this, &MainWindow::setMouseCapture, this, [this](bool state) {
        mouse_capture = state ? 1 : 0;
        qt_mouse_capture(mouse_capture);
        if (mouse_capture) {
            ui->stackedWidget->grabMouse();
#ifdef WAYLAND
            if (QGuiApplication::platformName().contains("wayland")) {
                wl_mouse_capture(this->windowHandle());
            }
#endif
        } else {
            ui->stackedWidget->releaseMouse();
#ifdef WAYLAND
            if (QGuiApplication::platformName().contains("wayland")) {
                wl_mouse_uncapture();
            }
#endif
        }
    });

    connect(this, &MainWindow::resizeContents, this, [this](int w, int h) {
        if (!QApplication::platformName().contains("eglfs")) {
            int modifiedHeight = h + menuBar()->height() + statusBar()->height();
            ui->stackedWidget->resize(w, h);
            if (vid_resize == 0) {
                setFixedSize(w, modifiedHeight);
            } else {
                resize(w, modifiedHeight);
            }
        }
    });

    connect(ui->menubar, &QMenuBar::triggered, this, [] {
        config_save();
    });

    connect(this, &MainWindow::updateStatusBarPanes, this, [this] {
        status->refresh(ui->statusbar);
    });
    connect(this, &MainWindow::updateStatusBarPanes, this, &MainWindow::refreshMediaMenu);
    connect(this, &MainWindow::updateStatusBarActivity, status.get(), &MachineStatus::setActivity);
    connect(this, &MainWindow::updateStatusBarEmpty, status.get(), &MachineStatus::setEmpty);
    connect(this, &MainWindow::statusBarMessage, status.get(), &MachineStatus::message);

    ui->actionKeyboard_requires_capture->setChecked(kbd_req_capture);
    ui->actionRight_CTRL_is_left_ALT->setChecked(rctrl_is_lalt);
    ui->actionResizable_window->setChecked(vid_resize > 0);
    ui->menuWindow_scale_factor->setEnabled(vid_resize == 0);
    switch (vid_api) {
    case 0:
        ui->stackedWidget->setCurrentIndex(0);
        ui->actionSoftware_Renderer->setChecked(true);
        break;
    case 1:
        ui->stackedWidget->setCurrentIndex(1);
        ui->actionHardware_Renderer_OpenGL->setChecked(true);
        break;
    case 2:
        ui->stackedWidget->setCurrentIndex(2);
        ui->actionHardware_Renderer_OpenGL_ES->setChecked(true);
        break;
    }
    switch (scale) {
    case 0:
        ui->action0_5x->setChecked(true);
        break;
    case 1:
        ui->action1x->setChecked(true);
        break;
    case 2:
        ui->action1_5x->setChecked(true);
        break;
    case 3:
        ui->action2x->setChecked(true);
        break;
    }
    switch (video_filter_method) {
    case 0:
        ui->actionNearest->setChecked(true);
        break;
    case 1:
        ui->actionLinear->setChecked(true);
        break;
    }
    switch (video_fullscreen_scale) {
    case FULLSCR_SCALE_FULL:
        ui->actionFullScreen_stretch->setChecked(true);
        break;
    case FULLSCR_SCALE_43:
        ui->actionFullScreen_43->setChecked(true);
        break;
    case FULLSCR_SCALE_KEEPRATIO:
        ui->actionFullScreen_keepRatio->setChecked(true);
        break;
    case FULLSCR_SCALE_INT:
        ui->actionFullScreen_int->setChecked(true);
        break;
    }
    switch (video_grayscale) {
    case 0:
        ui->actionRGB_Color->setChecked(true);
        break;
    case 1:
        ui->actionRGB_Grayscale->setChecked(true);
        break;
    case 2:
        ui->actionAmber_monitor->setChecked(true);
        break;
    case 3:
        ui->actionGreen_monitor->setChecked(true);
        break;
    case 4:
        ui->actionWhite_monitor->setChecked(true);
        break;
    }
    switch (video_graytype) {
    case 0:
        ui->actionBT601_NTSC_PAL->setChecked(true);
        break;
    case 1:
        ui->actionBT709_HDTV->setChecked(true);
        break;
    case 2:
        ui->actionAverage->setChecked(true);
        break;
    }

    setFocusPolicy(Qt::StrongFocus);
    ui->gles->setFocusPolicy(Qt::NoFocus);
    ui->sw->setFocusPolicy(Qt::NoFocus);
    ui->ogl->setFocusPolicy(Qt::NoFocus);
    ui->stackedWidget->setFocusPolicy(Qt::NoFocus);
    ui->centralwidget->setFocusPolicy(Qt::NoFocus);
    menuBar()->setFocusPolicy(Qt::NoFocus);
    menuWidget()->setFocusPolicy(Qt::NoFocus);
    statusBar()->setFocusPolicy(Qt::NoFocus);

    video_setblit(qt_blit);
}

MainWindow::~MainWindow() {
    delete ui;
}

void MainWindow::on_actionKeyboard_requires_capture_triggered() {
    kbd_req_capture ^= 1;
}

void MainWindow::on_actionRight_CTRL_is_left_ALT_triggered() {
    rctrl_is_lalt ^= 1;
}

void MainWindow::on_actionHard_Reset_triggered() {
    pc_reset_hard();
}

void MainWindow::on_actionCtrl_Alt_Del_triggered() {
    pc_send_cad();
}

void MainWindow::on_actionCtrl_Alt_Esc_triggered() {
    pc_send_cae();
}

void MainWindow::on_actionPause_triggered() {
    plat_pause(dopause ^ 1);
}

void MainWindow::on_actionExit_triggered() {
    close();
}

void MainWindow::on_actionSettings_triggered() {
    int currentPause = dopause;
    plat_pause(1);
    Settings settings(this);
    settings.setModal(true);
    settings.setWindowModality(Qt::WindowModal);
    settings.exec();

    switch (settings.result()) {
    case QDialog::Accepted:
        /*
        pc_reset_hard_close();
        settings.save();
        config_changed = 2;
        pc_reset_hard_init();
        */
        settings.save();
        config_changed = 2;
        pc_reset_hard();

        break;
    case QDialog::Rejected:
        break;
    }
    plat_pause(currentPause);
}

std::array<uint32_t, 256> x11_to_xt_base
{
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0x01,
    0x02,
    0x03,
    0x04,
    0x05,
    0x06,
    0x07,
    0x08,
    0x09,
    0x0A,
    0x0B,
    0x0C,
    0x0D,
    0x0E,
    0x0F,
    0x10,
    0x11,
    0x12,
    0x13,
    0x14,
    0x15,
    0x16,
    0x17,
    0x18,
    0x19,
    0x1A,
    0x1B,
    0x1C,
    0x1D,
    0x1E,
    0x1F,
    0x20,
    0x21,
    0x22,
    0x23,
    0x24,
    0x25,
    0x26,
    0x27,
    0x28,
    0x29,
    0x2A,
    0x2B,
    0x2C,
    0x2D,
    0x2E,
    0x2F,
    0x30,
    0x31,
    0x32,
    0x33,
    0x34,
    0x35,
    0x36,
    0x37,
    0x38,
    0x39,
    0x3A,
    0x3B,
    0x3C,
    0x3D,
    0x3E,
    0x3F,
    0x40,
    0x41,
    0x42,
    0x43,
    0x44,
    0x45,
    0x46,
    0x47,
    0x48,
    0x49,
    0x4A,
    0x4B,
    0x4C,
    0x4D,
    0x4E,
    0x4F,
    0x50,
    0x51,
    0x52,
    0x53,
    0x54,
    0x55,
    0x56,
    0x57,
    0x58,
    0x147,
    0x148,
    0x149,
    0,
    0x14B,
    0,
    0x14D,
    0x14F,
    0x150,
    0x151,
    0x152,
    0x153,
    0x11C,
    0x11D,
    0, // Pause/Break key.
    0x137,
    0x135,
    0x138,
    0, // Ditto as above comment.
    0x15B,
    0x15C,
    0x15D,
};

std::array<uint32_t, 256> x11_to_xt_2
{
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0x01,
    0x02,
    0x03,
    0x04,
    0x05,
    0x06,
    0x07,
    0x08,
    0x09,
    0x0A,
    0x0B,
    0x0C,
    0x0D,
    0x0E,
    0x0F,
    0x10,
    0x11,
    0x12,
    0x13,
    0x14,
    0x15,
    0x16,
    0x17,
    0x18,
    0x19,
    0x1A,
    0x1B,
    0x1C,
    0x1D,
    0x1E,
    0x1F,
    0x20,
    0x21,
    0x22,
    0x23,
    0x24,
    0x25,
    0x26,
    0x27,
    0x28,
    0x29,
    0x2A,
    0x2B,
    0x2C,
    0x2D,
    0x2E,
    0x2F,
    0x30,
    0x31,
    0x32,
    0x33,
    0x34,
    0x35,
    0x36,
    0x37,
    0x38,
    0x39,
    0x3A,
    0x3B,
    0x3C,
    0x3D,
    0x3E,
    0x3F,
    0x40,
    0x41,
    0x42,
    0x43,
    0x44,
    0x45,
    0x46,
    0x47,
    0x48,
    0x49,
    0x4A,
    0x4B,
    0x4C,
    0x4D,
    0x4E,
    0x4F,
    0x50,
    0x51,
    0x52,
    0x53,
    0x54,
    0x55,
    0x56,
    0x57,
    0x58,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0x11C,
    0x11D,
    0x135,
    0x137,
    0x138,
    0,
    0x147,
    0x148,
    0x149,
    0x14B,
    0x14D,
    0x14F,
    0x150,
    0x151,
    0x152,
    0x153
};

std::array<uint32_t, 256> x11_to_xt_vnc
{
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0x1D,
    0x11D,
    0x2A,
    0x36,
    0,
    0,
    0x38,
    0x138,
    0x39,
    0x0B,
    0x02,
    0x03,
    0x04,
    0x05,
    0x06,
    0x07,
    0x08,
    0x09,
    0x0A,
    0x0C,
    0x0D,
    0x1A,
    0x1B,
    0x27,
    0x28,
    0x29,
    0x33,
    0x34,
    0x35,
    0x2B,
    0x1E,
    0x30,
    0x2E,
    0x20,
    0x12,
    0x21,
    0x22,
    0x23,
    0x17,
    0x24,
    0x25,
    0x26,
    0x32,
    0x31,
    0x18,
    0x19,
    0x10,
    0x13,
    0x1F,
    0x14,
    0x16,
    0x2F,
    0x11,
    0x2D,
    0x15,
    0x2C,
    0x0E,
    0x1C,
    0x0F,
    0x01,
    0x153,
    0x147,
    0x14F,
    0x149,
    0x151,
    0x148,
    0x150,
    0x14B,
    0x14D,
};

std::array<uint32_t, 256> darwin_to_xt
{
    0x1E,
    0x1F,
    0x20,
    0x21,
    0x23,
    0x22,
    0x2C,
    0x2D,
    0x2E,
    0x2F,
    0x2B,
    0x30,
    0x10,
    0x11,
    0x12,
    0x13,
    0x15,
    0x14,
    0x02,
    0x03,
    0x04,
    0x05,
    0x07,
    0x06,
    0x0D,
    0x0A,
    0x08,
    0x0C,
    0x09,
    0x0B,
    0x1B,
    0x18,
    0x16,
    0x1A,
    0x17,
    0x19,
    0x1C,
    0x26,
    0x24,
    0x28,
    0x25,
    0x27,
    0x2B,
    0x33,
    0x35,
    0x31,
    0x32,
    0x34,
    0x0F,
    0x39,
    0x29,
    0x0E,
    0x11C,
    0x01,
    0x15C,
    0x15B,
    0x2A,
    0x3A,
    0x38,
    0x1D,
    0x36,
    0x138,
    0x11D,
    0x15C,
    0,
    0x53,
    0,
    0x37,
    0,
    0x4E,
    0,
    0x45,
    0x130,
    0x12E,
    0x120,
    0x135,
    0x11C,
    0,
    0x4A,
    0,
    0,
    0,
    0x52,
    0x4F,
    0x50,
    0x51,
    0x4B,
    0x4C,
    0x4D,
    0x47,
    0,
    0x48,
    0x49,
    0,
    0,
    0,
    0x3F,
    0x40,
    0x41,
    0x3D,
    0x42,
    0x43,
    0,
    0x57,
    0,
    0x137,
    0,
    0x46,
    0,
    0x44,
    0x15D,
    0x58,
    0,
    0, // Pause/Break key.
    0x152,
    0x147,
    0x149,
    0x153,
    0x3E,
    0x14F,
    0x3C,
    0x151,
    0x3B,
    0x14B,
    0x14D,
    0x150,
    0x148,
    0,
};

static std::unordered_map<uint32_t, uint16_t> evdev_to_xt =
    {
        {96, 0x11C},
        {97, 0x11D},
        {98, 0x135},
        {99, 0x71},
        {100, 0x138},
        {101, 0x1C},
        {102, 0x147},
        {103, 0x148},
        {104, 0x149},
        {105, 0x14B},
        {106, 0x14D},
        {107, 0x14F},
        {108, 0x150},
        {109, 0x151},
        {110, 0x152},
        {111, 0x153}
};

static std::array<uint32_t, 256>& selected_keycode = x11_to_xt_base;

uint16_t x11_keycode_to_keysym(uint32_t keycode)
{
#if defined(Q_OS_WINDOWS)
    return keycode & 0xFFFF;
#elif defined(__APPLE__)
    return darwin_to_xt[keycode];
#else
    static Display* x11display = nullptr;
    if (QApplication::platformName().contains("wayland"))
    {
        selected_keycode = x11_to_xt_2;
    }
    else if (QApplication::platformName().contains("eglfs"))
    {
        keycode -= 8;
        if (keycode <= 88) return keycode;
        else return evdev_to_xt[keycode];
    }
    else if (!x11display)
    {
        x11display = XOpenDisplay(nullptr);
        if (XKeysymToKeycode(x11display, XK_Home) == 110)
        {
            selected_keycode = x11_to_xt_2;
        }
        else if (XKeysymToKeycode(x11display, XK_Home) == 69)
        {
            selected_keycode = x11_to_xt_vnc;
        }
    }
    return selected_keycode[keycode];
#endif
}

void MainWindow::on_actionFullscreen_triggered() {
    if (video_fullscreen > 0) {
        showNormal();
        video_fullscreen = 0;
    } else {
        showFullScreen();
        video_fullscreen = 1;
    }

    auto widget = ui->stackedWidget->currentWidget();
    auto rc = dynamic_cast<RendererCommon*>(widget);
    rc->onResize(widget->width(), widget->height());
}

void MainWindow::setTitle_(const wchar_t *title)
{
    this->setWindowTitle(QString::fromWCharArray(title));
}

void MainWindow::setTitle(const wchar_t *title)
{
    if (QThread::currentThread() == this->thread()) {
        setTitle_(title);
    } else {
        emit setTitleForNonQtThread(title);
    }
}

void MainWindow::getTitle_(wchar_t *title)
{
    this->windowTitle().toWCharArray(title);
}

void MainWindow::getTitle(wchar_t *title)
{
    if (QThread::currentThread() == this->thread()) {
        getTitle_(title);
    } else {
        emit getTitleForNonQtThread(title);
    }
}

bool MainWindow::eventFilter(QObject* receiver, QEvent* event)
{
    if (this->keyboardGrabber() == this) {
        if (event->type() == QEvent::KeyPress) {
            event->accept();
            this->keyPressEvent((QKeyEvent*)event);
            return true;
        }
        if (event->type() == QEvent::KeyRelease) {
            event->accept();
            this->keyReleaseEvent((QKeyEvent*)event);
            return true;
        }
    }

    return QMainWindow::eventFilter(receiver, event);
}

void MainWindow::refreshMediaMenu() {
    mm->refresh(ui->menuMedia);
}

void MainWindow::showMessage(const QString& header, const QString& message) {
    if (QThread::currentThread() == this->thread()) {
        showMessage_(header, message);
    } else {
        emit showMessageForNonQtThread(header, message);
    }
}

void MainWindow::showMessage_(const QString &header, const QString &message) {
    QMessageBox box(QMessageBox::Warning, header, message, QMessageBox::NoButton, this);
    box.exec();
}

void MainWindow::keyPressEvent(QKeyEvent* event)
{
#ifdef __APPLE__
    keyboard_input(1, x11_keycode_to_keysym(event->nativeVirtualKey()));
#else
    keyboard_input(1, x11_keycode_to_keysym(event->nativeScanCode()));
#endif

    if (keyboard_isfsexit()) {
        ui->actionFullscreen->trigger();
    }

    if (keyboard_ismsexit()) {
        plat_mouse_capture(0);
    }
    event->accept();
}

void MainWindow::blitToWidget(int x, int y, int w, int h)
{
    ui->stackedWidget->blit(x, y, w, h);
}

void MainWindow::keyReleaseEvent(QKeyEvent* event)
{
#ifdef __APPLE__
    keyboard_input(0, x11_keycode_to_keysym(event->nativeVirtualKey()));
#else
    keyboard_input(0, x11_keycode_to_keysym(event->nativeScanCode()));
#endif
}

void MainWindow::on_actionSoftware_Renderer_triggered() {
    ui->stackedWidget->setCurrentIndex(0);
    ui->actionHardware_Renderer_OpenGL->setChecked(false);
    ui->actionHardware_Renderer_OpenGL_ES->setChecked(false);
    vid_api = 0;
}

void MainWindow::on_actionHardware_Renderer_OpenGL_triggered() {
    ui->stackedWidget->setCurrentIndex(1);
    ui->actionSoftware_Renderer->setChecked(false);
    ui->actionHardware_Renderer_OpenGL_ES->setChecked(false);
    vid_api = 1;
}

void MainWindow::on_actionHardware_Renderer_OpenGL_ES_triggered() {
    ui->stackedWidget->setCurrentIndex(2);
    ui->actionSoftware_Renderer->setChecked(false);
    ui->actionHardware_Renderer_OpenGL->setChecked(false);
    vid_api = 2;
}

void MainWindow::focusInEvent(QFocusEvent* event)
{
    this->grabKeyboard();
}

void MainWindow::focusOutEvent(QFocusEvent* event)
{
    this->releaseKeyboard();
}

void MainWindow::on_actionResizable_window_triggered(bool checked) {
    if (checked) {
        vid_resize = 1;
        setFixedSize(QWIDGETSIZE_MAX, QWIDGETSIZE_MAX);
    } else {
        vid_resize = 0;
    }
    ui->menuWindow_scale_factor->setEnabled(! checked);
    emit resizeContents(scrnsz_x, scrnsz_y);
}

static void
video_toggle_option(QAction* action, int *val)
{
    startblit();
    *val ^= 1;
    video_copy = (video_grayscale || invert_display) ? video_transform_copy : memcpy;
    action->setChecked(*val > 0 ? true : false);
    endblit();
    config_save();
    device_force_redraw();
}

void MainWindow::on_actionInverted_VGA_monitor_triggered() {
    video_toggle_option(ui->actionInverted_VGA_monitor, &invert_display);
}

static void update_scaled_checkboxes(Ui::MainWindow* ui, QAction* selected) {
    ui->action0_5x->setChecked(ui->action0_5x == selected);
    ui->action1x->setChecked(ui->action1x == selected);
    ui->action1_5x->setChecked(ui->action1_5x == selected);
    ui->action2x->setChecked(ui->action2x == selected);

    reset_screen_size();
    device_force_redraw();
    video_force_resize_set(1);
    doresize = 1;
    config_save();
}

void MainWindow::on_action0_5x_triggered() {
    scale = 0;
    update_scaled_checkboxes(ui, ui->action0_5x);
}

void MainWindow::on_action1x_triggered() {
    scale = 1;
    update_scaled_checkboxes(ui, ui->action1x);
}

void MainWindow::on_action1_5x_triggered() {
    scale = 2;
    update_scaled_checkboxes(ui, ui->action1_5x);
}

void MainWindow::on_action2x_triggered() {
    scale = 3;
    update_scaled_checkboxes(ui, ui->action2x);
}

void MainWindow::on_actionNearest_triggered() {
    video_filter_method = 0;
    ui->actionLinear->setChecked(false);
}

void MainWindow::on_actionLinear_triggered() {
    video_filter_method = 1;
    ui->actionNearest->setChecked(false);
}

static void update_fullscreen_scale_checkboxes(Ui::MainWindow* ui, QAction* selected) {
    ui->actionFullScreen_stretch->setChecked(ui->actionFullScreen_stretch == selected);
    ui->actionFullScreen_43->setChecked(ui->actionFullScreen_43 == selected);
    ui->actionFullScreen_keepRatio->setChecked(ui->actionFullScreen_keepRatio == selected);
    ui->actionFullScreen_int->setChecked(ui->actionFullScreen_int == selected);

    if (video_fullscreen > 0) {
        auto widget = ui->stackedWidget->currentWidget();
        auto rc = dynamic_cast<RendererCommon*>(widget);
        rc->onResize(widget->width(), widget->height());
    }

    device_force_redraw();
    config_save();
}

void MainWindow::on_actionFullScreen_stretch_triggered() {
    video_fullscreen_scale = FULLSCR_SCALE_FULL;
    update_fullscreen_scale_checkboxes(ui, ui->actionFullScreen_stretch);
}

void MainWindow::on_actionFullScreen_43_triggered() {
    video_fullscreen_scale = FULLSCR_SCALE_43;
    update_fullscreen_scale_checkboxes(ui, ui->actionFullScreen_43);
}

void MainWindow::on_actionFullScreen_keepRatio_triggered() {
    video_fullscreen_scale = FULLSCR_SCALE_KEEPRATIO;
    update_fullscreen_scale_checkboxes(ui, ui->actionFullScreen_keepRatio);
}

void MainWindow::on_actionFullScreen_int_triggered() {
    video_fullscreen_scale = FULLSCR_SCALE_INT;
    update_fullscreen_scale_checkboxes(ui, ui->actionFullScreen_int);
}

static void update_greyscale_checkboxes(Ui::MainWindow* ui, QAction* selected, int value) {
    ui->actionRGB_Color->setChecked(ui->actionRGB_Color == selected);
    ui->actionRGB_Grayscale->setChecked(ui->actionRGB_Grayscale == selected);
    ui->actionAmber_monitor->setChecked(ui->actionAmber_monitor == selected);
    ui->actionGreen_monitor->setChecked(ui->actionGreen_monitor == selected);
    ui->actionWhite_monitor->setChecked(ui->actionWhite_monitor == selected);

    startblit();
    video_grayscale = value;
    video_copy = (video_grayscale || invert_display) ? video_transform_copy : memcpy;
    endblit();
    device_force_redraw();
    config_save();
}

void MainWindow::on_actionRGB_Color_triggered() {
    update_greyscale_checkboxes(ui, ui->actionRGB_Color, 0);
}

void MainWindow::on_actionRGB_Grayscale_triggered() {
    update_greyscale_checkboxes(ui, ui->actionRGB_Grayscale, 1);
}

void MainWindow::on_actionAmber_monitor_triggered() {
    update_greyscale_checkboxes(ui, ui->actionAmber_monitor, 2);
}

void MainWindow::on_actionGreen_monitor_triggered() {
    update_greyscale_checkboxes(ui, ui->actionGreen_monitor, 3);
}

void MainWindow::on_actionWhite_monitor_triggered() {
    update_greyscale_checkboxes(ui, ui->actionWhite_monitor, 4);
}

static void update_greyscale_type_checkboxes(Ui::MainWindow* ui, QAction* selected, int value) {
    ui->actionBT601_NTSC_PAL->setChecked(ui->actionBT601_NTSC_PAL == selected);
    ui->actionBT709_HDTV->setChecked(ui->actionBT709_HDTV == selected);
    ui->actionAverage->setChecked(ui->actionAverage == selected);

    video_graytype = value;
    device_force_redraw();
    config_save();
}

void MainWindow::on_actionBT601_NTSC_PAL_triggered() {
    update_greyscale_type_checkboxes(ui, ui->actionBT601_NTSC_PAL, 0);
}

void MainWindow::on_actionBT709_HDTV_triggered() {
    update_greyscale_type_checkboxes(ui, ui->actionBT709_HDTV, 1);
}

void MainWindow::on_actionAverage_triggered() {
    update_greyscale_type_checkboxes(ui, ui->actionAverage, 2);
}
