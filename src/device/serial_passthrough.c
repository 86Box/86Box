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
 * Author:      Andreas J. Reichel, <webmaster@6th-dimension.com>
 *
 *              Copyright 2021          Andreas J. Reichel 
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
#include <86box/timer.h>
#include <86box/serial.h>
#include <86box/serial_passthrough.h>


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
#define serial_passthrough_log(fmt, ...)
#endif


void
serial_passthrough_init(void)
{
    int c;

    for (c = 0; c < SERIAL_MAX; c++) {
        if (serial_passthrough_enabled[c]) {
            /* Instance n for COM n */
            device_add_inst(&serial_passthrough_device, c + 1);
        }
    }    
}


static void
serial_passthrough_timers_off(serial_passthrough_t *dev)
{
    timer_stop(&dev->host_to_serial_timer);
}


static void
serial_passthrough_write(serial_t * s, void *priv, uint8_t val)
{
    printf("%02X\n", val);
}


static void
host_to_serial_cb(void *priv)
{
    serial_passthrough_t *dev = (serial_passthrough_t *)priv;
// serial_write_fifo(dev->serial, data);
    timer_on(&dev->host_to_serial_timer, dev->baudrate, 1);
}


static void
serial_passthrough_rcr_cb(struct serial_s *serial, void *priv)
{
    serial_passthrough_t *dev = (serial_passthrough_t *)priv;
}


static void
serial_passthrough_speed_changed(void *priv)
{
    serial_passthrough_t *dev = (serial_passthrough_t *)priv;

    timer_stop(&dev->host_to_serial_timer);
    /* FIXME: do something to dev->baudrate */
    timer_on(&dev->host_to_serial_timer, dev->baudrate, 1);
}


static void
serial_passthrough_dev_close(void *priv)
{
    serial_passthrough_t *dev = (serial_passthrough_t *)priv;

    /* Detach passthrough device from COM port */
    if (dev && dev->serial && dev->serial->sd)
        memset(dev->serial->sd, 0, sizeof(serial_device_t));

    free(dev);
}


/* Initialize the device for use by the user. */
static void *
serial_passthrough_dev_init(const device_t *info)
{
    serial_passthrough_t *dev;

    dev = (serial_passthrough_t *)malloc(sizeof(serial_passthrough_t));
    memset(dev, 0, sizeof(serial_passthrough_t));
    dev->mode = device_get_config_int("mode");

    dev->port = device_get_instance() - 1;
    dev->baudrate = device_get_config_int("baudrate");

    /* Attach passthrough device to a COM port */
    dev->serial = serial_attach(dev->port, serial_passthrough_rcr_cb,
                                serial_passthrough_write, dev);

    serial_passthrough_log("%s: port=COM%d\n", info->name, dev->port + 1);
    serial_passthrough_log("%s: baud=%u\n", info->name, dev->baudrate);
    serial_passthrough_log("%s: mode=%s\n", info->name, serpt_mode_names[dev->mode]);

    timer_add(&dev->host_to_serial_timer, host_to_serial_cb, dev, 0);

    timer_on(&dev->host_to_serial_timer, dev->baudrate, 1);

    /* Return our private data to the I/O layer. */
    return dev;
}


const char *serpt_mode_names[SERPT_MODES_MAX] = {
        [SERPT_MODE_VCON] = "vcon",
        [SERPT_MODE_TCP] = "tcp"
};


static const device_config_t serial_passthrough_config[] = {
    {
        "port", "Serial Port", CONFIG_SELECTION, "", 0, "", { 0 }, {
                {
                        "COM1", 0
                },
                {
                        "COM2", 1
                },
                {
                        "COM3", 2
                },
                {
                        "COM4", 3
                },
                {
                        ""
                }
        }
    },
    {
        "mode", "Passthrough Mode", CONFIG_SELECTION, "", 0, "", { 0 }, {
                {
                        "Pseudo Terminal/Virtual Console", 0
                },
                {
                        ""
                }
        }
    },
    {
        "baudrate", "Baud Rate of Passthrough", CONFIG_SELECTION, "", 115200, "", { 0 }, {
                {
                        "115200", 115200
                },
                {
                        "57600", 57600
                },
                {
                        "38400", 38400
                },
                {
                        "19200", 19200
                },
                {
                        "9600", 9600
                },
                {
                        "4800", 4800
                },
                {
                        "2400", 2400
                },
                {
                        "1200", 1200
                },
                {
                        "300", 300
                },
                {
                        "150", 150
                }
        }
    },
    {
        "", "", -1
    }
};


const device_t serial_passthrough_device = {
    .name = "Serial Passthrough Device",
    .flags = 0,
    .local = 0, 
    .init = serial_passthrough_dev_init,
    .close = serial_passthrough_dev_close,
    .reset = NULL,
    { .poll = NULL },
    .speed_changed = serial_passthrough_speed_changed,
    .force_redraw = NULL,
    .config = &serial_passthrough_config[0]
};

