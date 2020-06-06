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
    uint8_t	cur_reg,
		regs[16];
} rabbit_t;

/*
static void
rabbit_recalcmapping(rabbit_t *dev)
{
    uint32_t base;
    uint32_t i, shflags = 0;

    shadowbios = 0;
    shadowbios_write = 0;

    for (i = 0; i < 8; i++) {
	base = 0xc0000 + (i << 15);

	if (dev->regs[0x00] & 0x08) {
		shadowbios |= (base >= 0xe0000) && (dev->regs[0x02] & 0x80);
		shadowbios_write |= (base >= 0xe0000) && !(dev->regs[0x02] & 0x40);
		shflags = (dev->regs[0x00] & 0x01) ? MEM_READ_INTERNAL : MEM_READ_EXTANY;
		shflags |= (dev->regs[0x00] & 0x08) ? MEM_WRITE_EXTANY : MEM_WRITE_INTERNAL;
		mem_set_mem_state(base, 0x8000, shflags);
	} else
		mem_set_mem_state(base, 0x8000, MEM_READ_EXTANY | MEM_WRITE_EXTERNAL);
    }

    flushmmucache();
}
*/

static void
rabbit_write(uint16_t addr, uint8_t val, void *priv)
{
    rabbit_t *dev = (rabbit_t *) priv;

    switch (addr) {
	case 0x22:
		dev->cur_reg = val;
		break;
	case 0x23:
			dev->regs[dev->cur_reg] = val;
            /*
			if (dev->cur_reg == 0x00) {
				rabbit_recalcmapping(dev);
			}
            */
		break;
		}
}


static uint8_t
rabbit_read(uint16_t addr, void *priv)
{
    uint8_t ret = 0xff;
    rabbit_t *dev = (rabbit_t *) priv;

    switch (addr) {
	case 0x23:
			ret = dev->regs[dev->cur_reg];
		break;
    }

    return ret;
}


static void
rabbit_close(void *priv)
{
    rabbit_t *dev = (rabbit_t *) priv;

    free(dev);
}


static void *
rabbit_init(const device_t *info)
{
    rabbit_t *dev = (rabbit_t *) malloc(sizeof(rabbit_t));
    memset(dev, 0, sizeof(rabbit_t));

    io_sethandler(0x0022, 0x0001, rabbit_read, NULL, NULL, rabbit_write, NULL, NULL, dev);
    io_sethandler(0x0023, 0x0001, rabbit_read, NULL, NULL, rabbit_write, NULL, NULL, dev);

    return dev;
}


const device_t rabbit_device = {
    "SiS Rabbit",
    0,
    0,
    rabbit_init, rabbit_close, NULL,
    NULL, NULL, NULL,
    NULL
};