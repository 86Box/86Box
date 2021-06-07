/*
 * 86Box	A hypervisor and IBM PC system emulator that specializes in
 *		running old operating systems and software designed for IBM
 *		PC systems and compatibles from 1981 through fairly recent
 *		system designs based on the PCI bus.
 *
 *		This file is part of the 86Box distribution.
 *
 *		Implementation of the OPTi 82C283 chipset.
 *
 *
 *
 *      Authors: Tiseno100
 *
 *		Copyright 2021 Tiseno100
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
#include <86box/mem.h>
#include <86box/chipset.h>

#define ENABLE_OPTI283_LOG 1
#ifdef ENABLE_OPTI283_LOG
int opti283_do_log = ENABLE_OPTI283_LOG;
static void
opti283_log(const char *fmt, ...)
{
	va_list ap;

	if (opti283_do_log)
	{
		va_start(ap, fmt);
		pclog_ex(fmt, ap);
		va_end(ap);
	}
}
#else
#define opti283_log(fmt, ...)
#endif

typedef struct
{
	uint8_t index,
	    regs[5];
} opti283_t;

static void opti283_shadow_recalc(uint32_t base, int shadow_enabled, int wp_bit, opti283_t *dev)
{
	opti283_log("OPTi 283-SHADOW: BASE: 0x%05x ENABLE: %01x WP: %01x\n", base, shadow_enabled, wp_bit);
	if (shadow_enabled)
	{
		if (wp_bit)
			mem_set_mem_state_both(base, 0x4000, MEM_READ_INTERNAL | MEM_WRITE_DISABLED);
		else if (dev->regs[1] & 8)
			mem_set_mem_state_both(base, 0x4000, MEM_READ_INTERNAL | MEM_WRITE_INTERNAL);
		else
			mem_set_mem_state_both(base, 0x4000, MEM_READ_INTERNAL | MEM_WRITE_EXTANY);
	}
	else
		mem_set_mem_state_both(base, 0x4000, MEM_READ_EXTANY | MEM_WRITE_EXTANY);
}

static void opti283_shadow(opti283_t *dev)
{
	mem_set_mem_state_both(0xf0000, 0x10000, !!(dev->regs[1] & 0x80) ? (MEM_READ_EXTANY | MEM_WRITE_INTERNAL) : (MEM_READ_INTERNAL | MEM_WRITE_DISABLED));
	opti283_log("OPTi 283-SHADOW: BASE: 0xf0000 ENABLE: %01x WP: %01x\n", !(dev->regs[1] & 0x80), !!(dev->regs[1] & 0x80));

	for (int i = 0; i < 4; i++)
	{
		opti283_shadow_recalc(0xc0000 + (i << 14), !!((dev->regs[1] & 0x40) && (dev->regs[3] & (0x10 << i))), !!(dev->regs[1] & 1), dev);
		opti283_shadow_recalc(0xd0000 + (i << 14), !!((dev->regs[1] & 0x40) && (dev->regs[2] & (1 << i))), !!(dev->regs[1] & 2), dev);
		opti283_shadow_recalc(0xe0000 + (i << 14), !!((dev->regs[1] & 0x40) && (dev->regs[2] & (0x10 << i))), !!(dev->regs[1] & 4), dev);
	}
}

static void
opti283_clock(opti283_t *dev)
{
if(dev->regs[4] & 0x10)
cpu_set_isa_speed(cpu_busspeed / 8);
else if(dev->regs[4] & 1)
cpu_set_isa_speed(cpu_busspeed / 4);
else
cpu_set_isa_speed(cpu_busspeed / 6);
}

static void
opti283_write(uint16_t addr, uint8_t val, void *priv)
{
	opti283_t *dev = (opti283_t *)priv;

	switch (addr)
	{
	case 0x22:
		dev->index = val;
		break;
	case 0x24:

		if ((dev->index >= 0x10) && (dev->index <= 0x14))
		{
			opti283_log("OPTi 283: dev->regs[%02x] = %02x\n", dev->index, val);
			switch (dev->index)
			{
			case 0x10:
				dev->regs[dev->index - 0x10] = val;
				break;

			case 0x11:
			case 0x12:
			case 0x13:
				dev->regs[dev->index - 0x10] = val;
				opti283_shadow(dev);
				break;

			case 0x14:
				dev->regs[dev->index - 0x10] = val;
				break;
			}
			break;
		}
	}
}

static uint8_t
opti283_read(uint16_t addr, void *priv)
{
	opti283_t *dev = (opti283_t *)priv;

	if ((dev->index >= 0x10) && (dev->index <= 0x14) && (addr == 0x24))
		return dev->regs[dev->index - 0x10];
	else if (addr == 0x22)
		return dev->index;
	else
		return 0;
}

static void
opti283_close(void *priv)
{
	opti283_t *dev = (opti283_t *)priv;

	free(dev);
}

static void *
opti283_init(const device_t *info)
{
	opti283_t *dev = (opti283_t *)malloc(sizeof(opti283_t));
	memset(dev, 0, sizeof(opti283_t));

	io_sethandler(0x0022, 1, opti283_read, NULL, NULL, opti283_write, NULL, NULL, dev);
	io_sethandler(0x0024, 1, opti283_read, NULL, NULL, opti283_write, NULL, NULL, dev);

	dev->regs[0] = 0x3f;
	dev->regs[1] = 0xf0;

	opti283_shadow(dev);

	opti283_clock(dev);

	return dev;
}

const device_t opti283_device = {
    "OPTi 82C283",
    0,
    0,
    opti283_init,
    opti283_close,
    NULL,
    {NULL},
    NULL,
    NULL,
    NULL};
