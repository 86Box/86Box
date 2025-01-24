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
 *
 *
 * Authors: Miran Grca, <mgrca8@gmail.com>
 *
 *          Copyright 2016-2019 Miran Grca.
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

void
machine_at_premiere_common_init(const machine_t *model, int pci_switch)
{
    machine_at_common_init(model);
    device_add(&ide_pci_device);

    pci_init(PCI_CONFIG_TYPE_2 | pci_switch);
    pci_register_slot(0x00, PCI_CARD_NORTHBRIDGE, 0, 0, 0, 0);
    pci_register_slot(0x01, PCI_CARD_IDE,         0, 0, 0, 0);
    pci_register_slot(0x06, PCI_CARD_NORMAL,      3, 2, 1, 4);
    pci_register_slot(0x0E, PCI_CARD_NORMAL,      2, 1, 3, 4);
    pci_register_slot(0x0C, PCI_CARD_NORMAL,      1, 3, 2, 4);
    pci_register_slot(0x02, PCI_CARD_SOUTHBRIDGE, 0, 0, 0, 0);
    device_add(&keyboard_ps2_intel_ami_pci_device);
    device_add(&sio_zb_device);
    device_add(&fdc37c665_device);
    device_add(&intel_flash_bxt_ami_device);
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
    device_add(&keyboard_ps2_ami_pci_device);
    device_add(&fdc37c665_device);
    device_add(&intel_flash_bxt_device);
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
    device_add(&fdc37c665_device);
    device_add(&keyboard_ps2_ami_pci_device);
    device_add(&ide_cmd640_pci_legacy_only_device);

    device_add(&i430lx_device);
    device_add(&sio_zb_device);
    device_add(&intel_flash_bxt_ami_device);

    return ret;
}

int
machine_at_p5mp3_init(const machine_t *model)
{
    int ret;

    ret = bios_load_linear("roms/machines/p5mp3/0205.bin",
                           0x000e0000, 131072, 0);

    if (bios_only || !ret)
        return ret;

    machine_at_common_init(model);

    pci_init(PCI_CONFIG_TYPE_2 | PCI_NO_IRQ_STEERING);
    pci_register_slot(0x00, PCI_CARD_NORTHBRIDGE, 0, 0, 0, 0);
    pci_register_slot(0x05, PCI_CARD_NORMAL,      1, 2, 3, 4); /* 05 = Slot 1 */
    pci_register_slot(0x04, PCI_CARD_NORMAL,      2, 3, 4, 1); /* 04 = Slot 2 */
    pci_register_slot(0x03, PCI_CARD_NORMAL,      3, 4, 1, 2); /* 03 = Slot 3 */
    pci_register_slot(0x02, PCI_CARD_SOUTHBRIDGE, 0, 0, 0, 0);
    device_add(&fdc_at_device);
    device_add(&keyboard_ps2_pci_device);

    device_add(&sio_zb_device);
    device_add(&catalyst_flash_device);
    device_add(&i430lx_device);

    return ret;
}

int
machine_at_dellxp60_init(const machine_t *model)
{
    int ret;

    ret = bios_load_linear_inverted("roms/machines/dellxp60/XP60-A08.ROM",
                                    0x000e0000, 131072, 0);

    if (bios_only || !ret)
        return ret;

    machine_at_common_init(model);
    device_add(&ide_pci_device);

    pci_init(PCI_CONFIG_TYPE_2);
    pci_register_slot(0x00, PCI_CARD_NORTHBRIDGE, 0, 0, 0, 0);
    /* Not: 00, 02, 03, 04, 05, 06, 07, 08, 09, 0A, 0B, 0C, 0D, 0E, 0F. */
    /* Yes: 01, 10, 11, 12, 13, 14. */
    pci_register_slot(0x01, PCI_CARD_NORMAL,      1, 3, 2, 4);
    pci_register_slot(0x04, PCI_CARD_NORMAL,      4, 4, 3, 3);
    pci_register_slot(0x05, PCI_CARD_NORMAL,      1, 4, 3, 2);
    pci_register_slot(0x06, PCI_CARD_NORMAL,      2, 1, 3, 4);
    pci_register_slot(0x02, PCI_CARD_SOUTHBRIDGE, 0, 0, 0, 0);
    device_add(&i430lx_device);
    device_add(&keyboard_ps2_intel_ami_pci_device);
    device_add(&sio_zb_device);
    device_add(&fdc37c665_device);
    device_add(&intel_flash_bxt_ami_device);

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
    device_add(&ide_pci_2ch_device);

    pci_init(PCI_CONFIG_TYPE_2);
    pci_register_slot(0x00, PCI_CARD_NORTHBRIDGE, 0, 0, 0, 0);
    pci_register_slot(0x03, PCI_CARD_NORMAL,      4, 4, 3, 3);
    pci_register_slot(0x07, PCI_CARD_NORMAL,      1, 4, 3, 2);
    pci_register_slot(0x08, PCI_CARD_NORMAL,      2, 1, 3, 4);
    pci_register_slot(0x02, PCI_CARD_SOUTHBRIDGE, 0, 0, 0, 0);
    device_add(&i430lx_device);
    device_add(&keyboard_ps2_intel_ami_pci_device);
    device_add(&sio_zb_device);
    device_add(&i82091aa_device);
    device_add(&intel_flash_bxt_ami_device);

    return ret;
}

int
machine_at_ambradp60_init(const machine_t *model)
{
    int ret;

    ret = bios_load_linear_combined("roms/machines/ambradp60/1004AF1P.BIO",
                                    "roms/machines/ambradp60/1004AF1P.BI1",
                                    0x1c000, 128);

    if (bios_only || !ret)
        return ret;

    machine_at_premiere_common_init(model, 0);

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
    device_add(&ide_pci_2ch_device);

    pci_init(PCI_CONFIG_TYPE_2 | PCI_NO_IRQ_STEERING);
    pci_register_slot(0x00, PCI_CARD_NORTHBRIDGE, 0, 0, 0, 0);
    pci_register_slot(0x01, PCI_CARD_IDE,         0, 0, 0, 0);
    pci_register_slot(0x03, PCI_CARD_VIDEO,       3, 3, 3, 3);
    pci_register_slot(0x06, PCI_CARD_NORMAL,      3, 2, 1, 4);
    pci_register_slot(0x0E, PCI_CARD_NORMAL,      2, 1, 3, 4);
    pci_register_slot(0x0C, PCI_CARD_NORMAL,      1, 3, 2, 4);
    pci_register_slot(0x02, PCI_CARD_SOUTHBRIDGE, 0, 0, 0, 0);
    device_add(&keyboard_ps2_ps1_pci_device);
    device_add(&sio_device);
    device_add(&fdc37c665_device);
    device_add(&intel_flash_bxt_ami_device);

    device_add(&i430lx_device);

    if (gfxcard[0] == VID_INTERNAL)
        device_add(&mach32_onboard_pci_device);

    return ret;
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

    device_add(&keyboard_at_ami_device);
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

    device_add(&keyboard_ps2_pci_device);
    device_add(&sio_zb_device);
    device_add(&i82091aa_ide_device);
    device_add(&intel_flash_bxt_ami_device);

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
    device_add(&keyboard_ps2_phoenix_device);
    device_add(&ide_w83769f_pci_single_channel_device);	
    device_add(&fdc37c665_ide_sec_device);
    device_add(&intel_flash_bxt_ami_device);

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
    device_add(&keyboard_at_ami_device);

    if (fdc_current[0] == FDC_INTERNAL)
        device_add(&fdc_at_device);

    return ret;
}

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
    device_add(&fdc37c661_device);
    device_add(&keyboard_ps2_intel_ami_pci_device);

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
    device_add(&keyboard_at_ami_device);

    if (fdc_current[0] == FDC_INTERNAL)
        device_add(&fdc_at_device);

    return ret;
}

int
machine_at_excaliburpci2_init(const machine_t *model)
{
    int ret;

    ret = bios_load_linear_inverted("roms/machines/excaliburpci2/S722P.ROM",
                                    0x000e0000, 131072, 0);

    if (bios_only || !ret)
        return ret;

    machine_at_common_init_ex(model, 2);
    device_add(&ami_1994_nvr_device);

    pci_init(PCI_CONFIG_TYPE_1 | FLAG_TRC_CONTROLS_CPURST);
    pci_register_slot(0x00, PCI_CARD_NORTHBRIDGE, 0, 0, 0, 0);
    pci_register_slot(0x01, PCI_CARD_SOUTHBRIDGE, 0, 0, 0, 0);
    pci_register_slot(0x08, PCI_CARD_IDE,         0, 0, 0, 0);
    pci_register_slot(0x0A, PCI_CARD_NORMAL,      1, 2, 3, 4);
    pci_register_slot(0x0B, PCI_CARD_NORMAL,      2, 3, 4, 1);
    pci_register_slot(0x0C, PCI_CARD_NORMAL,      3, 4, 1, 2);
    pci_register_slot(0x0D, PCI_CARD_NORMAL,      4, 1, 2, 3);
    device_add(&fdc37c665_device);
    device_add(&keyboard_ps2_ami_pci_device);
    device_add(&ide_cmd640_pci_legacy_only_device);

    device_add(&sis_85c50x_device);
    device_add(&intel_flash_bxt_ami_device);

    return ret;
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
