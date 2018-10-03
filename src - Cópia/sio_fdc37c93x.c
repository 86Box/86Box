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
 * Version:	@(#)sio_fdc37c93x.c	1.0.13	2018/04/29
 *
 * Author:	Miran Grca, <mgrca8@gmail.com>
 *		Copyright 2016-2018 Miran Grca.
 */
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <wchar.h>
#include "86box.h"
#include "io.h"
#include "device.h"
#include "pci.h"
#include "lpt.h"
#include "serial.h"
#include "disk/hdc.h"
#include "disk/hdc_ide.h"
#include "floppy/fdd.h"
#include "floppy/fdc.h"
#include "sio.h"


static int fdc37c93x_locked;
static int fdc37c93x_curreg = 0;
static int fdc37c93x_gpio_reg = 0;
static uint8_t fdc37c93x_regs[48];
static uint8_t fdc37c93x_ld_regs[10][256];
static uint16_t fdc37c93x_gpio_base = 0x00EA;
static fdc_t *fdc37c93x_fdc;

static uint8_t tries;

static uint16_t make_port(uint8_t ld)
{
	uint16_t r0 = fdc37c93x_ld_regs[ld][0x60];
	uint16_t r1 = fdc37c93x_ld_regs[ld][0x61];

	uint16_t p = (r0 << 8) + r1;

	return p;
}

static uint8_t fdc37c93x_gpio_read(uint16_t port, void *priv)
{
	return fdc37c93x_gpio_reg;
}

static void fdc37c93x_gpio_write(uint16_t port, uint8_t val, void *priv)
{
	fdc37c93x_gpio_reg = val;
}

static void fdc37c93x_fdc_handler(void)
{
	uint16_t ld_port = 0;

	uint8_t global_enable = !!(fdc37c93x_regs[0x22] & (1 << 0));
	uint8_t local_enable = !!fdc37c93x_ld_regs[0][0x30];

	fdc_remove(fdc37c93x_fdc);
	if (global_enable && local_enable)
	{
		ld_port = make_port(0);
		if ((ld_port >= 0x0100) && (ld_port <= 0x0FF8)) {
			fdc_set_base(fdc37c93x_fdc, ld_port);
		}
	}
}

static void fdc37c93x_lpt_handler(void)
{
	uint16_t ld_port = 0;

	uint8_t global_enable = !!(fdc37c93x_regs[0x22] & (1 << 3));
	uint8_t local_enable = !!fdc37c93x_ld_regs[3][0x30];

	lpt1_remove();
	if (global_enable && local_enable)
	{
		ld_port = make_port(3);
		if ((ld_port >= 0x0100) && (ld_port <= 0x0FFC))
			lpt1_init(ld_port);
	}
}

static void fdc37c93x_serial_handler(int uart)
{
	uint16_t ld_port = 0;

	uint8_t uart_no = 3 + uart;

	uint8_t global_enable = !!(fdc37c93x_regs[0x22] & (1 << uart_no));
	uint8_t local_enable = !!fdc37c93x_ld_regs[uart_no][0x30];

	serial_remove(uart);
	if (global_enable && local_enable)
	{
		ld_port = make_port(uart_no);
		if ((ld_port >= 0x0100) && (ld_port <= 0x0FF8))
			serial_setup(uart, ld_port, fdc37c93x_ld_regs[uart_no][0x70]);
	}
}

static void fdc37c93x_auxio_handler(void)
{
	uint16_t ld_port = 0;

	uint8_t local_enable = !!fdc37c93x_ld_regs[8][0x30];

        io_removehandler(fdc37c93x_gpio_base, 0x0001, fdc37c93x_gpio_read, NULL, NULL, fdc37c93x_gpio_write, NULL, NULL,  NULL);
	if (local_enable)
	{
		fdc37c93x_gpio_base = ld_port = make_port(8);
		if ((ld_port >= 0x0100) && (ld_port <= 0x0FFF))
		        io_sethandler(fdc37c93x_gpio_base, 0x0001, fdc37c93x_gpio_read, NULL, NULL, fdc37c93x_gpio_write, NULL, NULL,  NULL);
	}
}

#define AB_RST	0x80

typedef struct {
	uint8_t control;
	uint8_t status;
	uint8_t own_addr;
	uint8_t data;
	uint8_t clock;
	uint16_t base;
} access_bus_t;

static access_bus_t access_bus;

static uint8_t fdc37c932fr_access_bus_read(uint16_t port, void *priv)
{
	switch(port & 3) {
		case 0:
			return (access_bus.status & 0xBF);
			break;
		case 1:
			return (access_bus.own_addr & 0x7F);
			break;
		case 2:
			return access_bus.data;
			break;
		case 3:
			return (access_bus.clock & 0x87);
			break;
		default:
			return 0xFF;
	}
}

static void fdc37c932fr_access_bus_write(uint16_t port, uint8_t val, void *priv)
{
	switch(port & 3) {
		case 0:
			access_bus.control = (val & 0xCF);
			break;
		case 1:
			access_bus.own_addr = (val & 0x7F);
			break;
		case 2:
			access_bus.data = val;
			break;
		case 3:
			access_bus.clock &= 0x80;
			access_bus.clock |= (val & 0x07);
			break;
	}
}


static void fdc37c932fr_access_bus_handler(void)
{
	uint16_t ld_port = 0;

	uint8_t global_enable = !!(fdc37c93x_regs[0x22] & (1 << 6));
	uint8_t local_enable = !!fdc37c93x_ld_regs[9][0x30];

        io_removehandler(access_bus.base, 0x0004, fdc37c932fr_access_bus_read, NULL, NULL, fdc37c932fr_access_bus_write, NULL, NULL,  NULL);
	if (global_enable && local_enable)
	{
		access_bus.base = ld_port = make_port(9);
		if ((ld_port >= 0x0100) && (ld_port <= 0x0FFC))
		        io_sethandler(access_bus.base, 0x0004, fdc37c932fr_access_bus_read, NULL, NULL, fdc37c932fr_access_bus_write, NULL, NULL,  NULL);
	}
}

static void fdc37c93x_write(uint16_t port, uint8_t val, void *priv)
{
	uint8_t index = (port & 1) ? 0 : 1;
	uint8_t valxor = 0;

	if (index)
	{
		if ((val == 0x55) && !fdc37c93x_locked)
		{
			if (tries)
			{
				fdc37c93x_locked = 1;
				fdc_3f1_enable(fdc37c93x_fdc, 0);
				tries = 0;
			}
			else
			{
				tries++;
			}
		}
		else
		{
			if (fdc37c93x_locked)
			{
				if (val == 0xaa)
				{
					fdc37c93x_locked = 0;
					fdc_3f1_enable(fdc37c93x_fdc, 1);
					return;
				}
				fdc37c93x_curreg = val;
			}
			else
			{
				if (tries)
					tries = 0;
			}
		}
	}
	else
	{
		if (fdc37c93x_locked)
		{
			if (fdc37c93x_curreg < 48)
			{
				valxor = val ^ fdc37c93x_regs[fdc37c93x_curreg];
				if ((val == 0x20) || (val == 0x21))
					return;
				fdc37c93x_regs[fdc37c93x_curreg] = val;
				goto process_value;
			}
			else
			{
				valxor = val ^ fdc37c93x_ld_regs[fdc37c93x_regs[7]][fdc37c93x_curreg];
				if (((fdc37c93x_curreg & 0xF0) == 0x70) && (fdc37c93x_regs[7] < 4))  return;
				/* Block writes to IDE configuration. */
				if (fdc37c93x_regs[7] == 1)  return;
				if (fdc37c93x_regs[7] == 2)  return;
				if ((fdc37c93x_regs[7] > 5) && (fdc37c93x_regs[7] != 8) && (fdc37c93x_regs[7] != 9))  return;
				if ((fdc37c93x_regs[7] == 9) && (fdc37c93x_regs[0x20] != 3))  return;
				fdc37c93x_ld_regs[fdc37c93x_regs[7]][fdc37c93x_curreg] = val;
				goto process_value;
			}
		}
	}
	return;

process_value:
	if (fdc37c93x_curreg < 48)
	{
		switch(fdc37c93x_curreg)
		{
			case 0x22:
				if (valxor & 0x01)
					fdc37c93x_fdc_handler();
				if (valxor & 0x08)
					fdc37c93x_lpt_handler();
				if (valxor & 0x10)
					fdc37c93x_serial_handler(1);
				if (valxor & 0x20)
					fdc37c93x_serial_handler(2);
				break;
		}

		return;
	}

	switch(fdc37c93x_regs[7])
	{
		case 0:
			/* FDD */
			switch(fdc37c93x_curreg)
			{
				case 0x30:
				case 0x60:
				case 0x61:
					if (valxor)
					{
						fdc37c93x_fdc_handler();
					}
					break;
				case 0xF0:
					if (valxor & 0x01)  fdc_update_enh_mode(fdc37c93x_fdc, val & 0x01);
					if (valxor & 0x10)  fdc_set_swap(fdc37c93x_fdc, (val & 0x10) >> 4);
					break;
				case 0xF1:
					if (valxor & 0xC)  fdc_update_densel_force(fdc37c93x_fdc, (val & 0xC) >> 2);
					break;
				case 0xF2:
					if (valxor & 0xC0)  fdc_update_rwc(fdc37c93x_fdc, 3, (valxor & 0xC0) >> 6);
					if (valxor & 0x30)  fdc_update_rwc(fdc37c93x_fdc, 2, (valxor & 0x30) >> 4);
					if (valxor & 0x0C)  fdc_update_rwc(fdc37c93x_fdc, 1, (valxor & 0x0C) >> 2);
					if (valxor & 0x03)  fdc_update_rwc(fdc37c93x_fdc, 0, (valxor & 0x03));
					break;
				case 0xF4:
					if (valxor & 0x18)  fdc_update_drvrate(fdc37c93x_fdc, 0, (val & 0x18) >> 3);
					break;
				case 0xF5:
					if (valxor & 0x18)  fdc_update_drvrate(fdc37c93x_fdc, 1, (val & 0x18) >> 3);
					break;
				case 0xF6:
					if (valxor & 0x18)  fdc_update_drvrate(fdc37c93x_fdc, 2, (val & 0x18) >> 3);
					break;
				case 0xF7:
					if (valxor & 0x18)  fdc_update_drvrate(fdc37c93x_fdc, 3, (val & 0x18) >> 3);
					break;
			}
			break;
		case 3:
			/* Parallel port */
			switch(fdc37c93x_curreg)
			{
				case 0x30:
				case 0x60:
				case 0x61:
					if (valxor)
					{
						fdc37c93x_lpt_handler();
					}
					break;
			}
			break;
		case 4:
			/* Serial port 1 */
			switch(fdc37c93x_curreg)
			{
				case 0x30:
				case 0x60:
				case 0x61:
				case 0x70:
					if (valxor)
					{
						fdc37c93x_serial_handler(1);
					}
					break;
			}
			break;
		case 5:
			/* Serial port 2 */
			switch(fdc37c93x_curreg)
			{
				case 0x30:
				case 0x60:
				case 0x61:
				case 0x70:
					if (valxor)
					{
						fdc37c93x_serial_handler(2);
					}
					break;
			}
			break;
		case 8:
			/* Auxiliary I/O */
			switch(fdc37c93x_curreg)
			{
				case 0x30:
				case 0x60:
				case 0x61:
				case 0x70:
					if (valxor)
					{
						fdc37c93x_auxio_handler();
					}
					break;
			}
			break;
		case 9:
			/* Access bus (FDC37C932FR only) */
			switch(fdc37c93x_curreg)
			{
				case 0x30:
				case 0x60:
				case 0x61:
				case 0x70:
					if (valxor)
					{
						fdc37c932fr_access_bus_handler();
					}
					break;
			}
			break;
	}
}

static uint8_t fdc37c93x_read(uint16_t port, void *priv)
{
	uint8_t index = (port & 1) ? 0 : 1;

	if (!fdc37c93x_locked)
	{
		return 0xff;
	}

	if (index)
		return fdc37c93x_curreg;
	else
	{
		if (fdc37c93x_curreg < 0x30)
		{
			return fdc37c93x_regs[fdc37c93x_curreg];
		}
		else
		{
			if ((fdc37c93x_regs[7] == 0) && (fdc37c93x_curreg == 0xF2))  return (fdc_get_rwc(fdc37c93x_fdc, 0) | (fdc_get_rwc(fdc37c93x_fdc, 1) << 2));
			return fdc37c93x_ld_regs[fdc37c93x_regs[7]][fdc37c93x_curreg];
		}
	}
}

static void fdc37c93x_reset(void)
{
	int i = 0;

	memset(fdc37c93x_regs, 0, 48);

	fdc37c93x_regs[0x03] = 3;
	fdc37c93x_regs[0x21] = 1;
	fdc37c93x_regs[0x22] = 0x39;
	fdc37c93x_regs[0x24] = 4;
	fdc37c93x_regs[0x26] = 0xF0;
	fdc37c93x_regs[0x27] = 3;

	for (i = 0; i < 10; i++)
	{
		memset(fdc37c93x_ld_regs[i], 0, 256);
	}

	/* Logical device 0: FDD */
	fdc37c93x_ld_regs[0][0x30] = 1;
	fdc37c93x_ld_regs[0][0x60] = 3;
	fdc37c93x_ld_regs[0][0x61] = 0xF0;
	fdc37c93x_ld_regs[0][0x70] = 6;
	fdc37c93x_ld_regs[0][0x74] = 2;
	fdc37c93x_ld_regs[0][0xF0] = 0xE;
	fdc37c93x_ld_regs[0][0xF2] = 0xFF;

	/* Logical device 1: IDE1 */
	fdc37c93x_ld_regs[1][0x30] = 0;
	fdc37c93x_ld_regs[1][0x60] = 1;
	fdc37c93x_ld_regs[1][0x61] = 0xF0;
	fdc37c93x_ld_regs[1][0x62] = 3;
	fdc37c93x_ld_regs[1][0x63] = 0xF6;
	fdc37c93x_ld_regs[1][0x70] = 0xE;
	fdc37c93x_ld_regs[1][0xF0] = 0xC;

	/* Logical device 2: IDE2 */
	fdc37c93x_ld_regs[2][0x30] = 0;
	fdc37c93x_ld_regs[2][0x60] = 1;
	fdc37c93x_ld_regs[2][0x61] = 0x70;
	fdc37c93x_ld_regs[2][0x62] = 3;
	fdc37c93x_ld_regs[2][0x63] = 0x76;
	fdc37c93x_ld_regs[2][0x70] = 0xF;

	/* Logical device 3: Parallel Port */
	fdc37c93x_ld_regs[3][0x30] = 1;
	fdc37c93x_ld_regs[3][0x60] = 3;
	fdc37c93x_ld_regs[3][0x61] = 0x78;
	fdc37c93x_ld_regs[3][0x70] = 7;
	fdc37c93x_ld_regs[3][0x74] = 4;
	fdc37c93x_ld_regs[3][0xF0] = 0x3C;

	/* Logical device 4: Serial Port 1 */
	fdc37c93x_ld_regs[4][0x30] = 1;
	fdc37c93x_ld_regs[4][0x60] = 3;
	fdc37c93x_ld_regs[4][0x61] = 0xf8;
	fdc37c93x_ld_regs[4][0x70] = 4;
	fdc37c93x_ld_regs[4][0xF0] = 3;
	serial_setup(1, 0x3f8, fdc37c93x_ld_regs[4][0x70]);

	/* Logical device 5: Serial Port 2 */
	fdc37c93x_ld_regs[5][0x30] = 1;
	fdc37c93x_ld_regs[5][0x60] = 2;
	fdc37c93x_ld_regs[5][0x61] = 0xf8;
	fdc37c93x_ld_regs[5][0x70] = 3;
	fdc37c93x_ld_regs[5][0x74] = 4;
	fdc37c93x_ld_regs[5][0xF1] = 2;
	fdc37c93x_ld_regs[5][0xF2] = 3;
	serial_setup(2, 0x2f8, fdc37c93x_ld_regs[5][0x70]);

	/* Logical device 6: RTC */
	fdc37c93x_ld_regs[6][0x63] = 0x70;
	fdc37c93x_ld_regs[6][0xF4] = 3;

	/* Logical device 7: Keyboard */
	fdc37c93x_ld_regs[7][0x30] = 1;
	fdc37c93x_ld_regs[7][0x61] = 0x60;
	fdc37c93x_ld_regs[7][0x70] = 1;

	/* Logical device 8: Auxiliary I/O */
	fdc37c93x_ld_regs[8][0x30] = 1;
	fdc37c93x_ld_regs[8][0x61] = 0xEA;

	/* Logical device 8: AUX I/O */

	/* Logical device 9: ACCESS.bus */

        io_removehandler(fdc37c93x_gpio_base, 0x0001, fdc37c93x_gpio_read, NULL, NULL, fdc37c93x_gpio_write, NULL, NULL,  NULL);
	fdc37c93x_gpio_base = 0x00EA;
        io_sethandler(fdc37c93x_gpio_base, 0x0001, fdc37c93x_gpio_read, NULL, NULL, fdc37c93x_gpio_write, NULL, NULL,  NULL);

	fdc37c93x_lpt_handler();
	fdc37c93x_serial_handler(1);
	fdc37c93x_serial_handler(2);
	fdc37c93x_auxio_handler();

	fdc_reset(fdc37c93x_fdc);

        fdc37c93x_locked = 0;
}

static void fdc37c932fr_reset(void)
{
	fdc37c93x_reset();

	fdc37c93x_regs[0x20] = 3;

	fdc37c932fr_access_bus_handler();
}

static void fdc37c935_reset(void)
{
	fdc37c93x_reset();

	fdc37c93x_regs[0x20] = 2;
}

static void fdc37c93x_init(void)
{
	lpt2_remove();

	fdc37c93x_fdc = device_add(&fdc_at_smc_device);

	fdc37c93x_gpio_reg = 0xFD;

        io_sethandler(0x3f0, 0x0002, fdc37c93x_read, NULL, NULL, fdc37c93x_write, NULL, NULL,  NULL);
}

void fdc37c932fr_init(void)
{
	fdc37c93x_init();
	fdc37c932fr_reset();
}

void fdc37c935_init(void)
{
	fdc37c93x_init();
	fdc37c935_reset();
}
