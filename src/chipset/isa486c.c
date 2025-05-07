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

typedef struct isa486c_t {
    uint8_t regs[3];
} isa486c_t;

static void
isa486c_recalcmapping(isa486c_t *dev)
{
    uint32_t shflags  = 0;
    uint32_t bases[5] = { 0x000c0000, 0x000c8000, 0x000d0000, 0x000d8000, 0x000e0000 };
    uint32_t sizes[5] = { 0x00008000, 0x00008000, 0x00008000, 0x00008000, 0x00020000 };

    if (dev->regs[1] & 0x20)
        shflags = MEM_READ_EXTANY | MEM_WRITE_INTERNAL;
    else
        shflags = MEM_READ_INTERNAL | MEM_WRITE_EXTANY;

    shadowbios       = 0;
    shadowbios_write = 0;

    for (uint8_t i = 0; i < 5; i++)
        if (dev->regs[1] & (1 << i)) {
            if (i == 4) {
                shadowbios       = 1;
                shadowbios_write = !!(dev->regs[1] & 0x20);
            }

            mem_set_mem_state_both(bases[i], sizes[i], shflags);
       } else
            mem_set_mem_state_both(bases[i], sizes[i], MEM_READ_EXTANY | MEM_WRITE_EXTANY);

    flushmmucache_nopc();
}

static void
isa486c_write(uint16_t addr, uint8_t val, void *priv)
{
    isa486c_t *dev = (isa486c_t *) priv;

    switch (addr) {
        case 0x0023:
            dev->regs[0] = val;
            break;
        /*
           Port 25h:
               - Bit 0 = Video BIOS (C000-C7FF) shadow enabled;
               - Bit 1 = C800-C8FF shadow enabled;
               - Bit 2 = D000-D7FF shadow enabled;
               - Bit 3 = D800-DFFF shadow enabled;
               - Bit 4 = E000-FFFF shadow enabled (or F0000-FFFFF?);
               - Bit 5 = If set, read from ROM, write to shadow;
               - Bit 6 = KEN Video & BIOS enabled (cacheability!).
         */
        case 0x0025:
            dev->regs[1] = val;
            isa486c_recalcmapping(dev);
            break;
        case 0x0027:
            dev->regs[2] = val;
            break;
    }
}

static uint8_t
isa486c_read(uint16_t addr, void *priv)
{
    isa486c_t *dev = (isa486c_t *) priv;
    uint8_t    ret = 0xff;

    switch (addr) {
        case 0x0023:
            ret = dev->regs[0];
            break;
        case 0x0025:
            ret = dev->regs[1];
            break;
        case 0x0027:
            ret = dev->regs[2];
            break;
    }

    return ret;
}

static void
isa486c_close(void *priv)
{
    isa486c_t *dev = (isa486c_t *) priv;

    free(dev);
}

static void *
isa486c_init(UNUSED(const device_t *info))
{
    isa486c_t *dev = (isa486c_t *) calloc(1, sizeof(isa486c_t));

    io_sethandler(0x0023, 0x0001, isa486c_read, NULL, NULL, isa486c_write, NULL, NULL, dev);
    io_sethandler(0x0025, 0x0001, isa486c_read, NULL, NULL, isa486c_write, NULL, NULL, dev);
    io_sethandler(0x0027, 0x0001, isa486c_read, NULL, NULL, isa486c_write, NULL, NULL, dev);

    return dev;
}

const device_t isa486c_device = {
    .name          = "ASUS ISA-486C Gate Array",
    .internal_name = "isa486c",
    .flags         = 0,
    .local         = 0,
    .init          = isa486c_init,
    .close         = isa486c_close,
    .reset         = NULL,
    .available     = NULL,
    .speed_changed = NULL,
    .force_redraw  = NULL,
    .config        = NULL
};
