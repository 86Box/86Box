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
 * Version:	@(#)acc2168.c	1.0.0	2019/05/13
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
#include "../86box.h"
#include "../cpu/cpu.h"
#include "../timer.h"
#include "../device.h"
#include "../keyboard.h"
#include "../io.h"
#include "../mem.h"
#include "../mouse.h"
#include "../port_92.h"
#include "../sio.h"
#include "../disk/hdc.h"
#include "../video/video.h"
#include "../video/vid_ht216.h"
#include "chipset.h"


typedef struct acc2168_t
{
    int reg_idx;
    uint8_t regs[256];
    uint8_t port_78;
} acc2168_t;


static void 
acc2168_shadow_recalc(acc2168_t *dev)
{
    if (dev->regs[0x02] & 8) {
	switch (dev->regs[0x02] & 0x30) {
		case 0x00:
			mem_set_mem_state(0xf0000, 0x10000, MEM_READ_EXTERNAL | MEM_WRITE_INTERNAL);
			break;
		case 0x10:
			mem_set_mem_state(0xf0000, 0x10000, MEM_READ_INTERNAL | MEM_WRITE_INTERNAL);
			break;
		case 0x20:
			mem_set_mem_state(0xf0000, 0x10000, MEM_READ_EXTERNAL | MEM_WRITE_EXTERNAL);
			break;
		case 0x30:
			mem_set_mem_state(0xf0000, 0x10000, MEM_READ_INTERNAL | MEM_WRITE_EXTERNAL);
			break;
	}
    } else
	mem_set_mem_state(0xf0000, 0x10000, MEM_READ_EXTERNAL | MEM_WRITE_EXTERNAL);

    if (dev->regs[0x02] & 4) {
	switch (dev->regs[0x02] & 0x30) {
		case 0x00:
			mem_set_mem_state(0xe0000, 0x10000, MEM_READ_EXTERNAL | MEM_WRITE_INTERNAL);
			break;
		case 0x10:
			mem_set_mem_state(0xe0000, 0x10000, MEM_READ_INTERNAL | MEM_WRITE_INTERNAL);
			break;
		case 0x20:
			mem_set_mem_state(0xe0000, 0x10000, MEM_READ_EXTERNAL | MEM_WRITE_EXTERNAL);
			break;
		case 0x30:
			mem_set_mem_state(0xe0000, 0x10000, MEM_READ_INTERNAL | MEM_WRITE_EXTERNAL);
			break;
	}
    } else
	mem_set_mem_state(0xe0000, 0x10000, MEM_READ_EXTERNAL | MEM_WRITE_EXTERNAL);
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
