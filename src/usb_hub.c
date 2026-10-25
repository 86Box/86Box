/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          USB 1.1 hub: a full-speed, self-powered hub with four
 *          downstream ports, per-port power switching and per-port
 *          over-current reporting, on the USB device core.
 *
 *          Written from:
 *          - Universal Serial Bus Specification, Revision 1.1,
 *            September 23, 1998 (chapters 7, 9 and 11).
 *          - Universal Serial Bus Specification, Revision 1.0,
 *            January 15, 1996 (the hub descriptor type).
 *
 * Authors: skiretic, <skiretic@proton.me>
 *
 *          Copyright 2026 skiretic.
 */
#include <stdarg.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>
#define HAVE_STDARG_H
#include <86box/86box.h>
#include <86box/device.h>
#include <86box/timer.h>
#include <86box/mem.h>
#include <86box/usb.h>
#include <86box/usb_dev.h>
#include <86box/plat_unused.h>

#define HUB_PORTS 4

/* Class request codes and feature selectors (USB 1.1 Tables 11-11 and
   11-12, p. 268 / PDF 284). */
#define HUB_GET_STATUS     0x00
#define HUB_CLEAR_FEATURE  0x01
#define HUB_SET_FEATURE    0x03
#define HUB_GET_DESCRIPTOR 0x06

#define C_HUB_LOCAL_POWER   0
#define C_HUB_OVER_CURRENT  1
#define PORT_CONNECTION     0
#define PORT_ENABLE         1
#define PORT_SUSPEND        2
#define PORT_OVER_CURRENT   3
#define PORT_RESET          4
#define PORT_POWER          8
#define PORT_LOW_SPEED      9
#define C_PORT_CONNECTION   16
#define C_PORT_ENABLE       17
#define C_PORT_SUSPEND      18
#define C_PORT_OVER_CURRENT 19
#define C_PORT_RESET        20

/* wPortStatus bits (USB 1.1 Table 11-15, p. 274 / PDF 290) and wPortChange
   bits (Table 11-16, p. 277 / PDF 293). A wPortChange bit number is its
   C_PORT_ feature selector minus 16. */
#define PS_CONNECTION 0x0001
#define PS_ENABLE     0x0002
#define PS_SUSPEND    0x0004
#define PS_RESET      0x0010
#define PS_POWER      0x0100
#define PS_LOW_SPEED  0x0200

#define PC_CONNECTION 0x0001
#define PC_ENABLE     0x0002
#define PC_SUSPEND    0x0004
#define PC_RESET      0x0010

/* bPwrOn2PwrGood is in 2 ms units (USB 1.1 Table 11-8, p. 264 / PDF 280):
   50 = 100 ms. Resetting lasts 10 to 20 ms, 10 ms preferred (s11.5.1.5,
   p. 242 / PDF 258); Resuming lasts 20 ms (s11.5.1.9, p. 243 / PDF 259). */
#define HUB_PWRON2PWRGOOD 50
#define HUB_RESET_US      10000.0
#define HUB_RESUME_US     20000.0

enum {
    HUB_TIMER_NONE = 0,
    HUB_TIMER_POWER, /* power on until power good */
    HUB_TIMER_RESET,
    HUB_TIMER_RESUME
};

typedef struct usb_hub_port_t {
    uint16_t   status;
    uint16_t   change;
    uint8_t    timer_op;
    int        num;
    usb_dev_t *dev;
    pc_timer_t timer;
} usb_hub_port_t;

typedef struct usb_hub_t {
    usb_dev_t      usb;
    usb_hub_port_t port[HUB_PORTS];
} usb_hub_t;

int usb_hub_type = 0;

/* Device and configuration descriptors. The hub class fixes the class
   codes, the single Status Change endpoint (interrupt IN) and its bInterval
   of FFh (USB 1.1 s11.15.1, p. 263 / PDF 279). MaxPower is what the hub
   draws from VBUS (s11.14, p. 262 / PDF 278); this hub is self-powered and
   draws nothing. */
static const uint8_t hub_dev_desc[18] = {
    18, 0x01,   /* bLength, DEVICE */
    0x10, 0x01, /* bcdUSB 1.10 */
    0x09, 0x00, /* bDeviceClass 09h (hub), bDeviceSubClass 0 */
    0x00,       /* bDeviceProtocol */
    64,         /* bMaxPacketSize0 */
    0x00, 0x00, /* idVendor */
    0x05, 0x00, /* idProduct */
    0x00, 0x01, /* bcdDevice 1.00 */
    1, 2, 0,    /* iManufacturer, iProduct, iSerialNumber */
    1           /* bNumConfigurations */
};

static const uint8_t hub_cfg_desc[25] = {
    9, 0x02, 25, 0, /* CONFIGURATION, wTotalLength */
    1, 1, 0,        /* bNumInterfaces, bConfigurationValue, iConfiguration */
    0xc0,           /* bmAttributes: self-powered */
    0,              /* MaxPower */
    9, 0x04, 0, 0, 1, 0x09, 0x00, 0x00, 0, /* INTERFACE: one endpoint, hub class */
    7, 0x05, 0x81, 0x03, 1, 0, 0xff        /* ENDPOINT: EP1 IN, interrupt, 1 byte, bInterval FFh */
};

/* Hub descriptor (USB 1.1 Table 11-8, p. 264-265 / PDF 280-281).
   wHubCharacteristics 0009h: per-port power switching, not part of a
   compound device, per-port over-current reporting. DeviceRemovable 0:
   every port is removable. PortPwrCtrlMask is kept only for USB 1.0
   software and is all ones (s11.11, p. 255 / PDF 271). */
static const uint8_t hub_desc[9] = {
    9, 0x29, HUB_PORTS,
    0x09, 0x00,
    HUB_PWRON2PWRGOOD,
    100,  /* bHubContrCurrent, mA */
    0x00, /* DeviceRemovable */
    0xff  /* PortPwrCtrlMask */
};

static const char *const hub_strings[] = {
    "86Box",
    "USB Hub"
};

static void
hub_port_timer_stop(usb_hub_port_t *p)
{
    timer_stop(&p->timer);
    p->timer_op = HUB_TIMER_NONE;
}

static void
hub_port_timer_start(usb_hub_port_t *p, int op, double us)
{
    p->timer_op = op;
    timer_on_auto(&p->timer, us);
}

/* A powered port with a device on it detects the attach: PORT_CONNECTION
   and C_PORT_CONNECTION are set (USB 1.1 s11.16.2.6.1.1, p. 275 / PDF 291;
   s11.16.2.6.2.1, p. 277 / PDF 293). PORT_LOW_SPEED is set at the same
   time; the host reads it only once the port is enabled. */
static void
hub_port_connect(usb_hub_port_t *p)
{
    p->status |= PS_CONNECTION | (p->dev->low_speed ? PS_LOW_SPEED : 0);
    p->change |= PC_CONNECTION;
}

/* Power is good bPwrOn2PwrGood after SetPortFeature(PORT_POWER). Until
   then the port sees no device, so a connect is reported no earlier than
   the host is told to wait. */
static int
hub_port_power_good(const usb_hub_port_t *p)
{
    return (p->status & PS_POWER) && (p->timer_op != HUB_TIMER_POWER);
}

/* Powered-off (and Not Configured, which this hub treats the same way):
   the port has no power and every wPortStatus and wPortChange bit reads 0
   (USB 1.1 s11.16.2.6.1-s11.16.2.6.2, p. 274-278 / PDF 290-294; this hub
   never sets the over-current bits). A device that loses power drops back
   to the Powered state, without address or configuration (Figure 9-1,
   p. 176 / PDF 192), and answers nothing until it sees a reset (s9.1.1.3,
   p. 178 / PDF 194). usb_dev_bus_reset() clears the address and
   configuration; clearing powered_reset makes the device ignore traffic
   until the next port reset. */
static void
hub_port_power_off(usb_hub_port_t *p)
{
    hub_port_timer_stop(p);
    if ((p->status & PS_POWER) && (p->dev != NULL)) {
        usb_dev_bus_reset(p->dev);
        p->dev->powered_reset = 0;
    }
    p->status = 0x0000;
    p->change = 0x0000;
}

static void
hub_port_timer(void *priv)
{
    usb_hub_port_t *p  = (usb_hub_port_t *) priv;
    int             op = p->timer_op;

    p->timer_op = HUB_TIMER_NONE;

    switch (op) {
        case HUB_TIMER_POWER:
            if (p->dev != NULL)
                hub_port_connect(p);
            break;

        case HUB_TIMER_RESET:
            /* At the end of reset signaling the hub sets C_PORT_RESET and
               enables the port at once (USB 1.1 s11.16.2.9, p. 279 /
               PDF 295). */
            p->status = (p->status & ~PS_RESET) | PS_ENABLE;
            p->change |= PC_RESET;
            break;

        case HUB_TIMER_RESUME:
            /* Resuming -> SendEOP -> Enabled in one step (USB 1.1
               s11.5.1.9-s11.5.1.10, p. 242-243 / PDF 258-259). PORT_SUSPEND
               reads 1 until here, as it means "suspended or resuming"
               (Table 11-15, p. 274 / PDF 290). C_PORT_SUSPEND is set as
               Resuming ends (s11.16.2.6.2.3, p. 278 / PDF 294). */
            p->status &= ~PS_SUSPEND;
            p->change |= PC_SUSPEND;
            break;

        default:
            break;
    }
}

/* Clearing PORT_ENABLE also clears PORT_SUSPEND (USB 1.1 s11.16.2.6.1.3,
   p. 275 / PDF 291) and stops a reset or resume in progress: a disabled
   port drives no signaling. */
static void
hub_port_disable(usb_hub_port_t *p)
{
    if ((p->timer_op == HUB_TIMER_RESET) || (p->timer_op == HUB_TIMER_RESUME))
        hub_port_timer_stop(p);
    p->status &= ~(PS_ENABLE | PS_SUSPEND | PS_RESET);
}

static int
hub_set_port_feature(usb_hub_port_t *p, int feature)
{
    switch (feature) {
        case PORT_RESET:
            /* A no-operation when Powered-off or Disconnected (USB 1.1
               s11.5.1.5, p. 242 / PDF 258). A port still waiting for power
               good has no connection yet, so it falls under Disconnected.
               A reset already in progress is not restarted. */
            if (!hub_port_power_good(p) || !(p->status & PS_CONNECTION) || (p->status & PS_RESET))
                return 0;
            hub_port_disable(p);
            p->status |= PS_RESET;
            usb_dev_bus_reset(p->dev);
            hub_port_timer_start(p, HUB_TIMER_RESET, HUB_RESET_US);
            return 0;

        /* A no-operation unless the port is Enabled; the same goes for
           PORT_POWER unless it is Powered-off (USB 1.1 s11.16.2.9, p. 280 /
           PDF 296). */
        case PORT_SUSPEND:
            if ((p->status & (PS_ENABLE | PS_SUSPEND | PS_RESET)) == PS_ENABLE) {
                p->status |= PS_SUSPEND;
            }
            return 0;

        case PORT_POWER:
            if (!(p->status & PS_POWER)) {
                p->status |= PS_POWER;
                hub_port_timer_start(p, HUB_TIMER_POWER, HUB_PWRON2PWRGOOD * 2000.0);
            }
            return 0;

        /* Software must not set PORT_CONNECTION, PORT_OVER_CURRENT or
           PORT_LOW_SPEED, and the hub treats them as no-operations (USB 1.1
           s11.16.2.6.1.1-s11.16.2.6.1.7, p. 275-276 / PDF 291-292).
           Setting a change bit is optional; a hub that does not support it
           may ignore the request (s11.16.2.6.2, p. 277 / PDF 293), as this
           one does. */
        case PORT_CONNECTION:
        case PORT_OVER_CURRENT:
        case PORT_LOW_SPEED:
        case C_PORT_CONNECTION:
        case C_PORT_ENABLE:
        case C_PORT_SUSPEND:
        case C_PORT_OVER_CURRENT:
        case C_PORT_RESET:
            return 0;

        /* SetPortFeature(PORT_ENABLE) is not specified; a Request Error is
           preferred (USB 1.1 s11.16.2.6.1.2, p. 275 / PDF 291). A selector
           missing from Table 11-12 is a Request Error (s11.16.2.9, p. 280 /
           PDF 296). */
        default:
            return USB_DATA_STALL;
    }
}

static int
hub_clear_port_feature(usb_hub_port_t *p, int feature)
{
    switch (feature) {
        /* Clearing PORT_ENABLE disables the port; it is a no-operation when
           Powered-off (USB 1.1 s11.16.2.2, p. 269 / PDF 285). */
        case PORT_ENABLE:
            if (p->status & PS_POWER) {
                hub_port_disable(p);
            }
            return 0;

        case PORT_SUSPEND:
            /* Host-initiated resume; a no-operation unless suspended (USB 1.1
               s11.16.2.2, p. 269 / PDF 285). */
            if ((p->status & PS_SUSPEND) && (p->timer_op != HUB_TIMER_RESUME)) {
                hub_port_timer_start(p, HUB_TIMER_RESUME, HUB_RESUME_US);
            }
            return 0;

        case PORT_POWER:
            hub_port_power_off(p);
            return 0;

        case C_PORT_CONNECTION:
        case C_PORT_ENABLE:
        case C_PORT_SUSPEND:
        case C_PORT_OVER_CURRENT:
        case C_PORT_RESET:
            p->change &= ~(1 << (feature - C_PORT_CONNECTION));
            return 0;

        /* No-operations, as on the SetPortFeature side (USB 1.1
           s11.16.2.6.1.1-s11.16.2.6.1.7, p. 275-276 / PDF 291-292). */
        case PORT_CONNECTION:
        case PORT_OVER_CURRENT:
        case PORT_RESET:
        case PORT_LOW_SPEED:
            return 0;

        default:
            return USB_DATA_STALL;
    }
}

/* Hub class requests (USB 1.1 s11.16.2, p. 267 / PDF 283); standard requests
   are handled by the device core. A request whose wValue, wIndex or wLength
   is not one the spec allows is a Request Error, answered with STALL.
   GetBusState and SetHubDescriptor are optional and not implemented, so
   they STALL as well (s11.16.2.3, p. 270 / PDF 286; s11.16.2.7, p. 278 /
   PDF 294). */
static int
hub_control(usb_dev_t *udev, const uint8_t *setup, uint8_t *data)
{
    usb_hub_t      *hub    = (usb_hub_t *) udev->priv;
    uint16_t        wvalue = setup[2] | (setup[3] << 8);
    uint16_t        windex = setup[4] | (setup[5] << 8);
    uint16_t        wlen   = setup[6] | (setup[7] << 8);
    usb_hub_port_t *p      = ((windex >= 1) && (windex <= HUB_PORTS)) ? &hub->port[windex - 1] : NULL;

    switch ((setup[0] << 8) | setup[1]) {
        case 0xa000 | HUB_GET_STATUS:
            /* wHubStatus 0: local power good, and no hub over-current, which
               a hub that reports over-current per port always reads as 0
               (USB 1.1 Table 11-13, p. 272 / PDF 288). wHubChange 0. */
            if ((wvalue != 0) || (windex != 0) || (wlen != 4))
                return USB_DATA_STALL;
            memset(data, 0x00, 4);
            return 4;

        case 0xa300 | HUB_GET_STATUS:
            if ((wvalue != 0) || (p == NULL) || (wlen != 4))
                return USB_DATA_STALL;
            data[0] = p->status & 0xff;
            data[1] = p->status >> 8;
            data[2] = p->change & 0xff;
            data[3] = p->change >> 8;
            return 4;

        case 0xa000 | HUB_GET_DESCRIPTOR:
            /* USB 1.0 names the hub descriptor by type 0 (USB 1.0
               s11.12.2.4, p. 258 / PDF 258); USB 1.1 uses 29h (USB 1.1
               Table 11-8, p. 264 / PDF 280). BIOSes written to USB 1.0
               still ask for type 0; the Intel AN430TX BIOS is one. Either
               type is accepted, with descriptor index 0. The device core
               cuts the reply to wLength (s11.16.2.4, p. 271 / PDF 287). */
            if (((wvalue != 0x0000) && (wvalue != 0x2900)) || (windex != 0))
                return USB_DATA_STALL;
            memcpy(data, hub_desc, sizeof(hub_desc));
            return sizeof(hub_desc);

        case 0x2000 | HUB_CLEAR_FEATURE:
        case 0x2000 | HUB_SET_FEATURE:
            /* Neither hub change bit is ever set. Setting them is optional,
               and a hub may treat SetHubFeature for them as a no-operation;
               clearing a clear bit is not an error (USB 1.1 Table 11-14,
               p. 272 / PDF 288). */
            if ((windex != 0) || (wlen != 0) || ((wvalue != C_HUB_LOCAL_POWER) && (wvalue != C_HUB_OVER_CURRENT)))
                return USB_DATA_STALL;
            return 0;

        case 0x2300 | HUB_SET_FEATURE:
            if ((p == NULL) || (wlen != 0))
                return USB_DATA_STALL;
            return hub_set_port_feature(p, wvalue);

        case 0x2300 | HUB_CLEAR_FEATURE:
            if ((p == NULL) || (wlen != 0))
                return USB_DATA_STALL;
            return hub_clear_port_feature(p, wvalue);

        default:
            return USB_DATA_STALL;
    }
}

/* The Status Change endpoint returns the Hub and Port Status Change Bitmap,
   bit N for port N, and NAKs while no change bit is set (USB 1.1 s11.13.1,
   p. 257 / PDF 273; s11.13.4, p. 259-260 / PDF 275-276). Bit 0 is the hub's
   own change bit; this hub never sets one, so it stays 0. */
static int
hub_data_in(usb_dev_t *udev, int ep, uint8_t *buf, UNUSED(int maxlen))
{
    const usb_hub_t *hub = (usb_hub_t *) udev->priv;
    uint8_t          map = 0x00;

    if (ep != 1)
        return USB_DATA_STALL;

    for (int i = 0; i < HUB_PORTS; i++) {
        if (hub->port[i].change != 0)
            map |= 1 << (i + 1);
    }
    if (map == 0x00)
        return USB_DATA_NAK;

    buf[0] = map;
    return 1;
}

/* A reset on the upstream port leaves every port Not Configured (USB 1.1
   s11.10.1, p. 254-255 / PDF 270-271). SetConfiguration puts every port
   in Not Configured (value 0) or Powered-off (any other value)
   (s11.5.1.1-s11.5.1.2, p. 240-241 / PDF 256-257). Either way the port has
   no power (s11.11, p. 255 / PDF 271), and the host powers it with
   SetPortFeature(PORT_POWER). */
static void
hub_all_ports_off(usb_hub_t *hub)
{
    for (int i = 0; i < HUB_PORTS; i++)
        hub_port_power_off(&hub->port[i]);
}

static void
hub_reset(usb_dev_t *udev)
{
    hub_all_ports_off((usb_hub_t *) udev->priv);
}

static void
hub_configured(usb_dev_t *udev, UNUSED(int config))
{
    hub_all_ports_off((usb_hub_t *) udev->priv);
}

/* A token reaches a device only through an enabled, unsuspended port, and
   only at the device's speed. A low-speed port gets nothing but packets
   that follow a PRE PID; those packets go to full-speed ports too, but no
   low-speed bit pattern decodes as a full-speed PID (USB 1.1 s11.8.4,
   p. 252-253 / PDF 268-269). */
static usb_dev_t *
hub_route(usb_dev_t *udev, int addr, int low_speed)
{
    const usb_hub_t *hub = (usb_hub_t *) udev->priv;

    for (int i = 0; i < HUB_PORTS; i++) {
        const usb_hub_port_t *p = &hub->port[i];

        if ((p->dev == NULL) || ((p->status & (PS_ENABLE | PS_SUSPEND | PS_RESET)) != PS_ENABLE))
            continue;
        if ((p->dev->addr == addr) && (p->dev->low_speed == low_speed))
            return p->dev;
    }

    return NULL;
}

static void
hub_port_attach(usb_dev_t *udev, usb_hub_port_t *p, usb_dev_t *dev)
{
    p->dev      = dev;
    dev->port   = p;
    dev->parent = udev;
    if (hub_port_power_good(p))
        hub_port_connect(p);
}

/* The device's last port on this hub if it is free, else the first free
   one. */
static int
hub_attach(usb_dev_t *udev, usb_dev_t *dev)
{
    usb_hub_t *hub = (usb_hub_t *) udev->priv;

    for (int i = 0; i < HUB_PORTS; i++) {
        if ((dev->home_port == &hub->port[i]) && (hub->port[i].dev == NULL)) {
            hub_port_attach(udev, &hub->port[i], dev);
            return 1;
        }
    }

    for (int i = 0; i < HUB_PORTS; i++) {
        if (hub->port[i].dev == NULL) {
            hub_port_attach(udev, &hub->port[i], dev);
            return 1;
        }
    }

    return 0;
}

/* A detach sets C_PORT_CONNECTION and disables the port. C_PORT_ENABLE is
   not set: it marks a Port Error only (USB 1.1 s11.16.2.6.1.2, p. 275 /
   PDF 291; s11.16.2.6.2.2, p. 277 / PDF 293). */
static void
hub_detach(UNUSED(usb_dev_t *udev), usb_dev_t *dev)
{
    usb_hub_port_t *p = (usb_hub_port_t *) dev->port;

    if (p == NULL)
        return;

    if (p->status & PS_CONNECTION)
        p->change |= PC_CONNECTION;
    hub_port_disable(p);
    p->status &= ~(PS_CONNECTION | PS_LOW_SPEED);
    p->dev      = NULL;
    dev->port   = NULL;
    dev->parent = NULL;
}

/* A device still sending at EOF2 is a Port Error: the port goes from
   Enabled to Disabled and C_PORT_ENABLE is set (USB 1.1 s11.8.1, p. 251 /
   PDF 267; s11.16.2.6.2.2, p. 277 / PDF 293). Only this port is disabled.
   The hub ends the packet upstream with its own EOP at EOF1, so the port
   above the hub is not disabled (s11.2.2, p. 234 / PDF 250). */
static void
hub_eof_babble(UNUSED(usb_dev_t *udev), usb_dev_t *dev)
{
    usb_hub_port_t *p = (usb_hub_port_t *) dev->port;

    if ((p == NULL) || ((p->status & (PS_ENABLE | PS_SUSPEND | PS_RESET)) != PS_ENABLE))
        return;

    hub_port_disable(p);
    p->change |= PC_ENABLE;
}

/* Whether the device on this port sees an idle bus, which suspends it. A
   disabled or suspended port passes no traffic (USB 1.1 s11.5.1.4,
   p. 241-242 / PDF 257-258; s11.16.2.6.1.3, p. 275 / PDF 291). An enabled
   port is idle when the hub itself is suspended. An unpowered or empty port,
   a port in reset (SE0) and a port driving resume (K) are not idle. */
static int
hub_port_idle(const usb_dev_t *udev, const usb_dev_t *dev)
{
    const usb_hub_port_t *p = (const usb_hub_port_t *) dev->port;

    if ((p == NULL) || !hub_port_power_good(p) || !(p->status & PS_CONNECTION) || (p->status & PS_RESET) ||
        (p->timer_op == HUB_TIMER_RESUME))
        return 0;
    if ((p->status & (PS_ENABLE | PS_SUSPEND)) != PS_ENABLE)
        return 1;
    return usb_dev_suspended(udev);
}

/* Resume signaling (K) from the device on this port. A disabled port does
   not pass it (USB 1.1 s11.5.1.4, p. 242 / PDF 258).

   A suspended hub repeats it upstream (s11.1.2.2, p. 232 / PDF 248). The
   port goes Restart_S (from Suspended) or Restart_E (from Enabled) ->
   Transmit (s11.5.1.11, p. 243 / PDF 259); only the Restart_S path sets
   C_PORT_SUSPEND and clears PORT_SUSPEND (s11.16.2.6.2.3, p. 278 / PDF 294;
   s11.16.2.6.1.3, p. 275 / PDF 291).

   An awake hub resumes a suspended port itself: Suspended -> Resuming for
   20 ms (s11.5.1.9, p. 242-243 / PDF 258-259). An enabled port on an awake
   hub is not idle, so its device is not suspended and the request is
   dropped. */
static void
hub_resume(usb_dev_t *udev, usb_dev_t *dev)
{
    usb_hub_port_t *p = (usb_hub_port_t *) dev->port;

    if (!hub_port_idle(udev, dev) || !(p->status & PS_ENABLE))
        return;

    if (usb_dev_suspended(udev)) {
        if (p->status & PS_SUSPEND) {
            p->status &= ~PS_SUSPEND;
            p->change |= PC_SUSPEND;
        }
        usb_resume_upstream(udev);
    } else {
        hub_port_timer_start(p, HUB_TIMER_RESUME, HUB_RESUME_US);
    }
}

static const usb_dev_ops_t hub_ops = {
    .control    = hub_control,
    .data_in    = hub_data_in,
    .data_out   = NULL,
    .reset      = hub_reset,
    .configured = hub_configured,
    .route      = hub_route,
    .attach     = hub_attach,
    .detach     = hub_detach,
    .eof_babble = hub_eof_babble,
    .port_idle  = hub_port_idle,
    .resume     = hub_resume
};

static void *
usb_hub_init(const device_t *info)
{
    usb_hub_t *hub = (usb_hub_t *) calloc(1, sizeof(usb_hub_t));

    hub->usb.name      = "USB Hub";
    hub->usb.low_speed = 0;
    hub->usb.dev_desc  = hub_dev_desc;
    hub->usb.cfg_desc  = hub_cfg_desc;
    hub->usb.cfg_len   = sizeof(hub_cfg_desc);
    hub->usb.strings   = hub_strings;
    hub->usb.n_strings = 2;
    hub->usb.ops       = &hub_ops;
    hub->usb.priv      = hub;

    for (int i = 0; i < HUB_PORTS; i++) {
        hub->port[i].num = i + 1;
        timer_add(&hub->port[i].timer, hub_port_timer, &hub->port[i], 0);
    }

    usb_dev_init(&hub->usb);
    usb_dev_add(&hub->usb, info);

    return hub;
}

static void
usb_hub_close(void *priv)
{
    usb_hub_t *hub = (usb_hub_t *) priv;

    for (int i = 0; i < HUB_PORTS; i++) {
        usb_hub_port_t *p = &hub->port[i];

        timer_disable(&p->timer);
        if (p->dev != NULL) {
            p->dev->port   = NULL;
            p->dev->parent = NULL;
        }
    }

    usb_dev_remove(&hub->usb);
    free(hub);
}

const device_t usb_hub_4port_device = {
    .name          = "4-port USB 1.1 hub",
    .internal_name = "usb_hub_4port",
    .flags         = DEVICE_USB,
    .local         = 0,
    .init          = usb_hub_init,
    .close         = usb_hub_close,
    .reset         = NULL,
    .available     = NULL,
    .speed_changed = NULL,
    .force_redraw  = NULL,
    .config        = NULL
};

/* The hub list on the Ports settings page; entry 0 is none. */
static const device_t *const usb_hub_devices[] = {
    &device_none,
    &usb_hub_4port_device,
    NULL
};

int
usb_hub_get_ndev(void)
{
    return (int) (sizeof(usb_hub_devices) / sizeof(usb_hub_devices[0])) - 1;
}

const device_t *
usb_hub_get_device(int hub)
{
    return usb_hub_devices[hub];
}

const char *
usb_hub_get_internal_name(int hub)
{
    return device_get_internal_name(usb_hub_devices[hub]);
}

int
usb_hub_get_from_internal_name(const char *s)
{
    for (int c = 0; usb_hub_devices[c] != NULL; c++) {
        if (!strcmp(usb_hub_devices[c]->internal_name, s))
            return c;
    }

    return 0;
}

/* Called on hard reset before any other USB device attaches, so the hub
   takes a root port and later devices go to its ports once the root ports
   are full. */
void
usb_hub_reset(void)
{
    if (usb_hub_type != 0)
        device_add(usb_hub_devices[usb_hub_type]);
}
