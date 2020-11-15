/*
 * 86Box	A hypervisor and IBM PC system emulator that specializes in
 *		running old operating systems and software designed for IBM
 *		PC systems and compatibles from 1981 through fairly recent
 *		system designs based on the PCI bus.
 *
 *		This file is part of the 86Box distribution.
 *
 *		Implementation of the UMC 491/493 chipset.
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
#include <86box/mem.h>
#include <86box/port_92.h>
#include <86box/chipset.h>

#ifdef ENABLE_UMC491_LOG
int ali1429_do_log = ENABLE_UMC491_LOG;
static void
umc491_log(const char *fmt, ...)
{
    va_list ap;

    if (umc491_do_log) {
	va_start(ap, fmt);
	pclog_ex(fmt, ap);
	va_end(ap);
    }
}
#else
#define umc491_log(fmt, ...)
#endif

typedef struct
{
    uint8_t	index,
	regs[256];
} umc491_t;

static void umc491_shadow_recalc(umc491_t *dev)
{

shadowbios = (dev->regs[0xcc] & 0x40);
shadowbios_write = (dev->regs[0xcc] & 0x80);

mem_set_mem_state_both(0xc0000, 0x4000, ((dev->regs[0xcd] & 0x40) ? MEM_READ_INTERNAL : MEM_READ_EXTANY) | ((dev->regs[0xcd] & 0x80) ? MEM_WRITE_INTERNAL : MEM_WRITE_EXTANY));
mem_set_mem_state_both(0xc4000, 0x4000, ((dev->regs[0xcd] & 0x10) ? MEM_READ_INTERNAL : MEM_READ_EXTANY) | ((dev->regs[0xcd] & 0x20) ? MEM_WRITE_INTERNAL : MEM_WRITE_EXTANY));
mem_set_mem_state_both(0xc8000, 0x4000, ((dev->regs[0xcd] & 0x04) ? MEM_READ_INTERNAL : MEM_READ_EXTANY) | ((dev->regs[0xcd] & 0x08) ? MEM_WRITE_INTERNAL : MEM_WRITE_EXTANY));
mem_set_mem_state_both(0xcc000, 0x4000, ((dev->regs[0xcd] & 0x01) ? MEM_READ_INTERNAL : MEM_READ_EXTANY) | ((dev->regs[0xcd] & 0x02) ? MEM_WRITE_INTERNAL : MEM_WRITE_EXTANY));

mem_set_mem_state_both(0xd0000, 0x4000, ((dev->regs[0xce] & 0x40) ? MEM_READ_INTERNAL : MEM_READ_EXTANY) | ((dev->regs[0xce] & 0x80) ? MEM_WRITE_INTERNAL : MEM_WRITE_EXTANY));
mem_set_mem_state_both(0xd4000, 0x4000, ((dev->regs[0xce] & 0x10) ? MEM_READ_INTERNAL : MEM_READ_EXTANY) | ((dev->regs[0xce] & 0x20) ? MEM_WRITE_INTERNAL : MEM_WRITE_EXTANY));
mem_set_mem_state_both(0xd8000, 0x4000, ((dev->regs[0xce] & 0x04) ? MEM_READ_INTERNAL : MEM_READ_EXTANY) | ((dev->regs[0xce] & 0x08) ? MEM_WRITE_INTERNAL : MEM_WRITE_EXTANY));
mem_set_mem_state_both(0xdc000, 0x4000, ((dev->regs[0xce] & 0x01) ? MEM_READ_INTERNAL : MEM_READ_EXTANY) | ((dev->regs[0xce] & 0x02) ? MEM_WRITE_INTERNAL : MEM_WRITE_EXTANY));

/*
Our machine has the E segment into parts although most AMI machines treat it as one.
Probably a flaw by the BIOS as only one register gets enabled for it anyways.
*/
mem_set_mem_state_both(0xe0000, 0x10000, ((dev->regs[0xcc] & 0x10) ? MEM_READ_INTERNAL : MEM_READ_EXTANY) | ((dev->regs[0xcc] & 0x20) ? MEM_WRITE_INTERNAL : MEM_WRITE_EXTANY));

mem_set_mem_state_both(0xf0000, 0x10000, ((dev->regs[0xcc] & 0x40) ? MEM_READ_INTERNAL : MEM_READ_EXTANY) | ((dev->regs[0xcc] & 0x80) ? MEM_WRITE_INTERNAL : MEM_WRITE_EXTANY));

flushmmucache();
}

static void
umc491_write(uint16_t addr, uint8_t val, void *priv)
{
    umc491_t *dev = (umc491_t *) priv;

    switch (addr) {
	case 0x8022:
		dev->index = val;
		break;
	case 0x8024:
        umc491_log("UMC 491: dev->regs[%02x] = %02x\n", dev->index, val);
		dev->regs[dev->index] = val;

        switch(dev->index)
        {
            case 0xcc:
            case 0xcd:
            case 0xce:
            umc491_shadow_recalc(dev);
            break;

            case 0xd0:
            cpu_update_waitstates();
            break;

            case 0xd1:
            cpu_cache_ext_enabled = (val & 0x01);
            break;
        }
		break;
    }
}


static uint8_t
umc491_read(uint16_t addr, void *priv)
{
    uint8_t ret = 0xff;
    umc491_t *dev = (umc491_t *) priv;

    switch (addr) {
	case 0x8024:
		ret = dev->regs[dev->index];
		break;
    }

    return ret;
}


static void
umc491_close(void *priv)
{
    umc491_t *dev = (umc491_t *) priv;

    free(dev);
}


static void *
umc491_init(const device_t *info)
{
    umc491_t *dev = (umc491_t *) malloc(sizeof(umc491_t));
    memset(dev, 0, sizeof(umc491_t));

    device_add(&port_92_device);

/*

UMC 491/493 Ports

8022h Index Port
8024h Data Port

*/
    io_sethandler(0x8022, 0x0001, umc491_read, NULL, NULL, umc491_write, NULL, NULL, dev);
    io_sethandler(0x8024, 0x0001, umc491_read, NULL, NULL, umc491_write, NULL, NULL, dev);
    
    dev->regs[0xcc] = 0x00;
    dev->regs[0xcd] = 0x00;
    dev->regs[0xce] = 0x00;
    umc491_shadow_recalc(dev);

    return dev;
}


const device_t umc491_device = {
    "UMC 491/493",
    0,
    0,
    umc491_init, umc491_close, NULL,
    { NULL }, NULL, NULL,
    NULL
};
