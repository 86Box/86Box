#include <cstdint>

#include <QDebug>
#include <QThread>
#include <QMessageBox>

#include <QStatusBar>

#include "qt_mainwindow.hpp"

std::atomic_int resize_pending = 0;
std::atomic_int resize_w = 0;
std::atomic_int resize_h = 0;

MainWindow* main_window = nullptr;

extern "C" {

#include <86box/plat.h>

void
plat_delay_ms(uint32_t count)
{
    QThread::msleep(count);
}

wchar_t* ui_window_title(wchar_t* str)
{
    if (str == nullptr) {
        static wchar_t title[512];
        int chars = main_window->windowTitle().toWCharArray(title);
        title[chars] = 0;
        str = title;
    } else {
        main_window->setWindowTitle(QString::fromWCharArray(str));
    }
    return str;
}

void mouse_poll() {
    main_window->pollMouse();
}

void plat_resize(int w, int h) {
    resize_w = w;
    resize_h = h;
    resize_pending = 1;

    main_window->resizeContents(w, h);
}

void plat_setfullscreen(int on) {
    main_window->setFullscreen(on > 0 ? true : false);
}

void plat_mouse_capture(int on) {
    main_window->setMouseCapture(on > 0 ? true : false);
}

int	ui_msgbox_header(int flags, void *header, void* message)
{
    if (header <= (void*)7168) header = plat_get_string(reinterpret_cast<long>(header));
    if (message <= (void*)7168) message = plat_get_string(reinterpret_cast<long>(message));

    auto hdr = QString::fromWCharArray(reinterpret_cast<const wchar_t*>(header));
    auto msg = QString::fromWCharArray(reinterpret_cast<const wchar_t*>(message));

    QMessageBox box(QMessageBox::Warning, hdr, msg);
    box.exec();
    return 0;
}

int	ui_msgbox(int flags, void *message) {
    return ui_msgbox_header(flags, nullptr, message);
}

void ui_sb_set_text_w(wchar_t *wstr) {
    main_window->statusBar()->showMessage(QString::fromWCharArray(wstr));
}

void
ui_sb_update_tip(int arg) {
    qDebug() << Q_FUNC_INFO << arg;
}

void
ui_sb_update_panes() {
    main_window->updateStatusBarPanes();
}

void ui_sb_bugui(char *str) {
    main_window->statusBar()->showMessage(str);
}

void ui_sb_set_ready(int ready) {
    qDebug() << Q_FUNC_INFO << ready;
}

void
ui_sb_update_icon_state(int tag, int state) {
    if (main_window == nullptr) {
        return;
    }
    main_window->updateStatusBarEmpty(tag, state > 0 ? true : false);
}

void
ui_sb_update_icon(int tag, int active) {
    main_window->updateStatusBarActivity(tag, active > 0 ? true : false);
}

}
