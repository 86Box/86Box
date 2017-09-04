/*
 * 86Box	A hypervisor and IBM PC system emulator that specializes in
 *		running old operating systems and software designed for IBM
 *		PC systems and compatibles from 1981 through fairly recent
 *		system designs based on the PCI bus.
 *
 *		This file is part of the 86Box distribution.
 *
 *		Emulation of the NatSemi PC87306 Super I/O chip.
 *
 * Version:	@(#)sio_pc87306.c	1.0.3	2017/09/03
 *
 * Author:	Miran Grca, <mgrca8@gmail.com>
 *		Copyright 2016,2017 Miran Grca.
 */
#include "ibm.h"
#include "io.h"
#include "lpt.h"
#include "serial.h"
#include "floppy/floppy.h"
#include "floppy/fdc.h"
#include "floppy/fdd.h"
#include "hdd/hdd_ide_at.h"
#include "sio.h"


static int pc87306_curreg;
static uint8_t pc87306_regs[29];
static uint8_t pc87306_gpio[2] = {0xFF, 0xFB};
static uint8_t tries;
static uint16_t lpt_port;

void pc87306_gpio_remove();
void pc87306_gpio_init();

void pc87306_gpio_write(uint16_t port, uint8_t val, void *priv)
{
	pc87306_gpio[port & 1] = val;
}

uint8_t uart_int1()
{
	/* 0: IRQ3, 1: IRQ4 */
	return ((pc87306_regs[0x1C] >> 2) & 1) ? 4 : 3;
}

uint8_t uart_int2()
{
	/* 0: IRQ3, 1: IRQ4 */
	return ((pc87306_regs[0x1C] >> 6) & 1) ? 4 : 3;
}

uint8_t uart1_int()
{
	uint8_t temp;
	temp = ((pc87306_regs[1] >> 2) & 1) ? 3 : 4;	/* 0 = COM1 (IRQ 4), 1 = COM2 (IRQ 3), 2 = COM3 (IRQ 4), 3 = COM4 (IRQ 3) */
	return (pc87306_regs[0x1C] & 1) ? uart_int1() : temp;
}

uint8_t uart2_int()
{
	uint8_t temp;
	temp = ((pc87306_regs[1] >> 4) & 1) ? 3 : 4;	/* 0 = COM1 (IRQ 4), 1 = COM2 (IRQ 3), 2 = COM3 (IRQ 4), 3 = COM4 (IRQ 3) */
	return (pc87306_regs[0x1C] & 1) ? uart_int2() : temp;
}

void lpt1_handler()
{
        int temp;
	temp = pc87306_regs[0x01] & 3;
	switch (temp)
	{
		case 0:
			lpt_port = 0x378;
			break;
		case 1:
			if (pc87306_regs[0x1B] & 0x40)
			{
				lpt_port = ((uint16_t) pc87306_regs[0x19]) << 2;
			}
			else
			{
				lpt_port = 0x3bc;
			}
			break;
		case 2:
			lpt_port = 0x278;
			break;
	}
	lpt1_init(lpt_port);
}

void serial1_handler()
{
        int temp;
	temp = (pc87306_regs[1] >> 2) & 3;
	switch (temp)
	{
		case 0: serial_setup(1, SERIAL1_ADDR, uart1_int()); break;
		case 1: serial_setup(1, SERIAL2_ADDR, uart1_int()); break;
		case 2:
			switch ((pc87306_regs[1] >> 6) & 3)
			{
				case 0: serial_setup(1, 0x3e8, uart1_int()); break;
				case 1: serial_setup(1, 0x338, uart1_int()); break;
				case 2: serial_setup(1, 0x2e8, uart1_int()); break;
				case 3: serial_setup(1, 0x220, uart1_int()); break;
			}
			break;
		case 3:
			switch ((pc87306_regs[1] >> 6) & 3)
			{
				case 0: serial_setup(1, 0x2e8, uart1_int()); break;
				case 1: serial_setup(1, 0x238, uart1_int()); break;
				case 2: serial_setup(1, 0x2e0, uart1_int()); break;
				case 3: serial_setup(1, 0x228, uart1_int()); break;
			}
			break;
	}
}

void serial2_handler()
{
        int temp;
	temp = (pc87306_regs[1] >> 4) & 3;
	switch (temp)
	{
		case 0: serial_setup(2, SERIAL1_ADDR, uart2_int()); break;
		case 1: serial_setup(2, SERIAL2_ADDR, uart2_int()); break;
		case 2:
			switch ((pc87306_regs[1] >> 6) & 3)
			{
				case 0: serial_setup(2, 0x3e8, uart2_int()); break;
				case 1: serial_setup(2, 0x338, uart2_int()); break;
				case 2: serial_setup(2, 0x2e8, uart2_int()); break;
				case 3: serial_setup(2, 0x220, uart2_int()); break;
			}
			break;
		case 3:
			switch ((pc87306_regs[1] >> 6) & 3)
			{
				case 0: serial_setup(2, 0x2e8, uart2_int()); break;
				case 1: serial_setup(2, 0x238, uart2_int()); break;
				case 2: serial_setup(2, 0x2e0, uart2_int()); break;
				case 3: serial_setup(2, 0x228, uart2_int()); break;
			}
			break;
	}
}

void pc87306_write(uint16_t port, uint8_t val, void *priv)
{
	uint8_t index;
	uint8_t valxor;
#if 0
	uint16_t or_value;
#endif

	index = (port & 1) ? 0 : 1;

	if (index)
	{
		pc87306_curreg = val & 0x1f;
		tries = 0;
		return;
	}
	else
	{
		if (tries)
		{
			if ((pc87306_curreg == 0) && (val == 8))
			{
				val = 0x4b;
			}
			if (pc87306_curreg <= 28)  valxor = val ^ pc87306_regs[pc87306_curreg];
			tries = 0;
			if ((pc87306_curreg == 0x19) && !(pc87306_regs[0x1B] & 0x40))
			{
				return;
			}
			if ((pc87306_curreg <= 28) && (pc87306_curreg != 8)/* && (pc87306_curreg != 0x18)*/)
			{
				if (pc87306_curreg == 0)
				{
					val &= 0x5f;
				}
				if (((pc87306_curreg == 0x0F) || (pc87306_curreg == 0x12)) && valxor)
				{
					pc87306_gpio_remove();
				}
				pc87306_regs[pc87306_curreg] = val;
				goto process_value;
			}
		}
		else
		{
			tries++;
			return;
		}
	}
	return;

process_value:
	switch(pc87306_curreg)
	{
		case 0:
			if (valxor & 1)
			{
				lpt1_remove();
				if (val & 1)
				{
					lpt1_handler();
				}
			}

			if (valxor & 2)
			{
				serial_remove(1);
				if (val & 2)
				{
					serial1_handler();
				}
			}
			if (valxor & 4)
			{
				serial_remove(2);
				if (val & 4)
				{
					serial2_handler();
				}
			}
			if (valxor & 0x28)
			{
				fdc_remove();
				if (val & 8)
				{
					fdc_set_base((val & 0x20) ? 0x370 : 0x3f0, 0);
				}
			}
			if (valxor & 0xc0)
			{
#if 0
				ide_pri_disable();
				if (val & 0x80)
				{
					or_value = 0;
				}
				else
				{
					or_value = 0x80;
				}
				ide_set_base(0, 0x170 | or_value);
				ide_set_side(0, 0x376 | or_value);
				if (val & 0x40)
				{
					ide_pri_enable_ex();
				}
#endif
			}
			
			break;
		case 1:
			if (valxor & 3)
			{
				lpt1_remove();
				if (pc87306_regs[0] & 1)
				{
					lpt1_handler();
				}
			}

			if (valxor & 0xcc)
			{
				if (pc87306_regs[0] & 2)
				{
					serial1_handler();
				}
				else
				{
					serial_remove(1);
				}
			}

			if (valxor & 0xf0)
			{
				if (pc87306_regs[0] & 4)
				{
					serial2_handler();
				}
				else
				{
					serial_remove(2);
				}
			}
			break;
		case 2:
			if (valxor & 1)
			{
				if (val & 1)
				{
					lpt1_remove();
					serial_remove(1);
					serial_remove(2);
					fdc_remove();
				}
				else
				{
					if (pc87306_regs[0] & 1)
					{
						lpt1_handler();
					}
					if (pc87306_regs[0] & 2)
					{
						serial1_handler();
					}
					if (pc87306_regs[0] & 4)
					{
						serial2_handler();
					}
					if (pc87306_regs[0] & 8)
					{
						fdc_set_base((pc87306_regs[0] & 0x20) ? 0x370 : 0x3f0, 0);
					}
				}
			}
			break;
		case 9:
			if (valxor & 0x44)
			{
				fdc_update_enh_mode((val & 4) ? 1 : 0);
				fdc_update_densel_polarity((val & 0x40) ? 1 : 0);
			}
			break;
		case 0xF:
			if (valxor)
			{
				pc87306_gpio_init();
			}
			break;
		case 0x12:
			if (valxor & 0x30)
			{
				pc87306_gpio_init();
			}
			break;
		case 0x19:
			if (valxor)
			{
				lpt1_remove();
				if (pc87306_regs[0] & 1)
				{
					lpt1_handler();
				}
			}
			break;
		case 0x1B:
			if (valxor & 0x40)
			{
				lpt1_remove();
				if (!(val & 0x40))
				{
					pc87306_regs[0x19] = 0xEF;
				}
				if (pc87306_regs[0] & 1)
				{
					lpt1_handler();
				}
			}
			break;
		case 0x1C:
			if (valxor)
			{
				if (pc87306_regs[0] & 2)
				{
					serial1_handler();
				}
				if (pc87306_regs[0] & 4)
				{
					serial2_handler();
				}
			}
			break;
	}
}

uint8_t pc87306_gpio_read(uint16_t port, void *priv)
{
	return pc87306_gpio[port & 1];
}

uint8_t pc87306_read(uint16_t port, void *priv)
{
	uint8_t index;
	index = (port & 1) ? 0 : 1;

	tries = 0;

	if (index)
	{
		return pc87306_curreg & 0x1f;
	}
	else
	{
	        if (pc87306_curreg >= 28)
		{
			return 0xff;
		}
		else if (pc87306_curreg == 8)
		{
			return 0x70;
		}
		else
		{
			return pc87306_regs[pc87306_curreg];
		}
	}
}

void pc87306_gpio_remove()
{
        io_removehandler(pc87306_regs[0xF] << 2, 0x0002, pc87306_gpio_read, NULL, NULL, pc87306_gpio_write, NULL, NULL,  NULL);
}

void pc87306_gpio_init()
{
	if ((pc87306_regs[0x12]) & 0x10)
	{
	        io_sethandler(pc87306_regs[0xF] << 2, 0x0001, pc87306_gpio_read, NULL, NULL, pc87306_gpio_write, NULL, NULL,  NULL);
	}

	if ((pc87306_regs[0x12]) & 0x20)
	{
	        io_sethandler((pc87306_regs[0xF] << 2) + 1, 0x0001, pc87306_gpio_read, NULL, NULL, pc87306_gpio_write, NULL, NULL,  NULL);
	}
}

void pc87306_reset(void)
{
	memset(pc87306_regs, 0, 29);

	/* pc87306_regs[0] = 0x4B; */
	pc87306_regs[0] = 0x0B;
	pc87306_regs[1] = 0x01;
	pc87306_regs[3] = 0x01;
	pc87306_regs[5] = 0x0D;
	pc87306_regs[8] = 0x70;
	pc87306_regs[9] = 0xC0;
	pc87306_regs[0xB] = 0x80;
	pc87306_regs[0xF] = 0x1E;
	pc87306_regs[0x12] = 0x30;
	pc87306_regs[0x19] = 0xEF;
	/*
		0 = 360 rpm @ 500 kbps for 3.5"
		1 = Default, 300 rpm @ 500,300,250,1000 kbps for 3.5"
	*/
	fdc_update_is_nsc(1);
	fdc_update_enh_mode(0);
	fdc_update_densel_polarity(1);
	fdc_update_max_track(85);
	fdc_remove();
	fdc_set_base(0x3f0, 0);
	fdd_swap = 0;
	serial_remove(1);
	serial_remove(2);
	serial1_handler();
	serial2_handler();
	pc87306_gpio_init();
}

void pc87306_init()
{
	lpt2_remove();

	pc87306_reset();

        io_sethandler(0x02e, 0x0002, pc87306_read, NULL, NULL, pc87306_write, NULL, NULL,  NULL);

	pci_reset_handler.super_io_reset = pc87306_reset;
}
