/*
 * 86Box	A hypervisor and IBM PC system emulator that specializes in
 *		running old operating systems and software designed for IBM
 *		PC systems and compatibles from 1981 through fairly recent
 *		system designs based on the PCI bus.
 *
 *		This file is part of the 86Box distribution.
 *
 *		Implementation of the VIA VT82C49X chipset.
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
} vt82c49x_t;

#ifdef ENABLE_VT82C49X_LOG
int vt82c49x_do_log = ENABLE_VT82C49X_LOG;
static void
vt82c49x_log(const char *fmt, ...)
{
    va_list ap;

    if (vt82c49x_do_log) {
	va_start(ap, fmt);
	pclog_ex(fmt, ap);
	va_end(ap);
    }
}
#else
#define vt82c49x_log(fmt, ...)
#endif

static void vt82c49x_shadow_recalc(vt82c49x_t *dev)
{

uint32_t wp_c, wp_e, wp_f;

/* Register 40h */
wp_c = (dev->regs[0x40] & 0x80) ? MEM_WRITE_EXTANY : MEM_WRITE_INTERNAL;
wp_f = (dev->regs[0x40] & 0x40) ? MEM_WRITE_EXTANY : MEM_WRITE_INTERNAL;
wp_e = (dev->regs[0x40] & 0x20) ? MEM_WRITE_EXTANY : MEM_WRITE_INTERNAL;

/* Register 30h */
mem_set_mem_state_both(0xc0000, 0x4000, ((dev->regs[0x30] & 0x02) ? MEM_READ_INTERNAL : MEM_READ_EXTANY) | ((dev->regs[0x30] & 0x01) ? wp_c : MEM_WRITE_EXTANY));
mem_set_mem_state_both(0xc4000, 0x4000, ((dev->regs[0x30] & 0x08) ? MEM_READ_INTERNAL : MEM_READ_EXTANY) | ((dev->regs[0x30] & 0x04) ? wp_c : MEM_WRITE_EXTANY));
mem_set_mem_state_both(0xc8000, 0x4000, ((dev->regs[0x30] & 0x20) ? MEM_READ_INTERNAL : MEM_READ_EXTANY) | ((dev->regs[0x30] & 0x10) ? MEM_WRITE_INTERNAL : MEM_WRITE_EXTANY));
mem_set_mem_state_both(0xcc000, 0x4000, ((dev->regs[0x30] & 0x80) ? MEM_READ_INTERNAL : MEM_READ_EXTANY) | ((dev->regs[0x30] & 0x40) ? MEM_WRITE_INTERNAL : MEM_WRITE_EXTANY));

/* Register 31h */
mem_set_mem_state_both(0xd0000, 0x4000, ((dev->regs[0x31] & 0x02) ? MEM_READ_INTERNAL : MEM_READ_EXTANY) | ((dev->regs[0x31] & 0x01) ? MEM_WRITE_INTERNAL : MEM_WRITE_EXTANY));
mem_set_mem_state_both(0xd4000, 0x4000, ((dev->regs[0x31] & 0x08) ? MEM_READ_INTERNAL : MEM_READ_EXTANY) | ((dev->regs[0x31] & 0x04) ? MEM_WRITE_INTERNAL : MEM_WRITE_EXTANY));
mem_set_mem_state_both(0xd8000, 0x4000, ((dev->regs[0x31] & 0x20) ? MEM_READ_INTERNAL : MEM_READ_EXTANY) | ((dev->regs[0x31] & 0x10) ? MEM_WRITE_INTERNAL : MEM_WRITE_EXTANY));
mem_set_mem_state_both(0xdc000, 0x4000, ((dev->regs[0x31] & 0x80) ? MEM_READ_INTERNAL : MEM_READ_EXTANY) | ((dev->regs[0x31] & 0x40) ? MEM_WRITE_INTERNAL : MEM_WRITE_EXTANY));

/* Register 32h */
shadowbios = (dev->regs[0x40] & 0x20);
shadowbios_write = (dev->regs[0x40] & 0x10);
mem_set_mem_state_both(0xe0000, 0x10000, ((dev->regs[0x32] & 0x80) ? MEM_READ_INTERNAL : MEM_READ_EXTANY) | ((dev->regs[0x32] & 0x40) ? wp_e : MEM_WRITE_EXTANY));
mem_set_mem_state_both(0xf0000, 0x10000, ((dev->regs[0x32] & 0x20) ? MEM_READ_INTERNAL : MEM_READ_EXTANY) | ((dev->regs[0x32] & 0x10) ? wp_f : MEM_WRITE_EXTANY));

}

static void
vt82c49x_write(uint16_t addr, uint8_t val, void *priv)
{
    vt82c49x_t *dev = (vt82c49x_t *) priv;

    switch (addr) {
	case 0xa8:
		dev->index = val;
		break;

	case 0xa9:
		dev->regs[dev->index] = val;

        vt82c49x_log("dev->regs[0x%02x] = %02x\n", dev->index, val);

        switch(dev->index){
            /* Wait States */
            case 0x03:
            cpu_update_waitstates();
            break;

            /* Shadow RAM */
            case 0x30:
            case 0x31:
            case 0x32:
            case 0x40:
            vt82c49x_shadow_recalc(dev);
            break;

            /* External Cache Enable(Based on the 486-VC-HD BIOS) */
            case 0x50:
            cpu_cache_ext_enabled = (val & 0x84);
            break;

            /* SMI/SMM(Not at all perfect or even functional :/) */
            case 0x5b:

            if(val & 0x40)
            mem_set_mem_state_smram(1, 0x30000, 0x20000, 0);

            if(val & 0x20)
            smi_line = 1;

            break;
        }
		break;
    }
}


static uint8_t
vt82c49x_read(uint16_t addr, void *priv)
{
    uint8_t ret = 0xff;
    vt82c49x_t *dev = (vt82c49x_t *) priv;

    switch (addr) {
	case 0xa9:
		ret = dev->regs[dev->index];
		break;
    }

    return ret;
}


static void
vt82c49x_close(void *priv)
{
    vt82c49x_t *dev = (vt82c49x_t *) priv;

    free(dev);
}


static void *
vt82c49x_init(const device_t *info)
{
    vt82c49x_t *dev = (vt82c49x_t *) malloc(sizeof(vt82c49x_t));
    memset(dev, 0, sizeof(vt82c49x_t));

    device_add(&port_92_device);

    io_sethandler(0x0a8, 0x0001, vt82c49x_read, NULL, NULL, vt82c49x_write, NULL, NULL, dev);
    io_sethandler(0x0a9, 0x0001, vt82c49x_read, NULL, NULL, vt82c49x_write, NULL, NULL, dev);

    dev->regs[0x30] = 0x00;
    dev->regs[0x31] = 0x00;
    dev->regs[0x32] = 0x00;
    vt82c49x_shadow_recalc(dev);
    
    return dev;
}


const device_t via_vt82c49x_device = {
    "VIA VT82C49X",
    0,
    0,
    vt82c49x_init, vt82c49x_close, NULL,
    NULL, NULL, NULL,
    NULL
};
