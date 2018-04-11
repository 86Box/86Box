/*
 * 86Box	A hypervisor and IBM PC system emulator that specializes in
 *		running old operating systems and software designed for IBM
 *		PC systems and compatibles from 1981 through fairly recent
 *		system designs based on the PCI bus.
 *
 *		Emulation of the SiS 85c471 chip.
 *
 *		SiS sis85c471 Super I/O Chip
 *		Used by DTK PKM-0038S E-2
 *
 * Version:	@(#)m_at_sis85c471.c	1.0.10	2018/03/18
 *
 * Author:	Miran Grca, <mgrca8@gmail.com>
 *
 *		Copyright 2015-2018 Miran Grca.
 */
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <wchar.h>
#include "../86box.h"
#include "../io.h"
#include "../memregs.h"
#include "../device.h"
#include "../lpt.h"
#include "../serial.h"
#include "../disk/hdc.h"
#include "../disk/hdc_ide.h"
#include "../floppy/fdd.h"
#include "../floppy/fdc.h"
#include "machine.h"


static int sis_85c471_curreg;
static uint8_t sis_85c471_regs[39];


static void sis_85c471_write(uint16_t port, uint8_t val, void *priv)
{
	uint8_t index = (port & 1) ? 0 : 1;
	uint8_t x;

	if (index)
	{
		if ((val >= 0x50) && (val <= 0x76))  sis_85c471_curreg = val;
		return;
	}
	else
	{
		if ((sis_85c471_curreg < 0x50) || (sis_85c471_curreg > 0x76))  return;
		x = val ^ sis_85c471_regs[sis_85c471_curreg - 0x50];
		/* Writes to 0x52 are blocked as otherwise, large hard disks don't read correctly. */
		if (sis_85c471_curreg != 0x52)  sis_85c471_regs[sis_85c471_curreg - 0x50] = val;
		goto process_value;
	}
	return;

process_value:
	switch(sis_85c471_curreg)
	{
		case 0x73:
#if 0
			if (x & 0x40)
			{
				if (val & 0x40)
					ide_pri_enable();
				else
					ide_pri_disable();
			}
#endif

			if (x & 0x20)
			{
				if (val & 0x20)
				{
					serial_setup(1, SERIAL1_ADDR, SERIAL1_IRQ);
					serial_setup(2, SERIAL2_ADDR, SERIAL2_IRQ);
				}
				else
				{
					serial_remove(1);
					serial_remove(2);
				}
			}

			if (x & 0x10)
			{
				if (val & 0x10)
					lpt1_init(0x378);
				else
					lpt1_remove();
			}

			break;
	}
	sis_85c471_curreg = 0;
}


static uint8_t sis_85c471_read(uint16_t port, void *priv)
{
	uint8_t index = (port & 1) ? 0 : 1;
	uint8_t temp;

	if (index)
		return sis_85c471_curreg;
	else
		if ((sis_85c471_curreg >= 0x50) && (sis_85c471_curreg <= 0x76))
		{
			temp = sis_85c471_regs[sis_85c471_curreg - 0x50];
			sis_85c471_curreg = 0;
			return temp;
		}
		else
			return 0xFF;
}


static void sis_85c471_init(void)
{
	int i = 0;

	lpt2_remove();

	sis_85c471_curreg = 0;
	for (i = 0; i < 0x27; i++)
	{
		sis_85c471_regs[i] = 0;
	}
	sis_85c471_regs[9] = 0x40;
	switch (mem_size)
	{
		case 0:
		case 1:
			sis_85c471_regs[9] |= 0;
			break;
		case 2:
		case 3:
			sis_85c471_regs[9] |= 1;
			break;
		case 4:
			sis_85c471_regs[9] |= 2;
			break;
		case 5:
			sis_85c471_regs[9] |= 0x20;
			break;
		case 6:
		case 7:
			sis_85c471_regs[9] |= 9;
			break;
		case 8:
		case 9:
			sis_85c471_regs[9] |= 4;
			break;
		case 10:
		case 11:
			sis_85c471_regs[9] |= 5;
			break;
		case 12:
		case 13:
		case 14:
		case 15:
			sis_85c471_regs[9] |= 0xB;
			break;
		case 16:
			sis_85c471_regs[9] |= 0x13;
			break;
		case 17:
			sis_85c471_regs[9] |= 0x21;
			break;
		case 18:
		case 19:
			sis_85c471_regs[9] |= 6;
			break;
		case 20:
		case 21:
		case 22:
		case 23:
			sis_85c471_regs[9] |= 0xD;
			break;
		case 24:
		case 25:
		case 26:
		case 27:
		case 28:
		case 29:
		case 30:
		case 31:
			sis_85c471_regs[9] |= 0xE;
			break;
		case 32:
		case 33:
		case 34:
		case 35:
			sis_85c471_regs[9] |= 0x1B;
			break;
		case 36:
		case 37:
		case 38:
		case 39:
			sis_85c471_regs[9] |= 0xF;
			break;
		case 40:
		case 41:
		case 42:
		case 43:
		case 44:
		case 45:
		case 46:
		case 47:
			sis_85c471_regs[9] |= 0x17;
			break;
		case 48:
			sis_85c471_regs[9] |= 0x1E;
			break;
		default:
			if (mem_size < 64)
			{
				sis_85c471_regs[9] |= 0x1E;
			}
			else if ((mem_size >= 65) && (mem_size < 68))
			{
				sis_85c471_regs[9] |= 0x22;
			}
			else
			{
				sis_85c471_regs[9] |= 0x24;
			}
			break;
	}

	sis_85c471_regs[0x11] = 9;
	sis_85c471_regs[0x12] = 0xFF;
	sis_85c471_regs[0x23] = 0xF0;
	sis_85c471_regs[0x26] = 1;

        io_sethandler(0x0022, 0x0002, sis_85c471_read, NULL, NULL, sis_85c471_write, NULL, NULL,  NULL);
}


void
machine_at_dtk486_init(const machine_t *model)
{
        machine_at_ide_init(model);
	device_add(&fdc_at_device);

	memregs_init();
	sis_85c471_init();
	secondary_ide_check();
}
