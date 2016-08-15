/* Copyright holders: Sarah Walker
   see COPYING for more details
*/
#include <stdlib.h>
#include <stdio.h>
#include "ibm.h"
#include "mem.h"
#include "rom.h"

FILE *romfopen(char *fn, char *mode)
{
        char s[512];
        strcpy(s, pcempath);
        put_backslash(s);
        strcat(s, fn);
        return fopen(s, mode);
}

int rom_present(char *fn)
{
        FILE *f;
        char s[512];
        
        strcpy(s, pcempath);
        put_backslash(s);
        strcat(s, fn);
        f = fopen(s, "rb");
        if (f)
        {
                fclose(f);
                return 1;
        }
        return 0;
}

static uint8_t rom_read(uint32_t addr, void *p)
{
        rom_t *rom = (rom_t *)p;
//        pclog("rom_read : %08x %08x %02x\n", addr, rom->mask, rom->rom[addr & rom->mask]);
        return rom->rom[addr & rom->mask];
}
uint16_t rom_readw(uint32_t addr, void *p)
{
        rom_t *rom = (rom_t *)p;
//        pclog("rom_readw: %08x %08x %04x\n", addr, rom->mask, *(uint16_t *)&rom->rom[addr & rom->mask]);
        return *(uint16_t *)&rom->rom[addr & rom->mask];
}
uint32_t rom_readl(uint32_t addr, void *p)
{
        rom_t *rom = (rom_t *)p;
//        pclog("rom_readl: %08x %08x %08x\n", addr, rom->mask, *(uint32_t *)&rom->rom[addr & rom->mask]);
        return *(uint32_t *)&rom->rom[addr & rom->mask];
}

int rom_init(rom_t *rom, char *fn, uint32_t address, int size, int mask, int file_offset, uint32_t flags)
{
        FILE *f = romfopen(fn, "rb");
        
        if (!f)
        {
                pclog("ROM image not found : %s\n", fn);
                return -1;
        }
        
        rom->rom = malloc(size);
        fseek(f, file_offset, SEEK_SET);
        fread(rom->rom, size, 1, f);
        fclose(f);
        
        rom->mask = mask;
        
        mem_mapping_add(&rom->mapping, address, size, rom_read,
                                                      rom_readw,
                                                      rom_readl,
                                                      mem_write_null,
                                                      mem_write_nullw,
                                                      mem_write_nulll,
                                                      rom->rom,
                                                      flags,
                                                      rom);

        return 0;
}

int rom_init_interleaved(rom_t *rom, char *fn_low, char *fn_high, uint32_t address, int size, int mask, int file_offset, uint32_t flags)
{
        FILE *f_low  = romfopen(fn_low, "rb");
        FILE *f_high = romfopen(fn_high, "rb");
        int c;
        
        if (!f_low || !f_high)
        {
                if (!f_low)
                        pclog("ROM image not found : %s\n", fn_low);
                else
                        fclose(f_low);
                if (!f_high)
                        pclog("ROM image not found : %s\n", fn_high);
                else
                        fclose(f_high);
                return -1;
        }
        
        rom->rom = malloc(size);
        fseek(f_low, file_offset, SEEK_SET);
        fseek(f_high, file_offset, SEEK_SET);
        for (c = 0; c < size; c += 2)
        {
                rom->rom[c]     = getc(f_low);
                rom->rom[c + 1] = getc(f_high);
        }
        fclose(f_high);
        fclose(f_low);
        
        rom->mask = mask;
        
        mem_mapping_add(&rom->mapping, address, size, rom_read,
                                                      rom_readw,
                                                      rom_readl,
                                                      mem_write_null,
                                                      mem_write_nullw,
                                                      mem_write_nulll,
                                                      rom->rom,
                                                      flags,
                                                      rom);

        return 0;
}
