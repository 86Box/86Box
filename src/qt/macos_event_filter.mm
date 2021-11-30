#include <SDL.h>
#include "86box/plat.h"
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
}

typedef struct mouseinputdata
{
    int deltax, deltay, deltaz;
    int mousebuttons;
} mouseinputdata;

extern SDL_mutex* mousemutex;
extern mouseinputdata mousedata;

CocoaEventFilter::~CocoaEventFilter()
{

}

bool CocoaEventFilter::nativeEventFilter(const QByteArray &eventType, void *message, long *result)
{
    if (mouse_capture)
    {
        if (eventType == "mac_generic_NSEvent")
        {
            NSEvent* event = (NSEvent*)message;
            if ([event type] == NSEventTypeMouseMoved)
            {
                SDL_LockMutex(mousemutex);
                mousedata.deltax += [event deltaX];
                mousedata.deltay += [event deltaY];
                SDL_UnlockMutex(mousemutex);
                return true;
            }
            if ([event type] == NSEventTypeScrollWheel)
            {
                SDL_LockMutex(mousemutex);
                mousedata.deltaz += [event deltaY];
                SDL_UnlockMutex(mousemutex);
                return true;
            }
            switch ([event type])
            {
                default: return false;
                case NSEventTypeLeftMouseDown:
                {
                    SDL_LockMutex(mousemutex);
                    mousedata.mousebuttons |= 1;
                    SDL_UnlockMutex(mousemutex);
                    break;
                }
                case NSEventTypeLeftMouseUp:
                {
                    SDL_LockMutex(mousemutex);
                    mousedata.mousebuttons &= ~1;
                    SDL_UnlockMutex(mousemutex);
                    break;
                }
                case NSEventTypeRightMouseDown:
                {
                    SDL_LockMutex(mousemutex);
                    mousedata.mousebuttons |= 2;
                    SDL_UnlockMutex(mousemutex);
                    break;
                }
                case NSEventTypeRightMouseUp:
                {
                    SDL_LockMutex(mousemutex);
                    mousedata.mousebuttons &= ~2;
                    SDL_UnlockMutex(mousemutex);
                    break;
                }
                case NSEventTypeOtherMouseDown:
                {
                    SDL_LockMutex(mousemutex);
                    mousedata.mousebuttons |= 4;
                    SDL_UnlockMutex(mousemutex);
                    break;
                }
                case NSEventTypeOtherMouseUp:
                {
                    if (mouse_get_buttons() < 3) { plat_mouse_capture(0); return true; }
                    SDL_LockMutex(mousemutex);
                    mousedata.mousebuttons &= ~4;
                    SDL_UnlockMutex(mousemutex);
                    break;
                }
            }
            return true;
        }
    }
    return false;
}
