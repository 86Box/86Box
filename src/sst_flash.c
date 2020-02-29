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
 * Version:	@(#)sst_flash.c	1.0.1	2020/02/03
 *
 * Authors:	Sarah Walker, <http://pcem-emulator.co.uk/>
 *		Miran Grca, <mgrca8@gmail.com>
 *		Melissa Goad, <mszoopers@protonmail.com>
 *
 *		Copyright 2008-2020 Sarah Walker.
 *		Copyright 2016-2020 Miran Grca.
 *		Copyright 2020 Melissa Goad.
 */
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <stdlib.h>
#include <wchar.h>
#include "86box.h"
#include "device.h"
#include "mem.h"
#include "machine.h"
#include "timer.h"
#include "nvr.h"
#include "plat.h"


typedef struct sst_t
{
    uint8_t		id, is_39, page_bytes, pad;

    int			command_state, id_mode,
			erase, dirty;

    uint32_t		size, mask;
        
    uint8_t		*array;

    mem_mapping_t	mapping[8], mapping_h[8];

    pc_timer_t		page_load_timer;
} sst_t;


static wchar_t	flash_path[1024];


#define SST_CHIP_ERASE    	0x10	/* Both 29 and 39, 6th cycle */
#define SST_SECTOR_ERASE  	0x30	/* Only 39, 6th cycle */
#define SST_SET_ID_MODE_ALT	0x60	/* Only 29, 6th cycle */
#define SST_ERASE         	0x80	/* Both 29 and 39 */
					/* With data 60h on 6th cycle, it's alt. ID */
#define SST_SET_ID_MODE   	0x90	/* Both 29 and 39 */
#define SST_BYTE_PROGRAM  	0xa0	/* Both 29 and 39 */
#define SST_CLEAR_ID_MODE 	0xf0	/* Both 29 and 39 */
					/* 1st cycle variant only on 39 */

#define SST_ID_MANUFACTURER	0xbf	/* SST Manufacturer's ID */
#define SST_ID_SST29EE010	0x07
#define SST_ID_SST29LE_VE010	0x08
#define SST_ID_SST29EE020	0x10
#define SST_ID_SST29LE_VE020	0x12
#define SST_ID_SST39SF512	0xb4
#define SST_ID_SST39SF010	0xb5
#define SST_ID_SST39SF020	0xb6
#define SST_ID_SST39SF040	0xb7


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

	case SST_SET_ID_MODE_ALT:
		if (!dev->is_39 && dev->erase && !dev->id_mode)
			dev->id_mode = 1;
		dev->command_state = 0;
		dev->erase = 0;
		break;

	case SST_BYTE_PROGRAM:
		dev->command_state = 3;
		if (!dev->is_39) {
			dev->page_bytes = 0;
			timer_set_delay_u64(&dev->page_load_timer, 100 * TIMER_USEC);
		}
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
    memset(&dev->array[addr & (dev->mask & ~0xfff)], 0xff, 4096);
    dev->dirty = 1;
}


static uint8_t
sst_read_id(uint32_t addr, void *p)
{
    sst_t *dev = (sst_t *) p;

    if ((addr & 0xffff) == 0)
	return SST_ID_MANUFACTURER;	/* SST */
    else if ((addr & 0xffff) == 1)
	return dev->id;
    else
	return 0xff;
}


static void
sst_write(uint32_t addr, uint8_t val, void *p)
{
    sst_t *dev = (sst_t *) p;

    switch (dev->command_state) {
	case 0:
		/* 1st Bus Write Cycle */
		if ((val == 0xf0) && dev->is_39) {
			if (dev->id_mode)
				dev->id_mode = 0;
		} else if ((addr & 0xffff) == 0x5555 && val == 0xaa)
			dev->command_state = 1;
		else
			dev->command_state = 0;
		break;
	case 1:
		/* 2nd Bus Write Cycle */
		if ((addr & 0xffff) == 0x2aaa && val == 0x55)
			dev->command_state = 2;
		else
			dev->command_state = 0;
		break;
	case 2:
		/* 3rd Bus Write Cycle */
		if ((addr & 0xffff) == 0x5555)
			sst_new_command(dev, val);
		else if (dev->is_39 && (val == SST_SECTOR_ERASE) && dev->erase) {
			sst_sector_erase(dev, addr);
			dev->command_state = 0;
		} else
			dev->command_state = 0;
		break;
	case 3:
		dev->array[addr & dev->mask] = val;
		if (!dev->is_39) {
			timer_disable(&dev->page_load_timer);
			if (dev->page_bytes == 0)
				timer_set_delay_u64(&dev->page_load_timer, 100 * TIMER_USEC);
			else
				timer_set_delay_u64(&dev->page_load_timer, 200 * TIMER_USEC);
			dev->page_bytes++;
		} else
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
sst_page_load(void *priv)
{
    sst_t *dev = (sst_t *) priv;

    dev->command_state = 0;
}


static void
sst_add_mappings(sst_t *dev)
{
    int i = 0, count;
    uint32_t base, fbase;
    uint32_t root_base;

    count = dev->size >> 16;
    root_base = 0x100000 - dev->size;

    for (i = 0; i < count; i++) {
	base = root_base + (i << 16);
	fbase = base & biosmask; 

	memcpy(&dev->array[fbase], &rom[base & biosmask], 0x10000);

	if (base >= 0xe0000) {
		mem_mapping_add(&(dev->mapping[i]), base, 0x10000,
				sst_read, sst_readw, sst_readl,
				sst_write, NULL, NULL,
				dev->array + fbase, MEM_MAPPING_EXTERNAL|MEM_MAPPING_ROMCS, (void *) dev);
	}
	mem_mapping_add(&(dev->mapping_h[i]), (base | 0xfff00000), 0x10000,
			sst_read, sst_readw, sst_readl,
			sst_write, NULL, NULL,
			dev->array + fbase, MEM_MAPPING_EXTERNAL|MEM_MAPPING_ROMCS, (void *) dev);
    }
}


static void *
sst_init(const device_t *info)
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

    dev->id = info->local;
    dev->is_39 = (dev->id >= SST_ID_SST39SF512);

    if (dev->id == SST_ID_SST39SF512)
	dev->size = 0x10000;
    else if ((dev->id == SST_ID_SST29EE020) || (dev->id == SST_ID_SST29LE_VE020) || (dev->id == SST_ID_SST39SF020))
	dev->size = 0x40000;
    else if (dev->id == SST_ID_SST39SF040)
	dev->size = 0x80000;
    else
	dev->size = 0x20000;
    dev->mask = dev->size - 1;

    sst_add_mappings(dev);

    f = nvr_fopen(flash_path, L"rb");
    if (f) {
	if (fread(&(dev->array[0x00000]), 1, dev->size, f) != dev->size)
		fatal("Less than %i bytes read from the SST Flash ROM file\n", dev->size);
	fclose(f);
    }

    free(flash_name);
    free(machine_name);

    if (!dev->is_39)
	timer_add(&dev->page_load_timer, sst_page_load, dev, 0);

    return dev;
}


static void
sst_close(void *p)
{
    FILE *f;
    sst_t *dev = (sst_t *)p;

    f = nvr_fopen(flash_path, L"wb");
    fwrite(&(dev->array[0x00000]), dev->size, 1, f);
    fclose(f);

    free(dev->array);
    dev->array = NULL;

    free(dev);
}


const device_t sst_flash_29ee010_device =
{
    "SST 29EE010 Flash BIOS",
    0,
    SST_ID_SST29EE010,
    sst_init,
    sst_close,
    NULL,
    NULL, NULL, NULL, NULL
};


const device_t sst_flash_29ee020_device =
{
    "SST 29EE020 Flash BIOS",
    0,
    SST_ID_SST29EE020,
    sst_init,
    sst_close,
    NULL,
    NULL, NULL, NULL, NULL
};


const device_t sst_flash_39sf010_device =
{
    "SST 39SF010 Flash BIOS",
    0,
    SST_ID_SST39SF010,
    sst_init,
    sst_close,
    NULL,
    NULL, NULL, NULL, NULL
};


const device_t sst_flash_39sf020_device =
{
    "SST 39SF020 Flash BIOS",
    0,
    SST_ID_SST39SF020,
    sst_init,
    sst_close,
    NULL,
    NULL, NULL, NULL, NULL
};
