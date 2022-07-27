/*
 * 86Box	A hypervisor and IBM PC system emulator that specializes in
 *		running old operating systems and software designed for IBM
 *		PC systems and compatibles from 1981 through fairly recent
 *		system designs based on the PCI bus.
 *
 *		This file is part of the 86Box distribution.
 *
 *		Implementation of the OPTi 82C283 chipset.
 *
 * Authors:	Tiseno100,
 *		Miran Grca, <mgrca8@gmail.com>
 *
 *		Copyright 2021 Tiseno100.
 *		Copyright 2021 Miran Grca.
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
#include <86box/chipset.h>


#ifdef ENABLE_OPTI283_LOG
int opti283_do_log = ENABLE_OPTI283_LOG;

static void
opti283_log(const char *fmt, ...)
{
    va_list ap;

    if (opti283_do_log)
    {
        va_start(ap, fmt);
        pclog_ex(fmt, ap);
        va_end(ap);
    }
}
#else
#define opti283_log(fmt, ...)
#endif


typedef struct
{
    uint32_t	phys, virt;
} mem_remapping_t;


typedef struct
{
    uint8_t		index, shadow_high,
			regs[256];
    mem_remapping_t	mem_remappings[2];
    mem_mapping_t	mem_mappings[2];
} opti283_t;


static uint8_t
opti283_read_remapped_ram(uint32_t addr, void *priv)
{
    mem_remapping_t *dev = (mem_remapping_t *) priv;

    return mem_read_ram((addr - dev->virt) + dev->phys, priv);
}


static uint16_t
opti283_read_remapped_ramw(uint32_t addr, void *priv)
{
    mem_remapping_t *dev = (mem_remapping_t *) priv;

    return mem_read_ramw((addr - dev->virt) + dev->phys, priv);
}


static uint32_t
opti283_read_remapped_raml(uint32_t addr, void *priv)
{
    mem_remapping_t *dev = (mem_remapping_t *) priv;

    return mem_read_raml((addr - dev->virt) + dev->phys, priv);
}


static void
opti283_write_remapped_ram(uint32_t addr, uint8_t val, void *priv)
{
    mem_remapping_t *dev = (mem_remapping_t *) priv;

    mem_write_ram((addr - dev->virt) + dev->phys, val, priv);
}


static void
opti283_write_remapped_ramw(uint32_t addr, uint16_t val, void *priv)
{
    mem_remapping_t *dev = (mem_remapping_t *) priv;

    mem_write_ramw((addr - dev->virt) + dev->phys, val, priv);
}


static void
opti283_write_remapped_raml(uint32_t addr, uint32_t val, void *priv)
{
    mem_remapping_t *dev = (mem_remapping_t *) priv;

    mem_write_raml((addr - dev->virt) + dev->phys, val, priv);
}


static void
opti283_shadow_recalc(opti283_t *dev)
{
    uint32_t i, base;
    uint32_t rbase;
    uint8_t sh_enable, sh_mode;
    uint8_t rom, sh_copy;

    shadowbios = shadowbios_write = 0;
    dev->shadow_high = 0;

    opti283_log("OPTI 283: %02X %02X %02X %02X\n", dev->regs[0x11], dev->regs[0x12], dev->regs[0x13], dev->regs[0x14]);

    if (dev->regs[0x11] & 0x80) {
	mem_set_mem_state_both(0xf0000, 0x10000, MEM_READ_EXTANY | MEM_WRITE_INTERNAL);
	opti283_log("OPTI 283: F0000-FFFFF READ_EXTANY, WRITE_INTERNAL\n");
	shadowbios_write = 1;
    } else {
	shadowbios = 1;
	if (dev->regs[0x14] & 0x80) {
		mem_set_mem_state_both(0xf0000, 0x01000, MEM_READ_INTERNAL | MEM_WRITE_INTERNAL);
		opti283_log("OPTI 283: F0000-F0FFF READ_INTERNAL, WRITE_INTERNAL\n");
		shadowbios_write = 1;
	} else {
		mem_set_mem_state_both(0xf0000, 0x01000, MEM_READ_INTERNAL | MEM_WRITE_DISABLED);
		opti283_log("OPTI 283: F0000-F0FFF READ_INTERNAL, WRITE_DISABLED\n");
	}

	mem_set_mem_state_both(0xf1000, 0x0f000, MEM_READ_INTERNAL | MEM_WRITE_DISABLED);
	opti283_log("OPTI 283: F1000-FFFFF READ_INTERNAL, WRITE_DISABLED\n");
    }

    sh_copy = dev->regs[0x11] & 0x08;
    for (i = 0; i < 12; i++) {
	base = 0xc0000 + (i << 14);
	if (i >= 4)
		sh_enable = dev->regs[0x12] & (1 << (i - 4));
	else
		sh_enable = dev->regs[0x13] & (1 << (i + 4));
	sh_mode = dev->regs[0x11] & (1 << (i >> 2));
	rom = dev->regs[0x11] & (1 << ((i >> 2) + 4));
	opti283_log("OPTI 283: %i/%08X: %i, %i, %i\n", i, base, (i >= 4) ? (1 << (i - 4)) : (1 << (i + 4)), (1 << (i >> 2)), (1 << ((i >> 2) + 4)));

	if (sh_enable && rom) {
		if (base >= 0x000e0000)
			shadowbios |= 1;
		if (base >= 0x000d0000)
			dev->shadow_high |= 1;

		if (sh_mode) {
			mem_set_mem_state_both(base, 0x4000, MEM_READ_INTERNAL | MEM_WRITE_DISABLED);
			opti283_log("OPTI 283: %08X-%08X READ_INTERNAL, WRITE_DISABLED\n", base, base + 0x3fff);
		} else {
			if (base >= 0x000e0000)
				shadowbios_write |= 1;

			if (sh_copy) {
				mem_set_mem_state_both(base, 0x4000, MEM_READ_INTERNAL | MEM_WRITE_INTERNAL);
				opti283_log("OPTI 283: %08X-%08X READ_INTERNAL, WRITE_INTERNAL\n", base, base + 0x3fff);
			} else {
				mem_set_mem_state_both(base, 0x4000, MEM_READ_INTERNAL | MEM_WRITE_EXTERNAL);
				opti283_log("OPTI 283: %08X-%08X READ_INTERNAL, WRITE_EXTERNAL\n", base, base + 0x3fff);
			}
		}
	} else {
		if (base >= 0xe0000) {
			mem_set_mem_state_both(base, 0x4000, MEM_READ_EXTANY | MEM_WRITE_DISABLED);
			opti283_log("OPTI 283: %08X-%08X READ_EXTANY, WRITE_DISABLED\n", base, base + 0x3fff);
		} else {
			mem_set_mem_state_both(base, 0x4000, MEM_READ_EXTERNAL | MEM_WRITE_DISABLED);
			opti283_log("OPTI 283: %08X-%08X READ_EXTERNAL, WRITE_DISABLED\n", base, base + 0x3fff);
		}
	}
    }

    rbase = ((uint32_t) (dev->regs[0x13] & 0x0f)) << 20;

    if (rbase > 0) {
	dev->mem_remappings[0].virt = rbase;
	mem_mapping_set_addr(&dev->mem_mappings[0], rbase, 0x00020000);

	if (!dev->shadow_high) {
		rbase += 0x00020000;
		dev->mem_remappings[1].virt = rbase;
		mem_mapping_set_addr(&dev->mem_mappings[1], rbase , 0x00020000);
	} else
		mem_mapping_disable(&dev->mem_mappings[1]);
    } else {
	mem_mapping_disable(&dev->mem_mappings[0]);
	mem_mapping_disable(&dev->mem_mappings[1]);
    }

    flushmmucache_nopc();
}


static void
opti283_write(uint16_t addr, uint8_t val, void *priv)
{
    opti283_t *dev = (opti283_t *)priv;

    switch (addr) {
	case 0x22:
		dev->index = val;
		break;

	case 0x24:
		opti283_log("OPTi 283: dev->regs[%02x] = %02x\n", dev->index, val);

		switch (dev->index) {
			case 0x10:
				dev->regs[dev->index] = val;
				break;

			case 0x14:
				reset_on_hlt = !!(val & 0x40);
				/* FALLTHROUGH */
			case 0x11: case 0x12:
			case 0x13:
				dev->regs[dev->index] = val;
				opti283_shadow_recalc(dev);
				break;
		}
		break;
    }
}


static uint8_t
opti283_read(uint16_t addr, void *priv)
{
    opti283_t *dev = (opti283_t *)priv;
    uint8_t ret = 0xff;

    if (addr == 0x24)
	ret = dev->regs[dev->index];

    return ret;
}


static void
opti283_close(void *priv)
{
    opti283_t *dev = (opti283_t *)priv;

    free(dev);
}


static void *
opti283_init(const device_t *info)
{
    opti283_t *dev = (opti283_t *)malloc(sizeof(opti283_t));
    memset(dev, 0x00, sizeof(opti283_t));

    io_sethandler(0x0022, 0x0001, opti283_read, NULL, NULL, opti283_write, NULL, NULL, dev);
    io_sethandler(0x0024, 0x0001, opti283_read, NULL, NULL, opti283_write, NULL, NULL, dev);

    dev->regs[0x10] = 0x3f;
    dev->regs[0x11] = 0xf0;

    dev->mem_remappings[0].phys = 0x000a0000;
    dev->mem_remappings[1].phys = 0x000d0000;

    mem_mapping_add(&dev->mem_mappings[0], 0, 0x00020000,
		    opti283_read_remapped_ram, opti283_read_remapped_ramw, opti283_read_remapped_raml,
		    opti283_write_remapped_ram, opti283_write_remapped_ramw, opti283_write_remapped_raml,
                    &ram[dev->mem_remappings[0].phys], MEM_MAPPING_INTERNAL, &dev->mem_remappings[0]);
    mem_mapping_disable(&dev->mem_mappings[0]);

    mem_mapping_add(&dev->mem_mappings[1], 0, 0x00020000,
		    opti283_read_remapped_ram, opti283_read_remapped_ramw, opti283_read_remapped_raml,
		    opti283_write_remapped_ram, opti283_write_remapped_ramw, opti283_write_remapped_raml,
                    &ram[dev->mem_remappings[1].phys], MEM_MAPPING_INTERNAL, &dev->mem_remappings[1]);
    mem_mapping_disable(&dev->mem_mappings[1]);

    opti283_shadow_recalc(dev);

    return dev;
}

const device_t opti283_device = {
    .name = "OPTi 82C283",
    .internal_name = "opti283",
    .flags = 0,
    .local = 0,
    .init = opti283_init,
    .close = opti283_close,
    .reset = NULL,
    { .available = NULL },
    .speed_changed = NULL,
    .force_redraw = NULL,
    .config = NULL
};
