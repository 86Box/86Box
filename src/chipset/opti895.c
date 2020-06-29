/*
 * 86Box	A hypervisor and IBM PC system emulator that specializes in
 *		running old operating systems and software designed for IBM
 *		PC systems and compatibles from 1981 through fairly recent
 *		system designs based on the PCI bus.
 *
 *		This file is part of the 86Box distribution.
 *
 *		Implementation of the OPTi 82C802G/82C895 chipset.
 *
 *
 *		Note: The shadowing of the chipset is enough to get the current machine
 *			  to work. Getting anything other to work will require excessive amount
 *			  of rewrites and improvements. Also, considering the similarities with the
 *			  82C495XLC & 82C802G it can be merged with opti495.c and also get 82C802G
 *			  implemented.
 *
 *		Copyright 2020 Tiseno100.
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
#include <86box/port_92.h>
#include <86box/chipset.h>


typedef struct
{
    uint8_t	idx,
		regs[256],
		scratch[2];
} opti895_t;

static void
opti895_recalc(opti895_t *dev)
{
    uint32_t base;
    uint32_t i, shflags = 0;

    shadowbios = 0;
    shadowbios_write = 0;

    for (i = 0; i < 8; i++) {
	if(dev->regs[0x22] & (i << 8) && (i==7)){
		shadowbios = 1;
		shadowbios_write = 1;
		mem_set_mem_state(0xf0000, 0x10000, MEM_READ_EXTANY | MEM_WRITE_INTERNAL);
	}
	else if(!(dev->regs[0x22] & (i << 8)) && (i==7)) {
		shadowbios = 0;
		shadowbios_write = 0;
		mem_set_mem_state(0xf0000, 0x10000, MEM_READ_INTERNAL | MEM_WRITE_INTERNAL);
	}

	/*
	We'll ignore it for now
	base = 0xc0000 + (i << 14);
	if (dev->regs[0x26] & (1 << i) && (i<=3)) {
		shflags = (dev->regs[0x26] & 0x20) ? (MEM_READ_INTERNAL | MEM_WRITE_DISABLED) : (MEM_READ_INTERNAL | MEM_WRITE_INTERNAL);
		mem_set_mem_state(base, 0x4000, shflags);
	} else mem_set_mem_state(base, 0x4000, MEM_READ_EXTANY | MEM_WRITE_EXTANY);
	*/

	base = 0xd0000 + (i << 14);
	if (dev->regs[0x23] & (1 << i)) {
		if(base < 0xe0000)
		shflags = (dev->regs[0x22] & 0x10) ? (MEM_READ_INTERNAL | MEM_WRITE_DISABLED) : (MEM_READ_INTERNAL | MEM_WRITE_INTERNAL);
		else
		shflags = (dev->regs[0x22] & 0x08) ? (MEM_READ_INTERNAL | MEM_WRITE_DISABLED) : (MEM_READ_INTERNAL | MEM_WRITE_INTERNAL);
		mem_set_mem_state(base, 0x4000, shflags);
	} else mem_set_mem_state(base, 0x4000, MEM_READ_EXTANY | MEM_WRITE_EXTANY);
    }

    flushmmucache();
}

static void
opti895_write(uint16_t addr, uint8_t val, void *priv)
{
    opti895_t *dev = (opti895_t *) priv;

    switch (addr) {
	case 0x22:
		dev->idx = val;
		break;
	case 0x24:
			dev->regs[dev->idx] = val;
			pclog("dev->regs[%04x] = %08x\n", dev->idx, val);
			switch(dev->idx){
				case 0x21:
				if(dev->regs[0x21] & 0x10){
				cpu_cache_ext_enabled = 1;
				cpu_update_waitstates();
				}
				break;

				case 0x22:
				case 0x23:
				case 0x26:
				opti895_recalc(dev);
				break;

			}

		break;

	case 0xe1:
	case 0xe2:
		dev->scratch[addr] = val;
		break;
    }
}


static uint8_t
opti895_read(uint16_t addr, void *priv)
{
    uint8_t ret = 0xff;
    opti895_t *dev = (opti895_t *) priv;

    switch (addr) {
	case 0x24:
		ret = dev->regs[dev->idx];
		break;
	case 0xe1:
	case 0xe2:
		ret = dev->scratch[addr];
		break;
    }

    return ret;
}


static void
opti895_close(void *priv)
{
    opti895_t *dev = (opti895_t *) priv;

    free(dev);
}


static void *
opti895_init(const device_t *info)
{
    opti895_t *dev = (opti895_t *) malloc(sizeof(opti895_t));
    memset(dev, 0, sizeof(opti895_t));

	device_add(&port_92_device);
	
    io_sethandler(0x0022, 0x0001, opti895_read, NULL, NULL, opti895_write, NULL, NULL, dev);
    io_sethandler(0x0024, 0x0001, opti895_read, NULL, NULL, opti895_write, NULL, NULL, dev);

    dev->scratch[0] = dev->scratch[1] = 0xff;

    io_sethandler(0x00e1, 0x0002, opti895_read, NULL, NULL, opti895_write, NULL, NULL, dev);

    return dev;
}


const device_t opti895_device = {
    "OPTi 82C895",
    0,
    0,
    opti895_init, opti895_close, NULL,
    NULL, NULL, NULL,
    NULL
};
