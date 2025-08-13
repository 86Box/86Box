/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          Implementation of the Radisys EPC-2012 Configuration registers.
 *
 * Authors: Miran Grca, <mgrca8@gmail.com>
 *
 *          Copyright 2025 Miran Grca.
 */
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>
#define HAVE_STDARG_H
#include <86box/86box.h>
#include "cpu.h"
#include <86box/timer.h>
#include <86box/io.h>
#include <86box/device.h>
#include <86box/machine.h>
#include <86box/lpt.h>
#include <86box/machine.h>
#include <86box/chipset.h>
#include <86box/plat_unused.h>

typedef struct radisys_config_t {
    uint8_t regs[2];
} radisys_config_t;

static uint8_t
radisys_config_read(uint16_t port, void *priv)
{
    radisys_config_t *dev = (radisys_config_t *) priv;
    uint8_t           ret = dev->regs[port & 0x0001];

    return ret;
}

static void
radisys_config_write(uint16_t port, uint8_t val, void *priv)
{
    radisys_config_t *dev = (radisys_config_t *) priv;

    dev->regs[port & 0x0001] = val;

    if (!(port & 0x0001) && machine_has_jumpered_ecp_dma(machine, MACHINE_DMA_USE_CONFIG))
        lpt1_dma((val & 0x02) ? 3 : 1);
}

static void
radisys_config_close(void *priv)
{
    radisys_config_t *dev = (radisys_config_t *) priv;

    free(dev);
}

static void *
radisys_config_init(UNUSED(const device_t *info))
{
    /* We have to return something non-NULL. */
    radisys_config_t *dev = (radisys_config_t *) calloc(1, sizeof(radisys_config_t));

    /* 370h is also supported. */
    io_sethandler(0x0270, 0x0002, radisys_config_read, NULL, NULL, radisys_config_write, NULL, NULL, dev);

    return dev;
}

const device_t radisys_config_device = {
    .name          = "Radisys EPC-2012 Configuration",
    .internal_name = "radisys_config",
    .flags         = 0,
    .local         = 0,
    .init          = radisys_config_init,
    .close         = radisys_config_close,
    .reset         = NULL,
    .available     = NULL,
    .speed_changed = NULL,
    .force_redraw  = NULL,
    .config        = NULL
};
