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


static void
serial_passthrough_timers_on(serial_passthrough_t *dev)
{
    timer_on_auto(&dev->serial_to_host_timer, dev->baudrate);
    timer_on_auto(&dev->host_to_serial_timer, dev->baudrate);
}


static void
serial_passthrough_timers_off(serial_passthrough_t *dev)
{
    timer_stop(&dev->serial_to_host_timer);
    timer_stop(&dev->host_to_serial_timer);
}


static void
serial_passthrough_receive_timer_cb(void *priv)
{
    serial_passthrough_t *dev = (serial_passthrough_t *)priv;
   
    uint8_t data;

    data = 'A';
 
    serial_write_fifo(dev->serial, data);
}


uint32_t
passthrough_get_baudrate(serial_passthrough_t *dev, uint32_t baudrate)
{
    /* try to get baudrate from host, if not possible set the
     * specified one */
    return baudrate;  
}


static void
serial_to_host_cb(void *priv)
{
    serial_passthrough_t *dev = (serial_passthrough_t *)priv;
}


static void
host_to_serial_cb(void *priv)
{
    serial_passthrough_t *dev = (serial_passthrough_t *)priv;

    dev->data = 'B';
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

    /* FIXME: do something to dev->baudrate */
    serial_passthrough_timers_off(dev);
    serial_passthrough_timers_on(dev);
}


static void
serial_passthrough_close(void *priv)
{
    serial_passthrough_t *dev = (serial_passthrough_t *)priv;

    /* Detach passthrough device from COM port */
    if (dev && dev->serial && dev->serial->sd)
        memset(dev->serial->sd, 0, sizeof(serial_device_t));

    free(dev);
}


/* Initialize the device for use by the user. */
static void *
serial_passthrough_init(const device_t *info)
{
    serial_passthrough_t *dev;

    dev = (serial_passthrough_t *)malloc(sizeof(serial_passthrough_t));
    memset(dev, 0, sizeof(serial_passthrough_t));
    dev->mode = device_get_config_int("mode");

    dev->baudrate = passthrough_get_baudrate(dev, 9600);
    dev->port = device_get_config_int("port");

    /* Attach passthrough device to a COM port */
    dev->serial = serial_attach(dev->port, serial_passthrough_rcr_cb,
                                NULL, dev);

    serial_passthrough_log("%s: port=COM%d\n", dev->name, dev->port + 1);

    timer_add(&dev->serial_to_host_timer, serial_to_host_cb, dev, 0);
    timer_add(&dev->host_to_serial_timer, host_to_serial_cb, dev, 0);

    serial_passthrough_timers_on(dev);

    /* Return our private data to the I/O layer. */
    return dev;
}


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
        "mode", "Passthrough Mode", CONFIG_SELECTION, "", 1, "", { 0 }, {
                {
                        "Pseudo Terminal/Virtual Console", 1
                },
                {
                        ""
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
    .init = serial_passthrough_init,
    .close = serial_passthrough_close,
    .reset = NULL,
    { .poll = NULL },
    .speed_changed = serial_passthrough_speed_changed,
    .force_redraw = NULL,
    .config = &serial_passthrough_config[0]
};

