/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          Implementation of the ALi M1531B CPU-to-PCI Bridge.
 *
 *
 *
 * Authors: Tiseno100,
 *
 *          Copyright 2021 Tiseno100.
 */
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>
#define HAVE_STDARG_H
#include <86box/86box.h>
#include <86box/timer.h>

#include <86box/device.h>
#include <86box/io.h>
#include <86box/mem.h>
#include <86box/pci.h>
#include <86box/plat_unused.h>
#include <86box/smram.h>
#include <86box/spd.h>

#include <86box/chipset.h>

typedef struct ali1531_t {
    uint8_t pci_slot;
    uint8_t pad;
    uint8_t pad0;
    uint8_t pad1;

    uint8_t pci_conf[256];

    smram_t *smram;
} ali1531_t;

#ifdef ENABLE_ALI1531_LOG
int ali1531_do_log = ENABLE_ALI1531_LOG;

static void
ali1531_log(const char *fmt, ...)
{
    va_list ap;

    if (ali1531_do_log) {
        va_start(ap, fmt);
        pclog_ex(fmt, ap);
        va_end(ap);
    }
}
#else
#    define ali1531_log(fmt, ...)
#endif

static void
ali1531_smram_recalc(uint8_t val, ali1531_t *dev)
{
    smram_disable_all();

    if (val & 1) {
        switch (val & 0x0c) {
            case 0x00:
                ali1531_log("SMRAM: D0000 -> B0000 (%i)\n", val & 2);
                smram_enable(dev->smram, 0xd0000, 0xb0000, 0x10000, val & 2, 1);
                if (val & 0x10)
                    mem_set_mem_state_smram_ex(1, 0xd0000, 0x10000, 0x02);
                break;
            case 0x04:
                ali1531_log("SMRAM: A0000 -> A0000 (%i)\n", val & 2);
                smram_enable(dev->smram, 0xa0000, 0xa0000, 0x20000, val & 2, 1);
                if (val & 0x10)
                    mem_set_mem_state_smram_ex(1, 0xa0000, 0x20000, 0x02);
                break;
            case 0x08:
                ali1531_log("SMRAM: 30000 -> B0000 (%i)\n", val & 2);
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
ali1531_shadow_recalc(UNUSED(int cur_reg), ali1531_t *dev)
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
        r_reg = 0x4c + (i >> 3);
        w_reg = 0x4e + (i >> 3);

        flags = (dev->pci_conf[r_reg] & (1 << bit)) ? MEM_READ_INTERNAL : MEM_READ_EXTANY;
        flags |= ((dev->pci_conf[w_reg] & (1 << bit)) ? MEM_WRITE_INTERNAL : MEM_WRITE_EXTANY);

        if (base >= 0x000e0000) {
            if (dev->pci_conf[r_reg] & (1 << bit))
                shadowbios |= 1;
            if (dev->pci_conf[w_reg] & (1 << bit))
                shadowbios_write |= 1;
        }

        ali1531_log("%08X-%08X shadow: R%c, W%c\n", base, base + 0x00003fff,
                    (dev->pci_conf[r_reg] & (1 << bit)) ? 'I' : 'E', (dev->pci_conf[w_reg] & (1 << bit)) ? 'I' : 'E');
        mem_set_mem_state_both(base, 0x00004000, flags);
    }

    flushmmucache_nopc();
}

static void
ali1531_write(UNUSED(int func), int addr, uint8_t val, void *priv)
{
    ali1531_t *dev = (ali1531_t *) priv;

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

        case 0x2c: /* Subsystem Vendor ID */
        case 0x2d:
        case 0x2e:
        case 0x2f:
            if (dev->pci_conf[0x70] & 0x08)
                dev->pci_conf[addr] = val;
            break;

        case 0x40:
            dev->pci_conf[addr] = val & 0xf1;
            break;

        case 0x41:
            dev->pci_conf[addr] = (val & 0xd6) | 0x08;
            break;

        case 0x42: /* L2 Cache */
            dev->pci_conf[addr]   = val & 0xf7;
            cpu_cache_ext_enabled = !!(val & 1);
            cpu_update_waitstates();
            break;

        case 0x43: /* L1 Cache */
            dev->pci_conf[addr]   = val;
            cpu_cache_int_enabled = !!(val & 1);
            cpu_update_waitstates();
            break;

        case 0x44:
            dev->pci_conf[addr] = val;
            break;
        case 0x45:
            dev->pci_conf[addr] = val;
            break;

        case 0x46:
            dev->pci_conf[addr] = val;
            break;

        case 0x47:
            dev->pci_conf[addr] = val & 0xfc;

            if (mem_size > 0xe00000)
                mem_set_mem_state_both(0xe00000, 0x100000, (val & 0x20) ? (MEM_READ_EXTANY | MEM_WRITE_EXTANY) : (MEM_READ_INTERNAL | MEM_WRITE_INTERNAL));

            if (mem_size > 0xf00000)
                mem_set_mem_state_both(0xf00000, 0x100000, (val & 0x10) ? (MEM_READ_EXTANY | MEM_WRITE_EXTANY) : (MEM_READ_INTERNAL | MEM_WRITE_INTERNAL));

            mem_set_mem_state_both(0xa0000, 0x20000, (val & 8) ? (MEM_READ_INTERNAL | MEM_WRITE_INTERNAL) : (MEM_READ_EXTANY | MEM_WRITE_EXTANY));
            mem_set_mem_state_both(0x80000, 0x20000, (val & 4) ? (MEM_READ_EXTANY | MEM_WRITE_EXTANY) : (MEM_READ_INTERNAL | MEM_WRITE_INTERNAL));

            flushmmucache_nopc();
            break;

        case 0x48: /* SMRAM */
            dev->pci_conf[addr] = val;
            ali1531_smram_recalc(val, dev);
            break;

        case 0x49:
            dev->pci_conf[addr] = val & 0x73;
            break;

        case 0x4a:
            dev->pci_conf[addr] = val;
            break;

        case 0x4c ... 0x4f: /* Shadow RAM */
            dev->pci_conf[addr] = val;
            ali1531_shadow_recalc(val, dev);
            break;

        case 0x50 ... 0x52:
        case 0x54 ... 0x56:
            dev->pci_conf[addr] = val;
            break;

        case 0x57: /* H2PO */
            dev->pci_conf[addr] = val & 0x60;
            /* Find where the Shut-down Special cycle is initiated. */
#if 0
            if (!(val & 0x20))
                outb(0x92, 0x01);
#endif
            break;

        case 0x58:
            dev->pci_conf[addr] = val & 0x86;
            break;

        case 0x59 ... 0x5a:
        case 0x5c:
            dev->pci_conf[addr] = val;
            break;

        case 0x5b:
            dev->pci_conf[addr] = val & 0x4f;
            break;

        case 0x5d:
            dev->pci_conf[addr] = val & 0x53;
            break;

        case 0x5f:
            dev->pci_conf[addr] = val & 0x7f;
            break;

        case 0x60 ... 0x6f: /* DRB's */
            dev->pci_conf[addr] = val;
            spd_write_drbs_interleaved(dev->pci_conf, 0x60, 0x6f, 1);
            break;

        case 0x70 ... 0x71:
            dev->pci_conf[addr] = val;
            break;

        case 0x72:
            dev->pci_conf[addr] = val & 0x0f;
            break;

        case 0x74:
            dev->pci_conf[addr] = val & 0x2b;
            break;

        case 0x76 ... 0x77:
            dev->pci_conf[addr] = val;
            break;

        case 0x80:
            dev->pci_conf[addr] = val & 0x84;
            break;

        case 0x81:
            dev->pci_conf[addr] = val & 0x81;
            break;

        case 0x83:
            dev->pci_conf[addr] = val & 0x10;
            break;

        default:
            break;
    }
}

static uint8_t
ali1531_read(UNUSED(int func), int addr, void *priv)
{
    const ali1531_t *dev = (ali1531_t *) priv;
    uint8_t          ret = 0xff;

    ret = dev->pci_conf[addr];

    return ret;
}

static void
ali1531_reset(void *priv)
{
    ali1531_t *dev = (ali1531_t *) priv;

    /* Default Registers */
    dev->pci_conf[0x00] = 0xb9;
    dev->pci_conf[0x01] = 0x10;
    dev->pci_conf[0x02] = 0x31;
    dev->pci_conf[0x03] = 0x15;
    dev->pci_conf[0x04] = 0x06;
    dev->pci_conf[0x05] = 0x00;
    dev->pci_conf[0x06] = 0x00;
    dev->pci_conf[0x07] = 0x04;
    dev->pci_conf[0x08] = 0xb0;
    dev->pci_conf[0x09] = 0x00;
    dev->pci_conf[0x0a] = 0x00;
    dev->pci_conf[0x0b] = 0x06;
    dev->pci_conf[0x0c] = 0x00;
    dev->pci_conf[0x0d] = 0x20;
    dev->pci_conf[0x0e] = 0x00;
    dev->pci_conf[0x0f] = 0x00;
    dev->pci_conf[0x2c] = 0xb9;
    dev->pci_conf[0x2d] = 0x10;
    dev->pci_conf[0x2e] = 0x31;
    dev->pci_conf[0x2f] = 0x15;
    dev->pci_conf[0x52] = 0xf0;
    dev->pci_conf[0x54] = 0xff;
    dev->pci_conf[0x55] = 0xff;
    dev->pci_conf[0x59] = 0x20;
    dev->pci_conf[0x5a] = 0x20;
    dev->pci_conf[0x70] = 0x22;

    ali1531_write(0, 0x42, 0x00, dev);
    ali1531_write(0, 0x43, 0x00, dev);

    ali1531_write(0, 0x47, 0x00, dev);
    ali1531_write(0, 0x48, 0x00, dev);

    for (uint8_t i = 0; i < 4; i++)
        ali1531_write(0, 0x4c + i, 0x00, dev);

    for (uint8_t i = 0; i < 16; i += 2) {
        ali1531_write(0, 0x60 + i, 0x08, dev);
        ali1531_write(0, 0x61 + i, 0x40, dev);
    }
}

static void
ali1531_close(void *priv)
{
    ali1531_t *dev = (ali1531_t *) priv;

    smram_del(dev->smram);
    free(dev);
}

static void *
ali1531_init(UNUSED(const device_t *info))
{
    ali1531_t *dev = (ali1531_t *) calloc(1, sizeof(ali1531_t));

    pci_add_card(PCI_ADD_NORTHBRIDGE, ali1531_read, ali1531_write, dev, &dev->pci_slot);

    dev->smram = smram_add();

    ali1531_reset(dev);

    return dev;
}

const device_t ali1531_device = {
    .name          = "ALi M1531 CPU-to-PCI Bridge",
    .internal_name = "ali1531",
    .flags         = DEVICE_PCI,
    .local         = 0,
    .init          = ali1531_init,
    .close         = ali1531_close,
    .reset         = ali1531_reset,
    .available     = NULL,
    .speed_changed = NULL,
    .force_redraw  = NULL,
    .config        = NULL
};
