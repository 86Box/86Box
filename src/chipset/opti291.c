/*
 * 86Box	A hypervisor and IBM PC system emulator that specializes in
 *		running old operating systems and software designed for IBM
 *		PC systems and compatibles from 1981 through fairly recent
 *		system designs based on the PCI bus.
 *
 *		This file is part of the 86Box distribution.
 *
 *		Implementation of the OPTi 82C291 chipset.

 * Authors:	plant/nerd73, Tiseno100
 *
 *              Copyright 2020 plant/nerd73.
 *              Copyright 2021 Tiseno100.
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
#include <86box/port_92.h>
#include <86box/chipset.h>

#ifdef ENABLE_OPTI291_LOG
int opti291_do_log = ENABLE_OPTI291_LOG;
static void
opti291_log(const char *fmt, ...)
{
	va_list ap;

	if (opti291_do_log)
	{
		va_start(ap, fmt);
		pclog_ex(fmt, ap);
		va_end(ap);
	}
}
#else
#define opti291_log(fmt, ...)
#endif

typedef struct
{
	uint8_t index, regs[256];
	port_92_t *port_92;
} opti291_t;

static void opti291_recalc(opti291_t *dev)
{
	mem_set_mem_state_both(0xf0000, 0x10000, (!(dev->regs[0x23] & 0x40) ? MEM_READ_INTERNAL : MEM_READ_EXTANY) | ((dev->regs[0x27] & 0x80) ? MEM_WRITE_DISABLED : MEM_WRITE_INTERNAL));

	for (uint32_t i = 0; i < 4; i++)
	{
		mem_set_mem_state_both(0xc0000 + (i << 14), 0x4000, ((dev->regs[0x26] & (1 << (i + 4))) ? MEM_READ_INTERNAL : MEM_READ_EXTANY) | ((dev->regs[0x27] & 0x10) ? MEM_WRITE_DISABLED : ((dev->regs[0x26] & (1 << i)) ? MEM_WRITE_INTERNAL : MEM_WRITE_EXTANY)));
		mem_set_mem_state_both(0xd0000 + (i << 14), 0x4000, ((dev->regs[0x25] & (1 << (i + 4))) ? MEM_READ_INTERNAL : MEM_READ_EXTANY) | ((dev->regs[0x27] & 0x20) ? MEM_WRITE_DISABLED : ((dev->regs[0x25] & (1 << i)) ? MEM_WRITE_INTERNAL : MEM_WRITE_EXTANY)));
		mem_set_mem_state_both(0xe0000 + (i << 14), 0x4000, ((dev->regs[0x24] & (1 << (i + 4))) ? MEM_READ_INTERNAL : MEM_READ_EXTANY) | ((dev->regs[0x27] & 0x40) ? MEM_WRITE_DISABLED : ((dev->regs[0x24] & (1 << i)) ? MEM_WRITE_INTERNAL : MEM_WRITE_EXTANY)));
	}
	flushmmucache();
}
static void
opti291_write(uint16_t addr, uint8_t val, void *priv)
{
	opti291_t *dev = (opti291_t *)priv;

	switch (addr)
	{
	case 0x22:
		dev->index = val;
		break;
	case 0x24:
		opti291_log("OPTi 291: dev->regs[%02x] = %02x\n", dev->index, val);
		switch (dev->index)
		{
		case 0x20:
			dev->regs[dev->index] = val & 0x3f;
			break;
		case 0x21:
			dev->regs[dev->index] = val & 0xf3;
			break;
		case 0x22:
			dev->regs[dev->index] = val;
			break;
		case 0x23:
		case 0x24:
		case 0x25:
		case 0x26:
			dev->regs[dev->index] = val;
			opti291_recalc(dev);
			break;
		case 0x27:
		case 0x28:
			dev->regs[dev->index] = val;
			break;
		case 0x29:
			dev->regs[dev->index] = val & 0x0f;
			break;
		case 0x2a:
		case 0x2b:
		case 0x2c:
			dev->regs[dev->index] = val;
			break;
		}
		break;
	}
}

static uint8_t
opti291_read(uint16_t addr, void *priv)
{
	opti291_t *dev = (opti291_t *)priv;

	return (addr == 0x24) ? dev->regs[dev->index] : 0xff;
}

static void
opti291_close(void *priv)
{
	opti291_t *dev = (opti291_t *)priv;

	free(dev);
}

static void *
opti291_init(const device_t *info)
{
	opti291_t *dev = (opti291_t *)malloc(sizeof(opti291_t));
	memset(dev, 0, sizeof(opti291_t));

	io_sethandler(0x022, 0x0001, opti291_read, NULL, NULL, opti291_write, NULL, NULL, dev);
	io_sethandler(0x024, 0x0001, opti291_read, NULL, NULL, opti291_write, NULL, NULL, dev);
	dev->regs[0x22] = 0xf0;
	dev->regs[0x23] = 0x40;
	dev->regs[0x28] = 0x08;
	dev->regs[0x29] = 0xa0;
	device_add(&port_92_device);
	opti291_recalc(dev);

	return dev;
}

const device_t opti291_device = {
    .name = "OPTi 82C291",
    .internal_name = "opti291",
    .flags = 0,
    .local = 0,
    .init = opti291_init,
    .close = opti291_close,
    .reset = NULL,
    { .available = NULL },
    .speed_changed = NULL,
    .force_redraw = NULL,
    .config = NULL
};
