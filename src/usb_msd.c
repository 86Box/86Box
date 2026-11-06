/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          USB mass storage: a bridge to one 86Box SCSI bus whose drives
 *          are its logical units.
 *
 *          Written from:
 *          - Universal Serial Bus Mass Storage Class Bulk-Only Transport,
 *            Revision 1.0, September 31, 1999 ("BOT 1.0"; the date is
 *            as printed).
 *          - Universal Serial Bus Mass Storage Class Specification
 *            Overview, Revision 1.4, February 19, 2010.
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
#include <86box/mem.h>
#include <86box/scsi.h>
#include <86box/scsi_device.h>
#include <86box/usb.h>
#include <86box/usb_dev.h>
#include <86box/plat_unused.h>

#define MSD_EP_IN  1
#define MSD_EP_OUT 2

#define CBW_SIGNATURE 0x43425355
#define CSW_SIGNATURE 0x53425355

#define CSW_PASSED      0x00
#define CSW_FAILED      0x01
#define CSW_PHASE_ERROR 0x02

#define MSD_RESET       0xff
#define MSD_GET_MAX_LUN 0xfe

enum {
    MSD_CBW = 0,  /* waiting for a CBW */
    MSD_DATA_IN,  /* sending data */
    MSD_DATA_OUT, /* taking the host's data */
    MSD_IN_STALL, /* data ended short of the host's length: Bulk-In STALLs, then the CSW */
    MSD_CSW,      /* the CSW is next on Bulk-In */
    MSD_INVALID   /* after an invalid CBW: both pipes stall; the Bulk-Only Mass
                     Storage Reset of a Reset Recovery ends this state */
};

static const uint8_t usb_msd_dev_desc[18] = {
    18, 0x01,   /* bLength, DEVICE */
    0x10, 0x01, /* bcdUSB 1.10 */
    0x00, 0x00, 0x00, /* class in the interface (BOT 1.0 s4.1, p. 9 / PDF 9) */
    64,         /* bMaxPacketSize0 */
    0x00, 0x00, /* idVendor */
    0x03, 0x00, /* idProduct */
    0x00, 0x01, /* bcdDevice */
    1, 2, 3,    /* iManufacturer, iProduct, iSerialNumber */
    1           /* bNumConfigurations */
};

#define USB_MSD_CFG_LEN (9 + 9 + 7 + 7)

static const uint8_t usb_msd_cfg_desc[USB_MSD_CFG_LEN] = {
    /* Configuration */
    9, 0x02, USB_MSD_CFG_LEN, 0x00, 1, 1, 0,
    0x80, /* bus powered */
    50,   /* 100 mA */
    /* Interface 0: mass storage, SCSI transparent command set (06h), Bulk-Only
       (50h) (Overview 1.4 Tables 1 and 2, p. 3-4 / PDF 11-12; BOT 1.0 s4.3,
       p. 11 / PDF 11) */
    9, 0x04, 0, 0, 2, 0x08, 0x06, 0x50, 0,
    /* Bulk IN EP1 and bulk OUT EP2, 64 bytes (BOT 1.0 s4.4, p. 11 / PDF 11) */
    7, 0x05, 0x80 | MSD_EP_IN, 0x02, 64, 0x00, 0,
    7, 0x05, MSD_EP_OUT, 0x02, 64, 0x00, 0
};

typedef struct usb_msd_t {
    usb_dev_t      usb;
    uint8_t        bus; /* SCSI bus, 0xff = none left */
    int            state;
    scsi_device_t *sd;       /* target of the command in progress */
    int            open;     /* the target's command still waits for phase 1 */
    uint32_t       tag;
    uint32_t       host_len; /* dCBWDataTransferLength */
    uint32_t       host_pos; /* bytes moved in the data stage */
    uint32_t       dev_len;  /* bytes the target sends or takes in it */
    uint32_t       residue;
    uint8_t        status;   /* bCSWStatus */
    char           serial[16];
    const char    *strings[3];
} usb_msd_t;

static uint32_t
usb_msd_le32(const uint8_t *p)
{
    return p[0] | (p[1] << 8) | (p[2] << 16) | ((uint32_t) p[3] << 24);
}

static void
usb_msd_put_le32(uint8_t *p, uint32_t v)
{
    p[0] = v & 0xff;
    p[1] = (v >> 8) & 0xff;
    p[2] = (v >> 16) & 0xff;
    p[3] = v >> 24;
}

/* The drives on the bus are LUN 0..n-1 in SCSI ID order, so the LUNs are
   contiguous from 0 as BOT 1.0 s3.2 requires (p. 7 / PDF 7). */
static scsi_device_t *
usb_msd_lun(const usb_msd_t *dev, int lun)
{
    if (dev->bus >= SCSI_BUS_MAX)
        return NULL;

    for (int id = 0; id < SCSI_ID_MAX; id++) {
        scsi_device_t *sd = &scsi_devices[dev->bus][id];

        if (scsi_device_present(sd) && (sd->sc != NULL) && (lun-- == 0))
            return sd;
    }

    return NULL;
}

static int
usb_msd_n_luns(const usb_msd_t *dev)
{
    int n = 0;

    while ((n < 16) && (usb_msd_lun(dev, n) != NULL))
        n++;

    return n;
}

/* End the target's command: phase 1 runs it to completion after a full
   data stage; otherwise it is stopped. */
static void
usb_msd_close_command(usb_msd_t *dev, int complete)
{
    if (!dev->open)
        return;

    dev->open = 0;
    if (complete) {
        scsi_device_command_phase1(dev->sd);
        if ((dev->status != CSW_PHASE_ERROR) && (dev->sd->status != SCSI_STATUS_OK))
            dev->status = CSW_FAILED;
    } else
        scsi_device_command_stop(dev->sd);
}

static void
usb_msd_abort(usb_msd_t *dev)
{
    usb_msd_close_command(dev, 0);
    dev->state = MSD_CBW;
}

static uint32_t
usb_msd_buf_len(const usb_msd_t *dev)
{
    if ((dev->sd == NULL) || (dev->sd->sc == NULL) || (dev->sd->sc->temp_buffer == NULL))
        return 0;

    return (uint32_t) dev->sd->sc->temp_buffer_sz;
}

/* Match the host's expectation (direction bit and dCBWDataTransferLength)
   against the target's intent, as in the thirteen cases of BOT 1.0 s6.7
   (Table 6.1, p. 19 / PDF 19; cases in s6.7.1-s6.7.3, p. 19-22 / PDF 19-22).
   Hn/Hi/Ho: the host expects no data, data in, data out; Dn/Di/Do: the
   same for the target. A Phase Error stops the target's command. */
static void
usb_msd_plan(usb_msd_t *dev, int host_in, int dev_dir)
{
    uint32_t n;

    dev->host_pos = 0;
    dev->residue  = 0;

    if (dev->host_len == 0) {
        /* (1) Hn = Dn; (2), (3) the target had data: Phase Error. */
        if (dev_dir != 0) {
            usb_msd_close_command(dev, 0);
            dev->status = CSW_PHASE_ERROR;
        }
        dev->state = MSD_CSW;
        return;
    }

    if (host_in) {
        if (dev_dir > 0) {
            /* (5) Hi > Di, (6) Hi = Di: send the target's data, residue is
               the rest; (7) Hi < Di: send the host's length, Phase Error. */
            if (dev->dev_len > dev->host_len) {
                dev->dev_len = dev->host_len;
                dev->status  = CSW_PHASE_ERROR;
            } else
                dev->residue = dev->host_len - dev->dev_len;
            dev->state = MSD_DATA_IN;
            return;
        }
        /* (4) Hi > Dn: nothing to send; (8) Hi <> Do: Phase Error. Either
           way no data moves, and the device must STALL Bulk-In when it sends
           less than the host asked for (s6.7.2, p. 20-21 / PDF 20-21). */
        if (dev_dir < 0) {
            usb_msd_close_command(dev, 0);
            dev->status = CSW_PHASE_ERROR;
        } else
            dev->residue = dev->host_len;
        dev->state = MSD_IN_STALL;
        return;
    }

    /* All dCBWDataTransferLength bytes from the host are accepted, which the
       device may do instead of stalling Bulk-Out (s6.7.3, p. 21-22 /
       PDF 21-22). (11) Ho > Do, (12) Ho = Do: the target takes the first
       dev_len; (9) Ho > Dn: it takes none; (10) Ho <> Di and (13) Ho < Do:
       it takes none, Phase Error. */
    n = 0;
    if (dev_dir < 0) {
        if (dev->dev_len <= dev->host_len)
            n = dev->dev_len;
        else {
            usb_msd_close_command(dev, 0);
            dev->status = CSW_PHASE_ERROR;
        }
    } else if (dev_dir > 0) {
        usb_msd_close_command(dev, 0);
        dev->status = CSW_PHASE_ERROR;
    }
    dev->dev_len = n;
    if (dev->status != CSW_PHASE_ERROR)
        dev->residue = dev->host_len - n;
    dev->state = MSD_DATA_OUT;
}

/* Returns 0, or USB_DATA_STALL for an invalid CBW. */
static int
usb_msd_cbw(usb_msd_t *dev, const uint8_t *buf, int len)
{
    uint8_t cdb[16];
    int     lun;
    int     cb_len;
    int     dev_dir = 0;

    /* A valid CBW is 31 bytes and starts with the signature (BOT 1.0 s6.2.1,
       p. 17 / PDF 17). After one that is not, Bulk-In stalls and Bulk-Out
       stalls too until a Reset Recovery (s6.6.1, p. 18 / PDF 18). */
    if ((len != 31) || (usb_msd_le32(buf) != CBW_SIGNATURE)) {
        dev->state                       = MSD_INVALID;
        dev->usb.ep_in[MSD_EP_IN].halt   = 1;
        dev->usb.ep_in[MSD_EP_IN].pending = 0;
        return USB_DATA_STALL;
    }

    dev->tag      = usb_msd_le32(buf + 4);
    dev->host_len = usb_msd_le32(buf + 8);
    dev->status   = CSW_PASSED;
    dev->dev_len  = 0;
    dev->open     = 0;
    lun           = buf[13] & 0x0f;
    cb_len        = buf[14] & 0x1f;

    /* A meaningful CBW has no reserved bit set, names an existing LUN and
       carries a 1 to 16 byte command block (s6.2.2, p. 17 / PDF 17; s5.1,
       p. 14 / PDF 14). The spec leaves the answer to one that is not open
       (s6.4, p. 18 / PDF 18); here it fails with no data. */
    dev->sd = NULL;
    if (!(buf[12] & 0x7f) && !(buf[13] & 0xf0) && !(buf[14] & 0xe0) && (cb_len >= 1) && (cb_len <= 16))
        dev->sd = usb_msd_lun(dev, lun);

    if (dev->sd == NULL)
        dev->status = CSW_FAILED;
    else {
        memset(cdb, 0x00, sizeof(cdb));
        memcpy(cdb, buf + 15, cb_len);

        /* Hosts following SCSI-2 also put the LUN in CDB byte 1 bits 7-5.
           The bridge routes by bCBWLUN, and each target is LUN 0 at its own
           SCSI ID and rejects a command with those bits set, so they are
           cleared when they repeat bCBWLUN. */
        if ((lun != 0) && ((cdb[1] >> 5) == lun))
            cdb[1] &= 0x1f;

        /* No length limit is passed to the target, so the length it wants is
           its intent in the s6.7 sense, compared with the host's later. */
        dev->sd->buffer_length = -1;
        scsi_device_command_phase0(dev->sd, cdb);
        dev->open = 1;

        if (dev->sd->status != SCSI_STATUS_OK)
            dev->status = CSW_FAILED;
        if ((dev->sd->phase == SCSI_PHASE_DATA_IN) || (dev->sd->phase == SCSI_PHASE_DATA_OUT)) {
            if (dev->sd->buffer_length > 0) {
                dev->dev_len = dev->sd->buffer_length;
                dev_dir      = (dev->sd->phase == SCSI_PHASE_DATA_IN) ? 1 : -1;
                /* Never past what the target's buffer holds. */
                if (dev->dev_len > usb_msd_buf_len(dev))
                    dev->dev_len = usb_msd_buf_len(dev);
            }
        }
        if (dev_dir == 0)
            usb_msd_close_command(dev, 1);
    }

    usb_msd_plan(dev, !!(buf[12] & 0x80), dev_dir);
    return 0;
}

static int
usb_msd_data_in(usb_dev_t *usb, int ep, uint8_t *buf, int maxlen)
{
    usb_msd_t *dev = (usb_msd_t *) usb->priv;
    uint32_t   n;

    if (ep != MSD_EP_IN)
        return USB_DATA_STALL;

    switch (dev->state) {
        case MSD_DATA_IN:
            n = dev->dev_len - dev->host_pos;
            if (n > (uint32_t) maxlen)
                n = maxlen;
            memcpy(buf, dev->sd->sc->temp_buffer + dev->host_pos, n);
            dev->host_pos += n;
            if (dev->host_pos >= dev->dev_len) {
                usb_msd_close_command(dev, dev->status != CSW_PHASE_ERROR);
                /* Less than the host asked for: the data may end in a short
                   packet, and Bulk-In then stalls (s6.7.2, p. 20 / PDF 20). */
                dev->state = (dev->dev_len < dev->host_len) ? MSD_IN_STALL : MSD_CSW;
            }
            return n;

        case MSD_IN_STALL:
            dev->state = MSD_CSW;
            return USB_DATA_STALL;

        case MSD_CSW:
            usb_msd_put_le32(buf, CSW_SIGNATURE);
            usb_msd_put_le32(buf + 4, dev->tag);
            usb_msd_put_le32(buf + 8, dev->residue);
            buf[12]    = dev->status;
            dev->state = MSD_CBW;
            return 13;

        case MSD_INVALID:
            return USB_DATA_STALL;

        default:
            return USB_DATA_NAK;
    }
}

static int
usb_msd_data_out(usb_dev_t *usb, int ep, const uint8_t *buf, int len)
{
    usb_msd_t *dev = (usb_msd_t *) usb->priv;
    uint32_t   n;

    if (ep != MSD_EP_OUT)
        return USB_DATA_STALL;

    switch (dev->state) {
        case MSD_CBW:
            return usb_msd_cbw(dev, buf, len);

        case MSD_DATA_OUT:
            if (dev->host_pos < dev->dev_len) {
                n = dev->dev_len - dev->host_pos;
                if (n > (uint32_t) len)
                    n = len;
                memcpy(dev->sd->sc->temp_buffer + dev->host_pos, buf, n);
            }
            dev->host_pos += len;
            if (dev->host_pos >= dev->host_len) {
                usb_msd_close_command(dev, dev->status != CSW_PHASE_ERROR);
                dev->state = MSD_CSW;
            }
            return 0;

        case MSD_INVALID:
            return USB_DATA_STALL;

        default:
            /* The host may not send a CBW until it has read the CSW of the
               last one (s3.4, p. 8 / PDF 8). */
            return USB_DATA_NAK;
    }
}

static int
usb_msd_control(usb_dev_t *usb, const uint8_t *setup, uint8_t *data)
{
    usb_msd_t *dev     = (usb_msd_t *) usb->priv;
    uint16_t   wvalue  = setup[2] | (setup[3] << 8);
    uint16_t   windex  = setup[4] | (setup[5] << 8);
    uint16_t   wlength = setup[6] | (setup[7] << 8);
    int        n;

    if ((windex != 0) || (wvalue != 0))
        return USB_DATA_STALL;

    /* Bulk-Only Mass Storage Reset: ready for the next CBW; data toggles and
       endpoint halts stay as they are (BOT 1.0 s3.1, p. 7 / PDF 7). */
    if ((setup[0] == 0x21) && (setup[1] == MSD_RESET) && (wlength == 0)) {
        usb_msd_abort(dev);
        return 0;
    }

    /* Get Max LUN: the highest LUN, 0 when there is none (s3.2, p. 7 /
       PDF 7). */
    if ((setup[0] == 0xa1) && (setup[1] == MSD_GET_MAX_LUN) && (wlength == 1)) {
        n       = usb_msd_n_luns(dev);
        data[0] = (n > 0) ? (n - 1) : 0;
        return 1;
    }

    return USB_DATA_STALL;
}

static void
usb_msd_reset(usb_dev_t *usb)
{
    usb_msd_abort((usb_msd_t *) usb->priv);
}

static void
usb_msd_configured(usb_dev_t *usb, UNUSED(int config))
{
    usb_msd_abort((usb_msd_t *) usb->priv);
}

static const usb_dev_ops_t usb_msd_ops = {
    .control    = usb_msd_control,
    .data_in    = usb_msd_data_in,
    .data_out   = usb_msd_data_out,
    .reset      = usb_msd_reset,
    .configured = usb_msd_configured
};

static void *
usb_msd_init(const device_t *info)
{
    usb_msd_t *dev = (usb_msd_t *) calloc(1, sizeof(usb_msd_t));

    /* It takes a SCSI bus like any host adapter. It is not on the SCSI
       controller list, so it counts itself among the other SCSI providers,
       which makes the Media menu and status bar show the drives on its bus. */
    dev->bus = scsi_get_bus();
    other_scsi_present++;

    /* At least 12 characters from 0-9 and A-F, the last 12 unique for this
       vendor and product ID (BOT 1.0 s4.1.1-s4.1.2, p. 9-10 / PDF 9-10).
       The SCSI bus number keeps two drives in one machine apart. */
    snprintf(dev->serial, sizeof(dev->serial), "86B00000%04X", dev->bus);
    dev->strings[0] = "86Box";
    dev->strings[1] = "USB Mass Storage";
    dev->strings[2] = dev->serial;

    dev->usb.name      = "USB Mass Storage";
    dev->usb.low_speed = 0;
    dev->usb.dev_desc  = usb_msd_dev_desc;
    dev->usb.cfg_desc  = usb_msd_cfg_desc;
    dev->usb.cfg_len   = USB_MSD_CFG_LEN;
    dev->usb.strings   = dev->strings;
    dev->usb.n_strings = 3;
    dev->usb.ops       = &usb_msd_ops;
    dev->usb.priv      = dev;
    usb_dev_init(&dev->usb);
    dev->state = MSD_CBW;

    usb_dev_add(&dev->usb, info);

    return dev;
}

static void
usb_msd_close(void *priv)
{
    usb_msd_t *dev = (usb_msd_t *) priv;

    usb_dev_remove(&dev->usb);
    free(dev);
}

const device_t usb_msd_device = {
    .name          = "USB Mass Storage",
    .internal_name = "usb_msd",
    .flags         = DEVICE_USB,
    .local         = 0,
    .init          = usb_msd_init,
    .close         = usb_msd_close,
    .reset         = NULL,
    .available     = NULL,
    .speed_changed = NULL,
    .force_redraw  = NULL,
    .config        = NULL
};

int usb_msd_type = 0;

/* The mass storage list on the Ports settings page; entry 0 is none. */
static const device_t *const usb_msd_devices[] = {
    &device_none,
    &usb_msd_device,
    NULL
};

int
usb_msd_get_ndev(void)
{
    return (int) (sizeof(usb_msd_devices) / sizeof(usb_msd_devices[0])) - 1;
}

const device_t *
usb_msd_get_device(int msd)
{
    return usb_msd_devices[msd];
}

const char *
usb_msd_get_internal_name(int msd)
{
    return device_get_internal_name(usb_msd_devices[msd]);
}

int
usb_msd_get_from_internal_name(const char *s)
{
    for (int c = 0; usb_msd_devices[c] != NULL; c++) {
        if (!strcmp(usb_msd_devices[c]->internal_name, s))
            return c;
    }

    return 0;
}

/* Called on hard reset right after the SCSI cards, so the device takes the
   SCSI bus after theirs, as scsi_plan() counts it. */
void
usb_msd_hard_reset(void)
{
    if (usb_msd_type != 0)
        device_add(usb_msd_devices[usb_msd_type]);
}
