/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          Implementation of Serial Mouse devices.
 *
 * TODO:    Add the Genius Serial Mouse.
 *
 * Authors: Miran Grca, <mgrca8@gmail.com>
 *
 *          Copyright 2023 Miran Grca.
 */
#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <stdlib.h>
#include <wchar.h>
#define HAVE_STDARG_H
#include <86box/86box.h>
#include <86box/device.h>
#include <86box/timer.h>
#include <86box/serial.h>
#include <86box/mouse.h>
#include <86box/plat.h>
#include <86box/version.h>

#define SERMOUSE_PORT 0 /* attach to Serial0 */

enum {
    STATE_RESET,
    STATE_BAUD_RATE,
    STATE_DORMANT,
    STATE_IDLE,
    STATE_COMMAND,
    STATE_DATA,
    STATE_TRANSMIT,
    STATE_TRANSMIT_REPORT,
    STATE_SKIP_REPORT
};

enum {
    FORMAT_BP1_ABS = 0x01,
    FORMAT_BP1_REL,
    FORMAT_MM_SERIES = 0x13,
    FORMAT_PB_3BYTE,
    FORMAT_PB_5BYTE,
    FORMAT_MSYSTEMS = 0x15,    /* Alias for FORMAT_PB_5BYTE. */
    FORMAT_MS,
    FORMAT_HEX,
    FORMAT_MS_4BYTE,
    FORMAT_MS_WHEEL,
    FORMATS_NUM
};

typedef struct mouse_t {
    const char *name;  /* name of this device */

    uint8_t     id[252];
    uint8_t     buf[256];

    uint8_t     flags; /* device flags */
    uint8_t     but; 
    uint8_t     rts_toggle;
    uint8_t     status;
    uint8_t     format;
    uint8_t     prompt;

    uint8_t     continuous;
    uint8_t     ib;
    uint8_t     command;
    uint8_t     buf_len;
    uint8_t     report_mode;
    uint8_t     id_len;
    uint8_t     buf_pos;
    uint8_t     rev;

    int8_t      type;  /* type of this device */
    int8_t      port;

    int         state;

    int         bps;
    int         rps;

    double      transmit_period;
    double      report_period;
    double      cur_period;
    double      min_bit_period;
    double      acc_time;
    double      host_transmit_period;

    pc_timer_t  timer;

    serial_t *  serial;
} mouse_t;

#define FLAG_INPORT  0x80 /* device is MS InPort */
#define FLAG_3BTN    0x20 /* enable 3-button mode */
#define FLAG_SCALED  0x10 /* enable delta scaling */
#define FLAG_INTR    0x04 /* dev can send interrupts */
#define FLAG_FROZEN  0x02 /* do not update counters */
#define FLAG_ENABLED 0x01 /* dev is enabled for use */

#ifdef ENABLE_MOUSE_SERIAL_LOG
int mouse_serial_do_log = ENABLE_MOUSE_SERIAL_LOG;

static void
mouse_serial_log(const char *fmt, ...)
{
    va_list ap;

    if (mouse_serial_do_log) {
        va_start(ap, fmt);
        pclog_ex(fmt, ap);
        va_end(ap);
    }
}
#else
#    define mouse_serial_log(fmt, ...)
#endif

static void
sermouse_set_period(mouse_t *dev, double period)
{
    dev->cur_period = period;    /* Needed for the recalculation of the timings. */

    timer_stop(&dev->timer);

    if (period > 0.0)
        timer_on_auto(&dev->timer, 10000.0);
}

static void
sermouse_transmit_byte(mouse_t *dev, int do_next)
{
    if (dev->buf_pos == 0)
        dev->acc_time = 0.0;

    if (dev->serial)
        serial_write_fifo(dev->serial, dev->buf[dev->buf_pos]);

    if (do_next) {
        dev->buf_pos = (dev->buf_pos + 1) % dev->buf_len;

        if (dev->buf_pos != 0)
            sermouse_set_period(dev, dev->transmit_period);
    }
}

static void
sermouse_transmit(mouse_t *dev, int len, int from_report, int to_report)
{
    dev->state = to_report ? STATE_TRANSMIT_REPORT : STATE_TRANSMIT;
    dev->buf_pos = 0;
    dev->buf_len = len;

    if (from_report) {
        if (dev->acc_time > dev->report_period)
            dev->acc_time -= dev->report_period;

        /* We have too little time left, pretend it's zero and handle
           schedule the next report at byte period. */
        if (dev->acc_time < dev->min_bit_period)
            sermouse_set_period(dev, dev->transmit_period);
        /* We have enough time, schedule the next report at report period,
           subtract the accumulated time from the total period, and add
           one byte period (the first byte delay). */
        else
            sermouse_set_period(dev, dev->report_period - dev->acc_time + dev->transmit_period);
    } else
        sermouse_set_period(dev, dev->transmit_period);
}

static uint8_t
sermouse_report_msystems(mouse_t *dev)
{
    int delta_x = 0;
    int delta_y = 0;
    int b = mouse_get_buttons_ex();

    mouse_subtract_coords(&delta_x, &delta_y, NULL, NULL, -128, 127, 1, 0);

    dev->buf[0] = 0x80;
    dev->buf[0] |= (b & 0x01) ? 0x00 : 0x04; /* left button */
    if (dev->but >= 3)
        dev->buf[0] |= (b & 0x04) ? 0x00 : 0x02; /* middle button */
    else
        dev->buf[0] |= 0x02; /* middle button */
    dev->buf[0] |= (b & 0x02) ? 0x00 : 0x01; /* right button */
    dev->buf[1] = delta_x;
    dev->buf[2] = delta_y;
    dev->buf[3] = delta_x;    /* same as byte 1 */
    dev->buf[4] = delta_y;    /* same as byte 2 */

    return 5;
}

static uint8_t
sermouse_report_3bp(mouse_t *dev)
{
    int delta_x = 0;
    int delta_y = 0;
    int b = mouse_get_buttons_ex();

    mouse_subtract_coords(&delta_x, &delta_y, NULL, NULL, -128, 127, 1, 0);

    dev->buf[0] = 0x80;
    dev->buf[0] |= (b & 0x01) ? 0x04 : 0x00; /* left button */
    if (dev->but >= 3)
        dev->buf[0] |= (b & 0x04) ? 0x02 : 0x00; /* middle button */
    dev->buf[0] |= (b & 0x02) ? 0x01 : 0x00; /* right button */
    dev->buf[1] = delta_x;
    dev->buf[2] = delta_y;

    return 3;
}

static uint8_t
sermouse_report_mmseries(mouse_t *dev)
{
    int delta_x = 0;
    int delta_y = 0;
    int b = mouse_get_buttons_ex();

    mouse_subtract_coords(&delta_x, &delta_y, NULL, NULL, -127, 127, 1, 0);

    dev->buf[0] = 0x80;
    if (delta_x >= 0)
        dev->buf[0] |= 0x10;
    if (delta_y >= 0)
        dev->buf[0] |= 0x08;

    dev->buf[0] |= (b & 0x01) ? 0x04 : 0x00; /* left button */
    if (dev->but >= 3)
        dev->buf[0] |= (b & 0x04) ? 0x02 : 0x00; /* middle button */
    dev->buf[0] |= (b & 0x02) ? 0x01 : 0x00; /* right button */
    dev->buf[1] = ABS(delta_x) & 0x7f;
    dev->buf[2] = ABS(delta_y) & 0x7f;
    mouse_serial_log("MM series mouse report: %02X %02X %02X\n", dev->buf[0], dev->buf[1], dev->buf[2]);

    return 3;
}

static uint8_t
sermouse_report_bp1(mouse_t *dev, int abs)
{
    int delta_x = 0;
    int delta_y = 0;
    int b = mouse_get_buttons_ex();

    mouse_subtract_coords(&delta_x, &delta_y, NULL, NULL, -2048, 2047, 1, abs);

    dev->buf[0] = 0x80;
    dev->buf[0] |= (b & 0x01) ? 0x10 : 0x00; /* left button */
    if (dev->but >= 3)
        dev->buf[0] |= (b & 0x04) ? 0x08 : 0x00; /* middle button */
    dev->buf[0] |= (b & 0x02) ? 0x04 : 0x00; /* right button */
    dev->buf[1] = (delta_x & 0x3f);
    dev->buf[2] = ((delta_x >> 6) & 0x3f);
    dev->buf[3] = (delta_y & 0x3f);
    dev->buf[4] = ((delta_y >> 6) & 0x3f);

    return 5;
}

static uint8_t
sermouse_report_ms(mouse_t *dev)
{
    uint8_t len;
    int delta_x = 0;
    int delta_y = 0;
    int delta_z = 0;
    int b = mouse_get_buttons_ex();

    mouse_subtract_coords(&delta_x, &delta_y, NULL, NULL, -128, 127, 0, 0);
    mouse_subtract_z(&delta_z, -8, 7, 1);

    dev->buf[0] = 0x40;
    dev->buf[0] |= (((delta_y >> 6) & 0x03) << 2);
    dev->buf[0] |= ((delta_x >> 6) & 0x03);
    if (b & 0x01)
        dev->buf[0] |= 0x20;
    if (b & 0x02)
        dev->buf[0] |= 0x10;
    dev->buf[1] = delta_x & 0x3f;
    dev->buf[2] = delta_y & 0x3f;
    mouse_serial_log("Microsoft serial mouse report: %02X %02X %02X\n", dev->buf[0], dev->buf[1], dev->buf[2]);
    if (dev->but == 3) {
        len = 3;
        if (dev->format == FORMAT_MS) {
            if (b & 0x04) {
                dev->buf[3] = 0x20;
                len++;
            }
        } else {
            if (mouse_mbut_changed()) {
                /* Microsoft 3-button mice send a fourth byte of 0x00 when the middle button
                   has changed. */
                dev->buf[3] = 0x00;
                len++;
            }
        }
    } else if (dev->but == 4) {
        len = 4;

        dev->buf[3] = delta_z & 0x0f;
        if (b & 0x04)
            dev->buf[3] |= 0x10;
    } else
        len = 3;

    return len;
}

static uint8_t
sermouse_report_hex(mouse_t *dev)
{
    char    ret[6] = { 0, 0, 0, 0, 0, 0 };
    uint8_t but = 0x00;
    int delta_x = 0;
    int delta_y = 0;
    int b = mouse_get_buttons_ex();

    mouse_subtract_coords(&delta_x, &delta_y, NULL, NULL, -128, 127, 1, 0);

    but |= (b & 0x01) ? 0x04 : 0x00; /* left button */
    if (dev->but >= 3)
        but |= (b & 0x04) ? 0x02 : 0x00; /* middle button */
    but |= (b & 0x02) ? 0x01 : 0x00; /* right button */

    sprintf(ret, "%01X%02X%02X", but & 0x0f, (int8_t) delta_x, (int8_t) delta_y);

    memcpy(dev->buf, ret, 5);

    return 5;
}

static int
sermouse_report(mouse_t *dev)
{
    int len = 0;

    memset(dev->buf, 0, 5);

    switch (dev->format) {
        case FORMAT_PB_5BYTE:
            len = sermouse_report_msystems(dev);
            break;
        case FORMAT_PB_3BYTE:
            len = sermouse_report_3bp(dev);
            break;
        case FORMAT_HEX:
            len = sermouse_report_hex(dev);
            break;
        case FORMAT_BP1_REL:
            len = sermouse_report_bp1(dev, 0);
            break;
        case FORMAT_MM_SERIES:
            len = sermouse_report_mmseries(dev);
            break;
        case FORMAT_BP1_ABS:
            len = sermouse_report_bp1(dev, 1);
            break;
        case FORMAT_MS:
        case FORMAT_MS_4BYTE:
        case FORMAT_MS_WHEEL:
            len = sermouse_report_ms(dev);
            break;

        default:
            break;
    }

    return len;
}

static void
sermouse_transmit_report(mouse_t *dev, int from_report)
{
    if (mouse_capture && mouse_state_changed())
        sermouse_transmit(dev, sermouse_report(dev), from_report, 1);
    else {
        if (dev->prompt || dev->continuous)
            sermouse_set_period(dev, 0.0);
        else {
            dev->state = STATE_SKIP_REPORT;
            /* Not in prompt or continuous mode and there have been no changes,
               skip the next report entirely. */
            if (from_report) {
                if (dev->acc_time > dev->report_period)
                    dev->acc_time -= dev->report_period;

                if (dev->acc_time < dev->min_bit_period)
                    sermouse_set_period(dev, dev->report_period);
                else
                    sermouse_set_period(dev, (dev->report_period * 2.0) - dev->acc_time);
            } else
                sermouse_set_period(dev, dev->report_period);
        }
    }
}

static int
sermouse_poll(void *priv)
{
    mouse_t *dev = (mouse_t *) priv;

    if (!mouse_capture || dev->prompt || !dev->continuous || (dev->state != STATE_IDLE))
        return 1;

    sermouse_transmit_report(dev, 0);
    return (dev->cur_period == 0.0) ? 1 : 0;
}

static void
ltsermouse_set_prompt_mode(mouse_t *dev, int prompt)
{
    dev->prompt = prompt;

    if (prompt || dev->continuous)
        sermouse_set_period(dev, 0.0);
    else
        sermouse_set_period(dev, dev->transmit_period);
}

static void
ltsermouse_set_report_period(mouse_t *dev, int rps)
{
    /* Limit the reports rate according to the baud rate. */
    if (rps == 0) {
        sermouse_set_period(dev, 0.0);

        dev->report_period = 0.0;
        dev->continuous = 1;
    } else {
#if 0
        if (rps > dev->max_rps)
            rps = dev->max_rps;
#endif

        dev->continuous = 0;
        dev->report_period = 1000000.0 / ((double) rps);
        /* Actual spacing between reports. */
    }
}

static void
ltsermouse_update_report_period(mouse_t *dev)
{
    ltsermouse_set_report_period(dev, dev->rps);

    ltsermouse_set_prompt_mode(dev, 0);
    mouse_serial_log("ltsermouse_update_report_period(): %i, %i\n", dev->continuous, dev->prompt);
    if (dev->continuous)
        dev->state = STATE_IDLE;
    else {
        sermouse_transmit_report(dev, 0);
        dev->state = STATE_TRANSMIT_REPORT;
    }
}

static void
ltsermouse_switch_baud_rate(mouse_t *dev, int next_state)
{
    double word_lens[FORMATS_NUM] = {
           [FORMAT_BP1_ABS]   = 7.0 + 1.0,    /* 7 data bits + even parity */
           [FORMAT_BP1_REL]   = 7.0 + 1.0,    /* 7 data bits + even parity */
           [FORMAT_MM_SERIES] = 8.0 + 1.0,    /* 8 data bits + odd parity */ 
           [FORMAT_PB_3BYTE]  = 8.0,          /* 8 data bits + no parity */
           [FORMAT_PB_5BYTE]  = 8.0,          /* 8 data bits + no parity */
           [FORMAT_MS]        = 7.0,          /* 7 datas bits + no parity */
           [FORMAT_HEX]       = 8.0,          /* 8 data bits + no parity */
           [FORMAT_MS_4BYTE]  = 7.0,          /* 7 datas bits + no parity */
           [FORMAT_MS_WHEEL]  = 7.0 };        /* 7 datas bits + no parity */
    double word_len = word_lens[dev->format];

    word_len += 1.0 + 2.0;            /* 1 start bit + 2 stop bits */

#if 0
    dev->max_rps = (int) floor(((double) dev->bps) / (word_len * num_words));
#endif

    if (next_state == STATE_BAUD_RATE)
        dev->transmit_period = dev->host_transmit_period;
    else
        dev->transmit_period = 1000000.0 / ((double) dev->bps);

    dev->min_bit_period = dev->transmit_period;

    dev->transmit_period *= word_len;
    /* The transmit period for the entire report, we're going to need this in ltsermouse_set_report_period(). */
#if 0
    dev->report_transmit_period = dev->transmit_period * num_words;
#endif

    ltsermouse_set_report_period(dev, dev->rps);

    if (!dev->continuous && (next_state != STATE_BAUD_RATE)) {
        if (dev->prompt)
            ltsermouse_set_prompt_mode(dev, 0);

        sermouse_transmit_report(dev, 0);
    }

    dev->state = next_state;
}

static int
sermouse_next_state(mouse_t *dev)
{
    int ret = STATE_IDLE;

    if (dev->prompt || (dev->rps == 0))
        ret = STATE_IDLE;
    else
        ret = STATE_TRANSMIT;

    return ret;
}

static void
ltsermouse_process_command(mouse_t *dev)
{
    int cmd_to_rps[9] = { 10, 20, 35, 70, 150, 0, -1, 100, 50 };
    int b;
    uint8_t format_codes[FORMATS_NUM] = {
                        [FORMAT_BP1_ABS]   = 0x0c,
                        [FORMAT_BP1_REL]   = 0x06,
                        [FORMAT_MM_SERIES] = 0x0a,
                        [FORMAT_PB_3BYTE]  = 0x00,
                        [FORMAT_PB_5BYTE]  = 0x02,
                        [FORMAT_MS]        = 0x0e,
                        [FORMAT_HEX]       = 0x04,
                        [FORMAT_MS_4BYTE]  = 0x08,         /* Guess */
                        [FORMAT_MS_WHEEL]  = 0x08 };       /* Guess */
    const char *copr = "\r\n(C) " COPYRIGHT_YEAR " 86Box, Revision 3.0";

    mouse_serial_log("ltsermouse_process_command(): %02X\n", dev->ib);
    dev->command = dev->ib;

    switch (dev->command) {
        case 0x20:
            /* Auto Baud Selection */
            dev->bps = (int) floor(1000000.0 / dev->host_transmit_period);
            dev->transmit_period = dev->host_transmit_period;

            dev->buf[0] = 0x06;
            sermouse_transmit(dev, 1, 0, 0);

            ltsermouse_switch_baud_rate(dev, STATE_BAUD_RATE);
            break;

        case 0x4a: /* Report Rate Selection commands */
        case 0x4b:
        case 0x4c:
        case 0x52:
        case 0x4d:
        case 0x51:
        case 0x4e:
        case 0x4f:
            dev->report_mode = dev->command;
            dev->rps = cmd_to_rps[dev->command - 0x4a];
            ltsermouse_update_report_period(dev);
            break;

        case 0x44:
            /* Select Prompt Mode */
            dev->report_mode = dev->command;
            ltsermouse_set_prompt_mode(dev, 1);
            dev->state = STATE_IDLE;
            break;
        case 0x50:
            /* Promopt to send a report (also enters Prompt Mode). */
            if (!dev->prompt) {
                dev->report_mode = 0x44;
                ltsermouse_set_prompt_mode(dev, 1);
            }
            sermouse_transmit_report(dev, 0);
            dev->state = STATE_TRANSMIT_REPORT;
            break;

        case 0x41:
            /* Absolute Bit Pad One Packed Binary Format */
            mouse_clear_coords();
            fallthrough;
        case 0x42:    /* Relative Bit Pad One Packed Binary Format */
        case 0x53:    /* MM Series Data Format */
        case 0x54:    /* Three Byte Packed Binary Format */
        case 0x55:    /* Five Byte Packed Binary Format (Mouse Systems-compatible) */
        case 0x56:    /* Microsoft Compatible Format */
        case 0x57:    /* Hexadecimal Format */
        case 0x58:    /* Microsoft Compatible Format (3+1 byte 3-button, from the FreeBSD source code) */
            if ((dev->rev >= 0x02) && ((dev->command != 0x58) || (dev->rev > 0x04))) {
                dev->format = dev->command & 0x1f;
                ltsermouse_switch_baud_rate(dev, sermouse_next_state(dev));
            }
            break;

        case 0x2a:
            if (dev->rev >= 0x03) {
                /* Programmable Baud Rate Selection */
                dev->state = STATE_DATA;
            }
            break;

        case 0x73:
            /* Status */
            dev->buf[0] = dev->prompt ? 0x4f : 0x0f;
            sermouse_transmit(dev, 1, 0, 0);
            break;
        case 0x05:
            /* Diagnostic */
            b = mouse_get_buttons_ex();
            dev->buf[0] = ((b & 0x01) << 2) | ((b & 0x06) >> 1);
            dev->buf[1] = dev->buf[2] = 0x00;
            sermouse_transmit(dev, 3, 0, 0);
            break;

        case 0x66:
            if (dev->rev >= 0x20) {
                /* Format and Revision Number */
                dev->buf[0] = format_codes[dev->format];
                dev->buf[0] |= 0x10;    /* Revision 3.0, 0x00 would be Revision 2.0 */
                sermouse_transmit(dev, 1, 0, 0);
            }
            break;

        case 0x74:
            /* Format and Mode in ASCII */
            if (dev->rev >= 0x03) {
                dev->buf[0] = dev->format | 0x40;
                dev->buf[1] = dev->report_mode;
                sermouse_transmit(dev, 2, 0, 0);
            }
            break;

        case 0x63:
            /* Copyright and Revision in ASCII */
            if (dev->rev >= 0x03) {
                memcpy(&(dev->buf[0]), copr, strlen(copr) + 1);
                sermouse_transmit(dev, strlen(copr) + 1, 0, 0);
            } else {
                memcpy(&(dev->buf[0]), copr, strlen(copr));
                sermouse_transmit(dev, strlen(copr), 0, 0);
            }
            dev->buf[29] = dev->rev | 0x30;
            break;

        case 0x64:
            /* Dormant State */
            dev->state = STATE_DORMANT;
            break;

        case 0x6b:
            /* Buttons - 86Box-specific command. */
            dev->state = dev->but;
            break;

        default:
            break;
    }
}

static void
ltsermouse_process_data(mouse_t *dev)
{
    mouse_serial_log("ltsermouse_process_data(): %02X (command = %02X)\n", dev->ib, dev->command);

    switch(dev->command) {
        case 0x2a:
            switch (dev->ib) {
                default:
                    fallthrough;
                case 0x6e:
                    dev->bps = 1200;
                    break;
                case 0x6f:
                    dev->bps = 2400;
                    break;
                case 0x70:
                    dev->bps = 4800;
                    break;
                case 0x71:
                    dev->bps = 9600;
                    break;
            }
            ltsermouse_switch_baud_rate(dev, (dev->prompt || dev->continuous) ? STATE_IDLE : STATE_TRANSMIT_REPORT);
            break;
        default:
            dev->state = STATE_IDLE;
            break;
    }
}

static void
sermouse_reset(mouse_t *dev, int callback)
{
    sermouse_set_period(dev, 0.0);

    dev->bps = 1200;
    dev->rps = 0;
    dev->prompt = 0;
    if (dev->id[0] == 'H')
        dev->format = FORMAT_MSYSTEMS;
    else  switch (dev->but) {
        default:
        case 2:
            dev->format = FORMAT_MS;
            break;
        case 3:
            dev->format = (dev->type == MOUSE_TYPE_LT3BUTTON) ? FORMAT_MS : FORMAT_MS_4BYTE;
            break;
        case 4:
            dev->format = FORMAT_MS_WHEEL;
            break;
    }

    ltsermouse_switch_baud_rate(dev, callback ? STATE_TRANSMIT : STATE_IDLE);
}

static void
sermouse_timer(void *priv)
{
    mouse_t *dev = (mouse_t *) priv;
#ifdef ENABLE_MOUSE_SERIAL_LOG
    int old_state = dev->state;
#endif

    switch (dev->state) {
        case STATE_RESET:
            /* All three mice default to continuous reporting. */
            sermouse_reset(dev, 0);
            break;
        case STATE_DATA:
            ltsermouse_process_data(dev);
            break;
        case STATE_COMMAND:
            ltsermouse_process_command(dev);
            break;
        case STATE_SKIP_REPORT:
            if (!dev->prompt && !dev->continuous)
                sermouse_transmit_report(dev, (dev->state == STATE_TRANSMIT_REPORT));
            else
                 dev->state = STATE_IDLE;
            break;
        case STATE_TRANSMIT_REPORT:
        case STATE_TRANSMIT:
        case STATE_BAUD_RATE:
            sermouse_transmit_byte(dev, 1);

            if (dev->buf_pos == 0) {
                if (!dev->prompt && !dev->continuous)
                    sermouse_transmit_report(dev, (dev->state == STATE_TRANSMIT_REPORT));
                else
                    dev->state = STATE_IDLE;
            }
            break;
        default:
            break;
    }

    mouse_serial_log("sermouse_timer(): %02i -> %02i\n", old_state, dev->state);
}

static void
ltsermouse_write(UNUSED(struct serial_s *serial), void *priv, uint8_t data)
{
    mouse_t *dev = (mouse_t *) priv;

    mouse_serial_log("ltsermouse_write(): %02X\n", data);

    dev->ib = data;

    switch (dev->state) {
        case STATE_RESET:
        case STATE_BAUD_RATE:
            break;
        case STATE_TRANSMIT_REPORT:
        case STATE_TRANSMIT:
        case STATE_SKIP_REPORT:
            sermouse_set_period(dev, 0.0);
            fallthrough;
        default:
            dev->state = STATE_COMMAND;
            fallthrough;
        case STATE_DATA:
            sermouse_timer(dev);
            break;
    }
}

/* Callback from serial driver: RTS was toggled. */
static void
sermouse_callback(UNUSED(struct serial_s *serial), void *priv)
{
    mouse_t *dev = (mouse_t *) priv;

    sermouse_reset(dev, 1);

    memcpy(dev->buf, dev->id, dev->id_len);
    sermouse_transmit(dev, dev->id_len, 0, 0);
}

static void
ltsermouse_transmit_period(UNUSED(serial_t *serial), void *priv, double transmit_period)
{
    mouse_t *dev = (mouse_t *) priv;

    dev->host_transmit_period = transmit_period;
}

static void
sermouse_speed_changed(void *priv)
{
    mouse_t *dev = (mouse_t *) priv;

    if (dev->cur_period != 0.0)
        sermouse_set_period(dev, dev->cur_period);
}

static void
sermouse_close(void *priv)
{
    mouse_t *dev = (mouse_t *) priv;

    /* Detach serial port from the mouse. */
    if (dev && dev->serial && dev->serial->sd)
        memset(dev->serial->sd, 0, sizeof(serial_device_t));

    free(dev);
}

/* Initialize the device for use by the user. */
static void *
sermouse_init(const device_t *info)
{
    mouse_t *dev;
    void (*rcr_callback)(struct serial_s *serial, void *priv);
    void (*dev_write)(struct serial_s *serial, void *priv, uint8_t data);
    void (*transmit_period_callback)(struct serial_s *serial, void *priv, double transmit_period);

    dev = (mouse_t *) calloc(1, sizeof(mouse_t));
    dev->name = info->name;
    dev->but  = device_get_config_int("buttons");
    dev->rev  = device_get_config_int("revision");

    if (info->local == 0)
        dev->rts_toggle  = 1;
    else
        dev->rts_toggle  = device_get_config_int("rts_toggle");

    if (dev->but > 2)
        dev->flags |= FLAG_3BTN;

    if (info->local == MOUSE_TYPE_MSYSTEMS) {
        dev->format    = 0;
        dev->type      = info->local;
        dev->id_len    = 1;
        dev->id[0]     = 'H';
    } else {
        dev->format    = 7;
        dev->status    = 0x0f;
        dev->id_len    = 1;
        dev->id[0]     = 'M';
        if (info->local)
            dev->rev  = device_get_config_int("revision");
        switch (dev->but) {
            default:
            case 2:
                dev->type = info->local ? MOUSE_TYPE_LOGITECH : MOUSE_TYPE_MICROSOFT;
                break;
            case 3:
                dev->type   = info->local ? MOUSE_TYPE_LT3BUTTON : MOUSE_TYPE_MS3BUTTON;
                dev->id_len = 2;
                dev->id[1]  = '3';
                break;
            case 4:
                dev->type   = MOUSE_TYPE_MSWHEEL;
                dev->id_len = 6;
                dev->id[1]  = 'Z';
                dev->id[2]  = '@';
                break;
        }
    }

    dev->port = device_get_config_int("port");

    /* Attach a serial port to the mouse. */
    rcr_callback = dev->rts_toggle ? sermouse_callback : NULL;
    dev_write = (info->local == 1) ? ltsermouse_write : NULL;
    transmit_period_callback = (info->local == 1) ? ltsermouse_transmit_period : NULL;

    dev->serial = serial_attach_ex(dev->port, rcr_callback, dev_write,
                                   transmit_period_callback, NULL, dev);

    mouse_serial_log("%s: port=COM%d\n", dev->name, dev->port + 1);

    timer_add(&dev->timer, sermouse_timer, dev, 0);

    /* The five second delay allows the mouse to execute internal initializations. */
    sermouse_set_period(dev, 5000000.0);

    /* Tell them how many buttons we have. */
    mouse_set_buttons(dev->but);

    mouse_set_poll(sermouse_poll, dev);

    /* Return our private data to the I/O layer. */
    return dev;
}

static const device_config_t msssermouse_config[] = {
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
        .name           = "buttons",
        .description    = "Buttons",
        .type           = CONFIG_SELECTION,
        .default_string = NULL,
        .default_int    = 2,
        .file_filter    = NULL,
        .spinner        = { 0 },
        .selection      = {
            { .description = "Two",   .value = 2 },
            { .description = "Three", .value = 3 },
            { .description = ""                  }
        },
        .bios           = { { 0 } }
    },
    {
        .name           = "rts_toggle",
        .description    = "RTS toggle",
        .type           = CONFIG_BINARY,
        .default_string = NULL,
        .default_int    = 0,
        .file_filter    = NULL,
        .spinner        = { 0 },
        .selection      = { { 0 } },
        .bios           = { { 0 } }
    },
    { .name = "", .description = "", .type = CONFIG_END }
  // clang-format on
};

static const device_config_t mssermouse_config[] = {
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
        .name           = "buttons",
        .description    = "Buttons",
        .type           = CONFIG_SELECTION,
        .default_string = NULL,
        .default_int    = 2,
        .file_filter    = NULL,
        .spinner        = { 0 },
        .selection      = {
            { .description = "Two",   .value = 2 },
            { .description = "Three", .value = 3 },
            { .description = "Wheel", .value = 4 },
            { .description = ""                  }
        },
        .bios           = { { 0 } }
    },
    { .name = "", .description = "", .type = CONFIG_END }
  // clang-format on
};

static const device_config_t ltsermouse_config[] = {
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
        .name           = "buttons",
        .description    = "Buttons",
        .type           = CONFIG_SELECTION,
        .default_string = NULL,
        .default_int    = 2,
        .file_filter    = NULL,
        .spinner        = { 0 },
        .selection      = {
            { .description = "Two",   .value = 2 },
            { .description = "Three", .value = 3 },
            { .description = ""                  }
        },
        .bios           = { { 0 } }
    },
    {
        .name           = "revision",
        .description    = "Revision",
        .type           = CONFIG_SELECTION,
        .default_string = NULL,
        .default_int    = 3,
        .file_filter    = NULL,
        .spinner        = { 0 },
        .selection      = {
            { .description = "LOGIMOUSE R7 1.0",  .value = 1 },
            { .description = "LOGIMOUSE R7 2.0",  .value = 2 },
            { .description = "LOGIMOUSE C7 3.0",  .value = 3 },
            { .description = "Logitech MouseMan", .value = 4 },
            { .description = ""                              }
        },
        .bios           = { { 0 } }
    },
    {
        .name           = "rts_toggle",
        .description    = "RTS toggle",
        .type           = CONFIG_BINARY,
        .default_string = NULL,
        .default_int    = 0,
        .file_filter    = NULL,
        .spinner        = { 0 },
        .selection      = { { 0 } },
        .bios           = { { 0 } }
    },
    { .name = "", .description = "", .type = CONFIG_END }
  // clang-format on
};

const device_t mouse_mssystems_device = {
    .name          = "Mouse Systems Serial Mouse",
    .internal_name = "mssystems",
    .flags         = DEVICE_COM,
    .local         = MOUSE_TYPE_MSYSTEMS,
    .init          = sermouse_init,
    .close         = sermouse_close,
    .reset         = NULL,
    .available     = NULL,
    .speed_changed = sermouse_speed_changed,
    .force_redraw  = NULL,
    .config        = msssermouse_config
};

const device_t mouse_msserial_device = {
    .name          = "Microsoft Serial Mouse",
    .internal_name = "msserial",
    .flags         = DEVICE_COM,
    .local         = 0,
    .init          = sermouse_init,
    .close         = sermouse_close,
    .reset         = NULL,
    .available     = NULL,
    .speed_changed = sermouse_speed_changed,
    .force_redraw  = NULL,
    .config        = mssermouse_config
};

const device_t mouse_ltserial_device = {
    .name          = "Logitech Serial Mouse",
    .internal_name = "ltserial",
    .flags         = DEVICE_COM,
    .local         = 1,
    .init          = sermouse_init,
    .close         = sermouse_close,
    .reset         = NULL,
    .available     = NULL,
    .speed_changed = sermouse_speed_changed,
    .force_redraw  = NULL,
    .config        = ltsermouse_config
};
