/* Copyright holders: Sarah Walker
   see COPYING for more details
*/
#include <stdlib.h>
#include "ibm.h"
#include "device.h"
#include "io.h"
#include "mem.h"
#include "tandy_rom.h"

static uint8_t *tandy_rom;
static uint8_t tandy_rom_bank;
static int tandy_rom_offset;
static mem_mapping_t tandy_rom_mapping;

uint8_t tandy_read_rom(uint32_t addr, void *p)
{
        uint32_t addr2 = (addr & 0xffff) + tandy_rom_offset;
        return tandy_rom[addr2];
}
uint16_t tandy_read_romw(uint32_t addr, void *p)
{
        uint32_t addr2 = (addr & 0xffff) + tandy_rom_offset;
        return *(uint16_t *)&tandy_rom[addr2];
}
uint32_t tandy_read_roml(uint32_t addr, void *p)
{
        return *(uint32_t *)&tandy_rom[addr];
}

uint8_t tandy_rom_bank_read(uint16_t port, void *p)
{
        if (port == 0xffea)
                return tandy_rom_bank ^ 0x10;
        else
                return 0xff;
}
void tandy_rom_bank_write(uint16_t port, uint8_t val, void *p)
{
        if (port == 0xffea)
        {
                tandy_rom_bank = val;
                tandy_rom_offset = ((val ^ 4) & 7) * 0x10000;
                mem_mapping_set_exec(&tandy_rom_mapping, &tandy_rom[tandy_rom_offset]);
        }
}

void *tandy_rom_init()
{
        FILE *f, *ff;
        int c;

        tandy_rom = malloc(0x80000);

        f  = romfopen("roms/tandy1000sl2/8079047.hu1" ,"rb");
        ff = romfopen("roms/tandy1000sl2/8079048.hu2","rb");
        for (c = 0x0000; c < 0x80000; c += 2)
        {
                tandy_rom[c] = getc(f);
                tandy_rom[c + 1] = getc(ff);
        }
        fclose(ff);
        fclose(f);

        mem_mapping_add(&tandy_rom_mapping, 0xe0000, 0x10000,
                        tandy_read_rom, tandy_read_romw, tandy_read_roml,
                        NULL, NULL, NULL,
                        tandy_rom,  MEM_MAPPING_EXTERNAL, NULL);

        io_sethandler(0xffe8, 0x0008, tandy_rom_bank_read, NULL, NULL, tandy_rom_bank_write, NULL, NULL, NULL);
        
        return tandy_rom;
}

void tandy_rom_close(void *p)
{
        free(p);
}

device_t tandy_rom_device =
{
        "Tandy 1000SL/2 ROM",
        0,
        tandy_rom_init,
        tandy_rom_close,
        NULL,
        NULL,
        NULL,
        NULL,
        NULL
};
