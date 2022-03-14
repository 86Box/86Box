/*
 * 86Box	A hypervisor and IBM PC system emulator that specializes in
 *		running old operating systems and software designed for IBM
 *		PC systems and compatibles from 1981 through fairly recent
 *		system designs based on the PCI bus.
 *
 *		This file is part of the 86Box distribution.
 *
 *		Implementation of the CMD PCI-0646 controller.
 *
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
    uint8_t	vlb_idx, single_channel,
		in_cfg, regs[256];
    uint32_t	local;
    int		slot, irq_mode[2],
		irq_pin;
    sff8038i_t	*bm[2];
} cmd646_t;


#ifdef ENABLE_CMD646_LOG
int cmd646_do_log = ENABLE_CMD646_LOG;
static void
cmd646_log(const char *fmt, ...)
{
    va_list ap;

    if (cmd646_do_log)
    {
	va_start(ap, fmt);
	pclog_ex(fmt, ap);
	va_end(ap);
    }
}
#else
#define cmd646_log(fmt, ...)
#endif


static void
cmd646_set_irq(int channel, void *priv)
{
    cmd646_t *dev = (cmd646_t *) priv;

    if (channel & 0x01) {
	if (!(dev->regs[0x57] & 0x10) || (channel & 0x40)) {
		dev->regs[0x57] &= ~0x10;
		dev->regs[0x57] |= (channel >> 2);
	}
    } else {
	if (!(dev->regs[0x50] & 0x04) || (channel & 0x40)) {
		dev->regs[0x50] &= ~0x04;
		dev->regs[0x50] |= (channel >> 4);
	}
    }

    sff_bus_master_set_irq(channel, dev->bm[channel & 0x01]);
}


static int
cmd646_bus_master_dma(int channel, uint8_t *data, int transfer_length, int out, void *priv)
{
    cmd646_t *dev = (cmd646_t *) priv;

    return sff_bus_master_dma(channel, data, transfer_length, out, dev->bm[channel & 0x01]);
}


static void
cmd646_ide_handlers(cmd646_t *dev)
{
    uint16_t main, side;
    int irq_mode[2] = { 0, 0 };

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

    if (dev->regs[0x09] & 0x01)
	irq_mode[0] = 1;

    sff_set_irq_mode(dev->bm[0], 0, irq_mode[0]);
    sff_set_irq_mode(dev->bm[0], 1, irq_mode[1]);

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

    if (dev->regs[0x09] & 0x04)
	irq_mode[1] = 1;

    sff_set_irq_mode(dev->bm[1], 0, irq_mode[0]);
    sff_set_irq_mode(dev->bm[1], 1, irq_mode[1]);

    if ((dev->regs[0x04] & 0x01) && (dev->regs[0x51] & 0x08))
	ide_sec_enable();

}


static void
cmd646_ide_bm_handlers(cmd646_t *dev)
{
    uint16_t base = (dev->regs[0x20] & 0xf0) | (dev->regs[0x21] << 8);

    sff_bus_master_handler(dev->bm[0], (dev->regs[0x04] & 1), base);
    sff_bus_master_handler(dev->bm[1], (dev->regs[0x04] & 1), base + 8);
}


static void
cmd646_pci_write(int func, int addr, uint8_t val, void *priv)
{
    cmd646_t *dev = (cmd646_t *) priv;

    cmd646_log("[%04X:%08X] (%08X) cmd646_pci_write(%i, %02X, %02X)\n", CS, cpu_state.pc, ESI, func, addr, val);

    if (func == 0x00)  switch (addr) {
	case 0x04:
		dev->regs[addr] = (val & 0x45);
		cmd646_ide_handlers(dev);
		break;
	case 0x07:
		dev->regs[addr] &= ~(val & 0xb1);
		break;
	case 0x09:
		if ((dev->regs[addr] & 0x0a) == 0x0a) {
			dev->regs[addr] = (dev->regs[addr] & 0x0a) | (val & 0x05);
			dev->irq_mode[0] = !!(val & 0x01);
			dev->irq_mode[1] = !!(val & 0x04);
			cmd646_ide_handlers(dev);
		}
		break;
	case 0x10:
		if (dev->regs[0x50] & 0x40) {
			dev->regs[0x10] = (val & 0xf8) | 1;
			cmd646_ide_handlers(dev);
		}
		break;
	case 0x11:
		if (dev->regs[0x50] & 0x40) {
			dev->regs[0x11] = val;
			cmd646_ide_handlers(dev);
		}
		break;
	case 0x14:
		if (dev->regs[0x50] & 0x40) {
			dev->regs[0x14] = (val & 0xfc) | 1;
			cmd646_ide_handlers(dev);
		}
		break;
	case 0x15:
		if (dev->regs[0x50] & 0x40) {
			dev->regs[0x15] = val;
			cmd646_ide_handlers(dev);
		}
		break;
	case 0x18:
		if (dev->regs[0x50] & 0x40) {
			dev->regs[0x18] = (val & 0xf8) | 1;
			cmd646_ide_handlers(dev);
		}
		break;
	case 0x19:
		if (dev->regs[0x50] & 0x40) {
			dev->regs[0x19] = val;
			cmd646_ide_handlers(dev);
		}
		break;
	case 0x1c:
		if (dev->regs[0x50] & 0x40) {
			dev->regs[0x1c] = (val & 0xfc) | 1;
			cmd646_ide_handlers(dev);
		}
		break;
	case 0x1d:
		if (dev->regs[0x50] & 0x40) {
			dev->regs[0x1d] = val;
			cmd646_ide_handlers(dev);
		}
		break;
	case 0x20:
		dev->regs[0x20] = (val & 0xf0) | 1;
		cmd646_ide_bm_handlers(dev);
		break;
	case 0x21:
		dev->regs[0x21] = val;
		cmd646_ide_bm_handlers(dev);
		break;
	case 0x51:
		dev->regs[addr] = val & 0xc8;
		cmd646_ide_handlers(dev);
		break;
	case 0x52: case 0x54: case 0x56: case 0x58:
	case 0x59: case 0x5b:
		dev->regs[addr] = val;
		break;
	case 0x53: case 0x55:
		dev->regs[addr] = val & 0xc0;
		break;
	case 0x57:
		dev->regs[addr] = (dev->regs[addr] & 0x10) | (val & 0xcc);
		break;
	case 0x70 ... 0x77:
		sff_bus_master_write(addr & 0x0f, val, dev->bm[0]);
		break;
	case 0x78 ... 0x7f:
		sff_bus_master_write(addr & 0x0f, val, dev->bm[1]);
		break;
    }
}


static uint8_t
cmd646_pci_read(int func, int addr, void *priv)
{
    cmd646_t *dev = (cmd646_t *) priv;
    uint8_t ret = 0xff;

    if (func == 0x00) {
	ret = dev->regs[addr];

	if (addr == 0x50)
		dev->regs[0x50] &= ~0x04;
	else if (addr == 0x57)
		dev->regs[0x57] &= ~0x10;
	else if ((addr >= 0x70) && (addr <= 0x77))
		ret = sff_bus_master_read(addr & 0x0f, dev->bm[0]);
	else if ((addr >= 0x78) && (addr <= 0x7f))
		ret = sff_bus_master_read(addr & 0x0f, dev->bm[0]);
    }

    cmd646_log("[%04X:%08X] (%08X) cmd646_pci_read(%i, %02X, %02X)\n", CS, cpu_state.pc, ESI, func, addr, ret);

    return ret;
}


static void
cmd646_reset(void *priv)
{
    cmd646_t *dev = (cmd646_t *) priv;
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

    cmd646_set_irq(0x00, priv);
    cmd646_set_irq(0x01, priv);

    memset(dev->regs, 0x00, sizeof(dev->regs));

    dev->regs[0x00] = 0x95;		/* CMD */
    dev->regs[0x01] = 0x10;
    dev->regs[0x02] = 0x46;		/* PCI-0646 */
    dev->regs[0x03] = 0x06;
    dev->regs[0x04] = 0x00;
    dev->regs[0x06] = 0x80;
    dev->regs[0x07] = 0x02;		/* DEVSEL timing: 01 medium */
    dev->regs[0x09] = dev->local;	/* Programming interface */
    dev->regs[0x0a] = 0x01;		/* IDE controller */
    dev->regs[0x0b] = 0x01;		/* Mass storage controller */

    if ((dev->local & 0xffff) == 0x8a) {
	dev->regs[0x50] = 0x40;		/* Enable Base address register R/W;
					   If 0, they return 0 and are read-only 8 */

	/* Base addresses (1F0, 3F4, 170, 374) */
	dev->regs[0x10] = 0xf1; dev->regs[0x11] = 0x01;
	dev->regs[0x14] = 0xf5; dev->regs[0x15] = 0x03;
	dev->regs[0x18] = 0x71; dev->regs[0x19] = 0x01;
	dev->regs[0x1c] = 0x75; dev->regs[0x1d] = 0x03;
    }

    dev->regs[0x20] = 0x01;

    dev->regs[0x3c] = 0x0e;		/* IRQ 14 */
    dev->regs[0x3d] = 0x01;		/* INTA */
    dev->regs[0x3e] = 0x02;		/* Min_Gnt */
    dev->regs[0x3f] = 0x04;		/* Max_Iat */

    if (!dev->single_channel)
	dev->regs[0x51] = 0x08;

    dev->regs[0x57] = 0x0c;
    dev->regs[0x59] = 0x40;

    dev->irq_mode[0] = dev->irq_mode[1] = 0;
    dev->irq_pin = PCI_INTA;

    cmd646_ide_handlers(dev);
    cmd646_ide_bm_handlers(dev);
}


static void
cmd646_close(void *priv)
{
    cmd646_t *dev = (cmd646_t *) priv;

    free(dev);
}


static void *
cmd646_init(const device_t *info)
{
    cmd646_t *dev = (cmd646_t *) malloc(sizeof(cmd646_t));
    memset(dev, 0x00, sizeof(cmd646_t));

    dev->local = info->local;

    device_add(&ide_pci_2ch_device);

    dev->slot = pci_add_card(PCI_ADD_IDE, cmd646_pci_read, cmd646_pci_write, dev);

    dev->single_channel = !!(info->local & 0x20000);

    dev->bm[0] = device_add_inst(&sff8038i_device, 1);
    if (!dev->single_channel)
	dev->bm[1] = device_add_inst(&sff8038i_device, 2);

    ide_set_bus_master(0, cmd646_bus_master_dma, cmd646_set_irq, dev);
    if (!dev->single_channel)
	ide_set_bus_master(1, cmd646_bus_master_dma, cmd646_set_irq, dev);

    sff_set_irq_mode(dev->bm[0], 0, 0);
    sff_set_irq_mode(dev->bm[0], 1, 0);

    if (!dev->single_channel) {
	sff_set_irq_mode(dev->bm[1], 0, 0);
	sff_set_irq_mode(dev->bm[1], 1, 0);
    }

    cmd646_reset(dev);

    return dev;
}


const device_t ide_cmd646_device = {
    .name = "CMD PCI-0646",
    .internal_name = "ide_cmd646",
    .flags = DEVICE_PCI,
    .local = 0x8a,
    .init = cmd646_init,
    .close = cmd646_close,
    .reset = cmd646_reset,
    { .available = NULL },
    .speed_changed = NULL,
    .force_redraw = NULL,
    .config = NULL
};

const device_t ide_cmd646_legacy_only_device = {
    .name = "CMD PCI-0646 (Legacy Mode Only)",
    .internal_name = "ide_cmd646_legacy_only",
    .flags = DEVICE_PCI,
    .local = 0x80,
    .init = cmd646_init,
    .close = cmd646_close,
    .reset = cmd646_reset,
    { .available = NULL },
    .speed_changed = NULL,
    .force_redraw = NULL,
    .config = NULL
};

const device_t ide_cmd646_single_channel_device = {
    .name = "CMD PCI-0646",
    .internal_name = "ide_cmd646_single_channel",
    .flags = DEVICE_PCI,
    .local = 0x2008a,
    .init = cmd646_init,
    .close = cmd646_close,
    .reset = cmd646_reset,
    { .available = NULL },
    .speed_changed = NULL,
    .force_redraw = NULL,
    .config = NULL
};
