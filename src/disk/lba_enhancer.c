/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          Vision Systems LBA Enhancer emulation.
 *
 *
 *
 * Authors: Cacodemon345
 *
 *          Copyright 2024 Cacodemon345
 */

#include <stdarg.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <stdlib.h>
#define HAVE_STDARG_H
#include <86box/86box.h>
#include <86box/io.h>
#include <86box/device.h>
#include <86box/mem.h>
#include <86box/rom.h>
#include <86box/plat_unused.h>

typedef struct lba_enhancer_t
{
    rom_t    rom;
} lba_enhancer_t;

#define BIOS_LBA_ENHANCER "roms/hdd/misc/lbaenhancer.bin"

void
lba_enhancer_close(void* priv)
{
    free(priv);

    return;
}

void *
lba_enhancer_init(UNUSED(const device_t *info))
{
    lba_enhancer_t *dev = (lba_enhancer_t *) calloc(1, sizeof(lba_enhancer_t));

    rom_init(&dev->rom, BIOS_LBA_ENHANCER,
             device_get_config_hex20("bios_addr"), 0x4000, 0x3fff, 0, MEM_MAPPING_EXTERNAL);

    return dev;
}

static int
lba_enhancer_available(void)
{
    return rom_present(BIOS_LBA_ENHANCER);
}

// clang-format off
static const device_config_t lba_enhancer_config[] = {
    {
        .name           = "bios_addr",
        .description    = "BIOS Address",
        .type           = CONFIG_HEX20,
        .default_string = NULL,
        .default_int    = 0xc8000,
        .file_filter    = NULL,
        .spinner        = { 0 },
        .selection      = {
            { .description = "C800H", .value = 0xc8000 },
            { .description = "CC00H", .value = 0xcc000 },
            { .description = "D000H", .value = 0xd0000 },
            { .description = "D400H", .value = 0xd4000 },
            { .description = "D800H", .value = 0xd8000 },
            { .description = "DC00H", .value = 0xdc000 },
            { .description = ""                        }
        },
        .bios           = { { 0 } }
    },
    { .name = "", .description = "", .type = CONFIG_END }
};
// clang-format on

const device_t lba_enhancer_device = {
    .name          = "Vision Systems LBA Enhancer",
    .internal_name = "lba_enhancer",
    .flags         = DEVICE_AT,
    .local         = 0,
    .init          = lba_enhancer_init,
    .close         = lba_enhancer_close,
    .reset         = NULL,
    .available     = lba_enhancer_available,
    .speed_changed = NULL,
    .force_redraw  = NULL,
    .config        = lba_enhancer_config
};
