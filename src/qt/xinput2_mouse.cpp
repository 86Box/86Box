/*
 * 86Box	A hypervisor and IBM PC system emulator that specializes in
 *		running old operating systems and software designed for IBM
 *		PC systems and compatibles from 1981 through fairly recent
 *		system designs based on the PCI bus.
 *
 *		This file is part of the 86Box distribution.
 *
 *		X11 Xinput2 mouse input module.
 *
 *
 *
 * Authors:	Cacodemon345
 *
 *		Copyright 2022 Cacodemon345
 */

/* Valuator parsing and duplicate event checking code from SDL2. */
#include <QDebug>
#include <QThread>
#include <QProcess>
#include <QApplication>
#include <QAbstractNativeEventFilter>

#include "qt_mainwindow.hpp"
extern MainWindow* main_window;

#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>

#include <atomic>

extern "C"
{
#include <X11/Xlib.h>
#include <X11/keysym.h>
#include <X11/extensions/XInput2.h>
#include <unistd.h>
#include <fcntl.h>

#include <86box/86box.h>
#include <86box/mouse.h>
#include <86box/plat.h>
}

int xi2flides[2] = { 0, 0 };

static Display* disp = nullptr;
static QThread* procThread = nullptr;
static XIEventMask ximask;
static std::atomic<bool> exitfromthread = false;
static std::atomic<double> xi2_mouse_x = 0, xi2_mouse_y = 0, xi2_mouse_abs_x = 0, xi2_mouse_abs_y = 0;
static int xi2opcode = 0;
static double prev_rel_coords[2] = { 0., 0. };
static Time prev_time = 0;

// From SDL2.
static void parse_valuators(const double *input_values, const unsigned char *mask,int mask_len,
                            double *output_values,int output_values_len) {
    int i = 0,z = 0;
    int top = mask_len * 8;
    if (top > 16)
        top = 16;

    memset(output_values,0,output_values_len * sizeof(double));
    for (; i < top && z < output_values_len; i++) {
        if (XIMaskIsSet(mask, i)) {
            const int value = (int) *input_values;
            output_values[z] = value;
            input_values++;
        }
        z++;
    }
}

static bool exitthread = false;

void xinput2_proc()
{
    Window win;
    win = DefaultRootWindow(disp);

    // XIAllMasterDevices doesn't work for click-and-drag operations.
    ximask.deviceid = XIAllDevices;
    ximask.mask_len = XIMaskLen(XI_LASTEVENT);
    ximask.mask = (unsigned char*)calloc(ximask.mask_len, sizeof(unsigned char));

    XISetMask(ximask.mask, XI_RawKeyPress);
    XISetMask(ximask.mask, XI_RawKeyRelease);
    XISetMask(ximask.mask, XI_RawButtonPress);
    XISetMask(ximask.mask, XI_RawButtonRelease);
    XISetMask(ximask.mask, XI_RawMotion);
    if (XKeysymToKeycode(disp, XK_Home) == 69) XISetMask(ximask.mask, XI_Motion);

    XISelectEvents(disp, win, &ximask, 1);

    XSync(disp, False);
    while(true)
    {
        XEvent ev;
        XGenericEventCookie *cookie = (XGenericEventCookie*)&ev.xcookie;
        XNextEvent(disp, (XEvent*)&ev);

        if (XGetEventData(disp, cookie) && cookie->type == GenericEvent && cookie->extension == xi2opcode) {
            switch (cookie->evtype) {
                case XI_RawMotion: {
                    const XIRawEvent *rawev = (const XIRawEvent*)cookie->data;
                    double relative_coords[2] = { 0., 0. };
                    parse_valuators(rawev->raw_values,rawev->valuators.mask,
                    rawev->valuators.mask_len,relative_coords,2);

                    if ((rawev->time == prev_time) && (relative_coords[0] == prev_rel_coords[0]) && (relative_coords[1] == prev_rel_coords[1])) {
                        break; // Ignore duplicated events.
                    }
                    xi2_mouse_x = xi2_mouse_x + relative_coords[0];
                    xi2_mouse_y = xi2_mouse_y + relative_coords[1];
                    prev_rel_coords[0] = relative_coords[0];
                    prev_rel_coords[1] = relative_coords[1];
                    prev_time = rawev->time;
                    if (!mouse_capture) break;
                    XWindowAttributes winattrib{};
                    if (XGetWindowAttributes(disp, main_window->winId(), &winattrib)) {
                         auto globalPoint = main_window->mapToGlobal(QPoint(main_window->width() / 2, main_window->height() / 2));
                         XWarpPointer(disp, XRootWindow(disp, XScreenNumberOfScreen(winattrib.screen)), XRootWindow(disp, XScreenNumberOfScreen(winattrib.screen)), 0, 0, 0, 0, globalPoint.x(), globalPoint.y());
                         XFlush(disp);
                    }

                }
                case XI_Motion: {
                    if (XKeysymToKeycode(disp, XK_Home) == 69) {
                        // No chance we will get raw motion events on VNC.
                        const XIDeviceEvent *motionev = (const XIDeviceEvent*)cookie->data;
                        if (xi2_mouse_abs_x != 0 || xi2_mouse_abs_y != 0) {
                            xi2_mouse_x = xi2_mouse_x + (motionev->event_x - xi2_mouse_abs_x);
                            xi2_mouse_y = xi2_mouse_y + (motionev->event_y - xi2_mouse_abs_y);
                        }
                        xi2_mouse_abs_x = motionev->event_x;
                        xi2_mouse_abs_y = motionev->event_y;
                    }
                }
            }
        }

        XFreeEventData(disp, cookie);
        if (exitthread) break;
    }
    XCloseDisplay(disp);
}

void xinput2_exit()
{
    if (!exitthread)
    {
        exitthread = true;
        procThread->wait(5000);
        procThread->terminate();
    }
}

void xinput2_init()
{
    disp = XOpenDisplay(nullptr);
    if (!disp)
    {
        qWarning() << "Cannot open current X11 display";
        return;
    }
    auto event = 0, err = 0, minor = 0, major = 2;
    if (XQueryExtension(disp, "XInputExtension", &xi2opcode, &event, &err))
    {
        if (XIQueryVersion(disp, &major, &minor) == Success)
        {
            procThread = QThread::create(xinput2_proc);
            procThread->start();
            atexit(xinput2_exit);
        }
    }
}

void xinput2_poll()
{
    if (procThread && mouse_capture)
    {
        mouse_x = xi2_mouse_x;
        mouse_y = xi2_mouse_y;
    }
    xi2_mouse_x = 0;
    xi2_mouse_y = 0;
}
