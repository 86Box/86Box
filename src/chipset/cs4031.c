/*
 * 86Box	A hypervisor and IBM PC system emulator that specializes in
 *		running old operating systems and software designed for IBM
 *		PC systems and compatibles from 1981 through fairly recent
 *		system designs based on the PCI bus.
 *
 *		This file is part of the 86Box distribution.
 *
 *		Implementation of the Chips & Technologies CS4031 chipset.
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


typedef struct
{
    uint8_t	index,
	regs[256];
    port_92_t * port_92;
} cs4031_t;

#ifdef ENABLE_CS4031_LOG
int cs4031_do_log = ENABLE_CS4031_LOG;
static void
cs4031_log(const char *fmt, ...)
{
    va_list ap;

    if (cs4031_do_log) {
	va_start(ap, fmt);
	pclog_ex(fmt, ap);
	va_end(ap);
    }
}
#else
#define cs4031_log(fmt, ...)
#endif

static void cs4031_shadow_recalc(cs4031_t *dev)
{

uint32_t romc0000, romc4000, romc8000, romcc000, romd0000, rome0000, romf0000;

/* Register 18h */
if(dev->regs[0x18] & 0x01)
mem_set_mem_state_both(0xa0000, 0x10000, MEM_READ_INTERNAL | MEM_WRITE_INTERNAL);
else
mem_set_mem_state_both(0xa0000, 0x10000, MEM_READ_EXTANY | MEM_WRITE_EXTANY);

if(dev->regs[0x18] & 0x02)
mem_set_mem_state_both(0xb0000, 0x10000, MEM_READ_INTERNAL | MEM_WRITE_INTERNAL);
else
mem_set_mem_state_both(0xb0000, 0x10000, MEM_READ_EXTANY | MEM_WRITE_EXTANY);


/* Register 19h-1ah-1bh*/

shadowbios = (dev->regs[0x19] & 0x40);
shadowbios_write = (dev->regs[0x1a] & 0x40);

/* ROMCS only functions if shadow write is disabled */
romc0000 = ((dev->regs[0x1b] & 0x80) && (dev->regs[0x1b] & 0x01)) ? MEM_WRITE_DISABLED : MEM_WRITE_EXTANY;
romc4000 = ((dev->regs[0x1b] & 0x80) && (dev->regs[0x1b] & 0x02)) ? MEM_WRITE_DISABLED : MEM_WRITE_EXTANY;
romc8000 = ((dev->regs[0x1b] & 0x80) && (dev->regs[0x1b] & 0x04)) ? MEM_WRITE_DISABLED : MEM_WRITE_EXTANY;
romcc000 = ((dev->regs[0x1b] & 0x80) && (dev->regs[0x1b] & 0x08)) ? MEM_WRITE_DISABLED : MEM_WRITE_EXTANY;
romd0000 = ((dev->regs[0x1b] & 0x80) && (dev->regs[0x1b] & 0x10)) ? MEM_WRITE_DISABLED : MEM_WRITE_EXTANY;
rome0000 = ((dev->regs[0x1b] & 0x80) && (dev->regs[0x1b] & 0x20)) ? MEM_WRITE_DISABLED : MEM_WRITE_EXTANY;
romf0000 = ((dev->regs[0x1b] & 0x80) && (dev->regs[0x1b] & 0x40)) ? MEM_WRITE_DISABLED : MEM_WRITE_EXTANY;


mem_set_mem_state_both(0xc0000, 0x4000, ((dev->regs[0x19] & 0x01) ? MEM_READ_INTERNAL : MEM_READ_EXTANY) | ((dev->regs[0x1a] & 0x01) ? MEM_WRITE_INTERNAL : romc0000));
mem_set_mem_state_both(0xc4000, 0x4000, ((dev->regs[0x19] & 0x02) ? MEM_READ_INTERNAL : MEM_READ_EXTANY) | ((dev->regs[0x1a] & 0x02) ? MEM_WRITE_INTERNAL : romc4000));
mem_set_mem_state_both(0xc8000, 0x4000, ((dev->regs[0x19] & 0x04) ? MEM_READ_INTERNAL : MEM_READ_EXTANY) | ((dev->regs[0x1a] & 0x04) ? MEM_WRITE_INTERNAL : romc8000));
mem_set_mem_state_both(0xcc000, 0x4000, ((dev->regs[0x19] & 0x08) ? MEM_READ_INTERNAL : MEM_READ_EXTANY) | ((dev->regs[0x1a] & 0x08) ? MEM_WRITE_INTERNAL : romcc000));
mem_set_mem_state_both(0xd0000, 0x10000, ((dev->regs[0x19] & 0x10) ? MEM_READ_INTERNAL : MEM_READ_EXTANY) | ((dev->regs[0x1a] & 0x10) ? MEM_WRITE_INTERNAL : romd0000));
mem_set_mem_state_both(0xe0000, 0x10000, ((dev->regs[0x19] & 0x20) ? MEM_READ_INTERNAL : MEM_READ_EXTANY) | ((dev->regs[0x1a] & 0x20) ? MEM_WRITE_INTERNAL : rome0000));
mem_set_mem_state_both(0xf0000, 0x10000, ((dev->regs[0x19] & 0x40) ? MEM_READ_INTERNAL : MEM_READ_EXTANY) | ((dev->regs[0x1a] & 0x40) ? MEM_WRITE_INTERNAL : romf0000));


}

static void
cs4031_write(uint16_t addr, uint8_t val, void *priv)
{
    cs4031_t *dev = (cs4031_t *) priv;

    switch (addr) {
	case 0x22:
		dev->index = val;
		break;
	case 0x23:
        cs4031_log("CS4031: dev->regs[%02x] = %02x\n", dev->index, val);
		dev->regs[dev->index] = val;

        switch(dev->index){
            case 0x06:
            cpu_update_waitstates();
            break;

            case 0x18:
            case 0x19:
            case 0x1a:
            case 0x1b:
            cs4031_shadow_recalc(dev);
            break;

            case 0x1c:

            if(dev->regs[0x1c] & 0x20)
            port_92_add(dev->port_92);
            else
            port_92_remove(dev->port_92);

            break;
            
        }
		break;
    }
}


static uint8_t
cs4031_read(uint16_t addr, void *priv)
{
    uint8_t ret = 0xff;
    cs4031_t *dev = (cs4031_t *) priv;

    switch (addr) {
	case 0x23:
		ret = dev->regs[dev->index];
		break;
    }

    return ret;
}


static void
cs4031_close(void *priv)
{
    cs4031_t *dev = (cs4031_t *) priv;

    free(dev);
}


static void *
cs4031_init(const device_t *info)
{
    cs4031_t *dev = (cs4031_t *) malloc(sizeof(cs4031_t));
    memset(dev, 0, sizeof(cs4031_t));

    dev->port_92 = device_add(&port_92_device);

    io_sethandler(0x022, 0x0001, cs4031_read, NULL, NULL, cs4031_write, NULL, NULL, dev);
    io_sethandler(0x023, 0x0001, cs4031_read, NULL, NULL, cs4031_write, NULL, NULL, dev);

    dev->regs[0x05] = 0x05;
    dev->regs[0x18] = 0x00;
    dev->regs[0x19] = 0x00;
    dev->regs[0x1a] = 0x00;
    dev->regs[0x1b] = 0x60;
    cs4031_shadow_recalc(dev);
    
    return dev;
}


const device_t cs4031_device = {
    "Chips & Technogies CS4031",
    0,
    0,
    cs4031_init, cs4031_close, NULL,
    NULL, NULL, NULL,
    NULL
};
