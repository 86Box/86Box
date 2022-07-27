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
 *
 *
 * Authors:	Sarah Walker, <http://pcem-emulator.co.uk/>
 *		Fred N. van Kempen, <decwiz@yahoo.com>
 *		Original by GreatPsycho for PCem.
 *		Miran Grca, <mgrca8@gmail.com>
 *
 *		Copyright 2010-2019 Sarah Walker.
 *		Copyright 2017-2019 Fred N. van Kempen.
 *		Copyright 2017-2019 Miran Grca.
 *		Copyright 2017-2019 GreatPsycho.
 */
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>
#include <86box/86box.h>
#include "cpu.h"
#include "x86.h"
#include <86box/timer.h>
#include <86box/io.h>
#include <86box/mem.h>
#include <86box/device.h>
#include <86box/fdd.h>
#include <86box/fdc.h>
#include <86box/port_92.h>
#include <86box/chipset.h>


typedef struct {
    uint8_t		valid, enabled;
    uint16_t		mr;
    uint32_t		virt_base;

    struct headland_t *	headland;
} headland_mr_t;


typedef struct headland_t {
    uint8_t		revision;

    uint8_t		cri;
    uint8_t		cr[7];

    uint8_t		indx;
    uint8_t		regs[256];

    uint8_t		ems_mar;

    headland_mr_t	null_mr,
			ems_mr[64];

    mem_mapping_t	low_mapping;
    mem_mapping_t	ems_mapping[64];
    mem_mapping_t	mid_mapping;
    mem_mapping_t	high_mapping;
    mem_mapping_t	shadow_mapping[2];
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
    uint32_t bank_base[4], bank_shift[4], shift, other_shift, bank;

    if ((addr >= 0x0e0000) && (addr <= 0x0fffff))
	return addr;
    else if ((addr >= 0xfe0000) && (addr <= 0xffffff))
	return addr & 0x0fffff;

    if (dev->revision == 8) {
	shift = (dev->cr[0] & 0x80) ? 21 : ((dev->cr[6] & 0x01) ? 23 : 19);
	other_shift = (dev->cr[0] & 0x80) ? ((dev->cr[6] & 0x01) ? 19 : 23) : 21;
    } else {
	shift = (dev->cr[0] & 0x80) ? 21 : 19;
	other_shift = (dev->cr[0] & 0x80) ? 21 : 19;
    }

    /* Bank size = 1 << (bank shift + 2) . */
    bank_shift[0] = bank_shift[1] = shift;

    bank_base[0] = 0x00000000;
    bank_base[1] = bank_base[0] + (1 << shift);
    bank_base[2] = bank_base[1] + (1 << shift);

    if ((dev->revision > 0) && (dev->revision < 8) && (dev->cr[1] & 0x40)) {
	bank_shift[2] = bank_shift[3] = other_shift;
	bank_base[3] = bank_base[2] + (1 << other_shift);
	/* First address after the memory is bank_base[3] + (1 << other_shift) */
    } else {
	bank_shift[2] = bank_shift[3] = shift;
	bank_base[3] = bank_base[2] + (1 << shift);
	/* First address after the memory is bank_base[3] + (1 << shift) */
    }

    if (mr && mr->valid && (dev->cr[0] & 2) && (mr->mr & 0x200)) {
	addr = (addr & 0x3fff) | ((mr->mr & 0x1F) << 14);

	bank = (mr->mr >> 7) & 3;

	if (bank_shift[bank] >= 21)
		addr |= (mr->mr & 0x060) << 14;

	if ((dev->revision == 8) && (bank_shift[bank] == 23))
		addr |= (mr->mr & 0xc00) << 11;

	addr |= bank_base[(mr->mr >> 7) & 3];
    } else if (((mr == NULL) || !mr->valid) && (mem_size >= 1024) && (addr >= 0x100000) && ((dev->cr[0] & 4) == 0))
	addr -= 0x60000;

    return addr;
}


static void
hl_ems_disable(headland_t *dev, uint8_t mar, uint32_t base_addr, uint8_t indx)
{
    if (base_addr < ((uint32_t)mem_size << 10))
	mem_mapping_set_exec(&dev->ems_mapping[mar & 0x3f], ram + base_addr);
    else
	mem_mapping_set_exec(&dev->ems_mapping[mar & 0x3f], NULL);
    mem_mapping_disable(&dev->ems_mapping[mar & 0x3f]);
    if (indx < 24) {
	mem_set_mem_state(base_addr, 0x4000, MEM_READ_INTERNAL | MEM_WRITE_INTERNAL);
	mem_mapping_enable(&dev->upper_mapping[indx]);
    } else
	mem_set_mem_state(base_addr, 0x4000, MEM_READ_EXTANY | MEM_WRITE_EXTANY);
}


static void
hl_ems_update(headland_t *dev, uint8_t mar)
{
    uint32_t base_addr, virt_addr;
    uint8_t indx = mar & 0x1f;

    base_addr = (indx + 16) << 14;
    if (indx >= 24)
	base_addr += 0x20000;
    hl_ems_disable(dev, mar, base_addr, indx);
    dev->ems_mr[mar & 0x3f].enabled = 0;
    dev->ems_mr[mar & 0x3f].virt_base = base_addr;
    if ((dev->cr[0] & 2) && ((dev->cr[0] & 1) == ((mar & 0x20) >> 5)) && (dev->ems_mr[mar & 0x3f].mr & 0x200)) {
	mem_set_mem_state(base_addr, 0x4000, MEM_READ_INTERNAL | MEM_WRITE_INTERNAL);
	virt_addr = get_addr(dev, base_addr, &dev->ems_mr[mar & 0x3f]);
	dev->ems_mr[mar & 0x3f].enabled = 1;
	dev->ems_mr[mar & 0x3f].virt_base = virt_addr;
	if (indx < 24)
		mem_mapping_disable(&dev->upper_mapping[indx]);
	if (virt_addr < ((uint32_t)mem_size << 10))
		mem_mapping_set_exec(&dev->ems_mapping[mar & 0x3f], ram + virt_addr);
	else
		mem_mapping_set_exec(&dev->ems_mapping[mar & 0x3f], NULL);
	mem_mapping_enable(&dev->ems_mapping[mar & 0x3f]);
    }

    flushmmucache();
}


static void
set_global_EMS_state(headland_t *dev, int state)
{
    int i;

    for (i = 0; i < 32; i++) {
	hl_ems_update(dev, i | (((dev->cr[0] & 0x01) << 5) ^ 0x20));
	hl_ems_update(dev, i | ((dev->cr[0] & 0x01) << 5));
   }
}


static void
memmap_state_default(headland_t *dev, uint8_t ht_romcs)
{
    mem_mapping_disable(&dev->mid_mapping);

    if (ht_romcs)
	mem_set_mem_state(0x0e0000, 0x20000, MEM_READ_ROMCS | MEM_WRITE_ROMCS);
    else
	mem_set_mem_state(0x0e0000, 0x20000, MEM_READ_EXTERNAL | MEM_WRITE_EXTERNAL);
    mem_set_mem_state(0xfe0000, 0x20000, MEM_READ_ROMCS | MEM_WRITE_ROMCS);

    mem_mapping_disable(&dev->shadow_mapping[0]);
    mem_mapping_disable(&dev->shadow_mapping[1]);
}


static void
memmap_state_update(headland_t *dev)
{
    uint32_t addr;
    int i;
    uint8_t ht_cr0 = dev->cr[0];
    uint8_t ht_romcs = !(dev->cr[4] & 0x01);
    if (dev->revision <= 1)
	ht_romcs = 1;
    if (!(dev->cr[0] & 0x04))
	ht_cr0 &= ~0x18;

    for (i = 0; i < 24; i++) {
	addr = get_addr(dev, 0x40000 + (i << 14), NULL);
	mem_mapping_set_exec(&dev->upper_mapping[i], addr < ((uint32_t)mem_size << 10) ? ram + addr : NULL);
    }

    memmap_state_default(dev, ht_romcs);

    if (mem_size > 640) {
	if (ht_cr0 & 0x04) {
		mem_mapping_set_addr(&dev->mid_mapping, 0xA0000, 0x40000);
		mem_mapping_set_exec(&dev->mid_mapping, ram + 0xA0000);
		mem_mapping_disable(&dev->mid_mapping);
		if (mem_size > 1024) {
			mem_set_mem_state((mem_size << 10), 0x60000, MEM_READ_INTERNAL | MEM_WRITE_INTERNAL);
			mem_mapping_set_addr(&dev->high_mapping, 0x100000, (mem_size - 1024) << 10);
			mem_mapping_set_exec(&dev->high_mapping, ram + 0x100000);
		}
	} else {
		/*	1 MB - 1 MB + 384k: RAM pointing to A0000-FFFFF
			1 MB + 384k: Any ram pointing 1 MB onwards. */
		/* First, do the addresses above 1 MB. */
		mem_mapping_set_addr(&dev->mid_mapping, 0x100000, mem_size > 1024 ? 0x60000 : (mem_size - 640) << 10);
		mem_mapping_set_exec(&dev->mid_mapping, ram + 0xA0000);
		if (mem_size > 1024) {
			/* We have ram above 1 MB, we need to relocate that. */
			mem_set_mem_state((mem_size << 10), 0x60000, MEM_READ_EXTANY | MEM_WRITE_EXTANY);
			mem_mapping_set_addr(&dev->high_mapping, 0x160000, (mem_size - 1024) << 10);
			mem_mapping_set_exec(&dev->high_mapping, ram + 0x100000);
		}
	}
    }

    switch (ht_cr0) {
	case 0x18:
		if ((mem_size << 10) > 0xe0000) {
			mem_set_mem_state(0x0e0000, 0x20000, MEM_READ_INTERNAL | MEM_WRITE_DISABLED);
			mem_set_mem_state(0xfe0000, 0x20000, MEM_READ_INTERNAL | MEM_WRITE_DISABLED);

			mem_mapping_set_addr(&dev->shadow_mapping[0], 0x0e0000, 0x20000);
			mem_mapping_set_exec(&dev->shadow_mapping[0], ram + 0xe0000);
			mem_mapping_set_addr(&dev->shadow_mapping[1], 0xfe0000, 0x20000);
			mem_mapping_set_exec(&dev->shadow_mapping[1], ram + 0xe0000);
		} else {
			mem_mapping_disable(&dev->shadow_mapping[0]);
			mem_mapping_disable(&dev->shadow_mapping[1]);
		}
		break;
	case 0x10:
		if ((mem_size << 10) > 0xf0000) {
			mem_set_mem_state(0x0f0000, 0x10000, MEM_READ_INTERNAL | MEM_WRITE_DISABLED);
			mem_set_mem_state(0xff0000, 0x10000, MEM_READ_INTERNAL | MEM_WRITE_DISABLED);

			mem_mapping_set_addr(&dev->shadow_mapping[0], 0x0f0000, 0x10000);
			mem_mapping_set_exec(&dev->shadow_mapping[0], ram + 0xf0000);
			mem_mapping_set_addr(&dev->shadow_mapping[1], 0xff0000, 0x10000);
			mem_mapping_set_exec(&dev->shadow_mapping[1], ram + 0xf0000);
		} else {
			mem_mapping_disable(&dev->shadow_mapping[0]);
			mem_mapping_disable(&dev->shadow_mapping[1]);
		}
		break;
	case 0x08:
		if ((mem_size << 10) > 0xe0000) {
			mem_set_mem_state(0x0e0000, 0x10000, MEM_READ_INTERNAL | MEM_WRITE_DISABLED);
			mem_set_mem_state(0xfe0000, 0x10000, MEM_READ_INTERNAL | MEM_WRITE_DISABLED);

			mem_mapping_set_addr(&dev->shadow_mapping[0], 0x0e0000, 0x10000);
			mem_mapping_set_exec(&dev->shadow_mapping[0], ram + 0xe0000);
			mem_mapping_set_addr(&dev->shadow_mapping[1], 0xfe0000, 0x10000);
			mem_mapping_set_exec(&dev->shadow_mapping[1], ram + 0xe0000);
		} else {
			mem_mapping_disable(&dev->shadow_mapping[0]);
			mem_mapping_disable(&dev->shadow_mapping[1]);
		}
		break;
	case 0x00:
	default:
		mem_mapping_disable(&dev->shadow_mapping[0]);
		mem_mapping_disable(&dev->shadow_mapping[1]);
		break;
    }

    set_global_EMS_state(dev, ht_cr0 & 3);
}


static void
hl_write(uint16_t addr, uint8_t val, void *priv)
{
    headland_t *dev = (headland_t *)priv;

    switch(addr) {
	case 0x01ec:
		dev->ems_mr[dev->ems_mar & 0x3f].mr = val | 0xff00;
		hl_ems_update(dev, dev->ems_mar & 0x3f);
		if (dev->ems_mar & 0x80)
			dev->ems_mar++;
		break;

	case 0x01ed:
		if (dev->revision > 0)
			dev->cri = val;
		break;

	case 0x01ee:
		dev->ems_mar = val;
		break;

	case 0x01ef:
		switch(dev->cri) {
			case 0:
				dev->cr[0] = (val & 0x1f) | mem_conf_cr0[(mem_size > 640 ? mem_size : mem_size - 128) >> 9];
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
				memmap_state_update(dev);
				break;

			case 6:
				if (dev->revision == 8) {
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

    switch(addr) {
	case 0x01ec:
		dev->ems_mr[dev->ems_mar & 0x3f].mr = val;
		hl_ems_update(dev, dev->ems_mar & 0x3f);
		if (dev->ems_mar & 0x80)
			dev->ems_mar++;
		break;

	default:
		break;
    }
}


static void
hl_writel(uint16_t addr, uint32_t val, void *priv)
{
    hl_writew(addr, val, priv);
    hl_writew(addr + 2, val >> 16, priv);
}


static uint8_t
hl_read(uint16_t addr, void *priv)
{
    headland_t *dev = (headland_t *)priv;
    uint8_t ret = 0xff;

    switch(addr) {
	case 0x01ec:
		ret = (uint8_t)dev->ems_mr[dev->ems_mar & 0x3f].mr;
		if (dev->ems_mar & 0x80)
			dev->ems_mar++;
		break;

	case 0x01ed:
		if (dev->revision > 0)
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
				if (dev->revision == 8)
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


static uint32_t
hl_readl(uint16_t addr, void *priv)
{
    uint32_t ret = 0xffffffff;

    ret = hl_readw(addr, priv);
    ret |= (hl_readw(addr + 2, priv) << 16);

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
    int ht386 = 0;
    uint32_t i;

    dev = (headland_t *) malloc(sizeof(headland_t));
    memset(dev, 0x00, sizeof(headland_t));

    dev->revision = info->local;

    if (dev->revision > 0)
	ht386 = 1;

    dev->cr[4] = dev->revision << 4;

   if (ht386)
	device_add(&port_92_inv_device);

    io_sethandler(0x01ec, 4,
		  hl_read,hl_readw,hl_readl, hl_write,hl_writew,hl_writel, dev);

    dev->null_mr.valid = 0;
    dev->null_mr.mr = 0xff;
    dev->null_mr.headland = dev;

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
	mem_mapping_add(&dev->mid_mapping, 0xa0000, 0x40000,
			mem_read_b, mem_read_w, mem_read_l,
			mem_write_b, mem_write_w, mem_write_l,
			ram + 0xa0000, MEM_MAPPING_INTERNAL, &dev->null_mr);
	mem_mapping_disable(&dev->mid_mapping);
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
			mem_size > (256 + (i << 4)) ? (ram + 0x40000 + (i << 14)) : NULL,
			MEM_MAPPING_INTERNAL, &dev->null_mr);
	mem_mapping_enable(&dev->upper_mapping[i]);
    }

    mem_mapping_add(&dev->shadow_mapping[0],
		    0xe0000, 0x20000,
		    mem_read_b, mem_read_w, mem_read_l,
		    mem_write_b, mem_write_w, mem_write_l,
		    ((mem_size << 10) > 0xe0000) ? (ram + 0xe0000) : NULL,
		    MEM_MAPPING_INTERNAL, &dev->null_mr);
    mem_mapping_disable(&dev->shadow_mapping[0]);

    mem_mapping_add(&dev->shadow_mapping[1],
		    0xfe0000, 0x20000,
		    mem_read_b, mem_read_w, mem_read_l,
		    mem_write_b, mem_write_w, mem_write_l,
		    ((mem_size << 10) > 0xe0000) ? (ram + 0xe0000) : NULL,
		    MEM_MAPPING_INTERNAL, &dev->null_mr);
    mem_mapping_disable(&dev->shadow_mapping[1]);

    for (i = 0; i < 64; i++) {
	dev->ems_mr[i].mr = 0x00;
	mem_mapping_add(&dev->ems_mapping[i],
			((i & 31) + ((i & 31) >= 24 ? 24 : 16)) << 14, 0x04000,
			mem_read_b, mem_read_w, mem_read_l,
			mem_write_b, mem_write_w, mem_write_l,
			ram + (((i & 31) + ((i & 31) >= 24 ? 24 : 16)) << 14),
			MEM_MAPPING_INTERNAL, &dev->ems_mr[i]);
	mem_mapping_disable(&dev->ems_mapping[i]);
    }

    memmap_state_update(dev);

    return(dev);
}

const device_t headland_gc10x_device = {
    .name = "Headland GC101/102/103",
    .internal_name = "headland_gc10x",
    .flags = 0,
    .local = 0,
    .init = headland_init,
    .close = headland_close,
    .reset = NULL,
    { .available = NULL },
    .speed_changed = NULL,
    .force_redraw = NULL,
    .config = NULL
};

const device_t headland_ht18a_device = {
    .name = "Headland HT18 Rev. A",
    .internal_name = "headland_ht18a",
    .flags = 0,
    .local = 1,
    .init = headland_init,
    .close = headland_close,
    .reset = NULL,
    { .available = NULL },
    .speed_changed = NULL,
    .force_redraw = NULL,
    .config = NULL
};

const device_t headland_ht18b_device = {
    .name = "Headland HT18 Rev. B",
    .internal_name = "headland_ht18b",
    .flags = 0,
    .local = 2,
    .init = headland_init,
    .close = headland_close,
    .reset = NULL,
    { .available = NULL },
    .speed_changed = NULL,
    .force_redraw = NULL,
    .config = NULL
};

const device_t headland_ht18c_device = {
    .name = "Headland HT18 Rev. C",
    .internal_name = "headland_ht18c",
    .flags = 0,
    .local = 8,
    .init = headland_init,
    .close = headland_close,
    .reset = NULL,
    { .available = NULL },
    .speed_changed = NULL,
    .force_redraw = NULL,
    .config = NULL
};
