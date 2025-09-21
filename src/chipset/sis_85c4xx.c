/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          Emulation of the SiS 85c401/85c402, 85c460, 85c461, and
 *          85c407/85c471 chipsets.
 *
 * Authors: Miran Grca, <mgrca8@gmail.com>
 *
 *          Copyright 2019-2020 Miran Grca.
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
#include "x86.h"
#include <86box/timer.h>
#include <86box/io.h>
#include <86box/device.h>
#include <86box/plat_unused.h>
#include <86box/port_92.h>
#include <86box/mem.h>
#include <86box/smram.h>
#include <86box/pic.h>
#include <86box/plat_fallthrough.h>
#include <86box/keyboard.h>
#include <86box/machine.h>
#include <86box/chipset.h>

typedef struct ram_bank_t {
    uint32_t      virt_base;
    uint32_t      virt_size;
    uint32_t      phys_base;
    uint32_t      phys_size;

    mem_mapping_t mapping;
} ram_bank_t;

typedef struct sis_85c4xx_t {
    uint8_t       cur_reg;
    uint8_t       tries;
    uint8_t       reg_base;
    uint8_t       reg_last;
    uint8_t       reg_00;
    uint8_t       is_471;
    uint8_t       ram_banks_val;
    uint8_t       force_flush;
    uint8_t       shadowed;
    uint8_t       smram_enabled;
    uint8_t       pad;
    uint8_t       regs[39];
    uint8_t       scratch[2];
    uint32_t      mem_state[8];
    ram_bank_t    ram_banks[8];
    smram_t *     smram;
    port_92_t *   port_92;
} sis_85c4xx_t;

static uint8_t ram_4xx[64] = { 0x00, 0x00, 0x01, 0x00, 0x02, 0x00, 0x03, 0x00,
                               0x04, 0x00, 0x05, 0x00, 0x0b, 0x00, 0x00, 0x00,
                               0x19, 0x00, 0x06, 0x00, 0x14, 0x00, 0x00, 0x00,
                               0x15, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
                               0x1b, 0x00, 0x00, 0x00, 0x16, 0x00, 0x00, 0x00,
                               0x17, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
                               0x1e, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
                               0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 };
static uint8_t ram_471[64] = { 0x00, 0x00, 0x01, 0x01, 0x02, 0x20, 0x09, 0x09,
                               0x04, 0x04, 0x05, 0x05, 0x0b, 0x0b, 0x0b, 0x0b,
                               0x13, 0x21, 0x06, 0x06, 0x0d, 0x0d, 0x0d, 0x0d,
                               0x0e, 0x0e, 0x0e, 0x0e, 0x0e, 0x0e, 0x0e, 0x0e,
                               0x1b, 0x1b, 0x1b, 0x1b, 0x0f, 0x0f, 0x0f, 0x0f,
                               0x17, 0x17, 0x17, 0x17, 0x17, 0x17, 0x17, 0x17,
                               0x3d, 0x3d, 0x3d, 0x3d, 0x3d, 0x3d, 0x3d, 0x3d,
                               0x3d, 0x3d, 0x3d, 0x3d, 0x3d, 0x3d, 0x3d, 0x3d };
static uint8_t ram_asus[64] = { 0x00, 0x00, 0x01, 0x10, 0x10, 0x20, 0x03, 0x11,
                                0x11, 0x05, 0x05, 0x12, 0x12, 0x13, 0x13, 0x13,
                                0x13, 0x21, 0x06, 0x14, 0x14, 0x15, 0x15, 0x15,
                                0x15, 0x1d, 0x1d, 0x1d, 0x1d, 0x1d, 0x1d, 0x1d,
                                0x1d, 0x16, 0x16, 0x16, 0x16, 0x17, 0x17, 0x17,
                                0x17, 0x1e, 0x1e, 0x1e, 0x1e, 0x1e, 0x1e, 0x1e,
                                0x1e, 0x1f, 0x1f, 0x1f, 0x1f, 0x1f, 0x1f, 0x1f,
                                0x1f, 0x1f, 0x1f, 0x1f, 0x1f, 0x1f, 0x1f, 0x1f };
static uint8_t ram_tg486g[64] = { 0x10, 0x10, 0x10, 0x10, 0x10, 0x11, 0x11, 0x11,
                                  0x11, 0x12, 0x12, 0x12, 0x12, 0x13, 0x13, 0x13,
                                  0x13, 0x14, 0x14, 0x14, 0x14, 0x15, 0x15, 0x15,
                                  0x15, 0x1d, 0x1d, 0x1d, 0x1d, 0x1d, 0x1d, 0x1d,
                                  0x1d, 0x16, 0x16, 0x16, 0x16, 0x17, 0x17, 0x17,
                                  0x17, 0x1e, 0x1e, 0x1e, 0x1e, 0x1e, 0x1e, 0x1e,
                                  0x1e, 0x1f, 0x1f, 0x1f, 0x1f, 0x1f, 0x1f, 0x1f,
                                  0x1f, 0x1f, 0x1f, 0x1f, 0x1f, 0x1f, 0x1f, 0x1f };

static uint32_t banks_471[64][4] = { { 0x00100000, 0x00000000, 0x00000000, 0x00000000 }, /* 0x00 */
                                     { 0x00100000, 0x00100000, 0x00000000, 0x00000000 },
                                     { 0x00100000, 0x00100000, 0x00200000, 0x00000000 },
                                     { 0x00100000, 0x00100000, 0x00400000, 0x00000000 },
                                     { 0x00100000, 0x00100000, 0x00200000, 0x00400000 },
                                     { 0x00100000, 0x00100000, 0x00400000, 0x00400000 },
                                     { 0x00100000, 0x00100000, 0x01000000, 0x00000000 },
                                     { 0x00200000, 0x00000000, 0x00000000, 0x00000000 },
                                     { 0x00200000, 0x00200000, 0x00000000, 0x00000000 }, /* 0x08 */
                                     { 0x00200000, 0x00400000, 0x00000000, 0x00000000 },
                                     { 0x00200000, 0x00200000, 0x00400000, 0x00000000 },
                                     { 0x00200000, 0x00200000, 0x00400000, 0x00400000 },
                                     { 0x00200000, 0x01000000, 0x00000000, 0x00000000 },
                                     { 0x00200000, 0x00200000, 0x01000000, 0x00000000 },
                                     { 0x00200000, 0x00200000, 0x00400000, 0x01000000 },
                                     { 0x00200000, 0x00200000, 0x01000000, 0x01000000 },
                                     { 0x00400000, 0x00000000, 0x00000000, 0x00000000 }, /* 0x10 */
                                     { 0x00400000, 0x00400000, 0x00000000, 0x00000000 },
                                     { 0x00400000, 0x00400000, 0x00400000, 0x00000000 },
                                     { 0x00400000, 0x00400000, 0x00400000, 0x00400000 },
                                     { 0x00400000, 0x01000000, 0x00000000, 0x00000000 },
                                     { 0x00400000, 0x00400000, 0x01000000, 0x00000000 },
                                     { 0x00400000, 0x01000000, 0x01000000, 0x00000000 },
                                     { 0x00400000, 0x00400000, 0x01000000, 0x01000000 },
                                     { 0x00800000, 0x00000000, 0x00000000, 0x00000000 }, /* 0x18 */
                                     { 0x00800000, 0x00800000, 0x00000000, 0x00000000 },
                                     { 0x00800000, 0x00800000, 0x00800000, 0x00000000 },
                                     { 0x00800000, 0x00800000, 0x00800000, 0x00800000 },
                                     { 0x01000000, 0x00000000, 0x00000000, 0x00000000 },
                                     { 0x01000000, 0x01000000, 0x00000000, 0x00000000 },
                                     { 0x01000000, 0x01000000, 0x01000000, 0x00000000 },
                                     { 0x01000000, 0x01000000, 0x01000000, 0x01000000 },
                                     { 0x00100000, 0x00400000, 0x00000000, 0x00000000 }, /* 0x20 */
                                     { 0x00100000, 0x01000000, 0x00000000, 0x00000000 },
                                     { 0x00100000, 0x04000000, 0x00000000, 0x00000000 },
                                     { 0x00400000, 0x00800000, 0x00000000, 0x00000000 },
                                     { 0x00400000, 0x04000000, 0x00000000, 0x00000000 },
                                     { 0x00400000, 0x00400000, 0x04000000, 0x00000000 },
                                     { 0x01000000, 0x04000000, 0x00000000, 0x00000000 },
                                     { 0x01000000, 0x01000000, 0x04000000, 0x00000000 },
                                     { 0x04000000, 0x00000000, 0x00000000, 0x00000000 }, /* 0x28 */
                                     { 0x04000000, 0x04000000, 0x00000000, 0x00000000 },
                                     { 0x00400000, 0x02000000, 0x00000000, 0x00000000 },
                                     { 0x00400000, 0x02000000, 0x02000000, 0x00000000 },
                                     { 0x00400000, 0x00400000, 0x02000000, 0x00000000 },
                                     { 0x00400000, 0x00400000, 0x02000000, 0x02000000 },
                                     { 0x01000000, 0x02000000, 0x00000000, 0x00000000 },
                                     { 0x01000000, 0x02000000, 0x02000000, 0x00000000 },
                                     { 0x01000000, 0x01000000, 0x02000000, 0x00000000 }, /* 0x30 */
                                     { 0x01000000, 0x01000000, 0x02000000, 0x02000000 },
                                     { 0x02000000, 0x00000000, 0x00000000, 0x00000000 },
                                     { 0x02000000, 0x02000000, 0x00000000, 0x00000000 },
                                     { 0x02000000, 0x02000000, 0x02000000, 0x00000000 },
                                     { 0x02000000, 0x02000000, 0x02000000, 0x02000000 },
                                     { 0x00400000, 0x00800000, 0x00800000, 0x00000000 },
                                     { 0x00400000, 0x00800000, 0x00800000, 0x00800000 },
                                     { 0x00400000, 0x00400000, 0x00800000, 0x00000000 }, /* 0x38 */
                                     { 0x00400000, 0x00400000, 0x00800000, 0x00800000 },
                                     { 0x00800000, 0x01000000, 0x00000000, 0x00000000 },
                                     { 0x00800000, 0x00800000, 0x00800000, 0x01000000 },
                                     { 0x00800000, 0x00800000, 0x01000000, 0x00000000 },
                                     { 0x00800000, 0x00800000, 0x01000000, 0x01000000 },
                                     { 0x00800000, 0x00800000, 0x02000000, 0x00000000 },
                                     { 0x00800000, 0x00800000, 0x02000000, 0x02000000 } };

static uint32_t
sis_85c471_get_row(ram_bank_t *dev, uint32_t addr)
{
    uint32_t ret = 0x00000000;

    switch (dev->virt_size) {
        case 0x00100000:
        case 0x00200000:
            ret |= (addr >> 13) & 0x00000001;
            ret |= ((addr >> 12) & 0x00000001) << 1;
            ret |= ((addr >> 14) & 0x0000003f) << 2;
            ret |= ((addr >> 11) & 0x00000001) << 8;
            ret |= ((addr >> 20) & 0x00000001) << 9;
            ret |= ((addr >> 22) & 0x00000001) << 10;
            ret |= ((addr >> 24) & 0x00000001) << 11;
            break;
        case 0x00400000:
        case 0x00800000:
            ret |= (addr >> 13) & 0x00000001;
            ret |= ((addr >> 12) & 0x00000001) << 1;
            ret |= ((addr >> 14) & 0x000000ff) << 2;
            ret |= ((addr >> 22) & 0x00000001) << 10;
            ret |= ((addr >> 24) & 0x00000001) << 11;
            break;
        case 0x01000000:
        case 0x02000000:
        case 0x04000000:
            ret |= (addr >> 13) & 0x00000001;
            ret |= ((addr >> 22) & 0x00000001) << 1;
            ret |= ((addr >> 14) & 0x000000ff) << 2;
            ret |= ((addr >> 23) & 0x00000001) << 10;
            ret |= ((addr >> 24) & 0x00000001) << 11;
            break;
    }

    return ret;
}

static uint32_t
sis_85c471_get_col(ram_bank_t *dev, uint32_t addr)
{
    uint32_t ret = 0x00000000;

    switch (dev->virt_size) {
        case 0x00100000:
        case 0x00200000:
            ret |= (addr >> 3) & 0x00000001;
            ret |= ((addr >> 2) & 0x00000001) << 1;
            ret |= ((addr >> 4) & 0x0000003f) << 2;
            ret |= ((addr >> 10) & 0x00000001) << 8;
            ret |= ((addr >> 21) & 0x00000001) << 9;
            ret |= ((addr >> 23) & 0x00000001) << 10;
            ret |= ((addr >> 25) & 0x00000001) << 11;
            break;
        case 0x00400000:
        case 0x00800000:
            ret |= (addr >> 3) & 0x00000001;
            ret |= ((addr >> 2) & 0x00000001) << 1;
            ret |= ((addr >> 4) & 0x000000ff) << 2;
            ret |= ((addr >> 23) & 0x00000001) << 10;
            ret |= ((addr >> 25) & 0x00000001) << 11;
            break;
        case 0x01000000:
        case 0x02000000:
        case 0x04000000:
            ret |= (addr >> 3) & 0x00000001;
            ret |= ((addr >> 2) & 0x00000001) << 1;
            ret |= ((addr >> 4) & 0x000001ff) << 2;
            ret |= ((addr >> 25) & 0x00000001) << 11;
            break;
    }

    return ret;
}

static uint32_t
sis_85c471_set_row(ram_bank_t *dev, uint32_t addr)
{
    uint32_t ret = 0x00000000;

    switch (dev->phys_size) {
        case 0x00100000:
            ret = (addr & 0x1ff) << 11;
            break;
        case 0x00200000:
            ret = (addr & 0x3ff) << 11;
            break;
        case 0x00400000:
            ret = (addr & 0x3ff) << 12;
            break;
        case 0x00800000:
            ret = (addr & 0x7ff) << 12;
            break;
        case 0x01000000:
            ret = (addr & 0x7ff) << 13;
            break;
        case 0x02000000:
            ret = (addr & 0xfff) << 13;
            break;
        case 0x04000000:
            ret = (addr & 0xfff) << 14;
            break;
    }

    return ret;
}

static uint32_t
sis_85c471_set_col(ram_bank_t *dev, uint32_t addr)
{
    uint32_t ret = 0x00000000;

    switch (dev->phys_size) {
        case 0x00100000:
        case 0x00200000:
            ret = (addr & 0x1ff) << 2;
            break;
        case 0x00400000:
        case 0x00800000:
            ret = (addr & 0x3ff) << 2;
            break;
        case 0x01000000:
        case 0x02000000:
            ret = (addr & 0x7ff) << 2;
            break;
        case 0x04000000:
            ret = (addr & 0xfff) << 2;
            break;
    }

    return ret;
}

uint8_t reg09 = 0x00;

static uint8_t
sis_85c471_read_ram(uint32_t addr, void *priv)
{
    ram_bank_t *dev = (ram_bank_t *) priv;
    uint32_t    rel = addr - dev->virt_base;
    uint8_t     ret = 0xff;

    uint32_t row = sis_85c471_set_row(dev, sis_85c471_get_row(dev, rel));
    uint32_t col = sis_85c471_set_col(dev, sis_85c471_get_col(dev, rel));
    uint32_t dw  = rel & 0x00000003;
    rel = row | col | dw;

    addr = (rel + dev->phys_base);

    if ((addr < (mem_size << 10)) && (rel < dev->phys_size))
        ret = ram[addr];

    return ret;
}

static uint16_t
sis_85c471_read_ramw(uint32_t addr, void *priv)
{
    ram_bank_t *dev = (ram_bank_t *) priv;
    uint32_t    rel = addr - dev->virt_base;
    uint16_t    ret = 0xffff;

    uint32_t row = sis_85c471_set_row(dev, sis_85c471_get_row(dev, rel));
    uint32_t col = sis_85c471_set_col(dev, sis_85c471_get_col(dev, rel));
    uint32_t dw  = rel & 0x00000003;
    rel = row | col | dw;

    addr = (rel + dev->phys_base);

    if ((addr < (mem_size << 10)) && (rel < dev->phys_size))
        ret = *(uint16_t *) &(ram[addr]);

    return ret;
}

static uint32_t
sis_85c471_read_raml(uint32_t addr, void *priv)
{
    ram_bank_t *dev = (ram_bank_t *) priv;
    uint32_t    rel = addr - dev->virt_base;
    uint32_t    ret = 0xffffffff;

    uint32_t row = sis_85c471_set_row(dev, sis_85c471_get_row(dev, rel));
    uint32_t col = sis_85c471_set_col(dev, sis_85c471_get_col(dev, rel));
    uint32_t dw  = rel & 0x00000003;
    rel = row | col | dw;

    addr = (rel + dev->phys_base);

    if ((addr < (mem_size << 10)) && (rel < dev->phys_size))
        ret = *(uint32_t *) &(ram[addr]);

    return ret;
}

static void
sis_85c471_write_ram(uint32_t addr, uint8_t val, void *priv)
{
    ram_bank_t *dev = (ram_bank_t *) priv;
    uint32_t    rel = addr - dev->virt_base;

    uint32_t row = sis_85c471_set_row(dev, sis_85c471_get_row(dev, rel));
    uint32_t col = sis_85c471_set_col(dev, sis_85c471_get_col(dev, rel));
    uint32_t dw  = rel & 0x00000003;
    rel = row | col | dw;

    addr = (rel + dev->phys_base);

    if ((addr < (mem_size << 10)) && (rel < dev->phys_size))
        ram[addr] = val;
}

static void
sis_85c471_write_ramw(uint32_t addr, uint16_t val, void *priv)
{
    ram_bank_t *dev = (ram_bank_t *) priv;
    uint32_t    rel = addr - dev->virt_base;

    uint32_t row = sis_85c471_set_row(dev, sis_85c471_get_row(dev, rel));
    uint32_t col = sis_85c471_set_col(dev, sis_85c471_get_col(dev, rel));
    uint32_t dw  = rel & 0x00000003;
    rel = row | col | dw;

    addr = (rel + dev->phys_base);

    if ((addr < (mem_size << 10)) && (rel < dev->phys_size))
        *(uint16_t *) &(ram[addr]) = val;
}

static void
sis_85c471_write_raml(uint32_t addr, uint32_t val, void *priv)
{
    ram_bank_t *dev = (ram_bank_t *) priv;
    uint32_t    rel = addr - dev->virt_base;

    uint32_t row = sis_85c471_set_row(dev, sis_85c471_get_row(dev, rel));
    uint32_t col = sis_85c471_set_col(dev, sis_85c471_get_col(dev, rel));
    uint32_t dw  = rel & 0x00000003;
    rel = row | col | dw;

    addr = (rel + dev->phys_base);

    if ((addr < (mem_size << 10)) && (rel < dev->phys_size))
        *(uint32_t *) &(ram[addr]) = val;
}

static void
sis_85c4xx_recalcremap(sis_85c4xx_t *dev)
{
    if (dev->is_471) {
        if ((mem_size > 8192) || (dev->shadowed & 0x3c) || (dev->regs[0x0b] & 0x02))
            mem_remap_top(0);
        else
            mem_remap_top(-256);
    }
}

static void
sis_85c4xx_recalcmapping(sis_85c4xx_t *dev)
{
    uint32_t base;
    uint32_t n         = 0;
    uint32_t shflags   = 0;
    uint32_t readext;
    uint32_t writeext;
    uint8_t  romcs     = 0xc0;
    uint8_t  cur_romcs;

    dev->shadowed = 0x00;

    shadowbios       = 0;
    shadowbios_write = 0;

    if (dev->regs[0x03] & 0x40)
        romcs |= 0x01;
    if (dev->regs[0x03] & 0x80)
        romcs |= 0x30;
    if (dev->regs[0x08] & 0x04)
        romcs |= 0x02;

    for (uint8_t i = 0; i < 8; i++) {
        base      = 0xc0000 + (i << 15);
        cur_romcs = romcs & (1 << i);
        readext   = cur_romcs ? MEM_READ_EXTANY : MEM_READ_EXTERNAL;
        writeext  = cur_romcs ? MEM_WRITE_EXTANY : MEM_WRITE_EXTERNAL;

        if ((i > 5) || (dev->regs[0x02] & (1 << i))) {
            shadowbios |= (base >= 0xe0000) && (dev->regs[0x02] & 0x80);
            shadowbios_write |= (base >= 0xe0000) && !(dev->regs[0x02] & 0x40);
            shflags = (dev->regs[0x02] & 0x80) ? MEM_READ_INTERNAL : readext;
            shflags |= (dev->regs[0x02] & 0x40) ? writeext : MEM_WRITE_INTERNAL;
            if (dev->regs[0x02] & 0x80)
                dev->shadowed |= (1 << i);
            if (!(dev->regs[0x02] & 0x40))
                dev->shadowed |= (1 << i);
            if (dev->force_flush || (dev->mem_state[i] != shflags)) {
                n++;
                mem_set_mem_state_both(base, 0x8000, shflags);
                if ((base >= 0xf0000) && (dev->mem_state[i] & MEM_READ_INTERNAL) && !(shflags & MEM_READ_INTERNAL))
                    mem_invalidate_range(base, base + 0x7fff);
                dev->mem_state[i] = shflags;
            }
        } else {
            shflags = readext | writeext;
            if (dev->force_flush || (dev->mem_state[i] != shflags)) {
                n++;
                mem_set_mem_state_both(base, 0x8000, shflags);
                dev->mem_state[i] = shflags;
            }
        }
    }

    if (dev->force_flush) {
        flushmmucache();
        dev->force_flush = 0;
    } else if (n > 0)
        flushmmucache_nopc();

    sis_85c4xx_recalcremap(dev);
}

static void
sis_85c4xx_sw_smi_out(UNUSED(uint16_t port), UNUSED(uint8_t val), void *priv)
{
    sis_85c4xx_t *dev = (sis_85c4xx_t *) priv;

    if (dev->regs[0x18] & 0x02) {
        if (dev->regs[0x0b] & 0x10)
            smi_raise();
        else
            picint(1 << ((dev->regs[0x0b] & 0x08) ? 15 : 12));
        soft_reset_mask = 1;
        dev->regs[0x19] |= 0x02;
    }
}

static void
sis_85c4xx_sw_smi_handler(sis_85c4xx_t *dev)
{
    uint16_t addr;

    if (!dev->is_471)
        return;

    addr = dev->regs[0x14] | (dev->regs[0x15] << 8);

    io_handler((dev->regs[0x0b] & 0x80) && (dev->regs[0x18] & 0x02), addr, 0x0001,
               NULL, NULL, NULL, sis_85c4xx_sw_smi_out, NULL, NULL, dev);
}

static void
sis_85c471_banks_split(uint32_t *b_ex, uint32_t *banks)
{
    for (uint8_t i = 0; i < 4; i++) {
        if ((banks[i] == 0x00200000) || (banks[i] == 0x00800000) ||
            (banks[i] == 0x02000000))
            b_ex[i << 1] = b_ex[(i << 1) + 1] = banks[i] >> 1;
        else {
            b_ex[i << 1] = banks[i];
            b_ex[(i << 1) + 1] = 0x00000000;
        }
    }
}

static void
sis_85c471_banks_recalc(sis_85c4xx_t *dev)
{
    reg09 = dev->regs[0x09];

    for (uint8_t i = 0; i < 8; i++)
        mem_mapping_disable(&dev->ram_banks[i].mapping);

    mem_mapping_disable(&ram_low_mapping);
    mem_mapping_disable(&ram_high_mapping);
    mem_set_mem_state_both(1 << 20, 127 << 20, MEM_READ_EXTERNAL | MEM_WRITE_EXTERNAL);

    if ((dev->regs[0x09] & 0x3f) == dev->ram_banks_val) {
        if (mem_size > 1024) {
            mem_mapping_enable(&ram_low_mapping);
            mem_mapping_enable(&ram_high_mapping);
            mem_set_mem_state_both(1 << 20, (mem_size << 10) - (1 << 20),
                                   MEM_READ_INTERNAL | MEM_WRITE_INTERNAL);
        }
    } else {
        uint8_t   banks_val = dev->regs[0x09] & 0x3f;
        uint32_t *banks     = banks_471[banks_val];
        uint32_t  b_ex[8]   = { 0x00000000 };
        uint32_t  size      = 0x00000000;

        sis_85c471_banks_split(b_ex, banks);

        for (uint8_t i = 0; i < 8; i++)  if (b_ex[i] != 0x00000000) {
            dev->ram_banks[i].virt_base = size;
            dev->ram_banks[i].virt_size = b_ex[i];

            mem_mapping_set_addr(&dev->ram_banks[i].mapping, size, b_ex[i]);

            size += b_ex[i];
        }

        mem_set_mem_state_both(1 << 20, 127 << 20, MEM_READ_INTERNAL | MEM_WRITE_INTERNAL);
    }

    flushmmucache_nopc();
}

static void
sis_85c4xx_out(uint16_t port, uint8_t val, void *priv)
{
    sis_85c4xx_t *dev       = (sis_85c4xx_t *) priv;
    uint8_t       rel_reg   = dev->cur_reg - dev->reg_base;
    uint8_t       valxor    = 0x00;
    uint32_t      host_base = 0x000e0000;
    uint32_t      ram_base  = 0x000a0000;

    switch (port) {
        case 0x22:
            dev->cur_reg = val;
            break;
        case 0x23:
            if ((dev->cur_reg >= dev->reg_base) && (dev->cur_reg <= dev->reg_last)) {
                valxor = val ^ dev->regs[rel_reg];

                if (!dev->is_471 && (rel_reg == 0x00))
                    dev->regs[rel_reg] = (dev->regs[rel_reg] & 0x1f) | (val & 0xe0);
                else
                    dev->regs[rel_reg] = val;

                switch (rel_reg) {
                    case 0x00:
                        break;

                    case 0x01:
                        cpu_cache_ext_enabled = ((val & 0x84) == 0x84);
                        cpu_update_waitstates();
                        break;

                    case 0x02:
                    case 0x03:
                    case 0x08:
                        if (valxor)
                            sis_85c4xx_recalcmapping(dev);
                        if ((rel_reg == 0x08) && dev->is_471)
                            flushmmucache();
                        break;

                    case 0x09:
                        if (dev->is_471)
                            sis_85c471_banks_recalc(dev);
                        break;

                    case 0x0b:
                        sis_85c4xx_sw_smi_handler(dev);
                        if (valxor & 0x02)
                            sis_85c4xx_recalcremap(dev);
                        break;

                    case 0x10:
                        if (dev->reg_base == 0x50) {
                            double bus_clk;

                            switch (val & 0xe0) {
                                default:
                                case 0x00:
                                     bus_clk = 7159091.0;
                                     break;
                                case 0x02:
                                     bus_clk = cpu_busspeed / 10.0;
                                     break;
                                case 0x04:
                                     bus_clk = cpu_busspeed / 8.0;
                                     break;
                                case 0x06:
                                     bus_clk = cpu_busspeed / 6.0;
                                     break;
                                case 0x80:
                                     bus_clk = cpu_busspeed / 5.0;
                                     break;
                                case 0xa0:
                                     bus_clk = cpu_busspeed / 4.0;
                                     break;
                                case 0xc0:
                                     bus_clk = cpu_busspeed / 3.0;
                                     break;
                                case 0xe0:
                                     bus_clk = cpu_busspeed / 2.0;
                                     break;
                            }
                            cpu_set_isa_speed((int) round(bus_clk));
                        }
                        break;

                    case 0x13:
                        if (dev->is_471 && (valxor & 0xf0)) {
                            smram_disable(dev->smram);
                            host_base = (val & 0x80) ? 0x00060000 : 0x000e0000;
                            switch ((val >> 5) & 0x03) {
                                case 0x00:
                                    ram_base = 0x000a0000;
                                    break;
                                case 0x01:
                                    ram_base = 0x000b0000;
                                    break;
                                case 0x02:
                                    ram_base = (val & 0x80) ? 0x00000000 : 0x000e0000;
                                    break;
                                default:
                                    ram_base = 0x00000000;
                                    break;
                            }
                            dev->smram_enabled = (ram_base != 0x00000000);
                            if (ram_base != 0x00000000)
                                smram_enable(dev->smram, host_base, ram_base, 0x00010000, (val & 0x10), 1);
                            sis_85c4xx_recalcremap(dev);
                        }
                        break;

                    case 0x14:
                    case 0x15:
                    case 0x18:
                        sis_85c4xx_sw_smi_handler(dev);
                        break;

                    case 0x1c:
                        if (dev->is_471)
                            soft_reset_mask = 0;
                        break;

                    case 0x22:
                        if (dev->is_471 && (valxor & 0x01)) {
                            port_92_remove(dev->port_92);
                            if (val & 0x01)
                                port_92_add(dev->port_92);
                        }
                        break;
                    default:
                        break;
                }
            } else if ((dev->reg_base == 0x60) && (dev->cur_reg == 0x00))
                dev->reg_00 = val;
            dev->cur_reg = 0x00;
            break;

        case 0xe1:
        case 0xe2:
            dev->scratch[port - 0xe1] = val;
            return;
        default:
            break;
    }
}

static uint8_t
sis_85c4xx_in(uint16_t port, void *priv)
{
    sis_85c4xx_t *dev     = (sis_85c4xx_t *) priv;
    uint8_t       rel_reg = dev->cur_reg - dev->reg_base;
    uint8_t       ret     = 0xff;

    switch (port) {
        case 0x23:
            if (dev->is_471 && (dev->cur_reg == 0x1c))
                ret = inb(0x70);
            /* On the SiS 40x, the shadow RAM read and write enable bits are write-only! */
            if ((dev->reg_base == 0x60) && (dev->cur_reg == 0x62))
                ret = dev->regs[rel_reg] & 0x3f;
            else if ((dev->cur_reg >= dev->reg_base) && (dev->cur_reg <= dev->reg_last))
                ret = dev->regs[rel_reg];
            else if ((dev->reg_base == 0x60) && (dev->cur_reg == 0x00))
                ret = dev->reg_00;
            if (dev->reg_base != 0x60)
                dev->cur_reg = 0x00;
            break;

        case 0xe1:
        case 0xe2:
            ret = dev->scratch[port - 0xe1];
            break;

        default:
            break;
    }

    return ret;
}

static void
sis_85c4xx_reset(void *priv)
{
    sis_85c4xx_t  *dev         = (sis_85c4xx_t *) priv;
    int            mem_size_mb = mem_size >> 10;

    memset(dev->regs, 0x00, sizeof(dev->regs));

    if (cpu_s->rspeed < 25000000)
        dev->regs[0x08] = 0x80;

    if (dev->is_471) {
        dev->regs[0x09] = 0x40;

        if (machines[machine].init == machine_at_vli486sv2g_init) {
            if (mem_size_mb == 64)
                dev->regs[0x09] |= 0x1f;
            else
                dev->regs[0x09] |= ram_asus[mem_size_mb];
        } else if (mem_size_mb >= 64) {
            if ((mem_size_mb >= 64) && (mem_size_mb < 68))
                dev->regs[0x09] |= 0x33;
            else if ((mem_size_mb >= 68) && (mem_size_mb < 72))
                dev->regs[0x09] |= 0x2b;
            else if ((mem_size_mb >= 72) && (mem_size_mb < 80))
                dev->regs[0x09] |= 0x2d;
            else if ((mem_size_mb >= 80) && (mem_size_mb < 96))
                dev->regs[0x09] |= 0x2f;
            else if ((mem_size_mb >= 96) && (mem_size_mb < 128))
                dev->regs[0x09] |= 0x34;
            else
                dev->regs[0x09] |= 0x35;
        } else if (machines[machine].init == machine_at_tg486g_init)
            dev->regs[0x09] |= ram_tg486g[mem_size_mb];
        else
            dev->regs[0x09] |= ram_471[mem_size_mb];
        dev->ram_banks_val = dev->regs[0x09] & 0x3f;
        dev->regs[0x09] = 0x00;

        uint32_t *banks   = banks_471[dev->ram_banks_val];
        uint32_t  b_ex[8] = { 0x00000000 };
        uint32_t  size    = 0x00000000;

        sis_85c471_banks_split(b_ex, banks);

        for (uint8_t i = 0; i < 8; i++) {
            dev->ram_banks[i].phys_base = size;
            dev->ram_banks[i].phys_size = b_ex[i];

            size += b_ex[i];
        }

        dev->regs[0x11] = 0x09;
        dev->regs[0x12] = 0xff;
        dev->regs[0x1f] = 0x20; /* Video access enabled. */
        dev->regs[0x23] = 0xf0;
        dev->regs[0x26] = 0x01;

        smram_enable(dev->smram, 0x000e0000, 0x000a0000, 0x00010000, 0, 1);

        port_92_remove(dev->port_92);

        soft_reset_mask = 0;

        sis_85c471_banks_recalc(dev);

        kbc_at_set_fast_reset(1);
        cpu_cpurst_on_sr = 0;
    } else {
        /* Bits 6 and 7 must be clear on the SiS 40x. */
        if (dev->reg_base == 0x60)
            dev->reg_00 = 0x24;

        if (mem_size_mb == 64)
            dev->regs[0x00] = 0x1f;
        else if (mem_size_mb < 64)
            dev->regs[0x00] = ram_4xx[mem_size_mb];

        dev->regs[0x11] = 0x01;
    }

    dev->scratch[0] = dev->scratch[1] = 0xff;

    cpu_cache_ext_enabled = 0;
    cpu_update_waitstates();

    dev->force_flush = 1;
    sis_85c4xx_recalcmapping(dev);

    if (dev->reg_base == 0x50)
        cpu_set_isa_speed((int) round(7159091.0));
}

static void
sis_85c4xx_close(void *priv)
{
    sis_85c4xx_t *dev = (sis_85c4xx_t *) priv;

    if (dev->is_471)
        smram_del(dev->smram);

    free(dev);
}

static void *
sis_85c4xx_init(const device_t *info)
{
    sis_85c4xx_t *dev = (sis_85c4xx_t *) calloc(1, sizeof(sis_85c4xx_t));

    dev->is_471 = (info->local >> 8) & 0xff;

    dev->reg_base = info->local & 0xff;

    if (dev->is_471) {
        dev->reg_last = 0x76;

        dev->smram = smram_add();

        dev->port_92 = device_add(&port_92_device);

        for (uint8_t i = 0; i < 8; i++) {
            mem_mapping_add(&dev->ram_banks[i].mapping, 0x00000000, 0x00000000,
                            sis_85c471_read_ram, sis_85c471_read_ramw, sis_85c471_read_raml,
                            sis_85c471_write_ram, sis_85c471_write_ramw, sis_85c471_write_raml,
                            NULL, MEM_MAPPING_INTERNAL, &(dev->ram_banks[i]));
            mem_mapping_disable(&dev->ram_banks[i].mapping);
        }
    } else
        dev->reg_last = dev->reg_base + 0x11;

    io_sethandler(0x0022, 0x0002,
                  sis_85c4xx_in, NULL, NULL, sis_85c4xx_out, NULL, NULL, dev);

    io_sethandler(0x00e1, 0x0002,
                  sis_85c4xx_in, NULL, NULL, sis_85c4xx_out, NULL, NULL, dev);

    sis_85c4xx_reset(dev);

    return dev;
}

const device_t sis_85c401_device = {
    .name          = "SiS 85c401/85c402",
    .internal_name = "sis_85c401",
    .flags         = 0,
    .local         = 0x060,
    .init          = sis_85c4xx_init,
    .close         = sis_85c4xx_close,
    .reset         = sis_85c4xx_reset,
    .available     = NULL,
    .speed_changed = NULL,
    .force_redraw  = NULL,
    .config        = NULL
};

const device_t sis_85c460_device = {
    .name          = "SiS 85c460",
    .internal_name = "sis_85c460",
    .flags         = 0,
    .local         = 0x050,
    .init          = sis_85c4xx_init,
    .close         = sis_85c4xx_close,
    .reset         = sis_85c4xx_reset,
    .available     = NULL,
    .speed_changed = NULL,
    .force_redraw  = NULL,
    .config        = NULL
};

/* TODO: Log to make sure the registers are correct. */
const device_t sis_85c461_device = {
    .name          = "SiS 85c461",
    .internal_name = "sis_85c461",
    .flags         = 0,
    .local         = 0x050,
    .init          = sis_85c4xx_init,
    .close         = sis_85c4xx_close,
    .reset         = sis_85c4xx_reset,
    .available     = NULL,
    .speed_changed = NULL,
    .force_redraw  = NULL,
    .config        = NULL
};

const device_t sis_85c471_device = {
    .name          = "SiS 85c407/85c471",
    .internal_name = "sis_85c471",
    .flags         = 0,
    .local         = 0x150,
    .init          = sis_85c4xx_init,
    .close         = sis_85c4xx_close,
    .reset         = sis_85c4xx_reset,
    .available     = NULL,
    .speed_changed = NULL,
    .force_redraw  = NULL,
    .config        = NULL
};
