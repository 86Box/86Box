/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          Implementation of the Western Digital WD76C10 chipset.
 *
 * Authors: Miran Grca, <mgrca8@gmail.com>
 *
 *          Copyright 2024 Miran Grca.
 *
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
#include <86box/timer.h>
#include <86box/io.h>
#include <86box/device.h>
#include <86box/dma.h>
#include <86box/fdd.h>
#include <86box/fdc.h>
#include <86box/hdd.h>
#include <86box/hdc.h>
#include <86box/hdc_ide.h>
#include <86box/lpt.h>
#include <86box/mem.h>
#include <86box/nvr.h>
#include <86box/port_92.h>
#include <86box/serial.h>
#include <86box/plat_fallthrough.h>
#include <86box/plat_unused.h>
#include <86box/chipset.h>

/* Lock/Unlock Procedures */
#define LOCK dev->locked
#define UNLOCKED !dev->locked

#define WD76C10_ADDR_INVALID    0x80000000

#ifdef ENABLE_WD76C10_LOG
int wd76c10_do_log = ENABLE_WD76C10_LOG;
static void
wd76c10_log(const char *fmt, ...)
{
    va_list ap;

    if (wd76c10_do_log)
    {
        va_start(ap, fmt);
        pclog_ex(fmt, ap);
        va_end(ap);
    }
}
#else
#define wd76c10_log(fmt, ...)
#endif

typedef struct {
    uint32_t enable;
    uint32_t virt_addr, phys_addr;
    uint32_t virt_size, phys_size;
} ram_bank_t;

typedef struct {
    uint8_t enabled;

    uint32_t virt, phys;
    uint32_t size;
} ems_page_t;

typedef struct
{
    uint8_t ep, p92;

    uint8_t vbios_states[4];
    uint8_t bios_states[8];
    uint8_t high_bios_states[8];
    uint8_t mem_pages[1024];
    uint8_t ram_state[4192];

    uint16_t toggle, cpuclk, fpu_ctl, mem_ctl,
             split_sa, sh_wp, hmwpb, npmdmt,
             ems_ctl, ems_pp, ser_par_cs, rtc_disk_cs,
             prog_cs, pmc_in;

    union
    {
        uint16_t bank_base_regs[2];
        uint8_t  bank_bases[4];
    };

    uint16_t ems_page_regs[40];

    int locked;

    uint32_t mem_top, hmwp_base;

    ram_bank_t ram_banks[5];

    ems_page_t ems_pages[40];

    mem_mapping_t ram_mapping;

    nvr_t     *nvr;

    fdc_t *fdc;
    serial_t *uart[2];
} wd76c10_t;

static uint32_t bank_sizes[4] = { 0x00020000,      /*  64 Kbit X 16 = 1024 Kbit = 128 kB,  8x 8 */
                                  0x00080000,      /* 256 Kbit X 16 = 4096 Kbit = 512 kB,  9x 9 */
                                  0x00200000,      /*   1 Mbit X 16 =   16 Mbit =   2 MB, 10x10 */
                                  0x00800000 };    /*   4 Mbit X 16 =   64 Mbit =   8 MB, 11x11 */

static uint32_t
wd76c10_calc_addr(wd76c10_t *dev, uint32_t addr)
{
    uint32_t ret;
    uint8_t ems_page;
    uint8_t ems_en = (uint8_t) ((dev->ems_ctl >> 10) & 0x03);
    ems_page_t *ep;
    uint8_t en_res = (uint8_t) ((dev->ems_ctl >> 7) & 0x01);
    uint32_t low_boundary = (((uint32_t) (dev->ems_ctl & 0x007f)) << 17) + 131072;
    ram_bank_t *rb = &(dev->ram_banks[4]);

    addr &= 0x00ffffff;
    ems_page = dev->mem_pages[addr >> 14];

    ep = &dev->ems_pages[ems_page];

    ret = addr;

    /* First, do any address translation (EMS, low boundary filtering). */
    if ((ems_page < 0x20) && (ems_en == 0x03))
        /* Low EMS pages. */
        ret = addr - ep->virt + ep->phys;
    else if ((ems_page >= 0x20) && (ems_page < 0x2a) && (ems_en >= 0x02) && ep->enabled)
        /* High EMS pages. */
        ret = addr - ep->virt + ep->phys;
    else if (en_res && (addr >= low_boundary))
        /* EMS low boundary. */
        ret = WD76C10_ADDR_INVALID;

    /* Then, do the split. */
    if (rb->enable && (ret >= rb->virt_addr) && (ret < (rb->virt_addr + rb->virt_size)))
        ret = ret - rb->virt_addr + rb->phys_addr;

    /* Then, disable the required amount of on-board memory between 128k and 640k if so requested. */
    if ((ret >= dev->mem_top) && (ret < 0x000a0000))
        ret = WD76C10_ADDR_INVALID;

    /* Then, handle the physical memory banks. */
    if (ret >= (mem_size << 10))
        /* The physical memory address is too high or disabled, which is invalid. */
        ret = WD76C10_ADDR_INVALID;
    /* Otherwise, map it to the correct bank so the BIOS can auto-size it correctly. */
    else for (uint8_t i = 0; i < 4; i++) {
        rb = &(dev->ram_banks[i]);
        if (rb->enable && (ret >= rb->virt_addr) && (ret < (rb->virt_addr + rb->virt_size))) {
            if (rb->phys_size == 0x00000000)
                ret = WD76C10_ADDR_INVALID;
            else
                ret = ((ret - rb->virt_addr) % rb->phys_size) + rb->phys_addr;
            break;
        }
    }

    return ret;
}

static uint8_t
wd76c10_read_ram(uint32_t addr, void *priv)
{
    wd76c10_t *dev = (wd76c10_t *) priv;
    uint8_t ret = 0xff;

    addr = wd76c10_calc_addr(dev, addr);

    if (addr != WD76C10_ADDR_INVALID)
        ret = mem_read_ram(addr, priv);

    return ret;
}

static uint16_t
wd76c10_read_ramw(uint32_t addr, void *priv)
{
    wd76c10_t *dev = (wd76c10_t *) priv;
    uint16_t ret = 0xffff;

    addr = wd76c10_calc_addr(dev, addr);

    if (addr != WD76C10_ADDR_INVALID)
        ret = mem_read_ramw(addr, priv);

    return ret;
}

static void
wd76c10_write_ram(uint32_t addr, uint8_t val, void *priv)
{
    wd76c10_t *dev = (wd76c10_t *) priv;

    addr = wd76c10_calc_addr(dev, addr);

    if (addr != WD76C10_ADDR_INVALID)
        mem_write_ram(addr, val, priv);
}

static void
wd76c10_write_ramw(uint32_t addr, uint16_t val, void *priv)
{
    wd76c10_t *dev = (wd76c10_t *) priv;

    addr = wd76c10_calc_addr(dev, addr);

    if (addr != WD76C10_ADDR_INVALID)
        mem_write_ramw(addr, val, priv);
}

static void
wd76c10_set_mem_state(wd76c10_t *dev, uint32_t base, uint32_t size, uint32_t access, uint8_t present)
{
    mem_set_mem_state_both(base, size, access);

    for (uint32_t i = base; i < (base + size); i += 4096)
        dev->ram_state[i >> 12] = present;
}

static void
wd76c10_recalc_exec(wd76c10_t *dev, uint32_t base, uint32_t size)
{
    uint32_t logical_addr = wd76c10_calc_addr(dev, base);
    void *exec;

    if (logical_addr != WD76C10_ADDR_INVALID)
        exec = &(ram[logical_addr]);
    else
        exec = NULL;

    for (uint32_t i = base; i < (base + size); i += 4096)
        if (dev->ram_state[i >> 12])
            _mem_exec[i >> 12] = exec;

    if (cpu_use_exec)
        flushmmucache_nopc();
}

static void
wd76c10_banks_recalc(wd76c10_t *dev)
{
    for (uint8_t i = 0; i < 4; i++) {
        ram_bank_t *rb = &(dev->ram_banks[i]);
        uint8_t bit = i << 1;
        rb->virt_size = bank_sizes[(dev->mem_ctl >> bit) & 0x03];
        bit = i + 12;
        rb->enable = (dev->split_sa >> bit) & 0x01;
        rb->virt_addr = ((uint32_t) dev->bank_bases[i]) << 17;

        if (cpu_use_exec)
            wd76c10_recalc_exec(dev, rb->virt_addr, rb->virt_size);
    }
}

static void
wd76c10_split_recalc(wd76c10_t *dev)
{
    uint32_t sp_size = (dev->split_sa >> 8) & 0x03;
    uint32_t split_size = ((sp_size - 1) * 65536);
    ram_bank_t *rb = &(dev->ram_banks[4]);

    if (rb->enable && (rb->virt_size != 0x00000000)) {
        wd76c10_set_mem_state(dev, rb->virt_addr, rb->virt_size, MEM_READ_EXTANY | MEM_WRITE_EXTANY, 0);

        if (cpu_use_exec)
            wd76c10_recalc_exec(dev, rb->virt_addr, rb->virt_size);
    }
    rb->virt_addr = ((uint32_t) ((dev->split_sa >> 2) & 0x3f)) << 19;
    switch (sp_size) {
        case 0x00:
            rb->virt_size = 0x00000000;
            break;
        default:
            rb->virt_size = 256 * 1024 + split_size;
            break;
    }
    rb->enable = !!sp_size;
    if (rb->enable && (rb->virt_size != 0x00000000)) {
        wd76c10_set_mem_state(dev, rb->virt_addr, rb->virt_size, MEM_READ_INTERNAL | MEM_WRITE_INTERNAL, 1);

        if (cpu_use_exec)
            wd76c10_recalc_exec(dev, rb->virt_addr, rb->virt_size);
    }
}

static void
wd76c10_dis_mem_recalc(wd76c10_t *dev)
{
    uint8_t dis_mem = (uint8_t) ((dev->sh_wp >> 14) & 0x03);
    uint32_t mem_top;

    switch (dis_mem) {
        case 0x00:
        default:
            mem_top  = 640 * 1024;
            break;
        case 0x01:
            mem_top = 512 * 1024;
            break;
        case 0x02:
            mem_top = 256 * 1024;
            break;
        case 0x03:
            mem_top = 128 * 1024;
            break;
    }

    dev->mem_top = mem_top;

    if (cpu_use_exec)
        wd76c10_recalc_exec(dev, 128 * 1024, (640 - 128) * 1024);
}

static void
wd76c10_shadow_ram_do_recalc(wd76c10_t *dev, uint8_t *new_st, uint8_t *old_st, uint8_t min, uint8_t max, uint32_t addr)
{
    uint32_t base = 0x00000000;
    int flags = 0;

    for (uint8_t i = min; i < max; i++) {
        if (new_st[i] != old_st[i]) {
            old_st[i] = new_st[i];
            base = addr + ((uint32_t) i) * 0x00004000;
            flags = (new_st[i] & 0x01) ? MEM_READ_INTERNAL :
                    ((new_st[i] & 0x04) ? MEM_READ_ROMCS : MEM_READ_EXTERNAL);
            flags |= (new_st[i] & 0x02) ? MEM_WRITE_INTERNAL :
                     ((new_st[i] & 0x04) ? MEM_WRITE_ROMCS : MEM_WRITE_EXTERNAL);
            wd76c10_set_mem_state(dev, base, 0x00004000, flags, new_st[i] & 0x01);
            if (cpu_use_exec)
                wd76c10_recalc_exec(dev, base, 0x000040000);
        }
    }
}


static void
wd76c10_shadow_ram_recalc(wd76c10_t *dev)
{
    uint8_t vbios_states[4] = { 0 };
    uint8_t bios_states[8] = { 0 };
    uint8_t high_bios_states[8] = { 0 };
    uint8_t wp = (uint8_t) ((dev->sh_wp >> 12) & 0x01);
    uint8_t shd = (uint8_t) ((dev->sh_wp >> 8) & 0x03);
    uint8_t x_mem = (uint8_t) ((dev->sh_wp >> 7) & 0x01);
    uint8_t vb_siz = (uint8_t) ((dev->sh_wp >> 4) & 0x03);
    uint8_t vb_top = vb_siz + 1;
    uint8_t rom_typ = (uint8_t) ((dev->sh_wp >> 2) & 0x03);

    switch (shd) {
        case 0x03:
            for (uint8_t i = 0; i < vb_top; i++) {
                vbios_states[i] |= 0x01;        /* Read. */
                if (!wp)
                    vbios_states[i] |= 0x02;    /* Write. */
            }
            if (x_mem) {
                for (uint8_t i = 2; i < 4; i++)
                    bios_states[i] |= 0x03;     /* Read/write. */
            }
            fallthrough;
        case 0x01:
            for (uint8_t i = 4; i < 8; i++) {
                bios_states[i] |= 0x01;         /* Read. */
                if (!wp)
                    bios_states[i] |= 0x02;     /* Write. */
            }
            break;
        case 0x02:
            for (uint8_t i = 0; i < 8; i++) {
                bios_states[i] |= 0x01;         /* Read. */
                if (!wp)
                    bios_states[i] |= 0x02;     /* Write. */
            }
            break;
    }

    switch (rom_typ) {
        case 0x00:
            for (uint8_t i = 0; i < 8; i++) {
                bios_states[i] |= 0x04;         /* CSPROM#. */
                high_bios_states[i] |= 0x04;    /* CSPROM#. */
            }
            break;
        case 0x02:
            for (uint8_t i = 0; i < vb_top; i++)
                vbios_states[i] |= 0x04;        /* CSPROM#. */
            fallthrough;
        case 0x01:
            for (uint8_t i = 4; i < 8; i++) {
                bios_states[i] |= 0x04;         /* CSPROM#. */
                high_bios_states[i] |= 0x04;    /* CSPROM#. */
            }
            break;
    }

   wd76c10_shadow_ram_do_recalc(dev, vbios_states, dev->vbios_states, 0, 4, 0x000c0000);
   wd76c10_shadow_ram_do_recalc(dev, bios_states, dev->bios_states, 0, 8, 0x000e0000);

   /* This is not shadowed, but there is a CSPROM# (= ROMCS#) toggle. */
   wd76c10_shadow_ram_do_recalc(dev, high_bios_states, dev->high_bios_states, 0, 8, 0x00fe0000);

   flushmmucache_nopc();
}

static void
wd76c10_high_mem_wp_recalc(wd76c10_t *dev)
{
    uint8_t hm_wp = (uint8_t) ((dev->sh_wp >> 13) & 0x01);
    uint32_t base = ((uint32_t) (dev->hmwpb & 0x00f0)) << 17;
    uint32_t size = 0x01000000 - dev->hmwp_base;

    /* ACCESS_NORMAL means both ACCESS_BUS and ACCESS_CPU are set. */
    mem_set_wp(dev->hmwp_base, size, ACCESS_NORMAL, 0);

    if (cpu_use_exec)
        wd76c10_recalc_exec(dev, dev->hmwp_base, size);

    size = 0x01000000 - base;
    mem_set_wp(base, size, ACCESS_NORMAL, hm_wp);

    if (cpu_use_exec)
        wd76c10_recalc_exec(dev, base, size);

    dev->hmwp_base = base;
}

static void
wd76c10_pf_loc_reset(wd76c10_t *dev)
{
    uint32_t base;

    for (uint8_t i = 0x031; i <= 0x03b; i++) {
        dev->mem_pages[i] = 0xff;
        base = ((uint32_t) i) << 14;
        wd76c10_set_mem_state(dev, base, 0x00004000, MEM_READ_EXTANY | MEM_WRITE_EXTANY, 0);

        if (cpu_use_exec)
            wd76c10_recalc_exec(dev, base, 0x00004000);
    }

    /* Re-apply any ROMCS#, etc. flags. */
    wd76c10_shadow_ram_recalc(dev);
}

static void
wd76c10_pf_loc_recalc(wd76c10_t *dev)
{
    uint8_t pf_loc = (uint8_t) ((dev->ems_ctl >> 13) & 0x03);
    uint8_t ems_en = (uint8_t) ((dev->ems_ctl >> 10) & 0x03);
    uint8_t ems_page;
    uint32_t base;

    for (uint8_t i = (0x031 + pf_loc); i <= (0x037 + pf_loc); i++) {
        ems_page = (i - 0x10) & 0xf7;
        dev->mem_pages[i] = ems_page;
        base = ((uint32_t) i) << 14;
        dev->ems_pages[ems_page].virt = base;
        if ((ems_en >= 0x02) && dev->ems_pages[ems_page].enabled) {
            wd76c10_set_mem_state(dev, dev->ems_pages[ems_page].virt,
                                  0x00004000, MEM_READ_INTERNAL | MEM_WRITE_INTERNAL, 1);

            if (cpu_use_exec)
                wd76c10_recalc_exec(dev, dev->ems_pages[ems_page].virt, 0x00004000);
        }
    }
}

static void
wd76c10_low_pages_recalc(wd76c10_t *dev)
{
    uint8_t ems_page;
    uint32_t base;

    for (uint8_t i = 0x008; i <= 0x027; i++) {
        ems_page = i & 0x1f;
        dev->mem_pages[i] = ems_page;
        base = ((uint32_t) i) << 14;
        dev->ems_pages[ems_page].virt = base;

        if (cpu_use_exec)
            wd76c10_recalc_exec(dev, dev->ems_pages[ems_page].virt, 0x00004000);
    }
}

static void
wd76c10_ser_par_cs_recalc(wd76c10_t *dev)
{
    /* Serial B */
    serial_remove(dev->uart[1]);
    switch ((dev->ser_par_cs >> 1) & 0x07) {
        case 0x01:
            serial_setup(dev->uart[1], 0x3f8, 3);
            break;
        case 0x02:
            serial_setup(dev->uart[1], 0x2f8, 3);
            break;
        case 0x03:
            serial_setup(dev->uart[1], 0x3e8, 3);
            break;
        case 0x04:
            serial_setup(dev->uart[1], 0x2e8, 3);
            break;
    }

    /* Serial A */
    serial_remove(dev->uart[0]);
    switch ((dev->ser_par_cs >> 5) & 0x07) {
        case 0x01:
            serial_setup(dev->uart[0], 0x3f8, 4);
            break;
        case 0x02:
            serial_setup(dev->uart[0], 0x2f8, 4);
            break;
        case 0x03:
            serial_setup(dev->uart[0], 0x3e8, 4);
            break;
        case 0x04:
            serial_setup(dev->uart[0], 0x2e8, 4);
            break;
    }

    /* LPT */
    lpt1_remove();
    switch ((dev->ser_par_cs >> 9) & 0x03) {
        case 1:
            lpt1_setup(LPT_MDA_ADDR);
            lpt1_irq(LPT1_IRQ);
            break;
        case 2:
            lpt1_setup(LPT1_ADDR);
            lpt1_irq(LPT1_IRQ);
            break;
        case 3:
            lpt1_setup(LPT2_ADDR);
            lpt1_irq(LPT1_IRQ);
            break;
    }
}

static void
wd76c10_disk_cs_recalc(wd76c10_t *dev)
{
    ide_pri_disable();
    ide_set_base(0, (dev->rtc_disk_cs & 0x0010) ? 0x0170 : 0x01f0);
    ide_set_side(0, (dev->rtc_disk_cs & 0x0010) ? 0x0376 : 0x03f6);
    if (!(dev->rtc_disk_cs & 0x0002))
        ide_pri_enable();

    fdc_remove(dev->fdc);
    if (!(dev->rtc_disk_cs & 0x0001))
        fdc_set_base(dev->fdc, (dev->rtc_disk_cs & 0x0010) ? FDC_SECONDARY_ADDR : FDC_PRIMARY_ADDR);
}

static void
wd76c10_outb(uint16_t port, uint8_t val, void *priv)
{
    wd76c10_t *dev = (wd76c10_t *)priv;
    uint8_t lk_psw = (uint8_t) ((dev->rtc_disk_cs >> 2) & 0x01);
    uint8_t valxor;

    switch (port) {
        case 0x0092:
            if ((lk_psw && (val & 0x08)) || ((dev->p92 & 0x08) && !(val & 0x08)))
                val = (val & 0xf7) | (dev->p92 & 0x08);

            valxor = (dev->p92 ^ val) & 0x08;
            dev->p92 = (val & 0x08) | 0xf7;

            if (valxor)
                nvr_lock_set(0x38, 0x08, (val & 0x08) ? 0x03 : 0x00, dev->nvr);
            break;
    }
}

static void
wd76c10_outw(uint16_t port, uint16_t val, void *priv)
{
    wd76c10_t *dev = (wd76c10_t *)priv;
    uint8_t inc = (uint8_t) ((dev->ems_ctl >> 15) & 0x01);
    uint8_t ems_en;

    if (!dev->locked || (port < 0x1072) || (port > 0xf872) ||
        (port == 0xe072) || (port == 0xe872) || (port == 0xf073))  switch (port) {
        case 0x1072:
            dev->cpuclk = val;
            break;

        case 0x1872:
            dev->fpu_ctl = val;
            break;

        case 0x2072:
            dev->ser_par_cs = val;
            wd76c10_ser_par_cs_recalc(dev);
            break;

        case 0x2872:
            dev->rtc_disk_cs = val;
            wd76c10_disk_cs_recalc(dev);
            break;

        case 0x3072:
            dev->prog_cs = val;
            break;

        /* TODO: Log this to determine how the BIOS does bank sizing. */
        case 0x3872:
            dev->mem_ctl = val;
            wd76c10_banks_recalc(dev);
            break;

        case 0x4072:
            dev->npmdmt = val;
            break;

        /* A17-A24 */
        case 0x4872:
            dev->bank_base_regs[0] = val;
            wd76c10_banks_recalc(dev);
            break;

        /* A17-A24 */
        case 0x5072:
            dev->bank_base_regs[1] = val;
            wd76c10_banks_recalc(dev);
            break;

        case 0x5872:
            dev->split_sa = val;
            wd76c10_banks_recalc(dev);
            wd76c10_split_recalc(dev);
            break;

        case 0x6072:
            dev->sh_wp = val;
            wd76c10_dis_mem_recalc(dev);
            wd76c10_pf_loc_reset(dev);
            wd76c10_pf_loc_recalc(dev);
            wd76c10_low_pages_recalc(dev);
            wd76c10_high_mem_wp_recalc(dev);
            break;

        case 0x6872:
            dev->ems_ctl = val;
            wd76c10_pf_loc_reset(dev);
            wd76c10_pf_loc_recalc(dev);
            wd76c10_low_pages_recalc(dev);
            break;

        case 0x8872:
            dev->pmc_in = val;
            break;

        case 0xc072:
            dev->hmwpb = val;
            wd76c10_high_mem_wp_recalc(dev);
            break;

        case 0xe072:
            dev->ems_pp = val;
            dev->ep = (val & 0x3f) % 40;
            break;

        case 0xe872:
            ems_en = (uint8_t) ((dev->ems_ctl >> 10) & 0x03);
            if (ems_en) {
                dev->ems_page_regs[dev->ep] = val;
                dev->ems_pages[dev->ep].phys = ((uint32_t) (val & 0x0fff)) << 14;
                if (dev->ep >= 32) {
                    dev->ems_pages[dev->ep].enabled = !!(val & 0x8000);
                    if (ems_en >= 0x02) {
                        wd76c10_pf_loc_reset(dev);
                        wd76c10_pf_loc_recalc(dev);
                    }
                } else {
                    dev->ems_pages[dev->ep].enabled = (ems_en == 0x03);
                    if (ems_en == 0x03)
                        wd76c10_low_pages_recalc(dev);
                }
            }
            if (inc)
                dev->ep = (dev->ep + 1) % 40;
            break;

        case 0xf073:
            dev->locked = ((val & 0x00ff) != 0x00da);
            break;

        case 0xf872:
            flushmmucache();
            break;
    }
}

static uint8_t
wd76c10_inb(uint16_t port, void *priv)
{
    wd76c10_t *dev = (wd76c10_t *)priv;
    uint8_t ret = 0xff;

    switch (port) {
        case 0x0092:
            ret = (dev->p92 & 0x08) | 0xf7;
            break;
    }

    return ret;
}

static uint16_t
wd76c10_inw(uint16_t port, void *priv)
{
    wd76c10_t *dev = (wd76c10_t *)priv;
    uint8_t inc = (uint8_t) ((dev->ems_ctl >> 15) & 0x01);
    uint16_t ret = 0xffff;

    wd76c10_log("WD76C10: R dev->regs[%04x]\n", port);

    if (!dev->locked || (port < 0x1072) || (port > 0xf872) ||
        (port == 0xe072) || (port == 0xe872) || (port == 0xf073))  switch (port) {
        case 0x1072:
            ret = dev->cpuclk;
            break;

        case 0x1872:
            ret = dev->fpu_ctl;
            break;

        case 0x2072:
            ret = dev->ser_par_cs;
            break;

        case 0x2872:
            ret = dev->rtc_disk_cs;
            break;

        case 0x3072:
            ret = dev->prog_cs;
            break;

        case 0x3872:
            ret = dev->mem_ctl;
            break;

        case 0x4072:
            ret = dev->npmdmt;
            break;

        case 0x4872:
            ret = dev->bank_base_regs[0];
            break;

        case 0x5072:
            ret = dev->bank_base_regs[1];
            break;

        case 0x5872:
            ret = dev->split_sa;
            break;

        case 0x6072:
            ret = dev->sh_wp;
            break;

        case 0x6872:
            ret = dev->ems_ctl;
            break;

        case 0x8872:
            ret = dev->pmc_in;
            break;

        case 0xb872:
            ret = dma[0].mode;
            ret |= (((uint16_t) dma[1].mode) << 8);
            break;

        case 0xc072:
            ret = dev->hmwpb;
            break;

        case 0xd072:
            ret = (serial_read(0x0002, dev->uart[0]) & 0xc0) << 8;
            ret |= (serial_read(0x0002, dev->uart[1]) & 0xc0) << 6;
            ret |= (lpt_read_port(0, 0x0002) & 0x0f) << 8;
            ret |= lpt_read_port(0, 0x0000);
            break;

        case 0xe072:
            ret = (dev->ems_pp & 0xffc0) | dev->ep;
            break;

        case 0xe872:
            ret = dev->ems_page_regs[dev->ep];
            if (inc)
                dev->ep = (dev->ep + 1) % 40;
            break;

        case 0xfc72:
            ret = ((lpt_read_status(0) & 0x20) >> 2);
            ret |= (((uint16_t) dma_m) << 4);
            ret |= dev->toggle;
            dev->toggle ^= 0x8000;
            break;
    }

    return ret;
}

static void
wd76c10_close(void *priv)
{
    wd76c10_t *dev = (wd76c10_t *)priv;

    free(dev);
}


static void
wd76c10_reset(void *priv)
{
    wd76c10_t *dev = (wd76c10_t *)priv;

    dev->locked = 1;
    dev->toggle = 0;

    dev->p92 = 0xf7;

    dev->cpuclk = 0x1000;
    dev->fpu_ctl = 0x00ca;
    dev->mem_ctl = 0x0000;
    dev->bank_base_regs[0] = 0x0000;
    dev->bank_base_regs[1] = 0x0000;
    dev->split_sa = 0x0000;
    dev->sh_wp = 0x0000;
    dev->hmwpb = 0x0000;
    dev->npmdmt = 0x0000;
    dev->ems_ctl = 0x0000;
    dev->ems_pp = 0x0000;
    dev->ser_par_cs = 0x0000;
    dev->rtc_disk_cs = 0x0000;

    for (uint8_t i = 0; i < 40; i++) {
        dev->ems_page_regs[i] = 0x0000;
        dev->ems_pages[i].enabled = 0;
        dev->ems_pages[i].phys = 0x00000000;
    }

    nvr_lock_set(0x38, 0x08, 0x00, dev->nvr);

    wd76c10_banks_recalc(dev);
    wd76c10_split_recalc(dev);
    wd76c10_dis_mem_recalc(dev);
    wd76c10_high_mem_wp_recalc(dev);
    wd76c10_pf_loc_reset(dev);
    wd76c10_pf_loc_recalc(dev);
    wd76c10_low_pages_recalc(dev);
    wd76c10_ser_par_cs_recalc(dev);
    wd76c10_disk_cs_recalc(dev);
}


static void *
wd76c10_init(UNUSED(const device_t *info))
{
    wd76c10_t *dev = (wd76c10_t *) calloc(1, sizeof(wd76c10_t));
    uint32_t total_mem = mem_size << 10;
    uint32_t accum_mem = 0x00000000;
    ram_bank_t *rb = NULL;

    /* Calculate the physical RAM banks. */
    for (uint8_t i = 0; i < 4; i++) {
        rb = &(dev->ram_banks[i]);
        uint32_t size = 0x00000000;
        for (int8_t j = 3; j >= 0; j--) {
            uint32_t *bs = &(bank_sizes[j]);
            if (*bs <= total_mem) {
                size = *bs;
                break;
            }
        }
        if (size != 0x00000000) {
            rb->phys_addr = accum_mem;
            rb->phys_size = size;
            total_mem -= size;
            accum_mem += size;
        }
    }

    rb = &(dev->ram_banks[4]);
    rb->phys_addr = 0x000a0000;
    rb->phys_size = 0x00060000;

    memset(dev->mem_pages, 0xff, sizeof(dev->mem_pages));
    for (uint8_t i = 0x008; i < 0x01f; i++)
        dev->mem_pages[i] = i;
    for (uint8_t i = 0x020; i < 0x027; i++)
        dev->mem_pages[i] = i - 0x20;

    device_add(&port_92_inv_device);
    dev->nvr = device_add(&amstrad_megapc_nvr_device);
    dev->uart[0] = device_add_inst(&ns16450_device, 1);
    dev->uart[1] = device_add_inst(&ns16450_device, 2);
    dev->fdc = device_add(&fdc_at_device);
    device_add(&ide_isa_device);

    wd76c10_reset(dev);

    /* Password Lock */
    io_sethandler(0x0092, 1, wd76c10_inb, NULL, NULL, wd76c10_outb, NULL, NULL, dev);

    /* Clock Control */
    io_sethandler(0x1072, 1, NULL, wd76c10_inw, NULL, NULL, wd76c10_outw, NULL, dev);

    /* FPU Bus Timing & Power Down Control */
    io_sethandler(0x1872, 1, NULL, wd76c10_inw, NULL, NULL, wd76c10_outw, NULL, dev);

    /* Refresh Control, Serial and Parallel Chip Selects */
    io_sethandler(0x2072, 1, NULL, wd76c10_inw, NULL, NULL, wd76c10_outw, NULL, dev);

    /* RTC, PVGA, 80287 Timing, and Disk Chip Selects */
    io_sethandler(0x2872, 1, NULL, wd76c10_inw, NULL, NULL, wd76c10_outw, NULL, dev);

    /* Programmable Chip Select Address */
    io_sethandler(0x3072, 1, NULL, wd76c10_inw, NULL, NULL, wd76c10_outw, NULL, dev);

    /* Memory Control */
    io_sethandler(0x3872, 1, NULL, wd76c10_inw, NULL, NULL, wd76c10_outw, NULL, dev);

    /* Non-page Mode DRAM Memory Timing */
    io_sethandler(0x4072, 1, NULL, wd76c10_inw, NULL, NULL, wd76c10_outw, NULL, dev);

    /* Bank 1 & 0 Start Address */
    io_sethandler(0x4872, 1, NULL, wd76c10_inw, NULL, NULL, wd76c10_outw, NULL, dev);

    /* Bank 3 & 2 Start Address */
    io_sethandler(0x5072, 1, NULL, wd76c10_inw, NULL, NULL, wd76c10_outw, NULL, dev);

    /* Split Address */
    io_sethandler(0x5872, 1, NULL, wd76c10_inw, NULL, NULL, wd76c10_outw, NULL, dev);

    /* RAM Shadow And Write Protect */
    io_sethandler(0x6072, 1, NULL, wd76c10_inw, NULL, NULL, wd76c10_outw, NULL, dev);

    /* EMS Control And Lower EMS Boundary */
    io_sethandler(0x6872, 1, NULL, wd76c10_inw, NULL, NULL, wd76c10_outw, NULL, dev);

    /* PMC Inputs */
    io_sethandler(0x8872, 1, NULL, wd76c10_inw, NULL, NULL, wd76c10_outw, NULL, dev);

    /* DMA Mode Shadow Register */
    io_sethandler(0xb872, 1, NULL, wd76c10_inw, NULL, NULL, NULL, NULL, dev);

    /* High Memory Write Protect Boundry */
    io_sethandler(0xc072, 1, NULL, wd76c10_inw, NULL, NULL, wd76c10_outw, NULL, dev);

    /* Shadow Register */
    io_sethandler(0xd072, 1, NULL, wd76c10_inw, NULL, NULL, NULL, NULL, dev);

    /* EMS Page Register Pointer */
    io_sethandler(0xe072, 1, NULL, wd76c10_inw, NULL, NULL, wd76c10_outw, NULL, dev);

    /* EMS Page Register */
    io_sethandler(0xe872, 1, NULL, wd76c10_inw, NULL, NULL, wd76c10_outw, NULL, dev);

    /* Lock/Unlock Configuration */
    io_sethandler(0xf073, 1, NULL, NULL, NULL, NULL, wd76c10_outw, NULL, dev);

    /* Cache Flush */
    io_sethandler(0xf872, 1, NULL, NULL, NULL, NULL, wd76c10_outw, NULL, dev);

    /* Lock Status */
    io_sethandler(0xfc72, 1, NULL, wd76c10_inw, NULL, NULL, NULL, NULL, dev);

    dma_ext_mode_init();

    mem_mapping_add(&dev->ram_mapping,
                    0x00000000,
                    (mem_size + 384) << 10,
                    wd76c10_read_ram,
                    wd76c10_read_ramw,
                    NULL,
                    wd76c10_write_ram,
                    wd76c10_write_ramw,
                    NULL,
                    ram,
                    MEM_MAPPING_INTERNAL,
                    dev);
    mem_mapping_disable(&ram_low_mapping);
    mem_mapping_disable(&ram_mid_mapping);
    mem_mapping_disable(&ram_high_mapping);
    mem_mapping_enable(&dev->ram_mapping);

    memset(dev->ram_state, 0x00, sizeof(dev->ram_state));

    return dev;
}

const device_t wd76c10_device = {
    .name          = "Western Digital WD76C10",
    .internal_name = "wd76c10",
    .flags         = 0,
    .local         = 0,
    .init          = wd76c10_init,
    .close         = wd76c10_close,
    .reset         = NULL,
    .available     = NULL,
    .speed_changed = NULL,
    .force_redraw  = NULL,
    .config        = NULL
};
