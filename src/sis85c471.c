/* Copyright holders: Tenshi
   see COPYING for more details
*/
/*
	SiS sis85c471 Super I/O Chip
	Used by Batman's Revenge
*/

#include "ibm.h"
#include "ide.h"

#include "fdc.h"
#include "fdd.h"
#include "io.h"
#include "lpt.h"
#include "mouse_serial.h"
#include "serial.h"
#include "sis85c471.h"

static int sis85c471_curreg;
static uint8_t sis85c471_regs[39];

void sis85c471_write(uint16_t port, uint8_t val, void *priv)
{
	uint8_t index = (port & 1) ? 0 : 1;
        int temp;
	uint8_t x;
        // pclog("sis85c471_write : port=%04x reg %02X = %02X\n", port, sis85c471_curreg, val);

	if (index)
	{
		if ((val >= 0x50) && (val <= 0x76))  sis85c471_curreg = val;
		return;
	}
	else
	{
		if ((sis85c471_curreg < 0x50) || (sis85c471_curreg > 0x76))  return;
		x = val ^ sis85c471_regs[sis85c471_curreg - 0x50];
		/* Writes to 0x52 are blocked as otherwise, large hard disks don't read correctly. */
		if (sis85c471_curreg != 0x52)  sis85c471_regs[sis85c471_curreg - 0x50] = val;
		goto process_value;
	}
	return;

process_value:
	switch(sis85c471_curreg)
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
					serial1_init(0x3f8, 4);
					serial2_init(0x2f8, 3);
					mouse_serial_init();
				}
				else
				{
					serial1_remove();
					serial2_remove();
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
	sis85c471_curreg = 0;
}

uint8_t sis85c471_read(uint16_t port, void *priv)
{
        // pclog("sis85c471_read : port=%04x reg %02X\n", port, sis85c471_curreg);
	uint8_t index = (port & 1) ? 0 : 1;
	uint8_t temp;

	if (index)
		return sis85c471_curreg;
	else
		if ((sis85c471_curreg >= 0x50) && (sis85c471_curreg <= 0x76))
		{
			temp = sis85c471_regs[sis85c471_curreg - 0x50];
			sis85c471_curreg = 0;
			return temp;
		}
		else
			return 0xFF;
}

void sis85c471_init()
{
	int i = 0;

	// pclog("SiS 85c471 Init\n");

	// ide_sec_disable();
	lpt2_remove();

	sis85c471_curreg = 0;
	for (i = 0; i < 0x27; i++)
	{
		sis85c471_regs[i] = 0;
	}
	sis85c471_regs[9] = 0x40;
	switch (mem_size)
	{
		case 0:
		case 1:
			sis85c471_regs[9] |= 0;
			break;
		case 2:
		case 3:
			sis85c471_regs[9] |= 1;
			break;
		case 4:
			sis85c471_regs[9] |= 2;
			break;
		case 5:
			sis85c471_regs[9] |= 0x20;
			break;
		case 6:
		case 7:
			sis85c471_regs[9] |= 9;
			break;
		case 8:
		case 9:
			sis85c471_regs[9] |= 4;
			break;
		case 10:
		case 11:
			sis85c471_regs[9] |= 5;
			break;
		case 12:
		case 13:
		case 14:
		case 15:
			sis85c471_regs[9] |= 0xB;
			break;
		case 16:
			sis85c471_regs[9] |= 0x13;
			break;
		case 17:
			sis85c471_regs[9] |= 0x21;
			break;
		case 18:
		case 19:
			sis85c471_regs[9] |= 6;
			break;
		case 20:
		case 21:
		case 22:
		case 23:
			sis85c471_regs[9] |= 0xD;
			break;
		case 24:
		case 25:
		case 26:
		case 27:
		case 28:
		case 29:
		case 30:
		case 31:
			sis85c471_regs[9] |= 0xE;
			break;
		case 32:
		case 33:
		case 34:
		case 35:
			sis85c471_regs[9] |= 0x1B;
			break;
		case 36:
		case 37:
		case 38:
		case 39:
			sis85c471_regs[9] |= 0xF;
			break;
		case 40:
		case 41:
		case 42:
		case 43:
		case 44:
		case 45:
		case 46:
		case 47:
			sis85c471_regs[9] |= 0x17;
			break;
		case 48:
			sis85c471_regs[9] |= 0x1E;
			break;
		default:
			if (mem_size < 64)
			{
				sis85c471_regs[9] |= 0x1E;
			}
			else if ((mem_size >= 65) && (mem_size < 68))
			{
				sis85c471_regs[9] |= 0x22;
			}
			else
			{
				sis85c471_regs[9] |= 0x24;
			}
			break;
	}

	sis85c471_regs[0x11] = 9;
	sis85c471_regs[0x12] = 0xFF;
	sis85c471_regs[0x23] = 0xF0;
	sis85c471_regs[0x26] = 1;

	fdc_update_densel_polarity(1);
	fdc_update_densel_force(0);
	fdd_swap = 0;
        io_sethandler(0x0022, 0x0002, sis85c471_read, NULL, NULL, sis85c471_write, NULL, NULL,  NULL);
}
