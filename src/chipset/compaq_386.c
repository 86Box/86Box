/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          Emulation of the Compaq 386 memory controller.
 *
 * Authors: Miran Grca, <mgrca8@gmail.com>
 *
 *          Copyright 2023 Miran Grca.
 */
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <stdlib.h>
#include <wchar.h>
#include <math.h>
#include <86box/86box.h>
#include "cpu.h"
#include <86box/io.h>
#include <86box/timer.h>
#include <86box/pit.h>
#include <86box/mem.h>
#include <86box/rom.h>
#include <86box/device.h>
#include <86box/keyboard.h>
#include <86box/fdd.h>
#include <86box/fdc.h>
#include <86box/fdc_ext.h>
#include <86box/hdc.h>
#include <86box/hdc_ide.h>
#include <86box/machine.h>
#include <86box/video.h>
#include <86box/vid_cga.h>
#include <86box/vid_cga_comp.h>
#include <86box/plat_unused.h>
#include <86box/chipset.h>

#define RAM_DIAG_L_BASE_MEM_640KB    0x00
#define RAM_DIAG_L_BASE_MEM_INV      0x10
#define RAM_DIAG_L_BASE_MEM_512KB    0x20
#define RAM_DIAG_L_BASE_MEM_256KB    0x30
#define RAM_DIAG_L_BASE_MEM_MASK     0x30
#define RAM_DIAG_L_PERMA_BITS        0x80

#define RAM_DIAG_H_SYS_RAM_4MB       0x01
#define RAM_DIAG_H_SYS_RAM_1MB       0x02
#define RAM_DIAG_H_SYS_RAM_NONE      0x03
#define RAM_DIAG_H_SYS_RAM_MASK      0x03
#define RAM_DIAG_H_MOD_A_RAM_4MB     0x04
#define RAM_DIAG_H_MOD_A_RAM_1MB     0x08
#define RAM_DIAG_H_MOD_A_RAM_NONE    0x0c
#define RAM_DIAG_H_MOD_A_RAM_MASK    0x0c
#define RAM_DIAG_H_MOD_B_RAM_4MB     0x10
#define RAM_DIAG_H_MOD_B_RAM_1MB     0x20
#define RAM_DIAG_H_MOD_B_RAM_NONE    0x30
#define RAM_DIAG_H_MOD_B_RAM_MASK    0x30
#define RAM_DIAG_H_MOD_C_RAM_4MB     0x40
#define RAM_DIAG_H_MOD_C_RAM_1MB     0x80
#define RAM_DIAG_H_MOD_C_RAM_NONE    0xc0
#define RAM_DIAG_H_MOD_C_RAM_MASK    0xc0

#define MEM_STATE_BUS                0x00
#define MEM_STATE_SYS                0x01
#define MEM_STATE_SYS_RELOC          0x02
#define MEM_STATE_MOD_A              0x04
#define MEM_STATE_MOD_B              0x08
#define MEM_STATE_MOD_C              0x10
#define MEM_STATE_MASK               (MEM_STATE_SYS | MEM_STATE_MOD_A | MEM_STATE_MOD_B | MEM_STATE_MOD_C)
#define MEM_STATE_WP                 0x20

typedef struct cpq_ram_t {
    uint8_t          wp;

    uint32_t         phys_base;
    uint32_t         virt_base;

    mem_mapping_t    mapping;
} cpq_ram_t;

typedef struct cpq_386_t {
    uint8_t          regs[8];

    uint8_t          old_state[256];
    uint8_t          mem_state[256];

    uint32_t         ram_bases[4];

    uint32_t         ram_sizes[4];
    uint32_t         ram_map_sizes[4];

    cpq_ram_t        ram[4][64];
    cpq_ram_t        high_ram[16];

    mem_mapping_t    regs_mapping;
} cpq_386_t;

static uint8_t
cpq_read_ram(uint32_t addr, void *priv)
{
    const cpq_ram_t *dev = (cpq_ram_t *) priv;
    uint8_t          ret = 0xff;

    addr = (addr - dev->virt_base) + dev->phys_base;

    if (addr < (mem_size << 10))
        ret = mem_read_ram(addr, priv);

    return ret;
}

static uint16_t
cpq_read_ramw(uint32_t addr, void *priv)
{
    const cpq_ram_t *dev = (cpq_ram_t *) priv;
    uint16_t         ret = 0xffff;

    addr = (addr - dev->virt_base) + dev->phys_base;

    if (addr < (mem_size << 10))
        ret = mem_read_ramw(addr, priv);

    return ret;
}

static uint32_t
cpq_read_raml(uint32_t addr, void *priv)
{
    const cpq_ram_t *dev = (cpq_ram_t *) priv;
    uint32_t         ret = 0xffffffff;

    addr = (addr - dev->virt_base) + dev->phys_base;

    if (addr < (mem_size << 10))
        ret = mem_read_raml(addr, priv);

    return ret;
}

static void
cpq_write_ram(uint32_t addr, uint8_t val, void *priv)
{
    const cpq_ram_t *dev = (cpq_ram_t *) priv;

    addr = (addr - dev->virt_base) + dev->phys_base;

    if (!dev->wp && (addr < (mem_size << 10)))
        mem_write_ram(addr, val, priv);
}

static void
cpq_write_ramw(uint32_t addr, uint16_t val, void *priv)
{
    const cpq_ram_t *dev = (cpq_ram_t *) priv;

    addr = (addr - dev->virt_base) + dev->phys_base;

    if (!dev->wp && (addr < (mem_size << 10)))
        mem_write_ramw(addr, val, priv);
}

static void
cpq_write_raml(uint32_t addr, uint32_t val, void *priv)
{
    const cpq_ram_t *dev = (cpq_ram_t *) priv;

    addr = (addr - dev->virt_base) + dev->phys_base;

    if (!dev->wp && (addr < (mem_size << 10)))
        mem_write_raml(addr, val, priv);
}

static uint8_t
cpq_read_regs(uint32_t addr, void *priv)
{
    const cpq_386_t *dev = (cpq_386_t *) priv;
    uint8_t          ret = 0xff;

    addr &= 0x00000fff;

    switch (addr) {
        case 0x00000000:
        case 0x00000001:
            /* RAM Diagnostics (Read Only) */
        case 0x00000002:
        case 0x00000003:
            /* RAM Setup Port (Read/Write) */
            ret = dev->regs[addr];
            break;

        default:
            break;
    }

    return ret;
}

static uint16_t
cpq_read_regsw(uint32_t addr, void *priv)
{
    uint16_t ret = 0xffff;

    ret = cpq_read_regs(addr, priv);
    ret |= (((uint16_t) cpq_read_regs(addr + 1, priv)) << 8);

    return ret;
}

static uint32_t
cpq_read_regsl(uint32_t addr, void *priv)
{
    uint32_t ret = 0xffffffff;

    ret = cpq_read_regsw(addr, priv);
    ret |= (((uint32_t) cpq_read_regsw(addr + 2, priv)) << 16);

    return ret;
}

static void
cpq_recalc_state(cpq_386_t *dev, uint8_t i)
{
    uint32_t addr;

    addr = ((uint32_t) i) << 16;
    if (dev->mem_state[i] == 0x00)
        mem_set_mem_state(addr, 0x00010000, MEM_READ_EXTANY | MEM_WRITE_EXTANY);
    else if (dev->mem_state[i] == MEM_STATE_WP)
        mem_set_mem_state(addr, 0x00010000, MEM_READ_EXTANY | MEM_WRITE_DISABLED);
    else if (dev->mem_state[i] & MEM_STATE_WP)
        mem_set_mem_state(addr, 0x00010000, MEM_READ_INTERNAL | MEM_WRITE_DISABLED);
    else
        mem_set_mem_state(addr, 0x00010000, MEM_READ_INTERNAL | MEM_WRITE_INTERNAL);

    dev->old_state[i] = dev->mem_state[i];
}

static void
cpq_recalc_states(cpq_386_t *dev)
{
    /* Recalculate the entire 16 MB space. */
    for (uint16_t i = 0; i < 256; i++) {
        if (dev->mem_state[i] != dev->old_state[i])
            cpq_recalc_state(dev, i);
    }

    flushmmucache_nopc();
}

static void
cpq_recalc_cache(cpq_386_t *dev)
{
    cpu_cache_ext_enabled = (dev->regs[0x00000002] & 0x40);
    cpu_update_waitstates();
}

static void
cpq_recalc_ram(cpq_386_t *dev)
{
    uint8_t  sys_ram             = (dev->regs[0x00000001] & RAM_DIAG_H_SYS_RAM_MASK) & 0x01;
    uint8_t  setup_port          = dev->regs[0x00000002] & 0x0f;
    uint8_t  sys_min_high        = sys_ram ? 0xfa : 0xf4;
    uint8_t  ram_states[4]       = { MEM_STATE_SYS, MEM_STATE_MOD_A,
                                     MEM_STATE_MOD_B, MEM_STATE_MOD_C };
    uint8_t  ram_bases[4][2][16] = { { { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
                                         0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 },
                                       { 0x10, 0x00, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10,
                                         0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10 } },
                                     { { 0x00, 0x00, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10,
                                         0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x00, 0x00 },
                                       { 0x40, 0x00, 0x00, 0x00, 0x00, 0x40, 0x40, 0x40,
                                         0x40, 0x40, 0x40, 0x40, 0x40, 0x40, 0x40, 0x40 } },
                                     { { 0x00, 0x00, 0x00, 0x20, 0x20, 0x00, 0x20, 0x20,
                                         0x50, 0x50, 0x50, 0x50, 0x50, 0x50, 0x00, 0x00 },
                                       { 0x80, 0x00, 0x00, 0x00, 0x00, 0x00, 0x50, 0x50,
                                         0x00, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80 } },
                                     { { 0x00, 0x00, 0x00, 0x00, 0x30, 0x00, 0x00, 0x60,
                                         0x00, 0x00, 0x90, 0x90, 0x90, 0x90, 0x00, 0x00 },
                                       { 0xc0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x60,
                                         0x00, 0x00, 0x90, 0x00, 0x00, 0xc0, 0xc0, 0xc0 } } };
    uint8_t  ram_sizes[4][2][16] = { { { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
                                         0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 },
                                       { 0x30, 0x00, 0x10, 0x20, 0x30, 0x30, 0x30, 0x30,
                                         0x30, 0x30, 0x30, 0x30, 0x30, 0x30, 0x30, 0x30 } },
                                     { { 0x00, 0x00, 0x10, 0x10, 0x10, 0x40, 0x10, 0x10,
                                         0x40, 0x40, 0x40, 0x40, 0x40, 0x40, 0x00, 0x00 },
                                       { 0x40, 0x00, 0x00, 0x00, 0x00, 0x10, 0x10, 0x10,
                                         0x40, 0x40, 0x40, 0x40, 0x40, 0x40, 0x40, 0x40 } },
                                     { { 0x00, 0x00, 0x00, 0x10, 0x10, 0x00, 0x40, 0x40,
                                         0x30, 0x40, 0x40, 0x40, 0x40, 0x40, 0x00, 0x00 },
                                       { 0x40, 0x00, 0x00, 0x00, 0x00, 0x00, 0x10, 0x10,
                                         0x00, 0x10, 0x10, 0x30, 0x40, 0x40, 0x40, 0x40 } },
                                     { { 0x00, 0x00, 0x00, 0x00, 0x10, 0x00, 0x00, 0x10,
                                         0x00, 0x00, 0x10, 0x20, 0x30, 0x40, 0x00, 0x00 },
                                       { 0x3a, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x10,
                                         0x00, 0x00, 0x10, 0x00, 0x00, 0x10, 0x20, 0x30 } } };
    uint8_t  size;
    uint8_t  start;
    uint8_t  end;
    uint8_t  k;
    uint32_t virt_base;
    cpq_ram_t *cram;

    for (uint16_t i = 0x10; i < sys_min_high; i++)
        dev->mem_state[i] &= ~MEM_STATE_MASK;

    for (uint8_t i = 0; i < 4; i++) {
        for (uint8_t j = 0; j <= 64; j++) {
            if ((i >= 1) || (j >= 0x10))
                mem_mapping_disable(&dev->ram[i][j].mapping);
        }
    }

    for (uint8_t i = 0; i < 4; i++) {
        size = ram_sizes[i][sys_ram][setup_port];
        if (size > 0x00) {
            start     = ram_bases[i][sys_ram][setup_port];
            end       = start + (size - 1);

            virt_base = ((uint32_t) start) << 16;

            for (uint16_t j = start; j <= end; j++) {
                k = j - start;
                if (i == 0)
                    k += 0x10;

                cram = &(dev->ram[i][k]);

                dev->mem_state[j] |= ram_states[i];

                cram->virt_base = ((uint32_t) j) << 16;
                cram->phys_base = cram->virt_base - virt_base + dev->ram_bases[i];

                mem_mapping_set_addr(&cram->mapping, cram->virt_base, 0x00010000);
                mem_mapping_set_exec(&cram->mapping, &(ram[cram->phys_base]));
            }
        }
    }

    /* Recalculate the entire 16 MB space. */
    cpq_recalc_states(dev);
}

static void
cpq_write_regs(uint32_t addr, uint8_t val, void *priv)
{
    cpq_386_t *dev = (cpq_386_t *) priv;

    addr &= 0x00000fff;

    switch (addr) {
        case 0x00000000:
        case 0x00000001:
            /* RAM Relocation (Write Only) */
            dev->regs[addr + 4] = val;
            if (addr == 0x00000000) {
                dev->mem_state[0x0e] &= ~(MEM_STATE_SYS | MEM_STATE_WP);
                dev->mem_state[0x0f] &= ~(MEM_STATE_SYS | MEM_STATE_WP);
                dev->mem_state[0xfe] &= ~MEM_STATE_WP;
                dev->mem_state[0xff] &= ~MEM_STATE_WP;
                if (!(val & 0x01)) {
                    dev->mem_state[0x0e] |= MEM_STATE_SYS;
                    dev->mem_state[0x0f] |= MEM_STATE_SYS;
                }
                if (!(val & 0x02)) {
                    dev->mem_state[0x0e] |= MEM_STATE_WP;
                    dev->mem_state[0x0f] |= MEM_STATE_WP;
                    dev->mem_state[0xfe] |= MEM_STATE_WP;
                    dev->mem_state[0xff] |= MEM_STATE_WP;
                }
                cpq_recalc_state(dev, 0x0e);
                cpq_recalc_state(dev, 0x0f);
                cpq_recalc_state(dev, 0xfe);
                cpq_recalc_state(dev, 0xff);
                flushmmucache_nopc();
            }
            break;
        case 0x00000002:
        case 0x00000003:
            /* RAM Setup Port (Read/Write) */
            dev->regs[addr] = val;
            if (addr == 0x00000002) {
                cpq_recalc_ram(dev);
                cpq_recalc_cache(dev);
            }
            break;

        default:
            break;
    }
}

static void
cpq_write_regsw(uint32_t addr, uint16_t val, void *priv)
{
    cpq_write_regs(addr, val & 0xff, priv);
    cpq_write_regs(addr + 1, (val >> 8) & 0xff, priv);
}

static void
cpq_write_regsl(uint32_t addr, uint32_t val, void *priv)
{
    cpq_write_regsw(addr, val & 0xff, priv);
    cpq_write_regsw(addr + 2, (val >> 16) & 0xff, priv);
}

static void
compaq_ram_init(cpq_ram_t *dev)
{
    mem_mapping_add(&dev->mapping,
                    0x00000000,
                    0x00010000,
                    cpq_read_ram,
                    cpq_read_ramw,
                    cpq_read_raml,
                    cpq_write_ram,
                    cpq_write_ramw,
                    cpq_write_raml,
                    NULL,
                    MEM_MAPPING_INTERNAL,
                    dev);

    mem_mapping_disable(&dev->mapping);
}

static void
compaq_ram_diags_parse(cpq_386_t *dev)
{
    uint8_t  val = dev->regs[0x00000001];
    uint32_t accum = 0x00100000;

    for (uint8_t i = 0; i < 4; i++) {
        dev->ram_bases[i] = accum;

        switch (val & 0x03) {
            case RAM_DIAG_H_SYS_RAM_1MB:
                dev->ram_sizes[i] = 0x00100000;
                break;
            case RAM_DIAG_H_SYS_RAM_4MB:
                dev->ram_sizes[i] = 0x00400000;
                break;

            default:
                break;
        }
        if (i == 0)
            dev->ram_sizes[i] -= 0x00100000;

        dev->ram_map_sizes[i] = dev->ram_sizes[i];
        accum += dev->ram_sizes[i];

        if (accum >= (mem_size << 10)) {
            dev->ram_sizes[i] = (mem_size << 10) - dev->ram_bases[i];
            break;
        }

        val >>= 2;
    }
}

static void
compaq_recalc_base_ram(cpq_386_t *dev)
{
    uint8_t  base_mem    = dev->regs[0x00000000] & RAM_DIAG_L_BASE_MEM_MASK;
    uint8_t  sys_ram     = dev->regs[0x00000001] & RAM_DIAG_H_SYS_RAM_MASK;
    uint8_t  low_start   = 0x00;
    uint8_t  low_end     = 0x00;
    uint8_t  high_start  = 0x00;
    uint8_t  high_end    = 0x00;
    cpq_ram_t *cram;

    switch (base_mem) {
        case RAM_DIAG_L_BASE_MEM_256KB:
            switch (sys_ram) {
                case RAM_DIAG_H_SYS_RAM_1MB:
                    low_start  = 0x00;
                    low_end    = 0x03;
                    high_start = 0xf4;
                    high_end   = 0xff;
                    break;
                case RAM_DIAG_H_SYS_RAM_4MB:
                    low_start  = 0x00;
                    low_end    = 0x03;
                    high_start = 0xfa;
                    high_end   = 0xff;
                    break;
                default:
                    fatal("Compaq 386 - Invalid configuation: %02X %02X\n", base_mem, sys_ram);
                    return;
            }
            break;
        case RAM_DIAG_L_BASE_MEM_512KB:
            switch (sys_ram) {
                case RAM_DIAG_H_SYS_RAM_1MB:
                    low_start  = 0x00;
                    low_end    = 0x07;
                    high_start = 0xf8;
                    high_end   = 0xff;
                    break;
                case RAM_DIAG_H_SYS_RAM_4MB:
                    low_start  = 0x00;
                    low_end    = 0x07;
                    high_start = 0xfa;
                    high_end   = 0xff;
                    break;
                default:
                    fatal("Compaq 386 - Invalid configuation: %02X %02X\n", base_mem, sys_ram);
                    return;
            }
            break;
        case RAM_DIAG_L_BASE_MEM_640KB:
            switch (sys_ram) {
                case RAM_DIAG_H_SYS_RAM_1MB:
                    low_start  = 0x00;
                    low_end    = 0x09;
                    high_start = 0xfa;
                    high_end   = 0xff;
                    break;
                case RAM_DIAG_H_SYS_RAM_4MB:
                    low_start  = 0x00;
                    low_end    = 0x09;
                    high_start = 0xfa;
                    high_end   = 0xff;
                    break;
                default:
                    fatal("Compaq 386 - Invalid configuation: %02X %02X\n", base_mem, sys_ram);
                    return;
            }
            break;
        default:
            fatal("Compaq 386 - Invalid configuation: %02X %02X\n", base_mem, sys_ram);
            return;
    }

    switch (sys_ram) {
        case RAM_DIAG_H_SYS_RAM_1MB:
            if (mem_size < 1024)
                dev->regs[0x00000002] = 0x01;
            else if (mem_size == 8192)
                dev->regs[0x00000002] = 0x09;
            else if (mem_size >= 11264)
                dev->regs[0x00000002] = 0x0d;
            else
                dev->regs[0x00000002] = (mem_size >> 10);
            break;
        case RAM_DIAG_H_SYS_RAM_4MB:
            if (mem_size < 4096)
                dev->regs[0x00000002] = 0x04;
            else if (mem_size == 11264)
                dev->regs[0x00000002] = 0x0c;
            else if (mem_size >= 16384)
                dev->regs[0x00000002] = 0x00;
            else if (mem_size > 13312)
                dev->regs[0x00000002] = 0x0d;
            else
                dev->regs[0x00000002] = (mem_size >> 10);
            break;
        default:
            fatal("Compaq 386 - Invalid configuation: %02X\n", sys_ram);
            return;
    }

    /* The base 640 kB. */
    for (uint8_t i = low_start; i <= low_end; i++) {
        cram = &(dev->ram[0][i]);

        cram->phys_base = cram->virt_base = ((uint32_t) i) << 16;
        dev->mem_state[i] |= MEM_STATE_SYS;

        mem_mapping_set_addr(&cram->mapping, cram->virt_base, 0x00010000);
        mem_mapping_set_exec(&cram->mapping, &(ram[cram->phys_base]));

        cpq_recalc_state(dev, i);
    }

    /* The relocated 128 kB. */
    for (uint8_t i = 0x0e; i <= 0x0f; i++) {
        cram = &(dev->ram[0][i]);

        cram->phys_base = cram->virt_base = ((uint32_t) i) << 16;

        mem_mapping_set_addr(&cram->mapping, cram->virt_base, 0x00010000);
        mem_mapping_set_exec(&cram->mapping, &(ram[cram->phys_base]));
    }

    /* Blocks FA-FF. */
    for (uint16_t i = high_start; i <= high_end; i++) {
        cram = &(dev->high_ram[i & 0x0f]);

        cram->phys_base = ((uint32_t) (i & 0x0f)) << 16;
        cram->virt_base = ((uint32_t) i) << 16;
        dev->mem_state[i] |= MEM_STATE_SYS;

        mem_mapping_set_addr(&cram->mapping, cram->virt_base, 0x00010000);
        mem_mapping_set_exec(&cram->mapping, &(ram[cram->phys_base]));

        cpq_recalc_state(dev, i);
    }
}

static void
compaq_386_close(void *priv)
{
    cpq_386_t *dev = (cpq_386_t *) priv;

    free(dev);
}

static void *
compaq_386_init(UNUSED(const device_t *info))
{
    cpq_386_t *dev = (cpq_386_t *) calloc(1, sizeof(cpq_386_t));

    mem_mapping_add(&dev->regs_mapping,
                    0x80c00000,
                    0x00001000,
                    cpq_read_regs,
                    cpq_read_regsw,
                    cpq_read_regsl,
                    cpq_write_regs,
                    cpq_write_regsw,
                    cpq_write_regsl,
                    NULL,
                    MEM_MAPPING_INTERNAL,
                    dev);

    mem_set_mem_state(0x80c00000, 0x00001000, MEM_READ_INTERNAL | MEM_WRITE_INTERNAL);

    dev->regs[0x00000000] = RAM_DIAG_L_PERMA_BITS;
    if (mem_size >= 640)
        dev->regs[0x00000000] |= RAM_DIAG_L_BASE_MEM_640KB;
    else if (mem_size >= 512)
        dev->regs[0x00000000] |= RAM_DIAG_L_BASE_MEM_512KB;
    else if (mem_size >= 256)
        dev->regs[0x00000000] |= RAM_DIAG_L_BASE_MEM_256KB;
    else
        dev->regs[0x00000000] |= RAM_DIAG_L_BASE_MEM_INV;
    /* Indicate no parity error. */
    dev->regs[0x00000000] |= 0x0f;

    if (mem_size >= 1024) {
        switch (mem_size) {
            case 1024:
                dev->regs[0x00000001] = RAM_DIAG_H_SYS_RAM_4MB | RAM_DIAG_H_MOD_A_RAM_NONE |
                                        RAM_DIAG_H_MOD_B_RAM_NONE | RAM_DIAG_H_MOD_C_RAM_NONE;
                break;
            case 2048:
                dev->regs[0x00000001] = RAM_DIAG_H_SYS_RAM_4MB | RAM_DIAG_H_MOD_A_RAM_NONE |
                                        RAM_DIAG_H_MOD_B_RAM_NONE | RAM_DIAG_H_MOD_C_RAM_NONE;
                break;
            case 3072:
                dev->regs[0x00000001] = RAM_DIAG_H_SYS_RAM_4MB | RAM_DIAG_H_MOD_A_RAM_NONE |
                                        RAM_DIAG_H_MOD_B_RAM_NONE | RAM_DIAG_H_MOD_C_RAM_NONE;
                break;
            case 4096:
                dev->regs[0x00000001] = RAM_DIAG_H_SYS_RAM_4MB | RAM_DIAG_H_MOD_A_RAM_NONE |
                                        RAM_DIAG_H_MOD_B_RAM_NONE | RAM_DIAG_H_MOD_C_RAM_NONE;
                break;
            case 5120:
                dev->regs[0x00000001] = RAM_DIAG_H_SYS_RAM_4MB | RAM_DIAG_H_MOD_A_RAM_1MB |
                                        RAM_DIAG_H_MOD_B_RAM_NONE | RAM_DIAG_H_MOD_C_RAM_NONE;
                break;
            case 6144:
                dev->regs[0x00000001] = RAM_DIAG_H_SYS_RAM_4MB | RAM_DIAG_H_MOD_A_RAM_1MB |
                                        RAM_DIAG_H_MOD_B_RAM_1MB | RAM_DIAG_H_MOD_C_RAM_NONE;
                break;
            case 7168:
                dev->regs[0x00000001] = RAM_DIAG_H_SYS_RAM_4MB | RAM_DIAG_H_MOD_A_RAM_1MB |
                                        RAM_DIAG_H_MOD_B_RAM_1MB | RAM_DIAG_H_MOD_C_RAM_1MB;
                break;
            case 8192:
                dev->regs[0x00000001] = RAM_DIAG_H_SYS_RAM_4MB | RAM_DIAG_H_MOD_A_RAM_4MB |
                                        RAM_DIAG_H_MOD_B_RAM_NONE | RAM_DIAG_H_MOD_C_RAM_NONE;
                break;
            case 9216:
                dev->regs[0x00000001] = RAM_DIAG_H_SYS_RAM_4MB | RAM_DIAG_H_MOD_A_RAM_4MB |
                                        RAM_DIAG_H_MOD_B_RAM_1MB | RAM_DIAG_H_MOD_C_RAM_NONE;
                break;
            case 10240:
                dev->regs[0x00000001] = RAM_DIAG_H_SYS_RAM_4MB | RAM_DIAG_H_MOD_A_RAM_4MB |
                                        RAM_DIAG_H_MOD_B_RAM_1MB | RAM_DIAG_H_MOD_C_RAM_1MB;
                break;
            case 11264:
            case 12288:
                dev->regs[0x00000001] = RAM_DIAG_H_SYS_RAM_4MB | RAM_DIAG_H_MOD_A_RAM_4MB |
                                        RAM_DIAG_H_MOD_B_RAM_4MB | RAM_DIAG_H_MOD_C_RAM_NONE;
                break;
            case 13312:
                dev->regs[0x00000001] = RAM_DIAG_H_SYS_RAM_4MB | RAM_DIAG_H_MOD_A_RAM_4MB |
                                        RAM_DIAG_H_MOD_B_RAM_4MB | RAM_DIAG_H_MOD_C_RAM_1MB;
                break;
            case 14336:
            case 15360:
            case 16384:
                dev->regs[0x00000001] = RAM_DIAG_H_SYS_RAM_4MB | RAM_DIAG_H_MOD_A_RAM_4MB |
                                        RAM_DIAG_H_MOD_B_RAM_4MB | RAM_DIAG_H_MOD_C_RAM_4MB;
                break;

            default:
                break;
        }
    } else
        dev->regs[0x00000001] = RAM_DIAG_H_SYS_RAM_1MB | RAM_DIAG_H_MOD_A_RAM_NONE |
                                RAM_DIAG_H_MOD_B_RAM_NONE | RAM_DIAG_H_MOD_C_RAM_NONE;

    dev->regs[0x00000003] = 0xfc;
    dev->regs[0x00000004] = dev->regs[0x00000005] = 0xff;

    compaq_ram_diags_parse(dev);

    mem_mapping_disable(&ram_low_mapping);
    mem_mapping_disable(&ram_mid_mapping);
    mem_mapping_disable(&ram_high_mapping);
#if (!(defined __amd64__ || defined _M_X64 || defined __aarch64__ || defined _M_ARM64))
    /* Should never be the case, but you never know what a user may set. */
    if (mem_size > 1048576)
        mem_mapping_disable(&ram_2gb_mapping);
#endif

    /* Initialize in reverse order for memory mapping precedence
       reasons. */
    for (int8_t i = 3; i >= 0; i--) {
        for (uint8_t j = 0; j < 64; j++)
            compaq_ram_init(&(dev->ram[i][j]));
    }

    for (uint8_t i = 0; i < 16; i++)
        compaq_ram_init(&(dev->high_ram[i]));

    /* First, set the entire 256 MB of space to invalid states. */
    for (uint16_t i = 0; i < 256; i++)
        dev->old_state[i] = 0xff;

    /* Then, recalculate the base RAM mappings. */
    compaq_recalc_base_ram(dev);

    /* Enable the external cache. */
    dev->regs[0x00000002] |= 0x40;
    cpq_recalc_cache(dev);

    /* Recalculate the rest of the RAM mapping. */
    cpq_recalc_ram(dev);

    return dev;
}

static void
compaq_genoa_outw(uint16_t port, uint16_t val, void *priv)
{
     if (port == 0x0c02)
        cpq_write_regs(0x80c00000, val, priv);
}

static void *
compaq_genoa_init(UNUSED(const device_t *info))
{
    void *cpq = device_add(&compaq_386_device);

    io_sethandler(0x0c02, 2, NULL, NULL, NULL, NULL, compaq_genoa_outw, NULL, cpq);

    return ram;
}

const device_t compaq_386_device = {
    .name          = "Compaq 386 Memory Control",
    .internal_name = "compaq_386",
    .flags         = 0,
    .local         = 0,
    .init          = compaq_386_init,
    .close         = compaq_386_close,
    .reset         = NULL,
    .available     = NULL,
    .speed_changed = NULL,
    .force_redraw  = NULL,
    .config        = NULL
};

const device_t compaq_genoa_device = {
    .name          = "Compaq Genoa Memory Control",
    .internal_name = "compaq_genoa",
    .flags         = 0,
    .local         = 0,
    .init          = compaq_genoa_init,
    .close         = NULL,
    .reset         = NULL,
    .available     = NULL,
    .speed_changed = NULL,
    .force_redraw  = NULL,
    .config        = NULL
};
