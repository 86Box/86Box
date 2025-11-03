/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          Handling of ROM image files.
 *
 * NOTES:   - pc2386 BIOS is corrupt (JMP at F000:FFF0 points to RAM)
 *          - pc2386 video BIOS is underdumped (16k instead of 24k)
 *          - c386sx16 BIOS fails checksum
 *
 * Authors: Sarah Walker, <https://pcem-emulator.co.uk/>
 *          Miran Grca, <mgrca8@gmail.com>
 *          Fred N. van Kempen, <decwiz@yahoo.com>
 *
 *          Copyright 2008-2019 Sarah Walker.
 *          Copyright 2016-2019 Miran Grca.
 *          Copyright 2018-2019 Fred N. van Kempen.
 */
#include <stdarg.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <stdlib.h>
#include <wchar.h>
#include <stdbool.h>
#define HAVE_STDARG_H
#include <86box/86box.h>
#include "cpu.h"
#include <86box/mem.h>
#include <86box/rom.h>
#include <86box/path.h>
#include <86box/plat.h>
#include <86box/machine.h>
#include <86box/m_xt_xi8088.h>

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
#    define rom_log(fmt, ...)
#endif

void
rom_add_path(const char *path)
{
    char cwd[1024] = { 0 };

    rom_path_t *rom_path = &rom_paths;

    if (rom_paths.path[0] != '\0') {
        // Iterate to the end of the list.
        while (rom_path->next != NULL) {
            rom_path = rom_path->next;
        }

        // Allocate the new entry.
        rom_path = rom_path->next = calloc(1, sizeof(rom_path_t));
    }

    // Save the path, turning it into absolute if needed.
    if (!path_abs((char *) path)) {
        plat_getcwd(cwd, sizeof(cwd));
        path_slash(cwd);
        snprintf(rom_path->path, sizeof(rom_path->path), "%s%s", cwd, path);
    } else {
        snprintf(rom_path->path, sizeof(rom_path->path), "%s", path);
    }

    // Ensure the path ends with a separator.
    path_slash(rom_path->path);
}

static int
rom_check(const char *fn)
{
    FILE *fp = NULL;
    int ret = 0;
    char last = fn[strlen(fn) - 1];

    if ((last == '/') || (last == '\\'))
        ret = plat_dir_check((char *) fn);
    else {
        fp = fopen(fn, "rb");
        ret = (fp != NULL);
        if (fp != NULL)
            fclose(fp);
    }

    return ret;
}

void
rom_get_full_path(char *dest, const char *fn)
{
    char temp[1024] = { 0 };

    dest[0] = 0x00;

    if (!strncmp(fn, "roms/", 5)) {
        /* Relative path */
        for (rom_path_t *rom_path = &rom_paths; rom_path != NULL; rom_path = rom_path->next) {
            path_append_filename(temp, rom_path->path, fn + 5);

            if (rom_check(temp)) {
                strcpy(dest, temp);
                return;
            }
        }

        return;
    } else {
        /* Absolute path */
        strcpy(dest, fn);
    }
}

FILE *
rom_fopen(const char *fn, char *mode)
{
    char        temp[1024];
    FILE       *fp = NULL;

    if ((fn == NULL) || (mode == NULL))
        return NULL;

    if (!strncmp(fn, "roms/", 5)) {
        /* Relative path */
        for (rom_path_t *rom_path = &rom_paths; rom_path != NULL; rom_path = rom_path->next) {
            path_append_filename(temp, rom_path->path, fn + 5);

            if ((fp = plat_fopen(temp, mode)) != NULL)
                return fp;
        }

        return fp;
    } else {
        /* Absolute path */
        return plat_fopen(fn, mode);
    }
}

int
rom_getfile(const char *fn, char *s, int size)
{
    char        temp[1024];

    if (!strncmp(fn, "roms/", 5)) {
        /* Relative path */
        for (rom_path_t *rom_path = &rom_paths; rom_path != NULL; rom_path = rom_path->next) {
            path_append_filename(temp, rom_path->path, fn + 5);

            if (plat_file_check(temp)) {
                strncpy(s, temp, size);
                return 1;
            }
        }

        return 0;
    } else {
        /* Absolute path */
        if (plat_file_check(fn)) {
            strncpy(s, fn, size);
            return 1;
        }

        return 0;
    }
}

int
rom_present(const char *fn)
{
    char temp[1024];

    if (fn == NULL)
        return 0;

    if (!strncmp(fn, "roms/", 5)) {
        /* Relative path */
        for (rom_path_t *rom_path = &rom_paths; rom_path != NULL; rom_path = rom_path->next) {
            path_append_filename(temp, rom_path->path, fn + 5);

            if (plat_file_check(temp))
                return 1;
        }

        return 0;
    } else {
        /* Absolute path */
        return plat_file_check(fn);
    }
}

uint8_t
rom_read(uint32_t addr, void *priv)
{
    const rom_t *rom = (rom_t *) priv;

#ifdef ROM_TRACE
    if (rom->mapping.base == ROM_TRACE)
        rom_log("ROM: read byte from BIOS at %06lX\n", addr);
#endif

    if (addr < rom->mapping.base)
        return 0xff;
    if (addr >= (rom->mapping.base + rom->sz))
        return 0xff;
    return (rom->rom[(addr - rom->mapping.base) & rom->mask]);
}

uint16_t
rom_readw(uint32_t addr, void *priv)
{
    rom_t *rom = (rom_t *) priv;

#ifdef ROM_TRACE
    if (rom->mapping.base == ROM_TRACE)
        rom_log("ROM: read word from BIOS at %06lX\n", addr);
#endif

    if (addr < (rom->mapping.base - 1))
        return 0xffff;
    if (addr >= (rom->mapping.base + rom->sz))
        return 0xffff;
    return (*(uint16_t *) &rom->rom[(addr - rom->mapping.base) & rom->mask]);
}

uint32_t
rom_readl(uint32_t addr, void *priv)
{
    rom_t *rom = (rom_t *) priv;

#ifdef ROM_TRACE
    if (rom->mapping.base == ROM_TRACE)
        rom_log("ROM: read long from BIOS at %06lX\n", addr);
#endif

    if (addr < (rom->mapping.base - 3))
        return 0xffffffff;
    if (addr >= (rom->mapping.base + rom->sz))
        return 0xffffffff;
    return (*(uint32_t *) &rom->rom[(addr - rom->mapping.base) & rom->mask]);
}

void
rom_write(uint32_t addr, uint8_t val, void *priv)
{
    const rom_t *rom = (rom_t *) priv;

#ifdef ROM_TRACE
    if (rom->mapping.base == ROM_TRACE)
        rom_log("ROM: write byte from BIOS at %06lX\n", addr);
#endif

    if (addr < rom->mapping.base)
        return;
    if (addr >= (rom->mapping.base + rom->sz))
        return;
    rom->rom[(addr - rom->mapping.base) & rom->mask] = val;
}

void
rom_writew(uint32_t addr, uint16_t val, void *priv)
{
    rom_t *rom = (rom_t *) priv;

#ifdef ROM_TRACE
    if (rom->mapping.base == ROM_TRACE)
        rom_log("ROM: write word from BIOS at %06lX\n", addr);
#endif

    if (addr < (rom->mapping.base - 1))
        return;
    if (addr >= (rom->mapping.base + rom->sz))
        return;
    *(uint16_t *) &rom->rom[(addr - rom->mapping.base) & rom->mask] = val;
}

void
rom_writel(uint32_t addr, uint32_t val, void *priv)
{
    rom_t *rom = (rom_t *) priv;

#ifdef ROM_TRACE
    if (rom->mapping.base == ROM_TRACE)
        rom_log("ROM: write long from BIOS at %06lX\n", addr);
#endif

    if (addr < (rom->mapping.base - 3))
        return;
    if (addr >= (rom->mapping.base + rom->sz))
        return;
    *(uint32_t *) &rom->rom[(addr - rom->mapping.base) & rom->mask] = val;
}

int
rom_load_linear_oddeven(const char *fn, uint32_t addr, int sz, int off, uint8_t *ptr)
{
    FILE *fp = rom_fopen(fn, "rb");

    if (fp == NULL) {
        rom_log("ROM: image '%s' not found\n", fn);
        return 0;
    }

    /* Make sure we only look at the base-256K offset. */
    if (addr >= 0x40000)
        addr = 0;
    else
        addr &= 0x03ffff;

    if (ptr != NULL) {
        if (fseek(fp, off, SEEK_SET) == -1)
            fatal("rom_load_linear(): Error seeking to the beginning of the file\n");
        for (int i = 0; i < (sz >> 1); i++) {
            if (fread(ptr + (addr + (i << 1)), 1, 1, fp) != 1)
                fatal("rom_load_linear(): Error reading even data\n");
        }
        for (int i = 0; i < (sz >> 1); i++) {
            if (fread(ptr + (addr + (i << 1) + 1), 1, 1, fp) != 1)
                fatal("rom_load_linear(): Error reading odd data\n");
        }
    }

    if (fp != NULL)
        (void) fclose(fp);

    return 1;
}

/* Load a ROM BIOS from its chips, interleaved mode. */
int
rom_load_linear(const char *fn, uint32_t addr, int sz, int off, uint8_t *ptr)
{
    FILE *fp = rom_fopen(fn, "rb");

    if (fp == NULL) {
        rom_log("ROM: image '%s' not found\n", fn);
        return 0;
    }

    /* Make sure we only look at the base-256K offset. */
    if (addr >= 0x40000)
        addr = 0;
    else
        addr &= 0x03ffff;

    if (ptr != NULL) {
        if (fseek(fp, off, SEEK_SET) == -1)
            fatal("rom_load_linear(): Error seeking to the beginning of the file\n");
        if (fread(ptr + addr, 1, sz, fp) > sz)
            fatal("rom_load_linear(): Error reading data\n");
    }

    if (fp != NULL)
        (void) fclose(fp);

    return 1;
}

/* Load a ROM BIOS from its chips, linear mode with high bit flipped. */
int
rom_load_linear_inverted(const char *fn, uint32_t addr, int sz, int off, uint8_t *ptr)
{
    FILE *fp = rom_fopen(fn, "rb");

    if (fp == NULL) {
        rom_log("ROM: image '%s' not found\n", fn);
        return 0;
    }

    /* Make sure we only look at the base-256K offset. */
    if (addr >= 0x40000) {
        addr = 0;
    } else {
        addr &= 0x03ffff;
    }

    (void) fseek(fp, 0, SEEK_END);
    if (ftell(fp) < sz) {
        (void) fclose(fp);
        return 0;
    }

    if (ptr != NULL) {
        if (fseek(fp, off, SEEK_SET) == -1)
            fatal("rom_load_linear_inverted(): Error seeking to the beginning of the file\n");
        if (fread(ptr + addr + 0x10000, 1, sz >> 1, fp) > (sz >> 1))
            fatal("rom_load_linear_inverted(): Error reading the upper half of the data\n");
        if (fread(ptr + addr, sz >> 1, 1, fp) > (sz >> 1))
            fatal("rom_load_linear_inverted(): Error reading the lower half of the data\n");
        if (sz == 0x40000) {
            if (fread(ptr + addr + 0x30000, 1, sz >> 1, fp) > (sz >> 1))
                fatal("rom_load_linear_inverted(): Error reading the upper half of the data\n");
            if (fread(ptr + addr + 0x20000, sz >> 1, 1, fp) > (sz >> 1))
                fatal("rom_load_linear_inverted(): Error reading the lower half of the data\n");
        }
    }

    if (fp != NULL)
        (void) fclose(fp);

    return 1;
}

/* Load a ROM BIOS from its chips, interleaved mode. */
int
rom_load_interleaved(const char *fnl, const char *fnh, uint32_t addr, int sz, int off, uint8_t *ptr)
{
    FILE *fpl = rom_fopen(fnl, "rb");
    FILE *fph = rom_fopen(fnh, "rb");

    if (fpl == NULL || fph == NULL) {
        if (fpl == NULL)
            rom_log("ROM: image '%s' not found\n", fnl);
        else
            (void) fclose(fpl);
        if (fph == NULL)
            rom_log("ROM: image '%s' not found\n", fnh);
        else
            (void) fclose(fph);

        return 0;
    }

    /* Make sure we only look at the base-256K offset. */
    if (addr >= 0x40000) {
        addr = 0;
    } else {
        addr &= 0x03ffff;
    }

    if (ptr != NULL) {
        (void) fseek(fpl, off, SEEK_SET);
        (void) fseek(fph, off, SEEK_SET);
        for (int c = 0; c < sz; c += 2) {
            ptr[addr + c]     = fgetc(fpl) & 0xff;
            ptr[addr + c + 1] = fgetc(fph) & 0xff;
        }
    }

    if (fph != NULL)
        (void) fclose(fph);
    if (fpl != NULL)
        (void) fclose(fpl);

    return 1;
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
    rom = (uint8_t *) malloc(biosmask + 1);
    rom_log("Filling ROM with FF's...\n");
    memset(rom, 0xff, biosmask + 1);

    return rom;
}

uint8_t
bios_read(uint32_t addr, UNUSED(void *priv))
{
    uint8_t ret = 0xff;

    addr &= 0x000fffff;

    if ((addr >= biosaddr) && (addr <= (biosaddr + biosmask)))
        ret = rom[addr - biosaddr];

    return ret;
}

uint16_t
bios_readw(uint32_t addr, UNUSED(void *priv))
{
    uint16_t ret = 0xffff;

    addr &= 0x000fffff;

    if ((addr >= biosaddr) && (addr <= (biosaddr + biosmask)))
        ret = *(uint16_t *) &rom[addr - biosaddr];

    return ret;
}

uint32_t
bios_readl(uint32_t addr, UNUSED(void *priv))
{
    uint32_t ret = 0xffffffff;

    addr &= 0x000fffff;

    if ((addr >= biosaddr) && (addr <= (biosaddr + biosmask)))
        ret = *(uint32_t *) &rom[addr - biosaddr];

    return ret;
}

static void
bios_add(void)
{
    int temp_cpu_type;
    int temp_cpu_16bitbus = 1;
    int temp_is286 = 0;
    int temp_is6117 = 0;

    if (/*AT && */ cpu_s) {
        temp_cpu_type     = cpu_s->cpu_type;
        temp_cpu_16bitbus = (temp_cpu_type == CPU_286 || temp_cpu_type == CPU_386SX || temp_cpu_type == CPU_486SLC || temp_cpu_type == CPU_IBM386SLC || temp_cpu_type == CPU_IBM486SLC);
        temp_is286        = (temp_cpu_type >= CPU_286);
        temp_is6117       = !strcmp(cpu_f->manufacturer, "ALi");
    }

    if (biosmask > 0x1ffff) {
        /* 256k+ BIOS'es only have low mappings at E0000-FFFFF. */
        mem_mapping_add(&bios_mapping, 0xe0000, 0x20000,
                        bios_read, bios_readw, bios_readl,
                        NULL, NULL, NULL,
                        &rom[biosmask + 1 - 0x20000], MEM_MAPPING_EXTERNAL | MEM_MAPPING_ROM | MEM_MAPPING_ROMCS, 0);

        mem_set_mem_state_both(0x0e0000, 0x20000,
                               MEM_READ_ROMCS | MEM_WRITE_ROMCS);
    } else {
        mem_mapping_add(&bios_mapping, biosaddr, biosmask + 1,
                        bios_read, bios_readw, bios_readl,
                        NULL, NULL, NULL,
                        rom, MEM_MAPPING_EXTERNAL | MEM_MAPPING_ROM | MEM_MAPPING_ROMCS, 0);

        mem_set_mem_state_both(biosaddr, biosmask + 1,
                               MEM_READ_ROMCS | MEM_WRITE_ROMCS);
    }

    if (temp_is6117) {
        mem_mapping_add(&bios_high_mapping, biosaddr | 0x03f00000, biosmask + 1,
                        bios_read, bios_readw, bios_readl,
                        NULL, NULL, NULL,
                        rom, MEM_MAPPING_EXTERNAL | MEM_MAPPING_ROM | MEM_MAPPING_ROMCS, 0);

        mem_set_mem_state_both(biosaddr | 0x03f00000, biosmask + 1,
                               MEM_READ_ROMCS | MEM_WRITE_ROMCS);
    } else if (temp_is286) {
        mem_mapping_add(&bios_high_mapping, biosaddr | (temp_cpu_16bitbus ? 0x00f00000 : 0xfff00000), biosmask + 1,
                        bios_read, bios_readw, bios_readl,
                        NULL, NULL, NULL,
                        rom, MEM_MAPPING_EXTERNAL | MEM_MAPPING_ROM | MEM_MAPPING_ROMCS, 0);

        mem_set_mem_state_both(biosaddr | (temp_cpu_16bitbus ? 0x00f00000 : 0xfff00000), biosmask + 1,
                               MEM_READ_ROMCS | MEM_WRITE_ROMCS);
    }
}

/* These four are for loading the BIOS. */
int
bios_load(const char *fn1, const char *fn2, uint32_t addr, int sz, int off, int flags)
{
    uint8_t  ret = 0;
    uint8_t *ptr = NULL;
    int      old_sz = sz;

    /*
        f0000, 65536 = prepare 64k rom starting at f0000, load 64k bios at 0000
        fe000, 65536 = prepare 64k rom starting at f0000, load 8k bios at e000
        fe000, 49152 = prepare 48k rom starting at f4000, load 8k bios at a000
        fe000, 8192 = prepare 16k rom starting at fc000, load 8k bios at 2000
     */
    if (!bios_only)
        ptr = (flags & FLAG_AUX) ? rom : rom_reset(addr, sz);
    else
        return (!fn1 || rom_present(fn1)) && (!fn2 || rom_present(fn2));

    if (!(flags & FLAG_AUX) && ((addr + sz) > 0x00100000))
        sz = 0x00100000 - addr;

#ifdef ENABLE_ROM_LOG
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

    if ((flags & FLAG_REP) && (old_sz >= 65536) && (sz < old_sz)) {
        old_sz /= sz;
        for (int i = 0; i < (old_sz - 1); i++) {
            rom_log("Copying ptr[%08X] to ptr[%08X]\n", addr - biosaddr, i * sz);
            memcpy(&(ptr[i * sz]), &(ptr[addr - biosaddr]), sz);
        }
    }

    if (ret && !(flags & FLAG_AUX))
        bios_add();

    return ret;
}

int
bios_load_linear_combined(const char *fn1, const char *fn2, int sz, UNUSED(int off))
{
    return bios_load_linear(fn1, 0x000f0000, 131072, 128) &&
        bios_load_aux_linear(fn2, 0x000e0000, sz - 65536, 128);
}

int
bios_load_linear_combined2(const char *fn1, const char *fn2, const char *fn3, const char *fn4, const char *fn5, int sz, int off)
{
    return bios_load_linear(fn3, 0x000f0000, 262144, off) &&
        bios_load_aux_linear(fn1, 0x000d0000, 65536, off) &&
        bios_load_aux_linear(fn2, 0x000c0000, 65536, off) &&
        bios_load_aux_linear(fn4, 0x000e0000, sz - 196608, off) &&
        (!fn5 || bios_load_aux_linear(fn5, 0x000ec000, 16384, 0));
}

int
bios_load_linear_combined2_ex(const char *fn1, const char *fn2, const char *fn3, const char *fn4, const char *fn5, int sz, int off)
{
    return bios_load_linear(fn3, 0x000e0000, 262144, off) &&
        bios_load_aux_linear(fn1, 0x000c0000, 65536, off) &&
        bios_load_aux_linear(fn2, 0x000d0000, 65536, off) &&
        bios_load_aux_linear(fn4, 0x000f0000, sz - 196608, off) &&
        (!fn5 || bios_load_aux_linear(fn5, 0x000fc000, 16384, 0));
}

int
rom_init(rom_t *rom, const char *fn, uint32_t addr, int sz, int mask, int off, uint32_t flags)
{
    rom_log("rom_init(%08X, %s, %08X, %08X, %08X, %08X, %08X)\n", rom, fn, addr, sz, mask, off, flags);

    /* Allocate a buffer for the image. */
    rom->rom = malloc(sz);
    memset(rom->rom, 0xff, sz);

    /* Load the image file into the buffer. */
    if (!rom_load_linear(fn, addr, sz, off, rom->rom)) {
        /* Nope.. clean up. */
        free(rom->rom);
        rom->rom = NULL;
        return (-1);
    }

    rom->sz   = sz;
    rom->mask = mask;

    mem_mapping_add(&rom->mapping,
                    addr, sz,
                    rom_read, rom_readw, rom_readl,
                    NULL, NULL, NULL,
                    rom->rom, flags | MEM_MAPPING_ROM_WS, rom);

    return 0;
}

int
rom_init_oddeven(rom_t *rom, const char *fn, uint32_t addr, int sz, int mask, int off, uint32_t flags)
{
    rom_log("rom_init(%08X, %08X, %08X, %08X, %08X, %08X, %08X)\n", rom, fn, addr, sz, mask, off, flags);

    /* Allocate a buffer for the image. */
    rom->rom = malloc(sz);
    memset(rom->rom, 0xff, sz);

    /* Load the image file into the buffer. */
    if (!rom_load_linear_oddeven(fn, addr, sz, off, rom->rom)) {
        /* Nope.. clean up. */
        free(rom->rom);
        rom->rom = NULL;
        return (-1);
    }

    rom->sz   = sz;
    rom->mask = mask;

    mem_mapping_add(&rom->mapping,
                    addr, sz,
                    rom_read, rom_readw, rom_readl,
                    NULL, NULL, NULL,
                    rom->rom, flags | MEM_MAPPING_ROM_WS, rom);

    return 0;
}

int
rom_init_interleaved(rom_t *rom, const char *fnl, const char *fnh, uint32_t addr, int sz, int mask, int off, uint32_t flags)
{
    /* Allocate a buffer for the image. */
    rom->rom = malloc(sz);
    memset(rom->rom, 0xff, sz);

    /* Load the image file into the buffer. */
    if (!rom_load_interleaved(fnl, fnh, addr, sz, off, rom->rom)) {
        /* Nope.. clean up. */
        free(rom->rom);
        rom->rom = NULL;
        return (-1);
    }

    rom->sz   = sz;
    rom->mask = mask;

    mem_mapping_add(&rom->mapping,
                    addr, sz,
                    rom_read, rom_readw, rom_readl,
                    NULL, NULL, NULL,
                    rom->rom, flags | MEM_MAPPING_ROM_WS, rom);

    return 0;
}
