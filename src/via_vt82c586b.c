/*
 * 86Box	A hypervisor and IBM PC system emulator that specializes in
 *		running old operating systems and software designed for IBM
 *		PC systems and compatibles from 1981 through fairly recent
 *		system designs based on the PCI bus.
 *
 *		Emulation of the VIA Apollo MVP3 southbridge
 *
 * Version:	@(#)via_vt82c586b.c	1.0.0	2020/01/14
 *
 * Authors:	Sarah Walker, <http://pcem-emulator.co.uk/>
 *		Miran Grca, <mgrca8@gmail.com>
 *		Melissa Goad, <mszoopers@protonmail.com>
 *
 *		Copyright 2008-2020 Sarah Walker.
 *		Copyright 2016-2020 Miran Grca.
 *		Copyright 2020 Melissa Goad.
 */

#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>
#define HAVE_STDARG_H
#include "86box.h"
#include "cdrom/cdrom.h"
#include "cpu/cpu.h"
#include "scsi/scsi_device.h"
#include "scsi/scsi_cdrom.h"
#include "dma.h"
#include "io.h"
#include "device.h"
#include "apm.h"
#include "keyboard.h"
#include "mem.h"
#include "timer.h"
#include "nvr.h"
#include "pci.h"
#include "pic.h"
#include "port_92.h"
#include "disk/hdc.h"
#include "disk/hdc_ide.h"
#include "disk/hdc_ide_sff8038i.h"
#include "disk/zip.h"
#include "machine/machine.h"
#include "via_vt82c586b.h"


#define ACPI_TIMER_FREQ 3579545

#define ACPI_IO_ENABLE   (1 << 7)
#define ACPI_TIMER_32BIT (1 << 3)


typedef struct
{
    uint8_t		pci_isa_regs[256];
    uint8_t		ide_regs[256];
    uint8_t		usb_regs[256];
    uint8_t		power_regs[256];
    sff8038i_t *	bm[2];
    nvr_t *		nvr;
    int			nvr_enabled;

    struct
    {
	uint16_t		io_base;
    } usb;

    struct
    {
	uint16_t		io_base;
    } power;
} via_vt82c586b_t;


static void
via_vt82c586b_reset_hard(void *priv)
{
    int i;

    via_vt82c586b_t *via_vt82c586b = (via_vt82c586b_t *) priv;
    uint16_t old_base = (via_vt82c586b->ide_regs[0x20] & 0xf0) | (via_vt82c586b->ide_regs[0x21] << 8);

    sff_bus_master_reset(via_vt82c586b->bm[0], old_base);
    sff_bus_master_reset(via_vt82c586b->bm[1], old_base + 8);

    memset(via_vt82c586b->pci_isa_regs, 0, 256);
    memset(via_vt82c586b->ide_regs, 0, 256);
    memset(via_vt82c586b->usb_regs, 0, 256);
    memset(via_vt82c586b->power_regs, 0, 256);

    via_vt82c586b->pci_isa_regs[0x00] = 0x06; via_vt82c586b->pci_isa_regs[0x01] = 0x11; /*VIA*/
    via_vt82c586b->pci_isa_regs[0x02] = 0x86; via_vt82c586b->pci_isa_regs[0x03] = 0x05; /*VT82C586B*/
    via_vt82c586b->pci_isa_regs[0x04] = 0x0f;
    via_vt82c586b->pci_isa_regs[0x07] = 0x02;
    via_vt82c586b->pci_isa_regs[0x0a] = 0x01;
    via_vt82c586b->pci_isa_regs[0x0b] = 0x06;
    via_vt82c586b->pci_isa_regs[0x0e] = 0x80;

    via_vt82c586b->pci_isa_regs[0x48] = 0x01;
    via_vt82c586b->pci_isa_regs[0x4a] = 0x04;
    via_vt82c586b->pci_isa_regs[0x4f] = 0x03;

    via_vt82c586b->pci_isa_regs[0x50] = 0x24;
    via_vt82c586b->pci_isa_regs[0x59] = 0x04;

    dma_e = 0x00;
    for (i = 0; i < 8; i++) {
	dma[i].ab &= 0xffff000f;
	dma[i].ac &= 0xffff000f;
    }

    /* IDE registers */
    via_vt82c586b->ide_regs[0x00] = 0x06; via_vt82c586b->ide_regs[0x01] = 0x11; /*VIA*/
    via_vt82c586b->ide_regs[0x02] = 0x71; via_vt82c586b->ide_regs[0x03] = 0x05; /*VT82C586B*/
    via_vt82c586b->ide_regs[0x04] = 0x80;
    via_vt82c586b->ide_regs[0x06] = 0x80; via_vt82c586b->ide_regs[0x07] = 0x02;
    via_vt82c586b->ide_regs[0x09] = 0x85;
    via_vt82c586b->ide_regs[0x0a] = 0x01;
    via_vt82c586b->ide_regs[0x0b] = 0x01;

    via_vt82c586b->ide_regs[0x10] = 0xf1; via_vt82c586b->ide_regs[0x11] = 0x01;
    via_vt82c586b->ide_regs[0x14] = 0xf5; via_vt82c586b->ide_regs[0x15] = 0x03;
    via_vt82c586b->ide_regs[0x18] = 0x71; via_vt82c586b->ide_regs[0x19] = 0x01;
    via_vt82c586b->ide_regs[0x1c] = 0x75; via_vt82c586b->ide_regs[0x1d] = 0x03;
    via_vt82c586b->ide_regs[0x20] = 0x01; via_vt82c586b->ide_regs[0x21] = 0xcc;
    via_vt82c586b->ide_regs[0x3c] = 0x0e;

    via_vt82c586b->ide_regs[0x40] = 0x08;
    via_vt82c586b->ide_regs[0x41] = 0x02;
    via_vt82c586b->ide_regs[0x42] = 0x09;
    via_vt82c586b->ide_regs[0x43] = 0x3a;
    via_vt82c586b->ide_regs[0x44] = 0x68;
    via_vt82c586b->ide_regs[0x46] = 0xc0;
    via_vt82c586b->ide_regs[0x48] = 0xa8; via_vt82c586b->ide_regs[0x49] = 0xa8;
    via_vt82c586b->ide_regs[0x4a] = 0xa8; via_vt82c586b->ide_regs[0x4b] = 0xa8;
    via_vt82c586b->ide_regs[0x4c] = 0xff;
    via_vt82c586b->ide_regs[0x4e] = 0xff;
    via_vt82c586b->ide_regs[0x4f] = 0xff;
    via_vt82c586b->ide_regs[0x50] = 0x03; via_vt82c586b->ide_regs[0x51] = 0x03;
    via_vt82c586b->ide_regs[0x52] = 0x03; via_vt82c586b->ide_regs[0x53] = 0x03;

    via_vt82c586b->ide_regs[0x61] = 0x02;
    via_vt82c586b->ide_regs[0x69] = 0x02;

    via_vt82c586b->usb_regs[0x00] = 0x06; via_vt82c586b->usb_regs[0x01] = 0x11; /*VIA*/
    via_vt82c586b->usb_regs[0x02] = 0x38; via_vt82c586b->usb_regs[0x03] = 0x30;
    via_vt82c586b->usb_regs[0x04] = 0x00; via_vt82c586b->usb_regs[0x05] = 0x00;
    via_vt82c586b->usb_regs[0x06] = 0x00; via_vt82c586b->usb_regs[0x07] = 0x02;
    via_vt82c586b->usb_regs[0x0a] = 0x03;
    via_vt82c586b->usb_regs[0x0b] = 0x0c;
    via_vt82c586b->usb_regs[0x0d] = 0x16;
    via_vt82c586b->usb_regs[0x20] = 0x01;
    via_vt82c586b->usb_regs[0x21] = 0x03;
    via_vt82c586b->usb_regs[0x3d] = 0x04;

    via_vt82c586b->usb_regs[0x60] = 0x10;
    via_vt82c586b->usb_regs[0xc1] = 0x20;

    via_vt82c586b->power_regs[0x00] = 0x06; via_vt82c586b->power_regs[0x01] = 0x11; /*VIA*/
    via_vt82c586b->power_regs[0x02] = 0x40; via_vt82c586b->power_regs[0x03] = 0x30;
    via_vt82c586b->power_regs[0x04] = 0x00; via_vt82c586b->power_regs[0x05] = 0x00;
    via_vt82c586b->power_regs[0x06] = 0x80; via_vt82c586b->power_regs[0x07] = 0x02;
    via_vt82c586b->power_regs[0x08] = 0x10; /*Production version (3041)*/
    via_vt82c586b->power_regs[0x48] = 0x01;

    pci_set_irq_routing(PCI_INTA, PCI_IRQ_DISABLED);
    pci_set_irq_routing(PCI_INTB, PCI_IRQ_DISABLED);
    pci_set_irq_routing(PCI_INTC, PCI_IRQ_DISABLED);
    pci_set_irq_routing(PCI_INTD, PCI_IRQ_DISABLED);

    pci_set_mirq_routing(PCI_MIRQ0, PCI_IRQ_DISABLED);
    pci_set_mirq_routing(PCI_MIRQ1, PCI_IRQ_DISABLED);
    pci_set_mirq_routing(PCI_MIRQ2, PCI_IRQ_DISABLED);

    ide_pri_disable();
    ide_sec_disable();
}


static void
via_vt82c586b_ide_handlers(via_vt82c586b_t *dev)
{
    uint16_t main, side;

    ide_pri_disable();
    ide_sec_disable();

    if (dev->ide_regs[0x09] & 0x01) {
	main = (dev->ide_regs[0x11] << 8) | (dev->ide_regs[0x10] & 0xf8);
	side = ((dev->ide_regs[0x15] << 8) | (dev->ide_regs[0x14] & 0xfc)) + 2;
    } else {
	main = 0x1f0;
	side = 0x3f6;
    }
    ide_set_base(0, main);
    ide_set_side(0, side);

    if (dev->ide_regs[0x09] & 0x04) {
	main = (dev->ide_regs[0x19] << 8) | (dev->ide_regs[0x18] & 0xf8);
	side = ((dev->ide_regs[0x1d] << 8) | (dev->ide_regs[0x1c] & 0xfc)) + 2;
    } else {
	main = 0x170;
	side = 0x376;
    }
    ide_set_base(1, main);
    ide_set_side(1, side);

    if (dev->ide_regs[0x04] & PCI_COMMAND_IO) {
	if (dev->ide_regs[0x40] & 0x02)
		ide_pri_enable();
	if (dev->ide_regs[0x40] & 0x01)
		ide_sec_enable();
    }
}


static void
via_vt82c586b_bus_master_handlers(via_vt82c586b_t *dev, uint16_t old_base)
{
    uint16_t base;
    base = (dev->ide_regs[0x20] & 0xf0) | (dev->ide_regs[0x21] << 8);

    sff_bus_master_handlers(dev->bm[0], old_base, base, (dev->ide_regs[0x04] & 1));
    sff_bus_master_handlers(dev->bm[1], old_base + 8, base + 8, (dev->ide_regs[0x04] & 1));
}


static uint8_t
via_vt82c586b_read(int func, int addr, void *priv)
{
    via_vt82c586b_t *dev = (via_vt82c586b_t *) priv;

    uint8_t ret = 0xff;
    int c;

    switch(func) {
	case 0:
		if ((addr >= 0x60) && (addr <= 0x6f)) {
			c = (addr & 0x0e) >> 1;
			if (addr & 0x01)
				ret = (dma[c].ab & 0x0000ff00) >> 8;
			else {
				ret = (dma[c].ab & 0x000000f0);
				ret |= (!!(dma_e & (1 << c)) << 3);
			}
		} else
			ret = dev->pci_isa_regs[addr];
		break;
        case 1:
		ret = dev->ide_regs[addr];
		break;
	case 2:
		ret = dev->usb_regs[addr];
		break;
        case 3:
		ret = dev->power_regs[addr];
		break;
    }

    return ret;
}


static uint8_t
usb_reg_read(uint16_t addr, void *p)
{
    uint8_t ret = 0xff;

    switch (addr & 0x1f) {
	case 0x10: case 0x11: case 0x12: case 0x13:
		/* Port status */
                ret = 0x00;
		break;
    }

    return ret;
}


static void
usb_reg_write(uint16_t addr, uint8_t val, void *p)
{
}


static void
nvr_update_io_mapping(via_vt82c586b_t *dev)
{
    if (dev->nvr_enabled)
	nvr_at_handler(0, 0x0074, dev->nvr);

    if ((dev->pci_isa_regs[0x5b] & 0x02) && (dev->pci_isa_regs[0x48] & 0x08))
	nvr_at_handler(1, 0x0074, dev->nvr);
}


static void
usb_update_io_mapping(via_vt82c586b_t *dev)
{
    if (dev->usb.io_base != 0x0000)
	io_removehandler(dev->usb.io_base, 0x20, usb_reg_read, NULL, NULL, usb_reg_write, NULL, NULL, dev);

    dev->usb.io_base = (dev->usb_regs[0x20] & ~0x1f) | (dev->usb_regs[0x21] << 8);

    if ((dev->usb_regs[PCI_REG_COMMAND] & PCI_COMMAND_IO) && (dev->usb.io_base != 0x0000))
	io_sethandler(dev->usb.io_base, 0x20, usb_reg_read, NULL, NULL, usb_reg_write, NULL, NULL, dev);
}


static uint8_t
power_reg_read(uint16_t addr, void *p)
{
    via_vt82c586b_t *dev = (via_vt82c586b_t *) p;

    uint32_t timer;
    uint8_t ret = 0xff;

    switch (addr & 0xff) {
	case 0x08: case 0x09: case 0x0a: case 0x0b:
		/* ACPI timer */
		timer = (tsc * ACPI_TIMER_FREQ) / machines[machine].cpu[cpu_manufacturer].cpus[cpu_effective].rspeed;
		if (!(dev->power_regs[0x41] & ACPI_TIMER_32BIT))
			timer &= 0x00ffffff;
		ret = (timer >> (8 * (addr & 3))) & 0xff;
		break;
    }

    return ret;
}


static void
power_reg_write(uint16_t addr, uint8_t val, void *p)
{
}


static void
power_update_io_mapping(via_vt82c586b_t *dev)
{
    if (dev->power.io_base != 0x0000)
	io_removehandler(dev->power.io_base, 0x100, power_reg_read, NULL, NULL, power_reg_write, NULL, NULL, dev);

    dev->power.io_base = dev->power_regs[0x41] | (dev->power_regs[0x49] << 8);

    if ((dev->power_regs[0x41] & ACPI_IO_ENABLE) && (dev->power.io_base != 0x0000))
	io_sethandler(dev->power.io_base, 0x100, power_reg_read, NULL, NULL, power_reg_write, NULL, NULL, dev);
}


static void
via_vt82c586b_write(int func, int addr, uint8_t val, void *priv)
{
    via_vt82c586b_t *dev = (via_vt82c586b_t *) priv;

    uint16_t old_base, base;
    int c;

    if (func > 3)
	return;

    old_base = (dev->ide_regs[0x20] & 0xf0) | (dev->ide_regs[0x21] << 8);

    switch(func) {
	case 0:		/* PCI-ISA bridge */
		/* Read-only addresses */
		if ((addr < 4) || (addr == 5) || (addr == 6) || ((addr >= 8) && (addr < 0x40)) ||
		    (addr == 0x49) || (addr == 0x4b) || ((addr >= 0x51) && (addr < 0x54)) || ((addr >= 0x5d) && (addr < 0x60)) ||
		    ((addr >= 0x68) && (addr < 0x6a)) || (addr >= 0x73))
		return;

		switch (addr) {
			case 0x04:
				dev->pci_isa_regs[0x04] = (val & 8) | 7;
				break;
			case 0x06:
				dev->pci_isa_regs[0x06] &= ~(val & 0xb0);
				break;

			case 0x47:
				if ((val & 0x81) == 0x81)
					resetx86();
				pci_elcr_set_enabled(!!(val & 0x20));
				dev->pci_isa_regs[0x47] = val & 0xfe;
				break;
			case 0x48:
				dev->pci_isa_regs[0x48] = val;
				nvr_update_io_mapping(dev);
				break;

			case 0x54:
				pci_set_irq_level(PCI_INTA, !(val & 8));
				pci_set_irq_level(PCI_INTB, !(val & 4));
				pci_set_irq_level(PCI_INTC, !(val & 2));
				pci_set_irq_level(PCI_INTD, !(val & 1));
				break;
			case 0x55:
				pci_set_irq_routing(PCI_INTD, (val & 0xf0) ? (val >> 4) : PCI_IRQ_DISABLED);
				pci_set_mirq_routing(PCI_MIRQ0, (val & 0x0f) ? (val & 0x0f) : PCI_IRQ_DISABLED);
				dev->pci_isa_regs[0x55] = val;
		                break;
	                case 0x56:
				pci_set_irq_routing(PCI_INTA, (val & 0xf0) ? (val >> 4) : PCI_IRQ_DISABLED);
				pci_set_irq_routing(PCI_INTB, (val & 0x0f) ? (val & 0x0f) : PCI_IRQ_DISABLED);
				dev->pci_isa_regs[0x56] = val;
				break;
			case 0x57:
				pci_set_irq_routing(PCI_INTC, (val & 0xf0) ? (val >> 4) : PCI_IRQ_DISABLED);
				pci_set_mirq_routing(PCI_MIRQ1, (val & 0x0f) ? (val & 0x0f) : PCI_IRQ_DISABLED);
				dev->pci_isa_regs[0x57] = val;
				break;
			case 0x58:
				pci_set_mirq_routing(PCI_MIRQ2, (val & 0x0f) ? (val & 0x0f) : PCI_IRQ_DISABLED);
				dev->pci_isa_regs[0x58] = val;
				break;
			case 0x5b:
				dev->pci_isa_regs[0x5b] = val;
				nvr_update_io_mapping(dev);
				break;

			case 0x60: case 0x62: case 0x64: case 0x66:
			case 0x68: case 0x6a: case 0x6c: case 0x6e:
				c = (addr & 0x0e) >> 1;
				dma[c].ab = (dma[c].ab & 0xffffff0f) | (val & 0xf0);
				dma[c].ac = (dma[c].ac & 0xffffff0f) | (val & 0xf0);
				if (val & 0x08)
					dma_e |= (1 << c);
				else
					dma_e &= ~(1 << c);
				break;
			case 0x61: case 0x63: case 0x65: case 0x67:
			case 0x69: case 0x6b: case 0x6d: case 0x6f:
				c = (addr & 0x0e) >> 1;
				dma[c].ab = (dma[c].ab & 0xffff00ff) | (val << 8);
				dma[c].ac = (dma[c].ac & 0xffff00ff) | (val << 8);
				break;

			case 0x70: case 0x71: case 0x72: case 0x73:
				dev->pci_isa_regs[(addr - 0x44)] = val;
				break;
		}
		break;

        case 1:		/* IDE regs */
		/* Read-only addresses */
		if ((addr < 4) || (addr == 5) || (addr == 8) || ((addr >= 0xa) && (addr < 0x0d)) ||
		    ((addr >= 0x0e) && (addr < 0x10)) || ((addr >= 0x12) && (addr < 0x13)) ||
		    ((addr >= 0x16) && (addr < 0x17)) || ((addr >= 0x1a) && (addr < 0x1b)) ||
		    ((addr >= 0x1e) && (addr < 0x1f)) || ((addr >= 0x22) && (addr < 0x3c)) ||
		     ((addr >= 0x3e) && (addr < 0x40)) || ((addr >= 0x54) && (addr < 0x60)) ||
		     ((addr >= 0x52) && (addr < 0x68)) || (addr >= 0x62))
			return;

		switch (addr) {
			case 0x04:
				base = (dev->ide_regs[0x20] & 0xf0) | (dev->ide_regs[0x21] << 8);
				dev->ide_regs[0x04] = val & 0x85;
				via_vt82c586b_ide_handlers(dev);
				via_vt82c586b_bus_master_handlers(dev, base);
				break;
			case 0x06:
				dev->ide_regs[0x06] &= ~(val & 0xb0);
				break;

			case 0x09:
				dev->ide_regs[0x09] = (val & 0x05) | 0x8a;
				via_vt82c586b_ide_handlers(dev);
				break;

			case 0x10:
				dev->ide_regs[0x10] = (val & 0xf8) | 1;
				via_vt82c586b_ide_handlers(dev);
				break;
			case 0x11:
				dev->ide_regs[0x11] = val;
				via_vt82c586b_ide_handlers(dev);
				break;

			case 0x14:
				dev->ide_regs[0x14] = (val & 0xfc) | 1;
				via_vt82c586b_ide_handlers(dev);
				break;
			case 0x15:
				dev->ide_regs[0x15] = val;
				via_vt82c586b_ide_handlers(dev);
				break;

			case 0x18:
				dev->ide_regs[0x18] = (val & 0xf8) | 1;
				via_vt82c586b_ide_handlers(dev);
				break;
			case 0x19:
				dev->ide_regs[0x19] = val;
				via_vt82c586b_ide_handlers(dev);
				break;

			case 0x1c:
				dev->ide_regs[0x1c] = (val & 0xfc) | 1;
				via_vt82c586b_ide_handlers(dev);
				break;
			case 0x1d:
				dev->ide_regs[0x1d] = val;
				via_vt82c586b_ide_handlers(dev);
				break;

			case 0x20:
				dev->ide_regs[0x20] = (val & 0xf0) | 1;
				via_vt82c586b_bus_master_handlers(dev, old_base);
				break;
			case 0x21:
				dev->ide_regs[0x21] = val;
				via_vt82c586b_bus_master_handlers(dev, old_base);
				break;

			case 0x3d:
				sff_set_irq_mode(dev->bm[0], val);
				sff_set_irq_mode(dev->bm[1], val);
				break;

			case 0x40:
				dev->ide_regs[0x40] = val;
				via_vt82c586b_ide_handlers(dev);
				break;

			default:
				dev->ide_regs[addr] = val;
				break;
		}
		break;

	case 2:
		/* Read-only addresses */
		if ((addr < 4) || (addr == 5) || (addr == 6) || ((addr >= 8) && (addr < 0xd)) ||
		    ((addr >= 0xe) && (addr < 0x20)) || ((addr >= 0x22) && (addr < 0x3c)) ||
		    ((addr >= 0x3e) && (addr < 0x40)) || ((addr >= 0x42) && (addr < 0x44)) ||
		    ((addr >= 0x46) && (addr < 0xc0)) || (addr >= 0xc2))
			return;

		switch (addr) {
			case 0x04:
				dev->usb_regs[0x04] = val & 0x97;
				break;
			case 0x07:
				dev->usb_regs[0x07] = val & 0x7f;
				break;

			case 0x20:
				dev->usb_regs[0x20] = (val & ~0x1f) | 1;
				usb_update_io_mapping(dev);
				break;
			case 0x21:
				dev->usb_regs[0x21] = val;
				usb_update_io_mapping(dev);
				break;

			default:
				dev->usb_regs[addr] = val;
				break;
		}
		break;

	case 3:
		/* Read-only addresses */
		if ((addr < 0xd) || ((addr >= 0xe && addr < 0x40)) || (addr == 0x43) || (addr == 0x48) ||
		    ((addr >= 0x4a) && (addr < 0x50)) || (addr >= 0x54))
			return;

		switch (addr) {
			case 0x41: case 0x49:
				dev->power_regs[addr] = val;
				power_update_io_mapping(dev);
				break;

			default:
				dev->power_regs[addr] = val;
				break;
		}
    }
}


static void
*via_vt82c586b_init(const device_t *info)
{
    via_vt82c586b_t *dev = (via_vt82c586b_t *) malloc(sizeof(via_vt82c586b_t));
    memset(dev, 0, sizeof(via_vt82c586b_t));

    pci_add_card(7, via_vt82c586b_read, via_vt82c586b_write, dev);

    dev->bm[0] = device_add_inst(&sff8038i_device, 1);
    sff_set_slot(dev->bm[0], 7);
    sff_set_irq_mode(dev->bm[0], 0);
    sff_set_irq_pin(dev->bm[0], PCI_INTA);

    dev->bm[1] = device_add_inst(&sff8038i_device, 2);
    sff_set_slot(dev->bm[1], 7);
    sff_set_irq_mode(dev->bm[1], 0);
    sff_set_irq_pin(dev->bm[1], PCI_INTA);

    dev->nvr = device_add(&via_nvr_device);

    via_vt82c586b_reset_hard(dev);

    device_add(&port_92_pci_device);

    dma_alias_set();

    pci_enable_mirq(0);
    pci_enable_mirq(1);
    pci_enable_mirq(2);

    return dev;
}

static void
via_vt82c586b_close(void *p)
{
    via_vt82c586b_t *via_vt82c586b = (via_vt82c586b_t *)p;

    free(via_vt82c586b);
}

const device_t via_vt82c586b_device =
{
    "VIA VT82C586B",
    DEVICE_PCI,
    0,
    via_vt82c586b_init, 
    via_vt82c586b_close, 
    NULL,
    NULL,
    NULL,
    NULL,
    NULL
};
