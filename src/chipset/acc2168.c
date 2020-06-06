/*
 * 86Box	A hypervisor and IBM PC system emulator that specializes in
 *		running old operating systems and software designed for IBM
 *		PC systems and compatibles from 1981 through fairly recent
 *		system designs based on the PCI bus.
 *
 *		This file is part of the 86Box distribution.
 *
 *		Implementation of the ACC 2168 chipset 
 *		used by the Packard Bell Legend 760 Supreme (PB410A or PB430).
 *
 *
 *
 * Authors:	Sarah Walker, <http://pcem-emulator.co.uk/>
 *
 *		Copyright 2019 Sarah Walker.
 */
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>
#include <86box/86box.h>
#include "cpu.h"
#include <86box/timer.h>
#include <86box/device.h>
#include <86box/keyboard.h>
#include <86box/io.h>
#include <86box/mem.h>
#include <86box/mouse.h>
#include <86box/port_92.h>
#include <86box/sio.h>
#include <86box/hdc.h>
#include <86box/video.h>
#include <86box/chipset.h>


typedef struct acc2168_t
{
    int reg_idx;
    uint8_t regs[256];
    uint8_t port_78;
} acc2168_t;


/*
    Based on reverse engineering using the AMI 386DX Clone BIOS:
	Bit 0 of register 02 controls shadowing of C0000-C7FFF (1 = enabled, 0 = disabled);
	Bit 1 of register 02 controls shadowing of C8000-CFFFF (1 = enabled, 0 = disabled);
	Bit 2 of register 02 controls shadowing of D0000-DFFFF (1 = enabled, 0 = disabled);
	Bit 3 of register 02 controls shadowing of E0000-EFFFF (1 = enabled, 0 = disabled);
	Bit 4 of register 02 controls shadowing of F0000-FFFFF (1 = enabled, 0 = disabled);
	Bit 5 is most likely: 1 = shadow enabled, 0 = shadow disabled;
	Bit 6 of register 02 controls shadow RAM cacheability (1 = cacheable, 0 = non-cacheable).
*/

static void 
acc2168_shadow_recalc(acc2168_t *dev)
{
    int state;

    if (dev->regs[0x02] & 0x20)
	state = (dev->regs[0x02] & 0x20) ? (MEM_READ_INTERNAL | MEM_WRITE_INTERNAL) : (MEM_READ_EXTANY | MEM_WRITE_EXTANY);

    mem_set_mem_state(0xc0000, 0x08000, (dev->regs[0x02] & 0x01) ? state : (MEM_READ_EXTANY | MEM_WRITE_EXTANY));
    mem_set_mem_state(0xc8000, 0x08000, (dev->regs[0x02] & 0x02) ? state : (MEM_READ_EXTANY | MEM_WRITE_EXTANY));
    mem_set_mem_state(0xd0000, 0x10000, (dev->regs[0x02] & 0x04) ? state : (MEM_READ_EXTANY | MEM_WRITE_EXTANY));
    mem_set_mem_state(0xe0000, 0x10000, (dev->regs[0x02] & 0x08) ? state : (MEM_READ_EXTANY | MEM_WRITE_EXTANY));
    mem_set_mem_state(0xf0000, 0x10000, (dev->regs[0x02] & 0x10) ? state : (MEM_READ_EXTANY | MEM_WRITE_EXTANY));
}


static void 
acc2168_write(uint16_t addr, uint8_t val, void *p)
{
    acc2168_t *dev = (acc2168_t *)p;

    if (!(addr & 1))
	dev->reg_idx = val;
    else {
	dev->regs[dev->reg_idx] = val;

	switch (dev->reg_idx) {
		case 0x02:
			acc2168_shadow_recalc(dev);
			break;
	}
    }
}


static uint8_t 
acc2168_read(uint16_t addr, void *p)
{
    acc2168_t *dev = (acc2168_t *)p;

   if (!(addr & 1))
	return dev->reg_idx;

    return dev->regs[dev->reg_idx];
}


/*
    Bit 7 = Super I/O chip: 1 = enabled, 0 = disabled;
    Bit 6 = Graphics card: 1 = standalone, 0 = on-board;
    Bit 5 = ???? (if 1, siren and hangs).
*/
static uint8_t 
acc2168_port_78_read(uint16_t addr, void *p)
{
    acc2168_t *dev = (acc2168_t *)p;

    return dev->port_78;
}


static void
acc2168_close(void *priv)
{
    acc2168_t *dev = (acc2168_t *) priv;

    free(dev);
}


static void *
acc2168_init(const device_t *info)
{
    acc2168_t *dev = (acc2168_t *)malloc(sizeof(acc2168_t));
    memset(dev, 0, sizeof(acc2168_t));
	
    io_sethandler(0x00f2, 0x0002,
		  acc2168_read, NULL, NULL, acc2168_write, NULL, NULL, dev);	
    io_sethandler(0x0078, 0x0001,
		  acc2168_port_78_read, NULL, NULL, NULL, NULL, NULL, dev);	

    device_add(&port_92_inv_device);

    if (gfxcard != VID_INTERNAL)
	dev->port_78 = 0x40;
    else
	dev->port_78 = 0;

    return dev;
}


const device_t acc2168_device = {
    "ACC 2168",
    0,
    0,
    acc2168_init, acc2168_close, NULL,
    NULL, NULL, NULL,
    NULL
};
