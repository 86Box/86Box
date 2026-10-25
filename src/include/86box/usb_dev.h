/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          Definitions for the USB device core: the device side of one
 *          bus transaction, the default control pipe and the standard
 *          requests, written from the Universal Serial Bus Specification
 *          Revision 1.1 (September 23, 1998), with the host-controller
 *          differences from the Universal Host Controller Interface
 *          (UHCI) Design Guide Revision 1.1 (March 1996) and the Open
 *          Host Controller Interface Specification for USB Release 1.0a
 *          (September 14, 1999).
 *
 * Authors: skiretic, <skiretic@proton.me>
 *
 *          Copyright 2026 skiretic.
 */

#ifndef EMU_USB_DEV_H
#define EMU_USB_DEV_H

#ifdef __cplusplus
extern "C" {
#endif

/* Token PID bytes: packet type in bits 0-3, its complement in bits 4-7
   (USB 1.1 s8.3.1, Table 8-1, p. 155-156 / PDF 171-172). */
#define USB_PID_OUT   0xe1
#define USB_PID_IN    0x69
#define USB_PID_SETUP 0x2d

/* Outcome of one transaction, as the host controller sees it. */
enum {
    USB_RET_ACK = 0,         /* OUT/SETUP accepted, or IN data received and ACKed */
    USB_RET_NAK,
    USB_RET_STALL,
    USB_RET_NO_RESPONSE,     /* no device answered: the host times out */
    USB_RET_BABBLE,          /* IN data longer than the host allowed, or cut off at EOF; not ACKed */
    USB_RET_TOGGLE_MISMATCH, /* IN data with the wrong DATA PID: ACKed, discarded */
    USB_RET_OVERRUN          /* IN data longer than the host allowed, ACKed: the excess is lost */
};

/* Class hooks return these to NAK or STALL the transaction. */
#define USB_DATA_NAK   (-1)
#define USB_DATA_STALL (-2)

/* Largest Data stage the default control pipe takes; a control write with
   a longer wLength is STALLed. */
#define USB_CTRL_BUF 1024

typedef struct usb_dev_t usb_dev_t;

typedef struct usb_packet_t {
    uint8_t  pid;
    uint8_t  ep;
    uint8_t  toggle; /* OUT/SETUP: DATA PID sent; IN: DATA PID the host expects */
    uint8_t *data;
    int      len;    /* OUT/SETUP: bytes sent; IN: bytes the host accepts */
    int      actual; /* IN: bytes received */
    /* IN: bytes the device put on the bus, the whole packet when it ran
       past len (OVERRUN, BABBLE); 0 when it sent no data. */
    int      bus_len;
    /* IN: the host ACKs data past len (OVERRUN) instead of leaving it
       unanswered (BABBLE). An OHCI does: on a data overrun the toggle has
       advanced (OpenHCI 1.0a s4.3.1.3.6.2, p. 24 / PDF 38). A UHCI does
       not: data past MaxLen is babble (UHCI 1.1 s3.2.3, p. 24 / PDF 30). */
    uint8_t  ack_overrun;
    /* IN: the host answers nothing the device sends, even a packet within
       len (a UHCI cutting the packet off at the end of the frame), so the
       device keeps the packet pending (BABBLE). */
    uint8_t  no_ack;
    /* IN: len was cut short at a hub's EOF1 (a UHCI behind a hub). There
       the hub sends an EOP upstream (USB 1.1 s11.2.2, p. 234 / PDF 250),
       so the host sees the truncated packet as a CRC error, not babble. The
       device acts the same; only the debug capture differs. */
    uint8_t  hub_cut;
} usb_packet_t;

typedef struct usb_dev_ops_t {
    /* Class and vendor requests, and standard requests to an interface that
       the core does not handle. Returns the Data-IN length, 0, or
       USB_DATA_STALL. */
    int (*control)(usb_dev_t *dev, const uint8_t *setup, uint8_t *data);
    /* Next packet for IN endpoint ep, at most maxlen bytes, or USB_DATA_NAK/STALL. */
    int (*data_in)(usb_dev_t *dev, int ep, uint8_t *buf, int maxlen);
    /* OUT data for endpoint ep. Returns 0, USB_DATA_NAK or USB_DATA_STALL. */
    int (*data_out)(usb_dev_t *dev, int ep, const uint8_t *buf, int len);
    void (*reset)(usb_dev_t *dev);
    void (*configured)(usb_dev_t *dev, int config);
    /* SET_INTERFACE selected alternate setting alt of interface intf. */
    void (*set_interface)(usb_dev_t *dev, int intf, int alt);
    /* Hubs only. route: the device behind the hub that a token for addr at
       this speed reaches, or NULL. attach: put udev on a free downstream
       port; returns 1 if it did. detach: udev leaves its port on this hub. */
    usb_dev_t *(*route)(usb_dev_t *dev, int addr, int low_speed);
    int (*attach)(usb_dev_t *dev, usb_dev_t *udev);
    void (*detach)(usb_dev_t *dev, usb_dev_t *udev);
    /* Hubs only: udev, on a port of this hub, was still sending at EOF2,
       where the hub disables the port as a babbler (USB 1.1 s11.2.2,
       p. 234 / PDF 250). */
    void (*eof_babble)(usb_dev_t *dev, usb_dev_t *udev);
    /* Hubs only. port_idle: no traffic, reset or resume reaches udev's port,
       so udev is suspended. resume: udev drives resume signaling (K). */
    int (*port_idle)(const usb_dev_t *dev, const usb_dev_t *udev);
    void (*resume)(usb_dev_t *dev, usb_dev_t *udev);
} usb_dev_ops_t;

#define USB_DEV_INTERFACES 16

typedef struct usb_ep_t {
    uint16_t mps;
    uint8_t  type;      /* bmAttributes transfer type: 0 control, 1 iso, 2 bulk, 3 interrupt */
    uint8_t  intf;      /* interface it belongs to, 0xff = EP0 or none */
    uint8_t  present;
    uint8_t  toggle;
    uint8_t  halt;
    uint8_t  pending;   /* IN packet sent but not yet ACKed: resent unchanged */
    int      pend_len;
    /* 64 bytes is the largest control, interrupt or bulk packet (USB 1.1
       s5.5.3, p. 37 / PDF 53; s5.7.3, p. 43 / PDF 59; s5.8.3, p. 47 /
       PDF 63). Isochronous IN does not use this buffer. */
    uint8_t  pend_buf[64];
} usb_ep_t;

struct usb_dev_t {
    const char           *name;
    int                   low_speed;
    const uint8_t        *dev_desc;
    const uint8_t        *cfg_desc;
    int                   cfg_len;
    const char *const    *strings; /* string descriptor 1..n_strings */
    int                   n_strings;
    const usb_dev_ops_t  *ops;
    void                 *priv;

    /* Device state (USB 1.1 chapter 9). */
    uint8_t               powered_reset; /* has seen a bus reset since attach */
    uint8_t               addr;
    uint8_t               new_addr;      /* SET_ADDRESS value, applied after its Status stage */
    uint8_t               set_addr;      /* new_addr is waiting */
    uint8_t               config;
    uint8_t               remote_wakeup;
    uint8_t               alt[USB_DEV_INTERFACES]; /* alternate setting of each interface */

    usb_ep_t              ep_in[16];
    usb_ep_t              ep_out[16];

    /* Default control pipe. */
    uint8_t               ctl_stage;
    uint8_t               ctl_setup[8];
    uint8_t               ctl_buf[USB_CTRL_BUF];
    int                   ctl_len; /* Data stage length */
    int                   ctl_pos; /* bytes of the Data stage done so far */
    int                   ctl_zlp; /* a zero-length packet still ends the Data stage */

    /* Port the device sits on, set by the host controller or the hub;
       parent is that hub, NULL on a root hub port. */
    void                 *port;
    usb_dev_t            *parent;

    /* The port the device was last attached to, and that port's hub (NULL
       for a root hub port). Plugging the device back in tries this port
       first. */
    void                 *home_port;
    usb_dev_t            *home_parent;
};

extern void usb_dev_init(usb_dev_t *dev);
extern void usb_dev_bus_reset(usb_dev_t *dev);
extern int  usb_dev_packet(usb_dev_t *dev, usb_packet_t *p);
/* The data packet a non-isochronous IN endpoint will answer its next token
   with, made pending so that the transaction sends exactly this packet.
   Returns its length and sets data and toggle, or returns -1 when the
   answer is not a data packet (NAK, STALL, no response). */
extern int  usb_dev_in_peek(usb_dev_t *dev, int ep, const uint8_t **data, int *toggle);
/* Remote wakeup: a suspended device with DEVICE_REMOTE_WAKEUP set drives
   resume signaling upstream. Returns 1 if it did. Emulation thread only. */
extern int  usb_dev_remote_wakeup(usb_dev_t *dev);

/* Attach to the device's last port if it is free, else to the first free
   root-hub port of any USB host controller, else to the first free port of
   a hub on one. */
extern int  usb_attach(usb_dev_t *dev);
extern void usb_detach(usb_dev_t *dev);

/* A device the machine has, called from its init and close. usb_dev_add
   gives it the hot-plug slot named by info's internal name, if there is
   one and no other device holds it, and attaches it unless it was left
   unplugged. usb_dev_remove detaches it and frees the slot. */
struct _device_;
extern void usb_dev_add(usb_dev_t *dev, const struct _device_ *info);
extern void usb_dev_remove(usb_dev_t *dev);
/* Whether nothing reaches dev from upstream (idle bus), so that it is
   suspended. */
extern int  usb_dev_suspended(const usb_dev_t *dev);
/* Resume signaling (K) from dev reaches the port it sits on. */
extern void usb_resume_upstream(usb_dev_t *dev);

#ifdef __cplusplus
}
#endif

#endif /*EMU_USB_DEV_H*/
