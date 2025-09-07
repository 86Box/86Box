/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          Implementation of the ACCESS.bus.
 *
 * Authors: Miran Grca, <mgrca8@gmail.com>
 *
 *          Copyright 2024-2025 Miran Grca.
 */
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>
#include <86box/86box.h>
#include <86box/io.h>
#include <86box/device.h>
#include <86box/access_bus.h>
#include <86box/plat_unused.h>

static uint8_t
access_bus_in(uint16_t port, void *priv)
{
    const access_bus_t *dev = (access_bus_t *) priv;
    uint8_t             ret = 0xff;

    switch (port & 3) {
        case 0:
            ret = (dev->status & 0xbf);
            break;
        case 1:
            ret = (dev->own_addr & 0x7f);
            break;
        case 2:
            ret = dev->data;
            break;
        case 3:
            ret = (dev->clock & 0x87);
            break;

        default:
            break;
    }

    return ret;
}

static void
access_bus_out(uint16_t port, uint8_t val, void *priv)
{
    access_bus_t *dev = (access_bus_t *) priv;

    switch (port & 3) {
        case 0:
            dev->control = (val & 0xcf);
            break;
        case 1:
            dev->own_addr = (val & 0x7f);
            break;
        case 2:
            dev->data = val;
            break;
        case 3:
            dev->clock &= 0x80;
            dev->clock |= (val & 0x07);
            break;

        default:
            break;
    }
}

void
access_bus_handler(access_bus_t *dev, uint8_t enable, uint16_t base)
{
    if (dev->enable && (dev->base >= 0x0100) && (dev->base <= 0x0ffc))
        io_removehandler(dev->base, 0x0004,
                         access_bus_in, NULL, NULL, access_bus_out, NULL, NULL, dev);

    dev->enable = enable;
    dev->base   = base;

    if (dev->enable && (dev->base >= 0x0100) && (dev->base <= 0x0ffc))
        io_sethandler(dev->base, 0x0004,
                      access_bus_in, NULL, NULL, access_bus_out, NULL, NULL, dev);
}


static void
access_bus_close(void *priv)
{
    access_bus_t *dev = (access_bus_t *) priv;

    free(dev);
}

static void *
access_bus_init(UNUSED(const device_t *info))
{
    access_bus_t *dev = (access_bus_t *) calloc(1, sizeof(access_bus_t));

    return dev;
}

const device_t access_bus_device = {
    .name          = "ACCESS.bus",
    .internal_name = "access_bus",
    .flags         = 0,
    .local         = 0,
    .init          = access_bus_init,
    .close         = access_bus_close,
    .reset         = NULL,
    .available     = NULL,
    .speed_changed = NULL,
    .force_redraw  = NULL,
    .config        = NULL
};
