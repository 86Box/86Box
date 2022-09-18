/*
 * 86Box	A hypervisor and IBM PC system emulator that specializes in
 *		running old operating systems and software designed for IBM
 *		PC systems and compatibles from 1981 through fairly recent
 *		system designs based on the PCI bus.
 *
 *		This file is part of the 86Box distribution.
 *
 *		Implement the VNC remote renderer with LibVNCServer.
 *
 *
 *
 * Authors:	Fred N. van Kempen, <decwiz@yahoo.com>
 *		Based on raw code by RichardG, <richardg867@gmail.com>
 *
 *		Copyright 2017-2019 Fred N. van Kempen.
 */
#include <stdarg.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>
#include <rfb/rfb.h>
#define HAVE_STDARG_H
#include <86box/86box.h>
#include <86box/device.h>
#include <86box/video.h>
#include <86box/keyboard.h>
#include <86box/mouse.h>
#include <86box/plat.h>
#include <86box/ui.h>
#include <86box/vnc.h>

#define VNC_MIN_X 320
#define VNC_MAX_X 2048
#define VNC_MIN_Y 200
#define VNC_MAX_Y 2048

static rfbScreenInfoPtr rfb = NULL;
static int              clients;
static int              updatingSize;
static int              allowedX,
    allowedY;
static int ptr_x, ptr_y, ptr_but;

#ifdef ENABLE_VNC_LOG
int vnc_do_log = ENABLE_VNC_LOG;

static void
vnc_log(const char *fmt, ...)
{
    va_list ap;

    if (vnc_do_log) {
        va_start(ap, fmt);
        pclog_ex(fmt, ap);
        va_end(ap);
    }
}
#else
#    define vnc_log(fmt, ...)
#endif

static void
vnc_kbdevent(rfbBool down, rfbKeySym k, rfbClientPtr cl)
{
    (void) cl;

    /* Handle it through the lookup tables. */
    vnc_kbinput(down ? 1 : 0, (int) k);
}

static void
vnc_ptrevent(int but, int x, int y, rfbClientPtr cl)
{
    if (x >= 0 && x < allowedX && y >= 0 && y < allowedY) {
        /* VNC uses absolute positions within the window, no deltas. */
        if (x != ptr_x || y != ptr_y) {
            mouse_x += (x - ptr_x) / 100;
            mouse_y += (y - ptr_y) / 100;
            ptr_x = x;
            ptr_y = y;
        }

        if (but != ptr_but) {
            mouse_buttons = 0;
            if (but & 0x01)
                mouse_buttons |= 0x01;
            if (but & 0x02)
                mouse_buttons |= 0x04;
            if (but & 0x04)
                mouse_buttons |= 0x02;
            ptr_but = but;
        }
    }

    rfbDefaultPtrAddEvent(but, x, y, cl);
}

static void
vnc_clientgone(rfbClientPtr cl)
{
    vnc_log("VNC: client disconnected: %s\n", cl->host);

    if (clients > 0)
        clients--;
    if (clients == 0) {
        /* No more clients, pause the emulator. */
        vnc_log("VNC: no clients, pausing..\n");

        /* Disable the mouse. */
        plat_mouse_capture(0);

        plat_pause(1);
    }
}

static enum rfbNewClientAction
vnc_newclient(rfbClientPtr cl)
{
    /* Hook the ClientGone function so we know when they're gone. */
    cl->clientGoneHook = vnc_clientgone;

    vnc_log("VNC: new client: %s\n", cl->host);
    if (++clients == 1) {
        /* Reset the mouse. */
        ptr_x   = allowedX / 2;
        ptr_y   = allowedY / 2;
        mouse_x = mouse_y = mouse_z = 0;
        mouse_buttons               = 0x00;

        /* We now have clients, un-pause the emulator if needed. */
        vnc_log("VNC: unpausing..\n");

        /* Enable the mouse. */
        plat_mouse_capture(1);

        plat_pause(0);
    }

    /* For now, we always accept clients. */
    return (RFB_CLIENT_ACCEPT);
}

static void
vnc_display(rfbClientPtr cl)
{
    /* Avoid race condition between resize and update. */
    if (!updatingSize && cl->newFBSizePending) {
        updatingSize = 1;
    } else if (updatingSize && !cl->newFBSizePending) {
        updatingSize = 0;

        allowedX = rfb->width;
        allowedY = rfb->height;
    }
}

static void
vnc_blit(int x, int y, int w, int h, int monitor_index)
{
    uint32_t *p;
    int       yy;

    if (monitor_index || (x < 0) || (y < 0) || (w <= 0) || (h <= 0) || (w > 2048) || (h > 2048) || (buffer32 == NULL)) {
        video_blit_complete_monitor(monitor_index);
        return;
    }

    for (yy = 0; yy < h; yy++) {
        p = (uint32_t *) &(((uint32_t *) rfb->frameBuffer)[yy * VNC_MAX_X]);

        if ((y + yy) >= 0 && (y + yy) < VNC_MAX_Y)
            video_copy(p, &(buffer32->line[yy]), w * sizeof(uint32_t));
    }

    if (screenshots)
        video_screenshot((uint32_t *) rfb->frameBuffer, 0, 0, VNC_MAX_X);

    video_blit_complete_monitor(monitor_index);

    if (!updatingSize)
        rfbMarkRectAsModified(rfb, 0, 0, allowedX, allowedY);
}

/* Initialize VNC for operation. */
int
vnc_init(UNUSED(void *arg))
{
    static char    title[128];
    rfbPixelFormat rpf = {
        /*
         * Screen format:
         *	32bpp; 32 depth;
         *	little endian;
         *	true color;
         *	max 255 R/G/B;
         *	red shift 16; green shift 8; blue shift 0;
         *	padding
         */
        32, 32, 0, 1, 255, 255, 255, 16, 8, 0, 0, 0
    };

    plat_pause(1);
    cgapal_rebuild_monitor(0);

    if (rfb == NULL) {
        wcstombs(title, ui_window_title(NULL), sizeof(title));
        updatingSize = 0;
        allowedX     = scrnsz_x;
        allowedY     = scrnsz_y;

        rfb              = rfbGetScreen(0, NULL, VNC_MAX_X, VNC_MAX_Y, 8, 3, 4);
        rfb->desktopName = title;
        rfb->frameBuffer = (char *) malloc(VNC_MAX_X * VNC_MAX_Y * 4);

        rfb->serverFormat  = rpf;
        rfb->alwaysShared  = TRUE;
        rfb->displayHook   = vnc_display;
        rfb->ptrAddEvent   = vnc_ptrevent;
        rfb->kbdAddEvent   = vnc_kbdevent;
        rfb->newClientHook = vnc_newclient;

        /* Set up our current resolution. */
        rfb->width  = allowedX;
        rfb->height = allowedY;

        rfbInitServer(rfb);

        rfbRunEventLoop(rfb, -1, TRUE);
    }

    /* Set up our BLIT handlers. */
    video_setblit(vnc_blit);

    clients = 0;

    vnc_log("VNC: init complete.\n");

    return (1);
}

void
vnc_close(void)
{
    video_setblit(NULL);

    if (rfb != NULL) {
        free(rfb->frameBuffer);

        rfbScreenCleanup(rfb);

        rfb = NULL;
    }
}

void
vnc_resize(int x, int y)
{
    rfbClientIteratorPtr iterator;
    rfbClientPtr         cl;

    if (rfb == NULL)
        return;

    /* TightVNC doesn't like certain sizes.. */
    if (x < VNC_MIN_X || x > VNC_MAX_X || y < VNC_MIN_Y || y > VNC_MAX_Y) {
        vnc_log("VNC: invalid resoltion %dx%d requested!\n", x, y);
        return;
    }

    if ((x != rfb->width || y != rfb->height) && x > 160 && y > 0) {
        vnc_log("VNC: updating resolution: %dx%d\n", x, y);

        allowedX = (rfb->width < x) ? rfb->width : x;
        allowedY = (rfb->width < y) ? rfb->width : y;

        rfb->width  = x;
        rfb->height = y;

        iterator = rfbGetClientIterator(rfb);
        while ((cl = rfbClientIteratorNext(iterator)) != NULL) {
            LOCK(cl->updateMutex);
            cl->newFBSizePending = 1;
            UNLOCK(cl->updateMutex);
        }
    }
}

/* Tell them to pause if we have no clients. */
int
vnc_pause(void)
{
    return ((clients > 0) ? 0 : 1);
}

void
vnc_take_screenshot(wchar_t *fn)
{
    vnc_log("VNC: take_screenshot\n");
}
