/*
 * 86Box     A hypervisor and IBM PC system emulator that specializes in
 *           running old operating systems and software designed for IBM
 *           PC systems and compatibles from 1981 through fairly recent
 *           system designs based on the PCI bus.
 *
 *           This file is part of the 86Box distribution.
 *
 *           Emulation of  Sergey Kiselev's Monster Floppy Disk Controller.
 *
 *
 *
 * Authors:  Jasmine Iwanek, <jasmine@iwanek.co.uk>
 *           Miran Grca, <mgrca8@gmail.com>
 *
 *           Copyright 2022-2025 Jasmine Iwanek.
 *           Copyright 2024      Miran Grca.
 */
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <wchar.h>
#define HAVE_STDARG_H
#include <86box/86box.h>
#include <86box/device.h>
#include <86box/io.h>
#include <86box/mem.h>
#include <86box/timer.h>
#include <86box/nvr.h>
#include <86box/rom.h>
#include <86box/machine.h>
#include <86box/timer.h>
#include <86box/fdd.h>
#include <86box/fdc.h>
#include <86box/fdc_ext.h>
#include <86box/plat_unused.h>

#define BIOS_ADDR       (uint32_t)(device_get_config_hex20("bios_addr") & 0x000fffff)
#define ROM_MONSTER_FDC "roms/floppy/monster-fdc/floppy_bios.bin"

typedef struct monster_fdc_t {
    rom_t  bios_rom;
    fdc_t *fdc_pri;
    fdc_t *fdc_sec;
    char   nvr_path[64];
} monster_fdc_t;

static void
rom_write(uint32_t addr, uint8_t val, void *priv)
{
    const rom_t *rom = (rom_t *) priv;

#ifdef ROM_TRACE
    if (rom->mapping.base == ROM_TRACE)
        rom_log("ROM: read byte from BIOS at %06lX\n", addr);
#endif

    if (addr < rom->mapping.base)
        return;
    if (addr >= (rom->mapping.base + rom->sz))
        return;
    rom->rom[(addr - rom->mapping.base) & rom->mask] = val;
}

static void
rom_writew(uint32_t addr, uint16_t val, void *priv)
{
    rom_t *rom = (rom_t *) priv;

#ifdef ROM_TRACE
    if (rom->mapping.base == ROM_TRACE)
        rom_log("ROM: read word from BIOS at %06lX\n", addr);
#endif

    if (addr < (rom->mapping.base - 1))
        return;
    if (addr >= (rom->mapping.base + rom->sz))
        return;
    *(uint16_t *) &rom->rom[(addr - rom->mapping.base) & rom->mask] = val;
}

static void
rom_writel(uint32_t addr, uint32_t val, void *priv)
{
    rom_t *rom = (rom_t *) priv;

#ifdef ROM_TRACE
    if (rom->mapping.base == ROM_TRACE)
        rom_log("ROM: read long from BIOS at %06lX\n", addr);
#endif

    if (addr < (rom->mapping.base - 3))
        return;
    if (addr >= (rom->mapping.base + rom->sz))
        return;
    *(uint32_t *) &rom->rom[(addr - rom->mapping.base) & rom->mask] = val;
}

static void
monster_fdc_close(void *priv)
{
    monster_fdc_t *dev = (monster_fdc_t *) priv;

    if (dev->nvr_path[0] != 0x00) {
        FILE *fp = nvr_fopen(dev->nvr_path, "wb");
        if (fp != NULL) {
            fwrite(dev->bios_rom.rom, 1, 0x2000, fp);
            fclose(fp);
        }
    }

    free(dev);
}

static void *
monster_fdc_init(UNUSED(const device_t *info))
{
    monster_fdc_t *dev;

    dev = (monster_fdc_t *) calloc(1, sizeof(monster_fdc_t));

#if 0
    uint8_t sec_irq = device_get_config_int("sec_irq");
    uint8_t sec_dma = device_get_config_int("sec_dma");
#endif

    if (BIOS_ADDR != 0)
        rom_init(&dev->bios_rom, ROM_MONSTER_FDC, BIOS_ADDR, 0x2000, 0x1ffff, 0, MEM_MAPPING_EXTERNAL);

    // Primary FDC
    dev->fdc_pri = device_add(&fdc_at_device);

#if 0
    // Secondary FDC
    uint8_t sec_enabled = device_get_config_int("sec_enabled");
    if (sec_enabled)
        dev->fdc_sec = device_add(&fdc_at_sec_device);
        fdc_set_irq(dev->fdc_sec, sec_irq);
        fdc_set_dma_ch(dev->fdc_sec, sec_dma);
#endif

    uint8_t rom_writes_enabled = device_get_config_int("rom_writes_enabled");
    if (rom_writes_enabled) {
        mem_mapping_set_write_handler(&dev->bios_rom.mapping, rom_write, rom_writew, rom_writel);
        sprintf(dev->nvr_path, "monster_fdc_%i.nvr", device_get_instance());
        FILE *fp = nvr_fopen(dev->nvr_path, "rb");
        if (fp != NULL) {
            (void) !fread(dev->bios_rom.rom, 1, 0x2000, fp);
            fclose(fp);
        }
    }

    return dev;
}

static int
monster_fdc_available(void)
{
    return rom_present(ROM_MONSTER_FDC);
}

static const device_config_t monster_fdc_config[] = {
  // clang-format off
#if 0
    {
        .name           = "sec_enabled",
        .description    = "Enable Secondary Controller",
        .type           = CONFIG_BINARY,
        .default_string = NULL,
        .default_int    = 0,
        .file_filter    = NULL,
        .spinner        = { 0 },
        .selection      = { { 0 } },
        .bios           = { { 0 } }
    },
    {
        .name           = "sec_irq",
        .description    = "Secondary Controller IRQ",
        .type           = CONFIG_SELECTION,
        .default_string = NULL,
        .default_int    = 6,
        .file_filter    = NULL,
        .spinner        = { 0 },
        .selection      = {
            { .description = "IRQ 2", .value = 2 },
            { .description = "IRQ 3", .value = 3 },
            { .description = "IRQ 4", .value = 4 },
            { .description = "IRQ 5", .value = 5 },
            { .description = "IRQ 6", .value = 6 },
            { .description = "IRQ 7", .value = 7 },
            { .description = ""                  }
        },
        .bios           = { { 0 } }
    },
    {
        .name           = "sec_dma",
        .description    = "Secondary Controller DMA",
        .type           = CONFIG_SELECTION,
        .default_string = NULL,
        .default_int    = 2,
        .file_filter    = NULL,
        .spinner        = { 0 },
        .selection      = {
            { .description = "DMA 1", .value = 1 },
            { .description = "DMA 2", .value = 2 },
            { .description = "DMA 3", .value = 3 },
            { .description = ""                  }
        },
        .bios           = { { 0 } }
    },
#endif
    {
        .name           = "bios_addr",
        .description    = "BIOS Address",
        .type           = CONFIG_HEX20,
        .default_string = NULL,
        .default_int    = 0xc8000,
        .file_filter    = NULL,
        .spinner        = { 0 },
        .selection      = {
            { .description = "Disabled", .value = 0 },
            { .description = "C000H",    .value = 0xc0000 },
            { .description = "C800H",    .value = 0xc8000 },
            { .description = "D000H",    .value = 0xd0000 },
            { .description = "D800H",    .value = 0xd8000 },
            { .description = "E000H",    .value = 0xe0000 },
            { .description = "E800H",    .value = 0xe8000 },
            { .description = ""                           }
        },
        .bios           = { { 0 } }
    },
#if 0
    {
        .name           = "bios_size",
        .description    = "BIOS Size:",
        .type           = CONFIG_HEX20,
        .default_string = NULL,
        .default_int    = 32,
        .file_filter    = NULL,
        .spinner        = { 0 },
        .selection      = {
            { .description = "8K",  .value = 8  },
            { .description = "32K", .value = 32 },
            { .description = ""                 }
        },
        .bios           = { { 0 } }
    },
#endif
    {
        .name           = "rom_writes_enabled",
        .description    = "Enable BIOS extension ROM Writes",
        .type           = CONFIG_BINARY,
        .default_string = NULL,
        .default_int    = 0,
        .file_filter    = NULL,
        .spinner        = { 0 },
        .selection      = { { 0 } },
        .bios           = { { 0 } }
    },
    { .name = "", .description = "", .type = CONFIG_END }
  // clang-format on
};

const device_t fdc_monster_device = {
    .name          = "Monster FDC Floppy Drive Controller",
    .internal_name = "monster_fdc",
    .flags         = DEVICE_ISA,
    .local         = 0,
    .init          = monster_fdc_init,
    .close         = monster_fdc_close,
    .reset         = NULL,
    .available     = monster_fdc_available,
    .speed_changed = NULL,
    .force_redraw  = NULL,
    .config        = monster_fdc_config
};
