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
#include <86box/chipset.h>


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

#define enabled_shadow (MEM_READ_INTERNAL | MEM_WRITE_INTERNAL)
#define disabled_shadow (MEM_READ_EXTANY | MEM_WRITE_EXTANY)

#define rw_shadow (MEM_READ_INTERNAL | MEM_WRITE_INTERNAL)
#define ro_shadow (MEM_READ_INTERNAL | MEM_WRITE_DISABLED)

#define extended_granuality_enabled (dev->reg_2c & 0x01)
#define determine_video_ram_write_access ((dev->reg_22 & (0x08 << 8)) ? rw_shadow : ro_shadow)

typedef struct
{

    uint16_t
    reg_22, reg_24, reg_26, reg_28, reg_2a, reg_2c, reg_2e;

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

    intel_82335_log("Register %02x: Write %04x\n", addr, val);

    switch (addr) {
    
	case 0x22:
    dev->reg_22 = val;

    if (!extended_granuality_enabled)
    {
    mem_set_mem_state_both(0xa0000, 0x20000, (dev->reg_22 & (0x04 << 8)) ? enabled_shadow : disabled_shadow);
    mem_set_mem_state_both(0xc0000, 0x20000, (dev->reg_22 & (0x02 << 8)) ? enabled_shadow : disabled_shadow);
    mem_set_mem_state_both(0xe0000, 0x20000, (dev->reg_22 & 0x01) ? determine_video_ram_write_access : disabled_shadow);
    }
    break;

    case 0x24:
    dev->reg_24 = val;
    break;

    case 0x26:
    dev->reg_26 = val;
    break;

    case 0x28:
    dev->reg_28 = val;
    break;

    case 0x2a:
    dev->reg_2a = val;
    break;

    case 0x2c:
    dev->reg_2c = val;
    break;

	case 0x2e:
	dev->reg_2e = val;

    if(extended_granuality_enabled)
    {
    for(i=0; i<8; i++)
    {
        base = 0xc0000 + (i << 15);
        mem_set_mem_state_both(base, 0x8000, (dev->reg_2e & (1 << (i+8))) ? ((dev->reg_2e & (1 << i)) ? ro_shadow : rw_shadow) : disabled_shadow);
    }
    break;
    }

    }
}


static uint16_t
intel_82335_read(uint16_t addr, void *priv)
{
    intel_82335_t *dev = (intel_82335_t *) priv;

    intel_82335_log("Register %02x: Reading\n", addr);

    switch (addr) {
	case 0x22:
		return dev->reg_22;
		break;
    case 0x24:
        return dev->reg_24;
        break;
    case 0x26:
        return dev->reg_26;
        break;
    case 0x28:
        return dev->reg_28;
        break;
    case 0x2a:
        return dev->reg_2a;
        break;
    case 0x2c:
        return dev->reg_2c;
        break;
	case 0x2e:
		return dev->reg_2e;
		break;
	default:
		return 0xff;
		break;
    }
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

	dev->reg_22 = 0x00;
    dev->reg_24 = 0x00;
    dev->reg_26 = 0x00;
    dev->reg_28 = 0xf9;
    dev->reg_2a = 0x00;
    dev->reg_2c = 0x00;
	dev->reg_2e = 0x00;

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
