/*
 * 86Box     A hypervisor and IBM PC system emulator.
 *
 *           Iomega ZIP 100 parallel-port drive (PPA).
 *
 *           The parallel ZIP 100 is a SCSI ZIP drive with Iomega's VPI0
 *           "PPA3" parallel-to-SCSI chip built in, at SCSI ID 6. This file
 *           models the chip; the SCSI drive behind it is the ZIP 100 of
 *           rdisk.c, which talks SCSI on this bus (see rdisk_is_scsi()).
 *
 *           Protocol source: the host side in Linux drivers/scsi/ppa.c
 *           (Grant Guenther, David Campbell, Tim Waugh; itself based on a
 *           trace of Iomega's DOS GUEST driver). Every sequence below is
 *           the device side of a ppa.c routine, named in the comments.
 *
 *           The host sees the chip through the status register: while
 *           SELECT IN is set, the high nibble is the SCSI bus (REQ, BSY,
 *           C/D, I/O) and bit 3 echoes AUTOFD; while SELECT IN is clear,
 *           the high nibble carries the byte being read, high nibble with
 *           AUTOFD clear and low nibble with AUTOFD set. Bytes to the drive
 *           are strobed with AUTOFD while SELECT IN is set; PS/2 reads see
 *           the byte on the data lines, EPP moves it in data cycles.
 *
 * Authors:  Mario Fetka, <mario.fetka@gmail.com>
 *
 *           Copyright 2026 Mario Fetka.
 */
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>
#define HAVE_STDARG_H
#include <86box/86box.h>
#include <86box/timer.h>
#include <86box/device.h>
#include <86box/lpt.h>
#include <86box/scsi.h>
#include <86box/scsi_device.h>
#include <86box/hdc_ide.h>
#include <86box/rdisk.h>
#include <86box/log.h>
#include <86box/plat_unused.h>

#ifdef ENABLE_LPT_ZIP_LOG
int lpt_zip_do_log = ENABLE_LPT_ZIP_LOG;

static void
lpt_zip_log(void *priv, const char *fmt, ...)
{
    if (lpt_zip_do_log) {
        va_list ap;
        va_start(ap, fmt);
        log_out(priv, fmt, ap);
        va_end(ap);
    }
}
#else
#    define lpt_zip_log(priv, fmt, ...)
#endif

/* The parallel ZIP drives answer at SCSI ID 6, the chip itself is ID 7. */
#define ZIP_SCSI_ID 6

/* Control register bits as the host writes them. */
#define CTRL_AUTOFD 0x02
#define CTRL_INIT   0x04
#define CTRL_SELIN  0x08
#define CTRL_DIR    0x20

/* The SCSI bus in the high nibble of the status register. */
#define STAT_REQ 0x80
#define STAT_BSY 0x40
#define STAT_CD  0x20
#define STAT_IO  0x10

/* What the port reads while the chip is not connected: a port with nothing on it. */
#define STAT_IDLE 0xd8

typedef enum {
    ZIP_BUS_FREE = 0,
    ZIP_COMMAND,
    ZIP_DATA_IN,
    ZIP_DATA_OUT,
    ZIP_STATUS,
    ZIP_MSG_IN
} zip_phase_t;

static const uint8_t zip_phase_bits[] = {
    [ZIP_BUS_FREE] = 0x00,
    [ZIP_COMMAND]  = STAT_REQ | STAT_BSY | STAT_CD,
    [ZIP_DATA_IN]  = STAT_REQ | STAT_BSY | STAT_IO,
    [ZIP_DATA_OUT] = STAT_REQ | STAT_BSY,
    [ZIP_STATUS]   = STAT_REQ | STAT_BSY | STAT_CD | STAT_IO,
    [ZIP_MSG_IN]   = STAT_REQ | STAT_BSY | STAT_CD | STAT_IO
};

static const uint8_t zip_cdb_length[8] = { 6, 10, 10, 6, 16, 12, 10, 6 };

/* ppa_connect() and ppa_disconnect(): four bytes, the last one varies for connect. */
static const uint8_t zip_pulse_seq[3] = { 0x00, 0x3c, 0x20 };

#define ZIP_CONNECT_LAST    0x8f /* 0xcf when the host is going to use EPP */
#define ZIP_DISCONNECT_LAST 0x0f

typedef struct lpt_zip_s {
    void *lpt;
    void *log;

    /* The drive, or NULL when no ZIP 100 is assigned to this port. */
    scsi_device_t *sd;
    uint8_t        port;
    uint8_t        no_drive;

    uint8_t dat;
    uint8_t ctrl;

    int     connected;
    uint8_t c_hist[4]; /* the last bytes of connect pulses */
    uint8_t d_hist[4]; /* the last bytes of disconnect pulses */
    int     d_step;    /* where in a disconnect pulse the host is */
    uint8_t d_byte;

    zip_phase_t phase;
    uint8_t     sel_id;   /* the target bits strobed before a selection */
    uint8_t     cdb[16];
    int         cdb_len;
    int         pos;      /* CDB or data byte */
} lpt_zip_t;

/*
 * The drive is set up by rdisk_hard_reset(), after the parallel port devices,
 * so it is looked up on first use.
 */
static int
lpt_zip_attach_drive(lpt_zip_t *dev)
{
    if (dev->sd != NULL)
        return 1;

    if (dev->no_drive)
        return 0;

    dev->sd = rdisk_get_lpt_device(dev->port);

    if (dev->sd == NULL) {
        dev->no_drive = 1;
        lpt_zip_log(dev->log, "no ZIP drive assigned to LPT%i\n", dev->port + 1);
        return 0;
    }

    lpt_zip_log(dev->log, "LPT%i drive attached\n", dev->port + 1);

    return 1;
}

static void
lpt_zip_bus_free(lpt_zip_t *dev)
{
    dev->phase = ZIP_BUS_FREE;
    dev->pos   = 0;
}

static void
lpt_zip_status_phase(lpt_zip_t *dev)
{
    dev->phase = ZIP_STATUS;
    lpt_zip_log(dev->log, "status %02X\n", dev->sd->status);
}

/* The data phase is over: let the drive finish the command (write the data out). */
static void
lpt_zip_finish_data(lpt_zip_t *dev)
{
    scsi_device_command_phase1(dev->sd);
    lpt_zip_status_phase(dev);
}

static void
lpt_zip_execute(lpt_zip_t *dev)
{
    scsi_device_t *sd = dev->sd;

    lpt_zip_log(dev->log, "command %02X\n", dev->cdb[0]);

    sd->buffer_length = -1;
    scsi_device_command_phase0(sd, dev->cdb);
    dev->pos = 0;

    if ((sd->status == SCSI_STATUS_OK) && (sd->buffer_length > 0) && sd->sc && sd->sc->temp_buffer &&
        ((sd->phase == SCSI_PHASE_DATA_IN) || (sd->phase == SCSI_PHASE_DATA_OUT))) {
        dev->phase = (sd->phase == SCSI_PHASE_DATA_IN) ? ZIP_DATA_IN : ZIP_DATA_OUT;
        lpt_zip_log(dev->log, "data %s, %i bytes\n", (dev->phase == ZIP_DATA_IN) ? "in" : "out",
                    sd->buffer_length);
    } else
        lpt_zip_status_phase(dev);
}

/* The byte the drive offers in an input phase. */
static uint8_t
lpt_zip_peek(const lpt_zip_t *dev)
{
    switch (dev->phase) {
        case ZIP_DATA_IN:
            return dev->sd->sc->temp_buffer[dev->pos];
        case ZIP_STATUS:
            return dev->sd->status;
        case ZIP_MSG_IN:
            return 0x00; /* COMMAND COMPLETE */
        default:
            return 0xff;
    }
}

/* The host has taken the byte offered. */
static void
lpt_zip_advance(lpt_zip_t *dev)
{
    switch (dev->phase) {
        case ZIP_DATA_IN:
            if (++dev->pos >= dev->sd->buffer_length)
                lpt_zip_finish_data(dev);
            break;
        case ZIP_STATUS:
            dev->phase = ZIP_MSG_IN;
            break;
        case ZIP_MSG_IN:
            lpt_zip_bus_free(dev);
            break;
        default:
            break;
    }
}

/* A byte from the host: the CDB, data, or before a selection the target bits. */
static void
lpt_zip_byte_out(lpt_zip_t *dev, const uint8_t val)
{
    switch (dev->phase) {
        case ZIP_BUS_FREE:
            dev->sel_id = val;
            break;
        case ZIP_COMMAND:
            dev->cdb[dev->pos++] = val;
            if (dev->pos == 1)
                dev->cdb_len = zip_cdb_length[val >> 5];
            if (dev->pos == dev->cdb_len) {
                memset(dev->cdb + dev->cdb_len, 0x00, sizeof(dev->cdb) - dev->cdb_len);
                lpt_zip_execute(dev);
            }
            break;
        case ZIP_DATA_OUT:
            dev->sd->sc->temp_buffer[dev->pos++] = val;
            if (dev->pos >= dev->sd->buffer_length)
                lpt_zip_finish_data(dev);
            break;
        default:
            break;
    }
}

static int
lpt_zip_pulse_match(const uint8_t *hist, const uint8_t last)
{
    return !memcmp(hist, zip_pulse_seq, sizeof(zip_pulse_seq)) && (hist[3] == last);
}

static void
lpt_zip_push(uint8_t *hist, const uint8_t val)
{
    memmove(hist, hist + 1, 3);
    hist[3] = val;
}

/* INIT pulled low with SELECT IN set: ppa_select() and ppa_reset_pulse(). */
static void
lpt_zip_init_low(lpt_zip_t *dev)
{
    if (dev->dat == 0x40) {
        lpt_zip_log(dev->log, "SCSI bus reset\n");
        if (lpt_zip_attach_drive(dev))
            scsi_device_reset(dev->sd);
        lpt_zip_bus_free(dev);
    } else if ((dev->dat & 0x80) && (dev->phase == ZIP_BUS_FREE) &&
               (dev->sel_id & (1 << ZIP_SCSI_ID)) && lpt_zip_attach_drive(dev)) {
        lpt_zip_log(dev->log, "selected\n");
        dev->phase = ZIP_COMMAND;
        dev->pos   = 0;
    }
}

static void
lpt_zip_write_data(uint8_t val, void *priv)
{
    lpt_zip_t *dev = (lpt_zip_t *) priv;

    dev->dat = val;
}

static void
lpt_zip_write_ctrl(uint8_t val, void *priv)
{
    lpt_zip_t    *dev  = (lpt_zip_t *) priv;
    const uint8_t prev = dev->ctrl;
    const int     rise = (val & CTRL_AUTOFD) && !(prev & CTRL_AUTOFD);
    const int     fall = !(val & CTRL_AUTOFD) && (prev & CTRL_AUTOFD);

    dev->ctrl = val;

    /* ppa_c_pulse(): the byte is taken on AUTOFD with SELECT IN clear (4, 6, 4, C). */
    if (rise && !(val & (CTRL_SELIN | CTRL_DIR))) {
        lpt_zip_push(dev->c_hist, dev->dat);
        if (lpt_zip_pulse_match(dev->c_hist, ZIP_CONNECT_LAST) ||
            lpt_zip_pulse_match(dev->c_hist, ZIP_CONNECT_LAST | 0x40)) {
            if (!dev->connected)
                lpt_zip_log(dev->log, "connect%s\n", (dev->dat & 0x40) ? " (EPP)" : "");
            dev->connected = 1;
            memset(dev->c_hist, 0xff, sizeof(dev->c_hist));
            memset(dev->d_hist, 0xff, sizeof(dev->d_hist));
        }
    }

    /*
     * ppa_d_pulse() strobes its byte like a data byte (C, E, C) and then goes
     * on to 4 and C, which a data transfer never does: only a byte followed
     * by that counts towards a disconnect.
     */
    if (rise && (val & CTRL_SELIN)) {
        dev->d_byte = dev->dat;
        dev->d_step = 1;
    } else if ((dev->d_step == 1) && (val == 0x0c))
        dev->d_step = 2;
    else if ((dev->d_step == 2) && (val == 0x04)) {
        dev->d_step = 0;
        lpt_zip_push(dev->d_hist, dev->d_byte);
        if (dev->connected && lpt_zip_pulse_match(dev->d_hist, ZIP_DISCONNECT_LAST)) {
            lpt_zip_log(dev->log, "disconnect\n");
            dev->connected = 0;
            /* Iomega's driver leaves the message byte unread. */
            if (dev->phase == ZIP_MSG_IN)
                lpt_zip_bus_free(dev);
            memset(dev->d_hist, 0xff, sizeof(dev->d_hist));
            return;
        }
    } else
        dev->d_step = 0;

    if (!dev->connected)
        return;

    /* ppa_byte_out(): data byte strobed with AUTOFD while SELECT IN is set (C, E, C). */
    if (rise && (val & CTRL_SELIN) && !(val & CTRL_DIR))
        lpt_zip_byte_out(dev, dev->dat);

    /*
     * ppa_nibble_in() moves on to the next byte when it leaves 6 (the low
     * nibble), ppa_byte_in() when it goes from 27 to 25.
     */
    if (fall && ((val & CTRL_DIR) || !(prev & CTRL_SELIN)))
        lpt_zip_advance(dev);

    if ((prev & CTRL_INIT) && !(val & CTRL_INIT) && (val & CTRL_SELIN))
        lpt_zip_init_low(dev);

    if (val & CTRL_DIR)
        lpt_write_to_dat(dev->lpt, lpt_zip_peek(dev));
}

static uint8_t
lpt_zip_read_status(void *priv)
{
    const lpt_zip_t *dev = (lpt_zip_t *) priv;
    uint8_t          val;
    uint8_t          echo;

    if (!dev->connected)
        return STAT_IDLE;

    echo = (dev->ctrl & CTRL_AUTOFD) ? 0x08 : 0x00;

    if ((dev->ctrl & (CTRL_SELIN | CTRL_DIR)) || (dev->phase < ZIP_DATA_IN) || (dev->phase == ZIP_DATA_OUT))
        return zip_phase_bits[dev->phase] | echo;

    val = lpt_zip_peek(dev);

    return ((dev->ctrl & CTRL_AUTOFD) ? (uint8_t) (val << 4) : (val & 0xf0)) | echo;
}

static uint8_t
lpt_zip_read_ctrl(void *priv)
{
    const lpt_zip_t *dev = (lpt_zip_t *) priv;

    return dev->ctrl;
}

/* ppa_out() and ppa_in() in EPP mode: one byte per data cycle. */
static void
lpt_zip_epp_write_data(uint8_t is_addr, uint8_t val, void *priv)
{
    lpt_zip_t *dev = (lpt_zip_t *) priv;

    if (dev->connected && !is_addr)
        lpt_zip_byte_out(dev, val);
}

static void
lpt_zip_epp_request_read(uint8_t is_addr, void *priv)
{
    lpt_zip_t *dev = (lpt_zip_t *) priv;

    if (!dev->connected || is_addr) {
        lpt_write_to_dat(dev->lpt, 0xff);
        return;
    }

    lpt_write_to_dat(dev->lpt, lpt_zip_peek(dev));
    lpt_zip_advance(dev);
}

static void *
lpt_zip_init(UNUSED(const device_t *info))
{
    lpt_zip_t *dev = (lpt_zip_t *) calloc(1, sizeof(lpt_zip_t));

    if (dev == NULL)
        return NULL;

    dev->log  = log_open("LPT ZIP");
    dev->port = (uint8_t) (device_get_instance() - 1);
    dev->ctrl = 0x0c;
    memset(dev->c_hist, 0xff, sizeof(dev->c_hist));
    memset(dev->d_hist, 0xff, sizeof(dev->d_hist));

    /* The drive does not exist yet - see lpt_zip_attach_drive(). */

    dev->lpt = lpt_attach_ex(dev->port,
                             lpt_zip_write_data, lpt_zip_write_ctrl, NULL,
                             lpt_zip_read_status, lpt_zip_read_ctrl,
                             lpt_zip_epp_write_data, lpt_zip_epp_request_read, dev);

    return dev;
}

static void
lpt_zip_close(void *priv)
{
    lpt_zip_t *dev = (lpt_zip_t *) priv;

    if (dev != NULL) {
        if (dev->log != NULL)
            log_close(dev->log);
        free(dev);
    }
}

const device_t lpt_zip_device = {
    .name          = "Iomega ZIP 100 parallel-port drive (PPA)",
    .internal_name = "lpt_zip",
    .flags         = DEVICE_LPT,
    .local         = 0,
    .init          = lpt_zip_init,
    .close         = lpt_zip_close,
    .reset         = NULL,
    .available     = NULL,
    .speed_changed = NULL,
    .force_redraw  = NULL,
    .config        = NULL
};
