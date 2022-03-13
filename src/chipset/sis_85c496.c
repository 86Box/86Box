/*
 * 86Box	A hypervisor and IBM PC system emulator that specializes in
 *		running old operating systems and software designed for IBM
 *		PC systems and compatibles from 1981 through fairly recent
 *		system designs based on the PCI bus.
 *
 *		This file is part of the 86Box distribution.
 *
 *		Implementation of the SiS 85c496/85c497 chip.
 *
 *
 *
 * Authors:	Miran Grca, <mgrca8@gmail.com>
 *
 *		Copyright 2019,2020 Miran Grca.
 */
#include <stdarg.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>
#define HAVE_STDARG_H
#include <86box/86box.h>
#include "cpu.h"
#include <86box/mem.h>
#include <86box/smram.h>
#include <86box/io.h>
#include <86box/pci.h>
#include <86box/device.h>
#include <86box/timer.h>
#include <86box/dma.h>
#include <86box/nvr.h>
#include <86box/pic.h>
#include <86box/port_92.h>
#include <86box/hdc_ide.h>
#include <86box/machine.h>
#include <86box/chipset.h>
#include <86box/spd.h>


typedef struct sis_85c496_t
{
    uint8_t	cur_reg, rmsmiblk_count,
		regs[127],
		pci_conf[256];
    smram_t	*smram;
    pc_timer_t	rmsmiblk_timer;
    port_92_t *	port_92;
    nvr_t *	nvr;
} sis_85c496_t;


#ifdef ENABLE_SIS_85C496_LOG
int sis_85c496_do_log = ENABLE_SIS_85C496_LOG;


void
sis_85c496_log(const char *fmt, ...)
{
    va_list ap;

    if (sis_85c496_do_log) {
	va_start(ap, fmt);
	pclog_ex(fmt, ap);
	va_end(ap);
    }
}
#else
#define sis_85c496_log(fmt, ...)
#endif


static void
sis_85c497_isa_write(uint16_t port, uint8_t val, void *priv)
{
    sis_85c496_t *dev = (sis_85c496_t *) priv;

    sis_85c496_log("[%04X:%08X] ISA Write %02X to   %04X\n", CS, cpu_state.pc, val, port);

    if (port == 0x22)
	dev->cur_reg = val;
    else if (port == 0x23)  switch (dev->cur_reg) {
	case 0x01:	/* Built-in 206 Timing Control */
		dev->regs[dev->cur_reg] = val;
		break;
	case 0x70:	/* ISA Bus Clock Selection */
		dev->regs[dev->cur_reg] = val & 0xc0;
		break;
	case 0x71:	/* ISA Bus Timing Control */
		dev->regs[dev->cur_reg] = val & 0xf6;
		break;
	case 0x72: case 0x76:	/* SMOUT */
	case 0x74:	/* BIOS Timer */
		dev->regs[dev->cur_reg] = val;
		break;
	case 0x73:	/* BIOS Timer */
		dev->regs[dev->cur_reg] = val & 0xfd;
		break;
	case 0x75:	/* DMA / Deturbo Control */
		dev->regs[dev->cur_reg] = val & 0xfc;
		dma_set_mask((val & 0x80) ? 0xffffffff : 0x00ffffff);
		break;
    }
}


static uint8_t
sis_85c497_isa_read(uint16_t port, void *priv)
{
    sis_85c496_t *dev = (sis_85c496_t *) priv;
    uint8_t ret = 0xff;

    if (port == 0x23)
	ret = dev->regs[dev->cur_reg];
    else if (port == 0x33)
	ret = 0x3c /*random_generate()*/;

    sis_85c496_log("[%04X:%08X] ISA Read  %02X from %04X\n", CS, cpu_state.pc, ret, port);

    return ret;
}


static void
sis_85c496_recalcmapping(sis_85c496_t *dev)
{
    uint32_t base;
    uint32_t i, shflags = 0;

    shadowbios = 0;
    shadowbios_write = 0;

    for (i = 0; i < 8; i++) {
	base = 0xc0000 + (i << 15);

	if (dev->pci_conf[0x44] & (1 << i)) {
		shadowbios |= (base >= 0xe0000) && (dev->pci_conf[0x45] & 0x02);
		shadowbios_write |= (base >= 0xe0000) && !(dev->pci_conf[0x45] & 0x01);
		shflags = (dev->pci_conf[0x45] & 0x02) ? MEM_READ_INTERNAL : MEM_READ_EXTANY;
		shflags |= (dev->pci_conf[0x45] & 0x01) ? MEM_WRITE_EXTANY : MEM_WRITE_INTERNAL;
		mem_set_mem_state_both(base, 0x8000, shflags);
	} else
		mem_set_mem_state_both(base, 0x8000, MEM_READ_EXTANY | MEM_WRITE_EXTANY);
    }

    flushmmucache_nopc();
}


static void
sis_85c496_ide_handler(sis_85c496_t *dev)
{
    uint8_t ide_cfg[2];

    ide_cfg[0] = dev->pci_conf[0x58];
    ide_cfg[1] = dev->pci_conf[0x59];

    ide_pri_disable();
    ide_sec_disable();

    if (ide_cfg[1] & 0x02) {
	ide_set_base(0, 0x0170);
	ide_set_side(0, 0x0376);
	ide_set_base(1, 0x01f0);
	ide_set_side(1, 0x03f6);

	if (ide_cfg[1] & 0x01) {
		if (!(ide_cfg[0] & 0x40))
			ide_pri_enable();
		if (!(ide_cfg[0] & 0x80))
			ide_sec_enable();
	}
    } else {
	ide_set_base(0, 0x01f0);
	ide_set_side(0, 0x03f6);
	ide_set_base(1, 0x0170);
	ide_set_side(1, 0x0376);

	if (ide_cfg[1] & 0x01) {
		if (!(ide_cfg[0] & 0x40))
			ide_sec_enable();
		if (!(ide_cfg[0] & 0x80))
			ide_pri_enable();
	}
    }
}


/* 00 - 3F = PCI Configuration, 40 - 7F = 85C496, 80 - FF = 85C497 */
static void
sis_85c49x_pci_write(int func, int addr, uint8_t val, void *priv)
{
    sis_85c496_t *dev = (sis_85c496_t *) priv;
    uint8_t old, valxor;
    uint8_t smm_irq[4] = { 10, 11, 12, 15 };
    uint32_t host_base, ram_base, size;

    old = dev->pci_conf[addr];
    valxor = (dev->pci_conf[addr]) ^ val;

    sis_85c496_log("[%04X:%08X] PCI Write %02X to   %02X:%02X\n", CS, cpu_state.pc, val, func, addr);

    switch (addr) {
	/* PCI Configuration Header Registers (00h ~ 3Fh) */
	case 0x04:	/* PCI Device Command */
		dev->pci_conf[addr] = val & 0x40;
		break;
	case 0x05:	/* PCI Device Command */
		dev->pci_conf[addr] = val & 0x03;
		break;
	case 0x07:	/* Device Status */
		dev->pci_conf[addr] &= ~(val & 0xf1);
		break;

	/* 86C496 Specific Registers (40h ~ 7Fh) */
	case 0x40:	/* CPU Configuration */
		dev->pci_conf[addr] = val & 0x7f;
		break;
	case 0x41:	/* DRAM Configuration */
		dev->pci_conf[addr] = val;
		break;
	case 0x42:	/* Cache Configure */
		dev->pci_conf[addr] = val;
		cpu_cache_ext_enabled = (val & 0x01);
		cpu_update_waitstates();
		break;
	case 0x43:	/* Cache Configure */
		dev->pci_conf[addr] = val & 0x8f;
		break;
	case 0x44:	/* Shadow Configure */
		dev->pci_conf[addr] = val;
		if (valxor & 0xff) {
			sis_85c496_recalcmapping(dev);
			if (((old & 0xf0) == 0xf0) && ((val & 0xf0) == 0x30))
				flushmmucache_nopc();
			else if (((old & 0xf0) == 0xf0) && ((val & 0xf0) == 0x00))
				flushmmucache_nopc();
			else
				flushmmucache();
		}
		break;
	case 0x45:	/* Shadow Configure */
		dev->pci_conf[addr] = val & 0x0f;
		if (valxor & 0x03)
			sis_85c496_recalcmapping(dev);
		break;
	case 0x46:	/* Cacheable Control */
		dev->pci_conf[addr] = val;
		break;
	case 0x47:	/* 85C496 Address Decoder */
		dev->pci_conf[addr] = val & 0x1f;
		break;
	case 0x48: case 0x49: case 0x4a: case 0x4b:	/* DRAM Boundary */
	case 0x4c: case 0x4d: case 0x4e: case 0x4f:
		// dev->pci_conf[addr] = val;
		spd_write_drbs(dev->pci_conf, 0x48, 0x4f, 1);
		break;
	case 0x50: case 0x51:	/* Exclusive Area 0 Setup */
		dev->pci_conf[addr] = val;
		break;
	case 0x52: case 0x53:	/* Exclusive Area 1 Setup */
		dev->pci_conf[addr] = val;
		break;
	case 0x54:		/* Exclusive Area 2 Setup */
		dev->pci_conf[addr] = val;
		break;
	case 0x55:		/* Exclusive Area 3 Setup */
		dev->pci_conf[addr] = val & 0xf0;
		break;
	case 0x56:		/* PCI / Keyboard Configure */
		dev->pci_conf[addr] = val;
		if (valxor & 0x02) {
			port_92_remove(dev->port_92);
			if (val & 0x02)
				port_92_add(dev->port_92);
		}
		break;
	case 0x57:		/* Output Pin Configuration */
		dev->pci_conf[addr] = val;
		break;
	case 0x58:		/* Build-in IDE Controller / VESA Bus Configuration */
		dev->pci_conf[addr] = val & 0xd7;
		if (valxor & 0xc0)
			sis_85c496_ide_handler(dev);
		break;
	case 0x59:		/* Build-in IDE Controller / VESA Bus Configuration */
		dev->pci_conf[addr] = val;
		if (valxor & 0x03)
			sis_85c496_ide_handler(dev);
		break;
	case 0x5a:		/* SMRAM Remapping Configuration */
		dev->pci_conf[addr] = val & 0xbe;
		if (valxor & 0x3e) {
			unmask_a20_in_smm = !!(val & 0x20);

			smram_disable_all();

			if (val & 0x02) {
				host_base = 0x00060000;
				ram_base = 0x000a0000;
				size = 0x00010000;
				switch ((val >> 3) & 0x03) {
					case 0x00:
						host_base = 0x00060000;
						ram_base = 0x000a0000;
						break;
					case 0x01:
						host_base = 0x00060000;
						ram_base = 0x000b0000;
						break;
					case 0x02:
						host_base = 0x000e0000;
						ram_base = 0x000a0000;
						break;
					case 0x03:
						host_base = 0x000e0000;
						ram_base = 0x000b0000;
						break;
				}

				smram_enable(dev->smram, host_base, ram_base, size,
					     ((val & 0x06) == 0x06), (val & 0x02));
			}
		}
		break;
	case 0x5b:		/* Programmable I/O Traps Configure */
	case 0x5c: case 0x5d:	/* Programmable I/O Trap 0 Base */
	case 0x5e: case 0x5f:	/* Programmable I/O Trap 0 Base */
	case 0x60: case 0x61:	/* IDE Controller Channel 0 Configuration */
	case 0x62: case 0x63:	/* IDE Controller Channel 1 Configuration */
	case 0x64: case 0x65:	/* Exclusive Area 3 Setup */
	case 0x66:		/* EDO DRAM Configuration */
	case 0x68: case 0x69:	/* Asymmetry DRAM Configuration */
		dev->pci_conf[addr] = val;
		break;
	case 0x67:		/* Miscellaneous Control */
		dev->pci_conf[addr] = val & 0xf9;
		if (valxor & 0x60)
			port_92_set_features(dev->port_92, !!(val & 0x20), !!(val & 0x40));
		break;

	/* 86C497 Specific Registers (80h ~ FFh) */
	case 0x80:		/* PMU Configuration */
	case 0x85:		/* STPCLK# Event Control */
	case 0x86: case 0x87:	/* STPCLK# Deassertion IRQ Selection */
	case 0x89:		/* Fast Timer Count */
	case 0x8a:		/* Generic Timer Count */
	case 0x8b:		/* Slow Timer Count */
	case 0x8e:		/* Clock Throttling On Timer Count */
	case 0x8f:		/* Clock Throttling Off Timer Count */
	case 0x90:		/* Clock Throttling On Timer Reload Condition */
	case 0x92:		/* Fast Timer Reload Condition */
	case 0x94:		/* Generic Timer Reload Condition */
	case 0x96:		/* Slow Timer Reload Condition */
	case 0x98: case 0x99:	/* Fast Timer Reload IRQ Selection */
	case 0x9a: case 0x9b:	/* Generic Timer Reload IRQ Selection */
	case 0x9c: case 0x9d:	/* Slow Timer Reload IRQ Selection */
	case 0xa2:		/* SMI Request Status Selection */
	case 0xa4: case 0xa5:	/* SMI Request IRQ Selection */
	case 0xa6: case 0xa7:	/* Clock Throttlign On Timer Reload IRQ Selection */
	case 0xa8:		/* GPIO Control */
	case 0xaa:		/* GPIO DeBounce Count */
	case 0xd2:		/* Exclusive Area 2 Base Address */
		dev->pci_conf[addr] = val;
		break;
	case 0x81:		/* PMU CPU Type Configuration */
		dev->pci_conf[addr] = val & 0x9f;
		break;
	case 0x88:		/* Timer Control */
		dev->pci_conf[addr] = val & 0x3f;
		break;
	case 0x8d:		/* RMSMIBLK Timer Count */
		dev->pci_conf[addr] = val;
		dev->rmsmiblk_count = val;
		timer_stop(&dev->rmsmiblk_timer);
		if (val >= 0x02)
			timer_on_auto(&dev->rmsmiblk_timer, 35.0);
		break;
	case 0x91:		/* Clock Throttling On Timer Reload Condition */
	case 0x93:		/* Fast Timer Reload Condition */
	case 0x95:		/* Generic Timer Reload Condition */
		dev->pci_conf[addr] = val & 0x03;
		break;
	case 0x97:		/* Slow Timer Reload Condition */
		dev->pci_conf[addr] = val & 0xc3;
		break;
	case 0x9e:		/* Soft-SMI Generation / RMSMIBLK Trigger */
		if (!smi_block && (val & 0x01) && (dev->pci_conf[0x80] & 0x80) && (dev->pci_conf[0xa2] & 0x10)) {
			if (dev->pci_conf[0x80] & 0x10)
				picint(1 << smm_irq[dev->pci_conf[0x81] & 0x03]);
			else
				smi_line = 1;
			smi_block = 1;
			dev->pci_conf[0xa0] |= 0x10;
		}
		if (val & 0x02) {
			timer_stop(&dev->rmsmiblk_timer);
			if (dev->rmsmiblk_count >= 0x02)
				timer_on_auto(&dev->rmsmiblk_timer, 35.0);
		}
		break;
	case 0xa0: case 0xa1:	/* SMI Request Status */
		dev->pci_conf[addr] &= ~val;
		break;
	case 0xa3:		/* SMI Request Status Selection */
		dev->pci_conf[addr] = val & 0x7f;
		break;
	case 0xa9:		/* GPIO SMI Request Status */
		dev->pci_conf[addr] = ~(val & 0x03);
		break;
	case 0xc0:		/* PCI INTA# -to-IRQ Link */
	case 0xc1:		/* PCI INTB# -to-IRQ Link */
	case 0xc2:		/* PCI INTC# -to-IRQ Link */
	case 0xc3:		/* PCI INTD# -to-IRQ Link */
		dev->pci_conf[addr] = val & 0x8f;
		if (val & 0x80)
			pci_set_irq_routing(PCI_INTA + (addr & 0x03), val & 0xf);
		else
			pci_set_irq_routing(PCI_INTA + (addr & 0x03), PCI_IRQ_DISABLED);
		break;
	case 0xc6:		/* 85C497 Post / INIT Configuration */
		dev->pci_conf[addr] = val & 0x0f;
		break;
	case 0xc8: case 0xc9: case 0xca: case 0xcb:	/* Mail Box */
		dev->pci_conf[addr] = val;
		break;
	case 0xd0:		/* ISA BIOS Configuration */
		dev->pci_conf[addr] = val & 0xfb;
		break;
	case 0xd1:		/* ISA Address Decoder */
		if (dev->pci_conf[0xd0] & 0x01)
			dev->pci_conf[addr] = val;
		break;
	case 0xd3:		/* Exclusive Area 2 Base Address */
		dev->pci_conf[addr] = val & 0xf0;
		break;
	case 0xd4:		/* Miscellaneous Configuration */
		dev->pci_conf[addr] = val & 0x6e;
		nvr_bank_set(0, !!(val & 0x40), dev->nvr);
		break;
    }
}


static uint8_t
sis_85c49x_pci_read(int func, int addr, void *priv)
{
    sis_85c496_t *dev = (sis_85c496_t *) priv;
    uint8_t ret = dev->pci_conf[addr];

    switch (addr) {
	case 0xa0:
		ret &= 0x10;
		break;
	case 0xa1:
		ret = 0x00;
		break;
	case 0x82: /*Port 22h Mirror*/
		ret = dev->cur_reg;
		break;
	case 0x83: /*Port 70h Mirror*/
		ret = inb(0x70);
		break;
    }

    sis_85c496_log("[%04X:%08X] PCI Read  %02X from %02X:%02X\n", CS, cpu_state.pc, ret, func, addr);

    return ret;
}


static void
sis_85c496_rmsmiblk_count(void *priv)
{
    sis_85c496_t *dev = (sis_85c496_t *) priv;

    dev->rmsmiblk_count--;

    if (dev->rmsmiblk_count == 1) {
	smi_block = 0;
	dev->rmsmiblk_count = 0;
	timer_stop(&dev->rmsmiblk_timer);
    } else
	timer_on_auto(&dev->rmsmiblk_timer, 35.0);
}


static void
sis_85c497_isa_reset(sis_85c496_t *dev)
{
    memset(dev->regs, 0, sizeof(dev->regs));

    dev->regs[0x01] = 0xc0;
    dev->regs[0x71] = 0x01;
    dev->regs[0x72] = 0xff;
    dev->regs[0x76] = 0xff;

    dma_set_mask(0x00ffffff);

    io_removehandler(0x0022, 0x0002,
		     sis_85c497_isa_read, NULL, NULL, sis_85c497_isa_write, NULL, NULL, dev);
    io_removehandler(0x0033, 0x0001,
		     sis_85c497_isa_read, NULL, NULL, sis_85c497_isa_write, NULL, NULL, dev);
    io_sethandler(0x0022, 0x0002,
		  sis_85c497_isa_read, NULL, NULL, sis_85c497_isa_write, NULL, NULL, dev);
    io_sethandler(0x0033, 0x0001,
		  sis_85c497_isa_read, NULL, NULL, sis_85c497_isa_write, NULL, NULL, dev);
}


static void
sis_85c496_reset(void *priv)
{
    sis_85c496_t *dev = (sis_85c496_t *) priv;
    int i;

    sis_85c49x_pci_write(0, 0x44, 0x00, dev);
    sis_85c49x_pci_write(0, 0x45, 0x00, dev);
    sis_85c49x_pci_write(0, 0x58, 0x00, dev);
    sis_85c49x_pci_write(0, 0x59, 0x00, dev);
    sis_85c49x_pci_write(0, 0x5a, 0x00, dev);
    // sis_85c49x_pci_write(0, 0x5a, 0x06, dev);

    for (i = 0; i < 8; i++)
	sis_85c49x_pci_write(0, 0x48 + i, 0x00, dev);

    sis_85c49x_pci_write(0, 0x80, 0x00, dev);
    sis_85c49x_pci_write(0, 0x81, 0x00, dev);
    sis_85c49x_pci_write(0, 0x9e, 0x00, dev);
    sis_85c49x_pci_write(0, 0x8d, 0x00, dev);
    sis_85c49x_pci_write(0, 0xa0, 0xff, dev);
    sis_85c49x_pci_write(0, 0xa1, 0xff, dev);
    sis_85c49x_pci_write(0, 0xc0, 0x00, dev);
    sis_85c49x_pci_write(0, 0xc1, 0x00, dev);
    sis_85c49x_pci_write(0, 0xc2, 0x00, dev);
    sis_85c49x_pci_write(0, 0xc3, 0x00, dev);
    sis_85c49x_pci_write(0, 0xc8, 0x00, dev);
    sis_85c49x_pci_write(0, 0xc9, 0x00, dev);
    sis_85c49x_pci_write(0, 0xca, 0x00, dev);
    sis_85c49x_pci_write(0, 0xcb, 0x00, dev);

    sis_85c49x_pci_write(0, 0xd0, 0x79, dev);
    sis_85c49x_pci_write(0, 0xd1, 0xff, dev);
    sis_85c49x_pci_write(0, 0xd0, 0x78, dev);
    sis_85c49x_pci_write(0, 0xd4, 0x00, dev);

    ide_pri_disable();
    ide_sec_disable();

    nvr_bank_set(0, 0, dev->nvr);

    sis_85c497_isa_reset(dev);
}


static void
sis_85c496_close(void *p)
{
    sis_85c496_t *dev = (sis_85c496_t *)p;

    smram_del(dev->smram);

    free(dev);
}


static void
*sis_85c496_init(const device_t *info)
{
    sis_85c496_t *dev = malloc(sizeof(sis_85c496_t));
    memset(dev, 0x00, sizeof(sis_85c496_t));

    dev->smram = smram_add();

    /* PCI Configuration Header Registers (00h ~ 3Fh) */
    dev->pci_conf[0x00] = 0x39;	/* SiS */
    dev->pci_conf[0x01] = 0x10;
    dev->pci_conf[0x02] = 0x96;	/* 496/497 */
    dev->pci_conf[0x03] = 0x04;
    dev->pci_conf[0x04] = 0x07;
    dev->pci_conf[0x06] = 0x80;
    dev->pci_conf[0x07] = 0x02;
    dev->pci_conf[0x08] = 0x02;	/* Device revision */
    dev->pci_conf[0x09] = 0x00;	/* Device class (PCI bridge) */
    dev->pci_conf[0x0b] = 0x06;

    /* 86C496 Specific Registers (40h ~ 7Fh) */

    /* 86C497 Specific Registers (80h ~ FFh) */
    dev->pci_conf[0xd0] = 0x78;	/* ROM at E0000-FFFFF, Flash enable. */
    dev->pci_conf[0xd1] = 0xff;

    pci_add_card(PCI_ADD_NORTHBRIDGE, sis_85c49x_pci_read, sis_85c49x_pci_write, dev);

    // sis_85c497_isa_reset(dev);

    dev->port_92 = device_add(&port_92_device);
    port_92_set_period(dev->port_92, 2ULL * TIMER_USEC);
    port_92_set_features(dev->port_92, 0, 0);

    sis_85c496_recalcmapping(dev);

    ide_pri_disable();
    ide_sec_disable();

    if (info->local)
	dev->nvr = device_add(&ami_1994_nvr_device);
    else
	dev->nvr = device_add(&at_nvr_device);

    dma_high_page_init();

    timer_add(&dev->rmsmiblk_timer, sis_85c496_rmsmiblk_count, dev, 0);

    sis_85c496_reset(dev);

    return dev;
}

const device_t sis_85c496_device = {
    .name = "SiS 85c496/85c497",
    .internal_name = "sis_85c496",
    .flags = DEVICE_PCI,
    .local = 0,
    .init = sis_85c496_init,
    .close = sis_85c496_close,
    .reset = sis_85c496_reset,
    { .available = NULL },
    .speed_changed = NULL,
    .force_redraw = NULL,
    .config = NULL
};

const device_t sis_85c496_ls486e_device = {
    .name = "SiS 85c496/85c497 (Lucky Star LS-486E)",
    .internal_name = "sis_85c496_ls486e",
    .flags = DEVICE_PCI,
    .local = 1,
    .init = sis_85c496_init,
    .close = sis_85c496_close,
    .reset = sis_85c496_reset,
    { .available = NULL },
    .speed_changed = NULL,
    .force_redraw = NULL,
    .config = NULL
};
