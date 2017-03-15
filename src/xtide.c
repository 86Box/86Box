#include "ibm.h"

#include "device.h"
#include "io.h"
#include "ide.h"
#include "mem.h"
#include "rom.h"
#include "xtide.h"

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
                writeidew(0, val | (xtide->data_high << 8));
                return;
                
                case 0x1: case 0x2: case 0x3:
                case 0x4: case 0x5: case 0x6: case 0x7:
                writeide(0, (port  & 0xf) | 0x1f0, val);
                return;
                
                case 0x8:
                xtide->data_high = val;
                return;
                
                case 0xe:
                writeide(0, 0x3f6, val);
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
                tempw = readidew(0);
                xtide->data_high = tempw >> 8;
                return tempw & 0xff;
                               
                case 0x1: case 0x2: case 0x3:
                case 0x4: case 0x5: case 0x6: case 0x7:
                return readide(0, (port  & 0xf) | 0x1f0);
                
                case 0x8:
                return xtide->data_high;
                
                case 0xe:
                return readide(0, 0x3f6);
        }
}

static void *xtide_init()
{
        xtide_t *xtide = malloc(sizeof(xtide_t));
        memset(xtide, 0, sizeof(xtide_t));

        rom_init(&xtide->bios_rom, "roms/ide_xt.bin", 0xc8000, 0x4000, 0x3fff, 0, MEM_MAPPING_EXTERNAL);
        ide_init();
        ide_pri_disable();
        ide_sec_disable();
        io_sethandler(0x0300, 0x0010, xtide_read, NULL, NULL, xtide_write, NULL, NULL, xtide);
        
        return xtide;
}

static void *xtide_at_init()
{
        xtide_t *xtide = malloc(sizeof(xtide_t));
        memset(xtide, 0, sizeof(xtide_t));

        rom_init(&xtide->bios_rom, "roms/ide_at.bin", 0xc8000, 0x4000, 0x3fff, 0, MEM_MAPPING_EXTERNAL);
        ide_init();
        
        return xtide;
}

static void xtide_close(void *p)
{
        xtide_t *xtide = (xtide_t *)p;

        free(xtide);
}

static int xtide_available()
{
        return rom_present("roms/ide_xt.bin");
}

static int xtide_at_available()
{
        return rom_present("roms/ide_at.bin");
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
