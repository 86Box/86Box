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
#endif

#include <86box/86box.h>
#include <86box/plat.h>
#include <86box/ui.h>
#include <86box/video.h>

#include <thread>
#include <iostream>

#include "qt_mainwindow.hpp"
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

class CustomTranslator : public QTranslator
{
protected:
    QString translate(const char *context, const char *sourceText,
                                  const char *disambiguation = nullptr, int n = -1) const override
    {
        if (strcmp(sourceText, "&Fullscreen") == 0) sourceText = "&Fullscreen\tCtrl+Alt+PageUP";
        if (strcmp(sourceText, "&Ctrl+Alt+Del") == 0) sourceText = "&Ctrl+Alt+Del\tCtrl+F12";
        if (strcmp(sourceText, "Take s&creenshot") == 0) sourceText = "Take s&creenshot\tCtrl+F11";
        if (strcmp(sourceText, "&Qt (Software)") == 0)
        {
            QString finalstr = QTranslator::translate("", "&SDL (Software)", disambiguation, n);
            finalstr.replace("SDL", "Qt");
            finalstr.replace("(&S)", "(&Q)");
            return finalstr;
        }
        QString finalstr = QTranslator::translate("", sourceText, disambiguation, n);
#ifdef Q_OS_MACOS
        if (finalstr.contains('\t')) finalstr.truncate(finalstr.indexOf('\t'));
#endif
        return finalstr;
    }
};

int main(int argc, char* argv[]) {
    QApplication app(argc, argv);
    Q_INIT_RESOURCE(qt_resources);
    Q_INIT_RESOURCE(qt_translations);
    QSurfaceFormat fmt = QSurfaceFormat::defaultFormat();
    fmt.setSwapInterval(0);
    QSurfaceFormat::setDefaultFormat(fmt);
    app.setStyle(new StyleOverride());
    QDirIterator it(":", QDirIterator::Subdirectories);
    while (it.hasNext()) {
        qDebug() << it.next() << "\n";
    }
    QTranslator qtTranslator;
    qtTranslator.load(QLocale::system(), QStringLiteral("qt_"), QString(), QLibraryInfo::location(QLibraryInfo::TranslationsPath));
    if (app.installTranslator(&qtTranslator))
    {
        qDebug() << "Qt translations loaded." << "\n";
    }
    CustomTranslator translator;
    qDebug() << QLocale::system().name() << "\n";
    auto localetofilename = QLocale::system().name().replace('_', '-');
    if (translator.load(QLatin1String("86box_"), QLatin1String(":/"), QString(), localetofilename + ".qm"))
    {
        qDebug() << "Translations loaded.\n";
        QCoreApplication::installTranslator(&translator);
    }
#ifdef __APPLE__
    CocoaEventFilter cocoafilter;
    app.installNativeEventFilter(&cocoafilter);
#endif
    elapsed_timer.start();

    if (!pc_init(argc, argv))
    {
        return 0;
    }
    if (! pc_init_modules()) {
        ui_msgbox_header(MBX_FATAL, (void*)IDS_2120, (void*)IDS_2056);
        return 6;
    }

    main_window = new MainWindow();
    main_window->show();
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

    pc_reset_hard_init();

    /* Set the PAUSE mode depending on the renderer. */
    // plat_pause(0);
    if (settings_only) dopause = 1;
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
