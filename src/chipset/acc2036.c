/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          Implementation of the ACC 2036 chipset.
 *
 * Authors: Miran Grca, <mgrca8@gmail.com>
 *
 *          Copyright 2025 Miran Grca.
 */
#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>
#include <86box/86box.h>
#include "cpu.h"
#include <86box/timer.h>
#include <86box/io.h>
#include <86box/device.h>
#include <86box/machine.h>
#include <86box/mem.h>
#include <86box/port_92.h>
#include <86box/plat_fallthrough.h>
#include <86box/plat_unused.h>
#include <86box/fdd.h>
#include <86box/fdc.h>
#include <86box/chipset.h>

typedef struct {
    uint32_t      virt;
    uint32_t      phys;

    mem_mapping_t mapping;
} ram_page_t;

typedef struct {
    uint8_t       reg;
    uint8_t       regs[32];

    ram_page_t    ram_mid_pages[24];
    ram_page_t    ems_pages[4];
} acc2036_t;

static uint8_t
acc2036_mem_read(uint32_t addr, void *priv)
{
    ram_page_t *dev = (ram_page_t *) priv;
    uint8_t     ret = 0xff;

    addr = (addr - dev->virt) + dev->phys;

    if (addr < (mem_size << 10))
        ret = ram[addr];

    return ret;
}

static uint16_t
acc2036_mem_readw(uint32_t addr, void *priv)
{
    ram_page_t *dev = (ram_page_t *) priv;
    uint16_t    ret = 0xffff;

    addr = (addr - dev->virt) + dev->phys;

    if (addr < (mem_size << 10))
        ret = *(uint16_t *) &(ram[addr]);

    return ret;
}

static void
acc2036_mem_write(uint32_t addr, uint8_t val, void *priv)
{
    ram_page_t *dev = (ram_page_t *) priv;

    addr = (addr - dev->virt) + dev->phys;

    if (addr < (mem_size << 10))
        ram[addr] = val;
}

static void
acc2036_mem_writew(uint32_t addr, uint16_t val, void *priv)
{
    ram_page_t *dev = (ram_page_t *) priv;

    addr = (addr - dev->virt) + dev->phys;

    if (addr < (mem_size << 10))
        *(uint16_t *) &(ram[addr]) = val;
}

static void
acc2036_recalc(acc2036_t *dev)
{
    uint32_t ems_bases[4] = { 0x000c0000, 0x000c8000, 0x000d0000, 0x000e0000 };

    int start_i = (ems_bases[dev->regs[0x0c] & 0x03] - 0x000a0000) >> 14;
    int end_i   = start_i + 3;

    for (int i = 0; i < 24; i++) {
        ram_page_t *rp = &dev->ram_mid_pages[i];
        mem_mapping_disable(&rp->mapping);
    }

    for (int i = 0; i < 4; i++) {
        ram_page_t *ep = &dev->ems_pages[i];
        mem_mapping_disable(&ep->mapping);
    }

    for (int i = 0; i < 24; i++) {
        ram_page_t *rp   = &dev->ram_mid_pages[i];

        if ((dev->regs[0x03] & 0x08) && (i >= start_i) && (i <= end_i)) {
            /* EMS */
            ram_page_t *ep = &dev->ems_pages[i - start_i];

            mem_mapping_disable(&rp->mapping);
            mem_mapping_set_addr(&ep->mapping, ep->virt, 0x000040000);
            mem_mapping_set_exec(&ep->mapping, ram + ep->phys);
            mem_set_mem_state_both(ep->virt, 0x00004000, MEM_READ_INTERNAL | MEM_WRITE_INTERNAL);
        } else {
            int     master_write;
            int     master_read;
            int     bit;
            int     ew_flag;
            int     er_flag;
            int     flags;
            uint8_t val;

            mem_mapping_set_addr(&rp->mapping, rp->virt, 0x000040000);
            mem_mapping_set_exec(&rp->mapping, ram + rp->phys);

            if ((i >= 8) && (i <= 15)) {
                /* 0C0000-0DFFFF */
                master_write  = dev->regs[0x02] & 0x08;
                master_read   = dev->regs[0x02] & 0x04;
                bit           = ((i - 8) >> 1);
                val           = dev->regs[0x0d] & (1 << bit);
                if (i >= 12) {
                    ew_flag = (dev->regs[0x07] & 0x80) ? MEM_WRITE_EXTANY : MEM_WRITE_EXTERNAL;
                    er_flag = (dev->regs[0x07] & 0x80) ? MEM_READ_EXTANY : MEM_READ_EXTERNAL;
                } else {
                    ew_flag = (dev->regs[0x07] & 0x40) ? MEM_WRITE_EXTANY : MEM_WRITE_EXTERNAL;
                    er_flag = (dev->regs[0x07] & 0x40) ? MEM_READ_EXTANY : MEM_READ_EXTERNAL;
                }
                flags         = (val && master_write) ? MEM_WRITE_INTERNAL : ew_flag;
                flags        |= (val && master_read) ? MEM_READ_INTERNAL : er_flag;
                mem_set_mem_state_both(rp->virt, 0x00004000, flags);
            } else if (i > 15) {
                /* 0E0000-0FFFFF */
                master_write = dev->regs[0x02] & 0x02;
                master_read  = dev->regs[0x02] & 0x01;
                bit           = ((i - 8) >> 2);
                val           = dev->regs[0x0c] & (1 << bit);
                if (i >= 20) {
                    ew_flag = MEM_WRITE_EXTANY;
                    er_flag = MEM_READ_EXTANY;
                } else {
                    ew_flag = (dev->regs[0x0c] & 0x10) ? MEM_WRITE_EXTANY : MEM_WRITE_EXTERNAL;
                    er_flag = (dev->regs[0x0c] & 0x10) ? MEM_READ_EXTANY : MEM_READ_EXTERNAL;
                }
                flags         = (val && master_write) ? MEM_WRITE_INTERNAL : ew_flag;
                flags        |= (val && master_read) ? MEM_READ_INTERNAL : er_flag;
                mem_set_mem_state_both(rp->virt, 0x00004000, flags);
            }
        }
    }

    if (dev->regs[0x00] & 0x40)
        mem_set_mem_state_both(0x00fe0000, 0x00010000, MEM_READ_EXTANY | MEM_WRITE_EXTANY);
    else
        mem_set_mem_state_both(0x00fe0000, 0x00010000, MEM_READ_INTERNAL | MEM_WRITE_INTERNAL);

    for (int i = 0x01; i <= 0x06; i++) {
        uint32_t base = 0x00fe0000 - (i * 0x00010000);

        if (dev->regs[i] & 0x40)
            mem_set_mem_state_both(base, 0x00008000, MEM_READ_EXTANY | MEM_WRITE_EXTANY);
        else
            mem_set_mem_state_both(base, 0x00008000, MEM_READ_INTERNAL | MEM_WRITE_INTERNAL);

        if (dev->regs[i] & 0x80)
            mem_set_mem_state_both(base + 0x00008000, 0x00008000, MEM_READ_EXTANY | MEM_WRITE_EXTANY);
        else
            mem_set_mem_state_both(base + 0x00008000, 0x00008000, MEM_READ_INTERNAL | MEM_WRITE_INTERNAL);
    }

    mem_remap_top(0);
    if (dev->regs[0x03] & 0x10) {
        if (dev->regs[0x02] & 0x0c)
            mem_remap_top(128);
        else if (dev->regs[0x02] & 0x03)
            mem_remap_top(256);
        else
            mem_remap_top(384);
    }

    flushmmucache_nopc();
}

static uint8_t
acc2036_in(uint16_t port, void *priv) {
    acc2036_t *dev = (acc2036_t *) priv;
    uint8_t    reg = dev->reg - 0x20;
    uint8_t    ret = 0xff;

    if (port & 0x0001)  switch (dev->reg) {
        default:
            break;
        case 0x20 ... 0x2e:
        case 0x31 ... 0x3f:
            ret = dev->regs[reg];
            break;
    } else
        ret = dev->reg;

    return ret;
}

static void
acc2036_out(uint16_t port, uint8_t val, void *priv) {
    acc2036_t *dev = (acc2036_t *) priv;
    uint8_t    reg = dev->reg - 0x20;

    if (port & 0x0001)  switch (dev->reg) {
        default:
            break;
        case 0x20 ... 0x23:
            dev->regs[reg] = val;
            acc2036_recalc(dev);
            break;
        case 0x24 ... 0x2b:
            dev->regs[reg] = val;
            dev->ems_pages[(reg - 0x04) >> 1].phys = ((dev->regs[reg & 0xfe] & 0x1f) << 19) |
                                                     ((dev->regs[reg | 0x01] & 0x1f) << 14);
            acc2036_recalc(dev);
            break;
        case 0x2c: case 0x2d:
            dev->regs[reg] = val;
            acc2036_recalc(dev);
            break;
        case 0x2e:
            dev->regs[reg] = val | 0x10;
            break;
        case 0x31:
            dev->regs[reg] = val;
            mem_a20_alt = (val & 0x01);
            mem_a20_recalc();
            flushmmucache();
            if (val & 0x02) {
                softresetx86(); /* Pulse reset! */
                cpu_set_edx();
                flushmmucache();
            }
            break;
        case 0x32 ... 0x3f:
            dev->regs[reg] = val;
            break;
    } else
        dev->reg = val;
}

static void
acc2036_close(void *priv)
{
    acc2036_t *dev = (acc2036_t *) priv;

    free(dev);
}

static void *
acc2036_init(UNUSED(const device_t *info))
{
    acc2036_t *dev = (acc2036_t *) calloc(1, sizeof(acc2036_t));

    for (int i = 0; i < 24; i++) {
        ram_page_t *rp = &dev->ram_mid_pages[i];

        rp->virt = 0x000a0000 + (i << 14);
        rp->phys = 0x000a0000 + (i << 14);
        mem_mapping_add(&rp->mapping, rp->virt, 0x00004000,
                        acc2036_mem_read, acc2036_mem_readw, NULL,
                        acc2036_mem_write, acc2036_mem_writew, NULL,
                        ram + rp->phys, MEM_MAPPING_INTERNAL, rp);
    }

    for (int i = 0; i < 4; i++) {
        ram_page_t *ep = &dev->ems_pages[i];

        ep->virt = 0x000d0000 + (i << 14);
        ep->phys = 0x00000000 + (i << 14);
        mem_mapping_add(&ep->mapping, ep->virt, 0x00004000,
                        acc2036_mem_read, acc2036_mem_readw, NULL,
                        acc2036_mem_write, acc2036_mem_writew, NULL,
                        ram + ep->phys, MEM_MAPPING_INTERNAL, ep);
        mem_mapping_disable(&ep->mapping);
    }

    mem_mapping_disable(&ram_mid_mapping);

    dev->regs[0x00] = 0x02;
    dev->regs[0x0e] = 0x10;
    dev->regs[0x11] = 0x01;
    dev->regs[0x13] = 0x40;
    dev->regs[0x15] = 0x40;
    dev->regs[0x17] = 0x40;
    dev->regs[0x19] = 0x40;
    dev->regs[0x1b] = 0x40;
    dev->regs[0x1c] = 0x22;
    dev->regs[0x1d] = 0xc4;
    dev->regs[0x1f] = 0x30;
    acc2036_recalc(dev);

    mem_a20_alt = 0x01;
    mem_a20_recalc();
    flushmmucache();

    io_sethandler(0x00f2, 0x0002,
                  acc2036_in, NULL, NULL, acc2036_out, NULL, NULL, dev);

    device_add(&port_92_device);

    return dev;
}

const device_t acc2036_device = {
    .name          = "ACC 2036",
    .internal_name = "acc2036",
    .flags         = 0,
    .local         = 0,
    .init          = acc2036_init,
    .close         = acc2036_close,
    .reset         = NULL,
    .available     = NULL,
    .speed_changed = NULL,
    .force_redraw  = NULL,
    .config        = NULL
};
