#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>
#define HAVE_STDARG_H
#include <86box/86box.h>
#include "cpu.h"
#include <86box/io.h>
#include <86box/device.h>
#include <86box/mem.h>
#include <86box/plat_unused.h>
#include <86box/chipset.h>

typedef struct asus48625_t {
    uint8_t regs[3];
} asus48625_t;

static void
asus48625_recalcmapping(asus48625_t *dev)
{
    uint32_t shflags  = 0;
	uint32_t offset = 0;

    if (dev->regs[2] & 0x20)
        shflags = MEM_READ_EXTANY | MEM_WRITE_INTERNAL;
    else
        shflags = MEM_READ_INTERNAL | MEM_WRITE_EXTANY;

    shadowbios       = 0;
    shadowbios_write = 0;


    if (dev->regs[2] & 0x10) {
        shadowbios       = 1;
        shadowbios_write = !!(dev->regs[2] & 0x20);		
        mem_set_mem_state_both(0xf0000, 0x10000, shflags);

    } else
        mem_set_mem_state_both(0xf0000, 0x10000, MEM_READ_EXTANY | MEM_WRITE_EXTANY);
		
    for (uint8_t i = 0; i < 4; i++) {
        offset = (i << 14);
        if (dev->regs[0] & (1 << i))
            mem_set_mem_state_both(0xc0000 + offset, 0x4000, shflags);
        else
            mem_set_mem_state_both(0xc0000 + offset, 0x4000, MEM_READ_EXTANY | MEM_WRITE_EXTANY);
		
        if (dev->regs[1] & (1 << i))
            mem_set_mem_state_both(0xd0000 + offset, 0x4000, shflags);
        else
            mem_set_mem_state_both(0xd0000 + offset, 0x4000, MEM_READ_EXTANY | MEM_WRITE_EXTANY);	
		
        if (dev->regs[2] & (1 << i))
            mem_set_mem_state_both(0xe0000 + offset, 0x4000, shflags);
        else
            mem_set_mem_state_both(0xe0000 + offset, 0x4000, MEM_READ_EXTANY | MEM_WRITE_EXTANY);			
	}	
    flushmmucache_nopc();
}

static void
asus48625_write(uint16_t addr, uint8_t val, void *priv)
{
    asus48625_t *dev = (asus48625_t *) priv;

    switch (addr) {
    /*Based on BIOS behavior, the 486/25 gate array looks to be a similar, but NOT identical, setup to the one in the later ISA-486C.
	The main differences appear to be 16KB granularity rather than 32KB, and as a result using all three ports for shadowing rather than just 25h.
	
    port 23h:
    bit 0: c0000-c3fff shadow enable
    bit 1: c4000-c7fff shadow enable
    bit 2: c8000-cbfff shadow enable
    bit 3: cc000-cffff shadow enable
    bit 4: c0000-effff caching enable
    bit 5: f0000-fffff caching enable

    port 25h:
    bit 0: d0000-d3fff shadow enable
    bit 1: d4000-d7fff shadow enable
    bit 2: d8000-dbfff shadow enable
    bit 3: dc000-dffff shadow enable

    port 27h:

    bit 0: e0000-e3fff shadow enable
    bit 1: e4000-e7fff shadow enable
    bit 2: e8000-ebfff shadow enable
    bit 3: ec000-effff shadow enable
    bit 4: f0000-fffff shadow enable (BIOS shadow)
    bit 5: read ROM, write to memory (Presumed to be the same behavior as the ISA-486C. The BIOS always sets the bit and then unsets it.)
*/		
        case 0x23:
            dev->regs[0] = val;
			asus48625_recalcmapping(dev);
            break;
        case 0x25:
            dev->regs[1] = val;
            asus48625_recalcmapping(dev);
            break;
        case 0x27:
            dev->regs[2] = val;
			asus48625_recalcmapping(dev);
            break;
    }
}

static uint8_t
asus48625_read(uint16_t addr, void *priv)
{
    asus48625_t *dev = (asus48625_t *) priv;
    uint8_t    ret = 0xff;

    switch (addr) {
        case 0x23:
            ret = dev->regs[0];
            break;
        case 0x25:
            ret = dev->regs[1];
            break;
        case 0x27:
            ret = dev->regs[2];
            break;
    }

    return ret;
}

static void
asus48625_close(void *priv)
{
    asus48625_t *dev = (asus48625_t *) priv;

    free(dev);
}

static void *
asus48625_init(UNUSED(const device_t *info))
{
    asus48625_t *dev = (asus48625_t *) calloc(1, sizeof(asus48625_t));

    io_sethandler(0x0023, 0x0001, asus48625_read, NULL, NULL, asus48625_write, NULL, NULL, dev);
    io_sethandler(0x0025, 0x0001, asus48625_read, NULL, NULL, asus48625_write, NULL, NULL, dev);
    io_sethandler(0x0027, 0x0001, asus48625_read, NULL, NULL, asus48625_write, NULL, NULL, dev);

    return dev;
}

const device_t asus48625_device = {
    .name          = "ASUS 486/25 Gate Array",
    .internal_name = "asus48625",
    .flags         = 0,
    .local         = 0,
    .init          = asus48625_init,
    .close         = asus48625_close,
    .reset         = NULL,
    .available     = NULL,
    .speed_changed = NULL,
    .force_redraw  = NULL,
    .config        = NULL
};
