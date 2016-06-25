#include <stdlib.h>
#include "ibm.h"
#include "device.h"
#include "mem.h"

enum
{
        CMD_READ_ARRAY = 0xff,
        CMD_IID = 0x90,
        CMD_READ_STATUS = 0x70,
        CMD_CLEAR_STATUS = 0x50,
        CMD_ERASE_SETUP = 0x20,
        CMD_ERASE_CONFIRM = 0xd0,
        CMD_ERASE_SUSPEND = 0xb0,
        CMD_PROGRAM_SETUP = 0x40
};

typedef struct flash_t
{
        uint32_t command, status;
	uint32_t data_addr1, data_addr2, data_start, boot_start;
	uint32_t main_start[2], main_end[2], main_len[2];
	uint32_t flash_id, invert_high_pin;
        mem_mapping_t read_mapping, write_mapping;
        mem_mapping_t read_mapping_h, write_mapping_h;
} flash_t;

static flash_t flash;

char flash_path[1024];

#if 0
mem_mapping_t flash_null_mapping[4];

uint8_t flash_read_null(uint32_t addr, void *priv)
{
        return 0xff;
}

uint16_t flash_read_nullw(uint32_t addr, void *priv)
{
        return 0xffff;
}

uint32_t flash_read_nulll(uint32_t addr, void *priv)
{
//        pclog("Read BIOS %08X %02X %04X:%04X\n", addr, *(uint32_t *)&rom[addr & biosmask], CS, pc);
        return 0xffffffff;
}

void flash_null_mapping_disable()
{
	mem_mapping_disable(&flash_null_mapping[0]);
	mem_mapping_disable(&flash_null_mapping[1]);
	mem_mapping_disable(&flash_null_mapping[2]);
	mem_mapping_disable(&flash_null_mapping[3]);
}

void flash_null_mapping_enable()
{
	mem_mapping_enable(&flash_null_mapping[0]);
	mem_mapping_enable(&flash_null_mapping[1]);
	mem_mapping_enable(&flash_null_mapping[2]);
	mem_mapping_enable(&flash_null_mapping[3]);
}

void flash_add_null_mapping()
{
	mem_mapping_add(&flash_null_mapping[0], 0xe0000, 0x04000, flash_read_null,   flash_read_nullw,   flash_read_nulll,   mem_write_null, mem_write_nullw, mem_write_nulll, NULL,                        MEM_MAPPING_EXTERNAL, 0);
	mem_mapping_add(&flash_null_mapping[1], 0xe4000, 0x04000, flash_read_null,   flash_read_nullw,   flash_read_nulll,   mem_write_null, mem_write_nullw, mem_write_nulll, NULL, MEM_MAPPING_EXTERNAL, 0);
	mem_mapping_add(&flash_null_mapping[2], 0xe8000, 0x04000, flash_read_null,   flash_read_nullw,   flash_read_nulll,   mem_write_null, mem_write_nullw, mem_write_nulll, NULL, MEM_MAPPING_EXTERNAL, 0);
	mem_mapping_add(&flash_null_mapping[3], 0xec000, 0x04000, flash_read_null,   flash_read_nullw,   flash_read_nulll,   mem_write_null, mem_write_nullw, mem_write_nulll, NULL, MEM_MAPPING_EXTERNAL, 0);

	flash_null_mapping_disable();
}
#endif

static uint8_t flash_read(uint32_t addr, void *p)
{
        flash_t *flash = (flash_t *)p;
        // pclog("flash_read : addr=%08x command=%02x %04x:%08x\n", addr, flash->command, CS, pc);
        switch (flash->command)
        {
                case CMD_IID:
                if (addr & 1)
                        return flash->flash_id;
                return 0x89;
                
                default:
                return flash->status;
        }
}

static void flash_write(uint32_t addr, uint8_t val, void *p)
{
        flash_t *flash = (flash_t *)p;
	int i;
        // pclog("flash_write : addr=%08x val=%02x command=%02x %04x:%08x\n", addr, val, flash->command, CS, pc);        
        switch (flash->command)
        {
                case CMD_ERASE_SETUP:
                if (val == CMD_ERASE_CONFIRM)
                {
                        // pclog("flash_write: erase %05x\n", addr & 0x1ffff);

                        if ((addr & 0x1f000) == flash->data_addr1)
                                memset(&rom[flash->data_addr1], 0xff, 0x1000);
                        if ((addr & 0x1f000) == flash->data_addr2)
                                memset(&rom[flash->data_addr2], 0xff, 0x1000);
                        if (((addr & 0x1ffff) >= flash->main_start[0]) && ((addr & 0x1ffff) <= flash->main_end[0]) && flash->main_len[0])
       	                        memset(&rom[flash->main_start[0]], 0xff, flash->main_len[0]);
               	        if (((addr & 0x1ffff) >= flash->main_start[1]) && ((addr & 0x1ffff) <= flash->main_end[1]) && flash->main_len[1])
                       	        memset(&rom[flash->main_start[1]], 0xff, flash->main_len[1]);

                        flash->status = 0x80;
                }
                flash->command = CMD_READ_STATUS;
                break;
                
                case CMD_PROGRAM_SETUP:
                // pclog("flash_write: program %05x %02x\n", addr & 0x1ffff, val);
                if ((addr & 0x1e000) != (flash->boot_start & 0x1e000))
       	                rom[addr & 0x1ffff] = val;
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
                                
                        case CMD_IID:
                        case CMD_READ_STATUS:
			for (i = 0; i < 8; i++)
			{
                        	mem_mapping_disable((addr & 0x8000000) ? &bios_high_mapping[i] : &bios_mapping[i]);
			}
                       	mem_mapping_enable((addr & 0x8000000) ? &flash->read_mapping_h : &flash->read_mapping);                        
                        break;
                        
                        case CMD_READ_ARRAY:
			for (i = 0; i < 8; i++)
			{
                        	mem_mapping_enable((addr & 0x8000000) ? &bios_high_mapping[i] : &bios_mapping[i]);
			}
                       	mem_mapping_disable((addr & 0x8000000) ? &flash->read_mapping_h : &flash->read_mapping);                        
#if 0
			if ((romset == ROM_MB500N) || (romset == ROM_430VX) || (romset == ROM_P55VA) || (romset == ROM_P55TVP4) || (romset == ROM_440FX))
			{
				for (i = 0; i < 4; i++)
				{
                	        	mem_mapping_disable(&bios_mapping[i]);
				}

				flash_null_mapping_enable();
			}
			pclog("; This line needed\n");
#endif
                        break;
                }
        }
}

void *intel_flash_init(int type)
{
        FILE *f;
        flash_t *flash = malloc(sizeof(flash_t));
	char fpath[1024];
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
		case ROM_P54TP4XE:
			strcpy(flash_path, "roms/p54tp4xe/");
			break;
		case ROM_ACERM3A:
			strcpy(flash_path, "roms/acerm3a/");
			break;
		case ROM_ACERV35N:
			strcpy(flash_path, "roms/acerv35n/");
			break;
		case ROM_P55TVP4:
			strcpy(flash_path, "roms/p55tvp4/");
			break;
		case ROM_P55T2P4:
			strcpy(flash_path, "roms/p55t2p4/");
			break;
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
	}
	// pclog("Flash init: Path is: %s\n", flash_path);

	switch(type)
	{
		case 0:
			flash->data_addr1 = 0xc000;
			flash->data_addr2 = 0xd000;
			flash->data_start = 0xc000;
			flash->boot_start = 0xe000;
			flash->main_start[0] = 0x0000;
			flash->main_end[0] = 0xbfff;
			flash->main_len[0] = 0xc000;
			flash->main_start[1] = 0x10000;
			flash->main_end[1] = 0x1ffff;
			flash->main_len[1] = 0x10000;
			break;
		case 1:
			flash->data_addr1 = 0x1c000;
			flash->data_addr2 = 0x1d000;
			flash->data_start = 0x1c000;
			flash->boot_start = 0x1e000;
			flash->main_start[0] = 0x00000;
			flash->main_end[0] = 0x1bfff;
			flash->main_len[0] = 0x1c000;
			flash->main_start[1] = flash->main_end[1] = flash->main_len[1] = 0;
			break;
		case 2:
			flash->data_addr1 = 0x3000;
			flash->data_addr2 = 0x2000;
			flash->data_start = 0x2000;
			flash->boot_start = 0x00000;
			flash->main_start[0] = 0x04000;
			flash->main_end[0] = 0x1ffff;
			flash->main_len[0] = 0x1c000;
			flash->main_start[1] = flash->main_end[1] = flash->main_len[1] = 0;
			break;
	}

	flash->flash_id = (type != 2) ? 0x94 : 0x95;
	flash->invert_high_pin = (type == 0) ? 1 : 0;

        mem_mapping_add(&flash->read_mapping,
                    0xe0000, 
                    0x20000,
                    flash_read, NULL, NULL,
                    NULL, NULL, NULL,
                    NULL, MEM_MAPPING_EXTERNAL, (void *)flash);
        mem_mapping_add(&flash->write_mapping,
                    0xe0000, 
                    0x20000,
                    NULL, NULL, NULL,
                    flash_write, NULL, NULL,
                    NULL, MEM_MAPPING_EXTERNAL, (void *)flash);
       	mem_mapping_disable(&flash->read_mapping);
	if (type > 0)
	{
	        mem_mapping_add(&flash->read_mapping_h,
        	            0xfffe0000, 
                	    0x20000,
	                    flash_read, NULL, NULL,
        	            NULL, NULL, NULL,
	                    NULL, 0, (void *)flash);
        	mem_mapping_add(&flash->write_mapping_h,
                	    0xfffe0000, 
	                    0x20000,
        	            NULL, NULL, NULL,
                	    flash_write, NULL, NULL,
	                    NULL, 0, (void *)flash);
	       	mem_mapping_disable(&flash->read_mapping_h);
	       	/* if (romset != ROM_P55TVP4)  */ mem_mapping_disable(&flash->write_mapping);
	}
        flash->command = CMD_READ_ARRAY;
        flash->status = 0;

	if ((romset == ROM_586MC1) || (romset == ROM_MB500N) || (type == 0))
	{
		memset(&rom[flash->data_addr2], 0xFF, 0x1000);
	}
	else
	{
		memset(&rom[flash->data_start], 0xFF, 0x2000);
	}
        
	if ((romset != ROM_586MC1) && (romset != ROM_MB500N) && (type > 0))
	{
		memset(fpath, 0, 1024);
		strcpy(fpath, flash_path);
		strcat(fpath, "dmi.bin");
	        f = romfopen(fpath, "rb");
        	if (f)
	        {
        	        fread(&rom[flash->data_addr1], 0x1000, 1, f);
                	fclose(f);
	        }
	}
	memset(fpath, 0, 1024);
	strcpy(fpath, flash_path);
	strcat(fpath, "escd.bin");
        f = romfopen(fpath, "rb");
        if (f)
        {
                fread(&rom[flash->data_addr2], 0x1000, 1, f);
                fclose(f);
        }

#if 0
	flash_add_null_mapping();
#endif
        
        return flash;
}

/* For AMI BIOS'es - Intel 28F001BXT with high address pin inverted. */
void *intel_flash_bxt_ami_init()
{
	return intel_flash_init(0);
}

/* For Award BIOS'es - Intel 28F001BXT with high address pin not inverted. */
void *intel_flash_bxt_init()
{
	return intel_flash_init(1);
}

/* For Acerd BIOS'es - Intel 28F001BXB. */
void *intel_flash_bxb_init()
{
	return intel_flash_init(2);
}

void intel_flash_close(void *p)
{
        FILE *f;
        flash_t *flash = (flash_t *)p;

	char fpath[1024];

	// pclog("Flash close: Path is: %s\n", flash_path);

	if ((romset != ROM_586MC1) && (romset != ROM_MB500N))
	{
		memset(fpath, 0, 1024);
		strcpy(fpath, flash_path);
		strcat(fpath, "dmi.bin");
	        f = romfopen(fpath, "wb");
	        fwrite(&rom[flash->data_addr1], 0x1000, 1, f);
        	fclose(f);
	}
	memset(fpath, 0, 1024);
	strcpy(fpath, flash_path);
	strcat(fpath, "escd.bin");
        f = romfopen(fpath, "wb");
        fwrite(&rom[flash->data_addr2], 0x1000, 1, f);
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
