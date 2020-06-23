/*
 * 86Box	A hypervisor and IBM PC system emulator that specializes in
 *		running old operating systems and software designed for IBM
 *		PC systems and compatibles from 1981 through fairly recent
 *		system designs based on the PCI bus.
 *
 *		This file is part of the 86Box distribution.
 *
 *		Implementation of the Intel 82335(KU82335) chipset.
 *
 *
 *
 * Authors:	Sarah Walker, <tommowalker@tommowalker.co.uk>
 *		Miran Grca, <mgrca8@gmail.com>
 *
 *		Copyright 2008-2020 Sarah Walker.
 *		Copyright 2016-2020 Miran Grca.
 *		Copyright 2020 Tiseno100
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
#include "cpu.h"
#include <86box/timer.h>
#include <86box/io.h>
#include <86box/device.h>
#include <86box/keyboard.h>
#include <86box/mem.h>
#include <86box/fdd.h>
#include <86box/fdc.h>
#include <86box/chipset.h>


typedef struct
{
	uint8_t reg_22;
	uint8_t reg_23;
} i82335_t;

static uint8_t i82335_read(uint16_t addr, void *priv);

static void
i82335_write(uint16_t addr, uint8_t val, void *priv)
{
    i82335_t *dev = (i82335_t *) priv;

	int mem_write = 0;

	switch (addr)
	{
		case 0x22:
			if ((val ^ dev->reg_22) & 1)
			{
				if (val & 1)
				{
					for (int i = 0; i < 8; i++)
					{
						mem_set_mem_state(0xe0000, 0x20000, MEM_READ_INTERNAL | MEM_WRITE_INTERNAL);
						shadowbios = 1;
					}
				}
				else
				{
					for (int i = 0; i < 8; i++)
					{
						mem_set_mem_state(0xe0000, 0x20000, MEM_READ_EXTANY | MEM_WRITE_EXTANY);
						shadowbios = 0;
					}
				}

			        flushmmucache();
			}

			dev->reg_22 = val | 0xd8;
			break;

		case 0x23:
			dev->reg_23 = val;

			if ((val ^ dev->reg_22) & 2)
			{
				if (val & 2)
				{
					for (int i = 0; i < 8; i++)
					{
						mem_set_mem_state(0xc0000, 0x20000, MEM_READ_INTERNAL | MEM_WRITE_INTERNAL);
						shadowbios = 1;
					}
				}
				else
				{
					for (int i = 0; i < 8; i++)
					{
						mem_set_mem_state(0xc0000, 0x20000, MEM_READ_EXTANY | MEM_WRITE_EXTANY);
						shadowbios = 0;
					}
				}
			}

			if ((val ^ dev->reg_22) & 0xc)
			{
				if (val & 2)
				{
					for (int i = 0; i < 8; i++)
					{
						mem_write = (val & 8) ? MEM_WRITE_DISABLED : MEM_WRITE_INTERNAL;
						mem_set_mem_state(0xa0000, 0x20000, MEM_READ_INTERNAL | mem_write);
						shadowbios = 1;
					}
				}
				else
				{
					for (int i = 0; i < 8; i++)
					{
						mem_write = (val & 8) ? MEM_WRITE_DISABLED : MEM_WRITE_EXTANY;
						mem_set_mem_state(0xa0000, 0x20000, MEM_READ_EXTANY | mem_write);
						shadowbios = 0;
					}
				}
			}

			if ((val ^ dev->reg_22) & 0xe)
			{
			        flushmmucache();
			}

			if (val & 0x80)
			{
			        io_removehandler(0x0022, 0x0001, i82335_read, NULL, NULL, i82335_write, NULL, NULL, NULL);
			        io_removehandler(0x0023, 0x0001, i82335_read, NULL, NULL, i82335_write, NULL, NULL, NULL);
			}
			break;
	}
}


static uint8_t
i82335_read(uint16_t addr, void *priv)
{
    uint8_t ret = 0xff;
    i82335_t *dev = (i82335_t *) priv;

	switch(addr){
		case 0x22:
			return dev->reg_22;
			break;
		case 0x23:
			return dev->reg_23;
			break;
		default:
			return 0;
			break;
	}

    return ret;
}


static void
i82335_close(void *priv)
{
    i82335_t *dev = (i82335_t *) priv;

    free(dev);
}


static void *
i82335_init(const device_t *info)
{
    i82335_t *dev = (i82335_t *) malloc(sizeof(i82335_t));
    memset(dev, 0, sizeof(i82335_t));

    dev->reg_22 = 0xd8;

    io_sethandler(0x0022, 0x0001, i82335_read, NULL, NULL, i82335_write, NULL, NULL, dev);
    io_sethandler(0x0023, 0x0001, i82335_read, NULL, NULL, i82335_write, NULL, NULL, dev);

    return dev;
}


const device_t i82335_device = {
    "Intel 82335",
    0,
    0,
    i82335_init, i82335_close, NULL,
    NULL, NULL, NULL,
    NULL
};
