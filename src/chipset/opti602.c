/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          Implementation of the OPTi 82C601/82C602 Buffer Devices.
 *
 * Authors: Miran Grca, <mgrca8@gmail.com>
 *
 *          Copyright 2023 Miran Grca.
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
#include <86box/io.h>
#include <86box/device.h>
#include <86box/mem.h>
#include <86box/timer.h>
#include <86box/nvr.h>
#include <86box/smram.h>
#include <86box/port_92.h>
#include <86box/chipset.h>
#include <86box/plat_unused.h>

typedef struct opti602_t {
    uint8_t    idx;

    uint8_t    regs[256];
    uint8_t    gpio[32];

    uint16_t   gpio_base;

    uint16_t   gpio_mask;
    uint16_t   gpio_size;

    nvr_t     *nvr;
} opti602_t;

#ifdef ENABLE_OPTI602_LOG
int opti602_do_log = ENABLE_OPTI602_LOG;

static void
opti602_log(const char *fmt, ...)
{
    va_list ap;

    if (opti602_do_log) {
        va_start(ap, fmt);
        pclog_ex(fmt, ap);
        va_end(ap);
    }
}
#else
#    define opti602_log(fmt, ...)
#endif

static void
opti602_gpio_write(uint16_t addr, uint8_t val, void *priv)
{
    opti602_t *dev = (opti602_t *) priv;

    dev->gpio[addr - dev->gpio_base] = val;
}

static uint8_t
opti602_gpio_read(uint16_t addr, void *priv)
{
    const opti602_t *dev = (opti602_t *) priv;
    uint8_t ret = 0xff;

    ret = dev->gpio[addr - dev->gpio_base];

    return ret;
}

static void
opti602_gpio_recalc(opti602_t *dev)
{
    if (dev->gpio_base != 0x0000)
        io_removehandler(dev->gpio_base, dev->gpio_size, opti602_gpio_read, NULL, NULL, opti602_gpio_write, NULL, NULL, dev);

    dev->gpio_base = dev->regs[0xf8];
    dev->gpio_base |= (((uint16_t) dev->regs[0xf7]) << 8);

    dev->gpio_size = 1 << ((dev->regs[0xf9] >> 2) & 0x07);

    dev->gpio_mask = ~(dev->gpio_size - 1);
    dev->gpio_base &= dev->gpio_mask;

    dev->gpio_mask = ~dev->gpio_mask;

    if (dev->gpio_base != 0x0000)
        io_sethandler(dev->gpio_base, dev->gpio_size, opti602_gpio_read, NULL, NULL, opti602_gpio_write, NULL, NULL, dev);
}

static void
opti602_write(uint16_t addr, uint8_t val, void *priv)
{
    opti602_t *dev = (opti602_t *) priv;

    switch (addr) {
        case 0x22:
            dev->idx = val;
            break;
        case 0x24:
           if ((dev->idx == 0xea) || ((dev->idx >= 0xf7) && (dev->idx <= 0xfa))) { 
                dev->regs[dev->idx] = val;
                opti602_log("dev->regs[%04x] = %08x\n", dev->idx, val);

                /* TODO: Registers 0x30-0x3F for OPTi 802GP and 898. */
                switch (dev->idx) {
                    case 0xea:
                        /* GREEN Power Port */
                        break;

                    case 0xf7:
                    case 0xf8:
                        /* General Purpose Chip Select Registers */
                        opti602_gpio_recalc(dev);
                        break;

                    case 0xf9:
                        /* General Purpose Chip Select Register */
                        nvr_bank_set(0, !!(val & 0x20), dev->nvr);
                        opti602_gpio_recalc(dev);
                        break;

                    case 0xfa:
                        /* GPM Port */
                        break;

                    default:
                        break;
                }
            }
            break;

        default:
            break;
    }
}

static uint8_t
opti602_read(uint16_t addr, void *priv)
{
    uint8_t          ret = 0xff;
    const opti602_t *dev = (opti602_t *) priv;

    switch (addr) {
        case 0x24:
           if ((dev->idx == 0xea) || ((dev->idx >= 0xf7) && (dev->idx <= 0xfa))) { 
                ret = dev->regs[dev->idx];
                if ((dev->idx == 0xfa) && (dev->regs[0xf9] & 0x40))
                    ret |= dev->regs[0xea];
            }
            break;

        default:
            break;
    }

    return ret;
}

static void
opti602_reset(void *priv)
{
    opti602_t *dev = (opti602_t *) priv;

    memset(dev->regs, 0x00, 256 * sizeof(uint8_t));
    memset(dev->gpio, 0x00, 32 * sizeof(uint8_t));

    dev->regs[0xfa] = 0x07;

    dev->gpio[0x01] |= 0xfe;

    nvr_bank_set(0, 0, dev->nvr);
    opti602_gpio_recalc(dev);
}

static void
opti602_close(void *priv)
{
    opti602_t *dev = (opti602_t *) priv;

    free(dev);
}

static void *
opti602_init(UNUSED(const device_t *info))
{
    opti602_t *dev = (opti602_t *) calloc(1, sizeof(opti602_t));

    io_sethandler(0x0022, 0x0001, opti602_read, NULL, NULL, opti602_write, NULL, NULL, dev);
    io_sethandler(0x0024, 0x0001, opti602_read, NULL, NULL, opti602_write, NULL, NULL, dev);

    dev->nvr   = device_add(&at_mb_nvr_device);

    opti602_reset(dev);

    return dev;
}

const device_t opti601_device = {
    .name          = "OPTi 82C601",
    .internal_name = "opti601",
    .flags         = 0,
    .local         = 0,
    .init          = opti602_init,
    .close         = opti602_close,
    .reset         = opti602_reset,
    .available     = NULL,
    .speed_changed = NULL,
    .force_redraw  = NULL,
    .config        = NULL
};

const device_t opti602_device = {
    .name          = "OPTi 82C602",
    .internal_name = "opti602",
    .flags         = 0,
    .local         = 0,
    .init          = opti602_init,
    .close         = opti602_close,
    .reset         = opti602_reset,
    .available     = NULL,
    .speed_changed = NULL,
    .force_redraw  = NULL,
    .config        = NULL
};
