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
} char_loopback_bits_t;

static const struct {
    const char_loopback_bits_t *bits;
} char_loopback_types[] = {
    [LOOPBACK_TYPE_NORTON] = {
        .bits = (const char_loopback_bits_t[]) {
            { 0x01, 0x00, 0, CHAR_LPT_ERROR },
            { 0x02, 0x00, 0, CHAR_LPT_SELECT },
            { 0x04, 0x00, 0, CHAR_LPT_PAPEROUT },
            { 0x08, 0x00, 0, CHAR_LPT_ACK },
            { 0x10, 0x00, 0, CHAR_LPT_BUSY },
            { 0 }
        }
    },
    [LOOPBACK_TYPE_CHECKIT] = {
        .bits = (const char_loopback_bits_t[]) {
            { 0x00, 0x00, CHAR_LPT_PSELECT, CHAR_LPT_BUSY },
            { 0x00, 0x00, CHAR_LPT_RESET, CHAR_LPT_ACK },
            { 0x00, 0x00, CHAR_LPT_LINEFEED, CHAR_LPT_PAPEROUT },
            { 0x00, 0x00, CHAR_LPT_STROBE, CHAR_LPT_SELECT },
            { 0x01, 0x00, 0, CHAR_LPT_ERROR },
            { 0 }
        }
    },
    [LOOPBACK_TYPE_SERIAL] = {
        .bits = (const char_loopback_bits_t[]) {
            { 0x00, 0x00, CHAR_COM_RTS, CHAR_COM_CTS },
            { 0x00, 0x00, CHAR_COM_DTR, CHAR_COM_DSR | CHAR_COM_DCD },
            { 0xff, 0xff, 0, 0 },
            { 0 }
        }
    }
};

#define ENABLE_CHAR_LOOPBACK_LOG 1
#ifdef ENABLE_CHAR_LOOPBACK_LOG
int char_loopback_do_log = ENABLE_CHAR_LOOPBACK_LOG;

static void
char_loopback_log(void *priv, const char *fmt, ...)
{
    va_list ap;

    if (char_loopback_do_log) {
        va_start(ap, fmt);
        log_out(priv, fmt, ap);
        va_end(ap);
    }
}
#else
#    define char_loopback_log(priv, fmt, ...)
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
} char_loopback_t;

static void
char_loopback_update(char_loopback_t *dev)
{
    dev->data_rx = 0;
    dev->status  = 0;
    for (int i = 0; char_loopback_types[dev->type].bits[i].data_tx || char_loopback_types[dev->type].bits[i].control; i++) {
        if ((dev->data_tx & char_loopback_types[dev->type].bits[i].data_tx) ||
            (dev->control & char_loopback_types[dev->type].bits[i].control)) {
            dev->data_rx |= dev->data_tx & char_loopback_types[dev->type].bits[i].data_rx;
            dev->status  |= char_loopback_types[dev->type].bits[i].status;
        }
    }
}

static ssize_t
char_loopback_read(uint8_t *buf, ssize_t len, void *priv)
{
    char_loopback_t *dev = (char_loopback_t *) priv;

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
char_loopback_write(uint8_t *buf, ssize_t len, void *priv)
{
    char_loopback_t *dev = (char_loopback_t *) priv;

    if (len > 0) {
        for (int i = 0; i < len; i++) {
            dev->data_tx = buf[i];
            char_loopback_update(dev);
            char_loopback_log(dev->log, "write(%02X) = %02X %08X\n", buf[i], dev->data_rx, dev->status);
        }
        dev->data_read = 0;
    }

    return len;
}

static uint32_t
char_loopback_status(void *priv)
{
    char_loopback_t *dev = (char_loopback_t *) priv;

    return dev->status;
}

static void
char_loopback_control(uint32_t flags, void *priv)
{
    char_loopback_t *dev = (char_loopback_t *) priv;

    dev->control = flags;
    char_loopback_update(dev);
    char_loopback_log(dev->log, "control(%08X) = %02X %08X\n", flags, dev->data_rx, dev->status);
}

static void
char_loopback_close(void *priv)
{
    char_loopback_t *dev = (char_loopback_t *) priv;

    char_loopback_log(dev->log, "close()\n");
}

static void *
char_loopback_init(const device_t *info)
{
    char_loopback_t *dev = (char_loopback_t *) calloc(1, sizeof(char_loopback_t));

    dev->log = log_open("Loopback");
    char_loopback_log(dev->log, "init()\n");

    /* Attach character device. */
    dev->port = char_attach(0, char_loopback_read, char_loopback_write, char_loopback_status, char_loopback_control, NULL, dev);

    if (info->config && !strcmp(info->config[0].name, "type"))
        dev->type = device_get_config_int(info->config[0].name);
    else
        dev->type = LOOPBACK_TYPE_SERIAL;
    if (dev->type >= (sizeof(char_loopback_types) / sizeof(char_loopback_types[0])))
        dev->type = 0;

    return dev;
}

const device_t char_loopback_com_device = {
    .name          = "Loopback Plug (COM)",
    .internal_name = "loopback",
    .flags         = DEVICE_COM,
    .local         = 0,
    .init          = char_loopback_init,
    .close         = char_loopback_close,
    .reset         = NULL,
    .available     = NULL,
    .speed_changed = NULL,
    .force_redraw  = NULL,
    .config        = NULL
};

static const device_config_t char_loopback_lpt_config[] = {
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

const device_t char_loopback_lpt_device = {
    .name          = "Loopback Plug (LPT)",
    .internal_name = "char_loopback_lpt",
    .flags         = DEVICE_LPT,
    .local         = 0,
    .init          = char_loopback_init,
    .close         = char_loopback_close,
    .reset         = NULL,
    .available     = NULL,
    .speed_changed = NULL,
    .force_redraw  = NULL,
    .config        = char_loopback_lpt_config
};
