/*
 * 86Box	A hypervisor and IBM PC system emulator that specializes in
 *		running old operating systems and software designed for IBM
 *		PC systems and compatibles from 1981 through fairly recent
 *		system designs based on the PCI bus.
 *
 *		This file is part of the 86Box distribution.
 *
 *		XT IDE controller emulation.
 *
 * Version:	@(#)xtide.c	1.0.1	2017/06/03
 *
 * Authors:	Sarah Walker, <http://pcem-emulator.co.uk/>
 *		Miran Grca, <mgrca8@gmail.com>
 *		Copyright 2008-2017 Sarah Walker.
 *		Copyright 2016-2017 Miran Grca.
 */
#include <malloc.h>
#include "ibm.h"
#include "io.h"
#include "mem.h"
#include "rom.h"
#include "device.h"
#include "ide.h"
#include "xtide.h"


#define XTIDE_ROM_PATH		L"roms/hdd/xtide/ide_xt.bin"
#define ATIDE_ROM_PATH		L"roms/hdd/xtide/ide_at.bin"


typedef struct xtide_t
{
        uint8_t data_high;
        rom_t bios_rom;
} xtide_t;


static void xtide_write(uint16_t port, uint8_t val, void *p)
{
        xtide_t *xtide = (xtide_t *)p;
        
        switch (port & 0xf)
        {
                case 0x0:
                writeidew(4, val | (xtide->data_high << 8));
                return;
                
                case 0x1: case 0x2: case 0x3:
                case 0x4: case 0x5: case 0x6: case 0x7:
                writeide(4, (port  & 0xf) | 0x1f0, val);
                return;
                
                case 0x8:
                xtide->data_high = val;
                return;
                
                case 0xe:
                writeide(4, 0x3f6, val);
                return;
        }
}


static uint8_t xtide_read(uint16_t port, void *p)
{
        xtide_t *xtide = (xtide_t *)p;
        uint16_t tempw;

        switch (port & 0xf)
        {
                case 0x0:
                tempw = readidew(4);
                xtide->data_high = tempw >> 8;
                return tempw & 0xff;
                               
                case 0x1: case 0x2: case 0x3:
                case 0x4: case 0x5: case 0x6: case 0x7:
                return readide(4, (port  & 0xf) | 0x1f0);
                
                case 0x8:
                return xtide->data_high;
                
                case 0xe:
                return readide(4, 0x3f6);

		default:
		return 0xff;
        }
}


static void *xtide_init(void)
{
        xtide_t *xtide = malloc(sizeof(xtide_t));
        memset(xtide, 0, sizeof(xtide_t));

        rom_init(&xtide->bios_rom, XTIDE_ROM_PATH, 0xc8000, 0x4000, 0x3fff, 0, MEM_MAPPING_EXTERNAL);
        ide_xtide_init();
        io_sethandler(0x0300, 0x0010, xtide_read, NULL, NULL, xtide_write, NULL, NULL, xtide);
        
        return xtide;
}


static void *xtide_at_init(void)
{
        xtide_t *xtide = malloc(sizeof(xtide_t));
        memset(xtide, 0, sizeof(xtide_t));

        rom_init(&xtide->bios_rom, ATIDE_ROM_PATH, 0xc8000, 0x4000, 0x3fff, 0, MEM_MAPPING_EXTERNAL);
        ide_init();
        
        return xtide;
}


static void *xtide_ps2_init(void)
{
        xtide_t *xtide = malloc(sizeof(xtide_t));
        memset(xtide, 0, sizeof(xtide_t));

        rom_init(&xtide->bios_rom, L"roms/hdd/xtide/SIDE1V12.BIN", 0xc8000, 0x8000, 0x7fff, 0, MEM_MAPPING_EXTERNAL);
        ide_xtide_init();
        io_sethandler(0x0360, 0x0010, xtide_read, NULL, NULL, xtide_write, NULL, NULL, xtide);
        
        return xtide;
}


static void *xtide_at_ps2_init(void)
{
        xtide_t *xtide = malloc(sizeof(xtide_t));
        memset(xtide, 0, sizeof(xtide_t));

        rom_init(&xtide->bios_rom, L"roms/hdd/xtide/ide_at_1_1_5.bin", 0xc8000, 0x4000, 0x3fff, 0, MEM_MAPPING_EXTERNAL);
        ide_init();
        
        return xtide;
}


static void xtide_close(void *p)
{
        xtide_t *xtide = (xtide_t *)p;

        free(xtide);
}


static int xtide_available(void)
{
        return rom_present(L"roms/hdd/xtide/ide_xt.bin");
}


static int xtide_at_available(void)
{
        return rom_present(L"roms/hdd/xtide/ide_at.bin");
}


static int xtide_ps2_available(void)
{
        return rom_present(L"roms/hdd/xtide/SIDE1V12.BIN");
}


static int xtide_at_ps2_available(void)
{
        return rom_present(L"roms/hdd/xtide/ide_at_1_1_5.bin");
}


device_t xtide_device =
{
        "XTIDE",
        0,
        xtide_init,
        xtide_close,
        xtide_available,
        NULL,
        NULL,
        NULL,
        NULL
};
device_t xtide_at_device =
{
        "XTIDE (AT)",
        DEVICE_AT,
        xtide_at_init,
        xtide_close,
        xtide_at_available,
        NULL,
        NULL,
        NULL,
        NULL
};

device_t xtide_ps2_device =
{
        "XTIDE (Acculogic)",
        0,
        xtide_ps2_init,
        xtide_close,
        xtide_ps2_available,
        NULL,
        NULL,
        NULL,
        NULL
};

device_t xtide_at_ps2_device =
{
        "XTIDE (AT) (1.1.5)",
        DEVICE_PS2,
        xtide_at_ps2_init,
        xtide_close,
        xtide_at_ps2_available,
        NULL,
        NULL,
        NULL,
        NULL
};
