#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <stdio.h>

#include "x11_util.h"

void set_wm_class(unsigned long window, char *res_name) {
    Display* display = XOpenDisplay(NULL);
    if (display == NULL) {
        return;
    }

    XClassHint hint;
    XGetClassHint(display, window, &hint);

    hint.res_name = res_name;
    XSetClassHint(display, window, &hint);

    // During testing, I've had to issue XGetClassHint after XSetClassHint
    // to get the window manager to recognize the change.
    XGetClassHint(display, window, &hint);
}
