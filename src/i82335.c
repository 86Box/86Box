/* Intel 82335 SX emulation, used by the Phoenix 386 clone. */

#include <stdint.h>
#include "ibm.h"

typedef struct
{
	uint8_t reg_22;
} i82335_t;

i82335_t i82335;

void i82335_write(uint16_t addr, uint8_t val, void *priv)
{
	int i = 0;

	switch (addr)
	{
		case 0x22:
			i82335_t.reg_22 = val | 0xd8;
			if (val & 1)
			{
				for (i = 0; i < 8; i++)
				{
					mem_mapping_enable(&bios_mapping[i]);
				}
			}
			else
			{
				for (i = 0; i < 8; i++)
				{
					mem_mapping_disable(&bios_mapping[i]);
				}
			}
			break;
		case 0x23:
			if (val & 0x80)
			{
			        io_removehandler(0x0022, 0x0001, i82335_read, NULL, NULL, i82335_write, NULL, NULL, NULL);
			}
			break;
	}
}

uint8_t i82335_read(uint16_t addr, void *priv)
{
}

void i82335_init()
{
	memset(i82335_t, 0, sizeof(i82335_t));

	i82335_t.reg_22 = 0xd9;

        io_sethandler(0x0022, 0x0014, i82335_read, NULL, NULL, i82335_write, NULL, NULL, NULL);
}
