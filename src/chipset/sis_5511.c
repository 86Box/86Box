/*
 * 86Box	A hypervisor and IBM PC system emulator that specializes in
 *		running old operating systems and software designed for IBM
 *		PC systems and compatibles from 1981 through fairly recent
 *		system designs based on the PCI bus.
 *
 *		This file is part of the 86Box distribution.
 *
 *		Implementation of the SiS 5511/5512/5513 Pentium PCI/ISA Chipset.
 *
 *
 *
 * Authors:	Tiseno100,
 *
 *		Copyright 2021 Tiseno100.
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

#include <86box/mem.h>
#include <86box/hdd.h>
#include <86box/hdc.h>
#include <86box/hdc_ide.h>
#include <86box/hdc_ide_sff8038i.h>
#include <86box/pci.h>
#include <86box/port_92.h>
#include <86box/smram.h>

#include <86box/chipset.h>

/* IDE Flags (1 Native / 0 Compatibility)*/
#define PRIMARY_COMP_NAT_SWITCH (dev->pci_conf_sb[1][9] & 1)
#define SECONDARY_COMP_NAT_SWITCH (dev->pci_conf_sb[1][9] & 4)
#define PRIMARY_NATIVE_BASE (dev->pci_conf_sb[1][0x11] << 8) | (dev->pci_conf_sb[1][0x10] & 0xf8)
#define PRIMARY_NATIVE_SIDE (((dev->pci_conf_sb[1][0x15] << 8) | (dev->pci_conf_sb[1][0x14] & 0xfc)) + 2)
#define SECONDARY_NATIVE_BASE (dev->pci_conf_sb[1][0x19] << 8) | (dev->pci_conf_sb[1][0x18] & 0xf8)
#define SECONDARY_NATIVE_SIDE (((dev->pci_conf_sb[1][0x1d] << 8) | (dev->pci_conf_sb[1][0x1c] & 0xfc)) + 2)
#define BUS_MASTER_BASE ((dev->pci_conf_sb[1][0x20] & 0xf0) | (dev->pci_conf_sb[1][0x21] << 8))

#ifdef ENABLE_SIS_5511_LOG
int sis_5511_do_log = ENABLE_SIS_5511_LOG;
static void
sis_5511_log(const char *fmt, ...)
{
	va_list ap;

	if (sis_5511_do_log)
	{
		va_start(ap, fmt);
		pclog_ex(fmt, ap);
		va_end(ap);
	}
}
#else
#define sis_5511_log(fmt, ...)
#endif

typedef struct sis_5511_t
{
	uint8_t pci_conf[256], pci_conf_sb[2][256],
		index, regs[16];

	int nb_pci_slot, sb_pci_slot;

	sff8038i_t *ide_drive[2];
	smram_t *smram;
	port_92_t *port_92;

} sis_5511_t;

static void
sis_5511_shadow_recalc(int cur_reg, sis_5511_t *dev)
{
	if (cur_reg == 0x86)
		mem_set_mem_state_both(0xf0000, 0x10000, ((dev->pci_conf[cur_reg] & 0x80) ? MEM_READ_INTERNAL : MEM_READ_EXTANY) | ((dev->pci_conf[cur_reg] & 0x20) ? MEM_WRITE_INTERNAL : MEM_WRITE_EXTANY));
	else
	{
		mem_set_mem_state_both(0xc0000 + ((cur_reg & 7) << 15), 0x4000, ((dev->pci_conf[cur_reg] & 0x80) ? MEM_READ_INTERNAL : MEM_READ_EXTANY) | ((dev->pci_conf[cur_reg] & 0x20) ? MEM_WRITE_INTERNAL : MEM_WRITE_EXTANY));
		mem_set_mem_state_both(0xc4000 + ((cur_reg & 7) << 15), 0x4000, ((dev->pci_conf[cur_reg] & 8) ? MEM_READ_INTERNAL : MEM_READ_EXTANY) | ((dev->pci_conf[cur_reg] & 2) ? MEM_WRITE_INTERNAL : MEM_WRITE_EXTANY));
	}

	flushmmucache_nopc();
}

static void
sis_5511_smram_recalc(sis_5511_t *dev)
{
	smram_disable_all();

		switch (dev->pci_conf[0x65] >> 6)
		{
		case 0:
			smram_enable(dev->smram, 0x000e0000, 0x000e0000, 0x8000, dev->pci_conf[0x65] & 0x10, 1);
			break;
		case 1:
			smram_enable(dev->smram, 0x000e0000, 0x000a0000, 0x8000, dev->pci_conf[0x65] & 0x10, 1);
			break;
		case 2:
			smram_enable(dev->smram, 0x000e0000, 0x000b0000, 0x8000, dev->pci_conf[0x65] & 0x10, 1);
			break;
		}

	flushmmucache();
}

void sis_5513_ide_handler(sis_5511_t *dev)
{
	ide_pri_disable();
	ide_sec_disable();
	if (dev->pci_conf_sb[1][4] & 1)
	{
		if (dev->pci_conf_sb[1][0x4a] & 4)
		{
			ide_set_base(0, PRIMARY_COMP_NAT_SWITCH ? PRIMARY_NATIVE_BASE : 0x1f0);
			ide_set_side(0, PRIMARY_COMP_NAT_SWITCH ? PRIMARY_NATIVE_SIDE : 0x3f6);
			ide_pri_enable();
		}
		if (dev->pci_conf_sb[1][0x4a] & 2)
		{
			ide_set_base(1, SECONDARY_COMP_NAT_SWITCH ? SECONDARY_NATIVE_BASE : 0x170);
			ide_set_side(1, SECONDARY_COMP_NAT_SWITCH ? SECONDARY_NATIVE_SIDE : 0x376);
			ide_sec_enable();
		}
	}
}

void sis_5513_bm_handler(sis_5511_t *dev)
{
	sff_bus_master_handler(dev->ide_drive[0], dev->pci_conf_sb[1][4] & 4, BUS_MASTER_BASE);
	sff_bus_master_handler(dev->ide_drive[1], dev->pci_conf_sb[1][4] & 4, BUS_MASTER_BASE + 8);
}

static void
sis_5511_write(int func, int addr, uint8_t val, void *priv)
{
	sis_5511_t *dev = (sis_5511_t *)priv;

	switch (addr)
	{
	case 0x04: /* Command - low  byte */
		dev->pci_conf[addr] = val;
		break;

	case 0x05: /* Command - high  byte */
		dev->pci_conf[addr] = val;
		break;

	case 0x06: /* Status - Low Byte */
		dev->pci_conf[addr] &= val;
		break;

	case 0x07: /* Status - High Byte */
		dev->pci_conf[addr] &= 0x16;
		break;

	case 0x50:
		dev->pci_conf[addr] = (val & 0xf9) | 4;
		cpu_cache_ext_enabled = !!(val & 0x40);
		cpu_update_waitstates();
		break;

	case 0x51:
		dev->pci_conf[addr] = val & 0xfe;
		break;

	case 0x52:
		dev->pci_conf[addr] = val & 0x3f;
		break;

	case 0x53:
	case 0x54:
		dev->pci_conf[addr] = val;
		break;

	case 0x55:
		dev->pci_conf[addr] = val & 0xf8;
		break;

	case 0x57:
	case 0x58:
	case 0x59:
		dev->pci_conf[addr] = val;
		break;

	case 0x5a:
		dev->pci_conf[addr] = val;
		port_92_set_features(dev->port_92, !!(val & 0x40), !!(val & 0x80));
		break;

	case 0x5b:
		dev->pci_conf[addr] = val & 0xf7;
		break;

	case 0x5c:
		dev->pci_conf[addr] = val & 0xcf;
		break;

	case 0x5d:
		dev->pci_conf[addr] = val;
		break;

	case 0x5e:
		dev->pci_conf[addr] = val & 0xfe;
		break;

	case 0x5f:
		dev->pci_conf[addr] = val;
		break;

	case 0x60:
		dev->pci_conf[addr] = val & 0x3e;
		if (!!(val & 2) && (dev->pci_conf[0x68] & 1))
		{
			smi_line = 1;
			dev->pci_conf[0x69] |= 1;
		}
		break;

	case 0x61: /* STPCLK# Assertion Timer */
	case 0x62: /* STPCLK# De-assertion Timer */
	case 0x63: /* System Standby Timer */
	case 0x64:
		dev->pci_conf[addr] = val;
		break;

	case 0x65:
		dev->pci_conf[addr] = val & 0xd0;
		sis_5511_smram_recalc(dev);
		break;

	case 0x66:
		dev->pci_conf[addr] = val & 0x7f;
		break;

	case 0x67:
	case 0x68:
		dev->pci_conf[addr] = val;
		break;

	case 0x69:
		dev->pci_conf[addr] &= val;
		break;

	case 0x6a:
	case 0x6b:
	case 0x6c:
	case 0x6d:
	case 0x6e:
		dev->pci_conf[addr] = val;
		break;

	case 0x6f:
		dev->pci_conf[addr] = val & 0x3f;
		break;

	case 0x70: /* DRAM Bank Register 0-0 */
	case 0x71: /* DRAM Bank Register 0-0 */
	case 0x72: /* DRAM Bank Register 0-1 */
		dev->pci_conf[addr] = val;
		break;

	case 0x73: /* DRAM Bank Register 0-1 */
		dev->pci_conf[addr] = val & 0x83;
		break;

	case 0x74: /* DRAM Bank Register 1-0 */
		dev->pci_conf[addr] = val;
		break;

	case 0x75: /* DRAM Bank Register 1-0 */
		dev->pci_conf[addr] = val & 0x7f;
		break;

	case 0x76: /* DRAM Bank Register 1-1 */
		dev->pci_conf[addr] = val;
		break;

	case 0x77: /* DRAM Bank Register 1-1 */
		dev->pci_conf[addr] = val & 0x83;
		break;

	case 0x78: /* DRAM Bank Register 2-0 */
		dev->pci_conf[addr] = val;
		break;

	case 0x79: /* DRAM Bank Register 2-0 */
		dev->pci_conf[addr] = val & 0x7f;
		break;

	case 0x7a: /* DRAM Bank Register 2-1 */
		dev->pci_conf[addr] = val;
		break;

	case 0x7b: /* DRAM Bank Register 2-1 */
		dev->pci_conf[addr] = val & 0x83;
		break;

	case 0x7c: /* DRAM Bank Register 3-0 */
		dev->pci_conf[addr] = val;
		break;

	case 0x7d: /* DRAM Bank Register 3-0 */
		dev->pci_conf[addr] = val & 0x7f;
		break;

	case 0x7e: /* DRAM Bank Register 3-1 */
		dev->pci_conf[addr] = val;
		break;

	case 0x7f: /* DRAM Bank Register 3-1 */
		dev->pci_conf[addr] = val & 0x83;
		break;

	case 0x80:
	case 0x81:
	case 0x82:
	case 0x83:
	case 0x84:
	case 0x85:
	case 0x86:
		dev->pci_conf[addr] = val & ((addr == 0x86) ? 0xe8 : 0xee);
		sis_5511_shadow_recalc(addr, dev);
		sis_5511_smram_recalc(dev);
		break;

	case 0x90: /* 5512 General Purpose Register Index */
	case 0x91: /* 5512 General Purpose Register Index */
	case 0x92: /* 5512 General Purpose Register Index */
	case 0x93: /* 5512 General Purpose Register Index */
		dev->pci_conf[addr] = val;
		break;
	}
	sis_5511_log("SiS 5511: dev->pci_conf[%02x] = %02x POST: %02x\n", addr, dev->pci_conf[addr], inb(0x80));
}

static uint8_t
sis_5511_read(int func, int addr, void *priv)
{
	sis_5511_t *dev = (sis_5511_t *)priv;
	sis_5511_log("SiS 5511: dev->pci_conf[%02x] (%02x) POST %02x\n", addr, dev->pci_conf[addr], inb(0x80));
	return dev->pci_conf[addr];
}

void sis_5513_pci_to_isa_write(int addr, uint8_t val, sis_5511_t *dev)
{
	switch (addr)
	{
	case 0x04: /* Command */
		dev->pci_conf_sb[0][addr] = val & 7;
		break;

	case 0x07: /* Status */
		dev->pci_conf_sb[0][addr] &= val & 0x36;
		break;

	case 0x40: /* BIOS Control Register */
		dev->pci_conf_sb[0][addr] = val & 0x3f;
		break;

	case 0x41: /* INTA# Remapping Control Register */
	case 0x42: /* INTB# Remapping Control Register */
	case 0x43: /* INTC# Remapping Control Register */
	case 0x44: /* INTD# Remapping Control Register */
		dev->pci_conf_sb[0][addr] = val & 0x8f;
		pci_set_irq_routing(addr & 7, (val & 0x80) ? (val & 0x80) : PCI_IRQ_DISABLED);
		break;

	case 0x48: /* ISA Master/DMA Memory Cycle Control Register 1 */
	case 0x49: /* ISA Master/DMA Memory Cycle Control Register 2 */
	case 0x4a: /* ISA Master/DMA Memory Cycle Control Register 3 */
	case 0x4b: /* ISA Master/DMA Memory Cycle Control Register 4 */
	case 0x4c:
	case 0x4d:
	case 0x4e:
	case 0x4f:
	case 0x50:
	case 0x51:
	case 0x52:
	case 0x53:
	case 0x54:
	case 0x55:
	case 0x56:
	case 0x57:
	case 0x58:
	case 0x59:
	case 0x5a:
	case 0x5b:
	case 0x5c:
	case 0x5d:
	case 0x5e:
	case 0x5f:
		dev->pci_conf_sb[0][addr] = val;
		break;

	case 0x60: /* MIRQ0 Remapping Control Register */
	case 0x61: /* MIRQ1 Remapping Control Register */
		dev->pci_conf_sb[0][addr] = val & 0xcf;
		pci_set_mirq_routing(addr & 1, (val & 0x80) ? (val & 0x0f) : PCI_IRQ_DISABLED);
		break;

	case 0x62: /* On-board Device DMA Control Register */
		dev->pci_conf_sb[0][addr] = val;
		break;

	case 0x63: /* IDEIRQ Remapping Control Register */
		dev->pci_conf_sb[0][addr] = val & 0x8f;
		if (val & 0x80)
		{
			sff_set_irq_line(dev->ide_drive[0], (val & 0x80) ? (val & 0x0f) : PCI_IRQ_DISABLED);
			sff_set_irq_line(dev->ide_drive[1], (val & 0x80) ? (val & 0x0f) : PCI_IRQ_DISABLED);
		}
		break;

	case 0x64: /* GPIO0 Control Register */
		dev->pci_conf_sb[0][addr] = val & 0xef;
		break;

	case 0x65:
		dev->pci_conf_sb[0][addr] = val & 0x80;
		break;

	case 0x66: /* GPIO0 Output Mode Control Register */
	case 0x67: /* GPIO0 Output Mode Control Register */
		dev->pci_conf_sb[0][addr] = val;
		break;

	case 0x6a: /* GPIO Status Register */
		dev->pci_conf_sb[0][addr] &= val & 0x15;
		break;
	}
}

void sis_5513_ide_write(int addr, uint8_t val, sis_5511_t *dev)
{
	switch (addr)
	{
	case 0x04: /* Command low byte */
		dev->pci_conf_sb[1][addr] = val & 5;
		sis_5513_ide_handler(dev);
		sis_5513_bm_handler(dev);
		break;
	case 0x07: /* Status high byte */
		dev->pci_conf_sb[1][addr] &= val & 0x3f;
		break;
	case 0x09: /* Programming Interface Byte */
		dev->pci_conf_sb[1][addr] = val;
		sis_5513_ide_handler(dev);
		break;
	case 0x0d: /* Latency Timer */
		dev->pci_conf_sb[1][addr] = val;
		break;

	case 0x10: /* Primary Channel Base Address Register */
	case 0x11: /* Primary Channel Base Address Register */
	case 0x12: /* Primary Channel Base Address Register */
	case 0x13: /* Primary Channel Base Address Register */
	case 0x14: /* Primary Channel Base Address Register */
	case 0x15: /* Primary Channel Base Address Register */
	case 0x16: /* Primary Channel Base Address Register */
	case 0x17: /* Primary Channel Base Address Register */
	case 0x18: /* Secondary Channel Base Address Register */
	case 0x19: /* Secondary Channel Base Address Register */
	case 0x1a: /* Secondary Channel Base Address Register */
	case 0x1b: /* Secondary Channel Base Address Register */
	case 0x1c: /* Secondary Channel Base Address Register */
	case 0x1d: /* Secondary Channel Base Address Register */
	case 0x1e: /* Secondary Channel Base Address Register */
	case 0x1f: /* Secondary Channel Base Address Register */
		dev->pci_conf_sb[1][addr] = val;
		sis_5513_ide_handler(dev);
		break;

	case 0x20: /* Bus Master IDE Control Register Base Address */
	case 0x21: /* Bus Master IDE Control Register Base Address */
	case 0x22: /* Bus Master IDE Control Register Base Address */
	case 0x23: /* Bus Master IDE Control Register Base Address */
		dev->pci_conf_sb[1][addr] = val;
		sis_5513_bm_handler(dev);
		break;

	case 0x30: /* Expansion ROM Base Address */
	case 0x31: /* Expansion ROM Base Address */
	case 0x32: /* Expansion ROM Base Address */
	case 0x33: /* Expansion ROM Base Address */
		dev->pci_conf_sb[1][addr] = val;
		break;

	case 0x40: /* IDE Primary Channel/Master Drive Data Recovery Time Control */
	case 0x41: /* IDE Primary Channel/Master Drive DataActive Time Control */
	case 0x42: /* IDE Primary Channel/Slave Drive Data Recovery Time Control */
	case 0x43: /* IDE Primary Channel/Slave Drive Data Active Time Control */
	case 0x44: /* IDE Secondary Channel/Master Drive Data Recovery Time Control */
	case 0x45: /* IDE Secondary Channel/Master Drive Data Active Time Control */
	case 0x46: /* IDE Secondary Channel/Slave Drive Data Recovery Time Control */
	case 0x47: /* IDE Secondary Channel/Slave Drive Data Active Time Control */
	case 0x48: /* IDE Command Recovery Time Control */
	case 0x49: /* IDE Command Active Time Control */
		dev->pci_conf_sb[1][addr] = val;
		break;

	case 0x4a: /* IDE General Control Register 0 */
		dev->pci_conf_sb[1][addr] = val & 0x9f;
		sis_5513_ide_handler(dev);
		break;

	case 0x4b: /* IDE General Control Register 1 */
		dev->pci_conf_sb[1][addr] = val & 0xef;
		break;

	case 0x4c: /* Prefetch Count of Primary Channel (Low Byte) */
	case 0x4d: /* Prefetch Count of Primary Channel (High Byte) */
	case 0x4e: /* Prefetch Count of Secondary Channel (Low Byte) */
	case 0x4f: /* Prefetch Count of Secondary Channel (High Byte) */
		dev->pci_conf_sb[1][addr] = val;
		break;
	}
}

static void
sis_5513_write(int func, int addr, uint8_t val, void *priv)
{
	sis_5511_t *dev = (sis_5511_t *)priv;
	switch (func)
	{
	case 0:
		sis_5513_pci_to_isa_write(addr, val, dev);
		break;
	case 1:
		sis_5513_ide_write(addr, val, dev);
		break;
	}
	sis_5511_log("SiS 5513: dev->pci_conf[%02x][%02x] = %02x POST: %02x\n", func, addr, dev->pci_conf_sb[func][addr], inb(0x80));
}

static uint8_t
sis_5513_read(int func, int addr, void *priv)
{
	sis_5511_t *dev = (sis_5511_t *)priv;

	sis_5511_log("SiS 5513: dev->pci_conf[%02x][%02x] = %02x POST %02x\n", func, addr, dev->pci_conf_sb[func][addr], inb(0x80));
	if ((func >= 0) && (func <= 1))
		return dev->pci_conf_sb[func][addr];
	else
		return 0xff;
}

static void
sis_5513_isa_write(uint16_t addr, uint8_t val, void *priv)
{
	sis_5511_t *dev = (sis_5511_t *)priv;

	switch (addr)
	{
	case 0x22:
		dev->index = val - 0x50;
		break;
	case 0x23:
		switch (dev->index)
		{
		case 0x00:
			dev->regs[dev->index] = val & 0xed;
			switch (val >> 6)
			{
			case 0:
				cpu_set_isa_speed(7159091);
				break;
			case 1:
				cpu_set_isa_pci_div(4);
				break;
			case 2:
				cpu_set_isa_pci_div(3);
				break;
			}
			break;
		case 0x01:
			dev->regs[dev->index] = val & 0xf4;
			break;
		case 0x03:
			dev->regs[dev->index] = val & 3;
			break;
		case 0x04: /* BIOS Register */
			dev->regs[dev->index] = val;
			break;
		case 0x05:
			dev->regs[dev->index] = inb(0x70);
			break;
		case 0x08:
		case 0x09:
		case 0x0a:
		case 0x0b:
			dev->regs[dev->index] = val;
			break;
		}
		sis_5511_log("SiS 5513-ISA: dev->regs[%02x] = %02x POST: %02x\n", dev->index + 0x50, dev->regs[dev->index], inb(0x80));
		break;
	}
}

static uint8_t
sis_5513_isa_read(uint16_t addr, void *priv)
{
	sis_5511_t *dev = (sis_5511_t *)priv;

	if (addr == 0x23)
	{
		sis_5511_log("SiS 5513-ISA: dev->regs[%02x] (%02x) POST: %02x\n", dev->index + 0x50, dev->regs[dev->index], inb(0x80));
		return dev->regs[dev->index];
	}
	else
		return 0xff;
}

static void
sis_5511_reset(void *priv)
{
	sis_5511_t *dev = (sis_5511_t *)priv;

	/* SiS 5511 */
	dev->pci_conf[0x00] = 0x39;
	dev->pci_conf[0x01] = 0x10;
	dev->pci_conf[0x02] = 0x11;
	dev->pci_conf[0x03] = 0x55;
	dev->pci_conf[0x04] = 7;
	dev->pci_conf[0x07] = 2;
	dev->pci_conf[0x0b] = 6;
	dev->pci_conf[0x52] = 0x20;
	dev->pci_conf[0x61] = 0xff;
	dev->pci_conf[0x62] = 0xff;
	dev->pci_conf[0x63] = 0xff;
	dev->pci_conf[0x67] = 0xff;
	dev->pci_conf[0x6b] = 0xff;
	dev->pci_conf[0x6c] = 0xff;
	dev->pci_conf[0x70] = 4;
	dev->pci_conf[0x72] = 4;
	dev->pci_conf[0x73] = 0x80;
	dev->pci_conf[0x74] = 4;
	dev->pci_conf[0x76] = 4;
	dev->pci_conf[0x77] = 0x80;
	dev->pci_conf[0x78] = 4;
	dev->pci_conf[0x7a] = 4;
	dev->pci_conf[0x7b] = 0x80;
	dev->pci_conf[0x7c] = 4;
	dev->pci_conf[0x7e] = 4;
	dev->pci_conf[0x7f] = 0x80;

	/* SiS 5513 */
	dev->pci_conf_sb[0][0x00] = 0x39;
	dev->pci_conf_sb[0][0x01] = 0x10;
	dev->pci_conf_sb[0][0x02] = 8;
	dev->pci_conf_sb[0][0x04] = 7;
	dev->pci_conf_sb[0][0x0a] = 1;
	dev->pci_conf_sb[0][0x0b] = 6;
	dev->pci_conf_sb[0][0x0e] = 0x80;

	/* SiS 5513 IDE Controller */
	dev->pci_conf_sb[1][0x00] = 0x39;
	dev->pci_conf_sb[1][0x01] = 0x10;
	dev->pci_conf_sb[1][0x02] = 0x13;
	dev->pci_conf_sb[1][0x03] = 0x55;
	dev->pci_conf_sb[1][0x0a] = 1;
	dev->pci_conf_sb[1][0x0b] = 1;
	dev->pci_conf_sb[1][0x0e] = 0x80;
	sff_set_slot(dev->ide_drive[0], dev->sb_pci_slot);
	sff_set_slot(dev->ide_drive[1], dev->sb_pci_slot);
    sff_bus_master_reset(dev->ide_drive[0], BUS_MASTER_BASE);
    sff_bus_master_reset(dev->ide_drive[1], BUS_MASTER_BASE + 8);
}

static void
sis_5511_close(void *priv)
{
	sis_5511_t *dev = (sis_5511_t *)priv;

	smram_del(dev->smram);
	free(dev);
}

static void *
sis_5511_init(const device_t *info)
{
	sis_5511_t *dev = (sis_5511_t *)malloc(sizeof(sis_5511_t));
	memset(dev, 0, sizeof(sis_5511_t));

	dev->nb_pci_slot = pci_add_card(PCI_ADD_NORTHBRIDGE, sis_5511_read, sis_5511_write, dev);		   /* Device 0: SiS 5511 */
	dev->sb_pci_slot = pci_add_card(PCI_ADD_SOUTHBRIDGE, sis_5513_read, sis_5513_write, dev);		   /* Device 1: SiS 5513 */
	io_sethandler(0x0022, 0x0002, sis_5513_isa_read, NULL, NULL, sis_5513_isa_write, NULL, NULL, dev); /* Ports 22h-23h: SiS 5513 ISA */

	/* MIRQ */
	pci_enable_mirq(0);
	pci_enable_mirq(1);

	/* Port 92h */
	dev->port_92 = device_add(&port_92_device);

	/* SFF IDE */
	dev->ide_drive[0] = device_add_inst(&sff8038i_device, 1);
	dev->ide_drive[1] = device_add_inst(&sff8038i_device, 2);

	/* SMRAM */
	dev->smram = smram_add();

	sis_5511_reset(dev);

	return dev;
}

const device_t sis_5511_device = {
	"SiS 5511",
	DEVICE_PCI,
	0,
	sis_5511_init,
	sis_5511_close,
	sis_5511_reset,
	{NULL},
	NULL,
	NULL,
	NULL};
