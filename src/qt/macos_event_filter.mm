#include <SDL.h>
//#include "86box/plat.h"
#include "cocoa_mouse.hpp"
#import <AppKit/AppKit.h>
extern "C"
{
#include <86box/86box.h>
#include <86box/keyboard.h>
#include <86box/mouse.h>
#include <86box/config.h>
//#include <86box/plat.h>
#include <86box/plat_dynld.h>
#include <86box/device.h>
#include <86box/timer.h>
#include <86box/ui.h>
#include <86box/video.h>
extern int mouse_capture;
extern void plat_mouse_capture(int);
}

typedef struct mouseinputdata
{
    int deltax, deltay, deltaz;
    int mousebuttons;
} mouseinputdata;

static mouseinputdata mousedata;

CocoaEventFilter::~CocoaEventFilter()
{

}

bool CocoaEventFilter::nativeEventFilter(const QByteArray &eventType, void *message, result_t *result)
{
    if (mouse_capture)
    {
        if (eventType == "mac_generic_NSEvent")
        {
            NSEvent* event = (NSEvent*)message;
            if ([event type] == NSEventTypeMouseMoved
                || [event type] == NSEventTypeLeftMouseDragged
                || [event type] == NSEventTypeRightMouseDragged
                || [event type] == NSEventTypeOtherMouseDragged)
            {
                mousedata.deltax += [event deltaX];
                mousedata.deltay += [event deltaY];
                return true;
            }
            if ([event type] == NSEventTypeScrollWheel)
            {
                mousedata.deltaz += [event deltaY];
                return true;
            }
            switch ([event type])
            {
                default: return false;
                case NSEventTypeLeftMouseDown:
                {
                    mousedata.mousebuttons |= 1;
                    break;
                }
                case NSEventTypeLeftMouseUp:
                {
                    mousedata.mousebuttons &= ~1;
                    break;
                }
                case NSEventTypeRightMouseDown:
                {
                    mousedata.mousebuttons |= 2;
                    break;
                }
                case NSEventTypeRightMouseUp:
                {
                    mousedata.mousebuttons &= ~2;
                    break;
                }
                case NSEventTypeOtherMouseDown:
                {
                    mousedata.mousebuttons |= 4;
                    break;
                }
                case NSEventTypeOtherMouseUp:
                {
                    if (mouse_get_buttons() < 3) { plat_mouse_capture(0); return true; }
                    mousedata.mousebuttons &= ~4;
                    break;
                }
            }
            return true;
        }
    }
    return false;
}

extern "C" void macos_poll_mouse()
{
    mouse_x = mousedata.deltax;
    mouse_y = mousedata.deltay;
    mouse_z = mousedata.deltaz;
    mousedata.deltax = mousedata.deltay = mousedata.deltaz = 0;
    mouse_buttons = mousedata.mousebuttons;
}
