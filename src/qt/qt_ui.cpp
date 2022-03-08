/*
 * 86Box	A hypervisor and IBM PC system emulator that specializes in
 *		running old operating systems and software designed for IBM
 *		PC systems and compatibles from 1981 through fairly recent
 *		system designs based on the PCI bus.
 *
 *		This file is part of the 86Box distribution.
 *
 *		Common UI functions.
 *
 *
 *
 * Authors:	Joakim L. Gilje <jgilje@jgilje.net>
 *          Cacodemon345
 *
 *		Copyright 2021 Joakim L. Gilje
 *      Copyright 2021-2022 Cacodemon345
 */
#include <cstdint>

#include <QDebug>
#include <QThread>
#include <QMessageBox>

#include <QStatusBar>

#include "qt_mainwindow.hpp"

MainWindow* main_window = nullptr;

static QString sb_text, sb_buguitext, sb_mt32lcdtext;

extern "C" {

#include <86box/plat.h>
#include <86box/ui.h>
#include <86box/mouse.h>

void
plat_delay_ms(uint32_t count)
{
    QThread::msleep(count);
}

wchar_t* ui_window_title(wchar_t* str)
{
    if (str == nullptr) {
        static wchar_t title[512];
        memset(title, 0, sizeof(title));
        main_window->getTitle(title);
        str = title;
    } else {
        emit main_window->setTitle(QString::fromWCharArray(str));
    }
    return str;
}

extern "C" void qt_blit(int x, int y, int w, int h)
{
    main_window->blitToWidget(x, y, w, h);
}

void mouse_poll() {
    main_window->pollMouse();
}

void plat_resize(int w, int h) {
    main_window->resizeContents(w, h);
}

void plat_setfullscreen(int on) {
    main_window->setFullscreen(on > 0 ? true : false);
}

void plat_mouse_capture(int on) {
    if (!kbd_req_capture && (mouse_type == MOUSE_TYPE_NONE))
        return;

    main_window->setMouseCapture(on > 0 ? true : false);
}

int	ui_msgbox_header(int flags, void *header, void* message) {
    if (header <= (void*)7168) header = plat_get_string((uintptr_t)header);
    if (message <= (void*)7168) message = plat_get_string((uintptr_t)message);

    auto hdr = (flags & MBX_ANSI) ? QString((char*)header) : QString::fromWCharArray(reinterpret_cast<const wchar_t*>(header));
    auto msg = (flags & MBX_ANSI) ? QString((char*)message) : QString::fromWCharArray(reinterpret_cast<const wchar_t*>(message));

    // any error in early init
    if (main_window == nullptr) {
        QMessageBox msgBox(QMessageBox::Icon::Critical, hdr, msg);
        msgBox.setTextFormat(Qt::TextFormat::RichText);
        msgBox.exec();
    } else {
        // else scope it to main_window
        main_window->showMessage(hdr, msg);
    }
    return 0;
}

int	ui_msgbox(int flags, void *message) {
    return ui_msgbox_header(flags, nullptr, message);
}

void ui_sb_update_text() {
    emit main_window->statusBarMessage( !sb_mt32lcdtext.isEmpty() ? sb_mt32lcdtext : sb_text.isEmpty() ? sb_buguitext : sb_text);
}

void ui_sb_mt32lcd(char* str)
{
    sb_mt32lcdtext = QString(str);
    ui_sb_update_text();
}

void ui_sb_set_text_w(wchar_t *wstr) {
    sb_text = QString::fromWCharArray(wstr);
    ui_sb_update_text();
}

void ui_sb_set_text(char *str) {
    sb_text = str;
    ui_sb_update_text();
}

void
ui_sb_update_tip(int arg) {
    main_window->updateStatusBarTip(arg);
}

void
ui_sb_update_panes() {
    main_window->updateStatusBarPanes();
}

void ui_sb_bugui(char *str) {
    sb_buguitext = str;
    ui_sb_update_text();;
}

void ui_sb_set_ready(int ready) {
    if (ready == 0) {
        ui_sb_bugui(nullptr);
        ui_sb_set_text(nullptr);
    }
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
    if (!update_icons) return;
    main_window->updateStatusBarActivity(tag, active > 0 ? true : false);
}

}
