/* Copyright holders: Sarah Walker, Tenshi
   see COPYING for more details
*/
#include <stdlib.h>
#include "ibm.h"
#include "device.h"
#include "mem.h"

#define FLASH_ALLOW_16	4
#define FLASH_IS_BXB	2
#define FLASH_INVERT	1

#define BLOCK_MAIN	0
#define BLOCK_DMI	1
#define BLOCK_ESCD	2
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
        CMD_PROGRAM_SETUP2 = 0x10
};

typedef struct flash_t
{
        uint32_t command, status;
	uint32_t flash_id;
	uint8_t type;			/* 0 = BXT, 1 = BXB */
	uint8_t invert_high_pin;	/* 0 = no, 1 = yes */
	uint8_t allow_word_write;	/* 0 = no, 1 = yes */
	mem_mapping_t mapping[8], mapping_h[8];
	uint32_t block_start[4], block_end[4], block_len[4];
	uint8_t array[131072];
} flash_t;

char flash_path[1024];

static uint8_t flash_read(uint32_t addr, void *p)
{
        flash_t *flash = (flash_t *)p;
	if (flash->invert_high_pin)
	{
	        // pclog("flash_read : addr=%08x/%08x val=%02x command=%02x %04x:%08x\n", addr, addr ^ 0x10000, flash->array[(addr ^ 0x10000) & 0x1ffff], flash->command, CS, cpu_state.pc);
		addr ^= 0x10000;
		if (addr & 0xfff00000)  return flash->array[addr & 0x1ffff];
	}
        // pclog("flash_read : addr=%08x command=%02x %04x:%08x\n", addr, flash->command, CS, cpu_state.pc);
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
	// pclog("flash_readw(%08X)\n", addr);
        flash_t *flash = (flash_t *)p;
	if (!flash->allow_word_write)
	{
		addr &= 0x1ffff;
		if (flash->invert_high_pin)  addr ^= 0x10000;
		return *(uint16_t *)&(flash->array[addr]);
	}
	if (flash->invert_high_pin)
	{
		addr ^= 0x10000;
		if (addr & 0xfff00000)  *(uint16_t *)&(flash->array[addr & 0x1ffff]);
	}
	addr &= 0x1ffff;
        switch (flash->command)
        {
		case CMD_READ_ARRAY:
                default:
                return *(uint16_t *)&(flash->array[addr]);

                case CMD_IID:
		// pclog("Flash Read ID 16: %08X\n", addr);
                if (addr & 1)
                        return 0x2274;
                return 0x89;

                case CMD_READ_STATUS:
                return flash->status;                
        }
}

static uint32_t flash_readl(uint32_t addr, void *p)
{
        flash_t *flash = (flash_t *)p;
	addr &= 0x1ffff;
	if (flash->invert_high_pin)  addr ^= 0x10000;
	return *(uint32_t *)&(flash->array[addr]);
}

static void flash_write(uint32_t addr, uint8_t val, void *p)
{
        flash_t *flash = (flash_t *)p;
	int i;
        // pclog("flash_write : addr=%08x val=%02x command=%02x %04x:%08x\n", addr, val, flash->command, CS, cpu_state.pc);        

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
                        // pclog("flash_write: erase %05x\n", addr);

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
                case CMD_PROGRAM_SETUP2:
                // pclog("flash_write: program %05x %02x\n", addr, val);
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

static void flash_writew(uint32_t addr, uint16_t val, void *p)
{
        flash_t *flash = (flash_t *)p;
	int i;
        // pclog("flash_writew : addr=%08x val=%02x command=%02x %04x:%08x\n", addr, val, flash->command, CS, cpu_state.pc);        

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
                        // pclog("flash_writew: erase %05x\n", addr);

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
                case CMD_PROGRAM_SETUP2:
                // pclog("flash_writew: program %05x %02x\n", addr, val);
                if ((addr & 0x1e000) != (flash->block_start[3] & 0x1e000))
       	                *(uint16_t *)&(flash->array[addr]) = val;
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

void intel_flash_add_mappings(flash_t *flash)
{
	int i = 0;

	for (i = 0; i <= 7; i++)
	{
		if (flash->allow_word_write)
		{
			mem_mapping_add(&(flash->mapping[i]), 0xe0000 + (i << 14), 0x04000, flash_read,   flash_readw,   flash_readl,   flash_write, flash_writew, mem_write_nulll, flash->array + ((i << 14) & 0x1ffff),                       MEM_MAPPING_EXTERNAL, (void *)flash);
			mem_mapping_add(&(flash->mapping_h[i]), 0xfffe0000 + (i << 14), 0x04000, flash_read,   flash_readw,   flash_readl,   flash_write, flash_writew, mem_write_nulll, flash->array + ((i << 14) & 0x1ffff),                       0, (void *)flash);
		}
		else
		{
			mem_mapping_add(&(flash->mapping[i]), 0xe0000 + (i << 14), 0x04000, flash_read,   flash_readw,   flash_readl,   flash_write, mem_write_nullw, mem_write_nulll, flash->array + ((i << 14) & 0x1ffff),                       MEM_MAPPING_EXTERNAL, (void *)flash);
			mem_mapping_add(&(flash->mapping_h[i]), 0xfffe0000 + (i << 14), 0x04000, flash_read,   flash_readw,   flash_readl,   flash_write, mem_write_nullw, mem_write_nulll, flash->array + ((i << 14) & 0x1ffff),                       0, (void *)flash);
		}
	}
}

/* This is for boards which invert the high pin - the flash->array pointers need to pointer invertedly in order for INTERNAL writes to go to the right part of the array. */
void intel_flash_add_mappings_inverted(flash_t *flash)
{
	int i = 0;

	for (i = 0; i <= 7; i++)
	{
		if (flash->allow_word_write)
		{
			mem_mapping_add(&(flash->mapping[i]), 0xe0000 + (i << 14), 0x04000, flash_read,   flash_readw,   flash_readl,   flash_write, flash_writew, mem_write_nulll, flash->array + (((i << 14) ^ 0x10000) & 0x1ffff),                       MEM_MAPPING_EXTERNAL, (void *)flash);
			mem_mapping_add(&(flash->mapping_h[i]), 0xfffe0000 + (i << 14), 0x04000, flash_read,   flash_readw,   flash_readl,   flash_write, flash_writew, mem_write_nulll, flash->array + (((i << 14) ^ 0x10000) & 0x1ffff),                       0, (void *)flash);
		}
		else
		{
			mem_mapping_add(&(flash->mapping[i]), 0xe0000 + (i << 14), 0x04000, flash_read,   flash_readw,   flash_readl,   flash_write, mem_write_nullw, mem_write_nulll, flash->array + (((i << 14) ^ 0x10000) & 0x1ffff),                       MEM_MAPPING_EXTERNAL, (void *)flash);
			mem_mapping_add(&(flash->mapping_h[i]), 0xfffe0000 + (i << 14), 0x04000, flash_read,   flash_readw,   flash_readl,   flash_write, mem_write_nullw, mem_write_nulll, flash->array + (((i << 14) ^ 0x10000) & 0x1ffff),                       0, (void *)flash);
		}
	}
}

void *intel_flash_init(uint8_t type)
{
        FILE *f;
        flash_t *flash = malloc(sizeof(flash_t));
	char fpath[1024];
	int i;
        memset(flash, 0, sizeof(flash_t));

	// pclog("Initializing Flash (type = %i)\n", type);

	memset(flash_path, 0, 1024);

	switch(romset)
	{
		case ROM_REVENGE:
			strcpy(flash_path, "roms/revenge/");
			break;
		case ROM_586MC1:
			strcpy(flash_path, "roms/586mc1/");
			break;
		case ROM_PLATO:
			strcpy(flash_path, "roms/plato/");
			break;
		case ROM_ENDEAVOR:
			strcpy(flash_path, "roms/endeavor/");
			break;
		case ROM_MB500N:
			strcpy(flash_path, "roms/mb500n/");
			break;
#if 0
		case ROM_P54TP4XE:
			strcpy(flash_path, "roms/p54tp4xe/");
			break;
#endif
		case ROM_ACERM3A:
			strcpy(flash_path, "roms/acerm3a/");
			break;
		case ROM_ACERV35N:
			strcpy(flash_path, "roms/acerv35n/");
			break;
		case ROM_P55TVP4:
			strcpy(flash_path, "roms/p55tvp4/");
			break;
#if 0
		case ROM_P55T2P4:
			strcpy(flash_path, "roms/p55t2p4/");
			break;
#endif
		case ROM_430VX:
			strcpy(flash_path, "roms/430vx/");
			break;
		case ROM_P55VA:
			strcpy(flash_path, "roms/p55va/");
			break;
		case ROM_440FX:
			strcpy(flash_path, "roms/440fx/");
			break;
		case ROM_KN97:
			strcpy(flash_path, "roms/kn97/");
			break;
		case ROM_MARL:
			strcpy(flash_path, "roms/marl/");
			break;
		case ROM_THOR:
			strcpy(flash_path, "roms/thor/");
			break;
	}
	// pclog("Flash init: Path is: %s\n", flash_path);

	flash->type = (type & 2) ? 1 : 0;
	flash->flash_id = (!flash->type) ? 0x94 : 0x95;
	flash->invert_high_pin = (type & 1);
	flash->allow_word_write = (type & 4) ? 1 : 0;

	/* The block lengths are the same both flash types. */
	flash->block_len[0] = 0x1c000;
	flash->block_len[1] = 0x01000;
	flash->block_len[2] = 0x01000;
	flash->block_len[3] = 0x02000;

	if(flash->type)					/* 28F001BX-B */
	{
		flash->block_start[0] = 0x04000;	/* MAIN BLOCK */
		flash->block_end[0] = 0x1ffff;
		flash->block_start[1] = 0x03000;	/* DMI BLOCK */
		flash->block_end[1] = 0x03fff;
		flash->block_start[2] = 0x04000;	/* ESCD BLOCK */
		flash->block_end[2] = 0x04fff;
		flash->block_start[3] = 0x00000;	/* BOOT BLOCK */
		flash->block_end[3] = 0x01fff;
	}
	else						/* 28F001BX-T */
	{
		flash->block_start[0] = 0x00000;	/* MAIN BLOCK */
		flash->block_end[0] = 0x1bfff;
		flash->block_start[1] = 0x1c000;	/* DMI BLOCK */
		flash->block_end[1] = 0x1cfff;
		flash->block_start[2] = 0x1d000;	/* ESCD BLOCK */
		flash->block_end[2] = 0x1dfff;
		flash->block_start[3] = 0x1e000;	/* BOOT BLOCK */
		flash->block_end[3] = 0x1ffff;
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

	/* Load the main block. */
	memset(fpath, 0, 1024);
	strcpy(fpath, flash_path);
	strcat(fpath, "main.bin");
        f = romfopen(fpath, "rb");
        if (f)
        {
                fread(&(flash->array[flash->block_start[BLOCK_MAIN]]), flash->block_len[BLOCK_MAIN], 1, f);
                fclose(f);
        }

	/* Load the DMI block. */
	memset(fpath, 0, 1024);
	strcpy(fpath, flash_path);
	strcat(fpath, "dmi.bin");
        f = romfopen(fpath, "rb");
       	if (f)
        {
       	        fread(&(flash->array[flash->block_start[BLOCK_DMI]]), flash->block_len[BLOCK_DMI], 1, f);
               	fclose(f);
        }

	/* Load the ESCD block. */
	memset(fpath, 0, 1024);
	strcpy(fpath, flash_path);
	strcat(fpath, "escd.bin");
        f = romfopen(fpath, "rb");
        if (f)
        {
                fread(&(flash->array[flash->block_start[BLOCK_ESCD]]), flash->block_len[BLOCK_ESCD], 1, f);
                fclose(f);
        }

        return flash;
}

/* For AMI BIOS'es - Intel 28F001BXT with high address pin inverted. */
void *intel_flash_bxt_ami_init()
{
	return intel_flash_init(FLASH_INVERT);
}

/* For later AMI BIOS'es - Intel 28F100BXT with high address pin inverted and 16-bit write capability. */
void *intel_flash_100bxt_ami_init()
{
	return intel_flash_init(FLASH_INVERT | FLASH_ALLOW_16);
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

	char fpath[1024];

	// pclog("Flash close: Path is: %s\n", flash_path);

	/* Save the main block. */
	memset(fpath, 0, 1024);
	strcpy(fpath, flash_path);
	strcat(fpath, "main.bin");
        f = romfopen(fpath, "wb");
        fwrite(&(flash->array[flash->block_start[BLOCK_MAIN]]), flash->block_len[BLOCK_MAIN], 1, f);
        fclose(f);

	/* Save the DMI block. */
	memset(fpath, 0, 1024);
	strcpy(fpath, flash_path);
	strcat(fpath, "dmi.bin");
        f = romfopen(fpath, "wb");
        fwrite(&(flash->array[flash->block_start[BLOCK_DMI]]), flash->block_len[BLOCK_DMI], 1, f);
       	fclose(f);

	/* Save the ESCD block. */
	memset(fpath, 0, 1024);
	strcpy(fpath, flash_path);
	strcat(fpath, "escd.bin");
        f = romfopen(fpath, "wb");
        fwrite(&(flash->array[flash->block_start[BLOCK_ESCD]]), flash->block_len[BLOCK_ESCD], 1, f);
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

device_t intel_flash_100bxt_ami_device =
{
        "Intel 28F100BXT Flash BIOS",
        0,
        intel_flash_100bxt_ami_init,
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
        "Intel 28F001BXT Flash BIOS",
        0,
        intel_flash_bxb_init,
        intel_flash_close,
        NULL,
        NULL,
        NULL,
        NULL,
        NULL
};
