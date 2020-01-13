/*
 * 86Box	A hypervisor and IBM PC system emulator that specializes in
 *		running old operating systems and software designed for IBM
 *		PC systems and compatibles from 1981 through fairly recent
 *		system designs based on the PCI bus.
 *
 *		This file is part of the 86Box distribution.
 *
 *		Emulation of the SiS 85c471 chip.
 *
 *		SiS sis85c471 Super I/O Chip
 *		Used by DTK PKM-0038S E-2
 *
 * Version:	@(#)sis_85c471.c	1.0.2	2019/10/21
 *
 * Authors:	Miran Grca, <mgrca8@gmail.com>
 *
 *		Copyright 2019 Miran Grca.
 */
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>
#include "../86box.h"
#include "../cpu/cpu.h"
#include "../mem.h"
#include "../io.h"
#include "../lpt.h"
#include "../rom.h"
#include "../pci.h"
#include "../device.h"
#include "../disk/hdc_ide.h"
#include "../keyboard.h"
#include "../timer.h"
#include "../port_92.h"
#include "../serial.h"
#include "../machine/machine.h"
#include "chipset.h"


typedef struct {
    uint8_t	cur_reg,
		regs[39],
		scratch[2];
    port_92_t *	port_92;
} sis_85c471_t;


static void
sis_85c471_recalcmapping(sis_85c471_t *dev)
{
    uint32_t base;
    uint32_t i, shflags = 0;

    shadowbios = 0;
    shadowbios_write = 0;

    for (i = 0; i < 8; i++) {
	base = 0xc0000 + (i << 15);

	if ((i > 5) || (dev->regs[0x02] & (1 << i))) {
		shadowbios |= (base >= 0xe0000) && (dev->regs[0x02] & 0x80);
		shadowbios_write |= (base >= 0xe0000) && !(dev->regs[0x02] & 0x40);
		shflags = (dev->regs[0x02] & 0x80) ? MEM_READ_INTERNAL : MEM_READ_EXTANY;
		shflags |= (dev->regs[0x02] & 0x40) ? MEM_WRITE_EXTANY : MEM_WRITE_INTERNAL;
		mem_set_mem_state(base, 0x8000, shflags);
	} else
		mem_set_mem_state(base, 0x8000, MEM_READ_EXTANY | MEM_WRITE_EXTERNAL);
    }

    flushmmucache();
}


static void
sis_85c471_write(uint16_t port, uint8_t val, void *priv)
{
    sis_85c471_t *dev = (sis_85c471_t *) priv;
    uint8_t valxor = 0x00;

    if (port == 0x22) {
	if ((val >= 0x50) && (val <= 0x76))
		dev->cur_reg = val;
	return;
    } else if (port == 0x23) {
	if ((dev->cur_reg < 0x50) || (dev->cur_reg > 0x76))
		return;
	valxor = val ^ dev->regs[dev->cur_reg - 0x50];
	dev->regs[dev->cur_reg - 0x50] = val;
    } else if ((port == 0xe1) || (port == 0xe2)) {
	dev->scratch[port - 0xe1] = val;
	return;
    }

    switch(dev->cur_reg) {
	case 0x51:
		cpu_cache_ext_enabled = ((val & 0x84) == 0x84);
		cpu_update_waitstates();
		break;

	case 0x52:
		sis_85c471_recalcmapping(dev);
		break;

	case 0x57:
		if (valxor & 0x12)
			port_92_set_features(dev->port_92, !!(val & 0x10), !!(val & 0x02));

		if (valxor & 0x08) {
			if (val & 0x08)
				port_92_set_period(dev->port_92, 6ULL * TIMER_USEC);
			else
				port_92_set_period(dev->port_92, 2ULL * TIMER_USEC);
		}
		break;

	case 0x5b:
		if (valxor & 0x02) {
			if (val & 0x02)
				mem_remap_top(0);
			else
				mem_remap_top(256);
		}
		break;

	case 0x63:
		if (valxor & 0x10) {
			if (dev->regs[0x13] & 0x10)
				mem_set_mem_state(0xa0000, 0x20000, MEM_READ_INTERNAL | MEM_WRITE_INTERNAL);
			else
				mem_set_mem_state(0xa0000, 0x20000, MEM_READ_EXTANY | MEM_WRITE_EXTANY);
		}
		break;

	case 0x72:
		if (valxor & 0x01) {
			port_92_remove(dev->port_92);
			if (val & 0x01)
				port_92_add(dev->port_92);
		}
		break;
    }

    dev->cur_reg = 0;
}


static uint8_t
sis_85c471_read(uint16_t port, void *priv)
{
    sis_85c471_t *dev = (sis_85c471_t *) priv;
    uint8_t ret = 0xff;

    if (port == 0x22)
	ret = dev->cur_reg;
    else if (port == 0x23) {
	if ((dev->cur_reg >= 0x50) && (dev->cur_reg <= 0x76)) {
		ret = dev->regs[dev->cur_reg - 0x50];
		if (dev->cur_reg == 0x58)
			ret &= 0xf7;
		dev->cur_reg = 0;
	}
    } else if ((port == 0xe1) || (port == 0xe2))
	ret = dev->scratch[port - 0xe1];

    return ret;
}


static void
sis_85c471_close(void *priv)
{
    sis_85c471_t *dev = (sis_85c471_t *) priv;

    free(dev);
}


static void *
sis_85c471_init(const device_t *info)
{
    int mem_size_mb, i = 0;

    sis_85c471_t *dev = (sis_85c471_t *) malloc(sizeof(sis_85c471_t));
    memset(dev, 0, sizeof(sis_85c471_t));

    dev->cur_reg = 0;
    for (i = 0; i < 0x27; i++)
	dev->regs[i] = 0x00;

    dev->regs[9] = 0x40;

    mem_size_mb = mem_size >> 10;
    switch (mem_size_mb) {
	case 0: case 1:
		dev->regs[9] |= 0;
		break;
	case 2: case 3:
		dev->regs[9] |= 1;
		break;
	case 4:
		dev->regs[9] |= 2;
		break;
	case 5:
		dev->regs[9] |= 0x20;
		break;
	case 6: case 7:
		dev->regs[9] |= 9;
		break;
	case 8: case 9:
		dev->regs[9] |= 4;
		break;
	case 10: case 11:
		dev->regs[9] |= 5;
		break;
	case 12: case 13: case 14: case 15:
		dev->regs[9] |= 0xB;
		break;
	case 16:
		dev->regs[9] |= 0x13;
		break;
	case 17:
		dev->regs[9] |= 0x21;
		break;
	case 18: case 19:
		dev->regs[9] |= 6;
		break;
	case 20: case 21: case 22: case 23:
		dev->regs[9] |= 0xD;
		break;
	case 24: case 25: case 26: case 27:
	case 28: case 29: case 30: case 31:
		dev->regs[9] |= 0xE;
		break;
	case 32: case 33: case 34: case 35:
		dev->regs[9] |= 0x1B;
		break;
	case 36: case 37: case 38: case 39:
		dev->regs[9] |= 0xF;
		break;
	case 40: case 41: case 42: case 43:
	case 44: case 45: case 46: case 47:
		dev->regs[9] |= 0x17;
		break;
	case 48:
		dev->regs[9] |= 0x1E;
		break;
	default:
		if (mem_size_mb < 64)
			dev->regs[9] |= 0x1E;
		else if ((mem_size_mb >= 65) && (mem_size_mb < 68))
			dev->regs[9] |= 0x22;
		else
			dev->regs[9] |= 0x24;
		break;
    }

    dev->regs[0x11] = 9;
    dev->regs[0x12] = 0xFF;
    dev->regs[0x1f] = 0x20;	/* Video access enabled. */
    dev->regs[0x23] = 0xF0;
    dev->regs[0x26] = 1;

    if (machines[machine].cpu[cpu_manufacturer].cpus[cpu_effective].rspeed < 25000000)
	dev->regs[0x08] |= 0x80;

    io_sethandler(0x0022, 0x0002,
		  sis_85c471_read, NULL, NULL, sis_85c471_write, NULL, NULL, dev);

    dev->scratch[0] = dev->scratch[1] = 0xff;

    io_sethandler(0x00e1, 0x0002,
		  sis_85c471_read, NULL, NULL, sis_85c471_write, NULL, NULL, dev);

    dev->port_92 = device_add(&port_92_device);
    port_92_set_period(dev->port_92, 2ULL * TIMER_USEC);
    port_92_set_features(dev->port_92, 0, 0);

    sis_85c471_recalcmapping(dev);

    return dev;
}


const device_t sis_85c471_device = {
    "SiS 85c471",
    0,
    0,
    sis_85c471_init, sis_85c471_close, NULL,
    NULL, NULL, NULL,
    NULL
};
