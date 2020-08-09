/*
 * 86Box	A hypervisor and IBM PC system emulator that specializes in
 *		running old operating systems and software designed for IBM
 *		PC systems and compatibles from 1981 through fairly recent
 *		system designs based on the PCI bus.
 *
 *		This file is part of the 86Box distribution.
 *
 *		Implementation of the ALi M1429 chipset.
 *
 *      Note: This chipset has no datasheet, everything were done via
 *      reverse engineering the BIOS of various machines using it.
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

#ifdef ENABLE_ALI1429_LOG
int ali1429_do_log = ENABLE_ALI1429_LOG;
static void
ali1429_log(const char *fmt, ...)
{
    va_list ap;

    if (ali1429_do_log) {
	va_start(ap, fmt);
	pclog_ex(fmt, ap);
	va_end(ap);
    }
}
#else
#define ali1429_log(fmt, ...)
#endif


typedef struct
{
    uint8_t	index, cfg_locked,
	regs[256];
} ali1429_t;

static void ali1429_shadow_recalc(ali1429_t *dev)
{

uint32_t base, i, can_write, can_read;

shadowbios = (dev->regs[0x13] & 0x40) && (dev->regs[0x14] & 0x01);
shadowbios_write = (dev->regs[0x13] & 0x40) && (dev->regs[0x14] & 0x02);

can_write = (dev->regs[0x14] & 0x02) ? MEM_WRITE_INTERNAL : MEM_WRITE_EXTANY;
can_read = (dev->regs[0x14] & 0x01) ? MEM_READ_INTERNAL : MEM_READ_EXTANY;

for(i = 0; i < 8; i++)
{
base = 0xc0000 + (i << 15);

if(dev->regs[0x13] & (1 << i))
mem_set_mem_state_both(base, 0x8000, can_read | can_write);
else
mem_set_mem_state_both(base, 0x8000, disabled_shadow);

}

}

static void
ali1429_write(uint16_t addr, uint8_t val, void *priv)
{
    ali1429_t *dev = (ali1429_t *) priv;

    switch (addr) {
	case 0x22:
		dev->index = val;
		break;
    
	case 0x23:

        /* Don't log register unlock patterns */
        if(dev->index != 0x03)
        {
        ali1429_log("M1429: dev->regs[%02x] = %02x\n", dev->index, val);
        }
        
		dev->regs[dev->index] = val;

        /* Unlock/Lock Registers */
        dev->cfg_locked = !(dev->regs[0x03] && 0xc5);

        if(dev->cfg_locked == 0)
        {
        switch(dev->index){
            /* Shadow RAM */
            case 0x13:
            case 0x14:
            ali1429_shadow_recalc(dev);
            break;

            /* Cache */
            case 0x18:
            cpu_cache_ext_enabled = (val & 0x80);
            break;
        }
        }

		break;
    }
}


static uint8_t
ali1429_read(uint16_t addr, void *priv)
{
    uint8_t ret = 0xff;
    ali1429_t *dev = (ali1429_t *) priv;

    switch (addr) {
	case 0x23:
        /* Do not conflict with Cyrix configuration registers */
        if(!(((dev->index >= 0xc0) || (dev->index == 0x20)) && cpu_iscyrix))
        ret = dev->regs[dev->index];
        break;
    }

    return ret;
}


static void
ali1429_close(void *priv)
{
    ali1429_t *dev = (ali1429_t *) priv;

    free(dev);
}


static void *
ali1429_init(const device_t *info)
{
    ali1429_t *dev = (ali1429_t *) malloc(sizeof(ali1429_t));
    memset(dev, 0, sizeof(ali1429_t));

    /*
    M1429 Ports:
    22h Index Port
    23h Data Port
    */
    io_sethandler(0x022, 0x0001, ali1429_read, NULL, NULL, ali1429_write, NULL, NULL, dev);
    io_sethandler(0x023, 0x0001, ali1429_read, NULL, NULL, ali1429_write, NULL, NULL, dev);
    
    dev->cfg_locked = 1;

    device_add(&port_92_device);

    dev->regs[0x13] = 0x00;
    dev->regs[0x14] = 0x00;
    ali1429_shadow_recalc(dev);

    return dev;
}


const device_t ali1429_device = {
    "ALi M1429",
    0,
    0,
    ali1429_init, ali1429_close, NULL,
    NULL, NULL, NULL,
    NULL
};
