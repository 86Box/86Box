#include <QApplication>
#include <QDebug>
#include <QElapsedTimer>
#include <QThread>
#include <QTimer>

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
#endif

#include <86box/86box.h>
#include <86box/plat.h>
#include <86box/ui.h>
#include <86box/video.h>

#include <thread>

#include "qt_mainwindow.hpp"
#include "cocoa_mouse.hpp"
#include "qt_styleoverride.hpp"

// Void Cast
#define VC(x) const_cast<wchar_t*>(x)

extern QElapsedTimer elapsed_timer;
extern int nvr_dosave;
extern MainWindow* main_window;

extern "C" {
    extern int qt_nvr_save(void);
}

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
        if (doresize && !video_fullscreen && !is_quit) {
            if (vid_resize & 2)
                plat_resize(fixed_size_x, fixed_size_y);
            else
                plat_resize(scrnsz_x, scrnsz_y);
            doresize = 0;
        }
    }

    is_quit = 1;
}

int main(int argc, char* argv[]) {
    QApplication app(argc, argv);
    app.setStyle(new StyleOverride());
#ifdef __APPLE__
    CocoaEventFilter cocoafilter;
    app.installNativeEventFilter(&cocoafilter);
#endif
    elapsed_timer.start();

    pc_init(argc, argv);
    if (! pc_init_modules()) {
        ui_msgbox_header(MBX_FATAL, VC(L"No ROMs found."), VC(L"86Box could not find any usable ROM images.\n\nPlease <a href='https://github.com/86Box/roms/releases/latest'>download</a> a ROM set and extract it into the \"roms\" directory."));
        return 6;
    }

    main_window = new MainWindow();
    main_window->show();
    main_window->setFocus();
    app.installEventFilter(main_window);

#ifdef Q_OS_WINDOWS
    auto rawInputFilter = WindowsRawInputFilter::Register(main_window);
    if (rawInputFilter)
    {
        app.installNativeEventFilter(rawInputFilter.get());
        QObject::disconnect(main_window, &MainWindow::pollMouse, 0, 0);
        QObject::connect(main_window, &MainWindow::pollMouse, (WindowsRawInputFilter*)rawInputFilter.get(), &WindowsRawInputFilter::mousePoll, Qt::DirectConnection);
        main_window->setSendKeyboardInput(false);
    }
#endif

    auto widgetList = app.allWidgets();
    for (auto curWidget : widgetList)
    {
        curWidget->setFocusPolicy(Qt::NoFocus);
    }
    main_window->setFocusPolicy(Qt::StrongFocus);

    pc_reset_hard_init();

    /* Set the PAUSE mode depending on the renderer. */
    // plat_pause(0);
    QTimer onesec;
    QObject::connect(&onesec, &QTimer::timeout, &app, [] {
        pc_onesec();
    });
    onesec.start(1000);

    /* Initialize the rendering window, or fullscreen. */
    auto main_thread = std::thread([] {
       main_thread_fn();
    });

    auto ret = app.exec();
    cpu_thread_run = 0;
    main_thread.join();

    return ret;
}
