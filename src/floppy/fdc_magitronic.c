/*
 * 86Box	A hypervisor and IBM PC system emulator that specializes in
 *		running old operating systems and software designed for IBM
 *		PC systems and compatibles from 1981 through fairly recent
 *		system designs based on the PCI bus.
 *
 *		This file is part of the 86Box distribution.
 *
 *		Implementation of the Magitronic B215 XT-FDC Controller.
 *
 *      Authors: Tiseno100
 *
 *		Copyright 2021 Tiseno100
 *
 */

#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>
#define HAVE_STDARG_H
#include <86box/86box.h>
#include <86box/timer.h>
#include <86box/io.h>
#include <86box/device.h>
#include <86box/mem.h>
#include <86box/rom.h>
#include <86box/fdd.h>
#include <86box/fdc.h>
#include <86box/fdc_ext.h>

#define ROM_B215     "roms/floppy/magitronic/Magitronic B215 - BIOS ROM.bin"
#define ROM_ADDR     (uint32_t)(device_get_config_hex20("bios_addr") & 0x000fffff)

#define DRIVE_SELECT (int) (real_drive(dev->fdc_controller, i))
typedef struct
{
    fdc_t *fdc_controller;
    rom_t  rom;
} b215_t;

static uint8_t
b215_read(uint16_t addr, void *priv)
{
    b215_t *dev = (b215_t *) priv;

    /*
    Register 3F0h

    Bit (3-2) for Drive B:
    Bit (1-0) for Drive A:
    0: 360KB
    1: 1.2MB
    2: 720KB
    3: 1.44MB
    4:
*/
    int drive_spec[2];

    for (int i = 0; i <= 1; i++) {
        if (fdd_is_525(DRIVE_SELECT)) {
            if (!fdd_is_dd(DRIVE_SELECT))
                drive_spec[i] = 1;
            else if (fdd_doublestep_40(DRIVE_SELECT))
                drive_spec[i] = 2;
            else
                drive_spec[i] = 0;
        } else {
            if (fdd_is_dd(DRIVE_SELECT) && !fdd_is_double_sided(DRIVE_SELECT))
                drive_spec[i] = 0;
            else if (fdd_is_dd(DRIVE_SELECT) && fdd_is_double_sided(DRIVE_SELECT))
                drive_spec[i] = 2;
            else
                drive_spec[i] = 3;
        }
    }

    return ((drive_spec[1] << 2) | drive_spec[0]) & 0x0f;
}

static void
b215_close(void *priv)
{
    b215_t *dev = (b215_t *) priv;

    free(dev);
}

static void *
b215_init(const device_t *info)
{
    b215_t *dev = (b215_t *) malloc(sizeof(b215_t));
    memset(dev, 0, sizeof(b215_t));

    rom_init(&dev->rom, ROM_B215, ROM_ADDR, 0x2000, 0x1fff, 0, MEM_MAPPING_EXTERNAL);

    dev->fdc_controller = device_add(&fdc_um8398_device);
    io_sethandler(FDC_PRIMARY_ADDR, 1, b215_read, NULL, NULL, NULL, NULL, NULL, dev);

    return dev;
}

static int
b215_available(void)
{
    return rom_present(ROM_B215);
}

static const device_config_t b215_config[] = {
  // clang-format off
    {
        .name = "bios_addr",
        .description = "BIOS Address:",
        .type = CONFIG_HEX20,
        .default_string = "",
        .default_int = 0xca000,
        .file_filter = "",
        .spinner = { 0 },
        .selection = {
            { .description = "CA00H", .value = 0xca000 },
            { .description = "CC00H", .value = 0xcc000 },
            { .description = ""                        }
        }
    },
    { .name = "", .description = "", .type = CONFIG_END }
// clang-format on
};

const device_t fdc_b215_device = {
    .name          = "Magitronic B215",
    .internal_name = "b215",
    .flags         = DEVICE_ISA,
    .local         = 0,
    .init          = b215_init,
    .close         = b215_close,
    .reset         = NULL,
    { .available = b215_available },
    .speed_changed = NULL,
    .force_redraw  = NULL,
    .config        = b215_config
};
