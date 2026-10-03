/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          Implementation of the ALi M1541/2 CPU-to-PCI Bridge.
 *
 * Authors: Miran Grca, <mgrca8@gmail.com>
 *
 *          Copyright 2021 Miran Grca.
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

#include <86box/device.h>
#include <86box/io.h>
#include <86box/mem.h>
#include <86box/pci.h>
#include <86box/plat_unused.h>
#include <86box/smram.h>
#include <86box/spd.h>

#include <86box/chipset.h>

typedef struct ali1541_t {
    uint8_t pci_slot;
    uint8_t pad;
    uint8_t pad0;
    uint8_t pad1;

    uint8_t pci_conf[256];

    smram_t *smram;
    void    *agp_bridge;

    int           row_decode;
    int           row_pop[8];
    int           row_ok[8];
    int           phys_sdram[8];
    uint32_t      row_start[8];
    uint32_t      row_end[8];
    uint32_t      phys_base[8];
    uint32_t      phys_size[8];
    mem_mapping_t row_low_mapping;
    mem_mapping_t row_high_mapping;
} ali1541_t;

#ifdef ENABLE_ALI1541_LOG
int ali1541_do_log = ENABLE_ALI1541_LOG;

static void
ali1541_log(const char *fmt, ...)
{
    va_list ap;

    if (ali1541_do_log) {
        va_start(ap, fmt);
        pclog_ex(fmt, ap);
        va_end(ap);
    }
}
#else
#    define ali1541_log(fmt, ...)
#endif

static void
ali1541_smram_recalc(uint8_t val, ali1541_t *dev)
{
    smram_disable_all();

    if (val & 1) {
        switch (val & 0x0c) {
            case 0x00:
                ali1541_log("SMRAM: D0000 -> B0000 (%i)\n", val & 2);
                smram_enable(dev->smram, 0xd0000, 0xb0000, 0x10000, val & 2, 1);
                if (val & 0x10)
                    mem_set_mem_state_smram_ex(1, 0xd0000, 0x10000, 0x02);
                break;
            case 0x04:
                ali1541_log("SMRAM: A0000 -> A0000 (%i)\n", val & 2);
                smram_enable(dev->smram, 0xa0000, 0xa0000, 0x20000, val & 2, 1);
                if (val & 0x10)
                    mem_set_mem_state_smram_ex(1, 0xa0000, 0x20000, 0x02);
                break;
            case 0x08:
                ali1541_log("SMRAM: 30000 -> B0000 (%i)\n", val & 2);
                smram_enable(dev->smram, 0x30000, 0xb0000, 0x10000, val & 2, 1);
                if (val & 0x10)
                    mem_set_mem_state_smram_ex(1, 0x30000, 0x10000, 0x02);
                break;
            default:
                break;
        }
    }

    flushmmucache_nopc();
}

static void
ali1541_shadow_recalc(UNUSED(int cur_reg), ali1541_t *dev)
{
    int      bit;
    int      r_reg;
    int      w_reg;
    uint32_t base;
    uint32_t flags = 0;

    shadowbios = shadowbios_write = 0;

    for (uint8_t i = 0; i < 16; i++) {
        base  = 0x000c0000 + (i << 14);
        bit   = i & 7;
        r_reg = 0x56 + (i >> 3);
        w_reg = 0x58 + (i >> 3);

        flags = (dev->pci_conf[r_reg] & (1 << bit)) ? MEM_READ_INTERNAL : MEM_READ_EXTANY;
        flags |= ((dev->pci_conf[w_reg] & (1 << bit)) ? MEM_WRITE_INTERNAL : MEM_WRITE_EXTANY);

        if (base >= 0x000e0000) {
            if (dev->pci_conf[r_reg] & (1 << bit))
                shadowbios |= 1;
            if (dev->pci_conf[w_reg] & (1 << bit))
                shadowbios_write |= 1;
        }

        ali1541_log("%08X-%08X shadow: R%c, W%c\n", base, base + 0x00003fff,
                    (dev->pci_conf[r_reg] & (1 << bit)) ? 'I' : 'E', (dev->pci_conf[w_reg] & (1 << bit)) ? 'I' : 'E');
        mem_set_mem_state_both(base, 0x00004000, flags);
    }

    flushmmucache_nopc();
}

static void
ali1541_mask_bar(ali1541_t *dev)
{
    uint32_t bar;
    uint32_t mask;

    switch (dev->pci_conf[0xbc] & 0x0f) {
        default:
        case 0x00:
            mask = 0x00000000;
            break;
        case 0x01:
            mask = 0xfff00000;
            break;
        case 0x02:
            mask = 0xffe00000;
            break;
        case 0x03:
            mask = 0xffc00000;
            break;
        case 0x04:
            mask = 0xff800000;
            break;
        case 0x06:
            mask = 0xff000000;
            break;
        case 0x07:
            mask = 0xfe000000;
            break;
        case 0x08:
            mask = 0xfc000000;
            break;
        case 0x09:
            mask = 0xf8000000;
            break;
        case 0x0a:
            mask = 0xf0000000;
            break;
    }

    bar                 = ((dev->pci_conf[0x13] << 24) | (dev->pci_conf[0x12] << 16)) & mask;
    dev->pci_conf[0x12] = (bar >> 16) & 0xff;
    dev->pci_conf[0x13] = (bar >> 24) & 0xff;
}


/* Row-aware DRAM decode, for BIOSes that size memory by probing each row
   (Acer V70MA/V72MA) instead of reading SPD. DBxCI/DBxCII (60h-6Fh) are kept
   as the guest writes them; while they do not describe the installed
   modules, an overlay decodes 0-640K and 1M-512M row by row:
   - a row claims the addresses from the previous row's top up to its own
     top ("not less than" decode); an FPM/EDO row with MA definition 00 is
     disabled;
   - an address in a row maps modulo the physical row size, as the module
     ignores the address lines it does not have;
   - an empty row, or a row whose programmed DRAM type does not suit the
     module (SDRAM driven with FPM/EDO cycles or vice versa), floats high.
     The BIOS tells the DRAM type apart this way.
   Once the guest programs the real layout the overlay turns off and RAM is
   linear again. The MA mapping tables themselves are not modelled. */
extern uint8_t spd_present;
extern spd_t  *spd_modules[SPD_MAX_SLOTS];

static int64_t
ali1541_row_xlate(ali1541_t *dev, uint32_t addr)
{
    for (uint8_t i = 0; i < 8; i++) {
        if (dev->row_pop[i] && (addr >= dev->row_start[i]) && (addr < dev->row_end[i])) {
            if (!dev->row_ok[i])
                return -1;
            return (int64_t) dev->phys_base[i] + ((addr - dev->row_start[i]) % dev->phys_size[i]);
        }
    }

    return -1;
}

static uint8_t
ali1541_row_readb(uint32_t addr, void *priv)
{
    int64_t off = ali1541_row_xlate((ali1541_t *) priv, addr);

    return ((off < 0) || (off >= ((int64_t) mem_size << 10))) ? 0xff : ram[off];
}

static uint16_t
ali1541_row_readw(uint32_t addr, void *priv)
{
    return ali1541_row_readb(addr, priv) | (ali1541_row_readb(addr + 1, priv) << 8);
}

static uint32_t
ali1541_row_readl(uint32_t addr, void *priv)
{
    return ali1541_row_readw(addr, priv) | (ali1541_row_readw(addr + 2, priv) << 16);
}

static void
ali1541_row_writeb(uint32_t addr, uint8_t val, void *priv)
{
    int64_t off = ali1541_row_xlate((ali1541_t *) priv, addr);

    if ((off >= 0) && (off < ((int64_t) mem_size << 10)))
        ram[off] = val;
}

static void
ali1541_row_writew(uint32_t addr, uint16_t val, void *priv)
{
    ali1541_row_writeb(addr, val & 0xff, priv);
    ali1541_row_writeb(addr + 1, val >> 8, priv);
}

static void
ali1541_row_writel(uint32_t addr, uint32_t val, void *priv)
{
    ali1541_row_writew(addr, val & 0xffff, priv);
    ali1541_row_writew(addr + 2, val >> 16, priv);
}

static void
ali1541_row_recalc(ali1541_t *dev)
{
    uint32_t base     = 0;
    uint32_t prev     = 0;
    int      identity = 1;

    /* Physical rows: two per DIMM, from SPD. */
    for (uint8_t i = 0; i < 8; i++) {
        uint32_t size = 0;
        uint8_t  type = SPD_TYPE_SDRAM;

        if (spd_present) {
            if (spd_modules[i >> 1]) {
                size = ((i & 1) ? spd_modules[i >> 1]->row2 : spd_modules[i >> 1]->row1) << 20;
                type = spd_modules[i >> 1]->data[2];
            }
        } else if (!i)
            size = mem_size << 10;

        dev->phys_size[i] = size;
        dev->phys_base[i] = base;
        dev->phys_sdram[i] = (type == SPD_TYPE_SDRAM);
        base += size;
    }

    /* Rows as programmed. */
    for (uint8_t i = 0; i < 8; i++) {
        uint8_t  lo    = dev->pci_conf[0x60 + (i << 1)];
        uint8_t  hi    = dev->pci_conf[0x61 + (i << 1)];
        uint8_t  sdram = !!(hi & 0x20);
        uint32_t top   = ((((hi & 0x0f) << 8) | lo) + 1) << 20;

        dev->row_pop[i] = (sdram || (hi & 0xc0)) && (top > prev);
        if (dev->row_pop[i]) {
            dev->row_start[i] = prev;
            dev->row_end[i]   = top;
            dev->row_ok[i]    = dev->phys_size[i] && (sdram == dev->phys_sdram[i]);
            prev              = top;
            if (!dev->row_ok[i] || (dev->row_start[i] != dev->phys_base[i]) || ((dev->row_end[i] - dev->row_start[i]) != dev->phys_size[i]))
                identity = 0;
        } else if (dev->phys_size[i])
            identity = 0;
    }

    if (identity) {
        mem_mapping_disable(&dev->row_low_mapping);
        mem_mapping_disable(&dev->row_high_mapping);
    } else {
        mem_mapping_enable(&dev->row_low_mapping);
        mem_mapping_enable(&dev->row_high_mapping);
    }
}

static void
ali1541_write(UNUSED(int func), int addr, UNUSED(int len), uint8_t val, void *priv)
{
    ali1541_t *dev = (ali1541_t *) priv;

    switch (addr) {
        case 0x04:
            dev->pci_conf[addr] = val;
            break;
        case 0x05:
            dev->pci_conf[addr] = val & 0x01;
            break;

        case 0x07:
            dev->pci_conf[addr] &= ~(val & 0xf8);
            break;

        case 0x0d:
            dev->pci_conf[addr] = val & 0xf8;
            break;

        case 0x12:
            dev->pci_conf[0x12] = (val & 0xc0);
            ali1541_mask_bar(dev);
            break;
        case 0x13:
            dev->pci_conf[0x13] = val;
            ali1541_mask_bar(dev);
            break;

        case 0x2c: /* Subsystem Vendor ID */
        case 0x2d:
        case 0x2e:
        case 0x2f:
            if (dev->pci_conf[0x90] & 0x01)
                dev->pci_conf[addr] = val;
            break;

        case 0x34:
            if (dev->pci_conf[0x90] & 0x02)
                dev->pci_conf[addr] = val;
            break;

        case 0x40:
            dev->pci_conf[addr] = val & 0x7f;
            break;

        case 0x41:
            dev->pci_conf[addr] = val & 0x7f;
            break;

        case 0x42: /* L2 Cache */
            dev->pci_conf[addr]   = val;
            cpu_cache_ext_enabled = !!(val & 1);
            cpu_update_waitstates();
            break;

        case 0x43: /* PLCTL-Pipe Line Control */
            dev->pci_conf[addr] = val & 0xf7;
            break;

        case 0x44:
            dev->pci_conf[addr] = val;
            break;
        case 0x45:
            dev->pci_conf[addr] = val;
            break;
        case 0x46:
            dev->pci_conf[addr] = val & 0xf0;
            break;
        case 0x47:
            dev->pci_conf[addr] = val;
            break;

        case 0x48:
            dev->pci_conf[addr] = val;
            break;
        case 0x49:
            dev->pci_conf[addr] = val;
            break;

        case 0x4a:
            dev->pci_conf[addr] = val & 0xf8;
            break;

        case 0x4b:
            dev->pci_conf[addr] = val;
            break;

        case 0x4c:
            dev->pci_conf[addr] = val;
            break;
        case 0x4d:
            dev->pci_conf[addr] = val;
            break;

        case 0x4e:
            dev->pci_conf[addr] = val;
            break;
        case 0x4f:
            dev->pci_conf[addr] = val;
            break;

        case 0x50:
            dev->pci_conf[addr] = val & 0x71;
            break;

        case 0x51:
            dev->pci_conf[addr] = val;
            break;

        case 0x52:
            dev->pci_conf[addr] = val;
            break;

        case 0x53:
            dev->pci_conf[addr] = val;
            break;

        case 0x54:
            dev->pci_conf[addr] = val & 0x3c;

            if (mem_size > 0x3800) /* 14 MB, and mem_size counts KiB */
                mem_set_mem_state_both(0xe00000, 0x100000, (val & 0x20) ? (MEM_READ_EXTANY | MEM_WRITE_EXTANY) : (MEM_READ_INTERNAL | MEM_WRITE_INTERNAL));

            if (mem_size > 0x3c00) /* 15 MB */
                mem_set_mem_state_both(0xf00000, 0x100000, (val & 0x10) ? (MEM_READ_EXTANY | MEM_WRITE_EXTANY) : (MEM_READ_INTERNAL | MEM_WRITE_INTERNAL));

            mem_set_mem_state_both(0xa0000, 0x20000, (val & 8) ? (MEM_READ_INTERNAL | MEM_WRITE_INTERNAL) : (MEM_READ_EXTANY | MEM_WRITE_EXTANY));
            mem_set_mem_state_both(0x80000, 0x20000, (val & 4) ? (MEM_READ_EXTANY | MEM_WRITE_EXTANY) : (MEM_READ_INTERNAL | MEM_WRITE_INTERNAL));

            flushmmucache_nopc();
            break;

        case 0x55: /* SMRAM */
            dev->pci_conf[addr] = val & 0x1f;
            ali1541_smram_recalc(val, dev);
            break;

        case 0x56 ... 0x59: /* Shadow RAM */
            dev->pci_conf[addr] = val;
            ali1541_shadow_recalc(val, dev);
            break;

        case 0x5a:
        case 0x5b:
            dev->pci_conf[addr] = val;
            break;

        case 0x5c:
            dev->pci_conf[addr] = val;
            break;

        case 0x5d:
            dev->pci_conf[addr] = val & 0x17;
            break;

        case 0x5e:
            dev->pci_conf[addr] = val;
            break;

        case 0x5f:
            dev->pci_conf[addr] = val & 0xc1;
            break;

        case 0x60 ... 0x6f: /* DRB's */
            dev->pci_conf[addr] = val;
            if (dev->row_decode)
                ali1541_row_recalc(dev);
            else
                spd_write_drbs_interleaved(dev->pci_conf, 0x60, 0x6f, 1);
            break;

        case 0x70:
            dev->pci_conf[addr] = val;
            break;

        case 0x71:
            dev->pci_conf[addr] = val;
            break;

        case 0x72:
            dev->pci_conf[addr] = val & 0xc7;
            break;

        case 0x73:
            dev->pci_conf[addr] = val & 0x1f;
            break;

        case 0x84:
        case 0x85:
            dev->pci_conf[addr] = val;
            break;

        case 0x86:
            dev->pci_conf[addr] = val & 0x0f;
            break;

        case 0x87: /* H2PO */
            dev->pci_conf[addr] = val;
            /* Find where the Shut-down Special cycle is initiated. */
#if 0
            if (!(val & 0x20))
                outb(0x92, 0x01);
#endif
            break;

        case 0x88:
            dev->pci_conf[addr] = val;
            break;

        case 0x89:
            dev->pci_conf[addr] = val;
            break;

        case 0x8a:
            dev->pci_conf[addr] = val;
            break;

        case 0x8b:
            dev->pci_conf[addr] = val & 0x3f;
            break;

        case 0x8c:
            dev->pci_conf[addr] = val;
            break;

        case 0x8d:
            dev->pci_conf[addr] = val;
            break;

        case 0x8e:
            dev->pci_conf[addr] = val;
            break;

        case 0x8f:
            dev->pci_conf[addr] = val;
            break;

        case 0x90:
            dev->pci_conf[addr] = val;
            pci_bridge_set_ctl(dev->agp_bridge, val);
            break;

        case 0x91:
            dev->pci_conf[addr] = val;
            break;

        case 0xb4:
            if (dev->pci_conf[0x90] & 0x01)
                dev->pci_conf[addr] = val & 0x03;
            break;
        case 0xb5:
            if (dev->pci_conf[0x90] & 0x01)
                dev->pci_conf[addr] = val & 0x02;
            break;
        case 0xb7:
            if (dev->pci_conf[0x90] & 0x01)
                dev->pci_conf[addr] = val;
            break;

        case 0xb8:
            dev->pci_conf[addr] = val & 0x03;
            break;
        case 0xb9:
            dev->pci_conf[addr] = val & 0x03;
            break;
        case 0xbb:
            dev->pci_conf[addr] = val;
            break;

        case 0xbc:
            dev->pci_conf[addr] = val & 0x0f;
            ali1541_mask_bar(dev);
            break;
        case 0xbd:
            dev->pci_conf[addr] = val & 0xf0;
            break;
        case 0xbe:
        case 0xbf:
            dev->pci_conf[addr] = val;
            break;

        case 0xc0:
            dev->pci_conf[addr] = val & 0x90;
            break;
        case 0xc1:
        case 0xc2:
        case 0xc3:
            dev->pci_conf[addr] = val;
            break;

        case 0xc8:
        case 0xc9:
            dev->pci_conf[addr] = val;
            break;

        case 0xd1:
            dev->pci_conf[addr] = val & 0xf1;
            break;
        case 0xd2:
        case 0xd3:
            dev->pci_conf[addr] = val;
            break;

        case 0xe0:
        case 0xe1:
            if (dev->pci_conf[0x90] & 0x20)
                dev->pci_conf[addr] = val;
            break;
        case 0xe2:
            if (dev->pci_conf[0x90] & 0x20)
                dev->pci_conf[addr] = val & 0x3f;
            break;
        case 0xe3:
            if (dev->pci_conf[0x90] & 0x20)
                dev->pci_conf[addr] = val & 0xfe;
            break;

        case 0xe4:
            if (dev->pci_conf[0x90] & 0x20)
                dev->pci_conf[addr] = val & 0x03;
            break;
        case 0xe5:
            if (dev->pci_conf[0x90] & 0x20)
                dev->pci_conf[addr] = val;
            break;

        case 0xe6:
            if (dev->pci_conf[0x90] & 0x20)
                dev->pci_conf[addr] = val & 0xc0;
            break;

        case 0xe7:
            if (dev->pci_conf[0x90] & 0x20)
                dev->pci_conf[addr] = val;
            break;

        case 0xe8:
        case 0xe9:
            if (dev->pci_conf[0x90] & 0x04)
                dev->pci_conf[addr] = val;
            break;

        case 0xea:
            dev->pci_conf[addr] = val & 0xcf;
            break;

        case 0xeb:
            dev->pci_conf[addr] = val & 0xcf;
            break;

        case 0xec:
            dev->pci_conf[addr] = val & 0x3f;
            break;

        case 0xed:
            dev->pci_conf[addr] = val;
            break;

        case 0xee:
            dev->pci_conf[addr] = val & 0x3e;
            break;
        case 0xef:
            dev->pci_conf[addr] = val;
            break;

        case 0xf3:
            dev->pci_conf[addr] = val & 0x08;
            break;

        case 0xf5:
            dev->pci_conf[addr] = val;
            break;

        case 0xf6:
            dev->pci_conf[addr] = val;
            break;

        case 0xf7:
            dev->pci_conf[addr] = val & 0x43;
            break;

        default:
            break;
    }
}

static uint8_t
ali1541_read(UNUSED(int func), int addr, UNUSED(int len), void *priv)
{
    const ali1541_t *dev = (ali1541_t *) priv;
    uint8_t          ret = 0xff;

    ret = dev->pci_conf[addr];

    return ret;
}

static void
ali1541_reset(void *priv)
{
    ali1541_t *dev = (ali1541_t *) priv;

    /* Default Registers */
    dev->pci_conf[0x00] = 0xb9;
    dev->pci_conf[0x01] = 0x10;
    dev->pci_conf[0x02] = 0x41;
    dev->pci_conf[0x03] = 0x15;
    dev->pci_conf[0x04] = 0x06;
    dev->pci_conf[0x05] = 0x00;
    dev->pci_conf[0x06] = 0x10;
    dev->pci_conf[0x07] = 0x04;
    dev->pci_conf[0x08] = 0x00;
    dev->pci_conf[0x09] = 0x00;
    dev->pci_conf[0x0a] = 0x00;
    dev->pci_conf[0x0b] = 0x06;
    dev->pci_conf[0x0c] = 0x00;
    dev->pci_conf[0x0d] = 0x20;
    dev->pci_conf[0x0e] = 0x00;
    dev->pci_conf[0x0f] = 0x00;
    dev->pci_conf[0x2c] = 0xb9;
    dev->pci_conf[0x2d] = 0x10;
    dev->pci_conf[0x2e] = 0x41;
    dev->pci_conf[0x2f] = 0x15;
    dev->pci_conf[0x34] = 0xb0;
    dev->pci_conf[0x89] = 0x20;
    dev->pci_conf[0x8a] = 0x20;
    dev->pci_conf[0x91] = 0x13;
    dev->pci_conf[0xb0] = 0x02;
    dev->pci_conf[0xb1] = 0xe0;
    dev->pci_conf[0xb2] = 0x10;
    dev->pci_conf[0xb4] = 0x03;
    dev->pci_conf[0xb5] = 0x02;
    dev->pci_conf[0xb7] = 0x1c;
    dev->pci_conf[0xc8] = 0xbf;
    dev->pci_conf[0xc9] = 0x0a;
    dev->pci_conf[0xe0] = 0x01;

    cpu_cache_int_enabled = 1;
    ali1541_write(0, 0x42, 1, 0x00, dev);

    ali1541_write(0, 0x54, 1, 0x00, dev);
    ali1541_write(0, 0x55, 1, 0x00, dev);

    for (uint8_t i = 0; i < 4; i++)
        ali1541_write(0, 0x56 + i, 1, 0x00, dev);

    ali1541_write(0, 0x60, 1, 0x07, dev);
    ali1541_write(0, 0x61, 1, 0x40, dev);

    for (uint8_t i = 0; i < 14; i += 2) {
        ali1541_write(0, 0x62 + i, 1, 0x00, dev);
        ali1541_write(0, 0x63 + i, 1, 0x00, dev);
    }
}

static void
ali1541_close(void *priv)
{
    ali1541_t *dev = (ali1541_t *) priv;

    smram_del(dev->smram);
    free(dev);
}

static void *
ali1541_init(const device_t *info)
{
    ali1541_t *dev = (ali1541_t *) calloc(1, sizeof(ali1541_t));

    pci_add_card(PCI_ADD_NORTHBRIDGE, ali1541_read, ali1541_write, dev, &dev->pci_slot);

    dev->smram = smram_add();

    dev->row_decode = info->local & 1;
    if (dev->row_decode) {
        mem_mapping_add(&dev->row_low_mapping, 0x00000000, 0x000a0000,
                        ali1541_row_readb, ali1541_row_readw, ali1541_row_readl,
                        ali1541_row_writeb, ali1541_row_writew, ali1541_row_writel,
                        NULL, MEM_MAPPING_INTERNAL, dev);
        mem_mapping_add(&dev->row_high_mapping, 0x00100000, 0x1ff00000,
                        ali1541_row_readb, ali1541_row_readw, ali1541_row_readl,
                        ali1541_row_writeb, ali1541_row_writew, ali1541_row_writel,
                        NULL, MEM_MAPPING_INTERNAL, dev);
        mem_mapping_disable(&dev->row_low_mapping);
        mem_mapping_disable(&dev->row_high_mapping);
        /* Let the overlay see the space above the installed RAM, where
           an oversized row aliases. */
        if ((mem_size << 10) < 0x20000000)
            mem_set_mem_state_both(mem_size << 10, 0x20000000 - (mem_size << 10), MEM_READ_INTERNAL | MEM_WRITE_INTERNAL);
    }

    ali1541_reset(dev);

    dev->agp_bridge = device_add(&ali5243_agp_device);

    return dev;
}

const device_t ali1541_device = {
    .name          = "ALi M1541 CPU-to-PCI Bridge",
    .internal_name = "ali1541",
    .flags         = DEVICE_PCI,
    .local         = 0,
    .init          = ali1541_init,
    .close         = ali1541_close,
    .reset         = ali1541_reset,
    .available     = NULL,
    .speed_changed = NULL,
    .force_redraw  = NULL,
    .config        = NULL
};

const device_t ali1541_rowdecode_device = {
    .name          = "ALi M1541 CPU-to-PCI Bridge",
    .internal_name = "ali1541_rowdecode",
    .flags         = DEVICE_PCI,
    .local         = 1, /* row-aware DRAM decode */
    .init          = ali1541_init,
    .close         = ali1541_close,
    .reset         = ali1541_reset,
    .available     = NULL,
    .speed_changed = NULL,
    .force_redraw  = NULL,
    .config        = NULL
};
