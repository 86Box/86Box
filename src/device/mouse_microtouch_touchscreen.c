/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          3M MicroTouch Serial emulation.
 *
 * Authors: Cacodemon345, mourix
 *
 *          Copyright 2024 Cacodemon345
 */

/* Reference: https://www.touchwindow.com/mm5/drivers/mtsctlrm.pdf
   (MicroTouch Touch Controllers Reference Guide; command section, pp. 24-81). */

/* Implemented: formats Tablet, Decimal, Hexadecimal, Binary (+ Stream), Zone, Raw and
   Calibrate Raw; modes Stream, Point, Down/Up, Polled (XON), Inactive and Status;
   Calibrate New/Extended/Interactive with the manual's acknowledgements; the
   parameter commands (PL, Ppds[b], SEn, FNnn, AD/AE, ^CFnn, RD) and their NOVRAM.

   TODO:
    - GP/SP blocks are not documented anywhere; GP1 still answers a dummy block.
    - NM (sent by 3M TouchWare) is undocumented; it gets a positive response.
    - Format Raw / Calibrate Raw data are synthesised from the touch position. */
#include <ctype.h>
#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <stdbool.h>
#include <time.h>
#include <86box/86box.h>
#include <86box/device.h>
#include <86box/timer.h>
#include <86box/mouse.h>
#include <86box/serial.h>
#include <86box/plat.h>
#include <86box/fifo8.h>
#include <86box/fifo.h>
#include <86box/video.h>
#include <86box/nvr.h>

/* NOVRAM: 0-15 calibration (4 floats); 16 on: the settings Parameter Lock,
   Parameter Set and Sensitivity Set store (older 16-byte files load as defaults). */
#define NVR_SIZE        32
#define NVR_MAGIC       16 /* 'M' when the settings below are valid */
#define NVR_FORMAT      17
#define NVR_MODE        18
#define NVR_FLAGS       19 /* bit 0 = Mode Status, bit 1 = AutoBaud */
#define NVR_BAUD        20 /* Parameter Set rate code, 1-5 */
#define NVR_SENSITIVITY 21
#define NVR_FILTER      22
#define NVR_SERIAL      23 /* parity (bits 0-1), 7 data bits (bit 2), 2 stop bits (bit 3) */

enum mtouch_formats {
    FORMAT_DEC     = 1,
    FORMAT_HEX     = 2,
    FORMAT_RAW     = 3,
    FORMAT_TABLET  = 4,
    FORMAT_BINARY  = 5,
    FORMAT_ZONE    = 6,
    FORMAT_CAL_RAW = 7
};

enum mtouch_modes {
    MODE_DOWNUP   = 1,
    MODE_INACTIVE = 2,
    MODE_POINT    = 3,
    MODE_STREAM   = 4,
    MODE_POLLED   = 5
};

enum mtouch_events {
    EV_NONE = 0,
    EV_DOWN,     /* touchdown */
    EV_CONT,     /* continued touch */
    EV_UP        /* liftoff */
};

enum mtouch_cal {
    CAL_NEW = 1, /* CN: targets at the corners */
    CAL_EXTENDED, /* CX: targets 1/8 inward */
    CAL_INTERACTIVE /* CI: corners, no range check, swaps reversed points */
};

static const char *mtouch_identity[] = {
    "A30100", /* SMT2 Serial / SMT3(R)V */
    "A40100", /* SMT2 PCBus */
    "P50100", /* TouchPen 4(+) */
    "Q10100", /* SMT3(R) Serial */
};

typedef struct mouse_microtouch_t {
    char         cmd[256];
    double       abs_x, abs_y;       /* calibrated, 0..1, y from the top */
    double       raw_x, raw_y;       /* before calibration, 0..1 */
    double       last_x, last_y;     /* last touched point (for the liftoff report) */
    double       last_raw_x, last_raw_y;
    bool         in_zone, last_in_zone;
    float        scale_x, scale_y, off_x, off_y;
    int          but, but_old;
    int          baud_rate, cmd_pos;
    uint8_t      format, prev_format, mode;
    uint8_t      id, cal_cntr, cal_type, pen_mode;
    uint8_t      sensitivity, filter, serial_cfg;
    int          touch_samples;      /* reports' worth of samples in the current touch */
    double       cal_x[2], cal_y[2]; /* raw calibration points */
    bool         mode_status, soh, autobaud;
    bool         in_reset, reset, power_on, ut_reset;
    /* Mode Polled */
    bool         xon, pq_down, pq_up;
    double       pq_down_x, pq_down_y, pq_up_x, pq_up_y;
    bool         pq_down_zone, pq_up_zone;
    uint8_t     *nvr;
    char         nvr_path[64];
    serial_t    *serial;
    Fifo8        resp;
    pc_timer_t   host_to_serial_timer;
    pc_timer_t   reset_timer;
} mouse_microtouch_t;

static mouse_microtouch_t *mtouch_inst = NULL;

static void
mtouch_savenvr(void *priv)
{
    mouse_microtouch_t *dev = (mouse_microtouch_t *) priv;

    FILE *fp;

    fp = nvr_fopen(dev->nvr_path, "wb");
    if (fp) {
        fwrite(dev->nvr, 1, NVR_SIZE, fp);
        fclose(fp);
        fp = NULL;
    }
}

static void
mtouch_writenvr(void *priv, float scale_x, float scale_y, float off_x, float off_y)
{
    mouse_microtouch_t *dev = (mouse_microtouch_t *) priv;

    memcpy(&dev->nvr[0], &scale_x, 4);
    memcpy(&dev->nvr[4], &scale_y, 4);
    memcpy(&dev->nvr[8], &off_x, 4);
    memcpy(&dev->nvr[12], &off_y, 4);
}

static void
mtouch_readnvr(void *priv)
{
    mouse_microtouch_t *dev = (mouse_microtouch_t *) priv;
    memcpy(&dev->scale_x, &dev->nvr[0], 4);
    memcpy(&dev->scale_y, &dev->nvr[4], 4);
    memcpy(&dev->off_x, &dev->nvr[8], 4);
    memcpy(&dev->off_y, &dev->nvr[12], 4);

    pclog("MT NVR CAL: scale_x=%f, scale_y=%f, off_x=%f, off_y=%f\n", dev->scale_x, dev->scale_y, dev->off_x, dev->off_y);
}

static void
mtouch_initnvr(void *priv)
{
    mouse_microtouch_t *dev = (mouse_microtouch_t *) priv;
    FILE *fp;

    /* Allocate and initialize the EEPROM. */
    dev->nvr = (uint8_t *) calloc(1, NVR_SIZE);

    fp = nvr_fopen(dev->nvr_path, "rb");
    if (fp) {
        /* Files from before the settings bytes hold only the calibration. */
        if (fread(dev->nvr, 1, NVR_SIZE, fp) < 16)
            mtouch_writenvr(dev, 1, 1, 0, 0);
        fclose(fp);
        fp = NULL;
    } else
        mtouch_writenvr(dev, 1, 1, 0, 0);
}

static int
mtouch_baud_from_code(char c)
{
    switch (c) {
        case '1': return 19200;
        case '3': return 4800;
        case '4': return 2400;
        case '5': return 1200;
        default:  return 9600;
    }
}

static char
mtouch_code_from_baud(int baud)
{
    switch (baud) {
        case 19200: return '1';
        case 4800:  return '3';
        case 2400:  return '4';
        case 1200:  return '5';
        default:    return '2';
    }
}

/* Parameter Lock, Parameter Set and Sensitivity Set store these. */
static void
mtouch_store_settings(mouse_microtouch_t *dev)
{
    dev->nvr[NVR_MAGIC]       = 'M';
    dev->nvr[NVR_FORMAT]      = dev->format;
    dev->nvr[NVR_MODE]        = dev->mode;
    dev->nvr[NVR_FLAGS]       = (dev->mode_status ? 1 : 0) | (dev->autobaud ? 2 : 0);
    dev->nvr[NVR_BAUD]        = (uint8_t) mtouch_code_from_baud(dev->baud_rate);
    dev->nvr[NVR_SENSITIVITY] = dev->sensitivity;
    dev->nvr[NVR_FILTER]      = dev->filter;
    dev->nvr[NVR_SERIAL]      = dev->serial_cfg;
    mtouch_savenvr(dev);
}

static void
mtouch_set_baud(mouse_microtouch_t *dev, int baud)
{
    dev->baud_rate = baud;
    timer_stop(&dev->host_to_serial_timer);
    timer_on_auto(&dev->host_to_serial_timer, (1000000. / dev->baud_rate) * 10);
}

/* Factory defaults (Table 11): SMT2 Decimal, the others Tablet; Mode Stream;
   Pen or Finger; 9600 baud; normal sensitivity. */
static void
mtouch_defaults(mouse_microtouch_t *dev)
{
    dev->format      = (dev->id < 2) ? FORMAT_DEC : FORMAT_TABLET;
    dev->prev_format = dev->format;
    dev->mode        = MODE_STREAM;
    dev->mode_status = false;
    dev->autobaud    = false;
    dev->pen_mode    = 3;
    dev->sensitivity = 0;
    dev->filter      = 0;
    dev->serial_cfg  = (dev->id < 2) ? 0x0c : 0x00; /* SMT2: N72, SMT3: N81 */
}

static void
mtouch_load_settings(mouse_microtouch_t *dev)
{
    mtouch_defaults(dev);
    if (dev->nvr[NVR_MAGIC] != 'M')
        return;
    if ((dev->nvr[NVR_FORMAT] >= FORMAT_DEC) && (dev->nvr[NVR_FORMAT] <= FORMAT_ZONE) && (dev->nvr[NVR_FORMAT] != FORMAT_RAW))
        dev->format = dev->prev_format = dev->nvr[NVR_FORMAT];
    if ((dev->nvr[NVR_MODE] >= MODE_DOWNUP) && (dev->nvr[NVR_MODE] <= MODE_POLLED))
        dev->mode = dev->nvr[NVR_MODE];
    dev->mode_status = !!(dev->nvr[NVR_FLAGS] & 1);
    dev->autobaud    = !!(dev->nvr[NVR_FLAGS] & 2);
    dev->baud_rate   = mtouch_baud_from_code((char) dev->nvr[NVR_BAUD]);
    dev->sensitivity = dev->nvr[NVR_SENSITIVITY] & 3;
    dev->filter      = dev->nvr[NVR_FILTER];
    dev->serial_cfg  = dev->nvr[NVR_SERIAL];
}

static void
mtouch_ack(mouse_microtouch_t *dev, char c)
{
    fifo8_push(&dev->resp, 0x01);
    fifo8_push(&dev->resp, (uint8_t) c);
    fifo8_push(&dev->resp, 0x0d);
}

static void
mtouch_reset_complete(void *priv)
{
    mouse_microtouch_t *dev = (mouse_microtouch_t *) priv;

    dev->reset = true;
    dev->ut_reset = true;
    dev->in_reset = false;
    mtouch_ack(dev, '0');
}

/* ---- reports ---------------------------------------------------------------- */

/* Status byte of Mode Status / Format Binary: ^Y touchdown, ^\ continued, ^R liftoff. */
static uint8_t
mtouch_status_byte(int ev)
{
    return (ev == EV_DOWN) ? 0x19 : ((ev == EV_UP) ? 0x18 : 0x1c);
}

static void
mtouch_push_1023(mouse_microtouch_t *dev, int v)
{
    fifo8_push(&dev->resp, 0x20 | ((v >> 5) & 0x1f));
    fifo8_push(&dev->resp, 0x20 | (v & 0x1f));
}

/* Calibrate Raw coordinate: 10 bits and a sign, -1024..1023. */
static void
mtouch_push_signed(mouse_microtouch_t *dev, double v01)
{
    int v = (int) (v01 * 2047.) - 1024;

    if (v < -1024)
        v = -1024;
    if (v > 1023)
        v = 1023;
    fifo8_push(&dev->resp, ((v & 0x0f) << 3) | ((v >> 4) & 0x07));
    fifo8_push(&dev->resp, ((v < 0) ? 0x40 : 0x00) | (((v >> 7) & 0x07) << 3));
}

static void
mtouch_report(mouse_microtouch_t *dev, int ev, double x, double y, double rx, double ry, bool zone)
{
    char     buffer[16];
    uint16_t x14, y14;
    int      x10, y10;
    int      but = dev->but;

    switch (dev->format) {
        case FORMAT_TABLET:
            /* Bit 3 is set in every Tablet report: 3M's TouchWare driver of 2002
               (TWDrv.o in MAXX Ruby 2 V11.00) reads a clear bit 3 as the 11-byte
               "double touch" report; the 2003 driver calls a report a double touch
               only when bits 4:3 are 10, so both take 0xC8/0x88. */
            x14 = (uint16_t) (16383 * x);
            y14 = (uint16_t) (16383 * (1 - y));
            fifo8_push(&dev->resp, 0x88 | ((ev != EV_UP) ? 0x40 : 0x00) |
                       ((dev->pen_mode == 2) ? ((1 << 5) | ((ev != EV_UP) ? (but & 3) : 0)) : 0));
            fifo8_push(&dev->resp, x14 & 0x7f);
            fifo8_push(&dev->resp, (x14 >> 7) & 0x7f);
            fifo8_push(&dev->resp, y14 & 0x7f);
            fifo8_push(&dev->resp, (y14 >> 7) & 0x7f);
            break;

        case FORMAT_DEC:
        case FORMAT_HEX:
            fifo8_push(&dev->resp, dev->mode_status ? mtouch_status_byte(ev) : 0x01);
            if (dev->format == FORMAT_DEC)
                snprintf(buffer, sizeof(buffer), "%03d,%03d\r", (uint16_t) (999 * x), (uint16_t) (999 * (1 - y)));
            else
                snprintf(buffer, sizeof(buffer), "%03X,%03X\r", (uint16_t) (1023 * x), (uint16_t) (1023 * (1 - y)));
            fifo8_push_all(&dev->resp, (uint8_t *) buffer, strlen(buffer));
            break;

        case FORMAT_BINARY:
        case FORMAT_ZONE:
            x10 = (int) (1023 * x);
            y10 = (int) (1023 * (1 - y));
            if ((dev->format == FORMAT_BINARY) && (ev == EV_DOWN))
                fifo8_push_all(&dev->resp, (uint8_t *) "\x17\x20\x20\x20\x20", 5);
            if ((dev->format == FORMAT_ZONE) && !dev->mode_status) {
                static const char zone_in[]  = { 0, 'D', 'B', 'A' };
                static const char zone_out[] = { 0, 'L', 'J', 'I' };
                fifo8_push(&dev->resp, (uint8_t) (zone ? zone_in[ev] : zone_out[ev]));
            } else
                fifo8_push(&dev->resp, mtouch_status_byte(ev));
            mtouch_push_1023(dev, x10);
            mtouch_push_1023(dev, y10);
            break;

        case FORMAT_CAL_RAW:
            fifo8_push(&dev->resp, 0x80 | ((ev != EV_UP) ? 0x40 : 0x00) |
                       ((dev->pen_mode == 2) ? ((1 << 5) | ((ev != EV_UP) ? (but & 3) : 0)) : 0));
            mtouch_push_signed(dev, rx);
            mtouch_push_signed(dev, 1 - ry);
            break;

        default:
            break;
    }
}

/* Format Raw: the four corner signal levels, one 7-byte packet per call. */
static void
mtouch_report_raw(mouse_microtouch_t *dev)
{
    double x = dev->raw_x, y = dev->raw_y, amount = dev->but ? (500. + 100. * dev->sensitivity) : 0.;
    int    ul = 200 + (int) (amount * (1 - x) * (1 - y));
    int    ur = 200 + (int) (amount * x * (1 - y));
    int    ll = 200 + (int) (amount * (1 - x) * y);
    int    lr = 200 + (int) (amount * x * y);

    fifo8_push(&dev->resp, 0x80 | 0x0a); /* drive level */
    fifo8_push(&dev->resp, ((ul >> 7) & 7) | (((ll >> 7) & 7) << 4));
    fifo8_push(&dev->resp, ((lr >> 7) & 7) | (((ur >> 7) & 7) << 4));
    fifo8_push(&dev->resp, ll & 0x7f);
    fifo8_push(&dev->resp, ul & 0x7f);
    fifo8_push(&dev->resp, ur & 0x7f);
    fifo8_push(&dev->resp, lr & 0x7f);
}

/* ---- calibration ------------------------------------------------------------ */

/* A calibration touch ends: the controller uses the point just before liftoff. */
static void
mtouch_calibration_point(mouse_microtouch_t *dev)
{
    int    idx   = 2 - dev->cal_cntr; /* 0 = lower left, 1 = upper right */
    double x     = dev->last_raw_x, y = dev->last_raw_y;
    bool   dec   = (dev->format == FORMAT_DEC) || (dev->format == FORMAT_HEX);
    char   pos   = '1', neg = '0';
    bool   valid;

    /* Calibrate New / Interactive, upper right, Decimal or Hexadecimal: the
       positive response is 0 and the negative 1 (Table 7). */
    if ((dev->cal_type != CAL_EXTENDED) && (idx == 1) && dec) {
        pos = '0';
        neg = '1';
    }

    /* Too short a touch for an accurate point (under ~30 ms: while calibrating
       the controller samples once per byte slot): the SMT3 and TouchPen answer
       2, the others nothing. */
    if (dev->touch_samples < 30) {
        if (dev->id >= 2)
            mtouch_ack(dev, '2');
        return;
    }

    if (dev->cal_type == CAL_INTERACTIVE)
        valid = true;
    else if (idx == 0)
        valid = (x < (1. / 3.)) && (y > (2. / 3.));
    else
        valid = (x > (2. / 3.)) && (y < (1. / 3.));
    if (!valid) {
        mtouch_ack(dev, neg);
        return;
    }

    dev->cal_x[idx] = x;
    dev->cal_y[idx] = y;
    mtouch_ack(dev, pos);
    if (--dev->cal_cntr)
        return;

    double x1 = dev->cal_x[0], y1 = dev->cal_y[0];
    double x2 = dev->cal_x[1], y2 = dev->cal_y[1];
    double inset = (dev->cal_type == CAL_EXTENDED) ? 0.125 : 0.;

    if (dev->cal_type == CAL_INTERACTIVE) {
        /* Swap the points if the lower left and upper right are reversed. */
        if (x1 > x2) {
            double t = x1;
            x1 = x2;
            x2 = t;
        }
        if (y1 < y2) {
            double t = y1;
            y1 = y2;
            y2 = t;
        }
    }
    if ((x2 - x1) < 0.05 || (y1 - y2) < 0.05)
        return; /* the SMT3 keeps its previous calibration (Table 6) */

    dev->scale_x = (float) (((1. - inset) - inset) / (x2 - x1));
    dev->off_x   = (float) (inset - dev->scale_x * x1);
    dev->scale_y = (float) ((inset - (1. - inset)) / (y2 - y1));
    dev->off_y   = (float) ((1. - inset) - dev->scale_y * y1);

    pclog("MT NEW CAL: scale_x=%f, scale_y=%f, off_x=%f, off_y=%f\n", dev->scale_x, dev->scale_y, dev->off_x, dev->off_y);
    mtouch_writenvr(dev, dev->scale_x, dev->scale_y, dev->off_x, dev->off_y);
    mtouch_savenvr(dev);
}

/* ---- commands --------------------------------------------------------------- */

static void
mtouch_set_format(mouse_microtouch_t *dev, uint8_t format)
{
    if ((format == FORMAT_DEC) || (format == FORMAT_HEX) || (format == FORMAT_ZONE)) {
        /* Resets Mode Status (Format Zone reports its zone letters until a
           Mode Status follows); after Format Binary, sets Mode Stream. */
        if (dev->format == FORMAT_BINARY)
            dev->mode = MODE_STREAM;
        dev->mode_status = false;
    }
    if ((format != FORMAT_RAW) && (format != FORMAT_CAL_RAW))
        dev->prev_format = format;
    dev->format = format;
}

static void
mtouch_process_commands(mouse_microtouch_t *dev)
{
    const char *c = dev->cmd;
    size_t      len;

    dev->cmd[strcspn(dev->cmd, "\r")] = '\0';
    len = strlen(c);
    pclog("MT Command: %s\n", dev->cmd);

    if (!strcmp(c, "AD")) {                               /* AutoBaud Disable */
        dev->autobaud = false;
    } else if (!strcmp(c, "AE")) {                        /* AutoBaud Enable */
        dev->autobaud = true;
    } else if (!strcmp(c, "CN") || !strcmp(c, "CX") || !strcmp(c, "CI")) { /* Calibrate New / Extended / Interactive */
        dev->cal_type = (c[1] == 'N') ? CAL_NEW : ((c[1] == 'X') ? CAL_EXTENDED : CAL_INTERACTIVE);
        dev->cal_cntr = 2;
    } else if (!strcmp(c, "CR")) {                        /* Calibrate Raw */
        mtouch_set_format(dev, FORMAT_CAL_RAW);
    } else if ((c[0] == 'F') && (c[1] == 'N') && isdigit((uint8_t) c[2])) { /* Filter Number */
        dev->filter = (uint8_t) atoi(&c[2]);
    } else if (!strcmp(c, "FO")) {                        /* Finger Only */
        dev->pen_mode = 1;
    } else if (!strcmp(c, "FB") || !strcmp(c, "FBS")) {   /* Format Binary [Stream] */
        mtouch_set_format(dev, FORMAT_BINARY);
        dev->mode        = (c[2] == 'S') ? MODE_STREAM : MODE_POLLED;
        dev->mode_status = true;
    } else if (!strcmp(c, "FD")) {                        /* Format Decimal */
        mtouch_set_format(dev, FORMAT_DEC);
    } else if (!strcmp(c, "FH")) {                        /* Format Hexadecimal */
        mtouch_set_format(dev, FORMAT_HEX);
    } else if (!strcmp(c, "FR")) {                        /* Format Raw (streams until Reset) */
        mtouch_set_format(dev, FORMAT_RAW);
        dev->cal_cntr = 0;
    } else if (!strcmp(c, "FT")) {                        /* Format Tablet */
        mtouch_set_format(dev, FORMAT_TABLET);
    } else if (!strcmp(c, "FZ")) {                        /* Format Zone */
        mtouch_set_format(dev, FORMAT_ZONE);
        dev->mode = MODE_STREAM;
    } else if ((c[0] == 0x03) && (c[1] == 'F')) {         /* Frequency Adjust (^C Fnn) */
        /* no analog front end to retune */
    } else if (!strcmp(c, "GP1")) {                       /* Get Parameter Block 1 (undocumented; dummy) */
        mtouch_ack(dev, 'A');
        fifo8_push_all(&dev->resp, (uint8_t *) "0000000000000000000000000\r", 26);
    } else if (!strcmp(c, "MDU")) {                       /* Mode Down/Up */
        dev->mode = MODE_DOWNUP;
    } else if (!strcmp(c, "MI")) {                        /* Mode Inactive */
        dev->mode = MODE_INACTIVE;
    } else if (!strcmp(c, "MP")) {                        /* Mode Point */
        dev->mode = MODE_POINT;
    } else if (!strcmp(c, "MQ")) {                        /* Mode Polled */
        dev->mode = MODE_POLLED;
        dev->xon = dev->pq_down = dev->pq_up = false;
    } else if (!strcmp(c, "MT")) {                        /* Mode Status */
        dev->mode_status = true;
    } else if (!strcmp(c, "MS")) {                        /* Mode Stream */
        dev->mode = MODE_STREAM;
    } else if (!strcmp(c, "OI")) {                        /* Output Identity */
        fifo8_push(&dev->resp, 0x01);
        fifo8_push_all(&dev->resp, (uint8_t *) mtouch_identity[dev->id], 6);
        fifo8_push(&dev->resp, 0x0D);
        return;
    } else if (!strcmp(c, "OS")) {                        /* Output Status */
        /* a: bit 6 always 1, bit 5 power-on flag (SMT2 only, cleared by OS);
           b: bit 6 always 1, bit 5 software reset flag. */
        fifo8_push(&dev->resp, 0x01);
        fifo8_push(&dev->resp, 0x40 | ((dev->power_on && (dev->id < 2)) ? 0x20 : 0x00));
        fifo8_push(&dev->resp, 0x40 | (dev->reset ? 0x20 : 0x00));
        fifo8_push(&dev->resp, 0x0D);
        dev->power_on = false;
        return;
    } else if (!strcmp(c, "PL")) {                        /* Parameter Lock */
        mtouch_store_settings(dev);
    } else if (!strcmp(c, "PO")) {                        /* Pen Only */
        dev->pen_mode = 2;
    } else if (!strcmp(c, "PF")) {                        /* Pen or Finger */
        dev->pen_mode = 3;
    } else if ((c[0] == 'P') && ((len == 4) || (len == 5)) && strchr("NOE", c[1]) &&
               ((c[2] == '7') || (c[2] == '8')) && ((c[3] == '1') || (c[3] == '2'))) { /* Parameter Set */
        dev->serial_cfg = ((c[1] == 'O') ? 1 : ((c[1] == 'E') ? 2 : 0)) | ((c[2] == '7') ? 4 : 0) | ((c[3] == '2') ? 8 : 0);
        mtouch_ack(dev, '0');
        if (len == 5)
            mtouch_set_baud(dev, mtouch_baud_from_code(c[4]));
        mtouch_store_settings(dev);
        return;
    } else if (!strcmp(c, "R") || !strcmp(c, "RD")) {     /* Reset / Restore Defaults */
        /* Stops sending data; cancels calibration, Format Raw and Calibrate Raw. */
        fifo8_reset(&dev->resp);
        dev->in_reset = true;
        dev->cal_cntr = 0;
        dev->xon = dev->pq_down = dev->pq_up = false;
        dev->but_old = 0;
        if ((dev->format == FORMAT_RAW) || (dev->format == FORMAT_CAL_RAW))
            dev->format = dev->prev_format;
        if (c[1] == 'D') {
            /* Factory defaults, including the factory calibration. */
            mtouch_defaults(dev);
            if (dev->baud_rate != 9600)
                mtouch_set_baud(dev, 9600);
            dev->scale_x = dev->scale_y = 1;
            dev->off_x = dev->off_y = 0;
            mtouch_writenvr(dev, 1, 1, 0, 0);
            mtouch_store_settings(dev);
        }
        timer_on_auto(&dev->reset_timer, 500. * 1000.);
        return;
    } else if ((c[0] == 'S') && (c[1] == 'E') && isdigit((uint8_t) c[2])) { /* Sensitivity Set */
        dev->sensitivity = (c[2] - '0') & 3;
        mtouch_store_settings(dev);
    } else if (!strcmp(c, "SP1")) {                       /* Set Parameter Block 1 (undocumented) */
        mtouch_ack(dev, 'A');
        return;
    } else if (!strcmp(c, "UT") || !strcmp(c, "UV")) {    /* Unit Type [Verify] */
        char ut[12];
        /* TouchPen status bit 5: no Unit Type since the last reset. */
        snprintf(ut, sizeof(ut), "%s****%02X", (dev->id == 2) ? "TP" : "QM",
                 ((dev->id == 2) && dev->ut_reset) ? 0x20 : 0x00);
        dev->ut_reset = false;
        fifo8_push(&dev->resp, 0x01);
        fifo8_push_all(&dev->resp, (uint8_t *) ut, 8);
        fifo8_push(&dev->resp, 0x0D);
        return;
    }
    /* Z (Null Command), NM (sent by 3M TouchWare, undocumented) and anything
       else get the positive response. */

    mtouch_ack(dev, '0');
}

static void
mtouch_write(UNUSED(serial_t *serial), void *priv, uint8_t data)
{
    mouse_microtouch_t *dev = (mouse_microtouch_t *) priv;

    if (data == '\x1') {
        dev->soh = true;
        dev->cmd_pos = 0;
    }
    else if (dev->soh) {
        if (data != '\r') {
            if (dev->cmd_pos < sizeof(dev->cmd) - 2) {
                dev->cmd[dev->cmd_pos++] = data;
            }
        } else {
            dev->soh = false;

            if (!dev->cmd_pos) {
                return;
            }

            dev->cmd[dev->cmd_pos++] = data;
            dev->cmd[dev->cmd_pos] = '\0';
            dev->cmd_pos = 0;
            mtouch_process_commands(dev);
        }
    }
    else if (data == 0x11) {
        /* XON (^Q): Mode Polled asks for one packet. */
        dev->xon = true;
    }
}

/* ---- touch reporting -------------------------------------------------------- */

static int
mtouch_prepare_transmit(void *priv)
{
    mouse_microtouch_t *dev = (mouse_microtouch_t *) priv;

    int ev = EV_NONE;

    if (dev->format == FORMAT_RAW) {
        /* Format Raw streams corner data continuously, touched or not. */
        mtouch_report_raw(dev);
        return 0;
    }

    if (dev->but && !dev->but_old)
        ev = EV_DOWN;
    else if (dev->but)
        ev = EV_CONT;
    else if (dev->but_old)
        ev = EV_UP;
    dev->but_old = dev->but;

    if (ev == EV_DOWN)
        dev->touch_samples = 0;
    if ((ev == EV_DOWN) || (ev == EV_CONT)) {
        dev->touch_samples++;
        dev->last_x       = dev->abs_x;
        dev->last_y       = dev->abs_y;
        dev->last_raw_x   = dev->raw_x;
        dev->last_raw_y   = dev->raw_y;
        dev->last_in_zone = dev->in_zone;
    }

    if (dev->cal_cntr) {
        /* Calibrating: no coordinates, one acknowledgement per touch. */
        if (ev == EV_UP)
            mtouch_calibration_point(dev);
        return 0;
    }

    switch (dev->mode) {
        case MODE_STREAM:
            if (ev != EV_NONE)
                mtouch_report(dev, ev, dev->last_x, dev->last_y, dev->last_raw_x, dev->last_raw_y, dev->last_in_zone);
            break;
        case MODE_POINT:
            if (ev == EV_DOWN)
                mtouch_report(dev, ev, dev->last_x, dev->last_y, dev->last_raw_x, dev->last_raw_y, dev->last_in_zone);
            break;
        case MODE_DOWNUP:
            if ((ev == EV_DOWN) || (ev == EV_UP))
                mtouch_report(dev, ev, dev->last_x, dev->last_y, dev->last_raw_x, dev->last_raw_y, dev->last_in_zone);
            break;
        case MODE_POLLED:
            /* One touchdown and one liftoff are remembered (the most recent);
               each ^Q sends one packet: the touchdown, then a continued touch
               per ^Q while touching, then the liftoff. */
            if (ev == EV_DOWN) {
                dev->pq_down      = true;
                dev->pq_up        = false;
                dev->pq_down_x    = dev->last_x;
                dev->pq_down_y    = dev->last_y;
                dev->pq_down_zone = dev->last_in_zone;
            } else if (ev == EV_UP) {
                dev->pq_up      = true;
                dev->pq_up_x    = dev->last_x;
                dev->pq_up_y    = dev->last_y;
                dev->pq_up_zone = dev->last_in_zone;
            }
            if (dev->xon) {
                if (dev->pq_down) {
                    mtouch_report(dev, EV_DOWN, dev->pq_down_x, dev->pq_down_y, dev->last_raw_x, dev->last_raw_y, dev->pq_down_zone);
                    dev->pq_down = false;
                    dev->xon     = false;
                } else if (dev->but && (ev != EV_DOWN)) {
                    mtouch_report(dev, EV_CONT, dev->last_x, dev->last_y, dev->last_raw_x, dev->last_raw_y, dev->last_in_zone);
                    dev->xon = false;
                } else if (dev->pq_up) {
                    mtouch_report(dev, EV_UP, dev->pq_up_x, dev->pq_up_y, dev->last_raw_x, dev->last_raw_y, dev->pq_up_zone);
                    dev->pq_up = false;
                    dev->xon   = false;
                }
            }
            break;
        default: /* MODE_INACTIVE */
            break;
    }
    return 0;
}

static void
mtouch_write_to_host(void *priv)
{
    mouse_microtouch_t *dev = (mouse_microtouch_t *) priv;

    if (dev->serial == NULL)
        goto no_write_to_machine;
    if ((dev->serial->type >= SERIAL_16550) && dev->serial->fifo_enabled) {
        if (fifo_get_full(dev->serial->rcvr_fifo)) {
            goto no_write_to_machine;
        }
    } else {
        if (dev->serial->lsr & 1) {
            goto no_write_to_machine;
        }
    }
    if (dev->in_reset) {
        goto no_write_to_machine;
    }
    /* serial_write_fifo() only places the byte in the receive shift register
       (out_new); the UART's receive timer moves it into RBR or the FIFO.
       Writing again before that has happened overwrites the previous byte.
       Losing the status byte of a liftoff report makes drivers miss the
       liftoff, so wait until the receive shift register is free. */
    if (dev->serial->out_new != 0xffff) {
        goto no_write_to_machine;
    }
    if (fifo8_num_used(&dev->resp)) {
        serial_write_fifo(dev->serial, fifo8_pop(&dev->resp));
    }
    else {
        mtouch_prepare_transmit(dev);
    }

no_write_to_machine:
    timer_on_auto(&dev->host_to_serial_timer, (1000000.0 / (double) dev->baud_rate) * (double) (1 + 8 + 1));
}

static int
mtouch_poll(void *priv)
{
    mouse_microtouch_t *dev = (mouse_microtouch_t *) priv;

    /* A press seen since the last poll counts even if the button is
       already up again, so a very quick tap still gives a touchdown and a
       liftoff instead of nothing. */
    dev->but = tablet_get_buttons_ex() | tablet_take_pressed();
    mouse_get_abs_coords(&dev->raw_x, &dev->raw_y);

    if (enable_overscan && mouse_tablet_in_proximity > 0) {
        int index = mouse_tablet_in_proximity - 1;

        dev->raw_x *= monitors[index].mon_unscaled_size_x - 1;
        dev->raw_y *= monitors[index].mon_efscrnsz_y - 1;

        if (dev->raw_x <= (monitors[index].mon_overscan_x / 2.)) {
            dev->raw_x = (monitors[index].mon_overscan_x / 2.);
        }
        if (dev->raw_y <= (monitors[index].mon_overscan_y / 2.)) {
            dev->raw_y = (monitors[index].mon_overscan_y / 2.);
        }
        dev->raw_x -= (monitors[index].mon_overscan_x / 2.);
        dev->raw_y -= (monitors[index].mon_overscan_y / 2.);
        dev->raw_x = dev->raw_x / (double) monitors[index].mon_xsize;
        dev->raw_y = dev->raw_y / (double) monitors[index].mon_ysize;
    }

    if (dev->raw_x >= 1.0) dev->raw_x = 1.0;
    if (dev->raw_y >= 1.0) dev->raw_y = 1.0;
    if (dev->raw_x <= 0.0) dev->raw_x = 0.0;
    if (dev->raw_y <= 0.0) dev->raw_y = 0.0;

    dev->abs_x = dev->scale_x * dev->raw_x + dev->off_x;
    dev->abs_y = dev->scale_y * dev->raw_y + dev->off_y;

    /* Format Zone: inside or outside the calibrated area. */
    dev->in_zone = (dev->abs_x >= 0.0) && (dev->abs_x <= 1.0) && (dev->abs_y >= 0.0) && (dev->abs_y <= 1.0);

    if (dev->abs_x >= 1.0) dev->abs_x = 1.0;
    if (dev->abs_y >= 1.0) dev->abs_y = 1.0;
    if (dev->abs_x <= 0.0) dev->abs_x = 0.0;
    if (dev->abs_y <= 0.0) dev->abs_y = 0.0;

    return 0;
}

static int
mtouch_poll_global(void* arg)
{
    (void)arg;
    return mtouch_poll(mtouch_inst);
}

void *
mtouch_init(UNUSED(const device_t *info))
{
    mouse_microtouch_t *dev = calloc(1, sizeof(mouse_microtouch_t));

    dev->serial = serial_attach(device_get_config_int("port"), NULL, mtouch_write, dev);
    if (dev->serial) {
        serial_set_cts(dev->serial, 1);
        serial_set_dsr(dev->serial, 1);
        serial_set_dcd(dev->serial, 1);
    }

    dev->id = device_get_config_int("identity");
    snprintf(dev->nvr_path, sizeof(dev->nvr_path), "mtouch_%s.nvr", mtouch_identity[dev->id]);
    mtouch_initnvr(dev);
    mtouch_readnvr(dev);
    mtouch_load_settings(dev);
    dev->power_on = true;
    dev->ut_reset = true;

    fifo8_create(&dev->resp, 256);
    timer_add(&dev->host_to_serial_timer, mtouch_write_to_host, dev, 0);
    timer_add(&dev->reset_timer, mtouch_reset_complete, dev, 0);
    if (!dev->baud_rate)
        dev->baud_rate = 9600;
    timer_on_auto(&dev->host_to_serial_timer, (1000000. / dev->baud_rate) * 10);

    mouse_set_buttons(2);
    mouse_set_poll_ex(mtouch_poll_global, dev);
    mtouch_inst = dev;

    return dev;
}

void
mtouch_close(void *priv)
{
    mouse_microtouch_t *dev = (mouse_microtouch_t *) priv;
    
    fifo8_destroy(&dev->resp);
    /* Detach serial port from the mouse. */
    if (dev && dev->serial && dev->serial->sd) {
        memset(dev->serial->sd, 0, sizeof(serial_device_t));
    }
    
    if (dev->nvr != NULL)
        free(dev->nvr);
    
    free(dev);
    mtouch_inst = NULL;
}

static const device_config_t mtouch_config[] = {
  // clang-format off
    {
        .name           = "port",
        .description    = "Serial Port",
        .type           = CONFIG_SELECTION,
        .default_string = NULL,
        .default_int    = 0,
        .file_filter    = NULL,
        .spinner        = { 0 },
        .selection      = {
            { .description = "COM1", .value = 0 },
            { .description = "COM2", .value = 1 },
            { .description = "COM3", .value = 2 },
            { .description = "COM4", .value = 3 },
            { .description = ""                 }
        },
        .bios           = { { 0 } }
    },
    {
        .name           = "identity",
        .description    = "Controller",
        .type           = CONFIG_SELECTION,
        .default_string = NULL,
        .default_int    = 0,
        .file_filter    = NULL,
        .spinner        = { 0 },
        .selection      = {
            { .description = "A3 - SMT2 Serial / SMT3(R)V", .value = 0 },
            { .description = "A4 - SMT2 PCBus",             .value = 1 },
            { .description = "P5 - TouchPen 4(+)",          .value = 2 },
            { .description = "Q1 - SMT3(R) Serial",         .value = 3 },
            { .description = ""                                        }
        },
        .bios           = { { 0 } }
    },
    { .name = "", .description = "", .type = CONFIG_END }
  // clang-format on
};

const device_t mouse_mtouch_device = {
    .name          = "3M MicroTouch (Serial)",
    .internal_name = "microtouch_touchpen",
    .flags         = DEVICE_COM,
    .local         = 0,
    .init          = mtouch_init,
    .close         = mtouch_close,
    .reset         = NULL,
    .available     = NULL,
    .speed_changed = NULL,
    .force_redraw  = NULL,
    .config        = mtouch_config
};