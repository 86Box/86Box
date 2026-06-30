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
 * Authors: Tiseno100,
 *          Miran Grca, <mgrca8@gmail.com>
 *
 *          Copyright 2008-2020 Tiseno100.
 *          Copyright 2016-2020 Miran Grca.
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
#include <86box/io.h>
#include <86box/device.h>
#include <86box/machine.h>
#include <86box/mem.h>
#include <86box/plat_fallthrough.h>
#include <86box/port_92.h>
#include <86box/chipset.h>

typedef struct opti495_t {
    uint8_t type;
    uint8_t max;
    uint8_t idx;
    uint8_t ram_mode;
    uint8_t regs[256];
    uint8_t scratch[2];

    mem_mapping_t ram_mapping;
} opti495_t;

static const uint8_t opti493_ram_modes[64] = {
    0x07, 0x17, 0x87, 0x87, 0x27, 0x97, 0x97, 0x97,
    0xc7, 0xc7, 0xc7, 0xc7, 0xc7, 0xc7, 0xc7, 0xc7,
    0xb7, 0xb7, 0xb7, 0xb7, 0xd7, 0xd7, 0xd7, 0xd7,
    0xd7, 0xd7, 0xd7, 0xd7, 0xd7, 0xd7, 0xd7, 0xd7,
    0xd0, 0xd0, 0xd0, 0xd0, 0xd1, 0xd1, 0xd1, 0xd1,
    0xd4, 0xd4, 0xd4, 0xd4, 0xd4, 0xd4, 0xd4, 0xd4,
    0xd3, 0xd3, 0xd3, 0xd3, 0xd5, 0xd5, 0xd5, 0xd5,
    0xd5, 0xd5, 0xd5, 0xd5, 0xd5, 0xd5, 0xd5, 0xd5
};

#define RAM_BANK_256K 0x01
#define RAM_BANK_1M   0x02
#define RAM_BANK_4M   0x03

static const uint32_t opti493_ram_bank_sizes[4] = {
    (0 << 10), (1024 << 10), (4096 << 10), (16384 << 10)
};

static const uint8_t opti493_ram_banks[16] = {
    0x10, 0x11, 0x12, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x20, 0x22, 0x23, 0x32, 0x30, 0x33, 0x00, 0x00
};

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

static uint32_t
opti495_addr_to_phys(const opti495_t *dev, uint32_t addr)
{
    const uint8_t  sel_mode     = dev->regs[0x24] & 0xf7;
    const uint8_t  sel_banks01  = opti493_ram_banks[sel_mode >> 4];
    const uint8_t  sel_banks23  = opti493_ram_banks[(sel_mode & 0x07) | 0x08];
    const uint8_t  ram_banks01  = opti493_ram_banks[dev->ram_mode >> 4];
    const uint8_t  ram_banks23  = opti493_ram_banks[(dev->ram_mode & 0x07) | 0x08];
    uint32_t       sel_start[5] = { 0 };
    uint32_t       ram_start[5] = { 0 };
    uint32_t       ret          = 0xffffffff;

    sel_start[1] = sel_start[0] + opti493_ram_bank_sizes[(sel_banks01 >> 4) & 0x03];
    sel_start[2] = sel_start[1] + opti493_ram_bank_sizes[sel_banks01 & 0x03];
    sel_start[3] = sel_start[2] + opti493_ram_bank_sizes[(sel_banks23 >> 4) & 0x03];
    sel_start[4] = sel_start[3] + opti493_ram_bank_sizes[sel_banks23 & 0x03];

    if (addr > sel_start[4])
        return 0xffffffff;

    ram_start[1] = ram_start[0] + opti493_ram_bank_sizes[(ram_banks01 >> 4) & 0x03];
    ram_start[2] = ram_start[1] + opti493_ram_bank_sizes[ram_banks01 & 0x03];
    ram_start[3] = ram_start[2] + opti493_ram_bank_sizes[(ram_banks23 >> 4) & 0x03];
    ram_start[4] = ram_start[3] + opti493_ram_bank_sizes[ram_banks23 & 0x03];

    for (int i = 0; i < 4; i++) {
        if ((addr >= sel_start[i]) && (addr < sel_start[i + 1])) {
            const uint32_t sel_size = sel_start[i + 1] - sel_start[i];
            const uint32_t ram_size = ram_start[i + 1] - ram_start[i];
            uint32_t       col      = 0x00000000;
            uint32_t       row      = 0x00000000;
            uint32_t       row2;

            switch (sel_size) {
                default:
                case (1024 << 10):
                    col  = (addr >>  2) & 0x000001ff;
                    row  = (addr >> 12) & 0x000000ff;
                    row |= (((addr >> 11) & 0x00000001) << 8);
                    break;
                case (4096 << 10):
                    col  = (addr >>  2) & 0x000003ff;
                    row  = (addr >> 12) & 0x000003ff;
                    break;
                case (16384 << 10):
                    col  = (addr >>  2) & 0x000007ff;
                    row  = (addr >> 12) & 0x000007fe;
                    row |= ((addr >> 23) & 0x00000001);
                    break;
            }

            switch (ram_size) {
                default:
                case (0 << 10):
                    break;
                case (1024 << 10):
                    col &= 0x000001ff;
                    row &= 0x000001ff;
                    row2 = ((row << 1) & 0x000001ff) | ((row >> 8) & 0x00000001);
                    ret = (col << 2) | (row2 << 11);
                    break;
                case (4096 << 10):
                    col &= 0x000003ff;
                    row &= 0x000003ff;
                    ret = (col << 2) | (row << 12);
                    break;
                case (16384 << 10):
                    col &= 0x000007ff;
                    row &= 0x000007ff;
                    row2 = (row >> 1) | ((row & 0x00000001) << 10);
                    ret = (col << 2) | (row2 << 13);
                    break;
            }

            ret = (ret | (addr & 0x00000003)) + ram_start[i];

            if (ret >= (mem_size << 10))
                ret = 0xffffffff;
            break;
        }
    }

    return ret;
}

static uint8_t
opti495_ram_readb(uint32_t addr, void *priv)
{
    uint8_t ret = 0xff;

    addr = opti495_addr_to_phys(priv, addr);

    if (addr != 0xffffffff)
        ret = ram[addr];

    return ret;
}

static uint16_t
opti495_ram_readw(uint32_t addr, void *priv)
{
    uint16_t ret = 0xffff;

    addr = opti495_addr_to_phys(priv, addr);

    if (addr != 0xffffffff)
        ret = *(uint16_t *) &(ram[addr]);

    return ret;
}

static uint32_t
opti495_ram_readl(uint32_t addr, void *priv)
{
    uint32_t ret = 0xffffffff;

    addr = opti495_addr_to_phys(priv, addr);

    if (addr != 0xffffffff)
        ret = *(uint32_t *) &(ram[addr]);

    return ret;
}

static void
opti495_ram_writeb(uint32_t addr, uint8_t val, void *priv)
{
    addr = opti495_addr_to_phys(priv, addr);

    if (addr != 0xffffffff)
        ram[addr] = val;
}

static void
opti495_ram_writew(uint32_t addr, uint16_t val, void *priv)
{
    addr = opti495_addr_to_phys(priv, addr);

    if (addr != 0xffffffff)
        *(uint16_t *) &(ram[addr]) = val;
}

static void
opti495_ram_writel(uint32_t addr, uint32_t val, void *priv)
{
    addr = opti495_addr_to_phys(priv, addr);

    if (addr != 0xffffffff)
        *(uint32_t *) &(ram[addr]) = val;
}

static int
opti495_mode_matches(const opti495_t *dev)
{
    uint8_t sel_mode = dev->regs[0x24] & 0xf7;

    if ((sel_mode & 0x07) == 0x06)
        sel_mode |= 0x01;

    return (sel_mode == dev->ram_mode);
}

static void
opti495_banked_mapping(opti495_t *dev)
{
    mem_mapping_disable(&ram_low_mapping);
    mem_mapping_disable(&ram_mid_mapping);
    mem_mapping_disable(&ram_high_mapping);
}

static void
opti495_default_mapping(opti495_t *dev)
{
    mem_mapping_disable(&dev->ram_mapping);
    mem_mapping_enable(&ram_low_mapping);
    mem_mapping_enable(&ram_mid_mapping);
    mem_mapping_enable(&ram_high_mapping);

    if (mem_size > 1024)
        mem_set_mem_state_both(0x00100000, (mem_size << 10) - 0x00100000,
                               MEM_READ_INTERNAL | MEM_WRITE_INTERNAL);
}

static void
opti495_recalc_banks(opti495_t *dev)
{
    mem_set_mem_state_both(0x00100000, 0x03f00000,
                           MEM_READ_EXTERNAL | MEM_WRITE_EXTERNAL);

    if (opti495_mode_matches(dev))
        opti495_default_mapping(dev);
    else {
        opti495_banked_mapping(dev);
        mem_mapping_set_addr(&dev->ram_mapping, 0x00000000, 0x04000000);
        mem_set_mem_state_both(0x00100000, 0x03f00000, MEM_READ_INTERNAL | MEM_WRITE_INTERNAL);
    }

    flushmmucache();
}

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

    flushmmucache_nopc();
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
        case 0x23:
            if (dev->idx == 0x01) {
                dev->regs[dev->idx] = val;
                /*
                   This is a hack but it is required to get rid of the
                   System Timer Error.
                 */
                if ((machines[machine].init == machine_at_svc486wb_init) &&
                    (CS == 0xf000) && (cpu_state.pc == 0x000091a5)) {
                    outb(0x0043, 0x36);
                    outb(0x0040, 0x00);
                    outb(0x0040, 0x00);
                }
            }
            break;
        case 0x24:
            if ((dev->idx >= 0x20) && (dev->idx <= dev->max)) {
                opti495_log("[%04X:%08X] [W] dev->regs[%04X] = %02X\n", CS, cpu_state.pc, dev->idx, val);

                dev->regs[dev->idx] = val & masks[dev->type][dev->idx - 0x20];
                if ((dev->type == OPTI493) && (dev->idx == 0x20))
                    dev->regs[dev->idx] |= 0x40;

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

                    case 0x24:
                        if (dev->type == OPTI493)
                            opti495_recalc_banks(dev);
                        break;

                    case 0x25: {
                        double bus_clk;
                        switch (val & 0x03) {
                            default:
                            case 0x00:
                                 bus_clk = cpu_busspeed / 6.0;
                                 break;
                            case 0x01:
                                 bus_clk = cpu_busspeed / 4.0;
                                 break;
                            case 0x02:
                                 bus_clk = cpu_busspeed / 3.0;
                                 break;
                            case 0x03:
                                 bus_clk = (cpu_busspeed * 2.0) / 5.0;
                                 break;
                        }
                        cpu_set_isa_speed((int) round(bus_clk));
                        break;
                    }
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
        case 0x23:
            if (dev->idx == 0x01)
                ret = dev->regs[dev->idx];
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

    if (info->local == OPTI493)
        io_sethandler(0x0023, 0x0001, opti495_read, NULL, NULL, opti495_write, NULL, NULL, dev);

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
        dev->ram_mode = opti493_ram_modes[(mem_size >> 10) - 1];

        mem_mapping_add(&dev->ram_mapping,
                   0x00000000,
                   65536 << 10,
                   opti495_ram_readb,
                   opti495_ram_readw,
                   opti495_ram_readl,
                   opti495_ram_writeb,
                   opti495_ram_writew,
                   opti495_ram_writel,
                   ram,
                   MEM_MAPPING_INTERNAL,
                   dev);
        mem_mapping_disable(&dev->ram_mapping);
    }

    opti495_recalc(dev);

    if (dev->type == OPTI493)
        opti495_recalc_banks(dev);

    io_sethandler(0x00e1, 0x0002, opti495_read, NULL, NULL, opti495_write, NULL, NULL, dev);

    cpu_set_isa_speed((int) round(cpu_busspeed / 6.0));

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

const device_t opti495slc_device = {
    .name          = "OPTi 82C495",
    .internal_name = "opti495slc",
    .flags         = 0,
    .local         = OPTI495SLC,
    .init          = opti495_init,
    .close         = opti495_close,
    .reset         = NULL,
    .available     = NULL,
    .speed_changed = NULL,
    .force_redraw  = NULL,
    .config        = NULL
};

const device_t opti495sx_device = {
    .name          = "OPTi 82C495SX",
    .internal_name = "opti495sx",
    .flags         = 0,
    .local         = OPTI495SX,
    .init          = opti495_init,
    .close         = opti495_close,
    .reset         = NULL,
    .available     = NULL,
    .speed_changed = NULL,
    .force_redraw  = NULL,
    .config        = NULL
};
