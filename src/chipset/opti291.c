/*
 * 86Box	A hypervisor and IBM PC system emulator that specializes in
 *		running old operating systems and software designed for IBM
 *		PC systems and compatibles from 1981 through fairly recent
 *		system designs based on the PCI bus.
 *
 *		This file is part of the 86Box distribution.
 *
 *		Implementation of the OPTi 82C291 chipset.
 
 * Authors:	plant/nerd73
 *
 *              Copyright 2020 plant/nerd73.
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
    uint8_t	index,
	regs[256];
    port_92_t  *port_92;  	
} opti291_t;

static void opti291_recalc(opti291_t *dev)
{
	uint32_t base;
	uint32_t i, shflags, write, writef = 0;
	
	
	writef = (dev->regs[0x27] & 0x80) ? MEM_WRITE_DISABLED : MEM_WRITE_INTERNAL;	
	if (!(dev->regs[0x23] & 0x40))
	mem_set_mem_state(0xf0000, 0x10000, MEM_READ_INTERNAL | writef);
	else
	mem_set_mem_state(0xf0000, 0x10000, MEM_READ_EXTANY | writef);		
	
	for (i = 0; i < 4; i++) {
	base = 0xe0000 + (i << 14);
	shflags = (dev->regs[0x24] & (1 << (i+4))) ? MEM_READ_INTERNAL : MEM_READ_EXTANY;
	write = (dev->regs[0x24] & (1 << i)) ? MEM_WRITE_INTERNAL : MEM_WRITE_EXTANY;	
	shflags |= (dev->regs[0x27] & 0x40) ? MEM_WRITE_DISABLED : write;
	mem_set_mem_state(base, 0x4000, shflags);
	}
	for (i = 0; i < 4; i++) {
	base = 0xd0000 + (i << 14);
	shflags = (dev->regs[0x25] & (1 << (i+4))) ? MEM_READ_INTERNAL : MEM_READ_EXTANY;
	write = (dev->regs[0x25] & (1 << i)) ? MEM_WRITE_INTERNAL : MEM_WRITE_EXTANY;
	shflags |= (dev->regs[0x27] & 0x20) ? MEM_WRITE_DISABLED : write;	
	mem_set_mem_state(base, 0x4000, shflags);
	}
	
	for (i = 0; i < 4; i++) {
	base = 0xc0000 + (i << 14);
	shflags = (dev->regs[0x26] & (1 << (i+4))) ? MEM_READ_INTERNAL : MEM_READ_EXTANY;
	write = (dev->regs[0x26] & (1 << i)) ? MEM_WRITE_INTERNAL : MEM_WRITE_EXTANY;
	shflags |= (dev->regs[0x27] & 0x10) ? MEM_WRITE_DISABLED : write;	
	mem_set_mem_state(base, 0x4000, shflags);
	}
    flushmmucache();	
}	
static void
opti291_write(uint16_t addr, uint8_t val, void *priv)
{
    opti291_t *dev = (opti291_t *) priv;

    switch (addr) {
	case 0x22:
		dev->index = val;
		break;
	case 0x24:
        pclog("OPTi 291: dev->regs[%02x] = %02x\n", dev->index, val);
		dev->regs[dev->index] = val;

        switch(dev->index){
				case 0x21:
					cpu_update_waitstates();
					break;
				case 0x23:
				case 0x24:
				case 0x25:
				case 0x26:
				case 0x27:
					opti291_recalc(dev);
					break;
        }
		break;
    }
}


static uint8_t
opti291_read(uint16_t addr, void *priv)
{
    uint8_t ret = 0xff;
    opti291_t *dev = (opti291_t *) priv;

    switch (addr) {
	case 0x24:
//        pclog("OPTi 291: read from dev->regs[%02x]\n", dev->index);	
		ret = dev->regs[dev->index];
		break;
    }

    return ret;
}


static void
opti291_close(void *priv)
{
    opti291_t *dev = (opti291_t *) priv;

    free(dev);
}


static void *
opti291_init(const device_t *info)
{
    opti291_t *dev = (opti291_t *) malloc(sizeof(opti291_t));
    memset(dev, 0, sizeof(opti291_t));

    io_sethandler(0x022, 0x0001, opti291_read, NULL, NULL, opti291_write, NULL, NULL, dev);
    io_sethandler(0x024, 0x0001, opti291_read, NULL, NULL, opti291_write, NULL, NULL, dev);
    dev->regs[0x23] = 0x40;
    dev->port_92 = device_add(&port_92_device);    
    opti291_recalc(dev);
    
    return dev;
}


const device_t opti291_device = {
    "OPTi 82C291",
    0,
    0,
    opti291_init, opti291_close, NULL,
    { NULL }, NULL, NULL,
    NULL
};
