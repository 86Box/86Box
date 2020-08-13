/*
 * 86Box	A hypervisor and IBM PC system emulator that specializes in
 *		running old operating systems and software designed for IBM
 *		PC systems and compatibles from 1981 through fairly recent
 *		system designs based on the PCI bus.
 *
 *		This file is part of the 86Box distribution.
 *
 *		Implementation of the ACC 2042/2168 chipset 
 *
 *
 *
 *
 * Authors:	Sarah Walker, <http://pcem-emulator.co.uk/>
 *          Tiseno100
 * 
 *		Copyright 2019 Sarah Walker.
 *      Copyright 2020 Tiseno100.
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

#define enabled_shadow (MEM_READ_INTERNAL | can_write)
#define disabled_shadow (MEM_READ_EXTANY | MEM_WRITE_EXTANY)

#ifdef ENABLE_ACC2168_LOG
int acc2168_do_log = ENABLE_ACC2168_LOG;
static void
acc2168_log(const char *fmt, ...)
{
    va_list ap;

    if (acc2168_do_log) {
	va_start(ap, fmt);
	pclog_ex(fmt, ap);
	va_end(ap);
    }
}
#else
#define acc2168_log(fmt, ...)
#endif

typedef struct acc2168_t
{
    int reg_idx;
    uint8_t regs[256], port_78;
} acc2168_t;

static void 
acc2168_shadow_recalc(acc2168_t *dev)
{

uint32_t can_write;

can_write = !(dev->regs[0x02] & 0x20) ? MEM_WRITE_INTERNAL : MEM_WRITE_EXTANY;

mem_set_mem_state_both(0xc0000, 0x8000, ((dev->regs[0x02] & 0x01) ? enabled_shadow : disabled_shadow));
mem_set_mem_state_both(0xc8000, 0x8000, ((dev->regs[0x02] & 0x02) ? enabled_shadow : disabled_shadow));
mem_set_mem_state_both(0xd0000, 0x10000, ((dev->regs[0x02] & 0x04) ? enabled_shadow : disabled_shadow));
mem_set_mem_state_both(0xe0000, 0x10000, ((dev->regs[0x02] & 0x08) ? enabled_shadow : disabled_shadow));
mem_set_mem_state_both(0xf0000, 0x10000, ((dev->regs[0x02] & 0x10) ? enabled_shadow : disabled_shadow));

}


static void 
acc2168_write(uint16_t addr, uint8_t val, void *p)
{
    acc2168_t *dev = (acc2168_t *)p;

    switch(addr){
    case 0xf2:
	dev->reg_idx = val;
    break;

    case 0xf3:
    acc2168_log("dev->regs[%02x] = %02x", dev->reg_idx, val);
	dev->regs[dev->reg_idx] = val;

	switch (dev->reg_idx) {
		case 0x02:
			acc2168_shadow_recalc(dev);
			break;

        case 0x1a:
            cpu_cache_int_enabled = !(val & 0x40);
            break;
	}
    }
}


static uint8_t 
acc2168_read(uint16_t addr, void *p)
{
    acc2168_t *dev = (acc2168_t *)p;

    return (addr == 0xf2) ? dev->reg_idx : dev->regs[dev->reg_idx];
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
	
    io_sethandler(0x00f2, 0x0002, acc2168_read, NULL, NULL, acc2168_write, NULL, NULL, dev);
    io_sethandler(0x00f3, 0x0002, acc2168_read, NULL, NULL, acc2168_write, NULL, NULL, dev);

    /* Port 78 must be moved on the SIO's */
    io_sethandler(0x0078, 0x0001, acc2168_port_78_read, NULL, NULL, NULL, NULL, NULL, dev);	

    device_add(&port_92_inv_device);

    if (gfxcard != VID_INTERNAL)
	dev->port_78 = 0x40;
    else
	dev->port_78 = 0;

    dev->regs[0x02] = 0x00;
    dev->regs[0x1c] = 0x04;
    acc2168_shadow_recalc(dev);

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
