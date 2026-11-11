/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          Definitions for the USB mass storage Bulk-Only Transport,
 *          shared by the SCSI bus bridge and the flash drives.
 *
 * Authors: skiretic, <skiretic@proton.me>
 *
 *          Copyright 2026 skiretic.
 */

#ifndef EMU_USB_MSD_H
#define EMU_USB_MSD_H

#ifdef __cplusplus
extern "C" {
#endif

/* The logical units behind a Bulk-Only Transport device. One command is
   open at a time: command() starts it, and then either complete() runs it
   to the end after its full data stage (a data-out command takes its data
   there) or stop() drops it. */
typedef struct usb_msd_target_ops_t {
    int (*lun_present)(void *priv, int lun);
    /* Starts cdb on lun. Sets dir (1 data in, -1 data out, 0 none), len
       (the bytes the unit sends or takes) and buf (where they are). Returns
       1 if the command has not failed so far. */
    int (*command)(void *priv, int lun, uint8_t *cdb, int *dir, uint32_t *len, uint8_t **buf);
    /* Returns 1 if the command passed. */
    int (*complete)(void *priv);
    void (*stop)(void *priv);
} usb_msd_target_ops_t;

typedef struct usb_msd_bot_t {
    usb_dev_t                   usb;
    const usb_msd_target_ops_t *ops;
    void                       *target;
    int                         state;
    int                         open;     /* a command waits for complete() or stop() */
    uint8_t                    *buf;      /* the unit's data for the command in progress */
    uint32_t                    tag;
    uint32_t                    host_len; /* dCBWDataTransferLength */
    uint32_t                    host_pos; /* bytes moved in the data stage */
    uint32_t                    dev_len;  /* bytes the unit sends or takes in it */
    uint32_t                    residue;
    uint8_t                     status;   /* bCSWStatus */
} usb_msd_bot_t;

/* The configuration descriptor every Bulk-Only device here uses. */
#define USB_MSD_CFG_LEN (9 + 9 + 7 + 7)
extern const uint8_t usb_msd_cfg_desc[USB_MSD_CFG_LEN];

/* Sets up the transport side of bot->usb; the caller fills in the
   descriptors and name, then calls usb_dev_init() and usb_dev_add(). */
extern void usb_msd_bot_init(usb_msd_bot_t *bot, const usb_msd_target_ops_t *ops, void *target);

#ifdef __cplusplus
}
#endif

#endif /*EMU_USB_MSD_H*/
