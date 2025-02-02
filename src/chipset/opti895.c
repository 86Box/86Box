/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          Implementation of the OPTi 82C802G/82C895 chipset.
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
#include <86box/smram.h>
#include <86box/port_92.h>
#include <86box/chipset.h>

typedef struct opti895_t {
    uint8_t idx;
    uint8_t forced_green;
    uint8_t is_pci;
    uint8_t regs[256];
    uint8_t scratch[2];

    smram_t *smram;
} opti895_t;

static uint8_t masks[0x10] = { 0x3f, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xfe,
                               0xe3, 0xff, 0xe3, 0xff, 0x00, 0xff, 0xff, 0xff };

#ifdef ENABLE_OPTI895_LOG
int opti895_do_log = ENABLE_OPTI895_LOG;

static void
opti895_log(const char *fmt, ...)
{
    va_list ap;

    if (opti895_do_log) {
        va_start(ap, fmt);
        pclog_ex(fmt, ap);
        va_end(ap);
    }
}
#else
#    define opti895_log(fmt, ...)
#endif

static void
opti895_recalc(opti895_t *dev)
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

    if (dev->is_pci)
        mem_set_mem_state_cpu_both(0xf0000, 0x10000, shflags);
    else
        mem_set_mem_state_both(0xf0000, 0x10000, shflags);

    for (uint8_t i = 0; i < 8; i++) {
        base = 0xd0000 + (i << 14);

        if (dev->regs[0x23] & (1 << i)) {
            shflags = MEM_READ_INTERNAL;
            shflags |= (dev->regs[0x22] & ((base >= 0xe0000) ? 0x08 : 0x10)) ? MEM_WRITE_DISABLED : MEM_WRITE_INTERNAL;
        } else {
            shflags = (dev->regs[0x2d] & (1 << ((i >> 1) + 2))) ? MEM_READ_EXTANY : MEM_READ_EXTERNAL;
            if (dev->regs[0x26] & 0x40)
                shflags |= (dev->regs[0x22] & ((base >= 0xe0000) ? 0x08 : 0x10)) ? MEM_WRITE_DISABLED : MEM_WRITE_INTERNAL;
            else {
                if (dev->regs[0x26] & 0x80)
                    shflags |= (dev->regs[0x2d] & (1 << ((i >> 1) + 2))) ? MEM_WRITE_EXTANY : MEM_WRITE_EXTERNAL;
                else
                    shflags |= MEM_WRITE_EXTERNAL;
            }
        }

        if (dev->is_pci)
            mem_set_mem_state_cpu_both(base, 0x4000, shflags);
        else
            mem_set_mem_state_both(base, 0x4000, shflags);
    }

    for (uint8_t i = 0; i < 4; i++) {
        base = 0xc0000 + (i << 14);

        if (dev->regs[0x26] & (1 << i)) {
            shflags = MEM_READ_INTERNAL;
            shflags |= (dev->regs[0x26] & 0x20) ? MEM_WRITE_DISABLED : MEM_WRITE_INTERNAL;
        } else {
            shflags = (dev->regs[0x2d] & (1 << (i >> 1))) ? MEM_READ_EXTANY : MEM_READ_EXTERNAL;
            if (dev->regs[0x26] & 0x40)
                shflags |= (dev->regs[0x26] & 0x20) ? MEM_WRITE_DISABLED : MEM_WRITE_INTERNAL;
            else {
                if (dev->regs[0x26] & 0x80)
                    shflags |= (dev->regs[0x2d] & (1 << (i >> 1))) ? MEM_WRITE_EXTANY : MEM_WRITE_EXTERNAL;
                else
                    shflags |= MEM_WRITE_EXTERNAL;
            }
        }

        if (dev->is_pci)
            mem_set_mem_state_cpu_both(base, 0x4000, shflags);
        else
            mem_set_mem_state_both(base, 0x4000, shflags);
    }

    flushmmucache_nopc();
}

static void
opti895_write(uint16_t addr, uint8_t val, void *priv)
{
    opti895_t *dev = (opti895_t *) priv;

    opti895_log("opti895_write(%04X, %08X)\n", addr, val);

    switch (addr) {
        case 0x22:
            dev->idx = val;
            break;
        case 0x23:
            if (dev->idx == 0x01) {
                dev->regs[dev->idx] = val;
                opti895_log("dev->regs[%04x] = %08x\n", dev->idx, val);
            }
            break;
        case 0x24:
            if (((dev->idx >= 0x20) && (dev->idx <= 0x2f) && (dev->idx != 0x2c)) ||
                ((dev->idx >= 0xe0) && (dev->idx <= 0xef))) {
                if (dev->idx > 0x2f)
                    dev->regs[dev->idx] = val;
                else
                    dev->regs[dev->idx] = val & masks[dev->idx - 0x20];
                opti895_log("dev->regs[%04x] = %08x\n", dev->idx, val);

                /* TODO: Registers 0x30-0x3F for OPTi 802GP and 898. */
                switch (dev->idx) {
                    case 0x21:
                        cpu_cache_ext_enabled = !!(dev->regs[0x21] & 0x10);
                        cpu_update_waitstates();
                        break;

                    case 0x22:
                    case 0x23:
                    case 0x26:
                    case 0x2d:
                        opti895_recalc(dev);
                        break;

                    case 0x24:
                        smram_state_change(dev->smram, 0, !!(val & 0x80));
                        break;

                    case 0xe0:
                        if (!(val & 0x01))
                            dev->forced_green = 0;
                        break;

                    case 0xe1:
                        if ((val & 0x08) && (dev->regs[0xe0] & 0x01)) {
                            smi_raise();
                            dev->forced_green = 1;
                            break;
                        }
                        break;

                    default:
                        break;
                }
            }
            break;

        case 0xe1:
        case 0xe2:
            dev->scratch[addr - 0xe1] = val;
            break;

        default:
            break;
    }
}

static uint8_t
opti895_read(uint16_t addr, void *priv)
{
    uint8_t          ret = 0xff;
    const opti895_t *dev = (opti895_t *) priv;

    switch (addr) {
        case 0x23:
            if (dev->idx == 0x01)
                ret = dev->regs[dev->idx];
            break;
        case 0x24:
            /* TODO: Registers 0x30-0x3F for OPTi 802GP and 898. */
            if (((dev->idx >= 0x20) && (dev->idx <= 0x2f) && (dev->idx != 0x2c)) ||
                ((dev->idx >= 0xe0) && (dev->idx <= 0xef))) {
                ret = dev->regs[dev->idx];
                if (dev->idx == 0xe0)
                    ret = (ret & 0xf6) | (in_smm ? 0x00 : 0x08) | !!dev->forced_green;
            }
            break;

        case 0xe1:
        case 0xe2:
            ret = dev->scratch[addr - 0xe1];
            break;

        default:
            break;
    }

    opti895_log("opti895_read(%04X) = %02X\n", addr, ret);

    return ret;
}

static void
opti895_close(void *priv)
{
    opti895_t *dev = (opti895_t *) priv;

    smram_del(dev->smram);

    free(dev);
}

static void *
opti895_init(const device_t *info)
{
    opti895_t *dev = (opti895_t *) calloc(1, sizeof(opti895_t));

    device_add(&port_92_device);

    io_sethandler(0x0022, 0x0003, opti895_read, NULL, NULL, opti895_write, NULL, NULL, dev);

    dev->is_pci = info->local;

    dev->scratch[0] = dev->scratch[1] = 0xff;

    dev->regs[0x01] = 0xc0;

    dev->regs[0x22] = 0xc4;
    dev->regs[0x25] = 0x7c;
    dev->regs[0x26] = 0x10;
    dev->regs[0x27] = 0xde;
    dev->regs[0x28] = 0xf8;
    dev->regs[0x29] = 0x10;
    dev->regs[0x2a] = 0xe0;
    dev->regs[0x2b] = 0x10;
    dev->regs[0x2d] = 0xc0;

    dev->regs[0xe8] = 0x08;
    dev->regs[0xe9] = 0x08;
    dev->regs[0xeb] = 0xff;
    dev->regs[0xef] = 0x40;

    opti895_recalc(dev);

    io_sethandler(0x00e1, 0x0002, opti895_read, NULL, NULL, opti895_write, NULL, NULL, dev);

    dev->smram = smram_add();

    smram_enable(dev->smram, 0x00030000, 0x000b0000, 0x00010000, 0, 1);

    return dev;
}

const device_t opti802g_device = {
    .name          = "OPTi 82C802G",
    .internal_name = "opti802g",
    .flags         = 0,
    .local         = 0,
    .init          = opti895_init,
    .close         = opti895_close,
    .reset         = NULL,
    .available     = NULL,
    .speed_changed = NULL,
    .force_redraw  = NULL,
    .config        = NULL
};

const device_t opti802g_pci_device = {
    .name          = "OPTi 82C802G (PCI)",
    .internal_name = "opti802g_pci",
    .flags         = 0,
    .local         = 1,
    .init          = opti895_init,
    .close         = opti895_close,
    .reset         = NULL,
    .available     = NULL,
    .speed_changed = NULL,
    .force_redraw  = NULL,
    .config        = NULL
};

const device_t opti895_device = {
    .name          = "OPTi 82C895",
    .internal_name = "opti895",
    .flags         = 0,
    .local         = 0,
    .init          = opti895_init,
    .close         = opti895_close,
    .reset         = NULL,
    .available     = NULL,
    .speed_changed = NULL,
    .force_redraw  = NULL,
    .config        = NULL
};
