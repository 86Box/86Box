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
#    include <86box/discord.h>
#endif
#include <86box/gdbstub.h>
#include <86box/version.h>
#include <86box/renderdefs.h>
#ifdef Q_OS_LINUX
#    define GAMEMODE_AUTO
#    include "../unix/gamemode/gamemode_client.h"
#endif
}

#ifdef Q_OS_WINDOWS
#    include "qt_rendererstack.hpp"
#    include "qt_winrawinputfilter.hpp"
#    include "qt_winmanagerfilter.hpp"
#    include "qt_vmmanager_windarkmodefilter.hpp"
#    include <86box/win.h>
#    include <shobjidl.h>
#    include <windows.h>
#endif

#include <thread>
#include <iostream>
#include <memory>

#include "qt_defs.hpp"
#include "qt_mainwindow.hpp"
#include "qt_progsettings.hpp"
#include "qt_settings.hpp"
#include "cocoa_mouse.hpp"
#include "qt_styleoverride.hpp"
#include "qt_unixmanagerfilter.hpp"
#include "qt_util.hpp"
#include "qt_vmmanager_clientsocket.hpp"
#include "qt_vmmanager_mainwindow.hpp"

// Void Cast
#define VC(x) const_cast<wchar_t *>(x)

extern QElapsedTimer elapsed_timer;
extern MainWindow   *main_window;

extern "C" {
#include <86box/keyboard.h>
#include "cpu.h"
#include <86box/timer.h>
#include <86box/nvr.h>
extern int  qt_nvr_save(void);
extern void exit_pause(void);

bool cpu_thread_running = false;
bool fast_forward = false;
}

#include <locale.h>

void qt_set_sequence_auto_mnemonic(bool b);

#ifdef Q_OS_WINDOWS
bool acp_utf8 = false;

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
    }
}

static LRESULT CALLBACK
emu_LowLevelKeyboardProc(int nCode, WPARAM wParam, LPARAM lParam)
{
    LPKBDLLHOOKSTRUCT lpKdhs = (LPKBDLLHOOKSTRUCT) lParam;
    /* Checks if CTRL was pressed. */
    BOOL bCtrlDown      = GetAsyncKeyState(VK_CONTROL) >> ((sizeof(SHORT) * 8) - 1);
    BOOL is_over_window = (GetForegroundWindow() == ((HWND) main_window->winId()));
    BOOL ret            = TRUE;

    static int last = 0;

    if (show_second_monitors)
        for (int monitor_index = 1; monitor_index < MONITORS_NUM; monitor_index++) {
            const auto &secondaryRenderer = main_window->renderers[monitor_index];
            is_over_window                = is_over_window || ((secondaryRenderer != nullptr) && (GetForegroundWindow() == ((HWND) secondaryRenderer->winId())));
        }

    bool skip = ((nCode < 0) || (nCode != HC_ACTION) || !is_over_window || (kbd_req_capture && !mouse_capture));

    if (skip)
        return CallNextHookEx(NULL, nCode, wParam, lParam);

    /* USB keyboards send a scancode of 0x00 for multimedia keys. */
    if (lpKdhs->scanCode == 0x00) {
        /* Handle USB keyboard multimedia keys where possible.
           Only a handful of keys can be handled via Virtual Key
           detection; rest can't be reliably detected. */
        DWORD vkCode = lpKdhs->vkCode;
        bool  up     = !!(lpKdhs->flags & LLKHF_UP);

        if (inhibit_multimedia_keys &&
           (lpKdhs->vkCode == VK_MEDIA_PLAY_PAUSE ||
           lpKdhs->vkCode == VK_MEDIA_NEXT_TRACK ||
           lpKdhs->vkCode == VK_MEDIA_PREV_TRACK ||
           lpKdhs->vkCode == VK_VOLUME_DOWN ||
           lpKdhs->vkCode == VK_VOLUME_UP ||
           lpKdhs->vkCode == VK_VOLUME_MUTE ||
           lpKdhs->vkCode == VK_MEDIA_STOP ||
           lpKdhs->vkCode == VK_LAUNCH_MEDIA_SELECT ||
           lpKdhs->vkCode == VK_LAUNCH_MAIL ||
           lpKdhs->vkCode == VK_LAUNCH_APP1 ||
           lpKdhs->vkCode == VK_LAUNCH_APP2 ||
           lpKdhs->vkCode == VK_HELP ||
           lpKdhs->vkCode == VK_BROWSER_BACK ||
           lpKdhs->vkCode == VK_BROWSER_FORWARD ||
           lpKdhs->vkCode == VK_BROWSER_FAVORITES ||
           lpKdhs->vkCode == VK_BROWSER_HOME ||
           lpKdhs->vkCode == VK_BROWSER_REFRESH ||
           lpKdhs->vkCode == VK_BROWSER_SEARCH ||
           lpKdhs->vkCode == VK_BROWSER_STOP))
            ret = TRUE;
        else
            ret = CallNextHookEx(NULL, nCode, wParam, lParam);

        switch (vkCode) {
            case VK_MEDIA_PLAY_PAUSE:
                {
                    win_keyboard_handle(0x22, up, 1, 0);
                    break;
                }
            case VK_MEDIA_STOP:
                {
                    win_keyboard_handle(0x24, up, 1, 0);
                    break;
                }
            case VK_VOLUME_UP:
                {
                    win_keyboard_handle(0x30, up, 1, 0);
                    break;
                }
            case VK_VOLUME_DOWN:
                {
                    win_keyboard_handle(0x2E, up, 1, 0);
                    break;
                }
            case VK_VOLUME_MUTE:
                {
                    win_keyboard_handle(0x20, up, 1, 0);
                    break;
                }
            case VK_MEDIA_NEXT_TRACK:
                {
                    win_keyboard_handle(0x19, up, 1, 0);
                    break;
                }
            case VK_MEDIA_PREV_TRACK:
                {
                    win_keyboard_handle(0x10, up, 1, 0);
                    break;
                }
            case VK_LAUNCH_MEDIA_SELECT:
                {
                    win_keyboard_handle(0x6D, up, 1, 0);
                    break;
                }
            case VK_LAUNCH_MAIL:
                {
                    win_keyboard_handle(0x6C, up, 1, 0);
                    break;
                }
            case VK_LAUNCH_APP1:
                {
                    win_keyboard_handle(0x6B, up, 1, 0);
                    break;
                }
            case VK_LAUNCH_APP2:
                {
                    win_keyboard_handle(0x21, up, 1, 0);
                    break;
                }
            case VK_BROWSER_BACK:
                {
                    win_keyboard_handle(0x6A, up, 1, 0);
                    break;
                }
            case VK_BROWSER_FORWARD:
                {
                    win_keyboard_handle(0x69, up, 1, 0);
                    break;
                }
            case VK_BROWSER_STOP:
                {
                    win_keyboard_handle(0x68, up, 1, 0);
                    break;
                }
            case VK_BROWSER_HOME:
                {
                    win_keyboard_handle(0x32, up, 1, 0);
                    break;
                }
            case VK_BROWSER_SEARCH:
                {
                    win_keyboard_handle(0x65, up, 1, 0);
                    break;
                }
            case VK_BROWSER_REFRESH:
                {
                    win_keyboard_handle(0x67, up, 1, 0);
                    break;
                }
            case VK_BROWSER_FAVORITES:
                {
                    win_keyboard_handle(0x66, up, 1, 0);
                    break;
                }
            case VK_HELP:
                {
                    win_keyboard_handle(0x3b, up, 1, 0);
                    break;
                }
        }

        return ret;
    } else if ((lpKdhs->scanCode == 0x01) && (lpKdhs->flags & LLKHF_ALTDOWN) && !(lpKdhs->flags & (LLKHF_UP | LLKHF_EXTENDED)))
        ret = TRUE;
    else if ((lpKdhs->scanCode == 0x01) && bCtrlDown && !(lpKdhs->flags & (LLKHF_UP | LLKHF_EXTENDED)))
        ret = TRUE;
    else if ((lpKdhs->scanCode == 0x0f) && (lpKdhs->flags & LLKHF_ALTDOWN) && !(lpKdhs->flags & (LLKHF_UP | LLKHF_EXTENDED)))
        ret = TRUE;
    else if ((lpKdhs->scanCode == 0x0f) && bCtrlDown && !(lpKdhs->flags & (LLKHF_UP | LLKHF_EXTENDED)))
        ret = TRUE;
    else if ((lpKdhs->scanCode == 0x39) && (lpKdhs->flags & LLKHF_ALTDOWN) && !(lpKdhs->flags & (LLKHF_UP | LLKHF_EXTENDED)))
        ret = TRUE;
    else if ((lpKdhs->scanCode == 0x3e) && (lpKdhs->flags & LLKHF_ALTDOWN) && !(lpKdhs->flags & (LLKHF_UP | LLKHF_EXTENDED)))
        ret = TRUE;
    else if ((lpKdhs->scanCode >= 0x5b) && (lpKdhs->scanCode <= 0x5d) && (lpKdhs->flags & LLKHF_EXTENDED))
        ret = TRUE;
    else if (inhibit_multimedia_keys &&
            (lpKdhs->vkCode == VK_MEDIA_PLAY_PAUSE ||
            lpKdhs->vkCode == VK_MEDIA_NEXT_TRACK ||
            lpKdhs->vkCode == VK_MEDIA_PREV_TRACK ||
            lpKdhs->vkCode == VK_VOLUME_DOWN ||
            lpKdhs->vkCode == VK_VOLUME_UP ||
            lpKdhs->vkCode == VK_VOLUME_MUTE ||
            lpKdhs->vkCode == VK_MEDIA_STOP ||
            lpKdhs->vkCode == VK_LAUNCH_MEDIA_SELECT ||
            lpKdhs->vkCode == VK_LAUNCH_MAIL ||
            lpKdhs->vkCode == VK_LAUNCH_APP1 ||
            lpKdhs->vkCode == VK_LAUNCH_APP2 ||
            lpKdhs->vkCode == VK_HELP ||
            lpKdhs->vkCode == VK_BROWSER_BACK ||
            lpKdhs->vkCode == VK_BROWSER_FORWARD ||
            lpKdhs->vkCode == VK_BROWSER_FAVORITES ||
            lpKdhs->vkCode == VK_BROWSER_HOME ||
            lpKdhs->vkCode == VK_BROWSER_REFRESH ||
            lpKdhs->vkCode == VK_BROWSER_SEARCH ||
            lpKdhs->vkCode == VK_BROWSER_STOP))
        ret = TRUE;
    else
        ret = CallNextHookEx(NULL, nCode, wParam, lParam);

    if (lpKdhs->scanCode == 0x00000045) {
        if ((lpKdhs->flags & LLKHF_EXTENDED) && ((lpKdhs->vkCode == 0x00000090) ||
                                                 (lpKdhs->vkCode == 0x00000013))) {
            /* NumLock. */
            lpKdhs->flags &= ~LLKHF_EXTENDED;
        } else if (!(lpKdhs->flags & LLKHF_EXTENDED) && (lpKdhs->vkCode == 0x00000013)) {
            /* Pause - send E1 1D. */
            win_keyboard_handle(0xe1, 0, 0, 0);
            win_keyboard_handle(0x1d, lpKdhs->flags & LLKHF_UP, 0, 0);
        }
    } else if (!last && (lpKdhs->scanCode == 0x00000036))
        /* Non-fake right shift. */
        lpKdhs->flags &= ~LLKHF_EXTENDED;

    if (lpKdhs->scanCode == 0x00000236)
        last = 1;
    else if (last && (lpKdhs->scanCode == 0x00000036))
        last = 0;

    if ((lpKdhs->scanCode == 0xf1) || (lpKdhs->scanCode == 0xf2))
        /* Hanja and Han/Eng keys, suppress the extended flag. */
        win_keyboard_handle(lpKdhs->scanCode, lpKdhs->flags & LLKHF_UP, 0, 0);
    else
        win_keyboard_handle(lpKdhs->scanCode, lpKdhs->flags & LLKHF_UP, lpKdhs->flags & LLKHF_EXTENDED, 0);

    return ret;
}
#endif

#ifdef Q_OS_WINDOWS
static HHOOK llhook = NULL;
#endif

void
main_thread_fn()
{
    int frames;

    QThread::currentThread()->setPriority(QThread::HighestPriority);
    plat_set_thread_name(nullptr, "main_thread");
    framecountx = 0;
    // title_update = 1;
    uint64_t old_time = elapsed_timer.elapsed();
    int      drawits = frames = 0;
    is_cpu_thread             = 1;
    while (!is_quit && cpu_thread_run) {
        /* See if it is time to run a frame of code. */
        const uint64_t new_time = elapsed_timer.elapsed();
#ifdef USE_GDBSTUB
        if (gdbstub_next_asap && (drawits <= 0))
            drawits = force_10ms ? 10 : 1;
        else
#endif
            drawits += static_cast<int>(new_time - old_time);
        old_time = new_time;
        if ((drawits > 0 || fast_forward) && !dopause) {
            /* Yes, so run frames now. */
            do {
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
                /* Every 2 emulated seconds we save the machine status. */
                if (++frames >= (force_10ms ? 200 : 2000) && nvr_dosave) {
                    qt_nvr_save();
                    nvr_dosave = 0;
                    frames     = 0;
                }
                
                drawits -= force_10ms ? 10 : 1;
                if (drawits > 50 || fast_forward)
                    drawits = 0;

            } while (drawits > 0);
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

    cpu_thread_running = false;
    is_quit            = 1;
    for (uint8_t i = 1; i < GFXCARD_MAX; i++) {
        if (gfxcard[i]) {
            ui_deinit_monitor(i);
            plat_delay_ms(500);
        }
    }
    QTimer::singleShot(0, QApplication::instance(), []() { QApplication::processEvents(); QApplication::instance()->quit(); });
}

static std::thread *main_thread;

QTimer discordupdate;

#ifdef Q_OS_WINDOWS
WindowsDarkModeFilter *vmm_dark_mode_filter = nullptr;
#endif

int
main(int argc, char *argv[])
{
#ifdef Q_OS_WINDOWS
    bool wasDarkTheme = false;
    /* Check if Windows supports UTF-8 */
    if (GetACP() == CP_UTF8)
        acp_utf8 = 1;
    else
        acp_utf8 = 0;
#endif
#if QT_VERSION < QT_VERSION_CHECK(6, 0, 0)
    QApplication::setAttribute(Qt::AA_DisableHighDpiScaling, false);
    QApplication::setAttribute(Qt::AA_UseHighDpiPixmaps);
    QApplication::setAttribute(Qt::AA_EnableHighDpiScaling);
#endif
#if QT_VERSION >= QT_VERSION_CHECK(5, 14, 0)
    QApplication::setHighDpiScaleFactorRoundingPolicy(Qt::HighDpiScaleFactorRoundingPolicy::PassThrough);
#endif
    QApplication::setAttribute(Qt::AA_UseDesktopOpenGL);

    QSurfaceFormat fmt = QSurfaceFormat::defaultFormat();
    fmt.setSwapInterval(0);
    fmt.setProfile(QSurfaceFormat::OpenGLContextProfile::CoreProfile);
    fmt.setRenderableType(QSurfaceFormat::OpenGL);
#ifdef Q_OS_MACOS
    fmt.setVersion(4, 1);
#else
    fmt.setVersion(3, 2);
#endif
    QSurfaceFormat::setDefaultFormat(fmt);

    QApplication app(argc, argv);
    QLocale::setDefault(QLocale::C);
    setlocale(LC_NUMERIC, "C");

#ifdef Q_OS_WINDOWS
    Q_INIT_RESOURCE(darkstyle);
    if (QFile(QApplication::applicationDirPath() + "/opengl32.dll").exists()) {
        qputenv("QT_OPENGL_DLL", QFileInfo(QApplication::applicationDirPath() + "/opengl32.dll").absoluteFilePath().toUtf8());
    }

    if (!util::isWindowsLightTheme()) {
        QFile f(":qdarkstyle/dark/darkstyle.qss");

        if (!f.exists()) {
            printf("Unable to set stylesheet, file not found\n");
        } else {
            f.open(QFile::ReadOnly | QFile::Text);
            QTextStream ts(&f);
            qApp->setStyleSheet(ts.readAll());
            wasDarkTheme = true;
        }
        QPalette palette(qApp->palette());
        palette.setColor(QPalette::Link, Qt::white);
        palette.setColor(QPalette::LinkVisited, Qt::lightGray);
        qApp->setPalette(palette);
    }
#endif

    Q_INIT_RESOURCE(qt_resources);
    Q_INIT_RESOURCE(qt_translations);

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

#ifdef Q_OS_WINDOWS
    if (util::isWindowsLightTheme() && wasDarkTheme) {
        qApp->setStyleSheet("");
        QPalette palette(qApp->palette());
        palette.setColor(QPalette::Link, Qt::blue);
        palette.setColor(QPalette::LinkVisited, Qt::magenta);
        qApp->setPalette(palette);
    }
#endif

    if (!start_vmm)
#ifdef Q_OS_MACOS
        qt_set_sequence_auto_mnemonic(false);
#else
        qt_set_sequence_auto_mnemonic(!!kbd_req_capture);
#endif
    app.setStyle(new StyleOverride());

    bool startMaximized = window_remember && monitor_settings[0].mon_window_maximized;
    fprintf(stderr, "Qt: version %s, platform \"%s\"\n", qVersion(), QApplication::platformName().toUtf8().data());
    ProgSettings::loadTranslators(&app);
#ifdef Q_OS_WINDOWS
    QApplication::setFont(ProgSettings::getUIFont());
    SetCurrentProcessExplicitAppUserModelID(L"86Box.86Box");
#endif

#ifndef Q_OS_MACOS
    app.setWindowIcon(QIcon(EMU_ICON_PATH));
#    ifdef Q_OS_UNIX
    app.setDesktopFileName("net.86box.86Box");
#    endif
#endif

    if (!pc_init_roms()) {
        QMessageBox fatalbox(QMessageBox::Icon::Critical, QObject::tr("No ROMs found"),
                             QObject::tr("86Box could not find any usable ROM images.\n\nPlease <a href=\"https://github.com/86Box/roms/releases/latest\">download</a> a ROM set and extract it into the \"roms\" directory."),
                             QMessageBox::Ok);
        fatalbox.setTextFormat(Qt::TextFormat::RichText);
        fatalbox.exec();
        return 6;
    }

    if (start_vmm) {
        // VMManagerMain vmm;
        // // Hackish until there is a proper solution
        // QApplication::setApplicationName("86Box VM Manager");
        // QApplication::setApplicationDisplayName("86Box VM Manager");
        // vmm.show();
        // vmm.exec();
#ifdef Q_OS_WINDOWS
        auto darkModeFilter = std::unique_ptr<WindowsDarkModeFilter>(new WindowsDarkModeFilter());
        if (darkModeFilter) {
            qApp->installNativeEventFilter(darkModeFilter.get());
        }
        QTimer::singleShot(0, [&darkModeFilter] {
#else
        QTimer::singleShot(0, [] {
#endif
            const auto vmm_main_window = new VMManagerMainWindow();
#ifdef Q_OS_WINDOWS
            darkModeFilter.get()->setWindow(vmm_main_window);
            // HACK
            vmm_dark_mode_filter = darkModeFilter.get();
#endif
            vmm_main_window->show();
        });
        QApplication::exec();
        return 0;
    }

    pc_init_modules();

    // UUID / copy / move detection
    if (!util::compareUuid()) {
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
        VMManagerClientSocket manager_socket;
        if (qgetenv("VMM_86BOX_SOCKET").size()) {
            manager_socket.IPCConnect(qgetenv("VMM_86BOX_SOCKET"));
            manager_socket.clientRunningStateChanged(VMManagerProtocol::RunningState::PausedWaiting);
        }
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

#ifdef Q_OS_WINDOWS
    // On Win32 the accuracy of Sleep() depends on the timer resolution, which can be set by calling timeBeginPeriod
    // https://learn.microsoft.com/en-us/windows/win32/api/timeapi/nf-timeapi-timebeginperiod
    exit_pause();
    timeBeginPeriod(1);
    atexit([]() -> void { timeEndPeriod(1); });
#endif

    main_window = new MainWindow();
    if (startMaximized) {
        main_window->showMaximized();
    } else {
        main_window->show();
    }
#ifdef WAYLAND
    if (QApplication::platformName().contains("wayland")) {
        /* Force a sync. */
        (void) main_window->winId();
        QApplication::sync();
        extern void wl_keyboard_grab(QWindow * window);
        wl_keyboard_grab(main_window->windowHandle());
    }
#endif

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
        pclog("WARNING: Debugger detected, forcing raw input\n");
        hook_enabled = 0;
    }

    if (hook_enabled) {
        /* Yes, low-level hooks *DO* work with raw input, at least global ones. */
        llhook = SetWindowsHookEx(WH_KEYBOARD_LL, emu_LowLevelKeyboardProc, NULL, 0);
        atexit([]() -> void {
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

    VMManagerClientSocket manager_socket;
    if (qgetenv("VMM_86BOX_SOCKET").size()) {
        manager_socket.IPCConnect(qgetenv("VMM_86BOX_SOCKET"));
        QObject::connect(&manager_socket, &VMManagerClientSocket::pause, main_window, &MainWindow::togglePause);
        QObject::connect(&manager_socket, &VMManagerClientSocket::resetVM, main_window, &MainWindow::hardReset);
        QObject::connect(&manager_socket, &VMManagerClientSocket::showsettings, main_window, &MainWindow::showSettings);
        QObject::connect(&manager_socket, &VMManagerClientSocket::ctrlaltdel, []() { pc_send_cad(); });
        QObject::connect(&manager_socket, &VMManagerClientSocket::request_shutdown, main_window, &MainWindow::close);
        QObject::connect(&manager_socket, &VMManagerClientSocket::force_shutdown, []() {
            do_stop();
            emit main_window->close();
        });
        QObject::connect(main_window, &MainWindow::vmmRunningStateChanged, &manager_socket, &VMManagerClientSocket::clientRunningStateChanged);
        QObject::connect(main_window, &MainWindow::vmmConfigurationChanged, &manager_socket, &VMManagerClientSocket::configurationChanged);
        QObject::connect(main_window, &MainWindow::vmmGlobalConfigurationChanged, &manager_socket, &VMManagerClientSocket::globalConfigurationChanged);
        main_window->installEventFilter(&manager_socket);

        manager_socket.sendWinIdMessage(main_window->winId());
    }

    // pc_reset_hard_init();

    QTimer onesec;
    QObject::connect(&onesec, &QTimer::timeout, &app, [] {
        pc_onesec();
    });
    onesec.setTimerType(Qt::PreciseTimer);
    onesec.start(1000);

#ifdef DISCORD
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
        discordupdate.setInterval(1000);
        if (enable_discord)
            discordupdate.start(1000);
    }
#endif

    /* Initialize the rendering window, or fullscreen. */
    QTimer::singleShot(0, &app, [] {
        plat_set_thread_name(nullptr, "qt_thread");

#ifdef Q_OS_WINDOWS
        extern bool NewDarkMode;
        NewDarkMode = util::isWindowsLightTheme();
#endif
        pc_reset_hard_init();

        /* Set the PAUSE mode depending on the renderer. */
#ifdef USE_VNC
        if (vid_api == RENDERER_VNC)
            plat_pause(1);
        else
#endif
            plat_pause(0);

        cpu_thread_running = true;
        main_thread        = new std::thread(main_thread_fn);
    });

    const auto ret = app.exec();
    cpu_thread_run = 0;
    main_thread->join();
    pc_close(nullptr);
    endblit();

    socket.close();
    return ret;
}
