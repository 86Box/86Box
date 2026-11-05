/*
 * 86Box     A hypervisor and IBM PC system emulator.
 *
 *           Iomega ZIP parallel-port drives: ZIP 100 (PPA) and ZIP 250 (IMM).
 *
 *           The parallel ZIP drives are SCSI ZIP drives with an Iomega
 *           parallel-to-SCSI chip built in, at SCSI ID 6: the VPI0 "PPA3"
 *           in the ZIP 100, the "IMM" (VPI2) in the ZIP Plus and ZIP 250.
 *           This file models the chips; the SCSI drive behind them is the
 *           ZIP of rdisk.c, which talks SCSI on this bus (rdisk_is_scsi()).
 *
 *           Protocol source: the host side in Linux drivers/scsi/ppa.c and
 *           imm.c (Grant Guenther, David Campbell, Tim Waugh; ppa.c is based
 *           on a trace of Iomega's DOS GUEST driver). Every sequence below
 *           is the device side of a ppa.c or imm.c routine, named in the
 *           comments.
 *
 *           PPA: the host sees the chip through the status register: while
 *           SELECT IN is set, the high nibble is the SCSI bus (REQ, BSY,
 *           C/D, I/O) and bit 3 echoes AUTOFD; while SELECT IN is clear,
 *           the high nibble carries the byte being read, high nibble with
 *           AUTOFD clear and low nibble with AUTOFD set. Bytes to the drive
 *           are strobed with AUTOFD while SELECT IN is set; PS/2 reads see
 *           the byte on the data lines, EPP moves it in data cycles.
 *
 *           IMM: the chip is addressed with IEEE 1284.3 command packets
 *           (CPP) and reads back in IEEE 1284 nibble or byte mode after a
 *           negotiation. The status register shows REQ, C/D, I/O and BSY
 *           in bits 7, 5, 4 and 3; bytes to the drive go in pairs, one on
 *           each edge of STROBE.
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

/* The same bus phases on the IMM status register. */
#define IMM_REQ 0x80
#define IMM_CD  0x20
#define IMM_IO  0x10
#define IMM_BSY 0x08

static const uint8_t imm_phase_bits[] = {
    [ZIP_BUS_FREE] = 0x00,
    [ZIP_COMMAND]  = IMM_REQ | IMM_BSY | IMM_CD,
    [ZIP_DATA_IN]  = IMM_REQ | IMM_BSY | IMM_IO,
    [ZIP_DATA_OUT] = IMM_REQ | IMM_BSY,
    [ZIP_STATUS]   = IMM_REQ | IMM_BSY | IMM_CD | IMM_IO,
    [ZIP_MSG_IN]   = IMM_REQ | IMM_BSY | IMM_CD | IMM_IO
};

/*
 * imm_cpp(): the command packet preamble. The host reads the status after
 * FF, 87 and 78; imm.c compares those readings against what the drive
 * gives (f8, 58, and 70 or 78).
 */
static const uint8_t imm_cpp_seq[6] = { 0xaa, 0x55, 0x00, 0xff, 0x87, 0x78 };

/*
 * The IEEE 1284 device ID, read by Iomega's ASPIPPM2.SYS through IEEE 1284.3
 * device addressing: it takes a device whose ID has "IMG" at the offset of
 * the manufacturer after "MFG:". Only that much is given here; the rest of
 * what the real chip reports is not known.
 */
static const uint8_t imm_device_id[] = { 0x00, 0x0a, 'M', 'F', 'G', ':', 'I', 'M', 'G', ';' };

#define IMM_NEGO_DEVICE_ID 0x04

#define IMM_CPP_CONNECT     0xe0 /* select device 0 in compatible mode */
#define IMM_CPP_CONNECT_EPP 0x28 /* select device 0 in EPP mode */
#define IMM_CPP_DISCONNECT  0x30 /* deselect all devices */

static const uint8_t zip_cdb_length[8] = { 6, 10, 10, 6, 16, 12, 10, 6 };

/* ppa_connect() and ppa_disconnect(): four bytes, the last one varies for connect. */
static const uint8_t zip_pulse_seq[3] = { 0x00, 0x3c, 0x20 };

#define ZIP_CONNECT_LAST    0x8f /* 0xcf when the host is going to use EPP */
#define ZIP_DISCONNECT_LAST 0x0f

typedef struct lpt_zip_s {
    void *lpt;
    void *log;

    /* The drive, or NULL when no ZIP is assigned to this port. */
    scsi_device_t *sd;
    uint8_t        port;
    uint8_t        no_drive;

    int     imm;     /* the chip is an IMM, not a PPA */
    uint8_t scsi_id; /* the drive's SCSI ID; the chip is ID 7 */
    uint8_t dat;
    uint8_t ctrl;

    int     connected;
    uint8_t c_hist[4]; /* the last bytes of connect pulses */
    uint8_t d_hist[4]; /* the last bytes of disconnect pulses */
    int     d_step;    /* where in a disconnect pulse the host is */
    uint8_t d_byte;

    int cpp_pos;     /* IMM: preamble bytes matched */
    int nego;        /* IMM: where in an IEEE 1284 negotiation the host is */
    uint8_t nego_mode; /* IMM: the extensibility byte of that negotiation */
    int     id_nibble; /* IMM: the next nibble of the device ID */
    int     term;      /* IMM: where in an IEEE 1284 termination the host is */
    int reverse;     /* IMM: negotiated into nibble or byte mode */

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
               (dev->sel_id & (1 << dev->scsi_id)) && lpt_zip_attach_drive(dev)) {
        lpt_zip_log(dev->log, "selected\n");
        dev->phase = ZIP_COMMAND;
        dev->pos   = 0;
    }
}


static void
ppa_write_ctrl(lpt_zip_t *dev, const uint8_t val)
{
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
ppa_read_status(const lpt_zip_t *dev)
{
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

/* imm_cpp(): a preamble byte, or after the preamble the command. */
static void
imm_cpp_data(lpt_zip_t *dev, const uint8_t val)
{
    if ((dev->cpp_pos < (int) sizeof(imm_cpp_seq)) && (val == imm_cpp_seq[dev->cpp_pos]))
        dev->cpp_pos++;
    else if (dev->cpp_pos != (int) sizeof(imm_cpp_seq))
        dev->cpp_pos = (val == imm_cpp_seq[0]) ? 1 : 0;
}

static void
imm_cpp_command(lpt_zip_t *dev, const uint8_t cmd)
{
    switch (cmd) {
        case IMM_CPP_CONNECT:
        case IMM_CPP_CONNECT_EPP:
            if (!dev->connected)
                lpt_zip_log(dev->log, "connect%s\n", (cmd == IMM_CPP_CONNECT_EPP) ? " (EPP)" : "");
            dev->connected = 1;
            break;
        case IMM_CPP_DISCONNECT:
            if (dev->connected)
                lpt_zip_log(dev->log, "disconnect\n");
            dev->connected = 0;
            dev->reverse   = 0;
            if (dev->phase == ZIP_MSG_IN)
                lpt_zip_bus_free(dev);
            break;
        default:
            lpt_zip_log(dev->log, "CPP command %02X ignored\n", cmd);
            break;
    }
}

/* STROBE with the data lines set while SELECT IN is asserted: imm_select() and imm_reset_pulse(). */
static void
imm_strobe(lpt_zip_t *dev)
{
    if (!(dev->dat & 0x80)) {
        if (dev->dat == 0x40) {
            lpt_zip_log(dev->log, "SCSI bus reset\n");
            if (lpt_zip_attach_drive(dev))
                scsi_device_reset(dev->sd);
            lpt_zip_bus_free(dev);
        }
    } else if ((dev->phase == ZIP_BUS_FREE) && (dev->dat & (1 << dev->scsi_id)) && lpt_zip_attach_drive(dev)) {
        lpt_zip_log(dev->log, "selected\n");
        dev->phase = ZIP_COMMAND;
        dev->pos   = 0;
    }
}

static void
imm_write_ctrl(lpt_zip_t *dev, const uint8_t val)
{
    const uint8_t prev = dev->ctrl;

    dev->ctrl = val;

    /*
     * imm_cpp(): the command byte is committed with STROBE (C, D, C; Iomega's
     * driver also E, F, E). Any other strobe ends the packet.
     */
    if ((val & 0x01) && !(prev & 0x01)) {
        const int cpp = dev->cpp_pos == (int) sizeof(imm_cpp_seq);

        dev->cpp_pos = 0;
        if (cpp) {
            imm_cpp_command(dev, dev->dat);
            return;
        }
    }

    if (!dev->connected)
        return;

    if ((val == 0x0d) && (prev == 0x0c)) {
        imm_strobe(dev);
        return;
    }

    /* imm_byte_out(): a byte on each edge of STROBE (4, 5, 0, 5, ..., 0, 4). */
    if (((val == 0x05) && ((prev == 0x04) || (prev == 0x00))) || ((val == 0x00) && (prev == 0x05))) {
        lpt_zip_byte_out(dev, dev->dat);
        return;
    }

    /*
     * imm_negotiate(): 4, then 6 with the mode on the data lines, the drive
     * answers, then 7 and 6. Ended by the host with 4, C, E, 4.
     */
    if (!dev->reverse) {
        if ((val == 0x06) && !(prev & CTRL_AUTOFD)) {
            dev->nego      = 1;
            dev->nego_mode = dev->dat;
        } else if ((val == 0x07) && (dev->nego == 1))
            dev->nego = 2;
        else if ((val == 0x06) && (dev->nego == 2)) {
            dev->nego      = 0;
            dev->reverse   = 1;
            dev->id_nibble = -1; /* the 6 to 4 that ends the negotiation is no nibble */
        } else
            dev->nego = 0;
    } else if (dev->nego_mode & IMM_NEGO_DEVICE_ID) {
        /*
         * IEEE 1284 termination: C (nACK low), E (nACK high), C. Iomega's
         * driver waits for the second step on the control register instead
         * of the status register and goes on after its timeout.
         */
        if ((val == 0x0c) && (dev->term == 2)) {
            dev->reverse = 0;
            dev->term    = 0;
        } else if (val == 0x0c)
            dev->term = 1;
        else if ((val == 0x0e) && (dev->term == 1))
            dev->term = 2;
        else if ((val == 0x04) && (prev == 0x06))
            dev->id_nibble++;
    } else if (val == 0x0e)
        dev->reverse = 0;
    /*
     * imm_nibble_in() takes the next byte at 5 to 4 (NIBBLE2.ILM at 5 to 6),
     * imm_byte_in() at 26 to 25.
     */
    else if (((prev == 0x05) && ((val == 0x04) || (val == 0x06))) || ((val == 0x25) && (prev == 0x26)))
        lpt_zip_advance(dev);

    if (val & CTRL_DIR)
        lpt_write_to_dat(dev->lpt, lpt_zip_peek(dev));
}

static uint8_t
imm_read_status(const lpt_zip_t *dev)
{
    const int input = (dev->phase == ZIP_DATA_IN) || (dev->phase == ZIP_STATUS) || (dev->phase == ZIP_MSG_IN);
    uint8_t   val;

    /* imm_cpp() reads the status after FF, 87 and 78. */
    switch (dev->cpp_pos) {
        case 4:
            return 0xf8;
        case 5:
            return 0x58;
        case 6:
            return dev->connected ? 0x78 : 0x70;
        default:
            break;
    }

    if (!dev->connected)
        return STAT_IDLE;

    /*
     * imm_negotiate(): PERROR set says there is data to read; Iomega's
     * NIBBLE2.ILM also wants BUSY clear, SELECT and nFAULT high (B8).
     */
    if (dev->nego == 1)
        return (input || (dev->nego_mode & IMM_NEGO_DEVICE_ID)) ? 0xb8 : 0x98;

    if (dev->reverse && (dev->nego_mode & IMM_NEGO_DEVICE_ID)) {
        const int more = dev->id_nibble < (int) (2 * sizeof(imm_device_id));

        if (dev->term)
            return (dev->term == 2) ? STAT_IDLE : 0x18;

        /* IEEE 1284 nibble mode: nACK low with the nibble in nFAULT, SELECT, PERROR and !BUSY. */
        if ((dev->ctrl == 0x06) && more) {
            val = imm_device_id[dev->id_nibble >> 1] >> ((dev->id_nibble & 1) ? 4 : 0);
            return ((val & 0x07) << 3) | ((val & 0x08) ? 0x00 : 0x80);
        }
        /* Between nibbles: nACK high, XFLAG set, nFAULT low while there is data. */
        return more ? 0x50 : 0x58;
    }

    /*
     * The end of an IEEE 1284 negotiation as the standard has it (Iomega's
     * driver waits for it, imm.c does not): nACK high, XFLAG set, and nFAULT
     * low for data available.
     */
    if (dev->reverse && (dev->ctrl == 0x04))
        return 0x50;

    if (dev->reverse && input && ((dev->ctrl == 0x06) || (dev->ctrl == 0x05))) {
        val = lpt_zip_peek(dev);
        /* nFAULT is the handshake: low with the low nibble, high with the high one. */
        return (dev->ctrl == 0x06) ? (uint8_t) (val << 4) : ((val & 0xf0) | 0x08);
    }

    return imm_phase_bits[dev->phase];
}

static void
lpt_zip_write_data(uint8_t val, void *priv)
{
    lpt_zip_t *dev = (lpt_zip_t *) priv;

    dev->dat = val;
    if (dev->imm)
        imm_cpp_data(dev, val);
}

static void
lpt_zip_write_ctrl(uint8_t val, void *priv)
{
    lpt_zip_t *dev = (lpt_zip_t *) priv;

    if (dev->imm)
        imm_write_ctrl(dev, val);
    else
        ppa_write_ctrl(dev, val);
}

static uint8_t
lpt_zip_read_status(void *priv)
{
    const lpt_zip_t *dev = (lpt_zip_t *) priv;

    return dev->imm ? imm_read_status(dev) : ppa_read_status(dev);
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
lpt_zip_init(const device_t *info)
{
    lpt_zip_t *dev = (lpt_zip_t *) calloc(1, sizeof(lpt_zip_t));

    if (dev == NULL)
        return NULL;

    dev->imm     = (int) info->local;
    dev->scsi_id = (uint8_t) device_get_config_int("scsi_id");
    dev->log  = log_open(dev->imm ? "LPT ZIP 250" : "LPT ZIP");
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

/*
 * The ZIP 100 answers at SCSI ID 6 (ppa.c finds it there). For the IMM drives
 * the ID is not known here; Iomega's ASPIPPM2.SYS 5.37 gives ASPI target n to
 * the n-th IEEE 1284.3 device but looks the device type up by SCSI ID, so it
 * is consistent only for a drive at ID 0, and that is the default.
 */
static const device_config_t lpt_zip_config[] = {
    {
        .name           = "scsi_id",
        .description    = "SCSI ID",
        .type           = CONFIG_SELECTION,
        .default_string = NULL,
        .default_int    = 6,
        .file_filter    = NULL,
        .spinner        = { 0 },
        .selection      = {
            { .description = "0", .value = 0 },
            { .description = "5", .value = 5 },
            { .description = "6", .value = 6 },
            { .description = ""              }
        },
        .bios           = { { 0 } }
    },
    { .name = "", .description = "", .type = CONFIG_END }
};

static const device_config_t lpt_zip250_config[] = {
    {
        .name           = "scsi_id",
        .description    = "SCSI ID",
        .type           = CONFIG_SELECTION,
        .default_string = NULL,
        .default_int    = 0,
        .file_filter    = NULL,
        .spinner        = { 0 },
        .selection      = {
            { .description = "0", .value = 0 },
            { .description = "5", .value = 5 },
            { .description = "6", .value = 6 },
            { .description = ""              }
        },
        .bios           = { { 0 } }
    },
    { .name = "", .description = "", .type = CONFIG_END }
};


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
    .config        = lpt_zip_config
};

const device_t lpt_zip250_device = {
    .name          = "Iomega ZIP Plus / ZIP 250 parallel-port drive (IMM)",
    .internal_name = "lpt_zip250",
    .flags         = DEVICE_LPT,
    .local         = 1,
    .init          = lpt_zip_init,
    .close         = lpt_zip_close,
    .reset         = NULL,
    .available     = NULL,
    .speed_changed = NULL,
    .force_redraw  = NULL,
    .config        = lpt_zip250_config
};
