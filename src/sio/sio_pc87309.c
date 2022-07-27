/*
 * 86Box	A hypervisor and IBM PC system emulator that specializes in
 *		running old operating systems and software designed for IBM
 *		PC systems and compatibles from 1981 through fairly recent
 *		system designs based on the PCI bus.
 *
 *		This file is part of the 86Box distribution.
 *
 *		Emulation of the NatSemi PC87309 Super I/O chip.
 *
 *
 *
 * Author:	Miran Grca, <mgrca8@gmail.com>
 *		Copyright 2020 Miran Grca.
 */
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>
#include <86box/86box.h>
#include <86box/io.h>
#include <86box/timer.h>
#include <86box/device.h>
#include <86box/lpt.h>
#include <86box/mem.h>
#include <86box/nvr.h>
#include <86box/pci.h>
#include <86box/rom.h>
#include <86box/serial.h>
#include <86box/hdc.h>
#include <86box/hdc_ide.h>
#include <86box/fdd.h>
#include <86box/fdc.h>
#include <86box/sio.h>


typedef struct {
    uint8_t id, pm_idx,
	    regs[48], ld_regs[256][208],
	    pm[8];
    uint16_t pm_base;
    int cur_reg;
    fdc_t *fdc;
    serial_t *uart[2];
} pc87309_t;


static void	fdc_handler(pc87309_t *dev);
static void	lpt1_handler(pc87309_t *dev);
static void	serial_handler(pc87309_t *dev, int uart);


static void
pc87309_pm_write(uint16_t port, uint8_t val, void *priv)
{
    pc87309_t *dev = (pc87309_t *) priv;

    if (port & 1) {
	dev->pm[dev->pm_idx] = val;

	switch (dev->pm_idx) {
		case 0x00:
			fdc_handler(dev);
			lpt1_handler(dev);
			serial_handler(dev, 1);
			serial_handler(dev, 0);
			break;
	}
    } else
	dev->pm_idx = val & 0x07;
}


uint8_t
pc87309_pm_read(uint16_t port, void *priv)
{
    pc87309_t *dev = (pc87309_t *) priv;

    if (port & 1)
	return dev->pm[dev->pm_idx];
    else
	return dev->pm_idx;
}


static void
pc87309_pm_remove(pc87309_t *dev)
{
    if (dev->pm_base != 0xffff) {
	io_removehandler(dev->pm_base, 0x0008,
			 pc87309_pm_read, NULL, NULL, pc87309_pm_write, NULL, NULL, dev);
	dev->pm_base = 0xffff;
    }
}


static void
pc87309_pm_init(pc87309_t *dev, uint16_t addr)
{
    dev->pm_base = addr;

    io_sethandler(dev->pm_base, 0x0008,
		  pc87309_pm_read, NULL, NULL, pc87309_pm_write, NULL, NULL, dev);
}


static void
fdc_handler(pc87309_t *dev)
{
    uint8_t irq, active;
    uint16_t addr;

    fdc_remove(dev->fdc);

    active = (dev->ld_regs[0x00][0x00] & 0x01) && (dev->pm[0x00] & 0x08);
    addr = ((dev->ld_regs[0x00][0x30] << 8) | dev->ld_regs[0x00][0x31]) - 0x0002;
    irq = (dev->ld_regs[0x00][0x40] & 0x0f);

    if (active) {
	fdc_set_base(dev->fdc, addr);
	fdc_set_irq(dev->fdc, irq);
    }
}


static void
lpt1_handler(pc87309_t *dev)
{
    uint8_t irq, active;
    uint16_t addr;

    lpt1_remove();

    active = (dev->ld_regs[0x01][0x00] & 0x01) && (dev->pm[0x00] & 0x10);
    addr = (dev->ld_regs[0x01][0x30] << 8) | dev->ld_regs[0x01][0x31];
    irq = (dev->ld_regs[0x01][0x40] & 0x0f);

    if (active) {
	lpt1_init(addr);
	lpt1_irq(irq);
    }
}


static void
serial_handler(pc87309_t *dev, int uart)
{
    uint8_t irq, active;
    uint16_t addr;

    serial_remove(dev->uart[uart]);

    active = (dev->ld_regs[0x03 - uart][0x00] & 0x01) && (dev->pm[0x00] & (1 << (6 - uart)));
    addr = (dev->ld_regs[0x03 - uart][0x30] << 8) | dev->ld_regs[0x03 - uart][0x31];
    irq = (dev->ld_regs[0x03 - uart][0x40] & 0x0f);

    if (active)
	serial_setup(dev->uart[uart], addr, irq);
}


static void
pm_handler(pc87309_t *dev)
{
    uint8_t active;
    uint16_t addr;

    pc87309_pm_remove(dev);

    active = (dev->ld_regs[0x04][0x00] & 0x01);
    addr = (dev->ld_regs[0x04][0x30] << 8) | dev->ld_regs[0x04][0x31];

    if (active)
	pc87309_pm_init(dev, addr);
}


static void
pc87309_write(uint16_t port, uint8_t val, void *priv)
{
    pc87309_t *dev = (pc87309_t *) priv;
    uint8_t index;

    index = (port & 1) ? 0 : 1;

    if (index) {
	dev->cur_reg = val;
	return;
    } else {
	switch (dev->cur_reg) {
		case 0x00: case 0x02: case 0x03: case 0x06:
		case 0x07: case 0x21:
			dev->regs[dev->cur_reg] = val;
			break;
		case 0x22:
			dev->regs[dev->cur_reg] = val & 0x7f;
			break;
		default:
			if (dev->cur_reg >= 0x30) {
				if ((dev->regs[0x07] != 0x06) || !(dev->regs[0x21] & 0x10))
					dev->ld_regs[dev->regs[0x07]][dev->cur_reg - 0x30] = val;
			}
			break;
	}
    }

    switch(dev->cur_reg) {
	case 0x30:
		dev->ld_regs[dev->regs[0x07]][dev->cur_reg - 0x30] = val & 0x01;
		switch (dev->regs[0x07]) {
			case 0x00:
				fdc_handler(dev);
				break;
			case 0x01:
				lpt1_handler(dev);
				break;
			case 0x02:
				serial_handler(dev, 1);
				break;
			case 0x03:
				serial_handler(dev, 0);
				break;
			case 0x04:
				pm_handler(dev);
				break;
		}
		break;
	case 0x60: case 0x62:
		dev->ld_regs[dev->regs[0x07]][dev->cur_reg - 0x30] = val & 0x07;
		if (dev->cur_reg == 0x62)
			break;
		switch (dev->regs[0x07]) {
			case 0x00:
				fdc_handler(dev);
				break;
			case 0x01:
				lpt1_handler(dev);
				break;
			case 0x02:
				serial_handler(dev, 1);
				break;
			case 0x03:
				serial_handler(dev, 0);
				break;
			case 0x04:
				pm_handler(dev);
				break;
		}
		break;
	case 0x63:
		if (dev->regs[0x07] == 0x06)
			dev->ld_regs[dev->regs[0x07]][dev->cur_reg - 0x30] = (val & 0xf8) | 0x04;
		break;
	case 0x61:
		switch (dev->regs[0x07]) {
			case 0x00:
				dev->ld_regs[dev->regs[0x07]][dev->cur_reg - 0x30] = (val & 0xfa) | 0x02;
				fdc_handler(dev);
				break;
			case 0x01:
				dev->ld_regs[dev->regs[0x07]][dev->cur_reg - 0x30] = val & 0xfc;
				lpt1_handler(dev);
				break;
			case 0x02:
				dev->ld_regs[dev->regs[0x07]][dev->cur_reg - 0x30] = val & 0xf8;
				serial_handler(dev, 1);
				break;
			case 0x03:
				dev->ld_regs[dev->regs[0x07]][dev->cur_reg - 0x30] = val & 0xf8;
				serial_handler(dev, 0);
				break;
			case 0x04:
				dev->ld_regs[dev->regs[0x07]][dev->cur_reg - 0x30] = val & 0xfe;
				pm_handler(dev);
				break;
			case 0x06:
				dev->ld_regs[dev->regs[0x07]][dev->cur_reg - 0x30] = val & 0xf8;
				break;
		}
		break;
	case 0x70:
	case 0x74: case 0x75:
		switch (dev->regs[0x07]) {
			case 0x00:
				fdc_handler(dev);
				break;
			case 0x01:
				lpt1_handler(dev);
				break;
			case 0x02:
				serial_handler(dev, 1);
				break;
			case 0x03:
				serial_handler(dev, 0);
				break;
			case 0x04:
				pm_handler(dev);
				break;
		}
		break;
	case 0xf0:
		switch (dev->regs[0x07]) {
			case 0x00:
				dev->ld_regs[dev->regs[0x07]][dev->cur_reg - 0x30] = val & 0xe1;
				fdc_update_densel_polarity(dev->fdc, (val & 0x20) ? 1 : 0);
				fdc_update_enh_mode(dev->fdc, (val & 0x40) ? 1 : 0);
				break;
			case 0x01:
				dev->ld_regs[dev->regs[0x07]][dev->cur_reg - 0x30] = val & 0xf3;
				lpt1_handler(dev);
				break;
			case 0x02: case 0x03:
				dev->ld_regs[dev->regs[0x07]][dev->cur_reg - 0x30] = val & 0x87;
				break;
			case 0x06:
				dev->ld_regs[dev->regs[0x07]][dev->cur_reg - 0x30] = val & 0xc1;
				break;
		}
		break;
	case 0xf1:
		if (dev->regs[0x07] == 0x00)
			dev->ld_regs[dev->regs[0x07]][dev->cur_reg - 0x30] = val & 0x0f;
		break;
    }
}


uint8_t
pc87309_read(uint16_t port, void *priv)
{
    pc87309_t *dev = (pc87309_t *) priv;
    uint8_t ret = 0xff, index;

    index = (port & 1) ? 0 : 1;

    if (index)
	ret = dev->cur_reg & 0x1f;
    else {
	if (dev->cur_reg >= 0x30)
		ret = dev->ld_regs[dev->regs[0x07]][dev->cur_reg - 0x30];
	else
		ret = dev->regs[dev->cur_reg];
    }

    return ret;
}


void
pc87309_reset(pc87309_t *dev)
{
    int i;

    memset(dev->regs, 0x00, 0x30);
    for (i = 0; i < 256; i++)
	memset(dev->ld_regs[i], 0x00, 0xd0);
    memset(dev->pm, 0x00, 0x08);

    dev->regs[0x20] = dev->id;
    dev->regs[0x21] = 0x04;

    dev->ld_regs[0x00][0x01] = 0x01;
    dev->ld_regs[0x00][0x30] = 0x03;
    dev->ld_regs[0x00][0x31] = 0xf2;
    dev->ld_regs[0x00][0x40] = 0x06;
    dev->ld_regs[0x00][0x41] = 0x03;
    dev->ld_regs[0x00][0x44] = 0x02;
    dev->ld_regs[0x00][0x45] = 0x04;
    dev->ld_regs[0x00][0xc0] = 0x02;

    dev->ld_regs[0x01][0x30] = 0x02;
    dev->ld_regs[0x01][0x31] = 0x78;
    dev->ld_regs[0x01][0x40] = 0x07;
    dev->ld_regs[0x01][0x44] = 0x04;
    dev->ld_regs[0x01][0x45] = 0x04;
    dev->ld_regs[0x01][0xc0] = 0xf2;

    dev->ld_regs[0x02][0x30] = 0x02;
    dev->ld_regs[0x02][0x31] = 0xf8;
    dev->ld_regs[0x02][0x40] = 0x03;
    dev->ld_regs[0x02][0x41] = 0x03;
    dev->ld_regs[0x02][0x44] = 0x04;
    dev->ld_regs[0x02][0x45] = 0x04;
    dev->ld_regs[0x02][0xc0] = 0x02;

    dev->ld_regs[0x03][0x30] = 0x03;
    dev->ld_regs[0x03][0x31] = 0xf8;
    dev->ld_regs[0x03][0x40] = 0x04;
    dev->ld_regs[0x03][0x41] = 0x03;
    dev->ld_regs[0x03][0x44] = 0x04;
    dev->ld_regs[0x03][0x45] = 0x04;
    dev->ld_regs[0x03][0xc0] = 0x02;

    dev->ld_regs[0x04][0x44] = 0x04;
    dev->ld_regs[0x04][0x45] = 0x04;

    dev->ld_regs[0x05][0x40] = 0x0c;
    dev->ld_regs[0x05][0x41] = 0x02;
    dev->ld_regs[0x05][0x44] = 0x04;
    dev->ld_regs[0x05][0x45] = 0x04;

    dev->ld_regs[0x06][0x01] = 0x01;
    dev->ld_regs[0x06][0x31] = 0x60;
    dev->ld_regs[0x06][0x33] = 0x64;
    dev->ld_regs[0x06][0x40] = 0x01;
    dev->ld_regs[0x06][0x41] = 0x02;
    dev->ld_regs[0x06][0x44] = 0x04;
    dev->ld_regs[0x06][0x45] = 0x04;
    dev->ld_regs[0x06][0xc0] = 0x40;

    dev->regs[0x00] = 0x0B;
    dev->regs[0x01] = 0x01;
    dev->regs[0x03] = 0x01;
    dev->regs[0x05] = 0x0D;
    dev->regs[0x08] = 0x70;
    dev->regs[0x09] = 0xC0;
    dev->regs[0x0b] = 0x80;
    dev->regs[0x0f] = 0x1E;
    dev->regs[0x12] = 0x30;
    dev->regs[0x19] = 0xEF;

    dev->pm[0] = 0x79;
    dev->pm[4] = 0x0e;

    dev->pm_base = 0xffff;

    /*
	0 = 360 rpm @ 500 kbps for 3.5"
	1 = Default, 300 rpm @ 500,300,250,1000 kbps for 3.5"
    */
    lpt1_remove();
    serial_remove(dev->uart[0]);
    serial_remove(dev->uart[1]);
    fdc_reset(dev->fdc);
}


static void
pc87309_close(void *priv)
{
    pc87309_t *dev = (pc87309_t *) priv;

    free(dev);
}


static void *
pc87309_init(const device_t *info)
{
    pc87309_t *dev = (pc87309_t *) malloc(sizeof(pc87309_t));
    memset(dev, 0, sizeof(pc87309_t));

    dev->id = info->local & 0xff;

    dev->fdc = device_add(&fdc_at_nsc_device);

    dev->uart[0] = device_add_inst(&ns16550_device, 1);
    dev->uart[1] = device_add_inst(&ns16550_device, 2);

    pc87309_reset(dev);

    if (info->local & 0x100) {
	io_sethandler(0x15c, 0x0002,
		      pc87309_read, NULL, NULL, pc87309_write, NULL, NULL, dev);
    } else {
	io_sethandler(0x02e, 0x0002,
		      pc87309_read, NULL, NULL, pc87309_write, NULL, NULL, dev);
    }

    return dev;
}

const device_t pc87309_device = {
    .name = "National Semiconductor PC87309 Super I/O",
    .internal_name = "pc87309",
    .flags = 0,
    .local = 0xe0,
    .init = pc87309_init,
    .close = pc87309_close,
    .reset = NULL,
    { .available = NULL },
    .speed_changed = NULL,
    .force_redraw = NULL,
    .config = NULL
};

const device_t pc87309_15c_device = {
    .name = "National Semiconductor PC87309 Super I/O (Port 15Ch)",
    .internal_name = "pc87309_15c",
    .flags = 0,
    .local = 0x1e0,
    .init = pc87309_init,
    .close = pc87309_close,
    .reset = NULL,
    { .available = NULL },
    .speed_changed = NULL,
    .force_redraw = NULL,
    .config = NULL
};
