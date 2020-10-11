/*
 * 86Box	A hypervisor and IBM PC system emulator that specializes in
 *		running old operating systems and software designed for IBM
 *		PC systems and compatibles from 1981 through fairly recent
 *		system designs based on the PCI bus.
 *
 *		Emulation of the VIA PIPC southbridges.
 *
 *
 *
 * Authors:	Sarah Walker, <http://pcem-emulator.co.uk/>
 *		Miran Grca, <mgrca8@gmail.com>
 *		Melissa Goad, <mszoopers@protonmail.com>
 *		RichardG, <richardg867@gmail.com>
 *
 *		Copyright 2008-2020 Sarah Walker.
 *		Copyright 2016-2020 Miran Grca.
 *		Copyright 2020 Melissa Goad.
 *		Copyright 2020 RichardG.
 */

#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>
#define HAVE_STDARG_H
#include <86box/86box.h>
#include <86box/cdrom.h>
#include "cpu.h"
#include <86box/scsi_device.h>
#include <86box/scsi_cdrom.h>
#include <86box/dma.h>
#include <86box/io.h>
#include <86box/device.h>
#include <86box/apm.h>
#include <86box/keyboard.h>
#include <86box/mem.h>
#include <86box/timer.h>
#include <86box/nvr.h>
#include <86box/acpi.h>
#include <86box/pci.h>
#include <86box/pic.h>
#include <86box/port_92.h>
#include <86box/hdc.h>
#include <86box/hdc_ide.h>
#include <86box/hdc_ide_sff8038i.h>
#include <86box/usb.h>
#include <86box/zip.h>
#include <86box/machine.h>
#include <86box/smbus_piix4.h>
#include <86box/chipset.h>


/* Most revision numbers (PCI-ISA bridge or otherwise) were lifted from PCI device
   listings on forums, as VIA's datasheets are not very helpful regarding those. */
#define VIA_PIPC_586A	0x05862500
#define VIA_PIPC_586B	0x05864700
#define VIA_PIPC_596A	0x05961200
#define VIA_PIPC_596B	0x05962300
#define VIA_PIPC_686A	0x06861400
#define VIA_PIPC_686B	0x06864000


typedef struct
{
    uint32_t	local;
    uint8_t	max_func;

    uint8_t	pci_isa_regs[256];
    uint8_t	ide_regs[256];
    uint8_t	usb_regs[2][256];
    uint8_t	power_regs[256];
    uint8_t	ac97_regs[2][256];
    sff8038i_t	*bm[2];
    nvr_t	*nvr;
    int		nvr_enabled, slot;
    smbus_piix4_t *smbus;
    usb_t	*usb[2];
    acpi_t	*acpi;
} pipc_t;


#ifdef ENABLE_PIPC_LOG
int pipc_do_log = ENABLE_PIPC_LOG;


static void
pipc_log(const char *fmt, ...)
{
    va_list ap;

    if (pipc_do_log) {
	va_start(ap, fmt);
	pclog_ex(fmt, ap);
	va_end(ap);
    }
}
#else
#define pipc_log(fmt, ...)
#endif


static void
pipc_reset_hard(void *priv)
{
    int i;

    pipc_log("PIPC: reset_hard()\n");

    pipc_t *dev = (pipc_t *) priv;
    uint16_t old_base = (dev->ide_regs[0x20] & 0xf0) | (dev->ide_regs[0x21] << 8);

    sff_bus_master_reset(dev->bm[0], old_base);
    sff_bus_master_reset(dev->bm[1], old_base + 8);

    memset(dev->pci_isa_regs, 0, 256);
    memset(dev->ide_regs, 0, 256);
    memset(dev->usb_regs, 0, 512);
    memset(dev->power_regs, 0, 256);
    memset(dev->ac97_regs, 0, 512);

    dev->pci_isa_regs[0x00] = 0x06; dev->pci_isa_regs[0x01] = 0x11;
    dev->pci_isa_regs[0x02] = dev->local >> 16;
    dev->pci_isa_regs[0x03] = dev->local >> 24;
    dev->pci_isa_regs[0x04] = (dev->local <= VIA_PIPC_586B) ? 0x0f : 0x87;
    dev->pci_isa_regs[0x07] = 0x02;
    dev->pci_isa_regs[0x08] = dev->local >> 8;
    dev->pci_isa_regs[0x0a] = 0x01;
    dev->pci_isa_regs[0x0b] = 0x06;
    dev->pci_isa_regs[0x0e] = 0x80;

    dev->pci_isa_regs[0x48] = 0x01;
    dev->pci_isa_regs[0x4a] = 0x04;
    dev->pci_isa_regs[0x4f] = 0x03;

    dev->pci_isa_regs[0x50] = (dev->local >= VIA_PIPC_686A) ? 0x2d : 0x24;
    dev->pci_isa_regs[0x59] = 0x04;
    if (dev->local >= VIA_PIPC_686A)
	dev->pci_isa_regs[0x5a] = dev->pci_isa_regs[0x5f] = 0x04;

    dma_e = 0x00;
    for (i = 0; i < 8; i++) {
	dma[i].ab &= 0xffff000f;
	dma[i].ac &= 0xffff000f;
    }

    pic_set_shadow(0);

    /* IDE registers */
    dev->max_func++;
    dev->ide_regs[0x00] = 0x06; dev->ide_regs[0x01] = 0x11;
    dev->ide_regs[0x02] = 0x71; dev->ide_regs[0x03] = 0x05;
    dev->ide_regs[0x04] = 0x80;
    dev->ide_regs[0x06] = (dev->local == VIA_PIPC_686A) ? 0x90 : 0x80; dev->ide_regs[0x07] = 0x02;
    dev->ide_regs[0x08] = (dev->local == VIA_PIPC_596B) ? 0x10 : 0x06; /* only 596B has rev 0x10? */
    dev->ide_regs[0x09] = 0x85;
    dev->ide_regs[0x0a] = 0x01;
    dev->ide_regs[0x0b] = 0x01;

    dev->ide_regs[0x10] = 0xf1; dev->ide_regs[0x11] = 0x01;
    dev->ide_regs[0x14] = 0xf5; dev->ide_regs[0x15] = 0x03;
    dev->ide_regs[0x18] = 0x71; dev->ide_regs[0x19] = 0x01;
    dev->ide_regs[0x1c] = 0x75; dev->ide_regs[0x1d] = 0x03;
    dev->ide_regs[0x20] = 0x01; dev->ide_regs[0x21] = 0xcc;
    if (dev->local >= VIA_PIPC_686A)
	dev->ide_regs[0x34] = 0xc0;
    dev->ide_regs[0x3c] = 0x0e;

    if (dev->local < VIA_PIPC_686A)
	dev->ide_regs[0x40] = 0x08;
    dev->ide_regs[0x41] = 0x02;
    dev->ide_regs[0x42] = 0x09;
    dev->ide_regs[0x43] = (dev->local >= VIA_PIPC_686A) ? 0x0a : 0x3a;
    dev->ide_regs[0x44] = 0x68;
    dev->ide_regs[0x46] = 0xc0;
    dev->ide_regs[0x48] = 0xa8; dev->ide_regs[0x49] = 0xa8;
    dev->ide_regs[0x4a] = 0xa8; dev->ide_regs[0x4b] = 0xa8;
    dev->ide_regs[0x4c] = 0xff;
    dev->ide_regs[0x4e] = 0xff;
    dev->ide_regs[0x4f] = 0xff;
    dev->ide_regs[0x50] = 0x03; dev->ide_regs[0x51] = 0x03;
    dev->ide_regs[0x52] = 0x03; dev->ide_regs[0x53] = 0x03;
    if (dev->local >= VIA_PIPC_596A)
	dev->ide_regs[0x54] = 0x06;

    dev->ide_regs[0x61] = 0x02;
    dev->ide_regs[0x69] = 0x02;

    if (dev->local >= VIA_PIPC_686A) {
	dev->ide_regs[0xc0] = 0x01;
	dev->ide_regs[0xc2] = 0x02;
    }

    for (i = 0; i <= (dev->local >= VIA_PIPC_686A); i++) {
	dev->max_func++;
	dev->usb_regs[i][0x00] = 0x06; dev->usb_regs[i][0x01] = 0x11;
	dev->usb_regs[i][0x02] = 0x38; dev->usb_regs[i][0x03] = 0x30;
	dev->usb_regs[i][0x04] = 0x00; dev->usb_regs[i][0x05] = 0x00;
	dev->usb_regs[i][0x06] = 0x00; dev->usb_regs[i][0x07] = 0x02;
	switch (dev->local) {
		case VIA_PIPC_586A:
		case VIA_PIPC_586B:
			dev->usb_regs[i][0x08] = 0x02;
			break;

		case VIA_PIPC_596A:
			dev->usb_regs[i][0x08] = 0x08;
			break;

		case VIA_PIPC_596B:
			dev->usb_regs[i][0x08] = 0x11;
			break;

		case VIA_PIPC_686A:
			dev->usb_regs[i][0x08] = 0x06;
			break;

		case VIA_PIPC_686B:
			dev->usb_regs[i][0x08] = 0x16;
			break;
	}

	dev->usb_regs[i][0x0a] = 0x03;
	dev->usb_regs[i][0x0b] = 0x0c;
	dev->usb_regs[i][0x0d] = 0x16;
	dev->usb_regs[i][0x20] = 0x01;
	dev->usb_regs[i][0x21] = 0x03;
	dev->usb_regs[i][0x3d] = 0x04;

	dev->usb_regs[i][0x60] = 0x10;
	if (dev->local >= VIA_PIPC_686A) {
		dev->usb_regs[i][0x80] = 0x01;
		dev->usb_regs[i][0x82] = 0x02;
	}
	dev->usb_regs[i][0xc1] = 0x20;
    }

    if (dev->acpi) {
	dev->max_func++;
	dev->power_regs[0x00] = 0x06; dev->power_regs[0x01] = 0x11;
	if (dev->local <= VIA_PIPC_586B)
		dev->power_regs[0x02] = 0x40;
	else if (dev->local <= VIA_PIPC_596B)
		dev->power_regs[0x02] = 0x50;
	else
		dev->power_regs[0x02] = 0x57;
	dev->power_regs[0x03] = 0x30;
	dev->power_regs[0x04] = 0x00; dev->power_regs[0x05] = 0x00;
	dev->power_regs[0x06] = (dev->local == VIA_PIPC_686B) ? 0x90 : 0x80; dev->power_regs[0x07] = 0x02;
	switch (dev->local) {
		case VIA_PIPC_586B:
		case VIA_PIPC_686A:
			dev->power_regs[0x08] = 0x10;
			break;

		case VIA_PIPC_596A:
			dev->power_regs[0x08] = 0x20;
			break;

		case VIA_PIPC_596B:
			dev->power_regs[0x08] = 0x30;
			break;

		case VIA_PIPC_686B:
			dev->power_regs[0x08] = 0x40;
			break;
	}
	if (dev->local >= VIA_PIPC_686A)
		dev->power_regs[0x42] = 0x40; /* external suspend-related pin, must be set */
	dev->power_regs[0x48] = 0x01;

	if (dev->local == VIA_PIPC_596A)
		dev->power_regs[0x80] = 0x01;
	else if (dev->local >= VIA_PIPC_596B)
		dev->power_regs[0x90] = 0x01;
    }

    if (dev->local >= VIA_PIPC_686A) {
	for (i = 0; i <= 1; i++) {
		dev->max_func++;
		dev->ac97_regs[i][0x00] = 0x06; dev->ac97_regs[i][0x01] = 0x11;
		dev->ac97_regs[i][0x02] = 0x58 + (0x10 * i); dev->ac97_regs[i][0x03] = 0x30;
		dev->ac97_regs[i][0x06] = 0x10 * (1 - i); dev->ac97_regs[i][0x07] = 0x02;
		dev->ac97_regs[i][0x08] = (dev->local == VIA_PIPC_686A) ? 0x12 : 0x50;

		if (i == 0) {
			dev->ac97_regs[i][0x0a] = 0x01;
			dev->ac97_regs[i][0x0b] = 0x04;
		} else {
			dev->ac97_regs[i][0x0a] = 0x80;
			dev->ac97_regs[i][0x0b] = 0x07;
		}

		dev->ac97_regs[i][0x10] = 0x01;
		dev->ac97_regs[i][0x14] = 0x01;

		dev->ac97_regs[i][0x3d] = 0x03;

		dev->ac97_regs[i][0x43] = 0x1c;
	}
    }

    pci_set_irq_routing(PCI_INTA, PCI_IRQ_DISABLED);
    pci_set_irq_routing(PCI_INTB, PCI_IRQ_DISABLED);
    pci_set_irq_routing(PCI_INTC, PCI_IRQ_DISABLED);
    pci_set_irq_routing(PCI_INTD, PCI_IRQ_DISABLED);

    if (dev->local <= VIA_PIPC_586B) {
	pci_set_mirq_routing(PCI_MIRQ0, PCI_IRQ_DISABLED);
	pci_set_mirq_routing(PCI_MIRQ1, PCI_IRQ_DISABLED);
	if (dev->local == VIA_PIPC_586B)
		pci_set_mirq_routing(PCI_MIRQ2, PCI_IRQ_DISABLED);
    }

    ide_pri_disable();
    ide_sec_disable();
}


static void
pipc_ide_handlers(pipc_t *dev)
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
pipc_ide_irqs(pipc_t *dev)
{
    int irq_mode[2] = { 0, 0 };

    if (dev->ide_regs[0x09] & 0x01)
	irq_mode[0] = (dev->ide_regs[0x3d] & 0x01);

    if (dev->ide_regs[0x09] & 0x04)
	irq_mode[1] = (dev->ide_regs[0x3d] & 0x01);

    sff_set_irq_mode(dev->bm[0], 0, irq_mode[0]);
    sff_set_irq_mode(dev->bm[0], 1, irq_mode[1]);

    sff_set_irq_mode(dev->bm[1], 0, irq_mode[0]);
    sff_set_irq_mode(dev->bm[1], 1, irq_mode[1]);
}


static void
pipc_bus_master_handlers(pipc_t *dev)
{
    uint16_t base = (dev->ide_regs[0x20] & 0xf0) | (dev->ide_regs[0x21] << 8);

    sff_bus_master_handler(dev->bm[0], (dev->ide_regs[0x04] & 1), base);
    sff_bus_master_handler(dev->bm[1], (dev->ide_regs[0x04] & 1), base + 8);
}


static uint8_t
pipc_read(int func, int addr, void *priv)
{
    pipc_t *dev = (pipc_t *) priv;
    uint8_t ret = 0xff;
    int c;
    uint8_t pm_func = dev->usb[1] ? 4 : 3;

    if (func > dev->max_func)
	return ret;
    else if (func == 0) { /* PCI-ISA bridge */
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
    }
    else if ((func == 1) && !(dev->pci_isa_regs[0x48] & 0x02)) /* IDE */
	ret = dev->ide_regs[addr];
    else if ((func < pm_func) && !((func == 2) ? (dev->pci_isa_regs[0x48] & 0x04) : (dev->pci_isa_regs[0x85] & 0x10))) /* USB */
	ret = dev->usb_regs[func - 2][addr];
    else if (func == pm_func) /* Power */
	ret = dev->power_regs[addr];
    else if ((func <= (pm_func + 2)) && !(dev->pci_isa_regs[0x85] & ((func == (pm_func + 1)) ? 0x04 : 0x08))) /* AC97 / MC97 */
	ret = dev->ac97_regs[func - pm_func - 1][addr];

    pipc_log("PIPC: read(%d, %02X) = %02X\n", func, addr, ret);

    return ret;
}


static void
nvr_update_io_mapping(pipc_t *dev)
{
    if (dev->nvr_enabled)
	nvr_at_handler(0, 0x0074, dev->nvr);

    if ((dev->pci_isa_regs[0x5b] & 0x02) && (dev->pci_isa_regs[0x48] & 0x08))
	nvr_at_handler(1, 0x0074, dev->nvr);
}


static void
usb_update_io_mapping(pipc_t *dev, int func)
{
    uhci_update_io_mapping(dev->usb[func - 2], dev->usb_regs[func - 2][0x20] & ~0x1f, dev->usb_regs[func - 2][0x21], dev->usb_regs[func - 2][PCI_REG_COMMAND] & PCI_COMMAND_IO);
}


static void
pipc_write(int func, int addr, uint8_t val, void *priv)
{
    pipc_t *dev = (pipc_t *) priv;
    int c;
    uint8_t pm_func = dev->usb[1] ? 4 : 3;

    if (func > dev->max_func)
	return;

    pipc_log("PIPC: write(%d, %02X, %02X)\n", func, addr, val);

    if (func == 0) { /* PCI-ISA bridge */
	/* Read-only addresses */
	if ((addr < 4) || (addr == 5) || ((addr >= 8) && (addr < 0x40)) || (addr == 0x49) || (addr == 0x4b) ||
	    (addr == 0x53) || ((addr >= 0x5d) && (addr < 0x5f)) || ((addr >= 0x71) && (addr < 0x74)) || (addr >= 0x90))
		return;

	if ((dev->local <= VIA_PIPC_586A) && ((addr >= 0x58) && (addr < 0x80)))
		return;

	if ((dev->local <= VIA_PIPC_586B) && (addr >= 0x74))
		return;

	if ((dev->local <= VIA_PIPC_596A) && ((addr == 0x51) || (addr == 0x52) || (addr == 0x5f) || (addr == 0x85) || 
	    (addr == 0x86) || ((addr >= 0x8a) && (addr < 0x90))))
		return;

	switch (addr) {
		case 0x04:
			dev->pci_isa_regs[0x04] = (val & 8) | 7;
			break;
		case 0x07:
			dev->pci_isa_regs[0x07] &= ~(val & 0xb0);
			break;

		case 0x47:
			if (val & 0x01)
				trc_write(0x0047, (val & 0x80) ? 0x06 : 0x04, NULL);
			pic_set_shadow(!!(val & 0x10));
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
			pci_set_irq_routing((dev->local >= VIA_PIPC_596A) ? PCI_INTA : PCI_INTD, (val & 0xf0) ? (val >> 4) : PCI_IRQ_DISABLED);
			if (dev->local <= VIA_PIPC_586B)
				pci_set_mirq_routing(PCI_MIRQ0, (val & 0x0f) ? (val & 0x0f) : PCI_IRQ_DISABLED);
			dev->pci_isa_regs[0x55] = val;
	                break;
                case 0x56:
			pci_set_irq_routing((dev->local >= VIA_PIPC_596A) ? PCI_INTC : PCI_INTA, (val & 0xf0) ? (val >> 4) : PCI_IRQ_DISABLED);
			pci_set_irq_routing(PCI_INTB, (val & 0x0f) ? (val & 0x0f) : PCI_IRQ_DISABLED);
			dev->pci_isa_regs[0x56] = val;
			break;
		case 0x57:
			pci_set_irq_routing((dev->local >= VIA_PIPC_596A) ? PCI_INTD : PCI_INTC, (val & 0xf0) ? (val >> 4) : PCI_IRQ_DISABLED);
			if (dev->local <= VIA_PIPC_586B)
				pci_set_mirq_routing(PCI_MIRQ1, (val & 0x0f) ? (val & 0x0f) : PCI_IRQ_DISABLED);
			dev->pci_isa_regs[0x57] = val;
			break;
		case 0x58:
			if (dev->local == VIA_PIPC_586B)
				pci_set_mirq_routing(PCI_MIRQ2, (val & 0x0f) ? (val & 0x0f) : PCI_IRQ_DISABLED);
			dev->pci_isa_regs[0x58] = val;
			break;
		case 0x5b:
			dev->pci_isa_regs[0x5b] = val;
			nvr_update_io_mapping(dev);
			break;

		case 0x60: case 0x62: case 0x64: case 0x66:
		case 0x6a: case 0x6c: case 0x6e:
			c = (addr & 0x0e) >> 1;
			dma[c].ab = (dma[c].ab & 0xffffff0f) | (val & 0xf0);
			dma[c].ac = (dma[c].ac & 0xffffff0f) | (val & 0xf0);
			if (val & 0x08)
				dma_e |= (1 << c);
			else
				dma_e &= ~(1 << c);
			break;
		case 0x61: case 0x63: case 0x65: case 0x67:
		case 0x6b: case 0x6d: case 0x6f:
			c = (addr & 0x0e) >> 1;
			dma[c].ab = (dma[c].ab & 0xffff00ff) | (val << 8);
			dma[c].ac = (dma[c].ac & 0xffff00ff) | (val << 8);
			break;

		case 0x70: case 0x71: case 0x72: case 0x73:
			dev->pci_isa_regs[(addr - 0x44)] = val;
			break;

		case 0x77:
			if (val & 0x10)
				pclog("PIPC: Warning: Internal I/O APIC enabled.\n");
			break;

		case 0x80: case 0x86: case 0x87:
			dev->pci_isa_regs[addr] &= ~(val);
			break;

		case 0x85:
			dev->pci_isa_regs[addr] = val;
			break;

		default:
			dev->pci_isa_regs[addr] = val;
			break;
	}
    } else if (func == 1) { /* IDE */
	/* Read-only addresses and disable bit */
	if ((addr < 4) || (addr == 5) || (addr == 8) || ((addr >= 0xa) && (addr < 0x0d)) ||
	    ((addr >= 0x0e) && (addr < 0x10)) || ((addr >= 0x12) && (addr < 0x13)) ||
	    ((addr >= 0x16) && (addr < 0x17)) || ((addr >= 0x1a) && (addr < 0x1b)) ||
	    ((addr >= 0x1e) && (addr < 0x1f)) || ((addr >= 0x22) && (addr < 0x3c)) ||
	    ((addr >= 0x3e) && (addr < 0x40)) || ((addr >= 0x55) && (addr < 0x60)) ||
	    ((addr >= 0x62) && (addr < 0x68)) || ((addr >= 0x6a) && (addr < 0x70)) ||
	    (addr == 0x72) || (addr == 0x73) || (addr == 0x76) || (addr == 0x77) ||
	    (addr == 0x7a) || (addr == 0x7b) || (addr == 0x7e) || (addr == 0x7f) ||
	    ((addr >= 0x84) && (addr < 0x88)) || (addr >= 0x8c) || (dev->pci_isa_regs[0x48] & 0x02))
		return;

	if ((dev->local <= VIA_PIPC_586B) && ((addr == 0x54) || (addr >= 0x70)))
		return;

	switch (addr) {
		case 0x04:
			dev->ide_regs[0x04] = val & 0x85;
			pipc_ide_handlers(dev);
			pipc_bus_master_handlers(dev);
			break;
		case 0x07:
			dev->ide_regs[0x07] &= ~(val & 0xf1);
			break;

		case 0x09:
			dev->ide_regs[0x09] = (val & 0x05) | 0x8a;
			pipc_ide_handlers(dev);
			pipc_ide_irqs(dev);
			break;

		case 0x10:
			dev->ide_regs[0x10] = (val & 0xf8) | 1;
			pipc_ide_handlers(dev);
			break;
		case 0x11:
			dev->ide_regs[0x11] = val;
			pipc_ide_handlers(dev);
			break;

		case 0x14:
			dev->ide_regs[0x14] = (val & 0xfc) | 1;
			pipc_ide_handlers(dev);
			break;
		case 0x15:
			dev->ide_regs[0x15] = val;
			pipc_ide_handlers(dev);
			break;

		case 0x18:
			dev->ide_regs[0x18] = (val & 0xf8) | 1;
			pipc_ide_handlers(dev);
			break;
		case 0x19:
			dev->ide_regs[0x19] = val;
			pipc_ide_handlers(dev);
			break;

		case 0x1c:
			dev->ide_regs[0x1c] = (val & 0xfc) | 1;
			pipc_ide_handlers(dev);
			break;
		case 0x1d:
			dev->ide_regs[0x1d] = val;
			pipc_ide_handlers(dev);
			break;

		case 0x20:
			dev->ide_regs[0x20] = (val & 0xf0) | 1;
			pipc_bus_master_handlers(dev);
			break;
		case 0x21:
			dev->ide_regs[0x21] = val;
			pipc_bus_master_handlers(dev);
			break;

		case 0x3d:
			dev->ide_regs[0x3d] = val & 0x01;
			pipc_ide_irqs(dev);
			break;

		case 0x40:
			dev->ide_regs[0x40] = val;
			pipc_ide_handlers(dev);
			break;

		default:
			dev->ide_regs[addr] = val;
			break;
	}
    } else if (func < pm_func) { /* USB */
	/* Read-only addresses */
	if ((addr < 4) || (addr == 5) || (addr == 6) || ((addr >= 8) && (addr < 0xd)) ||
	    ((addr >= 0xe) && (addr < 0x20)) || ((addr >= 0x22) && (addr < 0x3c)) ||
	    ((addr >= 0x3e) && (addr < 0x40)) || ((addr >= 0x42) && (addr < 0x44)) ||
	    ((addr >= 0x46) && (addr < 0xc0)) || (addr >= 0xc2))
		return;

	/* Check disable bits for both controllers */
	if ((func == 2) ? (dev->pci_isa_regs[0x48] & 0x04) : (dev->pci_isa_regs[0x85] & 0x10))
		return;

	switch (addr) {
		case 0x04:
			dev->usb_regs[func - 2][0x04] = val & 0x97;
			usb_update_io_mapping(dev, func);
			break;
		case 0x07:
			dev->usb_regs[func - 2][0x07] &= ~(val & 0x78);
			break;

		case 0x20:
			dev->usb_regs[func - 2][0x20] = (val & ~0x1f) | 1;
			usb_update_io_mapping(dev, func);
			break;
		case 0x21:
			dev->usb_regs[func - 2][0x21] = val;
			usb_update_io_mapping(dev, func);
			break;

		default:
			dev->usb_regs[func - 2][addr] = val;
			break;
	}
    } else if (func == pm_func) { /* Power */
	/* Read-only addresses */
	if ((addr < 0xd) || ((addr >= 0xe) && (addr < 0x40)) || (addr == 0x43) ||
	    (addr == 0x4e) || (addr == 0x4f) || (addr == 0x56) || (addr == 0x57) || ((addr >= 0x5c) && (addr < 0x61)) ||
	    ((addr >= 0x64) && (addr < 0x70)) || (addr == 0x72) || (addr == 0x73) || ((addr >= 0x75) && (addr < 0x80)) ||
	    (addr == 0x83) || ((addr >= 0x85) && (addr < 0x90)) || ((addr >= 0x92) && (addr < 0xd2)) || (addr >= 0xd7))
		return;

	if ((dev->local <= VIA_PIPC_586B) && ((addr == 0x48) || (addr == 0x4c) || (addr == 0x4d) || (addr >= 0x54)))
		return;

	if ((dev->local <= VIA_PIPC_596B) && ((addr >= 0x64) && (addr < (dev->local == VIA_PIPC_596A ? 0x80 : 0x85))))
		return;

	switch (addr) {
		case 0x41: case 0x48: case 0x49:
			dev->power_regs[addr] = val;
			c = (dev->power_regs[0x49] << 8);
			if (dev->local >= VIA_PIPC_596A)
				c |= (dev->power_regs[0x48] & 0x80);
			acpi_update_io_mapping(dev->acpi, c, dev->power_regs[0x41] & 0x80);
			break;

		case 0x61: case 0x62: case 0x63:
			dev->power_regs[(addr - 0x58)] = val;
			break;

		case 0x80: case 0x81: case 0x84: /* 596(A) has the SMBus I/O base here instead. Enable bit is assumed. */
			dev->power_regs[addr] = val;
			smbus_piix4_remap(dev->smbus, (dev->power_regs[0x81] << 8) | (dev->power_regs[0x80] & 0xf0), dev->power_regs[0x84] & 0x01);
			break;

		case 0x90: case 0x91: case 0xd2:
			dev->power_regs[addr] = val;
			smbus_piix4_remap(dev->smbus, (dev->power_regs[0x91] << 8) | (dev->power_regs[0x90] & 0xf0), dev->power_regs[0xd2] & 0x01);
			break;

		default:
			dev->power_regs[addr] = val;
			break;
	}
    } else if (func <= pm_func + 2) { /* AC97 / MC97 */
	/* Read-only addresses */
	if ((addr < 0x4) || ((addr >= 0x6) && (addr < 0xd)) || ((addr >= 0xe) && (addr < 0x10)) || ((addr >= 0x1c) && (addr < 0x2c)) ||
	    ((addr >= 0x30) && (addr < 0x34)) || ((addr >= 0x35) && (addr < 0x3c)) || ((addr >= 0x3d) && (addr < 0x41)) ||
	    ((addr >= 0x45) && (addr < 0x4a)) || (addr >= 0x4c))
		return;

	/* Also check disable bits for both controllers */
	if ((func == (pm_func + 1)) && ((addr == 0x44) || (dev->pci_isa_regs[0x85] & 0x04)))
		return;

	if ((func == (pm_func + 2)) && ((addr == 0x4a) || (addr == 0x4b) || (dev->pci_isa_regs[0x85] & 0x08)))
		return;

	switch (addr) {
		default:
			dev->ac97_regs[func - pm_func - 1][addr] = val;
			break;
	}
    }
}


static void *
pipc_init(const device_t *info)
{
    pipc_t *dev = (pipc_t *) malloc(sizeof(pipc_t));
    memset(dev, 0, sizeof(pipc_t));

    pipc_log("PIPC: init()\n");

    dev->local = info->local;
    dev->slot = pci_add_card(PCI_ADD_SOUTHBRIDGE, pipc_read, pipc_write, dev);

    dev->bm[0] = device_add_inst(&sff8038i_device, 1);
    sff_set_slot(dev->bm[0], dev->slot);
    sff_set_irq_mode(dev->bm[0], 0, 0);
    sff_set_irq_mode(dev->bm[0], 1, 0);
    sff_set_irq_pin(dev->bm[0], PCI_INTA);

    dev->bm[1] = device_add_inst(&sff8038i_device, 2);
    sff_set_slot(dev->bm[1], dev->slot);
    sff_set_irq_mode(dev->bm[1], 0, 0);
    sff_set_irq_mode(dev->bm[1], 1, 0);
    sff_set_irq_pin(dev->bm[1], PCI_INTA);

    dev->nvr = device_add(&via_nvr_device);

    if (dev->local >= VIA_PIPC_596A)
	dev->smbus = device_add(&piix4_smbus_device);

    if (dev->local >= VIA_PIPC_596A)
	dev->acpi = device_add(&acpi_via_596b_device);
    else if (dev->local >= VIA_PIPC_586B)
    	dev->acpi = device_add(&acpi_via_device);

    dev->usb[0] = device_add_inst(&usb_device, 1);
    if (dev->local >= VIA_PIPC_686A)
	dev->usb[1] = device_add_inst(&usb_device, 2);

    pipc_reset_hard(dev);

    device_add(&port_92_pci_device);

    dma_alias_set();

    if (dev->local <= VIA_PIPC_586B) {
	pci_enable_mirq(0);
	pci_enable_mirq(1);
	if (dev->local == VIA_PIPC_586B)
		pci_enable_mirq(2);
    }

    if (dev->acpi)
	acpi_init_gporeg(dev->acpi, 0xff, 0xbf, 0xff, 0x7f);

    return dev;
}

static void
pipc_close(void *p)
{
    pipc_t *pipc = (pipc_t *) p;

    pipc_log("PIPC: close()\n");

    free(pipc);
}


const device_t via_vt82c586b_device =
{
    "VIA VT82C586B",
    DEVICE_PCI,
    VIA_PIPC_586B,
    pipc_init, 
    pipc_close, 
    NULL,
    NULL,
    NULL,
    NULL,
    NULL
};

const device_t via_vt82c596_device =
{
    "VIA VT82C596(A)",
    DEVICE_PCI,
    VIA_PIPC_596A,
    pipc_init, 
    pipc_close, 
    NULL,
    NULL,
    NULL,
    NULL,
    NULL
};


const device_t via_vt82c596b_device =
{
    "VIA VT82C596B",
    DEVICE_PCI,
    VIA_PIPC_596B,
    pipc_init, 
    pipc_close, 
    NULL,
    NULL,
    NULL,
    NULL,
    NULL
};
