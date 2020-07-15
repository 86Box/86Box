/*
 * 86Box	A hypervisor and IBM PC system emulator that specializes in
 *		running old operating systems and software designed for IBM
 *		PC systems and compatibles from 1981 through fairly recent
 *		system designs based on the PCI bus.
 *
 *		This file is part of the 86Box distribution.
 *
 *		Implementation of the CMD PCI-0640B controller.
 
 * Authors:	Miran Grca, <mgrca8@gmail.com>
 *
 *              Copyright 2020 Miran Grca.
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
#include <86box/scsi_device.h>
#include <86box/scsi_cdrom.h>
#include <86box/dma.h>
#include <86box/io.h>
#include <86box/device.h>
#include <86box/keyboard.h>
#include <86box/mem.h>
#include <86box/pci.h>
#include <86box/pic.h>
#include <86box/timer.h>
#include <86box/hdc.h>
#include <86box/hdc_ide.h>
#include <86box/hdc_ide_sff8038i.h>
#include <86box/zip.h>
#include <86box/mo.h>


typedef struct
{
    uint8_t	vlb_idx, id,
		in_cfg, single_channel,
		regs[256];
    int		slot, irq_mode[2],
		irq_pin, irq_line;
} cmd640_t;


static int	next_id = 0;


void
cmd640_set_irq(int channel, void *priv)
{
    cmd640_t *dev = (cmd640_t *) priv;
    dev->regs[0x50] &= ~0x04;
    dev->regs[0x50] |= (channel >> 4);

    channel &= 0x01;
    if (dev->regs[0x50] & 0x04) {
	if (dev->irq_mode[channel] == 1)
		pci_set_irq(dev->slot, dev->irq_pin);
	else
		picint(1 << (14 + channel));
    } else {
	if (dev->irq_mode[channel] == 1)
		pci_clear_irq(dev->slot, dev->irq_pin);
	else
		picintc(1 << (14 + channel));
    }
}


static void
cmd640_ide_handlers(cmd640_t *dev)
{
    uint16_t main, side;

    ide_pri_disable();

    if ((dev->regs[0x09] & 0x01) && (dev->regs[0x50] & 0x40)) {
	main = (dev->regs[0x11] << 8) | (dev->regs[0x10] & 0xf8);
	side = ((dev->regs[0x15] << 8) | (dev->regs[0x14] & 0xfc)) + 2;
    } else {
	main = 0x1f0;
	side = 0x3f6;
    }

    ide_set_base(0, main);
    ide_set_side(0, side);

    if (dev->regs[0x04] & 0x01)
	ide_pri_enable();

    if (dev->single_channel)
	return;

    ide_sec_disable();

    if ((dev->regs[0x09] & 0x04) && (dev->regs[0x50] & 0x40)) {
	main = (dev->regs[0x19] << 8) | (dev->regs[0x18] & 0xf8);
	side = ((dev->regs[0x1d] << 8) | (dev->regs[0x1c] & 0xfc)) + 2;
    } else {
	main = 0x170;
	side = 0x376;
    }

    ide_set_base(1, main);
    ide_set_side(1, side);

    if ((dev->regs[0x04] & 0x01) && (dev->regs[0x51] & 0x08))
	ide_sec_enable();
}


static void
cmd640_common_write(int addr, uint8_t val, cmd640_t *dev)
{
    switch (addr) {
	case 0x51:
		dev->regs[addr] = val;
		cmd640_ide_handlers(dev);
		break;
	case 0x52: case 0x54: case 0x56: case 0x58:
	case 0x59:
		dev->regs[addr] = val;
		break;
	case 0x53: case 0x55:
		dev->regs[addr] = val & 0xc0;
		break;
	case 0x57:
		dev->regs[addr] = val & 0xdc;
		break;
    }
}


static void
cmd640_vlb_write(uint16_t addr, uint8_t val, void *priv)
{
    cmd640_t *dev = (cmd640_t *) priv;

    addr &= 0x00ff;
    
    switch (addr) {
	case 0x0078:
		if (dev->in_cfg)
			dev->vlb_idx = val;
		else if ((dev->regs[0x50] & 0x80) && (val == dev->id))
			dev->in_cfg = 1;
		break;
	case 0x007c:
		cmd640_common_write(dev->vlb_idx, val, dev);
		if (dev->regs[0x50] & 0x80)
			dev->in_cfg = 0;
		break;
    }
}


static void
cmd640_vlb_writew(uint16_t addr, uint16_t val, void *priv)
{
    cmd640_vlb_write(addr, val & 0xff, priv);
    cmd640_vlb_write(addr + 1, val >> 8, priv);
}


static void
cmd640_vlb_writel(uint16_t addr, uint32_t val, void *priv)
{
    cmd640_vlb_writew(addr, val & 0xffff, priv);
    cmd640_vlb_writew(addr + 2, val >> 16, priv);
}


static uint8_t
cmd640_vlb_read(uint16_t addr, void *priv)
{
    uint8_t ret = 0xff;
    cmd640_t *dev = (cmd640_t *) priv;

    addr &= 0x00ff;

    switch (addr) {
	case 0x0078:
		if (dev->in_cfg)
			ret = dev->vlb_idx;
		break;
	case 0x007c:
		ret = dev->regs[dev->vlb_idx];
		if (dev->vlb_idx == 0x50)
			dev->regs[0x50] &= ~0x04;
		if (dev->regs[0x50] & 0x80)
			dev->in_cfg = 0;
		break;
    }

    return ret;
}


static uint16_t
cmd640_vlb_readw(uint16_t addr, void *priv)
{
    uint16_t ret = 0xffff;

    ret = cmd640_vlb_read(addr, priv);
    ret |= (cmd640_vlb_read(addr + 1, priv) << 8);

    return ret;
}


static uint32_t
cmd640_vlb_readl(uint16_t addr, void *priv)
{
    uint32_t ret = 0xffffffff;

    ret = cmd640_vlb_readw(addr, priv);
    ret |= (cmd640_vlb_readw(addr + 2, priv) << 16);

    return ret;
}


static void
cmd640_pci_write(int func, int addr, uint8_t val, void *priv)
{
    cmd640_t *dev = (cmd640_t *) priv;

    if (func == 0x00)  switch (addr) {
	case 0x04:
		dev->regs[addr] = (val & 0x41);
		cmd640_ide_handlers(dev);
		break;
	case 0x07:
		dev->regs[addr] &= ~(val & 0x80);
		break;
	case 0x09:
		if ((dev->regs[addr] & 0x0a) == 0x0a) {
			dev->regs[addr] = (dev->regs[addr] & 0x0a) | (val & 0x05);
			dev->irq_mode[0] = !!(val & 0x01);
			dev->irq_mode[1] = !!(val & 0x04);
			cmd640_ide_handlers(dev);
		}
		break;
	case 0x10:
		if (dev->regs[0x50] & 0x40) {
			dev->regs[0x10] = (val & 0xf8) | 1;
			cmd640_ide_handlers(dev);
		}
		break;
	case 0x11:
		if (dev->regs[0x50] & 0x40) {
			dev->regs[0x11] = val;
			cmd640_ide_handlers(dev);
		}
		break;
	case 0x14:
		if (dev->regs[0x50] & 0x40) {
			dev->regs[0x14] = (val & 0xfc) | 1;
			cmd640_ide_handlers(dev);
		}
		break;
	case 0x15:
		if (dev->regs[0x50] & 0x40) {
			dev->regs[0x15] = val;
			cmd640_ide_handlers(dev);
		}
		break;
	case 0x18:
		if (dev->regs[0x50] & 0x40) {
			dev->regs[0x18] = (val & 0xf8) | 1;
			cmd640_ide_handlers(dev);
		}
		break;
	case 0x19:
		if (dev->regs[0x50] & 0x40) {
			dev->regs[0x19] = val;
			cmd640_ide_handlers(dev);
		}
		break;
	case 0x1c:
		if (dev->regs[0x50] & 0x40) {
			dev->regs[0x1c] = (val & 0xfc) | 1;
			cmd640_ide_handlers(dev);
		}
		break;
	case 0x1d:
		if (dev->regs[0x50] & 0x40) {
			dev->regs[0x1d] = val;
			cmd640_ide_handlers(dev);
		}
		break;
	default:
		cmd640_common_write(addr, val, dev);
		break;
    }
}


static uint8_t
cmd640_pci_read(int func, int addr, void *priv)
{
    cmd640_t *dev = (cmd640_t *) priv;
    uint8_t ret = 0xff;

    if (func == 0x00) {
	ret = dev->regs[addr];
	if (addr == 0x50)
		dev->regs[0x50] &= ~0x04;
    }

    return ret;
}


static void
cmd640_reset(void *p)
{
    int i = 0;

    for (i = 0; i < CDROM_NUM; i++) {
	if ((cdrom[i].bus_type == CDROM_BUS_ATAPI) &&
	    (cdrom[i].ide_channel < 4) && cdrom[i].priv)
		scsi_cdrom_reset((scsi_common_t *) cdrom[i].priv);
    }
    for (i = 0; i < ZIP_NUM; i++) {
	if ((zip_drives[i].bus_type == ZIP_BUS_ATAPI) &&
	    (zip_drives[i].ide_channel < 4) && zip_drives[i].priv)
		zip_reset((scsi_common_t *) zip_drives[i].priv);
    }
	for (i = 0; i < MO_NUM; i++) {
	if ((mo_drives[i].bus_type == MO_BUS_ATAPI) &&
	    (mo_drives[i].ide_channel < 4) && mo_drives[i].priv)
		mo_reset((scsi_common_t *) mo_drives[i].priv);
	}

    cmd640_set_irq(0x00, p);
    cmd640_set_irq(0x01, p);
}


static void
cmd640_close(void *priv)
{
    cmd640_t *dev = (cmd640_t *) priv;

    free(dev);

    next_id = 0;
}


static void *
cmd640_init(const device_t *info)
{
    cmd640_t *dev = (cmd640_t *) malloc(sizeof(cmd640_t));
    memset(dev, 0x00, sizeof(cmd640_t));

    dev->id = next_id | 0x60;

    dev->regs[0x50] = 0x02;		/* Revision 02 */
    dev->regs[0x50] |= (next_id << 3);	/* Device ID: 00 = 60h, 01 = 61h, 10 = 62h, 11 = 63h */

    dev->regs[0x59] = 0x40;

    if (info->flags & DEVICE_PCI) {
	if ((info->local & 0xffff) == 0x0a) {
		dev->regs[0x50] |= 0x40;	/* Enable Base address register R/W;
						   If 0, they return 0 and are read-only 8 */
	}

	dev->regs[0x00] = 0x95;		/* CMD */
	dev->regs[0x01] = 0x10;
	dev->regs[0x02] = 0x40;		/* PCI-0640B */
	dev->regs[0x03] = 0x06;
	dev->regs[0x07] = 0x02;		/* DEVSEL timing: 01 medium */
	dev->regs[0x08] = 0x02;		/* Revision 02 */
	dev->regs[0x09] = info->local;	/* Programming interface */
	dev->regs[0x0a] = 0x01;		/* IDE controller */
	dev->regs[0x0b] = 0x01;		/* Mass storage controller */

	/* Base addresses (1F0, 3F4, 170, 374) */
	if (dev->regs[0x50] & 0x40) {
		dev->regs[0x10] = 0xf1; dev->regs[0x11] = 0x01;
		dev->regs[0x14] = 0xf5; dev->regs[0x15] = 0x03;
		dev->regs[0x18] = 0x71; dev->regs[0x19] = 0x01;
		dev->regs[0x1c] = 0x75; dev->regs[0x1d] = 0x03;
	}

	dev->regs[0x3c] = 0x14;		/* IRQ 14 */
	dev->regs[0x3d] = 0x01;		/* INTA */

	device_add(&ide_vlb_2ch_device);

	dev->slot = pci_add_card(PCI_ADD_IDE, cmd640_pci_read, cmd640_pci_write, dev);
	dev->irq_mode[0] = dev->irq_mode[1] = 0;
	dev->irq_pin = PCI_INTA;
	dev->irq_line = 14;

	ide_set_bus_master(0, NULL, cmd640_set_irq, dev);
	ide_set_bus_master(1, NULL, cmd640_set_irq, dev);

	/* The CMD PCI-0640B IDE controller has no DMA capability,
	   so set our devices IDE devices to force ATA-3 (no DMA). */
	ide_board_set_force_ata3(0, 1);
	ide_board_set_force_ata3(1, 1);

	ide_pri_disable();
    } else if (info->flags & DEVICE_VLB) {
	if ((info->local & 0xffff) == 0x0078)
		dev->regs[0x50] |= 0x20;	/* 0 = 178h, 17Ch; 1 = 078h, 07Ch */
	/* If bit 7 is 1, then device ID has to be written on port x78h before
	   accessing the configuration registers */
	dev->in_cfg = 1;			/* Configuration register are accessible */

	device_add(&ide_pci_2ch_device);

	io_sethandler(0x0078, 0x0008,
		      cmd640_vlb_read, cmd640_vlb_readw, cmd640_vlb_readl,
		      cmd640_vlb_write, cmd640_vlb_writew, cmd640_vlb_writel,
		      dev);
    }

    dev->single_channel = !!(info->local & 0x20000);

    if (!dev->single_channel)
	ide_sec_disable();

    next_id++;

    return dev;
}


const device_t ide_cmd640_vlb_device = {
    "CMD PCI-0640B VLB",
    DEVICE_VLB,
    0x0078,
    cmd640_init, cmd640_close, NULL,
    NULL, NULL, NULL,
    NULL
};

const device_t ide_cmd640_vlb_178_device = {
    "CMD PCI-0640B VLB (Port 178h)",
    DEVICE_VLB,
    0x0178,
    cmd640_init, cmd640_close, NULL,
    NULL, NULL, NULL,
    NULL
};

const device_t ide_cmd640_pci_device = {
    "CMD PCI-0640B PCI",
    DEVICE_PCI,
    0x0a,
    cmd640_init, cmd640_close, cmd640_reset,
    NULL, NULL, NULL,
    NULL
};

const device_t ide_cmd640_pci_legacy_only_device = {
    "CMD PCI-0640B PCI (Legacy Mode Only)",
    DEVICE_PCI,
    0x00,
    cmd640_init, cmd640_close, cmd640_reset,
    NULL, NULL, NULL,
    NULL
};

const device_t ide_cmd640_pci_single_channel_device = {
    "CMD PCI-0640B PCI",
    DEVICE_PCI,
    0x2000a,
    cmd640_init, cmd640_close, cmd640_reset,
    NULL, NULL, NULL,
    NULL
};
