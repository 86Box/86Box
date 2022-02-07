#include <QApplication>
#include <QSurfaceFormat>
#include <QDebug>
#include <QElapsedTimer>
#include <QThread>
#include <QTimer>
#include <QTranslator>
#include <QDirIterator>
#include <QLibraryInfo>

#ifdef QT_STATIC
/* Static builds need plugin imports */
#include <QtPlugin>
Q_IMPORT_PLUGIN(QICOPlugin)
#ifdef Q_OS_WINDOWS
Q_IMPORT_PLUGIN(QWindowsIntegrationPlugin)
Q_IMPORT_PLUGIN(QWindowsVistaStylePlugin)
#endif
#endif

#ifdef Q_OS_WINDOWS
#include "qt_winrawinputfilter.hpp"
#include "qt_winmanagerfilter.hpp"
#include <86box/win.h>
#endif

extern "C"
{
#include <86box/86box.h>
#include <86box/config.h>
#include <86box/plat.h>
#include <86box/ui.h>
#include <86box/video.h>
#include <86box/discord.h>
}

#include <thread>
#include <iostream>
#include <memory>

#include "qt_mainwindow.hpp"
#include "qt_progsettings.hpp"
#include "qt_settings.hpp"
#include "cocoa_mouse.hpp"
#include "qt_styleoverride.hpp"

// Void Cast
#define VC(x) const_cast<wchar_t*>(x)

extern QElapsedTimer elapsed_timer;
extern MainWindow* main_window;

extern "C" {
#include <86box/timer.h>
#include <86box/nvr.h>
    extern int qt_nvr_save(void);
}

void qt_set_sequence_auto_mnemonic(bool b);

void
main_thread_fn()
{
    uint64_t old_time, new_time;
    int drawits, frames;

    QThread::currentThread()->setPriority(QThread::HighestPriority);
    framecountx = 0;
    //title_update = 1;
    old_time = elapsed_timer.elapsed();
    drawits = frames = 0;
    while (!is_quit && cpu_thread_run) {
        /* See if it is time to run a frame of code. */
        new_time = elapsed_timer.elapsed();
        drawits += (new_time - old_time);
        old_time = new_time;
        if (drawits > 0 && !dopause) {
            /* Yes, so do one frame now. */
            drawits -= 10;
            if (drawits > 50)
                drawits = 0;

            /* Run a block of code. */
            pc_run();

            /* Every 200 frames we save the machine status. */
            if (++frames >= 200 && nvr_dosave) {
                qt_nvr_save();
                nvr_dosave = 0;
                frames = 0;
            }
        } else {
            /* Just so we dont overload the host OS. */
            if (drawits < -1)
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            else
                std::this_thread::yield();
        }

        /* If needed, handle a screen resize. */
        if (!atomic_flag_test_and_set(&doresize) && !video_fullscreen && !is_quit) {
            if (vid_resize & 2)
                plat_resize(fixed_size_x, fixed_size_y);
            else
                plat_resize(scrnsz_x, scrnsz_y);
        }
    }

    is_quit = 1;
}

int main(int argc, char* argv[]) {
    QApplication app(argc, argv);    
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

    if (!pc_init(argc, argv))
    {
        return 0;
    }
    ProgSettings::loadTranslators(&app);
    if (! pc_init_modules()) {
        ui_msgbox_header(MBX_FATAL, (void*)IDS_2120, (void*)IDS_2056);
        return 6;
    }

    if (settings_only)
    {
        Settings settings;
        if (settings.exec() == QDialog::Accepted)
        {
            settings.save();
            config_save();
        }
        return 0;
    }

    discord_load();

    main_window = new MainWindow();
    main_window->show();
    app.installEventFilter(main_window);

#ifdef Q_OS_WINDOWS
    /* Setup VM-manager messages */
    std::unique_ptr<WindowsManagerFilter> wmfilter;
    if (source_hwnd)
    {
        HWND main_hwnd = (HWND)main_window->winId();

        wmfilter.reset(new WindowsManagerFilter());
        QObject::connect(wmfilter.get(), &WindowsManagerFilter::showsettings, main_window, &MainWindow::showSettings);
        QObject::connect(wmfilter.get(), &WindowsManagerFilter::pause, main_window, &MainWindow::togglePause);
        QObject::connect(wmfilter.get(), &WindowsManagerFilter::reset, main_window, &MainWindow::hardReset);
        QObject::connect(wmfilter.get(), &WindowsManagerFilter::shutdown, [](){ plat_power_off(); });
        QObject::connect(wmfilter.get(), &WindowsManagerFilter::ctrlaltdel, [](){ pc_send_cad(); });
        QObject::connect(wmfilter.get(), &WindowsManagerFilter::dialogstatus, [main_hwnd](bool open){
            PostMessage((HWND)(uintptr_t)source_hwnd, WM_SENDDLGSTATUS, (WPARAM)(open ? 1 : 0), (LPARAM)main_hwnd);
        });

        /* Native filter to catch VM-managers commands */
        app.installNativeEventFilter(wmfilter.get());

        /* Filter to catch main window being blocked (by modal dialog) */
        main_window->installEventFilter(wmfilter.get());

        /* Send main window HWND to manager */
        PostMessage((HWND)(uintptr_t)source_hwnd, WM_SENDHWND, (WPARAM)unique_id, (LPARAM)main_hwnd);

        /* Send shutdown message to manager */
        QObject::connect(&app, &QApplication::destroyed, [main_hwnd](QObject*) {
            PostMessage((HWND)(uintptr_t)source_hwnd, WM_HAS_SHUTDOWN, (WPARAM)0, (LPARAM)main_hwnd);
        });
    }

    /* Setup raw input */
    auto rawInputFilter = WindowsRawInputFilter::Register(main_window);
    if (rawInputFilter)
    {
        app.installNativeEventFilter(rawInputFilter.get());
        QObject::disconnect(main_window, &MainWindow::pollMouse, 0, 0);
        QObject::connect(main_window, &MainWindow::pollMouse, (WindowsRawInputFilter*)rawInputFilter.get(), &WindowsRawInputFilter::mousePoll, Qt::DirectConnection);
        main_window->setSendKeyboardInput(false);
    }    
#endif

    pc_reset_hard_init();

    /* Set the PAUSE mode depending on the renderer. */
    // plat_pause(0);
    QTimer onesec;
    QTimer discordupdate;
    QObject::connect(&onesec, &QTimer::timeout, &app, [] {
        pc_onesec();
    });
    onesec.setTimerType(Qt::PreciseTimer);
    onesec.start(1000);
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
        discordupdate.start(0);
    }

    /* Initialize the rendering window, or fullscreen. */
    auto main_thread = std::thread([] {
       main_thread_fn();
    });

    auto ret = app.exec();
    cpu_thread_run = 0;
    main_thread.join();

    return ret;
}
