/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          Common UI functions.
 *
 * Authors: Joakim L. Gilje <jgilje@jgilje.net>
 *          Cacodemon345
 *          skiretic
 *
 *          Copyright 2021 Joakim L. Gilje
 *          Copyright 2021-2022 Cacodemon345
 *          Copyright 2026 skiretic
 */
#include <cstdint>

#include <atomic>

#include <QDebug>
#include <QThread>
#include <QMessageBox>
#include <QProgressDialog>
#include <QProgressBar>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QTimer>

#include <QStatusBar>
#include <QApplication>
#include <QStringBuilder>

#include "qt_mainwindow.hpp"
#include "qt_machinestatus.hpp"

MainWindow *main_window = nullptr;

static QString sb_text;
static QString sb_buguitext;
static QString sb_mt32lcdtext;

extern "C" {

#include "86box/86box.h"
#include <86box/plat.h>
#include <86box/ui.h>
#include <86box/mouse.h>
#include <86box/timer.h>
#include <86box/86box.h>
#include <86box/device.h>
#include <86box/fdd.h>
#include <86box/hdc.h>
#include <86box/scsi.h>
#include <86box/scsi_device.h>
#include <86box/cartridge.h>
#include <86box/cassette.h>
#include <86box/cdrom.h>
#include <86box/rdisk.h>
#include <86box/mo.h>
#include <86box/scsi_tape.h>
#include <86box/hdd.h>
#include <86box/thread.h>
#include <86box/network.h>
#include <86box/machine_status.h>

#ifdef Q_OS_WINDOWS
#    include <86box/win.h>
#endif

void
plat_delay_ms(uint32_t count)
{
#ifdef Q_OS_WINDOWS
    Sleep(count);
#else
    QThread::msleep(count);
#endif
}

void
ui_emu_status(int speed_percent)
{
    extern int osd_percentage;
    osd_percentage = speed_percent;
    QString str = QString::number(speed_percent);
    if ((mouse_type == MOUSE_TYPE_NONE) || (mouse_input_mode >= 1))
        str += QStringLiteral("%");
    else if (mouse_capture == 1)
        str += QStringLiteral("% - ") % main_window->mouseStringCaptured;
    else
        str += QStringLiteral("% - ") % main_window->mouseStringUncaptured;

    emit main_window->setTitle(str);
}

void
ui_hard_reset_completed()
{
    emit main_window->hardResetCompleted();
}

extern "C" void
qt_blit(int x, int y, int w, int h, int monitor_index)
{
    main_window->blitToWidget(x, y, w, h, monitor_index);
}

extern "C" int vid_resize;
void
plat_resize_request(int w, int h, int monitor_index)
{
    if (video_fullscreen || is_quit)
        return;
    if (vid_resize & 2) {
        plat_resize(fixed_size_x, fixed_size_y, monitor_index);
    } else {
        plat_resize(w, h, monitor_index);
    }
}

void
plat_resize(int w, int h, int monitor_index)
{
    if (monitor_index >= 1)
        main_window->resizeContentsMonitor(w, h, monitor_index);
    else
        main_window->resizeContents(w, h);
}

#if defined _WIN32
extern HWND rw_hwnd;
#endif

void
plat_mouse_capture(int on)
{
    if (!kbd_req_capture && (mouse_type == MOUSE_TYPE_NONE) && !machine_has_mouse())
        return;

    main_window->setMouseCapture(on > 0 ? true : false);

#if defined _WIN32
    if (on) {
        QCursor cursor(Qt::BlankCursor);

        QApplication::setOverrideCursor(cursor);
        QApplication::changeOverrideCursor(cursor);

        RECT rect;

        GetWindowRect(rw_hwnd, &rect);

        ClipCursor(&rect);

    } else {
        ClipCursor(NULL);

        QApplication::restoreOverrideCursor();
    }
#endif
}

int
ui_msgbox_header(int flags, char *header, char *message)
{
    const auto hdr = QString::fromUtf8(header);
    const auto msg = QString::fromUtf8(message);

    if (flags & MBX_QUESTION_YN) {
        QMessageBox box((flags & MBX_WARNING) ? QMessageBox::Icon::Warning : QMessageBox::Icon::Question,
                        hdr.isEmpty() ? QString(EMU_NAME) : hdr, msg, QMessageBox::Yes | QMessageBox::No, main_window);
        box.setDefaultButton(QMessageBox::No);
        return (box.exec() == QMessageBox::Yes) ? 1 : 0;
    }

    // any error in early init
    if (main_window == nullptr) {
        auto defaultheader = QString();
        if (hdr.isEmpty()) {
            if (flags & MBX_FATAL)
                defaultheader = QObject::tr("Fatal error");
            else if (flags & MBX_ERROR)
                defaultheader = QObject::tr("Error");
            else
                defaultheader = EMU_NAME;
        }

        auto msgicon = QMessageBox::Icon::Information;
        if (flags & (MBX_ERROR | MBX_FATAL))
            msgicon = QMessageBox::Icon::Critical;
        else if (flags & MBX_WARNING)
            msgicon = QMessageBox::Icon::Warning;
//        else if (flags & MBX_QUESTION)
//            msgicon = QMessageBox::Icon::Question;
        QMessageBox msgBox(msgicon, (defaultheader.isEmpty() ? hdr : defaultheader), msg);
        msgBox.exec();
    } else {
        // else scope it to main_window
        main_window->showMessage(flags, hdr, msg, false);
    }
    return 0;
}

int
ui_confirm_unsupported_hardware(const ui_unsupported_hardware_t *items, int count, int machine_missing)
{
    if (count <= 0)
        return 1;

    QString message = QString::fromUtf8(plat_get_string(STRING_UNSUPPORTED_TEXT)) + QStringLiteral("\n\n");
    /* Show six names in full; for larger lists show five and the remainder. */
    const int listed = count <= 6 ? count : 5;
    for (int i = 0; i < listed; i++)
        message += QString::asprintf(plat_get_string(items[i].kind), items[i].name) + QLatin1Char('\n');
    if (count > listed)
        message += QString::asprintf(plat_get_string(STRING_UNSUPPORTED_OTHERS), count - listed) + QLatin1Char('\n');

    const int explanation = !machine_missing ? STRING_UNSUPPORTED_REMOVE :
                            count > 1 ? STRING_UNSUPPORTED_REPLACE_REMOVE : STRING_UNSUPPORTED_REPLACE;
    message += QLatin1Char('\n') + QString::fromUtf8(plat_get_string(explanation)) +
               QStringLiteral("\n\n") + QString::fromUtf8(plat_get_string(STRING_UNSUPPORTED_CONTINUE));

    QByteArray title = QByteArray(plat_get_string(STRING_UNSUPPORTED_TITLE));
    QByteArray body  = message.toUtf8();
    return ui_msgbox_header(MBX_WARNING | MBX_QUESTION_YN, title.data(), body.data()) == 1;
}

void
ui_init_monitor(int monitor_index)
{
    if (QThread::currentThread() == main_window->thread()) {
        emit main_window->initRendererMonitor(monitor_index);
    } else
        emit main_window->initRendererMonitorForNonQtThread(monitor_index);
}

void
ui_deinit_monitor(int monitor_index)
{
    if (QThread::currentThread() == main_window->thread()) {
        emit main_window->destroyRendererMonitor(monitor_index);
    } else
        emit main_window->destroyRendererMonitorForNonQtThread(monitor_index);
}

int
ui_msgbox(int flags, char *message)
{
    return ui_msgbox_header(flags, nullptr, message);
}

/* Progress state shared between a waiting non-UI thread and the UI-thread
   dialog closure. The waiter owns poll/arg and never touches this struct
   after storing done; the UI timer frees it (and the dialog) once done is
   seen, so poll is never called on a frame the waiter has already left. */
struct ui_progress_relay {
    std::atomic<int>  value { 0 };
    std::atomic<bool> done { false };
};

/* Qt 5.15's native macOS style stopped painting QProgressBar entirely on
   current macOS betas (label shows, bar area stays empty, every driving
   style). A stylesheet forces QStyleSheetStyle to draw the bar instead;
   the rules imitate the native thin rounded bar with the accent color. */
static void
ui_progress_style_bar(QProgressDialog *dlg)
{
#ifdef Q_OS_MACOS
    auto *bar = new QProgressBar(dlg);

    bar->setTextVisible(false);
    bar->setStyleSheet("QProgressBar {"
                       "  border: none;"
                       "  background: rgba(127, 127, 127, 64);"
                       "  border-radius: 3px;"
                       "  min-height: 6px;"
                       "  max-height: 6px;"
                       "} QProgressBar::chunk {"
                       "  background: palette(highlight);"
                       "  border-radius: 3px;"
                       "}");
    dlg->setBar(bar);
#else
    (void) dlg;
#endif
}

void
ui_progress_wait(const char *message, int total, int (*poll)(void *arg), void *arg)
{
    /* Off the UI thread the event loop is alive; this thread polls and
       publishes, a queued closure on the UI thread paints the dialog. */
    if (main_window == nullptr || QThread::currentThread() != main_window->thread()) {
        ui_progress_relay *relay = main_window ? new ui_progress_relay : nullptr;

        if (relay) {
            const QString msg = QString::fromUtf8(message);

            QMetaObject::invokeMethod(
                main_window,
                [relay, msg, total]() {
                    auto *dlg = new QProgressDialog(msg, QString(), 0, total, main_window);
                    ui_progress_style_bar(dlg);
                    dlg->setWindowModality(Qt::NonModal);
                    dlg->setWindowFlag(Qt::WindowCloseButtonHint, false);
                    dlg->setAutoClose(false);
                    dlg->setAutoReset(false);

                    auto *tick  = new QTimer(dlg);
                    auto *grace = new QElapsedTimer;

                    grace->start();
                    QObject::connect(tick, &QTimer::timeout, dlg, [relay, dlg, grace]() {
                        if (relay->done.load()) {
                            dlg->deleteLater();
                            delete grace;
                            delete relay;
                            return;
                        }
                        /* grace period so a warm (cached) pass never
                           flashes a dialog */
                        if (!dlg->isVisible() && grace->elapsed() > 250)
                            dlg->show();
                        if (dlg->isVisible())
                            dlg->setValue(relay->value.load());
                    });
                    tick->start(50);
                },
                Qt::QueuedConnection);
        }
        while (true) {
            int v = poll(arg);

            if (v >= total)
                break;
            if (relay)
                relay->value.store(v);
            QThread::msleep(50);
        }
        if (relay)
            relay->done.store(true);
        return;
    }

    /* Kept non-modal: a modal QProgressDialog dispatches user input from
       inside setValue(), which would re-enter half-done device init.
       The manual pump below excludes user input entirely, so the dialog
       repaints and animates but clicks and keys stay queued. */
    QProgressDialog dlg(QString::fromUtf8(message), QString(), 0, total, main_window);
    ui_progress_style_bar(&dlg);
    dlg.setWindowModality(Qt::NonModal);
    dlg.setWindowFlag(Qt::WindowCloseButtonHint, false);
    dlg.setAutoClose(false);
    dlg.setAutoReset(false);

    QElapsedTimer grace;
    bool          shown = false;
    int           v;

    grace.start();
    while ((v = poll(arg)) < total) {
        /* grace period so a warm (cached) pass never flashes a dialog */
        if (!shown && grace.elapsed() > 250) {
            dlg.show();
            shown = true;
        }
        if (shown)
            dlg.setValue(v);
        QCoreApplication::processEvents(QEventLoop::ExcludeUserInputEvents);
        QThread::msleep(16);
    }
}

/* The MT-32 LCD is an emulated display, so it takes the label alone. The
   diagnostic-card slot and the status slot share it instead: a card the user
   explicitly enabled must stay readable while a device posts progress, and
   the card goes first so its field does not shift as the status text comes
   and goes. The join is a plain run of spaces: a glyph would read as part of
   one field or the other, and the label is plain text so the run survives. */
void
ui_sb_update_text()
{
    QString msg;

    if (!sb_mt32lcdtext.isEmpty())
        msg = sb_mt32lcdtext;
    else if (sb_buguitext.isEmpty())
        msg = sb_text;
    else if (sb_text.isEmpty())
        msg = sb_buguitext;
    else
        msg = sb_buguitext + QStringLiteral("    ") + sb_text;

    emit main_window->statusBarMessage(msg);
}

void
ui_sb_mt32lcd(char *str)
{
    sb_mt32lcdtext = QString(str);
    ui_sb_update_text();
}

void
ui_sb_set_text(char *str)
{
    sb_text = str;
    ui_sb_update_text();
}

void
ui_sb_update_tip(int arg)
{
    main_window->updateStatusBarTip(arg);
}

void
ui_sb_update_panes()
{
    main_window->updateStatusBarPanes();
}

void
ui_sb_bugui(char *str)
{
    sb_buguitext = str;
    ui_sb_update_text();
}

void
ui_sb_set_ready(int ready)
{
    if (ready == 0) {
        ui_sb_bugui(nullptr);
        ui_sb_set_text(nullptr);
    }
}

void
ui_sb_update_icon_wp(int tag, int state)
{
    const auto temp     = static_cast<unsigned int>(tag);
    const int  category = static_cast<int>(temp & 0xfffffff0);
    const int  item     = tag & 0xf;

    switch (category) {
        default:
            break;
        case SB_CASSETTE:
            machine_status.cassette.write_prot = state > 0 ? true : false;
            break;
        case SB_FLOPPY:
            machine_status.fdd[item].write_prot = state > 0 ? true : false;
            break;
        case SB_RDISK:
            machine_status.rdisk[item].write_prot = state > 0 ? true : false;
            break;
        case SB_MO:
            machine_status.mo[item].write_prot = state > 0 ? true : false;
            break;
        case SB_TAPE:
            machine_status.tape[item].write_prot = state > 0 ? true : false;
            break;
    }

    if (main_window != nullptr)
        main_window->updateStatusEmptyIcons();
}

void
ui_sb_update_icon_state(int tag, int state)
{
    const auto temp     = static_cast<unsigned int>(tag);
    const int  category = static_cast<int>(temp & 0xfffffff0);
    const int  item     = tag & 0xf;

    switch (category) {
        default:
            break;
        case SB_CASSETTE:
            machine_status.cassette.empty = state > 0 ? true : false;
            break;
        case SB_CARTRIDGE:
            machine_status.cartridge[item].empty = state > 0 ? true : false;
            break;
        case SB_FLOPPY:
            machine_status.fdd[item].empty = state > 0 ? true : false;
            break;
        case SB_CDROM:
            machine_status.cdrom[item].empty = state > 0 ? true : false;
            break;
        case SB_RDISK:
            machine_status.rdisk[item].empty = state > 0 ? true : false;
            break;
        case SB_MO:
            machine_status.mo[item].empty = state > 0 ? true : false;
            break;
        case SB_TAPE:
            machine_status.tape[item].empty = state > 0 ? true : false;
            break;
        case SB_HDD:
            break;
        case SB_NETWORK:
            machine_status.net[item].empty = state > 0 ? true : false;
            break;
        case SB_SOUND:
        case SB_TEXT:
            break;
    }

    if (main_window != nullptr)
        main_window->updateStatusEmptyIcons();
}

void
ui_sb_update_icon(int tag, int active)
{
    const auto temp     = static_cast<unsigned int>(tag);
    const int  category = static_cast<int>(temp & 0xfffffff0);
    const int  item     = tag & 0xf;

    switch (category) {
        default:
        case SB_CASSETTE:
        case SB_CARTRIDGE:
            break;
        case SB_FLOPPY:
            machine_status.fdd[item].active = active > 0 ? true : false;
            break;
        case SB_CDROM:
            machine_status.cdrom[item].active = active > 0 ? true : false;
            break;
        case SB_RDISK:
            machine_status.rdisk[item].active = active > 0 ? true : false;
            break;
        case SB_MO:
            machine_status.mo[item].active = active > 0 ? true : false;
            break;
        case SB_TAPE:
            machine_status.tape[item].active = active > 0 ? true : false;
            break;
        case SB_HDD:
            machine_status.hdd[item].active = active > 0 ? true : false;
            break;
        case SB_NETWORK:
            machine_status.net[item].active = active > 0 ? true : false;
            break;
        case SB_SOUND:
        case SB_TEXT:
            break;
    }
}

void
ui_sb_update_icon_write(int tag, int write)
{
    const auto temp     = static_cast<unsigned int>(tag);
    const int  category = static_cast<int>(temp & 0xfffffff0);
    const int  item     = tag & 0xf;

    switch (category) {
        default:
        case SB_CASSETTE:
        case SB_CARTRIDGE:
            break;
        case SB_FLOPPY:
            machine_status.fdd[item].write_active = write > 0 ? true : false;
            break;
        case SB_CDROM:
            machine_status.cdrom[item].write_active = write > 0 ? true : false;
            break;
        case SB_RDISK:
            machine_status.rdisk[item].write_active = write > 0 ? true : false;
            break;
        case SB_MO:
            machine_status.mo[item].write_active = write > 0 ? true : false;
            break;
        case SB_TAPE:
            machine_status.tape[item].write_active = write > 0 ? true : false;
            break;
        case SB_HDD:
            machine_status.hdd[item].write_active = write > 0 ? true : false;
            break;
        case SB_NETWORK:
            machine_status.net[item].write_active = write > 0 ? true : false;
            break;
        case SB_SOUND:
        case SB_TEXT:
            break;
    }
}
}
