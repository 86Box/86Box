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
#include <86box/video.h>
#include <86box/machine.h>

int
machine_at_plato_init(const machine_t *model)
{
    int ret;

    ret = bios_load_linear_combined("roms/machines/plato/1016ax1_.bio",
                                    "roms/machines/plato/1016ax1_.bi1",
                                    0x1d000, 128);

    if (bios_only || !ret)
        return ret;

    machine_at_premiere_common_init(model, PCI_CAN_SWITCH_TYPE);

    device_add(&i430nx_device);

    return ret;
}

int
machine_at_ambradp90_init(const machine_t *model)
{
    int ret;

    ret = bios_load_linear_combined("roms/machines/ambradp90/1002AX1P.BIO",
                                    "roms/machines/ambradp90/1002AX1P.BI1",
                                    0x1d000, 128);

    if (bios_only || !ret)
        return ret;

    machine_at_premiere_common_init(model, PCI_CAN_SWITCH_TYPE);

    device_add(&i430nx_device);

    return ret;
}

int
machine_at_430nx_init(const machine_t *model)
{
    int ret;

    ret = bios_load_linear("roms/machines/430nx/IP.20",
                           0x000e0000, 131072, 0);

    if (bios_only || !ret)
        return ret;

    machine_at_award_common_init(model);

    device_add(&sio_device);
    device_add(&intel_flash_bxt_device);
    device_add(&i430nx_device);

    return ret;
}

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
    device_add(&keyboard_ps2_acer_pci_device);
    device_add(&fdc37c665_device);

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

    machine_at_common_init_ex(model, 2);
    device_add(&ami_1995_nvr_device);

    pci_init(PCI_CONFIG_TYPE_1);
    pci_register_slot(0x00, PCI_CARD_NORTHBRIDGE, 0, 0, 0, 0);
    pci_register_slot(0x08, PCI_CARD_NORMAL,      1, 2, 3, 4);
    pci_register_slot(0x09, PCI_CARD_NORMAL,      2, 3, 4, 1);
    pci_register_slot(0x0A, PCI_CARD_NORMAL,      3, 4, 1, 2);
    pci_register_slot(0x0B, PCI_CARD_NORMAL,      4, 1, 2, 3);
    pci_register_slot(0x07, PCI_CARD_SOUTHBRIDGE, 0, 0, 0, 0);
    device_add(&keyboard_ps2_ami_pci_device);
    device_add(&i430fx_device);
    device_add(&piix_device);
    device_add(&pc87332_398_device);
    device_add(&intel_flash_bxt_device);

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

int
machine_at_zappa_init(const machine_t *model)
{
    int ret;

    ret = bios_load_linear_combined("roms/machines/zappa/1006bs0_.bio",
                                    "roms/machines/zappa/1006bs0_.bi1",
                                    0x20000, 128);

    if (bios_only || !ret)
        return ret;

    machine_at_common_init_ex(model, 2);
    machine_at_zappa_gpio_init();

    pci_init(PCI_CONFIG_TYPE_1);
    pci_register_slot(0x00, PCI_CARD_NORTHBRIDGE, 0, 0, 0, 0);
    pci_register_slot(0x0D, PCI_CARD_NORMAL,      1, 2, 3, 4);
    pci_register_slot(0x0E, PCI_CARD_NORMAL,      3, 4, 1, 2);
    pci_register_slot(0x0F, PCI_CARD_NORMAL,      2, 3, 4, 1);
    pci_register_slot(0x07, PCI_CARD_SOUTHBRIDGE, 0, 0, 0, 0);
    device_add(&keyboard_ps2_intel_ami_pci_device);
    device_add(&i430fx_device);
    device_add(&piix_device);
    device_add(&pc87306_device);
    device_add(&intel_flash_bxt_ami_device);

    return ret;
}

int
machine_at_powermatev_init(const machine_t *model)
{
    int ret;

    ret = bios_load_linear("roms/machines/powermatev/BIOS.ROM",
                           0x000e0000, 131072, 0);

    if (bios_only || !ret)
        return ret;

    machine_at_common_init(model);

    pci_init(PCI_CONFIG_TYPE_1);
    pci_register_slot(0x00, PCI_CARD_NORTHBRIDGE, 0, 0, 0, 0);
    pci_register_slot(0x07, PCI_CARD_SOUTHBRIDGE, 0, 0, 0, 0);
    pci_register_slot(0x08, PCI_CARD_NORMAL,      0, 0, 0, 0);
    pci_register_slot(0x11, PCI_CARD_NORMAL,      1, 2, 3, 4);
    pci_register_slot(0x13, PCI_CARD_NORMAL,      2, 3, 4, 1);
    device_add(&keyboard_ps2_ami_pci_device);
    device_add(&i430fx_device);
    device_add(&piix_device);
    device_add(&fdc37c665_device);
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

    machine_at_common_init_ex(model, 2);
    device_add(&ami_1994_nvr_device);

    pci_init(PCI_CONFIG_TYPE_1);
    pci_register_slot(0x00, PCI_CARD_NORTHBRIDGE, 0, 0, 0, 0);
    pci_register_slot(0x14, PCI_CARD_NORMAL,      1, 2, 3, 4);
    pci_register_slot(0x13, PCI_CARD_NORMAL,      2, 3, 4, 1);
    pci_register_slot(0x12, PCI_CARD_NORMAL,      3, 4, 1, 2);
    pci_register_slot(0x07, PCI_CARD_SOUTHBRIDGE, 0, 0, 0, 0);
    device_add(&keyboard_ps2_tg_ami_pci_device);
    device_add(&i430fx_device);
    device_add(&piix_device);
    device_add(&fdc37c665_device);
    device_add(&intel_flash_bxt_device);

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
    device_add(&keyboard_ps2_intel_ami_pci_device);

    if (fdc_type == FDC_INTERNAL)
        device_add(&fdc_at_device);

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
    device_add(&keyboard_at_device);

    if (fdc_type == FDC_INTERNAL)
        device_add(&fdc_at_device);

    return ret;
}

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

    pci_init(PCI_CONFIG_TYPE_1);
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
    device_add(&keyboard_ps2_ami_pci_device);
    device_add(&fdc37c665_ide_device);
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

    pci_init(PCI_CONFIG_TYPE_1);
    pci_register_slot(0x00, PCI_CARD_NORTHBRIDGE, 0, 0, 0, 0);
    pci_register_slot(0x01, PCI_CARD_SOUTHBRIDGE, 0, 0, 0, 0);
    pci_register_slot(0x06, PCI_CARD_NORMAL, 1, 2, 3, 4);
    pci_register_slot(0x07, PCI_CARD_NORMAL, 2, 3, 4, 1);
    pci_register_slot(0x08, PCI_CARD_NORMAL, 3, 4, 1, 2);
    pci_register_slot(0x09, PCI_CARD_NORMAL, 4, 1, 2, 3);
    device_add(&sis_85c50x_device);
    device_add(&ide_pci_2ch_device);
    device_add(&keyboard_at_ami_device);
    device_add(&w83787f_device);
    device_add(&sst_flash_29ee010_device);

    return ret;
}
