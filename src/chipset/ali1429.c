/*
 * 86Box	A hypervisor and IBM PC system emulator that specializes in
 *		running old operating systems and software designed for IBM
 *		PC systems and compatibles from 1981 through fairly recent
 *		system designs based on the PCI bus.
 *
 *		This file is part of the 86Box distribution.
 *
 *		Implementation of the ALi M-1429/1431 chipset.
 *
 * Version:	@(#)ali1429.c	1.0.9	2019/10/09
 *
 * Authors:	Sarah Walker, <tommowalker@tommowalker.co.uk>
 *		Miran Grca, <mgrca8@gmail.com>
 *
 *		Copyright 2008-2019 Sarah Walker.
 *		Copyright 2016-2019 Miran Grca.
 */
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>
#include "86box.h"
#include "cpu.h"
#include "timer.h"
#include "86box_io.h"
#include "mem.h"
#include "device.h"
#include "keyboard.h"
#include "fdd.h"
#include "fdc.h"
#include "hdc.h"
#include "hdc_ide.h"
#include "timer.h"
#include "port_92.h"
#include "chipset.h"


typedef struct
{
    uint8_t cur_reg,
	    regs[256];
} ali1429_t;


static void
ali1429_recalc(ali1429_t *dev)
{
    uint32_t base;
    uint32_t i, shflags = 0;

    shadowbios = 0;
    shadowbios_write = 0;

    for (i = 0; i < 8; i++) {
	base = 0xc0000 + (i << 15);

	if (dev->regs[0x13] & (1 << i)) {
		shadowbios |= (base >= 0xe8000) && !!(dev->regs[0x14] & 0x01);
		shadowbios_write |= (base >= 0xe8000) && !!(dev->regs[0x14] & 0x02);
		shflags = (dev->regs[0x14] & 0x01) ? MEM_READ_INTERNAL : MEM_READ_EXTANY;
		shflags |= !(dev->regs[0x14] & 0x02) ? MEM_WRITE_EXTANY : MEM_WRITE_INTERNAL;
		mem_set_mem_state(base, 0x8000, shflags);
	} else
		mem_set_mem_state(base, 0x8000, MEM_READ_EXTANY | MEM_WRITE_EXTANY);
    }

    flushmmucache();
}


static void
ali1429_write(uint16_t port, uint8_t val, void *priv)
{
    ali1429_t *dev = (ali1429_t *) priv;

    if (port & 1) {
	dev->regs[dev->cur_reg] = val;

	switch (dev->cur_reg) {
		case 0x13:
			ali1429_recalc(dev);
			break;
		case 0x14:
			ali1429_recalc(dev);
			break;
	}
    } else
	dev->cur_reg = val;
}


static uint8_t
ali1429_read(uint16_t port, void *priv)
{
    uint8_t ret = 0xff;
    ali1429_t *dev = (ali1429_t *) priv;

    if (!(port & 1)) 
	ret = dev->cur_reg;
    else if (((dev->cur_reg >= 0xc0) || (dev->cur_reg == 0x20)) && cpu_iscyrix)
	ret = 0xff; /*Don't conflict with Cyrix config registers*/
    else
	ret = dev->regs[dev->cur_reg];

    return ret;
}


static void
ali1429_close(void *priv)
{
    ali1429_t *dev = (ali1429_t *) priv;

    free(dev);
}


static void *
ali1429_init(const device_t *info)
{
    ali1429_t *dev = (ali1429_t *) malloc(sizeof(ali1429_t));
    memset(dev, 0, sizeof(ali1429_t));

    memset(dev->regs, 0xff, 256);
    dev->regs[0x13] = dev->regs[0x14] = 0x00;

    io_sethandler(0x0022, 0x0002, ali1429_read, NULL, NULL, ali1429_write, NULL, NULL, dev);

    ali1429_recalc(dev);

    device_add(&port_92_device);

    return dev;
}


const device_t ali1429_device = {
    "ALi-M1429",
    0,
    0,
    ali1429_init, ali1429_close, NULL,
    NULL, NULL, NULL,
    NULL
};
