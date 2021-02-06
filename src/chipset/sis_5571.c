/*
 * 86Box	A hypervisor and IBM PC system emulator that specializes in
 *		running old operating systems and software designed for IBM
 *		PC systems and compatibles from 1981 through fairly recent
 *		system designs based on the PCI bus.
 *
 *		This file is part of the 86Box distribution.
 *
 *		Implementation of the SiS 5571 Chipset.
 *
 *
 *
 *	    Authors: Tiseno100,
 *
 *		Copyright 2020 Tiseno100.
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
#include <86box/dma.h>
#include <86box/mem.h>
#include <86box/pci.h>
#include <86box/port_92.h>
#include <86box/hdc_ide.h>
#include <86box/hdc_ide_sff8038i.h>
#include <86box/smram.h>
#include <86box/usb.h>

#include <86box/chipset.h>

#ifdef ENABLE_SIS_5571_LOG
int sis_5571_do_log = ENABLE_SIS_5571_LOG;
static void
sis_5571_log(const char *fmt, ...)
{
	va_list ap;

	if (sis_5571_do_log)
	{
		va_start(ap, fmt);
		pclog_ex(fmt, ap);
		va_end(ap);
	}
}
#else
#define sis_5571_log(fmt, ...)
#endif

typedef struct sis_5571_t
{
	uint8_t pci_conf[256], pci_conf_sb[3][256],
		sb_pci_slot;

	apm_t *apm;
	port_92_t *port_92;
	sff8038i_t *bm[2];
	uint32_t bus_master_base, program_status_pri, program_status_sec;
	smram_t *smram;
	usb_t *usb;

} sis_5571_t;

static void
sis_5571_shadow_recalc(sis_5571_t *dev)
{
	uint32_t i, can_read, can_write;

	for (i = 0; i < 6; i++)
	{
		can_read = (dev->pci_conf[0x70 + (i & 0x07)] & 0x08) ? MEM_READ_INTERNAL : MEM_READ_EXTANY;
		can_write = (dev->pci_conf[0x70 + (i & 0x07)] & 0x02) ? MEM_WRITE_INTERNAL : MEM_WRITE_EXTANY;
		mem_set_mem_state_both(0xc0000 + (0x8000 * (i & 0x07)), 0x4000, can_read | can_write);
		can_read = (dev->pci_conf[0x70 + (i & 0x07)] & 0x80) ? MEM_READ_INTERNAL : MEM_READ_EXTANY;
		can_write = (dev->pci_conf[0x70 + (i & 0x07)] & 0x20) ? MEM_WRITE_INTERNAL : MEM_WRITE_EXTANY;
		mem_set_mem_state_both(0xc4000 + (0x8000 * (i & 0x07)), 0x4000, can_read | can_write);
	}

	can_read = (dev->pci_conf[0x76] & 0x80) ? MEM_READ_INTERNAL : MEM_READ_EXTANY;
	can_write = (dev->pci_conf[0x76] & 0x20) ? MEM_WRITE_INTERNAL : MEM_WRITE_EXTANY;
	shadowbios = !!(dev->pci_conf[0x76] & 0x80);
	shadowbios_write = !!(dev->pci_conf[0x76] & 0x20);
	mem_set_mem_state_both(0xf0000, 0x10000, can_read | can_write);

	flushmmucache();
}

static void
sis_5571_smm_recalc(sis_5571_t *dev)
{
	switch (dev->pci_conf[0xa3] & 0xc0)
	{
	case 0x00:
		if (!dev->pci_conf[0x74])
			smram_enable(dev->smram, 0xe0000, 0xe0000, 0x8000, (dev->pci_conf[0xa3] & 0x10), 1);
		break;
	case 0x01:
		if (!dev->pci_conf[0x74])
			smram_enable(dev->smram, 0xa0000, 0xe0000, 0x8000, (dev->pci_conf[0xa3] & 0x10), 1);
		break;
	case 0x02:
		if (!dev->pci_conf[0x74])
			smram_enable(dev->smram, 0xb0000, 0xe0000, 0x8000, (dev->pci_conf[0xa3] & 0x10), 1);
		break;
	case 0x03:
		smram_enable(dev->smram, 0xa0000, 0xa0000, 0x10000, (dev->pci_conf[0xa3] & 0x10), 1);
		break;
	}

	flushmmucache();
}

static void
sis_5571_ide_handler(void *priv)
{
	sis_5571_t *dev = (sis_5571_t *)priv;
	uint32_t base, side;

	/* IDE IRQ remap */
	if (!(dev->pci_conf_sb[0][0x63] & 0x80))
	{
		sff_set_irq_line(dev->bm[0], dev->pci_conf_sb[0][0x63] & 0x0f);
		sff_set_irq_line(dev->bm[1], dev->pci_conf_sb[0][0x63] & 0x0f);
	}

	/* Compatibility(0)/Native(1) Mode Status Programming */
	if (dev->pci_conf_sb[1][0x08])
		dev->program_status_sec = dev->pci_conf_sb[1][0x09] & 0x04;

	if (dev->pci_conf_sb[1][0x02])
		dev->program_status_pri = dev->pci_conf_sb[1][0x09] & 0x01;

	/* Setting Base/Side */
	if (dev->program_status_pri)
	{
		base = ((dev->pci_conf_sb[1][0x13]) | (dev->pci_conf_sb[1][0x12] << 4) | (dev->pci_conf_sb[1][0x11] << 8) | (dev->pci_conf_sb[1][0x10]) << 12);
		side = ((dev->pci_conf_sb[1][0x17]) | (dev->pci_conf_sb[1][0x16] << 4) | (dev->pci_conf_sb[1][0x15] << 8) | (dev->pci_conf_sb[1][0x14]) << 12);
	}
	else
	{
		base = 0x1f0;
		side = 0x3f6;
	}
	ide_set_base(0, base);
	ide_set_side(0, side);

	if (dev->program_status_sec)
	{
		base = ((dev->pci_conf_sb[1][0x1b]) | (dev->pci_conf_sb[1][0x1a] << 4) | (dev->pci_conf_sb[1][0x19] << 8) | (dev->pci_conf_sb[1][0x18]) << 12);
		side = ((dev->pci_conf_sb[1][0x1f]) | (dev->pci_conf_sb[1][0x1e] << 4) | (dev->pci_conf_sb[1][0x1d] << 8) | (dev->pci_conf_sb[1][0x1c]) << 12);
	}
	else
	{
		base = 0x170;
		side = 0x376;
	}
	ide_set_base(1, base);
	ide_set_side(1, side);

	/* Enable/Disable(Default is Enabled) */
	ide_pri_disable();
	ide_sec_disable();

	if (dev->pci_conf_sb[1][0x4a] & 0x02)
		ide_pri_enable();

	if (dev->pci_conf_sb[1][0x4a] & 0x04)
		ide_sec_enable();

	/* Bus Mastering */
	dev->bus_master_base = ((dev->pci_conf_sb[1][0x23]) | (dev->pci_conf_sb[1][0x22] << 4) | (dev->pci_conf_sb[1][0x21] << 8) | (dev->pci_conf_sb[1][0x20]) << 12);
	sff_bus_master_handler(dev->bm[0], dev->pci_conf_sb[1][0x09] & 0x80, dev->bus_master_base);
	sff_bus_master_handler(dev->bm[1], dev->pci_conf_sb[1][0x09] & 0x80, dev->bus_master_base + 8);
}

static void
sis_5571_usb_handler(void *priv)
{
	sis_5571_t *dev = (sis_5571_t *)priv;

	/* USB Memory Base */
	ohci_update_mem_mapping(dev->usb, dev->pci_conf_sb[2][0x11], dev->pci_conf_sb[2][0x12], dev->pci_conf_sb[2][0x13], dev->pci_conf_sb[0][0x68] & 0x40);

	/* USB I/O Base*/
	uhci_update_io_mapping(dev->usb, dev->pci_conf_sb[2][0x14], dev->pci_conf_sb[2][0x17], dev->pci_conf_sb[0][0x68] & 0x40);
}

static void
memory_pci_bridge_write(int func, int addr, uint8_t val, void *priv)
{
	sis_5571_t *dev = (sis_5571_t *)priv;

	switch (addr)
	{
	case 0x07: /* Status */
		dev->pci_conf[addr] = val & 0xbe;
		break;

	case 0x50:
		dev->pci_conf[addr] = val & 0xec;
		break;

	case 0x51: /* L2 Cache */
		dev->pci_conf[addr] = val;
		cpu_cache_ext_enabled = !!(val & 0x40);
		cpu_update_waitstates();
		break;

	case 0x52:
		dev->pci_conf[addr] = val & 0xd0;
		break;

	case 0x53:
		dev->pci_conf[addr] = val & 0xfe;
		break;

	case 0x55:
		dev->pci_conf[addr] = val & 0xe0;
		break;

	case 0x56:
	case 0x57:
		dev->pci_conf[addr] = val & 0xf8;
		break;

	case 0x5a:
		dev->pci_conf[addr] = val & 0x03;
		break;

	case 0x70: /* Shadow RAM */
	case 0x71:
	case 0x72:
	case 0x73:
	case 0x74:
	case 0x75:
	case 0x76:
		dev->pci_conf[addr] = val & ((addr != 0x76) ? 0xee : 0xe8);
		sis_5571_shadow_recalc(dev);
		sis_5571_smm_recalc(dev);
		break;

	case 0x77:
		dev->pci_conf[addr] = val & 0x0f;
		break;

	case 0x80:
		dev->pci_conf[addr] = val & 0xfe;
		break;

	case 0x81:
		dev->pci_conf[addr] = val & 0xcc;
		break;

	case 0x83:
		dev->pci_conf[addr] = val;
		port_92_set_features(dev->port_92, !!(val & 0x40), !!(val & 0x80));
		break;

	case 0x87:
		dev->pci_conf[addr] = val & 0xf8;
		break;

	case 0x93: /* APM SMI */
		dev->pci_conf[addr] = val;
		apm_set_do_smi(dev->apm, !!((dev->pci_conf[0x9b] & 0x01) && (val & 0x02)));
		if (val & 0x02)
			dev->pci_conf[0x9d] |= 1;
		break;

	case 0x94:
		dev->pci_conf[addr] = val & 0xf8;
		break;

	case 0x95:
	case 0x96:
		dev->pci_conf[addr] = val & 0xfb;
		break;

	case 0xa3: /* SMRAM */
		dev->pci_conf[addr] = val & 0xd0;
		sis_5571_smm_recalc(dev);
		break;

	default:
		dev->pci_conf[addr] = val;
		break;
	}
	sis_5571_log("Memory/PCI Bridge: dev->pci_conf[%02x] = %02x\n", addr, val);
}

static uint8_t
memory_pci_bridge_read(int func, int addr, void *priv)
{
	sis_5571_t *dev = (sis_5571_t *)priv;
	sis_5571_log("Memory/PCI Bridge: dev->pci_conf[%02x] (%02x)\n", addr, dev->pci_conf[addr]);
	return dev->pci_conf[addr];
}

static void
pci_isa_bridge_write(int func, int addr, uint8_t val, void *priv)
{
	sis_5571_t *dev = (sis_5571_t *)priv;
	switch (func)
	{
	case 0: /* Bridge */
		switch (addr)
		{
		case 0x04:
			dev->pci_conf_sb[0][addr] = val & 0x0f;
			break;

		case 0x40:
			dev->pci_conf_sb[0][addr] = val & 0x3f;
			break;

		case 0x41: /* PCI IRQ Routing*/
		case 0x42:
		case 0x43:
		case 0x44:
			dev->pci_conf_sb[0][addr] = val & 0x8f;
			pci_set_irq_routing((addr & 0x07), !(val & 0x80) ? (val & 0x0f) : PCI_IRQ_DISABLED);
			break;

		case 0x45:
		case 0x46:
			dev->pci_conf_sb[0][addr] = val & 0xec;
			break;

		case 0x47:
			dev->pci_conf_sb[0][addr] = val & 0x3e;
			break;

		case 0x5f:
			dev->pci_conf_sb[0][addr] = val & 0x3f;
			break;

		case 0x61: /* MIRQ */
			dev->pci_conf_sb[0][addr] = val;
			pci_set_mirq_routing(PCI_MIRQ0, !(val & 0x80) ? (val & 0x0f) : PCI_IRQ_DISABLED);
			break;

		case 0x62: /* DMA */
			dev->pci_conf_sb[0][addr] = val & 0x0f;
			dma_set_drq((val & 0x07), 1);
			break;

		case 0x63: /* IDE IRQ Remap */
			dev->pci_conf_sb[0][addr] = val & 0x8f;
			sis_5571_ide_handler(dev);
			break;

		case 0x64:
			dev->pci_conf_sb[0][addr] = val & 0xef;
			break;

		case 0x65:
			dev->pci_conf_sb[0][addr] = val & 0x1b;
			break;

		case 0x68: /* USB IRQ Remap */
			dev->pci_conf_sb[0][addr] = val & 0x1b;
			sis_5571_usb_handler(dev);
			break;

		case 0x6a:
			dev->pci_conf_sb[0][addr] = val & 0xfc;
			break;

		case 0x6c:
			dev->pci_conf_sb[0][addr] = val & 0x03;
			break;

		case 0x70:
			dev->pci_conf_sb[0][addr] = val & 0xde;
			break;

		case 0x71:
			dev->pci_conf_sb[0][addr] = val & 0xfe;
			break;

		case 0x72:
		case 0x73:
			dev->pci_conf_sb[0][addr] = (addr == 0x72) ? val & 0xfe : val;
			break;

		default:
			dev->pci_conf_sb[0][addr] = val;
			break;
		}
		sis_5571_log("PCI to ISA Bridge: dev->pci_conf[%02x] = %02x\n", addr, val);
		break;

	case 1: /* IDE Controller */
		switch (addr)
		{
		case 0x04:
			dev->pci_conf_sb[1][addr] = val & 0x05;
			break;

		case 0x09:
			dev->pci_conf_sb[1][addr] = val & 0xcf;
			break;

		default:
			dev->pci_conf_sb[1][addr] = val;
			break;
		}
		sis_5571_log("IDE Controller: dev->pci_conf[%02x] = %02x\n", addr, val);

		if (((addr >= 0x09) && (addr <= 0x23)) || (addr == 0x4a))
			sis_5571_ide_handler(dev);
		break;

	case 2: /* USB Controller */
		switch (addr)
		{
		case 0x05:
			dev->pci_conf_sb[2][addr] = val & 0x03;
			break;

		case 0x06:
			dev->pci_conf_sb[2][addr] = val & 0xc0;
			break;

		default:
			dev->pci_conf_sb[2][addr] = val;
			break;
		}
		sis_5571_log("USB Controller: dev->pci_conf[%02x] = %02x\n", addr, val);

		if ((addr >= 0x11) && (addr <= 0x17))
			sis_5571_usb_handler(dev);
		break;
	}
}

static uint8_t
pci_isa_bridge_read(int func, int addr, void *priv)
{
	sis_5571_t *dev = (sis_5571_t *)priv;

	switch (func)
	{
	case 0:
		sis_5571_log("PCI to ISA Bridge: dev->pci_conf[%02x] (%02x)\n", addr, dev->pci_conf_sb[0][addr]);
		return dev->pci_conf_sb[0][addr];
	case 1:
		sis_5571_log("IDE Controller: dev->pci_conf[%02x] (%02x)\n", addr, dev->pci_conf_sb[1][addr]);
		return dev->pci_conf_sb[1][addr];
	case 2:
		sis_5571_log("USB Controller: dev->pci_conf[%02x] (%02x)\n", addr, dev->pci_conf_sb[2][addr]);
		return dev->pci_conf_sb[2][addr];
	default:
		return 0xff;
	}
}

static void
sis_5571_reset(void *priv)
{
	sis_5571_t *dev = (sis_5571_t *)priv;

	/* Memory/PCI Bridge */
	dev->pci_conf[0x00] = 0x39;
	dev->pci_conf[0x01] = 0x10;
	dev->pci_conf[0x02] = 0x71;
	dev->pci_conf[0x03] = 0x55;
	dev->pci_conf[0x04] = 0xfd;
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

	memory_pci_bridge_write(0, 0x51, 0x00, dev);
	memory_pci_bridge_write(0, 0x70, 0x00, dev);
	memory_pci_bridge_write(0, 0x71, 0x00, dev);
	memory_pci_bridge_write(0, 0x72, 0x00, dev);
	memory_pci_bridge_write(0, 0x73, 0x00, dev);
	memory_pci_bridge_write(0, 0x74, 0x00, dev);
	memory_pci_bridge_write(0, 0x75, 0x00, dev);
	memory_pci_bridge_write(0, 0x76, 0x00, dev);
	memory_pci_bridge_write(0, 0x93, 0x00, dev);
	memory_pci_bridge_write(0, 0xa3, 0x00, dev);

	/* PCI to ISA bridge */
	dev->pci_conf_sb[0][0x00] = 0x39;
	dev->pci_conf_sb[0][0x01] = 0x10;
	dev->pci_conf_sb[0][0x02] = 0x08;
	dev->pci_conf_sb[0][0x03] = 0x00;
	dev->pci_conf_sb[0][0x04] = 0xfd;
	dev->pci_conf_sb[0][0x05] = 0x00;
	dev->pci_conf_sb[0][0x06] = 0x00;
	dev->pci_conf_sb[0][0x07] = 0x00;
	dev->pci_conf_sb[0][0x08] = 0x01;
	dev->pci_conf_sb[0][0x09] = 0x00;
	dev->pci_conf_sb[0][0x0a] = 0x01;
	dev->pci_conf_sb[0][0x0b] = 0x06;
	dev->pci_conf_sb[0][0x0c] = 0x00;
	dev->pci_conf_sb[0][0x0d] = 0x00;
	dev->pci_conf_sb[0][0x0e] = 0x00;
	dev->pci_conf_sb[0][0x0f] = 0x00;

	pci_isa_bridge_write(0, 0x41, 0x80, dev);
	pci_isa_bridge_write(0, 0x42, 0x80, dev);
	pci_isa_bridge_write(0, 0x43, 0x80, dev);
	pci_isa_bridge_write(0, 0x44, 0x80, dev);
	pci_isa_bridge_write(0, 0x61, 0x80, dev);
	pci_isa_bridge_write(0, 0x62, 0x80, dev);
	dev->pci_conf_sb[0][0x63] = 0x80;

	/* IDE Controller */
	dev->pci_conf_sb[1][0x00] = 0x39;
	dev->pci_conf_sb[1][0x01] = 0x10;
	dev->pci_conf_sb[1][0x02] = 0x13;
	dev->pci_conf_sb[1][0x03] = 0x55;
	dev->pci_conf_sb[1][0x04] = 0x00;
	dev->pci_conf_sb[1][0x05] = 0x00;
	dev->pci_conf_sb[1][0x06] = 0x00;
	dev->pci_conf_sb[1][0x07] = 0x00;
	dev->pci_conf_sb[1][0x08] = 0xc0;
	dev->pci_conf_sb[1][0x09] = 0x00;
	dev->pci_conf_sb[1][0x0a] = 0x01;
	dev->pci_conf_sb[1][0x0b] = 0x01;
	dev->pci_conf_sb[1][0x0c] = 0x00;
	dev->pci_conf_sb[1][0x0d] = 0x00;
	dev->pci_conf_sb[1][0x0e] = 0x80;
	dev->pci_conf_sb[1][0x0f] = 0x00;
	dev->pci_conf_sb[1][0x4a] = 0x06;

	sff_bus_master_reset(dev->bm[0], dev->bus_master_base);
	sff_bus_master_reset(dev->bm[1], dev->bus_master_base + 8);

	sff_set_slot(dev->bm[0], dev->sb_pci_slot);
	sff_set_irq_pin(dev->bm[0], PCI_INTA);

	sff_set_slot(dev->bm[1], dev->sb_pci_slot);
	sff_set_irq_pin(dev->bm[1], PCI_INTA);

	sis_5571_ide_handler(dev);

	/* USB Controller */
	dev->pci_conf_sb[2][0x00] = 0x39;
	dev->pci_conf_sb[2][0x01] = 0x10;
	dev->pci_conf_sb[2][0x02] = 0x01;
	dev->pci_conf_sb[2][0x03] = 0x70;
	dev->pci_conf_sb[2][0x04] = 0x00;
	dev->pci_conf_sb[2][0x05] = 0x00;
	dev->pci_conf_sb[2][0x06] = 0x00;
	dev->pci_conf_sb[2][0x07] = 0x00;
	dev->pci_conf_sb[2][0x08] = 0xb0;
	dev->pci_conf_sb[2][0x09] = 0x10;
	dev->pci_conf_sb[2][0x0a] = 0x03;
	dev->pci_conf_sb[2][0x0b] = 0xc0;
	dev->pci_conf_sb[2][0x0c] = 0x00;
	dev->pci_conf_sb[2][0x0d] = 0x00;
	dev->pci_conf_sb[2][0x0e] = 0x80;
	dev->pci_conf_sb[2][0x0f] = 0x00;
	dev->pci_conf_sb[2][0x14] = 0x01;
	dev->pci_conf_sb[2][0x3d] = 0x01;

	sis_5571_usb_handler(dev);
}

static void
sis_5571_close(void *priv)
{
	sis_5571_t *dev = (sis_5571_t *)priv;

	smram_del(dev->smram);
	free(dev);
}

static void *
sis_5571_init(const device_t *info)
{
	sis_5571_t *dev = (sis_5571_t *)malloc(sizeof(sis_5571_t));
	memset(dev, 0x00, sizeof(sis_5571_t));

	pci_add_card(PCI_ADD_NORTHBRIDGE, memory_pci_bridge_read, memory_pci_bridge_write, dev);
	dev->sb_pci_slot = pci_add_card(PCI_ADD_SOUTHBRIDGE, pci_isa_bridge_read, pci_isa_bridge_write, dev);

	/* APM */
	dev->apm = device_add(&apm_pci_device);

	/* DMA */
	dma_alias_set();

	/* MIRQ */
	pci_enable_mirq(0);

	/* Port 92 & SMRAM */
	dev->port_92 = device_add(&port_92_pci_device);
	dev->smram = smram_add();

	/* SFF IDE */
	dev->bm[0] = device_add_inst(&sff8038i_device, 1);
	dev->bm[1] = device_add_inst(&sff8038i_device, 2);
	dev->program_status_pri = 0;
	dev->program_status_sec = 0;

	/* USB */
	dev->usb = device_add(&usb_device);

	sis_5571_reset(dev);

	return dev;
}

const device_t sis_5571_device = {
	"SiS 5571",
	DEVICE_PCI,
	0,
	sis_5571_init,
	sis_5571_close,
	sis_5571_reset,
	{NULL},
	NULL,
	NULL,
	NULL};
