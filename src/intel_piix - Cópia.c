/*
 * 86Box	A hypervisor and IBM PC system emulator that specializes in
 *		running old operating systems and software designed for IBM
 *		PC systems and compatibles from 1981 through fairly recent
 *		system designs based on the PCI bus.
 *
 *		Emulation of the Intel PIIX and PIIX3 Xcelerators.
 *
 *		PRD format :
 *		    word 0 - base address
 *		    word 1 - bits 1-15 = byte count, bit 31 = end of transfer
 *
 * Version:	@(#)intel_piix.c	1.0.23	2020/01/24
 *
 * Authors:	Sarah Walker, <http://pcem-emulator.co.uk/>
 *		Miran Grca, <mgrca8@gmail.com>
 *
 *		Copyright 2008-2020 Sarah Walker.
 *		Copyright 2016-2020 Miran Grca.
 */
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>
#define HAVE_STDARG_H
#include "86box.h"
#include "cdrom.h"
#include "cpu.h"
#include "scsi_device.h"
#include "scsi_cdrom.h"
#include "dma.h"
#include "86box_io.h"
#include "device.h"
#include "apm.h"
#include "keyboard.h"
#include "mem.h"
#include "timer.h"
#include "nvr.h"
#include "pci.h"
#include "pic.h"
#include "port_92.h"
#include "hdc.h"
#include "hdc_ide.h"
#include "hdc_ide_sff8038i.h"
#include "zip.h"
#include "machine.h"
#include "piix.h"


#define ACPI_TIMER_FREQ 3579545


typedef struct
{
    uint16_t		io_base;
    int			base_channel;
} ddma_t;


typedef struct
{
    int			type;
    uint8_t		cur_readout_reg,
			readout_regs[256],
			regs[256], regs_ide[256],
			regs_usb[256], regs_power[256];
    sff8038i_t		*bm[2];
    ddma_t		ddma[2];
    nvr_t *		nvr;

    struct
    {
	uint16_t		io_base;
    } usb;

    struct
    {
	uint16_t		io_base;
    } power;
} piix_t;


#ifdef ENABLE_PIIX_LOG
int piix_do_log = ENABLE_PIIX_LOG;


static void
piix_log(const char *fmt, ...)
{
    va_list ap;

    if (piix_do_log) {
	va_start(ap, fmt);
	pclog_ex(fmt, ap);
	va_end(ap);
    }
}
#else
#define piix_log(fmt, ...)
#endif


static void
piix_bus_master_handlers(piix_t *dev, uint16_t old_base)
{
    uint16_t base;

    base = (dev->regs_ide[0x20] & 0xf0) | (dev->regs_ide[0x21] << 8);

    sff_bus_master_handlers(dev->bm[0], old_base, base, (dev->regs_ide[0x04] & 1));
    sff_bus_master_handlers(dev->bm[1], old_base + 8, base + 8, (dev->regs_ide[0x04] & 1));
}


static uint8_t
kbc_alias_reg_read(uint16_t addr, void *p)
{
    uint8_t ret = inb(0x61);

    return ret;
}


static void
kbc_alias_reg_write(uint16_t addr, uint8_t val, void *p)
{
    outb(0x61, val);
}


static void
kbc_alias_update_io_mapping(piix_t *dev)
{
    io_removehandler(0x0063, 1, kbc_alias_reg_read, NULL, NULL, kbc_alias_reg_write, NULL, NULL, dev);
    io_removehandler(0x0065, 1, kbc_alias_reg_read, NULL, NULL, kbc_alias_reg_write, NULL, NULL, dev);
    io_removehandler(0x0067, 1, kbc_alias_reg_read, NULL, NULL, kbc_alias_reg_write, NULL, NULL, dev);

    if (dev->regs[0x4e] & 0x08) {
	io_sethandler(0x0063, 1, kbc_alias_reg_read, NULL, NULL, kbc_alias_reg_write, NULL, NULL, dev);
	io_sethandler(0x0065, 1, kbc_alias_reg_read, NULL, NULL, kbc_alias_reg_write, NULL, NULL, dev);
	io_sethandler(0x0067, 1, kbc_alias_reg_read, NULL, NULL, kbc_alias_reg_write, NULL, NULL, dev);
    }
}


static uint8_t
ddma_reg_read(uint16_t addr, void *p)
{
    ddma_t *dev = (ddma_t *) p;
    uint8_t ret = 0xff;
    int rel_ch = (addr & 0x30) >> 4;
    int ch = dev->base_channel + rel_ch;
    int dmab = (ch >= 4) ? 0xc0 : 0x00;

    switch (addr & 0x0f) {
	case 0x00:
		ret = dma[ch].ac & 0xff;
		break;
	case 0x01:
		ret = (dma[ch].ac >> 8) & 0xff;
		break;
	case 0x02:
		ret = dma[ch].page;
		break;
	case 0x04:
		ret = dma[ch].cc & 0xff;
		break;
	case 0x05:
		ret = (dma[ch].cc >> 8) & 0xff;
		break;
	case 0x09:
		ret = inb(dmab + 0x08);
		break;
    }

    return ret;
}


static void
ddma_reg_write(uint16_t addr, uint8_t val, void *p)
{
    ddma_t *dev = (ddma_t *) p;
    int rel_ch = (addr & 0x30) >> 4;
    int ch = dev->base_channel + rel_ch;
    int page_regs[4] = { 7, 3, 1, 2 };
    int i, dmab = (ch >= 4) ? 0xc0 : 0x00;

    switch (addr & 0x0f) {
	case 0x00:
		dma[ch].ab = (dma[ch].ab & 0xffff00) | val;
		dma[ch].ac = dma[ch].ab;
		break;
	case 0x01:
		dma[ch].ab = (dma[ch].ab & 0xff00ff) | (val << 8);
		dma[ch].ac = dma[ch].ab;
		break;
	case 0x02:
		if (ch >= 4)
			outb(0x88 + page_regs[rel_ch], val);
		else
			outb(0x80 + page_regs[rel_ch], val);
		break;
	case 0x04:
		dma[ch].cb = (dma[ch].cb & 0xffff00) | val;
		dma[ch].cc = dma[ch].cb;
		break;
	case 0x05:
		dma[ch].cb = (dma[ch].cb & 0xff00ff) | (val << 8);
		dma[ch].cc = dma[ch].cb;
		break;
	case 0x08:
		outb(dmab + 0x08, val);
		break;
	case 0x09:
		outb(dmab + 0x09, val);
		break;
	case 0x0a:
		outb(dmab + 0x0a, val);
		break;
	case 0x0b:
		outb(dmab + 0x0b, val);
		break;
	case 0x0d:
		outb(dmab + 0x0d, val);
		break;
	case 0x0e:
		for (i = 0; i < 4; i++)
			outb(dmab + 0x0a, i);
		break;
	case 0x0f:
		outb(dmab + 0x0a, (val << 2) | rel_ch);
		break;
    }
}


static void
ddma_update_io_mapping(piix_t *dev, int n)
{
    int base_reg = 0x92 + (n << 1);

    if (dev->ddma[n].io_base != 0x0000)
	io_removehandler(dev->usb.io_base, 0x40, ddma_reg_read, NULL, NULL, ddma_reg_write, NULL, NULL, &dev->ddma[n]);

    dev->ddma[n].io_base = (dev->regs[base_reg] & ~0x3f) | (dev->regs[base_reg + 1] << 8);

    if (dev->ddma[n].io_base != 0x0000)
	io_sethandler(dev->ddma[n].io_base, 0x40, ddma_reg_read, NULL, NULL, ddma_reg_write, NULL, NULL, &dev->ddma[n]);
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
usb_update_io_mapping(piix_t *dev)
{
    if (dev->usb.io_base != 0x0000)
	io_removehandler(dev->usb.io_base, 0x20, usb_reg_read, NULL, NULL, usb_reg_write, NULL, NULL, dev);

    dev->usb.io_base = (dev->regs_usb[0x20] & ~0x1f) | (dev->regs_usb[0x21] << 8);

    if ((dev->regs_usb[PCI_REG_COMMAND] & PCI_COMMAND_IO) && (dev->usb.io_base != 0x0000))
	io_sethandler(dev->usb.io_base, 0x20, usb_reg_read, NULL, NULL, usb_reg_write, NULL, NULL, dev);
}


static uint8_t
power_reg_read(uint16_t addr, void *p)
{
    uint32_t timer;
    uint8_t ret = 0xff;

    switch (addr & 0x3f) {
	case 0x08: case 0x09: case 0x0a: case 0x0b:
		/* ACPI timer */
		timer = (tsc * ACPI_TIMER_FREQ) / machines[machine].cpu[cpu_manufacturer].cpus[cpu_effective].rspeed;
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
power_update_io_mapping(piix_t *dev)
{
    if (dev->power.io_base != 0x0000)
	io_removehandler(dev->power.io_base, 0x40, power_reg_read, NULL, NULL, power_reg_write, NULL, NULL, dev);

    dev->power.io_base = (dev->regs_power[0x41] << 8) | (dev->regs_power[0x40] & 0xc0);

    if ((dev->regs_power[PCI_REG_COMMAND] & PCI_COMMAND_IO) && (dev->regs_power[0x80] & 0x01) && (dev->power.io_base != 0x0000))
	io_sethandler(dev->power.io_base, 0x100, power_reg_read, NULL, NULL, power_reg_write, NULL, NULL, dev);
}


static void
piix_write(int func, int addr, uint8_t val, void *priv)
{
    piix_t *dev = (piix_t *) priv;
    int type = dev->type & 0xff;
    uint8_t valxor;
    uint16_t old_base;

    if ((func > 0) && (dev->type & 0x100))	/* PB640's PIIX has no IDE part. */
	return;

    if ((func > 1) && ((type & 0xff) < 3))	/* PIIX has no USB part. */
	return;

    if ((func > 2) && ((type & 0xff) < 4))	/* PIIX and PIIX3 have no Power Management part. */
	return;

    if (func > 3)
	return;

    old_base = (dev->regs_ide[0x20] & 0xf0) | (dev->regs_ide[0x21] << 8);

    pclog("PIIX function %i write: %02X to %02X\n", func, val, addr);

    if (func == 3) {		/* Power Management */
	/* Read-only addresses */
	if ((addr < 4) || (addr == 5) || (addr == 6) || ((addr >= 8) && (addr < 0x3c)) ||
	    ((addr >= 0x3c) && (addr < 0x40)) || (addr == 0x53) ||
	    ((addr >= 0x81) && (addr < 0x90)) || ((addr >= 0x94) && (addr < 0xd2)) ||
	    (addr > 0xd6))
		return;

	switch (addr) {
		case 0x04:
			dev->regs_power[0x04] = val & 0x01;
			power_update_io_mapping(dev);
			break;
		case 0x07:
			dev->regs_power[0x07] = val & 0x0e;
			break;

		case 0x3c:
			dev->regs_power[0x3c] = val;
			break;

		case 0x40:
			dev->regs_power[0x20] = (val & ~0x3f) | 1;
			power_update_io_mapping(dev);
			break;
		case 0x41:
			dev->regs_power[0x21] = val;
			power_update_io_mapping(dev);
			break;

		case 0x80:
			dev->regs_power[0x80] = val & 0x01;
			power_update_io_mapping(dev);
			break;

		default:
			dev->regs_power[addr] = val;
			break;
	}
    } else if (func == 2) {		/* USB */
	/* Read-only addresses */
	if ((addr < 4) || (addr == 5) || (addr == 6) || ((addr >= 8) && (addr < 0xd)) ||
	    ((addr >= 0xe) && (addr < 0x20)) || ((addr >= 0x22) && (addr < 0x3c)) ||
	    ((addr >= 0x3e) && (addr < 0x40)) || ((addr >= 0x42) && (addr < 0x44)) ||
	    ((addr >= 0x46) && (addr < 0xc0)) || (addr >= 0xc2))
		return;

	switch (addr) {
		case 0x04:
			dev->regs_usb[0x04] = val & 0x97;
			usb_update_io_mapping(dev);
			break;
		case 0x07:
			dev->regs_usb[0x07] = val & 0x7f;
			break;

		case 0x20:
			dev->regs_usb[0x20] = (val & ~0x1f) | 1;
			usb_update_io_mapping(dev);
			break;
		case 0x21:
			dev->regs_usb[0x21] = val;
			usb_update_io_mapping(dev);
			break;

		case 0xff:
			if (type >= 4) {
				dev->regs_usb[addr] = val & 0x10;
				nvr_at_handler(0, 0x0070, dev->nvr);
				if ((dev->regs[0xcb] & 0x01) && (dev->regs_usb[0xff] & 0x10))
					nvr_at_handler(1, 0x0070, dev->nvr);
			}
			break;

		default:
			dev->regs_usb[addr] = val;
			break;
	}
    } else if (func == 1) {	/* IDE */
	piix_log("PIIX IDE write: %02X %02X\n", addr, val);
	valxor = val ^ dev->regs_ide[addr];

	switch (addr) {
		case 0x04:
			pclog("04 write: %02X\n", val);
			dev->regs_ide[0x04] = (val & 5);
			if (valxor & 0x01) {
				ide_pri_disable();
				ide_sec_disable();
				if (val & 0x01) {
					// pclog("04: I/O enabled\n");
					if (dev->regs_ide[0x41] & 0x80) {
						// pclog("04: PRI enabled\n");
						ide_pri_enable();
					}
					if (dev->regs_ide[0x43] & 0x80) {
						// pclog("04: SEC enabled\n");
						ide_sec_enable();
					}
				} else
					// pclog("04: I/O disabled\n");

				piix_bus_master_handlers(dev, old_base);
			}
			break;
		case 0x07:
			dev->regs_ide[0x07] = (dev->regs_ide[0x07] & 0xf9) | (val & 0x06);
			if (val & 0x20)
				dev->regs_ide[0x07] &= 0xdf;
			if (val & 0x10)
				dev->regs_ide[0x07] &= 0xef;
			if (val & 0x08)
				dev->regs_ide[0x07] &= 0xf7;
			if (val & 0x04)
				dev->regs_ide[0x07] &= 0xfb;
			break;
		case 0x0d:
			dev->regs_ide[0x0d] = val & 0xf0;
			break;

		case 0x20:
			dev->regs_ide[0x20] = (val & 0xf0) | 1;
			if (valxor)
				piix_bus_master_handlers(dev, old_base);
			break;
		case 0x21:
			dev->regs_ide[0x21] = val;
			if (valxor)
				piix_bus_master_handlers(dev, old_base);
			break;

		case 0x40:
			dev->regs_ide[0x40] = val;
			break;
		case 0x41:
			dev->regs_ide[0x41] = val & ((type >= 3) ? 0xf3 : 0xb3);
			if (valxor & 0x80) {
				ide_pri_disable();
				if ((val & 0x80) && (dev->regs_ide[0x04] & 0x01))
					ide_pri_enable();
			}
			break;
		case 0x42:
			dev->regs_ide[0x42] = val;
			break;
		case 0x43:
			dev->regs_ide[0x43] = val & ((type >= 3) ? 0xf3 : 0xb3);
			if (valxor & 0x80) {
				ide_sec_disable();
				if ((val & 0x80) && (dev->regs_ide[0x04] & 0x01))
					ide_sec_enable();
			}
			break;
		case 0x44:
			if (type >= 3)  dev->regs_ide[0x44] = val;
			break;
		case 0x48:
		case 0x4a: case 0x4b:
			if (type >= 4)  dev->regs_ide[addr] = val;
			break;
	}
    } else {
	piix_log("PIIX writing value %02X to register %02X\n", val, addr);
	valxor = val ^ dev->regs[addr];

	if ((addr >= 0x0f) && (addr < 0x4c))
		return;

	if ((addr >= 0xa0) && (addr < 0xb0) && (type == 4))
		return;

	switch (addr) {
		case 0x00: case 0x01: case 0x02: case 0x03:
		case 0x08: case 0x09: case 0x0a: case 0x0b:
		case 0x0e:
			return;

		case 0x07:
			dev->regs[0x07] = (dev->regs[0x07] & 0xf9) | (val & 0x06);
			if ((val & 0x40) && (type >= 3))
				dev->regs[0x07] &= 0xbf;
			if (val & 0x20)
				dev->regs[0x07] &= 0xdf;
			if (val & 0x10)
				dev->regs[0x07] &= 0xef;
			if (val & 0x08)
				dev->regs[0x07] &= 0xf7;
			if (val & 0x04)
				dev->regs[0x07] &= 0xfb;
			return;
			break;
		case 0x4c:
			if (valxor) {
				if (type >= 3)
					dma_alias_remove();
				else
					dma_alias_remove_piix();
				if (!(val & 0x80))
					dma_alias_set();
			}
			break;
		case 0x4e:
			keyboard_at_set_mouse_scan((val & 0x10) ? 1 : 0);
			if (type >= 4)
				kbc_alias_update_io_mapping(dev);
			break;
		case 0x60:
			piix_log("Set IRQ routing: INT A -> %02X\n", val);
			if (val & 0x80)
				pci_set_irq_routing(PCI_INTA, PCI_IRQ_DISABLED);
			else
				pci_set_irq_routing(PCI_INTA, val & 0xf);
			break;
		case 0x61:
			piix_log("Set IRQ routing: INT B -> %02X\n", val);
			if (val & 0x80)
				pci_set_irq_routing(PCI_INTB, PCI_IRQ_DISABLED);
			else
				pci_set_irq_routing(PCI_INTB, val & 0xf);
			break;
		case 0x62:
			piix_log("Set IRQ routing: INT C -> %02X\n", val);
			if (val & 0x80)
				pci_set_irq_routing(PCI_INTC, PCI_IRQ_DISABLED);
			else
				pci_set_irq_routing(PCI_INTC, val & 0xf);
			break;
		case 0x63:
			piix_log("Set IRQ routing: INT D -> %02X\n", val);
			if (val & 0x80)
				pci_set_irq_routing(PCI_INTD, PCI_IRQ_DISABLED);
			else
				pci_set_irq_routing(PCI_INTD, val & 0xf);
			break;
		case 0x6a:
			if (dev->type == 3)
				dev->regs[addr] = (val & 0xFD) | (dev->regs[addr] | 2);
			else
				dev->regs[addr] = (val & 0xFC) | (dev->regs[addr] | 3);
			return;
		case 0x70:
			piix_log("Set MIRQ routing: MIRQ0 -> %02X\n", val);
			if (type < 4) {
				if (val & 0x80)
					pci_set_mirq_routing(PCI_MIRQ0, PCI_IRQ_DISABLED);
				else
					pci_set_mirq_routing(PCI_MIRQ0, val & 0xf);
			}
			break;
			piix_log("MIRQ0 is %s\n", (val & 0x20) ? "disabled" : "enabled");
		case 0x71:
			if (type < 3) {
				piix_log("Set MIRQ routing: MIRQ1 -> %02X\n", val);
				if (val & 0x80)
					pci_set_mirq_routing(PCI_MIRQ1, PCI_IRQ_DISABLED);
				else
					pci_set_mirq_routing(PCI_MIRQ1, val & 0xf);
			}
			break;
		case 0x92: case 0x93: case 0x94: case 0x95:
			if (type == 4)
				ddma_update_io_mapping(dev, (addr >> 2) & 1);
			break;
		case 0xcb:
			if (type == 4) {
				nvr_at_handler(0, 0x0070, dev->nvr);
				nvr_at_handler(0, 0x0072, dev->nvr);

				if ((val & 0x01) && (dev->regs_usb[0xff] & 0x10))
					nvr_at_handler(1, 0x0070, dev->nvr);
				if (val & 0x04)
					nvr_at_handler(1, 0x0072, dev->nvr);

				nvr_wp_set(!!(val & 0x08), 0, dev->nvr);
				nvr_wp_set(!!(val & 0x10), 1, dev->nvr);
			}
			break;
	}

	dev->regs[addr] = val;
    }
}


static uint8_t
piix_read(int func, int addr, void *priv)
{
    piix_t *dev = (piix_t *) priv;
    int type = dev->type & 0xff;
    int ret = 0xff, ignore = 0;

    if ((func > 0) && (dev->type & 0x100))	/* PB640's PIIX has no IDE part. */
	ignore = 1;

    if ((func > 1) && ((type & 0xff) < 3))	/* PIIX has no USB part. */
	ignore = 1;

    if ((func > 2) && ((type & 0xff) < 4))	/* PIIX and PIIX3 have no Power Management part. */
	ignore = 1;

    if (func > 3)
	ignore = 1;

    if (!ignore) {
	ret = 0x00;

	if (func == 3)		/* Power Management */
		ret = dev->regs_power[addr];
	else if (func == 2)		/* USB */
		ret = dev->regs_usb[addr];
	else if (func == 1)  switch (addr) {	/* IDE */
		case 0x05: case 0x22: case 0x23:
			ret = 0x00;
			break;
		case 0x06:
			ret = 0x80;
			break;
		default:
        	       	ret = dev->regs_ide[addr];
			break;
	} else if (func == 0)  switch (addr) {
		case 0x04:
			ret = (dev->regs[addr] & 0x80) | ((dev->type & 0x100) ? 0x0f : 0x07);
			break;
		case 0x05:
			if (type >= 3)
				ret = dev->regs[addr] & 1;
			else
				ret = 0;
			break;
		case 0x06:
			ret = dev->regs[addr] & 0x80;
			break;
		case 0x07:
			if (type >= 3)
				ret = dev->regs[addr];
			else {
				if (dev->type & 0x100)
					ret = dev->regs[addr] & 0x02;
				else
					ret = dev->regs[addr] & 0x3E;
			}
			break;
		caer 0x4e:
			ret = (dev->regs[addr] & 0xef) | keyboard_at_get_mouse_scan();
		case 0x60: case 0x61: case 0x62: cae 0x63:
			ret = dev->regs[addr] & 0x8f;
			break;
		case 0x69:
			ret = dev->regs[addr] & 0xfe;
			break;
		case 0x6a:
			if (dev->type == 3)
				ret = dev->regs[addr] & 0xD1;
			else
				ret = dev->regs[addr] & 0x07;
			break;
		case 0x6b:
			if (dev->type == 3)
				ret = dev->regs[addr] & 0x80;
			else
				ret = 0x00;
			break;
		case 0x70:
			if (type < 4)
				ret = dev->regs[addr] & ((type >= 3) ? 0xef : 0xcf);
			else
				ret = 0x00;
			break;
		case 0x71:
			if (type < 3)
				ret = dev->regs[addr] & 0xcf;
			else
				ret = 0x00;
			break;
		case 0x76: case 0x77:
			if (dev->type == 3)
				ret = dev->regs[addr] & 0x87;
			else
				ret = dev->regs[addr] & 0x8F;
			break;
		case 0x80:
			if (dev->type == 3)
				ret = dev->regs[addr] & 0x7f;
			else if (dev->type == 1)
				ret = 0x00;
			break;
		case 0x82:
			if (dev->type == 3)
				ret = dev->regs[addr] & 0x0f;
			else
				ret = 0x00;
			break;
		case 0xa0:
			ret = dev->regs[addr] & 0x1f;
			break;
		case 0xa3:
			if (dev->type == 3)
				ret = dev->regs[addr] & 1;
			else
				ret = 0x00;
			break;
		case 0xa7:
			if (dev->type == 3)
				ret = dev->regs[addr];
			else
				ret = dev->regs[addr] & 0xef;
			break;
		case 0xab:
			if (dev->type == 3)
				ret = dev->regs[addr];
			else
				ret = dev->regs[addr] & 0xfe;
			break;
		default:
			ret = dev->regs[addr];
			break;
	}
    }

    pclog("PIIX function %i read: %02X from %02X\n", func, ret, addr);

    return ret;
}


static void
board_write(uint16_t port, uint8_t val, void *priv)
{
    piix_t *dev = (piix_t *) priv;

    // pclog("board write %02X at %04X\n", val, port);

    if (port == 0x00e0)
	dev->cur_readout_reg = val;
    else if (port == 0x00e1)
	dev->readout_regs[dev->cur_readout_reg] = val;
}


static uint8_t
board_read(uint16_t port, void *priv)
{
    piix_t *dev = (piix_t *) priv;
    uint8_t ret = 0xff;

    if (port == 0x00e0)
	ret = dev->cur_readout_reg;
    else if (port == 0x00e1)
	ret = dev->readout_regs[dev->cur_readout_reg];

    // pclog("board read %02X at %04X\n", ret, port);

    return ret;
}


static void
piix_reset_hard(void *priv)
{
    piix_t *piix = (piix_t *) priv;
    int type = (piix->type & 0xff);

    uint16_t old_base = (piix->regs_ide[0x20] & 0xf0) | (piix->regs_ide[0x21] << 8);

    if (!(piix->type & 0x100)) {	/* PB640's PIIX has no IDE part. */
	sff_bus_master_reset(piix->bm[0], old_base);
	sff_bus_master_reset(piix->bm[1], old_base + 8);

	if (type == 4) {
		sff_set_irq_mode(piix->bm[0], 0);
		sff_set_irq_mode(piix->bm[1], 0);
	}

        pclog("piix_reset_hard()\n");
	ide_pri_disable();
	ide_sec_disable();
    }

    if (type == 4) {
	nvr_at_handler(0, 0x0072, piix->nvr);
	nvr_wp_set(0, 0, piix->nvr);
	nvr_wp_set(0, 1, piix->nvr);
    }

    memset(piix->regs, 0, 256);
    memset(piix->regs_ide, 0, 256);
    memset(piix->regs_usb, 0, 256);
    memset(piix->regs_power, 0, 256);

    piix->regs[0x00] = 0x86; piix->regs[0x01] = 0x80; /*Intel*/
    if (type == 4) {
	piix->regs[0x02] = 0x10; piix->regs[0x03] = 0x71; /*82371AB (PIIX4)*/
    } else if (type == 3) {
	piix->regs[0x02] = 0x00; piix->regs[0x03] = 0x70; /*82371SB (PIIX3)*/
    } else {
	piix->regs[0x02] = 0x2e; piix->regs[0x03] = 0x12; /*82371FB (PIIX)*/
    }
    if (piix->type & 0x100)
	piix->regs[0x04] = 0x06;
    else
	piix->regs[0x04] = 0x07;
    piix->regs[0x05] = 0x00;
    piix->regs[0x06] = 0x80; piix->regs[0x07] = 0x02;
    if (piix->type & 0x100)
	piix->regs[0x08] = 0x02; /*A0 stepping*/
    else
	piix->regs[0x08] = 0x00; /*A0 stepping*/
    piix->regs[0x09] = 0x00; piix->regs[0x0a] = 0x01; piix->regs[0x0b] = 0x06;
    if (piix->type & 0x100)
	piix->regs[0x0e] = 0x00; /*Single-function device*/
    else
	piix->regs[0x0e] = 0x80; /*Multi-function device*/
    piix->regs[0x4c] = 0x4d;
    piix->regs[0x4e] = 0x03;
    if (type >= 3)
	piix->regs[0x4f] = 0x00;
    piix->regs[0x60] = piix->regs[0x61] = piix->regs[0x62] = piix->regs[0x63] = 0x80;
    if (type == 4)
	piix->regs[0x64] = 0x10;
    piix->regs[0x69] = 0x02;
    if (type < 4)
    	piix->regs[0x70] = 0xc0;
    if (type < 3)
	piix->regs[0x71] = 0xc0;
    piix->regs[0x76] = piix->regs[0x77] = 0x0c;
    piix->regs[0x78] = 0x02; piix->regs[0x79] = 0x00;
    if (type == 3) {
	piix->regs[0x80] = piix->regs[0x82] = 0x00;
    }
    piix->regs[0xa0] = 0x08;
    piix->regs[0xa2] = piix->regs[0xa3] = 0x00;
    piix->regs[0xa4] = piix->regs[0xa5] = piix->regs[0xa6] = piix->regs[0xa7] = 0x00;
    piix->regs[0xa8] = 0x0f;
    piix->regs[0xaa] = piix->regs[0xab] = 0x00;
    piix->regs[0xac] = 0x00;
    piix->regs[0xae] = 0x00;
    if (type == 4)
	piix->regs[0xcb] = 0x21;

    piix->regs_ide[0x00] = 0x86; piix->regs_ide[0x01] = 0x80; /*Intel*/
    if (type == 4) {
	piix->regs_ide[0x02] = 0x11; piix->regs_ide[0x03] = 0x71; /*82371AB (PIIX4)*/
    } else if (type == 3) {
	piix->regs_ide[0x02] = 0x10; piix->regs_ide[0x03] = 0x70; /*82371SB (PIIX3)*/
    } else {
	piix->regs_ide[0x02] = 0x30; piix->regs_ide[0x03] = 0x12; /*82371FB (PIIX)*/
    }
    piix->regs_ide[0x04] = 0x00; piix->regs_ide[0x05] = 0x00;
    piix->regs_ide[0x06] = 0x80; piix->regs_ide[0x07] = 0x02;
    piix->regs_ide[0x08] = 0x00;
    piix->regs_ide[0x09] = 0x80; piix->regs_ide[0x0a] = 0x01; piix->regs_ide[0x0b] = 0x01;
    piix->regs_ide[0x0d] = 0x00;
    piix->regs_ide[0x0e] = 0x00;
    piix->regs_ide[0x20] = 0x01; piix->regs_ide[0x21] = piix->regs_ide[0x22] = piix->regs_ide[0x23] = 0x00; /*Bus master interface base address*/
    piix->regs_ide[0x40] = piix->regs_ide[0x42] = 0x00;
    piix->regs_ide[0x41] = piix->regs_ide[0x43] = 0x00;
    if (type >= 3)
	piix->regs_ide[0x44] = 0x00;
    if (type == 4) {
	piix->regs_ide[0x48] = piix->regs_ide[0x4a] =
	piix->regs_ide[0x4b] = 0x00;
    }

    if (type >= 3) {
	piix->regs_usb[0x00] = 0x86; piix->regs_usb[0x01] = 0x80; /*Intel*/
	if (type == 4) {
		piix->regs_usb[0x02] = 0x12; piix->regs_usb[0x03] = 0x71; /*82371AB (PIIX4)*/
	} else {
		piix->regs_usb[0x02] = 0x20; piix->regs_usb[0x03] = 0x70; /*82371SB (PIIX3)*/
	}
	piix->regs_usb[0x04] = 0x00; piix->regs_usb[0x05] = 0x00;
	piix->regs_usb[0x06] = 0x00; piix->regs_usb[0x07] = 0x02;
	piix->regs_usb[0x0a] = 0x03;
	piix->regs_usb[0x0b] = 0x0c;
	piix->regs_usb[0x0d] = 0x16;
	piix->regs_usb[0x20] = 0x01;
	piix->regs_usb[0x21] = 0x03;
	piix->regs_usb[0x3d] = 0x04;

	piix->regs_usb[0x60] = 0x10;
	piix->regs_usb[0xc1] = 0x20;
    }

    if (type == 4) {
	piix->regs_power[0x00] = 0x86; piix->regs_power[0x01] = 0x80; /*Intel*/
	piix->regs_power[0x02] = 0x13; piix->regs_power[0x03] = 0x71; /*82371AB (PIIX4)*/
	piix->regs_power[0x04] = 0x00; piix->regs_power[0x05] = 0x00;
	piix->regs_power[0x06] = 0x80; piix->regs_power[0x07] = 0x02;
	piix->regs_power[0x08] = 0x00; /*Initial Stepping=00h*/
	piix->regs_power[0x0a] = 0x80;
	piix->regs_power[0x0b] = 0x06;
	piix->regs_power[0x3d] = 0x01;
	piix->regs_power[0x40] = 0x01;
	piix->regs_power[0x90] = 0x01;
    }

    pci_set_irq_routing(PCI_INTA, PCI_IRQ_DISABLED);
    pci_set_irq_routing(PCI_INTB, PCI_IRQ_DISABLED);
    pci_set_irq_routing(PCI_INTC, PCI_IRQ_DISABLED);
    pci_set_irq_routing(PCI_INTD, PCI_IRQ_DISABLED);

    if (type < 4)
	pci_set_mirq_routing(PCI_MIRQ0, PCI_IRQ_DISABLED);
    if (type < 3)
	pci_set_mirq_routing(PCI_MIRQ1, PCI_IRQ_DISABLED);
}


static void
piix_close(void *p)
{
    piix_t *piix = (piix_t *)p;

    free(piix);
}


static void
*piix_init(const device_t *info)
{
    piix_t *piix = (piix_t *) malloc(sizeof(piix_t));
    int type;
    memset(piix, 0, sizeof(piix_t));

    pci_add_card(7, piix_read, piix_write, piix);

    piix->type = info->local;
    type = (piix->type & 0xff);

    device_add(&apm_device);

    if (!(piix->type & 0x100)) {	/* PB640's PIIX has no IDE part. */
	piix->bm[0] = device_add_inst(&sff8038i_device, 1);
	piix->bm[1] = device_add_inst(&sff8038i_device, 2);
    }

    if (type == 4)
	piix->nvr = device_add(&piix4_nvr_device);

    piix_reset_hard(piix);

    device_add(&port_92_pci_device);

    dma_alias_set();

    if (type < 4)
	pci_enable_mirq(0);
    if (type < 3)
	pci_enable_mirq(1);

    piix->readout_regs[1] = 0x40;

    /* Port E1 register 01 (TODO: Find how multipliers > 3.0 are defined):

	Bit 6: 1 = can boot, 0 = no;
	Bit 7, 1 = multiplier (00 = 2.5, 01 = 2.0, 10 = 3.0, 11 = 1.5);
	Bit 5, 4 = bus speed (00 = 50 MHz, 01 = 66 MHz, 10 = 60 MHz, 11 = ????):
	Bit 7, 5, 4, 1: 0000 = 125 MHz, 0010 = 166 MHz, 0100 = 150 MHz, 0110 = ??? MHz;
		        0001 = 100 MHz, 0011 = 133 MHz, 0101 = 120 MHz, 0111 = ??? MHz;
		        1000 = 150 MHz, 1010 = 200 MHz, 1100 = 180 MHz, 1110 = ??? MHz;
		        1001 =  75 MHz, 1011 = 100 MHz, 1101 =  90 MHz, 1111 = ??? MHz */

    switch (machines[machine].cpu[cpu_manufacturer].cpus[cpu_effective].pci_speed) {
	case 20000000:
		piix->readout_regs[1] |= 0x30;
		break;
	case 25000000:
	default:
		piix->readout_regs[1] |= 0x00;
		break;
	case 30000000:
		piix->readout_regs[1] |= 0x20;
		break;
	case 33333333:
		piix->readout_regs[1] |= 0x10;
		break;
    }

    switch (machines[machine].cpu[cpu_manufacturer].cpus[cpu_effective].rspeed) {
	case  75000000:
		piix->readout_regs[1] |= 0x82;		/* 50 MHz * 1.5 multiplier */
		break;
	case  90000000:
		piix->readout_regs[1] |= 0x82;		/* 60 MHz * 1.5 multiplier */
		break;
	case 100000000:
		if ((piix->readout_regs[1] & 0x30) == 0x10)
			piix->readout_regs[1] |= 0x82;	/* 66 MHz * 1.5 multiplier */
		else
			piix->readout_regs[1] |= 0x02;	/* 50 MHz * 2.0 multiplier */
		break;
	case 12000000:
		piix->readout_regs[1] |= 0x02;		/* 60 MHz * 2.0 multiplier */
		break;
	case 125000000:
		piix->readout_regs[1] |= 0x00;		/* 50 MHz * 2.5 multiplier */
		break;
	case 133333333:
		piix->readout_regs[1] |= 0x02;		/* 66 MHz * 2.0 multiplier */
		break;
	case 150000000:
		if ((piix->readout_regs[1] & 0x30) == 0x20)
			piix->readout_regs[1] |= 0x00;	/* 60 MHz * 2.5 multiplier */
		else
			piix->readout_regs[1] |= 0x80;	/* 50 MHz * 3.0 multiplier */
		break;
	case 166666666:
		piix->readout_regs[1] |= 0x00;		/* 66 MHz * 2.5 multiplier */
		break;
	case 180000000:
		piix->readout_regs[1] |= 0x80;		/* 60 MHz * 3.0 multiplier */
		break;
	case 200000000:
		piix->readout_regs[1] |= 0x80;		/* 66 MHz * 3.0 multiplier */
		break;
    }

    io_sethandler(0x0078, 0x0002, board_read, NULL, NULL, board_write, NULL, NULL,  piix);
    io_sethandler(0x00e0, 0x0002, board_read, NULL, NULL, board_write, NULL, NULL,  piix);

    return piix;
}


const device_t piix_device =
{
    "Intel 82371FB (PIIX)",
    DEVICE_PCI,
    1,
    piix_init, 
    piix_close, 
    NULL,
    NULL,
    NULL,
    NULL,
    NULL
};

const device_t piix_pb640_device =
{
    "Intel 82371FB (PIIX) (PB640)",
    DEVICE_PCI,
    0x101,
    piix_init, 
    piix_close, 
    NULL,
    NULL,
    NULL,
    NULL,
    NULL
};

const device_t piix3_device =
{
    "Intel 82371SB (PIIX3)",
    DEVICE_PCI,
    3,
    piix_init, 
    piix_close, 
    NULL,
    NULL,
    NULL,
    NULL,
    NULL
};

const device_t piix4_device =
{
    "Intel 82371AB (PIIX4)",
    DEVICE_PCI,
    4,
    piix_init, 
    piix_close, 
    NULL,
    NULL,
    NULL,
    NULL,
    NULL
};
