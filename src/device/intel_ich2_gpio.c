/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          Implementation of Intel ICH2 GPIO
 *
 *
 *
 * Authors: Tiseno100,
 *
 *          Copyright 2022 Tiseno100.
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
#include <86box/intel_ich2_gpio.h>

#ifdef ENABLE_INTEL_ICH2_GPIO_LOG
int intel_ich2_gpio_do_log = ENABLE_INTEL_ICH2_GPIO_LOG;
static void
intel_ich2_gpio_log(const char *fmt, ...)
{
    va_list ap;

    if (intel_ich2_gpio_do_log) {
        va_start(ap, fmt);
        pclog_ex(fmt, ap);
        va_end(ap);
    }
}
#else
#    define intel_ich2_gpio_log(fmt, ...)
#endif

static void
intel_ich2_gpio_write(uint16_t addr, uint8_t val, void *priv)
{
    intel_ich2_gpio_t *dev = (intel_ich2_gpio_t *) priv;

    addr -= dev->gpio_addr;

    intel_ich2_gpio_log("Intel ICH2 GPIO: Write 0x%02x on GPIO Register 0x%02x\n", val, addr);

    switch (addr) {
        /* GPIO Use Enable */
        case 0x00:
            dev->gpio_regs[addr] = val & 0x3f;
            break;

        case 0x01:
            dev->gpio_regs[addr] = val & 8;
            break;

        case 0x02:
            dev->gpio_regs[addr] = val & 0x20;
            break;

        /* GPIO I/O Select */
        case 0x07:
            dev->gpio_regs[addr] = val & 0x1b;
            break;

        /* GPIO Level */
        case 0x0e:
            dev->gpio_regs[addr] = val;
            break;

        case 0x0f:
            dev->gpio_regs[addr] = val & 0x1b;
            dev->gpio_regs[addr] &= dev->gpio_regs[0x1b]; // Mask out whatever change if the bits aren't programmed as outputs.
            break;

        /* GPIO Blink which is not Utilized */
        case 0x1a:
            dev->gpio_regs[addr] = val & 6;
            break;

        case 0x1b:
            dev->gpio_regs[addr] = val & 0x1a;
            break;

        /* GPIO Signal Inverter */
        case 0x2d:
            dev->gpio_regs[addr] = val & 0x39;
            break;
    }
}

static uint8_t
intel_ich2_gpio_read(uint16_t addr, void *priv)
{
    intel_ich2_gpio_t *dev = (intel_ich2_gpio_t *) priv;

    addr -= dev->gpio_addr;

    intel_ich2_gpio_log("Intel ICH2 GPIO: Reading 0x%02x from Register 0x%02x\n", dev->gpio_regs[addr], addr);

    if (addr <= 0x2f)
        return dev->gpio_regs[addr];
    else
        return 0xff;
}

void
intel_ich2_gpio_base(int enable, uint16_t addr, intel_ich2_gpio_t *dev)
{
    if (dev->gpio_addr != 0)
        io_removehandler(dev->gpio_addr, 15, intel_ich2_gpio_read, NULL, NULL, intel_ich2_gpio_write, NULL, NULL, dev);

    dev->gpio_addr = addr;

    if ((addr != 0) && enable)
        io_sethandler(addr, 15, intel_ich2_gpio_read, NULL, NULL, intel_ich2_gpio_write, NULL, NULL, dev);
}

static void
intel_ich2_gpio_reset(void *priv)
{
    intel_ich2_gpio_t *dev = (intel_ich2_gpio_t *) priv;
    dev->gpio_addr         = 0;

    /* Enabled GPIO's */
    dev->gpio_regs[0x00] = 0x80;
    dev->gpio_regs[0x01] = 0x31;
    dev->gpio_regs[0x03] = 0x1a;

    /* GPIO Drives (Input or Output) */
    dev->gpio_regs[0x04] = 0xff;
    dev->gpio_regs[0x05] = 0xff;

    dev->gpio_regs[0x0e] = 0x3f;
    dev->gpio_regs[0x0f] = 0x1b;

    dev->gpio_regs[0x16] = 0x63;
    dev->gpio_regs[0x17] = 0x06;
}

static void
intel_ich2_gpio_close(void *priv)
{
    intel_ich2_gpio_t *dev = (intel_ich2_gpio_t *) priv;

    free(dev);
}

static void *
intel_ich2_gpio_init(const device_t *info)
{
    intel_ich2_gpio_t *dev = (intel_ich2_gpio_t *) malloc(sizeof(intel_ich2_gpio_t));
    memset(dev, 0, sizeof(intel_ich2_gpio_t));

    intel_ich2_gpio_reset(dev);

    return dev;
}

const device_t intel_ich2_gpio_device = {
    .name          = "Intel ICH2 GPIO",
    .internal_name = "intel_ich2_gpio",
    .flags         = 0,
    .local         = 0,
    .init          = intel_ich2_gpio_init,
    .close         = intel_ich2_gpio_close,
    .reset         = intel_ich2_gpio_reset,
    { .available = NULL },
    .speed_changed = NULL,
    .force_redraw  = NULL,
    .config        = NULL
};
