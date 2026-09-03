/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          Elo TouchSystems SmartSet Serial touchscreen emulation.
 *
 * Authors: Claude (Anthropic)
 *
 *          Copyright 2026 Claude (Anthropic).
 */

/* Reference: Elo TouchSystems, "SmartSet Touchscreen Controller Family
 *            Technical Reference Manual", 1993 (Info/SmartSet.PDF in the
 *            86Box-ELO workspace). This implements the native "SmartSet"
 *            binary serial protocol used by the E271-2200/E271-2210
 *            controllers (10-byte packet: 0x55 lead, 8 command/response
 *            bytes, checksum), which is also what the Linux kernel's
 *            drivers/input/touchscreen/elo.c calls its "10-byte" format.
 *
 * TODO:
 *   - Calibration/Scaling "Offset,Numerator,Denominator" direct set/query
 *     sub-form (axis given in lowercase) is not implemented; only
 *     "set by range" (axis uppercase + Low/High words) and two-point
 *     auto-calibrate ('C2') are. Add if a real driver is found to need it.
 *   - Report/Filter/Timer/Key/Low Power commands are accept-and-echo stubs
 *     with no functional effect beyond storing the value.
 *   - Configuration ('g') dump/restore is not implemented (responds with
 *     zero packets, matching ID's reported packet count of 0).
 *   - Checksum on incoming packets is never validated (matches factory
 *     default: validation is opt-in via the Parameter command).
 */
#include <ctype.h>
#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <stdbool.h>
#include <math.h>
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

#define ELO_RAW_MAX 4095

enum elo_mode1_bits {
    ELO_M1_INITIAL   = 0x01,
    ELO_M1_STREAM    = 0x02,
    ELO_M1_UNTOUCH   = 0x04,
    ELO_M1_WARNING   = 0x10,
    ELO_M1_RANGECHK  = 0x40,
    ELO_M1_ZRESERVED = 0x80
};

enum elo_mode2_bits {
    ELO_M2_TRIM  = 0x02,
    ELO_M2_CAL   = 0x04,
    ELO_M2_SCALE = 0x08,
    ELO_M2_TRACK = 0x40
};

typedef struct mouse_elo_t {
    /* Packet framing state. */
    uint8_t body[8];
    int     cmd_pos; /* -1 = waiting for lead byte, 0..7 = body index, 8 = waiting checksum */

    /* Response FIFO -> host, paced at baud rate. */
    Fifo8 resp;

    /* Serial port. */
    serial_t *serial;
    int       baud_rate;
    uint8_t   ser1, ser2; /* raw Parameter bytes, echoed back verbatim */
    bool      in_reset;

    /* Modes. */
    uint8_t mode1, mode2;
    bool    quiet_all, quiet_timer_flag, quiet_touch;

    /* Calibration (raw 0..4095 native space -> 0..4095 "calibrated" space). */
    bool    calibration_enabled;
    int32_t cal_lo[2], cal_hi[2];
    bool    swap_axes;

    /* Two-point auto-calibration ('C2') in progress. */
    bool    cal_active;
    int     cal_step;
    bool    cal_but_old;
    int32_t cal_points[2][2];

    /* Scaling (0..4095 calibrated space -> arbitrary signed output range). */
    bool    scaling_enabled;
    int32_t scale_lo[2], scale_hi[2];
    uint8_t invert_mask;

    /* Misc stubs (stored/echoed, not functionally wired up). */
    uint8_t  untouch_delay, rep_delay;
    uint8_t  diag_result;
    uint8_t  filter_slen, filter_width, filter_states, filter_control;
    uint8_t  timer_enable, timer_mode;
    uint16_t timer_interval;
    uint8_t  key_value;
    uint8_t  low_power;
    char     owner[7];

    /* Touch/pointer state. */
    int     but, but_old;
    int32_t raw_x, raw_y, raw_x_old, raw_y_old;

    char nvr_path[64];

    pc_timer_t host_to_serial_timer;
    pc_timer_t reset_timer;
} mouse_elo_t;

static mouse_elo_t *elo_inst = NULL;

static void
elo_save_nvr(void *priv)
{
    mouse_elo_t *dev = (mouse_elo_t *) priv;
    FILE        *fp;
    int32_t      buf[8];

    buf[0] = dev->cal_lo[0];
    buf[1] = dev->cal_hi[0];
    buf[2] = dev->cal_lo[1];
    buf[3] = dev->cal_hi[1];
    buf[4] = dev->scale_lo[0];
    buf[5] = dev->scale_hi[0];
    buf[6] = dev->scale_lo[1];
    buf[7] = dev->scale_hi[1];

    fp = nvr_fopen(dev->nvr_path, "wb");
    if (fp) {
        fwrite(buf, sizeof(buf), 1, fp);
        fclose(fp);
    }
}

static void
elo_load_defaults(mouse_elo_t *dev)
{
    dev->mode1               = ELO_M1_ZRESERVED | ELO_M1_INITIAL | ELO_M1_STREAM | ELO_M1_UNTOUCH;
    dev->mode2               = 0;
    dev->calibration_enabled = false;
    dev->scaling_enabled     = false;
    dev->swap_axes           = false;
    dev->invert_mask         = 0;
    dev->cal_lo[0] = dev->cal_lo[1] = 0;
    dev->cal_hi[0] = dev->cal_hi[1] = ELO_RAW_MAX;
    dev->scale_lo[0] = dev->scale_lo[1] = 0;
    dev->scale_hi[0] = dev->scale_hi[1] = ELO_RAW_MAX;
}

static void
elo_load_nvr(mouse_elo_t *dev)
{
    FILE   *fp;
    int32_t buf[8];

    elo_load_defaults(dev);

    fp = nvr_fopen(dev->nvr_path, "rb");
    if (fp) {
        if (fread(buf, sizeof(buf), 1, fp) == 1) {
            dev->cal_lo[0]   = buf[0];
            dev->cal_hi[0]   = buf[1];
            dev->cal_lo[1]   = buf[2];
            dev->cal_hi[1]   = buf[3];
            dev->scale_lo[0] = buf[4];
            dev->scale_hi[0] = buf[5];
            dev->scale_lo[1] = buf[6];
            dev->scale_hi[1] = buf[7];
        }
        fclose(fp);
    }
}

static void
elo_enqueue(mouse_elo_t *dev, uint8_t cmd, const uint8_t data[7])
{
    uint8_t buf[10];
    uint8_t sum = 0xAA;

    if (dev->quiet_all)
        return;
    if (fifo8_num_free(&dev->resp) < 10)
        return;

    buf[0] = 0x55;
    buf[1] = cmd;
    memcpy(&buf[2], data, 7);
    for (int i = 0; i < 9; i++)
        sum += buf[i];
    buf[9] = sum;

    fifo8_push_all(&dev->resp, buf, 10);
}

static void
elo_ack(mouse_elo_t *dev, uint8_t err)
{
    uint8_t data[7] = { err, 0, 0, 0, 0, 0, 0 };

    elo_enqueue(dev, 'A', data);
}

static int32_t
elo_transform(mouse_elo_t *dev, int axis, int32_t raw)
{
    int32_t mid = raw;
    int32_t lo, hi, out;

    if (dev->calibration_enabled) {
        lo  = dev->cal_lo[axis];
        hi  = dev->cal_hi[axis];
        mid = (hi != lo) ? (int32_t) (((int64_t) (raw - lo) * ELO_RAW_MAX) / (hi - lo)) : 0;
        if (dev->mode2 & ELO_M2_TRIM) {
            if (mid < 0)
                mid = 0;
            if (mid > ELO_RAW_MAX)
                mid = ELO_RAW_MAX;
        }
    }

    if (!dev->scaling_enabled)
        return mid;

    lo  = dev->scale_lo[axis];
    hi  = dev->scale_hi[axis];
    out = lo + (int32_t) (((int64_t) mid * (hi - lo)) / ELO_RAW_MAX);

    if (dev->invert_mask & (1 << axis))
        out = lo + hi - out;

    return out;
}

static void
elo_reset_complete(void *priv)
{
    mouse_elo_t *dev = (mouse_elo_t *) priv;

    dev->in_reset = false;
}

static void
elo_soft_reset(mouse_elo_t *dev)
{
    fifo8_reset(&dev->resp);
    dev->cal_active = false;
}

static void
elo_process_mode_ascii(mouse_elo_t *dev, const uint8_t *str)
{
    dev->mode1 = ELO_M1_ZRESERVED;
    dev->mode2 = 0;

    for (int i = 0; i < 7 && str[i]; i++) {
        switch (str[i]) {
            case 'I':
                dev->mode1 |= ELO_M1_INITIAL;
                break;
            case 'S':
                dev->mode1 |= ELO_M1_STREAM;
                break;
            case 'U':
                dev->mode1 |= ELO_M1_UNTOUCH;
                break;
            case 'T':
                dev->mode2 |= ELO_M2_TRACK;
                break;
            case 'P':
                dev->mode2 |= ELO_M2_TRIM;
                dev->mode1 |= ELO_M1_RANGECHK;
                dev->mode2 |= ELO_M2_CAL;
                break;
            case 'C':
                dev->mode2 |= ELO_M2_CAL;
                break;
            case 'M':
                dev->mode2 |= ELO_M2_SCALE;
                break;
            case 'B':
                dev->mode1 |= ELO_M1_RANGECHK;
                break;
            default:
                return; /* invalid char: rest of string ignored */
        }
    }
}

static void
elo_process_command(mouse_elo_t *dev, const uint8_t *body)
{
    uint8_t        cmd          = body[0];
    const uint8_t *data         = &body[1];
    bool           query        = islower((unsigned char) cmd) != 0;
    uint8_t        base         = (uint8_t) toupper((unsigned char) cmd);
    uint8_t        resp[7]      = { 0 };
    bool           has_response = true;

    switch (base) {
        case 'A':
            elo_ack(dev, 0);
            return;

        case 'T':
            if (query) {
                uint8_t tdata[7];
                int16_t x = (int16_t) elo_transform(dev, 0, dev->raw_x);
                int16_t y = (int16_t) elo_transform(dev, 1, dev->raw_y);
                tdata[0]  = (dev->but ? ELO_M1_STREAM : ELO_M1_UNTOUCH);
                tdata[1]  = x & 0xff;
                tdata[2]  = (x >> 8) & 0xff;
                tdata[3]  = y & 0xff;
                tdata[4]  = (y >> 8) & 0xff;
                tdata[5]  = 255;
                tdata[6]  = 0;
                elo_enqueue(dev, 'T', tdata);
            }
            return;

        case 'R':
            if (data[0] & 1) { /* soft reset */
                elo_soft_reset(dev);
                resp[0] = data[0];
            } else { /* hard reset: no output until reset completes */
                dev->in_reset = true;
                timer_on_auto(&dev->reset_timer, 500. * 1000.);
                return;
            }
            break;

        case 'B':
            if (!query) {
                dev->untouch_delay = data[0] & 0x0f;
                dev->rep_delay     = data[1];
            }
            resp[0] = dev->untouch_delay;
            resp[1] = dev->rep_delay;
            break;

        case 'C':
            if (data[0] == '2') {
                if (!query) {
                    dev->cal_active  = true;
                    dev->cal_step    = 0;
                    dev->cal_but_old = dev->but != 0;
                }
                resp[0] = '2';
            } else if (data[0] == 'S') {
                if (!query)
                    dev->swap_axes = (data[1] & 1) != 0;
                resp[0] = 'S';
                resp[1] = dev->swap_axes ? 1 : 0;
            } else if (data[0] == 'X' || data[0] == 'Y') {
                int axis = (data[0] == 'X') ? 0 : 1;
                if (!query) {
                    dev->cal_lo[axis]        = data[1] | (data[2] << 8);
                    dev->cal_hi[axis]        = data[3] | (data[4] << 8);
                    dev->calibration_enabled = true;
                }
                resp[0] = data[0];
                resp[1] = dev->cal_lo[axis] & 0xff;
                resp[2] = (dev->cal_lo[axis] >> 8) & 0xff;
                resp[3] = dev->cal_hi[axis] & 0xff;
                resp[4] = (dev->cal_hi[axis] >> 8) & 0xff;
            } else if (data[0] == 'x' || data[0] == 'y') {
                /* Offset/Numerator/Denominator form: HighPoint = Offset + Denominator
                 * (manual App. B); Numerator is fixed at 1 by our convention, both on
                 * the query response below and here on set, so the two stay symmetric. */
                int axis = (data[0] == 'x') ? 0 : 1;
                if (!query) {
                    int32_t offset           = (int16_t) (data[1] | (data[2] << 8));
                    int32_t denom            = (int16_t) (data[5] | (data[6] << 8));
                    dev->cal_lo[axis]        = offset;
                    dev->cal_hi[axis]        = offset + denom;
                    dev->calibration_enabled = true;
                    pclog("ELO: cal SET (o/n/d form) axis=%d lo=%d hi=%d\n", axis, dev->cal_lo[axis], dev->cal_hi[axis]);
                }
                int32_t offset = dev->cal_lo[axis];
                int32_t denom  = dev->cal_hi[axis] - dev->cal_lo[axis];
                resp[0]        = 0;
                resp[1]        = offset & 0xff;
                resp[2]        = (offset >> 8) & 0xff;
                resp[3]        = 1;
                resp[4]        = 0;
                resp[5]        = denom & 0xff;
                resp[6]        = (denom >> 8) & 0xff;
            } else {
                resp[0] = data[0];
            }
            break;

        case 'D':
            if (!query)
                dev->diag_result = 0; /* all tests always pass */
            resp[0] = dev->diag_result;
            break;

        case 'F':
            if (!query) {
                dev->filter_slen    = data[1];
                dev->filter_width   = data[2];
                dev->filter_states  = data[3];
                dev->filter_control = data[4];
            }
            resp[0] = '0';
            resp[1] = dev->filter_slen;
            resp[2] = dev->filter_width;
            resp[3] = dev->filter_states;
            resp[4] = dev->filter_control;
            break;

        case 'G':
            /* Configuration dump/restore: nothing to dump (ID reports P=0). */
            break;

        case 'H':
            if (!query) {
                dev->timer_enable   = data[0] & 1;
                dev->timer_mode     = data[1] & 1;
                dev->timer_interval = data[2] | (data[3] << 8);
            }
            resp[0] = dev->timer_enable;
            resp[1] = dev->timer_mode;
            resp[2] = dev->timer_interval & 0xff;
            resp[3] = (dev->timer_interval >> 8) & 0xff;
            resp[4] = dev->timer_interval & 0xff;
            resp[5] = (dev->timer_interval >> 8) & 0xff;
            break;

        case 'I':
            resp[0] = '0';  /* AccuTouch-class touchscreen */
            resp[1] = '0';  /* serial interface */
            resp[2] = 0x80; /* features: Z reported (constant) */
            resp[3] = 1;    /* firmware minor */
            resp[4] = 2;    /* firmware major */
            resp[5] = 0;    /* config packet count ('g') */
            resp[6] = 0;    /* IFlag: E271-2200-class */
            break;

        case 'J':
            resp[0] = '0'; /* AccuTouch */
            resp[1] = '0'; /* serial */
            resp[2] = '0'; /* booting from jumpers */
            resp[3] = '1'; /* Stream mode */
            resp[4] = 5;   /* baud index: 9600 */
            resp[5] = 1;   /* hardware handshaking enabled */
            resp[6] = 1;   /* binary mode */
            break;

        case 'K':
            if (!query)
                dev->key_value = data[0];
            resp[0] = dev->key_value;
            break;

        case 'L':
            if (!query)
                dev->low_power = data[0] & 1;
            resp[0] = dev->low_power;
            break;

        case 'M':
            if (!query) {
                if (data[0] == 0) {
                    dev->mode1 = data[1] | ELO_M1_ZRESERVED;
                    dev->mode2 = data[2];
                } else {
                    elo_process_mode_ascii(dev, data);
                }
            }
            resp[0] = dev->mode1;
            resp[1] = dev->mode2;
            break;

        case 'N':
            if (!query) {
                bool save = (data[0] & 1) != 0;
                if (save)
                    elo_save_nvr(dev);
                else
                    elo_load_nvr(dev);
            }
            resp[0] = data[0];
            resp[1] = data[1];
            resp[2] = data[2];
            break;

        case 'O':
            if (!query)
                memcpy(dev->owner, data, 7);
            memcpy(resp, dev->owner, 7);
            break;

        case 'P':
            if (!query) {
                static const int baud_table[8] = { 300, 600, 1200, 2400, 4800, 9600, 19200, 38400 };
                dev->ser1                      = data[1];
                dev->ser2                      = data[2];
                dev->baud_rate                 = baud_table[dev->ser1 & 7];
                timer_stop(&dev->host_to_serial_timer);
                timer_on_auto(&dev->host_to_serial_timer, (1000000. / dev->baud_rate) * 10.);
            }
            resp[0] = '0';
            resp[1] = dev->ser1;
            resp[2] = dev->ser2;
            break;

        case 'Q':
            if (!query) {
                dev->quiet_all        = (data[0] & 1) != 0;
                dev->quiet_timer_flag = (data[0] & 2) != 0;
                dev->quiet_touch      = (data[0] & 4) != 0;
            }
            resp[0] = (dev->quiet_all ? 1 : 0) | (dev->quiet_timer_flag ? 2 : 0) | (dev->quiet_touch ? 4 : 0);
            break;

        case 'S':
            if (data[0] == 'I') {
                if (!query)
                    dev->invert_mask = data[1] & 0x07;
                resp[0] = 'I';
                resp[1] = dev->invert_mask;
            } else if (data[0] == 'X' || data[0] == 'Y') {
                int axis = (data[0] == 'X') ? 0 : 1;
                if (!query) {
                    dev->scale_lo[axis]  = (int16_t) (data[1] | (data[2] << 8));
                    dev->scale_hi[axis]  = (int16_t) (data[3] | (data[4] << 8));
                    dev->scaling_enabled = true;
                }
                resp[0] = data[0];
                resp[1] = dev->scale_lo[axis] & 0xff;
                resp[2] = (dev->scale_lo[axis] >> 8) & 0xff;
                resp[3] = dev->scale_hi[axis] & 0xff;
                resp[4] = (dev->scale_hi[axis] >> 8) & 0xff;
            } else if (data[0] == 'x' || data[0] == 'y') {
                /* Offset/Numerator/Denominator form: HighPoint = Offset + Numerator
                 * (manual App. B); Denominator is fixed at 1 by our convention, both
                 * on the query response below and here on set. */
                int axis = (data[0] == 'x') ? 0 : 1;
                if (!query) {
                    int32_t offset       = (int16_t) (data[1] | (data[2] << 8));
                    int32_t numer        = (int16_t) (data[3] | (data[4] << 8));
                    dev->scale_lo[axis]  = offset;
                    dev->scale_hi[axis]  = offset + numer;
                    dev->scaling_enabled = true;
                    pclog("ELO: scale SET (o/n/d form) axis=%d lo=%d hi=%d\n", axis, dev->scale_lo[axis], dev->scale_hi[axis]);
                }
                int32_t offset = dev->scale_lo[axis];
                int32_t numer  = dev->scale_hi[axis] - dev->scale_lo[axis];
                resp[0]        = 0;
                resp[1]        = offset & 0xff;
                resp[2]        = (offset >> 8) & 0xff;
                resp[3]        = numer & 0xff;
                resp[4]        = (numer >> 8) & 0xff;
                resp[5]        = 1;
                resp[6]        = 0;
            } else {
                resp[0] = data[0];
            }
            break;

        default:
            has_response = false;
            elo_ack(dev, '5'); /* illegal command */
            return;
    }

    if (query && has_response)
        elo_enqueue(dev, base, resp);
    elo_ack(dev, 0);
}

static void
elo_write(UNUSED(serial_t *serial), void *priv, uint8_t data)
{
    mouse_elo_t *dev = (mouse_elo_t *) priv;

    if (dev->cmd_pos < 0) {
        if (data == 0x55)
            dev->cmd_pos = 0;
        return;
    }

    if (dev->cmd_pos < 8) {
        dev->body[dev->cmd_pos++] = data;
        return;
    }

    /* dev->cmd_pos == 8: this is the trailing checksum byte (not validated). */
    dev->cmd_pos = -1;
    elo_process_command(dev, dev->body);
}

static void
elo_prepare_transmit(mouse_elo_t *dev)
{
    uint8_t status = 0;
    bool    send   = false;

    if (dev->cal_active || (!dev->but && !dev->but_old))
        return;

    if (dev->but && !dev->but_old) {
        if (dev->mode1 & ELO_M1_INITIAL) {
            status |= ELO_M1_INITIAL;
            send = true;
        } else if (dev->mode1 & ELO_M1_STREAM) {
            status |= ELO_M1_STREAM;
            send = true;
        }
    } else if (dev->but && dev->but_old) {
        if (dev->mode1 & ELO_M1_STREAM) {
            status |= ELO_M1_STREAM;
            send = true;
        }
    } else {
        if (dev->mode1 & ELO_M1_UNTOUCH) {
            status |= ELO_M1_UNTOUCH;
            send = true;
        }
    }

    if (send && !dev->quiet_touch) {
        uint8_t data[7];
        int32_t rx = dev->but ? dev->raw_x : dev->raw_x_old;
        int32_t ry = dev->but ? dev->raw_y : dev->raw_y_old;
        int16_t x  = (int16_t) elo_transform(dev, 0, rx);
        int16_t y  = (int16_t) elo_transform(dev, 1, ry);

        data[0] = status;
        data[1] = x & 0xff;
        data[2] = (x >> 8) & 0xff;
        data[3] = y & 0xff;
        data[4] = (y >> 8) & 0xff;
        data[5] = 255;
        data[6] = 0;
        elo_enqueue(dev, 'T', data);
    }

    dev->raw_x_old = dev->raw_x;
    dev->raw_y_old = dev->raw_y;
    dev->but_old   = dev->but;
}

static void
elo_write_to_host(void *priv)
{
    mouse_elo_t *dev = (mouse_elo_t *) priv;

    if (dev->serial == NULL)
        goto no_write;
    if ((dev->serial->type >= SERIAL_16550) && dev->serial->fifo_enabled) {
        if (fifo_get_full(dev->serial->rcvr_fifo))
            goto no_write;
    } else if (dev->serial->lsr & 1)
        goto no_write;
    if (dev->in_reset)
        goto no_write;

    if (fifo8_num_used(&dev->resp))
        serial_write_fifo(dev->serial, fifo8_pop(&dev->resp));
    else
        elo_prepare_transmit(dev);

no_write:
    timer_on_auto(&dev->host_to_serial_timer, (1000000.0 / (double) dev->baud_rate) * 10.0);
}

static int
elo_poll(void *priv)
{
    mouse_elo_t *dev = (mouse_elo_t *) priv;
    double       abs_x, abs_y;

    dev->but = tablet_get_buttons_ex();
    mouse_get_abs_coords(&abs_x, &abs_y);

    if (enable_overscan && mouse_tablet_in_proximity > 0) {
        int index = mouse_tablet_in_proximity - 1;

        abs_x *= monitors[index].mon_unscaled_size_x - 1;
        abs_y *= monitors[index].mon_efscrnsz_y - 1;

        if (abs_x <= (monitors[index].mon_overscan_x / 2.))
            abs_x = (monitors[index].mon_overscan_x / 2.);
        if (abs_y <= (monitors[index].mon_overscan_y / 2.))
            abs_y = (monitors[index].mon_overscan_y / 2.);
        abs_x -= (monitors[index].mon_overscan_x / 2.);
        abs_y -= (monitors[index].mon_overscan_y / 2.);
        abs_x = abs_x / (double) monitors[index].mon_xsize;
        abs_y = abs_y / (double) monitors[index].mon_ysize;
    }

    if (abs_x >= 1.0)
        abs_x = 1.0;
    if (abs_y >= 1.0)
        abs_y = 1.0;
    if (abs_x <= 0.0)
        abs_x = 0.0;
    if (abs_y <= 0.0)
        abs_y = 0.0;

    if (dev->swap_axes) {
        double t = abs_x;
        abs_x    = abs_y;
        abs_y    = t;
    }

    dev->raw_x = (int32_t) lround(abs_x * ELO_RAW_MAX);
    dev->raw_y = (int32_t) lround(abs_y * ELO_RAW_MAX);

    if (dev->cal_active) {
        bool but_now = dev->but != 0;
        if (but_now && !dev->cal_but_old) {
            dev->cal_points[dev->cal_step][0] = dev->raw_x;
            dev->cal_points[dev->cal_step][1] = dev->raw_y;
            dev->cal_step++;
            elo_ack(dev, 0);
            if (dev->cal_step >= 2) {
                dev->cal_lo[0]           = dev->cal_points[0][0];
                dev->cal_hi[0]           = dev->cal_points[1][0];
                dev->cal_lo[1]           = dev->cal_points[0][1];
                dev->cal_hi[1]           = dev->cal_points[1][1];
                dev->calibration_enabled = true;
                dev->cal_active          = false;
            }
        }
        dev->cal_but_old = but_now;
    }

    return 0;
}

static int
elo_poll_global(void *arg)
{
    (void) arg;
    return elo_poll(elo_inst);
}

void *
elo_init(UNUSED(const device_t *info))
{
    mouse_elo_t *dev = calloc(1, sizeof(mouse_elo_t));

    dev->cmd_pos   = -1;
    dev->serial    = serial_attach(device_get_config_int("port"), NULL, elo_write, dev);
    dev->baud_rate = 9600;
    dev->ser1      = 5;    /* 9600 baud, 8N1 */
    dev->ser2      = 0x04; /* hardware handshaking enabled */
    if (dev->serial) {
        serial_set_cts(dev->serial, 1);
        serial_set_dsr(dev->serial, 1);
        serial_set_dcd(dev->serial, 1);
    }

    fifo8_create(&dev->resp, 256);
    timer_add(&dev->host_to_serial_timer, elo_write_to_host, dev, 0);
    timer_add(&dev->reset_timer, elo_reset_complete, dev, 0);
    timer_on_auto(&dev->host_to_serial_timer, (1000000. / dev->baud_rate) * 10.);

    dev->filter_slen    = 4;
    dev->filter_width   = 8;
    dev->filter_states  = 8;
    dev->filter_control = 0x90;
    dev->rep_delay      = 2;
    memcpy(dev->owner, "EloInc.", 7);

    snprintf(dev->nvr_path, sizeof(dev->nvr_path), "elo_smartset.nvr");
    elo_load_nvr(dev);

    mouse_set_buttons(2);
    mouse_set_poll_ex(elo_poll_global, dev);
    elo_inst = dev;

    return dev;
}

void
elo_close(void *priv)
{
    mouse_elo_t *dev = (mouse_elo_t *) priv;

    fifo8_destroy(&dev->resp);
    if (dev && dev->serial && dev->serial->sd)
        memset(dev->serial->sd, 0, sizeof(serial_device_t));

    free(dev);
    elo_inst = NULL;
}

static const device_config_t elo_config[] = {
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
    { .name = "", .description = "", .type = CONFIG_END }
    // clang-format on
};

const device_t mouse_elo_device = {
    .name          = "Elo TouchSystems SmartSet (Serial)",
    .internal_name = "elo_touchscreen",
    .flags         = DEVICE_COM,
    .local         = 0,
    .init          = elo_init,
    .close         = elo_close,
    .reset         = NULL,
    .available     = NULL,
    .speed_changed = NULL,
    .force_redraw  = NULL,
    .config        = elo_config
};
