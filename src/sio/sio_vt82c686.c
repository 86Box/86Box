/*
 * 86Box	A hypervisor and IBM PC system emulator that specializes in
 *		running old operating systems and software designed for IBM
 *		PC systems and compatibles from 1981 through fairly recent
 *		system designs based on the PCI bus.
 *
 *		This file is part of the 86Box distribution.
 *
 *		Emulation of the VIA VT82C686A/B integrated Super I/O.
 *
 *
 *
 * Author:	RichardG, <richardg867@gmail.com>
 *		Copyright 2020 RichardG.
 */
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>
#include <86box/86box.h>
#include <86box/device.h>
#include <86box/io.h>
#include <86box/timer.h>
#include <86box/pci.h>
#include <86box/mem.h>
#include <86box/rom.h>
#include <86box/lpt.h>
#include <86box/serial.h>
#include <86box/fdd.h>
#include <86box/fdc.h>
#include <86box/sio.h>


typedef struct {
    uint8_t	cur_reg, regs[32], fdc_dma, fdc_irq, uart_irq[2], lpt_dma, lpt_irq;
    fdc_t	*fdc;
    serial_t	*uart[2];
} vt82c686_t;


static uint8_t
get_lpt_length(vt82c686_t *dev)
{
    uint8_t length = 4;

    if ((dev->regs[0x02] & 0x03) == 0x2)
	length = 8;

    return length;
}


static void
vt82c686_fdc_handler(vt82c686_t *dev)
{
    uint16_t io_base = (dev->regs[0x03] & 0xfc) << 2;

    fdc_remove(dev->fdc);

    if (dev->regs[0x02] & 0x10)
	fdc_set_base(dev->fdc, io_base);

    fdc_set_dma_ch(dev->fdc, dev->fdc_dma);
    fdc_set_irq(dev->fdc, dev->fdc_irq);
}


static void
vt82c686_lpt_handler(vt82c686_t *dev)
{
    uint16_t io_mask, io_base = dev->regs[0x06] << 2;
    int io_len = get_lpt_length(dev);
    io_base &= (0xff8 | io_len);
    io_mask = 0x3fc;
    if (io_len == 8)
	io_mask = 0x3f8;

    lpt1_remove();

    if (((dev->regs[0x02] & 0x03) != 0x03) && (io_base >= 0x100) && (io_base <= io_mask))
	lpt1_init(io_base);

    lpt1_irq(dev->lpt_irq);
}


static void
vt82c686_serial_handler(vt82c686_t *dev, int uart)
{
    serial_remove(dev->uart[uart]);

    if (dev->regs[0x02] & (uart ? 0x08 : 0x04))
	serial_setup(dev->uart[uart], (dev->regs[0x07 + uart] & 0xfe) << 2, dev->uart_irq[uart]);
}


static void
vt82c686_write(uint16_t port, uint8_t val, void *priv)
{
    vt82c686_t *dev = (vt82c686_t *) priv;

    if (!(port & 1)) {
	dev->cur_reg = val;
	return;
    }

    /* NOTE: Registers are [0xE0:0xFF] but we store them as [0x00:0x1F]. */
    if (dev->cur_reg < 0xe0)
    	return;
    uint8_t reg = dev->cur_reg & 0x1f;

    /* Read-only registers */
    if ((reg < 0x02) || (reg == 0x04) || (reg == 0x05) || ((reg >= 0x09) && (reg < 0x0e)) ||
	(reg == 0x13) || (reg == 0x15) || (reg == 0x17) || (reg >= 0x19))
	return;

    dev->regs[reg] = val;

    switch (reg) {
	case 0x02:
		vt82c686_lpt_handler(dev);
		vt82c686_serial_handler(dev, 0);
		vt82c686_serial_handler(dev, 1);
		vt82c686_fdc_handler(dev);
		break;

	case 0x03:
		vt82c686_fdc_handler(dev);
		break;

	case 0x06:
		vt82c686_lpt_handler(dev);
		break;

	case 0x07:
		vt82c686_serial_handler(dev, 0);
		break;

	case 0x08:
		vt82c686_serial_handler(dev, 1);
		break;
    }
}


static uint8_t
vt82c686_read(uint16_t port, void *priv)
{
    vt82c686_t *dev = (vt82c686_t *) priv;
    uint8_t ret = 0xff;
    
    /* NOTE: Registers are [0xE0:0xFF] but we store them as [0x00:0x1F]. */
    if (!(port & 1))
	ret = dev->cur_reg;
    else if (dev->cur_reg < 0xe0)
	ret = 0xff;
    else
	ret = dev->regs[dev->cur_reg & 0x1f];

    return ret;
}


/* Writes to Super I/O-related configuration space registers
   of the VT82C686 PCI-ISA bridge are sent here by via_pipc.c */
void
vt82c686_sio_write(uint8_t addr, uint8_t val, void *priv)
{
    vt82c686_t *dev = (vt82c686_t *) priv;

    switch (addr) {
	case 0x50:
		dev->fdc_dma = val & 0x03;
		vt82c686_fdc_handler(dev);
		dev->lpt_dma = (val >> 2) & 0x03;
		vt82c686_lpt_handler(dev);
		break;

	case 0x51:
		dev->fdc_irq = val & 0x0f;
		vt82c686_fdc_handler(dev);
		dev->lpt_irq = val >> 4;
		vt82c686_lpt_handler(dev);
		break;

	case 0x52:
		dev->uart_irq[0] = val & 0x0f;
		vt82c686_serial_handler(dev, 0);
		dev->uart_irq[1] = val >> 4;
		vt82c686_serial_handler(dev, 1);
		break;

	case 0x85:
		io_removehandler(0x3f0, 2, vt82c686_read, NULL, NULL, vt82c686_write, NULL, NULL, dev);
		if (val & 0x02)
			io_sethandler(0x3f0, 2, vt82c686_read, NULL, NULL, vt82c686_write, NULL, NULL, dev);
		break;
    }
}


static void
vt82c686_reset(vt82c686_t *dev)
{
    memset(dev->regs, 0, 20);
    
    dev->regs[0x00] = 0x3c;
    dev->regs[0x02] = 0x03;
    dev->regs[0x03] = 0xfc;
    dev->regs[0x06] = 0xde;
    dev->regs[0x07] = 0xfe;
    dev->regs[0x08] = 0xbe;

    fdc_reset(dev->fdc);

    serial_setup(dev->uart[0], SERIAL1_ADDR, SERIAL1_IRQ);
    serial_setup(dev->uart[1], SERIAL2_ADDR, SERIAL2_IRQ);

    vt82c686_lpt_handler(dev);
    vt82c686_serial_handler(dev, 0);
    vt82c686_serial_handler(dev, 1);
    vt82c686_fdc_handler(dev);

    vt82c686_sio_write(0x85, 0x00, dev);
}


static void
vt82c686_close(void *priv)
{
    vt82c686_t *dev = (vt82c686_t *) priv;

    free(dev);
}


static void *
vt82c686_init(const device_t *info)
{
    vt82c686_t *dev = (vt82c686_t *) malloc(sizeof(vt82c686_t));
    memset(dev, 0, sizeof(vt82c686_t));

    dev->fdc = device_add(&fdc_at_smc_device);
    dev->fdc_dma = 2;

    dev->uart[0] = device_add_inst(&ns16550_device, 1);
    dev->uart[1] = device_add_inst(&ns16550_device, 2);

    dev->lpt_dma = 3;

    vt82c686_reset(dev);

    return dev;
}


const device_t via_vt82c686_sio_device = {
    "VIA VT82C686 Integrated Super I/O",
    0,
    0,
    vt82c686_init, vt82c686_close, NULL,
    { NULL }, NULL, NULL,
    NULL
};
