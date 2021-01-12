/*
 * 86Box	A hypervisor and IBM PC system emulator that specializes in
 *		running old operating systems and software designed for IBM
 *		PC systems and compatibles from 1981 through fairly recent
 *		system designs based on the PCI bus.
 *
 *		This file is part of the 86Box distribution.
 *
 *		Implementation of the ALi M1531B CPU-to-PCI Bridge.
 *
 *
 *
 * Authors:	Tiseno100,
 *
 *		Copyright 2021 Tiseno100.
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
#include <86box/timer.h>

#include <86box/device.h>
#include <86box/io.h>
#include <86box/mem.h>
#include <86box/pci.h>
#include <86box/smram.h>
#include <86box/spd.h>

#include <86box/chipset.h>

typedef struct ali1531_t
{
    uint8_t pci_conf[256];

    smram_t *smram;
} ali1531_t;

void ali1531_shadow_recalc(ali1531_t *dev)
{
    for (uint32_t i = 0; i < 8; i++)
    {
        mem_set_mem_state_both(0xc0000 + (i << 14), 0x4000, (((dev->pci_conf[0x4c] >> i) & 1) ? MEM_READ_INTERNAL : MEM_READ_EXTANY) | (((dev->pci_conf[0x4e] >> i) & 1) ? MEM_WRITE_INTERNAL : MEM_WRITE_EXTANY));
        mem_set_mem_state_both(0xe0000 + (i << 14), 0x4000, (((dev->pci_conf[0x4d] >> i) & 1) ? MEM_READ_INTERNAL : MEM_READ_EXTANY) | (((dev->pci_conf[0x4f] >> i) & 1) ? MEM_WRITE_INTERNAL : MEM_WRITE_EXTANY));
    }
}

void ali1531_smm_recalc(uint8_t smm_state, ali1531_t *dev)
{
    if (!!(dev->pci_conf[0x48] & 1))
    {
        switch (smm_state)
        {
        case 0:
            smram_enable(dev->smram, 0xd0000, 0xb0000, 0x10000, 0, 1);
            smram_map(1, 0xd0000, 0x10000, 1);
            break;
        case 1:
            smram_enable(dev->smram, 0xd0000, 0xb0000, 0x10000, 1, 1);
            smram_map(1, 0xd0000, 0x10000, 1);
            break;
        case 2:
            smram_enable(dev->smram, 0xa0000, 0xa0000, 0x20000, 0, 1);
            smram_map(1, 0xa0000, 0x20000, (dev->pci_conf[0x48] & 0x10) ? 2 : 1);
            break;
        case 3:
            smram_enable(dev->smram, 0xa0000, 0xa0000, 0x20000, 1, 1);
            smram_map(1, 0xa0000, 0x20000, (dev->pci_conf[0x48] & 0x10) ? 2 : 1);
            break;
        case 4:
            smram_enable(dev->smram, 0x30000, 0xb0000, 0x10000, 0, 1);
            smram_map(1, 0x30000, 0x10000, 1);
            break;
        case 5:
            smram_enable(dev->smram, 0x30000, 0xb0000, 0x10000, 1, 1);
            smram_map(1, 0x30000, 0x10000, 1);
            break;
        }
        
    }
    else
        smram_disable_all();

    flushmmucache();
}

static void
ali1531_write(int func, int addr, uint8_t val, void *priv)
{
    ali1531_t *dev = (ali1531_t *)priv;

    switch (addr)
    {
    case 0x05:
        dev->pci_conf[addr] = val & 1;
        break;

    case 0x07:
        dev->pci_conf[addr] = val & 0xfe;
        break;

    case 0x0d:
        dev->pci_conf[addr] = val & 0xf8;
        break;

    case 0x40:
        dev->pci_conf[addr] = val & 0xf1;
        break;

    case 0x41:
        dev->pci_conf[addr] = val & 0xdf;
        break;

    case 0x42: /* L2 Cache */
        dev->pci_conf[addr] = val & 0xf7;
        cpu_cache_ext_enabled = !!(val & 1);
        cpu_update_waitstates();
        break;

    case 0x43: /* L1 Cache */
        dev->pci_conf[addr] = val;
        cpu_cache_int_enabled = !!(val & 1);
        cpu_update_waitstates();
        break;

    case 0x47:
        dev->pci_conf[addr] = val & 0xfc;

        if (mem_size > 0xe00000)
            mem_set_mem_state_both(0xe00000, 0x100000, !(val & 0x20) ? (MEM_READ_INTERNAL | MEM_WRITE_INTERNAL) : (MEM_READ_EXTANY | MEM_WRITE_EXTANY));

        if (mem_size > 0xf00000)
            mem_set_mem_state_both(0xf00000, 0x100000, !(val & 0x10) ? (MEM_READ_INTERNAL | MEM_WRITE_INTERNAL) : (MEM_READ_EXTANY | MEM_WRITE_EXTANY));

        mem_set_mem_state_both(0xa0000, 0x20000, (val & 8) ? (MEM_READ_INTERNAL | MEM_WRITE_INTERNAL) : (MEM_READ_EXTANY | MEM_WRITE_EXTANY));
        mem_set_mem_state_both(0x80000, 0x20000, (val & 4) ? (MEM_READ_INTERNAL | MEM_WRITE_INTERNAL) : (MEM_READ_EXTANY | MEM_WRITE_EXTANY));
        break;

    case 0x48: /* SMRAM */
        dev->pci_conf[addr] = val;
        ali1531_smm_recalc((val >> 1) & 7, dev);
        break;

    case 0x49:
        dev->pci_conf[addr] = val & 0x73;
        break;

    case 0x4c: /* Shadow RAM */
    case 0x4d:
    case 0x4e:
    case 0x4f:
        dev->pci_conf[addr] = val;
        ali1531_shadow_recalc(dev);
        break;

    case 0x57: /* H2PO */
        dev->pci_conf[addr] = val & 0x60;
        if (!(val & 0x20))
            outb(0x92, 0x01);
        break;

    case 0x58:
        dev->pci_conf[addr] = val & 0x83;
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

    case 0x60: /* DRB's */
	case 0x61:
	case 0x62:
	case 0x63:
	case 0x64:
	case 0x65:
	case 0x66:
	case 0x67:
	case 0x68:
	case 0x69:
	case 0x6a:
	case 0x6b:
	case 0x6c:
	case 0x6d:
	case 0x6e:
	case 0x6f:
        dev->pci_conf[addr] = val;
        spd_write_drbs(dev->pci_conf, 0x60, 0x6f, 2);
        break;

    case 0x72:
        dev->pci_conf[addr] = val & 0xf;
        break;

    case 0x74:
        dev->pci_conf[addr] = val & 0x2b;
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
        dev->pci_conf[addr] = val;
        break;
    }
}

static uint8_t
ali1531_read(int func, int addr, void *priv)
{
    ali1531_t *dev = (ali1531_t *)priv;
    return dev->pci_conf[addr];
}

static void
ali1531_reset(void *priv)
{
    ali1531_t *dev = (ali1531_t *)priv;

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
    ali1531_shadow_recalc(dev);
    ali1531_write(0, 0x60, 0x08, dev);
    ali1531_write(0, 0x61, 0x40, dev);
}

static void
ali1531_close(void *priv)
{
    ali1531_t *dev = (ali1531_t *)priv;

    smram_del(dev->smram);
    free(dev);
}

static void *
ali1531_init(const device_t *info)
{
    ali1531_t *dev = (ali1531_t *)malloc(sizeof(ali1531_t));
    memset(dev, 0, sizeof(ali1531_t));

    pci_add_card(PCI_ADD_NORTHBRIDGE, ali1531_read, ali1531_write, dev);

    dev->smram = smram_add();

    ali1531_reset(dev);

    return dev;
}

const device_t ali1531_device = {
    "ALi M1531 CPU-to-PCI Bridge",
    DEVICE_PCI,
    0,
    ali1531_init,
    ali1531_close,
    ali1531_reset,
    {NULL},
    NULL,
    NULL,
    NULL};
