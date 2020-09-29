/*
 * 86Box	A hypervisor and IBM PC system emulator that specializes in
 *		running old operating systems and software designed for IBM
 *		PC systems and compatibles from 1981 through fairly recent
 *		system designs based on the PCI bus.
 *
 *		This file is part of the 86Box distribution.
 *
 *		Implementation of the ACC 2168 chipset
 *
 *
 *
 * Authors:	Sarah Walker, <http://pcem-emulator.co.uk/>
 *
 *		Copyright 2019 Sarah Walker.
 */
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>
#include <86box/86box.h>
#include "cpu.h"
#include <86box/timer.h>
#include <86box/device.h>
#include <86box/io.h>
#include <86box/mem.h>
#include <86box/port_92.h>
#include <86box/chipset.h>

#define enabled_shadow (MEM_READ_INTERNAL | ((dev->regs[0x02] & 0x20) ? MEM_WRITE_DISABLED : MEM_WRITE_INTERNAL))
#define disabled_shadow (MEM_READ_EXTANY | MEM_WRITE_EXTANY)

typedef struct acc2168_t
{
    int reg_idx;
    uint8_t regs[256];
} acc2168_t;

static void 
acc2168_shadow_recalc(acc2168_t *dev)
{
mem_set_mem_state_both(0xc0000, 0x8000, ((dev->regs[0x01] & 0x01) ? enabled_shadow : disabled_shadow));
mem_set_mem_state_both(0xc8000, 0x8000, ((dev->regs[0x01] & 0x02) ? enabled_shadow : disabled_shadow));
mem_set_mem_state_both(0xd0000, 0x10000, ((dev->regs[0x01] & 0x04) ? enabled_shadow : disabled_shadow));
mem_set_mem_state_both(0xe0000, 0x10000, ((dev->regs[0x01] & 0x08) ? enabled_shadow : disabled_shadow));
mem_set_mem_state_both(0xf0000, 0x10000, ((dev->regs[0x01] & 0x10) ? enabled_shadow : disabled_shadow));
}

static void 
acc2168_write(uint16_t addr, uint8_t val, void *p)
{
    acc2168_t *dev = (acc2168_t *)p;

    if (!(addr & 1))
	dev->reg_idx = val;
    else {
	dev->regs[dev->reg_idx] = val;

	switch (dev->reg_idx) {
		case 0x02:
			acc2168_shadow_recalc(dev);
			break;
	}
    }
}


static uint8_t 
acc2168_read(uint16_t addr, void *p)
{
    acc2168_t *dev = (acc2168_t *)p;

   if (!(addr & 1))
	return dev->reg_idx;

    return dev->regs[dev->reg_idx];
}

static void
acc2168_close(void *priv)
{
    acc2168_t *dev = (acc2168_t *) priv;

    free(dev);
}


static void *
acc2168_init(const device_t *info)
{
    acc2168_t *dev = (acc2168_t *)malloc(sizeof(acc2168_t));
    memset(dev, 0, sizeof(acc2168_t));
	
    io_sethandler(0x00f2, 0x0002,
		  acc2168_read, NULL, NULL, acc2168_write, NULL, NULL, dev);	

    device_add(&port_92_inv_device);

    return dev;
}


const device_t acc2168_device = {
    "ACC 2168",
    0,
    0,
    acc2168_init, acc2168_close, NULL,
    NULL, NULL, NULL,
    NULL
};
