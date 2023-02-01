/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          X11 Xinput2 mouse input module.
 *
 *
 *
 * Authors: Cacodemon345
 *          RichardG <richardg867@gmail.com>
 *
 *          Copyright 2022 Cacodemon345.
 *          Copyright 2023 RichardG.
 */

#include <QDebug>
#include <QThread>
#include <QProcess>
#include <QApplication>
#include <QAbstractNativeEventFilter>

#include "qt_mainwindow.hpp"
extern MainWindow *main_window;

#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>

#include <atomic>

extern "C" {
#include <X11/Xlib.h>
#include <X11/keysym.h>
#include <X11/extensions/XInput2.h>
#include <unistd.h>
#include <fcntl.h>

#include <86box/86box.h>
#include <86box/mouse.h>
#include <86box/plat.h>
}

static Display            *disp       = nullptr;
static QThread            *procThread = nullptr;
static XIEventMask         ximask;
static std::atomic<bool>   exitfromthread = false;
static std::atomic<double> xi2_mouse_x = 0, xi2_mouse_y = 0, xi2_mouse_abs_x = 0, xi2_mouse_abs_y = 0;
static int                 xi2opcode      = 0;
static double              prev_coords[2] = { 0.0 };
static Time                prev_time      = 0;

/* Based on SDL2. */
static void
parse_valuators(const double        *input_values,
                const unsigned char *mask, int mask_len,
                double *output_values, int output_values_len)
{
    int i = 0, z = 0;
    int top = mask_len * 8;
    if (top > 16)
        top = 16;

    memset(output_values, 0, output_values_len * sizeof(output_values[0]));
    for (; i < top && z < output_values_len; i++) {
        if (XIMaskIsSet(mask, i)) {
            output_values[z] = *input_values;
            input_values++;
        }
        z++;
    }
}

static bool exitthread = false;

void
xinput2_proc()
{
    Window win;
    win = DefaultRootWindow(disp);

    ximask.deviceid = XIAllMasterDevices;
    ximask.mask_len = XIMaskLen(XI_LASTEVENT);
    ximask.mask     = (unsigned char *) calloc(ximask.mask_len, sizeof(unsigned char));

    XISetMask(ximask.mask, XI_RawKeyPress);
    XISetMask(ximask.mask, XI_RawKeyRelease);
    XISetMask(ximask.mask, XI_RawButtonPress);
    XISetMask(ximask.mask, XI_RawButtonRelease);
    XISetMask(ximask.mask, XI_RawMotion);
    XISetMask(ximask.mask, XI_Motion);

    XISelectEvents(disp, win, &ximask, 1);

    XSync(disp, False);
    while (true) {
        XEvent               ev;
        XGenericEventCookie *cookie = (XGenericEventCookie *) &ev.xcookie;
        XNextEvent(disp, (XEvent *) &ev);

        if (XGetEventData(disp, cookie) && (cookie->type == GenericEvent) && (cookie->extension == xi2opcode)) {
            const XIRawEvent *rawev     = (const XIRawEvent *) cookie->data;
            double            coords[2] = { 0.0 };

            switch (cookie->evtype) {
                case XI_Motion:
                    {
                        const XIDeviceEvent *devev = (const XIDeviceEvent *) cookie->data;
                        parse_valuators(devev->valuators.values, devev->valuators.mask, devev->valuators.mask_len, coords, 2);

                        /* XIDeviceEvent and XIRawEvent share the XIEvent base struct, which
                           doesn't contain deviceid, but that's at the same offset on both. */
                        goto common_motion;
                    }

                case XI_RawMotion:
                    {
                        parse_valuators(rawev->raw_values, rawev->valuators.mask, rawev->valuators.mask_len, coords, 2);
common_motion:
                        /* Ignore duplicated events. */
                        if ((rawev->time == prev_time) && (coords[0] == prev_coords[0]) && (coords[1] == prev_coords[1]))
                            break;

                        /* SDL2 queries the device on every event, so doing that should be fine. */
                        int           i;
                        XIDeviceInfo *xidevinfo = XIQueryDevice(disp, rawev->deviceid, &i);
                        if (xidevinfo) {
                            /* Process the device's axes. */
                            int axis = 0;
                            for (i = 0; i < xidevinfo->num_classes; i++) {
                                const XIValuatorClassInfo *v = (const XIValuatorClassInfo *) xidevinfo->classes[i];
                                if (v->type == XIValuatorClass) {
                                    /* Is this an absolute or relative axis? */
                                    if (v->mode == XIModeRelative) {
                                        /* Set relative coordinates. */
                                        if (axis == 0)
                                            xi2_mouse_x = xi2_mouse_x + coords[axis];
                                        else
                                            xi2_mouse_y = xi2_mouse_y + coords[axis];
                                    } else {
                                        /* Convert absolute value range to pixel granularity, then to relative coordinates. */
                                        int    disp_screen = XDefaultScreen(disp);
                                        double abs_div;
                                        if (axis == 0) {
                                            abs_div = (v->max - v->min) / (double) XDisplayWidth(disp, disp_screen);
                                            if (abs_div <= 0)
                                                abs_div = 1;
                                            abs_div = (coords[axis] - v->min) / abs_div;

                                            if (xi2_mouse_abs_x != 0)
                                                xi2_mouse_x = xi2_mouse_x + (abs_div - xi2_mouse_abs_x);
                                            xi2_mouse_abs_x = abs_div;
                                        } else {
                                            abs_div = (v->max - v->min) / (double) XDisplayHeight(disp, disp_screen);
                                            if (abs_div <= 0)
                                                abs_div = 1;
                                            abs_div = (coords[axis] - v->min) / abs_div;

                                            if (xi2_mouse_abs_y != 0)
                                                xi2_mouse_y = xi2_mouse_y + (abs_div - xi2_mouse_abs_y);
                                            xi2_mouse_abs_y = abs_div;
                                        }
                                    }
                                    prev_coords[axis] = coords[axis];
                                    if (++axis >= 2) /* stop after X and Y processed */
                                        break;
                                }
                            }
                        }

                        prev_time = rawev->time;
                        if (!mouse_capture)
                            break;
                        XWindowAttributes winattrib {};
                        if (XGetWindowAttributes(disp, main_window->winId(), &winattrib)) {
                            auto globalPoint = main_window->mapToGlobal(QPoint(main_window->width() / 2, main_window->height() / 2));
                            XWarpPointer(disp, XRootWindow(disp, XScreenNumberOfScreen(winattrib.screen)), XRootWindow(disp, XScreenNumberOfScreen(winattrib.screen)), 0, 0, 0, 0, globalPoint.x(), globalPoint.y());
                            XFlush(disp);
                        }

                        break;
                    }
            }
        }

        XFreeEventData(disp, cookie);
        if (exitthread)
            break;
    }
    XCloseDisplay(disp);
}

void
xinput2_exit()
{
    if (!exitthread) {
        exitthread = true;
        procThread->wait(5000);
        procThread->terminate();
    }
}

void
xinput2_init()
{
    disp = XOpenDisplay(nullptr);
    if (!disp) {
        qWarning() << "Cannot open current X11 display";
        return;
    }
    auto event = 0, err = 0, minor = 1, major = 2;
    if (XQueryExtension(disp, "XInputExtension", &xi2opcode, &event, &err)) {
        if (XIQueryVersion(disp, &major, &minor) == Success) {
            procThread = QThread::create(xinput2_proc);
            procThread->start();
            atexit(xinput2_exit);
        }
    }
}

void
xinput2_poll()
{
    if (procThread && mouse_capture) {
        mouse_x = xi2_mouse_x;
        mouse_y = xi2_mouse_y;
    }
    xi2_mouse_x = 0;
    xi2_mouse_y = 0;
}
