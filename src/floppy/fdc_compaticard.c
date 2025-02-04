/*
 * 86Box     A hypervisor and IBM PC system emulator that specializes in
 *           running old operating systems and software designed for IBM
 *           PC systems and compatibles from 1981 through fairly recent
 *           system designs based on the PCI bus.
 *
 *           This file is part of the 86Box distribution.
 *
 *           Emulation of Micro Solutions CompatiCard I/II/IV.
 *
 * Authors:  Jasmine Iwanek, <jasmine@iwanek.co.uk>
 *
 *           Copyright 2022-2025 Jasmine Iwanek.
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
#include <86box/plat_unused.h>

#define DEVICE_COMPATICARD_I  0
#define DEVICE_COMPATICARD_II 1
#define DEVICE_COMPATICARD_IV 2

#define BIOS_ADDR (uint32_t)(device_get_config_hex20("bios_addr") & 0x000fffff)
#define ROM_COMPATICARD_IV "roms/floppy/compaticard/ccivbios1.05.bin"

#define CR_2_MASK 0x2f /* 00101111b */

typedef struct compaticard_s {
    rom_t   bios_rom;
    fdc_t  *fdc;
    /*
     * 7 - Reserved - Set to 0
     * 6 - Reserved - Set to 0
     * 5 - Programmable Pin 2 Logic I sets Pin 2 low (TODO)
     * 4 - Reserved - Set to 0
     * 3-0 - Data Transfer Rate Select (TODO)
     *       0000---250 Kbps
     *       0001-300 Kbps
     *       1111-500 Kbps
	 */
    uint8_t cr_2;
} compaticard_t;

static void
compaticard_out(UNUSED(uint16_t port), uint8_t val, void *priv)
{
    compaticard_t *dev = (compaticard_t *) priv;

    dev->cr_2 = (val & CR_2_MASK);
}

static uint8_t
compaticard_in(UNUSED(uint16_t port), void *priv)
{
    compaticard_t *dev  = (compaticard_t *) priv;
    uint8_t        ret  = (dev->cr_2 &CR_2_MASK);

    return ret;
}

static void
compaticard_close(void *priv)
{
    compaticard_t *dev = (compaticard_t *) priv;

    free(dev);
}

static void *
compaticard_init(const device_t *info)
{
    compaticard_t *dev       = calloc(1, sizeof(compaticard_t));
    uint16_t       base_addr = device_get_config_hex16("base");
    uint8_t        irq       = 6;
    uint8_t        dma       = 2;
    uint16_t       cr2_addr  = 0x7f2; // Control Register 2

    // CompatiCard II & IV have configurable IRQ and DMA
    if (info->local >= DEVICE_COMPATICARD_II) {
        irq = device_get_config_int("irq");
        dma = device_get_config_int("dma");
    }

    // Only on CompatiCard IV
    if ((info->local == DEVICE_COMPATICARD_IV) && (BIOS_ADDR != 0))
        rom_init(&dev->bios_rom, ROM_COMPATICARD_IV, BIOS_ADDR, 0x2000, 0x1ffff, 0, MEM_MAPPING_EXTERNAL);

    // TODO: Make this neater
    switch (base_addr) {
        case FDC_SECONDARY_ADDR:
            cr2_addr = 0x772;
            if (info->local == DEVICE_COMPATICARD_IV)
                dev->fdc = device_add(&fdc_at_sec_device);
            else
                dev->fdc = device_add(&fdc_xt_sec_device);
            break;

        case FDC_TERTIARY_ADDR:
            cr2_addr = 0x762;
            if (info->local == DEVICE_COMPATICARD_IV)
                dev->fdc = device_add(&fdc_at_ter_device);
            else
                dev->fdc = device_add(&fdc_xt_ter_device);
            break;

        case FDC_QUATERNARY_ADDR:
            cr2_addr = 0x7e2;
            if (info->local == DEVICE_COMPATICARD_IV)
                dev->fdc = device_add(&fdc_at_qua_device);
            else
                dev->fdc = device_add(&fdc_xt_qua_device);
            break;

        default:
            if (info->local == DEVICE_COMPATICARD_IV)
                dev->fdc = device_add(&fdc_at_device);
            else
                dev->fdc = device_add(&fdc_xt_device);
            break;
    }

    fdc_set_irq(dev->fdc, irq);
    fdc_set_dma_ch(dev->fdc, dma);

    io_sethandler(cr2_addr, 0x0001,
                  compaticard_in, NULL, NULL,
                  compaticard_out, NULL, NULL,
                  dev);

    return dev;
}

static int compaticard_iv_available(void)
{
    return rom_present(ROM_COMPATICARD_IV);
}

static const device_config_t compaticard_i_config[] = {
// clang-format off
    {
        .name           = "base",
        .description    = "Address",
        .type           = CONFIG_HEX16,
        .default_string = NULL,
        .default_int    = 0x3f0,
        .file_filter    = NULL,
        .spinner        = { 0 },
        .selection      = {
            { .description = "0x3f0", .value = 0x3f0 },
            { .description = "0x370", .value = 0x370 },
            { .description = "0x360", .value = 0x360 },
            { .description = "0x3e0", .value = 0x3e0 },
            { .description = ""                      }
        },
        .bios           = { { 0 } }
    },
    { .name = "", .description = "", .type = CONFIG_END }
// clang-format on
};

static const device_config_t compaticard_ii_config[] = {
// clang-format off
    {
        .name           = "base",
        .description    = "Address",
        .type           = CONFIG_HEX16,
        .default_string = NULL,
        .default_int    = 0x3f0,
        .file_filter    = NULL,
        .spinner        = { 0 },
        .selection      = {
            { .description = "0x3f0", .value = 0x3f0 },
            { .description = "0x370", .value = 0x370 },
            { .description = "0x360", .value = 0x360 },
            { .description = "0x3e0", .value = 0x3e0 },
            { .description = ""                      }
        },
        .bios           = { { 0 } }
    },
    {
        .name           = "irq",
        .description    = "IRQ",
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
        .name           = "dma",
        .description    = "DMA channel",
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
    { .name = "", .description = "", .type = CONFIG_END }
// clang-format on
};

static const device_config_t compaticard_iv_config[] = {
// clang-format off
    {
        .name           = "base",
        .description    = "Address",
        .type           = CONFIG_HEX16,
        .default_string = NULL,
        .default_int    = 0x3f0,
        .file_filter    = NULL,
        .spinner        = { 0 },
        .selection      = {
            { .description = "0x3f0", .value = 0x3f0 },
            { .description = "0x370", .value = 0x370 },
            { .description = "0x360", .value = 0x360 },
            { .description = "0x3e0", .value = 0x3e0 },
            { .description = ""                      }
        },
        .bios           = { { 0 } }
    },
    {
        .name           = "irq",
        .description    = "IRQ",
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
        .name           = "dma",
        .description    = "DMA channel",
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
    {
        .name           = "bios_addr",
        .description    = "BIOS Address:",
        .type           = CONFIG_HEX20,
        .default_string = NULL,
        .default_int    = 0xce000,
        .file_filter    = NULL,
        .spinner        = { 0 },
        .selection      = {
            { .description = "Disabled", .value = 0       },
            { .description = "CC00H",    .value = 0xcc000 },
            { .description = "CE00H",    .value = 0xce000 },
            { .description = "D000H",    .value = 0xd0000 },
            { .description = "D800H",    .value = 0xd8000 },
            { .description = "DE00H",    .value = 0xde000 },
            { .description = "E000H",    .value = 0xe0000 },
            { .description = "E800H",    .value = 0xe8000 },
            { .description = "EE00H",    .value = 0xee000 },
            { .description = ""                           }
        },
        .bios           = { { 0 } }
    },
#if 0
    {
        .name           = "autoboot_enabled",
        .description    = "Enable Autoboot",
        .type           = CONFIG_BINARY,
        .default_string = NULL,
        .default_int    = 0,
        .file_filter    = NULL,
        .spinner        = { 0 },
        .selection      = { { 0 } },
        .bios           = { { 0 } }
    },
#endif
    { .name = "", .description = "", .type = CONFIG_END }
// clang-format on
};

const device_t fdc_compaticard_i_device = {
    .name          = "Micro Solutions CompatiCard I",
    .internal_name = "compaticard_i",
    .flags         = DEVICE_ISA,
    .local         = 0,
    .init          = compaticard_init,
    .close         = compaticard_close,
    .reset         = NULL,
    .available     = NULL,
    .speed_changed = NULL,
    .force_redraw  = NULL,
    .config        = compaticard_i_config
};

const device_t fdc_compaticard_ii_device = {
    .name          = "Micro Solutions CompatiCard II",
    .internal_name = "compaticard_ii",
    .flags         = DEVICE_ISA,
    .local         = 1,
    .init          = compaticard_init,
    .close         = compaticard_close,
    .reset         = NULL,
    .available     = NULL,
    .speed_changed = NULL,
    .force_redraw  = NULL,
    .config        = compaticard_ii_config
};

const device_t fdc_compaticard_iv_device = {
    .name          = "Micro Solutions CompatiCard IV",
    .internal_name = "compaticard_iv",
    .flags         = DEVICE_ISA,
    .local         = 2,
    .init          = compaticard_init,
    .close         = compaticard_close,
    .reset         = NULL,
    .available     = compaticard_iv_available,
    .speed_changed = NULL,
    .force_redraw  = NULL,
    .config        = compaticard_iv_config
};
