/*
 * 86Box	A hypervisor and IBM PC system emulator that specializes in
 *		running old operating systems and software designed for IBM
 *		PC systems and compatibles from 1981 through fairly recent
 *		system designs based on the PCI bus.
 *
 *		This file is part of the 86Box distribution.
 *
 *		Main window module.
 *
 *
 *
 * Authors:	Joakim L. Gilje <jgilje@jgilje.net>
 *          Cacodemon345
 *          Teemu Korhonen
 *          dob205
 *
 *		Copyright 2021 Joakim L. Gilje
 *      Copyright 2021-2022 Cacodemon345
 *      Copyright 2021-2022 Teemu Korhonen
 *      Copyright 2022 dob205
 */
#include "qt_mainwindow.hpp"
#include "ui_qt_mainwindow.h"

#include "qt_specifydimensions.h"
#include "qt_soundgain.hpp"
#include "qt_progsettings.hpp"

#include "qt_rendererstack.hpp"
#include "qt_renderercommon.hpp"

extern "C" {
#include <86box/86box.h>
#include <86box/config.h>
#include <86box/keyboard.h>
#include <86box/plat.h>
#include <86box/discord.h>
#include <86box/video.h>
#include <86box/vid_ega.h>
#include <86box/version.h>

    extern int qt_nvr_save(void);

#ifdef MTR_ENABLED
#include <minitrace/minitrace.h>
#endif
};

#include <QGuiApplication>
#include <QWindow>
#include <QTimer>
#include <QThread>
#include <QKeyEvent>
#include <QMessageBox>
#include <QFocusEvent>
#include <QApplication>
#include <QPushButton>
#include <QDesktopServices>
#include <QUrl>
#include <QCheckBox>
#include <QActionGroup>
#include <QOpenGLContext>
#include <QScreen>
#include <QString>

#include <array>
#include <unordered_map>

#include "qt_settings.hpp"
#include "qt_machinestatus.hpp"
#include "qt_mediamenu.hpp"
#include "qt_util.hpp"

#if defined __unix__ && !defined __HAIKU__
#ifdef WAYLAND
#include "wl_mouse.hpp"
#endif
#include <X11/Xlib.h>
#include <X11/keysym.h>
#undef KeyPress
#undef KeyRelease
#endif

#ifdef Q_OS_MACOS
// The namespace is required to avoid clashing typedefs; we only use this
// header for its #defines anyway.
namespace IOKit {
    #include <IOKit/hidsystem/IOLLEvent.h>
}
#endif

#ifdef __HAIKU__
#include <os/AppKit.h>
#include <os/InterfaceKit.h>

extern MainWindow* main_window;

filter_result keyb_filter(BMessage *message, BHandler **target, BMessageFilter *filter)
{
    if (message->what == B_KEY_DOWN || message->what == B_KEY_UP
    ||  message->what == B_UNMAPPED_KEY_DOWN || message->what == B_UNMAPPED_KEY_UP)
    {
        int key_state = 0, key_scancode = 0;
        key_state = message->what == B_KEY_DOWN || message->what == B_UNMAPPED_KEY_DOWN;
        message->FindInt32("key", &key_scancode);
        QGuiApplication::postEvent(main_window, new QKeyEvent(key_state ? QEvent::KeyPress : QEvent::KeyRelease, 0, QGuiApplication::keyboardModifiers(), key_scancode, 0, 0));
        if (key_scancode == 0x68 && key_state)
        {
            QGuiApplication::postEvent(main_window, new QKeyEvent(QEvent::KeyRelease, 0, QGuiApplication::keyboardModifiers(), key_scancode, 0, 0));
        }
    }
    return B_DISPATCH_MESSAGE;
}

static BMessageFilter* filter;
#endif

extern void qt_mouse_capture(int);
extern "C" void qt_blit(int x, int y, int w, int h);

MainWindow::MainWindow(QWidget *parent) :
    QMainWindow(parent),
    ui(new Ui::MainWindow)
{
    mm = std::make_shared<MediaMenu>(this);
    MediaMenu::ptr = mm;
    status = std::make_unique<MachineStatus>(this);

#ifdef __HAIKU__
    filter = new BMessageFilter(B_PROGRAMMED_DELIVERY, B_ANY_SOURCE, keyb_filter);
    ((BWindow*)this->winId())->AddFilter(filter);
#endif
    setUnifiedTitleAndToolBarOnMac(true);
    ui->setupUi(this);
    ui->stackedWidget->setMouseTracking(true);
    statusBar()->setVisible(!hide_status_bar);
    statusBar()->setStyleSheet("QStatusBar::item {border: None; } QStatusBar QLabel { margin-right: 2px; margin-bottom: 1px; }");
    ui->toolBar->setVisible(!hide_tool_bar);

    auto toolbar_spacer = new QWidget();
    toolbar_spacer->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    ui->toolBar->addWidget(toolbar_spacer);

    auto toolbar_label = new QLabel();
    ui->toolBar->addWidget(toolbar_label);

#ifdef RELEASE_BUILD
    this->setWindowIcon(QIcon(":/settings/win/icons/86Box-green.ico"));
#elif defined ALPHA_BUILD
    this->setWindowIcon(QIcon(":/settings/win/icons/86Box-red.ico"));
#elif defined BETA_BUILD
    this->setWindowIcon(QIcon(":/settings/win/icons/86Box-yellow.ico"));
#else
    this->setWindowIcon(QIcon(":/settings/win/icons/86Box-gray.ico"));
#endif
    this->setWindowFlag(Qt::MSWindowsFixedSizeDialogHint, vid_resize != 1);
    this->setWindowFlag(Qt::WindowMaximizeButtonHint, vid_resize == 1);

    QString vmname(vm_name);
    if (vmname.at(vmname.size() - 1) == '"' || vmname.at(vmname.size() - 1) == '\'') vmname.truncate(vmname.size() - 1);
    this->setWindowTitle(QString("%1 - %2 %3").arg(vmname, EMU_NAME, EMU_VERSION_FULL));

    connect(this, &MainWindow::showMessageForNonQtThread, this, &MainWindow::showMessage_, Qt::BlockingQueuedConnection);

    connect(this, &MainWindow::setTitle, this, [this,toolbar_label](const QString& title) {
        if (dopause && !hide_tool_bar)
        {
            toolbar_label->setText(toolbar_label->text() + tr(" - PAUSED"));
            return;
        }
        if (!hide_tool_bar)
#ifdef _WIN32
            toolbar_label->setText(title);
#else
        {
            /* get the percentage and mouse message, TODO: refactor ui_window_title() */
            auto parts = title.split(" - ");
            if (parts.size() >= 2) {
                if (parts.size() < 5)
                    toolbar_label->setText(parts[1]);
                else
                    toolbar_label->setText(QString("%1 - %2").arg(parts[1], parts.last()));
            }
        }
#endif
        ui->actionPause->setChecked(dopause);
    });
    connect(this, &MainWindow::getTitleForNonQtThread, this, &MainWindow::getTitle_, Qt::BlockingQueuedConnection);

    connect(this, &MainWindow::updateMenuResizeOptions, [this]() {
        ui->actionResizable_window->setEnabled(vid_resize != 2);
        ui->actionResizable_window->setChecked(vid_resize == 1);
        ui->menuWindow_scale_factor->setEnabled(vid_resize == 0);
    });

    connect(this, &MainWindow::updateWindowRememberOption, [this]() {
        ui->actionRemember_size_and_position->setChecked(window_remember);
    });

    emit updateMenuResizeOptions();

    connect(this, &MainWindow::pollMouse, ui->stackedWidget, &RendererStack::mousePoll, Qt::DirectConnection);

    connect(this, &MainWindow::setMouseCapture, this, [this](bool state) {
        mouse_capture = state ? 1 : 0;
        qt_mouse_capture(mouse_capture);
        if (mouse_capture) {
            this->grabKeyboard();
#ifdef WAYLAND
            if (QGuiApplication::platformName().contains("wayland")) {
                wl_mouse_capture(this->windowHandle());
            }
#endif
        } else {
            this->releaseKeyboard();
#ifdef WAYLAND
            if (QGuiApplication::platformName().contains("wayland")) {
                wl_mouse_uncapture();
            }
#endif
        }
    });

    connect(qApp, &QGuiApplication::applicationStateChanged, [this](Qt::ApplicationState state) {
        if (mouse_capture && state != Qt::ApplicationState::ApplicationActive)
            emit setMouseCapture(false);
    });

    connect(this, &MainWindow::resizeContents, this, [this](int w, int h) {
        if (!QApplication::platformName().contains("eglfs") && vid_resize == 0) {
            w = (w / (!dpi_scale ? util::screenOfWidget(this)->devicePixelRatio() : 1.));

            int modifiedHeight = (h / (!dpi_scale ? util::screenOfWidget(this)->devicePixelRatio() : 1.))
                + menuBar()->height()
                + (statusBar()->height() * !hide_status_bar)
                + (ui->toolBar->height() * !hide_tool_bar);

            ui->stackedWidget->resize(w, h);
            setFixedSize(w, modifiedHeight);
        }
    });

    connect(ui->menubar, &QMenuBar::triggered, this, [this] {
        config_save();
        if (QApplication::activeWindow() == this)
        {
            ui->stackedWidget->setFocusRenderer();
        }
    });

    connect(this, &MainWindow::updateStatusBarPanes, this, [this] {
        refreshMediaMenu();
    });
    connect(this, &MainWindow::updateStatusBarPanes, this, &MainWindow::refreshMediaMenu);
    connect(this, &MainWindow::updateStatusBarTip, status.get(), &MachineStatus::updateTip);
    connect(this, &MainWindow::updateStatusBarActivity, status.get(), &MachineStatus::setActivity);
    connect(this, &MainWindow::updateStatusBarEmpty, status.get(), &MachineStatus::setEmpty);
    connect(this, &MainWindow::statusBarMessage, status.get(), &MachineStatus::message, Qt::QueuedConnection);

    ui->actionKeyboard_requires_capture->setChecked(kbd_req_capture);
    ui->actionRight_CTRL_is_left_ALT->setChecked(rctrl_is_lalt);
    ui->actionResizable_window->setChecked(vid_resize == 1);
    ui->actionRemember_size_and_position->setChecked(window_remember);
    ui->menuWindow_scale_factor->setEnabled(vid_resize == 0);
    ui->actionHiDPI_scaling->setChecked(dpi_scale);
    ui->actionHide_status_bar->setChecked(hide_status_bar);
    ui->actionHide_tool_bar->setChecked(hide_tool_bar);
    ui->actionUpdate_status_bar_icons->setChecked(update_icons);
    ui->actionEnable_Discord_integration->setChecked(enable_discord);

#if defined Q_OS_WINDOWS || defined Q_OS_MACOS
    /* Make the option visible only if ANGLE is loaded. */
    ui->actionHardware_Renderer_OpenGL_ES->setVisible(QOpenGLContext::openGLModuleType() == QOpenGLContext::LibGLES);
    if (QOpenGLContext::openGLModuleType() != QOpenGLContext::LibGLES && vid_api == 2) vid_api = 1;
#endif
    ui->actionHardware_Renderer_OpenGL->setVisible(QOpenGLContext::openGLModuleType() != QOpenGLContext::LibGLES);
    if (QOpenGLContext::openGLModuleType() == QOpenGLContext::LibGLES && vid_api == 1) vid_api = 0;

    if ((QApplication::platformName().contains("eglfs") || QApplication::platformName() == "haiku")) {
        if (vid_api >= 1) fprintf(stderr, "OpenGL renderers are unsupported on %s.\n", QApplication::platformName().toUtf8().data());
        vid_api = 0;
        ui->actionHardware_Renderer_OpenGL->setVisible(false);
        ui->actionHardware_Renderer_OpenGL_ES->setVisible(false);
        ui->actionOpenGL_3_0_Core->setVisible(false);
    }

    QActionGroup* actGroup = nullptr;

    actGroup = new QActionGroup(this);
    actGroup->addAction(ui->actionSoftware_Renderer);
    actGroup->addAction(ui->actionHardware_Renderer_OpenGL);
    actGroup->addAction(ui->actionHardware_Renderer_OpenGL_ES);
    actGroup->addAction(ui->actionOpenGL_3_0_Core);
    actGroup->setExclusive(true);

    connect(actGroup, &QActionGroup::triggered, [this](QAction* action) {
        vid_api = action->property("vid_api").toInt();
        switch (vid_api)
        {
        case 0:
            ui->stackedWidget->switchRenderer(RendererStack::Renderer::Software);
            break;
        case 1:
            ui->stackedWidget->switchRenderer(RendererStack::Renderer::OpenGL);
            break;
        case 2:
            ui->stackedWidget->switchRenderer(RendererStack::Renderer::OpenGLES);
            break;
        case 3:
            ui->stackedWidget->switchRenderer(RendererStack::Renderer::OpenGL3);
            break;
        }
    });

    connect(ui->stackedWidget, &RendererStack::rendererChanged, [this]() {
        ui->actionRenderer_options->setVisible(ui->stackedWidget->hasOptions());
    });

    /* Trigger initial renderer switch */
    for (auto action : actGroup->actions())
        if (action->property("vid_api").toInt() == vid_api) {
            action->setChecked(true);
            emit actGroup->triggered(action);
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
    actGroup = new QActionGroup(this);
    actGroup->addAction(ui->action0_5x);
    actGroup->addAction(ui->action1x);
    actGroup->addAction(ui->action1_5x);
    actGroup->addAction(ui->action2x);
    switch (video_filter_method) {
    case 0:
        ui->actionNearest->setChecked(true);
        break;
    case 1:
        ui->actionLinear->setChecked(true);
        break;
    }
    actGroup = new QActionGroup(this);
    actGroup->addAction(ui->actionNearest);
    actGroup->addAction(ui->actionLinear);
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
    actGroup = new QActionGroup(this);
    actGroup->addAction(ui->actionFullScreen_stretch);
    actGroup->addAction(ui->actionFullScreen_43);
    actGroup->addAction(ui->actionFullScreen_keepRatio);
    actGroup->addAction(ui->actionFullScreen_int);
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
    actGroup = new QActionGroup(this);
    actGroup->addAction(ui->actionRGB_Grayscale);
    actGroup->addAction(ui->actionAmber_monitor);
    actGroup->addAction(ui->actionGreen_monitor);
    actGroup->addAction(ui->actionWhite_monitor);
    actGroup->addAction(ui->actionRGB_Color);
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
    actGroup = new QActionGroup(this);
    actGroup->addAction(ui->actionBT601_NTSC_PAL);
    actGroup->addAction(ui->actionBT709_HDTV);
    actGroup->addAction(ui->actionAverage);
    if (force_43 > 0) {
        ui->actionForce_4_3_display_ratio->setChecked(true);
    }
    if (enable_overscan > 0) {
        ui->actionCGA_PCjr_Tandy_EGA_S_VGA_overscan->setChecked(true);
    }
    if (vid_cga_contrast > 0) {
        ui->actionChange_contrast_for_monochrome_display->setChecked(true);
    }

#ifdef Q_OS_MACOS
    ui->actionFullscreen->setShortcutVisibleInContextMenu(true);
    ui->actionCtrl_Alt_Del->setShortcutVisibleInContextMenu(true);
    ui->actionTake_screenshot->setShortcutVisibleInContextMenu(true);
#endif
    video_setblit(qt_blit);

    if (start_in_fullscreen) {
        connect(ui->stackedWidget, &RendererStack::blitToRenderer, this, [this] () {
            if (start_in_fullscreen) {
                QTimer::singleShot(100, ui->actionFullscreen, &QAction::trigger);
                start_in_fullscreen = 0;
            }
        });
    }

#ifdef MTR_ENABLED
    {
        ui->actionBegin_trace->setVisible(true);
        ui->actionEnd_trace->setVisible(true);
        ui->actionBegin_trace->setShortcut(QKeySequence(Qt::Key_Control + Qt::Key_T));
        ui->actionEnd_trace->setShortcut(QKeySequence(Qt::Key_Control + Qt::Key_T));
        ui->actionEnd_trace->setDisabled(true);
        static auto init_trace = [&]
        {
            mtr_init("trace.json");
            mtr_start();
        };
        static auto shutdown_trace = [&]
        {
            mtr_stop();
            mtr_shutdown();
        };
#ifdef Q_OS_MACOS
        ui->actionBegin_trace->setShortcutVisibleInContextMenu(true);
        ui->actionEnd_trace->setShortcutVisibleInContextMenu(true);
#endif
        static bool trace = false;
        connect(ui->actionBegin_trace, &QAction::triggered, this, [this]
        {
            if (trace) return;
            ui->actionBegin_trace->setDisabled(true);
            ui->actionEnd_trace->setDisabled(false);
            init_trace();
            trace = true;
        });
        connect(ui->actionEnd_trace, &QAction::triggered, this, [this]
        {
            if (!trace) return;
            ui->actionBegin_trace->setDisabled(false);
            ui->actionEnd_trace->setDisabled(true);
            shutdown_trace();
            trace = false;
        });
    }
#endif

    setContextMenuPolicy(Qt::PreventContextMenu);
}

void MainWindow::closeEvent(QCloseEvent *event) {
    if (mouse_capture) {
        event->ignore();
        return;
    }

    if (confirm_exit && confirm_exit_cmdl && cpu_thread_run)
    {
        QMessageBox questionbox(QMessageBox::Icon::Question, "86Box", tr("Are you sure you want to exit 86Box?"), QMessageBox::Yes | QMessageBox::No, this);
        QCheckBox *chkbox = new QCheckBox(tr("Don't show this message again"));
        questionbox.setCheckBox(chkbox);
        chkbox->setChecked(!confirm_exit);

        QObject::connect(chkbox, &QCheckBox::stateChanged, [](int state) {
            confirm_exit = (state == Qt::CheckState::Unchecked);
        });
        questionbox.exec();
        if (questionbox.result() == QMessageBox::No) {
            confirm_exit = true;
            event->ignore();
            return;
        }
    }
    if (window_remember) {
        window_w = ui->stackedWidget->width();
        window_h = ui->stackedWidget->height();
        if (!QApplication::platformName().contains("wayland")) {
            window_x = this->geometry().x();
            window_y = this->geometry().y();
        }
    }
    qt_nvr_save();
    config_save();
#if defined __unix__ && !defined __HAIKU__
    extern void xinput2_exit();
    if (QApplication::platformName() == "xcb") xinput2_exit();
#endif
    event->accept();
}

MainWindow::~MainWindow() {
    delete ui;
}

void MainWindow::showEvent(QShowEvent *event) {
    if (shownonce) return;
    shownonce = true;
    if (window_remember && !QApplication::platformName().contains("wayland")) {
        setGeometry(window_x, window_y, window_w, window_h + menuBar()->height() + (hide_status_bar ? 0 : statusBar()->height()) + (hide_tool_bar ? 0 : ui->toolBar->height()));
    }
    if (vid_resize == 2) {
        setFixedSize(fixed_size_x, fixed_size_y
            + menuBar()->height()
            + (hide_status_bar ? 0 : statusBar()->height())
            + (hide_tool_bar ? 0 : ui->toolBar->height()));

        scrnsz_x = fixed_size_x;
        scrnsz_y = fixed_size_y;
    }
    else if (window_remember && vid_resize == 1) {
        ui->stackedWidget->setFixedSize(window_w, window_h);
        adjustSize();
        ui->stackedWidget->setFixedSize(QWIDGETSIZE_MAX, QWIDGETSIZE_MAX);
        scrnsz_x = window_w;
        scrnsz_y = window_h;
    }
}

void MainWindow::on_actionKeyboard_requires_capture_triggered() {
    kbd_req_capture ^= 1;
}

void MainWindow::on_actionRight_CTRL_is_left_ALT_triggered() {
    rctrl_is_lalt ^= 1;
}

void MainWindow::on_actionHard_Reset_triggered() {
    if (confirm_reset)
    {
        QMessageBox questionbox(QMessageBox::Icon::Question, "86Box", tr("Are you sure you want to hard reset the emulated machine?"), QMessageBox::NoButton, this);
        questionbox.addButton(tr("Don't reset"), QMessageBox::AcceptRole);
        questionbox.addButton(tr("Reset"), QMessageBox::RejectRole);
        QCheckBox *chkbox = new QCheckBox(tr("Don't show this message again"));
        questionbox.setCheckBox(chkbox);
        chkbox->setChecked(!confirm_reset);

        QObject::connect(chkbox, &QCheckBox::stateChanged, [](int state) {
            confirm_reset = (state == Qt::CheckState::Unchecked);
        });
        questionbox.exec();
        if (questionbox.result() == QDialog::Rejected)
        {
            confirm_reset = true;
            return;
        }
    }
    config_changed = 2;
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

#ifdef __HAIKU__
static std::unordered_map<uint8_t, uint16_t> be_to_xt =
{
    {0x01, 0x01},
    {B_F1_KEY, 0x3B},
    {B_F2_KEY, 0x3C},
    {B_F3_KEY, 0x3D},
    {B_F4_KEY, 0x3E},
    {B_F5_KEY, 0x3F},
    {B_F6_KEY, 0x40},
    {B_F7_KEY, 0x41},
    {B_F8_KEY, 0x42},
    {B_F9_KEY, 0x43},
    {B_F10_KEY, 0x44},
    {B_F11_KEY, 0x57},
    {B_F12_KEY, 0x58},
    {0x11, 0x29},
    {0x12, 0x02},
    {0x13, 0x03},
    {0x14, 0x04},
    {0x15, 0x05},
    {0x16, 0x06},
    {0x17, 0x07},
    {0x18, 0x08},
    {0x19, 0x09},
    {0x1A, 0x0A},
    {0x1B, 0x0B},
    {0x1C, 0x0C},
    {0x1D, 0x0D},
    {0x1E, 0x0E},
    {0x1F, 0x152},
    {0x20, 0x147},
    {0x21, 0x149},
    {0x22, 0x45},
    {0x23, 0x135},
    {0x24, 0x37},
    {0x25, 0x4A},
    {0x26, 0x0F},
    {0x27, 0x10},
    {0x28, 0x11},
    {0x29, 0x12},
    {0x2A, 0x13},
    {0x2B, 0x14},
    {0x2C, 0x15},
    {0x2D, 0x16},
    {0x2E, 0x17},
    {0x2F, 0x18},
    {0x30, 0x19},
    {0x31, 0x1A},
    {0x32, 0x1B},
    {0x33, 0x2B},
    {0x34, 0x153},
    {0x35, 0x14F},
    {0x36, 0x151},
    {0x37, 0x47},
    {0x38, 0x48},
    {0x39, 0x49},
    {0x3A, 0x4E},
    {0x3B, 0x3A},
    {0x3C, 0x1E},
    {0x3D, 0x1F},
    {0x3E, 0x20},
    {0x3F, 0x21},
    {0x40, 0x22},
    {0x41, 0x23},
    {0x42, 0x24},
    {0x43, 0x25},
    {0x44, 0x26},
    {0x45, 0x27},
    {0x46, 0x28},
    {0x47, 0x1C},
    {0x48, 0x4B},
    {0x49, 0x4C},
    {0x4A, 0x4D},
    {0x4B, 0x2A},
    {0x4C, 0x2C},
    {0x4D, 0x2D},
    {0x4E, 0x2E},
    {0x4F, 0x2F},
    {0x50, 0x30},
    {0x51, 0x31},
    {0x52, 0x32},
    {0x53, 0x33},
    {0x54, 0x34},
    {0x55, 0x35},
    {0x56, 0x36},
    {0x57, 0x148},
    {0x58, 0x51},
    {0x59, 0x50},
    {0x5A, 0x4F},
    {0x5B, 0x11C},
    {0x5C, 0x1D},
    {0x5D, 0x38},
    {0x5E, 0x39},
    {0x5F, 0x138},
    {0x60, 0x11D},
    {0x61, 0x14B},
    {0x62, 0x150},
    {0x63, 0x14D},
    {0x64, 0x52},
    {0x65, 0x53},

    {0x0e, 0x137},
    {0x0f, 0x46},
    {0x66, 0x15B},
    {0x67, 0x15C},
    {0x68, 0x15D},
    {0x69, 0x56}
};
#endif

static std::array<uint32_t, 256>& selected_keycode = x11_to_xt_base;

uint16_t x11_keycode_to_keysym(uint32_t keycode)
{
    uint16_t finalkeycode = 0;
#if defined(Q_OS_WINDOWS)
    finalkeycode = (keycode & 0xFFFF);
#elif defined(Q_OS_MACOS)
    finalkeycode = darwin_to_xt[keycode];
#elif defined(__HAIKU__)
    finalkeycode = be_to_xt[keycode];
#else
    static Display* x11display = nullptr;
    if (QApplication::platformName().contains("wayland"))
    {
        selected_keycode = x11_to_xt_2;
    }
    else if (QApplication::platformName().contains("eglfs"))
    {
        keycode -= 8;
        if (keycode <= 88) finalkeycode = keycode;
        else finalkeycode = evdev_to_xt[keycode];
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
    if (!QApplication::platformName().contains("eglfs")) finalkeycode =  selected_keycode[keycode];
#endif
    if (rctrl_is_lalt && finalkeycode == 0x11D)
    {
        finalkeycode = 0x38;
    }
    return finalkeycode;
}

#ifdef Q_OS_MACOS
// These modifiers are listed as "device-dependent" in IOLLEvent.h, but
// that's followed up with "(really?)". It's the only way to distinguish
// left and right modifiers with Qt 6 on macOS, so let's just roll with it.
static std::unordered_map<uint32_t, uint16_t> mac_modifiers_to_xt = {
    {NX_DEVICELCTLKEYMASK, 0x1D},
    {NX_DEVICELSHIFTKEYMASK, 0x2A},
    {NX_DEVICERSHIFTKEYMASK, 0x36},
    {NX_DEVICELCMDKEYMASK, 0x15B},
    {NX_DEVICERCMDKEYMASK, 0x15C},
    {NX_DEVICELALTKEYMASK, 0x38},
    {NX_DEVICERALTKEYMASK, 0x138},
    {NX_DEVICE_ALPHASHIFT_STATELESS_MASK, 0x3A},
    {NX_DEVICERCTLKEYMASK, 0x11D},
};

void MainWindow::processMacKeyboardInput(bool down, const QKeyEvent* event) {
    // Per QTBUG-69608 (https://bugreports.qt.io/browse/QTBUG-69608),
    // QKeyEvents QKeyEvents for presses/releases of modifiers on macOS give
    // nativeVirtualKey() == 0 (at least in Qt 6). Handle this by manually
    // processing the nativeModifiers(). We need to check whether the key() is
    // a known modifier because because kVK_ANSI_A is also 0, so the
    // nativeVirtualKey() == 0 condition is ambiguous...
    if (event->nativeVirtualKey() == 0
        && (event->key() == Qt::Key_Shift
            || event->key() == Qt::Key_Control
            || event->key() == Qt::Key_Meta
            || event->key() == Qt::Key_Alt
            || event->key() == Qt::Key_AltGr
            || event->key() == Qt::Key_CapsLock)) {
        // We only process one modifier at a time since events from Qt seem to
        // always be non-coalesced (NX_NONCOALESCEDMASK is always set).
        uint32_t changed_modifiers = last_modifiers ^ event->nativeModifiers();
        for (auto const& pair : mac_modifiers_to_xt) {
            if (changed_modifiers & pair.first) {
                last_modifiers ^= pair.first;
                keyboard_input(down, pair.second);
                return;
            }
        }

        // Caps Lock seems to be delivered as a single key press event when
        // enabled and a single key release event when disabled, so we can't
        // detect Caps Lock being held down; just send an infinitesimally-long
        // press and release as a compromise.
        //
        // The event also doesn't get delivered if you turn Caps Lock off after
        // turning it on when the window isn't focused. Doing better than this
        // probably requires bypassing Qt input processing.
        //
        // It's possible that other lock keys get delivered in this way, but
        // standard Apple keyboards don't have them, so this is untested.
        if (event->key() == Qt::Key_CapsLock) {
            keyboard_input(1, 0x3A);
            keyboard_input(0, 0x3A);
        }
    } else {
        keyboard_input(down, x11_keycode_to_keysym(event->nativeVirtualKey()));
    }
}
#endif

void MainWindow::on_actionFullscreen_triggered() {
    if (video_fullscreen > 0) {
        showNormal();
        ui->menubar->show();
        if (!hide_status_bar) ui->statusbar->show();
        if (!hide_tool_bar) ui->toolBar->show();
        video_fullscreen = 0;
        if (vid_resize != 1) {
            if (vid_resize == 2) setFixedSize(fixed_size_x, fixed_size_y
                + menuBar()->height()
                + (!hide_status_bar ? statusBar()->height() : 0)
                + (!hide_tool_bar ? ui->toolBar->height() : 0));

            emit resizeContents(scrnsz_x, scrnsz_y);
        }
    } else {
        if (video_fullscreen_first)
        {
            bool wasCaptured = mouse_capture == 1;

            QMessageBox questionbox(QMessageBox::Icon::Information, tr("Entering fullscreen mode"), tr("Press Ctrl+Alt+PgDn to return to windowed mode."), QMessageBox::Ok, this);
            QCheckBox *chkbox = new QCheckBox(tr("Don't show this message again"));
            questionbox.setCheckBox(chkbox);
            chkbox->setChecked(!video_fullscreen_first);

            QObject::connect(chkbox, &QCheckBox::stateChanged, [](int state) {
                video_fullscreen_first = (state == Qt::CheckState::Unchecked);
            });
            questionbox.exec();
            config_save();

            /* (re-capture mouse after dialog. */
            if (wasCaptured)
                emit setMouseCapture(true);
        }
        video_fullscreen = 1;
        setFixedSize(QWIDGETSIZE_MAX, QWIDGETSIZE_MAX);
        ui->menubar->hide();
        ui->statusbar->hide();
        ui->toolBar->hide();
        showFullScreen();
    }
    ui->stackedWidget->onResize(width(), height());
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
    if (!dopause && (mouse_capture || !kbd_req_capture)) {
        if (event->type() == QEvent::Shortcut) {
            auto shortcutEvent = (QShortcutEvent*)event;
            if (shortcutEvent->key() == ui->actionExit->shortcut()) {
                event->accept();
                return true;
            }
        }
        if (event->type() == QEvent::KeyPress) {
            event->accept();
            this->keyPressEvent((QKeyEvent *) event);
            return true;
        }
        if (event->type() == QEvent::KeyRelease) {
            event->accept();
            this->keyReleaseEvent((QKeyEvent *) event);
            return true;
        }
    }

    if (receiver == this) {
        static auto curdopause = dopause;
        if (event->type() == QEvent::WindowBlocked) {
            curdopause = dopause;
            plat_pause(1);
            emit setMouseCapture(false);
        } else if (event->type() == QEvent::WindowUnblocked) {
            plat_pause(curdopause);
        }
    }

    return QMainWindow::eventFilter(receiver, event);
}

void MainWindow::refreshMediaMenu() {
    mm->refresh(ui->menuMedia);
    status->refresh(ui->statusbar);
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
    box.setTextFormat(Qt::TextFormat::RichText);
    box.exec();
}

void MainWindow::keyPressEvent(QKeyEvent* event)
{
    if (send_keyboard_input && !(kbd_req_capture && !mouse_capture && !video_fullscreen))
    {
#ifdef Q_OS_MACOS
        processMacKeyboardInput(true, event);
#else
        keyboard_input(1, x11_keycode_to_keysym(event->nativeScanCode()));
#endif
    }

    if ((video_fullscreen > 0) && keyboard_isfsexit()) {
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
    if (!send_keyboard_input)
        return;

#ifdef Q_OS_MACOS
    processMacKeyboardInput(false, event);
#else
    keyboard_input(0, x11_keycode_to_keysym(event->nativeScanCode()));
#endif
}

QSize MainWindow::getRenderWidgetSize()
{
    return ui->stackedWidget->size();
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
        setWindowFlag(Qt::WindowMaximizeButtonHint);
        setWindowFlag(Qt::MSWindowsFixedSizeDialogHint, false);
        setFixedSize(QWIDGETSIZE_MAX, QWIDGETSIZE_MAX);
    } else {
        vid_resize = 0;
        setWindowFlag(Qt::WindowMaximizeButtonHint, false);
        setWindowFlag(Qt::MSWindowsFixedSizeDialogHint);
    }
    show();
    ui->stackedWidget->switchRenderer((RendererStack::Renderer)vid_api);

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
    atomic_flag_clear(&doresize);
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
        ui->stackedWidget->onResize(widget->width(), widget->height());
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

void MainWindow::on_actionAbout_Qt_triggered()
{
    QApplication::aboutQt();
}

void MainWindow::on_actionAbout_86Box_triggered()
{
    QMessageBox msgBox;
    msgBox.setTextFormat(Qt::RichText);
    QString githash;
#ifdef EMU_GIT_HASH
    githash = QString(" [%1]").arg(EMU_GIT_HASH);
#endif
    msgBox.setText(QString("<b>%3%1%2</b>").arg(EMU_VERSION_FULL, githash, tr("86Box v")));
    msgBox.setInformativeText(tr("An emulator of old computers\n\nAuthors: Sarah Walker, Miran Grca, Fred N. van Kempen (waltje), SA1988, Tiseno100, reenigne, leilei, JohnElliott, greatpsycho, and others.\n\nReleased under the GNU General Public License version 2 or later. See LICENSE for more information."));
    msgBox.setWindowTitle("About 86Box");
    msgBox.addButton("OK", QMessageBox::ButtonRole::AcceptRole);
    auto webSiteButton = msgBox.addButton(EMU_SITE, QMessageBox::ButtonRole::HelpRole);
    webSiteButton->connect(webSiteButton, &QPushButton::released, []()
    {
        QDesktopServices::openUrl(QUrl("https://" EMU_SITE));
    });
#ifdef RELEASE_BUILD
    msgBox.setIconPixmap(QIcon(":/settings/win/icons/86Box-green.ico").pixmap(32, 32));
#elif defined ALPHA_BUILD
    msgBox.setIconPixmap(QIcon(":/settings/win/icons/86Box-red.ico").pixmap(32, 32));
#elif defined BETA_BUILD
    msgBox.setIconPixmap(QIcon(":/settings/win/icons/86Box-yellow.ico").pixmap(32, 32));
#else
    msgBox.setIconPixmap(QIcon(":/settings/win/icons/86Box-gray.ico").pixmap(32, 32));
#endif
    msgBox.setWindowFlags(Qt::Dialog | Qt::CustomizeWindowHint | Qt::WindowTitleHint | Qt::WindowCloseButtonHint);
    msgBox.exec();
}

void MainWindow::on_actionDocumentation_triggered()
{
     QDesktopServices::openUrl(QUrl(EMU_DOCS_URL));
}

void MainWindow::on_actionCGA_PCjr_Tandy_EGA_S_VGA_overscan_triggered() {
    update_overscan = 1;
    video_toggle_option(ui->actionCGA_PCjr_Tandy_EGA_S_VGA_overscan, &enable_overscan);
}

void MainWindow::on_actionChange_contrast_for_monochrome_display_triggered() {
    vid_cga_contrast ^= 1;
    cgapal_rebuild();
    config_save();
}

void MainWindow::on_actionForce_4_3_display_ratio_triggered() {
    video_toggle_option(ui->actionForce_4_3_display_ratio, &force_43);
    video_force_resize_set(1);
}

void MainWindow::on_actionRemember_size_and_position_triggered()
{
    window_remember ^= 1;
    window_w = ui->stackedWidget->width();
    window_h = ui->stackedWidget->height();
    if (!QApplication::platformName().contains("wayland")) {
        window_x = geometry().x();
        window_y = geometry().y();
    }
    ui->actionRemember_size_and_position->setChecked(window_remember);
}

void MainWindow::on_actionSpecify_dimensions_triggered()
{
    SpecifyDimensions dialog(this);
    dialog.setWindowModality(Qt::WindowModal);
    dialog.exec();
}

void MainWindow::on_actionHiDPI_scaling_triggered()
{
    dpi_scale ^= 1;
    ui->actionHiDPI_scaling->setChecked(dpi_scale);
    emit resizeContents(scrnsz_x, scrnsz_y);
}

void MainWindow::on_actionHide_status_bar_triggered()
{
    hide_status_bar ^= 1;
    ui->actionHide_status_bar->setChecked(hide_status_bar);
    statusBar()->setVisible(!hide_status_bar);
    if (vid_resize >= 2) {
         setFixedSize(fixed_size_x, fixed_size_y
            + menuBar()->height()
            + (hide_status_bar ? 0 : statusBar()->height())
            + (hide_tool_bar ? 0 : ui->toolBar->height()));
    } else {
        int vid_resize_orig = vid_resize;
        vid_resize = 0;
        emit resizeContents(scrnsz_x, scrnsz_y);
        vid_resize = vid_resize_orig;
    }
}

void MainWindow::on_actionHide_tool_bar_triggered()
{
    hide_tool_bar ^= 1;
    ui->actionHide_tool_bar->setChecked(hide_tool_bar);
    ui->toolBar->setVisible(!hide_tool_bar);
    if (vid_resize >= 2) {
         setFixedSize(fixed_size_x, fixed_size_y
            + menuBar()->height()
            + (hide_status_bar ? 0 : statusBar()->height())
            + (hide_tool_bar ? 0 : ui->toolBar->height()));
    } else {
        int vid_resize_orig = vid_resize;
        vid_resize = 0;
        emit resizeContents(scrnsz_x, scrnsz_y);
        vid_resize = vid_resize_orig;
    }
}

void MainWindow::on_actionUpdate_status_bar_icons_triggered()
{
    update_icons ^= 1;
    ui->actionUpdate_status_bar_icons->setChecked(update_icons);
}

void MainWindow::on_actionTake_screenshot_triggered()
{
    startblit();
    screenshots++;
    endblit();
    device_force_redraw();
}

void MainWindow::on_actionSound_gain_triggered()
{
    SoundGain gain(this);
    gain.exec();
}

void MainWindow::setSendKeyboardInput(bool enabled)
{
    send_keyboard_input = enabled;
}

void MainWindow::on_actionPreferences_triggered()
{
    ProgSettings progsettings(this);
    progsettings.exec();
}


void MainWindow::on_actionEnable_Discord_integration_triggered(bool checked)
{
    enable_discord = checked;
    if(enable_discord) {
        discord_init();
        discord_update_activity(dopause);
    } else
        discord_close();
}

void MainWindow::showSettings()
{
    if (findChild<Settings*>() == nullptr)
        ui->actionSettings->trigger();
}

void MainWindow::hardReset()
{
    ui->actionHard_Reset->trigger();
}

void MainWindow::togglePause()
{
    ui->actionPause->trigger();
}

void MainWindow::changeEvent(QEvent* event)
{
#ifdef Q_OS_WINDOWS
    if (event->type() == QEvent::LanguageChange)
    {
        auto font_name = tr("FONT_NAME");
        auto font_size = tr("FONT_SIZE");
        QApplication::setFont(QFont(font_name, font_size.toInt()));
    }
#endif
    QWidget::changeEvent(event);
}

void MainWindow::on_actionRenderer_options_triggered()
{
    auto dlg = ui->stackedWidget->getOptions(this);

    if (dlg)
        dlg->exec();
}
