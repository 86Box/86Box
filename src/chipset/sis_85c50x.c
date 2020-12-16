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
 *	    Authors: Tiseno100,
 *
 *		Copyright 2020 Tiseno100.
 */

#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>
#include <86box/86box.h>
#include <86box/device.h>
#include <86box/io.h>
#include <86box/timer.h>

#include <86box/apm.h>
#include <86box/mem.h>
#include <86box/smram.h>
#include <86box/pci.h>

#include <86box/chipset.h>

#ifdef ENABLE_SIS_85C50X_LOG
int sis_85c50x_do_log = ENABLE_SIS_85C50X_LOG;
static void
sis_85c50x_log(const char *fmt, ...)
{
	va_list ap;

	if (sis_85c50x_do_log)
	{
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
	uint8_t pci_conf[256], pci_conf_sb[256];

	apm_t *apm;
	smram_t *smram;
} sis_85c50x_t;

static void
sis_85c50x_shadow_recalc(sis_85c50x_t *dev)
{
	uint32_t base, i, can_read, can_write;

	can_read = (dev->pci_conf[0x53] & 0x40) ? MEM_READ_INTERNAL : MEM_READ_EXTANY;
	can_write = (dev->pci_conf[0x53] & 0x20) ? MEM_WRITE_EXTANY : MEM_WRITE_INTERNAL;

	mem_set_mem_state_both(0xf0000, 0x10000, can_read | can_write);

	for (i = 0; i < 4; i++)
	{
		base = 0xd0000 - ((i + 1) << 14);
		mem_set_mem_state_both(base, 0x4000, (dev->pci_conf[0x56] & (1 << (i + 4))) ? (can_read | can_write) : (MEM_READ_EXTANY | MEM_WRITE_EXTANY));
		mem_set_mem_state_both(base + 0x10000, 0x4000, (dev->pci_conf[0x55] & (1 << (i + 4))) ? (can_read | can_write) : (MEM_READ_EXTANY | MEM_WRITE_EXTANY));
		mem_set_mem_state_both(base + 0x20000, 0x4000, (dev->pci_conf[0x54] & (1 << (i + 4))) ? (can_read | can_write) : (MEM_READ_EXTANY | MEM_WRITE_EXTANY));
	}
	flushmmucache();
}

static void
sis_85c50x_smm_recalc(sis_85c50x_t *dev)
{
	//Note: Host Address determination is unclear
	switch ((dev->pci_conf[0x65] & 0xe0) >> 5)
	{
	case 0x00:
		if (dev->pci_conf[0x54] == 0x00)
			smram_enable(dev->smram, 0xa0000, 0xe0000, 0x10000, (dev->pci_conf[0x65] & 0x10), 1);
		break;
	case 0x01:
		smram_enable(dev->smram, 0xa0000, 0xb0000, 0x10000, (dev->pci_conf[0x65] & 0x10), 1);
		break;
	case 0x02:
		smram_enable(dev->smram, 0xa0000, 0xa0000, 0x10000, (dev->pci_conf[0x65] & 0x10), 1);
		break;
	case 0x04:
		smram_enable(dev->smram, 0xa0000, 0xa0000, 0x8000, (dev->pci_conf[0x65] & 0x10), 1);
		break;
	case 0x06:
		smram_enable(dev->smram, 0xa0000, 0xb0000, 0x8000, (dev->pci_conf[0x65] & 0x10), 1);
		break;
	}
}

static void
sis_85c50x_write(int func, int addr, uint8_t val, void *priv)
{

	sis_85c50x_t *dev = (sis_85c50x_t *)priv;

	dev->pci_conf[addr] = val;
	switch (addr)
	{
	case 0x51:
		cpu_cache_ext_enabled = (val & 0x40);
		break;

	case 0x53:
	case 0x54:
	case 0x55:
	case 0x56:
		sis_85c50x_shadow_recalc(dev);
		break;

	case 0x60:
		apm_set_do_smi(dev->apm, (val & 0x02));
		break;

	case 0x64:
	case 0x65:
		sis_85c50x_smm_recalc(dev);
		break;
	}
	sis_85c50x_log("85C501: dev->pci_conf[%02x] = %02x", addr, val);
}

static uint8_t
sis_85c50x_read(int func, int addr, void *priv)
{
	sis_85c50x_t *dev = (sis_85c50x_t *)priv;
	sis_85c50x_log("85C501: dev->pci_conf[%02x] (%02x)", addr, dev->pci_conf[addr]);
	return dev->pci_conf[addr];
}

static void
sis_85c50x_sb_write(int func, int addr, uint8_t val, void *priv)
{

	sis_85c50x_t *dev = (sis_85c50x_t *)priv;

	dev->pci_conf_sb[addr] = val;

	switch (addr)
	{
	case 0x41:
		pci_set_irq_routing(PCI_INTA, (val & 0x80) ? (val & 0x0f) : PCI_IRQ_DISABLED);
		break;
	case 0x42:
		pci_set_irq_routing(PCI_INTB, (val & 0x80) ? (val & 0x0f) : PCI_IRQ_DISABLED);
		break;
	case 0x43:
		pci_set_irq_routing(PCI_INTC, (val & 0x80) ? (val & 0x0f) : PCI_IRQ_DISABLED);
		break;
	case 0x44:
		pci_set_irq_routing(PCI_INTD, (val & 0x80) ? (val & 0x0f) : PCI_IRQ_DISABLED);
		break;
	}
	sis_85c50x_log("85C503: dev->pci_conf_sb[%02x] = %02x", addr, val);
}

static uint8_t
sis_85c50x_sb_read(int func, int addr, void *priv)
{
	sis_85c50x_t *dev = (sis_85c50x_t *)priv;
	sis_85c50x_log("85C503: dev->pci_conf_sb[%02x] (%02x)", addr, dev->pci_conf_sb[addr]);
	return dev->pci_conf_sb[addr];
}

static void
sis_85c50x_reset(void *priv)
{
	sis_85c50x_t *dev = (sis_85c50x_t *)priv;

	/* North Bridge */
	dev->pci_conf[0x00] = 0x39;
	dev->pci_conf[0x01] = 0x10;
	dev->pci_conf[0x02] = 0x06;
	dev->pci_conf[0x03] = 0x04;
	dev->pci_conf[0x04] = 0x04;
	dev->pci_conf[0x05] = 0x00;
	dev->pci_conf[0x06] = 0x00;
	dev->pci_conf[0x07] = 0x00;
	dev->pci_conf[0x08] = 0x00;
	dev->pci_conf[0x09] = 0x00;
	dev->pci_conf[0x0a] = 0x00;
	dev->pci_conf[0x0b] = 0x06;
	dev->pci_conf[0x0c] = 0x00;
	dev->pci_conf[0x0d] = 0x00;
	dev->pci_conf[0x0e] = 0x00;
	dev->pci_conf[0x0f] = 0x00;

	sis_85c50x_write(0, 0x51, 0x00, dev);
	sis_85c50x_write(0, 0x53, 0x00, dev);
	sis_85c50x_write(0, 0x54, 0x00, dev);
	sis_85c50x_write(0, 0x55, 0x00, dev);
	sis_85c50x_write(0, 0x56, 0x00, dev);
	sis_85c50x_write(0, 0x60, 0x00, dev);
	sis_85c50x_write(0, 0x64, 0x00, dev);
	sis_85c50x_write(0, 0x65, 0x00, dev);

	/* South Bridge */
	dev->pci_conf_sb[0x00] = 0x39;
	dev->pci_conf_sb[0x01] = 0x10;
	dev->pci_conf_sb[0x02] = 0x08;
	dev->pci_conf_sb[0x03] = 0x00;
	dev->pci_conf_sb[0x04] = 0x07;
	dev->pci_conf_sb[0x05] = 0x00;
	dev->pci_conf_sb[0x06] = 0x00;
	dev->pci_conf_sb[0x07] = 0x00;
	dev->pci_conf_sb[0x08] = 0x00;
	dev->pci_conf_sb[0x09] = 0x00;
	dev->pci_conf_sb[0x0a] = 0x01;
	dev->pci_conf_sb[0x0b] = 0x06;
	dev->pci_conf_sb[0x0c] = 0x00;
	dev->pci_conf_sb[0x0d] = 0x00;
	dev->pci_conf_sb[0x0e] = 0x00;
	dev->pci_conf_sb[0x0f] = 0x00;
	sis_85c50x_write(0, 0x41, 0x00, dev);
	sis_85c50x_write(0, 0x42, 0x00, dev);
	sis_85c50x_write(0, 0x43, 0x00, dev);
	sis_85c50x_write(0, 0x44, 0x00, dev);
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
	memset(dev, 0, sizeof(sis_85c50x_t));

	pci_add_card(PCI_ADD_NORTHBRIDGE, sis_85c50x_read, sis_85c50x_write, dev);
	pci_add_card(PCI_ADD_SOUTHBRIDGE, sis_85c50x_sb_read, sis_85c50x_sb_write, dev);
	dev->apm = device_add(&apm_pci_device);
	dev->smram = smram_add();
	sis_85c50x_reset(dev);

	return dev;
}

const device_t sis_85c50x_device = {
	"SiS 85C50x",
	DEVICE_PCI,
	0,
	sis_85c50x_init,
	sis_85c50x_close,
	sis_85c50x_reset,
	{NULL},
	NULL,
	NULL,
	NULL};
