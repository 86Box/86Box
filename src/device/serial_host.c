/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          Serial port policy: which COM port gets which host device,
 *          when that device is attached or detached, and who owns the
 *          port while an in-machine device (modem, mouse, tablet) is
 *          using it. No UART register, timing or interrupt logic
 *          belongs here.
 *
 *          The seam itself lives next to it: a serial_endpoint_t is the
 *          chip's end of the connection, and everything that hands a chip
 *          a backend goes through it. A COM port is a table entry pointing
 *          at an endpoint; a board that carries its own chip keeps the
 *          endpoint and never touches com_ports[].
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

ser_port_t com_ports[SERIAL_MAX] = { 0 };
/* A device can claim a COM port before the chip is plugged into it; park its
   callbacks here until the chip shows up. Only COM ports need this: a board
   that carries its own chip already holds the endpoint. */
static serial_device_t pending[SERIAL_MAX];

/* ---- the seam ---------------------------------------------------------- */

void
serial_endpoint_init(serial_endpoint_t *ep, void *chip, const serial_port_ops_t *ops)
{
    memset(ep, 0, sizeof(*ep));

    ep->chip = chip;
    ep->ops  = ops;
}

void
serial_endpoint_attach_device(serial_endpoint_t *ep, const serial_device_t *sd)
{
    if ((ep == NULL) || (sd == NULL))
        return;

    ep->dev = *sd;
    ep->be  = serial_backend_from_device(ep->serial, &ep->dev, &ep->store);

    if (ep->on_attach != NULL)
        ep->on_attach(ep->chip);
}

void
serial_endpoint_attach_chardev(serial_endpoint_t *ep, char_port_t *port)
{
    if (ep == NULL)
        return;

    ep->be = (port != NULL) ? serial_backend_from_chardev(port, &ep->store) : NULL;

    if (ep->on_attach != NULL)
        ep->on_attach(ep->chip);
}

void
serial_endpoint_detach(serial_endpoint_t *ep)
{
    if (ep == NULL)
        return;

    ep->be = NULL;
    memset(&ep->dev, 0, sizeof(ep->dev));

    if (ep->on_attach != NULL)
        ep->on_attach(ep->chip);
}

/* ---- device side: what an in-machine device can do to the chip ---------- */

void
serial_endpoint_receive(serial_endpoint_t *ep, uint8_t byte)
{
    if ((ep != NULL) && (ep->ops != NULL) && (ep->ops->receive != NULL))
        ep->ops->receive(ep->chip, byte);
}

int
serial_endpoint_can_receive(serial_endpoint_t *ep)
{
    if ((ep == NULL) || (ep->ops == NULL) || (ep->ops->can_receive == NULL))
        return 0;

    return ep->ops->can_receive(ep->chip);
}

void
serial_endpoint_set_line(serial_endpoint_t *ep, uint32_t line, int on)
{
    if ((ep != NULL) && (ep->ops != NULL) && (ep->ops->set_line != NULL))
        ep->ops->set_line(ep->chip, line, on);
}

int
serial_endpoint_get_line(serial_endpoint_t *ep, uint32_t line)
{
    if ((ep == NULL) || (ep->ops == NULL) || (ep->ops->get_line == NULL))
        return 0;

    return ep->ops->get_line(ep->chip, line);
}

/* ---- COM ports: policy on top of the seam ------------------------------- */

/* Hand the chip the device a COM port is waiting for, when the chip is there.
   Both sides call this, so the order of attach and chip init does not
   matter. */
void
serial_device_attach(int port)
{
    ser_port_t *sp;

    if ((port < 0) || (port >= SERIAL_MAX))
        return;

    sp = &com_ports[port];

    if (sp->ep == NULL)
        return;

    if ((pending[port].rcr_callback == NULL) && (pending[port].dev_write == NULL) &&
        (pending[port].dtr_callback == NULL) && (pending[port].lcr_callback == NULL) &&
        (pending[port].transmit_period_callback == NULL))
        return;

    serial_endpoint_attach_device(sp->ep, &pending[port]);
}

ser_port_t *
serial_attach_ex(int port,
                 void (*rcr_callback)(struct serial_s *serial, void *priv),
                 void (*dev_write)(struct serial_s *serial, void *priv, uint8_t data),
                 void (*transmit_period_callback)(struct serial_s *serial, void *priv, double transmit_period),
                 void (*lcr_callback)(struct serial_s *serial, void *priv, uint8_t data_bits),
                 void *priv)
{
    if ((port < 0) || (port >= SERIAL_MAX))
        return NULL;

    /* Make sure this port becomes non-hotunpluggable if a hotunpluggable dropdown
       device and a non-hotunpluggable external device are both trying to claim it. */
    uint8_t hotunplug         = com_ports[port].hotunplug;
    com_ports[port].hotunplug = CHAR_PORT_NOHOTUNPLUG;
    if (hotunplug != CHAR_PORT_DETACHED)
        return NULL;

    serial_device_t *sd = &pending[port];

    sd->transmit_period_callback = transmit_period_callback;
    sd->lcr_callback             = lcr_callback;
    sd->rcr_callback             = rcr_callback;
    sd->dtr_callback             = NULL;
    sd->priv                     = priv;
    sd->dev_write                = dev_write;

    serial_device_attach(port);

    return (com_ports[port].ep != NULL) ? &com_ports[port] : NULL;
}

ser_port_t *
serial_attach_ex_2(int port,
                   void (*rcr_callback)(struct serial_s *serial, void *priv),
                   void (*dev_write)(struct serial_s *serial, void *priv, uint8_t data),
                   void (*dtr_callback)(struct serial_s *serial, int status, void *priv),
                   void *priv)
{
    if ((port < 0) || (port >= SERIAL_MAX))
        return NULL;

    /* Make sure this port becomes non-hotunpluggable if a hotunpluggable dropdown
       device and a non-hotunpluggable external device are both trying to claim it. */
    uint8_t hotunplug         = com_ports[port].hotunplug;
    com_ports[port].hotunplug = CHAR_PORT_NOHOTUNPLUG;
    if (hotunplug != CHAR_PORT_DETACHED)
        return NULL;

    serial_device_t *sd = &pending[port];

    sd->rcr_callback             = rcr_callback;
    sd->dtr_callback             = dtr_callback;
    sd->dev_write                = dev_write;
    sd->transmit_period_callback = NULL;
    sd->lcr_callback             = NULL;
    sd->priv                     = priv;

    serial_device_attach(port);

    return (com_ports[port].ep != NULL) ? &com_ports[port] : NULL;
}

void
serial_devices_init(void)
{
    for (uint8_t i = 0; i < SERIAL_MAX; i++) {
        /* Leave non-hotunpluggable ports and their devices alone. */
        if (com_ports[i].hotunplug >= CHAR_PORT_NOHOTUNPLUG)
            continue;

        memset(&(pending[i]), 0, sizeof(serial_device_t));

        com_ports[i].hotunplug = CHAR_PORT_DETACHED;

        if (com_ports[i].ep == NULL)
            continue;

        serial_endpoint_attach_chardev(com_ports[i].ep, NULL);

        memset(&com_ports[i].char_port, 0, sizeof(com_ports[i].char_port));
#ifndef IS_TEST
        if (com_ports[i].device) {
            com_ports[i].char_port.type = CHAR_PORT_COM;
            snprintf(com_ports[i].char_port.name, sizeof(com_ports[i].char_port.name), "COM%i", i + 1);
            const device_t *device = char_get_device(com_ports[i].device);
            char_init(&com_ports[i].char_port, device, i + 1);

            serial_endpoint_attach_chardev(com_ports[i].ep, &com_ports[i].char_port);

            /* This port is hotunpluggable if the device allows it, unless further serial_attach attempts are made. */
            com_ports[i].hotunplug = (device->flags & DEVICE_HOTPLUG_OUT) ? CHAR_PORT_HOTUNPLUG : CHAR_PORT_NOHOTUNPLUG;
        }
#endif
    }
}

void
serial_devices_close(int hotplug)
{
    for (uint8_t i = 0; i < SERIAL_MAX; i++) {
        /* Leave non-hotunpluggable ports and their devices alone in a hotplug operation. */
        if (hotplug && (com_ports[i].hotunplug >= CHAR_PORT_NOHOTUNPLUG))
            continue;
        com_ports[i].hotunplug = CHAR_PORT_DETACHED;

        memset(&(pending[i]), 0, sizeof(serial_device_t));

        if (com_ports[i].ep == NULL)
            continue;

        serial_endpoint_detach(com_ports[i].ep);
        memset(&com_ports[i].char_port, 0, sizeof(char_port_t));
    }
}

void
serial_devices_reset(void)
{
#ifndef IS_TEST
    /* Only close the dropdown devices of the ports that serial_devices_close() and serial_devices_init()
       will go through. A port where another device also asked for the port, such as a serial mouse,
       is left alone by them, and closing its device would leave the port calling into it. */
    for (uint8_t i = 0; i < SERIAL_MAX; i++) {
        if (com_ports[i].hotunplug < CHAR_PORT_NOHOTUNPLUG)
            device_close_by_flags_inst(DEVICE_COM | DEVICE_HOTPLUG_OUT, i + 1);
    }
#endif

    serial_devices_close(1);
    serial_devices_init();
}

/* ---- port entry view of an endpoint ------------------------------------- */

void
serial_port_receive(ser_port_t *port, uint8_t byte)
{
    serial_endpoint_receive((port != NULL) ? port->ep : NULL, byte);
}

int
serial_port_can_receive(ser_port_t *port)
{
    return serial_endpoint_can_receive((port != NULL) ? port->ep : NULL);
}

void
serial_port_set_line(ser_port_t *port, uint32_t line, int on)
{
    serial_endpoint_set_line((port != NULL) ? port->ep : NULL, line, on);
}

int
serial_port_get_line(ser_port_t *port, uint32_t line)
{
    return serial_endpoint_get_line((port != NULL) ? port->ep : NULL, line);
}

void
serial_port_detach(ser_port_t *port)
{
    if ((port == NULL) || (port->ep == NULL))
        return;

    serial_endpoint_detach(port->ep);

    if ((port >= com_ports) && (port < (com_ports + SERIAL_MAX)))
        memset(&pending[port - com_ports], 0, sizeof(serial_device_t));
}
