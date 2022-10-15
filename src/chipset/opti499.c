/*
 * 86Box	A hypervisor and IBM PC system emulator that specializes in
 *		running old operating systems and software designed for IBM
 *		PC systems and compatibles from 1981 through fairly recent
 *		system designs based on the PCI bus.
 *
 *		This file is part of the 86Box distribution.
 *
 *		Implementation of the OPTi 82C493/82C499 chipset.
 *
 *
 *
 * Authors:	Tiseno100,
 *		Miran Grca, <mgrca8@gmail.com>
 *
 *		Copyright 2008-2020 Tiseno100.
 *		Copyright 2016-2020 Miran Grca.
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

typedef struct
{
    uint8_t idx,
        regs[256], scratch[2];
} opti499_t;

#ifdef ENABLE_OPTI499_LOG
int opti499_do_log = ENABLE_OPTI499_LOG;

static void
opti499_log(const char *fmt, ...)
{
    va_list ap;

    if (opti499_do_log) {
        va_start(ap, fmt);
        pclog_ex(fmt, ap);
        va_end(ap);
    }
}
#else
#    define opti499_log(fmt, ...)
#endif

static void
opti499_recalc(opti499_t *dev)
{
    uint32_t base;
    uint32_t i, shflags = 0;

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

    for (i = 0; i < 8; i++) {
        base = 0xd0000 + (i << 14);

        if ((dev->regs[0x22] & ((base >= 0xe0000) ? 0x20 : 0x40)) && (dev->regs[0x23] & (1 << i))) {
            shflags = MEM_READ_INTERNAL;
            shflags |= (dev->regs[0x22] & ((base >= 0xe0000) ? 0x08 : 0x10)) ? MEM_WRITE_DISABLED : MEM_WRITE_INTERNAL;
        } else {
            if (dev->regs[0x2d] && (1 << ((i >> 1) + 2)))
                shflags = MEM_READ_EXTANY | MEM_WRITE_EXTANY;
            else
                shflags = MEM_READ_EXTERNAL | MEM_WRITE_EXTERNAL;
        }

        mem_set_mem_state_both(base, 0x4000, shflags);
    }

    for (i = 0; i < 4; i++) {
        base = 0xc0000 + (i << 14);

        if ((dev->regs[0x26] & 0x10) && (dev->regs[0x26] & (1 << i))) {
            shflags = MEM_READ_INTERNAL;
            shflags |= (dev->regs[0x26] & 0x20) ? MEM_WRITE_DISABLED : MEM_WRITE_INTERNAL;
        } else {
            if (dev->regs[0x26] & 0x40) {
                if (dev->regs[0x2d] && (1 << (i >> 1)))
                    shflags = MEM_READ_EXTANY;
                else
                    shflags = MEM_READ_EXTERNAL;
                shflags |= (dev->regs[0x26] & 0x20) ? MEM_WRITE_DISABLED : MEM_WRITE_INTERNAL;
            } else {
                if (dev->regs[0x2d] && (1 << (i >> 1)))
                    shflags = MEM_READ_EXTANY | MEM_WRITE_EXTANY;
                else
                    shflags = MEM_READ_EXTERNAL | MEM_WRITE_EXTERNAL;
            }
        }

        mem_set_mem_state_both(base, 0x4000, shflags);
    }

    flushmmucache_nopc();
}

static void
opti499_write(uint16_t addr, uint8_t val, void *priv)
{
    opti499_t *dev = (opti499_t *) priv;

    switch (addr) {
        case 0x22:
            opti499_log("[%04X:%08X] [W] dev->idx = %02X\n", CS, cpu_state.pc, val);
            dev->idx = val;
            break;
        case 0x24:
            if ((dev->idx >= 0x20) && (dev->idx <= 0x2d)) {
                if (dev->idx == 0x20)
                    dev->regs[dev->idx] = (dev->regs[dev->idx] & 0xc0) | (val & 0x3f);
                else
                    dev->regs[dev->idx] = val;
                opti499_log("[%04X:%08X] [W] dev->regs[%04X] = %02X\n", CS, cpu_state.pc, dev->idx, val);

                switch (dev->idx) {
                    case 0x20:
                        reset_on_hlt = !(val & 0x02);
                        break;

                    case 0x21:
                        cpu_cache_ext_enabled = !!(dev->regs[0x21] & 0x10);
                        cpu_update_waitstates();
                        break;

                    case 0x22:
                    case 0x23:
                    case 0x26:
                    case 0x2d:
                        opti499_recalc(dev);
                        break;
                }
            }
            break;

        case 0xe1:
        case 0xe2:
            dev->scratch[~addr & 0x01] = val;
            break;
    }
}

static uint8_t
opti499_read(uint16_t addr, void *priv)
{
    uint8_t    ret = 0xff;
    opti499_t *dev = (opti499_t *) priv;

    switch (addr) {
        case 0x22:
            opti499_log("[%04X:%08X] [R] dev->idx = %02X\n", CS, cpu_state.pc, ret);
            break;
        case 0x24:
            if ((dev->idx >= 0x20) && (dev->idx <= 0x2d)) {
                if (dev->idx == 0x2d)
                    ret = dev->regs[dev->idx] & 0xbf;
                else
                    ret = dev->regs[dev->idx];
                opti499_log("[%04X:%08X] [R] dev->regs[%04X] = %02X\n", CS, cpu_state.pc, dev->idx, ret);
            }
            break;
        case 0xe1:
        case 0xe2:
            ret = dev->scratch[~addr & 0x01];
            break;
    }

    return ret;
}

static void
opti499_reset(void *priv)
{
    opti499_t *dev = (opti499_t *) priv;

    memset(dev->regs, 0xff, sizeof(dev->regs));
    memset(&(dev->regs[0x20]), 0x00, 14 * sizeof(uint8_t));

    dev->scratch[0] = dev->scratch[1] = 0xff;

    dev->regs[0x22] = 0x84;
    dev->regs[0x24] = 0x87;
    dev->regs[0x25] = 0xf0;
    dev->regs[0x27] = 0xd1;
    dev->regs[0x28] = dev->regs[0x2a] = 0x80;
    dev->regs[0x29] = dev->regs[0x2b] = 0x10;
    dev->regs[0x2d]                   = 0x40;

    reset_on_hlt = 1;

    cpu_cache_ext_enabled = 0;
    cpu_update_waitstates();

    opti499_recalc(dev);

    free(dev);
}

static void
opti499_close(void *priv)
{
    opti499_t *dev = (opti499_t *) priv;

    free(dev);
}

static void *
opti499_init(const device_t *info)
{
    opti499_t *dev = (opti499_t *) malloc(sizeof(opti499_t));
    memset(dev, 0, sizeof(opti499_t));

    device_add(&port_92_device);

    io_sethandler(0x0022, 0x0001, opti499_read, NULL, NULL, opti499_write, NULL, NULL, dev);
    io_sethandler(0x0024, 0x0001, opti499_read, NULL, NULL, opti499_write, NULL, NULL, dev);

    opti499_reset(dev);

    io_sethandler(0x00e1, 0x0002, opti499_read, NULL, NULL, opti499_write, NULL, NULL, dev);

    return dev;
}

const device_t opti499_device = {
    .name          = "OPTi 82C499",
    .internal_name = "opti499",
    .flags         = 0,
    .local         = 1,
    .init          = opti499_init,
    .close         = opti499_close,
    .reset         = opti499_reset,
    { .available = NULL },
    .speed_changed = NULL,
    .force_redraw  = NULL,
    .config        = NULL
};
