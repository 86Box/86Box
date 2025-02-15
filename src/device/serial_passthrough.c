/*
 * 86Box        A hypervisor and IBM PC system emulator that specializes in
 *              running old operating systems and software designed for IBM
 *              PC systems and compatibles from 1981 through fairly recent
 *              system designs based on the PCI bus.
 *
 *              This file is part of the 86Box distribution.
 *
 *              Implementation of Serial passthrough device.
 *
 *
 * Authors:     Andreas J. Reichel <webmaster@6th-dimension.com>,
 *              Jasmine Iwanek <jasmine@iwanek.co.uk>
 *
 *              Copyright 2021      Andreas J. Reichel.
 *              Copyright 2021-2022 Jasmine Iwanek.
 */

#include <stdarg.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <stdlib.h>
#include <wchar.h>
#define HAVE_STDARG_H
#include <86box/86box.h>
#include <86box/device.h>
#include <86box/fifo.h>
#include <86box/timer.h>
#include <86box/serial.h>
#include <86box/serial_passthrough.h>
#include <86box/plat_serial_passthrough.h>
#include <86box/plat_unused.h>

#define ENABLE_SERIAL_PASSTHROUGH_LOG 1
#ifdef ENABLE_SERIAL_PASSTHROUGH_LOG
int serial_passthrough_do_log = ENABLE_SERIAL_PASSTHROUGH_LOG;

static void
serial_passthrough_log(const char *fmt, ...)
{
    va_list ap;

    if (serial_passthrough_do_log) {
        va_start(ap, fmt);
        pclog_ex(fmt, ap);
        va_end(ap);
    }
}
#else
#    define serial_passthrough_log(fmt, ...)
#endif

void
serial_passthrough_init(void)
{
    for (uint8_t c = 0; c < SERIAL_MAX; c++) {
        if (serial_passthrough_enabled[c]) {
            /* Instance n for COM n */
            device_add_inst(&serial_passthrough_device, c + 1);
        }
    }
}

static void
serial_passthrough_write(UNUSED(serial_t *s), void *priv, uint8_t val)
{
    plat_serpt_write(priv, val);
}

static void
host_to_serial_cb(void *priv)
{
    serial_passthrough_t *dev = (serial_passthrough_t *) priv;

    uint8_t byte;

    /* write_fifo has no failure indication, but if we write to fast, the host
     * can never fetch the bytes in time, so check if the fifo is full if in
     * fifo mode or if lsr has bit 0 set if not in fifo mode */
    if ((dev->serial->type >= SERIAL_16550) && dev->serial->fifo_enabled) {
        if (fifo_get_full(dev->serial->rcvr_fifo)) {
            goto no_write_to_machine;
        }
    } else {
        if (dev->serial->lsr & 1) {
            goto no_write_to_machine;
        }
    }
    if (plat_serpt_read(dev, &byte)) {
#if 0
        printf("got byte %02X\n", byte);
#endif
        serial_write_fifo(dev->serial, byte);
#if 0
        serial_set_dsr(dev->serial, 1);
#endif
    }
no_write_to_machine:
#if 0
    serial_device_timeout(dev->serial);
#endif
    timer_on_auto(&dev->host_to_serial_timer, (1000000.0 / dev->baudrate) * (double) dev->bits);
}

static void
serial_passthrough_rcr_cb(UNUSED(struct serial_s *serial), void *priv)
{
    serial_passthrough_t *dev = (serial_passthrough_t *) priv;

    timer_stop(&dev->host_to_serial_timer);
    /* FIXME: do something to dev->baudrate */
    timer_on_auto(&dev->host_to_serial_timer, (1000000.0 / dev->baudrate) * (double) dev->bits);
#if 0
    serial_clear_fifo(dev->serial);
#endif
}

static void
serial_passthrough_speed_changed(void *priv)
{
    serial_passthrough_t *dev = (serial_passthrough_t *) priv;
    if (!dev)
        return;

    timer_stop(&dev->host_to_serial_timer);
    /* FIXME: do something to dev->baudrate */
    timer_on_auto(&dev->host_to_serial_timer, (1000000.0 / dev->baudrate) * (double) dev->bits);
#if 0
    serial_clear_fifo(dev->serial);
#endif
}

static void
serial_passthrough_dev_close(void *priv)
{
    serial_passthrough_t *dev = (serial_passthrough_t *) priv;
    if (!dev)
        return;

    /* Detach passthrough device from COM port */
    if (dev->serial && dev->serial->sd)
        memset(dev->serial->sd, 0, sizeof(serial_device_t));

    plat_serpt_close(dev);
    free(dev);
}

void
serial_passthrough_transmit_period(UNUSED(serial_t *serial), void *priv, double transmit_period)
{
    serial_passthrough_t *dev = (serial_passthrough_t *) priv;

    if (dev->mode != SERPT_MODE_HOSTSER)
        return;
    dev->baudrate = 1000000.0 / transmit_period;

    serial_passthrough_speed_changed(priv);
    plat_serpt_set_params(dev);
}

void
serial_passthrough_lcr_callback(serial_t *serial, void *priv, uint8_t lcr)
{
    serial_passthrough_t *dev = (serial_passthrough_t *) priv;

    if (dev->mode != SERPT_MODE_HOSTSER)
        return;
    dev->bits      = serial->bits;
    dev->data_bits = ((lcr & 0x03) + 5);
    serial_passthrough_speed_changed(priv);
    plat_serpt_set_params(dev);
}

/* Initialize the device for use by the user. */
static void *
serial_passthrough_dev_init(const device_t *info)
{
    serial_passthrough_t *dev;

    dev = (serial_passthrough_t *) calloc(1, sizeof(serial_passthrough_t));
    dev->mode = device_get_config_int("mode");

    dev->port      = device_get_instance() - 1;
    dev->baudrate  = device_get_config_int("baudrate");
    dev->data_bits = device_get_config_int("data_bits");

    /* Attach passthrough device to a COM port */
    dev->serial = serial_attach_ex(dev->port, serial_passthrough_rcr_cb,
                                   serial_passthrough_write, serial_passthrough_transmit_period, serial_passthrough_lcr_callback, dev);
    if (!dev->serial) {
        free(dev);
        return NULL;
    }

    strncpy(dev->host_serial_path, device_get_config_string("host_serial_path"), 1023);
#ifdef _WIN32
    strncpy(dev->named_pipe, device_get_config_string("named_pipe"), 1023);
#endif

    serial_passthrough_log("%s: port=COM%d\n", info->name, dev->port + 1);
    serial_passthrough_log("%s: baud=%f\n", info->name, dev->baudrate);
    serial_passthrough_log("%s: mode=%s\n", info->name, serpt_mode_names[dev->mode]);

    if (plat_serpt_open_device(dev)) {
        serial_passthrough_log("%s: not running\n", info->name);
        return NULL;
    }
    serial_passthrough_log("%s: running\n", info->name);

    memset(&dev->host_to_serial_timer, 0, sizeof(pc_timer_t));
    timer_add(&dev->host_to_serial_timer, host_to_serial_cb, dev, 1);
    serial_set_cts(dev->serial, 1);
    serial_set_dsr(dev->serial, 1);
    serial_set_dcd(dev->serial, 1);

    /* 1 start bit + data bits + stop bits (no parity assumed) */
    dev->bits = 1 + device_get_config_int("data_bits") + device_get_config_int("stop_bits");

    /* Return our private data to the I/O layer. */
    return dev;
}

const char *serpt_mode_names[SERPT_MODES_MAX] = {
    [SERPT_MODE_VCON]    = "vcon",
    [SERPT_MODE_TCPSRV]  = "tcpsrv",
    [SERPT_MODE_TCPCLNT] = "tcpclnt",
    [SERPT_MODE_HOSTSER] = "hostser",
};

// clang-format off
static const device_config_t serial_passthrough_config[] = {
    {
        .name           = "mode",
        .description    = "Passthrough Mode",
        .type           = CONFIG_SELECTION,
        .default_string = NULL,
        .default_int    = 0,
        .file_filter    = NULL,
        .spinner        = { 0 },
        .selection      = {
#ifdef _WIN32
            { .description = "Named Pipe (Server)",             .value = SERPT_MODE_VCON    },
#if 0 /* TODO */
            { .description = "Named Pipe (Client)",             .value = SERPT_MODE_VCON    },
#endif
#else /* _WIN32 */
            { .description = "Pseudo Terminal/Virtual Console", .value = SERPT_MODE_VCON    },
#endif /* _WIN32 */
#if 0 /* TODO */
            { .description = "TCP Server",                      .value = SERPT_MODE_TCPSRV  },
            { .description = "TCP Client",                      .value = SERPT_MODE_TCPCLNT },
#endif
            { .description = "Host Serial Passthrough",         .value = SERPT_MODE_HOSTSER },
            { .description = ""                                                             }
        },
        .bios           = { { 0 } }
    },
    {
        .name           = "host_serial_path",
        .description    = "Host Serial Device",
        .type           = CONFIG_SERPORT,
        .default_string = NULL,
        .default_int    = 0,
        .file_filter    = NULL,
        .spinner        = { 0 },
        .selection      = { { 0 } },
        .bios           = { { 0 } }
    },
#ifdef _WIN32
    {
        .name           = "named_pipe",
        .description    = "Name of pipe",
        .type           = CONFIG_STRING,
        .default_string = "\\\\.\\pipe\\86Box\\test",
        .default_int    = 0,
        .file_filter    = NULL,
        .spinner        = { 0 },
        .selection      = { { 0 } },
        .bios           = { { 0 } }
    },
#endif /* _WIN32 */
    {
        .name           = "data_bits",
        .description    = "Data bits",
        .type           = CONFIG_SELECTION,
        .default_string = NULL,
        .default_int    = 8,
        .file_filter    = NULL,
        .spinner        = { 0 },
        .selection      = {
#if 0 /* Mentioned by WFW 3.1x, not supported, atleast on Linux */
            { .description = "4", .value = 4 },
#endif
            { .description = "5", .value = 5 },
            { .description = "6", .value = 6 },
            { .description = "7", .value = 7 },
            { .description = "8", .value = 8 },
            { .description = ""              }
        },
        .bios           = { { 0 } }
    },
    {
        .name           = "stop_bits",
        .description    = "Stop bits",
        .type           = CONFIG_SELECTION,
        .default_string = NULL,
        .default_int    = 1,
        .file_filter    = NULL,
        .spinner        = { 0 },
        .selection      = {
            { .description = "1",   .value = 1   },
#if 0
            { .description = "1.5", .value = 1.5 },
#endif
            { .description = "2",   .value = 2   },
            { .description = ""                  }
        },
        .bios           = { { 0 } }
    },
    {
        .name           = "baudrate",
        .description    = "Baud Rate of Passthrough",
        .type           = CONFIG_SELECTION,
        .default_string = NULL,
        .default_int    = 115200,
        .file_filter    = NULL,
        .spinner        = { 0 },
        .selection      = {
#if 0
            { .description = "256000", .value = 256000   },
            { .description = "128000", .value = 128000   },
#endif
            { .description = "115200", .value = 115200   },
            { .description =  "57600", .value =  57600   },
            { .description =  "56000", .value =  56000   },
            { .description =  "38400", .value =  38400   },
            { .description =  "19200", .value =  19200   },
            { .description =  "14400", .value =  14400   },
            { .description =   "9600", .value =   9600   },
            { .description =   "7200", .value =   7200   },
            { .description =   "4800", .value =   4800   },
            { .description =   "2400", .value =   2400   },
            { .description =   "1800", .value =   1800   },
            { .description =   "1200", .value =   1200   },
            { .description =    "600", .value =    600   },
            { .description =    "300", .value =    300   },
            { .description =    "150", .value =    150   },
#if 0
            { .description =  "134.5", .value =    134.5 },
#endif
            { .description =    "110", .value =    110   },
            { .description =     "75", .value =     75   },
            { .description = ""                          }
        },
        .bios           = { { 0 } }
    },
    { .name = "", .description = "", .type = CONFIG_END }
};
// clang-format on

const device_t serial_passthrough_device = {
    .name          = "Serial Passthrough Device",
    .flags         = 0,
    .local         = 0,
    .init          = serial_passthrough_dev_init,
    .close         = serial_passthrough_dev_close,
    .reset         = NULL,
    .available     = NULL,
    .speed_changed = serial_passthrough_speed_changed,
    .force_redraw  = NULL,
    .config        = serial_passthrough_config
};
