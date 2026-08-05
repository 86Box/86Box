// #include "86box/plat.h"
#include "cocoa_mouse.hpp"
#import <AppKit/AppKit.h>
#import <Cocoa/Cocoa.h>
extern "C" {
#include <86box/86box.h>
#include <86box/keyboard.h>
#include <86box/mouse.h>
#include <86box/config.h>
// #include <86box/plat.h>
#include <86box/plat_dynld.h>
#include <86box/device.h>
#include <86box/timer.h>
#include <86box/ui.h>
#include <86box/video.h>
extern int  mouse_capture;
extern void plat_mouse_capture(int);
}

CocoaEventFilter::~CocoaEventFilter()
{
}

bool
CocoaEventFilter::nativeEventFilter(const QByteArray &eventType, void *message, result_t *result)
{
    int b = 0;

    if (mouse_capture) {
        if (eventType == "mac_generic_NSEvent") {
            NSEvent *event = (NSEvent *) message;
            if ([event type] == NSEventTypeMouseMoved
                || [event type] == NSEventTypeLeftMouseDragged
                || [event type] == NSEventTypeRightMouseDragged
                || [event type] == NSEventTypeOtherMouseDragged) {
                mouse_scalef((double) [event deltaX], (double) [event deltaY]);
                return true;
            }
            if ([event type] == NSEventTypeScrollWheel) {
                mouse_set_w(-[event deltaX]);
                mouse_set_z([event deltaY]);
                return true;
            }
            switch ([event type]) {
                default:
                    return false;
                case NSEventTypeLeftMouseDown:
                    {
                        b = mouse_get_buttons_ex() | 1;
                        mouse_set_buttons_ex(b);
                        break;
                    }
                case NSEventTypeLeftMouseUp:
                    {
                        b = mouse_get_buttons_ex() & ~1;
                        mouse_set_buttons_ex(b);
                        break;
                    }
                case NSEventTypeRightMouseDown:
                    {
                        b = mouse_get_buttons_ex() | 2;
                        mouse_set_buttons_ex(b);
                        break;
                    }
                case NSEventTypeRightMouseUp:
                    {
                        b = mouse_get_buttons_ex() & ~2;
                        mouse_set_buttons_ex(b);
                        break;
                    }
                case NSEventTypeOtherMouseDown:
                    {
                        b = mouse_get_buttons_ex() | 4;
                        mouse_set_buttons_ex(b);
                        break;
                    }
                case NSEventTypeOtherMouseUp:
                    {
                        if (mouse_get_buttons() < 3) {
                            plat_mouse_capture(0);
                            return true;
                        }
                        b = mouse_get_buttons_ex() & ~4;
                        mouse_set_buttons_ex(b);
                        break;
                    }
            }
            return true;
        }
    }
    return false;
}

static bool         pause_entered = true;
static id<NSObject> process_activity = nil;

void enter_pause(void)
{
    if (pause_entered)
        return;
    
    [[NSProcessInfo processInfo] endActivity: process_activity];
    process_activity = nil;
    pause_entered = true;
}

void exit_pause(void)
{
    if (!pause_entered)
        return;

    // NSActivityUserInteractive is a bitwise OR of NSActivityUserInitiated and NSActivityLatencyCritical.
    // However, allow the system to sleep if needed by using NSActivityUserInitiatedAllowingIdleSystemSleep instead of NSActivityUserInitiated.
    // And allow the system to terminate it as needed.
    process_activity = [[NSProcessInfo processInfo] beginActivityWithOptions: ((NSActivityUserInitiatedAllowingIdleSystemSleep &~ (NSActivitySuddenTerminationDisabled | NSActivityAutomaticTerminationDisabled)) | NSActivityLatencyCritical) reason:@"Unpaused."];
    pause_entered = false;
}
