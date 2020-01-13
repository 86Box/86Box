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
 * Version:	@(#)intel_piix.c	1.0.22	2018/10/31
 *
 * Authors:	Sarah Walker, <http://pcem-emulator.co.uk/>
 *		Miran Grca, <mgrca8@gmail.com>
 *
 *		Copyright 2008-2018 Sarah Walker.
 *		Copyright 2016-2018 Miran Grca.
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
#include "pci.h"
#include "pic.h"
#include "port_92.h"
#include "disk/hdc.h"
#include "disk/hdc_ide.h"
#include "disk/hdc_ide_sff8038i.h"
#include "disk/zip.h"
#include "machine/machine.h"
#include "piix.h"


typedef struct
{
    int			type;
    uint8_t		cur_readout_reg,
			readout_regs[256],
			regs[256], regs_ide[256];
    sff8038i_t		*bm[2];
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


static void
piix_write(int func, int addr, uint8_t val, void *priv)
{
    piix_t *dev = (piix_t *) priv;
    uint8_t valxor;
    uint16_t old_base;

    if ((func == 1) && (dev->type & 0x100))	/* PB640's PIIX has no IDE part. */
	return;

    if (func > 1)
	return;

    old_base = (dev->regs_ide[0x20] & 0xf0) | (dev->regs_ide[0x21] << 8);

    if (func == 1) {	/*IDE*/
	piix_log("PIIX IDE write: %02X %02X\n", addr, val);
	valxor = val ^ dev->regs_ide[addr];

	switch (addr) {
		case 0x04:
			dev->regs_ide[0x04] = (val & 5) | 2;
			if (valxor & 0x01) {
				ide_pri_disable();
				ide_sec_disable();
				if (val & 0x01) {
					if (dev->regs_ide[0x41] & 0x80)
						ide_pri_enable();
					if (dev->regs_ide[0x43] & 0x80)
						ide_sec_enable();
				}

				piix_bus_master_handlers(dev, old_base);
			}
			break;
		case 0x07:
			dev->regs_ide[0x07] = val & 0x3e;
			break;
		case 0x0d:
			dev->regs_ide[0x0d] = val;
			break;

		case 0x20:
			dev->regs_ide[0x20] = (val & ~0x0f) | 1;
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
			dev->regs_ide[0x41] = val;
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
			dev->regs_ide[0x43] = val;
			if (valxor & 0x80) {
				ide_sec_disable();
				if ((val & 0x80) && (dev->regs_ide[0x04] & 0x01))
					ide_sec_enable();
			}
			break;
		case 0x44:
			if (dev->type >= 3)  dev->regs_ide[0x44] = val;
			break;
	}
    } else {
	piix_log("PIIX writing value %02X to register %02X\n", val, addr);
	valxor = val ^ dev->regs[addr];

	if ((addr >= 0x0f) && (addr < 0x4c))
		return;

	switch (addr) {
		case 0x00: case 0x01: case 0x02: case 0x03:
		case 0x08: case 0x09: case 0x0a: case 0x0b:
		case 0x0e:
			return;

		case 0x4c:
			if (valxor) {
				if (dev->type == 3)
					dma_alias_remove();
				else
					dma_alias_remove_piix();
				if (!(val & 0x80))
					dma_alias_set();
			}
			break;
		case 0x4e:
			keyboard_at_set_mouse_scan((val & 0x10) ? 1 : 0);
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
			if (val & 0x80)
				pci_set_mirq_routing(PCI_MIRQ0, PCI_IRQ_DISABLED);
			else
				pci_set_mirq_routing(PCI_MIRQ0, val & 0xf);
			break;
			piix_log("MIRQ0 is %s\n", (val & 0x20) ? "disabled" : "enabled");
		case 0x71:
			if (dev->type == 1) {
				piix_log("Set MIRQ routing: MIRQ1 -> %02X\n", val);
				if (val & 0x80)
					pci_set_mirq_routing(PCI_MIRQ1, PCI_IRQ_DISABLED);
				else
					pci_set_mirq_routing(PCI_MIRQ1, val & 0xf);
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

    if ((func == 1) && (dev->type & 0x100))	/* PB640's PIIX has no IDE part. */
	return 0xff;
    if (func > 1)
	return 0xff;

    if (func == 1) {	/*IDE*/
	if (addr == 4)
		return (dev->regs_ide[addr] & 5) | 2;
	else if (addr == 5)
		return 0;
	else if (addr == 6)
		return 0x80;
	else if (addr == 7)
		return dev->regs_ide[addr] & 0x3E;
	else if (addr == 0xD)
		return dev->regs_ide[addr] & 0xF0;
	else if (addr == 0x20)
		return (dev->regs_ide[addr] & 0xF0) | 1;
	else if (addr == 0x22)
		return 0;
	else if (addr == 0x23)
		return 0;
	else if (addr == 0x41) {
		if (dev->type == 3)
			return dev->regs_ide[addr] & 0xF3;
		else
			return dev->regs_ide[addr] & 0xB3;
	} else if (addr == 0x43) {
		if (dev->type == 3)
			return dev->regs_ide[addr] & 0xF3;
		else
			return dev->regs_ide[addr] & 0xB3;
	} else
               	return dev->regs_ide[addr];
    } else {
	if ((addr & 0xFC) == 0x60)
		return dev->regs[addr] & 0x8F;

	if (addr == 4) {
		if (dev->type & 0x100)
			return (dev->regs[addr] & 0x80) | 0x0F;
		else
			return (dev->regs[addr] & 0x80) | 7;
	} else if (addr == 5) {
		if (dev->type == 3)
			return dev->regs[addr] & 1;
		else
			return 0;
	} else if (addr == 6)
			return dev->regs[addr] & 0x80;
	else if (addr == 7) {
		if (dev->type == 3)
			return dev->regs[addr];
		else {
			if (dev->type & 0x100)
				return dev->regs[addr] & 0x02;
			else
				return dev->regs[addr] & 0x3E;
		}
	} else if (addr == 0x4E)
		return (dev->regs[addr] & 0xEF) | keyboard_at_get_mouse_scan();
	else if (addr == 0x69)
		return dev->regs[addr] & 0xFE;
	else if (addr == 0x6A) {
		if (dev->type == 3)
			return dev->regs[addr] & 0xD1;
		else
			return dev->regs[addr] & 0x07;
	} else if (addr == 0x6B) {
		if (dev->type == 3)
			return dev->regs[addr] & 0x80;
		else
			return 0;
	}
	else if (addr == 0x70) {
		if (dev->type == 3)
			return dev->regs[addr] & 0xEF;
		else
			return dev->regs[addr] & 0xCF;
	} else if (addr == 0x71) {
		if (dev->type == 3)
			return 0;
		else
			return dev->regs[addr] & 0xCF;
	} else if (addr == 0x76) {
		if (dev->type == 3)
			return dev->regs[addr] & 0x87;
		else
			return dev->regs[addr] & 0x8F;
	} else if (addr == 0x77) {
		if (dev->type == 3)
			return dev->regs[addr] & 0x87;
		else
			return dev->regs[addr] & 0x8F;
	} else if (addr == 0x80) {
		if (dev->type == 3)
			return dev->regs[addr] & 0x7F;
		else if (dev->type == 1)
			return 0;
	} else if (addr == 0x82) {
		if (dev->type == 3)
			return dev->regs[addr] & 0x0F;
		else
			return 0;
	} else if (addr == 0xA0)
		return dev->regs[addr] & 0x1F;
	else if (addr == 0xA3) {
		if (dev->type == 3)
			return dev->regs[addr] & 1;
		else
			return 0;
	} else if (addr == 0xA7) {
		if (dev->type == 3)
			return dev->regs[addr];
		else
			return dev->regs[addr] & 0xEF;
	} else if (addr == 0xAB) {
		if (dev->type == 3)
			return dev->regs[addr];
		else
			return dev->regs[addr] & 0xFE;
	} else
		return dev->regs[addr];
    }

    return 0;
}


static void
board_write(uint16_t port, uint8_t val, void *priv)
{
    piix_t *dev = (piix_t *) priv;

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

    return ret;
}


static void
piix_reset_hard(void *priv)
{
    piix_t *piix = (piix_t *) priv;

    uint16_t old_base = (piix->regs_ide[0x20] & 0xf0) | (piix->regs_ide[0x21] << 8);

    if (!(piix->type & 0x100)) {	/* PB640's PIIX has no IDE part. */
	sff_bus_master_reset(piix->bm[0], old_base);
	sff_bus_master_reset(piix->bm[1], old_base + 8);
    }

    memset(piix->regs, 0, 256);
    memset(piix->regs_ide, 0, 256);

    piix->regs[0x00] = 0x86; piix->regs[0x01] = 0x80; /*Intel*/
    if (piix->type == 3) {
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
    if (piix->type == 3)
	piix->regs[0x4f] = 0x00;
    piix->regs[0x60] = piix->regs[0x61] = piix->regs[0x62] = piix->regs[0x63] = 0x80;
    piix->regs[0x69] = 0x02;
    piix->regs[0x70] = 0xc0;
    if (piix->type != 3)
	piix->regs[0x71] = 0xc0;
    piix->regs[0x76] = piix->regs[0x77] = 0x0c;
    piix->regs[0x78] = 0x02; piix->regs[0x79] = 0x00;
    if (piix->type == 3) {
	piix->regs[0x80] = piix->regs[0x82] = 0x00;
    }
    piix->regs[0xa0] = 0x08;
    piix->regs[0xa2] = piix->regs[0xa3] = 0x00;
    piix->regs[0xa4] = piix->regs[0xa5] = piix->regs[0xa6] = piix->regs[0xa7] = 0x00;
    piix->regs[0xa8] = 0x0f;
    piix->regs[0xaa] = piix->regs[0xab] = 0x00;
    piix->regs[0xac] = 0x00;
    piix->regs[0xae] = 0x00;

    piix->regs_ide[0x00] = 0x86; piix->regs_ide[0x01] = 0x80; /*Intel*/
    if (piix->type == 3) {
	piix->regs_ide[0x02] = 0x10; piix->regs_ide[0x03] = 0x70; /*82371SB (PIIX3)*/
    } else {
	piix->regs_ide[0x02] = 0x30; piix->regs_ide[0x03] = 0x12; /*82371FB (PIIX)*/
    }
    piix->regs_ide[0x04] = 0x03; piix->regs_ide[0x05] = 0x00;
    piix->regs_ide[0x06] = 0x80; piix->regs_ide[0x07] = 0x02;
    piix->regs_ide[0x08] = 0x00;
    piix->regs_ide[0x09] = 0x80; piix->regs_ide[0x0a] = 0x01; piix->regs_ide[0x0b] = 0x01;
    piix->regs_ide[0x0d] = 0x00;
    piix->regs_ide[0x0e] = 0x00;
    piix->regs_ide[0x20] = 0x01; piix->regs_ide[0x21] = piix->regs_ide[0x22] = piix->regs_ide[0x23] = 0x00; /*Bus master interface base address*/
    piix->regs_ide[0x40] = piix->regs_ide[0x42] = 0x00;
    piix->regs_ide[0x41] = piix->regs_ide[0x43] = 0x00;
    if (piix->type == 3)
	piix->regs_ide[0x44] = 0x00;

    pci_set_irq_routing(PCI_INTA, PCI_IRQ_DISABLED);
    pci_set_irq_routing(PCI_INTB, PCI_IRQ_DISABLED);
    pci_set_irq_routing(PCI_INTC, PCI_IRQ_DISABLED);
    pci_set_irq_routing(PCI_INTD, PCI_IRQ_DISABLED);

    pci_set_mirq_routing(PCI_MIRQ0, PCI_IRQ_DISABLED);
    if (piix->type != 3)
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
    memset(piix, 0, sizeof(piix_t));

    pci_add_card(7, piix_read, piix_write, piix);

    piix->type = info->local;

    device_add(&apm_device);

    if (!(piix->type & 0x100)) {	/* PB640's PIIX has no IDE part. */
	piix->bm[0] = device_add_inst(&sff8038i_device, 1);
	piix->bm[1] = device_add_inst(&sff8038i_device, 2);
    }

    piix_reset_hard(piix);

    device_add(&port_92_pci_device);

    dma_alias_set();

    pci_enable_mirq(0);
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
