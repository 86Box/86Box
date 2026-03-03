/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          Implementation of Socket 7 (Single Voltage) machines.
 *
 * Authors: Miran Grca, <mgrca8@gmail.com>
 *
 *          Copyright 2016-2025 Miran Grca.
 */
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>
#include <86box/86box.h>
#include <86box/mem.h>
#include <86box/io.h>
#include <86box/rom.h>
#include <86box/pci.h>
#include <86box/device.h>
#include <86box/chipset.h>
#include <86box/hdc.h>
#include <86box/hdc_ide.h>
#include <86box/keyboard.h>
#include <86box/flash.h>
#include <86box/sio.h>
#include <86box/hwm.h>
#include <86box/video.h>
#include <86box/spd.h>
#include "cpu.h"
#include <86box/machine.h>
#include <86box/timer.h>
#include <86box/fdd.h>
#include <86box/fdc.h>
#include <86box/nvr.h>
#include <86box/plat_unused.h>
#include <86box/sound.h>

/* i430FX */
static const device_config_t p54tp4xe_config[] = {
    // clang-format off
    {
        .name           = "bios",
        .description    = "BIOS Version",
        .type           = CONFIG_BIOS,
        .default_string = "p54tp4xe",
        .default_int    = 0,
        .file_filter    = NULL,
        .spinner        = { 0 },
        .selection      = { { 0 } },
        .bios           = {
            {
                .name          = "Award Modular BIOS v4.51PG - Revision 0302",
                .internal_name = "p54tp4xe",
                .bios_type     = BIOS_NORMAL,
                .files_no      = 1,
                .local         = 0,
                .size          = 131072,
                .files         = { "roms/machines/p54tp4xe/t15i0302.awd", "" }
            },
            {
                .name          = "MR BIOS V3.30",
                .internal_name = "p54tp4xe_mr",
                .bios_type     = BIOS_NORMAL,
                .files_no      = 1,
                .local         = 0,
                .size          = 131072,
                .files         = { "roms/machines/p54tp4xe/TRITON.BIO", "" }
            },
            { .files_no = 0 }
        },
    },
    { .name = "", .description = "", .type = CONFIG_END }
    // clang-format on
};

const device_t p54tp4xe_device = {
    .name          = "ASUS P/I-P55TP4XE",
    .internal_name = "p54tp4xe_device",
    .flags         = 0,
    .local         = 0,
    .init          = NULL,
    .close         = NULL,
    .reset         = NULL,
    .available     = NULL,
    .speed_changed = NULL,
    .force_redraw  = NULL,
    .config        = p54tp4xe_config
};

int
machine_at_p54tp4xe_init(const machine_t *model)
{
    int         ret = 0;
    const char *fn;

    /* No ROMs available */
    if (!device_available(model->device))
        return ret;

    device_context(model->device);
    fn  = device_get_bios_file(machine_get_device(machine), device_get_config_bios("bios"), 0);
    ret = bios_load_linear(fn, 0x000e0000, 131072, 0);
    device_context_restore();

    machine_at_common_init(model);

    pci_init(PCI_CONFIG_TYPE_1);
    pci_register_slot(0x00, PCI_CARD_NORTHBRIDGE, 0, 0, 0, 0);
    pci_register_slot(0x0C, PCI_CARD_NORMAL,      1, 2, 3, 4);
    pci_register_slot(0x0B, PCI_CARD_NORMAL,      2, 3, 4, 1);
    pci_register_slot(0x0A, PCI_CARD_NORMAL,      3, 4, 1, 2);
    pci_register_slot(0x09, PCI_CARD_NORMAL,      4, 1, 2, 3);
    pci_register_slot(0x07, PCI_CARD_SOUTHBRIDGE, 0, 0, 0, 0);

    device_add_params(machine_get_kbc_device(machine), (void *) model->kbc_params);
    device_add(&i430fx_device);
    device_add(&piix_device);
    device_add_params(&fdc37c6xx_device, (void *) FDC37C665);
    device_add(&intel_flash_bxt_device);

    return ret;
}

int
machine_at_exp8551_init(const machine_t *model)
{
    int ret;

    ret = bios_load_linear("roms/machines/exp8551/AMI20.BIO",
                           0x000e0000, 131072, 0);

    if (bios_only || !ret)
        return ret;

    machine_at_common_init(model);

    pci_init(PCI_CONFIG_TYPE_1);
    pci_register_slot(0x00, PCI_CARD_NORTHBRIDGE, 0, 0, 0, 0);
    pci_register_slot(0x13, PCI_CARD_NORMAL,      2, 3, 4, 1);
    pci_register_slot(0x14, PCI_CARD_NORMAL,      1, 2, 3, 4);
    pci_register_slot(0x12, PCI_CARD_NORMAL,      3, 4, 1, 2);
    pci_register_slot(0x11, PCI_CARD_NORMAL,      4, 1, 2, 3);
    pci_register_slot(0x07, PCI_CARD_SOUTHBRIDGE, 0, 0, 0, 0);

    device_add_params(machine_get_kbc_device(machine), (void *) model->kbc_params);
    device_add(&i430fx_device);
    device_add(&piix_device);
    device_add_params(&w837x7_device, (void *) (W83787F | W837X7_KEY_89));
    device_add(&sst_flash_29ee010_device);

    return ret;
}

static void
machine_at_holly_gpio_init(void)
{
    uint32_t gpio = 0xffffe2ff;

    /* Register 0x0079: */
    /* Bit 7: 0 = Clear password, 1 = Keep password. */
    /* Bit 6: 0 = NVRAM cleared by jumper, 1 = NVRAM normal. */
    /* Bit 5: 0 = CMOS Setup disabled, 1 = CMOS Setup enabled. */
    /* Bit 4: External CPU clock (Switch 8). */
    /* Bit 3: External CPU clock (Switch 7). */
    /*        50 MHz: Switch 7 = Off, Switch 8 = Off. */
    /*        60 MHz: Switch 7 = On, Switch 8 = Off. */
    /*        66 MHz: Switch 7 = Off, Switch 8 = On. */
    /* Bit 2: 0 = 2.5x multiplier, 1 = 1.5x/2x multiplier. */
    /* Bit 1: 0 = Soft Off capable power supply, 1 = Standard power supply. */
    /* Bit 0: 2x multiplier, 1 = 1.5x multiplier (Switch 6). */
    /* NOTE: A bit is read as 1 if switch is off, and as 0 if switch is on. */
    if (cpu_busspeed <= 50000000)
        gpio |= 0xffff00ff;
    else if ((cpu_busspeed > 50000000) && (cpu_busspeed <= 60000000))
        gpio |= 0xffff08ff;
    else if (cpu_busspeed > 60000000)
        gpio |= 0xffff10ff;

    if (cpu_dmulti <= 1.5)
        gpio |= 0xffff0500;
    else if ((cpu_dmulti > 1.5) && (cpu_dmulti <= 2.0))
        gpio |= 0xffff0400;
    else
        gpio |= 0xffff0000;

    machine_set_gpio_default(gpio);
}

int
machine_at_holly_init(const machine_t *model) /* HP Pavilion Holly, 7070/7090/5100/7100 */
{
    int ret;

    ret = bios_load_linear_combined("roms/machines/holly/1005CA2L.BIO",
                                    "roms/machines/holly/1005CA2L.BI1",
                                    0x20000, 128);

    if (bios_only || !ret)
        return ret;

    machine_at_common_init(model);
    machine_at_holly_gpio_init();

    pci_init(PCI_CONFIG_TYPE_1);
    pci_register_slot(0x00, PCI_CARD_NORTHBRIDGE, 0, 0, 0, 0);
    pci_register_slot(0x08, PCI_CARD_VIDEO,       4, 0, 0, 0);
    pci_register_slot(0x11, PCI_CARD_NORMAL,      1, 2, 3, 4);
    pci_register_slot(0x13, PCI_CARD_NORMAL,      2, 3, 4, 1);
    pci_register_slot(0x07, PCI_CARD_SOUTHBRIDGE, 0, 0, 0, 0);

    if (gfxcard[0] == VID_INTERNAL)
        device_add(machine_get_vid_device(machine));

    if (sound_card_current[0] == SOUND_INTERNAL)
        machine_snd = device_add(machine_get_snd_device(machine));

    device_add(&i430fx_device);
    device_add(&piix_device);
    device_add_params(&pc87306_device, (void *) PCX730X_AMI);
    device_add(&intel_flash_bxt_ami_device);

    return ret;
}

int
machine_at_vectra500mt_init(const machine_t *model)
{
    int ret;

    ret = bios_load_linear("roms/machines/vectra500mt/GJ0718.FUL",
                           0x000e0000, 131072, 0);

    if (bios_only || !ret)
        return ret;

    machine_at_common_init(model);

    pci_init(PCI_CONFIG_TYPE_1);
    pci_register_slot(0x00, PCI_CARD_NORTHBRIDGE, 0, 0, 0, 0);
    pci_register_slot(0x0F, PCI_CARD_SOUTHBRIDGE, 0, 0, 0, 0);
    pci_register_slot(0x0D, PCI_CARD_VIDEO,       0, 0, 0, 0);
    pci_register_slot(0x06, PCI_CARD_NORMAL,      1, 2, 3, 4);
    pci_register_slot(0x07, PCI_CARD_NORMAL,      2, 3, 4, 1);
    pci_register_slot(0x08, PCI_CARD_NORMAL,      3, 4, 1, 2);

    if (gfxcard[0] == VID_INTERNAL)
        device_add(machine_get_vid_device(machine));

    device_add(&i430fx_device);
    device_add(&piix_device);
    device_add_params(&fdc37c93x_device, (void *) (FDC37XXX2 | FDC37C93X_NORMAL));
    device_add(&sst_flash_29ee010_device);

    return ret;
}

static void
machine_at_vectra52_gpio_init(void)
{
    uint32_t gpio = 0x40;

    if (cpu_busspeed <= 40000000)
        gpio |= 0x30;
    else if ((cpu_busspeed > 40000000) && (cpu_busspeed <= 50000000))
        gpio |= 0x00;
    else if ((cpu_busspeed > 50000000) && (cpu_busspeed <= 60000000))
        gpio |= 0x20;
    else if (cpu_busspeed > 60000000)
        gpio |= 0x10;

    if (cpu_dmulti <= 1.5)
        gpio |= 0x82;
    else if ((cpu_dmulti > 1.5) && (cpu_dmulti <= 2.0))
        gpio |= 0x02;
    else if ((cpu_dmulti > 2.0) && (cpu_dmulti <= 2.5))
        gpio |= 0x00;
    else if (cpu_dmulti > 2.5)
        gpio |= 0x80;

    machine_set_gpio_default(gpio);
}

static const device_config_t vectra52_config[] = {
    // clang-format off
    {
        .name           = "bios",
        .description    = "BIOS Version",
        .type           = CONFIG_BIOS,
        .default_string = "vectra52_0705",
        .default_int    = 0,
        .file_filter    = NULL,
        .spinner        = { 0 },
        .selection      = { { 0 } },
        .bios           = {
            {
                .name          = "GU.07.02 (01/25/96)",
                .internal_name = "vectra52_0702",
                .bios_type     = BIOS_NORMAL,
                .files_no      = 1,
                .local         = 0,
                .size          = 131072,
                .files         = { "roms/machines/vectra52/d3653.bin", "" }
            },
            {
                .name          = "GU.07.05 (08/06/96)",
                .internal_name = "vectra52_0705",
                .bios_type     = BIOS_NORMAL,
                .files_no      = 1,
                .local         = 0,
                .size          = 131072,
                .files         = { "roms/machines/vectra52/GU0705US.FUL", "" }
            },
            { .files_no = 0 }
        }
    },
    { .name = "", .description = "", .type = CONFIG_END }
    // clang-format on
};

const device_t vectra52_device = {
    .name          = "HP Vectra VE 5/xxx Series 2",
    .internal_name = "vectra52",
    .flags         = 0,
    .local         = 0,
    .init          = NULL,
    .close         = NULL,
    .reset         = NULL,
    .available     = NULL,
    .speed_changed = NULL,
    .force_redraw  = NULL,
    .config        = vectra52_config
};

int
machine_at_vectra52_init(const machine_t *model)
{
    int         ret = 0;
    const char *fn;

    /* No ROMs available */
    if (!device_available(model->device))
        return ret;

    device_context(model->device);
    fn  = device_get_bios_file(machine_get_device(machine), device_get_config_bios("bios"), 0);
    ret = bios_load_linear(fn, 0x000e0000, 131072, 0);
    device_context_restore();

    machine_at_common_init(model);
    machine_at_vectra52_gpio_init();

    pci_init(PCI_CONFIG_TYPE_1);
    pci_register_slot(0x00, PCI_CARD_NORTHBRIDGE, 0, 0, 0, 0);
    pci_register_slot(0x0F, PCI_CARD_SOUTHBRIDGE, 0, 0, 0, 0);
    pci_register_slot(0x0D, PCI_CARD_VIDEO,       0, 0, 0, 0);
    pci_register_slot(0x06, PCI_CARD_NORMAL,      1, 2, 3, 4);
    pci_register_slot(0x07, PCI_CARD_NORMAL,      2, 3, 4, 1);
    pci_register_slot(0x08, PCI_CARD_NORMAL,      3, 4, 1, 2);
    device_add(&i430fx_device);
    device_add(&piix_device);
    device_add_params(&pc87306_device, (void *) PCX730X_PHOENIX_42);
    device_add(&intel_flash_bxt_device);
	
    if (gfxcard[0] == VID_INTERNAL)
        device_add(machine_get_vid_device(machine));

    return ret;
}

int
machine_at_vectra54_init(const machine_t *model)
{
    int ret;

    ret = bios_load_linear("roms/machines/vectra54/GT0724.22",
                           0x000e0000, 131072, 0);

    if (bios_only || !ret)
        return ret;

    machine_at_common_init(model);

    pci_init(PCI_CONFIG_TYPE_1);
    pci_register_slot(0x00, PCI_CARD_NORTHBRIDGE, 0, 0, 0, 0);
    pci_register_slot(0x0F, PCI_CARD_SOUTHBRIDGE, 0, 0, 0, 0);
    pci_register_slot(0x0D, PCI_CARD_VIDEO,       0, 0, 0, 0);
    pci_register_slot(0x06, PCI_CARD_NORMAL,      1, 2, 3, 4);
    pci_register_slot(0x07, PCI_CARD_NORMAL,      2, 3, 4, 1);
    pci_register_slot(0x08, PCI_CARD_NORMAL,      3, 4, 1, 2);

    if (gfxcard[0] == VID_INTERNAL)
        device_add(&s3_phoenix_trio64_onboard_pci_device);

    device_add(&i430fx_device);
    device_add(&piix_device);
    device_add_params(&fdc37c93x_device, (void *) (FDC37XXX2 | FDC37C93X_NORMAL));
    device_add(&sst_flash_29ee010_device);

    return ret;
}

static void
machine_at_atlantis_gpio_init(void)
{
    uint32_t gpio = 0xffffe0cf;

    /* Register 0x0079: */
    /* Bit 7: 0 = Clear password, 1 = Keep password. */
    /* Bit 6: 0 = NVRAM cleared by jumper, 1 = NVRAM normal. */
    /* Bit 5: 0 = CMOS Setup disabled, 1 = CMOS Setup enabled. */
    /* Bit 4: External CPU clock (Switch 8). */
    /* Bit 3: External CPU clock (Switch 7). */
    /*        50 MHz: Switch 7 = Off, Switch 8 = Off. */
    /*        60 MHz: Switch 7 = On, Switch 8 = Off. */
    /*        66 MHz: Switch 7 = Off, Switch 8 = On. */
    /* Bit 2: 0 = On-board audio absent, 1 = On-board audio present. */
    /* Bit 1: 0 = Soft-off capable power supply present, 1 = Soft-off capable power supply absent. */
    /* Bit 0: 0 = 2x multiplier, 1 = 1.5x multiplier (Switch 6). */
    /* NOTE: A bit is read as 1 if switch is off, and as 0 if switch is on. */
    if (cpu_busspeed <= 50000000)
        gpio |= 0xffff0000;
    else if ((cpu_busspeed > 50000000) && (cpu_busspeed <= 60000000))
        gpio |= 0xffff0800;
    else if (cpu_busspeed > 60000000)
        gpio |= 0xffff1000;

    if (sound_card_current[0] == SOUND_INTERNAL)
        gpio |= 0xffff0400;

    if (cpu_dmulti <= 1.5)
        gpio |= 0xffff0100;
    else
        gpio |= 0xffff0000;

    machine_set_gpio_default(gpio);
}

int
machine_at_atlantis_init(const machine_t *model)
{
    int ret;

    ret = bios_load_linear_combined("roms/machines/atlantis/1007CL0_.BIO",
                                    "roms/machines/atlantis/1007CL0_.BI1",
                                    0x20000, 128);

    if (bios_only || !ret)
        return ret;

    machine_at_common_init(model);
    machine_at_atlantis_gpio_init();

    pci_init(PCI_CONFIG_TYPE_1);
    pci_register_slot(0x00, PCI_CARD_NORTHBRIDGE, 0, 0, 0, 0);
    pci_register_slot(0x07, PCI_CARD_SOUTHBRIDGE, 0, 0, 0, 0);
    pci_register_slot(0x08, PCI_CARD_VIDEO,       4, 0, 0, 0);
    pci_register_slot(0x0D, PCI_CARD_NORMAL,      1, 2, 3, 4);
    pci_register_slot(0x0E, PCI_CARD_NORMAL,      2, 3, 4, 1);
    pci_register_slot(0x0F, PCI_CARD_NORMAL,      3, 4, 1, 2);
    pci_register_slot(0x10, PCI_CARD_NORMAL,      4, 1, 2, 3);

    if (gfxcard[0] == VID_INTERNAL)
        device_add(machine_get_vid_device(machine));

    if (sound_card_current[0] == SOUND_INTERNAL)
        machine_snd = device_add(machine_get_snd_device(machine));

    device_add(&i430fx_device);
    device_add(&piix_device);
    device_add_params(&pc87306_device, (void *) PCX730X_AMI);
    device_add(&intel_flash_bxt_ami_device);

    return ret;
}

static const device_config_t thor_config[] = {
    // clang-format off
    {
        .name           = "bios",
        .description    = "BIOS Version",
        .type           = CONFIG_BIOS,
        .default_string = "thor",
        .default_int    = 0,
        .file_filter    = "",
        .spinner        = { 0 },
        .selection      = { { 0 } },
        .bios           = {
            {
                .name          = "Intel AMIBIOS - Revision 1.00.03.CN0T (Gateway 2000)",
                .internal_name = "gw2katx",
                .bios_type     = BIOS_NORMAL,
                .files_no      = 2,
                .local         = 0,
                .size          = 131072,
                .files         = { "roms/machines/thor/1003CN0T.BIO", "roms/machines/thor/1003CN0T.BI1", "" }
            },
            {
                .name          = "Intel AMIBIOS - Revision 1.00.06.CN0",
                .internal_name = "thor",
                .bios_type     = BIOS_NORMAL,
                .files_no      = 2,
                .local         = 0,
                .size          = 131072,
                .files         = { "roms/machines/thor/1006cn0_.bio", "roms/machines/thor/1006cn0_.bi1", "" }
            },
            {
                .name          = "MR BIOS V3.28",
                .internal_name = "mrthor",
                .bios_type     = BIOS_NORMAL,
                .files_no      = 1,
                .local         = 0,
                .size          = 131072,
                .files         = { "roms/machines/thor/mr_atx.bio", "" }
            },
            { .files_no = 0 }
        }
    },
    { .name = "", .description = "", .type = CONFIG_END }
    // clang-format on
};

const device_t thor_device = {
    .name          = "Intel Advanced/ATX (Thor)",
    .internal_name = "thor_device",
    .flags         = 0,
    .local         = 0,
    .init          = NULL,
    .close         = NULL,
    .reset         = NULL,
    .available     = NULL,
    .speed_changed = NULL,
    .force_redraw  = NULL,
    .config        = thor_config
};

static void
machine_at_thor_gpio_init(void)
{
    uint32_t gpio = 0xffffe1cf;

    /* Register 0x0078 (Undocumented): */
    /* Bit 5: 0 = Multiplier. */
    /* Bit 4: 0 = Multiplier. */
    /*        1.5: 0, 0. */
    /*        3.0: 0, 1. */
    /*        2.0: 1, 0. */
    /*        2.5: 1, 1. */
    /* Bit 1: 0 = Error beep, 1 = No error. */
    if (cpu_dmulti <= 1.5)
        gpio |= 0xffff0000;
    else if ((cpu_dmulti > 1.5) && (cpu_dmulti <= 2.0))
        gpio |= 0xffff0020;
    else if ((cpu_dmulti > 2.0) && (cpu_dmulti <= 2.5))
        gpio |= 0xffff0030;
    else if (cpu_dmulti > 2.5)
        gpio |= 0xffff0010;

    /* Register 0x0079: */
    /* Bit 7: 0 = Clear password, 1 = Keep password. */
    /* Bit 6: 0 = NVRAM cleared by jumper, 1 = NVRAM normal. */
    /* Bit 5: 0 = CMOS Setup disabled, 1 = CMOS Setup enabled. */
    /* Bit 4: External CPU clock (Switch 8). */
    /* Bit 3: External CPU clock (Switch 7). */
    /*        50 MHz: Switch 7 = Off, Switch 8 = Off. */
    /*        60 MHz: Switch 7 = On, Switch 8 = Off. */
    /*        66 MHz: Switch 7 = Off, Switch 8 = On. */
    /* Bit 2: 0 = On-board audio absent, 1 = On-board audio present. */
    /* Bit 1: 0 = Soft-off capable power supply present, 1 = Soft-off capable power supply absent. */
    /* Bit 0: 0 = Reserved. */
    /* NOTE: A bit is read as 1 if switch is off, and as 0 if switch is on. */
    if (cpu_busspeed <= 50000000)
        gpio |= 0xffff0000;
    else if ((cpu_busspeed > 50000000) && (cpu_busspeed <= 60000000))
        gpio |= 0xffff0800;
    else if (cpu_busspeed > 60000000)
        gpio |= 0xffff1000;

    if (sound_card_current[0] == SOUND_INTERNAL)
        gpio |= 0xffff0400;

    machine_set_gpio_default(gpio);
}

int
machine_at_thor_init(const machine_t *model)
{
    int         ret = 0;
    const char *fn;
    const char *fn2;

    /* No ROMs available */
    if (!device_available(model->device))
        return ret;

    device_context(model->device);
    int is_mr     = !strcmp(device_get_config_bios("bios"), "mrthor");
    int has_video = !strcmp(device_get_config_bios("bios"), "thor");
    fn            = device_get_bios_file(machine_get_device(machine), device_get_config_bios("bios"), 0);
    if (is_mr)
        ret = bios_load_linear(fn, 0x000e0000, 131072, 0);
    else {
        fn2 = device_get_bios_file(machine_get_device(machine), device_get_config_bios("bios"), 1);
        ret = bios_load_linear_combined(fn, fn2, 0x20000, 128);
    }
    device_context_restore();

    machine_at_common_init(model);
    machine_at_thor_gpio_init();

    pci_init(PCI_CONFIG_TYPE_1);
    pci_register_slot(0x00, PCI_CARD_NORTHBRIDGE, 0, 0, 0, 0);
    pci_register_slot(0x08, PCI_CARD_VIDEO,       4, 0, 0, 0);
    pci_register_slot(0x0D, PCI_CARD_NORMAL,      1, 2, 3, 4);
    pci_register_slot(0x0E, PCI_CARD_NORMAL,      2, 3, 4, 1);
    pci_register_slot(0x0F, PCI_CARD_NORMAL,      3, 4, 2, 1);
    pci_register_slot(0x10, PCI_CARD_NORMAL,      4, 3, 2, 1);
    pci_register_slot(0x07, PCI_CARD_SOUTHBRIDGE, 0, 0, 0, 0);

    if (has_video && (gfxcard[0] == VID_INTERNAL))
        device_add(machine_get_vid_device(machine));

    if (has_video && (sound_card_current[0] == SOUND_INTERNAL))
        machine_snd = device_add(machine_get_snd_device(machine));

    device_add(&i430fx_device);
    device_add(&piix_device);
    device_add_params(&pc87306_device, (void *) PCX730X_AMI);
    device_add(&intel_flash_bxt_ami_device);

    return ret;
}

static void
machine_at_endeavor_gpio_init(void)
{
    uint32_t gpio = 0xffffe0cf;
    uint16_t addr;

    /* Register 0x0078 (Undocumented): */
    /* Bit 5,4: Vibra 16S base address: 0 = 220h, 1 = 260h, 2 = 240h, 3 = 280h. */
    device_context(machine_get_snd_device(machine));
    addr = device_get_config_hex16("base");
    switch (addr) {
        case 0x0220:
            gpio |= 0xffff00cf;
            break;
        case 0x0240:
            gpio |= 0xffff00ef;
            break;
        case 0x0260:
            gpio |= 0xffff00df;
            break;
        case 0x0280:
            gpio |= 0xffff00ff;
            break;
    }
    device_context_restore();

    /* Register 0x0079: */
    /* Bit 7: 0 = Clear password, 1 = Keep password. */
    /* Bit 6: 0 = NVRAM cleared by jumper, 1 = NVRAM normal. */
    /* Bit 5: 0 = CMOS Setup disabled, 1 = CMOS Setup enabled. */
    /* Bit 4: External CPU clock (Switch 8). */
    /* Bit 3: External CPU clock (Switch 7). */
    /*        50 MHz: Switch 7 = Off, Switch 8 = Off. */
    /*        60 MHz: Switch 7 = On, Switch 8 = Off. */
    /*        66 MHz: Switch 7 = Off, Switch 8 = On. */
    /* Bit 2: 0 = On-board audio absent, 1 = On-board audio present. */
    /* Bit 1: 0 = Soft-off capable power supply present, 1 = Soft-off capable power supply absent. */
    /* Bit 0: 0 = 2x multiplier, 1 = 1.5x multiplier (Switch 6). */
    /* NOTE: A bit is read as 1 if switch is off, and as 0 if switch is on. */
    if (cpu_busspeed <= 50000000)
        gpio |= 0xffff0000;
    else if ((cpu_busspeed > 50000000) && (cpu_busspeed <= 60000000))
        gpio |= 0xffff0800;
    else if (cpu_busspeed > 60000000)
        gpio |= 0xffff1000;

    if (sound_card_current[0] == SOUND_INTERNAL)
        gpio |= 0xffff0400;

    if (cpu_dmulti <= 1.5)
        gpio |= 0xffff0100;
    else
        gpio |= 0xffff0000;

    machine_set_gpio_default(gpio);
}

uint32_t
machine_at_endeavor_gpio_handler(uint8_t write, uint32_t val)
{
    uint32_t ret = machine_get_gpio_default();

    if (write) {
        ret &= ((val & 0xffffffcf) | 0xffff0000);
        ret |= (val & 0x00000030);
        if (machine_snd != NULL)
            switch ((val >> 4) & 0x03) {
                case 0x00:
                    sb_vibra16s_onboard_relocate_base(0x0220, machine_snd);
                    break;
                case 0x01:
                    sb_vibra16s_onboard_relocate_base(0x0260, machine_snd);
                    break;
                case 0x02:
                    sb_vibra16s_onboard_relocate_base(0x0240, machine_snd);
                    break;
                case 0x03:
                    sb_vibra16s_onboard_relocate_base(0x0280, machine_snd);
                    break;
            }
        machine_set_gpio(ret);
    } else
        ret = machine_get_gpio();

    return ret;
}

int
machine_at_endeavor_init(const machine_t *model)
{
    int ret;

    ret = bios_load_linear_combined("roms/machines/endeavor/1006cb0_.bio",
                                    "roms/machines/endeavor/1006cb0_.bi1",
                                    0x1d000, 128);

    if (bios_only || !ret)
        return ret;

    machine_at_common_init(model);
    machine_at_endeavor_gpio_init();

    pci_init(PCI_CONFIG_TYPE_1);
    pci_register_slot(0x00, PCI_CARD_NORTHBRIDGE, 0, 0, 0, 0);
    pci_register_slot(0x08, PCI_CARD_VIDEO,       4, 0, 0, 0);
    pci_register_slot(0x0D, PCI_CARD_NORMAL,      1, 2, 3, 4);
    pci_register_slot(0x0E, PCI_CARD_NORMAL,      2, 3, 4, 1);
    pci_register_slot(0x0F, PCI_CARD_NORMAL,      3, 4, 1, 2);
    pci_register_slot(0x10, PCI_CARD_NORMAL,      4, 1, 2, 3);
    pci_register_slot(0x07, PCI_CARD_SOUTHBRIDGE, 0, 0, 0, 0);

    if (gfxcard[0] == VID_INTERNAL)
        device_add(machine_get_vid_device(machine));

    if (sound_card_current[0] == SOUND_INTERNAL)
        machine_snd = device_add(machine_get_snd_device(machine));

    device_add(&i430fx_device);
    device_add(&piix_device);
    device_add_params(&pc87306_device, (void *) PCX730X_AMI);
    device_add(&intel_flash_bxt_ami_device);

    return ret;
}

/* The Monaco and Atlantis share the same GPIO config */
#define machine_at_monaco_gpio_init machine_at_atlantis_gpio_init

static const device_config_t monaco_config[] = {
    // clang-format off
    {
        .name           = "bios",
        .description    = "BIOS Version",
        .type           = CONFIG_BIOS,
        .default_string = "monaco",
        .default_int    = 0,
        .file_filter    = NULL,
        .spinner        = { 0 },
        .selection      = { { 0 } },
        .bios           = {
            {
                .name          = "Intel AMIBIOS - Revision 1.00.07.BU0",
                .internal_name = "monaco",
                .bios_type     = BIOS_NORMAL,
                .files_no      = 2,
                .local         = 0,
                .size          = 131072,
                .files         = { "roms/machines/monaco/1007BU0_.BIO", "roms/machines/monaco/1007BU0_.BI1", "" }
            },
            {
                .name          = "Intel AMIBIOS - Revision 1.00.12.BU0Q (AST Bravo MS-T)",
                .internal_name = "bravomst",
                .bios_type     = BIOS_NORMAL,
                .files_no      = 2,
                .local         = 0,
                .size          = 131072,
                .files         = { "roms/machines/monaco/1012BU0Q.BIO", "roms/machines/monaco/1012BU0Q.BI1", "" }
            },
            { .files_no = 0 }
        }
    },
    { .name = "", .description = "", .type = CONFIG_END }
    // clang-format on
};

const device_t monaco_device = {
    .name          = "Intel Advanced/MA (Monaco)",
    .internal_name = "monaco_device",
    .flags         = 0,
    .local         = 0,
    .init          = NULL,
    .close         = NULL,
    .reset         = NULL,
    .available     = NULL,
    .speed_changed = NULL,
    .force_redraw  = NULL,
    .config        = monaco_config
};

uint32_t
machine_at_monaco_gpio_handler(uint8_t write, uint32_t val)
{
    uint32_t ret = machine_get_gpio_default();

    if (write) {
        ret &= ((val & 0xffffffcf) | 0xffff0000);
        ret |= (val & 0x00000030);

        machine_set_gpio(ret);
    } else
        ret = machine_get_gpio();

    return ret;
}

int
machine_at_monaco_init(const machine_t *model)
{
    int         ret = 0;
    const char *fn;
    const char *fn2;

    /* No ROMs available */
    if (!device_available(model->device))
        return ret;

    device_context(model->device);
    fn  = device_get_bios_file(machine_get_device(machine), device_get_config_bios("bios"), 0);
    fn2 = device_get_bios_file(machine_get_device(machine), device_get_config_bios("bios"), 1);
    ret = bios_load_linear_combined(fn, fn2, 0x20000, 128);
    device_context_restore();

    machine_at_common_init(model);
    machine_at_monaco_gpio_init();

    pci_init(PCI_CONFIG_TYPE_1);
    pci_register_slot(0x00, PCI_CARD_NORTHBRIDGE, 0, 0, 0, 0);
    pci_register_slot(0x07, PCI_CARD_SOUTHBRIDGE, 0, 0, 0, 0);
    pci_register_slot(0x0B, PCI_CARD_VIDEO,       1, 2, 3, 4);
    pci_register_slot(0x11, PCI_CARD_NORMAL,      1, 3, 2, 4);
    pci_register_slot(0x13, PCI_CARD_NORMAL,      2, 1, 3, 4);

    if (gfxcard[0] == VID_INTERNAL)
        device_add(machine_get_vid_device(machine));

    if (sound_card_current[0] == SOUND_INTERNAL)
        machine_snd = device_add(machine_get_snd_device(machine));

    device_add(&i430fx_device);
    device_add(&piix_device);
    device_add_params(&pc87306_device, (void *) PCX730X_AMI);
    device_add(&intel_flash_bxt_ami_device);

    return ret;
}

static const device_config_t ms5119_config[] = {
    // clang-format off
    {
        .name           = "bios",
        .description    = "BIOS Version",
        .type           = CONFIG_BIOS,
        .default_string = "ms5119",
        .default_int    = 0,
        .file_filter    = NULL,
        .spinner        = { 0 },
        .selection      = { { 0 } },
        .bios           = {
            {
                .name          = "AMIBIOS 6 (071595) - Revision A37EB",
                .internal_name = "ms5119",
                .bios_type     = BIOS_NORMAL,
                .files_no      = 1,
                .local         = 0,
                .size          = 131072,
                .files         = { "roms/machines/ms5119/A37EB.ROM", "" }
            },
            {
                .name          = "Award Modular BIOS v4.51PG - Release 2.3 (by Rainbow)",
                .internal_name = "ms5119_451pg",
                .bios_type     = BIOS_NORMAL,
                .files_no      = 1,
                .local         = 0,
                .size          = 131072,
                .files         = { "roms/machines/ms5119/MS-5120.BIN", "" }
            },
            { .files_no = 0 }
        }
    },
    { .name = "", .description = "", .type = CONFIG_END }
    // clang-format on
};

const device_t ms5119_device = {
    .name          = "MSI MS-5119",
    .internal_name = "ms5119_device",
    .flags         = 0,
    .local         = 0,
    .init          = NULL,
    .close         = NULL,
    .reset         = NULL,
    .available     = NULL,
    .speed_changed = NULL,
    .force_redraw  = NULL,
    .config        = ms5119_config
};

int
machine_at_ms5119_init(const machine_t *model)
{
    int         ret = 0;
    const char *fn;

    /* No ROMs available */
    if (!device_available(model->device))
        return ret;

    device_context(model->device);
    fn  = device_get_bios_file(machine_get_device(machine), device_get_config_bios("bios"), 0);
    ret = bios_load_linear(fn, 0x000e0000, 131072, 0);
    device_context_restore();

    machine_at_common_init(model);

    pci_init(PCI_CONFIG_TYPE_1);
    pci_register_slot(0x00, PCI_CARD_NORTHBRIDGE, 0, 0, 0, 0);
    pci_register_slot(0x07, PCI_CARD_SOUTHBRIDGE, 0, 0, 0, 0);
    pci_register_slot(0x0d, PCI_CARD_NORMAL,      1, 2, 3, 4);
    pci_register_slot(0x0e, PCI_CARD_NORMAL,      2, 3, 4, 1);
    pci_register_slot(0x0f, PCI_CARD_NORMAL,      3, 4, 1, 2);

    device_add(&i430fx_device);
    device_add(&piix_device);
    device_add_params(machine_get_kbc_device(machine), (void *) model->kbc_params);
    device_add_params(&w837x7_device, (void *) (W83787F | W837X7_KEY_89));
    device_add(&sst_flash_29ee010_device);

    return ret;
}

static void
machine_at_pb640_gpio_init(void)
{
    uint32_t gpio = 0xffffe6ff;

    /* Register 0x0079: */
    /* Bit 7: 0 = Clear password, 1 = Keep password. */
    /* Bit 6: 0 = NVRAM cleared by jumper, 1 = NVRAM normal. */
    /* Bit 5: 0 = CMOS Setup disabled, 1 = CMOS Setup enabled. */
    /* Bit 4: External CPU clock (Switch 8). */
    /* Bit 3: External CPU clock (Switch 7). */
    /*        50 MHz: Switch 7 = Off, Switch 8 = Off. */
    /*        60 MHz: Switch 7 = On, Switch 8 = Off. */
    /*        66 MHz: Switch 7 = Off, Switch 8 = On. */
    /* Bit 2: No Connect. */
    /* Bit 1: No Connect. */
    /* Bit 0: 2x multiplier, 1 = 1.5x multiplier (Switch 6). */
    /* NOTE: A bit is read as 1 if switch is off, and as 0 if switch is on. */
    if (cpu_busspeed <= 50000000)
        gpio |= 0xffff00ff;
    else if ((cpu_busspeed > 50000000) && (cpu_busspeed <= 60000000))
        gpio |= 0xffff08ff;
    else if (cpu_busspeed > 60000000)
        gpio |= 0xffff10ff;

    if (cpu_dmulti <= 1.5)
        gpio |= 0xffff01ff;
    else
        gpio |= 0xffff00ff;

    machine_set_gpio_default(gpio);
}

int
machine_at_pb640_init(const machine_t *model)
{
    int ret;

    ret = bios_load_linear_combined("roms/machines/pb640/1007CP0R.BIO",
                                    "roms/machines/pb640/1007CP0R.BI1", 0x1d000, 128);

    if (bios_only || !ret)
        return ret;

    machine_at_common_init(model);
    machine_at_pb640_gpio_init();

    pci_init(PCI_CONFIG_TYPE_1);
    pci_register_slot(0x00, PCI_CARD_NORTHBRIDGE, 0, 0, 0, 0);
    pci_register_slot(0x08, PCI_CARD_VIDEO,       4, 0, 0, 0);
    pci_register_slot(0x11, PCI_CARD_NORMAL,      1, 2, 3, 4);
    pci_register_slot(0x13, PCI_CARD_NORMAL,      2, 1, 3, 4);
    pci_register_slot(0x0B, PCI_CARD_NORMAL,      3, 2, 1, 4);
    pci_register_slot(0x07, PCI_CARD_SOUTHBRIDGE, 0, 0, 0, 0);

    device_add(&i430fx_rev02_device);
    device_add(&piix_rev02_device);

    if (gfxcard[0] == VID_INTERNAL)
        device_add(machine_get_vid_device(machine));

    device_add_params(&pc87306_device, (void *) PCX730X_AMI);
    device_add(&intel_flash_bxt_ami_device);

    return ret;
}

int
machine_at_mb500n_init(const machine_t *model)
{
    int ret;

    ret = bios_load_linear("roms/machines/mb500n/031396s.bin",
                           0x000e0000, 131072, 0);

    if (bios_only || !ret)
        return ret;

    machine_at_common_init(model);

    pci_init(PCI_CONFIG_TYPE_1);
    pci_register_slot(0x00, PCI_CARD_NORTHBRIDGE, 0, 0, 0, 0);
    pci_register_slot(0x14, PCI_CARD_NORMAL,      1, 2, 3, 4);
    pci_register_slot(0x13, PCI_CARD_NORMAL,      2, 3, 4, 1);
    pci_register_slot(0x12, PCI_CARD_NORMAL,      3, 4, 1, 2);
    pci_register_slot(0x11, PCI_CARD_NORMAL,      4, 1, 2, 3);
    pci_register_slot(0x07, PCI_CARD_SOUTHBRIDGE, 0, 0, 0, 0);

    device_add_params(machine_get_kbc_device(machine), (void *) model->kbc_params);
    device_add(&i430fx_device);
    device_add(&piix_no_mirq_device);
    device_add_params(&fdc37c6xx_device, (void *) FDC37C665);
    device_add(&intel_flash_bxt_device);

    return ret;
}

static const device_config_t fmb_config[] = {
    // clang-format off
    {
        .name           = "bios",
        .description    = "BIOS Version",
        .type           = CONFIG_BIOS,
        .default_string = "fmb",
        .default_int    = 0,
        .file_filter    = NULL,
        .spinner        = { 0 },
        .selection      = { { 0 } },
        .bios           = {
            {
                .name          = "AMIBIOS 6 (071595) - Revision 1.83",
                .internal_name = "fmb",
                .bios_type     = BIOS_NORMAL,
                .files_no      = 1,
                .local         = 0,
                .size          = 131072,
                .files         = { "roms/machines/fmb/P5IV183.ROM", "" }
            },
            {
                .name          = "Award Modular BIOS v4.51PG - 2001 Release (by Rainbow)",
                .internal_name = "fmb_451pg",
                .bios_type     = BIOS_NORMAL,
                .files_no      = 1,
                .local         = 0,
                .size          = 131072,
                .files         = { "roms/machines/fmb/P5I437FM.BIN", "" }
            },
            { .files_no = 0 }
        }
    },
    { .name = "", .description = "", .type = CONFIG_END }
    // clang-format on
};

const device_t fmb_device = {
    .name          = "QDI FMB",
    .internal_name = "fmb_device",
    .flags         = 0,
    .local         = 0,
    .init          = NULL,
    .close         = NULL,
    .reset         = NULL,
    .available     = NULL,
    .speed_changed = NULL,
    .force_redraw  = NULL,
    .config        = fmb_config
};

int
machine_at_fmb_init(const machine_t *model)
{
    int         ret = 0;
    const char *fn;

    /* No ROMs available */
    if (!device_available(model->device))
        return ret;

    device_context(model->device);
    fn  = device_get_bios_file(machine_get_device(machine), device_get_config_bios("bios"), 0);
    ret = bios_load_linear(fn, 0x000e0000, 131072, 0);
    device_context_restore();

    machine_at_common_init(model);

    pci_init(PCI_CONFIG_TYPE_1);
    pci_register_slot(0x00, PCI_CARD_NORTHBRIDGE, 0, 0, 0, 0);
    pci_register_slot(0x07, PCI_CARD_SOUTHBRIDGE, 0, 0, 0, 0);
    pci_register_slot(0x14, PCI_CARD_NORMAL,      1, 2, 3, 4);
    pci_register_slot(0x13, PCI_CARD_NORMAL,      2, 3, 4, 1);
    pci_register_slot(0x12, PCI_CARD_NORMAL,      3, 4, 2, 1);
    pci_register_slot(0x11, PCI_CARD_NORMAL,      4, 3, 2, 1);

    device_add(&i430fx_device);
    device_add(&piix_no_mirq_device);
    device_add_params(machine_get_kbc_device(machine), (void *) model->kbc_params);
    device_add_params(&w837x7_device, (void *) (W83787F | W837X7_KEY_89));
    device_add(&intel_flash_bxt_device);

    return ret;
}

int
machine_at_acerv35n_init(const machine_t *model)
{
    int ret;

    ret = bios_load_linear("roms/machines/acerv35n/v35nd1s1.bin",
                           0x000e0000, 131072, 0);

    if (bios_only || !ret)
        return ret;

    machine_at_common_init(model);

    pci_init(PCI_CONFIG_TYPE_1);
    pci_register_slot(0x00, PCI_CARD_NORTHBRIDGE, 0, 0, 0, 0);
    pci_register_slot(0x07, PCI_CARD_SOUTHBRIDGE, 0, 0, 0, 0);
    pci_register_slot(0x11, PCI_CARD_NORMAL,      1, 2, 3, 4);
    pci_register_slot(0x12, PCI_CARD_NORMAL,      2, 3, 4, 1);
    pci_register_slot(0x13, PCI_CARD_NORMAL,      3, 4, 1, 2);
    pci_register_slot(0x14, PCI_CARD_NORMAL,      4, 1, 2, 3);
    pci_register_slot(0x0D, PCI_CARD_NORMAL,      1, 2, 3, 4);

    device_add(&i430hx_device);
    device_add(&piix3_device);
    /* The chip is not marked FR but the BIOS accesses register 06h of GPIO. */
    device_add_params(&fdc37c93x_device, (void *) (FDC37XXX5 | FDC37C93X_FR));
    device_add(&sst_flash_29ee010_device);

    return ret;
}

int
machine_at_ap53_init(const machine_t *model)
{
    int ret;

    ret = bios_load_linear("roms/machines/ap53/ap53r2c0.rom",
                           0x000e0000, 131072, 0);

    if (bios_only || !ret)
        return ret;

    machine_at_common_init(model);

    pci_init(PCI_CONFIG_TYPE_1);
    pci_register_slot(0x00, PCI_CARD_NORTHBRIDGE, 0, 0, 0, 0);
    pci_register_slot(0x11, PCI_CARD_NORMAL,      1, 2, 3, 4);
    pci_register_slot(0x12, PCI_CARD_NORMAL,      2, 3, 4, 1);
    pci_register_slot(0x13, PCI_CARD_NORMAL,      3, 4, 1, 2);
    pci_register_slot(0x14, PCI_CARD_NORMAL,      4, 1, 2, 3);
    pci_register_slot(0x07, PCI_CARD_SOUTHBRIDGE, 0, 0, 0, 0);
    pci_register_slot(0x06, PCI_CARD_VIDEO,       1, 2, 3, 4);

    device_add(&i430hx_device);
    device_add(&piix3_device);
    device_add_params(machine_get_kbc_device(machine), (void *) model->kbc_params);
    device_add_params(&fdc37c669_device, (void *) 0);
    device_add(&intel_flash_bxt_device);

    return ret;
}

int
machine_at_8500tuc_init(const machine_t *model)
{
    int ret;

    ret = bios_load_linear("roms/machines/8500tuc/Tuc0221b.rom",
                           0x000e0000, 131072, 0);

    if (bios_only || !ret)
        return ret;

    machine_at_common_init(model);

    pci_init(PCI_CONFIG_TYPE_1);
    pci_register_slot(0x00, PCI_CARD_NORTHBRIDGE, 0, 0, 0, 0);
    pci_register_slot(0x08, PCI_CARD_NORMAL,      1, 2, 3, 4);
    pci_register_slot(0x09, PCI_CARD_NORMAL,      2, 3, 4, 1);
    pci_register_slot(0x0A, PCI_CARD_NORMAL,      3, 4, 1, 2);
    pci_register_slot(0x0B, PCI_CARD_NORMAL,      4, 1, 2, 3);
    pci_register_slot(0x07, PCI_CARD_SOUTHBRIDGE, 1, 2, 3, 4);

    device_add(&i430hx_device);
    device_add(&piix3_device);
    device_add_params(machine_get_kbc_device(machine), (void *) model->kbc_params);
    device_add_params(&um8669f_device, (void *) 0);
    device_add(&intel_flash_bxt_device);

    return ret;
}

static const device_config_t d943_config[] = {
    // clang-format off
    {
        .name           = "bios",
        .description    = "BIOS Version",
        .type           = CONFIG_BIOS,
        .default_string = "d943",
        .default_int    = 0,
        .file_filter    = NULL,
        .spinner        = { 0 },
        .selection      = { { 0 } },
        .bios           = {
            {
                .name          = "PhoenixBIOS 4.05 - Revision 1.02.943",
                .internal_name = "d943_oct96",
                .bios_type     = BIOS_NORMAL,
                .files_no      = 1,
                .local         = 0,
                .size          = 131072,
                .files         = { "roms/machines/d943/d943_oct96.bin", "" }
            },
            {
                .name          = "PhoenixBIOS 4.05 - Revision 1.03.943",
                .internal_name = "d943_dec96",
                .bios_type     = BIOS_NORMAL,
                .files_no      = 1,
                .local         = 0,
                .size          = 131072,
                .files         = { "roms/machines/d943/d943_dec96.bin", "" }
            },
            {
                .name          = "PhoenixBIOS 4.05 - Revision 1.05.943",
                .internal_name = "d943_sept97",
                .bios_type     = BIOS_NORMAL,
                .files_no      = 1,
                .local         = 0,
                .size          = 131072,
                .files         = { "roms/machines/d943/d943_sept97.bin", "" }
            },
            {
                .name          = "PhoenixBIOS 4.05 - Revision 1.06.943",
                .internal_name = "d943",
                .bios_type     = BIOS_NORMAL,
                .files_no      = 1,
                .local         = 0,
                .size          = 131072,
                .files         = { "roms/machines/d943/d943_oct97.bin", "" }
            },
            { .files_no = 0 }
        }
    },
    { .name = "", .description = "", .type = CONFIG_END }
    // clang-format on
};

const device_t d943_device = {
    .name          = "Siemens-Nixdorf D943",
    .internal_name = "d943_device",
    .flags         = 0,
    .local         = 0,
    .init          = NULL,
    .close         = NULL,
    .reset         = NULL,
    .available     = NULL,
    .speed_changed = NULL,
    .force_redraw  = NULL,
    .config        = d943_config
};

int
machine_at_d943_init(const machine_t *model)
{
    int         ret = 0;
    const char *fn;

    /* No ROMs available */
    if (!device_available(model->device))
        return ret;

    device_context(model->device);
    fn  = device_get_bios_file(machine_get_device(machine), device_get_config_bios("bios"), 0);
    ret = bios_load_linear(fn, 0x000e0000, 131072, 0);
    device_context_restore();

    machine_at_common_init(model);

    pci_init(PCI_CONFIG_TYPE_1);
    pci_register_slot(0x00, PCI_CARD_NORTHBRIDGE, 0, 0, 0, 0);
    pci_register_slot(0x07, PCI_CARD_SOUTHBRIDGE, 0, 0, 0, 0);
    pci_register_slot(0x08, PCI_CARD_VIDEO,       4, 0, 0, 0);
    pci_register_slot(0x11, PCI_CARD_NORMAL,      3, 2, 4, 1);
    pci_register_slot(0x12, PCI_CARD_NORMAL,      2, 1, 3, 4);
    pci_register_slot(0x13, PCI_CARD_NORMAL,      1, 3, 2, 4);

    device_add(&i430hx_device);
    device_add(&piix3_device);
    device_add_params(machine_get_kbc_device(machine), (void *) model->kbc_params);
    device_add_params(&fdc37c6xx_device, (void *) FDC37C665);
    device_add(&intel_flash_bxt_device);
    spd_register(SPD_TYPE_EDO, 0x7, 256);

    if (gfxcard[0] == VID_INTERNAL)
        device_add(machine_get_vid_device(machine));

    if (sound_card_current[0] == SOUND_INTERNAL)
        machine_snd = device_add(machine_get_snd_device(machine));

    return ret;
}

/* i430VX */
int
machine_at_gw2kma_init(const machine_t *model)
{
    int ret;

    ret = bios_load_linear_combined2("roms/machines/gw2kma/1007DQ0T.BIO",
                                     "roms/machines/gw2kma/1007DQ0T.BI1",
                                     "roms/machines/gw2kma/1007DQ0T.BI2",
                                     "roms/machines/gw2kma/1007DQ0T.BI3",
                                     "roms/machines/gw2kma/1007DQ0T.RCV",
                                     0x3a000, 128);

    if (bios_only || !ret)
        return ret;

    machine_at_common_init(model);

    pci_init(PCI_CONFIG_TYPE_1);
    pci_register_slot(0x00, PCI_CARD_NORTHBRIDGE, 0, 0, 0, 0);
    pci_register_slot(0x0D, PCI_CARD_NORMAL,      1, 2, 3, 4);
    pci_register_slot(0x0E, PCI_CARD_NORMAL,      2, 3, 4, 1);
    pci_register_slot(0x0F, PCI_CARD_NORMAL,      3, 4, 1, 2);
    pci_register_slot(0x10, PCI_CARD_NORMAL,      4, 1, 2, 3);
    pci_register_slot(0x07, PCI_CARD_SOUTHBRIDGE, 0, 0, 0, 4);

    if ((sound_card_current[0] == SOUND_INTERNAL) && machine_get_snd_device(machine)->available())
        machine_snd = device_add(machine_get_snd_device(machine));

    device_add(&i430vx_device);
    device_add(&piix3_device);
    device_add_params(&fdc37c93x_device, (void *) (FDC37XXX2 | FDC37C93X_FR));
    device_add(&intel_flash_bxt_ami_device);

    return ret;
}

/* SiS 5501 */
static const device_config_t c5sbm2_config[] = {
    // clang-format off
    {
        .name           = "bios",
        .description    = "BIOS Version",
        .type           = CONFIG_BIOS,
        .default_string = "5sbm2",
        .default_int    = 0,
        .file_filter    = NULL,
        .spinner        = { 0 },
        .selection      = { { 0 } },
        .bios           = {
            {
                .name          = "Award Modular BIOS v4.50GP - Revision 07/17/1995",
                .internal_name = "5sbm2_v450gp",
                .bios_type     = BIOS_NORMAL,
                .files_no      = 1,
                .local         = 0,
                .size          = 131072,
                .files         = { "roms/machines/5sbm2/5SBM0717.BIN", "" }
            },
            {
                .name          = "Award Modular BIOS v4.50PG - Revision 03/26/1996",
                .internal_name = "5sbm2",
                .bios_type     = BIOS_NORMAL,
                .files_no      = 1,
                .local         = 0,
                .size          = 131072,
                .files         = { "roms/machines/5sbm2/5SBM0326.BIN", "" }
            },
            {
                .name          = "Award Modular BIOS v4.51PG - Revision 2.2 (by Unicore Software)",
                .internal_name = "5sbm2_451pg",
                .bios_type     = BIOS_NORMAL,
                .files_no      = 1,
                .local         = 0,
                .size          = 131072,
                .files         = { "roms/machines/5sbm2/2A5ICC3A.BIN", "" }
            },
            { .files_no = 0 }
        }
    },
    { .name = "", .description = "", .type = CONFIG_END }
    // clang-format on
};

const device_t c5sbm2_device = {
    .name          = "Chaintech 5SBM/5SBM2 (M103)",
    .internal_name = "5sbm2_device",
    .flags         = 0,
    .local         = 0,
    .init          = NULL,
    .close         = NULL,
    .reset         = NULL,
    .available     = NULL,
    .speed_changed = NULL,
    .force_redraw  = NULL,
    .config        = c5sbm2_config
};

int
machine_at_5sbm2_init(const machine_t *model)
{
    int         ret = 0;
    const char *fn;

    /* No ROMs available */
    if (!device_available(model->device))
        return ret;

    device_context(model->device);
    fn  = device_get_bios_file(machine_get_device(machine), device_get_config_bios("bios"), 0);
    ret = bios_load_linear(fn, 0x000e0000, 131072, 0);
    device_context_restore();

    if (bios_only || !ret)
        return ret;

    machine_at_common_init(model);

    pci_init(PCI_CONFIG_TYPE_1 | FLAG_TRC_CONTROLS_CPURST);
    pci_register_slot(0x00, PCI_CARD_NORTHBRIDGE, 0, 0, 0, 0);
    pci_register_slot(0x01, PCI_CARD_SOUTHBRIDGE, 0, 0, 0, 0);
    pci_register_slot(0x0D, PCI_CARD_NORMAL,      1, 2, 3, 4);
    pci_register_slot(0x0F, PCI_CARD_NORMAL,      2, 3, 4, 1);
    pci_register_slot(0x11, PCI_CARD_NORMAL,      3, 4, 1, 2);

    device_add_params(machine_get_kbc_device(machine), (void *) model->kbc_params);
    device_add(&sis_550x_device);
    device_add_params(&um866x_device, (void *) UM8663AF);
    device_add(&sst_flash_29ee010_device);

    return ret;
}

/* SiS 5511 */
int
machine_at_amis727_init(const machine_t *model)
{
    int ret;

    ret = bios_load_linear("roms/machines/amis727/S727p.rom",
                           0x000e0000, 131072, 0);

    if (bios_only || !ret)
        return ret;

    machine_at_common_init(model);

    pci_init(PCI_CONFIG_TYPE_1 | FLAG_TRC_CONTROLS_CPURST);
    pci_register_slot(0x00, PCI_CARD_NORTHBRIDGE, 0, 0, 0, 0);
    pci_register_slot(0x01, PCI_CARD_SOUTHBRIDGE, 0xFE, 0xFF, 0, 0);
    pci_register_slot(0x0A, PCI_CARD_NORMAL,      1, 2, 3, 4);
    pci_register_slot(0x0B, PCI_CARD_NORMAL,      2, 3, 4, 1);
    pci_register_slot(0x0C, PCI_CARD_NORMAL,      3, 4, 1, 2);
    pci_register_slot(0x0D, PCI_CARD_NORMAL,      4, 1, 2, 3);

    device_add(&sis_5511_device);
    device_add_params(machine_get_kbc_device(machine), (void *) model->kbc_params);
    device_add_params(&fdc37c6xx_device, (void *) FDC37C665);
    device_add(&intel_flash_bxt_device);

    return ret;
}

static const device_config_t ap5s_config[] = {
    // clang-format off
    {
        .name           = "bios",
        .description    = "BIOS Version",
        .type           = CONFIG_BIOS,
        .default_string = "ap5s",
        .default_int    = 0,
        .file_filter    = NULL,
        .spinner        = { 0 },
        .selection      = { { 0 } },
        .bios           = {
            {
                .name          = "Award Modular BIOS v4.50PG - Revision R1.20",
                .internal_name = "ap5s_450pg",
                .bios_type     = BIOS_NORMAL,
                .files_no      = 1,
                .local         = 0,
                .size          = 131072,
                .files         = { "roms/machines/ap5s/ap5s120.bin", "" }
            },
            {
                .name          = "Award Modular BIOS v4.51PG - Revision R1.50",
                .internal_name = "ap5s_r150",
                .bios_type     = BIOS_NORMAL,
                .files_no      = 1,
                .local         = 0,
                .size          = 131072,
                .files         = { "roms/machines/ap5s/AP5S150.BIN", "" }
            },
            {
                .name          = "Award Modular BIOS v4.51PG - Revision R1.60",
                .internal_name = "ap5s",
                .bios_type     = BIOS_NORMAL,
                .files_no      = 1,
                .local         = 0,
                .size          = 131072,
                .files         = { "roms/machines/ap5s/ap5s160.bin", "" }
            },
            { .files_no = 0 }
        }
    },
    { .name = "", .description = "", .type = CONFIG_END }
    // clang-format on
};

const device_t ap5s_device = {
    .name          = "AOpen AP5S",
    .internal_name = "ap5s_device",
    .flags         = 0,
    .local         = 0,
    .init          = NULL,
    .close         = NULL,
    .reset         = NULL,
    .available     = NULL,
    .speed_changed = NULL,
    .force_redraw  = NULL,
    .config        = ap5s_config
};

int
machine_at_ap5s_init(const machine_t *model)
{
    int         ret = 0;
    const char *fn;

    /* No ROMs available */
    if (!device_available(model->device))
        return ret;

    device_context(model->device);
    fn  = device_get_bios_file(machine_get_device(machine), device_get_config_bios("bios"), 0);
    ret = bios_load_linear(fn, 0x000e0000, 131072, 0);
    device_context_restore();

    machine_at_common_init(model);

    pci_init(PCI_CONFIG_TYPE_1 | FLAG_TRC_CONTROLS_CPURST);
    pci_register_slot(0x00, PCI_CARD_NORTHBRIDGE, 0, 0, 0, 0);
    pci_register_slot(0x01, PCI_CARD_SOUTHBRIDGE, 0, 0, 0, 0);
    pci_register_slot(0x0D, PCI_CARD_NORMAL,      1, 2, 3, 4);
    pci_register_slot(0x0F, PCI_CARD_NORMAL,      2, 3, 4, 1);
    pci_register_slot(0x11, PCI_CARD_NORMAL,      3, 4, 1, 2);
    pci_register_slot(0x13, PCI_CARD_NORMAL,      4, 1, 2, 3);

    device_add(&sis_5511_device);
    device_add_params(machine_get_kbc_device(machine), (void *) model->kbc_params);
    device_add_params(&fdc37c6xx_device, (void *) FDC37C665);
    device_add(&sst_flash_29ee010_device);

    return ret;
}

int
machine_at_fm562_init(const machine_t *model)
{
    int ret;

    ret = bios_load_linear("roms/machines/fm562/PR11_US.ROM",
                           0x000e0000, 131072, 0);

    if (bios_only || !ret)
        return ret;

    machine_at_common_init(model);

    pci_init(PCI_CONFIG_TYPE_1 | FLAG_TRC_CONTROLS_CPURST);
    pci_register_slot(0x00, PCI_CARD_NORTHBRIDGE, 1, 2, 3, 4);
    pci_register_slot(0x01, PCI_CARD_SOUTHBRIDGE, 0, 0, 0, 0);
    pci_register_slot(0x11, PCI_CARD_NORMAL,      1, 3, 2, 4);
    pci_register_slot(0x13, PCI_CARD_NORMAL,      2, 1, 3, 4);
    pci_register_slot(0x0B, PCI_CARD_VIDEO,       0, 0, 0, 0); /* Onboard video */

    if (sound_card_current[0] == SOUND_INTERNAL)
        machine_snd = device_add(machine_get_snd_device(machine));

    device_add(&sis_5511_device);
    device_add_params(machine_get_kbc_device(machine), (void *) model->kbc_params);
    device_add_params(&fdc37c669_device, (void *) 0);
    device_add(&intel_flash_bxt_device);

    return ret;
}

int
machine_at_pc140_6260_init(const machine_t *model)
{
    int ret;

    ret = bios_load_linear("roms/machines/pc140_6260/LYKT32A.ROM",
                           0x000e0000, 131072, 0);

    if (bios_only || !ret)
        return ret;

    machine_at_common_init(model);

    pci_init(PCI_CONFIG_TYPE_1 | FLAG_TRC_CONTROLS_CPURST);
    pci_register_slot(0x00, PCI_CARD_NORTHBRIDGE, 1, 2, 3, 4);
    pci_register_slot(0x01, PCI_CARD_SOUTHBRIDGE, 0, 0, 0, 0);
    pci_register_slot(0x0E, PCI_CARD_NORMAL,      1, 2, 3, 4);
    pci_register_slot(0x0F, PCI_CARD_NORMAL,      2, 3, 4, 1);
    pci_register_slot(0x14, PCI_CARD_VIDEO,       0, 0, 0, 0); /* Onboard video */

    if (gfxcard[0] == VID_INTERNAL)
        device_add(&gd5436_onboard_pci_device);

    device_add(&sis_5511_device);
    device_add_params(machine_get_kbc_device(machine), (void *) model->kbc_params);
    device_add_params(&fdc37c669_device, (void *) 0);
    device_add(&sst_flash_29ee010_device);

    return ret;
}

static const device_config_t ms5124_config[] = {
    // clang-format off
    {
        .name           = "bios",
        .description    = "BIOS Version",
        .type           = CONFIG_BIOS,
        .default_string = "ms5124",
        .default_int    = 0,
        .file_filter    = NULL,
        .spinner        = { 0 },
        .selection      = { { 0 } },
        .bios           = {
            {
                .name          = "AMI WinBIOS (101094) - Revision AG77",
                .internal_name = "ms5124",
                .bios_type     = BIOS_NORMAL,
                .files_no      = 1,
                .local         = 0,
                .size          = 131072,
                .files         = { "roms/machines/ms5124/AG77.ROM", "" }
            },
            {
                .name          = "Award Modular BIOS v4.51PG - Revision WG72P",
                .internal_name = "ms5124_451pg",
                .bios_type     = BIOS_NORMAL,
                .files_no      = 1,
                .local         = 0,
                .size          = 131072,
                .files         = { "roms/machines/ms5124/WG72P.BIN", "" }
            },
            { .files_no = 0 }
        }
    },
    { .name = "", .description = "", .type = CONFIG_END }
    // clang-format on
};

const device_t ms5124_device = {
    .name          = "MSI MS-5124",
    .internal_name = "ms5124_device",
    .flags         = 0,
    .local         = 0,
    .init          = NULL,
    .close         = NULL,
    .reset         = NULL,
    .available     = NULL,
    .speed_changed = NULL,
    .force_redraw  = NULL,
    .config        = ms5124_config
};

int
machine_at_ms5124_init(const machine_t *model)
{
    int         ret = 0;
    const char *fn;

    /* No ROMs available */
    if (!device_available(model->device))
        return ret;

    device_context(model->device);
    fn  = device_get_bios_file(machine_get_device(machine), device_get_config_bios("bios"), 0);
    ret = bios_load_linear(fn, 0x000e0000, 131072, 0);
    device_context_restore();

    machine_at_common_init(model);

    pci_init(PCI_CONFIG_TYPE_1 | FLAG_TRC_CONTROLS_CPURST);
    pci_register_slot(0x00, PCI_CARD_NORTHBRIDGE, 0, 0, 0, 0);
    pci_register_slot(0x01, PCI_CARD_SOUTHBRIDGE, 0xFE, 0xFF, 0, 0);
    pci_register_slot(0x10, PCI_CARD_NORMAL,      1, 2, 3, 4);
    pci_register_slot(0x11, PCI_CARD_NORMAL,      4, 1, 2, 3);
    pci_register_slot(0x12, PCI_CARD_NORMAL,      3, 4, 1, 2);
    pci_register_slot(0x0F, PCI_CARD_NORMAL,      2, 3, 4, 1);

    device_add(&sis_5511_device);
    device_add_params(machine_get_kbc_device(machine), (void *) model->kbc_params);
    device_add_params(&w837x7_device, (void *) (W83787F | W837X7_KEY_88));
    device_add(&sst_flash_29ee010_device);

    return ret;
}

/* VLSI Wildcat */
int
machine_at_zeoswildcat_init(const machine_t *model)
{
    int ret;

    ret = bios_load_linear("roms/machines/zeoswildcat/003606.BIN",
                           0x000e0000, 131072, 0);

    if (bios_only || !ret)
        return ret;

    machine_at_common_init(model);

    pci_init(PCI_CONFIG_TYPE_1);
    pci_register_slot(0x00, PCI_CARD_NORTHBRIDGE, 0, 0, 0, 0);
    pci_register_slot(0x01, PCI_CARD_SOUTHBRIDGE, 0, 0, 0, 0);
    pci_register_slot(0x0D, PCI_CARD_IDE,         1, 2, 0, 0); /* Onboard device */
    pci_register_slot(0x0E, PCI_CARD_SCSI,        1, 0, 0, 0); /* Onboard device */
    pci_register_slot(0x0F, PCI_CARD_NETWORK,     1, 0, 0, 0); /* Onboard device */
    pci_register_slot(0x11, PCI_CARD_NORMAL,      1, 2, 3, 4); /* Slot 03 */
    pci_register_slot(0x12, PCI_CARD_NORMAL,      4, 2, 3, 1); /* Slot 04 */
    pci_register_slot(0x13, PCI_CARD_NORMAL,      3, 4, 1, 2); /* Slot 05 */

    /* Per the machine's manual there was an option for AMD SCSI and/or LAN controllers */
    device_add(&vl82c59x_wildcat_device);
    device_add(&intel_flash_bxt_device);
    device_add_params(machine_get_kbc_device(machine), (void *) model->kbc_params);
    device_add_params(&fdc37c6xx_device, (void *) FDC37C665);
    device_add(&ide_rz1001_pci_device);

    return ret;
}
