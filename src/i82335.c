/* Intel 82335 SX emulation, used by the Phoenix 386 clone. */

#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <wchar.h>
#include "ibm.h"
#include "io.h"
#include "mem.h"

typedef struct
{
	uint8_t reg_22;
	uint8_t reg_23;
} i82335_t;

i82335_t i82335;

uint8_t i82335_read(uint16_t addr, void *priv);

void i82335_write(uint16_t addr, uint8_t val, void *priv)
{
	int i = 0;

	int mem_write = 0;

	// pclog("i82335_write(%04X, %02X)\n", addr, val);

	switch (addr)
	{
		case 0x22:
			if ((val ^ i82335.reg_22) & 1)
			{
				if (val & 1)
				{
					for (i = 0; i < 8; i++)
					{
						mem_set_mem_state(0xe0000, 0x20000, MEM_READ_INTERNAL | MEM_WRITE_INTERNAL);
						shadowbios = 1;
					}
				}
				else
				{
					for (i = 0; i < 8; i++)
					{
						mem_set_mem_state(0xe0000, 0x20000, MEM_READ_EXTERNAL | MEM_WRITE_EXTERNAL);
						shadowbios = 0;
					}
				}

			        flushmmucache();
			}

			i82335.reg_22 = val | 0xd8;
			break;
		case 0x23:
			i82335.reg_23 = val;

			if ((val ^ i82335.reg_22) & 2)
			{
				if (val & 2)
				{
					for (i = 0; i < 8; i++)
					{
						mem_set_mem_state(0xc0000, 0x20000, MEM_READ_INTERNAL | MEM_WRITE_INTERNAL);
						shadowbios = 1;
					}
				}
				else
				{
					for (i = 0; i < 8; i++)
					{
						mem_set_mem_state(0xc0000, 0x20000, MEM_READ_EXTERNAL | MEM_WRITE_EXTERNAL);
						shadowbios = 0;
					}
				}
			}

			if ((val ^ i82335.reg_22) & 0xc)
			{
				if (val & 2)
				{
					for (i = 0; i < 8; i++)
					{
						mem_write = (val & 8) ? MEM_WRITE_DISABLED : MEM_WRITE_INTERNAL;
						mem_set_mem_state(0xa0000, 0x20000, MEM_READ_INTERNAL | mem_write);
						shadowbios = 1;
					}
				}
				else
				{
					for (i = 0; i < 8; i++)
					{
						mem_write = (val & 8) ? MEM_WRITE_DISABLED : MEM_WRITE_EXTERNAL;
						mem_set_mem_state(0xa0000, 0x20000, MEM_READ_EXTERNAL | mem_write);
						shadowbios = 0;
					}
				}
			}

			if ((val ^ i82335.reg_22) & 0xe)
			{
			        flushmmucache();
			}

			if (val & 0x80)
			{
			        io_removehandler(0x0022, 0x0001, i82335_read, NULL, NULL, i82335_write, NULL, NULL, NULL);
			}
			break;
	}
}

uint8_t i82335_read(uint16_t addr, void *priv)
{
	// pclog("i82335_read(%04X)\n", addr);
	if (addr == 0x22)
	{
		return i82335.reg_22;
	}
	else if (addr == 0x23)
	{
		return i82335.reg_23;
	}
	else
	{
		return 0;
	}
}

void i82335_init()
{
	memset(&i82335, 0, sizeof(i82335_t));

	i82335.reg_22 = 0xd8;

        io_sethandler(0x0022, 0x0014, i82335_read, NULL, NULL, i82335_write, NULL, NULL, NULL);
}
