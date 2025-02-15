/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          Implementation of Socket 8 machines.
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
#include <86box/hdc.h>
#include <86box/hdc_ide.h>
#include <86box/keyboard.h>
#include <86box/flash.h>
#include <86box/timer.h>
#include <86box/nvr.h>
#include <86box/sio.h>
#include <86box/sound.h>
#include <86box/hwm.h>
#include <86box/spd.h>
#include <86box/video.h>
#include "cpu.h"
#include <86box/machine.h>

int
machine_at_ap61_init(const machine_t *model)
{
    int ret;

    ret = bios_load_linear("roms/machines/ap61/ap61r120.bin",
                           0x000e0000, 131072, 0);

    if (bios_only || !ret)
        return ret;

    machine_at_common_init(model);

    pci_init(PCI_CONFIG_TYPE_1);
    pci_register_slot(0x19, PCI_CARD_NORTHBRIDGE,     0, 0, 0, 0);
    pci_register_slot(0x14, PCI_CARD_NORTHBRIDGE_SEC, 0, 0, 0, 0);
    pci_register_slot(0x02, PCI_CARD_SOUTHBRIDGE,     0, 0, 0, 0);
    pci_register_slot(0x07, PCI_CARD_IDE,             0xFE, 0xFF, 0, 0);
    pci_register_slot(0x03, PCI_CARD_NORMAL,          1, 2, 3, 4);
    pci_register_slot(0x04, PCI_CARD_NORMAL,          2, 3, 4, 1);
    pci_register_slot(0x05, PCI_CARD_NORMAL,          3, 4, 1, 2);
    pci_register_slot(0x06, PCI_CARD_NORMAL,          4, 1, 2, 3);
    device_add(&i450kx_device);
    device_add(&sio_zb_device);
    device_add(&ide_cmd646_device);
    device_add(&keyboard_ps2_acer_pci_device);
    device_add(&fdc37c665_device);
    device_add(&sst_flash_29ee010_device);
    // device_add(&intel_flash_bxt_device);

    return ret;
}

int
machine_at_p6rp4_init(const machine_t *model)
{
    int ret;

    ret = bios_load_linear("roms/machines/p6rp4/OR6I0106.SMC",
                           0x000e0000, 131072, 0);

    if (bios_only || !ret)
        return ret;

    machine_at_common_init_ex(model, 2);
    device_add(&p6rp4_nvr_device);

    pci_init(PCI_CONFIG_TYPE_1);
    pci_register_slot(0x19, PCI_CARD_NORTHBRIDGE,     0, 0, 0, 0);
    pci_register_slot(0x14, PCI_CARD_NORTHBRIDGE_SEC, 0, 0, 0, 0);
    pci_register_slot(0x02, PCI_CARD_SOUTHBRIDGE,     0, 0, 0, 0);
    pci_register_slot(0x08, PCI_CARD_IDE,             0, 0, 0, 0);
    pci_register_slot(0x07, PCI_CARD_NORMAL,          1, 2, 3, 4);
    pci_register_slot(0x06, PCI_CARD_NORMAL,          2, 3, 4, 1);
    pci_register_slot(0x05, PCI_CARD_NORMAL,          3, 4, 1, 2);
    pci_register_slot(0x04, PCI_CARD_NORMAL,          4, 1, 2, 3);
    device_add(&i450kx_device);
    device_add(&sio_zb_device);
    device_add(&ide_cmd646_device);
    /* Input port bit 2 must be 1 or CMOS Setup is disabled. */
    device_add(&keyboard_ps2_ami_pci_device);
    device_add(&fdc37c665_device);
    device_add(&intel_flash_bxt_device);

    return ret;
}

int
machine_at_686nx_init(const machine_t *model)
{
    int ret;

    ret = bios_load_linear("roms/machines/686nx/6nx.140",
                           0x000e0000, 131072, 0);

    if (bios_only || !ret)
        return ret;

    machine_at_common_init(model);

    pci_init(PCI_CONFIG_TYPE_1);
    pci_register_slot(0x00, PCI_CARD_NORTHBRIDGE, 0, 0, 0, 0);
    pci_register_slot(0x07, PCI_CARD_SOUTHBRIDGE, 0, 0, 0, 0);
    pci_register_slot(0x08, PCI_CARD_NORMAL,      1, 2, 3, 4);
    pci_register_slot(0x09, PCI_CARD_NORMAL,      2, 3, 4, 1);
    pci_register_slot(0x0A, PCI_CARD_NORMAL,      3, 4, 1, 2);
    pci_register_slot(0x0B, PCI_CARD_NORMAL,      4, 1, 2, 3);
    device_add(&i440fx_device);
    device_add(&piix3_device);
    device_add(&keyboard_ps2_ami_pci_device); // Uses the AMIKEY keyboard controller
    device_add(&um8669f_device);
    device_add(&intel_flash_bxt_device);

    return ret;
}

int
machine_at_mb600n_init(const machine_t *model)
{
    int ret;

    ret = bios_load_linear("roms/machines/mb600n/60915cs.rom",
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
    device_add(&i440fx_device);
    device_add(&piix3_device);
    device_add(&keyboard_ps2_ami_pci_device);
    device_add(&fdc37c669_device);
    device_add(&intel_flash_bxt_device);

    return ret;
}

int
machine_at_acerv60n_init(const machine_t *model)
{
    int ret;

    ret = bios_load_linear("roms/machines/acerv60n/V60NE5.BIN",
                           0x000e0000, 131072, 0);

    if (bios_only || !ret)
        return ret;

    machine_at_common_init_ex(model,2 );

    pci_init(PCI_CONFIG_TYPE_1);
    pci_register_slot(0x00, PCI_CARD_NORTHBRIDGE, 0, 0, 0, 0);
    pci_register_slot(0x07, PCI_CARD_SOUTHBRIDGE, 0, 0, 0, 0);
    pci_register_slot(0x0D, PCI_CARD_NORMAL,      1, 2, 3, 4);
    pci_register_slot(0x0F, PCI_CARD_NORMAL,      2, 3, 4, 1);
    pci_register_slot(0x10, PCI_CARD_NORMAL,      3, 4, 1, 2);
    pci_register_slot(0x12, PCI_CARD_NORMAL,      4, 1, 2, 3);
    pci_register_slot(0x0C, PCI_CARD_NORMAL,      2, 3, 4, 1);
    device_add(&i440fx_device);
    device_add(&piix3_device);
    device_add(&fdc37c935_device);
    device_add(&sst_flash_29ee010_device);

    return ret;
}

int
machine_at_lgibmx61_init(const machine_t *model)
{
    int ret;

    ret = bios_load_linear("roms/machines/lgibmx61/bios.rom",
                           0x000e0000, 131072, 0);

    if (bios_only || !ret)
        return ret;

    machine_at_common_init(model);

    pci_init(PCI_CONFIG_TYPE_1);
    pci_register_slot(0x00, PCI_CARD_NORTHBRIDGE, 0, 0, 0, 0);
    pci_register_slot(0x07, PCI_CARD_SOUTHBRIDGE, 0, 0, 0, 0);
    pci_register_slot(0x0C, PCI_CARD_NORMAL,      1, 2, 3, 4);
    pci_register_slot(0x0D, PCI_CARD_NORMAL,      2, 3, 4, 1);
    pci_register_slot(0x0E, PCI_CARD_NORMAL,      3, 4, 1, 2);
    pci_register_slot(0x0F, PCI_CARD_NORMAL,      4, 1, 2, 3);
    device_add(&i440fx_device);
    device_add(&piix3_device);
    // device_add(&keyboard_ps2_ami_pci_device);
    device_add(&keyboard_ps2_ami_device);
    // device_add(&w83787f_device);
    device_add(&w83877f_president_device);
    device_add(&sst_flash_29ee010_device);

    return ret;
}

int
machine_at_vs440fx_init(const machine_t *model)
{
    int ret;

    ret = bios_load_linear_combined2("roms/machines/vs440fx/1018CS1_.BIO",
                                     "roms/machines/vs440fx/1018CS1_.BI1",
                                     "roms/machines/vs440fx/1018CS1_.BI2",
                                     "roms/machines/vs440fx/1018CS1_.BI3",
                                     "roms/machines/vs440fx/1018CS1_.RCV",
                                     0x3a000, 128);

    if (bios_only || !ret)
        return ret;

    machine_at_common_init(model);

    pci_init(PCI_CONFIG_TYPE_1);
    pci_register_slot(0x00, PCI_CARD_NORTHBRIDGE, 0, 0, 0, 0);
    pci_register_slot(0x0B, PCI_CARD_NORMAL,      1, 2, 3, 4);
    pci_register_slot(0x0F, PCI_CARD_NORMAL,      4, 1, 2, 3);
    pci_register_slot(0x11, PCI_CARD_NORMAL,      3, 4, 1, 2);
    pci_register_slot(0x13, PCI_CARD_NORMAL,      2, 3, 4, 1);
    pci_register_slot(0x07, PCI_CARD_SOUTHBRIDGE, 0, 0, 0, 0);
    device_add(&i440fx_device);
    device_add(&piix3_device);
    device_add(&keyboard_ps2_intel_ami_pci_device);
    device_add(&pc87307_device);

    device_add(&intel_flash_bxt_ami_device);

    if (sound_card_current[0] == SOUND_INTERNAL)
        device_add(machine_get_snd_device(machine));

    return ret;
}

int
machine_at_gw2kvenus_init(const machine_t *model)
{
    int ret;

    ret = bios_load_linear_combined2("roms/machines/gw2kvenus/1011CS1T.BIO",
                                     "roms/machines/gw2kvenus/1011CS1T.BI1",
                                     "roms/machines/gw2kvenus/1011CS1T.BI2",
                                     "roms/machines/gw2kvenus/1011CS1T.BI3",
                                     "roms/machines/gw2kvenus/1011CS1T.RCV",
                                     0x3a000, 128);

    if (bios_only || !ret)
        return ret;

    machine_at_common_init(model);

    pci_init(PCI_CONFIG_TYPE_1);
    pci_register_slot(0x00, PCI_CARD_NORTHBRIDGE, 0, 0, 0, 0);
    pci_register_slot(0x0B, PCI_CARD_NORMAL,      1, 2, 3, 4);
    pci_register_slot(0x0F, PCI_CARD_NORMAL,      4, 1, 2, 3);
    pci_register_slot(0x11, PCI_CARD_NORMAL,      3, 4, 1, 2);
    pci_register_slot(0x13, PCI_CARD_NORMAL,      2, 3, 4, 1);
    pci_register_slot(0x07, PCI_CARD_SOUTHBRIDGE, 0, 0, 0, 0);
    device_add(&i440fx_device);
    device_add(&piix3_device);
    device_add(&keyboard_ps2_intel_ami_pci_device);
    device_add(&pc87307_device);

    device_add(&intel_flash_bxt_ami_device);

    if (sound_card_current[0] == SOUND_INTERNAL)
        device_add(machine_get_snd_device(machine));

    return ret;
}

int
machine_at_ap440fx_init(const machine_t *model)
{
    int ret;

    ret = bios_load_linear_combined2("roms/machines/ap440fx/1011CT1_.BIO",
                                     "roms/machines/ap440fx/1011CT1_.BI1",
                                     "roms/machines/ap440fx/1011CT1_.BI2",
                                     "roms/machines/ap440fx/1011CT1_.BI3",
                                     "roms/machines/ap440fx/1011CT1_.RCV",
                                     0x3a000, 128);

    if (bios_only || !ret)
        return ret;

    machine_at_common_init(model);

    pci_init(PCI_CONFIG_TYPE_1);
    pci_register_slot(0x00, PCI_CARD_NORTHBRIDGE, 0, 0, 0, 0);
    pci_register_slot(0x08, PCI_CARD_VIDEO,       3, 0, 0, 0);
    pci_register_slot(0x11, PCI_CARD_NORMAL,      1, 3, 2, 4);
    pci_register_slot(0x13, PCI_CARD_NORMAL,      2, 1, 3, 4);
    pci_register_slot(0x0B, PCI_CARD_NORMAL,      3, 2, 1, 4);
    pci_register_slot(0x07, PCI_CARD_SOUTHBRIDGE, 0, 0, 0, 4);
    device_add(&i440fx_device);
    device_add(&piix3_device);
    device_add(&keyboard_ps2_ami_pci_device);
    device_add(&pc87307_device);
    device_add(&intel_flash_bxt_ami_device);

    if (sound_card_current[0] == SOUND_INTERNAL)
        device_add(machine_get_snd_device(machine));

    if (gfxcard[0] == VID_INTERNAL)
        device_add(machine_get_vid_device(machine));

    return ret;
}

int
machine_at_8600ttc_init(const machine_t *model)
{
    int ret;

    ret = bios_load_linear("roms/machines/8600ttc/TTC0715B.ROM",
                           0x000e0000, 131072, 0);

    if (bios_only || !ret)
        return ret;

    machine_at_common_init(model);

    pci_init(PCI_CONFIG_TYPE_1);
    pci_register_slot(0x00, PCI_CARD_NORTHBRIDGE, 0, 0, 0, 0);
    pci_register_slot(0x07, PCI_CARD_SOUTHBRIDGE, 0, 0, 0, 0);
    pci_register_slot(0x08, PCI_CARD_NORMAL,      1, 2, 3, 4);
    pci_register_slot(0x09, PCI_CARD_NORMAL,      2, 3, 4, 1);
    pci_register_slot(0x0A, PCI_CARD_NORMAL,      3, 4, 1, 2);
    pci_register_slot(0x0B, PCI_CARD_NORMAL,      4, 1, 2, 3);
    device_add(&i440fx_device);
    device_add(&piix3_device);
    device_add(&keyboard_ps2_ami_pci_device);
    device_add(&fdc37c669_device);
    device_add(&intel_flash_bxt_device);

    return ret;
}

int
machine_at_m6mi_init(const machine_t *model)
{
    int ret;

    ret = bios_load_linear("roms/machines/m6mi/M6MI05.ROM",
                           0x000e0000, 131072, 0);

    if (bios_only || !ret)
        return ret;

    machine_at_common_init_ex(model, 2);

    pci_init(PCI_CONFIG_TYPE_1);
    pci_register_slot(0x00, PCI_CARD_NORTHBRIDGE, 0, 0, 0, 0);
    pci_register_slot(0x07, PCI_CARD_SOUTHBRIDGE, 0, 0, 0, 0);
    pci_register_slot(0x12, PCI_CARD_NORMAL,      1, 2, 3, 4);
    pci_register_slot(0x11, PCI_CARD_NORMAL,      2, 3, 4, 1);
    pci_register_slot(0x10, PCI_CARD_NORMAL,      3, 4, 1, 2);
    pci_register_slot(0x0F, PCI_CARD_NORMAL,      4, 1, 2, 3);
    device_add(&i440fx_device);
    device_add(&piix3_device);
    device_add(&fdc37c935_device);
    device_add(&intel_flash_bxt_device);

    return ret;
}

void
machine_at_p65up5_common_init(const machine_t *model, const device_t *northbridge)
{
    machine_at_common_init(model);

    pci_init(PCI_CONFIG_TYPE_1);
    pci_register_slot(0x00, PCI_CARD_NORTHBRIDGE, 0, 0, 0, 0);
    pci_register_slot(0x01, PCI_CARD_SOUTHBRIDGE, 0, 0, 0, 0);
    pci_register_slot(0x09, PCI_CARD_NORMAL,      4, 1, 2, 3);
    pci_register_slot(0x0A, PCI_CARD_NORMAL,      3, 4, 1, 2);
    pci_register_slot(0x0B, PCI_CARD_NORMAL,      2, 3, 4, 1);
    pci_register_slot(0x0C, PCI_CARD_NORMAL,      1, 2, 3, 4);
    pci_register_slot(0x0D, PCI_CARD_NORMAL,      4, 1, 2, 3);
    device_add(northbridge);
    device_add(&piix3_ioapic_device);
    device_add(&keyboard_ps2_ami_pci_device);
    device_add(&w83877f_device);
    device_add(&sst_flash_29ee010_device);
    device_add(&ioapic_device);
}

int
machine_at_p65up5_cp6nd_init(const machine_t *model)
{
    int ret;

    ret = bios_load_linear("roms/machines/p65up5/ND6I0218.AWD",
                           0x000e0000, 131072, 0);

    if (bios_only || !ret)
        return ret;

    machine_at_p65up5_common_init(model, &i440fx_device);

    return ret;
}
