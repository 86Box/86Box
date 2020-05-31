/*
 * 86Box	A hypervisor and IBM PC system emulator that specializes in
 *		running old operating systems and software designed for IBM
 *		PC systems and compatibles from 1981 through fairly recent
 *		system designs based on the PCI bus.
 *
 *		This file is part of the 86Box distribution.
 *
 *		Basic implementation of the OPTi Python Chipset.
 *
 *		Emulator maintained by: Miran Grca, <mgrca8@gmail.com>
 *
 *
 *
 *
 *		Copyright 2020 Tiseno100.
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
#include <86box/chipset.h>


static void
python_shadow_recalc(uint32_t addr, uint32_t size, int state)
{
    switch (state & 3) {
	case 0:
		mem_set_mem_state(addr, size, MEM_READ_EXTANY | MEM_WRITE_EXTANY);
		break;
	case 1:
		mem_set_mem_state(addr, size, MEM_READ_EXTANY | MEM_WRITE_INTERNAL);
		break;
	case 2:
		mem_set_mem_state(addr, size, MEM_READ_INTERNAL | MEM_WRITE_EXTANY);
		break;
	case 3:
		mem_set_mem_state(addr, size, MEM_READ_INTERNAL | MEM_WRITE_INTERNAL);
		break;
    }

    flushmmucache_nopc();
}


typedef struct
{
    uint8_t	cur_reg,
		regs[256];
} python_t;

/*
static void
python_recalcmapping(python_t *dev) [TODO: Use the current Shadow RAM implementation]
{
    uint32_t base;
    uint32_t i, shflags = 0;
    uint32_t reg, lowest_bit;

    shadowbios = 0;
    shadowbios_write = 0;

    for (i = 0; i < 8; i++) {
    base = 0xc0000 + (i << 14);

    lowest_bit = (i << 2) & 0x07;
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
*/

static void
python_write(uint16_t addr, uint8_t val, void *priv)
{

    python_t *dev = (python_t *) priv;

switch(addr){
    
    //Setting the data to be applied on 24h.
    case 0x22:
    dev->cur_reg = val;
    break;

    case 0x24:
    
    pclog("python: Writing %d on Register %dh\n", val, dev->cur_reg);

    switch (dev->cur_reg){

        case 0x04: // Shadow RAM control 1
		if ((dev->regs[0x04] ^ val) & 0x03)
			python_shadow_recalc(0xc0000, 0x04000, val & 0x03);
		if ((dev->regs[0x04] ^ val) & 0x0c)
			python_shadow_recalc(0xc4000, 0x04000, (val & 0x0c) >> 2);
		if ((dev->regs[0x04] ^ val) & 0x30)
			python_shadow_recalc(0xc8000, 0x04000, (val & 0x30) >> 4);
		if ((dev->regs[0x04] ^ val) & 0xc0)
			python_shadow_recalc(0xcc000, 0x04000, (val & 0xc0) >> 6);
		dev->regs[0x04] = val;
		return;
        break;

	    case 0x05: // Shadow RAM Control 2
		if ((dev->regs[0x05] ^ val) & 0x03)
			python_shadow_recalc(0xd0000, 0x04000, val & 0x03);
		if ((dev->regs[0x05] ^ val) & 0x0c)
			python_shadow_recalc(0xd4000, 0x04000, (val & 0x0c) >> 2);
		if ((dev->regs[0x05] ^ val) & 0x30)
			python_shadow_recalc(0xd8000, 0x04000, (val & 0x30) >> 4);
		if ((dev->regs[0x05] ^ val) & 0xc0)
			python_shadow_recalc(0xdc000, 0x04000, (val & 0xc0) >> 6);
		dev->regs[0x05] = val;
		return;
        break;

	    case 0x06: // Shadow RAM Control 3
		if ((dev->regs[0x06] ^ val) & 0x30) {
			python_shadow_recalc(0xf0000, 0x10000, (val & 0x30) >> 4);
			shadowbios = (((val & 0x30) >> 4) & 0x02);
		}
		if ((dev->regs[0x06] ^ val) & 0xc0)
			python_shadow_recalc(0xe0000, 0x10000, (val & 0xc0) >> 6);
		dev->regs[0x06] = val;
		return;
        break;
        
		/*
        case 0x0d: // ROMCS Register [Let's have it disabled as it fails the Award board]
            if (!(val & 0x80))
					mem_set_mem_state(0xe0000, 0x10000, MEM_READ_INTERNAL | MEM_WRITE_DISABLED);
				else
					mem_set_mem_state(0xe0000, 0x10000, MEM_READ_EXTANY | MEM_WRITE_INTERNAL);
        break;
		*/

    default:
        dev->regs[addr] = val;
    break;
    }

}

}

static uint8_t
python_read(uint16_t addr, void *priv)
{
    uint8_t ret = 0xff;
    python_t *dev = (python_t *) priv;

    if(addr == 0x24){
		ret = dev->regs[dev->cur_reg - 0x20];
    }

    return ret;
}

static void
python_close(void *priv)
{

python_t *dev = (python_t *) priv;
free(dev);

}

static void *
python_init(const device_t *info)
{
    python_t *dev = (python_t *) malloc(sizeof(python_t));
    memset(dev, 0, sizeof(python_t));

    //Access Register which writes to 24h instantly
    io_sethandler(0x0022, 0x0001, python_read, NULL, NULL, python_write, NULL, NULL, dev);

    //R/W register data
    io_sethandler(0x0024, 0x0001, python_read, NULL, NULL, python_write, NULL, NULL, dev);

    //TODO: Understand it more specifically
    dev->regs[0x61] = 0x80;

    return dev;
}

const device_t python_device = {
    "OPTi Python",
    0,
    0,
    python_init, python_close, NULL,
    NULL, NULL, NULL,
    NULL
};