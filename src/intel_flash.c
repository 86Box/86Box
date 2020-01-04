/*
 * 86Box	A hypervisor and IBM PC system emulator that specializes in
 *		running old operating systems and software designed for IBM
 *		PC systems and compatibles from 1981 through fairly recent
 *		system designs based on the PCI bus.
 *
 *		This file is part of the 86Box distribution.
 *
 *		Implementation of the Intel 1 Mbit and 2 Mbit, 8-bit and
 *		16-bit flash devices.
 *
 * Version:	@(#)intel_flash.c	1.0.19	2019/06/25
 *
 * Authors:	Sarah Walker, <http://pcem-emulator.co.uk/>
 *		Miran Grca, <mgrca8@gmail.com>
 *
 *		Copyright 2008-2019 Sarah Walker.
 *		Copyright 2016-2019 Miran Grca.
 */
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <stdlib.h>
#include <wchar.h>
#include "86box.h"
#include "device.h"
#include "mem.h"
#include "machine/machine.h"
#include "timer.h"
#include "nvr.h"
#include "plat.h"


#define FLAG_WORD	4
#define FLAG_BXB	2
#define FLAG_INV_A16	1


enum
{
    BLOCK_MAIN1,
    BLOCK_MAIN2,
    BLOCK_DATA1,
    BLOCK_DATA2,
    BLOCK_BOOT,
    BLOCKS_NUM
};

enum
{
    CMD_READ_ARRAY = 0xff,
    CMD_IID = 0x90,
    CMD_READ_STATUS = 0x70,
    CMD_CLEAR_STATUS = 0x50,
    CMD_ERASE_SETUP = 0x20,
    CMD_ERASE_CONFIRM = 0xd0,
    CMD_ERASE_SUSPEND = 0xb0,
    CMD_PROGRAM_SETUP = 0x40,
    CMD_PROGRAM_SETUP_ALT = 0x10
};


typedef struct flash_t
{
    uint8_t		command, status,
			pad, flags,
			*array;

    uint16_t		flash_id, pad16;

    uint32_t		program_addr,
			block_start[BLOCKS_NUM], block_end[BLOCKS_NUM],
			block_len[BLOCKS_NUM];

    mem_mapping_t	mapping[4], mapping_h[8];
} flash_t;


static wchar_t	flash_path[1024];


static uint8_t
flash_read(uint32_t addr, void *p)
{
    flash_t *dev = (flash_t *) p;
    uint8_t ret = 0xff;

    if (dev->flags & FLAG_INV_A16)
	addr ^= 0x10000;
    addr &= biosmask;

    switch (dev->command) {
	case CMD_READ_ARRAY:
	default:
		ret = dev->array[addr];
		break;

	case CMD_IID:
		if (addr & 1)
                        ret = dev->flash_id & 0xff;
		else
			ret = 0x89;
		break;

	case CMD_READ_STATUS:
		ret = dev->status;
		break;
    }

    return ret;
}


static uint16_t
flash_readw(uint32_t addr, void *p)
{
    flash_t *dev = (flash_t *) p;
    uint16_t *q;
    uint16_t ret = 0xffff;

    if (dev->flags & FLAG_INV_A16)
	addr ^= 0x10000;
    addr &= biosmask;

    if (dev->flags & FLAG_WORD)
	addr &= 0xfffffffe;

    q = (uint16_t *)&(dev->array[addr]);
    ret = *q;

    if (dev->flags & FLAG_WORD)  switch (dev->command) {
	case CMD_READ_ARRAY:
	default:
		break;

	case CMD_IID:
		if (addr & 2)
			ret = dev->flash_id;
		else		
			ret = 0x0089;
		break;

	case CMD_READ_STATUS:
		ret = dev->status;
		break;
    }

    return ret;
}


static uint32_t
flash_readl(uint32_t addr, void *p)
{
    flash_t *dev = (flash_t *)p;
    uint32_t *q;

    if (dev->flags & FLAG_INV_A16)
	addr ^= 0x10000;
    addr &= biosmask;

    q = (uint32_t *)&(dev->array[addr]);

    return *q;
}


static void
flash_write(uint32_t addr, uint8_t val, void *p)
{
    flash_t *dev = (flash_t *) p;
    int i;
    uint32_t bb_mask = biosmask & 0xffffe000;
    if (biosmask == 0x3ffff)
	bb_mask &= 0xffffc000;

    if (dev->flags & FLAG_INV_A16)
	addr ^= 0x10000;
    addr &= biosmask;

    switch (dev->command) {
	case CMD_ERASE_SETUP:
		if (val == CMD_ERASE_CONFIRM) {
			for (i = 0; i < 3; i++) {
				if ((addr >= dev->block_start[i]) && (addr <= dev->block_end[i]))
					memset(&(dev->array[dev->block_start[i]]), 0xff, dev->block_len[i]);
			}

			dev->status = 0x80;
		}
		dev->command = CMD_READ_STATUS;
		break;

	case CMD_PROGRAM_SETUP:
	case CMD_PROGRAM_SETUP_ALT:
		if (((addr & bb_mask) != (dev->block_start[4] & bb_mask)) && (addr == dev->program_addr))
			dev->array[addr] = val;
		dev->command = CMD_READ_STATUS;
		dev->status = 0x80;
		break;

	default:
		dev->command = val;
		switch (val) {
			case CMD_CLEAR_STATUS:
				dev->status = 0;
				break;
			case CMD_PROGRAM_SETUP:
			case CMD_PROGRAM_SETUP_ALT:
				dev->program_addr = addr;
				break;
		}
    }
}


static void
flash_writew(uint32_t addr, uint16_t val, void *p)
{
    flash_t *dev = (flash_t *) p;
    int i;
    uint32_t bb_mask = biosmask & 0xffffe000;
    if (biosmask == 0x3ffff)
	bb_mask &= 0xffffc000;

    if (dev->flags & FLAG_INV_A16)
	addr ^= 0x10000;
    addr &= biosmask;

    if (dev->flags & FLAG_WORD)  switch (dev->command) {
	case CMD_ERASE_SETUP:
		if (val == CMD_ERASE_CONFIRM) {
			for (i = 0; i < 3; i++) {
				if ((addr >= dev->block_start[i]) && (addr <= dev->block_end[i]))
					memset(&(dev->array[dev->block_start[i]]), 0xff, dev->block_len[i]);
			}

			dev->status = 0x80;
		}
		dev->command = CMD_READ_STATUS;
		break;

	case CMD_PROGRAM_SETUP:
	case CMD_PROGRAM_SETUP_ALT:
		if (((addr & bb_mask) != (dev->block_start[4] & bb_mask)) && (addr == dev->program_addr))
			*(uint16_t *) (&dev->array[addr]) = val;
		dev->command = CMD_READ_STATUS;
		dev->status = 0x80;
		break;

	default:
		dev->command = val & 0xff;
		switch (val) {
			case CMD_CLEAR_STATUS:
				dev->status = 0;
				break;
			case CMD_PROGRAM_SETUP:
			case CMD_PROGRAM_SETUP_ALT:
				dev->program_addr = addr;
				break;
		}
    }
}


static void
flash_writel(uint32_t addr, uint32_t val, void *p)
{
#if 0
    flash_writew(addr, val & 0xffff, p);
    flash_writew(addr + 2, (val >> 16) & 0xffff, p);
#endif
}


static void
intel_flash_add_mappings(flash_t *dev)
{
    int max = 2, i = 0;
    uint32_t base, fbase;

    if (biosmask == 0x3ffff)
	max = 4;

    for (i = 0; i < max; i++) {
	if (biosmask == 0x3ffff)
		base = 0xc0000 + (i << 16);
	else
		base = 0xe0000 + (i << 16);
	fbase = base & biosmask; 
	if (dev->flags & FLAG_INV_A16)
		fbase ^= 0x10000;

	memcpy(&dev->array[fbase], &rom[base & biosmask], 0x10000);

	if ((max == 2) || (i >= 2)) {
		mem_mapping_add(&(dev->mapping[i]), base, 0x10000,
				flash_read, flash_readw, flash_readl,
				flash_write, flash_writew, flash_writel,
				dev->array + fbase, MEM_MAPPING_EXTERNAL|MEM_MAPPING_ROMCS, (void *) dev);
	}
	mem_mapping_add(&(dev->mapping_h[i]), (base | 0xfff00000) - 0x40000, 0x10000,
			flash_read, flash_readw, flash_readl,
			flash_write, flash_writew, flash_writel,
			dev->array + fbase, MEM_MAPPING_EXTERNAL|MEM_MAPPING_ROMCS, (void *) dev);
	mem_mapping_add(&(dev->mapping_h[i + 4]), (base | 0xfff00000), 0x10000,
			flash_read, flash_readw, flash_readl,
			flash_write, flash_writew, flash_writel,
			dev->array + fbase, MEM_MAPPING_EXTERNAL|MEM_MAPPING_ROMCS, (void *) dev);
    }
}


static void *
intel_flash_init(const device_t *info)
{
    FILE *f;
    int l;
    flash_t *dev;
    wchar_t *machine_name, *flash_name;
    uint8_t type = info->local & 0xff;

    dev = malloc(sizeof(flash_t));
    memset(dev, 0, sizeof(flash_t));

    l = strlen(machine_get_internal_name_ex(machine))+1;
    machine_name = (wchar_t *) malloc(l * sizeof(wchar_t));
    mbstowcs(machine_name, machine_get_internal_name_ex(machine), l);
    l = wcslen(machine_name)+5;
    flash_name = (wchar_t *)malloc(l*sizeof(wchar_t));
    swprintf(flash_name, l, L"%ls.bin", machine_name);

    wcscpy(flash_path, flash_name);

    dev->flags = info->local & 0xff;

    mem_mapping_disable(&bios_mapping);
    mem_mapping_disable(&bios_high_mapping);

    dev->array = (uint8_t *) malloc(biosmask + 1);
    memset(dev->array, 0xff, biosmask + 1);

    if (biosmask == 0x3ffff) {
	if (dev->flags & FLAG_WORD)
		dev->flash_id = (dev->flags & FLAG_BXB) ? 0x2275 : 0x2274;
	else
		dev->flash_id = (dev->flags & FLAG_BXB) ? 0x7D : 0x7C;

	/* The block lengths are the same both flash types. */
	dev->block_len[BLOCK_MAIN1] = 0x20000;
	dev->block_len[BLOCK_MAIN2] = 0x18000;
	dev->block_len[BLOCK_DATA1] = 0x02000;
	dev->block_len[BLOCK_DATA2] = 0x02000;
	dev->block_len[BLOCK_BOOT] = 0x04000;

	if (dev->flags & FLAG_BXB) {		/* 28F002BX-B/28F200BX-B */
		dev->block_start[BLOCK_MAIN1] = 0x20000;	/* MAIN BLOCK 1 */
		dev->block_end[BLOCK_MAIN1] = 0x3ffff;
		dev->block_start[BLOCK_MAIN2] = 0x08000;	/* MAIN BLOCK 2 */
		dev->block_end[BLOCK_MAIN2] = 0x1ffff;
		dev->block_start[BLOCK_DATA1] = 0x06000;	/* DATA AREA 1 BLOCK */
		dev->block_end[BLOCK_DATA1] = 0x07fff;
		dev->block_start[BLOCK_DATA2] = 0x04000;	/* DATA AREA 2 BLOCK */
		dev->block_end[BLOCK_DATA2] = 0x05fff;
		dev->block_start[BLOCK_BOOT] = 0x00000;		/* BOOT BLOCK */
		dev->block_end[BLOCK_BOOT] = 0x03fff;
	} else {					/* 28F002BX-T/28F200BX-T */
		dev->block_start[BLOCK_MAIN1] = 0x00000;	/* MAIN BLOCK 1 */
		dev->block_end[BLOCK_MAIN1] = 0x1ffff;
		dev->block_start[BLOCK_MAIN2] = 0x20000;	/* MAIN BLOCK 2 */
		dev->block_end[BLOCK_MAIN2] = 0x37fff;
		dev->block_start[BLOCK_DATA1] = 0x38000;	/* DATA AREA 1 BLOCK */
		dev->block_end[BLOCK_DATA1] = 0x39fff;
		dev->block_start[BLOCK_DATA2] = 0x3a000;	/* DATA AREA 2 BLOCK */
		dev->block_end[BLOCK_DATA2] = 0x3bfff;
		dev->block_start[BLOCK_BOOT] = 0x3c000;		/* BOOT BLOCK */
		dev->block_end[BLOCK_BOOT] = 0x3ffff;
	}
    } else {
	dev->flash_id = (type & FLAG_BXB) ? 0x95 : 0x94;

	/* The block lengths are the same both flash types. */
	dev->block_len[BLOCK_MAIN1] = 0x1c000;
	dev->block_len[BLOCK_MAIN2] = 0x00000;
	dev->block_len[BLOCK_DATA1] = 0x01000;
	dev->block_len[BLOCK_DATA2] = 0x01000;
	dev->block_len[BLOCK_BOOT] = 0x02000;

	if (dev->flags & FLAG_BXB) {		/* 28F001BX-B/28F100BX-B */
		dev->block_start[BLOCK_MAIN1] = 0x04000;	/* MAIN BLOCK 1 */
		dev->block_end[BLOCK_MAIN1] = 0x1ffff;
		dev->block_start[BLOCK_MAIN2] = 0xfffff;	/* MAIN BLOCK 2 */
		dev->block_end[BLOCK_MAIN2] = 0xfffff;
		dev->block_start[BLOCK_DATA1] = 0x02000;	/* DATA AREA 1 BLOCK */
		dev->block_end[BLOCK_DATA1] = 0x02fff;
		dev->block_start[BLOCK_DATA2] = 0x03000;	/* DATA AREA 2 BLOCK */
		dev->block_end[BLOCK_DATA2] = 0x03fff;
		dev->block_start[BLOCK_BOOT] = 0x00000;		/* BOOT BLOCK */
		dev->block_end[BLOCK_BOOT] = 0x01fff;
	} else {					/* 28F001BX-T/28F100BX-T */
		dev->block_start[BLOCK_MAIN1] = 0x00000;	/* MAIN BLOCK 1 */
		dev->block_end[BLOCK_MAIN1] = 0x1bfff;
		dev->block_start[BLOCK_MAIN2] = 0xfffff;	/* MAIN BLOCK 2 */
		dev->block_end[BLOCK_MAIN2] = 0xfffff;
		dev->block_start[BLOCK_DATA1] = 0x1c000;	/* DATA AREA 1 BLOCK */
		dev->block_end[BLOCK_DATA1] = 0x1cfff;
		dev->block_start[BLOCK_DATA2] = 0x1d000;	/* DATA AREA 2 BLOCK */
		dev->block_end[BLOCK_DATA2] = 0x1dfff;
		dev->block_start[BLOCK_BOOT] = 0x1e000;		/* BOOT BLOCK */
		dev->block_end[BLOCK_BOOT] = 0x1ffff;
	}
    }

    intel_flash_add_mappings(dev);

    dev->command = CMD_READ_ARRAY;
    dev->status = 0;

    f = nvr_fopen(flash_path, L"rb");
    if (f) {
	fread(&(dev->array[dev->block_start[BLOCK_MAIN1]]), dev->block_len[BLOCK_MAIN1], 1, f);
	if (dev->block_len[BLOCK_MAIN2])
		fread(&(dev->array[dev->block_start[BLOCK_MAIN2]]), dev->block_len[BLOCK_MAIN2], 1, f);
	fread(&(dev->array[dev->block_start[BLOCK_DATA1]]), dev->block_len[BLOCK_DATA1], 1, f);
	fread(&(dev->array[dev->block_start[BLOCK_DATA2]]), dev->block_len[BLOCK_DATA2], 1, f);
	fclose(f);
    }

    free(flash_name);
    free(machine_name);

    return dev;
}


static void
intel_flash_close(void *p)
{
    FILE *f;
    flash_t *dev = (flash_t *)p;

    f = nvr_fopen(flash_path, L"wb");
    fwrite(&(dev->array[dev->block_start[BLOCK_MAIN1]]), dev->block_len[BLOCK_MAIN1], 1, f);
    if (dev->block_len[BLOCK_MAIN2])
	fwrite(&(dev->array[dev->block_start[BLOCK_MAIN2]]), dev->block_len[BLOCK_MAIN2], 1, f);
    fwrite(&(dev->array[dev->block_start[BLOCK_DATA1]]), dev->block_len[BLOCK_DATA1], 1, f);
    fwrite(&(dev->array[dev->block_start[BLOCK_DATA2]]), dev->block_len[BLOCK_DATA2], 1, f);
    fclose(f);

    free(dev);
}


/* For AMI BIOS'es - Intel 28F001BXT with A16 pin inverted. */
const device_t intel_flash_bxt_ami_device =
{
    "Intel 28F001BXT/28F002BXT Flash BIOS",
    0,
    FLAG_INV_A16,
    intel_flash_init,
    intel_flash_close,
    NULL,
    NULL, NULL, NULL, NULL
};


#if defined(DEV_BRANCH) && defined(USE_TC430HX)
const device_t intel_flash_bxtw_ami_device =
{
    "Intel 28F100BXT/28F200BXT Flash BIOS",
    0,
    FLAG_INV_A16 | FLAG_WORD,
    intel_flash_init,
    intel_flash_close,
    NULL,
    NULL, NULL, NULL, NULL
};
#endif


const device_t intel_flash_bxt_device =
{
    "Intel 28F001BXT/28F002BXT Flash BIOS",
    0, 0,
    intel_flash_init,
    intel_flash_close,
    NULL,
    NULL, NULL, NULL, NULL
};


const device_t intel_flash_bxb_device =
{
    "Intel 28F001BXB/28F002BXB Flash BIOS",
    0, FLAG_BXB,
    intel_flash_init,
    intel_flash_close,
    NULL,
    NULL, NULL, NULL, NULL
};

const device_t intel_flash_bxb_ast_device =
{
    "Intel 28F001BXB/28F002BXB Flash BIOS (AST Adventure!)",
    0, FLAG_BXB | FLAG_INV_A16,
    intel_flash_init,
    intel_flash_close,
    NULL,
    NULL, NULL, NULL, NULL
};
