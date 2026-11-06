/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          Serial backends: adapters that let a UART chip talk to
 *          something that is not a UART.
 *
 * Authors: WNT50
 *
 *          Copyright 2026 WNT50.
 */
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <stdarg.h>
#define HAVE_STDARG_H
#include <86box/86box.h>
#include <86box/device.h>
#include <86box/timer.h>
#include <86box/char.h>
#include <86box/serial.h>

/* ---- adapter for a host chardev ---------------------------------------- */

static int
chardev_read_byte(void *priv)
{
    char_port_t *port = ((serial_backend_store_t *) priv)->port;
    uint8_t      val  = 0;

    if (port->chardev.read(&val, 1, port->chardev.priv) > 0)
        return (int) val;

    return -1;
}

static void
chardev_write_byte(void *priv, uint8_t val)
{
    char_port_t *port = ((serial_backend_store_t *) priv)->port;

    port->chardev.write(&val, 1, port->chardev.priv);
}

static uint32_t
chardev_status(void *priv)
{
    char_port_t *port = ((serial_backend_store_t *) priv)->port;

    return port->chardev.status(port->chardev.priv);
}

static void
chardev_control(void *priv, uint32_t flags)
{
    char_port_t *port = ((serial_backend_store_t *) priv)->port;

    port->chardev.control(flags, port->chardev.priv);
}

static void
chardev_set_format(void *priv, int baud, int data_bits, int parity, int stop_bits)
{
    char_port_t *port = ((serial_backend_store_t *) priv)->port;

    port->com.baud      = baud;
    port->com.data_bits = data_bits;
    port->com.parity    = parity;
    port->com.stop_bits = stop_bits;

    if (port->chardev.port_config != NULL)
        port->chardev.port_config(port->chardev.priv);
}

/* The capabilities of a host chardev are per port and per backend: a pipe
   has no modem lines, a socket has no port_config. Copy the ones that exist
   into the adapter and leave the rest NULL, so the chip can keep testing
   the pointer instead of knowing which backend is behind it. */
const serial_backend_t *
serial_backend_from_chardev(char_port_t *port, serial_backend_store_t *store)
{
    if ((port == NULL) || (store == NULL))
        return NULL;

    memset(store, 0, sizeof(*store));

    store->port          = port;
    store->be.priv       = store;
    store->be.status     = (port->chardev.status != NULL) ? chardev_status : NULL;
    store->be.control    = (port->chardev.control != NULL) ? chardev_control : NULL;
    store->be.read_byte  = (port->chardev.read != NULL) ? chardev_read_byte : NULL;
    store->be.write_byte = (port->chardev.write != NULL) ? chardev_write_byte : NULL;
    store->be.set_format = chardev_set_format;

    return &store->be;
}

/* ---- adapter for another emulated device ------------------------------- */

static void
device_write_byte(void *priv, uint8_t val)
{
    serial_backend_store_t *store = (serial_backend_store_t *) priv;

    store->sd->dev_write(store->serial, store->sd->priv, val);
}

static void
device_set_lcr(void *priv, uint8_t lcr)
{
    serial_backend_store_t *store = (serial_backend_store_t *) priv;

    store->sd->lcr_callback(store->serial, store->sd->priv, lcr);
}

static void
device_rcr(void *priv)
{
    serial_backend_store_t *store = (serial_backend_store_t *) priv;

    store->sd->rcr_callback(store->serial, store->sd->priv);
}

static void
device_dtr(void *priv, int on)
{
    serial_backend_store_t *store = (serial_backend_store_t *) priv;

    store->sd->dtr_callback(store->serial, on, store->sd->priv);
}

static void
device_transmit_period(void *priv, double period)
{
    serial_backend_store_t *store = (serial_backend_store_t *) priv;

    store->sd->transmit_period_callback(store->serial, store->sd->priv, period);
}

/* Unlike a chardev, an in-machine device pushes what it receives straight
   into the FIFO, so there is no read_byte and no status here - only the
   callbacks it asked for in serial_attach_ex*(). Receiving from the
   device keeps going through serial_write_fifo(). */
const serial_backend_t *
serial_backend_from_device(serial_t *serial, serial_device_t *sd, serial_backend_store_t *store)
{
    /* `serial` may be NULL: the 8251 has no serial_t, and the device
       callbacks ignore that argument. */
    if ((sd == NULL) || (store == NULL))
        return NULL;

    memset(store, 0, sizeof(*store));

    store->serial = serial;
    store->sd     = sd;

    store->be.priv            = store;
    store->be.rcr             = (sd->rcr_callback != NULL) ? device_rcr : NULL;
    store->be.dtr             = (sd->dtr_callback != NULL) ? device_dtr : NULL;
    store->be.set_lcr         = (sd->lcr_callback != NULL) ? device_set_lcr : NULL;
    store->be.write_byte      = (sd->dev_write != NULL) ? device_write_byte : NULL;
    store->be.transmit_period = (sd->transmit_period_callback != NULL) ? device_transmit_period : NULL;

    return &store->be;
}
