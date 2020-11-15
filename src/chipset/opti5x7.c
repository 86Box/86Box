/*
 * 86Box	A hypervisor and IBM PC system emulator that specializes in
 *		running old operating systems and software designed for IBM
 *		PC systems and compatibles from 1981 through fairly recent
 *		system designs based on the PCI bus.
 *
 *		This file is part of the 86Box distribution.
 *
 *		Implementation of the OPTi 82C546/82C547 & 82C596/82C597 chipsets.
 
 * Authors:	plant/nerd73
 *              Miran Grca, <mgrca8@gmail.com>
 *
 *              Copyright 2020 plant/nerd73.
 *              Copyright 2020 Miran Grca.
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
		regs[16];
    port_92_t  *port_92;    
} opti5x7_t;


#ifdef ENABLE_OPTI5X7_LOG
int opti5x7_do_log = ENABLE_OPTI5X7_LOG;


static void
opti5x7_log(const char *fmt, ...)
{
    va_list ap;

    if (opti5x7_do_log) {
	va_start(ap, fmt);
	pclog_ex(fmt, ap);
	va_end(ap);
    }
}
#else
#define opti5x7_log(fmt, ...)
#endif


static void
opti5x7_recalc(opti5x7_t *dev)
{
    uint32_t base;
    uint32_t i, shflags = 0;
    uint32_t reg, lowest_bit;

    shadowbios = 0;
    shadowbios_write = 0;

    for (i = 0; i < 8; i++) {
	base = 0xc0000 + (i << 14);

	lowest_bit = (i << 1) & 0x07;
	reg = 0x04 + ((base >> 16) & 0x01);

	shflags = (dev->regs[reg] & (1 << lowest_bit)) ? MEM_READ_INTERNAL : MEM_READ_EXTANY;
	shflags |= (dev->regs[reg] & (1 << (lowest_bit + 1))) ? MEM_WRITE_INTERNAL : MEM_WRITE_EXTANY;
	mem_set_mem_state(base, 0x4000, shflags);
    }

    shadowbios |= !!(dev->regs[0x06] & 0x05);
    shadowbios_write |= !!(dev->regs[0x06] & 0x0a);

    shflags = (dev->regs[0x06] & 0x01) ? MEM_READ_INTERNAL : MEM_READ_EXTANY;
    shflags |= (dev->regs[0x06] & 0x02) ? MEM_WRITE_INTERNAL : MEM_WRITE_EXTANY;
    mem_set_mem_state(0xe0000, 0x10000, shflags);

    shflags = (dev->regs[0x06] & 0x04) ? MEM_READ_INTERNAL : MEM_READ_EXTANY;
    shflags |= (dev->regs[0x06] & 0x08) ? MEM_WRITE_INTERNAL : MEM_WRITE_EXTANY;
    mem_set_mem_state(0xf0000, 0x10000, shflags);

    flushmmucache();
}


static void
opti5x7_write(uint16_t addr, uint8_t val, void *priv)
{	
    opti5x7_t *dev = (opti5x7_t *) priv;
    opti5x7_log("Write %02x to OPTi 5x7 address %02x\n", val, addr);
    
    switch (addr) {
	case 0x22:
		dev->idx = val;
		break;	    
	case 0x24:	
		dev->regs[dev->idx] = val;
		switch(dev->idx) {
			case 0x02:
				cpu_cache_ext_enabled = !!(dev->regs[0x02] & 0x04 & 0x08);
				break;

			case 0x04:
			case 0x05:
			case 0x06:
				opti5x7_recalc(dev);
				break;
		}
		break;	    
    }
}


static uint8_t
opti5x7_read(uint16_t addr, void *priv)
{
    uint8_t ret = 0xff;
    opti5x7_t *dev = (opti5x7_t *) priv;

    switch (addr) {
	case 0x24:
			opti5x7_log("Read from OPTi 5x7 register %02x\n", dev->idx);
			ret = dev->regs[dev->idx];
		break;
    }

    return ret;
}


static void
opti5x7_close(void *priv)
{
    opti5x7_t *dev = (opti5x7_t *) priv;

    free(dev);
}


static void *
opti5x7_init(const device_t *info)
{
    opti5x7_t *dev = (opti5x7_t *) malloc(sizeof(opti5x7_t));
    memset(dev, 0, sizeof(opti5x7_t));

    io_sethandler(0x0022, 0x0001, opti5x7_read, NULL, NULL, opti5x7_write, NULL, NULL, dev);
    io_sethandler(0x0024, 0x0001, opti5x7_read, NULL, NULL, opti5x7_write, NULL, NULL, dev);

    dev->port_92 = device_add(&port_92_device);    

    return dev;
}

const device_t opti5x7_device = {
    "OPTi 82C5x6/82C5x7",
    0,
    0,
    opti5x7_init, opti5x7_close, NULL,
    { NULL }, NULL, NULL,
    NULL
};
