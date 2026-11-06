/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          USB device core, written from chapters 5, 7, 8 and 9 of the
 *          Universal Serial Bus Specification Revision 1.1 (September 23,
 *          1998): handshakes, data toggles, endpoint halt, the default
 *          control pipe, the standard requests and remote wakeup. Where
 *          the answer depends on the host controller, it follows the
 *          Universal Host Controller Interface (UHCI) Design Guide
 *          Revision 1.1 (March 1996) and the Open Host Controller
 *          Interface Specification for USB Release 1.0a (September 14,
 *          1999). Classes plug in through usb_dev_ops_t.
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
#include <86box/usb_dev.h>

/* Stage of the transfer on the default control pipe. In CTL_STATUS_IN the
   request has run and the Status stage IN of a control write or no-data
   request gets a zero-length packet. The Status stage of a control read is
   an OUT taken in CTL_DATA_IN. CTL_STALL is a protocol stall that lasts
   until the next SETUP (USB 1.1 s8.5.2, p. 165 / PDF 181). */
enum {
    CTL_IDLE = 0,
    CTL_DATA_IN,
    CTL_DATA_OUT,
    CTL_STATUS_IN,
    CTL_STALL
};

#define REQ_GET_STATUS        0x00
#define REQ_CLEAR_FEATURE     0x01
#define REQ_SET_FEATURE       0x03
#define REQ_SET_ADDRESS       0x05
#define REQ_GET_DESCRIPTOR    0x06
#define REQ_GET_CONFIGURATION 0x08
#define REQ_SET_CONFIGURATION 0x09
#define REQ_GET_INTERFACE     0x0a
#define REQ_SET_INTERFACE     0x0b

#define FEAT_ENDPOINT_HALT        0
#define FEAT_DEVICE_REMOTE_WAKEUP 1

static void
usb_dev_reset_endpoints(usb_dev_t *dev, int with_ep0)
{
    for (int i = with_ep0 ? 0 : 1; i < 16; i++) {
        dev->ep_in[i].toggle   = 0;
        dev->ep_in[i].halt     = 0;
        dev->ep_in[i].pending  = 0;
        dev->ep_out[i].toggle  = 0;
        dev->ep_out[i].halt    = 0;
    }
}

static int
usb_dev_alt_exists(const usb_dev_t *dev, int intf, int alt)
{
    const uint8_t *d = dev->cfg_desc;

    for (int pos = 0; ((pos + 3) < dev->cfg_len) && (d[pos] != 0); pos += d[pos]) {
        if ((d[pos + 1] == 0x04) && (d[pos + 2] == intf) && (d[pos + 3] == alt))
            return 1;
    }

    return 0;
}

/* Each alternate setting has its own endpoint descriptors (USB 1.1 s9.6.3,
   p. 201 / PDF 217). Selecting one drops the interface's old endpoints,
   builds the new ones from the configuration descriptor, and puts them at
   their defaults: toggle DATA0 and Halt clear (s9.1.1.5, p. 178 / PDF 194;
   s9.4.5, p. 192 / PDF 208). Other interfaces' endpoints are left alone. */
static void
usb_dev_select_alt(usb_dev_t *dev, int intf, int alt)
{
    const uint8_t *d        = dev->cfg_desc;
    int            cur_intf = -1;
    int            cur_alt  = -1;

    for (int n = 1; n < 16; n++) {
        usb_ep_t *eps[2] = { &dev->ep_in[n], &dev->ep_out[n] };

        for (int i = 0; i < 2; i++) {
            if (eps[i]->intf == intf) {
                eps[i]->present = 0;
                eps[i]->intf    = 0xff;
            }
        }
    }

    for (int pos = 0; ((pos + 1) < dev->cfg_len) && (d[pos] != 0); pos += d[pos]) {
        if ((d[pos + 1] == 0x04) && ((pos + 3) < dev->cfg_len)) {
            cur_intf = d[pos + 2];
            cur_alt  = d[pos + 3];
        } else if ((d[pos + 1] == 0x05) && ((pos + 6) < dev->cfg_len) && (cur_intf == intf) && (cur_alt == alt) &&
                   (d[pos + 2] & 0x0f)) {
            usb_ep_t *ep = (d[pos + 2] & 0x80) ? &dev->ep_in[d[pos + 2] & 0x0f] : &dev->ep_out[d[pos + 2] & 0x0f];

            ep->present = 1;
            ep->intf    = intf;
            ep->type    = d[pos + 3] & 0x03;
            ep->mps     = (d[pos + 4] | (d[pos + 5] << 8)) & 0x07ff;
            if (ep->mps > sizeof(ep->pend_buf))
                ep->mps = sizeof(ep->pend_buf);
            ep->toggle  = 0;
            ep->halt    = 0;
            ep->pending = 0;
        }
    }

    dev->alt[intf] = alt;
}

/* Every interface in alternate setting 0, its default (USB 1.1 s9.6.3,
   p. 201 / PDF 217). */
static void
usb_dev_default_alts(usb_dev_t *dev)
{
    for (int i = 0; (i < dev->cfg_desc[4]) && (i < USB_DEV_INTERFACES); i++)
        usb_dev_select_alt(dev, i, 0);
}

void
usb_dev_init(usb_dev_t *dev)
{
    memset(dev->ep_in, 0x00, sizeof(dev->ep_in));
    memset(dev->ep_out, 0x00, sizeof(dev->ep_out));
    memset(dev->alt, 0x00, sizeof(dev->alt));

    for (int n = 0; n < 16; n++)
        dev->ep_in[n].intf = dev->ep_out[n].intf = 0xff;

    dev->ep_in[0].present  = dev->ep_out[0].present = 1;
    dev->ep_in[0].mps      = dev->ep_out[0].mps = dev->dev_desc[7];

    usb_dev_default_alts(dev);

    dev->powered_reset = 0;
    dev->addr          = 0;
    dev->set_addr      = 0;
    dev->config        = 0;
    dev->remote_wakeup = 0;
    dev->ctl_stage     = CTL_IDLE;
}

/* After a bus reset the device answers at the default address 0, is not
   configured, and has remote wakeup disabled (USB 1.1 s9.1.1.3-9.1.1.4,
   p. 178 / PDF 194; s9.2.1, p. 180 / PDF 196; s9.4.5, p. 191 / PDF 207). */
void
usb_dev_bus_reset(usb_dev_t *dev)
{
    dev->powered_reset = 1;
    dev->addr          = 0;
    dev->set_addr      = 0;
    dev->config        = 0;
    dev->remote_wakeup = 0;
    dev->ctl_stage     = CTL_IDLE;
    usb_dev_reset_endpoints(dev, 1);
    usb_dev_default_alts(dev);

    if (dev->ops->reset != NULL)
        dev->ops->reset(dev);
}

static int
usb_dev_interface_exists(const usb_dev_t *dev, int intf)
{
    return dev->config && (intf < dev->cfg_desc[4]) && (intf < USB_DEV_INTERFACES);
}

static usb_ep_t *
usb_dev_endpoint(usb_dev_t *dev, int windex)
{
    int n = windex & 0x0f;

    if ((n != 0) && !dev->config)
        return NULL;

    return (windex & 0x80) ? &dev->ep_in[n] : &dev->ep_out[n];
}

static int
usb_dev_string(const usb_dev_t *dev, int index, uint16_t langid, uint8_t *buf)
{
    const char *s;
    int         len;

    if (index == 0) {
        /* String 0 is the LANGID list (USB 1.1 s9.6.5, p. 204-205 / PDF
           220-221): US English, 0409h, only. */
        buf[0] = 4;
        buf[1] = 0x03;
        buf[2] = 0x09;
        buf[3] = 0x04;
        return 4;
    }

    /* wIndex is the LANGID (USB 1.1 s9.4.3, p. 189 / PDF 205). The strings
       exist in US English only, so another LANGID names a descriptor that
       does not exist: Request Error. */
    if ((index > dev->n_strings) || (langid != 0x0409))
        return USB_DATA_STALL;

    /* ASCII to UNICODE (UTF-16LE), no terminator (s9.6.5, p. 205 /
       PDF 221). bLength is one byte, so at most 126 characters fit. */
    s   = dev->strings[index - 1];
    len = (int) strlen(s);
    if (len > 126)
        len = 126;

    buf[0] = 2 + (len * 2);
    buf[1] = 0x03;
    for (int i = 0; i < len; i++) {
        buf[2 + (i * 2)] = (uint8_t) s[i];
        buf[3 + (i * 2)] = 0x00;
    }

    return buf[0];
}

/* Standard requests (USB 1.1 s9.4, p. 185 / PDF 201). Returns the Data-IN
   length, 0, or USB_DATA_STALL for a Request Error. */
static int
usb_dev_standard(usb_dev_t *dev, const uint8_t *setup, uint8_t *buf)
{
    uint8_t   rt     = setup[0];
    uint16_t  wvalue = setup[2] | (setup[3] << 8);
    uint16_t  windex = setup[4] | (setup[5] << 8);
    usb_ep_t *ep;

    switch (setup[1]) {
        case REQ_GET_STATUS:
            /* Device: D0 Self Powered (from the configuration's bmAttributes),
               D1 Remote Wakeup. Interface: all zero. Endpoint: D0 Halt. An
               interface, or an endpoint other than 0, exists only once the
               device is configured (USB 1.1 s9.4.5, p. 190-192 / PDF 206-208). */
            buf[1] = 0x00;
            switch (rt) {
                case 0x80:
                    buf[0] = ((dev->cfg_desc[7] & 0x40) ? 0x01 : 0x00) | (dev->remote_wakeup << 1);
                    return 2;
                case 0x81:
                    if (!usb_dev_interface_exists(dev, windex))
                        return USB_DATA_STALL;
                    buf[0] = 0x00;
                    return 2;
                case 0x82:
                    ep = usb_dev_endpoint(dev, windex);
                    if ((ep == NULL) || !ep->present)
                        return USB_DATA_STALL;
                    buf[0] = ep->halt;
                    return 2;
                default:
                    return USB_DATA_STALL;
            }

        case REQ_CLEAR_FEATURE:
        case REQ_SET_FEATURE:
            /* A feature the device does not have is a Request Error (USB 1.1
               s9.4.1, p. 188 / PDF 204; s9.4.9, p. 194 / PDF 210). Remote
               wakeup exists only if bmAttributes D5 declares it. */
            if ((rt == 0x00) && (wvalue == FEAT_DEVICE_REMOTE_WAKEUP)) {
                if (!(dev->cfg_desc[7] & 0x20))
                    return USB_DATA_STALL;
                dev->remote_wakeup = (setup[1] == REQ_SET_FEATURE);
                return 0;
            }
            if ((rt == 0x02) && (wvalue == FEAT_ENDPOINT_HALT)) {
                ep = usb_dev_endpoint(dev, windex);
                /* An isochronous endpoint never halts (USB 1.1 s5.6.5, p. 43
                   / PDF 59); Halt is for interrupt and bulk endpoints and,
                   optionally, the control pipe (s9.4.5, p. 192 / PDF 208). */
                if ((ep == NULL) || !ep->present || (ep->type == 1))
                    return USB_DATA_STALL;
                ep->halt = (setup[1] == REQ_SET_FEATURE);
                /* EP0 IN and OUT are one control pipe with one Halt feature;
                   either direction bit names it (s9.3.4, p. 184 / PDF 200). */
                if ((windex & 0x0f) == 0)
                    dev->ep_in[0].halt = dev->ep_out[0].halt = ep->halt;
                /* CLEAR_FEATURE(ENDPOINT_HALT) always resets the data toggle
                   to DATA0 (s9.4.5, p. 192 / PDF 208). The control pipe's
                   toggles are set by SETUP instead: this request's own Status
                   stage is still DATA1 (s8.5.2, p. 165 / PDF 181). */
                if (!ep->halt && ((windex & 0x0f) != 0)) {
                    ep->toggle  = 0;
                    ep->pending = 0;
                }
                return 0;
            }
            return USB_DATA_STALL;

        case REQ_SET_ADDRESS:
            if ((rt != 0x00) || (wvalue > 127))
                return USB_DATA_STALL;
            /* Takes effect after the Status stage (USB 1.1 s9.4.6, p. 192 /
               PDF 208); usb_dev_ctl_in_acked() applies it. An address over
               127 has unspecified behavior and is refused. */
            dev->new_addr = wvalue;
            dev->set_addr = 1;
            return 0;

        case REQ_GET_DESCRIPTOR:
            if (rt != 0x80)
                break;
            switch (wvalue >> 8) {
                case 0x01:
                    /* One device descriptor and one configuration: any
                       other index names a descriptor that does not exist, a
                       Request Error (USB 1.1 s9.4.3, p. 189 / PDF 205). */
                    if ((wvalue & 0xff) != 0)
                        return USB_DATA_STALL;
                    memcpy(buf, dev->dev_desc, 18);
                    return 18;
                case 0x02:
                    if ((wvalue & 0xff) != 0)
                        return USB_DATA_STALL;
                    memcpy(buf, dev->cfg_desc, dev->cfg_len);
                    return dev->cfg_len;
                case 0x03:
                    return usb_dev_string(dev, wvalue & 0xff, windex, buf);
                default:
                    return USB_DATA_STALL;
            }

        case REQ_GET_CONFIGURATION:
            if (rt != 0x80)
                return USB_DATA_STALL;
            buf[0] = dev->config;
            return 1;

        case REQ_SET_CONFIGURATION:
            if ((rt != 0x00) || (dev->addr == 0))
                return USB_DATA_STALL;
            if ((wvalue & 0xff) == 0)
                dev->config = 0;
            else if ((wvalue & 0xff) == dev->cfg_desc[5])
                dev->config = wvalue & 0xff;
            else
                return USB_DATA_STALL;
            /* Configuration 0 returns to the Address state; any value but 0
               or the one bConfigurationValue is a Request Error (USB 1.1
               s9.4.7, p. 193 / PDF 209). In the Default state (address 0)
               the behavior is unspecified and the request is refused. On
               success every endpoint's toggle
               and Halt are reset and every interface goes to alternate
               setting 0 (s9.1.1.5, p. 178 / PDF 194; s9.4.5, p. 192 /
               PDF 208; s9.6.3, p. 201 / PDF 217). */
            usb_dev_reset_endpoints(dev, 0);
            usb_dev_default_alts(dev);
            if (dev->ops->configured != NULL)
                dev->ops->configured(dev, dev->config);
            return 0;

        case REQ_GET_INTERFACE:
            if ((rt != 0x81) || !usb_dev_interface_exists(dev, windex))
                return USB_DATA_STALL;
            buf[0] = dev->alt[windex];
            return 1;

        case REQ_SET_INTERFACE:
            if ((rt != 0x01) || !usb_dev_interface_exists(dev, windex) || (wvalue > 0xff) ||
                !usb_dev_alt_exists(dev, windex, wvalue))
                return USB_DATA_STALL;
            usb_dev_select_alt(dev, windex, wvalue);
            if (dev->ops->set_interface != NULL)
                dev->ops->set_interface(dev, windex, wvalue);
            return 0;

        default:
            break;
    }

    /* Any other standard request addressed to an interface goes to the
       class, e.g. GET_DESCRIPTOR for a HID class descriptor. */
    if ((rt & 0x1f) == 0x01)
        return dev->ops->control(dev, setup, buf);

    return USB_DATA_STALL;
}

static int
usb_dev_request(usb_dev_t *dev, uint8_t *buf)
{
    if ((dev->ctl_setup[0] & 0x60) == 0x00)
        return usb_dev_standard(dev, dev->ctl_setup, buf);

    return dev->ops->control(dev, dev->ctl_setup, buf);
}

static void
usb_dev_setup(usb_dev_t *dev)
{
    uint16_t wlength = dev->ctl_setup[6] | (dev->ctl_setup[7] << 8);
    int      n;

    dev->ctl_pos  = 0;
    dev->ctl_len  = 0;
    dev->ctl_zlp  = 0;
    dev->set_addr = 0;

    /* A halted control pipe stalls the Data and Status stages of standard
       requests other than GET_STATUS, CLEAR_FEATURE and SET_FEATURE (USB 1.1
       s9.4.5, p. 192 / PDF 208). */
    if (dev->ep_in[0].halt && ((dev->ctl_setup[0] & 0x60) == 0x00) &&
        (dev->ctl_setup[1] != REQ_GET_STATUS) && (dev->ctl_setup[1] != REQ_CLEAR_FEATURE) &&
        (dev->ctl_setup[1] != REQ_SET_FEATURE)) {
        dev->ctl_stage = CTL_STALL;
        return;
    }

    /* wLength 0 means no Data stage whatever the direction bit says (USB 1.1
       s9.3.1, p. 183 / PDF 199). A control read runs the request now; a
       control write runs it at the Status stage, once its data is in. */
    if ((dev->ctl_setup[0] & 0x80) && (wlength != 0)) {
        n = usb_dev_request(dev, dev->ctl_buf);
        if (n < 0) {
            dev->ctl_stage = CTL_STALL;
            return;
        }
        /* Never more than wLength (s9.3.5, p. 184 / PDF 200; s9.4.3,
           p. 189 / PDF 205). An answer shorter than wLength that is a whole
           number of packets ends with a zero-length packet (s8.5.2.2,
           p. 166 / PDF 182). */
        dev->ctl_len   = (n < wlength) ? n : wlength;
        dev->ctl_zlp   = (dev->ctl_len < wlength) && ((dev->ctl_len % dev->ep_in[0].mps) == 0);
        dev->ctl_stage = CTL_DATA_IN;
    } else if (wlength == 0) {
        n              = usb_dev_request(dev, dev->ctl_buf);
        dev->ctl_stage = (n < 0) ? CTL_STALL : CTL_STATUS_IN;
    } else if (wlength > USB_CTRL_BUF) {
        dev->ctl_stage = CTL_STALL;
    } else {
        dev->ctl_len   = wlength;
        dev->ctl_stage = CTL_DATA_OUT;
    }
}

/* The next EP0 IN packet. It is not consumed here: usb_dev_ctl_in_acked()
   does that once the host ACKs it. */
static int
usb_dev_ctl_in(usb_dev_t *dev, uint8_t *buf, int mps)
{
    int n;

    switch (dev->ctl_stage) {
        case CTL_DATA_IN:
            n = dev->ctl_len - dev->ctl_pos;
            if (n > 0) {
                if (n > mps)
                    n = mps;
                memcpy(buf, dev->ctl_buf + dev->ctl_pos, n);
                return n;
            }
            /* An IN past the end of the data, after the short packet or
               zero-length packet that ended the Data stage: STALL, as for a
               request for more data than the Setup stage gave (USB 1.1
               s8.5.2.1, p. 166 / PDF 182). */
            return dev->ctl_zlp ? 0 : USB_DATA_STALL;

        case CTL_DATA_OUT:
            /* Status stage of a control write: the request runs now. A Data
               stage shorter than wLength is refused with STALL. */
            if (dev->ctl_pos != dev->ctl_len) {
                dev->ctl_stage = CTL_STALL;
                return USB_DATA_STALL;
            }
            if (usb_dev_request(dev, dev->ctl_buf) < 0) {
                dev->ctl_stage = CTL_STALL;
                return USB_DATA_STALL;
            }
            dev->ctl_stage = CTL_STATUS_IN;
            return 0;

        case CTL_STATUS_IN:
            return 0;

        default:
            return USB_DATA_STALL;
    }
}

static void
usb_dev_ctl_in_acked(usb_dev_t *dev, int len)
{
    if (dev->ctl_stage == CTL_DATA_IN) {
        if (len > 0)
            dev->ctl_pos += len;
        else
            dev->ctl_zlp = 0;
    } else if (dev->ctl_stage == CTL_STATUS_IN) {
        if (dev->set_addr)
            dev->addr = dev->new_addr;
        dev->set_addr  = 0;
        dev->ctl_stage = CTL_IDLE;
    }
}

static int
usb_dev_ctl_out(usb_dev_t *dev, const usb_packet_t *p)
{
    usb_ep_t *ep = &dev->ep_out[0];

    /* A packet with the wrong toggle is a retransmission: ACK and discard it
       (USB 1.1 s8.6.4, Figure 8-18, p. 170 / PDF 186). This covers a Status
       OUT resent after its ACK was lost, when the transfer has already ended
       here. */
    if ((dev->ctl_stage != CTL_STALL) && (p->toggle != ep->toggle))
        return USB_RET_ACK;

    switch (dev->ctl_stage) {
        case CTL_DATA_OUT:
            if ((dev->ctl_pos + p->len) > dev->ctl_len) {
                dev->ctl_stage = CTL_STALL;
                return USB_RET_STALL;
            }
            memcpy(dev->ctl_buf + dev->ctl_pos, p->data, p->len);
            dev->ctl_pos += p->len;
            ep->toggle ^= 1;
            return USB_RET_ACK;

        case CTL_DATA_IN:
            /* Status stage of a control read; the host should send a
               zero-length packet, but any length is accepted (USB 1.1
               s8.5.2.1, p. 166 / PDF 182). */
            ep->toggle ^= 1;
            dev->ctl_stage = CTL_IDLE;
            return USB_RET_ACK;

        default:
            dev->ctl_stage = CTL_STALL;
            return USB_RET_STALL;
    }
}

static int
usb_dev_transact(usb_dev_t *dev, usb_packet_t *p)
{
    int       n  = p->ep & 0x0f;
    usb_ep_t *ep;
    int       ret;

    /* Silent until the first bus reset (USB 1.1 s9.1.1.3, p. 178 / PDF 194). */
    if (!dev->powered_reset)
        return USB_RET_NO_RESPONSE;

    switch (p->pid) {
        case USB_PID_SETUP:
            /* SETUP is always ACKed, never NAKed or STALLed, and a
               non-control endpoint ignores it (USB 1.1 s8.4.5.4, p. 162 /
               PDF 178). It drops any IN packet still waiting for an ACK
               from the transfer it replaces. A Setup packet is always 8
               bytes (s9.3, p. 183 / PDF 199); another length is ACKed but
               the request fails at its next stage. */
            if (n != 0)
                return USB_RET_NO_RESPONSE;
            dev->ep_in[0].pending = 0;
            if (p->len != 8) {
                dev->ctl_stage = CTL_STALL;
                return USB_RET_ACK;
            }
            memcpy(dev->ctl_setup, p->data, 8);
            /* SETUP leaves both sequence bits at 1 for the next stage (USB
               1.1 s8.6.1, p. 169 / PDF 185). */
            dev->ep_in[0].toggle  = 1;
            dev->ep_out[0].toggle = 1;
            usb_dev_setup(dev);
            return USB_RET_ACK;

        case USB_PID_OUT:
            ep = &dev->ep_out[n];
            if (!ep->present || ((n != 0) && !dev->config))
                return USB_RET_NO_RESPONSE;
            if (n == 0)
                return usb_dev_ctl_out(dev, p);
            /* Order of precedence: Halt, then toggle, then the class's
               answer (USB 1.1 s8.4.5.3, Table 8-4, p. 162 / PDF 178). */
            if (ep->halt)
                return USB_RET_STALL;
            /* Isochronous: no data toggle and no handshake (USB 1.1 s8.5.4,
               p. 168 / PDF 184). USB_RET_ACK here only means "taken". */
            if (ep->type == 1) {
                if (dev->ops->data_out != NULL)
                    dev->ops->data_out(dev, n, p->data, p->len);
                return USB_RET_ACK;
            }
            /* Wrong toggle: a retransmission, ACKed and discarded. */
            if (p->toggle != ep->toggle)
                return USB_RET_ACK;
            ret = (dev->ops->data_out != NULL) ? dev->ops->data_out(dev, n, p->data, p->len) : USB_DATA_STALL;
            if (ret == USB_DATA_NAK)
                return USB_RET_NAK;
            /* A STALL from the class sets Halt, which holds until the host
               clears it (s8.4.4, p. 161 / PDF 177). */
            if (ret == USB_DATA_STALL) {
                ep->halt = 1;
                return USB_RET_STALL;
            }
            ep->toggle ^= 1;
            return USB_RET_ACK;

        case USB_PID_IN:
            ep         = &dev->ep_in[n];
            p->bus_len = 0;
            if (!ep->present || ((n != 0) && !dev->config))
                return USB_RET_NO_RESPONSE;
            /* EP0's Halt acts per request, in usb_dev_setup(). */
            if (ep->halt && (n != 0))
                return USB_RET_STALL;

            /* Isochronous: no handshake and no retry (USB 1.1 s8.5.4,
               p. 168 / PDF 184), so each packet is sent once, and with
               nothing to send the device does not answer. The device sends
               up to its own wMaxPacketSize whatever the host's buffer. Data
               past the buffer is babble to a UHCI (UHCI 1.1 s3.2.3, p. 24 /
               PDF 30) and a data overrun to an OHCI (OpenHCI 1.0a
               s4.3.1.3.6.2, p. 24 / PDF 38). */
            if (ep->type == 1) {
                uint8_t iso_buf[1023];
                int     max = ((ep->mps > 0) && (ep->mps <= 1023)) ? ep->mps : 1023;

                ret = (dev->ops->data_in != NULL) ? dev->ops->data_in(dev, n, iso_buf, max) : USB_DATA_NAK;
                if (ret < 0)
                    return USB_RET_NO_RESPONSE;
                p->bus_len = ret;
                p->actual  = (ret > p->len) ? p->len : ret;
                memcpy(p->data, iso_buf, p->actual);
                if (ret > p->len)
                    return p->ack_overrun ? USB_RET_OVERRUN : USB_RET_BABBLE;
                return USB_RET_ACK;
            }

            /* A packet the host has not ACKed stays pending and is resent
               unchanged with the same toggle (USB 1.1 s8.6.3-8.6.4,
               p. 170 / PDF 186). */
            if (!ep->pending) {
                if (n == 0)
                    ret = usb_dev_ctl_in(dev, ep->pend_buf, ep->mps);
                else if (dev->ops->data_in != NULL)
                    ret = dev->ops->data_in(dev, n, ep->pend_buf, ep->mps);
                else
                    ret = USB_DATA_NAK;

                if (ret == USB_DATA_NAK)
                    return USB_RET_NAK;
                if (ret == USB_DATA_STALL) {
                    /* On an interrupt or bulk endpoint a STALL sets Halt. On
                       EP0 it is a protocol stall that holds until the next
                       SETUP (USB 1.1 s8.4.4, p. 161 / PDF 177; s8.5.2,
                       p. 165 / PDF 181). */
                    if (n != 0)
                        ep->halt = 1;
                    else
                        dev->ctl_stage = CTL_STALL;
                    return USB_RET_STALL;
                }
                ep->pend_len = ret;
                ep->pending  = 1;
            }
            p->bus_len = ep->pend_len;

            /* Data the host cannot accept gets no handshake (USB 1.1
               s8.4.5.2, p. 161-162 / PDF 177-178), so the packet stays
               pending and is resent: a babbled packet, or any packet cut off
               at the end of the frame. An OHCI ACKs an overrun instead
               (ack_overrun). */
            if (p->no_ack || ((ep->pend_len > p->len) && !p->ack_overrun)) {
                p->actual = (ep->pend_len > p->len) ? p->len : ep->pend_len;
                memcpy(p->data, ep->pend_buf, p->actual);
                return USB_RET_BABBLE;
            }

            p->actual = (ep->pend_len > p->len) ? p->len : ep->pend_len;
            memcpy(p->data, ep->pend_buf, p->actual);

            /* The host ACKs both a good packet and one with the wrong toggle,
               which it discards (USB 1.1 s8.6.4, p. 170 / PDF 186). A toggle
               mismatch is reported ahead of an overrun: an OHCI checks for
               overrun only on a packet received without a transmission
               error, and counts a mismatch as one (OpenHCI 1.0a
               s4.3.1.3.6, p. 24 / PDF 38). */
            if (ep->toggle != p->toggle)
                ret = USB_RET_TOGGLE_MISMATCH;
            else
                ret = (ep->pend_len > p->len) ? USB_RET_OVERRUN : USB_RET_ACK;
            ep->pending = 0;
            ep->toggle ^= 1;
            if (n == 0)
                usb_dev_ctl_in_acked(dev, ep->pend_len);
            return ret;

        default:
            return USB_RET_NO_RESPONSE;
    }
}

/* The IN path of usb_dev_transact() up to the point where the packet is
   pending, for a host controller that needs the packet's length before the
   transaction runs, to time it on the wire. A NAK leaves nothing pending; a
   STALL sets Halt or the control stall as the transaction would. */
int
usb_dev_in_peek(usb_dev_t *dev, int n, const uint8_t **data, int *toggle)
{
    usb_ep_t *ep;
    int       ret;

    n &= 0x0f;
    ep = &dev->ep_in[n];
    if (!dev->powered_reset || !ep->present || ((n != 0) && !dev->config) || (ep->halt && (n != 0)) || (ep->type == 1))
        return -1;

    if (!ep->pending) {
        if (n == 0)
            ret = usb_dev_ctl_in(dev, ep->pend_buf, ep->mps);
        else if (dev->ops->data_in != NULL)
            ret = dev->ops->data_in(dev, n, ep->pend_buf, ep->mps);
        else
            ret = USB_DATA_NAK;

        if (ret == USB_DATA_NAK)
            return -1;
        if (ret == USB_DATA_STALL) {
            if (n != 0)
                ep->halt = 1;
            else
                dev->ctl_stage = CTL_STALL;
            return -1;
        }
        ep->pend_len = ret;
        ep->pending  = 1;
    }

    *data   = ep->pend_buf;
    *toggle = ep->toggle;
    return ep->pend_len;
}

/* Only a suspended device whose remote wakeup was enabled with
   SET_FEATURE(DEVICE_REMOTE_WAKEUP) may drive resume signaling (USB 1.1
   s7.1.7.5, p. 123 / PDF 139; s9.2.5.2, p. 181 / PDF 197). The 5 ms of
   idle bus it must wait first and the 1-15 ms it drives K are not timed:
   the port or hub it sits on latches the resume at once. */
int
usb_dev_remote_wakeup(usb_dev_t *dev)
{
    if (!dev->remote_wakeup || !usb_dev_suspended(dev))
        return 0;

    usb_resume_upstream(dev);
    return 1;
}

int
usb_dev_packet(usb_dev_t *dev, usb_packet_t *p)
{
    return usb_dev_transact(dev, p);
}
