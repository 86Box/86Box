/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          Implementation of Socket 5 machines.
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
#include <86box/fdc_ext.h>
#include <86box/hdc.h>
#include <86box/hdc_ide.h>
#include "cpu.h"
#include <86box/timer.h>
#include <86box/fdd.h>
#include <86box/fdc.h>
#include <86box/keyboard.h>
#include <86box/flash.h>
#include <86box/nvr.h>
#include <86box/scsi_ncr53c8xx.h>
#include <86box/sio.h>
#include <86box/video.h>
#include <86box/machine.h>
#include <86box/sound.h>

/* i430NX */
int
machine_at_p54np4_init(const machine_t *model)
{
    int ret;

    ret = bios_load_linear("roms/machines/p54np4/asus-642accdebcb75833703472.bin",
                           0x000e0000, 131072, 0);

    if (bios_only || !ret)
        return ret;

    machine_at_common_init(model);
    device_add(&ide_vlb_2ch_device);

    pci_init(PCI_CONFIG_TYPE_2 | PCI_CAN_SWITCH_TYPE);
    pci_register_slot(0x00, PCI_CARD_NORTHBRIDGE, 0, 0, 0, 0);
    pci_register_slot(0x07, PCI_CARD_NORMAL,      1, 2, 3, 4); /* 07 = Slot 1 */
    pci_register_slot(0x06, PCI_CARD_NORMAL,      2, 3, 4, 1); /* 06 = Slot 2 */
    pci_register_slot(0x05, PCI_CARD_NORMAL,      3, 4, 1, 2); /* 05 = Slot 3 */
    pci_register_slot(0x04, PCI_CARD_NORMAL,      4, 1, 2, 3); /* 04 = Slot 4 */
    pci_register_slot(0x02, PCI_CARD_SOUTHBRIDGE, 0, 0, 0, 0);

    device_add(&i430nx_device);
    device_add(&sio_zb_device);
    device_add_params(machine_get_kbc_device(machine), (void *) model->kbc_params);
    device_add_params(&fdc37c6xx_device, (void *) (FDC37C665 | FDC37C6XX_IDE_PRI));
    device_add(&intel_flash_bxt_device);

    return ret;
}

int
machine_at_586ip_init(const machine_t *model)
{
    int ret;

    ret = bios_load_linear("roms/machines/586ip/IP.20",
                           0x000e0000, 131072, 0);

    if (bios_only || !ret)
        return ret;

    machine_at_award_common_init(model);

    device_add(&i430nx_device);

    return ret;
}

static const device_config_t plato_config[] = {
    // clang-format off
    {
        .name           = "bios",
        .description    = "BIOS Version",
        .type           = CONFIG_BIOS,
        .default_string = "plato",
        .default_int    = 0,
        .file_filter    = NULL,
        .spinner        = { 0 },
        .selection      = { { 0 } },
        .bios           = {
            {
                .name          = "Intel AMIBIOS - Revision 1.00.02.AX1P (AMBRA DP90 PCI)",
                .internal_name = "ambradp90",
                .bios_type     = BIOS_NORMAL,
                .files_no      = 2,
                .local         = 0,
                .size          = 131072,
                .files         = { "roms/machines/plato/1002AX1P.BIO", "roms/machines/plato/1002AX1P.BI1", "" }
            },
            {
                .name          = "Intel AMIBIOS - Revision 1.00.16.AX1",
                .internal_name = "plato",
                .bios_type     = BIOS_NORMAL,
                .files_no      = 2,
                .local         = 0,
                .size          = 131072,
                .files         = { "roms/machines/plato/1016ax1_.bio", "roms/machines/plato/1016ax1_.bi1", "" }
            },
            {
                .name          = "Intel AMIBIOS - Revision 1.00.16.AX1J (Dell Dimension XPS P___)",
                .internal_name = "dellplato",
                .bios_type     = BIOS_NORMAL,
                .files_no      = 2,
                .local         = 0,
                .size          = 131072,
                .files         = { "roms/machines/plato/1016AX1J.BIO", "roms/machines/plato/1016AX1J.BI1", "" }
            },
            { .files_no = 0 }
        }
    },
    { .name = "", .description = "", .type = CONFIG_END }
    // clang-format on
};

const device_t plato_device = {
    .name          = "Intel Premiere/PCI II (Plato)",
    .internal_name = "plato_device",
    .flags         = 0,
    .local         = 0,
    .init          = NULL,
    .close         = NULL,
    .reset         = NULL,
    .available     = NULL,
    .speed_changed = NULL,
    .force_redraw  = NULL,
    .config        = plato_config
};

int
machine_at_plato_init(const machine_t *model)
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
    ret = bios_load_linear_combined(fn, fn2, 0x1d000, 128);
    device_context_restore();

    machine_at_premiere_common_init(model, PCI_CAN_SWITCH_TYPE);

    device_add(&i430nx_device);

    return ret;
}

static const device_config_t d842_config[] = {
    // clang-format off
    {
        .name           = "bios",
        .description    = "BIOS Version",
        .type           = CONFIG_BIOS,
        .default_string = "d842",
        .default_int    = 0,
        .file_filter    = NULL,
        .spinner        = { 0 },
        .selection      = { { 0 } },
        .bios           = {
            {
                .name          = "PhoenixBIOS Pentium 1.03 - Revision 1.03.842",
                .internal_name = "d842_103",
                .bios_type     = BIOS_NORMAL,
                .files_no      = 1,
                .local         = 0,
                .size          = 131072,
                .files         = { "roms/machines/d842/d842.BIN", "" }
            },
            {
                .name          = "PhoenixBIOS Pentium 1.03 - Revision 1.09.842",
                .internal_name = "d842_109",
                .bios_type     = BIOS_NORMAL,
                .files_no      = 1,
                .local         = 0,
                .size          = 131072,
                .files         = { "roms/machines/d842/d842_jul96.bin", "" }
            },
            {
                .name          = "PhoenixBIOS Pentium 1.03 - Revision 1.10.842",
                .internal_name = "d842",
                .bios_type     = BIOS_NORMAL,
                .files_no      = 1,
                .local         = 0,
                .size          = 131072,
                .files         = { "roms/machines/d842/d842_jun98_1.bin", "" }
            },
            {
                .name          = "PhoenixBIOS 4.04 - Revision 1.05.842",
                .internal_name = "d842_105",
                .bios_type     = BIOS_NORMAL,
                .files_no      = 1,
                .local         = 0,
                .size          = 131072,
                .files         = { "roms/machines/d842/d842_mar96.bin", "" }
            },
            {
                .name          = "PhoenixBIOS 4.04 - Revision 1.06.842",
                .internal_name = "d842_106",
                .bios_type     = BIOS_NORMAL,
                .files_no      = 1,
                .local         = 0,
                .size          = 131072,
                .files         = { "roms/machines/d842/d842_apr98.bin", "" }
            },
            {
                .name          = "PhoenixBIOS 4.04 - Revision 1.07.842",
                .internal_name = "d842_107",
                .bios_type     = BIOS_NORMAL,
                .files_no      = 1,
                .local         = 0,
                .size          = 131072,
                .files         = { "roms/machines/d842/d842_jun98.BIN", "" }
            },
            { .files_no = 0 }
        }
    },
    { .name = "", .description = "", .type = CONFIG_END }
    // clang-format on
};

const device_t d842_device = {
    .name          = "Siemens-Nixdorf D842",
    .internal_name = "d842_device",
    .flags         = 0,
    .local         = 0,
    .init          = NULL,
    .close         = NULL,
    .reset         = NULL,
    .available     = NULL,
    .speed_changed = NULL,
    .force_redraw  = NULL,
    .config        = d842_config
};

int
machine_at_d842_init(const machine_t *model)
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

    device_add(&ide_pci_2ch_device);
    pci_init(PCI_CONFIG_TYPE_2 | PCI_NO_IRQ_STEERING);
    pci_register_slot(0x00, PCI_CARD_NORTHBRIDGE, 0, 0, 0, 0);
    pci_register_slot(0x01, PCI_CARD_SOUTHBRIDGE, 0, 0, 0, 0); /* Onboard */
    pci_register_slot(0x03, PCI_CARD_VIDEO,       4, 0, 0, 0); /* Onboard */
    pci_register_slot(0x0C, PCI_CARD_NORMAL,      1, 3, 2, 4); /* Slot 01 */
    pci_register_slot(0x0E, PCI_CARD_NORMAL,      2, 1, 3, 4); /* Slot 02 */

    device_add_params(machine_get_kbc_device(machine), (void *) model->kbc_params);
    device_add(&i430nx_device);
    device_add(&sio_zb_device);
    device_add_params(&fdc37c6xx_device, (void *) FDC37C665);
    device_add(&intel_flash_bxt_device);

    return ret;
}

int
machine_at_tek932_init(const machine_t *model)
{
    int ret;

    ret = bios_load_linear("roms/machines/tek932/B932_019.BIN",
                           0x000e0000, 131072, 0);

    if (bios_only || !ret)
        return ret;

    machine_at_common_init(model);

    pci_init(PCI_CONFIG_TYPE_2 | PCI_CAN_SWITCH_TYPE);
    pci_register_slot(0x00, PCI_CARD_NORTHBRIDGE, 0, 0, 0, 0);
    pci_register_slot(0x02, PCI_CARD_SOUTHBRIDGE, 0, 0, 0, 0);
    pci_register_slot(0x0F, PCI_CARD_NORMAL,      2, 3, 4, 1);
    pci_register_slot(0x0E, PCI_CARD_NORMAL,      3, 4, 1, 2);
    pci_register_slot(0x0D, PCI_CARD_NORMAL,      4, 1, 2, 3);
    pci_register_slot(0x0C, PCI_CARD_NORMAL,      1, 3, 2, 4);

    machine_force_ps2(1);

    device_add_params(machine_get_kbc_device(machine), (void *) model->kbc_params);
    device_add(&i430nx_device);
    device_add(&sio_zb_device);
    device_add(&ide_vlb_device);
    device_add_params(&fdc37c6xx_device, (void *) (FDC37C665 | FDC37C6XX_IDE_PRI));
    device_add(&intel_flash_bxt_ami_device);

    return ret;
}

/* i430FX */
int
machine_at_acerv30_init(const machine_t *model)
{
    int ret;

    ret = bios_load_linear("roms/machines/acerv30/V30R01N9.BIN",
                           0x000e0000, 131072, 0);

    if (bios_only || !ret)
        return ret;

    machine_at_common_init(model);

    pci_init(PCI_CONFIG_TYPE_1);
    pci_register_slot(0x00, PCI_CARD_NORTHBRIDGE, 0, 0, 0, 0);
    pci_register_slot(0x07, PCI_CARD_SOUTHBRIDGE, 0, 0, 0, 0);
    pci_register_slot(0x12, PCI_CARD_NORMAL,      1, 2, 3, 4);
    pci_register_slot(0x11, PCI_CARD_NORMAL,      2, 3, 4, 1);
    pci_register_slot(0x14, PCI_CARD_NORMAL,      3, 4, 1, 2);
    pci_register_slot(0x13, PCI_CARD_NORMAL,      4, 1, 2, 3);

    device_add(&i430fx_device);
    device_add(&piix_device);
    device_add_params(machine_get_kbc_device(machine), (void *) model->kbc_params);
    device_add_params(&fdc37c6xx_device, (void *) FDC37C665);

    device_add(&sst_flash_29ee010_device);

    return ret;
}

int
machine_at_apollo_init(const machine_t *model)
{
    int ret;

    ret = bios_load_linear("roms/machines/apollo/S728P.ROM",
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
    pci_register_slot(0x07, PCI_CARD_SOUTHBRIDGE, 0, 0, 0, 0);

    device_add_params(machine_get_kbc_device(machine), (void *) model->kbc_params);
    device_add(&i430fx_device);
    device_add(&piix_device);
    device_add_params(&pc873xx_device, (void *) (PC87332 | PCX730X_398));
    device_add(&intel_flash_bxt_device);

    return ret;
}

int
machine_at_optiplexgxl_init(const machine_t *model)
{
    int ret;

    ret = bios_load_linear("roms/machines/optiplexgxl/DELL.ROM",
                           0x000e0000, 131072, 0);

    if (bios_only || !ret)
        return ret;

    machine_at_common_init(model);

    pci_init(PCI_CONFIG_TYPE_1);
    pci_register_slot(0x00, PCI_CARD_NORTHBRIDGE, 1, 2, 3, 4);
    pci_register_slot(0x0C, PCI_CARD_NORMAL,      1, 2, 3, 4);
    pci_register_slot(0x0D, PCI_CARD_NORMAL,      3, 4, 2, 1);
    pci_register_slot(0x10, PCI_CARD_VIDEO,       0, 0, 0, 0);
    pci_register_slot(0x07, PCI_CARD_SOUTHBRIDGE, 0, 0, 0, 0);

    if (gfxcard[0] == VID_INTERNAL)
        device_add(machine_get_vid_device(machine));

    if (sound_card_current[0] == SOUND_INTERNAL)
        machine_snd = device_add(machine_get_snd_device(machine));

    device_add_params(machine_get_kbc_device(machine), (void *) model->kbc_params);
    device_add(&i430fx_device);
    device_add(&piix_device);
    device_add_params(&pc873xx_device, (void *) (PC87332 | PCX730X_02E));
    device_add(&dell_jumper_device);
    device_add(&intel_flash_bxt_device);

    return ret;
}

static const device_config_t pt2000_config[] = {
    // clang-format off
    {
        .name           = "bios",
        .description    = "BIOS Version",
        .type           = CONFIG_BIOS,
        .default_string = "pt2000",
        .default_int    = 0,
        .file_filter    = NULL,
        .spinner        = { 0 },
        .selection      = { { 0 } },
        .bios           = {
            {
                .name          = "Award Modular BIOS v4.50GP - Revision T1.01",
                .internal_name = "pt2000",
                .bios_type     = BIOS_NORMAL,
                .files_no      = 1,
                .local         = 0,
                .size          = 131072,
                .files         = { "roms/machines/ficpt2000/PT2000_v1.01.BIN", "" }
            },
            {
                .name          = "Award Modular BIOS v4.51PG - Revision 3.072C806",
                .internal_name = "pt2000_451pg",
                .bios_type     = BIOS_NORMAL,
                .files_no      = 1,
                .local         = 0,
                .size          = 131072,
                .files         = { "roms/machines/ficpt2000/3072c806.bin", "" }
            },
            { .files_no = 0 }
        },
    },
    { .name = "", .description = "", .type = CONFIG_END }
    // clang-format on
};

const device_t pt2000_device = {
    .name          = "FIC PT-2000",
    .internal_name = "pt2000_device",
    .flags         = 0,
    .local         = 0,
    .init          = NULL,
    .close         = NULL,
    .reset         = NULL,
    .available     = NULL,
    .speed_changed = NULL,
    .force_redraw  = NULL,
    .config        = pt2000_config
};

int
machine_at_pt2000_init(const machine_t *model)
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
    pci_register_slot(0x08, PCI_CARD_NORMAL,      1, 2, 3, 4);
    pci_register_slot(0x09, PCI_CARD_NORMAL,      2, 3, 4, 1);
    pci_register_slot(0x0A, PCI_CARD_NORMAL,      3, 4, 1, 2);
    pci_register_slot(0x0B, PCI_CARD_NORMAL,      4, 1, 2, 3);
    pci_register_slot(0x07, PCI_CARD_SOUTHBRIDGE, 0, 0, 0, 0);

    device_add_params(machine_get_kbc_device(machine), (void *) model->kbc_params);
    device_add(&i430fx_device);
    device_add(&piix_device);
    device_add_params(&pc873xx_device, (void *) (PC87332 | PCX730X_398));
    device_add(&intel_flash_bxt_device);

    return ret;
}

static void
machine_at_morrison32_gpio_init(void)
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
    /* Bit 2: 0 = No onboard audio, 1 = Onboard audio present. */
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
        gpio |= 0xffff01ff;
    else
        gpio |= 0xffff00ff;

    if (sound_card_current[0] == SOUND_INTERNAL)
        gpio |= 0xffff04ff;

    machine_set_gpio_default(gpio);
}

int
machine_at_morrison32_init(const machine_t *model)
{
    int ret;

    ret = bios_load_linear_combined("roms/machines/morrison32/1011BT0L.BIO",
                                    "roms/machines/morrison32/1011BT0L.BI1",
                                    0x20000, 128);

    if (bios_only || !ret)
        return ret;

    machine_at_common_init(model);
    machine_at_morrison32_gpio_init();

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

/* Some stuff taken from Monaco */
static void
machine_at_morrison64_gpio_init(void)
{
    uint32_t gpio = 0xffffe0cf;

    /* Return to this after CS4232 PnP is working. */
    /* Register 0x0078 (Undocumented): */
    /* Bit 5,4: Vibra 16S base address: 0 = 220h, 1 = 260h, 2 = 240h, 3 = 280h. */
    /*device_context(machine_get_snd_device(machine));
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
    device_context_restore();*/

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
        gpio |= 0xffff01af;
    else if (cpu_dmulti <= 2.0)
        gpio |= 0xffffe2af;
    else if ((cpu_dmulti > 2.0) && (cpu_dmulti <= 2.5))
        gpio |= 0xffffe5cf;

    machine_set_gpio_default(gpio);
}

int
machine_at_pc330_65x6_init(const machine_t *model)
{
    int ret;

    ret = bios_load_linear("roms/machines/pc330_65x6/intel.bin",
                           0x000c0000, 262144, 0);

    if (bios_only || !ret)
        return ret;
    
    machine_at_common_init(model);
    machine_at_morrison64_gpio_init();

    pci_init(PCI_CONFIG_TYPE_1);
    pci_register_slot(0x00, PCI_CARD_NORTHBRIDGE, 0, 0, 0, 0);
	pci_register_slot(0x07, PCI_CARD_SOUTHBRIDGE, 0, 0, 0, 0);
    pci_register_slot(0x08, PCI_CARD_VIDEO,       4, 0, 0, 0);
    pci_register_slot(0x0B, PCI_CARD_NORMAL,      1, 2, 3, 4);
    pci_register_slot(0x11, PCI_CARD_NORMAL,      1, 3, 2, 4);
    pci_register_slot(0x13, PCI_CARD_NORMAL, 	  2, 1, 3, 4);
    device_add(&i430fx_device);
    device_add(&piix_device);
    device_add_params(&pc87306_device, (void *) PCX730X_AMI);
    device_add(&intel_flash_bxt_ami_device);
	
	if (gfxcard[0] == VID_INTERNAL)
        device_add(machine_get_vid_device(machine));

    return ret;
}

static void
machine_at_zappa_gpio_init(void)
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

static const device_config_t zappa_config[] = {
    // clang-format off
    {
        .name           = "bios",
        .description    = "BIOS Version",
        .type           = CONFIG_BIOS,
        .default_string = "zappa",
        .default_int    = 0,
        .file_filter    = NULL,
        .spinner        = { 0 },
        .selection      = { { 0 } },
        .bios           = {
            {
                .name          = "Intel AMIBIOS - Revision 1.00.06.BS0",
                .internal_name = "zappa",
                .bios_type     = BIOS_NORMAL,
                .files_no      = 2,
                .local         = 0,
                .size          = 131072,
                .files         = { "roms/machines/zappa/1006bs0_.bio", "roms/machines/zappa/1006bs0_.bi1", "" }
            },
            {
                .name          = "Intel AMIBIOS - Revision 1.00.11.BS0T (Gateway 2000)",
                .internal_name = "zappa_gw2k",
                .bios_type     = BIOS_NORMAL,
                .files_no      = 2,
                .local         = 0,
                .size          = 131072,
                .files         = { "roms/machines/zappa/1011BS0T.BIO", "roms/machines/zappa/1011BS0T.BI1", "" }
            },
            { .files_no = 0 }
        }
    },
    { .name = "", .description = "", .type = CONFIG_END }
    // clang-format on
};

const device_t zappa_device = {
    .name          = "Intel Advanced/ZP (Zappa)",
    .internal_name = "zappa_device",
    .flags         = 0,
    .local         = 0,
    .init          = NULL,
    .close         = NULL,
    .reset         = NULL,
    .available     = NULL,
    .speed_changed = NULL,
    .force_redraw  = NULL,
    .config        = zappa_config
};

int
machine_at_zappa_init(const machine_t *model)
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
    machine_at_zappa_gpio_init();

    pci_init(PCI_CONFIG_TYPE_1);
    pci_register_slot(0x00, PCI_CARD_NORTHBRIDGE, 0, 0, 0, 0);
    pci_register_slot(0x0D, PCI_CARD_NORMAL,      1, 2, 3, 4);
    pci_register_slot(0x0E, PCI_CARD_NORMAL,      3, 4, 1, 2);
    pci_register_slot(0x0F, PCI_CARD_NORMAL,      2, 3, 4, 1);
    pci_register_slot(0x07, PCI_CARD_SOUTHBRIDGE, 0, 0, 0, 0);

    device_add(&i430fx_device);
    device_add(&piix_device);
    device_add_params(&pc87306_device, (void *) PCX730X_AMI);
    device_add(&intel_flash_bxt_ami_device);

    return ret;
}

static const device_config_t powermatev_config[] = {
    // clang-format off
    {
        .name           = "bios",
        .description    = "BIOS Version",
        .type           = CONFIG_BIOS,
        .default_string = "powermatev",
        .default_int    = 0,
        .file_filter    = NULL,
        .spinner        = { 0 },
        .selection      = { { 0 } },
        .bios           = {
            {
                .name          = "PhoenixBIOS Version 4.05.M - Revision 00.04.08",
                .internal_name = "powermatev_122195",
                .bios_type     = BIOS_NORMAL,
                .files_no      = 1,
                .local         = 0,
                .size          = 131072,
                .files         = { "roms/machines/powermatev/B50NM00M.ROM", "" }
            },
            {
                .name          = "PhoenixBIOS Version 4.05.V - Revision 00.04.15",
                .internal_name = "powermatev",
                .bios_type     = BIOS_NORMAL,
                .files_no      = 1,
                .local         = 0,
                .size          = 131072,
                .files         = { "roms/machines/powermatev/B50NM00V.ROM", "" }
            },      
            { .files_no = 0 }            
        }
    },
    { .name = "", .description = "", .type = CONFIG_END }
    // clang-format on
};

const device_t powermatev_device = {
    .name          = "NEC PowerMate Vxxx",
    .internal_name = "powermatev_device",
    .flags         = 0,
    .local         = 0,
    .init          = NULL,
    .close         = NULL,
    .reset         = NULL,
    .available     = NULL,
    .speed_changed = NULL,
    .force_redraw  = NULL,
    .config        = powermatev_config
};

int
machine_at_powermatev_init(const machine_t *model)
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
    pci_register_slot(0x08, PCI_CARD_NORMAL,      0, 0, 0, 0);
    pci_register_slot(0x11, PCI_CARD_NORMAL,      1, 2, 3, 4);
    pci_register_slot(0x13, PCI_CARD_NORMAL,      2, 3, 4, 1);

    if (sound_card_current[0] == SOUND_INTERNAL)
        machine_snd = device_add(machine_get_snd_device(machine));

    device_add_params(machine_get_kbc_device(machine), (void *) model->kbc_params);
    device_add(&i430fx_device);
    device_add(&piix_device);
    device_add_params(&fdc37c6xx_device, (void *) FDC37C665);
    device_add(&intel_flash_bxt_device);

    return ret;
}

int
machine_at_hawk_init(const machine_t *model)
{
    int ret;

    ret = bios_load_linear("roms/machines/hawk/HAWK.ROM",
                           0x000e0000, 131072, 0);

    if (bios_only || !ret)
        return ret;

    machine_at_common_init(model);

    pci_init(PCI_CONFIG_TYPE_1);
    pci_register_slot(0x00, PCI_CARD_NORTHBRIDGE, 0, 0, 0, 0);
    pci_register_slot(0x14, PCI_CARD_NORMAL,      1, 2, 3, 4);
    pci_register_slot(0x13, PCI_CARD_NORMAL,      2, 3, 4, 1);
    pci_register_slot(0x12, PCI_CARD_NORMAL,      3, 4, 1, 2);
    pci_register_slot(0x07, PCI_CARD_SOUTHBRIDGE, 0, 0, 0, 0);

    device_add_params(machine_get_kbc_device(machine), (void *) model->kbc_params);
    device_add(&i430fx_device);
    device_add(&piix_device);
    device_add_params(&fdc37c6xx_device, (void *) FDC37C665);
    device_add(&intel_flash_bxt_device);

    return ret;
}

/* OPTi 597 */
int
machine_at_ncselp90_init(const machine_t *model)
{
    int ret;

    ret = bios_load_linear("roms/machines/ncselp90/elegancep90.bin",
                           0x000e0000, 131072, 0);

    if (bios_only || !ret)
        return ret;

    machine_at_common_init(model);

    pci_init(PCI_CONFIG_TYPE_1);
    pci_register_slot(0x10, PCI_CARD_NORTHBRIDGE, 0, 0, 0, 0);
    pci_register_slot(0x11, PCI_CARD_NORMAL,      1, 2, 3, 4);
    pci_register_slot(0x12, PCI_CARD_NORMAL,      2, 3, 4, 1);
    pci_register_slot(0x13, PCI_CARD_NORMAL,      3, 4, 1, 2);

    device_add(&opti5x7_pci_device);
    device_add(&opti822_device);
    device_add(&sst_flash_29ee010_device);
    device_add_params(machine_get_kbc_device(machine), (void *) model->kbc_params);
    device_add(&ide_opti611_vlb_device);
    device_add_params(&fdc37c6xx_device, (void *) (FDC37C665 | FDC37C6XX_IDE_SEC));
    device_add(&ide_vlb_2ch_device);

    return ret;
}

int
machine_at_hot543_init(const machine_t *model)
{
    int ret;

    ret = bios_load_linear("roms/machines/hot543/543_R21.BIN",
                           0x000e0000, 131072, 0);

    if (bios_only || !ret)
        return ret;

    machine_at_common_init(model);

    pci_init(PCI_CONFIG_TYPE_1);
    pci_register_slot(0x10, PCI_CARD_NORTHBRIDGE, 0, 0, 0, 0);
    pci_register_slot(0x11, PCI_CARD_NORMAL,      1, 2, 3, 4);
    pci_register_slot(0x12, PCI_CARD_NORMAL,      2, 3, 4, 1);
    pci_register_slot(0x13, PCI_CARD_NORMAL,      3, 4, 1, 2);

    device_add(&opti5x7_pci_device);
    device_add(&opti822_device);
    device_add(&sst_flash_29ee010_device);
    device_add_params(machine_get_kbc_device(machine), (void *) model->kbc_params);

    if (fdc_current[0] == FDC_INTERNAL)
        device_add(&fdc_at_device);

    return ret;
}

int
machine_at_pat54pv_init(const machine_t *model)
{
    int ret;

    ret = bios_load_linear("roms/machines/pat54pv/PAT54PV.bin",
                           0x000f0000, 65536, 0);

    if (bios_only || !ret)
        return ret;

    machine_at_common_init(model);

    device_add(&opti5x7_device);

    machine_force_ps2(1);
    device_add_params(machine_get_kbc_device(machine), (void *) model->kbc_params);

    if (fdc_current[0] == FDC_INTERNAL)
        device_add(&fdc_at_device);

    return ret;
}

/* SiS 501 */
int
machine_at_p54sp4_init(const machine_t *model)
{
    int ret;

    ret = bios_load_linear("roms/machines/p54sp4/SI5I0204.AWD",
                           0x000e0000, 131072, 0);

    if (bios_only || !ret)
        return ret;

    machine_at_sp4_common_init(model);

    return ret;
}

int
machine_at_sq588_init(const machine_t *model)
{
    int ret;

    ret = bios_load_linear("roms/machines/sq588/sq588b03.rom",
                           0x000e0000, 131072, 0);

    if (bios_only || !ret)
        return ret;

    machine_at_common_init(model);

    pci_init(PCI_CONFIG_TYPE_1 | FLAG_TRC_CONTROLS_CPURST);
    pci_register_slot(0x00, PCI_CARD_NORTHBRIDGE, 0, 0, 0, 0);
    pci_register_slot(0x01, PCI_CARD_SOUTHBRIDGE, 0, 0, 0, 0);
    /* Correct: 0D (01), 0F (02), 11 (03), 13 (04) */
    pci_register_slot(0x02, PCI_CARD_IDE,         1, 2, 3, 4);
    pci_register_slot(0x0D, PCI_CARD_NORMAL,      1, 2, 3, 4);
    pci_register_slot(0x0F, PCI_CARD_NORMAL,      2, 3, 4, 1);
    pci_register_slot(0x11, PCI_CARD_NORMAL,      3, 4, 1, 2);
    pci_register_slot(0x13, PCI_CARD_NORMAL,      4, 1, 2, 3);

    device_add(&sis_85c50x_device);
    device_add(&ide_cmd640_pci_single_channel_device);
    device_add_params(machine_get_kbc_device(machine), (void *) model->kbc_params);
    device_add_params(&fdc37c6xx_device, (void *) (FDC37C665 | FDC37C6XX_IDE_SEC));
    device_add(&sst_flash_29ee010_device);

    return ret;
}

int
machine_at_p54sps_init(const machine_t *model)
{
    int ret;

    ret = bios_load_linear("roms/machines/p54sps/35s106.bin",
                           0x000e0000, 131072, 0);

    if (bios_only || !ret)
        return ret;

    machine_at_common_init(model);

    pci_init(PCI_CONFIG_TYPE_1 | FLAG_TRC_CONTROLS_CPURST);
    pci_register_slot(0x00, PCI_CARD_NORTHBRIDGE, 0, 0, 0, 0);
    pci_register_slot(0x01, PCI_CARD_SOUTHBRIDGE, 0, 0, 0, 0);
    pci_register_slot(0x06, PCI_CARD_NORMAL,      1, 2, 3, 4);
    pci_register_slot(0x07, PCI_CARD_NORMAL,      2, 3, 4, 1);
    pci_register_slot(0x08, PCI_CARD_NORMAL,      3, 4, 1, 2);
    pci_register_slot(0x09, PCI_CARD_NORMAL,      4, 1, 2, 3);

    device_add(&sis_85c50x_device);
    device_add(&ide_pci_2ch_device);
    device_add_params(machine_get_kbc_device(machine), (void *) model->kbc_params);
    device_add_params(&w837x7_device, (void *) (W83787F | W837X7_KEY_89));
    device_add(&sst_flash_29ee010_device);

    return ret;
}

int
machine_at_ms5109_init(const machine_t *model)
{
    int ret;

    ret = bios_load_linear("roms/machines/ms5109/A778.ROM",
                           0x000e0000, 131072, 0);

    if (bios_only || !ret)
        return ret;

    machine_at_common_init(model);

    pci_init(PCI_CONFIG_TYPE_1 | FLAG_TRC_CONTROLS_CPURST);
    pci_register_slot(0x00, PCI_CARD_NORTHBRIDGE, 0, 0, 0, 0);
    pci_register_slot(0x01, PCI_CARD_SOUTHBRIDGE, 0, 0, 0, 0);
    pci_register_slot(0x07, PCI_CARD_IDE,         0, 0, 0, 0);
    pci_register_slot(0x0D, PCI_CARD_NORMAL,      1, 3, 2, 4);
    pci_register_slot(0x0F, PCI_CARD_NORMAL,      2, 1, 3, 4);
    pci_register_slot(0x11, PCI_CARD_NORMAL,      3, 3, 2, 4);
    pci_register_slot(0x13, PCI_CARD_NORMAL,      4, 1, 2, 3);

    device_add(&sis_550x_85c503_device);
    device_add(&ide_w83769f_pci_device);
    device_add_params(machine_get_kbc_device(machine), (void *) model->kbc_params);
    device_add_params(&w837x7_device, (void *) (W83787F | W837X7_KEY_89));
    device_add(&sst_flash_29ee010_device);

    return ret;
}

/* SiS 5501 */
int
machine_at_torino_init(const machine_t *model)
{
    int ret;

    ret = bios_load_linear_inverted("roms/machines/torino/PER113.ROM",
                                    0x000e0000, 131072, 0);

    if (bios_only || !ret)
        return ret;

    machine_at_common_init(model);

    pci_init(PCI_CONFIG_TYPE_1 | FLAG_TRC_CONTROLS_CPURST);
    pci_register_slot(0x00, PCI_CARD_NORTHBRIDGE, 0, 0, 0, 0);
    pci_register_slot(0x01, PCI_CARD_SOUTHBRIDGE, 0, 0, 0, 0);
    pci_register_slot(0x08, PCI_CARD_VIDEO,       0, 0, 0, 0);
    pci_register_slot(0x03, PCI_CARD_NORMAL,      1, 2, 3, 4);
    pci_register_slot(0x0A, PCI_CARD_NORMAL,      4, 1, 2, 3);
    pci_register_slot(0x0B, PCI_CARD_NORMAL,      3, 4, 1, 2);

    if (gfxcard[0] == VID_INTERNAL)
        device_add(machine_get_vid_device(machine));

    device_add(&sis_550x_85c503_device);
    device_add(&ide_um8673f_device);
    device_add_params(machine_get_kbc_device(machine), (void *) model->kbc_params);
    device_add_params(&fdc37c6xx_device, (void *) FDC37C665);
    device_add(&intel_flash_bxt_ami_device);

    return ret;
}

/* UMC 889x */
int
machine_at_hot539_init(const machine_t *model)
{
    int ret;

    ret = bios_load_linear("roms/machines/hot539/539_R17.ROM",
                           0x000e0000, 131072, 0);

    if (bios_only || !ret)
        return ret;

    machine_at_common_init(model);

    pci_init(PCI_CONFIG_TYPE_1);
    pci_register_slot(0x00, PCI_CARD_NORTHBRIDGE, 0, 0, 0, 0);
    pci_register_slot(0x12, PCI_CARD_SOUTHBRIDGE, 0, 0, 0, 0);
    pci_register_slot(0x0C, PCI_CARD_NORMAL,      1, 2, 3, 4);
    pci_register_slot(0x15, PCI_CARD_NORMAL,      1, 2, 3, 4);
    pci_register_slot(0x0D, PCI_CARD_NORMAL,      2, 3, 4, 1);
    pci_register_slot(0x16, PCI_CARD_NORMAL,      2, 3, 4, 1);
    pci_register_slot(0x0E, PCI_CARD_NORMAL,      3, 4, 1, 2);
    pci_register_slot(0x0F, PCI_CARD_NORMAL,      4, 1, 2, 3);

    device_add(&umc_8890_device);
    device_add(&umc_8886af_device);
    device_add(&sst_flash_29ee010_device);
    device_add_params(machine_get_kbc_device(machine), (void *) model->kbc_params);
    device_add_params(&um866x_device, (void *) UM8663AF);

    return ret;
}

/* VLSI SuperCore */
int
machine_at_bravoms586_init(const machine_t *model)
{
    int ret;

    ret = bios_load_linear("roms/machines/bravoms586/asttest.bin",
                           0x000e0000, 131072, 0);

    if (bios_only || !ret)
        return ret;

    machine_at_common_init(model);

    pci_init(PCI_CONFIG_TYPE_1);
    pci_register_slot(0x00, PCI_CARD_NORTHBRIDGE, 0, 0, 0, 0);
    pci_register_slot(0x01, PCI_CARD_SOUTHBRIDGE, 0, 0, 0, 0);
    pci_register_slot(0x06, PCI_CARD_IDE,         2, 0, 0, 0);
    pci_register_slot(0x08, PCI_CARD_VIDEO,       4, 0, 0, 0);
    pci_register_slot(0x09, PCI_CARD_NORMAL,      1, 2, 3, 4);
    pci_register_slot(0x0A, PCI_CARD_NORMAL,      2, 3, 4, 1);
    pci_register_slot(0x0B, PCI_CARD_NORMAL,      3, 4, 1, 2);
    pci_register_slot(0x0C, PCI_CARD_NORMAL,      4, 1, 2, 3);

    device_add(&vl82c59x_device);
    device_add(&intel_flash_bxt_device);
    device_add_params(machine_get_kbc_device(machine), (void *) model->kbc_params);
    device_add_params(&fdc37c6xx_device, (void *) (FDC37C665 | FDC37C6XX_IDE_SEC));
    device_add(&ide_cmd640_pci_single_channel_device);

    if (gfxcard[0] == VID_INTERNAL)
        device_add(machine_get_vid_device(machine));

    device_add(&ast_readout_device); /* AST custom jumper readout */
    device_add(&ast_nvr_device);     /* AST custom secondary NVR device */

    return ret;
}

int
machine_at_m54si_init(const machine_t *model)
{
    int ret;

    ret = bios_load_linear("roms/machines/m54si/M54SI.03",
                           0x000e0000, 131072, 0);

    if (bios_only || !ret)
        return ret;

    machine_at_common_init(model);

    pci_init(PCI_CONFIG_TYPE_1);
    pci_register_slot(0x00, PCI_CARD_NORTHBRIDGE, 0, 0, 0, 0);
    pci_register_slot(0x01, PCI_CARD_SOUTHBRIDGE, 0, 0, 0, 0);
    pci_register_slot(0x0D, PCI_CARD_IDE, 0, 0, 0, 0); /* Onboard device */
    pci_register_slot(0x10, PCI_CARD_NORMAL, 1, 2, 3, 4);
    pci_register_slot(0x11, PCI_CARD_NORMAL, 2, 3, 4, 1);
    pci_register_slot(0x12, PCI_CARD_NORMAL, 3, 4, 1, 2);

    /* Slots are a guess since this BIOS won't work with pcireg */
    device_add(&vl82c59x_device);
    device_add(&intel_flash_bxt_device);
    device_add_params(machine_get_kbc_device(machine), (void *) model->kbc_params);
    device_add_params(&fdc37c6xx_device, (void *) (FDC37C665 | FDC37C6XX_IDE_SEC));
    device_add(&ide_cmd640_pci_single_channel_device);

    return ret;
}

int
machine_at_pb600_init(const machine_t *model)
{
    int ret;

    ret = bios_load_linear("roms/machines/pb600/BIOS.ROM",
                           0x000e0000, 131072, 0);

    if (bios_only || !ret)
        return ret;

    machine_at_common_init(model);

    pci_init(PCI_CONFIG_TYPE_1);
    pci_register_slot(0x00, PCI_CARD_NORTHBRIDGE, 0, 0, 0, 0);
    pci_register_slot(0x06, PCI_CARD_SOUTHBRIDGE, 0, 0, 0, 0);
    pci_register_slot(0x0A, PCI_CARD_VIDEO, 4, 0, 0, 0);
    pci_register_slot(0x0D, PCI_CARD_IDE, 4, 0, 0, 0);
    pci_register_slot(0x0B, PCI_CARD_NORMAL, 3, 4, 1, 2);
    pci_register_slot(0x11, PCI_CARD_NORMAL, 1, 2, 3, 4);
    pci_register_slot(0x13, PCI_CARD_NORMAL, 2, 3, 4, 1);

    device_add(&vl82c59x_device);
    device_add(&intel_flash_bxt_device);
    device_add_params(machine_get_kbc_device(machine), (void *) model->kbc_params);
    device_add_params(&fdc37c6xx_device, (void *) FDC37C665);
    device_add(&phoenix_486_jumper_pci_pb600_device);
    device_add(&ide_cmd640_pci_device);

    if (gfxcard[0] == VID_INTERNAL)
        device_add(machine_get_vid_device(machine));

    return ret;
}

/* VLSI Wildcat */
int
machine_at_globalyst620_init(const machine_t *model)
{
    int ret;

    ret = bios_load_linear("roms/machines/globalyst620/p107.bin",
                           0x000e0000, 131072, 0);

    if (bios_only || !ret)
        return ret;

    machine_at_common_init(model);

    pci_init(PCI_CONFIG_TYPE_1);
    pci_register_slot(0x00, PCI_CARD_NORTHBRIDGE, 0, 0, 0, 0);
    pci_register_slot(0x01, PCI_CARD_SOUTHBRIDGE, 0, 0, 0, 0);
    pci_register_slot(0x0F, PCI_CARD_VIDEO, 0, 0, 0, 0); /* Onboard device */
    pci_register_slot(0x10, PCI_CARD_IDE, 0, 0, 0, 0); /* Onboard device */
    pci_register_slot(0x11, PCI_CARD_NORMAL, 1, 2, 3, 4); /* Slot 04 */
    pci_register_slot(0x12, PCI_CARD_NORMAL, 2, 3, 4, 1); /* Slot 05 */
    pci_register_slot(0x13, PCI_CARD_NORMAL, 3, 4, 1, 2); /* Slot 06 */

    device_add(&vl82c59x_wildcat_device);
    device_add(&intel_flash_bxt_device);
    device_add_params(machine_get_kbc_device(machine), (void *) model->kbc_params);
    device_add(&ide_cmd640_pci_single_channel_legacy_only_device);
    device_add_params(&fdc37c6xx_device, (void *) (FDC37C665 | FDC37C6XX_IDE_SEC));

    if (gfxcard[0] == VID_INTERNAL)
        device_add(machine_get_vid_device(machine));

    return ret;
}

int
machine_at_g586vpmc_init(const machine_t *model)
{
    int ret;

    ret = bios_load_linear("roms/machines/g586vpmc/Vpm_c3.bin",
                           0x000e0000, 131072, 0);

    if (bios_only || !ret)
        return ret;

    machine_at_common_init(model);

    pci_init(PCI_CONFIG_TYPE_1);
    pci_register_slot(0x00, PCI_CARD_NORTHBRIDGE, 0, 0, 0, 0);
    pci_register_slot(0x01, PCI_CARD_SOUTHBRIDGE, 0, 0, 0, 0);
    pci_register_slot(0x02, PCI_CARD_NORMAL, 1, 2, 3, 4);
    pci_register_slot(0x04, PCI_CARD_NORMAL, 2, 3, 4, 1);
    pci_register_slot(0x06, PCI_CARD_NORMAL, 3, 4, 1, 2);
    pci_register_slot(0x08, PCI_CARD_NORMAL, 4, 1, 2, 3);
    pci_register_slot(0x0A, PCI_CARD_IDE, 0, 0, 0, 0);

    device_add(&vl82c59x_wildcat_device);
    device_add(&sst_flash_29ee010_device);
    device_add_params(machine_get_kbc_device(machine), (void *) model->kbc_params);
    device_add_params(&pc873xx_device, (void *) (PC87332 | PCX730X_398));
    device_add(&ide_cmd646_device);
    return ret;
}
