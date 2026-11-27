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
          "directory:\n",
          what ? what : "ROM");
    if (candidates) {
        for (const char *const *p = candidates; *p; p++)
            pclog("[PC98]     %s\n", *p);
    }

    return 0;
}

#define PC98_IPL_PATH      "roms/machines/pc9801/ipl.rom"
#define PC98_ITF_PATH      "roms/machines/pc9801/itf.rom"
#define PC98_BIOS_PATH     "roms/machines/pc9801/bios.rom"
#define PC98_VX_YLL01_PATH "roms/machines/pc9801/nec_d27c256d-15_cpu_extboard_yll01.bin"
#define PC98_VX_YLL02_PATH "roms/machines/pc9801/nec_d27c256d-15_cpu_extboard_yll02.bin"
#define PC98_VX_YLL03_PATH "roms/machines/pc9801/nec_d27c256d-15_cpu_extboard_yll03.bin"
#define PC98_VX_YLL04_PATH "roms/machines/pc9801/nec_d27c256d-15_cpu_extboard_yll04.bin"

int
pc98_rom_read_exact(const char *path, uint8_t *dst, size_t bytes)
{
    FILE *fp = rom_fopen(path, "rb");
    if (!fp)
        return 0;
    if (fseek(fp, 0, SEEK_END) != 0 || ftell(fp) != (long) bytes || fseek(fp, 0, SEEK_SET) != 0 || fread(dst, 1, bytes, fp) != bytes) {
        fclose(fp);
        return 0;
    }
    fclose(fp);
    return 1;
}

int
pc98_rom_has_size(const char *path, size_t bytes)
{
    FILE *fp = rom_fopen(path, "rb");
    long  size;
    if (!fp)
        return 0;
    if (fseek(fp, 0, SEEK_END) != 0) {
        fclose(fp);
        return 0;
    }
    size = ftell(fp);
    fclose(fp);
    return size == (long) bytes;
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
uint8_t *
pc98_ipl_load(void)
{
    uint8_t *ipl = (uint8_t *) malloc(PC98_IPL_REGION_SIZE);
    if (!ipl)
        return NULL;
    memset(ipl, 0xff, PC98_IPL_REGION_SIZE);

    if (pc98_rom_read_exact(PC98_IPL_PATH, ipl, PC98_IPL_REGION_SIZE))
        return ipl;

    if (pc98_rom_has_size(PC98_ITF_PATH, 0x8000u) && pc98_rom_has_size(PC98_BIOS_PATH, PC98_IPL_WINDOW_SIZE)) {
        if (!pc98_rom_read_exact(PC98_ITF_PATH, ipl + 0x10000u, 0x8000u) || !pc98_rom_read_exact(PC98_BIOS_PATH, ipl + 0x18000u, PC98_IPL_WINDOW_SIZE)) {
            free(ipl);
            return NULL;
        }
        return ipl;
    }

    if (pc98_rom_has_size(PC98_VX_YLL01_PATH, 0x8000u) && pc98_rom_has_size(PC98_VX_YLL02_PATH, 0x8000u) && pc98_rom_has_size(PC98_VX_YLL03_PATH, 0x8000u) && pc98_rom_has_size(PC98_VX_YLL04_PATH, 0x8000u)) {
        uint8_t *biosrom = (uint8_t *) malloc(0x20000u);
        uint8_t *tmp     = (uint8_t *) malloc(0x8000u);
        if (!biosrom || !tmp) {
            free(tmp);
            free(biosrom);
            free(ipl);
            return NULL;
        }
        memset(biosrom, 0xff, 0x20000u);
        const char *paths[4] = {
            PC98_VX_YLL01_PATH, PC98_VX_YLL03_PATH,
            PC98_VX_YLL02_PATH, PC98_VX_YLL04_PATH
        };
        const unsigned bases[4] = { 0x00000u, 0x00001u, 0x10000u, 0x10001u };
        for (unsigned r = 0; r < 4; r++) {
            if (!pc98_rom_read_exact(paths[r], tmp, 0x8000u)) {
                free(tmp);
                free(biosrom);
                free(ipl);
                return NULL;
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

int
pc98_ipl_available(void)
{
    return pc98_rom_has_size(PC98_IPL_PATH, PC98_IPL_REGION_SIZE) || (pc98_rom_has_size(PC98_ITF_PATH, 0x8000u) && pc98_rom_has_size(PC98_BIOS_PATH, PC98_IPL_WINDOW_SIZE)) || (pc98_rom_has_size(PC98_VX_YLL01_PATH, 0x8000u) && pc98_rom_has_size(PC98_VX_YLL02_PATH, 0x8000u) && pc98_rom_has_size(PC98_VX_YLL03_PATH, 0x8000u) && pc98_rom_has_size(PC98_VX_YLL04_PATH, 0x8000u));
}

uint32_t
pc98_ipl_window_offset(uint32_t addr)
{
    if (addr >= 0x00fe8000u && addr <= 0x00ffffffu)
        return addr - 0x00fe8000u;
    if (addr >= 0x000e8000u && addr <= 0x000fffffu)
        return addr - 0x000e8000u;
    return UINT32_MAX;
}

static void
pc98_bx4_bank_path(unsigned bank, char *path, size_t len)
{
    snprintf(path, len, "roms/machines/pc9801bx4/pc98BANK%u.BIN", bank);
}

int
pc98_ipl_available_for_model(pc98_model_t model)
{
    if (model == PC98_MODEL_VX)
        return pc98_ipl_available();
    if (model != PC98_MODEL_BX4)
        return 0;
    char path[PC98_ROM_PATH_MAX];
    for (unsigned bank = 0; bank < 8; bank++) {
        pc98_bx4_bank_path(bank, path, sizeof(path));
        if (!pc98_rom_has_size(path, PC98_BX4_BANK_SIZE))
            return 0;
    }
    return 1;
}

uint8_t *
pc98_bx4_banks_load(void)
{
    uint8_t *banks = malloc(PC98_BX4_BANKS_SIZE);
    if (!banks)
        return NULL;
    char path[PC98_ROM_PATH_MAX];
    for (unsigned bank = 0; bank < 8; bank++) {
        pc98_bx4_bank_path(bank, path, sizeof(path));
        if (!pc98_rom_read_exact(path, banks + bank * PC98_BX4_BANK_SIZE,
                                 PC98_BX4_BANK_SIZE)) {
            free(banks);
            return NULL;
        }
    }
    return banks;
}

uint8_t *
pc98_ipl_load_for_model(pc98_model_t model)
{
    if (model == PC98_MODEL_VX)
        return pc98_ipl_load();
    if (model != PC98_MODEL_BX4)
        return NULL;
    uint8_t *banks = pc98_bx4_banks_load();
    if (!banks)
        return NULL;
    uint8_t *ipl = malloc(PC98_IPL_REGION_SIZE);
    if (ipl) {
        memset(ipl, 0xff, PC98_IPL_REGION_SIZE);
        /* getitf98 uses logical bank numbering, including on flash boards:
         * bank 4 = ITF, 5 = BASIC at E8000h, 6/7 = BIOS at F0000h/F8000h.
         * Do not interpret these files as physical flash-chip ordering. */
        memcpy(ipl + 0x10000u, banks + 4u * PC98_BX4_BANK_SIZE, PC98_BX4_BANK_SIZE);
        memcpy(ipl + 0x18000u, banks + 5u * PC98_BX4_BANK_SIZE, 3u * PC98_BX4_BANK_SIZE);
    }
    free(banks);
    return ipl;
}

uint32_t
pc98_ipl_window_offset_for_model(uint32_t addr, pc98_model_t model)
{
    if (model == PC98_MODEL_BX4 && addr >= 0xfffe8000u)
        return addr - 0xfffe8000u;
    return pc98_ipl_window_offset(addr);
}
