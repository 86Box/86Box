/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          Program settings UI module.
 *
 *
 *
 * Authors: Joakim L. Gilje <jgilje@jgilje.net>
 *
 *          Copyright 2021 Joakim L. Gilje
 */

#include "qt_renderercommon.hpp"

#include "qt_machinestatus.hpp"
#include "qt_mainwindow.hpp"

#include <QPainter>
#include <QWidget>
#include <QEvent>
#include <QApplication>

#include <cmath>
#include <qevent.h>
#include <qfileinfo.h>

extern "C" {
#include <86box/86box.h>
#include <86box/video.h>


#include <86box/timer.h>
#include <86box/device.h>
#include <86box/cassette.h>
#include <86box/cdrom.h>
#include <86box/machine.h>
#include <86box/plat.h>
#include <86box/fdd.h>
}

RendererCommon::RendererCommon() = default;

extern MainWindow *main_window;

static void
integer_scale(double *d, double *g)
{
    double ratio;

    if (*d > *g) {
        ratio = std::floor(*d / *g);
        *d    = *g * ratio;
    } else {
        ratio = std::ceil(*d / *g);
        *d    = *g / ratio;
    }
}

void
RendererCommon::onResize(int width, int height)
{
    if ((video_fullscreen == 0) && (video_fullscreen_scale_maximized ? ((parentWidget->isMaximized() == false) && (main_window->isAncestorOf(parentWidget) && main_window->isMaximized() == false)) : 1)) {
        destination.setRect(0, 0, width, height);
        return;
    }
    double dx, dy, dw, dh, gsr;

    double hw  = width;
    double hh  = height;
    double gw  = source.width();
    double gh  = source.height();
    double hsr = hw / hh;

    switch (video_fullscreen_scale) {
        case FULLSCR_SCALE_INT:
            gsr = gw / gh;
            if (gsr <= hsr) {
                dw = hh * gsr;
                dh = hh;
            } else {
                dw = hw;
                dh = hw / gsr;
            }
            integer_scale(&dw, &gw);
            integer_scale(&dh, &gh);
            dx = (hw - dw) / 2.0;
            dy = (hh - dh) / 2.0;
            destination.setRect(dx, dy, dw, dh);
            break;
        case FULLSCR_SCALE_43:
        case FULLSCR_SCALE_KEEPRATIO:
            if (video_fullscreen_scale == FULLSCR_SCALE_43) {
                gsr = 4.0 / 3.0;
            } else {
                gsr = gw / gh;
            }

            if (gsr <= hsr) {
                dw = hh * gsr;
                dh = hh;
            } else {
                dw = hw;
                dh = hw / gsr;
            }
            dx = (hw - dw) / 2.0;
            dy = (hh - dh) / 2.0;
            destination.setRect(dx, dy, dw, dh);
            break;
        case FULLSCR_SCALE_FULL:
        default:
            destination.setRect(0, 0, hw, hh);
            break;
    }
}

bool
RendererCommon::eventDelegate(QEvent *event, bool &result)
{
    switch (event->type()) {
        default:
            return false;
        case QEvent::KeyPress:
        case QEvent::KeyRelease:
            result = QApplication::sendEvent(main_window, event);
            return true;
        case QEvent::DragEnter:
            dragEnterEventDelegate((QDragEnterEvent*)event);
            result = event->isAccepted();
            return true;
        case QEvent::DragMove:
            dragMoveEventDelegate((QDragEnterEvent*)event);
            result = event->isAccepted();
            return true;
        case QEvent::Drop:
            dropEventDelegate((QDragEnterEvent*)event);
            result = event->isAccepted();
            return true;
        case QEvent::MouseButtonPress:
        case QEvent::MouseMove:
        case QEvent::MouseButtonRelease:
        case QEvent::Wheel:
        case QEvent::Enter:
        case QEvent::Leave:
            result = QApplication::sendEvent(parentWidget, event);
            return true;
    }
    return false;
}

void RendererCommon::dropEventDelegate(QDropEvent* event)
{
    if (event->dropAction() == Qt::CopyAction) {
        auto localFile = (event->mimeData()->urls()[0].toLocalFile());

        QFileInfo info(localFile);
        if (info.size() == 0) event->ignore();
        else {
            event->accept();
            if ((info.completeSuffix().toLower() == "iso" || info.completeSuffix().toLower() == "cue")) {
                bool done = false;
                MachineStatus::iterateCDROM([&] (int i) {
                    if (!done) {
                        cdrom_mount(i, info.absoluteFilePath().toLocal8Bit().data());
                        done = true;
                    }
                });
                return;
            }
            if (info.completeSuffix().toLower() == "zdi") {
                bool done = false;
                MachineStatus::iterateZIP([&] (int i) {
                    if (!done) {
                        zip_mount(i, info.absoluteFilePath().toLocal8Bit().data(), 0);
                        done = true;
                    }
                });
                return;
            }
            if (info.completeSuffix().toLower() == "mdi") {
                bool done = false;
                MachineStatus::iterateMO([&] (int i) {
                    if (!done) {
                        mo_mount(i, info.absoluteFilePath().toLocal8Bit().data(), 0);
                        done = true;
                    }
                });
                return;
            }
            if ((info.completeSuffix().toLower() == "a"
                 || info.completeSuffix().toLower() == "b"
                 || info.completeSuffix().toLower() == "jrc")
                && machine_has_cartridge(machine)) {
                cartridge_mount(0, info.absoluteFilePath().toLocal8Bit().data(), 0);
                return;
            }
            if ((info.completeSuffix().toLower() == "wav"
                || info.completeSuffix().toLower() == "pcm"
                || info.completeSuffix().toLower() == "raw"
                || info.completeSuffix().toLower() == "cas") && cassette_enable) {
                cassette_mount(info.absoluteFilePath().toLocal8Bit().data(), 0);
                return;
            }
            if (info.completeSuffix().toLower().left(2) == "im") {
                auto size = info.size();

                if (size <= 2949120) {
                    bool done = false;
                    MachineStatus::iterateFDD([&] (int i) {
                        if (!done) {
                            floppy_mount(i, info.absoluteFilePath().toLocal8Bit().data(), 0);
                            done = true;
                        }
                    });
                }
                else if (size <= (1024 * 1024 * 250)) {
                    bool done = false;
                    MachineStatus::iterateZIP([&] (int i) {
                        if (!done) {
                            zip_mount(i, info.absoluteFilePath().toLocal8Bit().data(), 0);
                            done = true;
                        }
                    });
                    if (!done) {
                        MachineStatus::iterateMO([&] (int i) {
                            if (!done) {
                                mo_mount(i, info.absoluteFilePath().toLocal8Bit().data(), 0);
                                done = true;
                            }
                        });
                    }
                }
                else {
                    bool done = false;
                    MachineStatus::iterateMO([&] (int i) {
                        if (!done) {
                            mo_mount(i, info.absoluteFilePath().toLocal8Bit().data(), 0);
                            done = true;
                        }
                    });
                }
                return;
            }

            if (fdd_loadable(info.absoluteFilePath().toLocal8Bit().data())) {
                bool done = false;
                MachineStatus::iterateFDD([&](int i) {
                    if (!done) {
                        floppy_mount(i, info.absoluteFilePath().toLocal8Bit().data(), 0);
                        done = true;
                    }
                });
            }
        }
    } else
        event->ignore();
}
