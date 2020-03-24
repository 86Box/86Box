/*
 * 86Box	A hypervisor and IBM PC system emulator that specializes in
 *		running old operating systems and software designed for IBM
 *		PC systems and compatibles from 1981 through fairly recent
 *		system designs based on the PCI bus.
 *
 *		This file is part of the 86Box distribution.
 *
 *		Emulation of the Winbond W83787F/IF Super I/O Chip.
 *
 *		Winbond W83787F Super I/O Chip
 *		Used by the Award 430HX
 *
 * Version:	@(#)sio_w83787f.c	1.0.0	2020/01/11
 *
 * Author:	Miran Grca, <mgrca8@gmail.com>
 *		Copyright 2020 Miran Grca.
 */
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>
#include "86box.h"
#include "device.h"
#include "86box_io.h"
#include "timer.h"
#include "pci.h"
#include "mem.h"
#include "rom.h"
#include "lpt.h"
#include "serial.h"
#include "fdd.h"
#include "fdc.h"
#include "sio.h"


#define FDDA_TYPE	(dev->regs[7] & 3)
#define FDDB_TYPE	((dev->regs[7] >> 2) & 3)
#define FDDC_TYPE	((dev->regs[7] >> 4) & 3)
#define FDDD_TYPE	((dev->regs[7] >> 6) & 3)

#define FD_BOOT		(dev->regs[8] & 3)
#define SWWP		((dev->regs[8] >> 4) & 1)
#define DISFDDWR	((dev->regs[8] >> 5) & 1)

#define EN3MODE		((dev->regs[9] >> 5) & 1)

#define DRV2EN_NEG	(dev->regs[0xB] & 1)		/* 0 = drive 2 installed */
#define INVERTZ		((dev->regs[0xB] >> 1) & 1)	/* 0 = invert DENSEL polarity */
#define IDENT		((dev->regs[0xB] >> 3) & 1)

#define HEFERE		((dev->regs[0xC] >> 5) & 1)


typedef struct {
    uint8_t tries, regs[42];
    uint16_t reg_init;
    int locked, rw_locked,
	cur_reg,
	key;
    fdc_t *fdc;
    serial_t *uart[2];
} w83787f_t;


static void	w83787f_write(uint16_t port, uint8_t val, void *priv);
static uint8_t	w83787f_read(uint16_t port, void *priv);


static void
w83787f_remap(w83787f_t *dev)
{
    io_removehandler(0x250, 0x0003,
		     w83787f_read, NULL, NULL, w83787f_write, NULL, NULL, dev);
    io_sethandler(0x250, 0x0003,
		  w83787f_read, NULL, NULL, w83787f_write, NULL, NULL, dev);
    dev->key = 0x88 | HEFERE;
}


#ifdef FIXME
/* FIXME: Implement EPP (and ECP) parallel port modes. */
static uint8_t
get_lpt_length(w83787f_t *dev)
{
    uint8_t length = 4;

    if (dev->regs[9] & 0x80) {
	if (dev->regs[0] & 0x04)
		length = 8;	/* EPP mode. */
	if (dev->regs[0] & 0x08)
		length |= 0x80;	/* ECP mode. */
    }

    return length;
}
#endif


static void
w83787f_serial_handler(w83787f_t *dev, int uart)
{
    int urs0 = !!(dev->regs[1] & (1 << uart));
    int urs1 = !!(dev->regs[1] & (4 << uart));
    int urs2 = !!(dev->regs[3] & (8 >> uart));
    int urs, irq = 4;
    uint16_t addr = 0x3f8, enable = 1;

    urs = (urs1 << 1) | urs0;

    if (urs2) {
	addr = uart ? 0x3f8 : 0x2f8;
	irq = uart ? 4 : 3;
    } else {
	switch (urs) {
		case 0:
			addr = uart ? 0x3e8 : 0x2e8;
			irq = uart ? 4 : 3;
			break;
		case 1:
			addr = uart ? 0x2e8 : 0x3e8;
			irq = uart ? 3 : 4;
			break;
		case 2:
			addr = uart ? 0x2f8 : 0x3f8;
			irq = uart ? 3 : 4;
			break;
		case 3:
		default:
			enable = 0;
			break;
	}
    }

    if (dev->regs[4] & (0x20 >> uart))
	enable = 0;

    serial_remove(dev->uart[uart]);
    if (enable)
	serial_setup(dev->uart[uart], addr, irq);
}


static void
w83787f_lpt_handler(w83787f_t *dev)
{
    int ptrs0 = !!(dev->regs[1] & 4);
    int ptrs1 = !!(dev->regs[1] & 5);
    int ptrs, irq = 7;
    uint16_t addr = 0x378, enable = 1;

    ptrs = (ptrs1 << 1) | ptrs0;

    switch (ptrs) {
	case 0:
		addr = 0x3bc;
		irq = 7;
		break;
	case 1:
		addr = 0x278;
		irq = 5;
		break;
	case 2:
		addr = 0x378;
		irq = 7;
		break;
	case 3:
	default:
		enable = 0;
		break;
    }

    if (dev->regs[4] & 0x80)
	enable = 0;

    lpt1_remove();
    if (enable) {
	lpt1_init(addr);
	lpt1_irq(irq);
    }
}


static void
w83787f_fdc_handler(w83787f_t *dev)
{
    fdc_remove(dev->fdc);
    if (!(dev->regs[0] & 0x20))
	fdc_set_base(dev->fdc, (dev->regs[0] & 0x10) ? 0x03f0 : 0x0370);
}


static void
w83787f_write(uint16_t port, uint8_t val, void *priv)
{
    w83787f_t *dev = (w83787f_t *) priv;
    uint8_t valxor = 0;
    uint8_t max = 0x15;
    pclog("W83787F: Write %02X to %04X\n", val, port);

    if (port == 0x250) {
	if (val == dev->key)
		dev->locked = 1;
	else
		dev->locked = 0;
	return;
    } else if (port == 0x251) {
	if (val <= max)
		dev->cur_reg = val;
	return;
    } else {
	if (dev->locked) {
		if (dev->rw_locked)
			return;
		if (dev->cur_reg == 6)
			val &= 0xF3;
		valxor = val ^ dev->regs[dev->cur_reg];
		dev->regs[dev->cur_reg] = val;
	} else
		return;
    }

    switch (dev->cur_reg) {
	case 0:
		if (valxor & 0x30)
			w83787f_fdc_handler(dev);
		if (valxor & 0x0c)
			w83787f_lpt_handler(dev);
		break;
	case 1:
		if (valxor & 0x80)
			fdc_set_swap(dev->fdc, (dev->regs[1] & 0x80) ? 1 : 0);
		if (valxor & 0x30)
			w83787f_lpt_handler(dev);
		if (valxor & 0x0a)
			w83787f_serial_handler(dev, 1);
		if (valxor & 0x05)
			w83787f_serial_handler(dev, 0);
		break;
	case 3:
		if (valxor & 0x80)
			w83787f_lpt_handler(dev);
		if (valxor & 0x08)
			w83787f_serial_handler(dev, 0);
		if (valxor & 0x04)
			w83787f_serial_handler(dev, 1);
		break;
	case 4:
		if (valxor & 0x10)
			w83787f_serial_handler(dev, 1);
		if (valxor & 0x20)
			w83787f_serial_handler(dev, 0);
		if (valxor & 0x80)
			w83787f_lpt_handler(dev);
		break;
	case 6:
		if (valxor & 0x08) {
			fdc_remove(dev->fdc);
			if (!(dev->regs[6] & 0x08))
				fdc_set_base(dev->fdc, 0x03f0);
		}
		break;
	case 7:
		if (valxor & 0x03)
			fdc_update_rwc(dev->fdc, 0, FDDA_TYPE);
		if (valxor & 0x0c)
			fdc_update_rwc(dev->fdc, 1, FDDB_TYPE);
		if (valxor & 0x30)
			fdc_update_rwc(dev->fdc, 2, FDDC_TYPE);
		if (valxor & 0xc0)
			fdc_update_rwc(dev->fdc, 3, FDDD_TYPE);
		break;
	case 8:
		if (valxor & 0x03)
			fdc_update_boot_drive(dev->fdc, FD_BOOT);
		if (valxor & 0x10)
			fdc_set_swwp(dev->fdc, SWWP ? 1 : 0);
		if (valxor & 0x20)
			fdc_set_diswr(dev->fdc, DISFDDWR ? 1 : 0);
		break;
	case 9:
		if (valxor & 0x20)
			fdc_update_enh_mode(dev->fdc, EN3MODE ? 1 : 0);
		if (valxor & 0x40)
			dev->rw_locked = (val & 0x40) ? 1 : 0;
		if (valxor & 0x80)
			w83787f_lpt_handler(dev);
		break;
	case 0xC:
		if (valxor & 0x20)
			w83787f_remap(dev);
		break;
    }
}


static uint8_t
w83787f_read(uint16_t port, void *priv)
{
    w83787f_t *dev = (w83787f_t *) priv;
    uint8_t ret = 0xff;

    if (dev->locked) {
	if (port == 0x251)
		ret = dev->cur_reg;
	else if (port == 0x252) {
		if (dev->cur_reg == 7)
			ret = (fdc_get_rwc(dev->fdc, 0) | (fdc_get_rwc(dev->fdc, 1) << 2));
		else if (!dev->rw_locked)
			ret = dev->regs[dev->cur_reg];
	}
    }

    pclog("W83787F: Read %02X from %04X\n", ret, port);

    return ret;
}


static void
w83787f_reset(w83787f_t *dev)
{
    lpt1_remove();
    lpt1_init(0x378);
    lpt1_irq(7);

    fdc_reset(dev->fdc);

    memset(dev->regs, 0, 0x2A);
    dev->regs[0x00] = 0x50;
    dev->regs[0x01] = 0x2C;
    dev->regs[0x03] = 0x30;
    dev->regs[0x07] = 0xF5;
    dev->regs[0x09] = dev->reg_init & 0xff;
    dev->regs[0x0a] = 0x1F;
    dev->regs[0x0c] = 0x2C;
    dev->regs[0x0d] = 0xA3;

    serial_setup(dev->uart[0], SERIAL1_ADDR, SERIAL1_IRQ);
    serial_setup(dev->uart[1], SERIAL2_ADDR, SERIAL2_IRQ);

    dev->key = 0x89;

    w83787f_remap(dev);

    dev->locked = 0;
    dev->rw_locked = 0;
}


static void
w83787f_close(void *priv)
{
    w83787f_t *dev = (w83787f_t *) priv;

    free(dev);
}


static void *
w83787f_init(const device_t *info)
{
    w83787f_t *dev = (w83787f_t *) malloc(sizeof(w83787f_t));
    memset(dev, 0, sizeof(w83787f_t));

    dev->fdc = device_add(&fdc_at_winbond_device);

    dev->uart[0] = device_add_inst(&ns16550_device, 1);
    dev->uart[1] = device_add_inst(&ns16550_device, 2);

    dev->reg_init = info->local;

    w83787f_reset(dev);

    return dev;
}


const device_t w83787f_device = {
    "Winbond W83787F/IF Super I/O",
    0,
    0x09,
    w83787f_init, w83787f_close, NULL,
    NULL, NULL, NULL,
    NULL
};
