// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * 86Box: TI TACT83000 (TACT83443 ATU and TACT83442 MCU).
 *
 * Implements the memory-controller setup used by the HP Vectra 386N.
 * The TACT83000 data sheet documents the architecture but refers register
 * programming to SRZU001. Register decoding here follows the D24xx L.03.05
 * firmware: word index/data at 26h/24h, PMRAM at 8000h, shadow control at
 * MCU 6/7 and fast A20 at ATU 803h. DRAM timing and interleaving timing are
 * not modeled. The page table exposes 16 KB blocks below 1 MB and 64 KB
 * blocks above it.
 */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>
#include <86box/86box.h>
#include "cpu.h"
#include <86box/timer.h>
#include <86box/device.h>
#include <86box/io.h>
#include <86box/mem.h>
#include <86box/chipset.h>
#include <86box/plat_unused.h>

typedef struct tact83000_t tact83000_t;
typedef struct {
    mem_mapping_t mapping;
    uint32_t      base, size, phys;
    uint16_t      entry;
} tact_page_t;
struct tact83000_t {
    uint16_t    index, regs[0x1000];
    uint32_t    bank_base[4], bank_size[4];
    tact_page_t page[560];
};

static uint8_t
tact_read(uint32_t addr, void *priv)
{
    tact_page_t *p    = priv;
    uint32_t     phys = p->phys + addr - p->base;
    return phys < (mem_size << 10) ? mem_read_ram(phys, NULL) : 0xff;
}

static void
tact_write(uint32_t addr, uint8_t val, void *priv)
{
    tact_page_t *p    = priv;
    uint32_t     phys = p->phys + addr - p->base;
    if (phys < (mem_size << 10))
        mem_write_ram(phys, val, NULL);
}

static uint16_t
tact_readw(uint32_t addr, void *priv)
{
    tact_page_t *p    = priv;
    uint32_t     phys = p->phys + addr - p->base;
    return phys + 1 < (mem_size << 10) ? mem_read_ramw(phys, NULL) : 0xffff;
}

static uint32_t
tact_readl(uint32_t addr, void *priv)
{
    tact_page_t *p    = priv;
    uint32_t     phys = p->phys + addr - p->base;
    return phys + 3 < (mem_size << 10) ? mem_read_raml(phys, NULL) : 0xffffffff;
}

static void
tact_writew(uint32_t addr, uint16_t val, void *priv)
{
    tact_page_t *p    = priv;
    uint32_t     phys = p->phys + addr - p->base;
    if (phys + 1 < (mem_size << 10))
        mem_write_ramw(phys, val, NULL);
}

static void
tact_writel(uint32_t addr, uint32_t val, void *priv)
{
    tact_page_t *p    = priv;
    uint32_t     phys = p->phys + addr - p->base;
    if (phys + 3 < (mem_size << 10))
        mem_write_raml(phys, val, NULL);
}

static void
tact_recalc_page(tact83000_t *dev, unsigned n)
{
    tact_page_t *p       = &dev->page[n];
    unsigned     bank    = (p->entry >> 13) & 3;
    int          enabled = (dev->regs[2] & 0x8000) && (p->entry & 0x8000) && dev->bank_size[bank];
    int          state   = MEM_READ_INTERNAL | MEM_WRITE_INTERNAL;
    mem_mapping_disable(&p->mapping);
    p->phys = dev->bank_size[bank] ? dev->bank_base[bank] + (((p->entry & 0x1fff) << 14) % dev->bank_size[bank]) : 0xffffffff;
    /* MCU bit 8 selects VGA decoding; clearing it exposes the test RAM window. */
    if (((dev->regs[2] & 0x0100) && p->base >= 0xa0000 && p->base < 0xc0000) || (p->base >= 0xfe0000 && p->base < 0x1000000))
        enabled = 0;
    if (p->base >= 0xc0000 && p->base < 0x100000) {
        unsigned block = (p->base - 0xc0000) >> 14;
        unsigned mode  = (dev->regs[6 + (block >> 3)] >> ((block & 7) * 2)) & 3;
        state          = (mode & 2) ? MEM_READ_INTERNAL : MEM_READ_EXTANY;
        state |= (mode & 1) ? MEM_WRITE_INTERNAL : MEM_WRITE_DISABLED;
    }
    if (!enabled)
        state = MEM_READ_EXTANY | MEM_WRITE_EXTANY;
    mem_set_mem_state_both(p->base, p->size, state);
    if (enabled) {
        mem_mapping_set_exec(&p->mapping, ram + p->phys);
        mem_mapping_enable(&p->mapping);
    }
}

static void
tact_recalc(tact83000_t *dev)
{
    mem_mapping_disable(&ram_low_mapping);
    mem_mapping_disable(&ram_mid_mapping);
    mem_mapping_disable(&ram_high_mapping);
    for (unsigned i = 0; i < 560; i++)
        tact_recalc_page(dev, i);
    flushmmucache();
}

static uint16_t
tact_inw(uint16_t port, void *priv)
{
    tact83000_t *dev = priv;
    if (port == 0x26)
        return dev->index;

    uint16_t idx = dev->index;
    uint16_t val = 0xffff;
    if (idx < 0x1000)
        val = dev->regs[idx];
    else if ((idx & 0xf800) == 0x8000) {
        unsigned n = idx & 0x7ff;
        n          = n < 64 ? n : 64 + ((n - 64) >> 2);
        val        = dev->page[n].entry;
    }
    return val;
}

static void
tact_outw(uint16_t port, uint16_t val, void *priv)
{
    tact83000_t *dev = priv;
    if (port == 0x26) {
        dev->index = val;
        return;
    }
    unsigned idx = dev->index;
#ifdef ENABLE_TACT83000_LOG
    pclog("TACT %04X:%04X W %04X=%04X\n", CS, cpu_state.pc, idx, val);
#endif
    if (idx < 0x1000) {
        dev->regs[idx] = val;
        if (idx == 0x803) {
            mem_a20_alt = !!(val & 0x400);
            mem_a20_recalc();
        }
        if (idx == 2 || idx == 6 || idx == 7 || idx == 0x0a)
            tact_recalc(dev);
    } else if ((idx & 0xf800) == 0x8000) {
        unsigned n = idx & 0x7ff;
        /* The firmware programs conventional RAM in 64 KB groups; the
           remaining low PMRAM addresses select individual 16 KB pages. */
        if (n < 64 && !(n & 3)) {
            for (unsigned j = 0; j < 4; j++) {
                dev->page[n + j].entry = val ? val + j : 0;
                tact_recalc_page(dev, n + j);
            }
        } else {
            n                  = n < 64 ? n : 64 + ((n - 64) >> 2);
            dev->page[n].entry = val;
            tact_recalc_page(dev, n);
        }
        flushmmucache();
    }
}

static void
tact_close(void *priv)
{
    free(priv);
}

static void *
tact_init(UNUSED(const device_t *info))
{
    tact83000_t *dev  = calloc(1, sizeof(*dev));
    unsigned     left = mem_size << 10, base = 0;
    for (unsigned i = 0; i < 4; i++) {
        unsigned size = 0;
        /* Use 2 MB or 8 MB SIMM pairs, with naturally aligned bank bases. */
        if (left >= (2 << 20))
            size = (2 << 20);
        if (mem_size > 8192 && left >= (8 << 20))
            size = (8 << 20);
        dev->bank_base[i] = base;
        dev->bank_size[i] = size;
        base += size;
        left -= size;
    }
    for (unsigned i = 0; i < 560; i++) {
        tact_page_t *p = &dev->page[i];
        p->base        = i < 64 ? i << 14 : 0x100000 + ((i - 64) << 16);
        p->size        = i < 64 ? 0x4000 : 0x10000;
        mem_mapping_add(&p->mapping, p->base, p->size, tact_read, tact_readw, tact_readl, tact_write, tact_writew, tact_writel, NULL, MEM_MAPPING_INTERNAL, p);
        mem_mapping_disable(&p->mapping);
    }
    dev->regs[0x0a] = 0xffff;
    io_sethandler(0x24, 4, NULL, tact_inw, NULL, NULL, tact_outw, NULL, dev);
    return dev;
}

const device_t tact83000_device = {
    .name          = "Texas Instruments TACT83000",
    .internal_name = "tact83000",
    .init          = tact_init,
    .close         = tact_close
};
