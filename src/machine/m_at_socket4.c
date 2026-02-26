/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          Implementation of Socket 4 machines.
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
#include <86box/timer.h>
#include <86box/fdd.h>
#include <86box/fdc.h>
#include <86box/keyboard.h>
#include <86box/flash.h>
#include <86box/nvr.h>
#include <86box/scsi_ncr53c8xx.h>
#include <86box/sio.h>
#include <86box/timer.h>
#include <86box/video.h>
#include <86box/machine.h>

/* i430LX */
static const device_config_t v12p_config[] = {
    // clang-format off
    {
        .name           = "bios",
        .description    = "BIOS Version",
        .type           = CONFIG_BIOS,
        .default_string = "v12p",
        .default_int    = 0,
        .file_filter    = NULL,
        .spinner        = { 0 },
        .selection      = { { 0 } },
        .bios           = {
            {
                .name          = "Acer BIOS V1.2 - Revision R1.4",
                .internal_name = "v12p_r14",
                .bios_type     = BIOS_NORMAL,
                .files_no      = 1,
                .local         = 0,
                .size          = 131072,
                .files         = { "roms/machines/v12p/v12p_14.bin", "" }
            },
            {
                .name          = "Acer BIOS V1.2 - Revision R1.6",
                .internal_name = "v12p",
                .bios_type     = BIOS_NORMAL,
                .files_no      = 1,
                .local         = 0,
                .size          = 131072,
                .files         = { "roms/machines/v12p/v12p_16.bin", "" }
            },      
            { .files_no = 0 }            
        }
    },
    { .name = "", .description = "", .type = CONFIG_END }
    // clang-format on
};

const device_t v12p_device = {
    .name          = "Acer V12P",
    .internal_name = "v12p_device",
    .flags         = 0,
    .local         = 0,
    .init          = NULL,
    .close         = NULL,
    .reset         = NULL,
    .available     = NULL,
    .speed_changed = NULL,
    .force_redraw  = NULL,
    .config        = v12p_config
};

int
machine_at_v12p_init(const machine_t *model)
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

    device_add(&ide_isa_device);
    pci_init(PCI_CONFIG_TYPE_2 | PCI_NO_IRQ_STEERING);
    pci_register_slot(0x00, PCI_CARD_NORTHBRIDGE, 0, 0, 0, 0);
    pci_register_slot(0x01, PCI_CARD_SCSI,        1, 4, 3, 2);
    pci_register_slot(0x02, PCI_CARD_SOUTHBRIDGE, 2, 1, 4, 3);
    pci_register_slot(0x03, PCI_CARD_NORMAL,      3, 2, 1, 4);
    pci_register_slot(0x04, PCI_CARD_NORMAL,      4, 0, 0, 0);
    pci_register_slot(0x05, PCI_CARD_NORMAL,      0, 0, 0, 0);

    device_add(&i430lx_device);
    device_add_params(machine_get_kbc_device(machine), (void *) model->kbc_params);
    device_add(&sio_zb_device);
    device_add_params(&pc87310_device, (void *) (PC87310_ALI));
    device_add(&amd_am28f010_flash_device);

    return ret;
}

int
machine_at_excaliburpci_init(const machine_t *model)
{
    int ret;

    ret = bios_load_linear_inverted("roms/machines/excaliburpci/S701P.ROM",
                                    0x000e0000, 131072, 0);

    if (bios_only || !ret)
        return ret;

    machine_at_common_init(model);

    pci_init(PCI_CONFIG_TYPE_2);
    pci_register_slot(0x00, PCI_CARD_NORTHBRIDGE, 0, 0, 0, 0);
    pci_register_slot(0x03, PCI_CARD_IDE,         0, 0, 0, 0);
    pci_register_slot(0x0F, PCI_CARD_NORMAL,      1, 2, 3, 4);
    pci_register_slot(0x0E, PCI_CARD_NORMAL,      2, 3, 4, 1);
    pci_register_slot(0x0D, PCI_CARD_NORMAL,      3, 4, 1, 2);
    pci_register_slot(0x02, PCI_CARD_SOUTHBRIDGE, 0, 0, 0, 0);

    device_add_params(&fdc37c6xx_device, (void *) FDC37C665);
    device_add_params(machine_get_kbc_device(machine), (void *) model->kbc_params);
    device_add(&ide_cmd640_pci_legacy_only_device);

    device_add(&i430lx_device);
    device_add(&sio_zb_device);
    device_add(&intel_flash_bxt_ami_device);

    return ret;
}

static const device_config_t p5mp3_config[] = {
    // clang-format off
    {
        .name           = "bios",
        .description    = "BIOS Version",
        .type           = CONFIG_BIOS,
        .default_string = "p5mp3",
        .default_int    = 0,
        .file_filter    = NULL,
        .spinner        = { 0 },
        .selection      = { { 0 } },
        .bios           = {
            {
                .name          = "Award Modular BIOS v4.50 - Revision 0205",
                .internal_name = "p5mp3",
                .bios_type     = BIOS_NORMAL,
                .files_no      = 1,
                .local         = 0,
                .size          = 131072,
                .files         = { "roms/machines/p5mp3/0205.bin", "" }
            },
            {
                .name          = "Award Modular BIOS v4.51G - Revision 0402 (Beta)",
                .internal_name = "p5mp3_0402",
                .bios_type     = BIOS_NORMAL,
                .files_no      = 1,
                .local         = 0,
                .size          = 131072,
                .files         = { "roms/machines/p5mp3/0402.001", "" }
            },      
            { .files_no = 0 }            
        }
    },
    { .name = "", .description = "", .type = CONFIG_END }
    // clang-format on
};

const device_t p5mp3_device = {
    .name          = "ASUS P/I-P5MP3",
    .internal_name = "p5mp3_device",
    .flags         = 0,
    .local         = 0,
    .init          = NULL,
    .close         = NULL,
    .reset         = NULL,
    .available     = NULL,
    .speed_changed = NULL,
    .force_redraw  = NULL,
    .config        = p5mp3_config
};

int
machine_at_p5mp3_init(const machine_t *model)
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

    pci_init(PCI_CONFIG_TYPE_2 | PCI_NO_IRQ_STEERING);
    pci_register_slot(0x00, PCI_CARD_NORTHBRIDGE, 0, 0, 0, 0);
    pci_register_slot(0x05, PCI_CARD_NORMAL,      1, 2, 3, 4); /* 05 = Slot 1 */
    pci_register_slot(0x04, PCI_CARD_NORMAL,      2, 3, 4, 1); /* 04 = Slot 2 */
    pci_register_slot(0x03, PCI_CARD_NORMAL,      3, 4, 1, 2); /* 03 = Slot 3 */
    pci_register_slot(0x02, PCI_CARD_SOUTHBRIDGE, 0, 0, 0, 0);

    device_add(&fdc_at_device);
    device_add_params(machine_get_kbc_device(machine), (void *) model->kbc_params);

    device_add(&i430lx_device);
    device_add(&sio_zb_device);
    device_add(&catalyst_flash_device);

    return ret;
}

int
machine_at_opti560l_init(const machine_t *model)
{
    int ret;

    ret = bios_load_linear_inverted("roms/machines/opti560l/560L_A06.ROM",
                                    0x000e0000, 131072, 0);

    if (bios_only || !ret)
        return ret;

    machine_at_common_init(model);

    device_add(&ide_pci_device);

    pci_init(PCI_CONFIG_TYPE_2);
    pci_register_slot(0x00, PCI_CARD_NORTHBRIDGE, 0, 0, 0, 0);
    pci_register_slot(0x03, PCI_CARD_NORMAL,      4, 4, 3, 3);
    pci_register_slot(0x07, PCI_CARD_NORMAL,      1, 4, 3, 2);
    pci_register_slot(0x08, PCI_CARD_NORMAL,      2, 1, 3, 4);
    pci_register_slot(0x02, PCI_CARD_SOUTHBRIDGE, 0, 0, 0, 0);

    device_add(&i430lx_device);
    device_add_params(machine_get_kbc_device(machine), (void *) model->kbc_params);
    device_add(&sio_zb_device);
    device_add_params(&i82091aa_device, (void *) I82091AA_022);
    device_add(&intel_flash_bxt_ami_device);

    return ret;
}

void
machine_at_award_common_init(const machine_t *model)
{
    machine_at_common_init(model);

    pci_init(PCI_CONFIG_TYPE_2 | PCI_NO_IRQ_STEERING);
    pci_register_slot(0x00, PCI_CARD_NORTHBRIDGE, 0, 0, 0, 0);
    pci_register_slot(0x03, PCI_CARD_NORMAL,      1, 2, 3, 4); /* 03 = Slot 1 */
    pci_register_slot(0x04, PCI_CARD_NORMAL,      2, 3, 4, 1); /* 04 = Slot 2 */
    pci_register_slot(0x05, PCI_CARD_NORMAL,      3, 4, 1, 2); /* 05 = Slot 3 */
    pci_register_slot(0x06, PCI_CARD_NORMAL,      4, 1, 2, 3); /* 06 = Slot 4 */
    pci_register_slot(0x07, PCI_CARD_SCSI,        1, 2, 3, 4); /* 07 = SCSI   */
    pci_register_slot(0x02, PCI_CARD_SOUTHBRIDGE, 0, 0, 0, 0);

    if (fdc_current[0] == FDC_INTERNAL)
        device_add(&fdc_at_device);

    device_add_params(machine_get_kbc_device(machine), (void *) model->kbc_params);
    device_add(&sio_zb_device);
    device_add(&intel_flash_bxt_device);
}

int
machine_at_586is_init(const machine_t *model)
{
    int ret;

    ret = bios_load_linear("roms/machines/586is/IS.34",
                           0x000e0000, 131072, 0);

    if (bios_only || !ret)
        return ret;

    machine_at_award_common_init(model);

    device_add(&i430lx_device);

    return ret;
}

int
machine_at_valuepointp60_init(const machine_t *model)
{
    int ret;

    ret = bios_load_linear_combined("roms/machines/valuepointp60/1006AV0M.BIO",
                                    "roms/machines/valuepointp60/1006AV0M.BI1",
                                    0x1d000, 128);

    if (bios_only || !ret)
        return ret;

    machine_at_common_init(model);
    device_add(&ide_pci_device);

    pci_init(PCI_CONFIG_TYPE_2 | PCI_NO_IRQ_STEERING);
    pci_register_slot(0x00, PCI_CARD_NORTHBRIDGE, 0, 0, 0, 0);
    pci_register_slot(0x01, PCI_CARD_IDE,         0, 0, 0, 0);
    pci_register_slot(0x03, PCI_CARD_VIDEO,       3, 3, 3, 3);
    pci_register_slot(0x06, PCI_CARD_NORMAL,      3, 2, 1, 4);
    pci_register_slot(0x0E, PCI_CARD_NORMAL,      2, 1, 3, 4);
    pci_register_slot(0x0C, PCI_CARD_NORMAL,      1, 3, 2, 4);
    pci_register_slot(0x02, PCI_CARD_SOUTHBRIDGE, 0, 0, 0, 0);

    device_add_params(machine_get_kbc_device(machine), (void *) model->kbc_params);
    device_add(&sio_device);
    device_add_params(&fdc37c6xx_device, (void *) (FDC37C665 | FDC37C6XX_IDE_PRI));
    device_add(&intel_flash_bxt_ami_device);

    device_add(&i430lx_device);

    if (gfxcard[0] == VID_INTERNAL)
        device_add(&mach32_onboard_pci_device);

    return ret;
}

static const device_config_t batman_config[] = {
    // clang-format off
    {
        .name           = "bios",
        .description    = "BIOS Version",
        .type           = CONFIG_BIOS,
        .default_string = "batman",
        .default_int    = 0,
        .file_filter    = NULL,
        .spinner        = { 0 },
        .selection      = { { 0 } },
        .bios           = {
            {
                .name          = "AMIBIOS 111192 - Revision A08 (Dell Dimension XPS P60)",
                .internal_name = "dellxp60",
                .bios_type     = BIOS_NORMAL,
                .files_no      = 1,
                .local         = 0,
                .size          = 131072,
                .files         = { "roms/machines/batman/XP60-A08.ROM", "" }
            },
            {
                .name          = "Intel AMIBIOS - Revision 1.00.04.AF1P (AMBRA DP60 PCI)",
                .internal_name = "ambradp60",
                .bios_type     = BIOS_NORMAL,
                .files_no      = 2,
                .local         = 0,
                .size          = 131072,
                .files         = { "roms/machines/batman/1004AF1P.BIO", "roms/machines/batman/1004AF1P.BI1", "" }
            },
            {
                .name          = "Intel AMIBIOS - Revision 1.00.08.AF1",
                .internal_name = "batman",
                .bios_type     = BIOS_NORMAL,
                .files_no      = 2,
                .local         = 0,
                .size          = 131072,
                .files         = { "roms/machines/batman/1008AF1_.BIO", "roms/machines/batman/1008AF1_.BI1", "" }
            },
            { .files_no = 0 }
        }
    },
    { .name = "", .description = "", .type = CONFIG_END }
    // clang-format on
};

const device_t batman_device = {
    .name          = "Intel Premiere/PCI (Batman)",
    .internal_name = "batman_device",
    .flags         = 0,
    .local         = 0,
    .init          = NULL,
    .close         = NULL,
    .reset         = NULL,
    .available     = NULL,
    .speed_changed = NULL,
    .force_redraw  = NULL,
    .config        = batman_config
};

int
machine_at_batman_init(const machine_t *model)
{
    int         ret = 0;
    const char *fn;
    const char *fn2;

    /* No ROMs available */
    if (!device_available(model->device))
        return ret;

    device_context(model->device);
    int is_dell = !strcmp(device_get_config_bios("bios"), "dellxp60");
    fn          = device_get_bios_file(machine_get_device(machine), device_get_config_bios("bios"), 0);
    if (is_dell)
        ret = bios_load_linear_inverted(fn, 0x000e0000, 131072, 0);
    else {
        fn2 = device_get_bios_file(machine_get_device(machine), device_get_config_bios("bios"), 1);
        ret = bios_load_linear_combined(fn, fn2, 0x1c000, 128);
    }
    device_context_restore();

    machine_at_common_init(model);

    device_add(&ide_pci_device);

    pci_init(PCI_CONFIG_TYPE_2);
    pci_register_slot(0x00, PCI_CARD_NORTHBRIDGE, 0, 0, 0, 0);
    pci_register_slot(0x01, PCI_CARD_IDE,         0, 0, 0, 0);
    if (is_dell) {
        pci_register_slot(0x04, PCI_CARD_NORMAL,  4, 4, 3, 3);
        pci_register_slot(0x05, PCI_CARD_NORMAL,  1, 4, 3, 2);
        pci_register_slot(0x06, PCI_CARD_NORMAL,  2, 1, 3, 4);
    } else {
        pci_register_slot(0x06, PCI_CARD_NORMAL,  3, 2, 1, 4);
        pci_register_slot(0x0E, PCI_CARD_NORMAL,  2, 1, 3, 4);
        pci_register_slot(0x0C, PCI_CARD_NORMAL,  1, 3, 2, 4);
    }
    pci_register_slot(0x02, PCI_CARD_SOUTHBRIDGE, 0, 0, 0, 0);

    device_add_params(machine_get_kbc_device(machine), (void *) model->kbc_params);
    device_add(&sio_zb_device);
    device_add_params(&fdc37c6xx_device, (void *) (FDC37C665 | FDC37C6XX_IDE_PRI));
    device_add(&intel_flash_bxt_ami_device);

    device_add(&i430lx_device);

    return ret;
}

void
machine_at_premiere_common_init(const machine_t *model, int pci_switch)
{
    machine_at_common_init(model);

    device_add(&ide_pci_2ch_device);

    pci_init(PCI_CONFIG_TYPE_2 | pci_switch);
    pci_register_slot(0x00, PCI_CARD_NORTHBRIDGE, 0, 0, 0, 0);
    pci_register_slot(0x01, PCI_CARD_IDE,         0, 0, 0, 0);
    pci_register_slot(0x06, PCI_CARD_NORMAL,      3, 2, 1, 4);
    pci_register_slot(0x0E, PCI_CARD_NORMAL,      2, 1, 3, 4);
    pci_register_slot(0x0C, PCI_CARD_NORMAL,      1, 3, 2, 4);
    pci_register_slot(0x02, PCI_CARD_SOUTHBRIDGE, 0, 0, 0, 0);

    device_add_params(machine_get_kbc_device(machine), (void *) model->kbc_params);
    device_add(&sio_zb_device);
    device_add(&ide_rz1000_pci_single_channel_device);
    device_add_params(&fdc37c6xx_device, (void *) (FDC37C665 | FDC37C6XX_IDE_SEC));
    device_add(&intel_flash_bxt_ami_device);
}

int
machine_at_revenge_init(const machine_t *model)
{
    int ret;

    ret = bios_load_linear_combined("roms/machines/revenge/1013af2_.bio",
                                    "roms/machines/revenge/1013af2_.bi1",
                                    0x1c000, 128);

    if (bios_only || !ret)
        return ret;

    machine_at_premiere_common_init(model, 0);

    device_add(&i430lx_device);

    return ret;
}

int
machine_at_m5pi_init(const machine_t *model)
{
    int ret;

    ret = bios_load_linear_inverted("roms/machines/m5pi/M5PI10R.BIN",
                                    0x000e0000, 131072, 0);

    if (bios_only || !ret)
        return ret;

    machine_at_common_init(model);

    pci_init(PCI_CONFIG_TYPE_2 | PCI_NO_IRQ_STEERING);
    pci_register_slot(0x00, PCI_CARD_NORTHBRIDGE, 0, 0, 0, 0);
    pci_register_slot(0x01, PCI_CARD_IDE,         0, 0, 0, 0);
    pci_register_slot(0x0f, PCI_CARD_NORMAL,      1, 2, 3, 4);
    pci_register_slot(0x0c, PCI_CARD_NORMAL,      2, 3, 4, 1);
    pci_register_slot(0x0b, PCI_CARD_NORMAL,      3, 4, 1, 2);
    pci_register_slot(0x02, PCI_CARD_SOUTHBRIDGE, 0, 0, 0, 0);

    device_add(&i430lx_device);
    device_add(&sio_zb_device);
    device_add_params(machine_get_kbc_device(machine), (void *) model->kbc_params);
    device_add(&ide_w83769f_pci_single_channel_device);
    device_add_params(&fdc37c6xx_device, (void *) (FDC37C665 | FDC37C6XX_IDE_SEC));
    device_add(&intel_flash_bxt_ami_device);

    return ret;
}

int
machine_at_pb520r_init(const machine_t *model)
{
    int ret;

    ret = bios_load_linear_combined("roms/machines/pb520r/1009bc0r.bio",
                                    "roms/machines/pb520r/1009bc0r.bi1",
                                    0x1d000, 128);

    if (bios_only || !ret)
        return ret;

    machine_at_common_init(model);

    pci_init(PCI_CONFIG_TYPE_2);
    pci_register_slot(0x00, PCI_CARD_NORTHBRIDGE, 0, 0, 0, 0);
    pci_register_slot(0x01, PCI_CARD_IDE,         0, 0, 0, 0);
    pci_register_slot(0x03, PCI_CARD_VIDEO,       3, 3, 3, 3);
    pci_register_slot(0x06, PCI_CARD_NORMAL,      3, 2, 1, 4);
    pci_register_slot(0x0E, PCI_CARD_NORMAL,      2, 1, 3, 4);
    pci_register_slot(0x0C, PCI_CARD_NORMAL,      1, 3, 2, 4);
    pci_register_slot(0x02, PCI_CARD_SOUTHBRIDGE, 0, 0, 0, 0);

    device_add(&i430lx_device);
    device_add(&ide_cmd640_pci_single_channel_device);

    if (gfxcard[0] == VID_INTERNAL)
        device_add(&gd5434_onboard_pci_device);

    device_add_params(machine_get_kbc_device(machine), (void *) model->kbc_params);
    device_add(&sio_zb_device);
    device_add_params(&i82091aa_device, (void *) (I82091AA_022 | I82091AA_IDE_PRI));
    device_add(&intel_flash_bxt_ami_device);

    return ret;
}

/* OPTi 597 */
int
machine_at_excalibur_init(const machine_t *model)
{
    int ret;

    ret = bios_load_linear_inverted("roms/machines/excalibur/S75P.ROM",
                                    0x000e0000, 131072, 0);

    if (bios_only || !ret)
        return ret;

    machine_at_common_init(model);

    device_add(&opti5x7_device);
    device_add(&ide_opti611_vlb_device);
    device_add_params(&fdc37c6xx_device, (void *) FDC37C661);
    device_add_params(machine_get_kbc_device(machine), (void *) model->kbc_params);

    return ret;
}

int
machine_at_globalyst330_p5_init(const machine_t *model)
{
    int ret;

    ret = bios_load_linear("roms/machines/globalyst330_p5/MiTAC_PB5500C_v1.02_120794_ATT.BIN",
                           0x000e0000, 131072, 0);

    if (bios_only || !ret)
        return ret;

    machine_at_common_init(model);

    pci_init(PCI_CONFIG_TYPE_1);
    pci_register_slot(0x10, PCI_CARD_NORTHBRIDGE, 0,  0,  0,  0);
    pci_register_slot(0x11, PCI_CARD_NORMAL,      1,  2,  3,  4);
    pci_register_slot(0x12, PCI_CARD_NORMAL,      5,  6,  7,  8);
    pci_register_slot(0x13, PCI_CARD_NORMAL,      9,  10, 11, 12);
    pci_register_slot(0x14, PCI_CARD_NORMAL,      13, 14, 15, 16);

    device_add(&opti5x7_pci_device);
    device_add(&opti822_device);
    device_add(&sst_flash_29ee010_device);
    device_add_params(machine_get_kbc_device(machine), (void *) model->kbc_params);

    if (fdc_current[0] == FDC_INTERNAL)
        device_add(&fdc_at_device);

    return ret;
}

int
machine_at_p5vl_init(const machine_t *model)
{
    int ret;

    ret = bios_load_linear("roms/machines/p5vl/SM507.ROM",
                           0x000e0000, 131072, 0);

    if (bios_only || !ret)
        return ret;

    machine_at_common_init(model);

    pci_init(PCI_CONFIG_TYPE_1);
    pci_register_slot(0x10, PCI_CARD_NORTHBRIDGE, 0, 0, 0, 0);
    pci_register_slot(0x11, PCI_CARD_NORMAL,       1,  2,  3,  4);
    pci_register_slot(0x12, PCI_CARD_NORMAL,       5,  6,  7,  8);
    pci_register_slot(0x13, PCI_CARD_NORMAL,      9,  10, 11, 12);
    pci_register_slot(0x14, PCI_CARD_NORMAL,      13, 14, 15, 16);

    device_add(&opti5x7_pci_device);
    device_add(&opti822_device);
    device_add(&sst_flash_29ee010_device);
    device_add_params(machine_get_kbc_device(machine), (void *) model->kbc_params);

    if (fdc_current[0] == FDC_INTERNAL)
        device_add(&fdc_at_device);

    return ret;
}

/* SiS 501 */
int
machine_at_excaliburpci2_init(const machine_t *model)
{
    int ret;

    ret = bios_load_linear_inverted("roms/machines/excaliburpci2/S722P.ROM",
                                    0x000e0000, 131072, 0);

    if (bios_only || !ret)
        return ret;

    machine_at_common_init(model);

    pci_init(PCI_CONFIG_TYPE_1 | FLAG_TRC_CONTROLS_CPURST);
    pci_register_slot(0x00, PCI_CARD_NORTHBRIDGE, 0, 0, 0, 0);
    pci_register_slot(0x01, PCI_CARD_SOUTHBRIDGE, 0, 0, 0, 0);
    pci_register_slot(0x08, PCI_CARD_IDE,         0, 0, 0, 0);
    pci_register_slot(0x0A, PCI_CARD_NORMAL,      1, 2, 3, 4);
    pci_register_slot(0x0B, PCI_CARD_NORMAL,      2, 3, 4, 1);
    pci_register_slot(0x0C, PCI_CARD_NORMAL,      3, 4, 1, 2);
    pci_register_slot(0x0D, PCI_CARD_NORMAL,      4, 1, 2, 3);

    device_add_params(&fdc37c6xx_device, (void *) FDC37C665);
    device_add_params(machine_get_kbc_device(machine), (void *) model->kbc_params);
    device_add(&ide_cmd640_pci_legacy_only_device);

    device_add(&sis_85c50x_device);
    device_add(&intel_flash_bxt_ami_device);

    return ret;
}

void
machine_at_sp4_common_init(const machine_t *model)
{
    machine_at_common_init(model);

    pci_init(PCI_CONFIG_TYPE_1 | FLAG_TRC_CONTROLS_CPURST);
    pci_register_slot(0x00, PCI_CARD_NORTHBRIDGE, 0, 0, 0, 0);
    pci_register_slot(0x01, PCI_CARD_SOUTHBRIDGE, 0, 0, 0, 0);
    /* Excluded: 02, 03, 04, 05, 06, 07, 08, 09, 0A, 0B, 0C, 0D, 0E, 0F, 10, 11, 12, 13, 14 */
    pci_register_slot(0x0D, PCI_CARD_IDE, 1, 2, 3, 4);
    /* Excluded: 02, 03*, 04*, 05*, 06*, 07*, 08* */
    /* Slots: 09 (04), 0A (03), 0B (02), 0C (07) */
    pci_register_slot(0x0C, PCI_CARD_NORMAL,      1, 2, 3, 4);
    pci_register_slot(0x0B, PCI_CARD_NORMAL,      2, 3, 4, 1);
    pci_register_slot(0x0A, PCI_CARD_NORMAL,      3, 4, 1, 2);
    pci_register_slot(0x09, PCI_CARD_NORMAL,      4, 1, 2, 3);

    device_add(&sis_85c50x_device);
    device_add(&ide_cmd640_pci_device);
    device_add_params(machine_get_kbc_device(machine), (void *) model->kbc_params);
    device_add_params(&fdc37c6xx_device, (void *) FDC37C665);
    device_add(&intel_flash_bxt_device);
}

int
machine_at_p5sp4_init(const machine_t *model)
{
    int ret;

    ret = bios_load_linear("roms/machines/p5sp4/0106.001",
                           0x000e0000, 131072, 0);

    if (bios_only || !ret)
        return ret;

    machine_at_sp4_common_init(model);

    return ret;
}

int
machine_at_ecs50x_init(const machine_t *model)
{
    int ret;

    ret = bios_load_linear("roms/machines/ecs50x/ECSSi5piaio.BIN",
                           0x000e0000, 131072, 0);

    if (bios_only || !ret)
        return ret;

    machine_at_common_init(model);

    pci_init(PCI_CONFIG_TYPE_1 | FLAG_TRC_CONTROLS_CPURST);
    pci_register_slot(0x00, PCI_CARD_NORTHBRIDGE, 0, 0, 0, 0);
    pci_register_slot(0x01, PCI_CARD_SOUTHBRIDGE, 0, 0, 0, 0);
    pci_register_slot(0x02, PCI_CARD_IDE,         1, 2, 3, 4);
    pci_register_slot(0x11, PCI_CARD_NORMAL,      1, 2, 3, 4);
    pci_register_slot(0x13, PCI_CARD_NORMAL,      2, 3, 4, 1);
    pci_register_slot(0x0D, PCI_CARD_NORMAL,      3, 4, 1, 2);
    pci_register_slot(0x0F, PCI_CARD_NORMAL,      4, 1, 2, 3);

    device_add(&sis_85c50x_device);
    device_add_params(&ide_cmd640_pci_device, (void *) 0x100000);
    device_add_params(machine_get_kbc_device(machine), (void *) model->kbc_params);
    device_add_params(&fdc37c6xx_device, (void *) FDC37C665);
    device_add(&intel_flash_bxt_device);

    return ret;
}
