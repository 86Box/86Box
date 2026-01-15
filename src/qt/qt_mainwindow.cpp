/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          Main window module.
 *
 * Authors: Joakim L. Gilje <jgilje@jgilje.net>
 *          Cacodemon345
 *          Teemu Korhonen
 *          dob205
 *
 *          Copyright 2021 Joakim L. Gilje
 *          Copyright 2021-2022 Cacodemon345
 *          Copyright 2021-2022 Teemu Korhonen
 *          Copyright 2022 dob205
 */
#include <QDebug>

#include "qt_mainwindow.hpp"
#include "ui_qt_mainwindow.h"

#include "qt_specifydimensions.h"
#include "qt_soundgain.hpp"
#include "qt_progsettings.hpp"
#include "qt_mcadevicelist.hpp"

#include "qt_rendererstack.hpp"
#include "qt_renderercommon.hpp"

#include "qt_cgasettingsdialog.hpp"

extern "C" {
#include <86box/86box.h>
#include <86box/config.h>
#include <86box/keyboard.h>
#include <86box/plat.h>
#include <86box/ui.h>
#ifdef DISCORD
#    include <86box/discord.h>
#endif
#include <86box/device.h>
#include <86box/video.h>
#include <86box/mouse.h>
#include <86box/machine.h>
#include <86box/vid_ega.h>
#include <86box/version.h>
#include <86box/timer.h>
#include <86box/apm.h>
#include <86box/nvr.h>
#include <86box/acpi.h>
#include <86box/renderdefs.h>

#ifdef USE_VNC
#    include <86box/vnc.h>
#endif

extern int qt_nvr_save(void);

#ifdef MTR_ENABLED
#    include <minitrace/minitrace.h>
#endif

extern bool cpu_thread_running;
extern bool fast_forward;
};

#include <QGuiApplication>
#include <QWindow>
#include <QTimer>
#include <QThread>
#include <QKeyEvent>
#include <QShortcut>
#include <QMessageBox>
#include <QFocusEvent>
#include <QApplication>
#include <QPushButton>
#include <QDesktopServices>
#include <QUrl>
#include <QMenuBar>
#include <QCheckBox>
#include <QActionGroup>
#include <QOpenGLContext>
#include <QScreen>
#include <QString>
#include <QDir>
#include <QSysInfo>
#if QT_CONFIG(vulkan)
#    include <QVulkanInstance>
#    include <QVulkanFunctions>
#endif

void qt_set_sequence_auto_mnemonic(bool b);

#include <array>
#include <memory>
#include <unordered_map>

#include "qt_settings.hpp"
#include "qt_about.hpp"
#include "qt_machinestatus.hpp"
#include "qt_mediamenu.hpp"
#include "qt_util.hpp"

#if defined __unix__ && !defined __HAIKU__
#    ifndef Q_OS_MACOS
#        include "evdev_keyboard.hpp"
#    endif
#    ifdef XKBCOMMON
#        include "xkbcommon_keyboard.hpp"
#        ifdef XKBCOMMON_X11
#            include "xkbcommon_x11_keyboard.hpp"
#        endif
#        ifdef WAYLAND
#            include "xkbcommon_wl_keyboard.hpp"
#        endif
#    endif
#    include <X11/Xlib.h>
#    include <X11/keysym.h>
#    undef KeyPress
#    undef KeyRelease
#endif

#if defined Q_OS_UNIX && !defined Q_OS_HAIKU && !defined Q_OS_MACOS
#    include <qpa/qplatformwindow.h>
#    include "x11_util.h"
#endif

#ifdef Q_OS_MACOS
#    include "cocoa_keyboard.hpp"
// The namespace is required to avoid clashing typedefs; we only use this
// header for its #defines anyway.
namespace IOKit {
#    include <IOKit/hidsystem/IOLLEvent.h>
}
#endif

#ifdef __HAIKU__
#    include <os/AppKit.h>
#    include <os/InterfaceKit.h>
#    include "be_keyboard.hpp"

extern MainWindow *main_window;
QShortcut         *windowedShortcut;

filter_result
keyb_filter(BMessage *message, BHandler **target, BMessageFilter *filter)
{
    if (message->what == B_KEY_DOWN || message->what == B_KEY_UP
        || message->what == B_UNMAPPED_KEY_DOWN || message->what == B_UNMAPPED_KEY_UP) {
        int key_state = 0, key_scancode = 0;
        key_state = message->what == B_KEY_DOWN || message->what == B_UNMAPPED_KEY_DOWN;
        message->FindInt32("key", &key_scancode);
        QGuiApplication::postEvent(main_window, new QKeyEvent(key_state ? QEvent::KeyPress : QEvent::KeyRelease, 0, QGuiApplication::keyboardModifiers(), key_scancode, 0, 0));
        if (key_scancode == 0x68 && key_state) {
            QGuiApplication::postEvent(main_window, new QKeyEvent(QEvent::KeyRelease, 0, QGuiApplication::keyboardModifiers(), key_scancode, 0, 0));
        }
    }
    return B_DISPATCH_MESSAGE;
}

static BMessageFilter *filter;
#endif

extern int      cpu_force_interpreter;

extern void     qt_mouse_capture(int);
extern "C" void qt_blit(int x, int y, int w, int h, int monitor_index);

extern MainWindow *main_window;

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
    , ui(new Ui::MainWindow)
{
    mm             = std::make_shared<MediaMenu>(this);
    MediaMenu::ptr = mm;
    status         = std::make_unique<MachineStatus>(this);

#ifdef __HAIKU__
    filter = new BMessageFilter(B_PROGRAMMED_DELIVERY, B_ANY_SOURCE, keyb_filter);
    ((BWindow *) this->winId())->AddFilter(filter);
#endif
    setUnifiedTitleAndToolBarOnMac(true);
    extern MainWindow *main_window;
    main_window = this;
    ui->setupUi(this);
    status->setSoundMenu(ui->menuSound);
    ui->actionMute_Unmute->setText(sound_muted ? tr("&Unmute") : tr("&Mute"));
    ui->menuEGA_S_VGA_settings->menuAction()->setMenuRole(QAction::NoRole);
    ui->stackedWidget->setMouseTracking(true);
    statusBar()->setVisible(!hide_status_bar);

    auto    hertz_label    = new QLabel;
    QTimer *frameRateTimer = new QTimer(this);
    frameRateTimer->setInterval(1000);
    frameRateTimer->setSingleShot(false);
    connect(frameRateTimer, &QTimer::timeout, [hertz_label] {
        auto hz = monitors[0].mon_actualrenderedframes.load();
#ifdef SCREENSHOT_MODE
        hz = ((hz + 2) / 5) * 5;
#endif
        hertz_label->setText(tr("%1 Hz").arg(QString::number(hz) + (monitors[0].mon_interlace ? "i" : "")));
    });
    statusBar()->addPermanentWidget(hertz_label);
    frameRateTimer->start(1000);

    num_icon        = QIcon(":/settings/qt/icons/num_lock_on.ico");
    num_icon_off    = QIcon(":/settings/qt/icons/num_lock_off.ico");
    scroll_icon     = QIcon(":/settings/qt/icons/scroll_lock_on.ico");
    scroll_icon_off = QIcon(":/settings/qt/icons/scroll_lock_off.ico");
    caps_icon       = QIcon(":/settings/qt/icons/caps_lock_on.ico");
    caps_icon_off   = QIcon(":/settings/qt/icons/caps_lock_off.ico");
    kana_icon       = QIcon(":/settings/qt/icons/kana_lock_on.ico");
    kana_icon_off   = QIcon(":/settings/qt/icons/kana_lock_off.ico");

    num_label = new QLabel;
    num_label->setPixmap(num_icon_off.pixmap(QSize(16, 16)));
    num_label->setToolTip(QShortcut::tr("Num Lock"));
    statusBar()->addPermanentWidget(num_label);

    caps_label = new QLabel;
    caps_label->setPixmap(caps_icon_off.pixmap(QSize(16, 16)));
    caps_label->setToolTip(QShortcut::tr("Caps Lock"));
    statusBar()->addPermanentWidget(caps_label);

    scroll_label = new QLabel;
    scroll_label->setPixmap(scroll_icon_off.pixmap(QSize(16, 16)));
    scroll_label->setToolTip(QShortcut::tr("Scroll Lock"));
    statusBar()->addPermanentWidget(scroll_label);

    kana_label = new QLabel;
    kana_label->setPixmap(kana_icon_off.pixmap(QSize(16, 16)));
    kana_label->setToolTip(QShortcut::tr("Kana Lock"));
    statusBar()->addPermanentWidget(kana_label);

    QTimer *ledKeyboardTimer = new QTimer(this);
    ledKeyboardTimer->setTimerType(Qt::CoarseTimer);
    ledKeyboardTimer->setInterval(20);
    connect(ledKeyboardTimer, &QTimer::timeout, this, [this]() {
        uint8_t prev_caps = 255, prev_num = 255, prev_scroll = 255, prev_kana = 255;
        uint8_t caps, num, scroll, kana;
        keyboard_get_states(&caps, &num, &scroll, &kana);

        if (num_label->isVisible() && prev_num != num)
            num_label->setPixmap(num ? this->num_icon.pixmap(QSize(16, 16)) : this->num_icon_off.pixmap(QSize(16, 16)));
        if (caps_label->isVisible() && prev_caps != caps)
            caps_label->setPixmap(caps ? this->caps_icon.pixmap(QSize(16, 16)) : this->caps_icon_off.pixmap(QSize(16, 16)));
        if (scroll_label->isVisible() && prev_scroll != scroll)
            scroll_label->setPixmap(scroll ? this->scroll_icon.pixmap(QSize(16, 16)) : this->scroll_icon_off.pixmap(QSize(16, 16)));

        if (kana_label->isVisible() && prev_kana != kana)
            kana_label->setPixmap(kana ? this->kana_icon.pixmap(QSize(16, 16)) : this->kana_icon_off.pixmap(QSize(16, 16)));

        prev_caps   = caps;
        prev_num    = num;
        prev_scroll = scroll;
        prev_kana   = kana;
    });
    ledKeyboardTimer->start();

#ifdef Q_OS_WINDOWS
    util::setWin11RoundedCorners(this->winId(), (hide_status_bar ? false : true));
#endif
    statusBar()->setStyleSheet("QStatusBar::item {border: None; } QStatusBar QLabel { margin-right: 2px; margin-bottom: 1px; }");
    this->centralWidget()->setStyleSheet("background-color: black;");
    ui->toolBar->setVisible(!hide_tool_bar);
#ifdef _WIN32
    ui->toolBar->setBackgroundRole(QPalette::Light);
#endif
    renderers[0].reset(nullptr);
    auto toolbar_spacer = new QWidget();
    toolbar_spacer->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    ui->toolBar->addWidget(toolbar_spacer);

    auto toolbar_label = new QLabel();
    ui->toolBar->addWidget(toolbar_label);

    this->setWindowFlag(Qt::MSWindowsFixedSizeDialogHint, vid_resize != 1);
    this->setWindowFlag(Qt::WindowMaximizeButtonHint, vid_resize == 1);

    QString vmname(vm_name);
    if (vmname.at(vmname.size() - 1) == '"' || vmname.at(vmname.size() - 1) == '\'')
        vmname.truncate(vmname.size() - 1);
    this->setWindowTitle(QString("%1 - %2 %3").arg(vmname, EMU_NAME, EMU_VERSION_FULL));

    connect(this, &MainWindow::forceInterpretationCompleted, this, [this]() {
        const auto fi_icon      = cpu_force_interpreter ? QIcon(":/menuicons/qt/icons/recompiler.ico") :
                                                          QIcon(":/menuicons/qt/icons/interpreter.ico");
        const auto tooltip_text = cpu_force_interpreter ? QString(tr("Allow recompilation")) :
                                                          QString(tr("Force interpretation"));
        const auto menu_text    = cpu_force_interpreter ? QString(tr("&Allow recompilation")) :
                                                          QString(tr("&Force interpretation"));

        ui->actionForce_interpretation->setIcon(fi_icon);
        ui->actionForce_interpretation->setToolTip(tooltip_text);
        ui->actionForce_interpretation->setText(menu_text);
        ui->actionForce_interpretation->setChecked(cpu_force_interpreter);
        ui->actionForce_interpretation->setEnabled(cpu_use_dynarec);
    });

    connect(this, &MainWindow::hardResetCompleted, this, [this]() {
        ui->actionMCA_devices->setVisible(machine_has_bus(machine, MACHINE_BUS_MCA));
        ui_update_force_interpreter();
        num_label->setVisible(machine_has_bus(machine, MACHINE_BUS_PS2_PORTS | MACHINE_BUS_AT_KBD));
        scroll_label->setVisible(machine_has_bus(machine, MACHINE_BUS_PS2_PORTS | MACHINE_BUS_AT_KBD));
        caps_label->setVisible(machine_has_bus(machine, MACHINE_BUS_PS2_PORTS | MACHINE_BUS_AT_KBD));
        int ext_ax_kbd = machine_has_bus(machine, MACHINE_BUS_PS2_PORTS | MACHINE_BUS_AT_KBD) && (keyboard_type == KEYBOARD_TYPE_AX);
        int int_ax_kbd = machine_has_flags(machine, MACHINE_KEYBOARD_JIS) && !machine_has_bus(machine, MACHINE_BUS_PS2_PORTS);
        kana_label->setVisible(ext_ax_kbd || int_ax_kbd);
        while (QApplication::overrideCursor())
            QApplication::restoreOverrideCursor();
#ifdef USE_WACOM
        ui->menuTablet_tool->menuAction()->setVisible(mouse_input_mode >= 1);
#else
        ui->menuTablet_tool->menuAction()->setVisible(false);
#endif

        bool enable_comp_option = false;
        for (int i = 0; i < MONITORS_NUM; i++) {
            if (monitors[i].mon_composite) {
                enable_comp_option = true;
                break;
            }
        }

        ui->actionCGA_composite_settings->setEnabled(enable_comp_option);
    });

    connect(this, &MainWindow::showMessageForNonQtThread, this, &MainWindow::showMessage_, Qt::QueuedConnection);

    connect(this, &MainWindow::setTitle, this, [toolbar_label](const QString &title) {
        if (dopause && !hide_tool_bar) {
            toolbar_label->setText(toolbar_label->text() + tr(" - PAUSED"));
            return;
        }
        if (!hide_tool_bar)
            toolbar_label->setText(title);
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

    connect(this, &MainWindow::setMouseCapture, this, [this](bool state) {
        mouse_capture = state ? 1 : 0;
        qt_mouse_capture(mouse_capture);
        if (mouse_capture) {
            if (hook_enabled)
                this->grabKeyboard();
            if (ui->stackedWidget->mouse_capture_func)
                ui->stackedWidget->mouse_capture_func(this->windowHandle());
        } else {
            this->releaseKeyboard();
            if (ui->stackedWidget->mouse_uncapture_func) {
                ui->stackedWidget->mouse_uncapture_func();
            }
            ui->stackedWidget->unsetCursor();
        }
#ifndef Q_OS_MACOS
        if (kbd_req_capture) {
            qt_set_sequence_auto_mnemonic(!mouse_capture);
            /* Hack to get the menubar to update the internal Alt+shortcut table */
            if (!video_fullscreen) {
                ui->menubar->hide();
                ui->menubar->show();
            }
        }
#endif
    });

    connect(qApp, &QGuiApplication::applicationStateChanged, [this](Qt::ApplicationState state) {
        if (state == Qt::ApplicationState::ApplicationActive) {
            if (auto_paused) {
                plat_pause(0);
                auto_paused = 0;
            }
        } else {
            if (mouse_capture)
                emit setMouseCapture(false);

            keyboard_all_up();

            if (do_auto_pause && !dopause) {
                auto_paused = 1;
                plat_pause(1);
            }
        }
    });

    connect(this, &MainWindow::resizeContents, this, [this](int w, int h) {
        if (shownonce) {
            if (resizableonce == false)
                ui->stackedWidget->setFixedSize(QWIDGETSIZE_MAX, QWIDGETSIZE_MAX);
            resizableonce = true;
        }
        if (!QApplication::platformName().contains("eglfs") && vid_resize != 1) {
            w = static_cast<int>(w / (!dpi_scale ? util::screenOfWidget(this)->devicePixelRatio() : 1.));

            const int modifiedHeight = static_cast<int>(h / (!dpi_scale ? util::screenOfWidget(this)->devicePixelRatio() : 1.))
                + menuBar()->height()
                + (statusBar()->height() * !hide_status_bar)
                + (ui->toolBar->height() * !hide_tool_bar);

            ui->stackedWidget->resize(w, static_cast<int>(h / (!dpi_scale ? util::screenOfWidget(this)->devicePixelRatio() : 1.)));
            setFixedSize(w, modifiedHeight);
        }
    });

    connect(this, &MainWindow::resizeContentsMonitor, this, [this](int w, int h, int monitor_index) {
        if (!QApplication::platformName().contains("eglfs") && vid_resize != 1) {
#ifdef QT_RESIZE_DEBUG
            qDebug() << "Resize";
#endif
            w = static_cast<int>(w / (!dpi_scale ? util::screenOfWidget(renderers[monitor_index].get())->devicePixelRatio() : 1.));

            int modifiedHeight = static_cast<int>(h / (!dpi_scale ? util::screenOfWidget(renderers[monitor_index].get())->devicePixelRatio() : 1.));

            renderers[monitor_index]->setFixedSize(w, modifiedHeight);
        }
    });

    connect(ui->menubar, &QMenuBar::triggered, this, [this] {
        config_save();
        if (QApplication::activeWindow() == this) {
            ui->stackedWidget->setFocusRenderer();
        }
    });

    connect(this, &MainWindow::updateStatusBarPanes, this, [this] {
        refreshMediaMenu();
    });
    connect(this, &MainWindow::updateStatusBarPanes, this, &MainWindow::refreshMediaMenu);
    connect(this, &MainWindow::updateStatusBarTip, status.get(), &MachineStatus::updateTip);
    connect(this, &MainWindow::statusBarMessage, status.get(), &MachineStatus::message, Qt::QueuedConnection);

    ui->actionKeyboard_requires_capture->setChecked(kbd_req_capture);
    ui->actionRight_CTRL_is_left_ALT->setChecked(rctrl_is_lalt);
    ui->actionResizable_window->setChecked(vid_resize == 1);
    ui->actionRemember_size_and_position->setChecked(window_remember);
    ui->menuWindow_scale_factor->setEnabled(vid_resize == 0);
    ui->actionHiDPI_scaling->setChecked(dpi_scale);
    ui->actionHide_status_bar->setChecked(hide_status_bar);
    ui->actionHide_tool_bar->setChecked(hide_tool_bar);
    ui->actionShow_non_primary_monitors->setChecked(show_second_monitors);
    ui->actionUpdate_status_bar_icons->setChecked(update_icons);
    ui->actionEnable_Discord_integration->setChecked(enable_discord);
    ui->actionApply_fullscreen_stretch_mode_when_maximized->setChecked(video_fullscreen_scale_maximized);

#ifndef DISCORD
    ui->actionEnable_Discord_integration->setVisible(false);
#else
    ui->actionEnable_Discord_integration->setEnabled(discord_loaded);
#endif

    if ((QApplication::platformName().contains("eglfs") || QApplication::platformName() == "haiku")) {
        if ((vid_api == RENDERER_OPENGL3) || (vid_api == RENDERER_VULKAN))
            fprintf(stderr, "OpenGL renderers are unsupported on %s.\n", QApplication::platformName().toUtf8().data());
        vid_api = RENDERER_SOFTWARE;
        ui->actionVulkan->setVisible(false);
        ui->actionOpenGL_3_0_Core->setVisible(false);
    }

#ifndef USE_VNC
    if (vid_api == RENDERER_VNC)
        vid_api = RENDERER_SOFTWARE;
    ui->actionVNC->setVisible(false);
#endif

#if QT_CONFIG(vulkan)
    bool vulkanAvailable = false;
    {
        QVulkanInstance instance;
        instance.setApiVersion(QVersionNumber(1, 0));
        if (instance.create()) {
            uint32_t physicalDevices = 0;
            instance.functions()->vkEnumeratePhysicalDevices(instance.vkInstance(), &physicalDevices, nullptr);
            if (physicalDevices != 0) {
                vulkanAvailable = true;
            }
        }
    }
    if (!vulkanAvailable)
#endif
    {
        if (vid_api == RENDERER_VULKAN)
            vid_api = RENDERER_SOFTWARE;
        ui->actionVulkan->setVisible(false);
    }

    auto actGroup = new QActionGroup(this);
    actGroup->addAction(ui->actionSoftware_Renderer);
    actGroup->addAction(ui->actionOpenGL_3_0_Core);
    actGroup->addAction(ui->actionVulkan);
    actGroup->addAction(ui->actionVNC);
    actGroup->setExclusive(true);

    connect(actGroup, &QActionGroup::triggered, [this](QAction *action) {
        vid_api = action->property("vid_api").toInt();
#ifdef USE_VNC
        if (vnc_enabled && vid_api != RENDERER_VNC) {
            startblit();
            vnc_enabled = 0;
            vnc_close();
            video_setblit(qt_blit);
            endblit();
        }
#endif
        RendererStack::Renderer newVidApi = RendererStack::Renderer::Software;
        switch (vid_api) {
            default:
                break;
            case RENDERER_SOFTWARE:
                newVidApi = RendererStack::Renderer::Software;
                break;
            case RENDERER_OPENGL3:
                newVidApi = RendererStack::Renderer::OpenGL3;
                break;
            case RENDERER_VULKAN:
                newVidApi = RendererStack::Renderer::Vulkan;
                break;
#ifdef USE_VNC
            case RENDERER_VNC:
                {
                    newVidApi = RendererStack::Renderer::Software;
                    startblit();
                    vnc_enabled = vnc_init(nullptr);
                    endblit();
                }
#endif
        }
        ui->stackedWidget->switchRenderer(newVidApi);
        ui->menuOpenGL_input_scale->setEnabled(newVidApi == RendererStack::Renderer::OpenGL3);
        ui->menuOpenGL_input_stretch_mode->setEnabled(newVidApi == RendererStack::Renderer::OpenGL3);
        if (!show_second_monitors)
            return;
        for (int i = 1; i < MONITORS_NUM; i++) {
            if (renderers[i])
                renderers[i]->switchRenderer(newVidApi);
        }
    });

    connect(ui->stackedWidget, &RendererStack::rendererChanged, [this]() {
        ui->actionRenderer_options->setEnabled(ui->stackedWidget->hasOptions());
    });

    /* Trigger initial renderer switch */
    for (const auto action : actGroup->actions())
        if (action->property("vid_api").toInt() == vid_api) {
            action->setChecked(true);
            emit actGroup->triggered(action);
            break;
        }

    ui->action_0_5x_2->setChecked(video_gl_input_scale < 1.0);
    ui->action_1x_2->setChecked(video_gl_input_scale >= 1.0 && video_gl_input_scale < 1.5);
    ui->action1_5x_2->setChecked(video_gl_input_scale >= 1.5 && video_gl_input_scale < 2.0);
    ui->action_2x_2->setChecked(video_gl_input_scale >= 2.0 && video_gl_input_scale < 3.0);
    ui->action_3x_2->setChecked(video_gl_input_scale >= 3.0 && video_gl_input_scale < 4.0);
    ui->action_4x_2->setChecked(video_gl_input_scale >= 4.0 && video_gl_input_scale < 5.0);
    ui->action_5x_2->setChecked(video_gl_input_scale >= 5.0 && video_gl_input_scale < 6.0);
    ui->action_6x_2->setChecked(video_gl_input_scale >= 6.0 && video_gl_input_scale < 7.0);
    ui->action_7x_2->setChecked(video_gl_input_scale >= 7.0 && video_gl_input_scale < 8.0);
    ui->action_8x_2->setChecked(video_gl_input_scale >= 8.0);

    actGroup = new QActionGroup(this);
    actGroup->addAction(ui->action_0_5x_2);
    actGroup->addAction(ui->action_1x_2);
    actGroup->addAction(ui->action1_5x_2);
    actGroup->addAction(ui->action_2x_2);
    actGroup->addAction(ui->action_3x_2);
    actGroup->addAction(ui->action_4x_2);
    actGroup->addAction(ui->action_5x_2);
    actGroup->addAction(ui->action_6x_2);
    actGroup->addAction(ui->action_7x_2);
    actGroup->addAction(ui->action_8x_2);
    connect(actGroup, &QActionGroup::triggered, this, [this](QAction *action) {
        if (action == ui->action_0_5x_2)
            video_gl_input_scale = 0.5;
        if (action == ui->action_1x_2)
            video_gl_input_scale = 1;
        if (action == ui->action1_5x_2)
            video_gl_input_scale = 1.5;
        if (action == ui->action_2x_2)
            video_gl_input_scale = 2;
        if (action == ui->action_3x_2)
            video_gl_input_scale = 3;
        if (action == ui->action_4x_2)
            video_gl_input_scale = 4;
        if (action == ui->action_5x_2)
            video_gl_input_scale = 5;
        if (action == ui->action_6x_2)
            video_gl_input_scale = 6;
        if (action == ui->action_7x_2)
            video_gl_input_scale = 7;
        if (action == ui->action_8x_2)
            video_gl_input_scale = 8;
    });

    switch (scale) {
        default:
            break;
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
        case 4:
            ui->action3x->setChecked(true);
            break;
        case 5:
            ui->action4x->setChecked(true);
            break;
        case 6:
            ui->action5x->setChecked(true);
            break;
        case 7:
            ui->action6x->setChecked(true);
            break;
        case 8:
            ui->action7x->setChecked(true);
            break;
        case 9:
            ui->action8x->setChecked(true);
            break;
    }
    actGroup = new QActionGroup(this);
    actGroup->addAction(ui->action0_5x);
    actGroup->addAction(ui->action1x);
    actGroup->addAction(ui->action1_5x);
    actGroup->addAction(ui->action2x);
    actGroup->addAction(ui->action3x);
    actGroup->addAction(ui->action4x);
    actGroup->addAction(ui->action5x);
    actGroup->addAction(ui->action6x);
    actGroup->addAction(ui->action7x);
    actGroup->addAction(ui->action8x);
    switch (video_filter_method) {
        default:
            break;
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
        default:
            break;
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
        case FULLSCR_SCALE_INT43:
            ui->actionFullScreen_int43->setChecked(true);
            break;
    }
    actGroup = new QActionGroup(this);
    actGroup->addAction(ui->actionFullScreen_stretch);
    actGroup->addAction(ui->actionFullScreen_43);
    actGroup->addAction(ui->actionFullScreen_keepRatio);
    actGroup->addAction(ui->actionFullScreen_int);
    actGroup->addAction(ui->actionFullScreen_int43);
    switch (video_gl_input_scale_mode) {
        default:
            break;
        case FULLSCR_SCALE_FULL:
            ui->action_Full_screen_stretch_gl->setChecked(true);
            break;
        case FULLSCR_SCALE_43:
            ui->action_4_3_gl->setChecked(true);
            break;
        case FULLSCR_SCALE_KEEPRATIO:
            ui->action_Square_pixels_keep_ratio_gl->setChecked(true);
            break;
        case FULLSCR_SCALE_INT:
            ui->action_Integer_scale_gl->setChecked(true);
            break;
        case FULLSCR_SCALE_INT43:
            ui->action4_3_Integer_scale_gl->setChecked(true);
            break;
    }
    actGroup = new QActionGroup(this);
    actGroup->addAction(ui->action_Full_screen_stretch_gl);
    actGroup->addAction(ui->action_4_3_gl);
    actGroup->addAction(ui->action_Square_pixels_keep_ratio_gl);
    actGroup->addAction(ui->action_Integer_scale_gl);
    actGroup->addAction(ui->action4_3_Integer_scale_gl);
    connect(actGroup, &QActionGroup::triggered, this, [this](QAction *action) {
        if (action == ui->action_Full_screen_stretch_gl)
            video_gl_input_scale_mode = FULLSCR_SCALE_FULL;
        if (action == ui->action_4_3_gl)
            video_gl_input_scale_mode = FULLSCR_SCALE_43;
        if (action == ui->action_Square_pixels_keep_ratio_gl)
            video_gl_input_scale_mode = FULLSCR_SCALE_KEEPRATIO;
        if (action == ui->action_Integer_scale_gl)
            video_gl_input_scale_mode = FULLSCR_SCALE_INT;
        if (action == ui->action4_3_Integer_scale_gl)
            video_gl_input_scale_mode = FULLSCR_SCALE_INT43;
    });
    switch (video_grayscale) {
        default:
            break;
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
        default:
            break;
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
    if (do_auto_pause > 0) {
        ui->actionAuto_pause->setChecked(true);
    }
    if (force_constant_mouse > 0) {
        ui->actionUpdate_mouse_every_CPU_frame->setChecked(true);
    }

    if (!vnc_enabled)
        video_setblit(qt_blit);

    if (start_in_fullscreen) {
        connect(ui->stackedWidget, &RendererStack::blitToRenderer, this, [this]() {
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
        static auto init_trace = [&] {
            mtr_init("trace.json");
            mtr_start();
        };
        static auto shutdown_trace = [&] {
            mtr_stop();
            mtr_shutdown();
        };
        static bool trace = false;
        connect(ui->actionBegin_trace, &QAction::triggered, this, [this] {
            if (trace)
                return;
            ui->actionBegin_trace->setDisabled(true);
            ui->actionEnd_trace->setDisabled(false);
            init_trace();
            trace = true;
        });
        connect(ui->actionEnd_trace, &QAction::triggered, this, [this] {
            if (!trace)
                return;
            ui->actionBegin_trace->setDisabled(false);
            ui->actionEnd_trace->setDisabled(true);
            shutdown_trace();
            trace = false;
        });
    }
#endif

    setContextMenuPolicy(Qt::PreventContextMenu);
    /* Remove default Shift+F10 handler, which unfocuses keyboard input even with no context menu. */
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
    connect(new QShortcut(QKeySequence(Qt::SHIFT | Qt::Key_F10), this), &QShortcut::activated, this, []() {});
#else
    connect(new QShortcut(QKeySequence(Qt::SHIFT + Qt::Key_F10), this), &QShortcut::activated, this, []() {});
#endif

    connect(this, &MainWindow::initRendererMonitor, this, &MainWindow::initRendererMonitorSlot);
    connect(this, &MainWindow::initRendererMonitorForNonQtThread, this, &MainWindow::initRendererMonitorSlot, Qt::BlockingQueuedConnection);
    connect(this, &MainWindow::destroyRendererMonitor, this, &MainWindow::destroyRendererMonitorSlot);
    connect(this, &MainWindow::destroyRendererMonitorForNonQtThread, this, &MainWindow::destroyRendererMonitorSlot, Qt::BlockingQueuedConnection);

#ifdef Q_OS_MACOS
    QTimer::singleShot(0, this, [this]() {
        for (auto curObj : this->menuBar()->children()) {
            if (qobject_cast<QMenu *>(curObj)) {
                auto menu = qobject_cast<QMenu *>(curObj);
                menu->setSeparatorsCollapsible(false);
                for (auto curObj2 : menu->children()) {
                    if (qobject_cast<QMenu *>(curObj2)) {
                        auto menu2 = qobject_cast<QMenu *>(curObj2);
                        menu2->setSeparatorsCollapsible(false);
                    }
                }
            }
        }
    });
#endif

    QTimer::singleShot(0, this, [this]() {
        for (auto curObj : this->menuBar()->children()) {
            if (qobject_cast<QMenu *>(curObj)) {
                auto menu = qobject_cast<QMenu *>(curObj);
                for (auto curObj2 : menu->children()) {
                    if (qobject_cast<QAction *>(curObj2)) {
                        auto action = qobject_cast<QAction *>(curObj2);
                        if (!action->shortcut().isEmpty()) {
                            this->insertAction(nullptr, action);
                        }
                    }
                }
            }
        }
    });

    actGroup = new QActionGroup(this);
    actGroup->addAction(ui->actionCursor_Puck);
    actGroup->addAction(ui->actionPen);

    if (tablet_tool_type == 1) {
        ui->actionPen->setChecked(true);
    } else {
        ui->actionCursor_Puck->setChecked(true);
    }

#ifdef XKBCOMMON
#    ifdef XKBCOMMON_X11
    if (QApplication::platformName().contains("xcb"))
        xkbcommon_x11_init();
    else
#    endif
#    ifdef WAYLAND
        if (QApplication::platformName().contains("wayland"))
        xkbcommon_wl_init();
    else
#    endif
    {
    }
#endif

#if defined Q_OS_UNIX && !defined Q_OS_MACOS && !defined Q_OS_HAIKU
    if (QApplication::platformName().contains("xcb")) {
        QTimer::singleShot(0, this, [this] {
            auto whandle = windowHandle();
            if (!whandle) {
                qWarning() << "No window handle";
            } else {
                QPlatformWindow *window = whandle->handle();
                set_wm_class(window->winId(), vm_name);
            }
        });
    }
#endif

    updateShortcuts();
}

void
MainWindow::closeEvent(QCloseEvent *event)
{
    if (mouse_capture) {
        event->ignore();
        return;
    }

    if (confirm_exit && confirm_exit_cmdl && cpu_thread_run) {
        QMessageBox questionbox(QMessageBox::Icon::Question, "86Box", tr("Are you sure you want to exit 86Box?"), QMessageBox::Yes | QMessageBox::No, this);
        auto        chkbox = new QCheckBox(tr("Don't show this message again"));
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
        for (int i = 1; i < MONITORS_NUM; i++) {
            if (renderers[i]) {
                monitor_settings[i].mon_window_w = renderers[i]->geometry().width();
                monitor_settings[i].mon_window_h = renderers[i]->geometry().height();
                if (QApplication::platformName().contains("wayland"))
                    continue;
                monitor_settings[i].mon_window_x = renderers[i]->geometry().x();
                monitor_settings[i].mon_window_y = renderers[i]->geometry().y();
            }
        }
    }

    if (ui->stackedWidget->mouse_exit_func)
        ui->stackedWidget->mouse_exit_func();

    ui->stackedWidget->switchRenderer(RendererStack::Renderer::Software);
    for (int i = 1; i < MONITORS_NUM; i++) {
        if (renderers[i] && renderers[i]->isHidden()) {
            renderers[i]->show();
            QApplication::processEvents();
            renderers[i]->switchRenderer(RendererStack::Renderer::Software);
            QApplication::processEvents();
        }
    }

    qt_nvr_save();
    config_save();
    QApplication::processEvents();
    cpu_thread_run = 0;
    event->accept();
}

void
ui_update_force_interpreter()
{
    emit main_window->forceInterpretationCompleted();
}

void
MainWindow::updateShortcuts()
{
    /*
     Update menu shortcuts from accelerator table

     Note that these only work in windowed mode. If you add any new shortcuts,
     you have to go duplicate them in MainWindow::eventFilter()
     */

    // First we need to wipe all existing accelerators, otherwise Qt will
    // run into conflicts with old ones.
    ui->actionTake_screenshot->setShortcut(QKeySequence());
    ui->actionCtrl_Alt_Del->setShortcut(QKeySequence());
    ui->actionCtrl_Alt_Esc->setShortcut(QKeySequence());
    ui->actionHard_Reset->setShortcut(QKeySequence());
    ui->actionPause->setShortcut(QKeySequence());
    ui->actionMute_Unmute->setShortcut(QKeySequence());
    ui->actionForce_interpretation->setShortcut(QKeySequence());

    int          accID;
    QKeySequence seq;

    accID = FindAccelerator("screenshot");
    seq   = QKeySequence::fromString(acc_keys[accID].seq);
    ui->actionTake_screenshot->setShortcut(seq);

    accID = FindAccelerator("raw_screenshot");
    seq   = QKeySequence::fromString(acc_keys[accID].seq);
    ui->actionTake_raw_screenshot->setShortcut(seq);

    accID = FindAccelerator("copy_screenshot");
    seq   = QKeySequence::fromString(acc_keys[accID].seq);
    ui->actionCopy_screenshot->setShortcut(seq);

    accID = FindAccelerator("copy_raw_screenshot");
    seq   = QKeySequence::fromString(acc_keys[accID].seq);
    ui->actionCopy_raw_screenshot->setShortcut(seq);

    accID = FindAccelerator("send_ctrl_alt_del");
    seq   = QKeySequence::fromString(acc_keys[accID].seq);
    ui->actionCtrl_Alt_Del->setShortcut(seq);

    accID = FindAccelerator("send_ctrl_alt_esc");
    seq   = QKeySequence::fromString(acc_keys[accID].seq);
    ui->actionCtrl_Alt_Esc->setShortcut(seq);

    accID = FindAccelerator("hard_reset");
    seq   = QKeySequence::fromString(acc_keys[accID].seq);
    ui->actionHard_Reset->setShortcut(seq);

    accID = FindAccelerator("fast_forward");
    seq   = QKeySequence::fromString(acc_keys[accID].seq);
    ui->actionFast_forward->setShortcut(seq);

    accID = FindAccelerator("fullscreen");
    seq   = QKeySequence::fromString(acc_keys[accID].seq);
    ui->actionFullscreen->setShortcut(seq);

    accID = FindAccelerator("pause");
    seq   = QKeySequence::fromString(acc_keys[accID].seq);
    ui->actionPause->setShortcut(seq);

    accID = FindAccelerator("mute");
    seq   = QKeySequence::fromString(acc_keys[accID].seq);
    ui->actionMute_Unmute->setShortcut(seq);

    accID = FindAccelerator("force_interpretation");
    seq   = QKeySequence::fromString(acc_keys[accID].seq);
    ui->actionForce_interpretation->setShortcut(seq);
}

void
MainWindow::resizeEvent(QResizeEvent *event)
{
#ifdef MOVE_WINDOW
    //qDebug() << pos().x() + event->size().width();
    //qDebug() << pos().y() + event->size().height();
    if (vid_resize == 1 || video_fullscreen)
        return;

    int newX = pos().x();
    int newY = pos().y();

    if (((frameGeometry().x() + event->size().width() + 1) > util::screenOfWidget(this)->availableGeometry().right())) {
        // move(util::screenOfWidget(this)->availableGeometry().right() - size().width() - 1, pos().y());
        newX = util::screenOfWidget(this)->availableGeometry().right() - frameGeometry().width() - 1;
        if (newX < 1)
            newX = 1;
    }

    if (((frameGeometry().y() + event->size().height() + 1) > util::screenOfWidget(this)->availableGeometry().bottom())) {
        newY = util::screenOfWidget(this)->availableGeometry().bottom() - frameGeometry().height() - 1;
        if (newY < 1)
            newY = 1;
    }
    move(newX, newY);
#endif /*MOVE_WINDOW*/
}

void
MainWindow::initRendererMonitorSlot(int monitor_index)
{
    auto &secondaryRenderer = this->renderers[monitor_index];
    secondaryRenderer       = std::make_unique<RendererStack>(nullptr, monitor_index);
    if (secondaryRenderer) {
        connect(secondaryRenderer.get(), &RendererStack::rendererChanged, this, [this, monitor_index] {
            this->renderers[monitor_index]->show();
        });
        secondaryRenderer->setWindowFlags(Qt::CustomizeWindowHint | Qt::WindowTitleHint | Qt::WindowCloseButtonHint);
        secondaryRenderer->setWindowTitle(QObject::tr("86Box Monitor #%1").arg(monitor_index + 1));
        secondaryRenderer->setContextMenuPolicy(Qt::PreventContextMenu);

        for (int i = 0; i < this->actions().size(); i++) {
            secondaryRenderer->addAction(this->actions()[i]);
        }

        if (vid_resize == 2)
            secondaryRenderer->setFixedSize(fixed_size_x, fixed_size_y);
        secondaryRenderer->setWindowIcon(this->windowIcon());
#ifdef Q_OS_WINDOWS
        util::setWin11RoundedCorners(secondaryRenderer->winId(), false);
#endif
        if (show_second_monitors) {
            secondaryRenderer->show();
            if (window_remember) {
                secondaryRenderer->setGeometry(monitor_settings[monitor_index].mon_window_x < 120 ? 120 : monitor_settings[monitor_index].mon_window_x,
                                               monitor_settings[monitor_index].mon_window_y < 120 ? 120 : monitor_settings[monitor_index].mon_window_y,
                                               monitor_settings[monitor_index].mon_window_w > 2048 ? 2048 : monitor_settings[monitor_index].mon_window_w,
                                               monitor_settings[monitor_index].mon_window_h > 2048 ? 2048 : monitor_settings[monitor_index].mon_window_h);
            }
            if (monitor_settings[monitor_index].mon_window_maximized)
                secondaryRenderer->showMaximized();
            secondaryRenderer->switchRenderer((RendererStack::Renderer) vid_api);
            secondaryRenderer->setMouseTracking(true);

            if (monitor_settings[monitor_index].mon_window_maximized) {
                if (renderers[monitor_index])
                    renderers[monitor_index]->onResize(renderers[monitor_index]->width(),
                                                       renderers[monitor_index]->height());

                device_force_redraw();
            }
        }
    }
}

void
MainWindow::destroyRendererMonitorSlot(int monitor_index)
{
    if (this->renderers[monitor_index]) {
        if (window_remember) {
            monitor_settings[monitor_index].mon_window_w = renderers[monitor_index]->geometry().width();
            monitor_settings[monitor_index].mon_window_h = renderers[monitor_index]->geometry().height();
            monitor_settings[monitor_index].mon_window_x = renderers[monitor_index]->geometry().x();
            monitor_settings[monitor_index].mon_window_y = renderers[monitor_index]->geometry().y();
        }
        config_save();
        this->renderers[monitor_index].release()->deleteLater();
        ui->stackedWidget->switchRenderer((RendererStack::Renderer) vid_api);
    }
}

MainWindow::~MainWindow()
{
    delete ui;
}

void
MainWindow::showEvent(QShowEvent *event)
{
    if (shownonce)
        return;
    shownonce = true;
    if (window_remember) {
        if (window_w == 0)
            window_w = 320;
        if (window_h == 0)
            window_h = 200;
    }

    if (window_remember && !QApplication::platformName().contains("wayland")) {
        setGeometry(window_x, window_y, window_w, window_h + menuBar()->height() + (hide_status_bar ? 0 : statusBar()->height()) + (hide_tool_bar ? 0 : ui->toolBar->height()));
    }
    if (vid_resize == 2) {
        setFixedSize(fixed_size_x, fixed_size_y + menuBar()->height() + (hide_status_bar ? 0 : statusBar()->height()) + (hide_tool_bar ? 0 : ui->toolBar->height()));

        monitors[0].mon_scrnsz_x = fixed_size_x;
        monitors[0].mon_scrnsz_y = fixed_size_y;
    }
    if (window_remember && vid_resize == 1) {
        ui->stackedWidget->setFixedSize(window_w, window_h);
#ifndef Q_OS_MACOS
        QApplication::processEvents();
#endif
        this->adjustSize();
    }
}

void
MainWindow::on_actionKeyboard_requires_capture_triggered()
{
    kbd_req_capture ^= 1;
#ifndef Q_OS_MACOS
    qt_set_sequence_auto_mnemonic(!!kbd_req_capture);
    /* Hack to get the menubar to update the internal Alt+shortcut table */
    if (!video_fullscreen) {
        ui->menubar->hide();
        ui->menubar->show();
    }
#endif
}

void
MainWindow::on_actionRight_CTRL_is_left_ALT_triggered()
{
    rctrl_is_lalt ^= 1;
}

void
MainWindow::on_actionHard_Reset_triggered()
{
    if (confirm_reset) {
        QMessageBox questionbox(QMessageBox::Icon::Question, "86Box", tr("Are you sure you want to hard reset the emulated machine?"), QMessageBox::NoButton, this);
        questionbox.addButton(tr("Reset"), QMessageBox::AcceptRole);
        questionbox.addButton(tr("Don't reset"), QMessageBox::RejectRole);
        const auto chkbox = new QCheckBox(tr("Don't show this message again"));
        questionbox.setCheckBox(chkbox);
        chkbox->setChecked(!confirm_reset);

        QObject::connect(chkbox, &QCheckBox::stateChanged, [](int state) {
            confirm_reset = (state == Qt::CheckState::Unchecked);
        });
        questionbox.exec();
        if (questionbox.result() == QDialog::Accepted) {
            confirm_reset = true;
            return;
        }
    }
    config_changed = 2;
    pc_reset_hard();
}

void
MainWindow::on_actionCtrl_Alt_Del_triggered()
{
    pc_send_cad();
}

void
MainWindow::on_actionCtrl_Alt_Esc_triggered()
{
    pc_send_cae();
}

void
MainWindow::on_actionPause_triggered()
{
    plat_pause(dopause ^ 1);
}

void
MainWindow::on_actionExit_triggered()
{
    close();
}

void
MainWindow::on_actionSettings_triggered()
{
    const int currentPause = dopause;
    plat_pause(1);
    Settings settings(this);
    settings.setModal(true);
    settings.setWindowModality(Qt::WindowModal);
    settings.setWindowFlag(Qt::CustomizeWindowHint, true);
    settings.setWindowFlag(Qt::WindowTitleHint, true);
    settings.setWindowFlag(Qt::WindowSystemMenuHint, false);
    settings.exec();

    switch (settings.result()) {
        default:
            break;
        case QDialog::Accepted:
            settings.save();
            config_changed = 2;
            updateShortcuts();
            emit vmmConfigurationChanged();
            pc_reset_hard();
            break;
        case QDialog::Rejected:
            break;
    }
    plat_pause(currentPause);
}

void
MainWindow::processKeyboardInput(bool down, uint32_t keycode)
{
#if defined(Q_OS_WINDOWS) /* non-raw input */
    keycode &= 0xffff;
#elif defined(Q_OS_MACOS)
    keycode = (keycode < 127) ? cocoa_keycodes[keycode] : 0;
#elif defined(__HAIKU__)
    keycode = be_keycodes[keycode];
#else
#    ifdef XKBCOMMON
    if (xkbcommon_keymap)
        keycode = xkbcommon_translate(keycode);
    else
#    endif
#    ifdef EVDEV_KEYBOARD_HPP
        keycode = evdev_translate(keycode - 8);
#    else
    keycode = 0;
#    endif
#endif

    /* Apply special cases. */
    switch (keycode) {
        default:
            break;

        case 0x54: /* Alt + Print Screen (special case, i.e. evdev SELECTIVE_SCREENSHOT) */
            /* Send Alt as well. */
            if (down) {
                keyboard_input(down, 0x38);
            } else {
                keyboard_input(down, keycode);
                keycode = 0x38;
            }
            break;

        case 0x80 ... 0xff:   /* regular break codes */
        case 0x10b:           /* Microsoft scroll up normal */
        case 0x180 ... 0x1ff: /* E0 break codes (including Microsoft scroll down normal) */
            /* This key uses a break code as make. Send it manually, only on press. */
            if (down && (mouse_capture || !kbd_req_capture || (video_fullscreen && !fullscreen_ui_visible))) {
                if (keycode & 0x100)
                    keyboard_send(0xe0);
                keyboard_send(keycode & 0xff);
            }
            return;

        case 0x11d: /* Right Ctrl */
            if (rctrl_is_lalt)
                keycode = 0x38; /* map to Left Alt */
            break;

        case 0x137:                                                  /* Print Screen */
            if (keyboard_recv_ui(0x38) || keyboard_recv_ui(0x138)) { /* Alt+ */
                keycode = 0x54;
            } else if (down) {
                keyboard_input(down, 0x12a);
            } else {
                keyboard_input(down, keycode);
                keycode = 0x12a;
            }
            break;

        case 0x145:                                                  /* Pause */
            if (keyboard_recv_ui(0x1d) || keyboard_recv_ui(0x11d)) { /* Ctrl+ */
                keycode = 0x146;
            } else {
                keyboard_input(down, 0xe11d);
                keycode &= 0x00ff;
            }
            break;
    }

    keyboard_input(down, keycode);
}

#ifdef Q_OS_MACOS
// These modifiers are listed as "device-dependent" in IOLLEvent.h, but
// that's followed up with "(really?)". It's the only way to distinguish
// left and right modifiers with Qt 6 on macOS, so let's just roll with it.
static std::unordered_map<uint32_t, uint16_t> mac_modifiers_to_xt = {
    { NX_DEVICELCTLKEYMASK,                0x1D  },
    { NX_DEVICELSHIFTKEYMASK,              0x2A  },
    { NX_DEVICERSHIFTKEYMASK,              0x36  },
    { NX_DEVICELCMDKEYMASK,                0x15B },
    { NX_DEVICERCMDKEYMASK,                0x15C },
    { NX_DEVICELALTKEYMASK,                0x38  },
    { NX_DEVICERALTKEYMASK,                0x138 },
    { NX_DEVICE_ALPHASHIFT_STATELESS_MASK, 0x3A  },
    { NX_DEVICERCTLKEYMASK,                0x11D },
};
static bool mac_iso_swap = false;

void
MainWindow::processMacKeyboardInput(bool down, const QKeyEvent *event)
{
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
        for (auto const &pair : mac_modifiers_to_xt) {
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
            keyboard_input(1, 0x3a);
            keyboard_input(0, 0x3a);
        }
    } else {
        /* Apple ISO keyboards are notorious for swapping ISO_Section and ANSI_Grave
           on *some* layouts and/or models. While macOS can sort this mess out at
           keymap level, it still provides applications with unfiltered, ambiguous
           keycodes, so we have to disambiguate them by making some bold assumptions
           about the user's keyboard layout based on the OS-provided key mappings. */
        auto nvk = event->nativeVirtualKey();
        if ((nvk == 0x0a) || (nvk == 0x32)) {
            /* Flaws:
               - Layouts with `~ on ISO_Section are partially detected due to a conflict with ANSI
               - Czech and Slovak are not detected as they have <> ANSI_Grave and \| ISO_Section (differing from PC actually)
               - Italian is partially detected due to \| conflicting with Brazilian
               - Romanian third level ANSI_Grave is unknown
               - Russian clusters <>, plusminus and paragraph into a four-level ANSI_Grave, with the aforementioned `~ on ISO_Section */
            auto key = event->key();
            if ((nvk == 0x32) && (                                                                 /* system reports ANSI_Grave for ISO_Section keys: */
                                  (key == Qt::Key_Less) || (key == Qt::Key_Greater) ||             /* Croatian, French, German, Icelandic, Italian, Norwegian, Portuguese, Spanish, Spanish Latin America, Turkish Q */
                                  (key == Qt::Key_Ugrave) ||                                       /* French Canadian */
                                  (key == Qt::Key_Icircumflex) ||                                  /* Romanian */
                                  (key == Qt::Key_Iacute) ||                                       /* Hungarian */
                                  (key == Qt::Key_BracketLeft) || (key == Qt::Key_BracketRight) || /* Russian upper two levels */
                                  (key == Qt::Key_W)                                               /* Turkish F */
                                  ))
                mac_iso_swap = true;
            else if ((nvk == 0x0a) && (                                                              /* system reports ISO_Section for ANSI_Grave keys: */
                                       (key == Qt::Key_paragraph) || (key == Qt::Key_plusminus) ||   /* Arabic, British, Bulgarian, Danish shifted, Dutch, Greek, Hebrew, Hungarian shifted, International English, Norwegian shifted, Portuguese, Russian lower two levels, Swiss unshifted, Swedish unshifted, Turkish F */
                                       (key == Qt::Key_At) || (key == Qt::Key_NumberSign) ||         /* Belgian, French */
                                       (key == Qt::Key_Apostrophe) ||                                /* Brazilian unshifted */
                                       (key == Qt::Key_QuoteDbl) ||                                  /* Brazilian shifted, Turkish Q unshifted */
                                       (key == Qt::Key_QuoteLeft) ||                                 /* Croatian (right quote unknown) */
                                       (key == Qt::Key_Dollar) ||                                    /* Danish unshifted */
                                       (key == Qt::Key_AsciiCircum) || (key == 0x1ffffff) ||         /* German unshifted (0x1ffffff according to one tester), Polish unshifted */
                                       (key == Qt::Key_degree) ||                                    /* German shifted, Icelandic unshifted, Spanish Latin America shifted, Swiss shifted, Swedish shifted */
                                       (key == Qt::Key_0) ||                                         /* Hungarian unshifted */
                                       (key == Qt::Key_diaeresis) ||                                 /* Icelandic shifted */
                                       (key == Qt::Key_acute) ||                                     /* Norwegian unshifted */
                                       (key == Qt::Key_Asterisk) ||                                  /* Polish shifted */
                                       (key == Qt::Key_masculine) || (key == Qt::Key_ordfeminine) || /* Spanish (masculine unconfirmed) */
                                       (key == Qt::Key_Eacute) ||                                    /* Turkish Q shifted */
                                       (key == Qt::Key_Slash)                                        /* French Canadian unshifted, Ukrainian shifted */
                                       ))
                mac_iso_swap = true;
#    if 0
            if (down) {
                QMessageBox questionbox(QMessageBox::Icon::Information, QString("Mac key swap test"), QString("nativeVirtualKey 0x%1\nnativeScanCode 0x%2\nkey 0x%3\nmac_iso_swap %4").arg(nvk, 0, 16).arg(event->nativeScanCode(), 0, 16).arg(key, 0, 16).arg(mac_iso_swap ? "yes" : "no"), QMessageBox::Ok, this);
                questionbox.exec();
            }
#    endif
            if (mac_iso_swap)
                nvk = (nvk == 0x0a) ? 0x32 : 0x0a;
        }
        // Special case for command + forward delete to send insert.
        if ((event->nativeModifiers() & NSEventModifierFlagCommand) && ((event->nativeVirtualKey() == nvk_Delete) || event->key() == Qt::Key_Delete)) {
            nvk = nvk_Insert; // Qt::Key_Help according to event->key()
        }

        processKeyboardInput(down, nvk);
    }
}
#endif

void
MainWindow::on_actionFullscreen_triggered()
{
    if (video_fullscreen > 0) {
        showNormal();
        ui->menubar->show();
        if (!hide_status_bar)
            ui->statusbar->show();
        if (!hide_tool_bar)
            ui->toolBar->show();
        video_fullscreen = 0;
        fullscreen_ui_visible = 0;
        if (vid_resize != 1) {
            emit resizeContents(vid_resize == 2 ? fixed_size_x : monitors[0].mon_scrnsz_x, vid_resize == 2 ? fixed_size_y : monitors[0].mon_scrnsz_y);
        }
    } else {
        video_fullscreen = 1;
        setFixedSize(QWIDGETSIZE_MAX, QWIDGETSIZE_MAX);
        ui->menubar->hide();
        ui->statusbar->hide();
        ui->toolBar->hide();
        ui->stackedWidget->setFixedSize(QWIDGETSIZE_MAX, QWIDGETSIZE_MAX);
        showFullScreen();
    }
    fs_on_signal  = false;
    fs_off_signal = false;
    ui->stackedWidget->onResize(width(), height());
}

void
MainWindow::getTitle_(wchar_t *title)
{
    this->windowTitle().toWCharArray(title);
}

void
MainWindow::getTitle(wchar_t *title)
{
    if (QThread::currentThread() == this->thread()) {
        getTitle_(title);
    } else {
        emit getTitleForNonQtThread(title);
    }
}

// Helper to find an accelerator key and return it's sequence
// TODO: Is there a more central place to put this?
QKeySequence
MainWindow::FindAcceleratorSeq(const char *name)
{
    int accID = FindAccelerator(name);
    if (accID == -1)
        return false;

    return (QKeySequence::fromString(acc_keys[accID].seq));
}

bool
MainWindow::eventFilter(QObject *receiver, QEvent *event)
{
    // Detect shortcuts when menubar is hidden
    // TODO: Could this be simplified by proxying the event and manually
    // shoving it into the menubar?
    if (event->type() == QEvent::KeyPress) {
        this->keyPressEvent((QKeyEvent *) event);

        // We check for mouse release even if we aren't fullscreen,
        // because it's not a menu accelerator.
        if (event->type() == QEvent::KeyPress) {
            QKeyEvent *ke = (QKeyEvent *) event;
            if ((QKeySequence) (ke->key() | (ke->modifiers() & ~Qt::KeypadModifier)) == FindAcceleratorSeq("release_mouse") || (QKeySequence) (ke->key() | ke->modifiers()) == FindAcceleratorSeq("release_mouse")) {
                plat_mouse_capture(0);
            }
        }

        if (event->type() == QEvent::KeyPress && video_fullscreen != 0) {
            QKeyEvent *ke = (QKeyEvent *) event;

            if ((QKeySequence) (ke->key() | (ke->modifiers() & ~Qt::KeypadModifier)) == FindAcceleratorSeq("screenshot")
                || (QKeySequence) (ke->key() | ke->modifiers()) == FindAcceleratorSeq("screenshot")) {
                ui->actionTake_screenshot->trigger();
            }
            if ((QKeySequence) (ke->key() | (ke->modifiers() & ~Qt::KeypadModifier)) == FindAcceleratorSeq("raw_screenshot")
                || (QKeySequence) (ke->key() | ke->modifiers()) == FindAcceleratorSeq("raw_screenshot")) {
                ui->actionTake_raw_screenshot->trigger();
            }
            if ((QKeySequence) (ke->key() | (ke->modifiers() & ~Qt::KeypadModifier)) == FindAcceleratorSeq("copy_screenshot")
                || (QKeySequence) (ke->key() | ke->modifiers()) == FindAcceleratorSeq("copy_screenshot")) {
                ui->actionCopy_screenshot->trigger();
            }
            if ((QKeySequence) (ke->key() | (ke->modifiers() & ~Qt::KeypadModifier)) == FindAcceleratorSeq("copy_raw_screenshot")
                || (QKeySequence) (ke->key() | ke->modifiers()) == FindAcceleratorSeq("copy_raw_screenshot")) {
                ui->actionCopy_raw_screenshot->trigger();
            }
            if ((QKeySequence) (ke->key() | (ke->modifiers() & ~Qt::KeypadModifier)) == FindAcceleratorSeq("fullscreen")
                || (QKeySequence) (ke->key() | ke->modifiers()) == FindAcceleratorSeq("fullscreen")) {
                ui->actionFullscreen->trigger();
            }
            if ((QKeySequence) (ke->key() | (ke->modifiers() & ~Qt::KeypadModifier)) == FindAcceleratorSeq("hard_reset")
                || (QKeySequence) (ke->key() | ke->modifiers()) == FindAcceleratorSeq("hard_reset")) {
                ui->actionHard_Reset->trigger();
            }
            if ((QKeySequence) (ke->key() | (ke->modifiers() & ~Qt::KeypadModifier)) == FindAcceleratorSeq("fast_forward")
                || (QKeySequence) (ke->key() | ke->modifiers()) == FindAcceleratorSeq("fast_forward")) {
                ui->actionFast_forward->trigger();
            }
            if ((QKeySequence) (ke->key() | (ke->modifiers() & ~Qt::KeypadModifier)) == FindAcceleratorSeq("send_ctrl_alt_del")
                || (QKeySequence) (ke->key() | ke->modifiers()) == FindAcceleratorSeq("send_ctrl_alt_del")) {
                ui->actionCtrl_Alt_Del->trigger();
            }
            if ((QKeySequence) (ke->key() | (ke->modifiers() & ~Qt::KeypadModifier)) == FindAcceleratorSeq("send_ctrl_alt_esc")
                || (QKeySequence) (ke->key() | ke->modifiers()) == FindAcceleratorSeq("send_ctrl_alt_esc")) {
                ui->actionCtrl_Alt_Esc->trigger();
            }
            if ((QKeySequence) (ke->key() | (ke->modifiers() & ~Qt::KeypadModifier)) == FindAcceleratorSeq("pause")
                || (QKeySequence) (ke->key() | ke->modifiers()) == FindAcceleratorSeq("pause")) {
                ui->actionPause->trigger();
            }
            if ((QKeySequence) (ke->key() | (ke->modifiers() & ~Qt::KeypadModifier)) == FindAcceleratorSeq("mute")
                || (QKeySequence) (ke->key() | ke->modifiers()) == FindAcceleratorSeq("mute")) {
                ui->actionMute_Unmute->trigger();
            }
            if ((QKeySequence) (ke->key() | (ke->modifiers() & ~Qt::KeypadModifier)) == FindAcceleratorSeq("toggle_ui_fullscreen")
                || (QKeySequence) (ke->key() | ke->modifiers()) == FindAcceleratorSeq("toggle_ui_fullscreen")) {
                toggleFullscreenUI();
            }

            return true;
        }
    }

    if (!dopause && (!kbd_req_capture || mouse_capture)) {
        if (event->type() == QEvent::Shortcut) {
            auto shortcutEvent = (QShortcutEvent *) event;
            if (shortcutEvent->key() == ui->actionExit->shortcut()) {
                event->accept();
                return true;
            }
        }
        if (event->type() == QEvent::KeyPress) {
            event->accept();

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
            window_blocked = true;
            curdopause     = dopause;
            plat_pause(isNonPause ? dopause : (isShowMessage ? 2 : 1));
            emit setMouseCapture(false);
            releaseKeyboard();
        } else if (event->type() == QEvent::WindowUnblocked) {
            window_blocked = false;
            plat_pause(curdopause);
        }
    }

    return QMainWindow::eventFilter(receiver, event);
}

void
MainWindow::refreshMediaMenu()
{
    mm->refresh(ui->menuMedia);
    status->setSoundMenu(ui->menuSound);
    status->refresh(ui->statusbar);
    ui->actionMCA_devices->setVisible(machine_has_bus(machine, MACHINE_BUS_MCA));
    ui->actionACPI_Shutdown->setEnabled(!!acpi_enabled);
    ui_update_force_interpreter();

    num_label->setToolTip(QShortcut::tr("Num Lock"));
    num_label->setVisible(machine_has_bus(machine, MACHINE_BUS_PS2_PORTS | MACHINE_BUS_AT_KBD));
    scroll_label->setToolTip(QShortcut::tr("Scroll Lock"));
    scroll_label->setVisible(machine_has_bus(machine, MACHINE_BUS_PS2_PORTS | MACHINE_BUS_AT_KBD));
    caps_label->setToolTip(QShortcut::tr("Caps Lock"));
    caps_label->setVisible(machine_has_bus(machine, MACHINE_BUS_PS2_PORTS | MACHINE_BUS_AT_KBD));
    kana_label->setToolTip(QShortcut::tr("Kana Lock"));
    int ext_ax_kbd = machine_has_bus(machine, MACHINE_BUS_PS2_PORTS | MACHINE_BUS_AT_KBD) && (keyboard_type == KEYBOARD_TYPE_AX);
    int int_ax_kbd = machine_has_flags(machine, MACHINE_KEYBOARD_JIS) && !machine_has_bus(machine, MACHINE_BUS_PS2_PORTS);
    kana_label->setVisible(ext_ax_kbd || int_ax_kbd);

    bool enable_comp_option = false;
    for (int i = 0; i < MONITORS_NUM; i++) {
        if (monitors[i].mon_composite) {
            enable_comp_option = true;
            break;
        }
    }

    ui->actionCGA_composite_settings->setEnabled(enable_comp_option);
}

void
MainWindow::showMessage(int flags, const QString &header, const QString &message, bool richText)
{
    if (QThread::currentThread() == this->thread()) {
        if (!cpu_thread_running) {
            showMessageForNonQtThread(flags, header, message, richText, nullptr);
        } else
            showMessage_(flags, header, message, richText);
    } else {
        std::atomic_bool done = false;
        emit             showMessageForNonQtThread(flags, header, message, richText, &done);
        while (!done) {
            QThread::msleep(1);
        }
    }
}

void
MainWindow::showMessage_(int flags, const QString &header, const QString &message, bool richText, std::atomic_bool *done)
{
    if (done) {
        *done = false;
    }
    isShowMessage = true;
    QMessageBox box(QMessageBox::Warning, header, message, QMessageBox::NoButton, this);
    if (flags & (MBX_FATAL)) {
        box.setIcon(QMessageBox::Critical);
    } else if (!(flags & (MBX_ERROR | MBX_WARNING))) {
        box.setIcon(QMessageBox::Warning);
    }
    if (richText)
        box.setTextFormat(Qt::TextFormat::RichText);
    box.exec();
    if (done) {
        *done = true;
    }
    isShowMessage = false;
    if (cpu_thread_run == 0)
        QApplication::exit(-1);
}

void
MainWindow::keyPressEvent(QKeyEvent *event)
{
    if (send_keyboard_input) {
#ifdef Q_OS_MACOS
        processMacKeyboardInput(true, event);
#else
        processKeyboardInput(true, event->nativeScanCode());
#endif
    }

    event->accept();
}

void
MainWindow::blitToWidget(int x, int y, int w, int h, int monitor_index)
{
    if (monitor_index >= 1) {
        if (renderers[monitor_index] && renderers[monitor_index]->isVisible())
            renderers[monitor_index]->blit(x, y, w, h);
        else
            video_blit_complete_monitor(monitor_index);
    } else
        ui->stackedWidget->blit(x, y, w, h);
}

void
MainWindow::keyReleaseEvent(QKeyEvent *event)
{
    if (event->key() == Qt::Key_Pause) {
        if (keyboard_recv_ui(0x38) && keyboard_recv_ui(0x138)) {
            plat_pause(dopause ^ 1);
        }
    }

    if (send_keyboard_input && !event->isAutoRepeat()) {
#ifdef Q_OS_MACOS
        processMacKeyboardInput(false, event);
#else
        processKeyboardInput(false, event->nativeScanCode());
#endif
    }
}

QSize
MainWindow::getRenderWidgetSize()
{
    return ui->stackedWidget->size();
}

void
MainWindow::focusInEvent(QFocusEvent *event)
{
    // this->grabKeyboard();
}

void
MainWindow::focusOutEvent(QFocusEvent *event)
{
    // this->releaseKeyboard();
}

void
MainWindow::on_actionResizable_window_triggered(bool checked)
{
    hide();
    if (checked) {
        vid_resize = 1;
        setFixedSize(QWIDGETSIZE_MAX, QWIDGETSIZE_MAX);
        setWindowFlag(Qt::MSWindowsFixedSizeDialogHint, false);
        setWindowFlag(Qt::WindowMaximizeButtonHint, true);
        for (int i = 1; i < MONITORS_NUM; i++) {
            if (monitors[i].target_buffer) {
                renderers[i]->setWindowFlag(Qt::WindowMaximizeButtonHint, true);
                renderers[i]->setFixedSize(QWIDGETSIZE_MAX, QWIDGETSIZE_MAX);
            }
        }
    } else {
        vid_resize = 0;
        setWindowFlag(Qt::WindowMaximizeButtonHint, false);
        setWindowFlag(Qt::MSWindowsFixedSizeDialogHint);
        for (int i = 1; i < MONITORS_NUM; i++) {
            if (monitors[i].target_buffer) {
                renderers[i]->setWindowFlag(Qt::WindowMaximizeButtonHint, false);
                emit resizeContentsMonitor(monitors[i].mon_scrnsz_x, monitors[i].mon_scrnsz_y, i);
            }
        }
    }
    show();
    ui->menuWindow_scale_factor->setEnabled(!checked);
    emit resizeContents(monitors[0].mon_scrnsz_x, monitors[0].mon_scrnsz_y);
    ui->stackedWidget->switchRenderer((RendererStack::Renderer) vid_api);
    for (int i = 1; i < MONITORS_NUM; i++) {
        if (monitors[i].target_buffer && show_second_monitors) {
            renderers[i]->show();
            renderers[i]->switchRenderer((RendererStack::Renderer) vid_api);
            QApplication::processEvents();
        }
    }
}

static void
video_toggle_option(QAction *action, int *val)
{
    startblit();
    *val ^= 1;
    video_copy = (video_grayscale || invert_display) ? video_transform_copy : memcpy;
    action->setChecked(*val > 0 ? true : false);
    endblit();
    config_save();
    reset_screen_size();
    device_force_redraw();
    for (int i = 0; i < MONITORS_NUM; i++) {
        if (monitors[i].target_buffer)
            video_force_resize_set_monitor(1, i);
    }
}

void
MainWindow::on_actionInverted_VGA_monitor_triggered()
{
    video_toggle_option(ui->actionInverted_VGA_monitor, &invert_display);
}

void
MainWindow::on_actionForce_interpretation_triggered()
{
    cpu_force_interpreter ^= 1;
    ui_update_force_interpreter();
}

static void
update_scaled_checkboxes(Ui::MainWindow *ui, QAction *selected)
{
    ui->action0_5x->setChecked(ui->action0_5x == selected);
    ui->action1x->setChecked(ui->action1x == selected);
    ui->action1_5x->setChecked(ui->action1_5x == selected);
    ui->action2x->setChecked(ui->action2x == selected);
    ui->action3x->setChecked(ui->action3x == selected);
    ui->action4x->setChecked(ui->action4x == selected);
    ui->action5x->setChecked(ui->action5x == selected);
    ui->action6x->setChecked(ui->action6x == selected);
    ui->action7x->setChecked(ui->action7x == selected);
    ui->action8x->setChecked(ui->action8x == selected);

    reset_screen_size();
    device_force_redraw();
    for (int i = 0; i < MONITORS_NUM; i++) {
        if (monitors[i].target_buffer)
            video_force_resize_set_monitor(1, i);
    }
    config_save();
}

void
MainWindow::on_action0_5x_triggered()
{
    scale = 0;
    update_scaled_checkboxes(ui, ui->action0_5x);
}

void
MainWindow::on_action1x_triggered()
{
    scale = 1;
    update_scaled_checkboxes(ui, ui->action1x);
}

void
MainWindow::on_action1_5x_triggered()
{
    scale = 2;
    update_scaled_checkboxes(ui, ui->action1_5x);
}

void
MainWindow::on_action2x_triggered()
{
    scale = 3;
    update_scaled_checkboxes(ui, ui->action2x);
}

void
MainWindow::on_action3x_triggered()
{
    scale = 4;
    update_scaled_checkboxes(ui, ui->action3x);
}

void
MainWindow::on_action4x_triggered()
{
    scale = 5;
    update_scaled_checkboxes(ui, ui->action4x);
}

void
MainWindow::on_action5x_triggered()
{
    scale = 6;
    update_scaled_checkboxes(ui, ui->action5x);
}

void
MainWindow::on_action6x_triggered()
{
    scale = 7;
    update_scaled_checkboxes(ui, ui->action6x);
}

void
MainWindow::on_action7x_triggered()
{
    scale = 8;
    update_scaled_checkboxes(ui, ui->action7x);
}

void
MainWindow::on_action8x_triggered()
{
    scale = 9;
    update_scaled_checkboxes(ui, ui->action8x);
}

void
MainWindow::on_actionNearest_triggered()
{
    video_filter_method = 0;
    ui->actionLinear->setChecked(false);
}

void
MainWindow::on_actionLinear_triggered()
{
    video_filter_method = 1;
    ui->actionNearest->setChecked(false);
}

static void
update_fullscreen_scale_checkboxes(Ui::MainWindow *ui, QAction *selected)
{
    ui->actionFullScreen_stretch->setChecked(selected == ui->actionFullScreen_stretch);
    ui->actionFullScreen_43->setChecked(selected == ui->actionFullScreen_43);
    ui->actionFullScreen_keepRatio->setChecked(selected == ui->actionFullScreen_keepRatio);
    ui->actionFullScreen_int->setChecked(selected == ui->actionFullScreen_int);
    ui->actionFullScreen_int43->setChecked(selected == ui->actionFullScreen_int43);

    {
        auto widget = ui->stackedWidget->currentWidget();
        ui->stackedWidget->onResize(widget->width(), widget->height());
    }

    for (int i = 1; i < MONITORS_NUM; i++) {
        if (main_window->renderers[i])
            main_window->renderers[i]->onResize(main_window->renderers[i]->width(),
                                                main_window->renderers[i]->height());
    }

    device_force_redraw();
    config_save();
}

void
MainWindow::on_actionFullScreen_stretch_triggered()
{
    video_fullscreen_scale = FULLSCR_SCALE_FULL;
    update_fullscreen_scale_checkboxes(ui, ui->actionFullScreen_stretch);
}

void
MainWindow::on_actionFullScreen_43_triggered()
{
    video_fullscreen_scale = FULLSCR_SCALE_43;
    update_fullscreen_scale_checkboxes(ui, ui->actionFullScreen_43);
}

void
MainWindow::on_actionFullScreen_keepRatio_triggered()
{
    video_fullscreen_scale = FULLSCR_SCALE_KEEPRATIO;
    update_fullscreen_scale_checkboxes(ui, ui->actionFullScreen_keepRatio);
}

void
MainWindow::on_actionFullScreen_int_triggered()
{
    video_fullscreen_scale = FULLSCR_SCALE_INT;
    update_fullscreen_scale_checkboxes(ui, ui->actionFullScreen_int);
}

void
MainWindow::on_actionFullScreen_int43_triggered()
{
    video_fullscreen_scale = FULLSCR_SCALE_INT43;
    update_fullscreen_scale_checkboxes(ui, ui->actionFullScreen_int43);
}

static void
update_greyscale_checkboxes(Ui::MainWindow *ui, QAction *selected, int value)
{
    ui->actionRGB_Color->setChecked(ui->actionRGB_Color == selected);
    ui->actionRGB_Grayscale->setChecked(ui->actionRGB_Grayscale == selected);
    ui->actionAmber_monitor->setChecked(ui->actionAmber_monitor == selected);
    ui->actionGreen_monitor->setChecked(ui->actionGreen_monitor == selected);
    ui->actionWhite_monitor->setChecked(ui->actionWhite_monitor == selected);

    startblit();
    video_grayscale = value;
    video_copy      = (video_grayscale || invert_display) ? video_transform_copy : memcpy;
    endblit();
    device_force_redraw();
    config_save();
}

void
MainWindow::on_actionRGB_Color_triggered()
{
    update_greyscale_checkboxes(ui, ui->actionRGB_Color, 0);
}

void
MainWindow::on_actionRGB_Grayscale_triggered()
{
    update_greyscale_checkboxes(ui, ui->actionRGB_Grayscale, 1);
}

void
MainWindow::on_actionAmber_monitor_triggered()
{
    update_greyscale_checkboxes(ui, ui->actionAmber_monitor, 2);
}

void
MainWindow::on_actionGreen_monitor_triggered()
{
    update_greyscale_checkboxes(ui, ui->actionGreen_monitor, 3);
}

void
MainWindow::on_actionWhite_monitor_triggered()
{
    update_greyscale_checkboxes(ui, ui->actionWhite_monitor, 4);
}

static void
update_greyscale_type_checkboxes(Ui::MainWindow *ui, QAction *selected, int value)
{
    ui->actionBT601_NTSC_PAL->setChecked(ui->actionBT601_NTSC_PAL == selected);
    ui->actionBT709_HDTV->setChecked(ui->actionBT709_HDTV == selected);
    ui->actionAverage->setChecked(ui->actionAverage == selected);

    video_graytype = value;
    device_force_redraw();
    config_save();
}

void
MainWindow::on_actionBT601_NTSC_PAL_triggered()
{
    update_greyscale_type_checkboxes(ui, ui->actionBT601_NTSC_PAL, 0);
}

void
MainWindow::on_actionBT709_HDTV_triggered()
{
    update_greyscale_type_checkboxes(ui, ui->actionBT709_HDTV, 1);
}

void
MainWindow::on_actionAverage_triggered()
{
    update_greyscale_type_checkboxes(ui, ui->actionAverage, 2);
}

void
MainWindow::on_actionAbout_Qt_triggered()
{
    QApplication::aboutQt();
}

void
MainWindow::on_actionAbout_86Box_triggered()
{
    const auto msgBox = new About(this);
    msgBox->exec();
}

void
MainWindow::on_actionDocumentation_triggered()
{
    QDesktopServices::openUrl(QUrl(EMU_DOCS_URL));
}

void
MainWindow::on_actionCGA_PCjr_Tandy_EGA_S_VGA_overscan_triggered()
{
    update_overscan = 1;
    video_toggle_option(ui->actionCGA_PCjr_Tandy_EGA_S_VGA_overscan, &enable_overscan);
}

void
MainWindow::on_actionChange_contrast_for_monochrome_display_triggered()
{
    startblit();
    vid_cga_contrast ^= 1;
    for (int i = 0; i < MONITORS_NUM; i++)
        cgapal_rebuild_monitor(i);
    config_save();
    endblit();
}

void
MainWindow::on_actionForce_4_3_display_ratio_triggered()
{
    video_toggle_option(ui->actionForce_4_3_display_ratio, &force_43);
    if (vid_resize) {
        const auto widget = ui->stackedWidget->currentWidget();
        ui->stackedWidget->onResize(widget->width(), widget->height());

        for (int i = 1; i < MONITORS_NUM; i++) {
            if (renderers[i])
                renderers[i]->onResize(renderers[i]->width(), renderers[i]->height());
        }
    }
}

void
MainWindow::on_actionAuto_pause_triggered()
{
    do_auto_pause ^= 1;
    ui->actionAuto_pause->setChecked(do_auto_pause > 0 ? true : false);
    config_save();
}

void
MainWindow::on_actionUpdate_mouse_every_CPU_frame_triggered()
{
    force_constant_mouse ^= 1;
    ui->actionUpdate_mouse_every_CPU_frame->setChecked(force_constant_mouse > 0 ? true : false);
    mouse_update_sample_rate();
    config_save();
}

void
MainWindow::on_actionFast_forward_triggered()
{
    fast_forward ^= 1;
}

void
MainWindow::on_actionRemember_size_and_position_triggered()
{
    window_remember ^= 1;
    if (!video_fullscreen) {
        window_w = ui->stackedWidget->width();
        window_h = ui->stackedWidget->height();
        if (!QApplication::platformName().contains("wayland")) {
            window_x = geometry().x();
            window_y = geometry().y();
        }
        for (int i = 1; i < MONITORS_NUM; i++) {
            if (window_remember && renderers[i]) {
                monitor_settings[i].mon_window_w = renderers[i]->geometry().width();
                monitor_settings[i].mon_window_h = renderers[i]->geometry().height();
                monitor_settings[i].mon_window_x = renderers[i]->geometry().x();
                monitor_settings[i].mon_window_y = renderers[i]->geometry().y();
            }
        }
    }
    ui->actionRemember_size_and_position->setChecked(window_remember);
}

void
MainWindow::on_actionSpecify_dimensions_triggered()
{
    SpecifyDimensions dialog(this);
    dialog.setWindowModality(Qt::WindowModal);
    dialog.exec();
}

void
MainWindow::on_actionHiDPI_scaling_triggered()
{
    dpi_scale ^= 1;
    ui->actionHiDPI_scaling->setChecked(dpi_scale);
    emit resizeContents(monitors[0].mon_scrnsz_x, monitors[0].mon_scrnsz_y);
    for (int i = 1; i < MONITORS_NUM; i++) {
        if (renderers[i])
            emit resizeContentsMonitor(monitors[i].mon_scrnsz_x, monitors[i].mon_scrnsz_y, i);
    }
}

void
MainWindow::on_actionHide_status_bar_triggered()
{
    auto w = ui->stackedWidget->width() * (!dpi_scale ? util::screenOfWidget(this)->devicePixelRatio() : 1.);
    auto h = ui->stackedWidget->height() * (!dpi_scale ? util::screenOfWidget(this)->devicePixelRatio() : 1.);

    hide_status_bar ^= 1;
    ui->actionHide_status_bar->setChecked(hide_status_bar);
    statusBar()->setVisible(!hide_status_bar);
#ifdef Q_OS_WINDOWS
    util::setWin11RoundedCorners(main_window->winId(), (hide_status_bar ? false : true));
#endif
    if (vid_resize >= 2) {
        setFixedSize(fixed_size_x, fixed_size_y + menuBar()->height() + (hide_status_bar ? 0 : statusBar()->height()) + (hide_tool_bar ? 0 : ui->toolBar->height()));
    } else {
        int vid_resize_orig = vid_resize;
        vid_resize          = 0;
        emit resizeContents(vid_resize_orig ? w : monitors[0].mon_scrnsz_x, vid_resize_orig ? h : monitors[0].mon_scrnsz_y);
        vid_resize = vid_resize_orig;
        if (vid_resize == 1)
            setFixedSize(QWIDGETSIZE_MAX, QWIDGETSIZE_MAX);
    }
}

void
MainWindow::on_actionHide_tool_bar_triggered()
{
    auto w = ui->stackedWidget->width() * (!dpi_scale ? util::screenOfWidget(this)->devicePixelRatio() : 1.);
    auto h = ui->stackedWidget->height() * (!dpi_scale ? util::screenOfWidget(this)->devicePixelRatio() : 1.);

    hide_tool_bar ^= 1;
    ui->actionHide_tool_bar->setChecked(hide_tool_bar);
    ui->toolBar->setVisible(!hide_tool_bar);
    if (vid_resize >= 2) {
        setFixedSize(fixed_size_x, fixed_size_y + menuBar()->height() + (hide_status_bar ? 0 : statusBar()->height()) + (hide_tool_bar ? 0 : ui->toolBar->height()));
    } else {
        int vid_resize_orig = vid_resize;
        vid_resize          = 0;
        emit resizeContents(vid_resize_orig ? w : monitors[0].mon_scrnsz_x, vid_resize_orig ? h : monitors[0].mon_scrnsz_y);
        vid_resize = vid_resize_orig;
        if (vid_resize == 1)
            setFixedSize(QWIDGETSIZE_MAX, QWIDGETSIZE_MAX);
    }
}

void
MainWindow::on_actionUpdate_status_bar_icons_triggered()
{
    update_icons ^= 1;
    ui->actionUpdate_status_bar_icons->setChecked(update_icons);

    /* Prevent icons staying when disabled during activity. */
    status->clearActivity();
}

void
MainWindow::toggleFullscreenUI()
{
    if (video_fullscreen == 0)
        return;

    fullscreen_ui_visible ^= 1;

    if (fullscreen_ui_visible) {
        // UI is being shown - save mouse capture state and release if captured
        mouse_was_captured = (mouse_capture != 0);
        if (mouse_was_captured) {
            plat_mouse_capture(0);
        }
    } else {
        // UI is being hidden - restore previous mouse capture state
        if (mouse_was_captured) {
            plat_mouse_capture(1);
        }
    }

    ui->menubar->setVisible(fullscreen_ui_visible);
    ui->statusbar->setVisible(fullscreen_ui_visible && !hide_status_bar);
    ui->toolBar->setVisible(fullscreen_ui_visible && !hide_tool_bar);
}

void
MainWindow::on_actionTake_screenshot_triggered()
{
    startblit();
    for (auto &monitor : monitors)
        ++monitor.mon_screenshots;
    endblit();
    device_force_redraw();
}

void
MainWindow::on_actionTake_raw_screenshot_triggered()
{
    startblit();
    for (auto &monitor : monitors)
        ++monitor.mon_screenshots_raw;
    endblit();
    device_force_redraw();
}

void
MainWindow::on_actionCopy_screenshot_triggered()
{
    startblit();
    for (auto &monitor : monitors)
        ++monitor.mon_screenshots_clipboard;
    endblit();
    device_force_redraw();
}

void
MainWindow::on_actionCopy_raw_screenshot_triggered()
{
    startblit();
    for (auto &monitor : monitors)
        ++monitor.mon_screenshots_raw_clipboard;
    endblit();
    device_force_redraw();
}

void
MainWindow::on_actionMute_Unmute_triggered()
{
    sound_muted ^= 1;
    config_save();
    status->updateSoundIcon();
    ui->actionMute_Unmute->setText(sound_muted ? tr("&Unmute") : tr("&Mute"));
}

void
MainWindow::on_actionSound_gain_triggered()
{
    SoundGain gain(this);
    gain.exec();
}

void
MainWindow::setSendKeyboardInput(bool enabled)
{
    send_keyboard_input = enabled;
}

void
MainWindow::updateUiPauseState()
{
    const auto pause_icon   = dopause ? QIcon(":/menuicons/qt/icons/run.ico") : QIcon(":/menuicons/qt/icons/pause.ico");
    const auto tooltip_text = dopause ? QString(tr("Resume execution")) : QString(tr("Pause execution"));
    const auto menu_text    = dopause ? QString(tr("Re&sume")) : QString(tr("&Pause"));
    ui->actionPause->setIcon(pause_icon);
    ui->actionPause->setToolTip(tooltip_text);
    ui->actionPause->setText(menu_text);
    emit vmmRunningStateChanged(static_cast<VMManagerProtocol::RunningState>(window_blocked ? (dopause ? VMManagerProtocol::RunningState::PausedWaiting : VMManagerProtocol::RunningState::RunningWaiting) : (VMManagerProtocol::RunningState) dopause));
}

void
MainWindow::updateStatusEmptyIcons()
{
    if (status != nullptr)
        status->refreshEmptyIcons();
}

void
MainWindow::on_actionPreferences_triggered()
{
    ProgSettings progsettings(this);
    if (progsettings.exec() == QDialog::Accepted) {
        emit vmmGlobalConfigurationChanged();
    }
}

void
MainWindow::on_actionEnable_Discord_integration_triggered(bool checked)
{
    enable_discord = checked;
#ifdef DISCORD
    if (enable_discord) {
        discord_init();
        discord_update_activity(dopause);
        discordupdate.start(1000);
    } else {
        discord_close();
        discordupdate.stop();
    }
#endif
}

void
MainWindow::showSettings()
{
    if (findChild<Settings *>() == nullptr)
        ui->actionSettings->trigger();
}

void
MainWindow::hardReset()
{
    ui->actionHard_Reset->trigger();
}

void
MainWindow::togglePause()
{
    ui->actionPause->trigger();
}

void
MainWindow::changeEvent(QEvent *event)
{
#ifdef Q_OS_WINDOWS
    if (event->type() == QEvent::LanguageChange) {
        auto size = this->centralWidget()->size();
        QApplication::setFont(QFont(ProgSettings::getFontName(lang_id), 9));
        QApplication::processEvents();
        main_window->centralWidget()->setFixedSize(size);
        QApplication::processEvents();
        if (vid_resize == 1) {
            main_window->centralWidget()->setFixedSize(QWIDGETSIZE_MAX, QWIDGETSIZE_MAX);
        }
    }
#endif
    QWidget::changeEvent(event);
    if (isVisible()) {
        monitor_settings[0].mon_window_maximized = isMaximized();
        config_save();
    }
}

void
MainWindow::reloadAllRenderers()
{
    reload_renderers = true;
}

void
MainWindow::on_actionRenderer_options_triggered()
{
    if (const auto dlg = ui->stackedWidget->getOptions(this)) {
        if (dlg->exec() == QDialog::Accepted) {
            if (ui->stackedWidget->reloadRendererOption()) {
                ui->stackedWidget->switchRenderer(static_cast<RendererStack::Renderer>(vid_api));
                if (show_second_monitors) {
                    for (int i = 1; i < MONITORS_NUM; i++) {
                        if (renderers[i] && renderers[i]->reloadRendererOption() && renderers[i]->hasOptions()) {
                            ui->stackedWidget->switchRenderer(static_cast<RendererStack::Renderer>(vid_api));
                        }
                    }
                }
            } else
                for (int i = 1; i < MONITORS_NUM; i++) {
                    if (renderers[i] && renderers[i]->hasOptions())
                        renderers[i]->reloadOptions();
                }
        } else if (reload_renderers && ui->stackedWidget->reloadRendererOption()) {
            reload_renderers = false;
            ui->stackedWidget->switchRenderer(static_cast<RendererStack::Renderer>(vid_api));
            if (show_second_monitors) {
                for (int i = 1; i < MONITORS_NUM; i++) {
                    if (renderers[i]) {
                        renderers[i]->switchRenderer(static_cast<RendererStack::Renderer>(vid_api));
                    }
                }
            }
        }
    }
}

void
MainWindow::on_actionMCA_devices_triggered()
{
    if (const auto dlg = new MCADeviceList(this))
        dlg->exec();
}

void
MainWindow::on_actionShow_non_primary_monitors_triggered()
{
    show_second_monitors = static_cast<int>(ui->actionShow_non_primary_monitors->isChecked());

    if (show_second_monitors) {
        for (int monitor_index = 1; monitor_index < MONITORS_NUM; monitor_index++) {
            const auto &secondaryRenderer = renderers[monitor_index];
            if (!renderers[monitor_index])
                continue;
            secondaryRenderer->show();
            if (window_remember) {
                secondaryRenderer->setGeometry(monitor_settings[monitor_index].mon_window_x < 120 ? 120 : monitor_settings[monitor_index].mon_window_x,
                                               monitor_settings[monitor_index].mon_window_y < 120 ? 120 : monitor_settings[monitor_index].mon_window_y,
                                               monitor_settings[monitor_index].mon_window_w > 2048 ? 2048 : monitor_settings[monitor_index].mon_window_w,
                                               monitor_settings[monitor_index].mon_window_h > 2048 ? 2048 : monitor_settings[monitor_index].mon_window_h);
            }
            secondaryRenderer->switchRenderer(static_cast<RendererStack::Renderer>(vid_api));
            ui->stackedWidget->switchRenderer(static_cast<RendererStack::Renderer>(vid_api));
        }
    } else {
        for (int monitor_index = 1; monitor_index < MONITORS_NUM; monitor_index++) {
            auto &secondaryRenderer = renderers[monitor_index];
            if (!renderers[monitor_index])
                continue;
            secondaryRenderer->hide();
            if (window_remember && renderers[monitor_index]) {
                monitor_settings[monitor_index].mon_window_w = renderers[monitor_index]->geometry().width();
                monitor_settings[monitor_index].mon_window_h = renderers[monitor_index]->geometry().height();
                monitor_settings[monitor_index].mon_window_x = renderers[monitor_index]->geometry().x();
                monitor_settings[monitor_index].mon_window_y = renderers[monitor_index]->geometry().y();
            }
        }
    }
}

void
MainWindow::on_actionOpen_screenshots_folder_triggered()
{
    static_cast<void>(QDir(QString(usr_path) + QString("/screenshots/")).mkpath("."));
    QDesktopServices::openUrl(QUrl(QString("file:///") + usr_path + QString("/screenshots/")));
}

void
MainWindow::on_actionOpen_printer_tray_triggered()
{
    static_cast<void>(QDir(QString(usr_path) + QString("/printer/")).mkpath("."));
    QDesktopServices::openUrl(QUrl(QString("file:///") + usr_path + QString("/printer/")));
}

void
MainWindow::on_actionApply_fullscreen_stretch_mode_when_maximized_triggered(bool checked)
{
    video_fullscreen_scale_maximized = checked;

    const auto widget = ui->stackedWidget->currentWidget();
    ui->stackedWidget->onResize(widget->width(), widget->height());

    for (int i = 1; i < MONITORS_NUM; i++) {
        if (renderers[i])
            renderers[i]->onResize(renderers[i]->width(), renderers[i]->height());
    }

    device_force_redraw();
    config_save();
}

void
MainWindow::on_actionCursor_Puck_triggered()
{
    tablet_tool_type = 0;
    config_save();
}

void
MainWindow::on_actionPen_triggered()
{
    tablet_tool_type = 1;
    config_save();
}

void
MainWindow::on_actionACPI_Shutdown_triggered()
{
    acpi_pwrbut_pressed = 1;
}

void
MainWindow::on_actionCGA_composite_settings_triggered()
{
    isNonPause = true;
    CGASettingsDialog dialog;
    dialog.setModal(true);
    dialog.exec();
    isNonPause = false;
    config_save();
}
