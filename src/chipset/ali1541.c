/*
 * 86Box	A hypervisor and IBM PC system emulator that specializes in
 *		running old operating systems and software designed for IBM
 *		PC systems and compatibles from 1981 through fairly recent
 *		system designs based on the PCI bus.
 *
 *		This file is part of the 86Box distribution.
 *
 *		Implementation of the ALi M1541/2 CPU-to-PCI Bridge.
 *
 * Authors:	Miran Grca, <mgrca8@gmail.com>
 *
 *		Copyright 2021 Miran Grca.
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


typedef struct ali1541_t
{
    uint8_t	pci_conf[256];

    smram_t *	smram;
    void    *	agp_bridge;
} ali1541_t;


#ifdef ENABLE_ALI1541_LOG
int ali1541_do_log = ENABLE_ALI1541_LOG;
static void
ali1541_log(const char *fmt, ...)
{
    va_list ap;

    if (ali1541_do_log)
    {
        va_start(ap, fmt);
        pclog_ex(fmt, ap);
        va_end(ap);
    }
}
#else
#define ali1541_log(fmt, ...)
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
	}
    }

    flushmmucache_nopc();
}


static void
ali1541_shadow_recalc(int cur_reg, ali1541_t *dev)
{
    int i, bit, r_reg, w_reg;
    uint32_t base, flags = 0;

    shadowbios = shadowbios_write = 0;

    for (i = 0; i < 16; i++) {
	base = 0x000c0000 + (i << 14);
	bit = i & 7;
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
    uint32_t bar, mask;

    switch (dev->pci_conf[0xbc] & 0x0f) {
	case 0x00:
	default:
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

    bar = ((dev->pci_conf[0x13] << 24) | (dev->pci_conf[0x12] << 16)) & mask;
    dev->pci_conf[0x12] = (bar >> 16) & 0xff;
    dev->pci_conf[0x13] = (bar >> 24) & 0xff;
}


static void
ali1541_write(int func, int addr, uint8_t val, void *priv)
{
    ali1541_t *dev = (ali1541_t *)priv;

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

	case 0x2c:	/* Subsystem Vendor ID */
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

	case 0x42:	/* L2 Cache */
		dev->pci_conf[addr] = val;
		cpu_cache_ext_enabled = !!(val & 1);
		cpu_update_waitstates();
		break;

	case 0x43:	/* PLCTL-Pipe Line Control */
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

		if (mem_size > 0xe00000)
			mem_set_mem_state_both(0xe00000, 0x100000, (val & 0x20) ? (MEM_READ_EXTANY | MEM_WRITE_EXTANY) : (MEM_READ_INTERNAL | MEM_WRITE_INTERNAL));

		if (mem_size > 0xf00000)
			mem_set_mem_state_both(0xf00000, 0x100000, (val & 0x10) ? (MEM_READ_EXTANY | MEM_WRITE_EXTANY) : (MEM_READ_INTERNAL | MEM_WRITE_INTERNAL));

		mem_set_mem_state_both(0xa0000, 0x20000, (val & 8) ? (MEM_READ_INTERNAL | MEM_WRITE_INTERNAL) : (MEM_READ_EXTANY | MEM_WRITE_EXTANY));
		mem_set_mem_state_both(0x80000, 0x20000, (val & 4) ? (MEM_READ_EXTANY | MEM_WRITE_EXTANY) : (MEM_READ_INTERNAL | MEM_WRITE_INTERNAL));

		flushmmucache_nopc();
		break;

	case 0x55:	/* SMRAM */
		dev->pci_conf[addr] = val & 0x1f;
		ali1541_smram_recalc(val, dev);
		break;

	case 0x56 ... 0x59:	/* Shadow RAM */
		dev->pci_conf[addr] = val;
		ali1541_shadow_recalc(val, dev);
		break;

	case 0x5a: case 0x5b:
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

	case 0x60 ... 0x6f:	/* DRB's */
		dev->pci_conf[addr] = val;
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

	case 0x84: case 0x85:
		dev->pci_conf[addr] = val;
		break;

	case 0x86:
		dev->pci_conf[addr] = val & 0x0f;
		break;

	case 0x87:	/* H2PO */
		dev->pci_conf[addr] = val;
		/* Find where the Shut-down Special cycle is initiated. */
		// if (!(val & 0x20))
			// outb(0x92, 0x01);
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
	case 0xbe: case 0xbf:
		dev->pci_conf[addr] = val;
		break;

	case 0xc0:
		dev->pci_conf[addr] = val & 0x90;
		break;
	case 0xc1: case 0xc2:
	case 0xc3:
		dev->pci_conf[addr] = val;
		break;

	case 0xc8: case 0xc9:
		dev->pci_conf[addr] = val;
		break;

	case 0xd1:
		dev->pci_conf[addr] = val & 0xf1;
		break;
	case 0xd2: case 0xd3:
		dev->pci_conf[addr] = val;
		break;

	case 0xe0: case 0xe1:
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

	case 0xe8: case 0xe9:
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
    }
}


static uint8_t
ali1541_read(int func, int addr, void *priv)
{
    ali1541_t *dev = (ali1541_t *)priv;
    uint8_t ret = 0xff;

    ret = dev->pci_conf[addr];

    return ret;
}


static void
ali1541_reset(void *priv)
{
    ali1541_t *dev = (ali1541_t *)priv;
    int i;

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
    ali1541_write(0, 0x42, 0x00, dev);

    ali1541_write(0, 0x54, 0x00, dev);
    ali1541_write(0, 0x55, 0x00, dev);

    for (i = 0; i < 4; i++)
	ali1541_write(0, 0x56 + i, 0x00, dev);

    ali1541_write(0, 0x60 + i, 0x07, dev);
    ali1541_write(0, 0x61 + i, 0x40, dev);
    for (i = 0; i < 14; i += 2) {
	ali1541_write(0, 0x62 + i, 0x00, dev);
	ali1541_write(0, 0x63 + i, 0x00, dev);
    }
}


static void
ali1541_close(void *priv)
{
    ali1541_t *dev = (ali1541_t *)priv;

    smram_del(dev->smram);
    free(dev);
}


static void *
ali1541_init(const device_t *info)
{
    ali1541_t *dev = (ali1541_t *)malloc(sizeof(ali1541_t));
    memset(dev, 0, sizeof(ali1541_t));

    pci_add_card(PCI_ADD_NORTHBRIDGE, ali1541_read, ali1541_write, dev);

    dev->smram = smram_add();

    ali1541_reset(dev);

    dev->agp_bridge = device_add(&ali5243_agp_device);

    return dev;
}

const device_t ali1541_device = {
    .name = "ALi M1541 CPU-to-PCI Bridge",
    .internal_name = "ali1541",
    .flags = DEVICE_PCI,
    .local = 0,
    .init = ali1541_init,
    .close = ali1541_close,
    .reset = ali1541_reset,
    { .available = NULL },
    .speed_changed = NULL,
    .force_redraw = NULL,
    .config = NULL
};
