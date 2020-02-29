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
 * Version:	@(#)sis_85c496.c	1.0.2	2019/10/21
 *
 * Authors:	Sarah Walker, <http://pcem-emulator.co.uk/>
 *		Miran Grca, <mgrca8@gmail.com>
 *
 *		Copyright 2008-2019 Sarah Walker.
 *		Copyright 2019 Miran Grca.
 */
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>
#include "86box.h"
#include "cpu.h"
#include "mem.h"
#include "86box_io.h"
#include "rom.h"
#include "pci.h"
#include "device.h"
#include "keyboard.h"
#include "timer.h"
#include "port_92.h"
#include "hdc_ide.h"
#include "machine.h"
#include "chipset.h"


typedef struct sis_85c496_t
{
    uint8_t	cur_reg,
		regs[127],
		pci_conf[256];
    port_92_t *	port_92;
} sis_85c496_t;


static void
sis_85c497_write(uint16_t port, uint8_t val, void *priv)
{
    sis_85c496_t *dev = (sis_85c496_t *) priv;
    uint8_t index = (port & 1) ? 0 : 1;

    if (index) {
	if ((val != 0x01) || ((val >= 0x70) && (val <= 0x76)))
		dev->cur_reg = val;
    } else {
	if (((dev->cur_reg < 0x70) && (dev->cur_reg != 0x01)) || (dev->cur_reg > 0x76))
		return;
	dev->regs[dev->cur_reg] = val;
	dev->cur_reg = 0;
    }
}


static uint8_t
sis_85c497_read(uint16_t port, void *priv)
{
    sis_85c496_t *dev = (sis_85c496_t *) priv;
    uint8_t index = (port & 1) ? 0 : 1;
    uint8_t ret = 0xff;

    if (index)
	ret = dev->cur_reg;
    else {
	if ((dev->cur_reg != 0x01) || ((dev->cur_reg >= 0x70) && (dev->cur_reg <= 0x76))) {
		ret = dev->regs[dev->cur_reg];
		dev->cur_reg = 0;
	}
    }

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
		mem_set_mem_state(base, 0x8000, shflags);
	} else
		mem_set_mem_state(base, 0x8000, MEM_READ_EXTANY | MEM_WRITE_EXTANY);
    }

    flushmmucache();
}


/* 00 - 3F = PCI Configuration, 40 - 7F = 85C496, 80 - FF = 85C497 */
static void
sis_85c496_write(int func, int addr, uint8_t val, void *priv)
{
    sis_85c496_t *dev = (sis_85c496_t *) priv;
    uint8_t old = dev->pci_conf[addr];
    uint8_t valxor;

    if ((addr >= 4 && addr < 8) || addr >= 0x40)
	dev->pci_conf[addr] = val;

    pclog("SiS 496 Write: %02X %02X %02X\n", func, addr, val);

    valxor = old ^ val;

    switch (addr) {
	case 0x42: /*Cache configure*/
		cpu_cache_ext_enabled = (val & 0x01);
		cpu_update_waitstates();
		break;

	case 0x44: /*Shadow configure*/
		if (valxor & 0xff)
			sis_85c496_recalcmapping(dev);
		break;
	case 0x45: /*Shadow configure*/
		if (valxor & 0x03)
			sis_85c496_recalcmapping(dev);
		break;

	case 0x56:
		if (valxor & 0x02) {
			port_92_remove(dev->port_92);
			if (val & 0x02)
				port_92_add(dev->port_92);
			pclog("Port 92: %sabled\n", (val & 0x02) ? "En" : "Dis");
		}
		break;

	case 0x59:
		if (valxor & 0x02) {
			if (val & 0x02) {
				ide_set_base(0, 0x0170);
				ide_set_side(0, 0x0376);
				ide_set_base(1, 0x01f0);
				ide_set_side(1, 0x03f6);
			} else {
				ide_set_base(0, 0x01f0);
				ide_set_side(0, 0x03f6);
				ide_set_base(1, 0x0170);
				ide_set_side(1, 0x0376);
			}
		}
		break;

	case 0x58:
		if (valxor & 0x80) {
			if (dev->pci_conf[0x59] & 0x02) {
				ide_sec_disable();
				if (val & 0x80)
					ide_sec_enable();
			} else {
				ide_pri_disable();
				if (val & 0x80)
					ide_pri_enable();
			}
		}
		if (valxor & 0x40) {
			if (dev->pci_conf[0x59] & 0x02) {
				ide_pri_disable();
				if (val & 0x40)
					ide_pri_enable();
			} else {
				ide_sec_disable();
				if (val & 0x40)
					ide_sec_enable();
			}
		}
		break;

	case 0x5a:
		if (valxor & 0x04) {
			if (val & 0x04)
				mem_set_mem_state(0xa0000, 0x20000, MEM_READ_INTERNAL | MEM_WRITE_INTERNAL);
			else
				mem_set_mem_state(0xa0000, 0x20000, MEM_READ_EXTANY | MEM_WRITE_EXTANY);
		}
		break;

	case 0x67:
		if (valxor & 0x60) {
			port_92_set_features(dev->port_92, !!(val & 0x20), !!(val & 0x40));
			pclog("[Port 92] Set features: %sreset, %sA20\n", !!(val & 0x20) ? "" : "no ", !!(val & 0x40) ? "" : "no ");
		}
		break;

	case 0x82:
		sis_85c497_write(0x22, val, priv);
		break;

	case 0xc0:
		if (val & 0x80)
			pci_set_irq_routing(PCI_INTA, val & 0xf);
		else
			pci_set_irq_routing(PCI_INTA, PCI_IRQ_DISABLED);
		break;
	case 0xc1:
		if (val & 0x80)
			pci_set_irq_routing(PCI_INTB, val & 0xf);
		else
			pci_set_irq_routing(PCI_INTB, PCI_IRQ_DISABLED);
		break;
	case 0xc2:
		if (val & 0x80)
			pci_set_irq_routing(PCI_INTC, val & 0xf);
		else
			pci_set_irq_routing(PCI_INTC, PCI_IRQ_DISABLED);
		break;
	case 0xc3:
		if (val & 0x80)
			pci_set_irq_routing(PCI_INTD, val & 0xf);
		else
			pci_set_irq_routing(PCI_INTD, PCI_IRQ_DISABLED);
		break;
    }
}


static uint8_t
sis_85c496_read(int func, int addr, void *priv)
{
    sis_85c496_t *dev = (sis_85c496_t *) priv;
    uint8_t ret = dev->pci_conf[addr];

    switch (addr) {
	case 0x82: /*Port 22h Mirror*/
		ret = inb(0x22);
		break;
	case 0x70: /*Port 70h Mirror*/
		ret = inb(0x70);
		break;
    }

    pclog("SiS 496 Read: %02X %02X %02X\n", func, addr, ret);

    return ret;
}
 

static void
sis_85c497_reset(sis_85c496_t *dev)
{
    memset(dev->regs, 0, sizeof(dev->regs));

    dev->regs[0x01] = 0xc0;
    dev->regs[0x71] = 0x01;
    dev->regs[0x72] = 0xff;

    io_removehandler(0x0022, 0x0002,
		     sis_85c497_read, NULL, NULL, sis_85c497_write, NULL, NULL, dev);
    io_sethandler(0x0022, 0x0002,
		  sis_85c497_read, NULL, NULL, sis_85c497_write, NULL, NULL, dev);
}


static void
sis_85c496_reset(void *priv)
{
    sis_85c496_t *dev = (sis_85c496_t *) priv;

    sis_85c497_reset(dev);
}


static void
sis_85c496_close(void *p)
{
    sis_85c496_t *sis_85c496 = (sis_85c496_t *)p;

    free(sis_85c496);
}


static void
*sis_85c496_init(const device_t *info)
{
    sis_85c496_t *dev = malloc(sizeof(sis_85c496_t));
    memset(dev, 0, sizeof(sis_85c496_t));

    dev->pci_conf[0x00] = 0x39; /*SiS*/
    dev->pci_conf[0x01] = 0x10; 
    dev->pci_conf[0x02] = 0x96; /*496/497*/
    dev->pci_conf[0x03] = 0x04; 

    dev->pci_conf[0x04] = 7;
    dev->pci_conf[0x05] = 0;

    dev->pci_conf[0x06] = 0x80;
    dev->pci_conf[0x07] = 0x02;

    dev->pci_conf[0x08] = 2; /*Device revision*/

    dev->pci_conf[0x09] = 0x00; /*Device class (PCI bridge)*/
    dev->pci_conf[0x0a] = 0x00;
    dev->pci_conf[0x0b] = 0x06;

    dev->pci_conf[0x0e] = 0x00; /*Single function device*/

    dev->pci_conf[0xd0] = 0x78;	/* ROM at E0000-FFFFF, Flash enable. */
    dev->pci_conf[0xd1] = 0xff;

    pci_add_card(PCI_ADD_NORTHBRIDGE, sis_85c496_read, sis_85c496_write, dev);

    sis_85c497_reset(dev);

    dev->port_92 = device_add(&port_92_device);
    port_92_set_period(dev->port_92, 2ULL * TIMER_USEC);
    port_92_set_features(dev->port_92, 0, 0);

    sis_85c496_recalcmapping(dev);

    return dev;
}


const device_t sis_85c496_device =
{
    "SiS 85c496/85c497",
    DEVICE_PCI,
    0,
    sis_85c496_init, 
    sis_85c496_close, 
    sis_85c496_reset,
    NULL,
    NULL,
    NULL,
    NULL
};
