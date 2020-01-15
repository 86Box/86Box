/*
 * 86Box	A hypervisor and IBM PC system emulator that specializes in
 *		running old operating systems and software designed for IBM
 *		PC systems and compatibles from 1981 through fairly recent
 *		system designs based on the PCI bus.
 *
 *		This file is part of the 86Box distribution.
 *
 *		Handling of ROM image files.
 *
 * NOTES:	- pc2386 BIOS is corrupt (JMP at F000:FFF0 points to RAM)
 *		- pc2386 video BIOS is underdumped (16k instead of 24k)
 *		- c386sx16 BIOS fails checksum
 *
 * Version:	@(#)rom.c	1.0.45	2019/03/03
 *
 * Authors:	Sarah Walker, <http://pcem-emulator.co.uk/>
 *		Miran Grca, <mgrca8@gmail.com>
 *		Fred N. van Kempen, <decwiz@yahoo.com>
 *
 *		Copyright 2008-2019 Sarah Walker.
 *		Copyright 2016-2019 Miran Grca.
 *		Copyright 2018,2019 Fred N. van Kempen.
 */
#include <stdarg.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <stdlib.h>
#include <wchar.h>
#define HAVE_STDARG_H
#include "86box.h"
#include "mem.h"
#include "rom.h"
#include "plat.h"
#include "machine/machine.h"
#include "machine/m_xt_xi8088.h"


#ifdef ENABLE_ROM_LOG
int rom_do_log = ENABLE_ROM_LOG;


static void
rom_log(const char *fmt, ...)
{
    va_list ap;

    if (rom_do_log) {
	va_start(ap, fmt);
	pclog_ex(fmt, ap);
	va_end(ap);
    }
}
#else
#define rom_log(fmt, ...)
#endif


FILE *
rom_fopen(wchar_t *fn, wchar_t *mode)
{
    wchar_t temp[1024];

    if (wcslen(exe_path) <= 1024)
	wcscpy(temp, exe_path);
    else
	wcsncpy(temp, exe_path, 1024);
    plat_put_backslash(temp);
    wcscat(temp, fn);

    return(plat_fopen(temp, mode));
}


int
rom_getfile(wchar_t *fn, wchar_t *s, int size)
{
    FILE *f;

    wcscpy(s, exe_path);
    plat_put_backslash(s);
    wcscat(s, fn);

    f = plat_fopen(s, L"rb");
    if (f != NULL) {
	(void)fclose(f);
	return(1);
    }

    return(0);
}


int
rom_present(wchar_t *fn)
{
    FILE *f;

    f = rom_fopen(fn, L"rb");
    if (f != NULL) {
	(void)fclose(f);
	return(1);
    }

    return(0);
}


uint8_t
rom_read(uint32_t addr, void *priv)
{
    rom_t *rom = (rom_t *)priv;

#ifdef ROM_TRACE
    if (rom->mapping.base==ROM_TRACE)
	rom_log("ROM: read byte from BIOS at %06lX\n", addr);
#endif

    if (addr < rom->mapping.base)
	return 0xff;
    if (addr >= (rom->mapping.base + rom->sz))
	return 0xff;
    return(rom->rom[addr & rom->mask]);
}


uint16_t
rom_readw(uint32_t addr, void *priv)
{
    rom_t *rom = (rom_t *)priv;

#ifdef ROM_TRACE
    if (rom->mapping.base==ROM_TRACE)
	rom_log("ROM: read word from BIOS at %06lX\n", addr);
#endif

    if (addr < (rom->mapping.base - 1))
	return 0xffff;
    if (addr >= (rom->mapping.base + rom->sz))
	return 0xffff;
    return(*(uint16_t *)&rom->rom[addr & rom->mask]);
}


uint32_t
rom_readl(uint32_t addr, void *priv)
{
    rom_t *rom = (rom_t *)priv;

#ifdef ROM_TRACE
    if (rom->mapping.base==ROM_TRACE)
	rom_log("ROM: read long from BIOS at %06lX\n", addr);
#endif

    if (addr < (rom->mapping.base - 3))
	return 0xffffffff;
    if (addr >= (rom->mapping.base + rom->sz))
	return 0xffffffff;
    return(*(uint32_t *)&rom->rom[addr & rom->mask]);
}


/* Load a ROM BIOS from its chips, interleaved mode. */
int
rom_load_linear(wchar_t *fn, uint32_t addr, int sz, int off, uint8_t *ptr)
{
    FILE *f = rom_fopen(fn, L"rb");
        
    if (f == NULL) {
	rom_log("ROM: image '%ls' not found\n", fn);
	return(0);
    }

    /* Make sure we only look at the base-256K offset. */
    if (addr >= 0x40000)
    {
	addr = 0;
    }
    else
    {
	addr &= 0x03ffff;
    }

    if (ptr != NULL) {
	if (fseek(f, off, SEEK_SET) == -1)
		fatal("rom_load_linear(): Error seeking to the beginning of the file\n");
	if (fread(ptr+addr, 1, sz, f) != sz)
		fatal("rom_load_linear(): Error reading data\n");
    }

    (void)fclose(f);

    return(1);
}


/* Load a ROM BIOS from its chips, linear mode with high bit flipped. */
int
rom_load_linear_inverted(wchar_t *fn, uint32_t addr, int sz, int off, uint8_t *ptr)
{
    FILE *f = rom_fopen(fn, L"rb");
        
    if (f == NULL) {
	rom_log("ROM: image '%ls' not found\n", fn);
	return(0);
    }

    /* Make sure we only look at the base-256K offset. */
    if (addr >= 0x40000)
    {
	addr = 0;
    }
    else
    {
	addr &= 0x03ffff;
    }

    (void)fseek(f, 0, SEEK_END);
    if (ftell(f) < sz) {
	(void)fclose(f);
	return(0);
    }

    if (ptr != NULL) {
	if (fseek(f, off, SEEK_SET) == -1)
		fatal("rom_load_linear_inverted(): Error seeking to the beginning of the file\n");
	if (fread(ptr+addr+0x10000, 1, sz >> 1, f) > (sz >> 1))
		fatal("rom_load_linear_inverted(): Error reading the upper half of the data\n");
	if (fread(ptr+addr, sz >> 1, 1, f) > (sz >> 1))
		fatal("rom_load_linear_inverted(): Error reading the lower half of the data\n");
    }

    (void)fclose(f);

    return(1);
}


/* Load a ROM BIOS from its chips, interleaved mode. */
int
rom_load_interleaved(wchar_t *fnl, wchar_t *fnh, uint32_t addr, int sz, int off, uint8_t *ptr)
{
    FILE *fl = rom_fopen(fnl, L"rb");
    FILE *fh = rom_fopen(fnh, L"rb");
    int c;

    if (fl == NULL || fh == NULL) {
	if (fl == NULL) rom_log("ROM: image '%ls' not found\n", fnl);
	  else (void)fclose(fl);
	if (fh == NULL) rom_log("ROM: image '%ls' not found\n", fnh);
	  else (void)fclose(fh);

	return(0);
    }

    /* Make sure we only look at the base-256K offset. */
    if (addr >= 0x40000)
    {
	addr = 0;
    }
    else
    {
	addr &= 0x03ffff;
    }

    if (ptr != NULL) {
	(void)fseek(fl, off, SEEK_SET);
	(void)fseek(fh, off, SEEK_SET);
	for (c=0; c<sz; c+=2) {
		ptr[addr+c] = fgetc(fl) & 0xff;
		ptr[addr+c+1] = fgetc(fh) & 0xff;
	}
    }

    (void)fclose(fh);
    (void)fclose(fl);

    return(1);
}


static int
bios_normalize(int n, int up)
{
    /* 0x2000 -> 0x0000; 0x4000 -> 0x4000; 0x6000 -> 0x4000 */
    int temp_n = n & ~MEM_GRANULARITY_MASK;

    /* 0x2000 -> 0x4000; 0x4000 -> 0x4000; 0x6000 -> 0x8000 */
    if (up && (n % MEM_GRANULARITY_SIZE))
	temp_n += MEM_GRANULARITY_SIZE;

    return temp_n;
}



static uint8_t *
rom_reset(uint32_t addr, int sz)
{
    biosaddr = bios_normalize(addr, 0);
    biosmask = bios_normalize(sz, 1) - 1;
    if ((biosaddr + biosmask) > 0x000fffff)
	biosaddr = 0x000fffff - biosmask;

    rom_log("Load BIOS: %i bytes at %08X-%08X\n", biosmask + 1, biosaddr, biosaddr + biosmask);

    /* If not done yet, allocate a 128KB buffer for the BIOS ROM. */
    if (rom != NULL) {
	rom_log("ROM allocated, freeing...\n");
	free(rom);
	rom = NULL;
    }
    rom_log("Allocating ROM...\n");
    rom = (uint8_t *)malloc(biosmask + 1);
    rom_log("Filling ROM with FF's...\n");
    memset(rom, 0xff, biosmask + 1);

    return rom;
}


/* These four are for loading the BIOS. */
int
bios_load(wchar_t *fn1, wchar_t *fn2, uint32_t addr, int sz, int off, int flags)
{
    uint8_t ret = 0;
    uint8_t *ptr = NULL;
    int i, old_sz = sz;

    /*
	f0000, 65536 = prepare 64k rom starting at f0000, load 64k bios at 0000
	fe000, 65536 = prepare 64k rom starting at f0000, load 8k bios at e000
	fe000, 49152 = prepare 48k rom starting at f4000, load 8k bios at a000
	fe000, 8192 = prepare 16k rom starting at fc000, load 8k bios at 2000
     */
    if (!bios_only)
	ptr = (flags & FLAG_AUX) ? rom : rom_reset(addr, sz);

    if (!(flags & FLAG_AUX) && ((addr + sz) > 0x00100000))
	sz = 0x00100000 - addr;

#ifdef ENABLE_ROM_LOG
    if (!bios_only)
	rom_log("%sing %i bytes of %sBIOS starting with ptr[%08X] (ptr = %08X)\n", (bios_only) ? "Check" : "Load", sz, (flags & FLAG_AUX) ? "auxiliary " : "", addr - biosaddr, ptr);
#endif

    if (flags & FLAG_INT)
	ret = rom_load_interleaved(fn1, fn2, addr - biosaddr, sz, off, ptr);
    else {
	if (flags & FLAG_INV)
		ret = rom_load_linear_inverted(fn1, addr - biosaddr, sz, off, ptr);
	else
		ret = rom_load_linear(fn1, addr - biosaddr, sz, off, ptr);
    }

    if (!bios_only && (flags & FLAG_REP) && (old_sz >= 65536) && (sz < old_sz)) {
	old_sz /= sz;
	for (i = 0; i < (old_sz - 1); i++) {
		rom_log("Copying ptr[%08X] to ptr[%08X]\n", addr - biosaddr, i * sz);
		memcpy(&(ptr[i * sz]), &(ptr[addr - biosaddr]), sz);
	}
    }

    if (!bios_only && ret && !(flags & FLAG_AUX))
	mem_add_bios();

    return ret;
}


int
bios_load_linear_combined(wchar_t *fn1, wchar_t *fn2, int sz, int off)
{
    uint8_t ret = 0;

    ret = bios_load_linear(fn1, 0x000f0000, 131072, 128);
    ret &= bios_load_aux_linear(fn2, 0x000e0000, sz - 65536, 128);

    return ret;
}


#if defined(DEV_BRANCH) && defined(USE_TC430HX)
int
bios_load_linear_combined2(wchar_t *fn1, wchar_t *fn2, wchar_t *fn3, wchar_t *fn4, wchar_t *fn5, int sz, int off)
{
    uint8_t ret = 0;

    ret = bios_load_linear(fn3, 0x000f0000, 262144, 128);
    ret &= bios_load_aux_linear(fn1, 0x000d0000, 65536, 128);
    ret &= bios_load_aux_linear(fn2, 0x000c0000, 65536, 128);
    ret &= bios_load_aux_linear(fn4, 0x000e0000, sz - 196608, 128);
    ret &= bios_load_aux_linear(fn5, 0x000ec000, 16384, 128);

    return ret;
}
#endif


int
rom_init(rom_t *rom, wchar_t *fn, uint32_t addr, int sz, int mask, int off, uint32_t flags)
{
    rom_log("rom_init(%08X, %08X, %08X, %08X, %08X, %08X, %08X)\n", rom, fn, addr, sz, mask, off, flags);

    /* Allocate a buffer for the image. */
    rom->rom = malloc(sz);
    memset(rom->rom, 0xff, sz);

    /* Load the image file into the buffer. */
    if (! rom_load_linear(fn, addr, sz, off, rom->rom)) {
	/* Nope.. clean up. */
	free(rom->rom);
	rom->rom = NULL;
	return(-1);
    }

    rom->sz = sz;
    rom->mask = mask;

    mem_mapping_add(&rom->mapping,
		    addr, sz,
		    rom_read, rom_readw, rom_readl,
		    mem_write_null, mem_write_nullw, mem_write_nulll,
		    rom->rom, flags | MEM_MAPPING_ROM, rom);

    return(0);
}


int
rom_init_interleaved(rom_t *rom, wchar_t *fnl, wchar_t *fnh, uint32_t addr, int sz, int mask, int off, uint32_t flags)
{
    /* Allocate a buffer for the image. */
    rom->rom = malloc(sz);
    memset(rom->rom, 0xff, sz);

    /* Load the image file into the buffer. */
    if (! rom_load_interleaved(fnl, fnh, addr, sz, off, rom->rom)) {
	/* Nope.. clean up. */
	free(rom->rom);
	rom->rom = NULL;
	return(-1);
    }

    rom->sz = sz;
    rom->mask = mask;

    mem_mapping_add(&rom->mapping,
		    addr, sz,
		    rom_read, rom_readw, rom_readl,
		    mem_write_null, mem_write_nullw, mem_write_nulll,
		    rom->rom, flags | MEM_MAPPING_ROM, rom);

    return(0);
}
