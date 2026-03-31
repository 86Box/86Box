/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          Implementation of Super Socket 7 machines.
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
#include <86box/spd.h>
#include <86box/hwm.h>
#include <86box/video.h>
#include "cpu.h"
#include <86box/machine.h>
#include <86box/sound.h>
#include <86box/snd_ac97.h>
#include <86box/clock.h>

/* ALi ALADDiN V */
int
machine_at_p5a_init(const machine_t *model)
{
    int ret;

    ret = bios_load_linear("roms/machines/p5a/1011.005",
                           0x000c0000, 262144, 0);

    if (bios_only || !ret)
        return ret;

    machine_at_common_init(model);

    pci_init(PCI_CONFIG_TYPE_1);
    pci_register_slot(0x00, PCI_CARD_NORTHBRIDGE,     0, 0, 0, 0);
    pci_register_slot(0x01, PCI_CARD_AGPBRIDGE,       1, 2, 0, 0);
    pci_register_slot(0x07, PCI_CARD_SOUTHBRIDGE,     1, 2, 3, 4);
    pci_register_slot(0x0F, PCI_CARD_SOUTHBRIDGE_IDE, 1, 2, 3, 4);
    pci_register_slot(0x03, PCI_CARD_SOUTHBRIDGE_PMU, 1, 2, 3, 4);
    pci_register_slot(0x02, PCI_CARD_SOUTHBRIDGE_USB, 1, 2, 3, 4);
    pci_register_slot(0x09, PCI_CARD_NORMAL,          4, 1, 2, 3);
    pci_register_slot(0x0A, PCI_CARD_NORMAL,          3, 4, 1, 2);
    pci_register_slot(0x0B, PCI_CARD_NORMAL,          2, 3, 4, 1);
    pci_register_slot(0x0C, PCI_CARD_NORMAL,          1, 2, 3, 4);
    pci_register_slot(0x0D, PCI_CARD_NORMAL,          4, 1, 2, 3);
    pci_register_slot(0x06, PCI_CARD_SOUND,           3, 4, 1, 2);

    device_add(&ali1541_device);
    device_add(&ali1543c_device); /* +0 */
    device_add(&sst_flash_39sf020_device);
    spd_register(SPD_TYPE_SDRAM, 0x7, 512);
    device_add(&w83781d_p5a_device); /* fans: Chassis, CPU, Power; temperatures: MB, unused, CPU */

    return ret;
}

int
machine_at_m579_init(const machine_t *model)
{
    int ret;

    ret = bios_load_linear("roms/machines/m579/MS6260S_Socket7_ALi_M1542_AMI.BIN",
                           0x000e0000, 131072, 0);

    if (bios_only || !ret)
        return ret;

    machine_at_common_init(model);

    pci_init(PCI_CONFIG_TYPE_1);
    pci_register_slot(0x00, PCI_CARD_NORTHBRIDGE,     0, 0, 0, 0);
    pci_register_slot(0x01, PCI_CARD_AGPBRIDGE,       1, 2, 0, 0);
    pci_register_slot(0x07, PCI_CARD_SOUTHBRIDGE,     1, 2, 3, 4);
    pci_register_slot(0x0F, PCI_CARD_SOUTHBRIDGE_IDE, 1, 2, 3, 4);
    pci_register_slot(0x03, PCI_CARD_SOUTHBRIDGE_PMU, 1, 2, 3, 4);
    pci_register_slot(0x02, PCI_CARD_SOUTHBRIDGE_USB, 1, 2, 3, 4);
    pci_register_slot(0x10, PCI_CARD_NORMAL,          3, 4, 1, 2);
    pci_register_slot(0x12, PCI_CARD_NORMAL,          2, 3, 4, 1);
    pci_register_slot(0x14, PCI_CARD_NORMAL,          1, 2, 3, 4);

    device_add(&ali1541_device);
    device_add(&ali1543c_device); /* +0 */
    device_add(&sst_flash_29ee010_device);
    spd_register(SPD_TYPE_SDRAM, 0x7, 512);

    return ret;
}

int
machine_at_gwlucas_init(const machine_t *model)
{
    int ret;

    ret = bios_load_linear("roms/machines/gwlucas/gw2kboot.rom",
                           0x000c0000, 262144, 0);

    if (bios_only || !ret)
        return ret;

    machine_at_common_init(model);

    pci_init(PCI_CONFIG_TYPE_1);
    pci_register_slot(0x00, PCI_CARD_NORTHBRIDGE,     0, 0, 0, 0);
    pci_register_slot(0x01, PCI_CARD_AGPBRIDGE,       1, 2, 0, 0);
    pci_register_slot(0x07, PCI_CARD_SOUTHBRIDGE,     1, 2, 3, 4);
    pci_register_slot(0x0E, PCI_CARD_SOUTHBRIDGE_IDE, 1, 2, 3, 4);
    pci_register_slot(0x03, PCI_CARD_SOUTHBRIDGE_PMU, 1, 2, 3, 4);
    pci_register_slot(0x02, PCI_CARD_SOUTHBRIDGE_USB, 1, 2, 3, 4);
    pci_register_slot(0x0F, PCI_CARD_SOUND,           1, 2, 3, 4); // ES1373
    pci_register_slot(0x14, PCI_CARD_NORMAL,          2, 3, 4, 1);
    pci_register_slot(0x12, PCI_CARD_NORMAL,          3, 4, 1, 2);
    pci_register_slot(0x10, PCI_CARD_NORMAL,          4, 1, 2, 3);

    device_add(&ali1541_device);
    device_add(&ali1543c_device); /* +0 */
    device_add(&sst_flash_39sf020_device);
    spd_register(SPD_TYPE_SDRAM, 0x7, 512);

    if (sound_card_current[0] == SOUND_INTERNAL) {
        device_add(machine_get_snd_device(machine));
        device_add(&cs4297_device);
    }

    return ret;
}

int
machine_at_5aa_init(const machine_t *model)
{
    int ret;

    ret = bios_load_linear("roms/machines/5aa/GA-5AA.F7b",
                           0x000e0000, 131072, 0);

    if (bios_only || !ret)
        return ret;

    machine_at_common_init(model);

    pci_init(PCI_CONFIG_TYPE_1);
    pci_register_slot(0x00, PCI_CARD_NORTHBRIDGE,     0, 0, 0, 0);
    pci_register_slot(0x01, PCI_CARD_AGPBRIDGE,       1, 2, 0, 0);
    pci_register_slot(0x07, PCI_CARD_SOUTHBRIDGE,     1, 2, 3, 4);
    pci_register_slot(0x0F, PCI_CARD_SOUTHBRIDGE_IDE, 1, 2, 3, 4);
    pci_register_slot(0x03, PCI_CARD_SOUTHBRIDGE_PMU, 1, 2, 3, 4);
    pci_register_slot(0x02, PCI_CARD_SOUTHBRIDGE_USB, 1, 2, 3, 4);
    pci_register_slot(0x08, PCI_CARD_NORMAL,          1, 2, 3, 4);
    pci_register_slot(0x09, PCI_CARD_NORMAL,          2, 3, 4, 1);
    pci_register_slot(0x0A, PCI_CARD_NORMAL,          3, 4, 1, 2);

    device_add(&ali1541_device);
    device_add(&ali1543c_device); /* +0 */
    device_add(&sst_flash_29ee010_device);
    spd_register(SPD_TYPE_SDRAM, 0x7, 512);

    return ret;
}

static const device_config_t g5x_config[] = {
    // clang-format off
    {
        .name           = "bios",
        .description    = "BIOS Version",
        .type           = CONFIG_BIOS,
        .default_string = "5ax",
        .default_int    = 0,
        .file_filter    = NULL,
        .spinner        = { 0 },
        .selection      = { { 0 } },
        .bios           = {
            {
                .name          = "Award Modular BIOS v4.51PG - Revision F4",
                .internal_name = "5ax",
                .bios_type     = BIOS_NORMAL,
                .files_no      = 1,
                .local         = 0,
                .size          = 131072,
                .files         = { "roms/machines/5ax/5AX.F4", "" }
            },
            {
                .name          = "Phoenix - AwardBIOS v6.00PG - Release 4.1 (by eSupport)",
                .internal_name = "5ax_600pg",
                .bios_type     = BIOS_NORMAL,
                .files_no      = 1,
                .local         = 0,
                .size          = 131072,
                .files         = { "roms/machines/5ax/6Z5KKG09.bin", "" }
            },
            { .files_no = 0 }
        }
    },
    { .name = "", .description = "", .type = CONFIG_END }
    // clang-format on
};

const device_t g5x_device = {
    .name          = "Gigabyte GA-5AX",
    .internal_name = "g5x_device",
    .flags         = 0,
    .local         = 0,
    .init          = NULL,
    .close         = NULL,
    .reset         = NULL,
    .available     = NULL,
    .speed_changed = NULL,
    .force_redraw  = NULL,
    .config        = g5x_config
};

int
machine_at_g5x_init(const machine_t *model)
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
    pci_register_slot(0x00, PCI_CARD_NORTHBRIDGE,     0, 0, 0, 0);
    pci_register_slot(0x01, PCI_CARD_AGPBRIDGE,       1, 2, 0, 0);
    pci_register_slot(0x07, PCI_CARD_SOUTHBRIDGE,     1, 2, 3, 4);
    pci_register_slot(0x0F, PCI_CARD_SOUTHBRIDGE_IDE, 1, 2, 3, 4);
    pci_register_slot(0x03, PCI_CARD_SOUTHBRIDGE_PMU, 1, 2, 3, 4);
    pci_register_slot(0x02, PCI_CARD_SOUTHBRIDGE_USB, 1, 2, 3, 4);
    pci_register_slot(0x08, PCI_CARD_NORMAL,          1, 2, 3, 4);
    pci_register_slot(0x09, PCI_CARD_NORMAL,          2, 3, 4, 1);
    pci_register_slot(0x0A, PCI_CARD_NORMAL,          3, 4, 1, 2);
    pci_register_slot(0x0B, PCI_CARD_NORMAL,          4, 1, 2, 3);
    pci_register_slot(0x0C, PCI_CARD_NORMAL,          1, 2, 3, 4);

    device_add(&ali1541_device);
    device_add(&ali1543c_device); /* +0 */
    device_add(&sst_flash_29ee010_device);
    spd_register(SPD_TYPE_SDRAM, 0x7, 512);

    return ret;
}

/* VIA MVP3 */
int
machine_at_ax59pro_init(const machine_t *model)
{
    int ret;

    ret = bios_load_linear("roms/machines/ax59pro/AX59P236.BIN",
                           0x000c0000, 262144, 0);

    if (bios_only || !ret)
        return ret;

    machine_at_common_init(model);

    pci_init(PCI_CONFIG_TYPE_1);
    pci_register_slot(0x00, PCI_CARD_NORTHBRIDGE, 0, 0, 0, 0);
    pci_register_slot(0x07, PCI_CARD_SOUTHBRIDGE, 1, 2, 0, 0);
    pci_register_slot(0x09, PCI_CARD_NORMAL,      4, 1, 2, 3);
    pci_register_slot(0x0A, PCI_CARD_NORMAL,      3, 4, 1, 2);
    pci_register_slot(0x0B, PCI_CARD_NORMAL,      2, 3, 4, 1);
    pci_register_slot(0x0C, PCI_CARD_NORMAL,      1, 2, 3, 4);
    pci_register_slot(0x01, PCI_CARD_AGPBRIDGE,   1, 2, 3, 4);

    device_add(&via_mvp3_device);
    device_add(&via_vt82c586b_device);
    device_add_params(&w83877_device, (void *) (W83877TF | W83877_250));
    device_add(&sst_flash_39sf020_device);
    spd_register(SPD_TYPE_SDRAM, 0x7, 256);

    return ret;
}

static const device_config_t delhi3_config[] = {
    // clang-format off
    {
        .name           = "bios",
        .description    = "BIOS Version",
        .type           = CONFIG_BIOS,
        .default_string = "delhi3",
        .default_int    = 0,
        .file_filter    = NULL,
        .spinner        = { 0 },
        .selection      = { { 0 } },
        .bios           = {
            {
                .name          = "AMIBIOS 6 (071595) - Revision 1.01",
                .internal_name = "delhi3_nonoem",
                .bios_type     = BIOS_NORMAL,
                .files_no      = 1,
                .local         = 0,
                .size          = 262144,
                .files         = { "roms/machines/delhi3/DELHI3_nonoem.ROM", "" }
            },
            {
                .name          = "AMIBIOS 6 (071595) - Revision 1.20 (eMachines eTower 3__k)",
                .internal_name = "delhi3",
                .bios_type     = BIOS_NORMAL,
                .files_no      = 1,
                .local         = 0,
                .size          = 262144,
                .files         = { "roms/machines/delhi3/DELHI3.ROM", "" }
            },
            { .files_no = 0 }
        }
    },
    { .name = "", .description = "", .type = CONFIG_END }
    // clang-format on
};

const device_t delhi3_device = {
    .name          = "TriGem Delhi-III",
    .internal_name = "delhi3_device",
    .flags         = 0,
    .local         = 0,
    .init          = NULL,
    .close         = NULL,
    .reset         = NULL,
    .available     = NULL,
    .speed_changed = NULL,
    .force_redraw  = NULL,
    .config        = delhi3_config
};

int
machine_at_delhi3_init(const machine_t *model)
{
    int         ret = 0;
    const char *fn;

    /* No ROMs available */
    if (!device_available(model->device))
        return ret;

    device_context(model->device);
    fn  = device_get_bios_file(machine_get_device(machine), device_get_config_bios("bios"), 0);
    ret = bios_load_linear(fn, 0x000c0000, 262144, 0);
    device_context_restore();

    machine_at_common_init(model);

    pci_init(PCI_CONFIG_TYPE_1);
    pci_register_slot(0x00, PCI_CARD_NORTHBRIDGE, 0, 0, 0, 0);
    pci_register_slot(0x07, PCI_CARD_SOUTHBRIDGE, 1, 2, 3, 4);
    pci_register_slot(0x13, PCI_CARD_NORMAL,      1, 2, 3, 4);
    pci_register_slot(0x12, PCI_CARD_NORMAL,      2, 3, 4, 1);

    device_add(&via_mvp3_device);
    device_add(&via_vt82c596a_device);
    device_add_params(&w83877_device, (void *) (W83877TF | W83877_250));
    device_add(&sst_flash_39sf020_device);
    spd_register(SPD_TYPE_SDRAM, 0x3, 256);

    if ((sound_card_current[0] == SOUND_INTERNAL) && machine_get_snd_device(machine))
        device_add(machine_get_snd_device(machine));

    return ret;
}

int
machine_at_mvp3_init(const machine_t *model)
{
    int ret;

    ret = bios_load_linear("roms/machines/ficva503p/je4333.bin",
                           0x000e0000, 131072, 0);

    if (bios_only || !ret)
        return ret;

    machine_at_common_init(model);

    pci_init(PCI_CONFIG_TYPE_1);
    pci_register_slot(0x00, PCI_CARD_NORTHBRIDGE, 0, 0, 0, 0);
    pci_register_slot(0x07, PCI_CARD_SOUTHBRIDGE, 1, 2, 3, 4);
    pci_register_slot(0x08, PCI_CARD_NORMAL,      1, 2, 3, 4);
    pci_register_slot(0x09, PCI_CARD_NORMAL,      2, 3, 4, 1);
    pci_register_slot(0x0A, PCI_CARD_NORMAL,      3, 4, 1, 2);
    pci_register_slot(0x01, PCI_CARD_AGPBRIDGE,   1, 2, 3, 4);

    device_add(&via_mvp3_device);
    device_add(&via_vt82c586b_device);
    device_add_params(&w83877_device, (void *) (W83877TF | W83877_3F0));
    device_add(&sst_flash_39sf010_device);
    spd_register(SPD_TYPE_SDRAM, 0x3, 256);

    return ret;
}

int
machine_at_ficva503a_init(const machine_t *model)
{
    int ret;

    ret = bios_load_linear("roms/machines/ficva503a/jn4116.bin",
                           0x000c0000, 262144, 0);

    if (bios_only || !ret)
        return ret;

    machine_at_common_init(model);

    pci_init(PCI_CONFIG_TYPE_1);
    pci_register_slot(0x00, PCI_CARD_NORTHBRIDGE, 0, 0, 0, 0);
    pci_register_slot(0x07, PCI_CARD_SOUTHBRIDGE, 1, 2, 3, 4);
    pci_register_slot(0x08, PCI_CARD_NORMAL,      1, 2, 3, 4);
    pci_register_slot(0x09, PCI_CARD_NORMAL,      2, 3, 4, 1);
    pci_register_slot(0x0A, PCI_CARD_NORMAL,      3, 4, 1, 2);
    pci_register_slot(0x0B, PCI_CARD_NORMAL,      4, 1, 2, 3);
    pci_register_slot(0x01, PCI_CARD_AGPBRIDGE,   1, 2, 3, 4);

    device_add(&via_mvp3_device);
    device_add(&via_vt82c686a_device); /* fans: CPU1, Chassis; temperatures: CPU, System, unused */
    device_add(&sst_flash_39sf020_device);
    spd_register(SPD_TYPE_SDRAM, 0x7, 256);
    hwm_values.temperatures[0] += 2; /* CPU offset */
    hwm_values.temperatures[1] += 2; /* System offset */
    hwm_values.temperatures[2] = 0;  /* unused */

    if (sound_card_current[0] == SOUND_INTERNAL)
        device_add(&wm9701a_device); /* on daughtercard */

    return ret;
}

int
machine_at_5emapro_init(const machine_t *model)
{
    int ret;

    ret = bios_load_linear("roms/machines/5emapro/5emo1aa2.bin",
                           0x000e0000, 131072, 0);

    if (bios_only || !ret)
        return ret;

    machine_at_common_init(model);

    pci_init(PCI_CONFIG_TYPE_1);
    pci_register_slot(0x00, PCI_CARD_NORTHBRIDGE, 0, 0, 0, 0);
    pci_register_slot(0x07, PCI_CARD_SOUTHBRIDGE, 0, 0, 3, 4);
    pci_register_slot(0x08, PCI_CARD_NORMAL,      1, 2, 3, 4);
    pci_register_slot(0x09, PCI_CARD_NORMAL,      2, 3, 4, 1);
    pci_register_slot(0x0A, PCI_CARD_NORMAL,      3, 4, 1, 2);
    pci_register_slot(0x0B, PCI_CARD_NORMAL,      4, 1, 2, 3);
    pci_register_slot(0x0C, PCI_CARD_NORMAL,      4, 1, 2, 3);
    pci_register_slot(0x01, PCI_CARD_AGPBRIDGE,   1, 2, 3, 4);

    device_add(&via_mvp3_device); /* Rebranded as EQ82C6638 */
    device_add(&via_vt82c686a_device);
    device_add(&sst_flash_39sf010_device);
    spd_register(SPD_TYPE_SDRAM, 0x7, 256);
    device_add(&via_vt82c686_hwm_device); /* fans: CPU1, Chassis; temperatures: CPU, System, unused */
    hwm_values.temperatures[0] += 2;      /* CPU offset */
    hwm_values.temperatures[1] += 2;      /* System offset */
    hwm_values.temperatures[2] = 0;       /* unused */

    return ret;
}

int
machine_at_k6bv3p_a_init(const machine_t *model)
{
    int ret;

    ret = bios_load_linear("roms/machines/k6bv3p_a/KB3A0805.BIN",
                           0x000e0000, 131072, 0);

    if (bios_only || !ret)
        return ret;

    machine_at_common_init(model);

    pci_init(PCI_CONFIG_TYPE_1);
    pci_register_slot(0x00, PCI_CARD_NORTHBRIDGE, 1, 2, 3, 5);
    pci_register_slot(0x07, PCI_CARD_SOUTHBRIDGE, 0, 0, 0, 5);
    pci_register_slot(0x08, PCI_CARD_NORMAL,      1, 2, 3, 5);
    pci_register_slot(0x09, PCI_CARD_NORMAL,      2, 3, 5, 1);
    pci_register_slot(0x0A, PCI_CARD_NORMAL,      3, 5, 1, 2);
    pci_register_slot(0x0B, PCI_CARD_NORMAL,      5, 1, 2, 3);
    pci_register_slot(0x01, PCI_CARD_AGPBRIDGE,   1, 2, 3, 5);

    device_add(&via_mvp3_device);
    device_add(&via_vt82c586b_device);
    device_add_params(&fdc37c669_device, (void *) 0); /* jmi2k: what's that param? */
    device_add(&winbond_flash_w29c011a_device);
    spd_register(SPD_TYPE_SDRAM, 0x7, 256);

    return ret;
}

/* SiS 5591 */
int
machine_at_5sg100_init(const machine_t *model)
{
    int ret;

    ret = bios_load_linear("roms/machines/5sg100/5sg.20g",
                           0x000e0000, 131072, 0);

    if (bios_only || !ret)
        return ret;

    machine_at_common_init(model);

    pci_init(PCI_CONFIG_TYPE_1);
    pci_register_slot(0x00, PCI_CARD_NORTHBRIDGE, 0, 0, 0, 0);
    pci_register_slot(0x01, PCI_CARD_SOUTHBRIDGE, 0, 0, 0, 0);
    pci_register_slot(0x09, PCI_CARD_NORMAL,      1, 2, 3, 4);
    pci_register_slot(0x0B, PCI_CARD_NORMAL,      2, 3, 4, 1);
    pci_register_slot(0x0D, PCI_CARD_NORMAL,      3, 4, 1, 2);
    pci_register_slot(0x0F, PCI_CARD_NORMAL,      4, 1, 2, 3);
    pci_register_slot(0x02, PCI_CARD_AGPBRIDGE,   0, 0, 0, 0);

    device_add(&sis_5591_1997_device);
    device_add_params(&w83877_device, (void *) (W83877TF | W83877_3F0));
    device_add(&sst_flash_29ee010_device);

    return ret;
}
