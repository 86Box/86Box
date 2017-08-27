/*
 * 86Box	A hypervisor and IBM PC system emulator that specializes in
 *		running old operating systems and software designed for IBM
 *		PC systems and compatibles from 1981 through fairly recent
 *		system designs based on the PCI bus.
 *
 *		This file is part of the 86Box distribution.
 *
 *		Implementation of the Intel 2 Mbit 8-bit flash devices.
 *
 * Version:	@(#)intel_flash.c	1.0.1	2017/08/23
 *
 * Authors:	Sarah Walker, <http://pcem-emulator.co.uk/>
 *		Miran Grca, <mgrca8@gmail.com>
 *		Copyright 2008-2017 Sarah Walker.
 *		Copyright 2016-2017 Miran Grca.
 */

#include <stdlib.h>
#include "ibm.h"
#include "cpu/cpu.h"
#include "device.h"
#include "mem.h"
#include "model.h"
#include "rom.h"

#define FLASH_IS_BXB	2
#define FLASH_INVERT	1

#define BLOCK_MAIN	0
#define BLOCK_DATA1	1
#define BLOCK_DATA2	2
#define BLOCK_BOOT	3

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
        uint8_t command, status;
	uint8_t flash_id;
	int invert_high_pin;
	mem_mapping_t mapping[8], mapping_h[8];
	uint32_t block_start[4], block_end[4], block_len[4];
	uint8_t array[131072];
} flash_t;

static wchar_t flash_path[1024];

static uint8_t flash_read(uint32_t addr, void *p)
{
        flash_t *flash = (flash_t *)p;
	if (flash->invert_high_pin)
	{
		addr ^= 0x10000;
		if (addr & 0xfff00000)  return flash->array[addr & 0x1ffff];
	}
	addr &= 0x1ffff;
        switch (flash->command)
        {
		case CMD_READ_ARRAY:
                default:
                return flash->array[addr];

                case CMD_IID:
                if (addr & 1)
                        return flash->flash_id;
                return 0x89;

                case CMD_READ_STATUS:
                return flash->status;                
        }
}

static uint16_t flash_readw(uint32_t addr, void *p)
{
        flash_t *flash = (flash_t *)p;
	uint16_t *q;
	addr &= 0x1ffff;
	if (flash->invert_high_pin)  addr ^= 0x10000;
	q = (uint16_t *)&(flash->array[addr]);
	return *q;
}

static uint32_t flash_readl(uint32_t addr, void *p)
{
        flash_t *flash = (flash_t *)p;
	uint32_t *q;
	addr &= 0x1ffff;
	if (flash->invert_high_pin)  addr ^= 0x10000;
	q = (uint32_t *)&(flash->array[addr]);
	return *q;
}

static void flash_write(uint32_t addr, uint8_t val, void *p)
{
        flash_t *flash = (flash_t *)p;
	int i;

	if (flash->invert_high_pin)
	{
		addr ^= 0x10000;
		if (addr & 0xfff00000)  return;
	}
	addr &= 0x1ffff;

        switch (flash->command)
        {
                case CMD_ERASE_SETUP:
                if (val == CMD_ERASE_CONFIRM)
                {
			for (i = 0; i < 3; i++)
			{
                        	if ((addr >= flash->block_start[i]) && (addr <= flash->block_end[i]))
                                	memset(&(flash->array[flash->block_start[i]]), 0xff, flash->block_len[i]);
			}

                        flash->status = 0x80;
                }
                flash->command = CMD_READ_STATUS;
                break;
                
                case CMD_PROGRAM_SETUP:
                case CMD_PROGRAM_SETUP_ALT:
                if ((addr & 0x1e000) != (flash->block_start[3] & 0x1e000))
       	                flash->array[addr] = val;
                flash->command = CMD_READ_STATUS;
                flash->status = 0x80;
                break;
                
                default:
                flash->command = val;
                switch (val)
                {
                        case CMD_CLEAR_STATUS:
                        flash->status = 0;
                        break;                                
                }
        }
}

static void intel_flash_add_mappings(flash_t *flash)
{
	int i = 0;

	for (i = 0; i <= 7; i++)
	{
		mem_mapping_add(&(flash->mapping[i]), 0xe0000 + (i << 14), 0x04000, flash_read,   flash_readw,   flash_readl,   flash_write, mem_write_nullw, mem_write_nulll, flash->array + ((i << 14) & 0x1ffff),                       MEM_MAPPING_EXTERNAL, (void *)flash);
		mem_mapping_add(&(flash->mapping_h[i]), 0xfffe0000 + (i << 14), 0x04000, flash_read,   flash_readw,   flash_readl,   flash_write, mem_write_nullw, mem_write_nulll, flash->array + ((i << 14) & 0x1ffff),                       0, (void *)flash);
	}
}

/* This is for boards which invert the high pin - the flash->array pointers need to pointer invertedly in order for INTERNAL writes to go to the right part of the array. */
static void intel_flash_add_mappings_inverted(flash_t *flash)
{
	int i = 0;

	for (i = 0; i <= 7; i++)
	{
		mem_mapping_add(&(flash->mapping[i]), 0xe0000 + (i << 14), 0x04000, flash_read,   flash_readw,   flash_readl,   flash_write, mem_write_nullw, mem_write_nulll, flash->array + (((i << 14) ^ 0x10000) & 0x1ffff),                       MEM_MAPPING_EXTERNAL, (void *)flash);
		mem_mapping_add(&(flash->mapping_h[i]), 0xfffe0000 + (i << 14), 0x04000, flash_read,   flash_readw,   flash_readl,   flash_write, mem_write_nullw, mem_write_nulll, flash->array + (((i << 14) ^ 0x10000) & 0x1ffff),                       0, (void *)flash);
	}
}

void *intel_flash_init(uint8_t type)
{
        FILE *f;
	int i;
        flash_t *flash;
	wchar_t *model_name;
	wchar_t *flash_name;

	flash = malloc(sizeof(flash_t));
        memset(flash, 0, sizeof(flash_t));

	model_name = (wchar_t *) malloc((strlen(model_get_internal_name_ex(model)) << 1) + 2);
	mbstowcs(model_name, model_get_internal_name_ex(model), strlen(model_get_internal_name_ex(model)) + 1);
	flash_name = (wchar_t *) malloc((wcslen(model_name) << 1) + 2 + 8);
	_swprintf(flash_name, L"%s.bin", model_name);

	wcscpy(flash_path, flash_name);

	pclog_w(L"Flash path: %s\n", flash_name);

	flash->flash_id = (type & FLASH_IS_BXB) ? 0x95 : 0x94;
	flash->invert_high_pin = (type & FLASH_INVERT);

	/* The block lengths are the same both flash types. */
	flash->block_len[BLOCK_MAIN] = 0x1c000;
	flash->block_len[BLOCK_DATA1] = 0x01000;
	flash->block_len[BLOCK_DATA2] = 0x01000;
	flash->block_len[BLOCK_BOOT] = 0x02000;

	if (type & FLASH_IS_BXB)			/* 28F001BX-B */
	{
		flash->block_start[BLOCK_MAIN] = 0x04000;	/* MAIN BLOCK */
		flash->block_end[BLOCK_MAIN] = 0x1ffff;
		flash->block_start[BLOCK_DATA1] = 0x03000;	/* DATA AREA 1 BLOCK */
		flash->block_end[BLOCK_DATA1] = 0x03fff;
		flash->block_start[BLOCK_DATA2] = 0x04000;	/* DATA AREA 2 BLOCK */
		flash->block_end[BLOCK_DATA2] = 0x04fff;
		flash->block_start[BLOCK_BOOT] = 0x00000;	/* BOOT BLOCK */
		flash->block_end[BLOCK_BOOT] = 0x01fff;
	}
	else						/* 28F001BX-T */
	{
		flash->block_start[BLOCK_MAIN] = 0x00000;	/* MAIN BLOCK */
		flash->block_end[BLOCK_MAIN] = 0x1bfff;
		flash->block_start[BLOCK_DATA1] = 0x1c000;	/* DATA AREA 1 BLOCK */
		flash->block_end[BLOCK_DATA1] = 0x1cfff;
		flash->block_start[BLOCK_DATA2] = 0x1d000;	/* DATA AREA 2 BLOCK */
		flash->block_end[BLOCK_DATA2] = 0x1dfff;
		flash->block_start[BLOCK_BOOT] = 0x1e000;	/* BOOT BLOCK */
		flash->block_end[BLOCK_BOOT] = 0x1ffff;
	}

	for (i = 0; i < 8; i++)
	{
		mem_mapping_disable(&bios_mapping[i]);
		mem_mapping_disable(&bios_high_mapping[i]);
	}

	if (flash->invert_high_pin)
	{
		memcpy(flash->array, rom + 65536, 65536);
		memcpy(flash->array + 65536, rom, 65536);
	}
	else
	{
		memcpy(flash->array, rom, 131072);
	}

	if (flash->invert_high_pin)
	{
		intel_flash_add_mappings_inverted(flash);
	}
	else
	{
		intel_flash_add_mappings(flash);
	}

        flash->command = CMD_READ_ARRAY;
        flash->status = 0;

        f = nvrfopen(flash_path, L"rb");
        if (f)
        {
                fread(&(flash->array[flash->block_start[BLOCK_MAIN]]), flash->block_len[BLOCK_MAIN], 1, f);
       	        fread(&(flash->array[flash->block_start[BLOCK_DATA1]]), flash->block_len[BLOCK_DATA1], 1, f);
                fread(&(flash->array[flash->block_start[BLOCK_DATA2]]), flash->block_len[BLOCK_DATA2], 1, f);
                fclose(f);
        }

	free(flash_name);
	free(model_name);

        return flash;
}

void *intel_flash_bxb_ami_init()
{
	return intel_flash_init(FLASH_IS_BXB | FLASH_INVERT);
}

/* For AMI BIOS'es - Intel 28F001BXT with high address pin inverted. */
void *intel_flash_bxt_ami_init()
{
	return intel_flash_init(FLASH_INVERT);
}

/* For Award BIOS'es - Intel 28F001BXT with high address pin not inverted. */
void *intel_flash_bxt_init()
{
	return intel_flash_init(0);
}

/* For Acer BIOS'es - Intel 28F001BXB. */
void *intel_flash_bxb_init()
{
	return intel_flash_init(FLASH_IS_BXB);
}

void intel_flash_close(void *p)
{
        FILE *f;
        flash_t *flash = (flash_t *)p;

        f = nvrfopen(flash_path, L"wb");
        fwrite(&(flash->array[flash->block_start[BLOCK_MAIN]]), flash->block_len[BLOCK_MAIN], 1, f);
        fwrite(&(flash->array[flash->block_start[BLOCK_DATA1]]), flash->block_len[BLOCK_DATA1], 1, f);
        fwrite(&(flash->array[flash->block_start[BLOCK_DATA2]]), flash->block_len[BLOCK_DATA2], 1, f);
        fclose(f);
        
        free(flash);
}

device_t intel_flash_bxt_ami_device =
{
        "Intel 28F001BXT Flash BIOS",
        0,
        intel_flash_bxt_ami_init,
        intel_flash_close,
        NULL,
        NULL,
        NULL,
        NULL,
        NULL
};

device_t intel_flash_bxb_ami_device =
{
        "Intel 28F001BXB Flash BIOS",
        0,
        intel_flash_bxb_ami_init,
        intel_flash_close,
        NULL,
        NULL,
        NULL,
        NULL,
        NULL
};

device_t intel_flash_bxt_device =
{
        "Intel 28F001BXT Flash BIOS",
        0,
        intel_flash_bxt_init,
        intel_flash_close,
        NULL,
        NULL,
        NULL,
        NULL,
        NULL
};

device_t intel_flash_bxb_device =
{
        "Intel 28F001BXB Flash BIOS",
        0,
        intel_flash_bxb_init,
        intel_flash_close,
        NULL,
        NULL,
        NULL,
        NULL,
        NULL
};
