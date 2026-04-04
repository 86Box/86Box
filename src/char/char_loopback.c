/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          Loopback plug character device.
 *
 *
 *
 * Authors: RichardG, <richardg867@gmail.com>
 *
 *          Copyright 2026 RichardG.
 */

#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define HAVE_STDARG_H
#include <86box/86box.h>
#include <86box/device.h>
#include <86box/ini.h>
#include <86box/char.h>
#include <86box/log.h>
#include <86box/plat.h>

enum {
    LOOPBACK_TYPE_CHECKIT = 0,
    LOOPBACK_TYPE_NORTON  = 1,
    LOOPBACK_TYPE_SERIAL  = 2 /* special value, increase when adding parallel types */
};

typedef struct {
    const uint8_t data_tx;
    const uint8_t data_rx;
    const uint32_t control;
    const uint32_t status;
} loopback_bits_t;

static const struct {
    const loopback_bits_t *bits;
} loopback_types[] = {
    [LOOPBACK_TYPE_NORTON] = {
        .bits = (const loopback_bits_t[]) {
            { 0x01, 0x00, 0, CHAR_LPT_ERROR },
            { 0x02, 0x00, 0, CHAR_LPT_SELECT },
            { 0x04, 0x00, 0, CHAR_LPT_PAPEROUT },
            { 0x08, 0x00, 0, CHAR_LPT_ACK },
            { 0x10, 0x00, 0, CHAR_LPT_BUSY },
            { 0 }
        }
    },
    [LOOPBACK_TYPE_CHECKIT] = {
        .bits = (const loopback_bits_t[]) {
            { 0x00, 0x00, CHAR_LPT_PSELECT, CHAR_LPT_BUSY },
            { 0x00, 0x00, CHAR_LPT_RESET, CHAR_LPT_ACK },
            { 0x00, 0x00, CHAR_LPT_LINEFEED, CHAR_LPT_PAPEROUT },
            { 0x00, 0x00, CHAR_LPT_STROBE, CHAR_LPT_SELECT },
            { 0x01, 0x00, 0, CHAR_LPT_ERROR },
            { 0 }
        }
    },
    [LOOPBACK_TYPE_SERIAL] = {
        .bits = (const loopback_bits_t[]) {
            { 0x00, 0x00, CHAR_COM_RTS, CHAR_COM_CTS },
            { 0x00, 0x00, CHAR_COM_DTR, CHAR_COM_DSR | CHAR_COM_DCD },
            { 0xff, 0xff, 0, 0 },
            { 0 }
        }
    }
};

#define ENABLE_LOOPBACK_LOG 1
#ifdef ENABLE_LOOPBACK_LOG
int loopback_do_log = ENABLE_LOOPBACK_LOG;

static void
loopback_log(void *priv, const char *fmt, ...)
{
    va_list ap;

    if (loopback_do_log) {
        va_start(ap, fmt);
        log_out(priv, fmt, ap);
        va_end(ap);
    }
}
#else
#    define loopback_log(priv, fmt, ...)
#endif

typedef struct {
    void *log;
    char_port_t *port;
    int type;
    uint8_t data_tx;
    uint8_t data_rx;
    uint8_t data_read : 1;
    uint32_t control;
    uint32_t status;
} loopback_t;

static void
loopback_update(loopback_t *dev)
{
    dev->data_rx = 0;
    dev->status  = 0;
    for (int i = 0; loopback_types[dev->type].bits[i].data_tx || loopback_types[dev->type].bits[i].control; i++) {
        if ((dev->data_tx & loopback_types[dev->type].bits[i].data_tx) ||
            (dev->control & loopback_types[dev->type].bits[i].control)) {
            dev->data_rx |= dev->data_tx & loopback_types[dev->type].bits[i].data_rx;
            dev->status  |= loopback_types[dev->type].bits[i].status;
        }
    }
}

static ssize_t
loopback_read(uint8_t *buf, ssize_t len, void *priv)
{
    loopback_t *dev = (loopback_t *) priv;

    if (dev->data_read)
        return 0;

    if (len > 0) {
        dev->data_read = 1;
        for (int i = 0; i < len; i++)
            buf[i] = dev->data_rx;
    }

    return len;
}

static ssize_t
loopback_write(uint8_t *buf, ssize_t len, void *priv)
{
    loopback_t *dev = (loopback_t *) priv;

    if (len > 0) {
        for (int i = 0; i < len; i++) {
            dev->data_tx = buf[i];
            loopback_update(dev);
            loopback_log(dev->log, "write(%02X) = %02X %08X\n", buf[i], dev->data_rx, dev->status);
        }
        dev->data_read = 0;
    }

    return len;
}

static uint32_t
loopback_status(void *priv)
{
    loopback_t *dev = (loopback_t *) priv;

    return dev->status;
}

static void
loopback_control(uint32_t flags, void *priv)
{
    loopback_t *dev = (loopback_t *) priv;

    dev->control = flags;
    loopback_update(dev);
    loopback_log(dev->log, "control(%08X) = %02X %08X\n", flags, dev->data_rx, dev->status);
}

static void
loopback_close(void *priv)
{
    loopback_t *dev = (loopback_t *) priv;

    loopback_log(dev->log, "close()\n");
}

static void *
loopback_init(const device_t *info)
{
    loopback_t *dev = (loopback_t *) calloc(1, sizeof(loopback_t));

    dev->log = log_open("Loopback");
    loopback_log(dev->log, "init()\n");

    /* Attach character device. */
    dev->port = char_attach(0, loopback_read, loopback_write, loopback_status, loopback_control, NULL, dev);

    if (info->config && !strcmp(info->config[0].name, "type"))
        dev->type = ini_get_int(dev->port->config, "", info->config[0].name, info->config[0].default_int);
    else
        dev->type = LOOPBACK_TYPE_SERIAL;
    if (dev->type >= (sizeof(loopback_types) / sizeof(loopback_types[0])))
        dev->type = 0;

    return dev;
}

const device_t loopback_com_device = {
    .name          = "Loopback Plug",
    .internal_name = "loopback",
    .flags         = DEVICE_COM,
    .local         = 0,
    .init          = loopback_init,
    .close         = loopback_close,
    .reset         = NULL,
    .available     = NULL,
    .speed_changed = NULL,
    .force_redraw  = NULL,
    .config        = NULL
};

static const device_config_t loopback_lpt_config[] = {
    {
        .name         = "type",
        .description  = "Loopback Type",
        .type         = CONFIG_SELECTION,
        .default_int  = 0,
        .selection    = {
            { .description = "CheckIt / IBM Diagnostic", .value = LOOPBACK_TYPE_CHECKIT },
            { .description = "Norton",                   .value = LOOPBACK_TYPE_NORTON  },
            { NULL                                                                      }
        }
    },
    { .name = "", .description = "", .type = CONFIG_END }
};

const device_t loopback_lpt_device = {
    .name          = "Loopback Plug",
    .internal_name = "loopback",
    .flags         = DEVICE_LPT,
    .local         = 0,
    .init          = loopback_init,
    .close         = loopback_close,
    .reset         = NULL,
    .available     = NULL,
    .speed_changed = NULL,
    .force_redraw  = NULL,
    .config        = loopback_lpt_config
};
