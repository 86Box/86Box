/*
 * 86Box	A hypervisor and IBM PC system emulator that specializes in
 *		running old operating systems and software designed for IBM
 *		PC systems and compatibles from 1981 through fairly recent
 *		system designs based on the PCI bus.
 *
 *		This file is part of the 86Box distribution.
 *
 *		Implementation of the SMC FDC37C669 Super I/O Chip.
 *
 * Version:	@(#)sio_fdc37c669.c	1.0.5	2017/10/16
 *
 * Author:	Miran Grca, <mgrca8@gmail.com>
 *		Copyright 2016,2017 Miran Grca.
 */
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <wchar.h>
#include "86box.h"
#include "ibm.h"
#include "io.h"
#include "device.h"
#include "lpt.h"
#include "serial.h"
#include "disk/hdc.h"
#include "disk/hdc_ide.h"
#include "floppy/floppy.h"
#include "floppy/fdc.h"
#include "floppy/fdd.h"
#include "sio.h"


static int fdc37c669_locked;
static int fdc37c669_rw_locked = 0;
static int fdc37c669_curreg = 0;
static uint8_t fdc37c669_regs[42];
static uint8_t tries;

static uint16_t make_port(uint8_t reg)
{
	uint16_t p = 0;

	uint16_t mask = 0;

	switch(reg)
	{
		case 0x20:
		case 0x21:
		case 0x22:
			mask = 0xfc;
			break;
		case 0x23:
			mask = 0xff;
			break;
		case 0x24:
		case 0x25:
			mask = 0xfe;
			break;
	}

	p = ((uint16_t) (fdc37c669_regs[reg] & mask)) << 2;
	if (reg == 0x22)
	{
		p |= 6;
	}

	return p;
}

void fdc37c669_write(uint16_t port, uint8_t val, void *priv)
{
	uint8_t index = (port & 1) ? 0 : 1;
	uint8_t valxor = 0;
	uint8_t max = 42;
        /* pclog("fdc37c669_write : port=%04x reg %02X = %02X locked=%i\n", port, fdc37c669_curreg, val, fdc37c669_locked); */

	if (index)
	{
		if ((val == 0x55) && !fdc37c669_locked)
		{
			if (tries)
			{
				fdc37c669_locked = 1;
				tries = 0;
			}
			else
			{
				tries++;
			}
		}
		else
		{
			if (fdc37c669_locked)
			{
				if (val < max)  fdc37c669_curreg = val;
				if (val == 0xaa)  fdc37c669_locked = 0;
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
		if (fdc37c669_locked)
		{
			if ((fdc37c669_curreg < 0x18) && (fdc37c669_rw_locked))  return;
			if ((fdc37c669_curreg >= 0x26) && (fdc37c669_curreg <= 0x27))  return;
			if (fdc37c669_curreg == 0x29)  return;
			valxor = val ^ fdc37c669_regs[fdc37c669_curreg];
			fdc37c669_regs[fdc37c669_curreg] = val;
			goto process_value;
		}
	}
	return;

process_value:
	switch(fdc37c669_curreg)
	{
		case 0:
#if 0
			if (valxor & 3)
			{
				ide_pri_disable();
				if ((fdc37c669_regs[0] & 3) == 2)  ide_pri_enable_ex();
				break;
			}
#endif
			if (valxor & 8)
			{
				fdc_remove();
				if ((fdc37c669_regs[0] & 8) && (fdc37c669_regs[0x20] & 0xc0))  fdc_set_base(make_port(0x20), 1);
			}
			break;
		case 1:
			if (valxor & 4)
			{
				/* pclog("Removing LPT1\n"); */
				lpt1_remove();
				if ((fdc37c669_regs[1] & 4) && (fdc37c669_regs[0x23] >= 0x40)) 
				{
					/* pclog("LPT1 init (%02X)\n", make_port(0x23)); */
					lpt1_init(make_port(0x23));
				}
			}
			if (valxor & 7)
			{
				fdc37c669_rw_locked = (val & 8) ? 0 : 1;
			}
			break;
		case 2:
			if (valxor & 8)
			{
				/* pclog("Removing UART1\n"); */
				serial_remove(1);
				if ((fdc37c669_regs[2] & 8) && (fdc37c669_regs[0x24] >= 0x40))
				{
					/* pclog("UART1 init (%02X, %i)\n", make_port(0x24), (fdc37c669_regs[0x28] & 0xF0) >> 4); */
					serial_setup(1, make_port(0x24), (fdc37c669_regs[0x28] & 0xF0) >> 4);
				}
			}
			if (valxor & 0x80)
			{
				/* pclog("Removing UART2\n"); */
				serial_remove(2);
				if ((fdc37c669_regs[2] & 0x80) && (fdc37c669_regs[0x25] >= 0x40))
				{
					/* pclog("UART2 init (%02X, %i)\n", make_port(0x25), fdc37c669_regs[0x28] & 0x0F); */
					serial_setup(2, make_port(0x25), fdc37c669_regs[0x28] & 0x0F);
				}
			}
			break;
		case 3:
			if (valxor & 2)  fdc_update_enh_mode((val & 2) ? 1 : 0);
			break;
		case 5:
			if (valxor & 0x18)  fdc_update_densel_force((val & 0x18) >> 3);
			if (valxor & 0x20)  fdd_swap = ((val & 0x20) >> 5);
			break;
		case 0xB:
			if (valxor & 3)  fdc_update_rwc(0, val & 3);
			if (valxor & 0xC)  fdc_update_rwc(1, (val & 0xC) >> 2);
			break;
		case 0x20:
			if (valxor & 0xfc)
			{
				fdc_remove();
				if ((fdc37c669_regs[0] & 8) && (fdc37c669_regs[0x20] & 0xc0))  fdc_set_base(make_port(0x20), 1);
			}
			break;
		case 0x21:
		case 0x22:
#if 0
			if (valxor & 0xfc)
			{
				ide_pri_disable();
				switch (fdc37c669_curreg)
				{
					case 0x21:
						ide_set_base(0, make_port(0x21));
						break;
					case 0x22:
						ide_set_side(0, make_port(0x22));
						break;
				}
				if ((fdc37c669_regs[0] & 3) == 2)  ide_pri_enable_ex();
			}
#endif
			break;
		case 0x23:
			if (valxor)
			{
				/* pclog("Removing LPT1\n"); */
				lpt1_remove();
				if ((fdc37c669_regs[1] & 4) && (fdc37c669_regs[0x23] >= 0x40)) 
				{
					/* pclog("LPT1 init (%02X)\n", make_port(0x23)); */
					lpt1_init(make_port(0x23));
				}
			}
			break;
		case 0x24:
			if (valxor & 0xfe)
			{
				/* pclog("Removing UART1\n"); */
				serial_remove(1);
				if ((fdc37c669_regs[2] & 8) && (fdc37c669_regs[0x24] >= 0x40))
				{
					/* pclog("UART1 init (%02X, %i)\n", make_port(0x24), (fdc37c669_regs[0x28] & 0xF0) >> 4); */
					serial_setup(1, make_port(0x24), (fdc37c669_regs[0x28] & 0xF0) >> 4);
				}
			}
			break;
		case 0x25:
			if (valxor & 0xfe)
			{
				/* pclog("Removing UART2\n"); */
				serial_remove(2);
				if ((fdc37c669_regs[2] & 0x80) && (fdc37c669_regs[0x25] >= 0x40))
				{
					/* pclog("UART2 init (%02X, %i)\n", make_port(0x25), fdc37c669_regs[0x28] & 0x0F); */
					serial_setup(2, make_port(0x25), fdc37c669_regs[0x28] & 0x0F);
				}
			}
			break;
		case 0x28:
			if (valxor & 0xf)
			{
				/* pclog("Removing UART2\n"); */
				serial_remove(2);
				if ((fdc37c669_regs[2] & 0x80) && (fdc37c669_regs[0x25] >= 0x40))
				{
					/* pclog("UART2 init (%02X, %i)\n", make_port(0x25), fdc37c669_regs[0x28] & 0x0F); */
					serial_setup(2, make_port(0x25), fdc37c669_regs[0x28] & 0x0F);
				}
			}
			if (valxor & 0xf0)
			{
				/* pclog("Removing UART1\n"); */
				serial_remove(1);
				if ((fdc37c669_regs[2] & 8) && (fdc37c669_regs[0x24] >= 0x40))
				{
					/* pclog("UART1 init (%02X, %i)\n", make_port(0x24), (fdc37c669_regs[0x28] & 0xF0) >> 4); */
					serial_setup(1, make_port(0x24), (fdc37c669_regs[0x28] & 0xF0) >> 4);
				}
			}
			break;
	}
}

uint8_t fdc37c669_read(uint16_t port, void *priv)
{
	uint8_t index = (port & 1) ? 0 : 1;

        /* pclog("fdc37c669_read : port=%04x reg %02X locked=%i\n", port, fdc37c669_curreg, fdc37c669_locked); */

	if (!fdc37c669_locked)
	{
		return 0xFF;
	}

	if (index)
		return fdc37c669_curreg;
	else
	{
		/* pclog("0x03F1: %02X\n", fdc37c669_regs[fdc37c669_curreg]); */
		if ((fdc37c669_curreg < 0x18) && (fdc37c669_rw_locked))  return 0xff;
		return fdc37c669_regs[fdc37c669_curreg];
	}
}

void fdc37c669_reset(void)
{
	fdc_remove();
	fdc_add_for_superio();

        fdc_update_is_nsc(0);

	serial_remove(1);
	serial_setup(1, SERIAL1_ADDR, SERIAL1_IRQ);

	serial_remove(2);
	serial_setup(2, SERIAL2_ADDR, SERIAL2_IRQ);

	lpt2_remove();

	lpt1_remove();
	lpt1_init(0x378);
        
	memset(fdc37c669_regs, 0, 42);
	fdc37c669_regs[0] = 0x28;
	fdc37c669_regs[1] = 0x9C;
	fdc37c669_regs[2] = 0x88;
	fdc37c669_regs[3] = 0x78;
	fdc37c669_regs[4] = 0;
	fdc37c669_regs[5] = 0;
	fdc37c669_regs[6] = 0xFF;
	fdc37c669_regs[7] = 0;
	fdc37c669_regs[8] = 0;
	fdc37c669_regs[9] = 0;
	fdc37c669_regs[0xA] = 0;
	fdc37c669_regs[0xB] = 0;
	fdc37c669_regs[0xC] = 0;
	fdc37c669_regs[0xD] = 3;
	fdc37c669_regs[0xE] = 2;
	fdc37c669_regs[0x1E] = 0x80;	/* Gameport controller. */
	fdc37c669_regs[0x20] = (0x3f0 >> 2) & 0xfc;
	fdc37c669_regs[0x21] = (0x1f0 >> 2) & 0xfc;
	fdc37c669_regs[0x22] = ((0x3f6 >> 2) & 0xfc) | 1;
	fdc37c669_regs[0x23] = (0x378 >> 2);
	fdc37c669_regs[0x24] = (0x3f8 >> 2) & 0xfe;
	fdc37c669_regs[0x25] = (0x2f8 >> 2) & 0xfe;
	fdc37c669_regs[0x26] = (2 << 4) | 3;
	fdc37c669_regs[0x27] = (6 << 4) | 7;
	fdc37c669_regs[0x28] = (4 << 4) | 3;

	fdc_update_densel_polarity(1);
	fdc_update_densel_force(0);
	fdd_swap = 0;
        fdc37c669_locked = 0;
        fdc37c669_rw_locked = 0;
}

void fdc37c669_init()
{
        io_sethandler(0x3f0, 0x0002, fdc37c669_read, NULL, NULL, fdc37c669_write, NULL, NULL,  NULL);

	fdc37c669_reset();

	pci_reset_handler.super_io_reset = fdc37c669_reset;
}
