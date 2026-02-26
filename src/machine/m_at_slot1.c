/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          Implementation of Slot 1 machines.
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
#include <86box/device.h>
#include <86box/pci.h>
#include <86box/chipset.h>
#include <86box/hdc.h>
#include <86box/hdc_ide.h>
#include <86box/keyboard.h>
#include <86box/flash.h>
#include <86box/sio.h>
#include <86box/hwm.h>
#include <86box/spd.h>
#include <86box/video.h>
#include "cpu.h"
#include <86box/machine.h>
#include <86box/sound.h>
#include <86box/clock.h>
#include <86box/snd_ac97.h>

/* ALi ALADDiN-PRO II */
int
machine_at_m729_init(const machine_t *model)
{
    int ret;

    ret = bios_load_linear("roms/machines/m729/M729NEW.BIN",
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
    pci_register_slot(0x14, PCI_CARD_NORMAL,          1, 2, 3, 4);
    pci_register_slot(0x12, PCI_CARD_NORMAL,          2, 3, 4, 1);
    pci_register_slot(0x10, PCI_CARD_NORMAL,          3, 4, 1, 2);

    device_add(&ali1621_device);
    device_add(&ali1543c_device); /* +0 */
    device_add(&winbond_flash_w29c010_device);
    spd_register(SPD_TYPE_SDRAM, 0x7, 512);

    return ret;
}

/* i440FX */
int
machine_at_acerv62x_init(const machine_t *model)
{
    int ret;

    ret = bios_load_linear("roms/machines/acerv62x/v62xc0s1.bin",
                           0x000c0000, 262144, 0);

    if (bios_only || !ret)
        return ret;

    machine_at_common_init(model);

    pci_init(PCI_CONFIG_TYPE_1);
    pci_register_slot(0x00, PCI_CARD_NORTHBRIDGE, 0, 0, 0, 0);
    pci_register_slot(0x07, PCI_CARD_SOUTHBRIDGE, 5, 0, 0, 4);
    pci_register_slot(0x12, PCI_CARD_NORMAL,      4, 1, 2, 3);
    pci_register_slot(0x10, PCI_CARD_NORMAL,      3, 4, 1, 2);
    pci_register_slot(0x0E, PCI_CARD_NORMAL,      1, 2, 3, 4);
    pci_register_slot(0x0F, PCI_CARD_NORMAL,      2, 3, 4, 1);
    pci_register_slot(0x0D, PCI_CARD_NORMAL,      2, 3, 4, 1);

    device_add(&i440fx_device);
    device_add(&piix3_device);
    device_add_params(&fdc37c93x_device, (void *) (FDC37XXX5 | FDC37C93X_APM));
    device_add(&sst_flash_29ee020_device);
    spd_register(SPD_TYPE_SDRAM, 0x7, 128);

    return ret;
}

int
machine_at_p65up5_cpknd_init(const machine_t *model)
{
    int ret;

    ret = bios_load_linear("roms/machines/p65up5/NDKN0218.AWD",
                           0x000e0000, 131072, 0);

    if (bios_only || !ret)
        return ret;

    machine_at_p65up5_common_init(model, &i440fx_device);

    return ret;
}

int
machine_at_kn97_init(const machine_t *model)
{
    int ret;

    ret = bios_load_linear("roms/machines/kn97/0116I.001",
                           0x000e0000, 131072, 0);

    if (bios_only || !ret)
        return ret;

    machine_at_common_init(model);

    pci_init(PCI_CONFIG_TYPE_1);
    pci_register_slot(0x00, PCI_CARD_NORTHBRIDGE, 0, 0, 0, 0);
    pci_register_slot(0x01, PCI_CARD_SOUTHBRIDGE, 0, 0, 0, 0);
    pci_register_slot(0x09, PCI_CARD_NORMAL,      4, 1, 2, 3);
    pci_register_slot(0x0A, PCI_CARD_NORMAL,      3, 4, 1, 2);
    pci_register_slot(0x0B, PCI_CARD_NORMAL,      2, 3, 4, 1);
    pci_register_slot(0x0C, PCI_CARD_NORMAL,      1, 2, 3, 4);
    pci_register_slot(0x0D, PCI_CARD_NORMAL,      4, 1, 2, 3);

    device_add(&i440fx_device);
    device_add(&piix3_device);
    device_add_params(machine_get_kbc_device(machine), (void *) model->kbc_params);
    device_add_params(&w83877_device, (void *) (W83877F | W83877_3F0));
    device_add(&intel_flash_bxt_device);
    device_add(&lm78_device); /* fans: Chassis, CPU, Power; temperature: MB */
    for (uint8_t i = 0; i < 3; i++)
        hwm_values.fans[i] *= 2; /* BIOS reports fans with the wrong divisor for some reason */

    return ret;
}

/* i440LX */
static const device_config_t lx6_config[] = {
    // clang-format off
    {
        .name           = "bios",
        .description    = "BIOS Version",
        .type           = CONFIG_BIOS,
        .default_string = "lx6",
        .default_int    = 0,
        .file_filter    = NULL,
        .spinner        = { 0 },
        .selection      = { { 0 } },
        .bios           = {
            {
                .name          = "Award Modular BIOS v4.51PG - Revision LY",
                .internal_name = "lx6",
                .bios_type     = BIOS_NORMAL, 
                .files_no      = 1,
                .local         = 0,
                .size          = 131072,
                .files         = { "roms/machines/lx6/LX6C_LY.bin", "" }
            },
            {
                .name          = "Award Modular BIOS v4.51PG - Revision PZ (Beta)",
                .internal_name = "lx6_pz",
                .bios_type     = BIOS_NORMAL, 
                .files_no      = 1,
                .local         = 0,
                .size          = 131072,
                .files         = { "roms/machines/lx6/LX6C_PZ.B00", "" }
            },
            { .files_no = 0 }
        },
    },
    { .name = "", .description = "", .type = CONFIG_END }
    // clang-format on
};

const device_t lx6_device = {
    .name          = "ABIT AB-LX6",
    .internal_name = "lx6_device",
    .flags         = 0,
    .local         = 0,
    .init          = NULL,
    .close         = NULL,
    .reset         = NULL,
    .available     = NULL,
    .speed_changed = NULL,
    .force_redraw  = NULL,
    .config        = lx6_config
};

int
machine_at_lx6_init(const machine_t *model)
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
    pci_register_slot(0x07, PCI_CARD_SOUTHBRIDGE, 1, 2, 3, 4);
    pci_register_slot(0x09, PCI_CARD_NORMAL,      4, 1, 2, 3);
    pci_register_slot(0x0B, PCI_CARD_NORMAL,      3, 4, 1, 2);
    pci_register_slot(0x0D, PCI_CARD_NORMAL,      2, 3, 4, 1);
    pci_register_slot(0x0F, PCI_CARD_NORMAL,      1, 2, 3, 4);
    pci_register_slot(0x01, PCI_CARD_AGPBRIDGE,   1, 2, 3, 4);

    device_add(&i440lx_device);
    device_add(&piix4e_device);
    device_add_params(&w83977_device, (void *) (W83977TF | W83977_AMI | W83977_NO_NVR));
    device_add(&sst_flash_29ee010_device);
    spd_register(SPD_TYPE_SDRAM, 0xF, 256);

    return ret;
}

int
machine_at_optiplexgxa_init(const machine_t *model)
{
    int ret;

    ret = bios_load_linear("roms/machines/optiplexgxa/DELL.ROM",
                           0x000c0000, 262144, 0);

    if (bios_only || !ret)
        return ret;

    machine_at_common_init(model);

    pci_init(PCI_CONFIG_TYPE_1);
    pci_register_slot(0x07, PCI_CARD_SOUTHBRIDGE, 0, 0, 0, 4);
    pci_register_slot(0x00, PCI_CARD_NORTHBRIDGE, 1, 2, 3, 4);
    pci_register_slot(0x01, PCI_CARD_AGPBRIDGE,   1, 2, 3, 4);
    pci_register_slot(0x11, PCI_CARD_NETWORK,     4, 0, 0, 0);
    pci_register_slot(0x0E, PCI_CARD_NORMAL,      3, 4, 2, 1);
    pci_register_slot(0x0D, PCI_CARD_NORMAL,      2, 1, 3, 4);
    pci_register_slot(0x0F, PCI_CARD_BRIDGE,      0, 0, 0, 0);

    if (sound_card_current[0] == SOUND_INTERNAL)
        device_add(machine_get_snd_device(machine));

    device_add(&i440lx_device);
    device_add(&piix4_device);
    machine_at_optiplex_21152_init();
    device_add_params(&pc87307_device, (void *) (PCX730X_PHOENIX_42 | PCX7307_PC87307));
    device_add(&intel_flash_bxt_device);
    spd_register(SPD_TYPE_SDRAM, 0x7, 256);

    return ret;
}

int
machine_at_spitfire_init(const machine_t *model)
{
    int ret;

    ret = bios_load_linear("roms/machines/spitfire/SPIHM.02",
                           0x000c0000, 262144, 0);

    if (bios_only || !ret)
        return ret;

    machine_at_common_init(model);

    pci_init(PCI_CONFIG_TYPE_1);
    pci_register_slot(0x00, PCI_CARD_NORTHBRIDGE, 0, 0, 0, 0);
    pci_register_slot(0x08, PCI_CARD_VIDEO,       1, 2, 0, 0);
    pci_register_slot(0x12, PCI_CARD_NORMAL,      1, 2, 3, 4);
    pci_register_slot(0x11, PCI_CARD_NORMAL,      2, 3, 4, 1);
    pci_register_slot(0x10, PCI_CARD_NORMAL,      3, 4, 1, 2);
    pci_register_slot(0x07, PCI_CARD_SOUTHBRIDGE, 0, 0, 0, 0);
    pci_register_slot(0x01, PCI_CARD_AGPBRIDGE,   1, 2, 3, 4);

    device_add(&i440lx_device);
    device_add(&piix4e_device);
    device_add_params(&fdc37c93x_device, (void *) (FDC37XXX5 | FDC37C93X_NORMAL | FDC37C93X_NO_NVR));
    device_add(&intel_flash_bxt_device);
    spd_register(SPD_TYPE_SDRAM, 0xF, 256);
    device_add(&lm78_device); /* no reporting in BIOS */

    return ret;
}

static const device_config_t ms6117_config[] = {
    // clang-format off
    {
        .name           = "bios",
        .description    = "BIOS Version",
        .type           = CONFIG_BIOS,
        .default_string = "ms6117a",
        .default_int    = 0,
        .file_filter    = NULL,
        .spinner        = { 0 },
        .selection      = { { 0 } },
        .bios           = {
            {
                .name          = "AMIBIOS 6 (071595) - Revision 2.0",
                .internal_name = "ms6117a",
                .bios_type     = BIOS_NORMAL,
                .files_no      = 1,
                .local         = 0,
                .size          = 131072,
                .files         = { "roms/machines/ms6117/A617MS20.ROM", "" }
            },
            {
                .name          = "AMIBIOS 6 (071595) - Revision 1.10j (Japanese)",
                .internal_name = "ms6117aj",
                .bios_type     = BIOS_NORMAL,
                .files_no      = 1,
                .local         = 0,
                .size          = 131072,
                .files         = { "roms/machines/ms6117/A617J110.ROM", "" }
            },
            {
                .name          = "AMIBIOS 6 (071595) - Revision 3.11sc (Simplified Chinese)",
                .internal_name = "ms6117asc",
                .bios_type     = BIOS_NORMAL,
                .files_no      = 1,
                .local         = 0,
                .size          = 131072,
                .files         = { "roms/machines/ms6117/A617C311.ROM", "" }
            },
            {
                .name          = "AMIBIOS 6 (071595) - Revision 4.10tc (Traditional Chinese)",
                .internal_name = "ms6117atc",
                .bios_type     = BIOS_NORMAL,
                .files_no      = 1,
                .local         = 0,
                .size          = 131072,
                .files         = { "roms/machines/ms6117/A617C410.ROM", "" }
            },
            {
                .name          = "Award Modular BIOS v4.51PG - Revision 3.2",
                .internal_name = "ms6117w",
                .bios_type     = BIOS_NORMAL,
                .files_no      = 1,
                .local         = 0,
                .size          = 262144,
                .files         = { "roms/machines/ms6117/W617MS32.BIN", "" }
            },
            {
                .name          = "Award Modular BIOS v4.51PG - Revision 3.2 [Patched for larger drives]",
                .internal_name = "ms6117wp",
                .bios_type     = BIOS_NORMAL,
                .files_no      = 1,
                .local         = 0,
                .size          = 262144,
                .files         = { "roms/machines/ms6117/611732x_patched.BIN", "" }
            },
            {
                .name          = "Award Modular BIOS v4.51PG - Revision 1.4 (Fujitsu-Siemens OEM)",
                .internal_name = "ms6117wfs",
                .bios_type     = BIOS_NORMAL,
                .files_no      = 1,
                .local         = 0,
                .size          = 131072,
                .files         = { "roms/machines/ms6117/AWARD 1.04 .BIN", "" }
            },
            {
                .name          = "Award Modular BIOS v4.51PG - Revision 1.02 (LG IBM Multinet x7E)",
                .internal_name = "ms6117wlg",
                .bios_type     = BIOS_NORMAL,
                .files_no      = 1,
                .local         = 0,
                .size          = 262144,
                .files         = { "roms/machines/ms6117/BIOS.BIN", "" }
            },
            {
                .name          = "Award Modular BIOS v4.51PG - Revision 1.5 (Viglen Vig67M)",
                .internal_name = "ms6117wvi",
                .bios_type     = BIOS_NORMAL,
                .files_no      = 1,
                .local         = 0,
                .size          = 131072,
                .files         = { "roms/machines/ms6117/w617v115.BIN", "" }
            },
            { .files_no = 0 }
        }
    },
    { .name = "", .description = "", .type = CONFIG_END }
    // clang-format on
};

const device_t ms6117_device = {
    .name          = "MSI MS-6117",
    .internal_name = "ms6117",
    .flags         = 0,
    .local         = 0,
    .init          = NULL,
    .close         = NULL,
    .reset         = NULL,
    .available     = NULL,
    .speed_changed = NULL,
    .force_redraw  = NULL,
    .config        = ms6117_config
};

int
machine_at_ms6117_init(const machine_t *model)
{
    int         ret = 0;
    const char *fn;

    /* No ROMs available */
    if (!device_available(model->device))
        return ret;

    device_context(model->device);
    fn  = device_get_bios_file(machine_get_device(machine), device_get_config_bios("bios"), 0);
    if (!strcmp(fn, "roms/machines/ms6117/W617MS32.BIN") || !strcmp(fn, "roms/machines/ms6117/611732x_patched.BIN") || !strcmp(fn, "roms/machines/ms6117/BIOS.BIN"))
        ret = bios_load_linear(fn, 0x000c0000, 262144, 0);
    else
        ret = bios_load_linear(fn, 0x000e0000, 131072, 0);
    device_context_restore();

    if (bios_only || !ret)
        return ret;

    machine_at_common_init(model);

    pci_init(PCI_CONFIG_TYPE_1);
    pci_register_slot(0x00, PCI_CARD_NORTHBRIDGE, 0, 0, 0, 0);
    pci_register_slot(0x0E, PCI_CARD_NORMAL,      1, 2, 3, 4);
    pci_register_slot(0x0F, PCI_CARD_NORMAL,      2, 3, 4, 1);
    pci_register_slot(0x10, PCI_CARD_NORMAL,      3, 4, 1, 2);
    pci_register_slot(0x11, PCI_CARD_NORMAL,      4, 1, 2, 3);
    pci_register_slot(0x12, PCI_CARD_NORMAL,      1, 2, 3, 4);
    pci_register_slot(0x07, PCI_CARD_SOUTHBRIDGE, 1, 2, 3, 4);
    pci_register_slot(0x01, PCI_CARD_AGPBRIDGE,   1, 2, 3, 4);

    device_add(&i440lx_device);
    device_add(&piix4e_device);
    device_add_params(&w83977_device, (void *) (W83977TF | W83977_AMI | W83977_NO_NVR));

    if (!strcmp(fn, "roms/machines/ms6117/W617MS32.BIN") || !strcmp(fn, "roms/machines/ms6117/611732x_patched.BIN") || !strcmp(fn, "roms/machines/ms6117/BIOS.BIN"))
        device_add(&winbond_flash_w29c020_device); /* assumed */
    else
        device_add(&winbond_flash_w29c011a_device);
    
    spd_register(SPD_TYPE_SDRAM, 0xF, 256);

    return ret;
}

int
machine_at_ma30d_init(const machine_t *model)
{
    int ret;

    ret = bios_load_linear("roms/machines/ma30d/BIOS.ROM",
                           0x000c0000, 262144, 0);

    if (bios_only || !ret)
        return ret;

    machine_at_common_init(model);

    pci_init(PCI_CONFIG_TYPE_1);
    pci_register_slot(0x00, PCI_CARD_NORTHBRIDGE, 0, 0, 0, 0);
#ifdef UNKNOWN_SLOT
    pci_register_slot(0x0A, PCI_CARD_NETWORK,     2, 3, 4, 1); /* ???? device - GPIO? */
#endif
    pci_register_slot(0x14, PCI_CARD_NORMAL,      1, 2, 3, 4);
    pci_register_slot(0x12, PCI_CARD_NORMAL,      2, 3, 4, 1);
    pci_register_slot(0x10, PCI_CARD_NORMAL,      3, 4, 1, 2);
    pci_register_slot(0x0E, PCI_CARD_NORMAL,      4, 1, 2, 3);
    pci_register_slot(0x0C, PCI_CARD_NORMAL,      1, 2, 3, 4);
    pci_register_slot(0x07, PCI_CARD_SOUTHBRIDGE, 1, 2, 3, 4);
    pci_register_slot(0x01, PCI_CARD_AGPBRIDGE,   3, 0, 0, 0);

    device_add(&i440lx_device);
    device_add(&piix4e_device);
    device_add(&nec_mate_unk_device);
    device_add_params(&fdc37c67x_device, (void *) (FDC37XXX2 | FDC37XXXX_370));
    device_add(&intel_flash_bxt_device);
    spd_register(SPD_TYPE_SDRAM, 0x7, 256);

    return ret;
}

/* i440EX */
int
machine_at_brio83xx_init(const machine_t *model)
{
    int ret;

    ret = bios_load_linear("roms/machines/brio83xx/QHL0700.rom",
                           0x000c0000, 262144, 0);

    if (bios_only || !ret)
        return ret;

    machine_at_common_init(model);

    pci_init(PCI_CONFIG_TYPE_1);
    // Actual settings!
    pci_register_slot(0x00, PCI_CARD_NORTHBRIDGE, 1, 2, 3, 4); /* Onboard */
    pci_register_slot(0x01, PCI_CARD_AGPBRIDGE,   1, 2, 3, 4); /* Onboard */
    pci_register_slot(0x07, PCI_CARD_SOUTHBRIDGE, 1, 2, 3, 4); /* Onboard */
    pci_register_slot(0x0F, PCI_CARD_NORMAL,      1, 2, 3, 4); /* Slot 01 */
    pci_register_slot(0x10, PCI_CARD_NORMAL,      2, 3, 4, 1); /* Slot 02 */
    pci_register_slot(0x12, PCI_CARD_NORMAL,      3, 4, 1, 2); /* Slot 03 */
    pci_register_slot(0x14, PCI_CARD_VIDEO,       1, 2, 3, 4); /* Onboard */

    if (gfxcard[0] == VID_INTERNAL)
        device_add(&s3_trio64v2_dx_onboard_pci_device);

    device_add(&i440ex_device);
    device_add(&piix4_device);

    device_add_params(&fdc37c67x_device, (void *) (FDC37XXX5));

    /* Chip not quite confirmed, but this does operate fine. */
    device_add(&sst_flash_29ee020_device);

    spd_register(SPD_TYPE_SDRAM, 0x3, 256);

    return ret;
}

static const device_config_t como_config[] = {
    // clang-format off
    {
        .name           = "bios",
        .description    = "BIOS Version",
        .type           = CONFIG_BIOS,
        .default_string = "como",
        .default_int    = 0,
        .file_filter    = NULL,
        .spinner        = { 0 },
        .selection      = { { 0 } },
        .bios           = {
            {
                .name          = "AMIBIOS 6 (071595) - Revision 1.08 (Olivetti OEM)",
                .internal_name = "como_olivetti",
                .bios_type     = BIOS_NORMAL,
                .files_no      = 1,
                .local         = 0,
                .size          = 262144,
                .files         = { "roms/machines/como/COMO_Olivetti_OEM.ROM", "" }
            },
            {
                .name          = "AMIBIOS 6 (071595) - Revision 1.12 (eMachines OEM)",
                .internal_name = "como",
                .bios_type     = BIOS_NORMAL,
                .files_no      = 1,
                .local         = 0,
                .size          = 262144,
                .files         = { "roms/machines/como/COMO.ROM", "" }
            },
            { .files_no = 0 }
        }
    },
    { .name = "", .description = "", .type = CONFIG_END }
    // clang-format on
};

const device_t como_device = {
    .name          = "TriGem Como",
    .internal_name = "como_device",
    .flags         = 0,
    .local         = 0,
    .init          = NULL,
    .close         = NULL,
    .reset         = NULL,
    .available     = NULL,
    .speed_changed = NULL,
    .force_redraw  = NULL,
    .config        = como_config
};

int
machine_at_como_init(const machine_t *model)
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
    pci_register_slot(0x01, PCI_CARD_AGPBRIDGE,   1, 2, 3, 4);
    pci_register_slot(0x07, PCI_CARD_SOUTHBRIDGE, 0, 0, 0, 4);
    pci_register_slot(0x0B, PCI_CARD_NORMAL,      3, 4, 1, 2);
    pci_register_slot(0x12, PCI_CARD_NORMAL,      2, 3, 4, 1);
    pci_register_slot(0x13, PCI_CARD_NORMAL,      1, 2, 3, 4);

    device_add(&i440ex_device);
    device_add(&piix4e_device);
    device_add_params(&fdc37m60x_device, (void*)(FDC37XXX2 | FDC37C93X_NO_NVR | FDC37XXXX_370));
    device_add(&intel_flash_bxt_device);
    device_add(&lm78_device);

    if (sound_card_current[0] == SOUND_INTERNAL)
        device_add(&cs4235_onboard_device);

    return ret;
}

int
machine_at_p6i440e2_init(const machine_t *model)
{
    int ret;

    ret = bios_load_linear("roms/machines/p6i440e2/E2_v14sl.bin",
                           0x000e0000, 131072, 0);

    if (bios_only || !ret)
        return ret;

    machine_at_common_init(model);

    pci_init(PCI_CONFIG_TYPE_1);
    pci_register_slot(0x00, PCI_CARD_NORTHBRIDGE, 0, 0, 0, 0);
    pci_register_slot(0x07, PCI_CARD_SOUTHBRIDGE, 1, 2, 3, 4);
    pci_register_slot(0x09, PCI_CARD_NORMAL,      1, 2, 3, 4);
    pci_register_slot(0x0A, PCI_CARD_NORMAL,      2, 3, 4, 1);
    pci_register_slot(0x01, PCI_CARD_AGPBRIDGE,   1, 2, 3, 4);

    device_add(&i440ex_device);
    device_add(&piix4_device);
    device_add_params(&w83977_device, (void *) (W83977TF | W83977_AMI | W83977_NO_NVR));
    device_add(&sst_flash_29ee010_device);
    spd_register(SPD_TYPE_SDRAM, 0x03, 256);
    device_add(&w83781d_device);       /* fans: CPU, CHS, PS; temperatures: unused, CPU, System */
    hwm_values.temperatures[0] = 0;    /* unused */
    hwm_values.voltages[1]     = 1500; /* CPUVTT */

    return ret;
}

/* i440BX */
int
machine_at_bf6_init(const machine_t *model)
{
    int ret;

    ret = bios_load_linear("roms/machines/bf6/Beh_70.bin",
                           0x000c0000, 262144, 0);

    if (bios_only || !ret)
        return ret;

    machine_at_common_init(model);

    pci_init(PCI_CONFIG_TYPE_1);
    pci_register_slot(0x00, PCI_CARD_NORTHBRIDGE, 0, 0, 0, 0);
    pci_register_slot(0x07, PCI_CARD_SOUTHBRIDGE, 1, 2, 3, 4);
    pci_register_slot(0x08, PCI_CARD_NORMAL,      3, 4, 1, 2);
    pci_register_slot(0x09, PCI_CARD_NORMAL,      2, 1, 4, 3);
    pci_register_slot(0x0B, PCI_CARD_NORMAL,      4, 1, 2, 3);
    pci_register_slot(0x11, PCI_CARD_NORMAL,      1, 2, 3, 4);
    pci_register_slot(0x0D, PCI_CARD_NORMAL,      3, 4, 1, 2);
    pci_register_slot(0x0F, PCI_CARD_NORMAL,      2, 3, 4, 1);
    pci_register_slot(0x13, PCI_CARD_NORMAL,      3, 3, 1, 2);
    pci_register_slot(0x01, PCI_CARD_AGPBRIDGE,   1, 2, 3, 4);

    device_add(&i440bx_device);
    device_add(&piix4e_device);
    device_add_params(&w83977_device, (void *) (W83977EF | W83977_AMI | W83977_NO_NVR));
    device_add(&sst_flash_39sf020_device);
    spd_register(SPD_TYPE_SDRAM, 0x7, 256);

    return ret;
}

static const device_config_t bx6_config[] = {
    // clang-format off
    {
        .name           = "bios",
        .description    = "BIOS Version",
        .type           = CONFIG_BIOS,
        .default_string = "bx6",
        .default_int    = 0,
        .file_filter    = NULL,
        .spinner        = { 0 },
        .selection      = { { 0 } },
        .bios           = {
            {
                .name          = "Award Modular BIOS v4.51PG - Revision EG",
                .internal_name = "bx6",
                .bios_type     = BIOS_NORMAL,
                .files_no      = 1,
                .local         = 0,
                .size          = 131072,
                .files         = { "roms/machines/bx6/BX6_EG.BIN", "" }
            },
            {
                .name          = "Award Modular BIOS v4.51PG - Revision CW",
                .internal_name = "bx6_CW",
                .bios_type     = BIOS_NORMAL,
                .files_no      = 1,
                .local         = 0,
                .size          = 131072,
                .files         = { "roms/machines/bx6/BX6_CW.bin", "" }
            },
            {
                .name          = "Award Modular BIOS v4.51PG - Revision GQ",
                .internal_name = "bx6_GQ",
                .bios_type     = BIOS_NORMAL,
                .files_no      = 1,
                .local         = 0,
                .size          = 131072,
                .files         = { "roms/machines/bx6/BX6_GQ.bin", "" }
            },
            {
                .name          = "Award Modular BIOS v4.51PG - Revision JL",
                .internal_name = "bx6_JL",
                .bios_type     = BIOS_NORMAL,
                .files_no      = 1,
                .local         = 0,
                .size          = 131072,
                .files         = { "roms/machines/bx6/BX6_JL.bin", "" }
            },
            {
                .name          = "Award Modular BIOS v4.51PG - Revision QS",
                .internal_name = "bx6_qs",
                .bios_type     = BIOS_NORMAL,
                .files_no      = 1,
                .local         = 0,
                .size          = 131072,
                .files         = { "roms/machines/bx6/BX6_QS.bin", "" }
            },
            { .files_no = 0 }
        }
    },
    { .name = "", .description = "", .type = CONFIG_END }
    // clang-format on
};

const device_t bx6_device = {
    .name          = "ABIT AB-BX6",
    .internal_name = "bx6_device",
    .flags         = 0,
    .local         = 0,
    .init          = NULL,
    .close         = NULL,
    .reset         = NULL,
    .available     = NULL,
    .speed_changed = NULL,
    .force_redraw  = NULL,
    .config        = bx6_config
};

int
machine_at_bx6_init(const machine_t *model)
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
    pci_register_slot(0x07, PCI_CARD_SOUTHBRIDGE, 1, 2, 3, 4);
    pci_register_slot(0x09, PCI_CARD_NORMAL,      4, 1, 2, 3);
    pci_register_slot(0x0B, PCI_CARD_NORMAL,      3, 4, 1, 2);
    pci_register_slot(0x0D, PCI_CARD_NORMAL,      2, 3, 4, 1);
    pci_register_slot(0x0F, PCI_CARD_NORMAL,      1, 2, 3, 4);
    pci_register_slot(0x01, PCI_CARD_AGPBRIDGE,   1, 2, 3, 4);

    device_add(&i440bx_device);
    device_add(&piix4e_device);
    device_add_params(&w83977_device, (void *) (W83977TF | W83977_AMI | W83977_NO_NVR));
    device_add(&sst_flash_29ee010_device);
    spd_register(SPD_TYPE_SDRAM, 0xF, 256);

    return ret;
}

int
machine_at_p2bls_init(const machine_t *model)
{
    int ret;

    ret = bios_load_linear("roms/machines/p2bls/1014ls.003",
                           0x000c0000, 262144, 0);

    if (bios_only || !ret)
        return ret;

    machine_at_common_init(model);

    pci_init(PCI_CONFIG_TYPE_1);
    pci_register_slot(0x00, PCI_CARD_NORTHBRIDGE, 0, 0, 0, 0);
    pci_register_slot(0x04, PCI_CARD_SOUTHBRIDGE, 1, 2, 3, 4);
    pci_register_slot(0x06, PCI_CARD_SCSI,        4, 1, 2, 3);
    pci_register_slot(0x07, PCI_CARD_NETWORK,     3, 4, 1, 2);
    pci_register_slot(0x0B, PCI_CARD_NORMAL,      2, 3, 4, 1);
    pci_register_slot(0x0C, PCI_CARD_NORMAL,      1, 2, 3, 4);
    pci_register_slot(0x09, PCI_CARD_NORMAL,      4, 1, 2, 3);
    pci_register_slot(0x0A, PCI_CARD_NORMAL,      3, 4, 1, 2);
    pci_register_slot(0x01, PCI_CARD_AGPBRIDGE,   1, 2, 3, 4);

    device_add(&i440bx_device);
    device_add(&piix4e_device);
    device_add_params(&w83977_device, (void *) (W83977EF | W83977_AMI | W83977_NO_NVR));
#if 0
    device_add(ics9xxx_get(ICS9150_08)); /* setting proper speeds requires some interaction with the AS97127F ASIC */
#endif
    device_add(&sst_flash_39sf020_device);
    spd_register(SPD_TYPE_SDRAM, 0xF, 256);
    device_add(&w83781d_device);     /* fans: Chassis, CPU, Power; temperatures: MB, unused, CPU */
    hwm_values.temperatures[1] = 0;  /* unused */
    hwm_values.temperatures[2] -= 3; /* CPU offset */

    return ret;
}

int
machine_at_p3bf_init(const machine_t *model)
{
    int ret;

    ret = bios_load_linear("roms/machines/p3bf/1008f.004",
                           0x000c0000, 262144, 0);

    if (bios_only || !ret)
        return ret;

    machine_at_common_init(model);

    pci_init(PCI_CONFIG_TYPE_1);
    pci_register_slot(0x00, PCI_CARD_NORTHBRIDGE, 0, 0, 0, 0);
    pci_register_slot(0x04, PCI_CARD_SOUTHBRIDGE, 1, 2, 3, 4);
    pci_register_slot(0x09, PCI_CARD_NORMAL,      4, 1, 2, 3);
    pci_register_slot(0x0A, PCI_CARD_NORMAL,      3, 4, 1, 2);
    pci_register_slot(0x0B, PCI_CARD_NORMAL,      2, 3, 4, 1);
    pci_register_slot(0x0C, PCI_CARD_NORMAL,      1, 2, 3, 4);
    pci_register_slot(0x0D, PCI_CARD_NORMAL,      4, 1, 2, 3);
    pci_register_slot(0x0E, PCI_CARD_NORMAL,      3, 4, 1, 2);
    pci_register_slot(0x01, PCI_CARD_AGPBRIDGE,   1, 2, 3, 4);

    device_add(&i440bx_device);
    device_add(&piix4e_device);
    device_add_params(&w83977_device, (void *) (W83977EF | W83977_AMI | W83977_NO_NVR));
    device_add(ics9xxx_get(ICS9250_08));
    device_add(&sst_flash_39sf020_device);
    spd_register(SPD_TYPE_SDRAM, 0xF, 256);
    device_add(&as99127f_device);                    /* fans: Chassis, CPU, Power; temperatures: MB, JTPWR, CPU */
    hwm_values.voltages[4] = hwm_values.voltages[5]; /* +12V reading not in line with other boards; appears to be close to the -12V reading */

    return ret;
}

static const device_config_t ax6bc_config[] = {
    // clang-format off
    {
        .name           = "bios",
        .description    = "BIOS Version",
        .type           = CONFIG_BIOS,
        .default_string = "ax6bc",
        .default_int    = 0,
        .file_filter    = NULL,
        .spinner        = { 0 },
        .selection      = { { 0 } },
        .bios           = {
            {
                .name          = "Award Modular BIOS v4.51PGM - Revision R1.10",
                .internal_name = "ax6bc_451pg",
                .bios_type     = BIOS_NORMAL,
                .files_no      = 1,
                .local         = 0,
                .size          = 262144,
                .files         = { "roms/machines/ax6bc/ax6bc110.bin", "" }
            },
            {
                .name          = "Award Modular BIOS v4.60PGMA - Revision R2.20 (RM Accelerator 350P2XB/450P3XB)",
                .internal_name = "ax6bc_rm",
                .bios_type     = BIOS_NORMAL,
                .files_no      = 1,
                .local         = 0,
                .size          = 262144,
                .files         = { "roms/machines/ax6bc/ax6bc220.bin", "" }
            },
            {
                .name          = "Award Modular BIOS v4.60PGMA - Revision R2.59",
                .internal_name = "ax6bc",
                .bios_type     = BIOS_NORMAL,
                .files_no      = 1,
                .local         = 0,
                .size          = 262144,
                .files         = { "roms/machines/ax6bc/AX6BC_R2.59.bin", "" }
            },
            { .files_no = 0 }
        }
    },
    { .name = "", .description = "", .type = CONFIG_END }
    // clang-format on
};

const device_t ax6bc_device = {
    .name          = "AOpen AX6BC",
    .internal_name = "ax6bc_device",
    .flags         = 0,
    .local         = 0,
    .init          = NULL,
    .close         = NULL,
    .reset         = NULL,
    .available     = NULL,
    .speed_changed = NULL,
    .force_redraw  = NULL,
    .config        = ax6bc_config
};

int
machine_at_ax6bc_init(const machine_t *model)
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
    pci_register_slot(0x09, PCI_CARD_NORMAL,      4, 1, 2, 3);
    pci_register_slot(0x0A, PCI_CARD_NORMAL,      3, 4, 1, 2);
    pci_register_slot(0x0B, PCI_CARD_NORMAL,      2, 3, 4, 1);
    pci_register_slot(0x0C, PCI_CARD_NORMAL,      1, 2, 3, 4);
    pci_register_slot(0x0D, PCI_CARD_NORMAL,      4, 1, 2, 3);
    pci_register_slot(0x01, PCI_CARD_AGPBRIDGE,   1, 2, 3, 4);

    device_add(&i440bx_device);
    device_add(&piix4e_device);
    device_add_params(&w83977_device, (void *) (W83977TF | W83977_AMI | W83977_NO_NVR));
    device_add(&sst_flash_29ee020_device);
    spd_register(SPD_TYPE_SDRAM, 0x7, 256);
    device_add(&gl518sm_2d_device); /* fans: System, CPU; temperature: CPU; no reporting in BIOS */

    return ret;
}

static const device_config_t ga686_config[] = {
    // clang-format off
    {
        .name           = "bios",
        .description    = "BIOS Version",
        .type           = CONFIG_BIOS,
        .default_string = "686bx",
        .default_int    = 0,
        .file_filter    = NULL,
        .spinner        = { 0 },
        .selection      = { { 0 } },
        .bios           = {
            {
                .name          = "Award Modular BIOS v4.51PG - Revision 5/11/1998 (Amptron PII-3100)",
                .internal_name = "pii3100",
                .bios_type     = BIOS_NORMAL,
                .files_no      = 1,
                .local         = 0,
                .size          = 262144,
                .files         = { "roms/machines/686bx/31nologo.bin", "" }
            },
            {
                .name          = "Award Modular BIOS v4.51PG - Revision F1",
                .internal_name = "686bx_f1",
                .bios_type     = BIOS_NORMAL,
                .files_no      = 1,
                .local         = 0,
                .size          = 262144,
                .files         = { "roms/machines/686bx/6BX.F1", "" }
            },
            {
                .name          = "Award Modular BIOS v4.51PG - Revision F2a (Beta)",
                .internal_name = "686bx",
                .bios_type     = BIOS_NORMAL,
                .files_no      = 1,
                .local         = 0,
                .size          = 262144,
                .files         = { "roms/machines/686bx/6BX.F2a", "" }
            },
            { .files_no = 0 }
        }
    },
    { .name = "", .description = "", .type = CONFIG_END }
    // clang-format on
};

const device_t ga686_device = {
    .name          = "Gigabyte GA-686BX",
    .internal_name = "ga686_device",
    .flags         = 0,
    .local         = 0,
    .init          = NULL,
    .close         = NULL,
    .reset         = NULL,
    .available     = NULL,
    .speed_changed = NULL,
    .force_redraw  = NULL,
    .config        = ga686_config
};

int
machine_at_ga686_init(const machine_t *model)
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
    pci_register_slot(0x08, PCI_CARD_NORMAL,      1, 2, 3, 4);
    pci_register_slot(0x09, PCI_CARD_NORMAL,      2, 3, 4, 1);
    pci_register_slot(0x0A, PCI_CARD_NORMAL,      3, 4, 1, 2);
    pci_register_slot(0x0B, PCI_CARD_NORMAL,      4, 1, 2, 3);
    pci_register_slot(0x01, PCI_CARD_AGPBRIDGE,   1, 2, 3, 4);

    device_add(&i440bx_device);
    device_add(&piix4e_device);
    device_add_params(&w83977_device, (void *) (W83977TF | W83977_AMI | W83977_NO_NVR));
    device_add(&intel_flash_bxt_device);
    spd_register(SPD_TYPE_SDRAM, 0xF, 256);
    device_add(&w83781d_device);     /* fans: CPU, unused, unused; temperatures: unused, CPU, unused */
    hwm_values.temperatures[0] = 0;  /* unused */
    hwm_values.temperatures[1] += 4; /* CPU offset */
    hwm_values.temperatures[2] = 0;  /* unused */
    hwm_values.fans[1]         = 0;  /* unused */
    hwm_values.fans[2]         = 0;  /* unused */

    return ret;
}

static const device_config_t ms6119_config[] = {
    // clang-format off
    {
        .name           = "bios",
        .description    = "BIOS Version",
        .type           = CONFIG_BIOS,
        .default_string = "ms6119",
        .default_int    = 0,
        .file_filter    = NULL,
        .spinner        = { 0 },
        .selection      = { { 0 } },
        .bios           = {
            {
                .name          = "AMIBIOS 6 (071595) - Revision 1.72 (Packard Bell Tacoma with logo)",
                .internal_name = "tacoma_172",
                .bios_type     = BIOS_NORMAL,
                .files_no      = 1,
                .local         = 0,
                .size          = 262144,
                .files         = { "roms/machines/ms6119/A19P2172.ROM", "" }
            },
            {
                .name          = "AMIBIOS 6 (071595) - Revision 1.90 (Packard Bell Tacoma)",
                .internal_name = "tacoma",
                .bios_type     = BIOS_NORMAL,
                .files_no      = 1,
                .local         = 0,
                .size          = 262144,
                .files         = { "roms/machines/ms6119/A19P2190.ROM", "" }
            },
            {
                .name          = "Award Modular BIOS v4.51PG - Revision 2.10",
                .internal_name = "ms6119",
                .bios_type     = BIOS_NORMAL,
                .files_no      = 1,
                .local         = 0,
                .size          = 262144,
                .files         = { "roms/machines/ms6119/w6119ims.2a0", "" }
            },
            {
                .name          = "Award Modular BIOS v4.51PG - Revision 2.12 (Viglen Vig69M)",
                .internal_name = "vig69m",
                .bios_type     = BIOS_NORMAL,
                .files_no      = 1,
                .local         = 0,
                .size          = 262144,
                .files         = { "roms/machines/ms6119/vig69m.212", "" }
            },
            {
                .name          = "Award Modular BIOS v4.51PG - Revision 3.30b1 (LG IBM Multinet x7G)",
                .internal_name = "lgibmx7g",
                .bios_type     = BIOS_NORMAL,
                .files_no      = 1,
                .local         = 0,
                .size          = 262144,
                .files         = { "roms/machines/ms6119/ms6119.331", "" }
            },
            { .files_no = 0 }
        }
    },
    { .name = "", .description = "", .type = CONFIG_END }
    // clang-format on
};

const device_t ms6119_device = {
    .name          = "MSI MS-6119",
    .internal_name = "ms6119_device",
    .flags         = 0,
    .local         = 0,
    .init          = NULL,
    .close         = NULL,
    .reset         = NULL,
    .available     = NULL,
    .speed_changed = NULL,
    .force_redraw  = NULL,
    .config        = ms6119_config
};

int
machine_at_ms6119_init(const machine_t *model)
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
    pci_register_slot(0x0E, PCI_CARD_NORMAL,      1, 2, 3, 4);
    pci_register_slot(0x10, PCI_CARD_NORMAL,      2, 3, 4, 1);
    pci_register_slot(0x12, PCI_CARD_NORMAL,      3, 4, 1, 2);
    pci_register_slot(0x14, PCI_CARD_NORMAL,      4, 1, 2, 3);
    pci_register_slot(0x01, PCI_CARD_AGPBRIDGE,   1, 2, 3, 4);

    device_add(&i440bx_device);
    device_add(&piix4e_device);
    device_add_params(&w83977_device, (void *) (W83977TF | W83977_AMI | W83977_NO_NVR));
    device_add(&winbond_flash_w29c020_device);
    spd_register(SPD_TYPE_SDRAM, 0x7, 256);

    return ret;
}

static const device_config_t ms6147_config[] = {
    // clang-format off
    {
        .name           = "bios",
        .description    = "BIOS Version",
        .type           = CONFIG_BIOS,
        .default_string = "ms6147",
        .default_int    = 0,
        .file_filter    = NULL,
        .spinner        = { 0 },
        .selection      = { { 0 } },
        .bios           = {
            {
                .name          = "Award Modular BIOS v4.51PG - Revision 1.2 (Fujitsu ErgoPro e368)",
                .internal_name = "ergoproe368",
                .bios_type     = BIOS_NORMAL,
                .files_no      = 1,
                .local         = 0,
                .size          = 262144,
                .files         = { "roms/machines/ms6147/W647F412.BIN", "" }
            },
            {
                .name          = "Award Modular BIOS v4.51PG - Revision 1.8",
                .internal_name = "ms6147",
                .bios_type     = BIOS_NORMAL,
                .files_no      = 1,
                .local         = 0,
                .size          = 262144,
                .files         = { "roms/machines/ms6147/W647MS18.BIN", "" }
            },
            {
                .name          = "Award Modular BIOS v4.51PG - Revision 2.1 (Packard Bell Tempest)",
                .internal_name = "pbtempest",
                .bios_type     = BIOS_NORMAL,
                .files_no      = 1,
                .local         = 0,
                .size          = 262144,
                .files         = { "roms/machines/ms6147/w647p221.pbc", "" }
            },
            { .files_no = 0 }
        }
    },
    { .name = "", .description = "", .type = CONFIG_END }
    // clang-format on
};

const device_t ms6147_device = {
    .name          = "MSI MS-6147",
    .internal_name = "ms6147",
    .flags         = 0,
    .local         = 0,
    .init          = NULL,
    .close         = NULL,
    .reset         = NULL,
    .available     = NULL,
    .speed_changed = NULL,
    .force_redraw  = NULL,
    .config        = ms6147_config
};

int
machine_at_ms6147_init(const machine_t *model)
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
    pci_register_slot(0x14, PCI_CARD_SOUND,       3, 4, 1, 2);
    pci_register_slot(0x0E, PCI_CARD_NORMAL,      1, 2, 3, 4);
    pci_register_slot(0x10, PCI_CARD_NORMAL,      2, 3, 4, 1);
    pci_register_slot(0x12, PCI_CARD_NORMAL,      3, 4, 1, 2);
    pci_register_slot(0x07, PCI_CARD_SOUTHBRIDGE, 1, 2, 3, 4);
    pci_register_slot(0x01, PCI_CARD_AGPBRIDGE,   1, 2, 3, 4);
    device_add(&i440bx_device);
    device_add(&piix4e_device);
    device_add_params(&w83977_device, (void *) (W83977TF | W83977_AMI | W83977_NO_NVR));
    device_add(&winbond_flash_w29c020_device);
    spd_register(SPD_TYPE_SDRAM, 0x7, 256);

    if (sound_card_current[0] == SOUND_INTERNAL) {
        device_add(machine_get_snd_device(machine));
        device_add(&ct1297_device);
    }

    return ret;
}

static const device_config_t p6sba_config[] = {
    // clang-format off
    {
        .name           = "bios",
        .description    = "BIOS Version",
        .type           = CONFIG_BIOS,
        .default_string = "p6sba",
        .default_int    = 0,
        .file_filter    = NULL,
        .spinner        = { 0 },
        .selection      = { { 0 } },
        .bios           = {
            {
                .name          = "AMIBIOS 6 (071595) - Revision R3.1",
                .internal_name = "p6sba",
                .bios_type     = BIOS_NORMAL,
                .files_no      = 1,
                .local         = 0,
                .size          = 262144,
                .files         = { "roms/machines/p6sba/SBAB21.ROM", "" }
            },
            {
                .name          = "Award Modular BIOS v4.60PGA - Revision 05/07/1999 (Leadtek WinFast 8000BX)",
                .internal_name = "8000bx",
                .bios_type     = BIOS_NORMAL,
                .files_no      = 1,
                .local         = 0,
                .size          = 262144,
                .files         = { "roms/machines/p6sba/leadtek-8000bx-80000507.bin", "" }
            },
            { .files_no = 0 }
        }
    },
    { .name = "", .description = "", .type = CONFIG_END }
    // clang-format on
};

const device_t p6sba_device = {
    .name          = "Supermicro P6SBA",
    .internal_name = "p6sba_device",
    .flags         = 0,
    .local         = 0,
    .init          = NULL,
    .close         = NULL,
    .reset         = NULL,
    .available     = NULL,
    .speed_changed = NULL,
    .force_redraw  = NULL,
    .config        = p6sba_config
};

int
machine_at_p6sba_init(const machine_t *model)
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
    pci_register_slot(0x0F, PCI_CARD_NORMAL,      1, 2, 3, 4);
    pci_register_slot(0x10, PCI_CARD_NORMAL,      2, 3, 4, 1);
    pci_register_slot(0x12, PCI_CARD_NORMAL,      3, 4, 1, 2);
    pci_register_slot(0x14, PCI_CARD_NORMAL,      4, 1, 2, 3);
    pci_register_slot(0x0E, PCI_CARD_NORMAL,      3, 4, 0, 0);
    pci_register_slot(0x0D, PCI_CARD_NORMAL,      3, 0, 0, 0);
    pci_register_slot(0x01, PCI_CARD_AGPBRIDGE,   1, 2, 3, 4);

    device_add(&i440bx_device);
    device_add(&piix4e_device);
    device_add_params(&w83977_device, (void *) (W83977TF | W83977_AMI | W83977_NO_NVR));
    device_add(&intel_flash_bxt_device);
    spd_register(SPD_TYPE_SDRAM, 0x7, 256);
    device_add(&w83781d_device);   /* fans: CPU1, CPU2, Thermal Control; temperatures: unused, CPU1, CPU2 */
    hwm_values.voltages[1] = 1500; /* potentially Vtt; Leadtek BIOS calls it CPUi/o; Supermicro BIOS calls it CPU2 and reads a voltage this low as N/A */

    return ret;
}

static const device_config_t s1846_config[] = {
    // clang-format off
    {
        .name           = "bios",
        .description    = "BIOS Version",
        .type           = CONFIG_BIOS,
        .default_string = "s1846",
        .default_int    = 0,
        .file_filter    = NULL,
        .spinner        = { 0 },
        .selection      = { { 0 } },
        .bios           = {
            {
                .name          = "AMIBIOS 6 (071595) - Revision 2.00.04",
                .internal_name = "s1846",
                .bios_type     = BIOS_NORMAL,
                .files_no      = 1,
                .local         = 0,
                .size          = 262144,
                .files         = { "roms/machines/s1846/bx46200f.rom", "" }
            },
            {
                .name          = "AMIBIOS 6 (071595) - Revision 2.00.0x (Tulip Vision Line TP90)",
                .internal_name = "tp90",
                .bios_type     = BIOS_NORMAL,
                .files_no      = 1,
                .local         = 0,
                .size          = 262144,
                .files         = { "roms/machines/s1846/1846TP90.BIN", "" }
            },
            { .files_no = 0 }
        }
    },
    { .name = "", .description = "", .type = CONFIG_END }
    // clang-format on
};

const device_t s1846_device = {
    .name          = "Tyan Tsunami ATX",
    .internal_name = "s1846_device",
    .flags         = 0,
    .local         = 0,
    .init          = NULL,
    .close         = NULL,
    .reset         = NULL,
    .available     = NULL,
    .speed_changed = NULL,
    .force_redraw  = NULL,
    .config        = s1846_config
};

int
machine_at_s1846_init(const machine_t *model)
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
    pci_register_slot(0x0F, PCI_CARD_SOUND,       1, 0, 0, 0);
    pci_register_slot(0x10, PCI_CARD_NORMAL,      1, 2, 3, 4);
    pci_register_slot(0x11, PCI_CARD_NORMAL,      2, 3, 4, 1);
    pci_register_slot(0x12, PCI_CARD_NORMAL,      3, 4, 1, 2);
    pci_register_slot(0x13, PCI_CARD_NORMAL,      4, 1, 2, 3);
    pci_register_slot(0x14, PCI_CARD_NORMAL,      1, 2, 3, 4);
    pci_register_slot(0x01, PCI_CARD_AGPBRIDGE,   1, 2, 3, 4);

    device_add(&i440bx_device);
    device_add(&piix4e_device);
    device_add_params(&pc87309_device, (void *) (PCX730X_AMI | PC87309_PC87309));
    device_add(&intel_flash_bxt_device);
    spd_register(SPD_TYPE_SDRAM, 0x7, 256);

    if (sound_card_current[0] == SOUND_INTERNAL) {
        device_add(machine_get_snd_device(machine));
        device_add(&ct1297_device); /* no good pictures, but the marking looks like CT1297 from a distance */
    }

    return ret;
}

static const device_config_t vei8_config[] = {
    // clang-format off
    {
        .name           = "bios",
        .description    = "BIOS Version",
        .type           = CONFIG_BIOS,
        .default_string = "6110zu",
        .default_int    = 0,
        .file_filter    = NULL,
        .spinner        = { 0 },
        .selection      = { { 0 } },
        .bios           = {
            {
                .name          = "Award Modular BIOS v6.00PG - Revision 61100003 (beta)",
                .internal_name = "6110zu0003",
                .bios_type     = BIOS_NORMAL,
                .files_no      = 1,
                .local         = 0,
                .size          = 262144,
                .files         = { "roms/machines/vei8/61100003.BIN", "" }
            },
            {
                .name          = "Award Modular BIOS v6.00PG - Revision R804",
                .internal_name = "6110zu",
                .bios_type     = BIOS_NORMAL,
                .files_no      = 1,
                .local         = 0,
                .size          = 262144,
                .files         = { "roms/machines/vei8/r804.bin", "" }
            },
            {
                .name          = "Award Modular BIOS v6.00PG - Revision QHW.10.01 (HP Sherwood-B)",
                .internal_name = "vei8",
                .bios_type     = BIOS_NORMAL,
                .files_no      = 1,
                .local         = 0,
                .size          = 262144,
                .files         = { "roms/machines/vei8/QHW1001.BIN", "" }
            },
            { .files_no = 0 }
        }
    },
    { .name = "", .description = "", .type = CONFIG_END }
    // clang-format on
};

const device_t vei8_device = {
    .name          = "MiTAC/Trigon 6110Zu",
    .internal_name = "vei8_device",
    .flags         = 0,
    .local         = 0,
    .init          = NULL,
    .close         = NULL,
    .reset         = NULL,
    .available     = NULL,
    .speed_changed = NULL,
    .force_redraw  = NULL,
    .config        = vei8_config
};

/* i440ZX */
int
machine_at_vei8_init(const machine_t *model)
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
    pci_register_slot(0x0F, PCI_CARD_NORMAL,      1, 2, 3, 4);
    pci_register_slot(0x10, PCI_CARD_NORMAL,      2, 3, 4, 1);
    pci_register_slot(0x12, PCI_CARD_NORMAL,      3, 4, 1, 2);
    pci_register_slot(0x01, PCI_CARD_AGPBRIDGE,   1, 2, 3, 4);

    device_add(&i440zx_device);
    device_add(&piix4e_device);
    device_add_params(&fdc37m60x_device, (void *) (FDC37XXX2 | FDC37XXXX_370));
    device_add(ics9xxx_get(ICS9250_08));
    device_add(&sst_flash_39sf020_device);
    spd_register(SPD_TYPE_SDRAM, 0x3, 512);
    device_add(&as99127f_device); /* fans: Chassis, CPU, Power; temperatures: MB, JTPWR, CPU */

    return ret;
}

static void
machine_at_ms6168_common_init(const machine_t *model)
{
    machine_at_common_init(model);

    pci_init(PCI_CONFIG_TYPE_1);
    pci_register_slot(0x00, PCI_CARD_NORTHBRIDGE, 0, 0, 0, 0);
    pci_register_slot(0x14, PCI_CARD_SOUND,       3, 4, 1, 2);
    pci_register_slot(0x0E, PCI_CARD_NORMAL,      1, 2, 3, 4);
    pci_register_slot(0x10, PCI_CARD_NORMAL,      2, 3, 4, 1);
    pci_register_slot(0x12, PCI_CARD_NORMAL,      3, 4, 1, 2);
    pci_register_slot(0x07, PCI_CARD_SOUTHBRIDGE, 1, 2, 3, 4);
    pci_register_slot(0x01, PCI_CARD_AGPBRIDGE,   1, 2, 3, 4);

    device_add(&i440zx_device);
    device_add(&piix4e_device);

    if (gfxcard[0] == VID_INTERNAL)
        device_add(&voodoo_3_2000_agp_onboard_8m_device);

    device_add_params(&w83977_device, (void *) (W83977EF | W83977_AMI | W83977_NO_NVR));
    device_add(&intel_flash_bxt_device);
    spd_register(SPD_TYPE_SDRAM, 0x3, 256);

    if (sound_card_current[0] == SOUND_INTERNAL) {
        device_add(machine_get_snd_device(machine));
        device_add(&cs4297_device);
    }
}

int
machine_at_ms6168_init(const machine_t *model)
{
    int ret;

    ret = bios_load_linear("roms/machines/ms6168/w6168ims.130",
                           0x000c0000, 262144, 0);

    if (bios_only || !ret)
        return ret;

    machine_at_ms6168_common_init(model);

    return ret;
}

int
machine_at_borapro_init(const machine_t *model)
{
    int ret;

    ret = bios_load_linear("roms/machines/borapro/MS6168V2.50",
                           0x000c0000, 262144, 0);

    if (bios_only || !ret)
        return ret;

    machine_at_ms6168_common_init(model);

    return ret;
}

/* SMSC VictoryBX-66 */
int
machine_at_atc6310bxii_init(const machine_t *model)
{
    int ret;

    ret = bios_load_linear("roms/machines/atc6310bxii/6310s102.bin",
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

    device_add(&i440bx_device);
    device_add(&slc90e66_device);
    device_add_params(&w83977_device, (void *) (W83977EF | W83977_AMI | W83977_NO_NVR));
    device_add(&sst_flash_39sf020_device);
    spd_register(SPD_TYPE_SDRAM, 0x7, 256);

    return ret;
}

/* VIA Apollo Pro */
int
machine_at_ficka6130_init(const machine_t *model)
{
    int ret;

    ret = bios_load_linear("roms/machines/ficka6130/qa4163.bin",
                           0x000c0000, 262144, 0);

    if (bios_only || !ret)
        return ret;

    machine_at_common_init(model);

    pci_init(PCI_CONFIG_TYPE_1);
    pci_register_slot(0x00, PCI_CARD_NORTHBRIDGE, 0, 0, 0, 0);
    pci_register_slot(0x07, PCI_CARD_SOUTHBRIDGE, 0, 0, 0, 4);
    pci_register_slot(0x08, PCI_CARD_NORMAL,      1, 2, 3, 4);
    pci_register_slot(0x09, PCI_CARD_NORMAL,      2, 3, 4, 1);
    pci_register_slot(0x0A, PCI_CARD_NORMAL,      3, 4, 1, 2);
    pci_register_slot(0x0B, PCI_CARD_NORMAL,      4, 1, 2, 3);
    pci_register_slot(0x01, PCI_CARD_AGPBRIDGE,   1, 2, 3, 4);

    device_add(&via_apro_device);
    device_add(&via_vt82c596a_device);
    device_add_params(&w83877_device, (void *) (W83877TF | W83877_3F0));
    device_add(&sst_flash_29ee020_device);
    spd_register(SPD_TYPE_SDRAM, 0x7, 256);

    return ret;
}

/* VIA Apollo Pro 133 */
int
machine_at_p3v133_init(const machine_t *model)
{
    int ret;

    ret = bios_load_linear("roms/machines/p3v133/1003.002",
                           0x000c0000, 262144, 0);

    if (bios_only || !ret)
        return ret;

    machine_at_common_init(model);

    pci_init(PCI_CONFIG_TYPE_1);
    pci_register_slot(0x00, PCI_CARD_NORTHBRIDGE, 0, 0, 0, 0);
    pci_register_slot(0x04, PCI_CARD_SOUTHBRIDGE, 1, 2, 3, 4);
    pci_register_slot(0x09, PCI_CARD_NORMAL,      4, 1, 2, 3);
    pci_register_slot(0x0A, PCI_CARD_NORMAL,      3, 4, 1, 2);
    pci_register_slot(0x0B, PCI_CARD_NORMAL,      2, 3, 4, 1);
    pci_register_slot(0x0C, PCI_CARD_NORMAL,      1, 2, 3, 4);
    pci_register_slot(0x0D, PCI_CARD_NORMAL,      4, 1, 2, 3);
    pci_register_slot(0x0E, PCI_CARD_NORMAL,      3, 4, 1, 2);
    pci_register_slot(0x01, PCI_CARD_AGPBRIDGE,   1, 2, 3, 4);

    device_add(&via_apro133_device);
    device_add(&via_vt82c596b_device);
    device_add_params(&w83977_device, (void *) (W83977EF | W83977_AMI | W83977_NO_NVR));
    device_add(ics9xxx_get(ICS9248_39));
    device_add(&sst_flash_39sf020_device);
    spd_register(SPD_TYPE_SDRAM, 0x7, 512);
    device_add(&w83781d_device);     /* fans: Chassis, CPU, Power; temperatures: MB, unused, CPU */
    hwm_values.temperatures[1] = 0;  /* unused */
    hwm_values.temperatures[2] -= 3; /* CPU offset */

    return ret;
}

static const device_config_t ms6199va_config[] = {
    // clang-format off
    {
        .name           = "bios",
        .description    = "BIOS Version",
        .type           = CONFIG_BIOS,
        .default_string = "ms6199va",
        .default_int    = 0,
        .file_filter    = NULL,
        .spinner        = { 0 },
        .selection      = { { 0 } },
        .bios           = {
            {
                .name          = "Award Modular BIOS v4.51PG - Revision 3.5",
                .internal_name = "ms6199va",
                .bios_type     = BIOS_NORMAL,
                .files_no      = 1,
                .local         = 0,
                .size          = 262144,
                .files         = { "roms/machines/ms6199va/w6199vms.350", "" }
            },
            {
                .name          = "Award Modular BIOS v4.51PG - Revision 2.0 (Compaq ProSignia/Deskpro 693A)",
                .internal_name = "ms6199va_200",
                .bios_type     = BIOS_NORMAL,
                .files_no      = 1,
                .local         = 0,
                .size          = 262144,
                .files         = { "roms/machines/ms6199va/W6199VC8.BIN", "" }
            },
            {
                .name          = "Award Modular BIOS v4.51PG - Revision 2.0 (Compaq ProSignia/Deskpro 693A) [Patched for larger drives]",
                .internal_name = "ms6199va_200p",
                .bios_type     = BIOS_NORMAL,
                .files_no      = 1,
                .local         = 0,
                .size          = 262144,
                .files         = { "roms/machines/ms6199va/W6199VC8.PCD", "" }
            },
            {
                .name          = "Award Modular BIOS v4.51PG - Revision 3.7 (Packard Bell Phoenix)",
                .internal_name = "ms6199va_370",
                .bios_type     = BIOS_NORMAL,
                .files_no      = 1,
                .local         = 0,
                .size          = 262144,
                .files         = { "roms/machines/ms6199va/w6199VP2.370", "" }
            },
            { .files_no = 0 }
        }
    },
    { .name = "", .description = "", .type = CONFIG_END }
    // clang-format on
};

const device_t ms6199va_device = {
    .name          = "MSI MS-6199VA",
    .internal_name = "ms6199va_device",
    .flags         = 0,
    .local         = 0,
    .init          = NULL,
    .close         = NULL,
    .reset         = NULL,
    .available     = NULL,
    .speed_changed = NULL,
    .force_redraw  = NULL,
    .config        = ms6199va_config
};

int
machine_at_ms6199va_init(const machine_t *model)
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
    pci_register_slot(0x07, PCI_CARD_SOUTHBRIDGE, 0, 0, 3, 4);
    pci_register_slot(0x0C, PCI_CARD_SOUND,       3, 4, 1, 2);
    pci_register_slot(0x0E, PCI_CARD_NORMAL,      1, 2, 3, 4);
    pci_register_slot(0x10, PCI_CARD_NORMAL,      2, 3, 4, 1);
    pci_register_slot(0x12, PCI_CARD_NORMAL,      3, 4, 1, 2);
    pci_register_slot(0x13, PCI_CARD_NORMAL,      2, 1, 4, 3);
    pci_register_slot(0x14, PCI_CARD_NORMAL,      2, 1, 4, 3);
    pci_register_slot(0x01, PCI_CARD_AGPBRIDGE,   1, 2, 3, 4);

    device_add(&via_apro133a_device);
    device_add(&via_vt82c596b_device);
    device_add_params(&w83977_device, (void *) (W83977EF | W83977_AMI | W83977_NO_NVR));
    device_add(&winbond_flash_w29c020_device);
    spd_register(SPD_TYPE_SDRAM, 0x7, 512);
    device_add(&w83782d_device); /* fans: Chassis, Power, CPU; temperatures: System, CPU, unused */
    hwm_values.temperatures[2] = 0; /* unused */

    if (sound_card_current[0] == SOUND_INTERNAL) {
        device_add(machine_get_snd_device(machine));
        device_add(&w83971d_device);
    }

    return ret;
}

/* VIA Apollo Pro 133A */
int
machine_at_p3v4x_init(const machine_t *model)
{
    int ret;

    ret = bios_load_linear("roms/machines/p3v4x/1006.004",
                           0x000c0000, 262144, 0);

    if (bios_only || !ret)
        return ret;

    machine_at_common_init(model);

    pci_init(PCI_CONFIG_TYPE_1);
    pci_register_slot(0x00, PCI_CARD_NORTHBRIDGE, 0, 0, 0, 0);
    pci_register_slot(0x04, PCI_CARD_SOUTHBRIDGE, 1, 2, 3, 4);
    pci_register_slot(0x09, PCI_CARD_NORMAL,      4, 1, 2, 3);
    pci_register_slot(0x0A, PCI_CARD_NORMAL,      3, 4, 1, 2);
    pci_register_slot(0x0B, PCI_CARD_NORMAL,      2, 3, 4, 1);
    pci_register_slot(0x0C, PCI_CARD_NORMAL,      1, 2, 3, 4);
    pci_register_slot(0x0D, PCI_CARD_NORMAL,      4, 1, 2, 3);
    pci_register_slot(0x0E, PCI_CARD_NORMAL,      3, 4, 1, 2);
    pci_register_slot(0x01, PCI_CARD_AGPBRIDGE,   1, 2, 3, 4);

    device_add(&via_apro133a_device);
    device_add(&via_vt82c596b_device);
    device_add_params(&w83977_device, (void *) (W83977EF | W83977_AMI | W83977_NO_NVR));
    device_add(ics9xxx_get(ICS9250_18));
    device_add(&sst_flash_39sf020_device);
    spd_register(SPD_TYPE_SDRAM, 0xF, 512);
    device_add(&as99127f_device); /* fans: Chassis, CPU, Power; temperatures: MB, JTPWR, CPU */

    return ret;
}

int
machine_at_gt694va_init(const machine_t *model)
{
    int ret;

    ret = bios_load_linear("roms/machines/gt694va/21071100.bin",
                           0x000c0000, 262144, 0);

    if (bios_only || !ret)
        return ret;

    machine_at_common_init(model);

    pci_init(PCI_CONFIG_TYPE_1);
    pci_register_slot(0x00, PCI_CARD_NORTHBRIDGE, 0, 0, 0, 0);
    pci_register_slot(0x07, PCI_CARD_SOUTHBRIDGE, 0, 0, 3, 4);
    pci_register_slot(0x0D, PCI_CARD_SOUND,       4, 1, 2, 3); /* assumed */
    pci_register_slot(0x0F, PCI_CARD_NORMAL,      3, 4, 1, 2);
    pci_register_slot(0x11, PCI_CARD_NORMAL,      2, 3, 4, 1);
    pci_register_slot(0x13, PCI_CARD_NORMAL,      1, 2, 3, 4);
    pci_register_slot(0x01, PCI_CARD_AGPBRIDGE,   1, 2, 3, 4);

    device_add(&via_apro133a_device);
    device_add(&via_vt82c596b_device);
    device_add_params(&w83977_device, (void *) (W83977EF | W83977_AMI | W83977_NO_NVR));
    device_add(&sst_flash_39sf020_device);
    spd_register(SPD_TYPE_SDRAM, 0x7, 1024);
    device_add(&w83782d_device);       /* fans: CPU, unused, unused; temperatures: System, CPU1, unused */
    hwm_values.voltages[1]     = 1500; /* IN1 (unknown purpose, assumed Vtt) */
    hwm_values.fans[0]         = 4500; /* BIOS does not display <4411 RPM */
    hwm_values.fans[1]         = 0;    /* unused */
    hwm_values.fans[2]         = 0;    /* unused */
    hwm_values.temperatures[2] = 0;    /* unused */

    if (sound_card_current[0] == SOUND_INTERNAL) {
        device_add(machine_get_snd_device(machine));
        device_add(&cs4297_device); /* no good pictures, but the marking looks like CS4297 from a distance */
    }

    return ret;
}

/* SiS 5600 */
int
machine_at_p6f99_init(const machine_t *model)
{
    int ret;

    ret = bios_load_linear("roms/machines/p6f99/99-8.BIN",
                           0x000c0000, 262144, 0);

    if (bios_only || !ret)
        return ret;

    machine_at_common_init(model);

    pci_init(PCI_CONFIG_TYPE_1 | FLAG_TRC_CONTROLS_CPURST);
    pci_register_slot(0x00, PCI_CARD_NORTHBRIDGE, 0, 0, 0, 0);
    pci_register_slot(0x01, PCI_CARD_SOUTHBRIDGE, 0, 0, 0, 0);
    pci_register_slot(0x0C, PCI_CARD_SOUND,       2, 3, 4, 1);
    pci_register_slot(0x09, PCI_CARD_NORMAL,      1, 2, 3, 4);
    pci_register_slot(0x0B, PCI_CARD_NORMAL,      4, 1, 2, 3);
    pci_register_slot(0x0D, PCI_CARD_NORMAL,      3, 4, 1, 2);
    pci_register_slot(0x0F, PCI_CARD_NORMAL,      2, 3, 4, 1);
    pci_register_slot(0x02, PCI_CARD_AGPBRIDGE,   0, 0, 0, 0);

    device_add(&sis_5600_device);
    device_add(&it8661f_device);
    device_add(&winbond_flash_w29c020_device);

    if (sound_card_current[0] == SOUND_INTERNAL) {
        device_add(machine_get_snd_device(machine));
        device_add(&ct1297_device);
    }

    return ret;
}

int
machine_at_m747_init(const machine_t *model)
{
    int ret;

    ret = bios_load_linear("roms/machines/m747/990521.rom",
                           0x000c0000, 262144, 0);

    if (bios_only || !ret)
        return ret;

    machine_at_common_init(model);

    pci_init(PCI_CONFIG_TYPE_1 | FLAG_TRC_CONTROLS_CPURST);
    pci_register_slot(0x00, PCI_CARD_NORTHBRIDGE, 0, 0, 0, 0);
    pci_register_slot(0x01, PCI_CARD_SOUTHBRIDGE, 0, 0, 0, 0);
    pci_register_slot(0x09, PCI_CARD_NORMAL,      1, 2, 3, 4);
    pci_register_slot(0x0B, PCI_CARD_NORMAL,      4, 1, 2, 3);
    pci_register_slot(0x0D, PCI_CARD_NORMAL,      3, 4, 1, 2);
    pci_register_slot(0x02, PCI_CARD_AGPBRIDGE,   0, 0, 0, 0);

    device_add(&sis_5600_device);
    device_add(&it8661f_device);
    device_add(&winbond_flash_w29c020_device);

    return ret;
}
