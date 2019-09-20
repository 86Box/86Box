/*
 * 86Box	A hypervisor and IBM PC system emulator that specializes in
 *		running old operating systems and software designed for IBM
 *		PC systems and compatibles from 1981 through fairly recent
 *		system designs based on the PCI bus.
 *
 *		This file is part of the 86Box distribution.
 *
 *		Implementation of the HEADLAND AT286 chipset.
 *
 * Version:	@(#)headland.c	1.0.0	2019/05/14
 *
 * Authors:	Sarah Walker, <http://pcem-emulator.co.uk/>
 *		Fred N. van Kempen, <decwiz@yahoo.com>
 *		Original by GreatPsycho for PCem.
 *		Miran Grca, <mgrca8@gmail.com>
 *
 *		Copyright 2010-2019 Sarah Walker.
 *		Copyright 2017-2019 Fred N. van Kempen.
 *		Copyright 2017,2018 Miran Grca.
 *		Copyright 2017,2018 GreatPsycho.
 */
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>
#include "../86box.h"
#include "../cpu/cpu.h"
#include "../cpu/x86.h"
#include "../timer.h"
#include "../io.h"
#include "../mem.h"
#include "../rom.h"
#include "../device.h"
#include "../keyboard.h"
#include "../floppy/fdd.h"
#include "../floppy/fdc.h"
#include "../port_92.h"
#include "chipset.h"


typedef struct {
    uint8_t		valid, pad;
    uint16_t		mr;

    struct headland_t *	headland;
} headland_mr_t;


typedef struct headland_t {
    uint8_t		type;

    uint8_t		cri;
    uint8_t		cr[8];

    uint8_t		indx;
    uint8_t		regs[256];

    uint8_t		ems_mar;

    headland_mr_t	null_mr,
			ems_mr[64];

    rom_t		vid_bios;

    mem_mapping_t	low_mapping;
    mem_mapping_t	ems_mapping[64];
    mem_mapping_t	mid_mapping;
    mem_mapping_t	high_mapping;
    mem_mapping_t	upper_mapping[24];
} headland_t;


/* TODO - Headland chipset's memory address mapping emulation isn't fully implemented yet,
	  so memory configuration is hardcoded now. */
static const int mem_conf_cr0[41] = {
    0x00, 0x00, 0x20, 0x40, 0x60, 0xA0, 0x40, 0xE0,
    0xA0, 0xC0, 0xE0, 0xE0, 0xC0, 0xE0, 0xE0, 0xE0,
    0xE0, 0x20, 0x40, 0x40, 0xA0, 0xC0, 0xE0, 0xE0,
    0xC0, 0xE0, 0xE0, 0xE0, 0xE0, 0xE0, 0xE0, 0xE0,
    0x20, 0x40, 0x60, 0x60, 0xC0, 0xE0, 0xE0, 0xE0,
    0xE0
};
static const int mem_conf_cr1[41] = {
    0x00, 0x40, 0x00, 0x00, 0x00, 0x40, 0x40, 0x40,
    0x00, 0x40, 0x40, 0x40, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x40, 0x40, 0x40, 0x00, 0x00, 0x00, 0x00,
    0x40, 0x40, 0x40, 0x40, 0x40, 0x40, 0x40, 0x40,
    0x00, 0x00, 0x40, 0x40, 0x00, 0x00, 0x00, 0x00,
    0x40
};


static uint32_t
get_addr(headland_t *dev, uint32_t addr, headland_mr_t *mr)
{
    if (mr && mr->valid && (dev->cr[0] & 2) && (mr->mr & 0x200)) {
	addr = (addr & 0x3fff) | ((mr->mr & 0x1F) << 14);
	if (dev->cr[1] & 0x40) {
		if ((dev->cr[4] & 0x80) && (dev->cr[6] & 1)) {
			if (dev->cr[0] & 0x80) {
				addr |= (mr->mr & 0x60) << 14;
				if (mr->mr & 0x100)
					addr += ((mr->mr & 0xC00) << 13) + (((mr->mr & 0x80) + 0x80) << 15);
				else
					addr += (mr->mr & 0x80) << 14;
			} else if (mr->mr & 0x100)
				addr += ((mr->mr & 0xC00) << 13) + (((mr->mr & 0x80) + 0x20) << 15);
			else
				addr += (mr->mr & 0x80) << 12;
		} else if (dev->cr[0] & 0x80)
			addr |= (mr->mr & 0x100) ? ((mr->mr & 0x80) + 0x400) << 12 : (mr->mr & 0xE0) << 14;
		else
			addr |= (mr->mr & 0x100) ? ((mr->mr & 0xE0) + 0x40) << 14 : (mr->mr & 0x80) << 12;
	} else {
		if ((dev->cr[4] & 0x80) && (dev->cr[6] & 1)) {
			if (dev->cr[0] & 0x80) {
				addr |= ((mr->mr & 0x60) << 14);
				if (mr->mr & 0x180)
					addr += ((mr->mr & 0xC00) << 13) + (((mr->mr & 0x180) - 0x60) << 16);
			} else
				addr |= ((mr->mr & 0x60) << 14) | ((mr->mr & 0x180) << 16) | ((mr->mr & 0xC00) << 13);
		} else if (dev->cr[0] & 0x80)
			addr |= (mr->mr & 0x1E0) << 14;
		else
			addr |= (mr->mr & 0x180) << 12;
	}
    } else if ((mr == NULL) && ((dev->cr[0] & 4) == 0) && (mem_size >= 1024) && (addr >= 0x100000))
	addr -= 0x60000;

    return addr;
}


static void
set_global_EMS_state(headland_t *dev, int state)
{
    uint32_t base_addr, virt_addr;
    int i;

    for (i = 0; i < 32; i++) {
	base_addr = (i + 16) << 14;
	if (i >= 24)
		base_addr += 0x20000;
	if ((state & 2) && (dev->ems_mr[((state & 1) << 5) | i].mr & 0x200)) {
		virt_addr = get_addr(dev, base_addr, &dev->ems_mr[((state & 1) << 5) | i]);
		if (i < 24)
			mem_mapping_disable(&dev->upper_mapping[i]);
		mem_mapping_disable(&dev->ems_mapping[(((state ^ 1) & 1) << 5) | i]);
		mem_mapping_enable(&dev->ems_mapping[((state & 1) << 5) | i]);
		if (virt_addr < ((uint32_t)mem_size << 10))
			mem_mapping_set_exec(&dev->ems_mapping[((state & 1) << 5) | i], ram + virt_addr);
		else
			mem_mapping_set_exec(&dev->ems_mapping[((state & 1) << 5) | i], NULL);
	} else {
		mem_mapping_set_exec(&dev->ems_mapping[((state & 1) << 5) | i], ram + base_addr);
		mem_mapping_disable(&dev->ems_mapping[(((state ^ 1) & 1) << 5) | i]);
		mem_mapping_disable(&dev->ems_mapping[((state & 1) << 5) | i]);
		if (i < 24)
			mem_mapping_enable(&dev->upper_mapping[i]);
	}
    }

    flushmmucache();
}


static void
memmap_state_update(headland_t *dev)
{
    uint32_t addr;
    int i;

    for (i = 0; i < 24; i++) {
	addr = get_addr(dev, 0x40000 + (i << 14), NULL);
	mem_mapping_set_exec(&dev->upper_mapping[i], addr < ((uint32_t)mem_size << 10) ? ram + addr : NULL);
    }
    mem_set_mem_state(0xA0000, 0x40000, MEM_READ_EXTERNAL | MEM_WRITE_EXTERNAL);
    if (mem_size > 640) {
	if ((dev->cr[0] & 4) == 0) {
		mem_mapping_set_addr(&dev->mid_mapping, 0x100000, mem_size > 1024 ? 0x60000 : (mem_size - 640) << 10);
		mem_mapping_set_exec(&dev->mid_mapping, ram + 0xA0000);
		if (mem_size > 1024) {
			mem_mapping_set_addr(&dev->high_mapping, 0x160000, (mem_size - 1024) << 10);
			mem_mapping_set_exec(&dev->high_mapping, ram + 0x100000);
		}
	} else {
		mem_mapping_set_addr(&dev->mid_mapping, 0xA0000, mem_size > 1024 ? 0x60000 : (mem_size - 640) << 10);
		mem_mapping_set_exec(&dev->mid_mapping, ram + 0xA0000);
		if (mem_size > 1024) {
			mem_mapping_set_addr(&dev->high_mapping, 0x100000, (mem_size - 1024) << 10);
			mem_mapping_set_exec(&dev->high_mapping, ram + 0x100000);
		}
	}
    }

    set_global_EMS_state(dev, dev->cr[0] & 3);
}


static void
hl_write(uint16_t addr, uint8_t val, void *priv)
{
    headland_t *dev = (headland_t *)priv;
    uint32_t base_addr, virt_addr;
    uint8_t old_val, indx;

    switch(addr) {
	case 0x0022:
		dev->indx = val;
		break;

	case 0x0023:
		old_val = dev->regs[dev->indx];
		if ((dev->indx == 0xc1) && !is486)
			val = 0;
		dev->regs[dev->indx] = val;
		if (dev->indx == 0x82) {
			shadowbios = val & 0x10;
			shadowbios_write = !(val & 0x10);
			if (shadowbios)
				mem_set_mem_state(0xf0000, 0x10000, MEM_READ_INTERNAL | MEM_WRITE_DISABLED);
			else
				mem_set_mem_state(0xf0000, 0x10000, MEM_READ_EXTERNAL | MEM_WRITE_INTERNAL);
		} else if (dev->indx == 0x87) {
			if ((val & 1) && !(old_val & 1))
				softresetx86();
		}
		break;

	case 0x01ec:
		dev->ems_mr[dev->ems_mar & 0x3f].mr = val | 0xff00;
		indx = dev->ems_mar & 0x1f;
		base_addr = (indx + 16) << 14;
		if (indx >= 24)
			base_addr += 0x20000;
		if ((dev->cr[0] & 2) && ((dev->cr[0] & 1) == ((dev->ems_mar & 0x20) >> 5))) {
			virt_addr = get_addr(dev, base_addr, &dev->ems_mr[dev->ems_mar & 0x3F]);
			if (indx < 24)
				mem_mapping_disable(&dev->upper_mapping[indx]);
			if (virt_addr < ((uint32_t)mem_size << 10))
				mem_mapping_set_exec(&dev->ems_mapping[dev->ems_mar & 0x3f], ram + virt_addr);
			else
				mem_mapping_set_exec(&dev->ems_mapping[dev->ems_mar & 0x3f], NULL);
			mem_mapping_enable(&dev->ems_mapping[dev->ems_mar & 0x3f]);
			flushmmucache();
		}
		if (dev->ems_mar & 0x80)
			dev->ems_mar++;
		break;

	case 0x01ed:
		dev->cri = val;
		break;

	case 0x01ee:
		dev->ems_mar = val;
		break;

	case 0x01ef:
		old_val = dev->cr[dev->cri];
		switch(dev->cri) {
			case 0:
				dev->cr[0] = (val & 0x1f) | mem_conf_cr0[(mem_size > 640 ? mem_size : mem_size - 128) >> 9];
				mem_set_mem_state(0xe0000, 0x10000, (val & 8 ? MEM_READ_INTERNAL : MEM_READ_EXTERNAL) | MEM_WRITE_DISABLED);
				mem_set_mem_state(0xf0000, 0x10000, (val & 0x10 ? MEM_READ_INTERNAL: MEM_READ_EXTERNAL) | MEM_WRITE_DISABLED);
				memmap_state_update(dev);
				break;

			case 1:
				dev->cr[1] = (val & 0xbf) | mem_conf_cr1[(mem_size > 640 ? mem_size : mem_size - 128) >> 9];
				memmap_state_update(dev);
				break;

			case 2:
			case 3:
			case 5:
				dev->cr[dev->cri] = val;
				memmap_state_update(dev);
				break;

			case 4:
				dev->cr[4] = (dev->cr[4] & 0xf0) | (val & 0x0f);
				if (val & 1) {
					mem_mapping_set_addr(&bios_mapping, 0x000f0000, 0x10000);
					mem_mapping_set_exec(&bios_mapping, &(rom[0x10000]));
				} else {
					mem_mapping_set_addr(&bios_mapping, 0x000e0000, 0x20000);
					mem_mapping_set_exec(&bios_mapping, rom);
				}
				break;

			case 6:
				if (dev->cr[4] & 0x80) {
					dev->cr[dev->cri] = (val & 0xfe) | (mem_size > 8192 ? 1 : 0);
					memmap_state_update(dev);
				}
				break;

			default:
				break;
		}
		break;

	default:
		break;
    }
}


static void
hl_writew(uint16_t addr, uint16_t val, void *priv)
{
    headland_t *dev = (headland_t *)priv;
    uint32_t base_addr, virt_addr;
    uint8_t indx;

    switch(addr) {
	case 0x01ec:
		dev->ems_mr[dev->ems_mar & 0x3f].mr = val;
		indx = dev->ems_mar & 0x1f;
		base_addr = (indx + 16) << 14;
		if (indx >= 24)
			base_addr += 0x20000;
		if ((dev->cr[0] & 2) && (dev->cr[0] & 1) == ((dev->ems_mar & 0x20) >> 5)) {
			if (val & 0x200) {
				virt_addr = get_addr(dev, base_addr, &dev->ems_mr[dev->ems_mar & 0x3f]);
				if (indx < 24)
					mem_mapping_disable(&dev->upper_mapping[indx]);
				if (virt_addr < ((uint32_t)mem_size << 10))
					mem_mapping_set_exec(&dev->ems_mapping[dev->ems_mar & 0x3f], ram + virt_addr);
                                else
					mem_mapping_set_exec(&dev->ems_mapping[dev->ems_mar & 0x3f], NULL);
				mem_mapping_enable(&dev->ems_mapping[dev->ems_mar & 0x3f]);
			} else {
				mem_mapping_set_exec(&dev->ems_mapping[dev->ems_mar & 0x3f], ram + base_addr);
				mem_mapping_disable(&dev->ems_mapping[dev->ems_mar & 0x3f]);
				if (indx < 24)
					mem_mapping_enable(&dev->upper_mapping[indx]);
			}
			flushmmucache();
		}
		if (dev->ems_mar & 0x80)
			dev->ems_mar++;
		break;

	default:
		break;
    }
}


static uint8_t
hl_read(uint16_t addr, void *priv)
{
    headland_t *dev = (headland_t *)priv;
    uint8_t ret = 0xff;

    switch(addr) {
	case 0x0022:
		ret = dev->indx;
		break;

	case 0x0023:
		if ((dev->indx >= 0xc0 || dev->indx == 0x20) && cpu_iscyrix)
			ret = 0xff; /*Don't conflict with Cyrix config registers*/
		else
			ret = dev->regs[dev->indx];
		break;

	case 0x01ec:
		ret = (uint8_t)dev->ems_mr[dev->ems_mar & 0x3f].mr;
		if (dev->ems_mar & 0x80)
			dev->ems_mar++;
		break;

	case 0x01ed:
		ret = dev->cri;
		break;

	case 0x01ee:
		ret = dev->ems_mar;
		break;

	case 0x01ef:
		switch(dev->cri) {
			case 0:
				ret = (dev->cr[0] & 0x1f) | mem_conf_cr0[(mem_size > 640 ? mem_size : mem_size - 128) >> 9];
				break;

			case 1:
				ret = (dev->cr[1] & 0xbf) | mem_conf_cr1[(mem_size > 640 ? mem_size : mem_size - 128) >> 9];
				break;

			case 6:
				if (dev->cr[4] & 0x80)
					ret = (dev->cr[6] & 0xfe) | (mem_size > 8192 ? 1 : 0);
				else
					ret = 0;
				break;

			default:
				ret = dev->cr[dev->cri];
				break;
		}
		break;

	default:
		break;
    }

    return ret;
}


static uint16_t
hl_readw(uint16_t addr, void *priv)
{
    headland_t *dev = (headland_t *)priv;
    uint16_t ret = 0xffff;

    switch(addr) {
	case 0x01ec:
		ret = dev->ems_mr[dev->ems_mar & 0x3f].mr | ((dev->cr[4] & 0x80) ? 0xf000 : 0xfc00);
		if (dev->ems_mar & 0x80)
			dev->ems_mar++;
		break;

	default:
		break;
    }

    return ret;
}


static uint8_t
mem_read_b(uint32_t addr, void *priv)
{
    headland_mr_t *mr = (headland_mr_t *) priv;
    headland_t *dev = mr->headland;
    uint8_t ret = 0xff;

    addr = get_addr(dev, addr, mr);
    if (addr < ((uint32_t)mem_size << 10))
	ret = ram[addr];

    return ret;
}


static uint16_t
mem_read_w(uint32_t addr, void *priv)
{
    headland_mr_t *mr = (headland_mr_t *) priv;
    headland_t *dev = mr->headland;
    uint16_t ret = 0xffff;

    addr = get_addr(dev, addr, mr);
    if (addr < ((uint32_t)mem_size << 10))
	ret = *(uint16_t *)&ram[addr];

    return ret;
}


static uint32_t
mem_read_l(uint32_t addr, void *priv)
{
    headland_mr_t *mr = (headland_mr_t *) priv;
    headland_t *dev = mr->headland;
    uint32_t ret = 0xffffffff;

    addr = get_addr(dev, addr, mr);
    if (addr < ((uint32_t)mem_size << 10))
	ret = *(uint32_t *)&ram[addr];

    return ret;
}


static void
mem_write_b(uint32_t addr, uint8_t val, void *priv)
{
    headland_mr_t *mr = (headland_mr_t *) priv;
    headland_t *dev = mr->headland;

    addr = get_addr(dev, addr, mr);
    if (addr < ((uint32_t)mem_size << 10))
	ram[addr] = val;
}


static void
mem_write_w(uint32_t addr, uint16_t val, void *priv)
{
    headland_mr_t *mr = (headland_mr_t *) priv;
    headland_t *dev = mr->headland;

    addr = get_addr(dev, addr, mr);
    if (addr < ((uint32_t)mem_size << 10))
	*(uint16_t *)&ram[addr] = val;
}


static void
mem_write_l(uint32_t addr, uint32_t val, void *priv)
{
    headland_mr_t *mr = (headland_mr_t *) priv;
    headland_t *dev = mr->headland;

    addr = get_addr(dev, addr, mr);
    if (addr < ((uint32_t)mem_size << 10))
	*(uint32_t *)&ram[addr] = val;
}


static void
headland_close(void *priv)
{
    headland_t *dev = (headland_t *)priv;

    free(dev);
}


static void *
headland_init(const device_t *info)
{
    headland_t *dev;
    int ht386;
    uint32_t i;

    dev = (headland_t *) malloc(sizeof(headland_t));
    memset(dev, 0x00, sizeof(headland_t));
    dev->type = info->local;

    ht386 = (dev->type == 32) ? 1 : 0;

    for (i = 0; i < 8; i++)
	dev->cr[i] = 0x00;
    dev->cr[0] = 0x04;

   if (ht386) {
	dev->cr[4] = 0x20;

	device_add(&port_92_inv_device);
    } else
	dev->cr[4] = 0x00;

    io_sethandler(0x01ec, 1,
		  hl_read,hl_readw,NULL, hl_write,hl_writew,NULL, dev);

    io_sethandler(0x01ed, 3, hl_read,NULL,NULL, hl_write,NULL,NULL, dev);

    dev->ems_mr[i].valid = 0;
    dev->ems_mr[i].mr = 0xff;
    dev->ems_mr[i].headland = dev;

    for (i = 0; i < 64; i++) {
	dev->ems_mr[i].valid = 1;
	dev->ems_mr[i].mr = 0x00;
	dev->ems_mr[i].headland = dev;
    }

    /* Turn off mem.c mappings. */
    mem_mapping_disable(&ram_low_mapping);
    mem_mapping_disable(&ram_mid_mapping);
    mem_mapping_disable(&ram_high_mapping);

    mem_mapping_add(&dev->low_mapping, 0, 0x40000,
		    mem_read_b, mem_read_w, mem_read_l,
		    mem_write_b, mem_write_w, mem_write_l,
		    ram, MEM_MAPPING_INTERNAL, &dev->null_mr);

    if (mem_size > 640) {
	mem_mapping_add(&dev->mid_mapping, 0xa0000, 0x60000,
			mem_read_b, mem_read_w, mem_read_l,
			mem_write_b, mem_write_w, mem_write_l,
			ram + 0xa0000, MEM_MAPPING_INTERNAL, &dev->null_mr);
	mem_mapping_enable(&dev->mid_mapping);
    }

    if (mem_size > 1024) {
	mem_mapping_add(&dev->high_mapping, 0x100000, ((mem_size-1024)*1024),
			mem_read_b, mem_read_w, mem_read_l,
			mem_write_b, mem_write_w, mem_write_l,
			ram + 0x100000, MEM_MAPPING_INTERNAL, &dev->null_mr);
	mem_mapping_enable(&dev->high_mapping);
    }

    for (i = 0; i < 24; i++) {
	mem_mapping_add(&dev->upper_mapping[i],
			0x40000 + (i << 14), 0x4000,
			mem_read_b, mem_read_w, mem_read_l,
			mem_write_b, mem_write_w, mem_write_l,
			mem_size > 256 + (i << 4) ? ram + 0x40000 + (i << 14) : NULL,
			MEM_MAPPING_INTERNAL, &dev->null_mr);
	mem_mapping_enable(&dev->upper_mapping[i]);
    }

    for (i = 0; i < 64; i++) {
	dev->ems_mr[i].mr = 0x00;
	mem_mapping_add(&dev->ems_mapping[i],
			((i & 31) + ((i & 31) >= 24 ? 24 : 16)) << 14, 0x04000,
			mem_read_b, mem_read_w, mem_read_l,
			mem_write_b, mem_write_w, mem_write_l,
			ram + (((i & 31) + ((i & 31) >= 24 ? 24 : 16)) << 14),
			0, &dev->ems_mr[i]);
    }

    memmap_state_update(dev);

    return(dev);
}


const device_t headland_device = {
    "Headland 286",
    0,
    0,
    headland_init, headland_close, NULL,
    NULL, NULL, NULL,
    NULL
};

const device_t headland_386_device = {
    "Headland 386",
    0,
    32,
    headland_init, headland_close, NULL,
    NULL, NULL, NULL,
    NULL
};
