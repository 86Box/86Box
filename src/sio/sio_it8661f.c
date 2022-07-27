/*
 * 86Box	A hypervisor and IBM PC system emulator that specializes in
 *		running old operating systems and software designed for IBM
 *		PC systems and compatibles from 1981 through fairly recent
 *		system designs based on the PCI bus.
 *
 *		This file is part of the 86Box distribution.
 *
 *		Implementation of the ITE IT8661F chipset.
 *
 *		Note: This Super I/O is partially incomplete and intended only for having the intended machine to function
 *
 *		Authors: Tiseno100
 *
 *		Copyright 2021 Tiseno100
 *
 */
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
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
#include <86box/fdd.h>
#include <86box/fdc.h>
#include <86box/fdd_common.h>
#include <86box/sio.h>


#define LDN dev->regs[7]


typedef struct
{
    fdc_t *fdc_controller;
    serial_t *uart[2];

    uint8_t index, regs[256], device_regs[6][256];
    int unlocked, enumerator;
} it8661f_t;


static uint8_t		mb_pnp_key[32] = {0x6a, 0xb5, 0xda, 0xed, 0xf6, 0xfb, 0x7d, 0xbe, 0xdf, 0x6f, 0x37, 0x1b, 0x0d, 0x86, 0xc3, 0x61, 0xb0, 0x58, 0x2c, 0x16, 0x8b, 0x45, 0xa2, 0xd1, 0xe8, 0x74, 0x3a, 0x9d, 0xce, 0xe7, 0x73, 0x39};


static void	it8661f_reset(void *priv);


#ifdef ENABLE_IT8661_LOG
int it8661_do_log = ENABLE_IT8661_LOG;


void
it8661_log(const char *fmt, ...)
{
    va_list ap;

    if (it8661_do_log) {
	va_start(ap, fmt);
	pclog_ex(fmt, ap);
	va_end(ap);
    }
}
#else
#define it8661_log(fmt, ...)
#endif


static void
it8661_fdc(uint16_t addr, uint8_t val, it8661f_t *dev)
{
    fdc_remove(dev->fdc_controller);

    if (((addr == 0x30) && (val & 1)) || (dev->device_regs[0][0x30] & 1)) {
	switch (addr) {
		case 0x30:
			dev->device_regs[0][addr] = val & 1;
			break;

		case 0x31:
			dev->device_regs[0][addr] = val & 3;
			if (val & 1)
				dev->device_regs[0][addr] |= 0x55;
			break;

		case 0x60:
		case 0x61:
			dev->device_regs[0][addr] = val & ((addr == 0x61) ? 0xff : 0xf8);
			break;

		case 0x70:
			dev->device_regs[0][addr] = val & 0x0f;
			break;

		case 0x74:
			dev->device_regs[0][addr] = val & 7;
			break;

		case 0xf0:
			dev->device_regs[0][addr] = val & 0x0f;
			break;
	}

	fdc_set_base(dev->fdc_controller, (dev->device_regs[0][0x60] << 8) | (dev->device_regs[0][0x61]));
	fdc_set_irq(dev->fdc_controller, dev->device_regs[0][0x70] & 0x0f);
	fdc_set_dma_ch(dev->fdc_controller, dev->device_regs[0][0x74] & 7);

	if (dev->device_regs[0][0xf0] & 1)
		fdc_writeprotect(dev->fdc_controller);

        it8661_log("ITE 8661-FDC: BASE %04x IRQ %02x\n", (dev->device_regs[0][0x60] << 8) | (dev->device_regs[0][0x61]),
		   dev->device_regs[0][0x70] & 0x0f);
    }
}


static void
it8661_serial(int uart, uint16_t addr, uint8_t val, it8661f_t *dev)
{
    serial_remove(dev->uart[uart]);

    if (((addr == 0x30) && (val & 1)) || (dev->device_regs[1 + uart][0x30] & 1)) {
	switch (addr) {
		case 0x30:
			dev->device_regs[1 + uart][addr] = val & 1;
			break;

		case 0x60:
		case 0x61:
			dev->device_regs[1 + uart][addr] = val & ((addr == 0x61) ? 0xff : 0xf8);
			break;

		case 0x70:
			dev->device_regs[1 + uart][addr] = val & 0x0f;
			break;

		case 0x74:
			dev->device_regs[1 + uart][addr] = val & 7;
			break;

		case 0xf0:
			dev->device_regs[1 + uart][addr] = val & 3;
			break;
	}

	serial_setup(dev->uart[uart], (dev->device_regs[1 + uart][0x60] << 8) | (dev->device_regs[1 + uart][0x61]), dev->device_regs[1 + uart][0x70] & 0x0f);

	it8661_log("ITE 8661-UART%01x: BASE %04x IRQ %02x\n", 1 + (LDN % 1),
		   (dev->device_regs[1 + uart][0x60] << 8) | (dev->device_regs[1 + uart][0x61]),
		   dev->device_regs[1 + uart][0x70] & 0x0f);
    }
}


void
it8661_lpt(uint16_t addr, uint8_t val, it8661f_t *dev)
{
    lpt1_remove();

    if (((addr == 0x30) && (val & 1)) || (dev->device_regs[3][0x30] & 1)) {
	switch (addr) {
		case 0x30:
			dev->device_regs[3][addr] = val & 1;
			break;

		case 0x60:
		case 0x61:
			dev->device_regs[3][addr] = val & ((addr == 0x61) ? 0xff : 0xf8);
			break;

		case 0x70:
			dev->device_regs[3][addr] = val & 0x0f;
			break;

		case 0x74:
			dev->device_regs[3][addr] = val & 7;
			break;

		case 0xf0:
			dev->device_regs[3][addr] = val & 3;
			break;
	}

	lpt1_init((dev->device_regs[3][0x60] << 8) | (dev->device_regs[3][0x61]));
	lpt1_irq(dev->device_regs[3][0x70] & 0x0f);

	it8661_log("ITE 8661-LPT: BASE %04x IRQ %02x\n", (dev->device_regs[3][0x60] << 8) | (dev->device_regs[3][0x61]),
		   dev->device_regs[3][0x70] & 0x0f);
    }
}


void
it8661_ldn(uint16_t addr, uint8_t val, it8661f_t *dev)
{
    switch (LDN) {
	case 0:
		it8661_fdc(addr, val, dev);
		break;
	case 1:
	case 2:
		it8661_serial(LDN & 2, addr, val, dev);
		break;
	case 3:
		it8661_lpt(addr, val, dev);
		break;
    }
}


static void
it8661f_write(uint16_t addr, uint8_t val, void *priv)
{
    it8661f_t *dev = (it8661f_t *)priv;

    switch (addr) {
	case FDC_SECONDARY_ADDR:
		if (!dev->unlocked) {
			(val == mb_pnp_key[dev->enumerator]) ? dev->enumerator++ : (dev->enumerator = 0);
			if (dev->enumerator == 31) {
				dev->unlocked = 1;
				it8661_log("ITE8661F: Unlocked!\n");
			}
		} else
			dev->index = val;
		break;

	case 0x371:
		if (dev->unlocked) {
			switch (dev->index) {
				case 0x02:
					dev->regs[dev->index] = val;
					if (val & 1)
						it8661f_reset(dev);
					if (val & 2)
						dev->unlocked = 0;
					break;
				case 0x07:
					dev->regs[dev->index] = val;
					break;
				case 0x22:
					dev->regs[dev->index] = val & 0x30;
					break;
				case 0x23:
					dev->regs[dev->index] = val & 0x1f;
					break;
				default:
					it8661_ldn(dev->index, val, dev);
					break;
		}
	}
	break;
    }

    return;
}


static uint8_t
it8661f_read(uint16_t addr, void *priv)
{
    it8661f_t *dev = (it8661f_t *)priv;

    it8661_log("IT8661F:\n", addr, dev->regs[dev->index]);
    return (addr == 0xa79) ? dev->regs[dev->index] : 0xff;
}


static void
it8661f_reset(void *priv)
{
    it8661f_t *dev = (it8661f_t *)priv;
    dev->regs[0x20] = 0x86;
    dev->regs[0x21] = 0x61;

    dev->device_regs[0][0x60] = 3;
    dev->device_regs[0][0x61] = 0xf0;
    dev->device_regs[0][0x70] = 6;
    dev->device_regs[0][0x71] = 2;
    dev->device_regs[0][0x74] = 2;

    dev->device_regs[1][0x60] = 3;
    dev->device_regs[1][0x61] = 0xf8;
    dev->device_regs[1][0x70] = 4;
    dev->device_regs[1][0x71] = 2;

    dev->device_regs[2][0x60] = 2;
    dev->device_regs[2][0x61] = 0xf8;
    dev->device_regs[2][0x70] = 3;
    dev->device_regs[2][0x71] = 2;

    dev->device_regs[3][0x60] = 3;
    dev->device_regs[3][0x61] = 0x78;
    dev->device_regs[3][0x70] = 7;
    dev->device_regs[3][0x71] = 2;
    dev->device_regs[3][0x74] = 3;
    dev->device_regs[3][0xf0] = 3;
}


static void
it8661f_close(void *priv)
{
    it8661f_t *dev = (it8661f_t *)priv;

    free(dev);
}


static void *
it8661f_init(const device_t *info)
{
    it8661f_t *dev = (it8661f_t *)malloc(sizeof(it8661f_t));
    memset(dev, 0, sizeof(it8661f_t));

    dev->fdc_controller = device_add(&fdc_at_smc_device);
    fdc_reset(dev->fdc_controller);

    dev->uart[0] = device_add_inst(&ns16550_device, 1);
    dev->uart[1] = device_add_inst(&ns16550_device, 2);

    io_sethandler(FDC_SECONDARY_ADDR, 0x0002, it8661f_read, NULL, NULL, it8661f_write, NULL, NULL, dev);

    dev->enumerator = 0;
    dev->unlocked = 0;

    it8661f_reset(dev);
    return dev;
}

const device_t it8661f_device = {
    .name = "ITE IT8661F",
    .internal_name = "it8661f",
    .flags = 0,
    .local = 0,
    .init = it8661f_init,
    .close = it8661f_close,
    .reset = NULL,
    { .available = NULL },
    .speed_changed = NULL,
    .force_redraw = NULL,
    .config = NULL
};
