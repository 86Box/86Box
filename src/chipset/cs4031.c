/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          Implementation of the Chips & Technologies CS4031 chipset.
 *
 *
 *
 * Authors: Tiseno100
 *
 *          Copyright 2021 Tiseno100
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
#include <86box/mem.h>
#include <86box/plat_unused.h>
#include <86box/port_92.h>
#include <86box/chipset.h>

typedef struct cs4031_t {
    uint8_t    index;
    uint8_t    regs[256];
    port_92_t *port_92;
} cs4031_t;

#ifdef ENABLE_CS4031_LOG
int cs4031_do_log = ENABLE_CS4031_LOG;

static void
cs4031_log(const char *fmt, ...)
{
    va_list ap;

    if (cs4031_do_log) {
        va_start(ap, fmt);
        pclog_ex(fmt, ap);
        va_end(ap);
    }
}
#else
#    define cs4031_log(fmt, ...)
#endif

static void
cs4031_shadow_recalc(cs4031_t *dev)
{
    mem_set_mem_state_both(0xa0000, 0x10000, (dev->regs[0x18] & 0x01) ? (MEM_READ_INTERNAL | MEM_WRITE_INTERNAL) : (MEM_READ_EXTANY | MEM_WRITE_EXTANY));
    mem_set_mem_state_both(0xb0000, 0x10000, (dev->regs[0x18] & 0x02) ? (MEM_READ_INTERNAL | MEM_WRITE_INTERNAL) : (MEM_READ_EXTANY | MEM_WRITE_EXTANY));

    for (uint32_t i = 0; i < 7; i++) {
        if (i < 4)
            mem_set_mem_state_both(0xc0000 + (i << 14), 0x4000, ((dev->regs[0x19] & (1 << i)) ? MEM_READ_INTERNAL : MEM_READ_EXTANY) | ((dev->regs[0x1a] & (1 << i)) ? MEM_WRITE_INTERNAL : MEM_WRITE_EXTANY));
        else
            mem_set_mem_state_both(0xd0000 + ((i - 4) << 16), 0x10000, ((dev->regs[0x19] & (1 << i)) ? MEM_READ_INTERNAL : MEM_READ_EXTANY) | ((dev->regs[0x1a] & (1 << i)) ? MEM_WRITE_INTERNAL : MEM_WRITE_EXTANY));
    }
    shadowbios       = !!(dev->regs[0x19] & 0x40);
    shadowbios_write = !!(dev->regs[0x1a] & 0x40);
}

static void
cs4031_write(uint16_t addr, uint8_t val, void *priv)
{
    cs4031_t *dev = (cs4031_t *) priv;

    switch (addr) {
        case 0x22:
            dev->index = val;
            break;
        case 0x23:
            cs4031_log("CS4031: dev->regs[%02x] = %02x\n", dev->index, val);
            switch (dev->index) {
                case 0x05:
                    dev->regs[dev->index] = val & 0x3f;
                    break;

                case 0x06:
                    dev->regs[dev->index] = val & 0xbc;
                    break;

                case 0x07:
                    dev->regs[dev->index] = val & 0x0f;
                    break;

                case 0x10:
                    dev->regs[dev->index] = val & 0x3d;
                    break;

                case 0x11:
                    dev->regs[dev->index] = val & 0x8d;
                    break;

                case 0x12:
                case 0x13:
                    dev->regs[dev->index] = val & 0x8d;
                    break;

                case 0x14:
                case 0x15:
                case 0x16:
                case 0x17:
                    dev->regs[dev->index] = val & 0x7f;
                    break;

                case 0x18:
                    dev->regs[dev->index] = val & 0xf3;
                    cs4031_shadow_recalc(dev);
                    break;

                case 0x19:
                case 0x1a:
                    dev->regs[dev->index] = val & 0x7f;
                    cs4031_shadow_recalc(dev);
                    break;

                case 0x1b:
                    dev->regs[dev->index] = val;
                    break;

                case 0x1c:
                    dev->regs[dev->index] = val & 0xb3;
                    port_92_set_features(dev->port_92, val & 0x10, val & 0x20);
                    break;

                default:
                    break;
            }
            break;

            default:
                break;
    }
}

static uint8_t
cs4031_read(uint16_t addr, void *priv)
{
    const cs4031_t *dev = (cs4031_t *) priv;

    return (addr == 0x23) ? dev->regs[dev->index] : 0xff;
}

static void
cs4031_close(void *priv)
{
    cs4031_t *dev = (cs4031_t *) priv;

    free(dev);
}

static void *
cs4031_init(UNUSED(const device_t *info))
{
    cs4031_t *dev = (cs4031_t *) calloc(1, sizeof(cs4031_t));

    dev->port_92 = device_add(&port_92_device);

    dev->regs[0x05] = 0x05;
    dev->regs[0x1b] = 0x60;

    io_sethandler(0x0022, 0x0002, cs4031_read, NULL, NULL, cs4031_write, NULL, NULL, dev);

    return dev;
}

const device_t cs4031_device = {
    .name          = "Chips & Technogies CS4031",
    .internal_name = "cs4031",
    .flags         = 0,
    .local         = 0,
    .init          = cs4031_init,
    .close         = cs4031_close,
    .reset         = NULL,
    .available     = NULL,
    .speed_changed = NULL,
    .force_redraw  = NULL,
    .config        = NULL
};
