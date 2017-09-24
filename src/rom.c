/* Copyright holders: Sarah Walker
   see COPYING for more details
*/
#include <stdlib.h>
#include <stdio.h>
#include "config.h"
#include "ibm.h"
#include "mem.h"
#include "rom.h"


FILE *romfopen(wchar_t *fn, wchar_t *mode)
{
        wchar_t s[1024];

        wcscpy(s, exe_path);
        put_backslash_w(s);
        wcscat(s, fn);

        return _wfopen(s, mode);
}



int rom_getfile(wchar_t *fn, wchar_t *s, int size)
{
        FILE *f;

        wcscpy(s, exe_path);
	put_backslash_w(s);
	wcscat(s, fn);
	f = _wfopen(s, L"rb");
	if (f)
	{
		fclose(f);
		return 1;
	}
        return 0;
}

int rom_present(wchar_t *fn)
{
        FILE *f;
        wchar_t s[512];
        
        wcscpy(s, exe_path);
        put_backslash_w(s);
        wcscat(s, fn);
        f = _wfopen(s, L"rb");
        if (f)
        {
                fclose(f);
                return 1;
        }
        return 0;
}


uint8_t rom_read(uint32_t addr, void *p)
{
        rom_t *rom = (rom_t *)p;
#ifdef ROM_TRACE
	if (rom->mapping.base==ROM_TRACE)
		pclog("ROM: read byte from BIOS at %06lX\n", addr);
#endif
        return rom->rom[addr & rom->mask];
}


uint16_t rom_readw(uint32_t addr, void *p)
{
        rom_t *rom = (rom_t *)p;
#ifdef ROM_TRACE
	if (rom->mapping.base==ROM_TRACE)
		pclog("ROM: read word from BIOS at %06lX\n", addr);
#endif
        return *(uint16_t *)&rom->rom[addr & rom->mask];
}


uint32_t rom_readl(uint32_t addr, void *p)
{
        rom_t *rom = (rom_t *)p;
#ifdef ROM_TRACE
	if (rom->mapping.base==ROM_TRACE)
		pclog("ROM: read long from BIOS at %06lX\n", addr);
#endif
        return *(uint32_t *)&rom->rom[addr & rom->mask];
}


int rom_init(rom_t *rom, wchar_t *fn, uint32_t address, int size, int mask, int file_offset, uint32_t flags)
{
        FILE *f = romfopen(fn, L"rb");
        
        if (!f)
        {
                pclog("ROM image not found : %ws\n", fn);
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


int rom_init_interleaved(rom_t *rom, wchar_t *fn_low, wchar_t *fn_high, uint32_t address, int size, int mask, int file_offset, uint32_t flags)
{
        FILE *f_low  = romfopen(fn_low, L"rb");
        FILE *f_high = romfopen(fn_high, L"rb");
        int c;
        
        if (!f_low || !f_high)
        {
                if (!f_low)
                        pclog("ROM image not found : %ws\n", fn_low);
                else
                        fclose(f_low);
                if (!f_high)
                        pclog("ROM image not found : %ws\n", fn_high);
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
