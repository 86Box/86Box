/*
 * 86Box	A hypervisor and IBM PC system emulator that specializes in
 *		running old operating systems and software designed for IBM
 *		PC systems and compatibles from 1981 through fairly recent
 *		system designs based on the PCI bus.
 *
 *		Emulation of the Intel PIIX, PIIX3, PIIX4, PIIX4E, and SMSC
 *		SLC90E66 (Victory66) Xcelerators.
 *
 *		PRD format :
 *		    word 0 - base address
 *		    word 1 - bits 1-15 = byte count, bit 31 = end of transfer
 *
 *
 *
 * Authors:	Miran Grca, <mgrca8@gmail.com>
 *
 *		Copyright 2016-2020 Miran Grca.
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
#include <86box/machine.h>
#include <86box/mem.h>
#include <86box/timer.h>
#include <86box/nvr.h>
#include <86box/acpi.h>
#include <86box/ddma.h>
#include <86box/pci.h>
#include <86box/pic.h>
#include <86box/pit.h>
#include <86box/port_92.h>
#include <86box/hdc.h>
#include <86box/hdc_ide.h>
#include <86box/hdc_ide_sff8038i.h>
#include <86box/usb.h>
#include <86box/zip.h>
#include <86box/machine.h>
#include <86box/smbus_piix4.h>
#include <86box/chipset.h>


typedef struct
{
    uint8_t		cur_readout_reg, rev,
			type, func_shift,
			max_func, pci_slot,
			regs[4][256],
			readout_regs[256], board_config[2];
    uint16_t		func0_id, nvr_io_base,
			acpi_io_base;
    double		fast_off_period;
    sff8038i_t		*bm[2];
    smbus_piix4_t *	smbus;
    apm_t *		apm;
    nvr_t *		nvr;
    ddma_t *		ddma;
    usb_t *		usb;
    acpi_t *		acpi;
    port_92_t *		port_92;
    pc_timer_t		fast_off_timer;
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
smsc_ide_irqs(piix_t *dev)
{
    int irq_line = 3, irq_mode[2] = { 0, 0 };

    if (dev->regs[1][0x09] & 0x01)
	irq_mode[0] = (dev->regs[0][0xe1] & 0x01) ? 3 : 1;

    if (dev->regs[1][0x09] & 0x04)
	irq_mode[1] = (dev->regs[0][0xe1] & 0x01) ? 3 : 1;

    switch ((dev->regs[0][0xe1] >> 1) & 0x07) {
	case 0x00:
		irq_line = 3;
		break;
	case 0x01:
		irq_line = 5;
		break;
	case 0x02:
		irq_line = 7;
		break;
	case 0x03:
		irq_line = 8;
		break;
	case 0x04:
		irq_line = 11;
		break;
	case 0x05:
		irq_line = 12;
		break;
	case 0x06:
		irq_line = 14;
		break;
	case 0x07:
		irq_line = 15;
		break;
    }

    sff_set_irq_line(dev->bm[0], irq_line);
    sff_set_irq_mode(dev->bm[0], 0, irq_mode[0]);
    sff_set_irq_mode(dev->bm[0], 1, irq_mode[1]);

    sff_set_irq_line(dev->bm[1], irq_line);
    sff_set_irq_mode(dev->bm[1], 0, irq_mode[0]);
    sff_set_irq_mode(dev->bm[1], 1, irq_mode[1]);
}


static void
piix_ide_handlers(piix_t *dev, int bus)
{
    uint16_t main, side;

    if (bus & 0x01) {
	ide_pri_disable();

	if (dev->type == 5) {
		if (dev->regs[1][0x09] & 0x01) {
			main = (dev->regs[1][0x11] << 8) | (dev->regs[1][0x10] & 0xf8);
			side = ((dev->regs[1][0x15] << 8) | (dev->regs[1][0x14] & 0xfc)) + 2;
		} else {
			main = 0x1f0;
			side = 0x3f6;
		}

		ide_set_base(0, main);
		ide_set_side(0, side);
	}

	if ((dev->regs[1][0x04] & 0x01) && (dev->regs[1][0x41] & 0x80))
		ide_pri_enable();
    }

    if (bus & 0x02) {
	ide_sec_disable();

	if (dev->type == 5) {
		if (dev->regs[1][0x09] & 0x04) {
			main = (dev->regs[1][0x19] << 8) | (dev->regs[1][0x18] & 0xf8);
			side = ((dev->regs[1][0x1d] << 8) | (dev->regs[1][0x1c] & 0xfc)) + 2;
		} else {
			main = 0x170;
			side = 0x376;
		}

		ide_set_base(1, main);
		ide_set_side(1, side);
	}

	if ((dev->regs[1][0x04] & 0x01) && (dev->regs[1][0x43] & 0x80))
		ide_sec_enable();
    }
}


static void
piix_ide_bm_handlers(piix_t *dev)
{
    uint16_t base = (dev->regs[1][0x20] & 0xf0) | (dev->regs[1][0x21] << 8);

    sff_bus_master_handler(dev->bm[0], (dev->regs[1][0x04] & 1), base);
    sff_bus_master_handler(dev->bm[1], (dev->regs[1][0x04] & 1), base + 8);
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

    if (dev->regs[0][0x4e] & 0x08) {
	io_sethandler(0x0063, 1, kbc_alias_reg_read, NULL, NULL, kbc_alias_reg_write, NULL, NULL, dev);
	io_sethandler(0x0065, 1, kbc_alias_reg_read, NULL, NULL, kbc_alias_reg_write, NULL, NULL, dev);
	io_sethandler(0x0067, 1, kbc_alias_reg_read, NULL, NULL, kbc_alias_reg_write, NULL, NULL, dev);
    }
}


static void
smbus_update_io_mapping(piix_t *dev)
{
    smbus_piix4_remap(dev->smbus, (dev->regs[3][0x91] << 8) | (dev->regs[3][0x90] & 0xf0), (dev->regs[3][PCI_REG_COMMAND] & PCI_COMMAND_IO) && (dev->regs[3][0xd2] & 0x01));
}


static void
nvr_update_io_mapping(piix_t *dev)
{
    if (dev->nvr_io_base != 0x0000) {
	piix_log("Removing NVR at %04X...\n", dev->nvr_io_base);
	nvr_at_handler(0, dev->nvr_io_base, dev->nvr);
	nvr_at_handler(0, dev->nvr_io_base + 0x0002, dev->nvr);
	nvr_at_handler(0, dev->nvr_io_base + 0x0004, dev->nvr);
    }

    if (dev->type == 5)
	dev->nvr_io_base = (dev->regs[0][0xd5] << 8) | (dev->regs[0][0xd4] & 0xf0);
    else
	dev->nvr_io_base = 0x70;
    piix_log("New NVR I/O base: %04X\n", dev->nvr_io_base);

    if (dev->regs[0][0xcb] & 0x01) {
	piix_log("Adding low NVR at %04X...\n", dev->nvr_io_base);
	if (dev->nvr_io_base != 0x0000) {
		nvr_at_handler(1, dev->nvr_io_base, dev->nvr);
		nvr_at_handler(1, dev->nvr_io_base + 0x0004, dev->nvr);
	}
    }
    if (dev->regs[0][0xcb] & 0x04) {
	piix_log("Adding high NVR at %04X...\n", dev->nvr_io_base + 0x0002);
	if (dev->nvr_io_base != 0x0000)
		nvr_at_handler(1, dev->nvr_io_base + 0x0002, dev->nvr);
    }
}


static void
piix_write(int func, int addr, uint8_t val, void *priv)
{
    piix_t *dev = (piix_t *) priv;
    uint8_t *fregs;
    int i;

    /* Return on unsupported function. */
    if (dev->max_func > 0) {
	if (func > dev->max_func)
		return;
    } else {
	if (func > 1)
		return;
    }

    /* Ignore the new IDE BAR's on the Intel chips. */
    if ((dev->type < 5) && (func == 1) && (addr >= 0x10) && (addr <= 0x1f))
	return;

    piix_log("PIIX function %i write: %02X to %02X\n", func, val, addr);
    fregs = (uint8_t *) dev->regs[func];

    if (func == 0)  switch (addr) {
	case 0x04:
		fregs[0x04] = (val & 0x08) | 0x07;
		break;
	case 0x05:
		if (dev->type > 1)
			fregs[0x05] = (val & 0x01);
		break;
	case 0x07:
		if ((val & 0x40) && (dev->type > 1))
			fregs[0x07] &= 0xbf;
		if (val & 0x20)
			fregs[0x07] &= 0xdf;
		if (val & 0x10)
			fregs[0x07] &= 0xef;
		if (val & 0x08)
			fregs[0x07] &= 0xf7;
		if (val & 0x04)
			fregs[0x07] &= 0xfb;
		break;
	case 0x4c:
		fregs[0x4c] = val;
		if (dev->type > 1)
			dma_alias_remove();
		else
			dma_alias_remove_piix();
		if (!(val & 0x80)) {
			if (dev->type > 1)
				dma_alias_set();
			else
				dma_alias_set_piix();
		}
		break;
	case 0x4e:
		fregs[0x4e] = val;
		keyboard_at_set_mouse_scan((val & 0x10) ? 1 : 0);
		if (dev->type >= 4)
			kbc_alias_update_io_mapping(dev);
		break;
	case 0x4f:
		if (dev->type > 3)
			fregs[0x4f] = val & 0x07;
		else if (dev->type == 3)
			fregs[0x4f] = val & 0x01;
		break;
	case 0x60: case 0x61: case 0x62: case 0x63:
		piix_log("Set IRQ routing: INT %c -> %02X\n", 0x41 + (addr & 0x03), val);
		fregs[addr] = val & 0x8f;
		if (val & 0x80)
			pci_set_irq_routing(PCI_INTA + (addr & 0x03), PCI_IRQ_DISABLED);
		else
			pci_set_irq_routing(PCI_INTA + (addr & 0x03), val & 0xf);
		break;
	case 0x64:
		if (dev->type > 3)
			fregs[0x64] = val;
		break;
	case 0x65:
		if (dev->type > 4)
			fregs[0x65] = val;
		break;
	case 0x66:
		if (dev->type > 4)
			fregs[0x66] = val & 0x81;
		break;
	case 0x69:
		if (dev->type > 1)
			fregs[0x69] = val & 0xfe;
		else
			fregs[0x69] = val & 0xfa;
		break;
	case 0x6a:
		switch (dev->type) {
			case 1:
			default:
				fregs[0x6a] = (fregs[0x6a] & 0xfb) | (val & 0x04);
				fregs[0x0e] = (val & 0x04) ? 0x80 : 0x00;
				piix_log("PIIX: Write %02X\n", val);
				dev->max_func = 0 + !!(val & 0x04);
				break;
			case 3:
				fregs[0x6a] = val & 0xd1;
				piix_log("PIIX3: Write %02X\n", val);
				dev->max_func = 1 + !!(val & 0x10);
				break;
			case 4:
				fregs[0x6a] = val & 0x80;
				break;
			case 5:
				/* This case is needed so it doesn't behave the PIIX way on the SMSC. */
				break;
		}
		break;
	case 0x6b:
		if ((dev->type > 1) && (dev->type <= 4) && (val & 0x80))
			fregs[0x6b] &= 0x7f;
		return;
	case 0x70: case 0x71:
		if ((dev->type > 1) && (addr == 0x71))
			break;
		if (dev->type < 4) {
			piix_log("Set MIRQ routing: MIRQ%i -> %02X\n", addr & 0x01, val);
			if (dev->type > 1)
				fregs[addr] = val & 0xef;
			else
				fregs[addr] = val & 0xcf;
			if (val & 0x80)
				pci_set_mirq_routing(PCI_MIRQ0, PCI_IRQ_DISABLED);
			else
				pci_set_mirq_routing(PCI_MIRQ0, val & 0xf);
			piix_log("MIRQ%i is %s\n", addr & 0x01, (val & 0x20) ? "disabled" : "enabled");
		}
		break;
	case 0x76: case 0x77:
		if (dev->type > 1)
			fregs[addr] = val & 0x87;
		else if (dev->type <= 4)
			fregs[addr] = val & 0x8f;
		break;
	case 0x78: case 0x79:
		if (dev->type < 4)
			fregs[addr] = val;
		break;
	case 0x80:
		if (dev->type > 1)
			fregs[addr] = val & 0x7f;
		break;
	case 0x81:
		if (dev->type > 1)
			fregs[addr] = val & 0x0f;
		break;
	case 0x82:
		if (dev->type > 3)
			fregs[addr] = val & 0x0f;
		break;
	case 0x90:
		if (dev->type > 3)
			fregs[addr] = val;
		break;
	case 0x91:
		if (dev->type > 3)
			fregs[addr] = val & 0xfc;
		break;
	case 0x92: case 0x93: case 0x94: case 0x95:
		if (dev->type > 3) {
			if (addr & 0x01)
				fregs[addr] = val & 0xff;
			else
				fregs[addr] = val & 0xc0;

			for (i = 0; i < 4; i++)
				ddma_update_io_mapping(dev->ddma, (addr & 4) + i, fregs[addr & 0xfe] + (i << 4), fregs[addr | 0x01], 1);
		}
		break;
	case 0xa0:
		if (dev->type < 4) {
			fregs[addr] = val & 0x1f;
			apm_set_do_smi(dev->apm, !!(val & 0x01) && !!(fregs[0xa2] & 0x80));
			switch ((val & 0x18) >> 3) {
				case 0x00:
					dev->fast_off_period = PCICLK * 32768.0 * 60000.0;
					break;
				case 0x01:
				default:
					dev->fast_off_period = 0.0;
					break;
				case 0x02:
					dev->fast_off_period = PCICLK;
					break;
				case 0x03:
					dev->fast_off_period = PCICLK * 32768.0;
					break;
			}
			cpu_fast_off_count = fregs[0xa8] + 1;
			timer_disable(&dev->fast_off_timer);
			if (dev->fast_off_period != 0.0)
				timer_on_auto(&dev->fast_off_timer, dev->fast_off_period);
		}
		break;
	case 0xa2:
		if (dev->type < 4) {
			fregs[addr] = val & 0xff;
			apm_set_do_smi(dev->apm, !!(fregs[0xa0] & 0x01) && !!(val & 0x80));
		}
		break;
	case 0xac: case 0xae:
		if (dev->type < 4)
			fregs[addr] = val & 0xff;
		break;
	case 0xa3:
		if (dev->type == 3)
			fregs[addr] = val & 0x01;
		break;
	case 0xa4:
		if (dev->type < 4) {
			fregs[addr] = val & 0xfb;
			cpu_fast_off_flags = (cpu_fast_off_flags & 0xffffff00) | fregs[addr];
		}
		break;
	case 0xa5:
		if (dev->type < 4) {
			fregs[addr] = val & 0xff;
			cpu_fast_off_flags = (cpu_fast_off_flags & 0xffff00ff) | (fregs[addr] << 8);
		}
		break;
	case 0xa6:
		if (dev->type < 4) {
			fregs[addr] = val & 0xff;
			cpu_fast_off_flags = (cpu_fast_off_flags & 0xff00ffff) | (fregs[addr] << 16);
		}
		break;
	case 0xa7:
		if (dev->type == 3)
			fregs[addr] = val & 0xef;
		else if (dev->type < 3)
			fregs[addr] = val;
		if (dev->type < 4)
			cpu_fast_off_flags = (cpu_fast_off_flags & 0x00ffffff) | (fregs[addr] << 24);
		break;
	case 0xa8:
		if (dev->type < 3) {
			fregs[addr] = val & 0xff;
			cpu_fast_off_val = val;
			cpu_fast_off_count = val + 1;
			timer_disable(&dev->fast_off_timer);
			if (dev->fast_off_period != 0.0)
				timer_on_auto(&dev->fast_off_timer, dev->fast_off_period);
		}
		break;
	case 0xaa:
		if (dev->type < 4)
			fregs[addr] &= val;
		break;
	case 0xab:
		if (dev->type == 3)
			fregs[addr] &= (val & 0x01);
		break;
	case 0xb0:
		if (dev->type == 4)
			fregs[addr] = (fregs[addr] & 0x8c) | (val & 0x73);
		else if (dev->type == 5)
			fregs[addr] = val & 0x7f;
		break;
	case 0xb1:
		if (dev->type > 3)
			fregs[addr] = val & 0xdf;
		break;
	case 0xb2:
		if (dev->type > 3)
			fregs[addr] = val;
		break;
	case 0xb3:
		if (dev->type > 3)
			fregs[addr] = val & 0xfb;
		break;
	case 0xcb:
		if (dev->type > 3) {
			fregs[addr] = val & 0x3d;

			nvr_update_io_mapping(dev);

			nvr_wp_set(!!(val & 0x08), 0, dev->nvr);
			nvr_wp_set(!!(val & 0x10), 1, dev->nvr);
		}
		break;
	case 0xd4:
		if ((dev->type > 4) && !(fregs[addr] & 0x01)) {
			fregs[addr] = val & 0xf1;
			nvr_update_io_mapping(dev);
		}
		break;
	case 0xd5:
		if ((dev->type > 4) && !(fregs[0xd4] & 0x01)) {
			fregs[addr] = val & 0xff;
			nvr_update_io_mapping(dev);
		}
		break;
	case 0xe0:
		if (dev->type > 4)
			fregs[addr] = val & 0xe7;
		break;
	case 0xe1: case 0xe4: case 0xe5: case 0xe6: case 0xe7:
	case 0xe8: case 0xe9: case 0xea: case 0xeb:
		if (dev->type > 4) {
			fregs[addr] = val;
			if ((dev->type == 5) && (addr == 0xe1)) {
				smsc_ide_irqs(dev);
				port_92_set_features(dev->port_92, !!(val & 0x40), !!(val & 0x40));
			}
		}
		break;
    } else if (func == 1)  switch(addr) {	/* IDE */
	case 0x04:
		fregs[0x04] = (val & 5);
		if (dev->type < 3)
			fregs[0x04] |= 0x02;
		piix_ide_handlers(dev, 0x03);
		piix_ide_bm_handlers(dev);
		break;
	case 0x07:
		if (val & 0x20)
			fregs[0x07] &= 0xdf;
		if (val & 0x10)
			fregs[0x07] &= 0xef;
		if (val & 0x08)
			fregs[0x07] &= 0xf7;
		break;
	case 0x09:
		if (dev->type == 5) {
			fregs[0x09] = (fregs[0x09] & 0xfa) | (val & 0x05);
			piix_ide_handlers(dev, 0x03);
			smsc_ide_irqs(dev);
		}
		break;
	case 0x0d:
		fregs[0x0d] = val & 0xf0;
		break;
	case 0x10:
		fregs[0x10] = (val & 0xf8) | 1;
		piix_ide_handlers(dev, 0x01);
		break;
	case 0x11:
		fregs[0x11] = val;
		piix_ide_handlers(dev, 0x01);
		break;
	case 0x14:
		fregs[0x14] = (val & 0xfc) | 1;
		piix_ide_handlers(dev, 0x01);
		break;
	case 0x15:
		fregs[0x15] = val;
		piix_ide_handlers(dev, 0x01);
		break;
	case 0x18:
		fregs[0x18] = (val & 0xf8) | 1;
		piix_ide_handlers(dev, 0x02);
		break;
	case 0x19:
		fregs[0x19] = val;
		piix_ide_handlers(dev, 0x02);
		break;
	case 0x1c:
		fregs[0x1c] = (val & 0xfc) | 1;
		piix_ide_handlers(dev, 0x02);
		break;
	case 0x1d:
		fregs[0x1d] = val;
		piix_ide_handlers(dev, 0x02);
		break;
	case 0x20:
		fregs[0x20] = (val & 0xf0) | 1;
		piix_ide_bm_handlers(dev);
		break;
	case 0x21:
		fregs[0x21] = val;
		piix_ide_bm_handlers(dev);
		break;
	case 0x3c:
		fregs[0x3c] = val;
		break;
	case 0x3d:
		if (dev->type == 5)
			fregs[0x3d] = val;
		break;
	case 0x40: case 0x42:
		fregs[addr] = val;
		break;
	case 0x41: case 0x43:
		fregs[addr] = val & ((dev->type > 1) ? 0xf3 : 0xb3);
		piix_ide_handlers(dev, 1 << !!(addr & 0x02));
		break;
	case 0x44:
		if (dev->type > 1)
			fregs[0x44] = val;
		break;
	case 0x45:
		if (dev->type > 4)
			fregs[0x45] = val;
		break;
	case 0x46:
		if (dev->type > 4)
			fregs[0x46] = val & 0x03;
		break;
	case 0x48:
		if (dev->type > 3)
			fregs[0x48] = val & 0x0f;
		break;
	case 0x4a: case 0x4b:
		if (dev->type > 3)
			fregs[addr] = val & 0x33;
		break;
	case 0x5c: case 0x5d:
		if (dev->type > 4)
			fregs[addr] = val;
		break;
    } else if (func == 2)  switch(addr) {	/* USB */
	case 0x04:
		if (dev->type > 4) {
			fregs[0x04] = (val & 7);
			ohci_update_mem_mapping(dev->usb, fregs[0x11], fregs[0x12], fregs[0x13], fregs[PCI_REG_COMMAND] & PCI_COMMAND_MEM);
		} else {
			fregs[0x04] = (val & 5);
			uhci_update_io_mapping(dev->usb, fregs[0x20] & ~0x1f, fregs[0x21], fregs[PCI_REG_COMMAND] & PCI_COMMAND_IO);
		}
		break;
	case 0x07:
		if (dev->type > 4) {
			if (val & 0x80)
				fregs[0x07] &= 0x7f;
			if (val & 0x40)
				fregs[0x07] &= 0xbf;
		}
		if (val & 0x20)
			fregs[0x07] &= 0xdf;
		if (val & 0x10)
			fregs[0x07] &= 0xef;
		if (val & 0x08)
			fregs[0x07] &= 0xf7;
		break;
	case 0x0c:
		if (dev->type > 4)
			fregs[0x0c] = val;
		break;
	case 0x0d:
		if (dev->type < 5)
			fregs[0x0d] = val & 0xf0;
		break;
	case 0x11:
		if (dev->type > 4) {
			fregs[addr] = val & 0xf0;
			ohci_update_mem_mapping(dev->usb, fregs[0x11], fregs[0x12], fregs[0x13], 1 /*fregs[PCI_REG_COMMAND] & PCI_COMMAND_MEM*/);
		}
		break;
	case 0x12: case 0x13:
		if (dev->type > 4) {
			fregs[addr] = val;
			ohci_update_mem_mapping(dev->usb, fregs[0x11], fregs[0x12], fregs[0x13], 1 /*fregs[PCI_REG_COMMAND] & PCI_COMMAND_MEM*/);
		}
		break;
	case 0x20:
		if (dev->type < 5) {
			fregs[0x20] = (val & 0xe0) | 1;
			uhci_update_io_mapping(dev->usb, fregs[0x20] & ~0x1f, fregs[0x21], fregs[PCI_REG_COMMAND] & PCI_COMMAND_IO);
		}
		break;
	case 0x21:
		if (dev->type < 5) {
			fregs[0x21] = val;
			uhci_update_io_mapping(dev->usb, fregs[0x20] & ~0x1f, fregs[0x21], fregs[PCI_REG_COMMAND] & PCI_COMMAND_IO);
		}
		break;
	case 0x3c:
		fregs[0x3c] = val;
		break;
	case 0x3e: case 0x3f:
	case 0x40: case 0x41: case 0x43:
		if (dev->type > 4)
			fregs[addr] = val;
		break;
	case 0x42:
		if (dev->type > 4)
			fregs[addr] = val & 0x8f;
		break;
	case 0x44: case 0x45:
		if (dev->type > 4)
			fregs[addr] = val & 0x01;
		break;
	case 0x6a:
		if (dev->type <= 4)
			fregs[0x6a] = val & 0x01;
		break;
	case 0xc0:
		if (dev->type <= 4)
			fregs[0xc0] = (fregs[0xc0]  & ~(val & 0xbf)) | (val & 0x20);
		break;
	case 0xc1:
		if (dev->type <= 4)
			fregs[0xc1] &= ~val;
		break;
	case 0xff:
		if (dev->type == 4) {
			fregs[addr] = val & 0x10;
			nvr_read_addr_set(!!(val & 0x10), dev->nvr);
		}
		break;
    } else if (func == 3)  switch(addr) {	/* Power Management */
	case 0x04:
		fregs[0x04] = (val & 0x01);
		smbus_update_io_mapping(dev);
		apm_set_do_smi(dev->acpi->apm, !!(fregs[0x5b] & 0x02) && !!(val & 0x01));
		break;
	case 0x07:
		if (val & 0x08)
			fregs[0x07] &= 0xf7;
		break;
#if 0
	case 0x3c:
		fregs[0x3c] = val;
		break;
#endif
	case 0x40:
		fregs[0x40] = (val & 0xc0) | 1;
		dev->acpi_io_base = (dev->regs[3][0x41] << 8) | (dev->regs[3][0x40] & 0xc0);
		acpi_update_io_mapping(dev->acpi, dev->acpi_io_base, (dev->regs[3][0x80] & 0x01));
		break;
	case 0x41:
		fregs[0x41] = val;
		dev->acpi_io_base = (dev->regs[3][0x41] << 8) | (dev->regs[3][0x40] & 0xc0);
		acpi_update_io_mapping(dev->acpi, dev->acpi_io_base, (dev->regs[3][0x80] & 0x01));
		break;
	case 0x44: case 0x45: case 0x46: case 0x47:
	case 0x48: case 0x49:
	case 0x4c: case 0x4d: case 0x4e:
	case 0x54: case 0x55: case 0x56: case 0x57:
	case 0x59: case 0x5a:
	case 0x5c: case 0x5d: case 0x5e: case 0x5f:
	case 0x60: case 0x61: case 0x62:
	case 0x64: case 0x65:
	case 0x67: case 0x68: case 0x69:
	case 0x6c: case 0x6e: case 0x6f:
	case 0x70: case 0x71:
	case 0x74: case 0x77: case 0x78: case 0x79:
	case 0x7c: case 0x7d:
	case 0xd3: case 0xd4:
	case 0xd5:
		fregs[addr] = val;
		break;
	case 0x4a:
		fregs[addr] = val & 0x73;
		break;
	case 0x4b:
		fregs[addr] = val & 0x01;
		break;
	case 0x4f: case 0x80: case 0xd2:
		fregs[addr] = val & 0x0f;
		if (addr == 0x80)
			acpi_update_io_mapping(dev->acpi, dev->acpi_io_base, (dev->regs[3][0x80] & 0x01));
		else if (addr == 0xd2)
			smbus_update_io_mapping(dev);
		break;
	case 0x50:
		fregs[addr] = val & 0x3f;
		break;
	case 0x51:
		fregs[addr] = val & 0x58;
		break;
	case 0x52:
		fregs[addr] = val & 0x7f;
		break;
	case 0x58:
		fregs[addr] = val & 0x77;
		break;
	case 0x5b:
		fregs[addr] = val & 0x03;
		apm_set_do_smi(dev->acpi->apm, !!(val & 0x02) && !!(fregs[0x04] & 0x01));
		break;
	case 0x63:
		fregs[addr] = val & 0xf7;
		break;
	case 0x66:
		fregs[addr] = val & 0xef;
		break;
	case 0x6a: case 0x72: case 0x7a: case 0x7e:
		fregs[addr] = val & 0x1f;
		break;
	case 0x6d: case 0x75:
		fregs[addr] = val & 0x80;
		break;
	case 0x90:
		fregs[0x90] = (val & 0xf0) | 1;
		smbus_update_io_mapping(dev);
		break;
	case 0x91:
		fregs[0x91] = val;
		smbus_update_io_mapping(dev);
		break;
    }
}


static uint8_t
piix_read(int func, int addr, void *priv)
{
    piix_t *dev = (piix_t *) priv;
    uint8_t ret = 0xff, *fregs;

    if ((dev->type == 3) && (func == 2) && (dev->max_func == 1) && (addr >= 0x40))
	ret = 0x00;

    /* Return on unsupported function. */
    if ((func <= dev->max_func) || ((func == 1) && (dev->max_func == 0))) {
    	fregs = (uint8_t *) dev->regs[func];
	ret = fregs[addr];
	if ((func == 0) && (addr == 0x4e))
		ret |= keyboard_at_get_mouse_scan();

	piix_log("PIIX function %i read: %02X from %02X\n", func, ret, addr);
    }

    return ret;
}


static void
board_write(uint16_t port, uint8_t val, void *priv)
{
    piix_t *dev = (piix_t *) priv;

    if (port == 0x0078)
	dev->board_config[0] = val;
    else if (port == 0x00e0)
	dev->cur_readout_reg = val;
    else if (port == 0x00e1)
	dev->readout_regs[dev->cur_readout_reg] = val;
}


static uint8_t
board_read(uint16_t port, void *priv)
{
    piix_t *dev = (piix_t *) priv;
    uint8_t ret = 0x64;

    if (port == 0x0078)
	ret = dev->board_config[0];
    else if (port == 0x0079)
	ret = dev->board_config[1];
    else if (port == 0x00e0)
	ret = dev->cur_readout_reg;
    else if (port == 0x00e1)
	ret = dev->readout_regs[dev->cur_readout_reg];

    return ret;
}


static void
piix_reset_hard(piix_t *dev)
{
    int i;
    uint8_t *fregs;

    uint16_t old_base = (dev->regs[1][0x20] & 0xf0) | (dev->regs[1][0x21] << 8);

    sff_bus_master_reset(dev->bm[0], old_base);
    sff_bus_master_reset(dev->bm[1], old_base + 8);

    if (dev->type >= 4) {
	sff_set_slot(dev->bm[0], dev->pci_slot);
	sff_set_irq_pin(dev->bm[0], PCI_INTA);
	sff_set_irq_line(dev->bm[0], 14);
	sff_set_irq_mode(dev->bm[0], 0, 0);
	sff_set_irq_mode(dev->bm[0], 1, 0);

	sff_set_slot(dev->bm[1], dev->pci_slot);
	sff_set_irq_pin(dev->bm[1], PCI_INTA);
	sff_set_irq_line(dev->bm[1], 14);
	sff_set_irq_mode(dev->bm[1], 0, 0);
	sff_set_irq_mode(dev->bm[1], 1, 0);
    }

#ifdef ENABLE_PIIX_LOG
    piix_log("piix_reset_hard()\n");
#endif
    ide_pri_disable();
    ide_sec_disable();

    if (dev->type > 3) {
	nvr_at_handler(0, 0x0072, dev->nvr);
	nvr_wp_set(0, 0, dev->nvr);
	nvr_wp_set(0, 1, dev->nvr);
	nvr_at_handler(1, 0x0074, dev->nvr);
	dev->nvr_io_base = 0x0070;
    }

    /* Clear all 4 functions' arrays and set their vendor and device ID's. */
    for (i = 0; i < 4; i++) {
    	memset(dev->regs[i], 0, 256);
    	if (dev->type == 5) {
    		dev->regs[i][0x00] = 0x55; dev->regs[i][0x01] = 0x10;		/* SMSC/EFAR */
    		if (i == 1) { /* IDE controller is 9130, breaking convention */
    			dev->regs[i][0x02] = 0x30;
    			dev->regs[i][0x03] = 0x91;
    		} else {
    			dev->regs[i][0x02] = (dev->func0_id & 0xff) + (i << dev->func_shift);
    			dev->regs[i][0x03] = (dev->func0_id >> 8);
    		}
    	} else {
    		dev->regs[i][0x00] = 0x86; dev->regs[i][0x01] = 0x80;		/* Intel */
    		dev->regs[i][0x02] = (dev->func0_id & 0xff) + (i << dev->func_shift);
    		dev->regs[i][0x03] = (dev->func0_id >> 8);
    	}
    }

    /* Function 0: PCI to ISA Bridge */
    fregs = (uint8_t *) dev->regs[0];
    piix_log("PIIX Function 0: %02X%02X:%02X%02X\n", fregs[0x01], fregs[0x00], fregs[0x03], fregs[0x02]);
    fregs[0x04] = 0x07;
    fregs[0x06] = 0x80; fregs[0x07] = 0x02;
    if (dev->type == 4)
	fregs[0x08] = (dev->rev & 0x08) ? 0x02 : (dev->rev & 0x07);
    else
    	fregs[0x08] = dev->rev;
    fregs[0x09] = 0x00;
    fregs[0x0a] = 0x01; fregs[0x0b] = 0x06;
    fregs[0x0e] = ((dev->type > 1) || (dev->rev != 2)) ? 0x80 : 0x00;
    fregs[0x4c] = 0x4d;
    fregs[0x4e] = 0x03;
    fregs[0x60] = fregs[0x61] = fregs[0x62] = fregs[0x63] = 0x80;
    fregs[0x64] = (dev->type > 3) ? 0x10 : 0x00;
    fregs[0x69] = 0x02;
    if ((dev->type == 1) && (dev->rev != 2))
	fregs[0x6a] = 0x04;
    else if (dev->type == 3)
	fregs[0x6a] = 0x10;
    fregs[0x70] = (dev->type < 4) ? 0x80 : 0x00;
    fregs[0x71] = (dev->type < 3) ? 0x80 : 0x00;
    if (dev->type <= 4) {
	fregs[0x76] = fregs[0x77] = (dev->type > 1) ? 0x04 : 0x0c;
    }
    fregs[0x78] = (dev->type < 4) ? 0x02 : 0x00;
    fregs[0xa0] = (dev->type < 4) ? 0x08 : 0x00;
    fregs[0xa8] = (dev->type < 4) ? 0x0f : 0x00;
    if (dev->type > 3)
	fregs[0xb0] = (is_pentium) ? 0x00 : 0x04;
    fregs[0xcb] = (dev->type > 3) ? 0x21 : 0x00;
    if (dev->type > 4) {
	fregs[0xd4] = 0x70;
	fregs[0xe1] = 0x40;
	fregs[0xe6] = 0x12;
	fregs[0xe8] = 0x02;
	fregs[0xea] = 0x12;
    }
    dev->max_func = 0;

    /* Function 1: IDE */
    fregs = (uint8_t *) dev->regs[1];
    piix_log("PIIX Function 1: %02X%02X:%02X%02X\n", fregs[0x01], fregs[0x00], fregs[0x03], fregs[0x02]);
    fregs[0x06] = 0x80; fregs[0x07] = 0x02;
    if (dev->type == 4)
	fregs[0x08] = dev->rev & 0x07;
    else
	fregs[0x08] = dev->rev;
    if (dev->type == 5)
	fregs[0x09] = 0x8a;
    else
	fregs[0x09] = 0x80;
    fregs[0x0a] = 0x01; fregs[0x0b] = 0x01;
    if (dev->type == 5) {
	fregs[0x10] = 0xf1; fregs[0x11] = 0x01;
	fregs[0x14] = 0xf5; fregs[0x15] = 0x03;
	fregs[0x18] = 0x71; fregs[0x19] = 0x01;
	fregs[0x1c] = 0x75; fregs[0x1d] = 0x03;
    }
    fregs[0x20] = 0x01;
    if (dev->type == 5) {
	fregs[0x3c] = 0x0e;
	fregs[0x3d] = 0x01;
    }
    if ((dev->type == 1) && (dev->rev == 2))
	dev->max_func = 0;		/* It starts with IDE disabled, then enables it. */
    else
	dev->max_func = 1;

    /* Function 2: USB */
    if (dev->type > 1) {
	fregs = (uint8_t *) dev->regs[2];
	piix_log("PIIX Function 2: %02X%02X:%02X%02X\n", fregs[0x01], fregs[0x00], fregs[0x03], fregs[0x02]);
	fregs[0x06] = 0x80; fregs[0x07] = 0x02;
	if (dev->type == 4)
		fregs[0x08] = dev->rev & 0x07;
	else if (dev->type < 4)
		fregs[0x08] = dev->rev;
	else
		fregs[0x08] = 0x02;
	if (dev->type > 4)
		fregs[0x09] = 0x10; /* SMSC has OHCI rather than UHCI */
	fregs[0x0a] = 0x03; fregs[0x0b] = 0x0c;
	if (dev->type < 5)
		fregs[0x20] = 0x01;
	fregs[0x3d] = 0x04;
	if (dev->type > 4)
		fregs[0x60] = (dev->type > 3) ? 0x10 : 0x00;
	if (dev->type < 5) {
		fregs[0x6a] = (dev->type == 3) ? 0x01 : 0x00;
		fregs[0xc1] = 0x20;
		fregs[0xff] = (dev->type > 3) ? 0x10 : 0x00;
	}
	dev->max_func = 2;	/* It starts with USB disabled, then enables it. */
    }

    /* Function 3: Power Management */
    if (dev->type > 3) {
	fregs = (uint8_t *) dev->regs[3];	
	piix_log("PIIX Function 3: %02X%02X:%02X%02X\n", fregs[0x01], fregs[0x00], fregs[0x03], fregs[0x02]);
	fregs[0x06] = 0x80; fregs[0x07] = 0x02;
	if (dev->type > 4)
		fregs[0x08] = 0x02;
	else
		fregs[0x08] = (dev->rev & 0x08) ? 0x02 : (dev->rev & 0x07);
	fregs[0x0a] = 0x80; fregs[0x0b] = 0x06;
	/* NOTE: The Specification Update says this should default to 0x00 and be read-only. */
#ifdef WRONG_SPEC
	if (dev->type == 4)
		fregs[0x3d] = 0x01;
#endif
	fregs[0x40] = 0x01;
	fregs[0x90] = 0x01;
	dev->max_func = 3;
    }

    pci_set_irq_routing(PCI_INTA, PCI_IRQ_DISABLED);
    pci_set_irq_routing(PCI_INTB, PCI_IRQ_DISABLED);
    pci_set_irq_routing(PCI_INTC, PCI_IRQ_DISABLED);
    pci_set_irq_routing(PCI_INTD, PCI_IRQ_DISABLED);

    if (dev->type < 4)
	pci_set_mirq_routing(PCI_MIRQ0, PCI_IRQ_DISABLED);
    if (dev->type < 3)
	pci_set_mirq_routing(PCI_MIRQ1, PCI_IRQ_DISABLED);

    if (dev->type >= 4)
	acpi_init_gporeg(dev->acpi, 0xff, 0xbf, 0xff, 0x7f);
}


static void
piix_apm_out(uint16_t port, uint8_t val, void *p)
{
    piix_t *dev = (piix_t *) p;

    if (dev->apm->do_smi) {
	if (dev->type < 4)
		dev->regs[0][0xaa] |= 0x80;
    }
}


static void
piix_fast_off_count(void *priv)
{
    piix_t *dev = (piix_t *) priv;

    cpu_fast_off_count--;

    if (cpu_fast_off_count == 0) {
	smi_line = 1;
	dev->regs[0][0xaa] |= 0x20;
	cpu_fast_off_count = dev->regs[0][0xa8] + 1;
    }

    timer_on_auto(&dev->fast_off_timer, dev->fast_off_period);
}


static void
piix_reset(void *p)
{
    piix_t *dev = (piix_t *)p;

    if (dev->type > 3) {
	piix_write(3, 0x04, 0x00, p);
	piix_write(3, 0x5b, 0x00, p);
    } else {
	piix_write(0, 0xa0, 0x08, p);
	piix_write(0, 0xa2, 0x00, p);
	piix_write(0, 0xa4, 0x00, p);
	piix_write(0, 0xa5, 0x00, p);
	piix_write(0, 0xa6, 0x00, p);
	piix_write(0, 0xa7, 0x00, p);
	piix_write(0, 0xa8, 0x0f, p);
    }

    piix_write(1, 0x04, 0x00, p);
    piix_write(1, 0x41, 0x00, p);
    piix_write(1, 0x43, 0x00, p);

    ide_pri_disable();
    ide_sec_disable();
}


static void
piix_close(void *p)
{
    piix_t *piix = (piix_t *)p;

    free(piix);
}


static void
piix_speed_changed(void *priv)
{
    piix_t *dev = (piix_t *) priv;
    int te;

    te = timer_is_enabled(&dev->fast_off_timer);

    timer_stop(&dev->fast_off_timer);
    if (te)
	timer_on_auto(&dev->fast_off_timer, dev->fast_off_period);
}


static void
*piix_init(const device_t *info)
{
    piix_t *dev = (piix_t *) malloc(sizeof(piix_t));
    memset(dev, 0, sizeof(piix_t));

    dev->type = info->local & 0x0f;
    /* If (dev->type == 4) and (dev->rev & 0x08), then this is PIIX4E. */
    dev->rev = (info->local >> 4) & 0x0f;
    dev->func_shift = info->local >> 8;
    dev->func0_id = info->local >> 16;

    dev->pci_slot = pci_add_card(PCI_ADD_SOUTHBRIDGE, piix_read, piix_write, dev);
    piix_log("PIIX%i: Added to slot: %02X\n", dev->type, dev->pci_slot);
    piix_log("PIIX%i: Added to slot: %02X\n", dev->type, dev->pci_slot);

    dev->bm[0] = device_add_inst(&sff8038i_device, 1);
    dev->bm[1] = device_add_inst(&sff8038i_device, 2);
    if ((dev->type == 1) && (dev->rev == 2)) {
	/* PIIX rev. 02 has faulty bus mastering on real hardware,
	   so set our devices IDE devices to force ATA-3 (no DMA). */
	ide_board_set_force_ata3(0, 1);
	ide_board_set_force_ata3(1, 1);
    }

    if (dev->type >= 3)
	dev->usb = device_add(&usb_device);

    if (dev->type > 3) {
	dev->nvr = device_add(&piix4_nvr_device);
	dev->smbus = device_add(&piix4_smbus_device);

	dev->acpi = device_add(&acpi_intel_device);
	acpi_set_slot(dev->acpi, dev->pci_slot);
	acpi_set_nvr(dev->acpi, dev->nvr);

	dev->ddma = device_add(&ddma_device);
    } else
	timer_add(&dev->fast_off_timer, piix_fast_off_count, dev, 0);

    piix_reset_hard(dev);
    piix_log("Maximum function: %i\n", dev->max_func);
    cpu_fast_off_flags = 0x00000000;
    if (dev->type < 4) {
	cpu_fast_off_val = dev->regs[0][0xa8];
	cpu_fast_off_count = cpu_fast_off_val + 1;
    } else
	cpu_fast_off_val = cpu_fast_off_count = 0;

    /* On PIIX4, PIIX4E, and SMSC, APM is added by the ACPI device. */
    if (dev->type < 4) {
    	dev->apm = device_add(&apm_pci_device);
	/* APM intercept handler to update PIIX/PIIX3 and PIIX4/4E/SMSC ACPI SMI status on APM SMI. */
	io_sethandler(0x00b2, 0x0001, NULL, NULL, NULL, piix_apm_out, NULL, NULL, dev);
    }

    dev->port_92 = device_add(&port_92_pci_device);

    dma_alias_set();

    if (dev->type < 4)
	pci_enable_mirq(0);
    if (dev->type < 3)
	pci_enable_mirq(1);

    dev->readout_regs[1] = 0x40;

    /* Port E1 register 01 (TODO: Find how multipliers > 3.0 are defined):

	Bit 6: 1 = can boot, 0 = no;
	Bit 7, 1 = multiplier (00 = 2.5, 01 = 2.0, 10 = 3.0, 11 = 1.5);
	Bit 5, 4 = bus speed (00 = 50 MHz, 01 = 66 MHz, 10 = 60 MHz, 11 = ????):
	Bit 7, 5, 4, 1: 0000 = 125 MHz, 0010 = 166 MHz, 0100 = 150 MHz, 0110 = ??? MHz;
		        0001 = 100 MHz, 0011 = 133 MHz, 0101 = 120 MHz, 0111 = ??? MHz;
		        1000 = 150 MHz, 1010 = 200 MHz, 1100 = 180 MHz, 1110 = ??? MHz;
		        1001 =  75 MHz, 1011 = 100 MHz, 1101 =  90 MHz, 1111 = ??? MHz */

    if (cpu_busspeed <= 40000000)
		dev->readout_regs[1] |= 0x30;
    else if ((cpu_busspeed > 40000000) && (cpu_busspeed <= 50000000))
		dev->readout_regs[1] |= 0x00;
    else if ((cpu_busspeed > 50000000) && (cpu_busspeed <= 60000000))
		dev->readout_regs[1] |= 0x20;
    else if (cpu_busspeed > 60000000)
		dev->readout_regs[1] |= 0x10;

    if (cpu_dmulti <= 1.5)
	dev->readout_regs[1] |= 0x82;
    else if ((cpu_dmulti > 1.5) && (cpu_dmulti <= 2.0))
	dev->readout_regs[1] |= 0x02;
    else if ((cpu_dmulti > 2.0) && (cpu_dmulti <= 2.5))
	dev->readout_regs[1] |= 0x00;
    else if (cpu_dmulti > 2.5)
	dev->readout_regs[1] |= 0x80;

    io_sethandler(0x0078, 0x0002, board_read, NULL, NULL, board_write, NULL, NULL, dev);
    io_sethandler(0x00e0, 0x0002, board_read, NULL, NULL, board_write, NULL, NULL, dev);

    dev->board_config[0] = 0xff;
    /* Register 0x0079: */
    /* Bit 7: 0 = Clear password, 1 = Keep password. */
    /* Bit 6: 0 = NVRAM cleared by jumper, 1 = NVRAM normal. */
    /* Bit 5: 0 = CMOS Setup disabled, 1 = CMOS Setup enabled. */
    /* Bit 4: External CPU clock (Switch 8). */
    /* Bit 3: External CPU clock (Switch 7). */
    /*		50 MHz: Switch 7 = Off, Switch 8 = Off. */
    /*		60 MHz: Switch 7 = On, Switch 8 = Off. */
    /*		66 MHz: Switch 7 = Off, Switch 8 = On. */
    /* Bit 2: 0 = On-board audio absent, 1 = On-board audio present. */
    /* Bit 1: 0 = Soft-off capable power supply present, 1 = Soft-off capable power supply absent. */
    /* Bit 0: 0 = 1.5x multiplier, 1 = 2x multiplier (Switch 6). */
    /* NOTE: A bit is read as 1 if switch is off, and as 0 if switch is on. */
    dev->board_config[1] = 0xe0;

    if (cpu_busspeed <= 50000000)
		dev->board_config[1] |= 0x10;
    else if ((cpu_busspeed > 50000000) && (cpu_busspeed <= 60000000))
		dev->board_config[1] |= 0x18;
    else if (cpu_busspeed > 60000000)
		dev->board_config[1] |= 0x00;

    if (cpu_dmulti <= 1.5)
	dev->board_config[1] |= 0x01;
    else
	dev->board_config[1] |= 0x00;

    return dev;
}


const device_t piix_device =
{
    "Intel 82371FB (PIIX)",
    DEVICE_PCI,
    0x122e0101,
    piix_init, 
    piix_close, 
    piix_reset,
    NULL,
    piix_speed_changed,
    NULL,
    NULL
};

const device_t piix_rev02_device =
{
    "Intel 82371FB (PIIX) (Faulty BusMastering!!)",
    DEVICE_PCI,
    0x122e0121,
    piix_init, 
    piix_close, 
    piix_reset,
    NULL,
    piix_speed_changed,
    NULL,
    NULL
};

const device_t piix3_device =
{
    "Intel 82371SB (PIIX3)",
    DEVICE_PCI,
    0x70000403,
    piix_init, 
    piix_close, 
    piix_reset,
    NULL,
    piix_speed_changed,
    NULL,
    NULL
};

const device_t piix4_device =
{
    "Intel 82371AB/EB (PIIX4/PIIX4E)",
    DEVICE_PCI,
    0x71100004,
    piix_init, 
    piix_close, 
    piix_reset,
    NULL,
    piix_speed_changed,
    NULL,
    NULL
};

const device_t piix4e_device =
{
    "Intel 82371EB (PIIX4E)",
    DEVICE_PCI,
    0x71100094,
    piix_init, 
    piix_close, 
    piix_reset,
    NULL,
    piix_speed_changed,
    NULL,
    NULL
};

const device_t slc90e66_device =
{
    "SMSC SLC90E66 (Victory66)",
    DEVICE_PCI,
    0x94600005,
    piix_init, 
    piix_close, 
    piix_reset,
    NULL,
    piix_speed_changed,
    NULL,
    NULL
};
