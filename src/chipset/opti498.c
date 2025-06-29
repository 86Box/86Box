/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          Implementation of the OPTi 82C498 chipset.
 *
 * Authors: Miran Grca, <mgrca8@gmail.com>
 *
 *          Copyright 2025 Miran Grca.
 */
#include <math.h>
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
#include <86box/plat_fallthrough.h>
#include <86box/plat_unused.h>
#include <86box/port_92.h>
#include <86box/chipset.h>

#ifdef ENABLE_OPTI498_LOG
int opti498_do_log = ENABLE_OPTI498_LOG;

static void
opti498_log(const char *fmt, ...)
{
    va_list ap;

    if (opti498_do_log) {
        va_start(ap, fmt);
        pclog_ex(fmt, ap);
        va_end(ap);
    }
}
#else
#    define opti498_log(fmt, ...)
#endif

typedef struct mem_remapping_t {
    uint32_t phys;
    uint32_t virt;
} mem_remapping_t;

typedef struct opti498_t {
    uint8_t         index;
    /* 0x30 for 496/497, 0x70 for 498. */
    uint8_t         reg_base;
    uint8_t         shadow_high;
    uint8_t         regs[256];
    mem_remapping_t mem_remappings[2];
    mem_mapping_t   mem_mappings[2];
} opti498_t;

static uint8_t
opti498_read_remapped_ram(uint32_t addr, void *priv)
{
    const mem_remapping_t *dev = (mem_remapping_t *) priv;

    return mem_read_ram((addr - dev->virt) + dev->phys, priv);
}

static uint16_t
opti498_read_remapped_ramw(uint32_t addr, void *priv)
{
    const mem_remapping_t *dev = (mem_remapping_t *) priv;

    return mem_read_ramw((addr - dev->virt) + dev->phys, priv);
}

static uint32_t
opti498_read_remapped_raml(uint32_t addr, void *priv)
{
    const mem_remapping_t *dev = (mem_remapping_t *) priv;

    return mem_read_raml((addr - dev->virt) + dev->phys, priv);
}

static void
opti498_write_remapped_ram(uint32_t addr, uint8_t val, void *priv)
{
    const mem_remapping_t *dev = (mem_remapping_t *) priv;

    mem_write_ram((addr - dev->virt) + dev->phys, val, priv);
}

static void
opti498_write_remapped_ramw(uint32_t addr, uint16_t val, void *priv)
{
    const mem_remapping_t *dev = (mem_remapping_t *) priv;

    mem_write_ramw((addr - dev->virt) + dev->phys, val, priv);
}

static void
opti498_write_remapped_raml(uint32_t addr, uint32_t val, void *priv)
{
    const mem_remapping_t *dev = (mem_remapping_t *) priv;

    mem_write_raml((addr - dev->virt) + dev->phys, val, priv);
}

static void
opti498_shadow_recalc(opti498_t *dev)
{
    uint32_t base;
    uint32_t rbase;
    uint8_t  sh_enable;
    uint8_t  sh_mode;
    uint8_t  rom;
    uint8_t  sh_copy;

    shadowbios = shadowbios_write = 0;
    dev->shadow_high              = 0;

    opti498_log("OPTI 498: %02X %02X %02X %02X\n", dev->regs[0x02], dev->regs[0x03], dev->regs[0x04], dev->regs[0x05]);

    if (dev->regs[0x02] & 0x80) {
        if (dev->regs[0x04] & 0x02) {
            mem_set_mem_state_both(0xf0000, 0x10000, MEM_READ_EXTANY | MEM_WRITE_EXTANY);
            opti498_log("OPTI 498: F0000-FFFFF READ_EXTANY, WRITE_EXTANY\n");
        } else {
            shadowbios_write = 1;
            mem_set_mem_state_both(0xf0000, 0x10000, MEM_READ_EXTANY | MEM_WRITE_INTERNAL);
            opti498_log("OPTI 498: F0000-FFFFF READ_EXTANY, WRITE_INTERNAL\n");
        }
    } else {
        shadowbios = 1;
        mem_set_mem_state_both(0xf0000, 0x10000, MEM_READ_INTERNAL | MEM_WRITE_DISABLED);
        opti498_log("OPTI 498: F0000-FFFFF READ_INTERNAL, WRITE_DISABLED\n");
    }

    sh_copy = dev->regs[0x02] & 0x08;
    for (uint8_t i = 0; i < 12; i++) {
        base = 0xc0000 + (i << 14);
        if (i >= 4)
            sh_enable = dev->regs[0x03] & (1 << (i - 4));
        else
            sh_enable = dev->regs[0x04] & (1 << (i + 4));
        sh_mode = dev->regs[0x02] & (1 << (i >> 2));
        rom     = dev->regs[0x02] & (1 << ((i >> 2) + 4));
        opti498_log("OPTI 498: %i/%08X: %i, %i, %i\n", i, base, (i >= 4) ? (1 << (i - 4)) : (1 << (i + 4)), (1 << (i >> 2)), (1 << ((i >> 2) + 4)));

        if (sh_copy) {
            if (base >= 0x000e0000)
                shadowbios_write |= 1;
            if (base >= 0x000d0000)
                dev->shadow_high |= 1;

            if (base >= 0xe0000) {
                mem_set_mem_state_both(base, 0x4000, MEM_READ_EXTANY | MEM_WRITE_INTERNAL);
                opti498_log("OPTI 498: %08X-%08X READ_EXTANY, WRITE_INTERNAL\n", base, base + 0x3fff);
            } else {
                mem_set_mem_state_both(base, 0x4000, MEM_READ_EXTERNAL | MEM_WRITE_INTERNAL);
                opti498_log("OPTI 498: %08X-%08X READ_EXTERNAL, WRITE_INTERNAL\n", base, base + 0x3fff);
            }
        } else if (sh_enable && rom) {
            if (base >= 0x000e0000)
                shadowbios |= 1;
            if (base >= 0x000d0000)
                dev->shadow_high |= 1;

            if (sh_mode) {
                mem_set_mem_state_both(base, 0x4000, MEM_READ_INTERNAL | MEM_WRITE_DISABLED);
                opti498_log("OPTI 498: %08X-%08X READ_INTERNAL, WRITE_DISABLED\n", base, base + 0x3fff);
            } else {
                if (base >= 0x000e0000)
                    shadowbios_write |= 1;

                mem_set_mem_state_both(base, 0x4000, MEM_READ_INTERNAL | MEM_WRITE_INTERNAL);
                opti498_log("OPTI 498: %08X-%08X READ_INTERNAL, WRITE_INTERNAL\n", base, base + 0x3fff);
            }
        } else {
            if (base >= 0xe0000) {
                mem_set_mem_state_both(base, 0x4000, MEM_READ_EXTANY | MEM_WRITE_DISABLED);
                opti498_log("OPTI 498: %08X-%08X READ_EXTANY, WRITE_DISABLED\n", base, base + 0x3fff);
            } else {
                mem_set_mem_state_both(base, 0x4000, MEM_READ_EXTERNAL | MEM_WRITE_DISABLED);
                opti498_log("OPTI 498: %08X-%08X READ_EXTERNAL, WRITE_DISABLED\n", base, base + 0x3fff);
            }
        }
    }

    rbase = ((uint32_t) (dev->regs[0x05] & 0x3f)) << 20;

    if (rbase > 0) {
        dev->mem_remappings[0].virt = rbase;
        mem_mapping_set_addr(&dev->mem_mappings[0], rbase, 0x00020000);

        if (!dev->shadow_high) {
            rbase += 0x00020000;
            dev->mem_remappings[1].virt = rbase;
            mem_mapping_set_addr(&dev->mem_mappings[1], rbase, 0x00020000);
        } else
            mem_mapping_disable(&dev->mem_mappings[1]);
    } else {
        mem_mapping_disable(&dev->mem_mappings[0]);
        mem_mapping_disable(&dev->mem_mappings[1]);
    }

    flushmmucache_nopc();
}

static void
opti498_write(uint16_t addr, uint8_t val, void *priv)
{
    opti498_t *dev = (opti498_t *) priv;
    uint8_t    reg = dev->index - dev->reg_base;

    switch (addr) {
        default:
            break;

        case 0x22:
            dev->index = val;
            break;

        case 0x24:
            opti498_log("OPTi 498: dev->regs[%02x] = %02x\n", dev->index, val);

            if ((reg >= 0x00) && (reg <= 0x0b))  switch (reg) {
                default:
                    break;

                case 0x00:
                    dev->regs[reg] = (dev->regs[reg] & 0xc0) | (val & 0x3f);
                    break;

                case 0x01:
                case 0x07 ... 0x0b:
                    dev->regs[reg] = val;
                    break;

                case 0x02:
                case 0x03:
                case 0x04:
                case 0x05:
                    dev->regs[reg] = val;
                    opti498_shadow_recalc(dev);
                    break;

                case 0x06: {
                    double bus_clk;
                    dev->regs[reg] = val;
                    switch (val & 0x03) {
                        default:
                        case 0x00:
                             bus_clk = cpu_busspeed / 8.0;
                             break;
                        case 0x01:
                             bus_clk = cpu_busspeed / 6.0;
                             break;
                        case 0x02:
                             bus_clk = cpu_busspeed / 5.0;
                             break;
                        case 0x03:
                             bus_clk = cpu_busspeed / 4.0;
                             break;
                    }
                    cpu_set_isa_speed((int) round(bus_clk));
                    reset_on_hlt = !!(val & 0x40);
                    break;
                }
            }

            dev->index = 0xff;
            break;
    }
}

static uint8_t
opti498_read(uint16_t addr, void *priv)
{
    opti498_t *dev = (opti498_t *) priv;
    uint8_t    reg = dev->index - dev->reg_base;
    uint8_t    ret = 0xff;

    if (addr == 0x24) {
        if ((reg >= 0x00) && (reg <= 0x0b))
            ret = dev->regs[reg];

        dev->index = 0xff;
    }

    return ret;
}

static void
opti498_close(void *priv)
{
    opti498_t *dev = (opti498_t *) priv;

    free(dev);
}

static void *
opti498_init(UNUSED(const device_t *info))
{
    opti498_t *dev = (opti498_t *) calloc(1, sizeof(opti498_t));

    dev->reg_base = info->local & 0xff;

    io_sethandler(0x0022, 0x0001, opti498_read, NULL, NULL, opti498_write, NULL, NULL, dev);
    io_sethandler(0x0024, 0x0001, opti498_read, NULL, NULL, opti498_write, NULL, NULL, dev);

    dev->regs[0x00] = 0x1f;
    dev->regs[0x01] = 0x8f;
    dev->regs[0x02] = 0xf0;
    dev->regs[0x07] = 0x70;
    dev->regs[0x09] = 0x70;

    dev->mem_remappings[0].phys = 0x000a0000;
    dev->mem_remappings[1].phys = 0x000d0000;

    mem_mapping_add(&dev->mem_mappings[0], 0, 0x00020000,
                    opti498_read_remapped_ram, opti498_read_remapped_ramw, opti498_read_remapped_raml,
                    opti498_write_remapped_ram, opti498_write_remapped_ramw, opti498_write_remapped_raml,
                    &ram[dev->mem_remappings[0].phys], MEM_MAPPING_INTERNAL, &dev->mem_remappings[0]);
    mem_mapping_disable(&dev->mem_mappings[0]);

    mem_mapping_add(&dev->mem_mappings[1], 0, 0x00020000,
                    opti498_read_remapped_ram, opti498_read_remapped_ramw, opti498_read_remapped_raml,
                    opti498_write_remapped_ram, opti498_write_remapped_ramw, opti498_write_remapped_raml,
                    &ram[dev->mem_remappings[1].phys], MEM_MAPPING_INTERNAL, &dev->mem_remappings[1]);
    mem_mapping_disable(&dev->mem_mappings[1]);

    opti498_shadow_recalc(dev);

    cpu_set_isa_speed((int) round(cpu_busspeed / 8.0));

    device_add(&port_92_device);

    return dev;
}

const device_t opti498_device = {
    .name          = "OPTi 82C498",
    .internal_name = "opti498",
    .flags         = 0,
    .local         = 0x70,
    .init          = opti498_init,
    .close         = opti498_close,
    .reset         = NULL,
    .available     = NULL,
    .speed_changed = NULL,
    .force_redraw  = NULL,
    .config        = NULL
};
