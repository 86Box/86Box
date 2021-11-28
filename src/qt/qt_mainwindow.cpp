#include "qt_mainwindow.hpp"
#include "ui_qt_mainwindow.h"

extern "C" {
#include <86box/86box.h>
//#include <86box/keyboard.h>
//#include <86box/mouse.h>
#include <86box/config.h>
#include <86box/plat.h>

#include "qt_sdl.h"
};

#include <QWindow>
#include <QDebug>
#include <QTimer>
#include <QKeyEvent>

#include "qt_settings.hpp"

MainWindow::MainWindow(QWidget *parent) :
    QMainWindow(parent),
    ui(new Ui::MainWindow)
{
    Q_INIT_RESOURCE(qt_resources);

    ui->setupUi(this);

    connect(this, &MainWindow::pollMouse, this, [] {
        sdl_mouse_poll();
    });

    connect(this, &MainWindow::setMouseCapture, this, [](bool state) {
        mouse_capture = state ? 1 : 0;
        sdl_mouse_capture(mouse_capture);
    });

    connect(this, &MainWindow::setFullscreen, this, [](bool state) {
        video_fullscreen = state ? 1 : 0;
        sdl_set_fs(video_fullscreen);
    });

    connect(this, &MainWindow::resizeContents, this, [this](int w, int h) {
        sdl_resize(w, h);
    });

    connect(ui->menubar, &QMenuBar::triggered, this, [] {
        config_save();
    });

    connect(this, &MainWindow::updateStatusBarPanes, ui->machineStatus, &MachineStatus::refresh);
    connect(this, &MainWindow::updateStatusBarActivity, ui->machineStatus, &MachineStatus::setActivity);
    connect(this, &MainWindow::updateStatusBarEmpty, ui->machineStatus, &MachineStatus::setEmpty);

    ui->actionKeyboard_requires_capture->setChecked(kbd_req_capture);
    ui->actionRight_CTRL_is_left_ALT->setChecked(rctrl_is_lalt);

    sdl_inits();
    sdl_timer = new QTimer(this);
    connect(sdl_timer, &QTimer::timeout, this, [] {
        auto status = sdl_main();
        if (status == SdlMainQuit) {
            QApplication::quit();
        }
    });
    sdl_timer->start(5);
}

MainWindow::~MainWindow() {
    sdl_close();
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
    Settings settings;
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
}

static const int keycode_entries = 136;
// xmodmap -pk
static const uint16_t xfree86_keycode_table[keycode_entries] = {
    /*   0 */ 0,
    /*   1 */ 0,
    /*   2 */ 0,
    /*   3 */ 0,
    /*   4 */ 0,
    /*   5 */ 0,
    /*   6 */ 0,
    /*   7 */ 0,
    /*   8 */ 0,
    /*   9 */ 0x01, // Esc
    /*  10 */ 0x02, // 1
    /*  11 */ 0x03, // 2
    /*  12 */ 0x04, // 3
    /*  13 */ 0x05, // 4
    /*  14 */ 0x06, // 5
    /*  15 */ 0x07, // 6
    /*  16 */ 0x08, // 7
    /*  17 */ 0x09, // 8
    /*  18 */ 0x0a, // 9
    /*  19 */ 0x0b, // 0
    /*  20 */ 0x0c, // -
    /*  21 */ 0x0d, // =
    /*  22 */ 0x0e, // BackSpace
    /*  23 */ 0x0f, // Tab
    /*  24 */ 0x10, // Q
    /*  25 */ 0x11, // W
    /*  26 */ 0x12, // E
    /*  27 */ 0x13, // R
    /*  28 */ 0x14, // T
    /*  29 */ 0x15, // Y
    /*  30 */ 0x16, // U
    /*  31 */ 0x17, // I
    /*  32 */ 0x18, // O
    /*  33 */ 0x19, // P
    /*  34 */ 0x1a, // [
    /*  35 */ 0x1b, // ]
    /*  36 */ 0x1c, // Return
    /*  37 */ 0x1d, // LeftControl
    /*  38 */ 0x1e, // A
    /*  39 */ 0x1f, // S
    /*  40 */ 0x20, // D
    /*  41 */ 0x21, // F
    /*  42 */ 0x22, // G
    /*  43 */ 0x23, // H
    /*  44 */ 0x24, // J
    /*  45 */ 0x25, // K
    /*  46 */ 0x26, // L
    /*  47 */ 0x27, // ;
    /*  48 */ 0x28, // '
    /*  49 */ 0x29, // ` (???)
    /*  50 */ 0x2a, // LeftShift
    /*  51 */ 0x2b, // BackSlash
    /*  52 */ 0x2c, // Z
    /*  53 */ 0x2d, // X
    /*  54 */ 0x2e, // C
    /*  55 */ 0x2f, // V
    /*  56 */ 0x30, // B
    /*  57 */ 0x31, // N
    /*  58 */ 0x32, // M
    /*  59 */ 0x33, // ,
    /*  60 */ 0x34, // .
    /*  61 */ 0x35, // -
    /*  62 */ 0x36, // RightShift
    /*  63 */ 0x37, // KeyPad Multiply
    /*  64 */ 0x38, // LeftAlt
    /*  65 */ 0x39, // Space
    /*  66 */ 0x3a, // CapsLock
    /*  67 */ 0x3b, // F01
    /*  68 */ 0x3c, // F02
    /*  69 */ 0x3d, // F03
    /*  70 */ 0x3e, // F04
    /*  71 */ 0x3f, // F05
    /*  72 */ 0x40, // F06
    /*  73 */ 0x41, // F07
    /*  74 */ 0x42, // F08
    /*  75 */ 0x43, // F09
    /*  76 */ 0x44, // F10
    /*  77 */ 0x45, // NumLock
    /*  78 */ 0x46, // ScrollLock
    /*  79 */ 0x47, // KeyPad7
    /*  80 */ 0x48, // KeyPad8
    /*  81 */ 0x49, // KeyPad9
    /*  82 */ 0x4a, // KeyPad Minus
    /*  83 */ 0x4b, // KeyPad4
    /*  84 */ 0x4c, // KeyPad5
    /*  85 */ 0x4d, // KeyPad6
    /*  86 */ 0x4e, // KeyPad Plus
    /*  87 */ 0x4f, // KeyPad1
    /*  88 */ 0x50, // KeyPad2
    /*  89 */ 0x51, // KeyPad3
    /*  90 */ 0x52, // KeyPad0
    /*  91 */ 0x53, // KeyPad .
    /*  92 */ 0,
    /*  93 */ 0,
    /*  94 */ 0x56, // Less/Great
    /*  95 */ 0x57, // F11
    /*  96 */ 0x58, // F12
    /*  97 */ 0,
    /*  98 */ 0,
    /*  99 */ 0,
    /* 100 */ 0,
    /* 101 */ 0,
    /* 102 */ 0,
    /* 103 */ 0,
    /* 104 */ 0x11c, // KeyPad Enter
    /* 105 */ 0x11d, // RightControl
    /* 106 */ 0x135, // KeyPad Divide
    /* 107 */ 0x137, // PrintScreen / SysReq
    /* 108 */ 0x138, // RightAlt
    /* 109 */ 0,
    /* 110 */ 0x147, // Home
    /* 111 */ 0x148, // Up
    /* 112 */ 0x149, // PageUp
    /* 113 */ 0x14b, // Left
    /* 114 */ 0x14d, // Right
    /* 115 */ 0x14f, // End
    /* 116 */ 0x150, // Down
    /* 117 */ 0x151, // PageDown
    /* 118 */ 0x152, // Insert
    /* 119 */ 0x153, // Delete
    /* 120 */ 0,
    /* 121 */ 0,
    /* 122 */ 0,
    /* 123 */ 0,
    /* 124 */ 0,
    /* 125 */ 0,
    /* 126 */ 0,
    /* 127 */ 0,
    /* 128 */ 0,
    /* 129 */ 0,
    /* 130 */ 0,
    /* 131 */ 0,
    /* 132 */ 0,
    /* 133 */ 0x15b, // SuperLeft
    /* 134 */ 0x15c, // SuperRight
    /* 135 */ 0x15d, // Application
};

//static void handle_keypress_event(int state, quint32 native_scancode) {
//    if (native_scancode > keycode_entries) {
//        return;
//    }
//    uint16_t translated_code = xfree86_keycode_table[native_scancode];
//    if (translated_code == 0) {
//        return;
//    }
//    keyboard_input(state, translated_code);

//    if (keyboard_isfsexit() > 0) {
//        plat_setfullscreen(0);
//    }

//    if (keyboard_ismsexit() > 0) {
//        plat_mouse_capture(0);
//    }
//}

void MainWindow::on_actionFullscreen_triggered() {
    setFullscreen(true);
}
