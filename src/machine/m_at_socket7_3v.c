/*
 * 86Box	A hypervisor and IBM PC system emulator that specializes in
 *		running old operating systems and software designed for IBM
 *		PC systems and compatibles from 1981 through fairly recent
 *		system designs based on the PCI bus.
 *
 *		This file is part of the 86Box distribution.
 *
 *		Implementation of Socket 7 (Single Voltage) machines.
 *
 *
 *
 * Authors:	Sarah Walker, <http://pcem-emulator.co.uk/>
 *		Miran Grca, <mgrca8@gmail.com>
 *		Melissa Goad, <mszoopers@protonmail.com>
 *
 *		Copyright 2010-2020 Sarah Walker.
 *		Copyright 2016-2020 Miran Grca.
 *
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

static void
machine_at_thor_common_init(const machine_t *model, int mr)
{
    machine_at_common_init_ex(model, mr);

    pci_init(PCI_CONFIG_TYPE_1);
    pci_register_slot(0x00, PCI_CARD_NORTHBRIDGE, 0, 0, 0, 0);
    pci_register_slot(0x08, PCI_CARD_VIDEO, 4, 0, 0, 0);
    pci_register_slot(0x0D, PCI_CARD_NORMAL, 1, 2, 3, 4);
    pci_register_slot(0x0E, PCI_CARD_NORMAL, 2, 3, 4, 1);
    pci_register_slot(0x0F, PCI_CARD_NORMAL, 3, 4, 2, 1);
    pci_register_slot(0x10, PCI_CARD_NORMAL, 4, 3, 2, 1);
    pci_register_slot(0x07, PCI_CARD_SOUTHBRIDGE, 0, 0, 0, 0);

    if (gfxcard == VID_INTERNAL)
        device_add(&s3_phoenix_trio64vplus_onboard_pci_device);

    // device_add(&keyboard_ps2_ami_pci_device);
    device_add(&keyboard_ps2_intel_ami_pci_device);
    device_add(&i430fx_device);
    device_add(&piix_device);
    device_add(&pc87306_device);
    device_add(&intel_flash_bxt_ami_device);
}

static void
machine_at_p54tp4xe_common_init(const machine_t *model)
{
    machine_at_common_init(model);

    pci_init(PCI_CONFIG_TYPE_1);
    pci_register_slot(0x00, PCI_CARD_NORTHBRIDGE, 0, 0, 0, 0);
    pci_register_slot(0x0C, PCI_CARD_NORMAL, 1, 2, 3, 4);
    pci_register_slot(0x0B, PCI_CARD_NORMAL, 2, 3, 4, 1);
    pci_register_slot(0x0A, PCI_CARD_NORMAL, 3, 4, 1, 2);
    pci_register_slot(0x09, PCI_CARD_NORMAL, 4, 1, 2, 3);
    pci_register_slot(0x07, PCI_CARD_SOUTHBRIDGE, 0, 0, 0, 0);
    device_add(&keyboard_ps2_pci_device);
    device_add(&i430fx_device);
    device_add(&piix_device);
    device_add(&fdc37c665_device);
    device_add(&intel_flash_bxt_device);
}

int
machine_at_p54tp4xe_init(const machine_t *model)
{
    int ret;

    ret = bios_load_linear("roms/machines/p54tp4xe/t15i0302.awd",
                           0x000e0000, 131072, 0);

    if (bios_only || !ret)
        return ret;

    machine_at_p54tp4xe_common_init(model);

    return ret;
}

int
machine_at_p54tp4xe_mr_init(const machine_t *model)
{
    int ret;

    ret = bios_load_linear("roms/machines/p54tp4xe/TRITON.BIO",
                           0x000e0000, 131072, 0);

    if (bios_only || !ret)
        return ret;

    machine_at_p54tp4xe_common_init(model);

    return ret;
}

int
machine_at_gw2katx_init(const machine_t *model)
{
    int ret;

    ret = bios_load_linear_combined("roms/machines/gw2katx/1003CN0T.BIO",
                                    "roms/machines/gw2katx/1003CN0T.BI1", 0x20000, 128);

    if (bios_only || !ret)
        return ret;

    machine_at_thor_common_init(model, 0);

    return ret;
}

int
machine_at_thor_init(const machine_t *model)
{
    int ret;

    ret = bios_load_linear_combined("roms/machines/thor/1006cn0_.bio",
                                    "roms/machines/thor/1006cn0_.bi1", 0x20000, 128);

    if (bios_only || !ret)
        return ret;

    machine_at_thor_common_init(model, 0);

    return ret;
}

int
machine_at_mrthor_init(const machine_t *model)
{
    int ret;

    ret = bios_load_linear("roms/machines/mrthor/mr_atx.bio",
                           0x000e0000, 131072, 0);

    if (bios_only || !ret)
        return ret;

    machine_at_thor_common_init(model, 1);

    return ret;
}

int
machine_at_endeavor_init(const machine_t *model)
{
    int ret;

    ret = bios_load_linear_combined("roms/machines/endeavor/1006cb0_.bio",
                                    "roms/machines/endeavor/1006cb0_.bi1", 0x1d000, 128);

    if (bios_only || !ret)
        return ret;

    machine_at_common_init(model);

    pci_init(PCI_CONFIG_TYPE_1);
    pci_register_slot(0x00, PCI_CARD_NORTHBRIDGE, 0, 0, 0, 0);
    pci_register_slot(0x08, PCI_CARD_VIDEO, 4, 0, 0, 0);
    pci_register_slot(0x0D, PCI_CARD_NORMAL, 1, 2, 3, 4);
    pci_register_slot(0x0E, PCI_CARD_NORMAL, 2, 3, 4, 1);
    pci_register_slot(0x0F, PCI_CARD_NORMAL, 3, 4, 1, 2);
    pci_register_slot(0x10, PCI_CARD_NORMAL, 4, 1, 2, 3);
    pci_register_slot(0x07, PCI_CARD_SOUTHBRIDGE, 0, 0, 0, 0);

    if (gfxcard == VID_INTERNAL)
        device_add(&s3_phoenix_trio64_onboard_pci_device);

    device_add(&keyboard_ps2_intel_ami_pci_device);
    device_add(&i430fx_device);
    device_add(&piix_device);
    device_add(&pc87306_device);
    device_add(&intel_flash_bxt_ami_device);

    return ret;
}

int
machine_at_ms5119_init(const machine_t *model)
{
    int ret;

    ret = bios_load_linear("roms/machines/ms5119/A37E.ROM",
                           0x000e0000, 131072, 0);

    if (bios_only || !ret)
        return ret;

    machine_at_common_init(model);

    pci_init(PCI_CONFIG_TYPE_1);
    pci_register_slot(0x00, PCI_CARD_NORTHBRIDGE, 0, 0, 0, 0);
    pci_register_slot(0x07, PCI_CARD_SOUTHBRIDGE, 0, 0, 0, 0);
    pci_register_slot(0x0d, PCI_CARD_NORMAL, 1, 2, 3, 4);
    pci_register_slot(0x0e, PCI_CARD_NORMAL, 2, 3, 4, 1);
    pci_register_slot(0x0f, PCI_CARD_NORMAL, 3, 4, 1, 2);

    device_add(&i430fx_device);
    device_add(&piix_device);
    device_add(&keyboard_ps2_ami_pci_device);
    device_add(&w83787f_device);
    device_add(&sst_flash_29ee010_device);

    return ret;
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

    pci_init(PCI_CONFIG_TYPE_1);
    pci_register_slot(0x00, PCI_CARD_NORTHBRIDGE, 0, 0, 0, 0);
    pci_register_slot(0x08, PCI_CARD_VIDEO, 4, 0, 0, 0);
    pci_register_slot(0x11, PCI_CARD_NORMAL, 1, 2, 3, 4);
    pci_register_slot(0x13, PCI_CARD_NORMAL, 2, 1, 3, 4);
    pci_register_slot(0x0B, PCI_CARD_NORMAL, 3, 2, 1, 4);
    pci_register_slot(0x07, PCI_CARD_SOUTHBRIDGE, 0, 0, 0, 0);
    device_add(&i430fx_rev02_device);
    device_add(&piix_rev02_device);

    if (gfxcard == VID_INTERNAL)
        device_add(&gd5440_onboard_pci_device);

    device_add(&keyboard_ps2_intel_ami_pci_device);
    device_add(&pc87306_device);
    device_add(&intel_flash_bxt_ami_device);

    return ret;
}

int
machine_at_fmb_init(const machine_t *model)
{
    int ret;

    ret = bios_load_linear("roms/machines/fmb/P5IV183.ROM",
                           0x000e0000, 131072, 0);

    if (bios_only || !ret)
        return ret;

    machine_at_common_init(model);

    pci_init(PCI_CONFIG_TYPE_1);
    pci_register_slot(0x00, PCI_CARD_NORTHBRIDGE, 0, 0, 0, 0);
    pci_register_slot(0x07, PCI_CARD_SOUTHBRIDGE, 0, 0, 0, 0);
    pci_register_slot(0x14, PCI_CARD_NORMAL, 1, 2, 3, 4);
    pci_register_slot(0x13, PCI_CARD_NORMAL, 2, 3, 4, 1);
    pci_register_slot(0x12, PCI_CARD_NORMAL, 3, 4, 2, 1);
    pci_register_slot(0x11, PCI_CARD_NORMAL, 4, 3, 2, 1);

    device_add(&i430fx_device);
    device_add(&piix_no_mirq_device);
    device_add(&keyboard_at_ami_device);
    device_add(&w83787f_device);
    device_add(&intel_flash_bxt_device);

    return ret;
}

int
machine_at_acerm3a_init(const machine_t *model)
{
    int ret;

    ret = bios_load_linear("roms/machines/acerm3a/r01-b3.bin",
                           0x000e0000, 131072, 0);

    if (bios_only || !ret)
        return ret;

    machine_at_common_init(model);

    pci_init(PCI_CONFIG_TYPE_1);
    pci_register_slot(0x00, PCI_CARD_NORTHBRIDGE, 0, 0, 0, 0);
    pci_register_slot(0x07, PCI_CARD_SOUTHBRIDGE, 0, 0, 0, 0);
    pci_register_slot(0x0C, PCI_CARD_NORMAL, 1, 2, 3, 4);
    pci_register_slot(0x0D, PCI_CARD_NORMAL, 2, 3, 4, 1);
    pci_register_slot(0x0E, PCI_CARD_NORMAL, 3, 4, 1, 2);
    pci_register_slot(0x0F, PCI_CARD_NORMAL, 4, 1, 2, 3);
    pci_register_slot(0x10, PCI_CARD_VIDEO, 4, 0, 0, 0);
    device_add(&i430hx_device);
    device_add(&piix3_device);
    device_add(&keyboard_ps2_pci_device);
    device_add(&fdc37c932fr_device);

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
    pci_register_slot(0x11, PCI_CARD_NORMAL, 1, 2, 3, 4);
    pci_register_slot(0x12, PCI_CARD_NORMAL, 2, 3, 4, 1);
    pci_register_slot(0x13, PCI_CARD_NORMAL, 3, 4, 1, 2);
    pci_register_slot(0x14, PCI_CARD_NORMAL, 4, 1, 2, 3);
    pci_register_slot(0x07, PCI_CARD_SOUTHBRIDGE, 0, 0, 0, 0);
    pci_register_slot(0x06, PCI_CARD_VIDEO, 1, 2, 3, 4);
    device_add(&i430hx_device);
    device_add(&piix3_device);
    device_add(&keyboard_ps2_ami_pci_device);
    device_add(&fdc37c669_device);
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
    pci_register_slot(0x08, PCI_CARD_NORMAL, 1, 2, 3, 4);
    pci_register_slot(0x09, PCI_CARD_NORMAL, 2, 3, 4, 1);
    pci_register_slot(0x0A, PCI_CARD_NORMAL, 3, 4, 1, 2);
    pci_register_slot(0x0B, PCI_CARD_NORMAL, 4, 1, 2, 3);
    pci_register_slot(0x07, PCI_CARD_SOUTHBRIDGE, 1, 2, 3, 4);
    device_add(&i430hx_device);
    device_add(&piix3_device);
    device_add(&keyboard_ps2_ami_pci_device);
    device_add(&um8669f_device);
    device_add(&intel_flash_bxt_device);

    return ret;
}

int
machine_at_p55t2s_init(const machine_t *model)
{
    int ret;

    ret = bios_load_linear("roms/machines/p55t2s/s6y08t.rom",
                           0x000e0000, 131072, 0);

    if (bios_only || !ret)
        return ret;

    machine_at_common_init(model);

    pci_init(PCI_CONFIG_TYPE_1);
    pci_register_slot(0x00, PCI_CARD_NORTHBRIDGE, 0, 0, 0, 0);
    pci_register_slot(0x12, PCI_CARD_NORMAL, 1, 2, 3, 4);
    pci_register_slot(0x13, PCI_CARD_NORMAL, 4, 1, 2, 3);
    pci_register_slot(0x14, PCI_CARD_NORMAL, 3, 4, 1, 2);
    pci_register_slot(0x11, PCI_CARD_NORMAL, 2, 3, 4, 1);
    pci_register_slot(0x07, PCI_CARD_SOUTHBRIDGE, 0, 0, 0, 0);
    device_add(&i430hx_device);
    device_add(&piix3_device);
    device_add(&keyboard_ps2_ami_pci_device);
    device_add(&pc87306_device);
    device_add(&intel_flash_bxt_device);

    return ret;
}

int
machine_at_p5vxb_init(const machine_t *model)
{
    int ret;

    ret = bios_load_linear("roms/machines/p5vxb/P5VXB10.BIN",
                           0x000e0000, 131072, 0);

    if (bios_only || !ret)
        return ret;

    machine_at_common_init(model);

    pci_init(PCI_CONFIG_TYPE_1);
    pci_register_slot(0x00, PCI_CARD_NORTHBRIDGE, 0, 0, 0, 0);
    pci_register_slot(0x05, PCI_CARD_NORMAL, 1, 2, 3, 4);
    pci_register_slot(0x06, PCI_CARD_NORMAL, 2, 3, 4, 1);
    pci_register_slot(0x08, PCI_CARD_NORMAL, 3, 4, 1, 2);
    pci_register_slot(0x09, PCI_CARD_NORMAL, 4, 1, 2, 3);
    pci_register_slot(0x07, PCI_CARD_SOUTHBRIDGE, 0, 0, 0, 4);
    device_add(&i430vx_device);
    device_add(&piix3_device);
    device_add(&keyboard_ps2_ami_pci_device);
    device_add(&w83877f_device);
    device_add(&sst_flash_29ee010_device);

    return ret;
}

int
machine_at_gw2kte_init(const machine_t *model)
{
    int ret;

    ret = bios_load_linear_combined2("roms/machines/gw2kte/1008CY1T.BIO",
                                     "roms/machines/gw2kte/1008CY1T.BI1",
                                     "roms/machines/gw2kte/1008CY1T.BI2",
                                     "roms/machines/gw2kte/1008CY1T.BI3",
                                     "roms/machines/gw2kte/1008CY1T.RCV",
                                     0x3a000, 128);

    if (bios_only || !ret)
        return ret;

    machine_at_common_init(model);

    pci_init(PCI_CONFIG_TYPE_1);
    pci_register_slot(0x00, PCI_CARD_NORTHBRIDGE, 0, 0, 0, 0);
    pci_register_slot(0x08, PCI_CARD_VIDEO, 4, 0, 0, 0);
    pci_register_slot(0x0D, PCI_CARD_NORMAL, 1, 2, 3, 4);
    pci_register_slot(0x0E, PCI_CARD_NORMAL, 2, 3, 4, 1);
    pci_register_slot(0x0F, PCI_CARD_NORMAL, 3, 4, 1, 2);
    pci_register_slot(0x10, PCI_CARD_NORMAL, 3, 4, 1, 2);
    pci_register_slot(0x07, PCI_CARD_SOUTHBRIDGE, 0, 0, 0, 4);
    device_add(&i430vx_device);
    device_add(&piix3_device);
    device_add(&keyboard_ps2_ami_pci_device);
    device_add(&fdc37c932fr_device);
    device_add(&intel_flash_bxt_ami_device);

    return ret;
}

int
machine_at_ap5s_init(const machine_t *model)
{
    int ret;

    ret = bios_load_linear("roms/machines/ap5s/AP5S150.BIN",
                           0x000e0000, 131072, 0);

    if (bios_only || !ret)
        return ret;

    machine_at_common_init(model);

    pci_init(PCI_CONFIG_TYPE_1);
    pci_register_slot(0x00, PCI_CARD_NORTHBRIDGE, 0, 0, 0, 0);
    pci_register_slot(0x01, PCI_CARD_SOUTHBRIDGE, 0, 0, 0, 0);
    pci_register_slot(0x0D, PCI_CARD_NORMAL, 1, 2, 3, 4);
    pci_register_slot(0x0F, PCI_CARD_NORMAL, 2, 3, 4, 1);
    pci_register_slot(0x11, PCI_CARD_NORMAL, 3, 4, 2, 1);
    pci_register_slot(0x13, PCI_CARD_NORMAL, 4, 3, 2, 1);

    device_add(&sis_5511_device);
    device_add(&keyboard_ps2_ami_pci_device);
    device_add(&fdc37c665_device);
    device_add(&sst_flash_29ee010_device);

    return ret;
}

int
machine_at_ms5124_init(const machine_t *model)
{
    int ret;

    ret = bios_load_linear("roms/machines/ms5124/AG77.ROM",
                           0x000e0000, 131072, 0);

    if (bios_only || !ret)
        return ret;

    machine_at_common_init(model);

    pci_init(PCI_CONFIG_TYPE_1);
    pci_register_slot(0x00, PCI_CARD_NORTHBRIDGE, 0, 0, 0, 0);
    pci_register_slot(0x01, PCI_CARD_SOUTHBRIDGE, 0xFE, 0xFF, 0, 0);
    pci_register_slot(0x10, PCI_CARD_NORMAL, 0x41, 0x42, 0x43, 0x44);
    pci_register_slot(0x11, PCI_CARD_NORMAL, 0x44, 0x41, 0x42, 0x43);
    pci_register_slot(0x12, PCI_CARD_NORMAL, 0x43, 0x44, 0x41, 0x42);
    pci_register_slot(0x0F, PCI_CARD_NORMAL, 0x42, 0x43, 0x44, 0x41);

    device_add(&sis_5511_device);
    device_add(&keyboard_ps2_ami_pci_device);
    device_add(&w83787f_device);
    device_add(&sst_flash_29ee010_device);

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

    machine_at_common_init_ex(model, 2);

    pci_init(PCI_CONFIG_TYPE_1);
    pci_register_slot(0x00, PCI_CARD_NORTHBRIDGE, 0, 0, 0, 0);
    pci_register_slot(0x0F, PCI_CARD_SOUTHBRIDGE, 0, 0, 0, 0);
    pci_register_slot(0x0D, PCI_CARD_VIDEO, 0, 0, 0, 0);
    pci_register_slot(0x06, PCI_CARD_NORMAL, 1, 2, 3, 4);
    pci_register_slot(0x07, PCI_CARD_NORMAL, 2, 3, 4, 1);
    pci_register_slot(0x08, PCI_CARD_NORMAL, 3, 4, 1, 2);

    if (gfxcard == VID_INTERNAL)
        device_add(&s3_phoenix_trio64_onboard_pci_device);

    device_add(&keyboard_ps2_ami_pci_device);
    device_add(&i430fx_device);
    device_add(&piix_device);
    device_add(&fdc37c931apm_device);
    device_add(&sst_flash_29ee010_device);

    return ret;
}
