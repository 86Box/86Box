/*
 * 86Box	A hypervisor and IBM PC system emulator that specializes in
 *		running old operating systems and software designed for IBM
 *		PC systems and compatibles from 1981 through fairly recent
 *		system designs based on the PCI bus.
 *
 *		This file is part of the 86Box distribution.
 *
 *		Implementation of the SiS 85C50x Chipset.
 *
 *
 *
 * Authors:	Tiseno100,
 *		Miran Grca, <mgrca8@gmail.com>
 *
 *		Copyright 2020,2021 Tiseno100.
 *		Copyright 2020,2021 Miran Grca.
 */
#include <stdarg.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>
#define HAVE_STDARG_H
#include <86box/86box.h>
#include <86box/device.h>
#include <86box/io.h>
#include <86box/timer.h>

#include <86box/apm.h>
#include <86box/mem.h>
#include <86box/smram.h>
#include <86box/pci.h>
#include <86box/port_92.h>

#include <86box/chipset.h>


#ifdef ENABLE_SIS_85C50X_LOG
int sis_85c50x_do_log = ENABLE_SIS_85C50X_LOG;
static void
sis_85c50x_log(const char *fmt, ...)
{
    va_list ap;

    if (sis_85c50x_do_log) {
	va_start(ap, fmt);
	pclog_ex(fmt, ap);
	va_end(ap);
    }
}
#else
#define sis_85c50x_log(fmt, ...)
#endif


typedef struct sis_85c50x_t
{
    uint8_t	index,
		pci_conf[256], pci_conf_sb[256],
		regs[256];

    smram_t *	smram;
    port_92_t *	port_92;
} sis_85c50x_t;


static void
sis_85c50x_shadow_recalc(sis_85c50x_t *dev)
{
    uint32_t base, i, can_read, can_write;

    can_read = (dev->pci_conf[0x53] & 0x40) ? MEM_READ_INTERNAL : MEM_READ_EXTANY;
    can_write = (dev->pci_conf[0x53] & 0x20) ? MEM_WRITE_EXTANY : MEM_WRITE_INTERNAL;
    if (!can_read)
	can_write = MEM_WRITE_EXTANY;

    mem_set_mem_state_both(0xf0000, 0x10000, can_read | can_write);
    shadowbios = 1;
    shadowbios_write = 1;

    for (i = 0; i < 4; i++) {
	base = 0xe0000 + (i << 14);
	mem_set_mem_state_both(base, 0x4000, (dev->pci_conf[0x54] & (1 << (7 - i))) ? (can_read | can_write) : (MEM_READ_EXTANY | MEM_WRITE_EXTANY));
	base = 0xd0000 + (i << 14);
	mem_set_mem_state_both(base, 0x4000, (dev->pci_conf[0x55] & (1 << (7 - i))) ? (can_read | can_write) : (MEM_READ_EXTANY | MEM_WRITE_EXTANY));
	base = 0xc0000 + (i << 14);
	mem_set_mem_state_both(base, 0x4000, (dev->pci_conf[0x56] & (1 << (7 - i))) ? (can_read | can_write) : (MEM_READ_EXTANY | MEM_WRITE_EXTANY));
    }

    flushmmucache_nopc();
}


static void
sis_85c50x_smm_recalc(sis_85c50x_t *dev)
{
    /* NOTE: Naming mismatch - what the datasheet calls "host address" is what we call ram_base. */
    uint32_t ram_base = (dev->pci_conf[0x64] << 20) |
			 ((dev->pci_conf[0x65] & 0x07) << 28);

    smram_disable(dev->smram);

    if ((((dev->pci_conf[0x65] & 0xe0) >> 5) != 0x00) && (ram_base == 0x00000000))
	return;

    switch ((dev->pci_conf[0x65] & 0xe0) >> 5) {
	case 0x00:
		smram_enable(dev->smram, 0xe0000, 0xe0000, 0x8000, (dev->pci_conf[0x65] & 0x10), 1);
		break;
	case 0x01:
		smram_enable(dev->smram, 0xb0000, ram_base, 0x10000, (dev->pci_conf[0x65] & 0x10), 1);
		break;
	case 0x02:
		smram_enable(dev->smram, 0xa0000, ram_base, 0x10000, (dev->pci_conf[0x65] & 0x10), 1);
		break;
	case 0x04:
		smram_enable(dev->smram, 0xa0000, ram_base, 0x8000, (dev->pci_conf[0x65] & 0x10), 1);
		break;
	case 0x06:
		smram_enable(dev->smram, 0xb0000, ram_base, 0x8000, (dev->pci_conf[0x65] & 0x10), 1);
		break;
    }
}


static void
sis_85c50x_write(int func, int addr, uint8_t val, void *priv)
{
    sis_85c50x_t *dev = (sis_85c50x_t *)priv;
    uint8_t valxor = (val ^ dev->pci_conf[addr]);

    switch (addr) {
	case 0x04:	/* Command - low byte */
		dev->pci_conf[addr] = (dev->pci_conf[addr] & 0xb4) | (val & 0x4b);
		break;
	case 0x07:	/* Status - high byte */
		dev->pci_conf[addr] = ((dev->pci_conf[addr] & 0xf9) & ~(val & 0xf8)) | (val & 0x06);
		break;
	case 0x50:
		dev->pci_conf[addr] = val;
		break;
	case 0x51: /* Cache */
		dev->pci_conf[addr] = val;
		cpu_cache_ext_enabled = (val & 0x40);
		cpu_update_waitstates();
		break;
	case 0x52:
		dev->pci_conf[addr] = val;
		break;
	case 0x53: /* Shadow RAM */
	case 0x54:
	case 0x55:
	case 0x56:
		dev->pci_conf[addr] = val;
		sis_85c50x_shadow_recalc(dev);
		if (addr == 0x54)
			sis_85c50x_smm_recalc(dev);
		break;
	case 0x57: case 0x58: case 0x59: case 0x5a:
	case 0x5c: case 0x5d: case 0x5e: case 0x61:
	case 0x62: case 0x63: case 0x67: case 0x68:
	case 0x6a: case 0x6b: case 0x6c: case 0x6d:
	case 0x6e: case 0x6f:
		dev->pci_conf[addr] = val;
		break;
	case 0x5f:
		dev->pci_conf[addr] = val & 0xfe;
		break;
	case 0x5b:
		dev->pci_conf[addr] = val;
		if (valxor & 0xc0)
			port_92_set_features(dev->port_92, !!(val & 0x40), !!(val & 0x80));
		break;
	case 0x60:	/* SMI */
		if ((dev->pci_conf[0x68] & 0x01) && !(dev->pci_conf[addr] & 0x02) && (val & 0x02)) {
			dev->pci_conf[0x69] |= 0x01;
			smi_line = 1;
		}
		dev->pci_conf[addr] = val & 0x3e;
		break;
	case 0x64:	/* SMRAM */
	case 0x65:
		dev->pci_conf[addr] = val;
		sis_85c50x_smm_recalc(dev);
		break;
	case 0x66:
		dev->pci_conf[addr] = (val & 0x7f);
		break;
	case 0x69:
		dev->pci_conf[addr] &= ~(val);
		break;
    }

    sis_85c50x_log("85C501: dev->pci_conf[%02x] = %02x\n", addr, val);
}


static uint8_t
sis_85c50x_read(int func, int addr, void *priv)
{
    sis_85c50x_t *dev = (sis_85c50x_t *)priv;

    sis_85c50x_log("85C501: dev->pci_conf[%02x] (%02x)\n", addr, dev->pci_conf[addr]);

    return dev->pci_conf[addr];
}


static void
sis_85c50x_sb_write(int func, int addr, uint8_t val, void *priv)
{
    sis_85c50x_t *dev = (sis_85c50x_t *)priv;

    switch (addr) {
	case 0x04:	/* Command */
		dev->pci_conf_sb[addr] = val & 0x0f;
		break;
	case 0x07:	/* Status */
		dev->pci_conf_sb[addr] &= ~(val & 0x30);
		break;
	case 0x40:	/* BIOS Control Register */
		dev->pci_conf_sb[addr] = val & 0x3f;
		break;
	case 0x41: case 0x42: case 0x43: case 0x44:
		/* INTA/B/C/D# Remapping Control Register */
		dev->pci_conf_sb[addr] = val & 0x8f;
		if (val & 0x80)
			pci_set_irq_routing(PCI_INTA + (addr - 0x41), PCI_IRQ_DISABLED);
		else
			pci_set_irq_routing(PCI_INTA + (addr - 0x41), val & 0xf);
		break;
	case 0x48:	/* ISA Master/DMA Memory Cycle Control Register 1 */
	case 0x49:	/* ISA Master/DMA Memory Cycle Control Register 2 */
	case 0x4a:	/* ISA Master/DMA Memory Cycle Control Register 3 */
	case 0x4b:	/* ISA Master/DMA Memory Cycle Control Register 4 */
		dev->pci_conf_sb[addr] = val;
		break;
    }

    sis_85c50x_log("85C503: dev->pci_conf_sb[%02x] = %02x\n", addr, val);
}


static uint8_t
sis_85c50x_sb_read(int func, int addr, void *priv)
{
    sis_85c50x_t *dev = (sis_85c50x_t *)priv;
    sis_85c50x_log("85C503: dev->pci_conf_sb[%02x] (%02x)\n", addr, dev->pci_conf_sb[addr]);

    return dev->pci_conf_sb[addr];
}


static void
sis_85c50x_isa_write(uint16_t addr, uint8_t val, void *priv)
{
    sis_85c50x_t *dev = (sis_85c50x_t *)priv;

    switch (addr) {
	case 0x22:
		dev->index = val;
		break;

	case 0x23:
		switch (dev->index) {
			case 0x80:
				dev->regs[dev->index] = val & 0xe7;
				break;
			case 0x81:
				dev->regs[dev->index] = val & 0xf4;
				break;
			case 0x84: case 0x88: case 0x9: case 0x8a:
			case 0x8b:
				dev->regs[dev->index] = val;
				break;
			case 0x85:
				outb(0x70, val);
				break;
		}
		break;
    }

    sis_85c50x_log("85C501-ISA: dev->regs[%02x] = %02x\n", addr, val);
}


static uint8_t
sis_85c50x_isa_read(uint16_t addr, void *priv)
{
    sis_85c50x_t *dev = (sis_85c50x_t *)priv;
    uint8_t ret = 0xff;

    switch (addr) {
	case 0x22:
		ret = dev->index;
		break;

	case 0x23:
		if (dev->index == 0x85)
			ret = inb(0x70);
		else
			ret = dev->regs[dev->index];
		break;
    }

    sis_85c50x_log("85C501-ISA: dev->regs[%02x] (%02x)\n", dev->index, ret);

    return ret;
}


static void
sis_85c50x_reset(void *priv)
{
    sis_85c50x_t *dev = (sis_85c50x_t *)priv;

    /* North Bridge (SiS 85C501/502) */
    dev->pci_conf[0x00] = 0x39;
    dev->pci_conf[0x01] = 0x10;
    dev->pci_conf[0x02] = 0x06;
    dev->pci_conf[0x03] = 0x04;
    dev->pci_conf[0x04] = 0x04;
    dev->pci_conf[0x07] = 0x04;
    dev->pci_conf[0x09] = 0x00;
    dev->pci_conf[0x0a] = 0x00;
    dev->pci_conf[0x0b] = 0x06;

    sis_85c50x_write(0, 0x51, 0x00, dev);
    sis_85c50x_write(0, 0x53, 0x00, dev);
    sis_85c50x_write(0, 0x54, 0x00, dev);
    sis_85c50x_write(0, 0x55, 0x00, dev);
    sis_85c50x_write(0, 0x56, 0x00, dev);
    sis_85c50x_write(0, 0x5b, 0x00, dev);
    sis_85c50x_write(0, 0x60, 0x00, dev);
    sis_85c50x_write(0, 0x64, 0x00, dev);
    sis_85c50x_write(0, 0x65, 0x00, dev);
    sis_85c50x_write(0, 0x68, 0x00, dev);
    sis_85c50x_write(0, 0x69, 0xff, dev);

    /* South Bridge (SiS 85C503) */
    dev->pci_conf_sb[0x00] = 0x39;
    dev->pci_conf_sb[0x01] = 0x10;
    dev->pci_conf_sb[0x02] = 0x08;
    dev->pci_conf_sb[0x03] = 0x00;
    dev->pci_conf_sb[0x04] = 0x07;
    dev->pci_conf_sb[0x05] = 0x00;
    dev->pci_conf_sb[0x06] = 0x00;
    dev->pci_conf_sb[0x07] = 0x02;
    dev->pci_conf_sb[0x08] = 0x00;
    dev->pci_conf_sb[0x09] = 0x00;
    dev->pci_conf_sb[0x0a] = 0x01;
    dev->pci_conf_sb[0x0b] = 0x06;
    sis_85c50x_write(0, 0x41, 0x80, dev);
    sis_85c50x_write(0, 0x42, 0x80, dev);
    sis_85c50x_write(0, 0x43, 0x80, dev);
    sis_85c50x_write(0, 0x44, 0x80, dev);
}


static void
sis_85c50x_close(void *priv)
{
    sis_85c50x_t *dev = (sis_85c50x_t *)priv;

    smram_del(dev->smram);
    free(dev);
}


static void *
sis_85c50x_init(const device_t *info)
{
    sis_85c50x_t *dev = (sis_85c50x_t *)malloc(sizeof(sis_85c50x_t));
    memset(dev, 0x00, sizeof(sis_85c50x_t));

    /* 501/502 (Northbridge) */
    pci_add_card(PCI_ADD_NORTHBRIDGE, sis_85c50x_read, sis_85c50x_write, dev);

    /* 503 (Southbridge) */
    pci_add_card(PCI_ADD_SOUTHBRIDGE, sis_85c50x_sb_read, sis_85c50x_sb_write, dev);
    io_sethandler(0x0022, 0x0002, sis_85c50x_isa_read, NULL, NULL, sis_85c50x_isa_write, NULL, NULL, dev);

    dev->smram = smram_add();
    dev->port_92 = device_add(&port_92_device);

    sis_85c50x_reset(dev);

    return dev;
}

const device_t sis_85c50x_device = {
    .name = "SiS 85C50x",
    .internal_name = "sis_85c50x",
    .flags = DEVICE_PCI,
    .local = 0,
    .init = sis_85c50x_init,
    .close = sis_85c50x_close,
    .reset = sis_85c50x_reset,
    { .available = NULL },
    .speed_changed = NULL,
    .force_redraw = NULL,
    .config = NULL
};
