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
 *
 *           Copyright 2022 Jasmine Iwanek.
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
#include <86box/rom.h>
#include <86box/machine.h>
#include <86box/timer.h>
#include <86box/fdd.h>
#include <86box/fdc.h>
#include <86box/fdc_ext.h>

#define BIOS_ADDR (uint32_t)(device_get_config_hex20("bios_addr") & 0x000fffff)
#define ROM_MONSTER_FDC "roms/floppy/monster-fdc/floppy_bios.rom"

typedef struct
{
    rom_t bios_rom;
} monster_fdc_t;

static void
monster_fdc_close(void *priv)
{
    monster_fdc_t *dev = (monster_fdc_t *)priv;

    free(dev);
}

static void *
monster_fdc_init(const device_t *info)
{
    monster_fdc_t *dev;

    dev = (monster_fdc_t *)malloc(sizeof(monster_fdc_t));
    memset(dev, 0, sizeof(monster_fdc_t));

    if (BIOS_ADDR != 0)
        rom_init(&dev->bios_rom, ROM_MONSTER_FDC, BIOS_ADDR, 0x2000, 0x1ffff, 0, MEM_MAPPING_EXTERNAL);

    // Primary FDC
    device_add(&fdc_at_device);

    // Secondary FDC
    // device_add(&fdc_at_sec_device);

    return dev;
}

static int monster_fdc_available(void)
{
    return rom_present(ROM_MONSTER_FDC);
}

static const device_config_t monster_fdc_config[] = {
// clang-format off
/*
    {
        .name = "sec_irq",
        .description = "Secondary Controller IRQ",
        .type = CONFIG_SELECTION,
        .default_string = "",
        .default_int = 6,
        .file_filter = "",
        .spinner = { 0 },
        .selection = {
            {
                .description = "IRQ 2",
                .value = 2
            },
            {
                .description = "IRQ 3",
                .value = 3
            },
            {
                .description = "IRQ 4",
                .value = 4
            },
            {
                .description = "IRQ 5",
                .value = 5
            },
            {
                .description = "IRQ 6",
                .value = 6
            },
            {
                .description = "IRQ 7",
                .value = 7
            },
            { .description = "" }
        }
    },
    {
        .name = "sec_dma",
        .description = "Secondary Controller DMA",
        .type = CONFIG_SELECTION,
        .default_string = "",
        .default_int = 2,
        .file_filter = "",
        .spinner = { 0 },
        .selection = {
            {
                .description = "DMA 1",
                .value = 1
            },
            {
                .description = "DMA 2",
                .value = 2
            },
            {
                .description = "DMA 3",
                .value = 3
            },
            { .description = "" }
        }
    },
*/
    {
        .name = "bios_addr",
        .description = "BIOS Address:",
        .type = CONFIG_HEX20,
        .default_string = "",
        .default_int = 0xc8000,
        .file_filter = "",
        .spinner = { 0 },
        .selection = {
            { .description = "Disabled", .value = 0 },
            { .description = "C000H",    .value = 0xc0000 },
            { .description = "C800H",    .value = 0xc8000 },
            { .description = "D000H",    .value = 0xd0000 },
            { .description = "D800H",    .value = 0xd8000 },
            { .description = "E000H",    .value = 0xe0000 },
            { .description = "E800H",    .value = 0xe8000 },
            { .description = ""                           }
        }
    },
/*
    {
        .name = "bios_size",
        .description = "BIOS Size:",
        .type = CONFIG_HEX20,
        .default_string = "32",
        .default_int = 0xc8000,
        .file_filter = "",
        .spinner = { 0 },
        .selection = {
            { .description = "8K",  .value = 8  },
            { .description = "32K", .value = 32 },
            { .description = ""                 }
        }
    },
*/
    // BIOS extension ROM writes: Enabled/Disabled
    { .name = "", .description = "", .type = CONFIG_END }
// clang-format on
};

const device_t fdc_monster_device = {
    .name = "Monster FDC Floppy Drive Controller",
    .internal_name = "monster_fdc",
    .flags = DEVICE_ISA,
    .local = 0,
    .init = monster_fdc_init,
    .close = monster_fdc_close,
    .reset = NULL,
    { .available = monster_fdc_available },
    .speed_changed = NULL,
    .force_redraw = NULL,
    .config =monster_fdc_config
};
