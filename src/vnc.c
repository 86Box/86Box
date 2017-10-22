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
 * Version:	@(#)lnx_vnc.c	1.0.4	2017/10/18
 *
 * Authors:	Fred N. van Kempen, <decwiz@yahoo.com>
 *		Based on raw code by RichardG, <richardg867@gmail.com>
 *
 *		Copyright 2017 Fred N. van Kempen.
 */
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>
#include "86box.h"
#include "device.h"
#include "video/video.h"
#include "plat.h"
#include "plat_keyboard.h"
#include "plat_mouse.h"
#include "ui.h"
#define BITMAP MY_BITMAP
#include <rfb/rfb.h>
#undef BITMAP
#include "vnc.h"


#define VNC_MIN_X	320
#define VNC_MAX_X	2048
#define VNC_MIN_Y	200
#define VNC_MAX_Y	2048


static rfbScreenInfoPtr	rfb = NULL;
static int	clients;
static int	updatingSize;
static int	allowedX,
		allowedY;

static int keysyms_00[] = {
    0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, // 00-07
    0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, // 08-0f
    0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, // 10-17
    0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, // 18-1f
    0x0039, 0x2a02, 0x2a28, 0x2a04, 0x2a05, 0x2a06, 0x2a08, 0x0028, // 20-27
    0x2a0a, 0x2a0b, 0x2a09, 0x2a0d, 0x0033, 0x000c, 0x0034, 0x0035, // 28-2f
    0x000b, 0x0002, 0x0003, 0x0004, 0x0005, 0x0006, 0x0007, 0x0008, // 30-37
    0x0009, 0x000a, 0x2a27, 0x0027, 0x2a33, 0x000d, 0x2a34, 0x2a35, // 38-3f
    0x2a03, 0x2a1e, 0x2a30, 0x2a2e, 0x2a20, 0x2a12, 0x2a21, 0x2a22, // 40-47
    0x2a23, 0x2a17, 0x2a24, 0x2a25, 0x2a26, 0x2a32, 0x2a31, 0x2a18, // 48-4f
    0x2a19, 0x2a10, 0x2a13, 0x2a1f, 0x2a14, 0x2a16, 0x2a2f, 0x2a11, // 50-57
    0x2a2d, 0x2a15, 0x2a2c, 0x001a, 0x0000, 0x001b, 0x2a07, 0x2a0c, // 58-5f
    0x0029, 0x001e, 0x0030, 0x002e, 0x0020, 0x0012, 0x0021, 0x0022, // 60-67
    0x0023, 0x0017, 0x0024, 0x0025, 0x0026, 0x0032, 0x0031, 0x0018, // 68-6f
    0x0019, 0x0010, 0x0013, 0x001f, 0x0014, 0x0016, 0x002f, 0x0011, // 70-77
    0x002d, 0x0015, 0x002c, 0x2a1a, 0x0000, 0x2a1b, 0x2a29, 0x0000, // 78-7f
    0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, // 80-87
    0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, // 88-8f
    0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, // 90-97
    0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, // 98-9f
    0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, // a0-a7
    0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, // a8-af
    0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, // b0-b7
    0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, // b8-bf
    0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, // c0-c7
    0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, // c8-cf
    0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, // d0-d7
    0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, // d8-df
    0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, // e0-e7
    0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, // e8-ef
    0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, // f0-f7
    0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000  // f8-ff
};

static int keysyms_ff[] = {
    0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, // 00-07
    0x000e, 0x000f, 0x0000, 0x004c, 0x0000, 0x001c, 0x0000, 0x0000, // 08-0f
    0x0000, 0x0000, 0x0000, 0xff45, 0x0000, 0x0000, 0x0000, 0x0000, // 10-17
    0x0000, 0x0000, 0x0000, 0x0001, 0x0000, 0x0000, 0x0000, 0x0000, // 18-1f
    0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, // 20-27
    0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, // 28-2f
    0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, // 30-37
    0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, // 38-3f
    0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, // 40-47
    0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, // 48-4f
    0x0047, 0x00cb, 0xaac8, 0x00cd, 0x00d0, 0x0049, 0x0051, 0x004f, // 50-57
    0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, // 58-5f
    0x0000, 0x0000, 0x0000, 0x0052, 0x0000, 0x0000, 0x0000, 0x00dd, // 60-67
    0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, // 68-6f
    0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, // 70-77
    0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, // 78-7f
    0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, // 80-87
    0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x009c, 0x0000, 0x0000, // 88-8f
    0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, // 90-97
    0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, // 98-9f
    0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, // a0-a7
    0x0000, 0x0000, 0x0037, 0x004e, 0x0000, 0x004a, 0x0000, 0x00b5, // a8-af
    0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, // b0-b7
    0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x003b, 0x003c, // b8-bf
    0x003d, 0x003e, 0x003f, 0x0040, 0x0041, 0x0042, 0x0043, 0x0044, // c0-c7
    0x0057, 0x0058, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, // c8-cf
    0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, // d0-d7
    0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, // d8-df
    0x0000, 0x0036, 0x0000, 0x001d, 0x009d, 0x0000, 0x0000, 0x0000, // e0-e7
    0x0000, 0x0038, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, // e8-ef
    0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, // f0-f7
    0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0053  // f8-ff
};


static void
vnc_kbdevent(rfbBool down, rfbKeySym k, rfbClientPtr cl)
{
    int will_press = 0;
    int key;

#if 0
    pclog("VNC: kbdevent %d %x\n", down, k);
#endif
   
    if ((k >> 8) == 0x00) {
	will_press = keysyms_00[k & 0xff];
    } else if ((k >> 8) == 0xff) {
	will_press = keysyms_ff[k & 0xff];
    }

    if (will_press <= 0) return;

#if 0
    pclog("VNC: translated to %x %x\n",
	(will_press >> 8) & 0xff, will_press & 0xff);
#endif

    /* First key. */
    key = (will_press >> 8) & 0xff;
    if (key > 0)
	recv_key[key] = down;

    /* Second key. */
    key = will_press & 0xff;
    if (key > 0)
	recv_key[key] = down;
}


static void
vnc_ptrevent(int but, int x, int y, rfbClientPtr cl)
{
   if (x>=0 && x<allowedX && y>=0 && y<allowedY) {
	pclog("VNC: mouse event, x=%d, y=%d, buttons=%02x\n", x, y, but);

	/* TODO: stuff a new "packet" into the platform mouse buffer. */
   }

   rfbDefaultPtrAddEvent(but, x, y, cl);
}


static void
vnc_clientgone(rfbClientPtr cl)
{
    pclog("VNC: client disconnected: %s\n", cl->host);

    if (clients > 0)
	clients--;
    if (clients == 0) {
	/* No more clients, pause the emulator. */
	pclog("VNC: no clients, pausing..\n");
	plat_pause(1);
    }
}


static enum rfbNewClientAction
vnc_newclient(rfbClientPtr cl)
{
    /* Hook the ClientGone function so we know when they're gone. */
    cl->clientGoneHook = vnc_clientgone;

    pclog("VNC: new client: %s\n", cl->host);
    if (++clients == 1) {
	/* We now have clients, un-pause the emulator if needed. */
	pclog("VNC: unpausing..\n");
	plat_pause(0);
    }

    /* For now, we always accept clients. */
    return(RFB_CLIENT_ACCEPT);
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
vnc_blit(int x, int y, int y1, int y2, int w, int h)
{
    uint32_t *p;
    int yy;

    for (yy=y1; yy<y2; yy++) {
	p = (uint32_t *)&(((uint32_t *)rfb->frameBuffer)[yy*VNC_MAX_X]);

	if ((y+yy) >= 0 && (y+yy) < VNC_MAX_Y)
		memcpy(p, &(((uint32_t *)buffer32->line[y+yy])[x]), w*4);
    }
 
    video_blit_complete();

    if (! updatingSize)
	rfbMarkRectAsModified(rfb, 0,0, allowedX,allowedY);
}


/* Initialize VNC for operation. */
int
vnc_init(UNUSED(void *arg))
{
    static char title[128];
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
	32, 32, 0, 1, 255,255,255, 16, 8, 0, 0, 0
    };

    if (rfb == NULL) {
	wcstombs(title, ui_window_title(NULL), sizeof(title));
	updatingSize = 0;
	allowedX = scrnsz_x;
	allowedY = scrnsz_y;
 
	rfb = rfbGetScreen(0, NULL, VNC_MAX_X, VNC_MAX_Y, 8, 3, 4);
	rfb->desktopName = title;
	rfb->frameBuffer = (char *)malloc(VNC_MAX_X*VNC_MAX_Y*4);

	rfb->serverFormat = rpf;
	rfb->alwaysShared = TRUE;
	rfb->displayHook = vnc_display;
	rfb->ptrAddEvent = vnc_ptrevent;
	rfb->kbdAddEvent = vnc_kbdevent;
	rfb->newClientHook = vnc_newclient;
 
	/* Set up our current resolution. */
	rfb->width = allowedX;
	rfb->height = allowedY;
 
	rfbInitServer(rfb);

	rfbRunEventLoop(rfb, -1, TRUE);
    }
 
    /* Set up our BLIT handlers. */
    video_setblit(vnc_blit);

    clients = 0;

    pclog("VNC: init complete.\n");

    return(1);
}


void
vnc_close(void)
{
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
    rfbClientPtr cl;

    if (rfb == NULL) return;

    /* TightVNC doesn't like certain sizes.. */
    if (x < VNC_MIN_X || x > VNC_MAX_X || y < VNC_MIN_Y || y > VNC_MAX_Y) {
	pclog("VNC: invalid resoltion %dx%d requested!\n", x, y);
	return;
    }

    if ((x != rfb->width || y != rfb->height) && x > 160 && y > 0) {
	pclog("VNC: updating resolution: %dx%d\n", x, y);
 
	allowedX = (rfb->width < x) ? rfb->width : x;
	allowedY = (rfb->width < y) ? rfb->width : y;
 
	rfb->width = x;
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
    return((clients > 0) ? 0 : 1);
}


void
vnc_take_screenshot(wchar_t *fn)
{
    pclog("VNC: take_screenshot\n");
}
