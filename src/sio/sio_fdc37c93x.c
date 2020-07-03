/*
 * 86Box	A hypervisor and IBM PC system emulator that specializes in
 *		running old operating systems and software designed for IBM
 *		PC systems and compatibles from 1981 through fairly recent
 *		system designs based on the PCI bus.
 *
 *		This file is part of the 86Box distribution.
 *
 *		Implementation of the SMC FDC37C932FR and FDC37C935 Super
 *		I/O Chips.
 *
 *
 *
 * Author:	Miran Grca, <mgrca8@gmail.com>
 *		Copyright 2016-2018 Miran Grca.
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
#include <86box/nvr.h>
#include <86box/sio.h>


#define AB_RST	0x80


typedef struct {
	uint8_t control;
	uint8_t status;
	uint8_t own_addr;
	uint8_t data;
	uint8_t clock;
	uint16_t base;
} access_bus_t;

typedef struct {
    uint8_t chip_id, tries,
	    gpio_regs[2], auxio_reg,
	    regs[48],
	    ld_regs[10][256];
    uint16_t gpio_base,	/* Set to EA */
	     auxio_base, nvr_sec_base;
    int locked,
	cur_reg;
    fdc_t *fdc;
    serial_t *uart[2];
    access_bus_t *access_bus;
    nvr_t *nvr;
} fdc37c93x_t;


static uint16_t
make_port(fdc37c93x_t *dev, uint8_t ld)
{
    uint16_t r0 = dev->ld_regs[ld][0x60];
    uint16_t r1 = dev->ld_regs[ld][0x61];

    uint16_t p = (r0 << 8) + r1;

    return p;
}


static uint16_t
make_port_sec(fdc37c93x_t *dev, uint8_t ld)
{
    uint16_t r0 = dev->ld_regs[ld][0x62];
    uint16_t r1 = dev->ld_regs[ld][0x63];

    uint16_t p = (r0 << 8) + r1;

    return p;
}


static uint8_t
fdc37c93x_auxio_read(uint16_t port, void *priv)
{
    fdc37c93x_t *dev = (fdc37c93x_t *) priv;

    return dev->auxio_reg;
}


static void
fdc37c93x_auxio_write(uint16_t port, uint8_t val, void *priv)
{
    fdc37c93x_t *dev = (fdc37c93x_t *) priv;

    dev->auxio_reg = val;
}


static uint8_t
fdc37c93x_gpio_read(uint16_t port, void *priv)
{
    fdc37c93x_t *dev = (fdc37c93x_t *) priv;
    uint8_t ret = 0xff;

    ret = dev->gpio_regs[port & 1];

    return ret;
}


static void
fdc37c93x_gpio_write(uint16_t port, uint8_t val, void *priv)
{
    fdc37c93x_t *dev = (fdc37c93x_t *) priv;

    if (!(port & 1))
	dev->gpio_regs[0] = (dev->gpio_regs[0] & 0xfc) | (val & 0x03);
}


static void
fdc37c93x_fdc_handler(fdc37c93x_t *dev)
{
    uint16_t ld_port = 0;
    uint8_t global_enable = !!(dev->regs[0x22] & (1 << 0));
    uint8_t local_enable = !!dev->ld_regs[0][0x30];

    fdc_remove(dev->fdc);
    if (global_enable && local_enable) {
	ld_port = make_port(dev, 0);
	if ((ld_port >= 0x0100) && (ld_port <= 0x0FF8))
		fdc_set_base(dev->fdc, ld_port);
    }
}


static void
fdc37c93x_lpt_handler(fdc37c93x_t *dev)
{
    uint16_t ld_port = 0;
    uint8_t global_enable = !!(dev->regs[0x22] & (1 << 3));
    uint8_t local_enable = !!dev->ld_regs[3][0x30];
    uint8_t lpt_irq = dev->ld_regs[3][0x70];

    if (lpt_irq > 15)
	lpt_irq = 0xff;

    lpt1_remove();
    if (global_enable && local_enable) {
	ld_port = make_port(dev, 3);
	if ((ld_port >= 0x0100) && (ld_port <= 0x0FFC))
		lpt1_init(ld_port);
    }
    lpt1_irq(lpt_irq);
}


static void
fdc37c93x_serial_handler(fdc37c93x_t *dev, int uart)
{
    uint16_t ld_port = 0;
    uint8_t uart_no = 4 + uart;
    uint8_t global_enable = !!(dev->regs[0x22] & (1 << uart_no));
    uint8_t local_enable = !!dev->ld_regs[uart_no][0x30];

    serial_remove(dev->uart[uart]);
    if (global_enable && local_enable) {
	ld_port = make_port(dev, uart_no);
	if ((ld_port >= 0x0100) && (ld_port <= 0x0FF8))
		serial_setup(dev->uart[uart], ld_port, dev->ld_regs[uart_no][0x70]);
    }
}


static void
fdc37c93x_nvr_pri_handler(fdc37c93x_t *dev)
{
    uint8_t local_enable = !!dev->ld_regs[6][0x30];

    nvr_at_handler(0, 0x70, dev->nvr);
    if (local_enable)
	nvr_at_handler(1, 0x70, dev->nvr);
}


static void
fdc37c93x_nvr_sec_handler(fdc37c93x_t *dev)
{
    uint16_t ld_port = 0;
    uint8_t local_enable = !!dev->ld_regs[6][0x30];

    nvr_at_sec_handler(0, dev->nvr_sec_base, dev->nvr);
    if (local_enable) {
	dev->nvr_sec_base = ld_port = make_port_sec(dev, 6);
	if ((ld_port >= 0x0100) && (ld_port <= 0x0FFE))
		nvr_at_sec_handler(1, ld_port, dev->nvr);
    }
}


static void
fdc37c93x_auxio_handler(fdc37c93x_t *dev)
{
    uint16_t ld_port = 0;
    uint8_t local_enable = !!dev->ld_regs[8][0x30];

    io_removehandler(dev->auxio_base, 0x0001,
		     fdc37c93x_auxio_read, NULL, NULL, fdc37c93x_auxio_write, NULL, NULL, dev);
    if (local_enable) {
	dev->auxio_base = ld_port = make_port(dev, 8);
	if ((ld_port >= 0x0100) && (ld_port <= 0x0FFF))
	        io_sethandler(dev->auxio_base, 0x0001,
			      fdc37c93x_auxio_read, NULL, NULL, fdc37c93x_auxio_write, NULL, NULL, dev);
    }
}


static void fdc37c93x_gpio_handler(fdc37c93x_t *dev)
{
    uint16_t ld_port = 0;
    uint8_t local_enable;

    local_enable = !!(dev->regs[0x03] & 0x80);

    io_removehandler(dev->gpio_base, 0x0002,
		     fdc37c93x_gpio_read, NULL, NULL, fdc37c93x_gpio_write, NULL, NULL, dev);
    if (local_enable) {
	switch (dev->regs[0x03] & 0x03) {
		case 0:
			ld_port = 0xe0;
			break;
		case 1:
			ld_port = 0xe2;
			break;
		case 2:
			ld_port = 0xe4;
			break;
		case 3:
			ld_port = 0xea;	/* Default */
			break;
	}
	dev->gpio_base = ld_port;
	if (ld_port > 0x0000)
	        io_sethandler(dev->gpio_base, 0x0002,
			      fdc37c93x_gpio_read, NULL, NULL, fdc37c93x_gpio_write, NULL, NULL, dev);
    }
}


static uint8_t
fdc37c932fr_access_bus_read(uint16_t port, void *priv)
{
    access_bus_t *dev = (access_bus_t *) priv;
    uint8_t ret = 0xff;

    switch(port & 3) {
	case 0:
		ret = (dev->status & 0xBF);
		break;
	case 1:
		ret = (dev->own_addr & 0x7F);
		break;
	case 2:
		ret = dev->data;
		break;
	case 3:
		ret = (dev->clock & 0x87);
		break;
    }

    return ret;
}


static void
fdc37c932fr_access_bus_write(uint16_t port, uint8_t val, void *priv)
{
    access_bus_t *dev = (access_bus_t *) priv;

    switch(port & 3) {
	case 0:
		dev->control = (val & 0xCF);
		break;
	case 1:
		dev->own_addr = (val & 0x7F);
		break;
	case 2:
		dev->data = val;
		break;
	case 3:
		dev->clock &= 0x80;
		dev->clock |= (val & 0x07);
		break;
    }
}


static void fdc37c932fr_access_bus_handler(fdc37c93x_t *dev)
{
    uint16_t ld_port = 0;
    uint8_t global_enable = !!(dev->regs[0x22] & (1 << 6));
    uint8_t local_enable = !!dev->ld_regs[9][0x30];

    io_removehandler(dev->access_bus->base, 0x0004,
		     fdc37c932fr_access_bus_read, NULL, NULL, fdc37c932fr_access_bus_write, NULL, NULL, dev->access_bus);
    if (global_enable && local_enable) {
	dev->access_bus->base = ld_port = make_port(dev, 9);
	if ((ld_port >= 0x0100) && (ld_port <= 0x0FFC))
	        io_sethandler(dev->access_bus->base, 0x0004,
			      fdc37c932fr_access_bus_read, NULL, NULL, fdc37c932fr_access_bus_write, NULL, NULL, dev->access_bus);
    }
}


static void
fdc37c93x_write(uint16_t port, uint8_t val, void *priv)
{
    fdc37c93x_t *dev = (fdc37c93x_t *) priv;
    uint8_t index = (port & 1) ? 0 : 1;
    uint8_t valxor = 0;

    if (index) {
	if ((val == 0x55) && !dev->locked) {
		if (dev->tries) {
			dev->locked = 1;
			fdc_3f1_enable(dev->fdc, 0);
			dev->tries = 0;
		} else
			dev->tries++;
	} else {
		if (dev->locked) {
			if (val == 0xaa) {
				dev->locked = 0;
				fdc_3f1_enable(dev->fdc, 1);
				return;
			}
			dev->cur_reg = val;
		} else {
			if (dev->tries)
				dev->tries = 0;
		}
	}
	return;
    } else {
	if (dev->locked) {
		if (dev->cur_reg < 48) {
			valxor = val ^ dev->regs[dev->cur_reg];
			if ((val == 0x20) || (val == 0x21))
				return;
			dev->regs[dev->cur_reg] = val;
		} else {
			valxor = val ^ dev->ld_regs[dev->regs[7]][dev->cur_reg];
			if (((dev->cur_reg & 0xF0) == 0x70) && (dev->regs[7] < 4))
				return;
			/* Block writes to some logical devices. */
			if (dev->regs[7] > 9)
				return;
			else switch (dev->regs[7]) {
				case 1:
				case 2:
				case 7:
					return;
				case 9:
					/* If we're on the FDC37C935, return as this is not a valid
					   logical device there. */
					if (dev->chip_id == 0x02)
						return;
					break;
			}
			dev->ld_regs[dev->regs[7]][dev->cur_reg] = val;
		}
	} else
		return;
    }

    if (dev->cur_reg < 48) {
	switch(dev->cur_reg) {
		case 0x03:
			if (valxor & 0x83)
				fdc37c93x_gpio_handler(dev);
			dev->regs[0x03] &= 0x83;
			break;
		case 0x22:
			if (valxor & 0x01)
				fdc37c93x_fdc_handler(dev);
			if (valxor & 0x08)
				fdc37c93x_lpt_handler(dev);
			if (valxor & 0x10)
				fdc37c93x_serial_handler(dev, 0);
			if (valxor & 0x20)
				fdc37c93x_serial_handler(dev, 1);
			if ((valxor & 0x40) && (dev->chip_id != 0x02))
				fdc37c932fr_access_bus_handler(dev);
			break;
	}

	return;
    }

    switch(dev->regs[7]) {
	case 0:
		/* FDD */
		switch(dev->cur_reg) {
			case 0x30:
			case 0x60:
			case 0x61:
				if ((dev->cur_reg == 0x30) && (val & 0x01))
					dev->regs[0x22] |= 0x01;
				if (valxor)
					fdc37c93x_fdc_handler(dev);
				break;
			case 0xF0:
				if (valxor & 0x01)
					fdc_update_enh_mode(dev->fdc, val & 0x01);
				if (valxor & 0x10)
					fdc_set_swap(dev->fdc, (val & 0x10) >> 4);
				break;
			case 0xF1:
				if (valxor & 0xC)
					fdc_update_densel_force(dev->fdc, (val & 0xC) >> 2);
				break;
			case 0xF2:
				if (valxor & 0xC0)
					fdc_update_rwc(dev->fdc, 3, (valxor & 0xC0) >> 6);
				if (valxor & 0x30)
					fdc_update_rwc(dev->fdc, 2, (valxor & 0x30) >> 4);
				if (valxor & 0x0C)
					fdc_update_rwc(dev->fdc, 1, (valxor & 0x0C) >> 2);
				if (valxor & 0x03)
					fdc_update_rwc(dev->fdc, 0, (valxor & 0x03));
				break;
			case 0xF4:
				if (valxor & 0x18)
					fdc_update_drvrate(dev->fdc, 0, (val & 0x18) >> 3);
				break;
			case 0xF5:
				if (valxor & 0x18)
					fdc_update_drvrate(dev->fdc, 1, (val & 0x18) >> 3);
				break;
			case 0xF6:
				if (valxor & 0x18)
					fdc_update_drvrate(dev->fdc, 2, (val & 0x18) >> 3);
				break;
			case 0xF7:
				if (valxor & 0x18)
					fdc_update_drvrate(dev->fdc, 3, (val & 0x18) >> 3);
				break;
		}
		break;
	case 3:
		/* Parallel port */
		switch(dev->cur_reg) {
			case 0x30:
			case 0x60:
			case 0x61:
			case 0x70:
				if ((dev->cur_reg == 0x30) && (val & 0x01))
					dev->regs[0x22] |= 0x08;
				if (valxor)
					fdc37c93x_lpt_handler(dev);
				break;
		}
		break;
	case 4:
		/* Serial port 1 */
		switch(dev->cur_reg) {
			case 0x30:
			case 0x60:
			case 0x61:
			case 0x70:
				if ((dev->cur_reg == 0x30) && (val & 0x01))
					dev->regs[0x22] |= 0x10;
				if (valxor)
					fdc37c93x_serial_handler(dev, 0);
				break;
		}
		break;
	case 5:
		/* Serial port 2 */
		switch(dev->cur_reg) {
			case 0x30:
			case 0x60:
			case 0x61:
			case 0x70:
				if ((dev->cur_reg == 0x30) && (val & 0x01))
					dev->regs[0x22] |= 0x20;
				if (valxor)
					fdc37c93x_serial_handler(dev, 1);
				break;
		}
		break;
	case 6:
		/* RTC/NVR */
		if (dev->chip_id != 0x30)
			break;
		switch(dev->cur_reg) {
			case 0x30:
				if (valxor)
					fdc37c93x_nvr_pri_handler(dev);
				/* FALLTHROUGH */
			case 0x60:
			case 0x61:
				if (valxor)
					fdc37c93x_nvr_sec_handler(dev);
				break;
			case 0xf0:
				if (valxor) {
					nvr_lock_set(0x80, 0x20, !!(dev->ld_regs[6][dev->cur_reg] & 0x01), dev->nvr);
					nvr_lock_set(0xa0, 0x20, !!(dev->ld_regs[6][dev->cur_reg] & 0x02), dev->nvr);
					nvr_lock_set(0xc0, 0x20, !!(dev->ld_regs[6][dev->cur_reg] & 0x04), dev->nvr);
					nvr_lock_set(0xe0, 0x20, !!(dev->ld_regs[6][dev->cur_reg] & 0x08), dev->nvr);
					if (dev->ld_regs[6][dev->cur_reg] & 0x80)  switch ((dev->ld_regs[6][dev->cur_reg] >> 4) & 0x07) {
						case 0x00:
						default:
							nvr_bank_set(0, 0xff, dev->nvr);
							nvr_bank_set(1, 1, dev->nvr);
							break;
						case 0x01:
							nvr_bank_set(0, 0, dev->nvr);
							nvr_bank_set(1, 1, dev->nvr);
							break;
						case 0x02: case 0x04:
							nvr_bank_set(0, 0xff, dev->nvr);
							nvr_bank_set(1, 0xff, dev->nvr);
							break;
						case 0x03: case 0x05:
							nvr_bank_set(0, 0, dev->nvr);
							nvr_bank_set(1, 0xff, dev->nvr);
							break;
						case 0x06:
							nvr_bank_set(0, 0xff, dev->nvr);
							nvr_bank_set(1, 2, dev->nvr);
							break;
						case 0x07:
							nvr_bank_set(0, 0, dev->nvr);
							nvr_bank_set(1, 2, dev->nvr);
							break;
					} else {
						nvr_bank_set(0, 0, dev->nvr);
						nvr_bank_set(1, 0xff, dev->nvr);
					}
				}
				break;
		}
		break;
	case 8:
		/* Auxiliary I/O */
		switch(dev->cur_reg) {
			case 0x30:
			case 0x60:
			case 0x61:
			case 0x70:
				if (valxor)
					fdc37c93x_auxio_handler(dev);
				break;
		}
		break;
	case 9:
		/* Access bus (FDC37C932FR only) */
		switch(dev->cur_reg) {
			case 0x30:
			case 0x60:
			case 0x61:
			case 0x70:
				if ((dev->cur_reg == 0x30) && (val & 0x01))
					dev->regs[0x22] |= 0x40;
				if (valxor)
					fdc37c932fr_access_bus_handler(dev);
				break;
		}
		break;
    }
}


static uint8_t fdc37c93x_read(uint16_t port, void *priv)
{
    fdc37c93x_t *dev = (fdc37c93x_t *) priv;
    uint8_t index = (port & 1) ? 0 : 1;
    uint8_t ret = 0xff;

    if (dev->locked) {
	if (index)
		ret = dev->cur_reg;
	else {
		if (dev->cur_reg < 0x30) {
			if (dev->cur_reg == 0x20)
				ret = dev->chip_id;
			else
				ret = dev->regs[dev->cur_reg];
		} else {
			if ((dev->regs[7] == 0) && (dev->cur_reg == 0xF2)) {
				ret = (fdc_get_rwc(dev->fdc, 0) | (fdc_get_rwc(dev->fdc, 1) << 2) |
				      (fdc_get_rwc(dev->fdc, 2) << 4) | (fdc_get_rwc(dev->fdc, 3) << 6));
			} else
				ret = dev->ld_regs[dev->regs[7]][dev->cur_reg];
		}
	}
    }

    return ret;
}


static void
fdc37c93x_reset(fdc37c93x_t *dev)
{
    int i = 0;

    memset(dev->regs, 0, 48);

    dev->regs[0x03] = 0x03;
    dev->regs[0x20] = dev->chip_id;
    dev->regs[0x21] = 0x01;
    dev->regs[0x22] = 0x39;
    dev->regs[0x24] = 0x04;
    dev->regs[0x26] = 0xF0;
    dev->regs[0x27] = 0x03;

    for (i = 0; i < 10; i++)
	memset(dev->ld_regs[i], 0, 256);

    /* Logical device 0: FDD */
    dev->ld_regs[0][0x30] = 1;
    dev->ld_regs[0][0x60] = 3;
    dev->ld_regs[0][0x61] = 0xF0;
    dev->ld_regs[0][0x70] = 6;
    dev->ld_regs[0][0x74] = 2;
    dev->ld_regs[0][0xF0] = 0xE;
    dev->ld_regs[0][0xF2] = 0xFF;

    /* Logical device 1: IDE1 */
    dev->ld_regs[1][0x30] = 0;
    dev->ld_regs[1][0x60] = 1;
    dev->ld_regs[1][0x61] = 0xF0;
    dev->ld_regs[1][0x62] = 3;
    dev->ld_regs[1][0x63] = 0xF6;
    dev->ld_regs[1][0x70] = 0xE;
    dev->ld_regs[1][0xF0] = 0xC;

    /* Logical device 2: IDE2 */
    dev->ld_regs[2][0x30] = 0;
    dev->ld_regs[2][0x60] = 1;
    dev->ld_regs[2][0x61] = 0x70;
    dev->ld_regs[2][0x62] = 3;
    dev->ld_regs[2][0x63] = 0x76;
    dev->ld_regs[2][0x70] = 0xF;

    /* Logical device 3: Parallel Port */
    dev->ld_regs[3][0x30] = 1;
    dev->ld_regs[3][0x60] = 3;
    dev->ld_regs[3][0x61] = 0x78;
    dev->ld_regs[3][0x70] = 7;
    dev->ld_regs[3][0x74] = 4;
    dev->ld_regs[3][0xF0] = 0x3C;

    /* Logical device 4: Serial Port 1 */
    dev->ld_regs[4][0x30] = 1;
    dev->ld_regs[4][0x60] = 3;
    dev->ld_regs[4][0x61] = 0xf8;
    dev->ld_regs[4][0x70] = 4;
    dev->ld_regs[4][0xF0] = 3;
    serial_setup(dev->uart[0], 0x3f8, dev->ld_regs[4][0x70]);

    /* Logical device 5: Serial Port 2 */
    dev->ld_regs[5][0x30] = 1;
    dev->ld_regs[5][0x60] = 2;
    dev->ld_regs[5][0x61] = 0xf8;
    dev->ld_regs[5][0x70] = 3;
    dev->ld_regs[5][0x74] = 4;
    dev->ld_regs[5][0xF1] = 2;
    dev->ld_regs[5][0xF2] = 3;
    serial_setup(dev->uart[1], 0x2f8, dev->ld_regs[5][0x70]);

    /* Logical device 6: RTC */
    dev->ld_regs[5][0x30] = 1;
    dev->ld_regs[6][0x63] = 0x00;
    dev->ld_regs[6][0xF4] = 3;

    /* Logical device 7: Keyboard */
    dev->ld_regs[7][0x30] = 1;
    dev->ld_regs[7][0x61] = 0x60;
    dev->ld_regs[7][0x70] = 1;

    /* Logical device 8: Auxiliary I/O */

    /* Logical device 9: ACCESS.bus */

    fdc37c93x_gpio_handler(dev);
    fdc37c93x_lpt_handler(dev);
    fdc37c93x_serial_handler(dev, 0);
    fdc37c93x_serial_handler(dev, 1);
    fdc37c93x_auxio_handler(dev);
    if (dev->chip_id == 0x03)
	fdc37c932fr_access_bus_handler(dev);

    fdc_reset(dev->fdc);
    fdc37c93x_fdc_handler(dev);

    dev->locked = 0;
}


static void
access_bus_close(void *priv)
{
    access_bus_t *dev = (access_bus_t *) priv;

    free(dev);
}


static void *
access_bus_init(const device_t *info)
{
    access_bus_t *dev = (access_bus_t *) malloc(sizeof(access_bus_t));
    memset(dev, 0, sizeof(access_bus_t));

    return dev;
}


static const device_t access_bus_device = {
    "SMC FDC37C932FR ACCESS.bus",
    0,
    0x03,
    access_bus_init, access_bus_close, NULL,
    NULL, NULL, NULL,
    NULL
};


static void
fdc37c93x_close(void *priv)
{
    fdc37c93x_t *dev = (fdc37c93x_t *) priv;

    free(dev);
}


static void *
fdc37c93x_init(const device_t *info)
{
    fdc37c93x_t *dev = (fdc37c93x_t *) malloc(sizeof(fdc37c93x_t));
    memset(dev, 0, sizeof(fdc37c93x_t));

    dev->fdc = device_add(&fdc_at_smc_device);

    dev->uart[0] = device_add_inst(&ns16550_device, 1);
    dev->uart[1] = device_add_inst(&ns16550_device, 2);

    dev->chip_id = info->local;

    dev->gpio_regs[0] = 0xff;
    dev->gpio_regs[1] = 0xfd;

    if (dev->chip_id == 0x30) {
	dev->nvr = device_add(&at_nvr_device);

	nvr_bank_set(0, 0, dev->nvr);
	nvr_bank_set(1, 0xff, dev->nvr);
    }

    if (dev->chip_id == 0x03)
	dev->access_bus = device_add(&access_bus_device);

    io_sethandler(0x370, 0x0002,
		  fdc37c93x_read, NULL, NULL, fdc37c93x_write, NULL, NULL, dev);
    io_sethandler(0x3f0, 0x0002,
		  fdc37c93x_read, NULL, NULL, fdc37c93x_write, NULL, NULL, dev);

    fdc37c93x_reset(dev);

    return dev;
}


const device_t fdc37c932fr_device = {
    "SMC FDC37C932FR Super I/O",
    0,
    0x03,
    fdc37c93x_init, fdc37c93x_close, NULL,
    NULL, NULL, NULL,
    NULL
};

const device_t fdc37c932qf_device = {
    "SMC FDC37C932QF Super I/O",
    0,
    0x30,	/* Share the same ID with the 935. */
    fdc37c93x_init, fdc37c93x_close, NULL,
    NULL, NULL, NULL,
    NULL
};

const device_t fdc37c935_device = {
    "SMC FDC37C935 Super I/O",
    0,
    0x02,
    fdc37c93x_init, fdc37c93x_close, NULL,
    NULL, NULL, NULL,
    NULL
};
