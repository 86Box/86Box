/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          Serial port backend for the automation socket.
 *
 *          What the guest sends is kept for automation clients to read,
 *          and what they write is handed to the guest at the UART's own
 *          rate, in emulated time. The buffers live in automation.c, so
 *          they survive the hard resets that close this backend.
 *
 * Authors: Justin Akers, <5621217+dafugg@users.noreply.github.com>
 *
 *          Copyright 2026 Justin Akers.
 */
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define HAVE_STDARG_H
#include <86box/86box.h>
#include <86box/device.h>
#include <86box/char.h>
#include <86box/plat.h>
#include <86box/automation.h>

typedef struct {
    int          port; /* 1 = COM1 */
    char_port_t *char_port;
} char_automation_t;

/* The UART polls for input once per bit time, so that a byte waiting is
   taken as soon as the receiver is free; a client's bytes would then arrive
   up to ten times faster than the line could carry them, and a guest that
   reads its UART on interrupts at line rate (a terminal program writing a
   capture file, say) overruns. A character is given out no faster than its
   frame takes on the line the guest has set up: start, data, parity, stop. */
static size_t
char_automation_read(uint8_t *buf, size_t len, void *priv)
{
    char_automation_t *dev     = (char_automation_t *) priv;
    double             byte_us = 0.0;

    if (dev->char_port->com.baud) {
        int bits = 1 + dev->char_port->com.data_bits + (dev->char_port->com.parity & 1) + dev->char_port->com.stop_bits;
        byte_us  = (bits * 1000000.0) / dev->char_port->com.baud;
    }
    if (len && automation_serial_in(dev->port, buf, byte_us))
        return 1;
    return 0;
}

static size_t
char_automation_write(uint8_t *buf, size_t len, void *priv)
{
    char_automation_t *dev = (char_automation_t *) priv;

    for (size_t i = 0; i < len; i++)
        automation_serial_out(dev->port, buf[i]);
    return len;
}

static uint32_t
char_automation_status(void *priv)
{
    char_automation_t *dev = (char_automation_t *) priv;

    return automation_serial_lines(dev->port);
}

static void
char_automation_control(uint32_t flags, void *priv)
{
    char_automation_t *dev = (char_automation_t *) priv;

    automation_serial_control(dev->port, flags);
}

static void
char_automation_close(void *priv)
{
    char_automation_t *dev = (char_automation_t *) priv;

    automation_serial_attach(dev->port, 0);
    free(dev);
}

static void *
char_automation_init(UNUSED(const device_t *info))
{
    char_automation_t *dev = (char_automation_t *) calloc(1, sizeof(char_automation_t));

    dev->char_port = char_attach(0, char_automation_read, char_automation_write, char_automation_status,
                                 char_automation_control, NULL, dev);
    /* serial_devices_init() names the port "COMn". */
    dev->port = atoi(dev->char_port->name + 3);
    automation_serial_attach(dev->port, 1);

    return dev;
}

const device_t char_automation_com_device = {
    .name          = "Automation socket",
    .internal_name = "automation",
    .flags         = DEVICE_COM | DEVICE_HOTPLUG,
    .local         = 0,
    .init          = char_automation_init,
    .close         = char_automation_close,
    .reset         = NULL,
    .available     = NULL,
    .speed_changed = NULL,
    .force_redraw  = NULL,
    .config        = NULL
};
