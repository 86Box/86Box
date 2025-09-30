/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          Emulation of C&T CS8220 ("PC/AT") chipset.
 *
 * Authors: Miran Grca, <mgrca8@gmail.com>
 *
 *          Copyright 2025 Miran Grca.
 */
#include <stdio.h>
#include <stdint.h>
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
#include <86box/plat_fallthrough.h>
#include <86box/plat_unused.h>
#include <86box/fdd.h>
#include <86box/fdc.h>
#include <86box/chipset.h>

typedef struct {
    uint32_t      virt;
    uint32_t      phys;

    uint32_t      size;

    mem_mapping_t mapping;
} ram_bank_t;

typedef struct {
    uint8_t       regs[3];

    ram_bank_t    ram_banks[3];
} cs8220_t;

static uint8_t
cs8220_mem_read(uint32_t addr, void *priv)
{
    ram_bank_t *dev = (ram_bank_t *) priv;
    uint8_t     ret = 0xff;

    addr = (addr - dev->virt) + dev->phys;

    if (addr < (mem_size << 10))
        ret = ram[addr];

    return ret;
}

static uint16_t
cs8220_mem_readw(uint32_t addr, void *priv)
{
    ram_bank_t *dev = (ram_bank_t *) priv;
    uint16_t    ret = 0xffff;

    addr = (addr - dev->virt) + dev->phys;

    if (addr < (mem_size << 10))
        ret = *(uint16_t *) &(ram[addr]);

    return ret;
}

static void
cs8220_mem_write(uint32_t addr, uint8_t val, void *priv)
{
    ram_bank_t *dev = (ram_bank_t *) priv;

    addr = (addr - dev->virt) + dev->phys;

    if (addr < (mem_size << 10))
        ram[addr] = val;
}

static void
cs8220_mem_writew(uint32_t addr, uint16_t val, void *priv)
{
    ram_bank_t *dev = (ram_bank_t *) priv;

    addr = (addr - dev->virt) + dev->phys;

    if (addr < (mem_size << 10))
        *(uint16_t *) &(ram[addr]) = val;
}

static uint8_t
cs8220_in(uint16_t port, void *priv) {
    cs8220_t *dev = (cs8220_t *) priv;
    uint8_t   ret = 0xff;

    switch (port) {
        case 0x00a4 ... 0x00a5:
            ret = dev->regs[port & 0x0001];
            break;
        case 0x00ab:
            ret = dev->regs[2];
            break;
    }

    return ret;
}

static void
cs8220_out(uint16_t port, uint8_t val, void *priv) {
    cs8220_t *dev = (cs8220_t *) priv;

    switch (port) {
        case 0x00a4:
            dev->regs[0] = val;
            mem_a20_alt = val & 0x40;
            mem_a20_recalc();
            break;
        case 0x00a5:
            dev->regs[1] = val;
            if (val & 0x01) {
                mem_mapping_set_addr(&dev->ram_banks[0].mapping, 0, 0x000040000);
                mem_mapping_disable(&dev->ram_banks[1].mapping);
                mem_mapping_disable(&dev->ram_banks[2].mapping);
            } else {
                mem_mapping_set_addr(&dev->ram_banks[0].mapping, 0, dev->ram_banks[0].size);
                mem_mapping_enable(&dev->ram_banks[1].mapping);
                mem_mapping_enable(&dev->ram_banks[2].mapping);
            }
            break;
        case 0x00ab:
            dev->regs[2] = val;
            break;
    }
}

static void
cs8220_close(void *priv)
{
    cs8220_t *dev = (cs8220_t *) priv;

    free(dev);
}

static void *
cs8220_init(UNUSED(const device_t *info))
{
    cs8220_t *dev = (cs8220_t *) calloc(1, sizeof(cs8220_t));

    mem_mapping_disable(&ram_low_mapping);
    mem_mapping_disable(&ram_mid_mapping);
    mem_mapping_disable(&ram_high_mapping);

    /*
       Dell System 200: 640 kB soldered on-board, any other RAM is expansion.
     */
    if ((machines[machine].init == machine_at_dells200_init))  switch (mem_size) {
        default:
            dev->ram_banks[2].virt = 0x00100000;
            dev->ram_banks[2].phys = 0x000a0000;
            dev->ram_banks[2].size = (mem_size << 10) - 0x000a0000;
            fallthrough;
        case 640:
            dev->ram_banks[0].virt = 0x00000000;
            dev->ram_banks[0].phys = 0x00000000;
            dev->ram_banks[0].size = 0x00080000;
            dev->ram_banks[1].virt = 0x00080000;
            dev->ram_banks[1].phys = 0x00080000;
            dev->ram_banks[1].size = 0x00020000;
            break;
    /*
       We are limited to steps of equal size, so we have to simulate some
       memory expansions to work around the chipset's limits.
     */
    } else  switch (mem_size) {
        case 256:
            dev->ram_banks[0].virt = 0x00000000;
            dev->ram_banks[0].phys = 0x00000000;
            dev->ram_banks[0].size = 0x00020000;
            dev->ram_banks[1].virt = 0x00020000;
            dev->ram_banks[1].phys = 0x00020000;
            dev->ram_banks[1].size = 0x00020000;
            break;
        case 384:
            dev->ram_banks[0].virt = 0x00000000;
            dev->ram_banks[0].phys = 0x00000000;
            dev->ram_banks[0].size = 0x00020000;
            /* Pretend there's a 128k expansion. */
            dev->ram_banks[2].virt = 0x00020000;
            dev->ram_banks[2].phys = 0x00020000;
            dev->ram_banks[2].size = 0x00040000;
            break;
        case 512:
            dev->ram_banks[0].virt = 0x00000000;
            dev->ram_banks[0].phys = 0x00000000;
            dev->ram_banks[0].size = 0x00080000;
            break;
        default:
            dev->ram_banks[2].virt = 0x00100000;
            dev->ram_banks[2].phys = 0x000a0000;
            dev->ram_banks[2].size = (mem_size << 10) - 0x000a0000;
            fallthrough;
        case 640:
            dev->ram_banks[0].virt = 0x00000000;
            dev->ram_banks[0].phys = 0x00000000;
            dev->ram_banks[0].size = 0x00080000;
            dev->ram_banks[1].virt = 0x00080000;
            dev->ram_banks[1].phys = 0x00080000;
            dev->ram_banks[1].size = 0x00020000;
            break;
        case 768:
            dev->ram_banks[0].virt = 0x00000000;
            dev->ram_banks[0].phys = 0x00000000;
            dev->ram_banks[0].size = 0x00080000;
            dev->ram_banks[1].virt = 0x00080000;
            dev->ram_banks[1].phys = 0x00080000;
            dev->ram_banks[1].size = 0x00020000;
            /* Pretend there's a 128k expansion. */
            dev->ram_banks[2].virt = 0x00100000;
            dev->ram_banks[2].phys = 0x00080000;
            dev->ram_banks[2].size = 0x00020000;
            break;
        case 896:
            dev->ram_banks[0].virt = 0x00000000;
            dev->ram_banks[0].phys = 0x00000000;
            dev->ram_banks[0].size = 0x00080000;
            dev->ram_banks[1].virt = 0x00080000;
            dev->ram_banks[1].phys = 0x00080000;
            dev->ram_banks[1].size = 0x00020000;
            /* Pretend there's a 256k expansion. */
            dev->ram_banks[2].virt = 0x00100000;
            dev->ram_banks[2].phys = 0x00080000;
            dev->ram_banks[2].size = 0x00040000;
            break;
        case 1024:
            dev->ram_banks[0].virt = 0x00000000;
            dev->ram_banks[0].phys = 0x00000000;
            dev->ram_banks[0].size = 0x00080000;
            dev->ram_banks[1].virt = 0x00100000;
            dev->ram_banks[1].phys = 0x00080000;
            dev->ram_banks[1].size = 0x00080000;
            break;
    }

    if (dev->ram_banks[0].size > 0x00000000)
        mem_mapping_add(&dev->ram_banks[0].mapping, dev->ram_banks[0].virt, dev->ram_banks[0].size,
                        cs8220_mem_read, cs8220_mem_readw, NULL,
                        cs8220_mem_write, cs8220_mem_writew, NULL,
                        ram + dev->ram_banks[0].phys, MEM_MAPPING_INTERNAL, &(dev->ram_banks[0]));

    if (dev->ram_banks[1].size > 0x00000000)
        mem_mapping_add(&dev->ram_banks[1].mapping, dev->ram_banks[1].virt, dev->ram_banks[1].size,
                        cs8220_mem_read, cs8220_mem_readw, NULL,
                        cs8220_mem_write, cs8220_mem_writew, NULL,
                        ram + dev->ram_banks[1].phys, MEM_MAPPING_INTERNAL, &(dev->ram_banks[1]));

    if (dev->ram_banks[2].size > 0x00000000)
        mem_mapping_add(&dev->ram_banks[2].mapping, dev->ram_banks[2].virt, dev->ram_banks[2].size,
                        cs8220_mem_read, cs8220_mem_readw, NULL,
                        cs8220_mem_write, cs8220_mem_writew, NULL,
                        ram + dev->ram_banks[2].phys, MEM_MAPPING_INTERNAL, &(dev->ram_banks[2]));

    io_sethandler(0x00a4, 0x0002,
                  cs8220_in, NULL, NULL, cs8220_out, NULL, NULL, dev);
    io_sethandler(0x00ab, 0x0001,
                  cs8220_in, NULL, NULL, cs8220_out, NULL, NULL, dev);

    return dev;
}

const device_t cs8220_device = {
    .name          = "C&T CS8220 (PC/AT)",
    .internal_name = "cs8220",
    .flags         = 0,
    .local         = 0,
    .init          = cs8220_init,
    .close         = cs8220_close,
    .reset         = NULL,
    .available     = NULL,
    .speed_changed = NULL,
    .force_redraw  = NULL,
    .config        = NULL
};
