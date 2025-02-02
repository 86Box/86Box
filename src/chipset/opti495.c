/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          Implementation of the OPTi 82C493/82C495 chipset.
 *
 *
 *
 * Authors: Tiseno100,
 *          Miran Grca, <mgrca8@gmail.com>
 *
 *          Copyright 2008-2020 Tiseno100.
 *          Copyright 2016-2020 Miran Grca.
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
#include <86box/port_92.h>
#include <86box/chipset.h>

typedef struct opti495_t {
    uint8_t type;
    uint8_t max;
    uint8_t idx;
    uint8_t regs[256];
    uint8_t scratch[2];
} opti495_t;

#ifdef ENABLE_OPTI495_LOG
int opti495_do_log = ENABLE_OPTI495_LOG;

static void
opti495_log(const char *fmt, ...)
{
    va_list ap;

    if (opti495_do_log) {
        va_start(ap, fmt);
        pclog_ex(fmt, ap);
        va_end(ap);
    }
}
#else
#    define opti495_log(fmt, ...)
#endif

enum {
    OPTI493 = 0,
    OPTI495,
    OPTI495SLC,
    OPTI495SX,
    OPTI495XLC,
    TMAX
};

/* OPTi 82C493: According to The Last Byte, bit 1 of register 22h, while unused, must still be writable. */
static uint8_t masks[TMAX][0x1c] = { { 0x3f, 0xff, 0xff, 0xff, 0xf7, 0xfb, 0x7f, 0x9f, 0xe3, 0xff, 0xe3, 0xff },
                                     { 0x3a, 0x7f, 0xff, 0xff, 0xf0, 0xfb, 0x7f, 0xbf, 0xe3, 0xff, 0x00, 0x00 },
                                     { 0x3a, 0x7f, 0xfc, 0xff, 0xf0, 0xfb, 0xff, 0xbf, 0xe3, 0xff, 0x00, 0x00 },
                                     { 0x3a, 0xff, 0xfd, 0xff, 0xf0, 0xfb, 0x7f, 0xbf, 0xe3, 0xff, 0x00, 0x00 },
                                     { 0x3a, 0xff, 0xfc, 0xff, 0xf0, 0xfb, 0xff, 0xbf, 0xe3, 0xff, 0x00, 0x00 } };

static void
opti495_recalc(opti495_t *dev)
{
    uint32_t base;
    uint32_t shflags = 0;

    shadowbios       = 0;
    shadowbios_write = 0;

    if (dev->regs[0x22] & 0x80) {
        shadowbios       = 1;
        shadowbios_write = 0;
        shflags          = MEM_READ_EXTANY | MEM_WRITE_INTERNAL;
    } else {
        shadowbios       = 0;
        shadowbios_write = 1;
        shflags          = MEM_READ_INTERNAL | MEM_WRITE_DISABLED;
    }

    mem_set_mem_state_both(0xf0000, 0x10000, shflags);

    for (uint8_t i = 0; i < 8; i++) {
        base = 0xd0000 + (i << 14);

        if ((dev->regs[0x22] & ((base >= 0xe0000) ? 0x20 : 0x40)) && (dev->regs[0x23] & (1 << i))) {
            shflags = MEM_READ_INTERNAL;
            shflags |= (dev->regs[0x22] & ((base >= 0xe0000) ? 0x08 : 0x10)) ? MEM_WRITE_DISABLED : MEM_WRITE_INTERNAL;
        } else {
            if (dev->regs[0x26] & 0x40) {
                shflags = MEM_READ_EXTANY;
                shflags |= (dev->regs[0x22] & ((base >= 0xe0000) ? 0x08 : 0x10)) ? MEM_WRITE_DISABLED : MEM_WRITE_INTERNAL;
            } else
                shflags = MEM_READ_EXTANY | MEM_WRITE_EXTANY;
        }

        mem_set_mem_state_both(base, 0x4000, shflags);
    }

    for (uint8_t i = 0; i < 4; i++) {
        base = 0xc0000 + (i << 14);

        if ((dev->regs[0x26] & 0x10) && (dev->regs[0x26] & (1 << i))) {
            shflags = MEM_READ_INTERNAL;
            shflags |= (dev->regs[0x26] & 0x20) ? MEM_WRITE_DISABLED : MEM_WRITE_INTERNAL;
        } else {
            if (dev->regs[0x26] & 0x40) {
                shflags = MEM_READ_EXTANY;
                shflags |= (dev->regs[0x26] & 0x20) ? MEM_WRITE_DISABLED : MEM_WRITE_INTERNAL;
            } else
                shflags = MEM_READ_EXTANY | MEM_WRITE_EXTANY;
        }

        mem_set_mem_state_both(base, 0x4000, shflags);
    }

    flushmmucache();
}

static void
opti495_write(uint16_t addr, uint8_t val, void *priv)
{
    opti495_t *dev = (opti495_t *) priv;

    switch (addr) {
        default:
            break;

        case 0x22:
            opti495_log("[%04X:%08X] [W] dev->idx = %02X\n", CS, cpu_state.pc, val);
            dev->idx = val;
            break;
        case 0x24:
            if ((dev->idx >= 0x20) && (dev->idx <= dev->max)) {
                opti495_log("[%04X:%08X] [W] dev->regs[%04X] = %02X\n", CS, cpu_state.pc, dev->idx, val);

                dev->regs[dev->idx] = val & masks[dev->type][dev->idx - 0x20];
                if ((dev->type == OPTI493) && (dev->idx == 0x20))
                    val |= 0x40;

                switch (dev->idx) {
                    default:
                        break;

                    case 0x21:
                        cpu_cache_ext_enabled = !!(dev->regs[0x21] & 0x10);
                        cpu_update_waitstates();
                        break;

                    case 0x22:
                    case 0x23:
                    case 0x26:
                        opti495_recalc(dev);
                        break;
                }
            }

            dev->idx = 0xff;
            break;

        case 0xe1:
        case 0xe2:
            dev->scratch[~addr & 0x01] = val;
            break;
    }
}

static uint8_t
opti495_read(uint16_t addr, void *priv)
{
    uint8_t    ret = 0xff;
    opti495_t *dev = (opti495_t *) priv;

    switch (addr) {
        case 0x22:
            opti495_log("[%04X:%08X] [R] dev->idx = %02X\n", CS, cpu_state.pc, ret);
            break;
        case 0x24:
            if ((dev->idx >= 0x20) && (dev->idx <= dev->max)) {
                ret = dev->regs[dev->idx];
                opti495_log("[%04X:%08X] [R] dev->regs[%04X] = %02X\n", CS, cpu_state.pc, dev->idx, ret);
            }

            dev->idx = 0xff;
            break;
        case 0xe1:
        case 0xe2:
            ret = dev->scratch[~addr & 0x01];
            break;
        default:
            break;
    }

    return ret;
}

static void
opti495_close(void *priv)
{
    opti495_t *dev = (opti495_t *) priv;

    free(dev);
}

static void *
opti495_init(const device_t *info)
{
    opti495_t *dev = (opti495_t *) calloc(1, sizeof(opti495_t));

    device_add(&port_92_device);

    io_sethandler(0x0022, 0x0001, opti495_read, NULL, NULL, opti495_write, NULL, NULL, dev);
    io_sethandler(0x0024, 0x0001, opti495_read, NULL, NULL, opti495_write, NULL, NULL, dev);

    dev->scratch[0] = dev->scratch[1] = 0xff;

    dev->type = info->local;

    if (info->local >= OPTI495) {
        /* 85C495 */
        dev->max = 0x29;
        dev->regs[0x20] = 0x02;
        dev->regs[0x21] = 0x20;
        dev->regs[0x22] = 0xe4;
        dev->regs[0x25] = 0xf0;
        dev->regs[0x26] = 0x80;
        dev->regs[0x27] = 0xb1;
        dev->regs[0x28] = 0x80;
        dev->regs[0x29] = 0x10;
    } else {
        /* 85C493 */
        dev->max = 0x2b;
        dev->regs[0x20] = 0x40;
        dev->regs[0x22] = 0x84;
        dev->regs[0x24] = 0x87;
        dev->regs[0x25] = 0xf1; /* Note: 0xf0 is also valid default. */
        dev->regs[0x27] = 0x91;
        dev->regs[0x28] = 0x80;
        dev->regs[0x29] = 0x10;
        dev->regs[0x2a] = 0x80;
        dev->regs[0x2b] = 0x10;
    }

    opti495_recalc(dev);

    io_sethandler(0x00e1, 0x0002, opti495_read, NULL, NULL, opti495_write, NULL, NULL, dev);

    return dev;
}

const device_t opti493_device = {
    .name          = "OPTi 82C493",
    .internal_name = "opti493",
    .flags         = 0,
    .local         = OPTI493,
    .init          = opti495_init,
    .close         = opti495_close,
    .reset         = NULL,
    .available     = NULL,
    .speed_changed = NULL,
    .force_redraw  = NULL,
    .config        = NULL
};

const device_t opti495_device = {
    .name          = "OPTi 82C495",
    .internal_name = "opti495",
    .flags         = 0,
    .local         = OPTI495XLC,
    .init          = opti495_init,
    .close         = opti495_close,
    .reset         = NULL,
    .available     = NULL,
    .speed_changed = NULL,
    .force_redraw  = NULL,
    .config        = NULL
};
