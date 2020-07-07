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
 *		Copyright 2020 Tiseno100
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
#include <86box/keyboard.h>
#include <86box/mem.h>
#include <86box/fdd.h>
#include <86box/fdc.h>
#include <86box/port_92.h>
#include <86box/chipset.h>

#define disabled_shadow (MEM_READ_EXTANY | MEM_WRITE_EXTANY)


typedef struct
{
    uint8_t	index,
	regs[256];
} opti283_t;

static void opti283_shadow_recalc(opti283_t *dev)
{
uint32_t base, i;
uint32_t shflagsc, shflagsd, shflagse, shflagsf;

shadowbios = !(dev->regs[0x11] & 0x80);
shadowbios_write = (dev->regs[0x11] & 0x80);

if(dev->regs[0x11] & 0x10){
    shflagsc = MEM_READ_INTERNAL;
    shflagsc |= (dev->regs[0x11] & 0x08) ? MEM_WRITE_INTERNAL : MEM_WRITE_EXTANY;
} else shflagsc = disabled_shadow;

if(dev->regs[0x11] & 0x20){
    shflagsd = MEM_READ_INTERNAL;
    shflagsd |= (dev->regs[0x11] & 0x08) ? MEM_WRITE_INTERNAL : MEM_WRITE_EXTANY;
} else shflagsd = disabled_shadow;

if(dev->regs[0x11] & 0x40){
    shflagse = MEM_READ_INTERNAL;
    shflagse |= (dev->regs[0x11] & 0x08) ? MEM_WRITE_INTERNAL : MEM_WRITE_EXTANY;
} else shflagse = disabled_shadow;

if(!(dev->regs[0x11] & 0x80)){
    shflagsf = MEM_READ_INTERNAL | MEM_WRITE_DISABLED;
} else shflagsf = MEM_READ_EXTANY | MEM_WRITE_INTERNAL;

mem_set_mem_state_both(0xf0000, 0x10000, shflagsf);

for(i = 4; i < 8; i++){
base = 0xc0000 + ((i-4) << 14);
mem_set_mem_state_both(base, 0x4000, (dev->regs[0x13] & (1 << i)) ? shflagsc : disabled_shadow);
}

for(i = 0; i < 4; i++){
base = 0xd0000 + (i << 14);
mem_set_mem_state_both(base, 0x4000, (dev->regs[0x12] & (1 << i)) ? shflagsd : disabled_shadow);
}

for(i = 4; i < 8; i++){
base = 0xe0000 + ((i-4) << 14);
mem_set_mem_state_both(base, 0x4000, (dev->regs[0x12] & (1 << i)) ? shflagse : disabled_shadow);
}

}

static void
opti283_write(uint16_t addr, uint8_t val, void *priv)
{
    opti283_t *dev = (opti283_t *) priv;

    switch (addr) {
	case 0x22:
		dev->index = val;
		break;
	case 0x24:
        pclog("OPTi 283: dev->regs[%02x] = %02x\n", dev->index, val);
		dev->regs[dev->index] = val;

        switch(dev->index){
            case 0x10:
            cpu_update_waitstates();
            break;

            case 0x11:
            case 0x12:
            case 0x13:
            opti283_shadow_recalc(dev);
            break;
        }
		break;
    }
}


static uint8_t
opti283_read(uint16_t addr, void *priv)
{
    uint8_t ret = 0xff;
    opti283_t *dev = (opti283_t *) priv;

    switch (addr) {
	case 0x24:
		ret = dev->regs[dev->index];
		break;
    }

    return ret;
}


static void
opti283_close(void *priv)
{
    opti283_t *dev = (opti283_t *) priv;

    free(dev);
}


static void *
opti283_init(const device_t *info)
{
    opti283_t *dev = (opti283_t *) malloc(sizeof(opti283_t));
    memset(dev, 0, sizeof(opti283_t));

    io_sethandler(0x022, 0x0001, opti283_read, NULL, NULL, opti283_write, NULL, NULL, dev);
    io_sethandler(0x024, 0x0001, opti283_read, NULL, NULL, opti283_write, NULL, NULL, dev);

    dev->regs[0x10] = 0x3f;
    dev->regs[0x11] = 0xf0;
    opti283_shadow_recalc(dev);
    
    return dev;
}


const device_t opti283_device = {
    "OPTi 82C283",
    0,
    0,
    opti283_init, opti283_close, NULL,
    NULL, NULL, NULL,
    NULL
};
