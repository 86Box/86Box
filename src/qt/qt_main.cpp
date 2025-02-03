/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          Main entry point module
 *
 *
 * Authors: Joakim L. Gilje <jgilje@jgilje.net>
 *          Cacodemon345
 *          Teemu Korhonen
 *
 *          Copyright 2021 Joakim L. Gilje
 *          Copyright 2021-2022 Cacodemon345
 *          Copyright 2021-2022 Teemu Korhonen
 */
#include <QApplication>
#include <QSurfaceFormat>
#include <QDebug>
#include <QElapsedTimer>
#include <QThread>
#include <QTimer>
#include <QTranslator>
#include <QDirIterator>
#include <QLibraryInfo>
#include <QString>
#include <QFont>
#include <QDialog>
#include <QMessageBox>
#include <QPushButton>
#include <QFile>
#include <QTextStream>

#ifdef QT_STATIC
/* Static builds need plugin imports */
#    include <QtPlugin>
Q_IMPORT_PLUGIN(QICOPlugin)
#    ifdef Q_OS_WINDOWS
Q_IMPORT_PLUGIN(QWindowsIntegrationPlugin)
Q_IMPORT_PLUGIN(QWindowsVistaStylePlugin)
#    endif
#endif

extern "C" {
#include <86box/86box.h>
#include <86box/config.h>
#include <86box/plat.h>
#include <86box/ui.h>
#include <86box/video.h>
#ifdef DISCORD
#   include <86box/discord.h>
#endif
#include <86box/gdbstub.h>
#include <86box/version.h>
}

#ifdef Q_OS_WINDOWS
#    include "qt_rendererstack.hpp"
#    include "qt_winrawinputfilter.hpp"
#    include "qt_winmanagerfilter.hpp"
#    include <86box/win.h>
#    include <shobjidl.h>
#    include <windows.h>
#endif

#include <thread>
#include <iostream>
#include <memory>

#include "qt_mainwindow.hpp"
#include "qt_progsettings.hpp"
#include "qt_settings.hpp"
#include "cocoa_mouse.hpp"
#include "qt_styleoverride.hpp"
#include "qt_unixmanagerfilter.hpp"
#include "qt_util.hpp"

// Void Cast
#define VC(x) const_cast<wchar_t *>(x)

extern QElapsedTimer elapsed_timer;
extern MainWindow   *main_window;

extern "C" {
#include <86box/keyboard.h>
#include <86box/timer.h>
#include <86box/nvr.h>
extern int qt_nvr_save(void);
}

void qt_set_sequence_auto_mnemonic(bool b);

#ifdef Q_OS_WINDOWS
static void
keyboard_getkeymap()
{
    const LPCSTR  keyName   = "SYSTEM\\CurrentControlSet\\Control\\Keyboard Layout";
    const LPCSTR  valueName = "Scancode Map";
    unsigned char buf[32768];
    DWORD         bufSize;
    HKEY          hKey;
    int           j;
    UINT32       *bufEx2;
    int           scMapCount;
    UINT16       *bufEx;
    int           scancode_unmapped;
    int           scancode_mapped;

    /* First, prepare the default scan code map list which is 1:1.
     * Remappings will be inserted directly into it.
     * 512 bytes so this takes less memory, bit 9 set means E0
     * prefix.
     */
    for (j = 0; j < 512; j++)
        scancode_map[j] = j;

    /* Get the scan code remappings from:
    HKEY_LOCAL_MACHINE\SYSTEM\CurrentControlSet\Control\Keyboard Layout */
    bufSize = 32768;
    if (RegOpenKeyExA(HKEY_LOCAL_MACHINE, keyName, 0, 1, &hKey) == ERROR_SUCCESS) {
        if (RegQueryValueExA(hKey, valueName, NULL, NULL, buf, &bufSize) == ERROR_SUCCESS) {
            bufEx2     = (UINT32 *) buf;
            scMapCount = bufEx2[2];
            if ((bufSize != 0) && (scMapCount != 0)) {
                bufEx = (UINT16 *) (buf + 12);
                for (j = 0; j < scMapCount * 2; j += 2) {
                    /* Each scan code is 32-bit: 16 bits of remapped scan code,
                    and 16 bits of original scan code. */
                    scancode_unmapped = bufEx[j + 1];
                    scancode_mapped   = bufEx[j];

                    scancode_unmapped = convert_scan_code(scancode_unmapped);
                    scancode_mapped   = convert_scan_code(scancode_mapped);

                    /* Ignore source scan codes with prefixes other than E1
                       that are not E1 1D. */
                    if (scancode_unmapped != 0xFFFF)
                        scancode_map[scancode_unmapped] = scancode_mapped;
                }
            }
        }
        RegCloseKey(hKey);
    }
}

void
win_keyboard_handle(uint32_t scancode, int up, int e0, int e1)
{
    /* If it's not a scan code that starts with 0xE1 */
    if (e1) {
        if (scancode == 0x1D) {
            scancode = scancode_map[0x100]; /* Translate E1 1D to 0x100 (which would
                                               otherwise be E0 00 but that is invalid
                                               anyway).
                                               Also, take a potential mapping into
                                               account. */
        } else
            scancode = 0xFFFF;
        if (scancode != 0xFFFF)
            keyboard_input(!up, scancode);
    } else {
        if (e0)
            scancode |= 0x100;

        /* Translate the scan code to 9-bit */
        scancode = convert_scan_code(scancode);

        /* Remap it according to the list from the Registry */
        if ((scancode < (sizeof(scancode_map) / sizeof(scancode_map[0]))) && (scancode != scancode_map[scancode])) {
            // pclog("Scan code remap: %03X -> %03X\n", scancode, scancode_map[scancode]);
            scancode = scancode_map[scancode];
        }

        /* If it's not 0xFFFF, send it to the emulated
           keyboard.
           We use scan code 0xFFFF to mean a mapping that
           has a prefix other than E0 and that is not E1 1D,
           which is, for our purposes, invalid. */

        /* Translate right CTRL to left ALT if the user has so
           chosen. */
        if ((scancode == 0x11d) && rctrl_is_lalt)
            scancode = 0x038;

        /* Normal scan code pass through, pass it through as is if
           it's not an invalid scan code. */
        if (scancode != 0xFFFF)
            keyboard_input(!up, scancode);

        main_window->checkFullscreenHotkey();
    }
}

static LRESULT CALLBACK
emu_LowLevelKeyboardProc(int nCode, WPARAM wParam, LPARAM lParam)
{
    LPKBDLLHOOKSTRUCT lpKdhs    = (LPKBDLLHOOKSTRUCT) lParam;
    /* Checks if CTRL was pressed. */
    BOOL              bCtrlDown = GetAsyncKeyState (VK_CONTROL) >> ((sizeof(SHORT) * 8) - 1);
    BOOL              is_over_window = (GetForegroundWindow() == ((HWND) main_window->winId()));
    BOOL              ret       = TRUE;

    static int        last      = 0;

    if (show_second_monitors)  for (int monitor_index = 1; monitor_index < MONITORS_NUM; monitor_index++) {
        const auto &secondaryRenderer = main_window->renderers[monitor_index];
        is_over_window = is_over_window || ((secondaryRenderer != nullptr) &&
                         (GetForegroundWindow() == ((HWND) secondaryRenderer->winId())));
    }

    if ((nCode < 0) || (nCode != HC_ACTION) || !is_over_window)
        return CallNextHookEx(NULL, nCode, wParam, lParam);
    else if ((lpKdhs->scanCode == 0x01) && (lpKdhs->flags & LLKHF_ALTDOWN) &&
        !(lpKdhs->flags & (LLKHF_UP | LLKHF_EXTENDED)))
        ret = TRUE;
    else if ((lpKdhs->scanCode == 0x01) && bCtrlDown && !(lpKdhs->flags & (LLKHF_UP | LLKHF_EXTENDED)))
        ret = TRUE;
    else if ((lpKdhs->scanCode == 0x0f) && (lpKdhs->flags & LLKHF_ALTDOWN) &&
             !(lpKdhs->flags & (LLKHF_UP | LLKHF_EXTENDED)))
        ret = TRUE;
    else if ((lpKdhs->scanCode == 0x0f) && bCtrlDown && !(lpKdhs->flags & (LLKHF_UP | LLKHF_EXTENDED)))
        ret = TRUE;
    else if ((lpKdhs->scanCode == 0x39) && (lpKdhs->flags & LLKHF_ALTDOWN) &&
             !(lpKdhs->flags & (LLKHF_UP | LLKHF_EXTENDED)))
        ret = TRUE;
    else if ((lpKdhs->scanCode == 0x3e) && (lpKdhs->flags & LLKHF_ALTDOWN) &&
             !(lpKdhs->flags & (LLKHF_UP | LLKHF_EXTENDED)))
        ret = TRUE;
    else if ((lpKdhs->scanCode == 0x49) && bCtrlDown && !(lpKdhs->flags & LLKHF_UP))
        ret = TRUE;
    else if ((lpKdhs->scanCode >= 0x5b) && (lpKdhs->scanCode <= 0x5d) && (lpKdhs->flags & LLKHF_EXTENDED))
        ret = TRUE;
    else
        ret = CallNextHookEx(NULL, nCode, wParam, lParam);

    if (lpKdhs->scanCode == 0x00000045) {
        if ((lpKdhs->flags & LLKHF_EXTENDED) && (lpKdhs->vkCode == 0x00000090)) {
             /* NumLock. */
             lpKdhs->flags &= ~LLKHF_EXTENDED;
        } else if (!(lpKdhs->flags & LLKHF_EXTENDED) && (lpKdhs->vkCode == 0x00000013)) {
            /* Pause - send E1 1D. */
            win_keyboard_handle(0xe1, 0, 0, 0);
            win_keyboard_handle(0x1d, LLKHF_UP, 0, 0);
        }
    } else if (!last && (lpKdhs->scanCode == 0x00000036))
        /* Non-fake right shift. */
        lpKdhs->flags &= ~LLKHF_EXTENDED;

    if (lpKdhs->scanCode == 0x00000236)
        last = 1;
    else if (last && (lpKdhs->scanCode == 0x00000036))
        last = 0;

    win_keyboard_handle(lpKdhs->scanCode, lpKdhs->flags & LLKHF_UP, lpKdhs->flags & LLKHF_EXTENDED, 0);

    return ret;
}
#endif

#ifdef Q_OS_WINDOWS
static HHOOK llhook  = NULL;
#endif

void
main_thread_fn()
{
    int      frames;

    QThread::currentThread()->setPriority(QThread::HighestPriority);
    plat_set_thread_name(nullptr, "main_thread_fn");
    framecountx = 0;
    // title_update = 1;
    uint64_t old_time = elapsed_timer.elapsed();
    int drawits = frames = 0;
    while (!is_quit && cpu_thread_run) {
        /* See if it is time to run a frame of code. */
        const uint64_t new_time = elapsed_timer.elapsed();
#ifdef USE_GDBSTUB
        if (gdbstub_next_asap && (drawits <= 0))
            drawits = 10;
        else
#endif
            drawits += static_cast<int>(new_time - old_time);
        old_time = new_time;
        if (drawits > 0 && !dopause) {
            /* Yes, so do one frame now. */
            drawits -= 10;
            if (drawits > 50)
                drawits = 0;

#ifdef USE_INSTRUMENT
            uint64_t start_time = elapsed_timer.nsecsElapsed();
#endif
            /* Run a block of code. */
            pc_run();

#ifdef USE_INSTRUMENT
            if (instru_enabled) {
                uint64_t elapsed_us       = (elapsed_timer.nsecsElapsed() - start_time) / 1000;
                uint64_t total_elapsed_ms = (uint64_t) ((double) tsc / cpu_s->rspeed * 1000);
                printf("[instrument] %llu, %llu\n", total_elapsed_ms, elapsed_us);
                if (instru_run_ms && total_elapsed_ms >= instru_run_ms)
                    break;
            }
#endif
            /* Every 200 frames we save the machine status. */
            if (++frames >= 200 && nvr_dosave) {
                qt_nvr_save();
                nvr_dosave = 0;
                frames     = 0;
            }
        } else {
            /* Just so we dont overload the host OS. */

            /* Trigger a hard reset if one is pending. */
            if (hard_reset_pending) {
                hard_reset_pending = 0;
                pc_reset_hard_close();
                pc_reset_hard_init();
            }

            if (dopause)
                ack_pause();

            plat_delay_ms(1);
        }
    }

    is_quit = 1;
    for (uint8_t i = 1; i < GFXCARD_MAX; i ++) {
        if (gfxcard[i]) {
            ui_deinit_monitor(i);
            plat_delay_ms(500);
        }
    }
    QTimer::singleShot(0, QApplication::instance(), []() { QApplication::processEvents(); QApplication::instance()->quit(); });
}

static std::thread *main_thread;

#ifdef Q_OS_WINDOWS
extern bool windows_is_light_theme();
#endif

int
main(int argc, char *argv[])
{
#if QT_VERSION < QT_VERSION_CHECK(6, 0, 0)
    QApplication::setAttribute(Qt::AA_DisableHighDpiScaling, false);
    QApplication::setAttribute(Qt::AA_UseHighDpiPixmaps);
    QApplication::setAttribute(Qt::AA_EnableHighDpiScaling);
#endif
#if QT_VERSION >= QT_VERSION_CHECK(5, 14, 0)
    QApplication::setHighDpiScaleFactorRoundingPolicy(Qt::HighDpiScaleFactorRoundingPolicy::PassThrough);
#endif

    QApplication app(argc, argv);
    QLocale::setDefault(QLocale::C);

#ifdef Q_OS_WINDOWS
    Q_INIT_RESOURCE(darkstyle);
    QApplication::setAttribute(Qt::AA_NativeWindows);

    if (!windows_is_light_theme()) {
        QFile f(":qdarkstyle/dark/darkstyle.qss");

        if (!f.exists())   {
            printf("Unable to set stylesheet, file not found\n");
        } else   {
            f.open(QFile::ReadOnly | QFile::Text);
            QTextStream ts(&f);
            qApp->setStyleSheet(ts.readAll());
        }
    }
#endif

    qt_set_sequence_auto_mnemonic(false);
    Q_INIT_RESOURCE(qt_resources);
    Q_INIT_RESOURCE(qt_translations);
    QSurfaceFormat fmt = QSurfaceFormat::defaultFormat();
    fmt.setSwapInterval(0);
    QSurfaceFormat::setDefaultFormat(fmt);
    app.setStyle(new StyleOverride());

#ifdef __APPLE__
    CocoaEventFilter cocoafilter;
    app.installNativeEventFilter(&cocoafilter);
#endif
    elapsed_timer.start();

    for (size_t i = 0; i < sizeof(scancode_map) / sizeof(scancode_map[0]); i++)
        scancode_map[i] = i;

#ifdef Q_OS_WINDOWS
    keyboard_getkeymap();
#endif

    if (!pc_init(argc, argv)) {
        return 0;
    }

    bool startMaximized = window_remember && monitor_settings[0].mon_window_maximized;
    fprintf(stderr, "Qt: version %s, platform \"%s\"\n", qVersion(), QApplication::platformName().toUtf8().data());
    ProgSettings::loadTranslators(&app);
#ifdef Q_OS_WINDOWS
    QApplication::setFont(QFont(ProgSettings::getFontName(lang_id), 9));
    SetCurrentProcessExplicitAppUserModelID(L"86Box.86Box");
#endif

#ifndef Q_OS_MACOS
#    ifdef RELEASE_BUILD
    app.setWindowIcon(QIcon(":/settings/qt/icons/86Box-green.ico"));
#    elif defined ALPHA_BUILD
    app.setWindowIcon(QIcon(":/settings/qt/icons/86Box-red.ico"));
#    elif defined BETA_BUILD
    app.setWindowIcon(QIcon(":/settings/qt/icons/86Box-yellow.ico"));
#    else
    app.setWindowIcon(QIcon(":/settings/qt/icons/86Box-gray.ico"));
#    endif

#    ifdef Q_OS_UNIX
    app.setDesktopFileName("net.86box.86Box");
#    endif
#endif

    if (!pc_init_modules()) {
        QMessageBox fatalbox(QMessageBox::Icon::Critical, QObject::tr("No ROMs found"),
                             QObject::tr("86Box could not find any usable ROM images.\n\nPlease <a href=\"https://github.com/86Box/roms/releases/latest\">download</a> a ROM set and extract it into the \"roms\" directory."),
                             QMessageBox::Ok);
        fatalbox.setTextFormat(Qt::TextFormat::RichText);
        fatalbox.exec();
        return 6;
    }

    // UUID / copy / move detection
    if(!util::compareUuid()) {
        QMessageBox movewarnbox;
        movewarnbox.setIcon(QMessageBox::Icon::Warning);
        movewarnbox.setText(QObject::tr("This machine might have been moved or copied."));
        movewarnbox.setInformativeText(QObject::tr("In order to ensure proper networking functionality, 86Box needs to know if this machine was moved or copied.\n\nSelect \"I Copied It\" if you are not sure."));
        const QPushButton *movedButton  = movewarnbox.addButton(QObject::tr("I Moved It"), QMessageBox::AcceptRole);
        const QPushButton *copiedButton = movewarnbox.addButton(QObject::tr("I Copied It"), QMessageBox::DestructiveRole);
        QPushButton       *cancelButton = movewarnbox.addButton(QObject::tr("Cancel"), QMessageBox::RejectRole);
        movewarnbox.setDefaultButton(cancelButton);
        movewarnbox.exec();
        if (movewarnbox.clickedButton() == copiedButton) {
            util::storeCurrentUuid();
            util::generateNewMacAdresses();
        } else if (movewarnbox.clickedButton() == movedButton) {
            util::storeCurrentUuid();
        }
    }

#ifdef Q_OS_WINDOWS
#    if !defined(EMU_BUILD_NUM) || (EMU_BUILD_NUM != 5624)
    HWND winbox = FindWindowW(L"TWinBoxMain", NULL);
    if (winbox &&
        FindWindowExW(winbox, NULL, L"TToolBar", NULL) &&
        FindWindowExW(winbox, NULL, L"TListBox", NULL) &&
        FindWindowExW(winbox, NULL, L"TStatusBar", NULL) &&
        (winbox = FindWindowExW(winbox, NULL, L"TPageControl", NULL)) && /* holds a TTabSheet even on VM pages */
        FindWindowExW(winbox, NULL, L"TTabSheet", NULL))
#    endif
    {
        QMessageBox warningbox(QMessageBox::Icon::Warning, QObject::tr("WinBox is no longer supported"),
                               QObject::tr("Development of the WinBox manager stopped in 2022 due to a lack of maintainers. As we direct our efforts towards making 86Box even better, we have made the decision to no longer support WinBox as a manager.\n\nNo further updates will be provided through WinBox, and you may encounter incorrect behavior should you continue using it with newer versions of 86Box. Any bug reports related to WinBox behavior will be closed as invalid.\n\nGo to 86box.net for a list of other managers you can use."),
                               QMessageBox::NoButton);
        warningbox.addButton(QObject::tr("Continue"), QMessageBox::AcceptRole);
        warningbox.addButton(QObject::tr("Exit"), QMessageBox::RejectRole);
        warningbox.exec();
        if (warningbox.result() == QDialog::Accepted)
              return 0;
    }
#endif

    if (settings_only) {
        Settings settings;
        if (settings.exec() == QDialog::Accepted) {
            settings.save();
            config_save();
        }
        return 0;
    }

    /* Warn the user about unsupported configs */
    if (cpu_override) {
        QMessageBox warningbox(QMessageBox::Icon::Warning, QObject::tr("You are loading an unsupported configuration"),
                               QObject::tr("CPU type filtering based on selected machine is disabled for this emulated machine.\n\nThis makes it possible to choose a CPU that is otherwise incompatible with the selected machine. However, you may run into incompatibilities with the machine BIOS or other software.\n\nEnabling this setting is not officially supported and any bug reports filed may be closed as invalid."),
                               QMessageBox::NoButton);
        warningbox.addButton(QObject::tr("Continue"), QMessageBox::AcceptRole);
        warningbox.addButton(QObject::tr("Exit"), QMessageBox::RejectRole);
        warningbox.exec();
        if (warningbox.result() == QDialog::Accepted)
              return 0;
    }

#ifdef DISCORD
    discord_load();
#endif

    main_window = new MainWindow();
    if (startMaximized) {
        main_window->showMaximized();
    } else {
        main_window->show();
    }

    app.installEventFilter(main_window);

#ifdef Q_OS_WINDOWS
    /* Setup VM-manager messages */
    std::unique_ptr<WindowsManagerFilter> wmfilter;
    if (source_hwnd) {
        HWND main_hwnd = (HWND) main_window->winId();

        wmfilter.reset(new WindowsManagerFilter());
        QObject::connect(wmfilter.get(), &WindowsManagerFilter::showsettings, main_window, &MainWindow::showSettings);
        QObject::connect(wmfilter.get(), &WindowsManagerFilter::pause, main_window, &MainWindow::togglePause);
        QObject::connect(wmfilter.get(), &WindowsManagerFilter::reset, main_window, &MainWindow::hardReset);
        QObject::connect(wmfilter.get(), &WindowsManagerFilter::request_shutdown, main_window, &MainWindow::close);
        QObject::connect(wmfilter.get(), &WindowsManagerFilter::force_shutdown, []() {
            do_stop();
            emit main_window->close();
        });
        QObject::connect(wmfilter.get(), &WindowsManagerFilter::ctrlaltdel, []() { pc_send_cad(); });
        QObject::connect(wmfilter.get(), &WindowsManagerFilter::dialogstatus, [main_hwnd](bool open) {
            PostMessage((HWND) (uintptr_t) source_hwnd, WM_SENDDLGSTATUS, (WPARAM) (open ? 1 : 0), (LPARAM) main_hwnd);
        });

        /* Native filter to catch VM-managers commands */
        app.installNativeEventFilter(wmfilter.get());

        /* Filter to catch main window being blocked (by modal dialog) */
        main_window->installEventFilter(wmfilter.get());

        /* Send main window HWND to manager */
        PostMessage((HWND) (uintptr_t) source_hwnd, WM_SENDHWND, (WPARAM) unique_id, (LPARAM) main_hwnd);

        /* Send shutdown message to manager */
        QObject::connect(&app, &QApplication::destroyed, [main_hwnd](QObject *) {
            PostMessage((HWND) (uintptr_t) source_hwnd, WM_HAS_SHUTDOWN, (WPARAM) 0, (LPARAM) main_hwnd);
        });
    }

    /* Force raw input if a debugger is present. */
    if (IsDebuggerPresent()) {
        pclog("WARNING: Debugged detected, forcing raw input\n");
        hook_enabled = 0;
    }

    if (hook_enabled) {
        /* Yes, low-level hooks *DO* work with raw input, at least global ones. */
        llhook = SetWindowsHookEx(WH_KEYBOARD_LL, emu_LowLevelKeyboardProc, NULL, 0);
        atexit([] () -> void {
            if (llhook)
                UnhookWindowsHookEx(llhook);
        });
    }

    /* Setup raw input */
    auto rawInputFilter = WindowsRawInputFilter::Register(main_window);
    if (rawInputFilter) {
        app.installNativeEventFilter(rawInputFilter.get());
        main_window->setSendKeyboardInput(false);
    }
#endif

    UnixManagerSocket socket;
    if (qgetenv("86BOX_MANAGER_SOCKET").size()) {
        QObject::connect(&socket, &UnixManagerSocket::showsettings, main_window, &MainWindow::showSettings);
        QObject::connect(&socket, &UnixManagerSocket::pause, main_window, &MainWindow::togglePause);
        QObject::connect(&socket, &UnixManagerSocket::resetVM, main_window, &MainWindow::hardReset);
        QObject::connect(&socket, &UnixManagerSocket::request_shutdown, main_window, &MainWindow::close);
        QObject::connect(&socket, &UnixManagerSocket::force_shutdown, []() {
            do_stop();
            emit main_window->close();
        });
        QObject::connect(&socket, &UnixManagerSocket::ctrlaltdel, []() { pc_send_cad(); });
        main_window->installEventFilter(&socket);
        socket.connectToServer(qgetenv("86BOX_MANAGER_SOCKET"));
    }

    // pc_reset_hard_init();

    QTimer onesec;
    QObject::connect(&onesec, &QTimer::timeout, &app, [] {
        pc_onesec();
    });
    onesec.setTimerType(Qt::PreciseTimer);
    onesec.start(1000);

#ifdef DISCORD
    QTimer discordupdate;
    if (discord_loaded) {
        QTimer::singleShot(1000, &app, [] {
            if (enable_discord) {
                discord_init();
                discord_update_activity(dopause);
            } else
                discord_close();
        });
        QObject::connect(&discordupdate, &QTimer::timeout, &app, [] {
            discord_run_callbacks();
        });
        discordupdate.start(1000);
    }
#endif

    /* Initialize the rendering window, or fullscreen. */
    QTimer::singleShot(0, &app, [] {
        pc_reset_hard_init();

        /* Set the PAUSE mode depending on the renderer. */
#ifdef USE_VNC
        if (vid_api == 5)
            plat_pause(1);
        else
#endif
            plat_pause(0);

        main_thread = new std::thread(main_thread_fn);
    });

    const auto ret       = app.exec();
    cpu_thread_run = 0;
    main_thread->join();
    pc_close(nullptr);
    endblit();

    socket.close();
    return ret;
}
