/*
 * 86Box	A hypervisor and IBM PC system emulator that specializes in
 *		running old operating systems and software designed for IBM
 *		PC systems and compatibles from 1981 through fairly recent
 *		system designs based on the PCI bus.
 *
 *		This file is part of the 86Box distribution.
 *
 *		Implementation of an SST flash chip.
 *
 * Version:	@(#)sst_flash.c	1.0.19	2019/06/25
 *
 * Authors:	Sarah Walker, <http://pcem-emulator.co.uk/>
 *		Miran Grca, <mgrca8@gmail.com>
 *      Melissa Goad, <mszoopers@protonmail.com>
 *
 *		Copyright 2008-2020 Sarah Walker.
 *		Copyright 2016-2020 Miran Grca.
 *      Copyright 2020 Melissa Goad.
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

typedef struct sst_t
{
    int			command_state, id_mode,
			erase, dirty;
        
    uint8_t		*array;

    mem_mapping_t	mapping[2], mapping_h[2];
} sst_t;


static wchar_t	flash_path[1024];


#define SST_CHIP_ERASE    0x10
#define SST_SECTOR_ERASE  0x30
#define SST_ERASE         0x80
#define SST_SET_ID_MODE   0x90
#define SST_BYTE_PROGRAM  0xa0
#define SST_CLEAR_ID_MODE 0xf0


static void
sst_new_command(sst_t *dev, uint8_t val)
{
    switch (val) {
	case SST_CHIP_ERASE:
		if (dev->erase)
			memset(dev->array, 0xff, 0x20000);
		dev->command_state = 0;
		dev->erase = 0;
		break;

	case SST_ERASE:
		dev->command_state = 0;
		dev->erase = 1;
		break;

	case SST_SET_ID_MODE:
		if (!dev->id_mode)
			dev->id_mode = 1;
		dev->command_state = 0;
		dev->erase = 0;
		break;

	case SST_BYTE_PROGRAM:
		dev->command_state = 3;
		dev->erase = 0;
		break;

	case SST_CLEAR_ID_MODE:
		if (dev->id_mode)
			dev->id_mode = 0;
		dev->command_state = 0;
		dev->erase = 0;
		break;

	default:
		dev->command_state = 0;
		dev->erase = 0;
    }
}


static void
sst_sector_erase(sst_t *dev, uint32_t addr)
{
    memset(&dev->array[addr & 0x1f000], 0xff, 4096);
    dev->dirty = 1;
}


static uint8_t
sst_read_id(uint32_t addr, void *p)
{
    if ((addr & 0xffff) == 0)
	return 0xbf;	/* SST */
    else if ((addr & 0xffff) == 1)
	return 0xb5;	/* 39SF010 */
    else
	return 0xff;
}


static void
sst_write(uint32_t addr, uint8_t val, void *p)
{
    sst_t *dev = (sst_t *) p;

    switch (dev->command_state) {
	case 0:
		if (val == 0xf0) {
			if (dev->id_mode)
				dev->id_mode = 0;
		} else if ((addr & 0xffff) == 0x5555 && val == 0xaa)
			dev->command_state = 1;
		else
			dev->command_state = 0;
		break;
	case 1:
		if ((addr & 0xffff) == 0x2aaa && val == 0x55)
			dev->command_state = 2;
		else
			dev->command_state = 0;
		break;
	case 2:
		if ((addr & 0xffff) == 0x5555)
			sst_new_command(dev, val);
		else if ((val == SST_SECTOR_ERASE) && dev->erase) {
			sst_sector_erase(dev, addr);
			dev->command_state = 0;
		} else
			dev->command_state = 0;
		break;
	case 3:
		dev->array[addr & 0x1ffff] = val;
		dev->command_state = 0;
		dev->dirty = 1;
		break;
    }
}


static uint8_t
sst_read(uint32_t addr, void *p)
{
    sst_t *dev = (sst_t *) p;
    uint8_t ret = 0xff;

    addr &= 0x000fffff;

    if (dev->id_mode)
	ret = sst_read_id(addr, p);
    else {
	if ((addr >= biosaddr) && (addr <= (biosaddr + biosmask)))
		ret = dev->array[addr - biosaddr];
    }

    return ret;
}


static uint16_t
sst_readw(uint32_t addr, void *p)
{
    sst_t *dev = (sst_t *) p;
    uint16_t ret = 0xffff;

    addr &= 0x000fffff;

    if (dev->id_mode)
	ret = sst_read(addr, p) | (sst_read(addr + 1, p) << 8);
    else {
	if ((addr >= biosaddr) && (addr <= (biosaddr + biosmask)))
		ret = *(uint16_t *)&dev->array[addr - biosaddr];
    }

    return ret;
}


static uint32_t
sst_readl(uint32_t addr, void *p)
{
    sst_t *dev = (sst_t *) p;
    uint32_t ret = 0xffffffff;

    addr &= 0x000fffff;

    if (dev->id_mode)
	ret = sst_readw(addr, p) | (sst_readw(addr + 2, p) << 16);
    else {
	if ((addr >= biosaddr) && (addr <= (biosaddr + biosmask)))
		ret = *(uint32_t *)&dev->array[addr - biosaddr];
    }

    return ret;
}


static void
sst_add_mappings(sst_t *dev)
{
    int i = 0;
    uint32_t base, fbase;

    for (i = 0; i < 2; i++) {
	base = 0xe0000 + (i << 16);
	fbase = base & biosmask; 

	memcpy(&dev->array[fbase], &rom[base & biosmask], 0x10000);

	mem_mapping_add(&(dev->mapping[i]), base, 0x10000,
			sst_read, sst_readw, sst_readl,
			sst_write, NULL, NULL,
			dev->array + fbase, MEM_MAPPING_EXTERNAL|MEM_MAPPING_ROMCS, (void *) dev);
	mem_mapping_add(&(dev->mapping_h[i]), (base | 0xfff00000), 0x10000,
			sst_read, sst_readw, sst_readl,
			sst_write, NULL, NULL,
			dev->array + fbase, MEM_MAPPING_EXTERNAL|MEM_MAPPING_ROMCS, (void *) dev);
    }
}


static void *
sst_39sf010_init(const device_t *info)
{
    FILE *f;
    sst_t *dev = malloc(sizeof(sst_t));
    memset(dev, 0, sizeof(sst_t));

    size_t l = strlen(machine_get_internal_name_ex(machine))+1;
    wchar_t *machine_name = (wchar_t *) malloc(l * sizeof(wchar_t));
    mbstowcs(machine_name, machine_get_internal_name_ex(machine), l);
    l = wcslen(machine_name)+5;
    wchar_t *flash_name = (wchar_t *)malloc(l*sizeof(wchar_t));
    swprintf(flash_name, l, L"%ls.bin", machine_name);

    if (wcslen(flash_name) <= 1024)
	wcscpy(flash_path, flash_name);
    else
	wcsncpy(flash_path, flash_name, 1024);

    mem_mapping_disable(&bios_mapping);
    mem_mapping_disable(&bios_high_mapping);

    dev->array = (uint8_t *) malloc(biosmask + 1);
    memset(dev->array, 0xff, biosmask + 1);

    sst_add_mappings(dev);

    f = nvr_fopen(flash_path, L"rb");
    if (f) {
	if (fread(&(dev->array[0x00000]), 1, 0x20000, f) != 0x20000)
		fatal("Less than 131072 bytes read from the SST Flash ROM file\n");
	fclose(f);
    }

    free(flash_name);
    free(machine_name);

    return dev;
}


static void
sst_39sf010_close(void *p)
{
    FILE *f;
    sst_t *dev = (sst_t *)p;

    f = nvr_fopen(flash_path, L"wb");
    fwrite(&(dev->array[0x00000]), 0x20000, 1, f);
    fclose(f);

    free(dev->array);
    dev->array = NULL;

    free(dev);
}


const device_t sst_flash_39sf010_device =
{
    "SST 39SF010 Flash BIOS",
    0,
    0,
    sst_39sf010_init,
    sst_39sf010_close,
    NULL,
    NULL, NULL, NULL, NULL
};
