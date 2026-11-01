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

static serial_device_t serial_devices[SERIAL_MAX];

/* Hand the chip its callback slot and, if a device already asked for this
   port before the chip existed, build the backend now. Both sides call
   this, so the order of attach and chip init no longer matters. */
void
serial_device_attach(int port)
{
    serial_device_t *sd;

    if ((port < 0) || (port >= SERIAL_MAX))
        return;

    sd = &serial_devices[port];

    if (com_ports[port].serial != NULL)
        com_ports[port].serial->sd = sd;

    if ((sd->rcr_callback == NULL) && (sd->dev_write == NULL) &&
        (sd->dtr_callback == NULL) && (sd->lcr_callback == NULL) &&
        (sd->transmit_period_callback == NULL))
        return;

    if (com_ports[port].set_device_backend != NULL)
        com_ports[port].set_device_backend(com_ports[port].chip, sd);
}

ser_port_t *
serial_attach_ex(int port,
                 void (*rcr_callback)(struct serial_s *serial, void *priv),
                 void (*dev_write)(struct serial_s *serial, void *priv, uint8_t data),
                 void (*transmit_period_callback)(struct serial_s *serial, void *priv, double transmit_period),
                 void (*lcr_callback)(struct serial_s *serial, void *priv, uint8_t data_bits),
                 void *priv)
{
    /* Make sure this port becomes non-hotunpluggable if a hotunpluggable dropdown
       device and a non-hotunpluggable external device are both trying to claim it. */
    uint8_t hotunplug         = com_ports[port].hotunplug;
    com_ports[port].hotunplug = CHAR_PORT_NOHOTUNPLUG;
    if (hotunplug != CHAR_PORT_DETACHED)
        return NULL;

    serial_device_t *sd = &serial_devices[port];

    sd->rcr_callback             = rcr_callback;
    sd->dev_write                = dev_write;
    sd->transmit_period_callback = transmit_period_callback;
    sd->lcr_callback             = lcr_callback;
    sd->priv                     = priv;

    serial_device_attach(port);

    return (com_ports[port].chip != NULL) ? &com_ports[port] : NULL;
}

ser_port_t *
serial_attach_ex_2(int port,
                   void (*rcr_callback)(struct serial_s *serial, void *priv),
                   void (*dev_write)(struct serial_s *serial, void *priv, uint8_t data),
                   void (*dtr_callback)(struct serial_s *serial, int status, void *priv),
                   void *priv)
{
    /* Make sure this port becomes non-hotunpluggable if a hotunpluggable dropdown
       device and a non-hotunpluggable external device are both trying to claim it. */
    uint8_t hotunplug         = com_ports[port].hotunplug;
    com_ports[port].hotunplug = CHAR_PORT_NOHOTUNPLUG;
    if (hotunplug != CHAR_PORT_DETACHED)
        return NULL;

    serial_device_t *sd = &serial_devices[port];

    sd->rcr_callback             = rcr_callback;
    sd->dtr_callback             = dtr_callback;
    sd->dev_write                = dev_write;
    sd->transmit_period_callback = NULL;
    sd->lcr_callback             = NULL;
    sd->priv                     = priv;

    serial_device_attach(port);

    return (com_ports[port].chip != NULL) ? &com_ports[port] : NULL;
}

void
serial_devices_init(void)
{
    for (uint8_t i = 0; i < SERIAL_MAX; i++) {
        /* Leave non-hotunpluggable ports and their devices alone. */
        if (com_ports[i].hotunplug >= CHAR_PORT_NOHOTUNPLUG)
            continue;

        memset(&(serial_devices[i]), 0, sizeof(serial_device_t));

        com_ports[i].hotunplug = CHAR_PORT_DETACHED;

        if (com_ports[i].set_host_backend == NULL)
            continue;

        com_ports[i].set_host_backend(com_ports[i].chip, NULL);

        memset(&com_ports[i].char_port, 0, sizeof(com_ports[i].char_port));
        if (com_ports[i].device) {
            com_ports[i].char_port.type = CHAR_PORT_COM;
            snprintf(com_ports[i].char_port.name, sizeof(com_ports[i].char_port.name), "COM%i", i + 1);
            const device_t *device = char_get_device(com_ports[i].device);
            char_init(&com_ports[i].char_port, device, i + 1);

            com_ports[i].set_host_backend(com_ports[i].chip, &com_ports[i].char_port);

            /* This port is hotunpluggable if the device allows it, unless further serial_attach attempts are made. */
            com_ports[i].hotunplug = (device->flags & DEVICE_HOTPLUG_OUT) ? CHAR_PORT_HOTUNPLUG : CHAR_PORT_NOHOTUNPLUG;
        }
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

        memset(&(serial_devices[i]), 0, sizeof(serial_device_t));

        if (com_ports[i].set_host_backend == NULL)
            continue;

        com_ports[i].set_host_backend(com_ports[i].chip, NULL);
        memset(&com_ports[i].char_port, 0, sizeof(char_port_t));
    }
}

void
serial_devices_reset(void)
{
    /* Only close the dropdown devices of the ports that serial_devices_close() and serial_devices_init()
       will go through. A port where another device also asked for the port, such as a serial mouse,
       is left alone by them, and closing its device would leave the port calling into it. */
    for (uint8_t i = 0; i < SERIAL_MAX; i++) {
        if (com_ports[i].hotunplug < CHAR_PORT_NOHOTUNPLUG)
            device_close_by_flags_inst(DEVICE_COM | DEVICE_HOTPLUG_OUT, i + 1);
    }

    serial_devices_close(1);
    serial_devices_init();
}

/* ---- device side: what an in-machine device can do to the chip ---------- */

void
serial_port_receive(ser_port_t *port, uint8_t byte)
{
    if ((port != NULL) && (port->ops != NULL) && (port->ops->receive != NULL))
        port->ops->receive(port->chip, byte);
}

int
serial_port_can_receive(ser_port_t *port)
{
    if ((port == NULL) || (port->ops == NULL) || (port->ops->can_receive == NULL))
        return 0;

    return port->ops->can_receive(port->chip);
}

void
serial_port_set_line(ser_port_t *port, uint32_t line, int on)
{
    if ((port != NULL) && (port->ops != NULL) && (port->ops->set_line != NULL))
        port->ops->set_line(port->chip, line, on);
}

int
serial_port_get_line(ser_port_t *port, uint32_t line)
{
    if ((port == NULL) || (port->ops == NULL) || (port->ops->get_line == NULL))
        return 0;

    return port->ops->get_line(port->chip, line);
}

void
serial_port_detach(ser_port_t *port)
{
    int i;

    if ((port == NULL) || (port->set_host_backend == NULL))
        return;

    i = (int) (port - com_ports);
    if ((i < 0) || (i >= SERIAL_MAX))
        return;

    port->set_host_backend(port->chip, NULL);
    memset(&serial_devices[i], 0, sizeof(serial_device_t));
}
