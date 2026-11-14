/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          USB flash drives: each one a USB mass storage device of its
 *          own, with one logical unit backed by a raw disk image.
 *
 *          Written from:
 *          - Universal Serial Bus Mass Storage Class Bulk-Only Transport,
 *            Revision 1.0 ("BOT 1.0").
 *          - Universal Serial Bus Mass Storage Class UFI Command
 *            Specification, Revision 1.0 ("UFI 1.0").
 *          - Seagate SCSI Commands Reference Manual, 100293068 Rev. J
 *            ("SCSI Commands"), for the SPC and SBC commands.
 *          - Microsoft Extensible Firmware Initiative FAT32 File System
 *            Specification, version 1.03 ("FAT 1.03"), for new images.
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
#include <time.h>
#include <wchar.h>
#define HAVE_STDARG_H
#include <86box/86box.h>
#include <86box/device.h>
#include <86box/mem.h>
#include <86box/plat.h>
#include <86box/thread.h>
#include <86box/usb.h>
#include <86box/usb_dev.h>
#include <86box/usb_msd.h>
#include <86box/exfat_upcase.h>

#define FLASH_BLOCK 512

/* The largest single transfer the drive buffers: a READ(10) or WRITE(10)
   can ask for at most 65535 blocks, and READ(12)/WRITE(12) past that are
   refused as an invalid field. */
#define FLASH_MAX_BLOCKS 65535

/* Sense keys and additional sense codes (UFI 1.0 Table 51, p. 51 /
   PDF 51; the same values as SCSI, SCSI Commands Table 28, p. 59 /
   PDF 59). */
#define FLASH_SENSE_NONE            0x00
#define FLASH_SENSE_MEDIUM_ERROR    0x03
#define FLASH_SENSE_ILLEGAL_REQUEST 0x05
#define FLASH_SENSE_DATA_PROTECT    0x07
#define FLASH_SENSE_MISCOMPARE      0x0e

#define FLASH_ASC_WRITE_FAULT       0x03
#define FLASH_ASC_UNRECOVERED_READ  0x11
#define FLASH_ASC_MISCOMPARE        0x1d
#define FLASH_ASC_INVALID_OPCODE    0x20
#define FLASH_ASC_LBA_OUT_OF_RANGE  0x21
#define FLASH_ASC_INVALID_FIELD     0x24
#define FLASH_ASC_WRITE_PROTECTED   0x27
#define FLASH_ASC_SAVING_PARAMS     0x39

static const uint8_t usb_flash_dev_desc[18] = {
    18, 0x01,   /* bLength, DEVICE */
    0x10, 0x01, /* bcdUSB 1.10 */
    0x00, 0x00, 0x00, /* class in the interface (BOT 1.0 s4.1, p. 9 / PDF 9) */
    64,         /* bMaxPacketSize0 */
    0x00, 0x00, /* idVendor */
    0x07, 0x00, /* idProduct */
    0x00, 0x01, /* bcdDevice */
    1, 2, 3,    /* iManufacturer, iProduct, iSerialNumber */
    1           /* bNumConfigurations */
};

typedef struct usb_flash_t {
    usb_msd_bot_t bot;
    char          fn[MAX_IMAGE_PATH_LEN];
    char          label[USB_PORT_LABEL]; /* the saved port it was plugged in for */
    uint8_t       req_wp; /* the user asked for write protection */
    FILE         *fp;
    int           wp;     /* asked for, or the file is read-only */
    uint32_t      blocks;

    uint8_t      *buf;
    uint32_t      buf_sz;

    /* The command in progress, for complete(). */
    uint8_t       op;
    uint8_t       bytchk;
    uint32_t      lba;
    uint32_t      nblk;

    uint8_t       sense_key;
    uint8_t       asc;
    uint8_t       ascq;

    char          serial[16];
    const char   *strings[3];
} usb_flash_t;

static uint32_t
flash_be32(const uint8_t *p)
{
    return ((uint32_t) p[0] << 24) | (p[1] << 16) | (p[2] << 8) | p[3];
}

static void
flash_put_be32(uint8_t *p, uint32_t v)
{
    p[0] = v >> 24;
    p[1] = (v >> 16) & 0xff;
    p[2] = (v >> 8) & 0xff;
    p[3] = v & 0xff;
}

static void
flash_put_le16(uint8_t *p, uint16_t v)
{
    p[0] = v & 0xff;
    p[1] = v >> 8;
}

static void
flash_put_le32(uint8_t *p, uint32_t v)
{
    p[0] = v & 0xff;
    p[1] = (v >> 8) & 0xff;
    p[2] = (v >> 16) & 0xff;
    p[3] = v >> 24;
}

static void
flash_put_le64(uint8_t *p, uint64_t v)
{
    p[0] = v & 0xff;
    p[1] = (v >> 8) & 0xff;
    p[2] = (v >> 16) & 0xff;
    p[3] = (v >> 24) & 0xff;
    p[4] = (v >> 32) & 0xff;
    p[5] = (v >> 40) & 0xff;
    p[6] = (v >> 48) & 0xff;
    p[7] = v >> 56;
}

static void
flash_sense(usb_flash_t *dev, uint8_t key, uint8_t asc)
{
    dev->sense_key = key;
    dev->asc       = asc;
    dev->ascq      = 0;
}

static uint8_t *
flash_buf(usb_flash_t *dev, uint32_t len)
{
    if (len > dev->buf_sz) {
        uint8_t *p = (uint8_t *) realloc(dev->buf, len);

        if (p == NULL)
            return NULL;
        dev->buf    = p;
        dev->buf_sz = len;
    }

    return dev->buf;
}

static int
flash_io(usb_flash_t *dev, int write, uint32_t lba, uint32_t nblk, uint8_t *data)
{
    size_t len = (size_t) nblk * FLASH_BLOCK;

    if (fseeko64(dev->fp, (off64_t) lba * FLASH_BLOCK, SEEK_SET) != 0)
        return 0;
    if (write) {
        if (fwrite(data, 1, len, dev->fp) != len)
            return 0;
        /* Each write reaches the image file at once, so the file on the
           host is whole whenever no command is running. */
        return fflush(dev->fp) == 0;
    }
    return fread(data, 1, len, dev->fp) == len;
}

/* The block range of a READ, WRITE or VERIFY: LBA and transfer length from
   a 6-, 10- or 12-byte CDB. A 6-byte CDB's length 0 means 256 blocks; the
   others transfer nothing for 0 (SCSI Commands READ (10), p. 136 / PDF
   136). */
static void
flash_range(const uint8_t *cdb, uint32_t *lba, uint32_t *nblk)
{
    switch (cdb[0] >> 5) {
        case 0:
            *lba  = ((cdb[1] & 0x1f) << 16) | (cdb[2] << 8) | cdb[3];
            *nblk = cdb[4] ? cdb[4] : 256;
            break;
        case 5:
            *lba  = flash_be32(cdb + 2);
            *nblk = flash_be32(cdb + 6);
            break;
        default:
            *lba  = flash_be32(cdb + 2);
            *nblk = (cdb[7] << 8) | cdb[8];
            break;
    }
}

/* Mode parameter header, no block descriptors and no pages: WP in the
   device-specific parameter is the only thing a host reads from a USB
   stick here (SCSI Commands Tables 361-362, p. 378 / PDF 378). */
static uint32_t
flash_mode_sense(usb_flash_t *dev, const uint8_t *cdb, uint8_t *buf)
{
    int ten  = (cdb[0] == 0x5a);
    int pc   = cdb[2] >> 6;
    int page = cdb[2] & 0x3f;

    /* No mode page is implemented, so "all pages" (3Fh) returns the header
       alone and any single page is an invalid field. Saved values are not
       supported (UFI 1.0 Table 51, 05h/39h, p. 51 / PDF 51). */
    if (pc == 3) {
        flash_sense(dev, FLASH_SENSE_ILLEGAL_REQUEST, FLASH_ASC_SAVING_PARAMS);
        return 0;
    }
    if (page != 0x3f) {
        flash_sense(dev, FLASH_SENSE_ILLEGAL_REQUEST, FLASH_ASC_INVALID_FIELD);
        return 0;
    }

    if (ten) {
        memset(buf, 0x00, 8);
        buf[1] = 6;
        buf[3] = dev->wp ? 0x80 : 0x00;
        return 8;
    }

    memset(buf, 0x00, 4);
    buf[0] = 3;
    buf[2] = dev->wp ? 0x80 : 0x00;
    return 4;
}

static int
usb_flash_lun_present(UNUSED(void *priv), int lun)
{
    return lun == 0;
}

/* The commands a USB stick's firmware answers on its SCSI transparent
   command set interface. Every command but REQUEST SENSE starts with no
   sense data, so REQUEST SENSE reports the last command's outcome. */
static int
usb_flash_command(void *priv, UNUSED(int lun), uint8_t *cdb, int *dir, uint32_t *len, uint8_t **buf)
{
    usb_flash_t *dev   = (usb_flash_t *) priv;
    uint8_t     *b     = flash_buf(dev, 64);
    uint32_t     alloc = 0;
    uint32_t     n     = 0;

    *dir = 0;
    *len = 0;
    *buf = b;

    dev->op     = cdb[0];
    dev->nblk   = 0;
    dev->bytchk = 0;

    if (cdb[0] != 0x03)
        flash_sense(dev, FLASH_SENSE_NONE, 0);

    if (b == NULL) {
        flash_sense(dev, FLASH_SENSE_MEDIUM_ERROR, FLASH_ASC_UNRECOVERED_READ);
        return 0;
    }

    switch (cdb[0]) {
        case 0x00: /* TEST UNIT READY */
        case 0x1b: /* START STOP UNIT: the medium cannot leave the stick */
            break;

        case 0x03: /* REQUEST SENSE: fixed format (SCSI Commands Table 27,
                      p. 56 / PDF 56) */
            memset(b, 0x00, 18);
            b[0]  = 0x70;
            b[2]  = dev->sense_key;
            b[7]  = 10;
            b[12] = dev->asc;
            b[13] = dev->ascq;
            n     = 18;
            alloc = cdb[4];
            flash_sense(dev, FLASH_SENSE_NONE, 0);
            break;

        case 0x12: /* INQUIRY: standard data only (SCSI Commands Table 59,
                      p. 94 / PDF 94) */
            if ((cdb[1] & 0x01) || cdb[2]) {
                flash_sense(dev, FLASH_SENSE_ILLEGAL_REQUEST, FLASH_ASC_INVALID_FIELD);
                return 0;
            }
            memset(b, 0x00, 36);
            b[0] = 0x00; /* direct access block device */
            b[1] = 0x80; /* RMB: removable */
            b[2] = 0x02; /* SCSI-2 */
            b[3] = 0x02; /* response data format 2 */
            b[4] = 36 - 5;
            memcpy(b + 8, "86Box   ", 8);
            memcpy(b + 16, "USB Flash Drive ", 16);
            memcpy(b + 32, "1.00", 4);
            n     = 36;
            alloc = (cdb[3] << 8) | cdb[4];
            break;

        case 0x1a: /* MODE SENSE(6) */
            n     = flash_mode_sense(dev, cdb, b);
            alloc = cdb[4];
            if (n == 0)
                return 0;
            break;

        case 0x5a: /* MODE SENSE(10) */
            n     = flash_mode_sense(dev, cdb, b);
            alloc = (cdb[7] << 8) | cdb[8];
            if (n == 0)
                return 0;
            break;

        case 0x1e: /* PREVENT ALLOW MEDIUM REMOVAL: a stick has no lock, so
                      ALLOW passes and PREVENT is an invalid field (UFI 1.0
                      s4.6, p. 29 / PDF 29) */
            if (cdb[4] & 0x01) {
                flash_sense(dev, FLASH_SENSE_ILLEGAL_REQUEST, FLASH_ASC_INVALID_FIELD);
                return 0;
            }
            break;

        case 0x23: /* READ FORMAT CAPACITIES: the list header and the
                      current capacity, formatted media (UFI 1.0 s4.10,
                      Tables 31-33, p. 34 / PDF 34) */
            memset(b, 0x00, 12);
            b[3] = 8;
            flash_put_be32(b + 4, dev->blocks);
            b[8]  = 0x02;
            b[10] = FLASH_BLOCK >> 8;
            b[11] = FLASH_BLOCK & 0xff;
            n     = 12;
            alloc = (cdb[7] << 8) | cdb[8];
            break;

        case 0x25: /* READ CAPACITY(10): the last LBA and the block length
                      (SCSI Commands Table 120, p. 156 / PDF 156) */
            flash_put_be32(b, dev->blocks - 1);
            flash_put_be32(b + 4, FLASH_BLOCK);
            n     = 8;
            alloc = 8;
            break;

        case 0x35: /* SYNCHRONIZE CACHE(10): writes are never held back */
            break;

        case 0x08: /* READ(6) */
        case 0x28: /* READ(10) */
        case 0xa8: /* READ(12) */
        case 0x0a: /* WRITE(6) */
        case 0x2a: /* WRITE(10) */
        case 0xaa: /* WRITE(12) */
        case 0x2f: /* VERIFY(10) */
            flash_range(cdb, &dev->lba, &dev->nblk);
            if (cdb[0] == 0x2f)
                dev->bytchk = !!(cdb[1] & 0x02);
            if (((uint64_t) dev->lba + dev->nblk) > dev->blocks) {
                flash_sense(dev, FLASH_SENSE_ILLEGAL_REQUEST, FLASH_ASC_LBA_OUT_OF_RANGE);
                dev->nblk = 0;
                return 0;
            }
            if (dev->nblk > FLASH_MAX_BLOCKS) {
                flash_sense(dev, FLASH_SENSE_ILLEGAL_REQUEST, FLASH_ASC_INVALID_FIELD);
                dev->nblk = 0;
                return 0;
            }
            /* Writes to a write-protected stick fail before any data
               moves (SCSI Commands, SWP bit, p. 400 / PDF 400). */
            if (dev->wp && ((cdb[0] & 0x1f) == 0x0a)) {
                flash_sense(dev, FLASH_SENSE_DATA_PROTECT, FLASH_ASC_WRITE_PROTECTED);
                dev->nblk = 0;
                return 0;
            }
            if (dev->nblk == 0)
                break;
            /* VERIFY without BYTCHK only checks the range. */
            if ((cdb[0] == 0x2f) && !dev->bytchk) {
                dev->nblk = 0;
                break;
            }

            n = dev->nblk * FLASH_BLOCK;
            b = flash_buf(dev, n);
            if (b == NULL) {
                flash_sense(dev, FLASH_SENSE_ILLEGAL_REQUEST, FLASH_ASC_INVALID_FIELD);
                dev->nblk = 0;
                return 0;
            }
            *buf = b;
            *len = n;

            if ((cdb[0] & 0x1f) == 0x08) {
                if (!flash_io(dev, 0, dev->lba, dev->nblk, b)) {
                    flash_sense(dev, FLASH_SENSE_MEDIUM_ERROR, FLASH_ASC_UNRECOVERED_READ);
                    *len = 0;
                    return 0;
                }
                *dir = 1;
            } else
                *dir = -1;
            return 1;

        default:
            flash_sense(dev, FLASH_SENSE_ILLEGAL_REQUEST, FLASH_ASC_INVALID_OPCODE);
            return 0;
    }

    if (n > 0) {
        *dir = 1;
        *len = (alloc < n) ? alloc : n;
    }
    return 1;
}

/* After the full data stage: a WRITE puts its blocks in the image, a VERIFY
   with BYTCHK compares them with it. */
static int
usb_flash_complete(void *priv)
{
    usb_flash_t *dev = (usb_flash_t *) priv;
    uint8_t     *tmp;
    int          ok  = 1;

    if (dev->nblk == 0)
        return dev->sense_key == FLASH_SENSE_NONE;

    if ((dev->op & 0x1f) == 0x0a) {
        if (!flash_io(dev, 1, dev->lba, dev->nblk, dev->buf)) {
            flash_sense(dev, FLASH_SENSE_MEDIUM_ERROR, FLASH_ASC_WRITE_FAULT);
            ok = 0;
        }
    } else if (dev->op == 0x2f) {
        tmp = (uint8_t *) malloc((size_t) dev->nblk * FLASH_BLOCK);
        if ((tmp == NULL) || !flash_io(dev, 0, dev->lba, dev->nblk, tmp)) {
            flash_sense(dev, FLASH_SENSE_MEDIUM_ERROR, FLASH_ASC_UNRECOVERED_READ);
            ok = 0;
        } else if (memcmp(tmp, dev->buf, (size_t) dev->nblk * FLASH_BLOCK) != 0) {
            flash_sense(dev, FLASH_SENSE_MISCOMPARE, FLASH_ASC_MISCOMPARE);
            ok = 0;
        }
        free(tmp);
    }

    dev->nblk = 0;
    return ok;
}

/* A command dropped before its data stage ended changes nothing: a WRITE
   reaches the image only in complete(). */
static void
usb_flash_stop(void *priv)
{
    ((usb_flash_t *) priv)->nblk = 0;
}

static const usb_msd_target_ops_t usb_flash_target_ops = {
    .lun_present = usb_flash_lun_present,
    .command     = usb_flash_command,
    .complete    = usb_flash_complete,
    .stop        = usb_flash_stop
};

/* A new stick on no port yet, or NULL when the image cannot be used. */
static usb_flash_t *
usb_flash_new(const char *fn, uint8_t wp)
{
    usb_flash_t *dev  = (usb_flash_t *) calloc(1, sizeof(usb_flash_t));
    off64_t      size = 0;
    uint32_t     hash = 0x811c9dc5;

    snprintf(dev->fn, sizeof(dev->fn), "%s", fn);
    dev->req_wp = wp;
    dev->wp     = wp;
    if (!dev->wp)
        dev->fp = plat_fopen64(fn, "rb+");
    /* A file the host does not let us write is a write-protected stick. */
    if (dev->fp == NULL) {
        dev->fp = plat_fopen64(fn, "rb");
        dev->wp = 1;
    }
    if (dev->fp != NULL) {
        fseeko64(dev->fp, 0, SEEK_END);
        size = ftello64(dev->fp);
    }
    /* The capacity is the whole blocks in the file; the file is never
       resized. */
    if ((dev->fp == NULL) || (size < FLASH_BLOCK) || ((size / FLASH_BLOCK) > 0xffffffffLL)) {
        pclog("USB flash drive: cannot use image \"%s\"\n", fn);
        if (dev->fp != NULL)
            fclose(dev->fp);
        free(dev);
        return NULL;
    }
    dev->blocks = (uint32_t) (size / FLASH_BLOCK);

    /* At least 12 characters from 0-9 and A-F (BOT 1.0 s4.1.1, p. 9 /
       PDF 9). It comes from the image's file name, so the same image is the
       same stick to the guest every time it is plugged in, and two images
       are two sticks. */
    for (const char *p = fn; *p != '\0'; p++)
        hash = (hash ^ (uint8_t) *p) * 0x01000193;
    snprintf(dev->serial, sizeof(dev->serial), "86B0%08X", hash);
    dev->strings[0] = "86Box";
    dev->strings[1] = "USB Flash Drive";
    dev->strings[2] = dev->serial;

    usb_msd_bot_init(&dev->bot, &usb_flash_target_ops, dev);
    dev->bot.usb.name      = "USB Flash Drive";
    dev->bot.usb.low_speed = 0;
    dev->bot.usb.dev_desc  = usb_flash_dev_desc;
    dev->bot.usb.cfg_desc  = usb_msd_cfg_desc;
    dev->bot.usb.cfg_len   = USB_MSD_CFG_LEN;
    dev->bot.usb.strings   = dev->strings;
    dev->bot.usb.n_strings = 3;
    usb_dev_init(&dev->bot.usb);

    return dev;
}

/* Pulled out: the port sees the disconnect and the image file closes, so
   it is whole and free to use on the host. */
static void
usb_flash_free(usb_flash_t *dev)
{
    usb_dev_remove(&dev->bot.usb);
    fclose(dev->fp);
    free(dev->buf);
    free(dev);
}

/* Which image the user wants in which port. The GUI writes it, the
   emulation thread follows it; the lock covers both. An entry that could
   not go in when the machine started (its port is gone, say) is parked:
   it stays in the config, out of the machine, until the user picks that
   port again. */
typedef struct usb_flash_cfg_t {
    char    label[USB_PORT_LABEL];
    char    fn[MAX_IMAGE_PATH_LEN];
    uint8_t wp;
    uint8_t parked;
} usb_flash_cfg_t;

/* The recent images, under usb_flash_mutex: the GUI changes the list while
   a config save may be reading it on the emulation thread. */
static char usb_flash_history[MAX_PREV_IMAGES][MAX_IMAGE_PATH_LEN];

static usb_flash_cfg_t usb_flash_cfg[USB_PORTS_MAX];
static usb_flash_cfg_t usb_flash_want_copy[USB_PORTS_MAX];
static mutex_t        *usb_flash_mutex;
/* The sticks in the machine; emulation thread only. */
static usb_flash_t    *usb_flash_sticks[USB_PORTS_MAX];

/* Main thread, when the config is loaded, before the emulation thread
   runs. */
void
usb_flash_cfg_clear(void)
{
    if (usb_flash_mutex == NULL)
        usb_flash_mutex = thread_create_mutex();
    memset(usb_flash_cfg, 0x00, sizeof(usb_flash_cfg));
    memset(usb_flash_history, 0x00, sizeof(usb_flash_history));
}

int
usb_flash_cfg_get(int i, char *label, char *fn, uint8_t *wp)
{
    int ret;

    if ((i < 0) || (i >= USB_PORTS_MAX) || (usb_flash_mutex == NULL))
        return 0;

    thread_wait_mutex(usb_flash_mutex);
    ret = (usb_flash_cfg[i].fn[0] != '\0');
    if (ret) {
        memcpy(label, usb_flash_cfg[i].label, USB_PORT_LABEL);
        memcpy(fn, usb_flash_cfg[i].fn, MAX_IMAGE_PATH_LEN);
        *wp = usb_flash_cfg[i].wp;
    }
    thread_release_mutex(usb_flash_mutex);

    return ret;
}

void
usb_flash_cfg_set(int i, const char *label, const char *fn, uint8_t wp)
{
    if ((i < 0) || (i >= USB_PORTS_MAX) || (usb_flash_mutex == NULL))
        return;

    thread_wait_mutex(usb_flash_mutex);
    snprintf(usb_flash_cfg[i].label, USB_PORT_LABEL, "%s", label);
    snprintf(usb_flash_cfg[i].fn, MAX_IMAGE_PATH_LEN, "%s", fn);
    usb_flash_cfg[i].wp     = wp;
    usb_flash_cfg[i].parked = 0;
    thread_release_mutex(usb_flash_mutex);
}

void
usb_flash_cfg_rehome(const char *from, const char *to)
{
    const size_t n = strlen(from);
    char         moved[USB_PORT_LABEL];

    if (usb_flash_mutex == NULL)
        return;

    thread_wait_mutex(usb_flash_mutex);
    for (int i = 0; i < USB_PORTS_MAX; i++) {
        char *label = usb_flash_cfg[i].label;

        if ((usb_flash_cfg[i].fn[0] != '\0') && !strncmp(label, from, n) && (label[n] == '.')) {
            snprintf(moved, sizeof(moved), "%s%s", to, &label[n]);
            memcpy(label, moved, USB_PORT_LABEL);
        }
    }
    thread_release_mutex(usb_flash_mutex);
}

/* The newest image goes to the top of the recent list; GUI thread, with
   usb_flash_mutex held. */
static void
usb_flash_history_add(const char *fn)
{
    int i;

    for (i = 0; i < (MAX_PREV_IMAGES - 1); i++) {
        if (!strcmp(usb_flash_history[i], fn))
            break;
    }
    memmove(usb_flash_history[1], usb_flash_history[0], (size_t) i * MAX_IMAGE_PATH_LEN);
    snprintf(usb_flash_history[0], MAX_IMAGE_PATH_LEN, "%s", fn);
}

int
usb_flash_history_get(int i, char *fn, int n)
{
    int ret;

    if ((i < 0) || (i >= MAX_PREV_IMAGES) || (usb_flash_mutex == NULL))
        return 0;

    thread_wait_mutex(usb_flash_mutex);
    ret = (usb_flash_history[i][0] != '\0');
    snprintf(fn, n, "%s", usb_flash_history[i]);
    thread_release_mutex(usb_flash_mutex);

    return ret;
}

void
usb_flash_history_set(int i, const char *fn)
{
    if ((i < 0) || (i >= MAX_PREV_IMAGES) || (usb_flash_mutex == NULL))
        return;

    thread_wait_mutex(usb_flash_mutex);
    snprintf(usb_flash_history[i], MAX_IMAGE_PATH_LEN, "%s", fn);
    thread_release_mutex(usb_flash_mutex);
}

void
usb_flash_history_clear(void)
{
    if (usb_flash_mutex == NULL)
        return;

    thread_wait_mutex(usb_flash_mutex);
    memset(usb_flash_history, 0x00, sizeof(usb_flash_history));
    thread_release_mutex(usb_flash_mutex);
}

void
usb_port_flash_want(const char *label, const char *fn, uint8_t wp)
{
    int  plug = (fn != NULL) && (fn[0] != '\0');
    int  at   = -1;
    int  in   = -1;
    char here[MAX_IMAGE_PATH_LEN];
    int  shown;

    if (usb_flash_mutex == NULL)
        return;

    /* The request is for the port as the user sees it. A stick the
       controller moved on its own (the chipset turned the controller off)
       sits in a port other than its saved one, so its entry is found by the
       image shown in this port: one image is one stick. */
    shown = usb_port_flash_image(label, here, sizeof(here));

    thread_wait_mutex(usb_flash_mutex);
    for (int i = 0; i < USB_PORTS_MAX; i++) {
        usb_flash_cfg_t *e = &usb_flash_cfg[i];

        if (e->fn[0] == '\0')
            continue;
        if (shown && !strcmp(e->fn, here))
            in = i;
        else if (!strcmp(e->label, label))
            at = i;
        /* One image is one stick: moving it to this port takes it out of
           the one it was in. */
        else if (plug && !strcmp(e->fn, fn))
            memset(e, 0x00, sizeof(usb_flash_cfg_t));
    }
    if (in >= 0)
        at = in;
    if (plug && (at < 0)) {
        for (int i = 0; (i < USB_PORTS_MAX) && (at < 0); i++) {
            if (usb_flash_cfg[i].fn[0] == '\0')
                at = i;
        }
    }
    if (at >= 0) {
        memset(&usb_flash_cfg[at], 0x00, sizeof(usb_flash_cfg_t));
        if (plug) {
            snprintf(usb_flash_cfg[at].label, USB_PORT_LABEL, "%s", label);
            snprintf(usb_flash_cfg[at].fn, MAX_IMAGE_PATH_LEN, "%s", fn);
            usb_flash_cfg[at].wp = wp;
        }
    }
    if (plug)
        usb_flash_history_add(fn);
    thread_release_mutex(usb_flash_mutex);

    usb_hotplug_kick();
}

int
usb_flash_dev_info(const usb_dev_t *dev, char *fn, int n, uint8_t *wp)
{
    for (int i = 0; i < USB_PORTS_MAX; i++) {
        const usb_flash_t *s = usb_flash_sticks[i];

        if ((s != NULL) && (&s->bot.usb == dev)) {
            snprintf(fn, n, "%s", s->fn);
            *wp = s->wp;
            return 1;
        }
    }

    return 0;
}

/* Drops entry e of the config, or parks it, matched by label and image. */
static void
usb_flash_cfg_drop(const usb_flash_cfg_t *e, int park)
{
    thread_wait_mutex(usb_flash_mutex);
    for (int i = 0; i < USB_PORTS_MAX; i++) {
        usb_flash_cfg_t *c = &usb_flash_cfg[i];

        if (!strcmp(c->label, e->label) && !strcmp(c->fn, e->fn)) {
            if (park)
                c->parked = 1;
            else
                memset(c, 0x00, sizeof(usb_flash_cfg_t));
        }
    }
    thread_release_mutex(usb_flash_mutex);
}

/* Makes the sticks in the ports match the config: a stick that is no
   longer wanted where it is comes out, then each wanted image goes into its
   port. Swapping is both: the old stick leaves, the new one arrives. When
   the machine starts, a stick whose port is taken or gone goes to any free
   port, and the config follows it there. A stick the controller moved on
   its own (the chipset turned the controller off) stays where it went, as a
   device does: it is still wanted under the saved port it was plugged in
   for, which the config keeps for the next start. */
void
usb_flash_reconcile(int at_reset)
{
    usb_flash_cfg_t *want = usb_flash_want_copy;
    char             label[USB_PORT_LABEL];

    if (usb_flash_mutex == NULL)
        return;

    thread_wait_mutex(usb_flash_mutex);
    memcpy(want, usb_flash_cfg, sizeof(usb_flash_cfg));
    thread_release_mutex(usb_flash_mutex);

    for (int k = 0; k < USB_PORTS_MAX; k++) {
        usb_flash_t *s    = usb_flash_sticks[k];
        int          keep = 0;

        if (s == NULL)
            continue;
        usb_port_label(&s->bot.usb, label, sizeof(label));
        for (int i = 0; (i < USB_PORTS_MAX) && !keep; i++) {
            if ((want[i].fn[0] == '\0') || want[i].parked || strcmp(want[i].fn, s->fn) || (want[i].wp != s->req_wp))
                continue;
            if (!strcmp(want[i].label, label)) {
                /* Where it is now; after a hub move the saved port is the
                   renumbered one. */
                memcpy(s->label, want[i].label, USB_PORT_LABEL);
                keep = 1;
            } else
                keep = !strcmp(want[i].label, s->label);
        }
        if (!keep) {
            usb_flash_sticks[k] = NULL;
            usb_flash_free(s);
        }
    }

    for (int i = 0; i < USB_PORTS_MAX; i++) {
        usb_flash_cfg_t *e    = &want[i];
        usb_flash_t     *s;
        int              have = 0;
        int              slot = -1;

        if ((e->fn[0] == '\0') || e->parked)
            continue;
        for (int k = 0; k < USB_PORTS_MAX; k++) {
            if (usb_flash_sticks[k] == NULL) {
                if (slot < 0)
                    slot = k;
                continue;
            }
            usb_port_label(&usb_flash_sticks[k]->bot.usb, label, sizeof(label));
            if ((!strcmp(label, e->label) || !strcmp(usb_flash_sticks[k]->label, e->label)) &&
                !strcmp(usb_flash_sticks[k]->fn, e->fn))
                have = 1;
        }
        if (have || (slot < 0))
            continue;

        s = usb_flash_new(e->fn, e->wp);
        if (s == NULL) {
            usb_flash_cfg_drop(e, at_reset);
            continue;
        }
        memcpy(s->label, e->label, USB_PORT_LABEL);
        if (!usb_port_attach(&s->bot.usb, e->label)) {
            if (at_reset && usb_attach_quiet(&s->bot.usb)) {
                usb_port_label(&s->bot.usb, label, sizeof(label));
                memcpy(s->label, label, USB_PORT_LABEL);
                thread_wait_mutex(usb_flash_mutex);
                for (int c = 0; c < USB_PORTS_MAX; c++) {
                    if (!strcmp(usb_flash_cfg[c].label, e->label) && !strcmp(usb_flash_cfg[c].fn, e->fn))
                        snprintf(usb_flash_cfg[c].label, USB_PORT_LABEL, "%s", label);
                }
                thread_release_mutex(usb_flash_mutex);
            } else {
                pclog("USB flash drive %s: port %s is not free\n", e->fn, e->label);
                usb_dev_remove(&s->bot.usb);
                fclose(s->fp);
                free(s);
                usb_flash_cfg_drop(e, at_reset);
                continue;
            }
        }
        usb_flash_sticks[slot] = s;
    }

    usb_ports_refresh();
}

/* The sticks belong to one device, added on every hard reset, so that they
   go in once the controllers and the other USB devices are there, and come
   out when the machine closes. */
static void *
usb_flash_mgr_init(UNUSED(const device_t *info))
{
    if (usb_flash_mutex == NULL)
        usb_flash_mutex = thread_create_mutex();

    usb_flash_reconcile(1);

    return usb_flash_sticks;
}

static void
usb_flash_mgr_close(UNUSED(void *priv))
{
    for (int k = 0; k < USB_PORTS_MAX; k++) {
        if (usb_flash_sticks[k] != NULL) {
            usb_dev_remove(&usb_flash_sticks[k]->bot.usb);
            fclose(usb_flash_sticks[k]->fp);
            free(usb_flash_sticks[k]->buf);
            free(usb_flash_sticks[k]);
            usb_flash_sticks[k] = NULL;
        }
    }
}

const device_t usb_flash_device = {
    .name          = "USB Flash Drives",
    .internal_name = "usb_flash",
    .flags         = DEVICE_USB,
    .local         = 0,
    .init          = usb_flash_mgr_init,
    .close         = usb_flash_mgr_close,
    .reset         = NULL,
    .available     = NULL,
    .speed_changed = NULL,
    .force_redraw  = NULL,
    .config        = NULL
};

/* Called on hard reset after the other USB devices. */
void
usb_flash_hard_reset(void)
{
    device_add(&usb_flash_device);
}

/* New images. One primary partition starts at LBA 63, the first track of
   the CHS geometry the partition table is written for, which is the BIOS
   LBA-assisted translation: 63 sectors per track and the fewest heads out
   of 16, 32, 64, 128 and 255 that keep the cylinders within 1024. */
#define FLASH_PART_START 63

typedef struct flash_layout_t {
    uint32_t total;   /* sectors in the image */
    uint32_t heads;
    uint32_t part;    /* sectors in the partition */
    uint32_t spc;     /* sectors per cluster */
    uint32_t rsvd;
    uint32_t root_ents;
    uint32_t fat_sz;
    uint32_t clusters;
} flash_layout_t;

typedef struct flash_spc_t {
    uint32_t size;
    uint32_t spc;
} flash_spc_t;

/* Sectors per cluster for a volume size, the first entry whose size is not
   below the volume's; 0 is a size the type cannot have (FAT 1.03, "FAT
   Volume Initialization", DskTableFAT16 and DskTableFAT32). The tables
   assume the reserved sectors, FAT count and root entries set below. */
static const flash_spc_t flash_fat16_spc[] = {
    { 8400, 0 }, { 32680, 2 }, { 262144, 4 }, { 524288, 8 }, { 1048576, 16 },
    { 2097152, 32 }, { 4194304, 64 }, { 0xffffffff, 0 }
};

static const flash_spc_t flash_fat32_spc[] = {
    { 66600, 0 }, { 532480, 1 }, { 16777216, 8 }, { 33554432, 16 }, { 67108864, 32 },
    { 0xffffffff, 64 }
};

static const flash_spc_t flash_exfat_spc[] = {
    { 65536, 0}, { 262144, 8 }, { 1048576, 16 }, { 4194304, 32 }, { 0xffffffff, 64 }
};

/* Lays out the volume; returns 0 when the type cannot hold the size. */
static int
flash_layout(uint64_t bytes, int fs, flash_layout_t *l)
{
    const flash_spc_t *t = (fs == USB_FLASH_FS_FAT32) ? flash_fat32_spc : flash_fat16_spc;
    const flash_spc_t *exfat_t = flash_exfat_spc;
    uint32_t           root_secs;
    uint32_t           tmp1;
    uint32_t           tmp2;
    uint32_t           data;

    memset(l, 0x00, sizeof(flash_layout_t));
    if ((bytes < USB_FLASH_MIN_SIZE) || (bytes > USB_FLASH_MAX_SIZE))
        return 0;

    l->total = (uint32_t) (bytes / FLASH_BLOCK);
    l->heads = 255;
    for (uint32_t h = 16; h <= 128; h <<= 1) {
        if (l->total <= (1024 * h * 63)) {
            l->heads = h;
            break;
        }
    }

    if (fs == USB_FLASH_FS_BLANK)
        return 1;

    if (fs == USB_FLASH_FS_EXFAT) {
        l->part = l->total - FLASH_PART_START;
        while (l->part > exfat_t->size)
            exfat_t++;
        l->spc = exfat_t->spc;
        if (l->spc == 0)
            return 0;
        l->clusters = l->part / l->spc;
        return (l->clusters >= 1);
    }

    l->part = l->total - FLASH_PART_START;
    while (l->part > t->size)
        t++;
    l->spc = t->spc;
    if (l->spc == 0)
        return 0;

    l->rsvd      = (fs == USB_FLASH_FS_FAT32) ? 32 : 1;
    l->root_ents = (fs == USB_FLASH_FS_FAT32) ? 0 : 512;

    /* The FAT size, which may come out a few sectors large but never small
       (FAT 1.03, "FAT Volume Initialization"). */
    root_secs = ((l->root_ents * 32) + (FLASH_BLOCK - 1)) / FLASH_BLOCK;
    tmp1      = l->part - (l->rsvd + root_secs);
    tmp2      = (256 * l->spc) + 2;
    if (fs == USB_FLASH_FS_FAT32)
        tmp2 /= 2;
    l->fat_sz = (tmp1 + (tmp2 - 1)) / tmp2;

    /* The cluster count alone decides the FAT type: FAT16 has 4085 to
       65524 clusters, FAT32 at least 65525 (FAT 1.03, "FAT Type
       Determination"). */
    data        = l->part - (l->rsvd + (2 * l->fat_sz) + root_secs);
    l->clusters = data / l->spc;
    if (fs == USB_FLASH_FS_FAT32)
        return l->clusters >= 65525;
    return (l->clusters >= 4085) && (l->clusters < 65525);
}

int
usb_flash_fs_ok(uint64_t bytes, int fs)
{
    flash_layout_t l;

    return flash_layout(bytes, fs, &l);
}

/* The default for a size: FAT16 where it fits, else FAT32. */
int
usb_flash_fs_default(uint64_t bytes)
{
    if (usb_flash_fs_ok(bytes, USB_FLASH_FS_FAT16))
        return USB_FLASH_FS_FAT16;
    if (usb_flash_fs_ok(bytes, USB_FLASH_FS_FAT32))
        return USB_FLASH_FS_FAT32;
    if (usb_flash_fs_ok(bytes, USB_FLASH_FS_EXFAT))
        return USB_FLASH_FS_EXFAT;
    return USB_FLASH_FS_BLANK;
}

static void
flash_chs(const flash_layout_t *l, uint32_t lba, uint8_t *p)
{
    uint32_t c = lba / (l->heads * 63);
    uint32_t h = (lba / 63) % l->heads;
    uint32_t s = (lba % 63) + 1;

    /* An address past cylinder 1023 is written as the largest one. */
    if (c > 1023) {
        c = 1023;
        h = l->heads - 1;
        s = 63;
    }
    p[0] = h;
    p[1] = s | ((c >> 2) & 0xc0);
    p[2] = c & 0xff;
}

static int
flash_write_sector(FILE *fp, uint32_t lba, const uint8_t *sec)
{
    if (fseeko64(fp, (off64_t) lba * FLASH_BLOCK, SEEK_SET) != 0)
        return 0;
    return fwrite(sec, 1, FLASH_BLOCK, fp) == FLASH_BLOCK;
}

/* The boot sector of the volume. Its code only hands the boot to the next
   device (INT 18h), as the stick is not bootable. */
static void
flash_boot_sector(const flash_layout_t *l, int fs, uint32_t vol_id, uint8_t *b)
{
    int     fat32 = (fs == USB_FLASH_FS_FAT32);
    uint8_t ext   = fat32 ? 0x40 : 0x24; /* offset of BS_DrvNum */

    memset(b, 0x00, FLASH_BLOCK);
    b[0] = 0xeb;
    b[1] = ext + 26 - 2; /* jump past the BPB to the code */
    b[2] = 0x90;
    /* The OEM name least likely to upset a FAT driver (FAT 1.03, "Boot
       Sector and BPB", BS_OEMName). */
    memcpy(b + 3, "MSWIN4.1", 8);
    flash_put_le16(b + 11, FLASH_BLOCK);
    b[13] = l->spc;
    flash_put_le16(b + 14, l->rsvd);
    b[16] = 2; /* FATs */
    flash_put_le16(b + 17, l->root_ents);
    if (!fat32 && (l->part < 65536))
        flash_put_le16(b + 19, l->part);
    else
        flash_put_le32(b + 32, l->part);
    b[21] = 0xf8; /* fixed media: a stick is partitioned like a hard disk */
    if (!fat32)
        flash_put_le16(b + 22, l->fat_sz);
    flash_put_le16(b + 24, 63);
    flash_put_le16(b + 26, l->heads);
    flash_put_le32(b + 28, FLASH_PART_START);

    if (fat32) {
        flash_put_le32(b + 36, l->fat_sz);
        flash_put_le32(b + 44, 2); /* root directory cluster */
        flash_put_le16(b + 48, 1); /* FSInfo sector */
        flash_put_le16(b + 50, 6); /* backup boot sector */
    }

    b[ext]      = 0x80; /* drive number */
    b[ext + 2]  = 0x29; /* the next three fields are present */
    flash_put_le32(b + ext + 3, vol_id);
    memcpy(b + ext + 7, "NO NAME    ", 11);
    memcpy(b + ext + 18, fat32 ? "FAT32   " : "FAT16   ", 8);

    b[ext + 26]     = 0xcd; /* INT 18h */
    b[ext + 27]     = 0x18;
    b[ext + 28]     = 0xeb; /* JMP $ */
    b[ext + 29]     = 0xfe;
    b[510]          = 0x55;
    b[511]          = 0xaa;
}

static uint32_t
exfat_boot_checksum(const uint8_t *sectors, uint32_t sector_size)
{
    uint32_t bytes = sector_size * 11;
    uint32_t sum   = 0;

    for (uint32_t i = 0; i < bytes; i++) {
        if (i == 106 || i == 107 || i == 112)
            continue;
        sum = ((sum & 1) ? 0x80000000u : 0) + (sum >> 1) + sectors[i];
    }
    return sum;
}

static void
exfat_boot_sector(const flash_layout_t *l, uint32_t vol_id,
                  uint32_t fat_offset, uint32_t fat_length,
                  uint32_t cluster_heap_offset, uint32_t clusters,
                  uint32_t root_cluster, uint8_t *b)
{
    uint8_t spc_shift = 0;

    for (uint32_t s = l->spc; s > 1; s >>= 1)
        spc_shift++;

    memset(b, 0x00, FLASH_BLOCK);
    b[0] = 0xeb; b[1] = 0x76; b[2] = 0x90;
    memcpy(b + 3, "EXFAT   ", 8);
    flash_put_le64(b + 64, FLASH_PART_START);     /* PartitionOffset */
    flash_put_le64(b + 72, l->part);              /* VolumeLength */
    flash_put_le32(b + 80, fat_offset);           /* FatOffset */
    flash_put_le32(b + 84, fat_length);           /* FatLength */
    flash_put_le32(b + 88, cluster_heap_offset);  /* ClusterHeapOffset */
    flash_put_le32(b + 92, clusters);             /* ClusterCount */
    flash_put_le32(b + 96, root_cluster);         /* RootDirectoryCluster */
    flash_put_le32(b + 100, vol_id);              /* VolumeSerialNumber */
    flash_put_le16(b + 104, 0x0100);              /* FileSystemRevision */
    flash_put_le16(b + 106, 0x0000);              /* VolumeFlags: clean */
    b[108] = 0x09;                                /* BytesPerSectorShift */
    b[109] = spc_shift;                           /* SectorsPerClusterShift */
    b[110] = 0x01;                                /* NumberOfFats */
    b[111] = 0x80;                                /* DriveSelect */
    b[112] = 0xff;                                /* PercentInUse (uknown) */
    b[510] = 0x55;
    b[511] = 0xaa;
}

int
usb_flash_create(const char *fn, uint64_t bytes, int fs)
{
    flash_layout_t l;
    uint8_t        sec[FLASH_BLOCK];
    uint8_t        boot[FLASH_BLOCK];
    uint32_t       vol_id = (uint32_t) time(NULL);
    uint32_t       end;
    uint32_t       fat;
    FILE          *fp;
    int            ok;
    uint8_t        type;

    if (!flash_layout(bytes, fs, &l))
        return 0;

    fp = plat_fopen64(fn, "wb");
    if (fp == NULL)
        return 0;

    /* The image is all zeros but for the structures written below; writing
       its last byte sizes it (sparse where the host file system can). */
    memset(sec, 0x00, sizeof(sec));
    ok = (fseeko64(fp, (off64_t) (l.total * (uint64_t) FLASH_BLOCK) - 1, SEEK_SET) == 0) && (fwrite(sec, 1, 1, fp) == 1);

    if (ok && (fs != USB_FLASH_FS_BLANK)) {
        /* Master boot record: one partition, not active. FAT16 is type 04h
           under 65536 sectors, else 06h; FAT32 is 0Bh, both with their
           LBA types (0Eh, 0Ch) when the partition ends past what CHS can
           address. */
        end = FLASH_PART_START + l.part - 1;
        if (fs == USB_FLASH_FS_EXFAT)
            type = 0x07;
        else if (fs == USB_FLASH_FS_FAT32)
            type = (end < (1024 * l.heads * 63)) ? 0x0b : 0x0c;
        else if (end >= (1024 * l.heads * 63))
            type = 0x0e;
        else
            type = (l.part < 65536) ? 0x04 : 0x06;

        memset(sec, 0x00, sizeof(sec));
        sec[0] = 0xcd; /* INT 18h */
        sec[1] = 0x18;
        sec[2] = 0xeb; /* JMP $ */
        sec[3] = 0xfe;
        flash_put_le32(sec + 440, vol_id); /* disk signature */
        flash_chs(&l, FLASH_PART_START, sec + 447);
        sec[450] = type;
        flash_chs(&l, end, sec + 451);
        flash_put_le32(sec + 454, FLASH_PART_START);
        flash_put_le32(sec + 458, l.part);
        sec[510] = 0x55;
        sec[511] = 0xaa;
        ok       = flash_write_sector(fp, 0, sec);

        if (fs == USB_FLASH_FS_EXFAT) {
            uint32_t       fat_offset;
            uint32_t       fat_length;
            uint32_t       cluster_heap_offset;
            uint32_t       clusters;
            uint8_t        boot_region[12 * FLASH_BLOCK];
            uint32_t       boot_checksum;

            fat_offset = 24;

            clusters = (l.part - fat_offset) / l.spc;
            if (clusters < 4)
                clusters = 4;

            fat_length = (((clusters + 2) * 4) + FLASH_BLOCK - 1) / FLASH_BLOCK;
            if (fat_length < 1)
                fat_length = 1;

            cluster_heap_offset = fat_offset + fat_length;
            clusters = (l.part - cluster_heap_offset) / l.spc;
            if (clusters < 4)
                goto fail;

            uint32_t bitmap_bytes = (clusters + 7) / 8;
            uint32_t cluster_size = l.spc * FLASH_BLOCK;
            uint32_t bitmap_clusters = (bitmap_bytes + cluster_size - 1) / cluster_size;
            uint32_t upcase_clusters = (EXFAT_UPCASE_TABLE_SIZE + cluster_size - 1) / cluster_size;
            uint32_t bitmap_cluster = 2;
            uint32_t upcase_cluster = bitmap_cluster + bitmap_clusters;
            uint32_t root_cluster = upcase_cluster + upcase_clusters;

            memset(boot_region, 0x00, sizeof(boot_region));

            exfat_boot_sector(&l, vol_id, fat_offset, fat_length,
                              cluster_heap_offset, clusters, root_cluster,
                              boot_region);

            for (int s = 1; s <= 8; s++) {
                uint8_t *eb = boot_region + s * FLASH_BLOCK;
                eb[FLASH_BLOCK - 4] = 0x00;
                eb[FLASH_BLOCK - 3] = 0x00;
                eb[FLASH_BLOCK - 2] = 0x55;
                eb[FLASH_BLOCK - 1] = 0xaa;
            }

            boot_checksum = exfat_boot_checksum(boot_region, FLASH_BLOCK);
            {
                uint8_t *cs = boot_region + 11 * FLASH_BLOCK;
                for (uint32_t i = 0; i < FLASH_BLOCK; i += 4)
                    flash_put_le32(cs + i, boot_checksum);
            }

            /* Write main and backup boot regions */
            for (int s = 0; s < 12; s++) {
                ok = ok && flash_write_sector(fp, FLASH_PART_START + s,
                                              boot_region + s * FLASH_BLOCK);
                ok = ok && flash_write_sector(fp, FLASH_PART_START + 12 + s,
                                              boot_region + s * FLASH_BLOCK);
            }

            memset(sec, 0x00, FLASH_BLOCK);
            flash_put_le32(sec + 0, 0xfffffff8);
            flash_put_le32(sec + 4, 0xffffffff);
            for (uint32_t i = 0; i < bitmap_clusters; i++) {
                uint32_t clu = bitmap_cluster + i;
                uint32_t entry = clu * 4;
                if (entry < FLASH_BLOCK) {
                    uint32_t next = (i + 1 < bitmap_clusters) ? (clu + 1) : 0xffffffff;
                    flash_put_le32(sec + entry, next);
                }
            }
            for (uint32_t i = 0; i < upcase_clusters; i++) {
                uint32_t clu = upcase_cluster + i;
                uint32_t entry = clu * 4;
                if (entry < FLASH_BLOCK) {
                    uint32_t next = (i + 1 < upcase_clusters) ? (clu + 1) : 0xffffffff;
                    flash_put_le32(sec + entry, next);
                }
            }
            {
                uint32_t entry = root_cluster * 4;
                if (entry < FLASH_BLOCK)
                    flash_put_le32(sec + entry, 0xffffffff);
            }

            ok = ok && flash_write_sector(fp, FLASH_PART_START + fat_offset, sec);

            /* Allocation bitmap */
            {
                uint32_t off = 0;
                uint32_t cluster = bitmap_cluster;
                uint32_t used_end = root_cluster + 1;

                while (off < bitmap_bytes) {
                    uint32_t n = bitmap_bytes - off;
                    if (n > FLASH_BLOCK)
                        n = FLASH_BLOCK;

                    memset(sec, 0x00, FLASH_BLOCK);

                    for (uint32_t bit = 0; bit < n * 8; bit++) {
                        uint32_t clu = 2 + off * 8 + bit;
                        if (clu < used_end)
                            sec[bit / 8] |= (1u << (bit % 8));
                        /* bits for clu > clusters + 1 stay 0 (reserved) */
                    }

                    uint32_t sector_in_cluster = (off / FLASH_BLOCK) % l.spc;
                    ok = ok && flash_write_sector(fp,
                                                  FLASH_PART_START + cluster_heap_offset
                                                  + (cluster - 2) * l.spc
                                                  + sector_in_cluster,
                                                  sec);

                    off += n;
                    if ((off % cluster_size) == 0)
                        cluster++;
                }
            }

            /* Write an up-case table */
            {
                uint8_t *table = malloc(EXFAT_UPCASE_TABLE_SIZE);
                memcpy(table, exfat_upcase_table, EXFAT_UPCASE_TABLE_SIZE);

                uint32_t off = 0;
                uint32_t cluster = upcase_cluster;
                uint32_t cluster_size = l.spc * FLASH_BLOCK;

                while (off < EXFAT_UPCASE_TABLE_SIZE) {
                    uint32_t n = EXFAT_UPCASE_TABLE_SIZE - off;
                    if (n > FLASH_BLOCK)
                        n = FLASH_BLOCK;

                    memset(sec, 0x00, FLASH_BLOCK);
                    memcpy(sec, table + off, n);

                    uint32_t sector_in_cluster = (off / FLASH_BLOCK) % l.spc;
                    uint32_t lba = FLASH_PART_START + cluster_heap_offset
                        + (cluster - 2) * l.spc
                        + sector_in_cluster;

                    ok = ok && flash_write_sector(fp, lba, sec);

                    off += n;
                    if ((off % cluster_size) == 0)
                        cluster++;
                }
            }

            {
                uint32_t root_lba = FLASH_PART_START + cluster_heap_offset
                    + (root_cluster - 2) * l.spc;
                memset(sec, 0x00, FLASH_BLOCK);
                for (uint32_t s = 0; s < l.spc; s++)
                    ok = ok && flash_write_sector(fp, root_lba + s, sec);
            }

            /* Root directory */
            memset(sec, 0x00, FLASH_BLOCK);
            sec[0]  = 0x83;
            sec[32] = 0x81;
            flash_put_le32(sec + 32 + 20, bitmap_cluster);
            flash_put_le64(sec + 32 + 24, bitmap_bytes);
            sec[64] = 0x82;
            flash_put_le32(sec + 64 + 4, EXFAT_UPCASE_TABLE_CHKSUM);
            flash_put_le32(sec + 64 + 20, upcase_cluster);
            flash_put_le64(sec + 64 + 24, EXFAT_UPCASE_TABLE_SIZE);

            ok = ok && flash_write_sector(fp, FLASH_PART_START
                                          + cluster_heap_offset
                                          + (root_cluster - 2) * l.spc,
                                          sec);
        } else {
            flash_boot_sector(&l, fs, vol_id, boot);
            ok = ok && flash_write_sector(fp, FLASH_PART_START, boot);

            if (fs == USB_FLASH_FS_FAT32) {
                /* FSInfo, and the backup boot sector and FSInfo copy at 6 and
                   7 (FAT 1.03, "FAT32 FSInfo Sector Structure and Backup Boot
                   Sector"). Cluster 2 holds the empty root directory. */
                memset(sec, 0x00, sizeof(sec));
                flash_put_le32(sec, 0x41615252);
                flash_put_le32(sec + 484, 0x61417272);
                flash_put_le32(sec + 488, l.clusters - 1);
                flash_put_le32(sec + 492, 3);
                flash_put_le32(sec + 508, 0xaa550000);
                ok = ok && flash_write_sector(fp, FLASH_PART_START + 1, sec);
                ok = ok && flash_write_sector(fp, FLASH_PART_START + 6, boot);
                ok = ok && flash_write_sector(fp, FLASH_PART_START + 7, sec);
            }

            /* FAT[0] holds the media byte with every other bit set, FAT[1] the
               end-of-chain mark with the clean-shutdown bit set; on FAT32 the
               root directory's cluster 2 is one cluster long (FAT 1.03, "FAT
               Data Structure"). */
            memset(sec, 0x00, sizeof(sec));
            if (fs == USB_FLASH_FS_FAT32) {
                flash_put_le32(sec, 0x0ffffff8);
                flash_put_le32(sec + 4, 0x0fffffff);
                flash_put_le32(sec + 8, 0x0fffffff);
            } else {
                flash_put_le16(sec, 0xfff8);
                flash_put_le16(sec + 2, 0xffff);
            }
            for (int i = 0; i < 2; i++) {
                fat = FLASH_PART_START + l.rsvd + (i * l.fat_sz);
                ok  = ok && flash_write_sector(fp, fat, sec);
            }
        }
    }

  fail:
    if (fclose(fp) != 0)
        ok = 0;
    return ok;
}
