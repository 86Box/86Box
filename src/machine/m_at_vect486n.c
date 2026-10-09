// SPDX-License-Identifier: GPL-2.0-or-later
/* HP Vectra 486N system board. */
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>
#define HAVE_STDARG_H
#include <86box/86box.h>
#include "cpu.h"
#include <86box/device.h>
#include <86box/timer.h>
#include <86box/pit.h>
#include <86box/io.h>
#include <86box/mem.h>
#include <86box/rom.h>
#include <86box/chipset.h>
#include <86box/hdc.h>
#include <86box/sio.h>
#include <86box/video.h>
#include <86box/machine.h>
#include <86box/nmc93cxx.h>
#include <86box/plat_unused.h>
#include "m_at_vect486n.h"

typedef struct vect486n_t {
    uint8_t            control[2];
    pit_t             *pit;
    nmc93cxx_eeprom_t  *eeprom;
    mem_mapping_t      setup_mapping;
} vect486n_t;

static uint8_t
vect486n_read(uint16_t port, void *priv)
{
    vect486n_t *dev   = priv;
    uint8_t     value = dev->control[port & 1];

    if (port == 0x9a)
        value = (value & ~0x02) | (pit_classic_intf.get_outlevel(dev->pit, 0) ? 0x02 : 0);
    if (port == 0x9b) {
        /* The input at bit 2 is the active-low configuration-clear switch;
           writes to the same bit drive the EEPROM clock. */
        value = (value & ~0x14) | 0x04 | (nmc93cxx_eeprom_read(dev->eeprom) ? 0x10 : 0);
    }
    return value;
}

static void
vect486n_write(uint16_t port, uint8_t value, void *priv)
{
    vect486n_t *dev = priv;

    dev->control[port & 1] = value;
    if (port == 0x9b)
        nmc93cxx_eeprom_write(dev->eeprom, !!(value & 0x80), !!(value & 0x04), !!(value & 0x10));
}

static uint8_t
vect486n_setup_read(uint32_t addr, UNUSED(void *priv))
{
    return rom[addr & 0x1ffff];
}

static void
vect486n_reset(void *priv)
{
    vect486n_t *dev = priv;

    memset(dev->control, 0, sizeof(dev->control));
    if (dev->pit) {
        pit_device_reset(dev->pit);
        for (int i = 0; i < 3; i++)
            pit_ctr_set_gate(dev->pit, i, 1);
    }
    nmc93cxx_eeprom_write(dev->eeprom, false, false, false);
}

static void *
vect486n_init(UNUSED(const device_t *info))
{
    vect486n_t              *dev    = calloc(1, sizeof(*dev));
    nmc93cxx_eeprom_params_t params = {
        .type            = NMC_93C66_x16_256,
        .filename        = "vect486n_93c66.nvr",
        .default_content = NULL
    };

    dev->eeprom = device_add_params(&nmc93cxx_device, &params);
    dev->pit    = device_add(&i8254_ext_io_device);
    pit_handler(1, 0x009c, 4, dev->pit);
    vect486n_reset(dev);
    io_sethandler(0x009a, 2, vect486n_read, NULL, NULL, vect486n_write, NULL, NULL, dev);

    /* The ROM module loader reads the lower flash half through this alias.
       The 64 KiB T.04.02 dump does not contain these modules. */
    if (biosmask == 0x3ffff) {
        mem_mapping_add(&dev->setup_mapping, 0xf7fe0000, 0x20000,
                        vect486n_setup_read, NULL, NULL, NULL, NULL, NULL,
                        rom, MEM_MAPPING_EXTERNAL | MEM_MAPPING_ROM, dev);
    }
    return dev;
}

static void
vect486n_speed_changed(void *priv)
{
    vect486n_t *dev = priv;

    pit_set_pit_const(dev->pit, PITCONST);
}

static void
vect486n_close(void *priv)
{
    free(priv);
}

static const device_t vect486n_board_device = {
    .name          = "HP Vectra 486N system board",
    .internal_name = "vect486n_board",
    .init          = vect486n_init,
    .close         = vect486n_close,
    .reset         = vect486n_reset,
    .speed_changed = vect486n_speed_changed
};

/* VLSI 82C486 */
static const device_config_t vect486n_config[] = {
    // clang-format off
    {
        .name           = "bios",
        .description    = "BIOS Version",
        .type           = CONFIG_BIOS,
        .default_string = "t0405",
        .default_int    = 0,
        .bios           = {
            {
                .name          = "PhoenixBIOS (HP) - Revision T.04.02 (08/27/92) (Incomplete dump)",
                .internal_name = "t0402",
                .bios_type     = BIOS_NORMAL,
                .files_no      = 1,
                .local         = 0,
                .size          = 65536,
                .files         = { "roms/machines/vect486n/f000-64k.rom", "" }
            },
            {
                .name          = "PhoenixBIOS (HP) - Revision T.04.05 (10/11/94)",
                .internal_name = "t0405",
                .bios_type     = BIOS_NORMAL,
                .files_no      = 1,
                .local         = 0,
                .size          = 262144,
                .files         = { "roms/machines/vect486n/t0405-flash.rom", "" }
            },
            { .files_no = 0 }
        }
    },
    {
        .name        = "cpu_cache",
        .description = "Emulate CPU cache (Intel 486, experimental, slow)",
        .type        = CONFIG_BINARY,
        .default_int = 0
    },
    { .name = "", .description = "", .type = CONFIG_END }
    // clang-format on
};

const device_t vect486n_device = {
    .name          = "HP Vectra 486N",
    .internal_name = "vect486n",
    .flags         = 0,
    .local         = 0,
    .init          = NULL,
    .close         = NULL,
    .reset         = NULL,
    .available     = NULL,
    .speed_changed = NULL,
    .force_redraw  = NULL,
    .config        = vect486n_config
};

int
machine_at_vect486n_init(const machine_t *model)
{
    int         ret = 0;
    void       *chipset;
    const char *fn;

    if (!device_available(model->device))
        return ret;

    device_context(model->device);
    fn                  = device_get_bios_file(machine_get_device(machine), device_get_config_bios("bios"), 0);
    const uint32_t size = device_get_bios_file_size(model->device, device_get_config_bios("bios"));
    ret                 = bios_load_linear(fn, 0x00100000 - size, size, 0);
    device_context_restore();

    if (bios_only || !ret)
        return ret;

    machine_at_common_init(model);

    device_add(&vect486n_board_device);
    chipset              = device_add(&vl82c486_device);
    uint8_t *system_bios = rom + (biosmask + 1 - 0x10000);
    if (machine_get_config_int("cpu_cache") && vl82c486_cpu_cache_enable(chipset))
        pclog("HP Vectra 486N: functional CPU cache enabled; BIOS cache test unchanged\n");
    else {
        if (machine_get_config_int("cpu_cache"))
            pclog("HP Vectra 486N: functional cache unsupported for this CPU; using BIOS bypass\n");
        if (!vect486n_patch_cache_test(system_bios, 0x10000))
            fatal("HP Vectra 486N: cache-test bypass requires an original T.04.02 (08/27/92) or T.04.05 (10/11/94) BIOS\n");
        pclog("HP Vectra 486N: bypassed %.7s MPU cache test in the loaded ROM\n", system_bios + 0x93);
    }
    device_add(&vl82c113_device);
    device_add(&ide_isa_device);
    device_add_params(&pc873xx_device, (void *) (PCX73XX_IDE_PRI | PCX730X_26E));

    if (gfxcard[0] == VID_INTERNAL)
        device_add(machine_get_vid_device(machine));

    return ret;
}
