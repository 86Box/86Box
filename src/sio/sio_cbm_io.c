/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          Implementation of the ACC 3221-SP Super I/O Chip.
 *
 * Authors: Sarah Walker, <https://pcem-emulator.co.uk/>
 *
 *          Copyright 2019 Sarah Walker.
 */
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>
#include <86box/86box.h>
#include <86box/io.h>
#include <86box/timer.h>
#include <86box/device.h>
#include <86box/pci.h>
#include <86box/lpt.h>
#include <86box/serial.h>
#include <86box/hdc.h>
#include <86box/hdc_ide.h>
#include <86box/fdd.h>
#include <86box/fdc.h>
#include <86box/sio.h>
#include <86box/plat_unused.h>

typedef struct cbm_io_t {
    serial_t *uart;
    lpt_t    *lpt;
} cbm_io_t;

static void
cbm_io_write(UNUSED(uint16_t port), uint8_t val, void *priv)
{
    cbm_io_t *dev = (cbm_io_t *) priv;

    lpt_port_remove(dev->lpt);

    switch (val & 0x03) {
        case 0x01:
            lpt_port_setup(dev->lpt, LPT_MDA_ADDR);
            break;
        case 0x02:
            lpt_port_setup(dev->lpt, LPT1_ADDR);
            break;
        case 0x03:
            lpt_port_setup(dev->lpt, LPT2_ADDR);
            break;

        default:
            break;
    }

    switch (val & 0x0c) {
        case 0x04:
            serial_setup(dev->uart, COM2_ADDR, COM2_IRQ);
            break;
        case 0x08:
            serial_setup(dev->uart, COM1_ADDR, COM1_IRQ);
            break;

        default:
            break;
    }
}

static void
cbm_io_close(void *priv)
{
    cbm_io_t *dev = (cbm_io_t *) priv;

    free(dev);
}

static void *
cbm_io_init(UNUSED(const device_t *info))
{
    cbm_io_t *dev = (cbm_io_t *) calloc(1, sizeof(cbm_io_t));

    dev->uart = device_add_inst(&ns16450_device, 1);

    dev->lpt  = device_add_inst(&lpt_port_device, 1);

    io_sethandler(0x0230, 0x0001,
                  NULL, NULL, NULL, cbm_io_write, NULL, NULL,
                  dev);

    return dev;
}

const device_t cbm_io_device = {
    .name          = "Commodore CBM I/O",
    .internal_name = "cbm_io",
    .flags         = 0,
    .local         = 0,
    .init          = cbm_io_init,
    .close         = cbm_io_close,
    .reset         = NULL,
    .available     = NULL,
    .speed_changed = NULL,
    .force_redraw  = NULL,
    .config        = NULL
};
