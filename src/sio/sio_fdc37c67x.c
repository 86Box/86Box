/*
 * 86Box	A hypervisor and IBM PC system emulator that specializes in
 *		running old operating systems and software designed for IBM
 *		PC systems and compatibles from 1981 through fairly recent
 *		system designs based on the PCI bus.
 *
 *		This file is part of the 86Box distribution.
 *
 *		Implementation of the SMC FDC37C67X Super I/O Chip.
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
#include <86box/pic.h>
#include <86box/pci.h>
#include <86box/lpt.h>
#include <86box/serial.h>
#include <86box/hdc.h>
#include <86box/hdc_ide.h>
#include <86box/fdd.h>
#include <86box/fdc.h>
#include "cpu.h"
#include <86box/sio.h>


#define AB_RST	0x80


typedef struct {
    uint8_t chip_id, is_apm,
	    tries,
	    gpio_regs[2], auxio_reg,
	    regs[48],
	    ld_regs[11][256];
    uint16_t gpio_base,	/* Set to EA */
	     auxio_base, sio_base;
    int locked,
	cur_reg;
    fdc_t *fdc;
    serial_t *uart[2];
} fdc37c67x_t;


static void	fdc37c67x_write(uint16_t port, uint8_t val, void *priv);
static uint8_t	fdc37c67x_read(uint16_t port, void *priv);


static uint16_t
make_port(fdc37c67x_t *dev, uint8_t ld)
{
    uint16_t r0 = dev->ld_regs[ld][0x60];
    uint16_t r1 = dev->ld_regs[ld][0x61];

    uint16_t p = (r0 << 8) + r1;

    return p;
}


static uint8_t
fdc37c67x_auxio_read(uint16_t port, void *priv)
{
    fdc37c67x_t *dev = (fdc37c67x_t *) priv;

    return dev->auxio_reg;
}


static void
fdc37c67x_auxio_write(uint16_t port, uint8_t val, void *priv)
{
    fdc37c67x_t *dev = (fdc37c67x_t *) priv;

    dev->auxio_reg = val;
}


static uint8_t
fdc37c67x_gpio_read(uint16_t port, void *priv)
{
    fdc37c67x_t *dev = (fdc37c67x_t *) priv;
    uint8_t ret = 0xff;

    ret = dev->gpio_regs[port & 1];

    return ret;
}


static void
fdc37c67x_gpio_write(uint16_t port, uint8_t val, void *priv)
{
    fdc37c67x_t *dev = (fdc37c67x_t *) priv;

    if (!(port & 1))
	dev->gpio_regs[0] = (dev->gpio_regs[0] & 0xfc) | (val & 0x03);
}


static void
fdc37c67x_fdc_handler(fdc37c67x_t *dev)
{
    uint16_t ld_port = 0;
    uint8_t global_enable = !!(dev->regs[0x22] & (1 << 0));
    uint8_t local_enable = !!dev->ld_regs[0][0x30];

    fdc_remove(dev->fdc);
    if (global_enable && local_enable) {
	ld_port = make_port(dev, 0) & 0xFFF8;
	if ((ld_port >= 0x0100) && (ld_port <= 0x0FF8))
		fdc_set_base(dev->fdc, ld_port);
    }
}


static void
fdc37c67x_lpt_handler(fdc37c67x_t *dev)
{
    uint16_t ld_port = 0;
    uint8_t global_enable = !!(dev->regs[0x22] & (1 << 3));
    uint8_t local_enable = !!dev->ld_regs[3][0x30];
    uint8_t lpt_irq = dev->ld_regs[3][0x70];

    if (lpt_irq > 15)
	lpt_irq = 0xff;

    lpt1_remove();
    if (global_enable && local_enable) {
	ld_port = make_port(dev, 3) & 0xFFFC;
	if ((ld_port >= 0x0100) && (ld_port <= 0x0FFC))
		lpt1_init(ld_port);
    }
    lpt1_irq(lpt_irq);
}


static void
fdc37c67x_serial_handler(fdc37c67x_t *dev, int uart)
{
    uint16_t ld_port = 0;
    uint8_t uart_no = 4 + uart;
    uint8_t global_enable = !!(dev->regs[0x22] & (1 << uart_no));
    uint8_t local_enable = !!dev->ld_regs[uart_no][0x30];

    serial_remove(dev->uart[uart]);
    if (global_enable && local_enable) {
	ld_port = make_port(dev, uart_no) & 0xFFF8;
	if ((ld_port >= 0x0100) && (ld_port <= 0x0FF8))
		serial_setup(dev->uart[uart], ld_port, dev->ld_regs[uart_no][0x70]);
    }
}


static void
fdc37c67x_auxio_handler(fdc37c67x_t *dev)
{
    uint16_t ld_port = 0;
    uint8_t local_enable = !!dev->ld_regs[8][0x30];

    io_removehandler(dev->auxio_base, 0x0001,
		     fdc37c67x_auxio_read, NULL, NULL, fdc37c67x_auxio_write, NULL, NULL, dev);
    if (local_enable) {
	dev->auxio_base = ld_port = make_port(dev, 8);
	if ((ld_port >= 0x0100) && (ld_port <= 0x0FFF))
	        io_sethandler(dev->auxio_base, 0x0001,
			      fdc37c67x_auxio_read, NULL, NULL, fdc37c67x_auxio_write, NULL, NULL, dev);
    }
}


static void
fdc37c67x_sio_handler(fdc37c67x_t *dev)
{
#if 0
    if (dev->sio_base) {
	io_removehandler(dev->sio_base, 0x0002,
			 fdc37c67x_read, NULL, NULL, fdc37c67x_write, NULL, NULL, dev);
    }
    dev->sio_base = (((uint16_t) dev->regs[0x27]) << 8) | dev->regs[0x26];
    if (dev->sio_base) {
	io_sethandler(dev->sio_base, 0x0002,
		      fdc37c67x_read, NULL, NULL, fdc37c67x_write, NULL, NULL, dev);
    }
#endif
}


static void
fdc37c67x_gpio_handler(fdc37c67x_t *dev)
{
    uint16_t ld_port = 0;
    uint8_t local_enable;

    local_enable = !!(dev->regs[0x03] & 0x80);

    io_removehandler(dev->gpio_base, 0x0002,
		     fdc37c67x_gpio_read, NULL, NULL, fdc37c67x_gpio_write, NULL, NULL, dev);
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
			      fdc37c67x_gpio_read, NULL, NULL, fdc37c67x_gpio_write, NULL, NULL, dev);
    }
}


static void
fdc37c67x_smi_handler(fdc37c67x_t *dev)
{
    /* TODO: 8042 P1.2 SMI#. */
    pic_reset_smi_irq_mask();
    pic_set_smi_irq_mask(dev->ld_regs[3][0x70], dev->ld_regs[8][0xb4] & 0x02);
    pic_set_smi_irq_mask(dev->ld_regs[5][0x70], dev->ld_regs[8][0xb4] & 0x04);
    pic_set_smi_irq_mask(dev->ld_regs[4][0x70], dev->ld_regs[8][0xb4] & 0x08);
    pic_set_smi_irq_mask(dev->ld_regs[0][0x70], dev->ld_regs[8][0xb4] & 0x10);
    pic_set_smi_irq_mask(12, dev->ld_regs[8][0xb5] & 0x01);
    pic_set_smi_irq_mask(1, dev->ld_regs[8][0xb5] & 0x02);
    pic_set_smi_irq_mask(10, dev->ld_regs[8][0xb5] & 0x80);
}


static void
fdc37c67x_write(uint16_t port, uint8_t val, void *priv)
{
    fdc37c67x_t *dev = (fdc37c67x_t *) priv;
    uint8_t index = (port & 1) ? 0 : 1;
    uint8_t valxor = 0x00, keep = 0x00;

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
			if (dev->regs[7] > 0x0a)
				return;
			else switch (dev->regs[7]) {
				case 0x01:
				case 0x02:
				case 0x07:
					return;
			}
			dev->ld_regs[dev->regs[7]][dev->cur_reg] = val | keep;
		}
	} else
		return;
    }

    if (dev->cur_reg < 48) {
	switch(dev->cur_reg) {
		case 0x03:
			if (valxor & 0x83)
				fdc37c67x_gpio_handler(dev);
			dev->regs[0x03] &= 0x83;
			break;
		case 0x22:
			if (valxor & 0x01)
				fdc37c67x_fdc_handler(dev);
			if (valxor & 0x08)
				fdc37c67x_lpt_handler(dev);
			if (valxor & 0x10)
				fdc37c67x_serial_handler(dev, 0);
			if (valxor & 0x20)
				fdc37c67x_serial_handler(dev, 1);
			break;
		case 0x26: case 0x27:
			fdc37c67x_sio_handler(dev);
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
					fdc37c67x_fdc_handler(dev);
				break;
			case 0xF0:
				if (valxor & 0x01)
					fdc_update_enh_mode(dev->fdc, val & 0x01);
				if (valxor & 0x10)
					fdc_set_swap(dev->fdc, (val & 0x10) >> 4);
				break;
			case 0xF1:
				if (valxor & 0xC)
					fdc_update_densel_force(dev->fdc, (val & 0xc) >> 2);
				break;
			case 0xF2:
				if (valxor & 0xC0)
					fdc_update_rwc(dev->fdc, 3, (val & 0xc0) >> 6);
				if (valxor & 0x30)
					fdc_update_rwc(dev->fdc, 2, (val & 0x30) >> 4);
				if (valxor & 0x0C)
					fdc_update_rwc(dev->fdc, 1, (val & 0x0c) >> 2);
				if (valxor & 0x03)
					fdc_update_rwc(dev->fdc, 0, (val & 0x03));
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
					fdc37c67x_lpt_handler(dev);
				if (dev->cur_reg == 0x70)
					fdc37c67x_smi_handler(dev);
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
					fdc37c67x_serial_handler(dev, 0);
				if (dev->cur_reg == 0x70)
					fdc37c67x_smi_handler(dev);
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
					fdc37c67x_serial_handler(dev, 1);
				if (dev->cur_reg == 0x70)
					fdc37c67x_smi_handler(dev);
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
					fdc37c67x_auxio_handler(dev);
				break;
			case 0xb4:
			case 0xb5:
				fdc37c67x_smi_handler(dev);
				break;
		}
		break;
    }
}


static uint8_t
fdc37c67x_read(uint16_t port, void *priv)
{
    fdc37c67x_t *dev = (fdc37c67x_t *) priv;
    uint8_t index = (port & 1) ? 0 : 1;
    uint8_t ret = 0xff;
    uint16_t smi_stat = pic_get_smi_irq_status();
    int f_irq = dev->ld_regs[0][0x70];
    int p_irq = dev->ld_regs[3][0x70];
    int s1_irq = dev->ld_regs[4][0x70];
    int s2_irq = dev->ld_regs[5][0x70];

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

			/* TODO: 8042 P1.2 SMI#. */
			if ((dev->regs[7] == 8) && (dev->cur_reg == 0xb6)) {
				ret = dev->ld_regs[dev->regs[7]][dev->cur_reg] & 0xe1;
				ret |= ((!!(smi_stat & (1 << p_irq))) << 1);
				ret |= ((!!(smi_stat & (1 << s2_irq))) << 2);
				ret |= ((!!(smi_stat & (1 << s1_irq))) << 3);
				ret |= ((!!(smi_stat & (1 << f_irq))) << 4);
			} else if ((dev->regs[7] == 8) && (dev->cur_reg == 0xb7)) {
				ret = dev->ld_regs[dev->regs[7]][dev->cur_reg] & 0xec;
				ret |= ((!!(smi_stat & (1 << 12))) << 0);
				ret |= ((!!(smi_stat & (1 << 1))) << 1);
				ret |= ((!!(smi_stat & (1 << 10))) << 4);
			}
		}
	}
    }

    return ret;
}


static void
fdc37c67x_reset(fdc37c67x_t *dev)
{
    int i = 0;

    memset(dev->regs, 0, 48);

    dev->regs[0x03] = 0x03;
    dev->regs[0x20] = dev->chip_id;
    dev->regs[0x22] = 0x39;
    dev->regs[0x24] = 0x04;
    dev->regs[0x26] = 0xf0;
    dev->regs[0x27] = 0x03;

    for (i = 0; i < 11; i++)
	memset(dev->ld_regs[i], 0, 256);

    /* Logical device 0: FDD */
    dev->ld_regs[0][0x30] = 1;
    dev->ld_regs[0][0x60] = 3;
    dev->ld_regs[0][0x61] = 0xf0;
    dev->ld_regs[0][0x70] = 6;
    dev->ld_regs[0][0x74] = 2;
    dev->ld_regs[0][0xf0] = 0x0e;
    dev->ld_regs[0][0xf2] = 0xff;

    /* Logical device 3: Parallel Port */
    dev->ld_regs[3][0x30] = 1;
    dev->ld_regs[3][0x60] = 3;
    dev->ld_regs[3][0x61] = 0x78;
    dev->ld_regs[3][0x70] = 7;
    dev->ld_regs[3][0x74] = 4;
    dev->ld_regs[3][0xf0] = 0x3c;

    /* Logical device 4: Serial Port 1 */
    dev->ld_regs[4][0x30] = 1;
    dev->ld_regs[4][0x60] = 3;
    dev->ld_regs[4][0x61] = 0xf8;
    dev->ld_regs[4][0x70] = 4;
    dev->ld_regs[4][0xf0] = 3;
    serial_setup(dev->uart[0], COM1_ADDR, dev->ld_regs[4][0x70]);

    /* Logical device 5: Serial Port 2 */
    dev->ld_regs[5][0x30] = 1;
    dev->ld_regs[5][0x60] = 2;
    dev->ld_regs[5][0x61] = 0xf8;
    dev->ld_regs[5][0x70] = 3;
    dev->ld_regs[5][0x74] = 4;
    dev->ld_regs[5][0xf1] = 2;
    dev->ld_regs[5][0xf2] = 3;
    serial_setup(dev->uart[1], COM2_ADDR, dev->ld_regs[5][0x70]);

    /* Logical device 7: Keyboard */
    dev->ld_regs[7][0x30] = 1;
    dev->ld_regs[7][0x61] = 0x60;
    dev->ld_regs[7][0x70] = 1;
    dev->ld_regs[7][0x72] = 12;

    /* Logical device 8: Auxiliary I/O */
    dev->ld_regs[8][0xc0] = 6;
    dev->ld_regs[8][0xc1] = 3;

    fdc37c67x_gpio_handler(dev);
    fdc37c67x_lpt_handler(dev);
    fdc37c67x_serial_handler(dev, 0);
    fdc37c67x_serial_handler(dev, 1);
    fdc37c67x_auxio_handler(dev);
    fdc37c67x_sio_handler(dev);

    fdc_reset(dev->fdc);
    fdc37c67x_fdc_handler(dev);

    dev->locked = 0;
}


static void
fdc37c67x_close(void *priv)
{
    fdc37c67x_t *dev = (fdc37c67x_t *) priv;

    free(dev);
}


static void *
fdc37c67x_init(const device_t *info)
{
    fdc37c67x_t *dev = (fdc37c67x_t *) malloc(sizeof(fdc37c67x_t));
    memset(dev, 0, sizeof(fdc37c67x_t));

    dev->fdc = device_add(&fdc_at_smc_device);

    dev->uart[0] = device_add_inst(&ns16550_device, 1);
    dev->uart[1] = device_add_inst(&ns16550_device, 2);

    dev->chip_id = info->local & 0xff;

    dev->gpio_regs[0] = 0xff;
    // dev->gpio_regs[1] = (info->local == 0x0030) ? 0xff : 0xfd;
    dev->gpio_regs[1] = (dev->chip_id == 0x30) ? 0xff : 0xfd;

    fdc37c67x_reset(dev);

    io_sethandler(FDC_SECONDARY_ADDR, 0x0002,
		  fdc37c67x_read, NULL, NULL, fdc37c67x_write, NULL, NULL, dev);
    io_sethandler(FDC_PRIMARY_ADDR, 0x0002,
		  fdc37c67x_read, NULL, NULL, fdc37c67x_write, NULL, NULL, dev);

    return dev;
}


const device_t fdc37c67x_device = {
    .name = "SMC FDC37C67X Super I/O",
    .internal_name = "fdc37c67x",
    .flags = 0,
    .local = 0x40,
    .init = fdc37c67x_init,
    .close = fdc37c67x_close,
    .reset = NULL,
    { .available = NULL },
    .speed_changed = NULL,
    .force_redraw = NULL,
    .config = NULL
};
