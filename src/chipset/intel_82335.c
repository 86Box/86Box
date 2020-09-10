/*
 * 86Box	A hypervisor and IBM PC system emulator that specializes in
 *		running old operating systems and software designed for IBM
 *		PC systems and compatibles from 1981 through fairly recent
 *		system designs based on the PCI bus.
 *
 *		This file is part of the 86Box distribution.
 *
 *		Implementation of the Intel 82335(KU82335) chipset.
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
#define rw_shadow (MEM_READ_INTERNAL | MEM_WRITE_INTERNAL)
#define ro_shadow (MEM_READ_INTERNAL | MEM_WRITE_DISABLED)

#define extended_granuality_enabled (dev->regs[0x2c] & 0x01)
#define determine_video_ram_write_access ((dev->regs[0x22] & (0x08 << 8)) ? rw_shadow : ro_shadow)

typedef struct
{

    uint16_t regs[256],
    
    cfg_locked;

} intel_82335_t;

#ifdef ENABLE_INTEL_82335_LOG
int intel_82335_do_log = ENABLE_INTEL_82335_LOG;
static void
intel_82335_log(const char *fmt, ...)
{
    va_list ap;

    if (intel_82335_do_log) {
	va_start(ap, fmt);
	pclog_ex(fmt, ap);
	va_end(ap);
    }
}
#else
#define intel_82335_log(fmt, ...)
#endif

static void
intel_82335_write(uint16_t addr, uint16_t val, void *priv)
{
    intel_82335_t *dev = (intel_82335_t *) priv;
    uint32_t base, i;

    dev->regs[addr] = val;

    dev->cfg_locked = (dev->regs[0x22] & (0x80 << 8));

    if(!dev->cfg_locked)
    {

    intel_82335_log("Register %02x: Write %04x\n", addr, val);

    switch (addr) {
	case 0x22:
    if (!extended_granuality_enabled)
    {
    mem_set_mem_state_both(0xa0000, 0x20000, (dev->regs[0x22] & (0x04 << 8)) ? determine_video_ram_write_access : disabled_shadow);
    mem_set_mem_state_both(0xc0000, 0x20000, (dev->regs[0x22] & (0x02 << 8)) ? rw_shadow : disabled_shadow);
    mem_set_mem_state_both(0xe0000, 0x20000, (dev->regs[0x22] & 0x01) ? rw_shadow : disabled_shadow);
    }
    break;

	case 0x2e:
    if(extended_granuality_enabled)
    {
    for(i=0; i<8; i++)
    {
        base = 0xc0000 + (i << 15);
        mem_set_mem_state_both(base, 0x8000, (dev->regs[0x2e] & (1 << (i+8))) ? ((dev->regs[0x2e] & (1 << i)) ? ro_shadow : rw_shadow) : disabled_shadow);
    }
    break;
    }
    }
    }

}


static uint16_t
intel_82335_read(uint16_t addr, void *priv)
{
    intel_82335_t *dev = (intel_82335_t *) priv;

    intel_82335_log("Register %02x: Reading\n", addr);

    return dev->regs[addr];

}

static void
intel_82335_close(void *priv)
{
    intel_82335_t *dev = (intel_82335_t *) priv;

    free(dev);
}


static void *
intel_82335_init(const device_t *info)
{
    intel_82335_t *dev = (intel_82335_t *) malloc(sizeof(intel_82335_t));
    memset(dev, 0, sizeof(intel_82335_t));

    device_add(&port_92_device);
    memset(dev->regs, 0, sizeof(dev->regs));

    dev->regs[0x28] = 0xf9;

    dev->cfg_locked = 1;

    /* Memory Configuration */
    io_sethandler(0x0022, 0x0001, NULL, intel_82335_read, NULL, NULL, intel_82335_write, NULL, dev);

    /* Roll Comparison */
    io_sethandler(0x0024, 0x0001, NULL, intel_82335_read, NULL, NULL, intel_82335_write, NULL, dev);
    io_sethandler(0x0026, 0x0001, NULL, intel_82335_read, NULL, NULL, intel_82335_write, NULL, dev);

    /* Address Range Comparison */
    io_sethandler(0x0028, 0x0001, NULL, intel_82335_read, NULL, NULL, intel_82335_write, NULL, dev);
    io_sethandler(0x002a, 0x0001, NULL, intel_82335_read, NULL, NULL, intel_82335_write, NULL, dev);

    /* Granuality Enable */
    io_sethandler(0x002c, 0x0001, NULL, intel_82335_read, NULL, NULL, intel_82335_write, NULL, dev);

    /* Extended Granuality */
	io_sethandler(0x002e, 0x0001, NULL, intel_82335_read, NULL, NULL, intel_82335_write, NULL, dev);
    
    return dev;
}


const device_t intel_82335_device = {
    "Intel 82335",
    0,
    0,
    intel_82335_init, intel_82335_close, NULL,
    NULL, NULL, NULL,
    NULL
};
