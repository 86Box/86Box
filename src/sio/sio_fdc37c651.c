/*
 * 86Box	A hypervisor and IBM PC system emulator that specializes in
 *		running old operating systems and software designed for IBM
 *		PC systems and compatibles from 1981 through fairly recent
 *		system designs based on the PCI bus.
 *
 *		This file is part of the 86Box distribution.
 *
 *		Implementation of the SMC FDC37C651 Super I/O Chip.
 *
 * Authors:	Miran Grca, <mgrca8@gmail.com>
 *
 *		Copyright 2021 Miran Grca.
 */
#include <stdarg.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>
#define HAVE_STDARG_H
#include <86box/86box.h>
#include <86box/io.h>
#include <86box/timer.h>
#include <86box/device.h>
#include <86box/lpt.h>
#include <86box/serial.h>
#include <86box/hdc.h>
#include <86box/hdc_ide.h>
#include <86box/fdd.h>
#include <86box/fdc.h>
#include <86box/sio.h>


#ifdef ENABLE_FDC37C651_LOG
int fdc37c651_do_log = ENABLE_FDC37C651_LOG;


static void
fdc37c651_log(const char *fmt, ...)
{
    va_list ap;

    if (fdc37c651_do_log) {
        va_start(ap, fmt);
        pclog_ex(fmt, ap);
        va_end(ap);
    }
}
#else
#define fdc37c651_log(fmt, ...)
#endif


typedef struct {
    uint8_t tries, has_ide,
	    regs[16];
    int cur_reg,
	com3_addr, com4_addr;
    fdc_t *fdc;
    serial_t *uart[2];
} fdc37c651_t;


static void
set_com34_addr(fdc37c651_t *dev)
{
    switch (dev->regs[1] & 0x60) {
	case 0x00:
		dev->com3_addr = 0x338;
		dev->com4_addr = 0x238;
		break;
	case 0x20:
		dev->com3_addr = 0x3e8;
		dev->com4_addr = 0x2e8;
		break;
	case 0x40:
		dev->com3_addr = 0x3e8;
		dev->com4_addr = 0x2e0;
		break;
	case 0x60:
		dev->com3_addr = 0x220;
		dev->com4_addr = 0x228;
		break;
    }
}


static void
set_serial_addr(fdc37c651_t *dev, int port)
{
    uint8_t shift = (port << 2);

    serial_remove(dev->uart[port]);
    if (dev->regs[2] & (4 << shift)) {
	switch ((dev->regs[2] >> shift) & 3) {
		case 0:
			serial_setup(dev->uart[port], SERIAL1_ADDR, SERIAL1_IRQ);
			break;
		case 1:
			serial_setup(dev->uart[port], SERIAL2_ADDR, SERIAL2_IRQ);
			break;
		case 2:
			serial_setup(dev->uart[port], dev->com3_addr, 4);
			break;
		case 3:
			serial_setup(dev->uart[port], dev->com4_addr, 3);
			break;
	}
    }
}


static void
lpt1_handler(fdc37c651_t *dev)
{
    lpt1_remove();
    switch (dev->regs[1] & 3) {
	case 1:
		lpt1_init(0x3bc);
		lpt1_irq(7);
		break;
	case 2:
		lpt1_init(0x378);
		lpt1_irq(7 /*5*/);
		break;
	case 3:
		lpt1_init(0x278);
		lpt1_irq(7 /*5*/);
		break;
    }
}


static void
fdc_handler(fdc37c651_t *dev)
{
    fdc_remove(dev->fdc);
    if (dev->regs[0] & 0x10)
	fdc_set_base(dev->fdc, 0x03f0);
}



static void
ide_handler(fdc37c651_t *dev)
{
    /* TODO: Make an ide_disable(channel) and ide_enable(channel) so we can simplify this. */
    if (dev->has_ide == 2) {
	ide_sec_disable();
	ide_set_base(1, 0x1f0);
	ide_set_side(1, 0x3f6);
	if (dev->regs[0x00] & 0x01)
		ide_sec_enable();
    } else if (dev->has_ide == 1) {
	ide_pri_disable();
	ide_set_base(0, 0x1f0);
	ide_set_side(0, 0x3f6);
	if (dev->regs[0x00] & 0x01)
		ide_pri_enable();
    }
}


static void
fdc37c651_write(uint16_t port, uint8_t val, void *priv)
{
    fdc37c651_t *dev = (fdc37c651_t *) priv;
    uint8_t valxor = 0;

    if (dev->tries == 2) {
	if (port == 0x3f0) {
		if (val == 0xaa)
			dev->tries = 0;
		else
			dev->cur_reg = val;
	} else {
		if (dev->cur_reg > 15)
			return;

		valxor = val ^ dev->regs[dev->cur_reg];
		dev->regs[dev->cur_reg] = val;

		switch(dev->cur_reg) {
			case 0:
				if (dev->has_ide && (valxor & 0x01))
					ide_handler(dev);
				if (valxor & 0x10)
					fdc_handler(dev);
				break;
			case 1:
				if (valxor & 3)
					lpt1_handler(dev);
				if (valxor & 0x60) {
					set_com34_addr(dev);
					set_serial_addr(dev, 0);
					set_serial_addr(dev, 1);
				}
				break;
			case 2:
				if (valxor & 7)
					set_serial_addr(dev, 0);
				if (valxor & 0x70)
					set_serial_addr(dev, 1);
				break;
		}
	}
    } else if ((port == 0x3f0) && (val == 0x55))
	dev->tries++;
}


static uint8_t
fdc37c651_read(uint16_t port, void *priv)
{
    fdc37c651_t *dev = (fdc37c651_t *) priv;
    uint8_t ret = 0x00;

    if (dev->tries == 2) {
	if (port == 0x3f1)
		ret = dev->regs[dev->cur_reg];
    }

    return ret;
}


static void
fdc37c651_reset(fdc37c651_t *dev)
{
    dev->com3_addr = 0x338;
    dev->com4_addr = 0x238;

    serial_remove(dev->uart[0]);
    serial_setup(dev->uart[0], SERIAL1_ADDR, SERIAL1_IRQ);

    serial_remove(dev->uart[1]);
    serial_setup(dev->uart[1], SERIAL2_ADDR, SERIAL2_IRQ);

    lpt1_remove();
    lpt1_init(0x378);

    fdc_reset(dev->fdc);
    fdc_remove(dev->fdc);

    dev->tries = 0;
    memset(dev->regs, 0, 16);

    dev->regs[0x0] = 0x3f;
    dev->regs[0x1] = 0x9f;
    dev->regs[0x2] = 0xdc;

    set_serial_addr(dev, 0);
    set_serial_addr(dev, 1);

    lpt1_handler(dev);

    fdc_handler(dev);

    if (dev->has_ide)
	ide_handler(dev);
}


static void
fdc37c651_close(void *priv)
{
    fdc37c651_t *dev = (fdc37c651_t *) priv;

    free(dev);
}


static void *
fdc37c651_init(const device_t *info)
{
    fdc37c651_t *dev = (fdc37c651_t *) malloc(sizeof(fdc37c651_t));
    memset(dev, 0, sizeof(fdc37c651_t));

    dev->fdc = device_add(&fdc_at_smc_device);

    dev->uart[0] = device_add_inst(&ns16450_device, 1);
    dev->uart[1] = device_add_inst(&ns16450_device, 2);

    dev->has_ide = (info->local >> 8) & 0xff;

    io_sethandler(0x03f0, 0x0002,
		  fdc37c651_read, NULL, NULL, fdc37c651_write, NULL, NULL, dev);

    fdc37c651_reset(dev);

    return dev;
}


/* The three appear to differ only in the chip ID, if I
   understood their datasheets correctly. */
const device_t fdc37c651_device = {
    "SMC FDC37C651 Super I/O",
    0,
    0,
    fdc37c651_init, fdc37c651_close, NULL,
    { NULL }, NULL, NULL,
    NULL
};

const device_t fdc37c651_ide_device = {
    "SMC FDC37C651 Super I/O (With IDE)",
    0,
    0x100,
    fdc37c651_init, fdc37c651_close, NULL,
    { NULL }, NULL, NULL,
    NULL
};
