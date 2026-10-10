// SPDX-License-Identifier: GPL-2.0-or-later
/* HP Vectra 486N (D27xxA) system board. */
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

typedef struct vect486n_d27xx_t {
    uint8_t           control[4];
    pit_t            *pit;
    nmc93cxx_eeprom_t *eeprom;
    mem_mapping_t     setup_mapping;
} vect486n_d27xx_t;

static uint8_t
vect486n_d27xx_read(uint16_t port, void *priv)
{
    vect486n_d27xx_t *dev   = priv;
    uint8_t          value = dev->control[port & 3];

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
vect486n_d27xx_write(uint16_t port, uint8_t value, void *priv)
{
    vect486n_d27xx_t *dev = priv;

    const uint8_t changed = dev->control[port & 3] ^ value;

    dev->control[port & 3] = value;
    if ((port == 0x98) && (changed & 3))
        mem_mapping_set_exec(&dev->setup_mapping, rom + ((3 - (value & 3)) << 16));
    if (port == 0x9b)
        nmc93cxx_eeprom_write(dev->eeprom, !!(value & 0x80), !!(value & 0x04), !!(value & 0x10));
}

static uint8_t
vect486n_d27xx_setup_read(uint32_t addr, void *priv)
{
    const vect486n_d27xx_t *dev = priv;

    return rom[((3 - (dev->control[0] & 3)) << 16) | (addr & 0xffff)];
}

static void
vect486n_d27xx_reset(void *priv)
{
    vect486n_d27xx_t *dev = priv;

    memset(dev->control, 0, sizeof(dev->control));
    if (dev->setup_mapping.size)
        mem_mapping_set_exec(&dev->setup_mapping, rom + 0x30000);
    if (dev->pit) {
        pit_device_reset(dev->pit);
        for (int i = 0; i < 3; i++)
            pit_ctr_set_gate(dev->pit, i, 1);
    }
    nmc93cxx_eeprom_write(dev->eeprom, false, false, false);
}

static void *
vect486n_d27xx_init(UNUSED(const device_t *info))
{
    vect486n_d27xx_t        *dev    = calloc(1, sizeof(*dev));
    nmc93cxx_eeprom_params_t params = {
        .type            = NMC_93C66_x16_256,
        .filename        = "vect486n_d27xx_93c66.nvr",
        .default_content = NULL
    };

    dev->eeprom = device_add_params(&nmc93cxx_device, &params);
    dev->pit    = device_add(&i8254_ext_io_device);
    pit_handler(1, 0x009c, 4, dev->pit);
    vect486n_d27xx_reset(dev);
    io_sethandler(0x0098, 4, vect486n_d27xx_read, NULL, NULL, vect486n_d27xx_write, NULL, NULL, dev);

    /* Port 98h selects one of four 64 KB flash banks at FFFF0000h.
       Bank zero selects the system BIOS; banks 3, 2 and 1 select the
       preceding flash quarters. Low system BIOS shadowing is independent. */
    if (biosmask == 0x3ffff) {
        mem_mapping_add(&dev->setup_mapping, 0xffff0000, 0x10000,
                        vect486n_d27xx_setup_read, NULL, NULL, NULL, NULL, NULL,
                        rom + 0x30000, MEM_MAPPING_EXTERNAL | MEM_MAPPING_ROM | MEM_MAPPING_ROMCS, dev);
    }
    return dev;
}

static void
vect486n_d27xx_speed_changed(void *priv)
{
    vect486n_d27xx_t *dev = priv;

    pit_set_pit_const(dev->pit, PITCONST);
}

static void
vect486n_d27xx_close(void *priv)
{
    free(priv);
}

static const device_t vect486n_d27xx_board_device = {
    .name          = "HP Vectra 486N (D27xxA) system board",
    .internal_name = "vect486n_d27xx_board",
    .init          = vect486n_d27xx_init,
    .close         = vect486n_d27xx_close,
    .reset         = vect486n_d27xx_reset,
    .speed_changed = vect486n_d27xx_speed_changed
};

/* VLSI 82C486 */
static const device_config_t vect486n_d27xx_config[] = {
    // clang-format off
    {
        .name           = "bios",
        .description    = "BIOS Version",
        .type           = CONFIG_BIOS,
        .default_string = "v0409",
        .default_int    = 0,
        .bios           = {
            {
                .name          = "PhoenixBIOS (HP) - Revision V.04.09 (04/11/95)",
                .internal_name = "v0409",
                .bios_type     = BIOS_NORMAL,
                .files_no      = 1,
                .local         = 0,
                .size          = 262144,
                .files         = { "roms/machines/vect486n_d27xx/v0409-flash.rom", "" }
            },
            { .files_no = 0 }
        }
    },
    { .name = "", .description = "", .type = CONFIG_END }
    // clang-format on
};

const device_t vect486n_d27xx_device = {
    .name          = "HP Vectra 486N (D27xxA)",
    .internal_name = "vect486n_d27xx",
    .flags         = 0,
    .local         = 0,
    .init          = NULL,
    .close         = NULL,
    .reset         = NULL,
    .available     = NULL,
    .speed_changed = NULL,
    .force_redraw  = NULL,
    .config        = vect486n_d27xx_config
};

int
machine_at_vect486n_d27xx_init(const machine_t *model)
{
    int         ret = 0;
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

    device_add(&vect486n_d27xx_board_device);
    device_add(&vl82c486_device);
    device_add(&vl82c113_device);
    device_add(&ide_isa_device);
    device_add_params(&pc873xx_device, (void *) (PCX73XX_IDE_PRI | PCX730X_398));

    if (gfxcard[0] == VID_INTERNAL)
        device_add(machine_get_vid_device(machine));

    return ret;
}
