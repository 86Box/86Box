/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          Locating PC-98 C-BUS board firmware across the layouts these
 *          ROM sets actually ship in.
 */
#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <86box/mem.h>
#include <86box/86box.h>
#include <86box/pc98_rom.h>
#include <86box/rom.h>
#include <86box/vid_pc98.h>

int
pc98_rom_find_quiet(const char *const *candidates, char *out, size_t out_len)
{
    if (!candidates || !out || !out_len)
        return 0;

    for (const char *const *p = candidates; *p; p++) {
        /*
         * rom_present() goes through rom_fopen(), which tries the path
         * under every configured ROM root - so this covers both "a
         * different directory" and "a different layout inside it".
         */
        if (rom_present(*p)) {
            snprintf(out, out_len, "%s", *p);
            return 1;
        }
    }

    out[0] = '\0';
    return 0;
}

int
pc98_rom_find(const char *const *candidates, char *out, size_t out_len,
              const char *what)
{
    if (pc98_rom_find_quiet(candidates, out, out_len))
        return 1;

    /* Say what was looked for and where, rather than just failing. */
    pclog("[PC98] %s not found.  Tried, under every configured ROM "
          "directory:\n", what ? what : "ROM");
    if (candidates) {
        for (const char *const *p = candidates; *p; p++)
            pclog("[PC98]     %s\n", *p);
    }

    return 0;
}

#define PC98_IPL_PATH       "roms/machines/pc9801/ipl.rom"
#define PC98_ITF_PATH       "roms/machines/pc9801/itf.rom"
#define PC98_BIOS_PATH      "roms/machines/pc9801/bios.rom"
#define PC98_VX_YLL01_PATH  "roms/machines/pc9801/nec_d27c256d-15_cpu_extboard_yll01.bin"
#define PC98_VX_YLL02_PATH  "roms/machines/pc9801/nec_d27c256d-15_cpu_extboard_yll02.bin"
#define PC98_VX_YLL03_PATH  "roms/machines/pc9801/nec_d27c256d-15_cpu_extboard_yll03.bin"
#define PC98_VX_YLL04_PATH  "roms/machines/pc9801/nec_d27c256d-15_cpu_extboard_yll04.bin"

int pc98_rom_read_exact(const char *path, uint8_t *dst, size_t bytes)
{
    FILE *fp = rom_fopen(path, "rb");
    if (!fp) return 0;
    if (fseek(fp, 0, SEEK_END) != 0 || ftell(fp) != (long)bytes ||
        fseek(fp, 0, SEEK_SET) != 0 || fread(dst, 1, bytes, fp) != bytes) {
        fclose(fp);
        return 0;
    }
    fclose(fp);
    return 1;
}

int pc98_rom_has_size(const char *path, size_t bytes)
{
    FILE *fp = rom_fopen(path, "rb");
    long size;
    if (!fp) return 0;
    if (fseek(fp, 0, SEEK_END) != 0) { fclose(fp); return 0; }
    size = ftell(fp);
    fclose(fp);
    return size == (long)bytes;
}

/* Build the banked PC-98 IPL layout from user-supplied dumps.
 *
 *   region 00000-0ffff : erased
 *   region 10000-17fff : ITF (reset bank)
 *   region 18000-2ffff : main BIOS bank
 *
 * Accepted inputs, in preference order:
 *   1) a ready-made 0x30000 ipl.rom;
 *   2) 0x8000 itf.rom + 0x18000 bios.rom;
 *   3) the four physical 27C256 VX CPU-board dumps.
 */
uint8_t *pc98_ipl_load(void)
{
    uint8_t *ipl = (uint8_t *)malloc(PC98_IPL_REGION_SIZE);
    if (!ipl) return NULL;
    memset(ipl, 0xff, PC98_IPL_REGION_SIZE);

    if (pc98_rom_read_exact(PC98_IPL_PATH, ipl, PC98_IPL_REGION_SIZE))
        return ipl;

    if (pc98_rom_has_size(PC98_ITF_PATH, 0x8000u) &&
        pc98_rom_has_size(PC98_BIOS_PATH, PC98_IPL_WINDOW_SIZE)) {
        if (!pc98_rom_read_exact(PC98_ITF_PATH, ipl + 0x10000u, 0x8000u) ||
            !pc98_rom_read_exact(PC98_BIOS_PATH, ipl + 0x18000u, PC98_IPL_WINDOW_SIZE)) {
            free(ipl);
            return NULL;
        }
        return ipl;
    }

    if (pc98_rom_has_size(PC98_VX_YLL01_PATH, 0x8000u) &&
        pc98_rom_has_size(PC98_VX_YLL02_PATH, 0x8000u) &&
        pc98_rom_has_size(PC98_VX_YLL03_PATH, 0x8000u) &&
        pc98_rom_has_size(PC98_VX_YLL04_PATH, 0x8000u)) {
        uint8_t *biosrom = (uint8_t *)malloc(0x20000u);
        uint8_t *tmp = (uint8_t *)malloc(0x8000u);
        if (!biosrom || !tmp) { free(tmp); free(biosrom); free(ipl); return NULL; }
        memset(biosrom, 0xff, 0x20000u);
        const char *paths[4] = {
            PC98_VX_YLL01_PATH, PC98_VX_YLL03_PATH,
            PC98_VX_YLL02_PATH, PC98_VX_YLL04_PATH
        };
        const unsigned bases[4] = { 0x00000u, 0x00001u, 0x10000u, 0x10001u };
        for (unsigned r = 0; r < 4; r++) {
            if (!pc98_rom_read_exact(paths[r], tmp, 0x8000u)) {
                free(tmp); free(biosrom); free(ipl); return NULL;
            }
            for (unsigned i = 0; i < 0x8000u; i++)
                biosrom[bases[r] + i * 2u] = tmp[i];
        }
        memcpy(ipl + 0x10000u, biosrom + 0x18000u, 0x8000u);
        memcpy(ipl + 0x18000u, biosrom + 0x08000u, 0x8000u);
        memcpy(ipl + 0x20000u, biosrom + 0x00000u, 0x8000u);
        memcpy(ipl + 0x28000u, biosrom + 0x10000u, 0x8000u);
        free(tmp);
        free(biosrom);
        return ipl;
    }

    free(ipl);
    return NULL;
}


int pc98_ipl_available(void)
{
    return pc98_rom_has_size(PC98_IPL_PATH, PC98_IPL_REGION_SIZE) ||
        (pc98_rom_has_size(PC98_ITF_PATH, 0x8000u) &&
         pc98_rom_has_size(PC98_BIOS_PATH, PC98_IPL_WINDOW_SIZE)) ||
        (pc98_rom_has_size(PC98_VX_YLL01_PATH, 0x8000u) &&
         pc98_rom_has_size(PC98_VX_YLL02_PATH, 0x8000u) &&
         pc98_rom_has_size(PC98_VX_YLL03_PATH, 0x8000u) &&
         pc98_rom_has_size(PC98_VX_YLL04_PATH, 0x8000u));
}

uint32_t pc98_ipl_window_offset(uint32_t addr)
{
    if (addr >= 0x00fe8000u && addr <= 0x00ffffffu)
        return addr - 0x00fe8000u;
    if (addr >= 0x000e8000u && addr <= 0x000fffffu)
        return addr - 0x000e8000u;
    return UINT32_MAX;
}
