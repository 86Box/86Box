/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          Implementation of Slot 1/Socket 370 machines machines.
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
#include <86box/spd.h>
#include <86box/video.h>
#include "cpu.h"
#include <86box/machine.h>
#include <86box/clock.h>
#include <86box/sound.h>
#include <86box/snd_ac97.h>

/* i440BX */
int
machine_at_prosignias31x_bx_init(const machine_t *model)
{
    int ret;

    ret = bios_load_linear("roms/machines/prosignias31x_bx/p6bxt-ap-092600.bin",
                           0x000c0000, 262144, 0);

    if (bios_only || !ret)
        return ret;

    machine_at_common_init_ex(model, 2);

    pci_init(PCI_CONFIG_TYPE_1);
    pci_register_slot(0x00, PCI_CARD_NORTHBRIDGE, 0, 0, 0, 0);
    pci_register_slot(0x07, PCI_CARD_SOUTHBRIDGE, 1, 2, 3, 4);
    pci_register_slot(0x09, PCI_CARD_NORMAL, 1, 2, 3, 4);
    pci_register_slot(0x0a, PCI_CARD_NORMAL, 2, 3, 4, 1);
    pci_register_slot(0x0b, PCI_CARD_NORMAL, 3, 4, 1, 2);
    pci_register_slot(0x0c, PCI_CARD_NORMAL, 4, 1, 2, 3);
    pci_register_slot(0x0d, PCI_CARD_SOUND,  4, 3, 2, 1); /* assumed */
    pci_register_slot(0x01, PCI_CARD_AGPBRIDGE, 1, 2, 3, 4);
    device_add(&i440bx_device);
    device_add(&piix4e_device);
    device_add_params(&w83977_device, (void *) (W83977EF | W83977_AMI | W83977_NO_NVR));
    device_add(&winbond_flash_w29c020_device);
    spd_register(SPD_TYPE_SDRAM, 0x7, 256);
    device_add(&gl520sm_2d_device);  /* fans: CPU, Chassis; temperature: System */
    hwm_values.temperatures[0] += 2; /* System offset */
    hwm_values.temperatures[1] += 2; /* CPU offset */
    hwm_values.voltages[0] = 3300;   /* Vcore and 3.3V are swapped */
    hwm_values.voltages[2] = hwm_get_vcore();

    if (sound_card_current[0] == SOUND_INTERNAL)
        device_add(&cmi8738_onboard_device);

    return ret;
}

int
machine_at_s1857_init(const machine_t *model)
{
    int ret;

    ret = bios_load_linear("roms/machines/s1857/BX57200A.ROM",
                           0x000c0000, 262144, 0);

    if (bios_only || !ret)
        return ret;

    machine_at_common_init_ex(model, 2);

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
    device_add_params(&w83977_device, (void *) (W83977EF | W83977_AMI | W83977_NO_NVR));
    device_add(&intel_flash_bxt_device);
    spd_register(SPD_TYPE_SDRAM, 0x7, 256);

    if (sound_card_current[0] == SOUND_INTERNAL) {
        device_add(machine_get_snd_device(machine));
        device_add(&cs4297_device); /* no good pictures, but the marking looks like CS4297 from a distance */
    }

    return ret;
}

/* VIA Apollo Pro 133 */
int
machine_at_p6bat_init(const machine_t *model)
{
    int ret;

    ret = bios_load_linear("roms/machines/p6bat/bata+56.BIN",
                           0x000c0000, 262144, 0);

    if (bios_only || !ret)
        return ret;

    machine_at_common_init_ex(model, 2);

    pci_init(PCI_CONFIG_TYPE_1);
    pci_register_slot(0x00, PCI_CARD_NORTHBRIDGE, 0, 0, 0, 0);
    pci_register_slot(0x07, PCI_CARD_SOUTHBRIDGE, 0, 0, 3, 4);
    pci_register_slot(0x09, PCI_CARD_NORMAL, 1, 2, 3, 4);
    pci_register_slot(0x0a, PCI_CARD_NORMAL, 2, 3, 4, 1);
    pci_register_slot(0x0b, PCI_CARD_NORMAL, 3, 4, 1, 2);
    pci_register_slot(0x0c, PCI_CARD_NORMAL, 4, 1, 2, 3);
    pci_register_slot(0x0d, PCI_CARD_NORMAL, 4, 3, 2, 1);
    pci_register_slot(0x01, PCI_CARD_AGPBRIDGE, 1, 2, 3, 4);
    device_add(&via_apro133_device);
    device_add(&via_vt82c596b_device);
    device_add_params(&w83977_device, (void *) (W83977EF | W83977_AMI | W83977_NO_NVR));
    device_add(&sst_flash_39sf020_device);
    spd_register(SPD_TYPE_SDRAM, 0x7, 256);

    if (sound_card_current[0] == SOUND_INTERNAL)
        device_add(&cmi8738_onboard_device);

    return ret;
}
