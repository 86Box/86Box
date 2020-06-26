/*
 * 86Box	A hypervisor and IBM PC system emulator that specializes in
 *		running old operating systems and software designed for IBM
 *		PC systems and compatibles from 1981 through fairly recent
 *		system designs based on the PCI bus.
 *
 *		This file is part of the 86Box distribution.
 *
 *		Implementation of the SMC FDC37C661 Super
 *		I/O Chip.
 *
 *
 *
 * Authors:	Sarah Walker, <http://pcem-emulator.co.uk/>
 *		Miran Grca, <mgrca8@gmail.com>
 *
 *		Copyright 2008-2020 Sarah Walker.
 *		Copyright 2016-2020 Miran Grca.
 *		Copyright 2020 plant/nerd73.
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
#include <86box/pci.h>
#include <86box/lpt.h>
#include <86box/serial.h>
#include <86box/hdc.h>
#include <86box/hdc_ide.h>
#include <86box/fdd.h>
#include <86box/fdc.h>
#include <86box/sio.h>


typedef struct {
    uint8_t lock[2],
	    regs[4];
    int cur_reg,
	com3_addr, com4_addr;
    fdc_t *fdc;
    serial_t *uart[2];
} fdc37c661_t;


static void
write_lock(fdc37c661_t *dev, uint8_t val)
{
    if (val == 0x55 && dev->lock[1] == 0x55)
	fdc_3f1_enable(dev->fdc, 0);
    if ((dev->lock[0] == 0x55) && (dev->lock[1] == 0x55) && (val != 0x55))
	fdc_3f1_enable(dev->fdc, 1);

    dev->lock[0] = dev->lock[1];
    dev->lock[1] = val;
}


static void
set_com34_addr(fdc37c661_t *dev)
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
set_serial_addr(fdc37c661_t *dev, int port)
{
    uint8_t shift = (port << 4);

    if (dev->regs[2] & (4 << shift)) {
	switch (dev->regs[2] & (3 << shift)) {
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
lpt1_handler(fdc37c661_t *dev)
{
    lpt1_remove();
    switch (dev->regs[1] & 3) {
	case 1:
		lpt1_init(0x3bc);
		lpt1_irq(7);
		break;
	case 2:
		lpt1_init(0x378);
		lpt1_irq(5);
		break;
	case 3:
		lpt1_init(0x278);
		lpt1_irq(5);
		break;
    }
}


static void
fdc_handler(fdc37c661_t *dev)
{
    fdc_remove(dev->fdc);
    if (dev->regs[0] & 0x10)
	fdc_set_base(dev->fdc, 0x03f0);    
}


static void
fdc37c661_write(uint16_t port, uint8_t val, void *priv)
{
    fdc37c661_t *dev = (fdc37c661_t *) priv;
    uint8_t valxor = 0;

    if ((dev->lock[0] == 0x55) && (dev->lock[1] == 0x55)) {
	if (port == 0x3f0) {
		if (val == 0xaa)
			write_lock(dev, val);
		else
			dev->cur_reg = val;
	} else {
		if (dev->cur_reg > 4)
			return;

		valxor = val ^ dev->regs[dev->cur_reg];
		dev->regs[dev->cur_reg] = val;

		switch(dev->cur_reg) {
			case 0:
				if (valxor & 0x10)
					fdc_handler(dev);

				break;
			case 1:
				if (valxor & 3)
					lpt1_handler(dev);
				if (valxor & 0x60) {
					serial_remove(dev->uart[0]);
					serial_remove(dev->uart[1]);
					set_com34_addr(dev);
					set_serial_addr(dev, 0);
					set_serial_addr(dev, 1);
				}
				break;
			case 2:
				if (valxor & 7) {
                	                serial_remove(dev->uart[0]);
					set_serial_addr(dev, 0);
				}
				if (valxor & 0x70) {
                	                serial_remove(dev->uart[1]);
					set_serial_addr(dev, 1);
				}
				break;
			case 3:
				if (valxor & 4)
					fdc_update_enh_mode(dev->fdc, (dev->regs[3] & 4) ? 1 : 0);
				break;
		}
	}
    } else {
	if (port == 0x3f0)
		write_lock(dev, val);
    }
}


static uint8_t
fdc37c661_read(uint16_t port, void *priv)
{
    fdc37c661_t *dev = (fdc37c661_t *) priv;
    uint8_t ret = 0xff;

    if ((dev->lock[0] == 0x55) && (dev->lock[1] == 0x55)) {
	if (port == 0x3f1)
		ret = dev->regs[dev->cur_reg];
    }

    return ret;
}


static void
fdc37c661_reset(fdc37c661_t *dev)
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

    memset(dev->lock, 0, 2);
    memset(dev->regs, 0, 16);

    dev->regs[0x0] = 0x3f;
    dev->regs[0x1] = 0x9f;
    dev->regs[0x2] = 0xdc;
    dev->regs[0x3] = 0x78;
}


static void
fdc37c661_close(void *priv)
{
    fdc37c661_t *dev = (fdc37c661_t *) priv;

    free(dev);
}


static void *
fdc37c661_init(const device_t *info)
{
    fdc37c661_t *dev = (fdc37c661_t *) malloc(sizeof(fdc37c661_t));
    memset(dev, 0, sizeof(fdc37c661_t));

    dev->fdc = device_add(&fdc_at_smc_device);

    dev->uart[0] = device_add_inst(&ns16550_device, 1);
    dev->uart[1] = device_add_inst(&ns16550_device, 2);

    io_sethandler(0x03f0, 0x0002,
		  fdc37c661_read, NULL, NULL, fdc37c661_write, NULL, NULL, dev);

    fdc37c661_reset(dev);
    
    return dev;
}

const device_t fdc37c661_device = {
    "SMC FDC37C661 Super I/O",
    0,
    0,
    fdc37c661_init, fdc37c661_close, NULL,
    NULL, NULL, NULL,
    NULL
};

