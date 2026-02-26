/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          Implementation of Socket 3 PCI machines.
 *
 * Authors: Miran Grca, <mgrca8@gmail.com>
 *
 *          Copyright 2016-2025 Miran Grca.
 */
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <wchar.h>
#define HAVE_STDARG_H
#include <86box/86box.h>
#include "cpu.h"
#include <86box/timer.h>
#include <86box/io.h>
#include <86box/device.h>
#include <86box/chipset.h>
#include <86box/keyboard.h>
#include <86box/mem.h>
#include <86box/nvr.h>
#include <86box/pci.h>
#include <86box/dma.h>
#include <86box/fdd.h>
#include <86box/fdc.h>
#include <86box/fdc_ext.h>
#include <86box/gameport.h>
#include <86box/pic.h>
#include <86box/pit.h>
#include <86box/rom.h>
#include <86box/sio.h>
#include <86box/hdc.h>
#include <86box/port_6x.h>
#include <86box/port_92.h>
#include <86box/video.h>
#include <86box/flash.h>
#include <86box/scsi_ncr53c8xx.h>
#include <86box/hwm.h>
#include <86box/machine.h>
#include <86box/plat_unused.h>
#include <86box/sound.h>

/* ALi M1429G */
int
machine_at_ms4134_init(const machine_t *model)
{
    int ret;

    ret = bios_load_linear("roms/machines/ms4134/4alm001.bin",
                           0x000e0000, 131072, 0);

    if (bios_only || !ret)
        return ret;

    machine_at_common_ide_init(model);

    device_add(&ali1429g_device);

    device_add_params(&fdc37c6xx_device, (void *) (FDC37C665 | FDC37C6XX_IDE_PRI));

    pci_init(FLAG_MECHANISM_1 | FLAG_MECHANISM_2 | PCI_ALWAYS_EXPOSE_DEV0);
    pci_register_slot(0x00, PCI_CARD_NORTHBRIDGE, 0, 0, 0, 0);
    pci_register_slot(0x0B, PCI_CARD_SCSI,        4, 1, 2, 3);
    pci_register_slot(0x08, PCI_CARD_NORMAL,      1, 2, 3, 4);
    pci_register_slot(0x09, PCI_CARD_NORMAL,      2, 3, 4, 1);
    pci_register_slot(0x0A, PCI_CARD_NORMAL,      3, 4, 1, 2);
    pci_register_slot(0x10, PCI_CARD_NORMAL,      1, 2, 3, 4);

    device_add(&ali1435_device);
    device_add(&sst_flash_29ee010_device);

    device_add_params(machine_get_kbc_device(machine), (void *) model->kbc_params);

    return ret;
}

int
machine_at_tg486gp_init(const machine_t *model)
{
    int ret;

    ret = bios_load_linear("roms/machines/tg486gp/tg486gp.bin",
                           0x000e0000, 131072, 0);

    if (bios_only || !ret)
        return ret;

    machine_at_common_ide_init(model);

    device_add(&ali1429g_device);

    device_add_params(&fdc37c6xx_device, (void *) (FDC37C665 | FDC37C6XX_IDE_PRI));

    pci_init(FLAG_MECHANISM_1 | FLAG_MECHANISM_2 | PCI_ALWAYS_EXPOSE_DEV0);
    pci_register_slot(0x00, PCI_CARD_NORTHBRIDGE, 0, 0, 0, 0);
    pci_register_slot(0x0F, PCI_CARD_NORMAL,      1, 2, 3, 4);
    pci_register_slot(0x0D, PCI_CARD_NORMAL,      2, 3, 4, 1);
    pci_register_slot(0x0B, PCI_CARD_NORMAL,      3, 4, 1, 2);
    pci_register_slot(0x10, PCI_CARD_NORMAL,      1, 2, 3, 4);

    device_add(&ali1435_device);
    device_add(&sst_flash_29ee010_device);

    device_add_params(machine_get_kbc_device(machine), (void *) model->kbc_params);

    return ret;
}

/* ALi M1489 */
int
machine_at_sbc490_init(const machine_t *model)
{
    int ret;

    ret = bios_load_linear("roms/machines/sbc490/07159589.rom",
                           0x000e0000, 131072, 0);

    if (bios_only || !ret)
        return ret;

    machine_at_common_init(model);

    pci_init(PCI_CONFIG_TYPE_1);
    pci_register_slot(0x00, PCI_CARD_NORTHBRIDGE, 0, 0, 0, 0);
    pci_register_slot(0x0F, PCI_CARD_NORMAL,      2, 3, 4, 1);
    pci_register_slot(0x0E, PCI_CARD_NORMAL,      3, 4, 1, 2);
    pci_register_slot(0x0D, PCI_CARD_NORMAL,      4, 1, 2, 3);
    pci_register_slot(0x0C, PCI_CARD_NORMAL,      1, 2, 3, 4);
    pci_register_slot(0x01, PCI_CARD_VIDEO,       4, 1, 2, 3);

    if (gfxcard[0] == VID_INTERNAL)
        device_add(machine_get_vid_device(machine));

    device_add(&ali1489_device);
    device_add_params(&fdc37c6xx_device, (void *) FDC37C665);

    device_add_params(machine_get_kbc_device(machine), (void *) model->kbc_params);

    device_add(&sst_flash_29ee010_device);

    return ret;
}

int
machine_at_abpb4_init(const machine_t *model)
{
    int ret;

    ret = bios_load_linear("roms/machines/abpb4/486-AB-PB4.BIN",
                           0x000e0000, 131072, 0);

    if (bios_only || !ret)
        return ret;

    machine_at_common_init(model);

    pci_init(PCI_CAN_SWITCH_TYPE);
    pci_register_slot(0x00, PCI_CARD_NORTHBRIDGE, 0, 0, 0, 0);
    pci_register_slot(0x03, PCI_CARD_NORMAL,      1, 2, 3, 4);
    pci_register_slot(0x04, PCI_CARD_NORMAL,      2, 3, 4, 1);
    pci_register_slot(0x05, PCI_CARD_NORMAL,      3, 4, 1, 2);

    device_add(&ali1489_device);
    device_add_params(&w837x7_device, (void *) (W83787F | W837X7_KEY_89));
    device_add_params(machine_get_kbc_device(machine), (void *) model->kbc_params);
    device_add(&sst_flash_29ee010_device);

    return ret;
}

int
machine_at_arb1476_init(const machine_t *model)
{
    int ret;

    ret = bios_load_linear("roms/machines/arb1476/w1476b.v21",
                           0x000e0000, 131072, 0);

    if (bios_only || !ret)
        return ret;

    machine_at_common_init(model);
    pci_init(PCI_CONFIG_TYPE_1);
    pci_register_slot(0x00, PCI_CARD_NORTHBRIDGE, 0, 0, 0, 0);

    device_add(&ali1489_device);
    device_add_params(&fdc37c669_device, (void *) 0);

    device_add_params(machine_get_kbc_device(machine), (void *) model->kbc_params);

    device_add(&sst_flash_29ee010_device);

    return ret;
}

int
machine_at_win486pci_init(const machine_t *model)
{
    int ret;

    ret = bios_load_linear("roms/machines/win486pci/v1hj3.BIN",
                           0x000e0000, 131072, 0);

    if (bios_only || !ret)
        return ret;

    machine_at_common_init(model);

    pci_init(PCI_CONFIG_TYPE_1);
    pci_register_slot(0x00, PCI_CARD_NORTHBRIDGE, 0, 0, 0, 0);
    pci_register_slot(0x03, PCI_CARD_NORMAL,      1, 2, 3, 4);
    pci_register_slot(0x04, PCI_CARD_NORMAL,      2, 3, 4, 1);
    pci_register_slot(0x05, PCI_CARD_NORMAL,      3, 4, 1, 2);

    device_add(&ali1489_device);
    device_add_params(&gm82c803ab_device, (void *) GM82C803B);
    device_add_params(machine_get_kbc_device(machine), (void *) model->kbc_params);

    return ret;
}

int
machine_at_tf486_init(const machine_t *model)
{
    int ret;

    ret = bios_load_linear("roms/machines/tf486/tf486v10.BIN",
                           0x000e0000, 131072, 0);

    if (bios_only || !ret)
        return ret;

    machine_at_common_init(model);

    pci_init(PCI_CONFIG_TYPE_1);
    pci_register_slot(0x00, PCI_CARD_NORTHBRIDGE, 0, 0, 0, 0);
    pci_register_slot(0x0C, PCI_CARD_NORMAL,      1, 2, 3, 4);

    device_add(&ali1489_device);

    device_add_params(machine_get_kbc_device(machine), (void *) model->kbc_params);

    device_add_params(&w83977_device, (void *) (W83977EF | W83977_NO_NVR));
    device_add(&sst_flash_29ee010_device);

    return ret;
}

int
machine_at_ms4145_init(const machine_t *model)
{
    int ret;

    ret = bios_load_linear("roms/machines/ms4145/AG56S.ROM",
                           0x000e0000, 131072, 0);

    if (bios_only || !ret)
        return ret;

    machine_at_common_init(model);

    pci_init(PCI_CONFIG_TYPE_1);
    pci_register_slot(0x00, PCI_CARD_NORTHBRIDGE, 0, 0, 0, 0);
    pci_register_slot(0x03, PCI_CARD_NORMAL,      1, 2, 3, 4);
    pci_register_slot(0x04, PCI_CARD_NORMAL,      2, 3, 4, 1);
    pci_register_slot(0x05, PCI_CARD_NORMAL,      3, 4, 1, 2);
    pci_register_slot(0x06, PCI_CARD_NORMAL,      4, 1, 2, 3);

    device_add(&ali1489_device);
    device_add_params(&w837x7_device, (void *) (W83787F | W837X7_KEY_88));

    device_add_params(machine_get_kbc_device(machine), (void *) model->kbc_params);

    device_add(&sst_flash_29ee010_device);

    return ret;
}

/* OPTi 802G */
static const device_config_t pc330_6573_config[] = {
    // clang-format off
    {
        .name           = "bios",
        .description    = "BIOS Language",
        .type           = CONFIG_BIOS,
        .default_string = "pc330_6573",
        .default_int    = 0,
        .file_filter    = NULL,
        .spinner        = { 0 },
        .selection      = { { 0 } },
        .bios           = {
            {
                .name          = "English (PC 330, type 6573)",
                .internal_name = "pc330_6573", .bios_type = BIOS_NORMAL,
                .files_no      = 1,
                .local         = 0,
                .size          = 131072,
                .files         = { "roms/machines/pc330_6573/$IMAGES.USF", "" }
            },
            {
                .name          = "Japanese (Aptiva 510/710/Vision)",
                .internal_name = "aptiva510",
                .bios_type     = BIOS_NORMAL,
                .files_no      = 1,
                .local         = 0,
                .size          = 131072,
                .files         = { "roms/machines/pc330_6573/aptiva510_$IMAGES.USF", "" }
            },
            { .files_no = 0 }
        }
    },
    { .name = "", .description = "", .type = CONFIG_END }
    // clang-format on
};

const device_t pc330_6573_device = {
    .name          = "IBM PC 330 (type 6573)",
    .internal_name = "pc330_6573_device",
    .flags         = 0,
    .local         = 0,
    .init          = NULL,
    .close         = NULL,
    .reset         = NULL,
    .available     = NULL,
    .speed_changed = NULL,
    .force_redraw  = NULL,
    .config        = pc330_6573_config
};

int
machine_at_pc330_6573_init(const machine_t *model)
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
    device_add(&ide_vlb_2ch_device);

    pci_init(PCI_CONFIG_TYPE_1);
    pci_register_slot(0x10, PCI_CARD_NORTHBRIDGE,  0,  0,  0,  0);
    pci_register_slot(0x0B, PCI_CARD_NORMAL,       1,  2,  3,  4);
    pci_register_slot(0x0C, PCI_CARD_NORMAL,       5,  6,  7,  8);
    pci_register_slot(0x0D, PCI_CARD_NORMAL,       9, 10, 11, 12);
    /* This is a guess because the BIOS always gives it a video BIOS
       and never gives it an IRQ, so it is impossible to known for
       certain until we obtain PCI readouts from the real machine. */
    pci_register_slot(0x0E, PCI_CARD_VIDEO,       13, 14, 15, 16);

    if (gfxcard[0] == VID_INTERNAL)
        device_add(machine_get_vid_device(machine));

    device_add(&opti602_device);
    device_add(&opti802g_device);
    device_add(&opti822_device);

    device_add_params(machine_get_kbc_device(machine), (void *) model->kbc_params);

    device_add_params(&fdc37c6xx_device, (void *) (FDC37C665 | FDC37C6XX_IDE_SEC));
    device_add(&ide_opti611_vlb_device);
    device_add(&intel_flash_bxt_device);

    return ret;
}

/* OPTi 895 */
static const device_config_t pb450_config[] = {
    // clang-format off
    {
        .name           = "bios",
        .description    = "BIOS Version",
        .type           = CONFIG_BIOS,
        .default_string = "pb450a",
        .default_int    = 0,
        .file_filter    = NULL,
        .spinner        = { 0 },
        .selection      = { { 0 } },
        .bios           = {
            {
                .name          = "PhoenixBIOS 4.03 - Revision PCI 1.0A",
                .internal_name = "pb450a_pci10a" /*"pci10a"*/,
                .bios_type     = BIOS_NORMAL,
                .files_no      = 1,
                .local         = 0,
                .size          = 131072,
                .files         = { "roms/machines/pb450/OPTI802.bin", "" }
            },
            {
                .name          = "PhoenixBIOS 4.03 - Revision PNP 1.1A",
                .internal_name = "pb450a",
                .bios_type     = BIOS_NORMAL,
                .files_no      = 1,
                .local         = 0,
                .size          = 131072,
                .files         = { "roms/machines/pb450/PNP11A.bin", "" }
            },
            {
                .name          = "PhoenixBIOS 4.05 - Revision P4HS20 (by Micro Firmware)",
                .internal_name = "pb450a_p4hs20",
                .bios_type     = BIOS_NORMAL,
                .files_no      = 1,
                .local         = 0,
                .size          = 131072,
                .files         = { "roms/machines/pb450/p4hs20.bin", "" }
            },
            { .files_no = 0 }
        }
    },
    { .name = "", .description = "", .type = CONFIG_END }
    // clang-format on
};

const device_t pb450_device = {
    .name          = "Packard Bell PB450",
    .internal_name = "pb450_device",
    .flags         = 0,
    .local         = 0,
    .init          = NULL,
    .close         = NULL,
    .reset         = NULL,
    .available     = NULL,
    .speed_changed = NULL,
    .force_redraw  = NULL,
    .config        = pb450_config
};

int
machine_at_pb450_init(const machine_t *model)
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
    device_add(&ide_vlb_2ch_device);

    pci_init(PCI_CONFIG_TYPE_1);
    pci_register_slot(0x10, PCI_CARD_NORTHBRIDGE, 0, 0, 0, 0);
    pci_register_slot(0x11, PCI_CARD_NORMAL,      1, 2, 3, 4);
    pci_register_slot(0x12, PCI_CARD_NORMAL,      5, 6, 7, 8);

    if (gfxcard[0] == VID_INTERNAL)
        device_add(machine_get_vid_device(machine));

    device_add(&opti895_device);
    device_add(&opti602_device);
    device_add(&opti822_device);
    device_add_params(machine_get_kbc_device(machine), (void *) model->kbc_params);
    device_add_params(&fdc37c6xx_device, (void *) (FDC37C665 | FDC37C6XX_IDE_SEC));
    device_add(&ide_opti611_vlb_device);
    device_add(&intel_flash_bxt_device);
    device_add(&phoenix_486_jumper_pci_device);

    return ret;
}

/* i420EX */
int
machine_at_486pi_init(const machine_t *model)
{
    int ret;

    ret = bios_load_linear("roms/machines/486pi/486pi.bin",
                           0x000e0000, 131072, 0);

    if (bios_only || !ret)
        return ret;

    machine_at_common_init(model);

    pci_init(PCI_CONFIG_TYPE_1);
    pci_register_slot(0x05, PCI_CARD_NORTHBRIDGE, 0, 0, 0, 0);
    pci_register_slot(0x12, PCI_CARD_NORMAL,      1, 2, 1, 2);
    pci_register_slot(0x13, PCI_CARD_NORMAL,      2, 1, 2, 1);
    pci_register_slot(0x14, PCI_CARD_NORMAL,      1, 2, 1, 2);

    device_add_params(machine_get_kbc_device(machine), (void *) model->kbc_params);

    device_add_params(&fdc37c6xx_device, (void *) FDC37C665);
    device_add(&i420ex_device);

    return ret;
}

int
machine_at_bat4ip3e_init(const machine_t *model)
{
    int ret;

    ret = bios_load_linear("roms/machines/bat4ip3e/404C.ROM",
                           0x000e0000, 131072, 0);

    if (bios_only || !ret)
        return ret;

    machine_at_common_init(model);

    pci_init(PCI_CONFIG_TYPE_1);
    pci_register_slot(0x05, PCI_CARD_NORTHBRIDGE, 0, 0, 0, 0);
    pci_register_slot(0x01, PCI_CARD_IDE,   0xfe, 0xff, 0, 0);
    pci_register_slot(0x08, PCI_CARD_NORMAL,      1, 2, 1, 2);
    pci_register_slot(0x09, PCI_CARD_NORMAL,      2, 1, 2, 1);
    pci_register_slot(0x0a, PCI_CARD_NORMAL,      1, 2, 1, 2);

    device_add(&phoenix_486_jumper_pci_device);

    device_add_params(machine_get_kbc_device(machine), (void *) model->kbc_params);

    device_add(&i420ex_device);
    device_add(&ide_cmd640_pci_device);
    device_add_params(&fdc37c6xx_device, (void *) FDC37C665);

    return ret;
}

int
machine_at_486ap4_init(const machine_t *model)
{
    int ret;

    ret = bios_load_linear("roms/machines/486ap4/0205.002",
                           0x000e0000, 131072, 0);

    if (bios_only || !ret)
        return ret;

    machine_at_common_init(model);

    pci_init(PCI_CONFIG_TYPE_1);
    /* Excluded: 5, 6, 7, 8 */
    pci_register_slot(0x05, PCI_CARD_NORTHBRIDGE, 0, 0, 0, 0);
    pci_register_slot(0x09, PCI_CARD_NORMAL,      1, 2, 3, 4); /* 09 = Slot 1 */
    pci_register_slot(0x0a, PCI_CARD_NORMAL,      2, 3, 4, 1); /* 0a = Slot 2 */
    pci_register_slot(0x0b, PCI_CARD_NORMAL,      3, 4, 1, 2); /* 0b = Slot 3 */
    pci_register_slot(0x0c, PCI_CARD_NORMAL,      4, 1, 2, 3); /* 0c = Slot 4 */

    device_add_params(machine_get_kbc_device(machine), (void *) model->kbc_params);

    if (fdc_current[0] == FDC_INTERNAL)
        device_add(&fdc_at_device);

    device_add(&i420ex_device);

    return ret;
}

int
machine_at_ninja_init(const machine_t *model)
{
    int ret;

    ret = bios_load_linear_combined("roms/machines/ninja/1008AY0_.BIO",
                                    "roms/machines/ninja/1008AY0_.BI1", 0x1c000, 128);

    if (bios_only || !ret)
        return ret;

    machine_at_common_init(model);

    pci_init(PCI_CONFIG_TYPE_1);
    pci_register_slot(0x05, PCI_CARD_NORTHBRIDGE, 0, 0, 0, 0);
    pci_register_slot(0x11, PCI_CARD_NORMAL,      1, 2, 1, 2);
    pci_register_slot(0x13, PCI_CARD_NORMAL,      2, 1, 2, 1);
    pci_register_slot(0x0B, PCI_CARD_NORMAL,      2, 1, 2, 1);

    machine_force_ps2(1);
    device_add_params(machine_get_kbc_device(machine), (void *) model->kbc_params);

    device_add(&intel_flash_bxt_ami_device);

    device_add(&i420ex_device);
    device_add_params(&i82091aa_device, (void *) I82091AA_022);

    return ret;
}

int
machine_at_sb486p_init(const machine_t *model)
{
    int ret;

    ret = bios_load_linear("roms/machines/sb486p/amiboot.rom",
                           0x000e0000, 131072, 0);

    if (bios_only || !ret)
        return ret;

    machine_at_common_init(model);

    pci_init(PCI_CONFIG_TYPE_1);
    pci_register_slot(0x05, PCI_CARD_NORTHBRIDGE, 0, 0, 0, 0);
    pci_register_slot(0x14, PCI_CARD_NORMAL,      1, 2, 1, 2);
    pci_register_slot(0x13, PCI_CARD_NORMAL,      2, 1, 2, 1);

    device_add_params(machine_get_kbc_device(machine), (void *) model->kbc_params);

    device_add_params(&i82091aa_device, (void *) I82091AA_26E);
    device_add(&i420ex_device);

    return ret;
}

/* i420TX */
int
machine_at_amis76_init(const machine_t *model)
{
    int ret;

    ret = bios_load_linear_inverted("roms/machines/s76p/S76P.ROM",
                                    0x000e0000, 131072, 0);

    if (bios_only || !ret)
        return ret;

    machine_at_common_init(model);
    pci_init(PCI_CONFIG_TYPE_2 | PCI_NO_IRQ_STEERING);
    pci_register_slot(0x00, PCI_CARD_NORTHBRIDGE, 0, 0, 0, 0);
    // pci_register_slot(0x01, PCI_CARD_IDE,         1, 2, 3 ,4);
    pci_register_slot(0x0E, PCI_CARD_NORMAL,      1, 2, 3, 4);
    pci_register_slot(0x0F, PCI_CARD_NORMAL,      2, 3, 4, 1);
    pci_register_slot(0x02, PCI_CARD_SOUTHBRIDGE, 0, 0, 0, 0);

    device_add_params(machine_get_kbc_device(machine), (void *) model->kbc_params);

    device_add(&sio_device);
    device_add_params(&fdc37c6xx_device, (void *) FDC37C665);
    device_add(&intel_flash_bxt_ami_device);

    device_add(&i420tx_device);
    // device_add(&ide_cmd640_pci_device); /* is this actually cmd640? is it single channel? */
    device_add(&ide_pci_device);

    return ret;
}

int
machine_at_486sp3_init(const machine_t *model)
{
    int ret;

    ret = bios_load_linear("roms/machines/486sp3/awsi2737.bin",
                           0x000e0000, 131072, 0);

    if (bios_only || !ret)
        return ret;

    machine_at_common_init(model);
    device_add(&ide_isa_device);

    pci_init(PCI_CONFIG_TYPE_2 | PCI_NO_IRQ_STEERING);
    pci_register_slot(0x00, PCI_CARD_NORTHBRIDGE, 0, 0, 0, 0);
    pci_register_slot(0x01, PCI_CARD_SCSI,        1, 2, 3, 4); /* 01 = SCSI */
    pci_register_slot(0x03, PCI_CARD_NORMAL,      1, 2, 3, 4); /* 03 = Slot 1 */
    pci_register_slot(0x04, PCI_CARD_NORMAL,      2, 3, 4, 1); /* 04 = Slot 2 */
    pci_register_slot(0x05, PCI_CARD_NORMAL,      3, 4, 1, 2); /* 05 = Slot 3 */
    pci_register_slot(0x06, PCI_CARD_NORMAL,      4, 1, 2, 3); /* 06 = Slot 4 */
    pci_register_slot(0x02, PCI_CARD_SOUTHBRIDGE, 0, 0, 0, 0);

    device_add_params(machine_get_kbc_device(machine), (void *) model->kbc_params);

    device_add(&sio_device);
    device_add_params(&fdc37c6xx_device, (void *) (FDC37C663 | FDC37C6XX_IDE_PRI));
    device_add(&sst_flash_29ee010_device);

    device_add(&i420tx_device);
    device_add(&ncr53c810_onboard_pci_device);

    return ret;
}

int
machine_at_alfredo_init(const machine_t *model)
{
    int ret;

    ret = bios_load_linear_combined("roms/machines/alfredo/1010AQ0_.BIO",
                                    "roms/machines/alfredo/1010AQ0_.BI1", 0x1c000, 128);

    if (bios_only || !ret)
        return ret;

    machine_at_common_init(model);

    device_add(&ide_pci_device);

    pci_init(PCI_CONFIG_TYPE_2 | PCI_NO_IRQ_STEERING);
    pci_register_slot(0x00, PCI_CARD_NORTHBRIDGE, 0, 0, 0, 0);
    pci_register_slot(0x01, PCI_CARD_IDE,         0, 0, 0, 0);
    pci_register_slot(0x06, PCI_CARD_NORMAL,      3, 2, 1, 4);
    pci_register_slot(0x0E, PCI_CARD_NORMAL,      2, 1, 3, 4);
    pci_register_slot(0x0C, PCI_CARD_NORMAL,      1, 3, 2, 4);
    pci_register_slot(0x02, PCI_CARD_SOUTHBRIDGE, 0, 0, 0, 0);

    device_add_params(machine_get_kbc_device(machine), (void *) model->kbc_params);

    device_add(&sio_device);
    device_add_params(&fdc37c6xx_device, (void *) FDC37C663);
    device_add(&intel_flash_bxt_ami_device);

    device_add(&i420tx_device);

    return ret;
}

/* i420ZX */
int
machine_at_486sp3g_init(const machine_t *model)
{
    int ret;

    ret = bios_load_linear("roms/machines/486sp3g/PCI-I-486SP3G_0306.001 (Beta).bin",
                           0x000e0000, 131072, 0);

    if (bios_only || !ret)
        return ret;

    machine_at_common_init(model);
    device_add(&ide_isa_device);

    pci_init(PCI_CONFIG_TYPE_2);
    pci_register_slot(0x00, PCI_CARD_NORTHBRIDGE, 0, 0, 0, 0);
    pci_register_slot(0x01, PCI_CARD_SCSI,        1, 2, 3, 4); /* 01 = SCSI */
    pci_register_slot(0x06, PCI_CARD_NORMAL,      1, 2, 3, 4); /* 06 = Slot 1 */
    pci_register_slot(0x05, PCI_CARD_NORMAL,      2, 3, 4, 1); /* 05 = Slot 2 */
    pci_register_slot(0x04, PCI_CARD_NORMAL,      3, 4, 1, 2); /* 04 = Slot 3 */
    pci_register_slot(0x02, PCI_CARD_SOUTHBRIDGE, 0, 0, 0, 0);

    device_add_params(machine_get_kbc_device(machine), (void *) model->kbc_params);

    device_add(&sio_zb_device);
    device_add_params(&pc873xx_device, (void *) (PC87332 | PCX73XX_IDE_PRI | PCX730X_398));
    device_add(&sst_flash_29ee010_device);

    device_add(&i420zx_device);
    device_add(&ncr53c810_onboard_pci_device);

    return ret;
}

static const device_config_t sb486pv_config[] = {
    // clang-format off
    {
        .name           = "bios",
        .description    = "BIOS Version",
        .type           = CONFIG_BIOS,
        .default_string = "sb486pv",
        .default_int    = 0,
        .file_filter    = NULL,
        .spinner        = { 0 },
        .selection      = { { 0 } },
        .bios           = {
            {
                .name          = "AMI WinBIOS (062594) - Revision 0108",
                .internal_name = "sb486pv_0108",
                .bios_type     = BIOS_NORMAL,
                .files_no      = 1,
                .local         = 0,
                .size          = 131072,
                .files         = { "roms/machines/sb486pv/41-0108-062594-SATURN2.rom", "" }
            },
            {
                .name          = "AMI WinBIOS (062594) - Revision 0301",
                .internal_name = "sb486pv_0301",
                .bios_type     = BIOS_NORMAL,
                .files_no      = 1,
                .local         = 0,
                .size          = 131072,
                .files         = { "roms/machines/sb486pv/0301-062594-SATURN2.rom", "" }
            },
            {
                .name          = "AMIBIOS 6 (071595) - Revision 1301",
                .internal_name = "sb486pv",
                .bios_type     = BIOS_NORMAL,
                .files_no      = 1,
                .local         = 0,
                .size          = 131072,
                .files         = { "roms/machines/sb486pv/amiboot.rom", "" }
            },
            { .files_no = 0 }
        }
    },
    { .name = "", .description = "", .type = CONFIG_END }
    // clang-format on
};

const device_t sb486pv_device = {
    .name          = "ICS SB486PV",
    .internal_name = "sb486pv_device",
    .flags         = 0,
    .local         = 0,
    .init          = NULL,
    .close         = NULL,
    .reset         = NULL,
    .available     = NULL,
    .speed_changed = NULL,
    .force_redraw  = NULL,
    .config        = sb486pv_config
};

int
machine_at_sb486pv_init(const machine_t *model)
{
    int         ret = 0;
    const char *fn;

    /* No ROMs available */
    if (!device_available(model->device))
        return ret;

    device_context(model->device);
    fn = device_get_bios_file(machine_get_device(machine), device_get_config_bios("bios"), 0);
    if (!strcmp(fn, "roms/machines/sb486pv/amiboot.rom"))
        ret = bios_load_linear(fn, 0x000e0000, 131072, 0);
    else
        ret = bios_load_linear_inverted(fn, 0x000e0000, 131072, 0);
    device_context_restore();

    machine_at_common_init(model);

    pci_init(PCI_CONFIG_TYPE_2);
    pci_register_slot(0x00, PCI_CARD_NORTHBRIDGE, 0, 0, 0, 0);
    pci_register_slot(0x0e, PCI_CARD_IDE,         0, 0, 0, 0);
    pci_register_slot(0x0f, PCI_CARD_VIDEO,       1, 2, 3, 4);
    pci_register_slot(0x02, PCI_CARD_SOUTHBRIDGE, 0, 0, 0, 0);

    if (gfxcard[0] == VID_INTERNAL)
        device_add(machine_get_vid_device(machine));

    device_add_params(machine_get_kbc_device(machine), (void *) model->kbc_params);
    device_add(&sio_zb_device);
    device_add(&ide_rz1000_pci_single_channel_device);
    device_add_params(&i82091aa_device, (void *) I82091AA_26E);
    if (!strcmp(fn, "roms/machines/sb486pv/amiboot.rom"))
        device_add(&intel_flash_bxt_device);
    else
        device_add(&intel_flash_bxt_ami_device);

    device_add(&i420zx_device);

    return ret;
}

/* IMS 8848 */
int
machine_at_pci400cb_init(const machine_t *model)
{
    int ret;

    ret = bios_load_linear("roms/machines/pci400cb/032295.ROM",
                           0x000e0000, 131072, 0);

    if (bios_only || !ret)
        return ret;

    machine_at_common_init(model);

    pci_init(PCI_CONFIG_TYPE_1);
    pci_register_slot(0x00, PCI_CARD_NORTHBRIDGE, 0, 0, 0, 0);
    pci_register_slot(0x0F, PCI_CARD_NORMAL,      4, 3, 2, 1); /* 0F = Slot 1 */
    pci_register_slot(0x0E, PCI_CARD_NORMAL,      3, 4, 1, 2); /* 0E = Slot 2 */
    pci_register_slot(0x0D, PCI_CARD_NORMAL,      2, 3, 4, 1); /* 0D = Slot 3 */
    pci_register_slot(0x0C, PCI_CARD_NORMAL,      1, 2, 3, 4); /* 0C = Slot 4 */

    device_add_params(machine_get_kbc_device(machine), (void *) model->kbc_params);

    device_add(&ims8848_device);

    if (fdc_current[0] == FDC_INTERNAL)
        device_add(&fdc_at_device);

    return ret;
}

/* SiS 496 */
static void
machine_at_sis_85c496_common_init(UNUSED(const machine_t *model))
{
    device_add(&ide_pci_2ch_device);

    pci_init(PCI_CONFIG_TYPE_1 | FLAG_TRC_CONTROLS_CPURST);
    pci_register_slot(0x05, PCI_CARD_NORTHBRIDGE, 0, 0, 0, 0);

    pci_set_irq_routing(PCI_INTA, PCI_IRQ_DISABLED);
    pci_set_irq_routing(PCI_INTB, PCI_IRQ_DISABLED);
    pci_set_irq_routing(PCI_INTC, PCI_IRQ_DISABLED);
    pci_set_irq_routing(PCI_INTD, PCI_IRQ_DISABLED);

    device_add_params(machine_get_kbc_device(machine), (void *) model->kbc_params);
}

int
machine_at_acerp3_init(const machine_t *model)
{
    int ret;

    ret = bios_load_linear("roms/machines/acerp3/Acer Mate 600 P3 BIOS U13 V2.0R02-J3 ACR8DE00-S00-950911-R02-J3.bin",
                           0x000e0000, 131072, 0);

    if (bios_only || !ret)
        return ret;

    machine_at_common_init(model);

    machine_at_sis_85c496_common_init(model);
    device_add(&sis_85c496_device);

    pci_register_slot(0x09, PCI_CARD_VIDEO,  0, 0, 0, 0);
    pci_register_slot(0x0A, PCI_CARD_IDE,    0, 0, 0, 0);
    pci_register_slot(0x12, PCI_CARD_NORMAL, 3, 4, 1, 2);
    pci_register_slot(0x13, PCI_CARD_NORMAL, 2, 3, 4, 1);
    pci_register_slot(0x14, PCI_CARD_NORMAL, 1, 2, 3, 4);

    device_add_params(&fdc37c6xx_device, (void *) (FDC37C665 | FDC37C6XX_IDE_PRI));
    device_add_params(machine_get_kbc_device(machine), (void *) model->kbc_params);
    device_add(&ide_cmd640_pci_legacy_only_device);

    if (gfxcard[0] == VID_INTERNAL)
        device_add(&gd5434_onboard_pci_device);

    device_add(&intel_flash_bxt_device);

    return ret;
}

int
machine_at_486sp3c_init(const machine_t *model)
{
    int ret;

    ret = bios_load_linear("roms/machines/486sp3c/SI4I0306.AWD",
                           0x000e0000, 131072, 0);

    if (bios_only || !ret)
        return ret;

    machine_at_common_init(model);

    machine_at_sis_85c496_common_init(model);
    device_add(&sis_85c496_device);

    pci_register_slot(0x0C, PCI_CARD_NORMAL, 1, 2, 3, 4);
    pci_register_slot(0x0B, PCI_CARD_NORMAL, 2, 3, 4, 1);
    pci_register_slot(0x0A, PCI_CARD_NORMAL, 3, 4, 1, 2);

    device_add_params(&fdc37c6xx_device, (void *) FDC37C665);
    device_add_params(machine_get_kbc_device(machine), (void *) model->kbc_params);

    device_add(&intel_flash_bxt_device);

    return ret;
}

int
machine_at_ls486e_init(const machine_t *model)
{
    int ret;

    ret = bios_load_linear("roms/machines/ls486e/LS486E RevC.BIN",
                           0x000e0000, 131072, 0);

    if (bios_only || !ret)
        return ret;

    machine_at_common_init(model);

    machine_at_sis_85c496_common_init(model);
    device_add(&sis_85c496_ls486e_device);

    pci_register_slot(0x0B, PCI_CARD_NORMAL, 1, 2, 3, 4);
    pci_register_slot(0x0D, PCI_CARD_NORMAL, 2, 3, 4, 1);
    pci_register_slot(0x0F, PCI_CARD_NORMAL, 3, 4, 1, 2);
    pci_register_slot(0x06, PCI_CARD_NORMAL, 4, 1, 2, 3);

    device_add_params(&fdc37c6xx_device, (void *) FDC37C665);
    device_add_params(machine_get_kbc_device(machine), (void *) model->kbc_params);

    return ret;
}

int
machine_at_m4li_init(const machine_t *model)
{
    int ret;

    ret = bios_load_linear("roms/machines/m4li/M4LI.04S",
                           0x000e0000, 131072, 0);

    if (bios_only || !ret)
        return ret;

    machine_at_common_init(model);

    machine_at_sis_85c496_common_init(model);
    device_add(&sis_85c496_device);

    pci_register_slot(0x0B, PCI_CARD_NORMAL, 1, 2, 3, 4);
    pci_register_slot(0x0D, PCI_CARD_NORMAL, 2, 3, 4, 1);
    pci_register_slot(0x07, PCI_CARD_NORMAL, 4, 1, 2, 3);
    pci_register_slot(0x0F, PCI_CARD_NORMAL, 3, 4, 1, 2);

    device_add_params(&fdc37c6xx_device, (void *) FDC37C665);
    device_add_params(machine_get_kbc_device(machine), (void *) model->kbc_params);

    return ret;
}

int
machine_at_ms4144_init(const machine_t *model)
{
    int ret;

    ret = bios_load_linear("roms/machines/ms4144/ms-4144-1.4.bin",
                           0x000e0000, 131072, 0);

    if (bios_only || !ret)
        return ret;

    machine_at_common_init(model);

    machine_at_sis_85c496_common_init(model);
    device_add(&sis_85c496_ls486e_device);

    pci_register_slot(0x03, PCI_CARD_NORMAL, 1, 2, 3, 4);
    pci_register_slot(0x0D, PCI_CARD_NORMAL, 2, 3, 4, 1);
    pci_register_slot(0x0F, PCI_CARD_NORMAL, 3, 4, 1, 2);

    device_add_params(&w837x7_device, (void *) (W83787F | W837X7_KEY_89));
    device_add_params(machine_get_kbc_device(machine), (void *) model->kbc_params);

    device_add(&sst_flash_29ee010_device);

    return ret;
}

int
machine_at_r418_init(const machine_t *model)
{
    int ret;

    ret = bios_load_linear("roms/machines/r418/r418i.bin",
                           0x000e0000, 131072, 0);

    if (bios_only || !ret)
        return ret;

    machine_at_common_init(model);

    machine_at_sis_85c496_common_init(model);
    device_add(&sis_85c496_device);

    pci_register_slot(0x0B, PCI_CARD_NORMAL, 1, 2, 3, 4);
    pci_register_slot(0x0D, PCI_CARD_NORMAL, 2, 3, 4, 1);
    pci_register_slot(0x0F, PCI_CARD_NORMAL, 3, 4, 1, 2);
    pci_register_slot(0x07, PCI_CARD_NORMAL, 4, 1, 2, 3);

    device_add_params(&fdc37c6xx_device, (void *) FDC37C665);
    device_add_params(machine_get_kbc_device(machine), (void *) model->kbc_params);

    return ret;
}

int
machine_at_4saw2_init(const machine_t *model)
{
    int ret;

    ret = bios_load_linear("roms/machines/4saw2/4saw0911.bin",
                           0x000e0000, 131072, 0);

    if (bios_only || !ret)
        return ret;

    machine_at_common_init(model);

    machine_at_sis_85c496_common_init(model);
    device_add(&sis_85c496_device);

    pci_register_slot(0x0B, PCI_CARD_NORMAL, 1, 2, 3, 4);
    pci_register_slot(0x0D, PCI_CARD_NORMAL, 2, 3, 4, 1);
    pci_register_slot(0x0F, PCI_CARD_NORMAL, 3, 4, 1, 2);
    pci_register_slot(0x11, PCI_CARD_NORMAL, 4, 1, 2, 3);

    device_add_params(&w837x7_device, (void *) (W83777F | W837X7_KEY_89));
    device_add_params(machine_get_kbc_device(machine), (void *) model->kbc_params);

    device_add(&intel_flash_bxt_device);

    return ret;
}

int
machine_at_4dps_init(const machine_t *model)
{
    int ret;

    ret = bios_load_linear("roms/machines/4dps/4DPS172G.BIN",
                           0x000e0000, 131072, 0);

    if (bios_only || !ret)
        return ret;

    machine_at_common_init(model);

    machine_at_sis_85c496_common_init(model);
    device_add(&sis_85c496_device);

    pci_register_slot(0x0B, PCI_CARD_NORMAL, 1, 2, 3, 4);
    pci_register_slot(0x0D, PCI_CARD_NORMAL, 2, 3, 4, 1);
    pci_register_slot(0x0E, PCI_CARD_NORMAL, 3, 4, 1, 2);
    pci_register_slot(0x07, PCI_CARD_NORMAL, 4, 1, 2, 3);

    device_add_params(&w837x7_device, (void *) (W83787IF | W837X7_KEY_89));
    device_add_params(machine_get_kbc_device(machine), (void *) model->kbc_params);

    device_add(&intel_flash_bxt_device);

    return ret;
}

/* UMC 8881 */
int
machine_at_atc1415_init(const machine_t *model)
{
    int ret;

    ret = bios_load_linear("roms/machines/atc1415/1415V330.ROM",
                           0x000e0000, 131072, 0);

    if (bios_only || !ret)
        return ret;

    machine_at_common_init(model);

    pci_init(PCI_CONFIG_TYPE_1);
    pci_register_slot(0x10, PCI_CARD_NORTHBRIDGE, 0, 0, 0, 0);
    pci_register_slot(0x12, PCI_CARD_SOUTHBRIDGE, 1, 2, 3, 4);
    pci_register_slot(0x0C, PCI_CARD_NORMAL,      1, 2, 3, 4);
    pci_register_slot(0x13, PCI_CARD_NORMAL,      4, 1, 2, 3);
    pci_register_slot(0x14, PCI_CARD_NORMAL,      3, 4, 1, 2);

    device_add(&umc_hb4_device);
    device_add(&umc_8886bf_device);
    device_add(&intel_flash_bxt_device);

    if (fdc_current[0] == FDC_INTERNAL)
        device_add(&fdc_at_device);

    return ret;
}

int
machine_at_84xxuuda_init(const machine_t *model)
{
    int ret;

    ret = bios_load_linear("roms/machines/84xxuuda/uud0520s.bin",
                           0x000e0000, 131072, 0);

    if (bios_only || !ret)
        return ret;

    machine_at_common_init(model);

    pci_init(PCI_CONFIG_TYPE_1);
    pci_register_slot(0x10, PCI_CARD_NORTHBRIDGE, 0, 0, 0, 0);
    pci_register_slot(0x12, PCI_CARD_SOUTHBRIDGE, 1, 2, 3, 4);
    pci_register_slot(0x03, PCI_CARD_NORMAL,      1, 2, 3, 4);
    pci_register_slot(0x04, PCI_CARD_NORMAL,      2, 3, 4, 1);
    pci_register_slot(0x05, PCI_CARD_NORMAL,      3, 4, 1, 2);

    device_add(&umc_hb4_device);
    device_add(&umc_8886bf_device);
    device_add_params(&um866x_device, (void *) UM8663BF);
    device_add(&winbond_flash_w29c010_device);

    return ret;
}

int
machine_at_pl4600c_init(const machine_t *model)
{
    int ret;

    ret = bios_load_linear("roms/machines/pl4600c/SST29EE010.BIN",
                           0x000e0000, 131072, 0);

    if (bios_only || !ret)
        return ret;

    machine_at_common_init(model);

    pci_init(PCI_CONFIG_TYPE_1);

    pci_register_slot(0x0C, PCI_CARD_NORMAL,      1, 2, 3, 4); /* Slot 01 */
    pci_register_slot(0x0D, PCI_CARD_NORMAL,      4, 1, 2, 3); /* Slot 02 */
    pci_register_slot(0x10, PCI_CARD_NORTHBRIDGE, 0, 0, 0, 0);
    pci_register_slot(0x12, PCI_CARD_SOUTHBRIDGE, 0, 0, 0, 0); /* Onboard */
    pci_register_slot(0x13, PCI_CARD_VIDEO,       0, 0, 0, 0); /* Onboard */

    device_add(&umc_hb4_device);
    device_add(&umc_8886af_device);
    device_add_params(&um866x_device, (void *) UM8663AF);
    device_add(&sst_flash_29ee010_device);
    device_add_params(machine_get_kbc_device(machine), (void *) model->kbc_params);

    if (gfxcard[0] == VID_INTERNAL)
        device_add(&gd5430_onboard_pci_device);

    if (sound_card_current[0] == SOUND_INTERNAL)
        device_add(&ess_1688_device);

    if (fdc_current[0] == FDC_INTERNAL) {
        fdd_set_turbo(0, 1);
        fdd_set_turbo(1, 1);
    }

    return ret;
}

int
machine_at_ecs486_init(const machine_t *model)
{
    int ret;

    ret = bios_load_linear("roms/machines/ecs486/8810AIO.32J",
                           0x000e0000, 131072, 0);

    if (bios_only || !ret)
        return ret;

    machine_at_common_init(model);

    pci_init(PCI_CONFIG_TYPE_1);
    pci_register_slot(0x10, PCI_CARD_NORTHBRIDGE, 0, 0, 0, 0);
    pci_register_slot(0x12, PCI_CARD_SOUTHBRIDGE, 1, 2, 3, 4);
    pci_register_slot(0x0F, PCI_CARD_IDE,         0, 0, 0, 0);
    pci_register_slot(0x0C, PCI_CARD_NORMAL,      1, 2, 3, 4);
    pci_register_slot(0x0D, PCI_CARD_NORMAL,      2, 3, 4, 1);
    pci_register_slot(0x0E, PCI_CARD_NORMAL,      3, 4, 1, 2);

    device_add(&umc_hb4_device);
    device_add(&umc_8886f_device);
    device_add(&ide_cmd640_pci_legacy_only_device);
    device_add_params(&fdc37c6xx_device, (void *) FDC37C665);
    device_add(&intel_flash_bxt_device);

    machine_force_ps2(1);
    device_add_params(machine_get_kbc_device(machine), (void *) model->kbc_params);

    return ret;
}

int
machine_at_actionpc2600_init(const machine_t *model)
{
    int ret;

    ret = bios_load_linear("roms/machines/actionpc2600/action2600.BIN",
                           0x000e0000, 131072, 0);

    if (bios_only || !ret)
        return ret;

    machine_at_common_init(model);

    pci_init(PCI_CONFIG_TYPE_1);
    pci_register_slot(0x10, PCI_CARD_NORTHBRIDGE, 0, 0, 0, 3);
    pci_register_slot(0x12, PCI_CARD_SOUTHBRIDGE, 1, 2, 3, 4);
    pci_register_slot(0x0E, PCI_CARD_VIDEO,       0, 0, 0, 0);
    pci_register_slot(0x0C, PCI_CARD_NORMAL,      1, 2, 3, 4);
    pci_register_slot(0x0D, PCI_CARD_NORMAL,      4, 1, 2, 3);

    device_add(&umc_hb4_device);
    device_add(&umc_8886bf_device);
    device_add_params(&fdc37c6xx_device, (void *) FDC37C665);
    device_add(&intel_flash_bxt_device);

    device_add_params(machine_get_kbc_device(machine), (void *) model->kbc_params);

    if (gfxcard[0] == VID_INTERNAL)
        device_add(machine_get_vid_device(machine));

    return ret;
}

int
machine_at_actiontower8400_init(const machine_t *model)
{
    int ret;

    ret = bios_load_linear("roms/machines/actiontower8400/V31C.ROM",
                           0x000e0000, 131072, 0);

    if (bios_only || !ret)
        return ret;
    machine_at_common_init(model);

    pci_init(PCI_CONFIG_TYPE_1);
    pci_register_slot(0x10, PCI_CARD_NORTHBRIDGE, 0, 0, 0, 0);
    pci_register_slot(0x12, PCI_CARD_SOUTHBRIDGE, 1, 2, 3, 4);
    pci_register_slot(0x15, PCI_CARD_VIDEO,       0, 0, 0, 0);
    pci_register_slot(0x16, PCI_CARD_IDE,         0, 0, 0, 0);
    pci_register_slot(0x13, PCI_CARD_NORMAL,      1, 2, 3, 4);
    pci_register_slot(0x14, PCI_CARD_NORMAL,      2, 3, 4, 1);

    device_add(&umc_hb4_device);
    device_add(&umc_8886f_device);
    device_add_params(&fdc37c6xx_device, (void *) FDC37C665);
    device_add(&ide_cmd640_pci_device);
    device_add(&intel_flash_bxt_device); // The ActionPC 2600 has this so I'm gonna assume this does too.

    device_add_params(machine_get_kbc_device(machine), (void *) model->kbc_params);

    if (gfxcard[0] == VID_INTERNAL)
        device_add(machine_get_vid_device(machine));

    return ret;
}

int
machine_at_m919_init(const machine_t *model)
{
    int ret;

    ret = bios_load_linear("roms/machines/m919/9190914s.rom",
                           0x000e0000, 131072, 0);

    if (bios_only || !ret)
        return ret;

    machine_at_common_init(model);

    pci_init(PCI_CONFIG_TYPE_1);
    pci_register_slot(0x10, PCI_CARD_NORTHBRIDGE, 0, 0, 0, 0);
    pci_register_slot(0x12, PCI_CARD_SOUTHBRIDGE, 1, 2, 3, 4);
    pci_register_slot(0x0C, PCI_CARD_NORMAL,      1, 2, 3, 4);
    pci_register_slot(0x0D, PCI_CARD_NORMAL,      4, 1, 2, 3);
    pci_register_slot(0x0E, PCI_CARD_NORMAL,      3, 4, 1, 2);

    device_add(&umc_hb4_device);
    device_add(&umc_8886af_device); /* AF is correct - the BIOS does IDE writes to ports 108h and 109h. */
    device_add_params(&um866x_device, (void *) UM8663BF);
    device_add(&sst_flash_29ee010_device);

    return ret;
}

int
machine_at_spc7700plw_init(const machine_t *model)
{
    int ret;

    ret = bios_load_linear("roms/machines/spc7700plw/77LW13FH.P24",
                           0x000e0000, 131072, 0);

    if (bios_only || !ret)
        return ret;

    machine_at_common_init(model);

    pci_init(PCI_CONFIG_TYPE_1);
    pci_register_slot(0x10, PCI_CARD_NORTHBRIDGE, 0, 0, 0, 0);
    pci_register_slot(0x12, PCI_CARD_SOUTHBRIDGE, 1, 2, 3, 4);
    pci_register_slot(0x0C, PCI_CARD_NORMAL,      2, 3, 4, 1);
    pci_register_slot(0x0D, PCI_CARD_NORMAL,      1, 2, 3, 4);
    pci_register_slot(0x0E, PCI_CARD_NORMAL,      3, 4, 1, 2);

    device_add(&umc_hb4_device);
    device_add(&umc_8886af_device);
    device_add_params(&fdc37c6xx_device, (void *) FDC37C665);
    device_add(&intel_flash_bxt_device);
    device_add_params(machine_get_kbc_device(machine), (void *) model->kbc_params);

    return ret;
}

static const device_config_t hot433a_config[] = {
    // clang-format off
    {
        .name           = "bios",
        .description    = "BIOS Version",
        .type           = CONFIG_BIOS,
        .default_string = "hot433a",
        .default_int    = 0,
        .file_filter    = NULL,
        .spinner        = { 0 },
        .selection      = { { 0 } },
        .bios           = {
            {
                .name          = "AMIBIOS 5 (101094) - Revision 433AUS33",
                .internal_name = "hot433a",
                .bios_type     = BIOS_NORMAL,
                .files_no      = 1,
                .local         = 0,
                .size          = 131072,
                .files         = { "roms/machines/hot433/433AUS33.ROM", "" }
            },
            {
                .name          = "Award Modular BIOS v4.51PG - Revision 2.5 (by eSupport)",
                .internal_name = "hot433a_v451pg",
                .bios_type     = BIOS_NORMAL,
                .files_no      = 1,
                .local         = 0,
                .size          = 131072,
                .files         = { "roms/machines/hot433/2A4X5H21.BIN", "" }
            },
            { .files_no = 0 }
        }
    },
    { .name = "", .description = "", .type = CONFIG_END }
    // clang-format on
};

const device_t hot433a_device = {
    .name          = "Shuttle HOT-433A",
    .internal_name = "hot433a_device",
    .flags         = 0,
    .local         = 0,
    .init          = NULL,
    .close         = NULL,
    .reset         = NULL,
    .available     = NULL,
    .speed_changed = NULL,
    .force_redraw  = NULL,
    .config        = hot433a_config
};

int
machine_at_hot433a_init(const machine_t *model)
{
    int         ret = 0;
    const char *fn;

    /* No ROMs available */
    if (!device_available(model->device))
        return ret;

    device_context(model->device);
    int is_award = !strcmp(device_get_config_bios("bios"), "hot433a_v451pg");
    fn           = device_get_bios_file(machine_get_device(machine), device_get_config_bios("bios"), 0);
    ret          = bios_load_linear(fn, 0x000e0000, 131072, 0);
    device_context_restore();

    machine_at_common_init(model);
    device_add_params(&nvr_at_device, (void *) (uintptr_t) (is_award ? (NVR_AT_ZERO_DEFAULT) : (NVR_AMI_1994)));

    pci_init(PCI_CONFIG_TYPE_1);
    pci_register_slot(0x10, PCI_CARD_NORTHBRIDGE, 0, 0, 0, 0);
    pci_register_slot(0x12, PCI_CARD_SOUTHBRIDGE, 1, 2, 3, 4);
    pci_register_slot(0x0C, PCI_CARD_NORMAL,      1, 2, 3, 4);
    pci_register_slot(0x0D, PCI_CARD_NORMAL,      4, 1, 2, 3);
    pci_register_slot(0x0E, PCI_CARD_NORMAL,      3, 4, 1, 2);
    pci_register_slot(0x0F, PCI_CARD_NORMAL,      2, 3, 4, 1);

    device_add(&umc_hb4_device);
    device_add(&umc_8886bf_device);
    if (is_award)
        device_add_params(&um866x_device, (void *) UM8663AF);
    else
        device_add_params(&um8669f_device, (void *) 0);
    device_add(&winbond_flash_w29c010_device);
    if (is_award)
        machine_force_ps2(1);
    else
        machine_force_ps2(0);
    device_add_params(machine_get_kbc_device(machine), (void *) model->kbc_params);

    pic_toggle_latch(is_award);

    return ret;
}

/* VIA VT82C496G */
int
machine_at_g486vpa_init(const machine_t *model)
{
    int ret;

    ret = bios_load_linear("roms/machines/g486vpa/3.BIN",
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

    device_add(&via_vt82c49x_pci_ide_device);
    device_add(&via_vt82c505_device);
    device_add_params(&pc873xx_device, (void *) (PC87332 | PCX73XX_IDE_SEC | PCX730X_398));
    device_add_params(machine_get_kbc_device(machine), (void *) model->kbc_params);
    device_add(&sst_flash_29ee010_device);

    return ret;
}

int
machine_at_486vipio2_init(const machine_t *model)
{
    int ret;

    ret = bios_load_linear("roms/machines/486vipio2/1175G701.BIN",
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

    device_add(&via_vt82c49x_pci_ide_device);
    device_add(&via_vt82c505_device);
    device_add_params(&w837x7_device, (void *) (W83787F | W837X7_KEY_89));

    device_add_params(machine_get_kbc_device(machine), (void *) model->kbc_params);

    device_add(&winbond_flash_w29c010_device);

    return ret;
}
