#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>
#define HAVE_STDARG_H
#include <86box/86box.h>
#include "cpu.h"
#include <86box/io.h>
#include <86box/device.h>
#include <86box/mem.h>
#include <86box/chipset.h>


typedef struct
{
    uint8_t	cur_reg, tries,
		regs[258];
} rabbit_t;


static void
rabbit_recalcmapping(rabbit_t *dev)
{
    uint32_t shread, shwrite;
    uint32_t shflags = 0;

    shread = !!(dev->regs[0x101] & 0x40);
    shwrite = !!(dev->regs[0x100] & 0x02);

    shflags = shread ? MEM_READ_INTERNAL : MEM_READ_EXTANY;
    shflags |= shwrite ? MEM_WRITE_INTERNAL : MEM_WRITE_EXTANY;

    shadowbios = !!shread;
    shadowbios_write = !!shwrite;

#ifdef USE_SHADOW_C0000
    mem_set_mem_state(0x000c0000, 0x00040000, MEM_READ_EXTANY | MEM_WRITE_EXTANY);
#else
    mem_set_mem_state(0x000e0000, 0x00020000, MEM_READ_EXTANY | MEM_WRITE_EXTANY);
#endif

    switch (dev->regs[0x100] & 0x09) {
	case 0x01:
/* The one BIOS we use seems to use something else to control C0000-DFFFF shadow,
   no idea what. */
#ifdef USE_SHADOW_C0000
		/* 64K at 0C0000-0CFFFF */
		mem_set_mem_state(0x000c0000, 0x00010000, shflags);
		/* FALLTHROUGH */
#endif
	case 0x00:
		/* 64K at 0F0000-0FFFFF */
		mem_set_mem_state(0x000f0000, 0x00010000, shflags);
		break;

	case 0x09:
#ifdef USE_SHADOW_C0000
		/* 128K at 0C0000-0DFFFF */
		mem_set_mem_state(0x000c0000, 0x00020000, shflags);
		/* FALLTHROUGH */
#endif
	case 0x08:
		/* 128K at 0E0000-0FFFFF */
		mem_set_mem_state(0x000e0000, 0x00020000, shflags);
		break;
    }

    flushmmucache();
}


static void
rabbit_write(uint16_t addr, uint8_t val, void *priv)
{
    rabbit_t *dev = (rabbit_t *) priv;

    switch (addr) {
	case 0x22:
		dev->cur_reg = val;
		dev->tries = 0;
		break;
	case 0x23:
		if (dev->cur_reg == 0x83) {
			if (dev->tries < 0x02) {
				dev->regs[dev->tries++ | 0x100] = val;
				if (dev->tries == 0x02)
					rabbit_recalcmapping(dev);
			}
		} else
			dev->regs[dev->cur_reg] = val;
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
		if (dev->cur_reg == 0x83) {
			if (dev->tries < 0x02)
				ret = dev->regs[dev->tries++ | 0x100];
		} else
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

    io_sethandler(0x0022, 0x0002, rabbit_read, NULL, NULL, rabbit_write, NULL, NULL, dev);

    return dev;
}

const device_t rabbit_device = {
    .name = "SiS Rabbit",
    .internal_name = "rabbit",
    .flags = 0,
    .local = 0,
    .init = rabbit_init,
    .close = rabbit_close,
    .reset = NULL,
    { .available = NULL },
    .speed_changed = NULL,
    .force_redraw = NULL,
    .config = NULL
};
