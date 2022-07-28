/*
 * 86Box	A hypervisor and IBM PC system emulator that specializes in
 *		running old operating systems and software designed for IBM
 *		PC systems and compatibles from 1981 through fairly recent
 *		system designs based on the PCI bus.
 *
 *		This file is part of the 86Box distribution.
 *
 *		Implementation of Super Socket 7 machines.
 *
 *
 *
 * Authors:	Sarah Walker, <http://pcem-emulator.co.uk/>
 *		Miran Grca, <mgrca8@gmail.com>
 *		Melissa Goad, <mszoopers@protonmail.com>
 *
 *		Copyright 2010-2020 Sarah Walker.
 *		Copyright 2016-2020 Miran Grca.
 *		Copyright 2020 Melissa Goad.
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

int
machine_at_p5a_init(const machine_t *model)
{
    int ret;

    ret = bios_load_linear("roms/machines/p5a/1011.005",
                           0x000c0000, 262144, 0);

    if (bios_only || !ret)
        return ret;

    machine_at_common_init_ex(model, 2);

    pci_init(PCI_CONFIG_TYPE_1);
    pci_register_slot(0x00, PCI_CARD_NORTHBRIDGE, 0, 0, 0, 0);
    pci_register_slot(0x01, PCI_CARD_AGPBRIDGE, 1, 2, 0, 0);
    pci_register_slot(0x07, PCI_CARD_SOUTHBRIDGE, 1, 2, 3, 4);
    pci_register_slot(0x0F, PCI_CARD_SOUTHBRIDGE_IDE, 1, 2, 3, 4);
    pci_register_slot(0x03, PCI_CARD_SOUTHBRIDGE_PMU, 1, 2, 3, 4);
    pci_register_slot(0x02, PCI_CARD_SOUTHBRIDGE_USB, 1, 2, 3, 4);
    pci_register_slot(0x0C, PCI_CARD_NORMAL, 1, 2, 3, 4);
    pci_register_slot(0x0B, PCI_CARD_NORMAL, 2, 3, 4, 1);
    pci_register_slot(0x0A, PCI_CARD_NORMAL, 3, 4, 1, 2);
    pci_register_slot(0x09, PCI_CARD_NORMAL, 4, 1, 2, 3);
    pci_register_slot(0x0D, PCI_CARD_NORMAL, 4, 1, 2, 3);
    pci_register_slot(0x06, PCI_CARD_NORMAL, 3, 4, 1, 2);
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

    machine_at_common_init_ex(model, 2);

    pci_init(PCI_CONFIG_TYPE_1);
    pci_register_slot(0x00, PCI_CARD_NORTHBRIDGE, 0, 0, 0, 0);
    pci_register_slot(0x01, PCI_CARD_AGPBRIDGE, 1, 2, 0, 0);
    pci_register_slot(0x07, PCI_CARD_SOUTHBRIDGE, 1, 2, 3, 4);
    pci_register_slot(0x0F, PCI_CARD_SOUTHBRIDGE_IDE, 1, 2, 3, 4);
    pci_register_slot(0x03, PCI_CARD_SOUTHBRIDGE_PMU, 1, 2, 3, 4);
    pci_register_slot(0x02, PCI_CARD_SOUTHBRIDGE_USB, 1, 2, 3, 4);
    pci_register_slot(0x10, PCI_CARD_NORMAL, 3, 4, 1, 2);
    pci_register_slot(0x12, PCI_CARD_NORMAL, 2, 3, 4, 1);
    pci_register_slot(0x14, PCI_CARD_NORMAL, 1, 2, 3, 4);
    device_add(&ali1541_device);
    device_add(&ali1543c_device); /* +0 */
    device_add(&sst_flash_29ee010_device);
    spd_register(SPD_TYPE_SDRAM, 0x7, 512);

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

    machine_at_common_init_ex(model, 2);

    pci_init(PCI_CONFIG_TYPE_1);
    pci_register_slot(0x00, PCI_CARD_NORTHBRIDGE, 0, 0, 0, 0);
    pci_register_slot(0x01, PCI_CARD_AGPBRIDGE, 1, 2, 0, 0);
    pci_register_slot(0x07, PCI_CARD_SOUTHBRIDGE, 1, 2, 3, 4);
    pci_register_slot(0x0F, PCI_CARD_SOUTHBRIDGE_IDE, 1, 2, 3, 4);
    pci_register_slot(0x03, PCI_CARD_SOUTHBRIDGE_PMU, 1, 2, 3, 4);
    pci_register_slot(0x02, PCI_CARD_SOUTHBRIDGE_USB, 1, 2, 3, 4);
    pci_register_slot(0x08, PCI_CARD_NORMAL, 1, 2, 3, 4);
    pci_register_slot(0x09, PCI_CARD_NORMAL, 2, 3, 4, 1);
    pci_register_slot(0x0A, PCI_CARD_NORMAL, 3, 4, 1, 2);
    device_add(&ali1541_device);
    device_add(&ali1543c_device); /* +0 */
    device_add(&sst_flash_29ee010_device);
    spd_register(SPD_TYPE_SDRAM, 0x7, 512);

    return ret;
}

int
machine_at_5ax_init(const machine_t *model)
{
    int ret;

    ret = bios_load_linear("roms/machines/5ax/5AX.F4",
                           0x000e0000, 131072, 0);

    if (bios_only || !ret)
        return ret;

    machine_at_common_init_ex(model, 2);

    pci_init(PCI_CONFIG_TYPE_1);
    pci_register_slot(0x00, PCI_CARD_NORTHBRIDGE, 0, 0, 0, 0);
    pci_register_slot(0x01, PCI_CARD_AGPBRIDGE, 1, 2, 0, 0);
    pci_register_slot(0x07, PCI_CARD_SOUTHBRIDGE, 1, 2, 3, 4);
    pci_register_slot(0x0F, PCI_CARD_SOUTHBRIDGE_IDE, 1, 2, 3, 4);
    pci_register_slot(0x03, PCI_CARD_SOUTHBRIDGE_PMU, 1, 2, 3, 4);
    pci_register_slot(0x02, PCI_CARD_SOUTHBRIDGE_USB, 1, 2, 3, 4);
    pci_register_slot(0x08, PCI_CARD_NORMAL, 1, 2, 3, 4);
    pci_register_slot(0x09, PCI_CARD_NORMAL, 2, 3, 4, 1);
    pci_register_slot(0x0A, PCI_CARD_NORMAL, 3, 4, 1, 2);
    pci_register_slot(0x0B, PCI_CARD_NORMAL, 4, 1, 2, 3);
    pci_register_slot(0x0C, PCI_CARD_NORMAL, 1, 2, 3, 4);
    device_add(&ali1541_device);
    device_add(&ali1543c_device); /* +0 */
    device_add(&sst_flash_29ee010_device);
    spd_register(SPD_TYPE_SDRAM, 0x7, 512);

    return ret;
}

int
machine_at_ax59pro_init(const machine_t *model)
{
    int ret;

    ret = bios_load_linear("roms/machines/ax59pro/AX59P236.BIN",
                           0x000c0000, 262144, 0);

    if (bios_only || !ret)
        return ret;

    machine_at_common_init_ex(model, 2);

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
    device_add(&keyboard_ps2_pci_device);
    device_add(&w83877tf_device);
    device_add(&sst_flash_39sf020_device);
    spd_register(SPD_TYPE_SDRAM, 0x7, 256);

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

    machine_at_common_init_ex(model, 2);

    pci_init(PCI_CONFIG_TYPE_1);
    pci_register_slot(0x00, PCI_CARD_NORTHBRIDGE, 0, 0, 0, 0);
    pci_register_slot(0x07, PCI_CARD_SOUTHBRIDGE, 1, 2, 3, 4);
    pci_register_slot(0x08, PCI_CARD_NORMAL,      1, 2, 3, 4);
    pci_register_slot(0x09, PCI_CARD_NORMAL,      2, 3, 4, 1);
    pci_register_slot(0x0A, PCI_CARD_NORMAL,      3, 4, 1, 2);
    pci_register_slot(0x01, PCI_CARD_AGPBRIDGE,   1, 2, 3, 4);

    device_add(&via_mvp3_device);
    device_add(&via_vt82c586b_device);
    device_add(&keyboard_ps2_pci_device);
    device_add(&w83877tf_device);
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

    machine_at_common_init_ex(model, 2);

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
    device_add(&keyboard_ps2_ami_pci_device);
    device_add(&sst_flash_39sf020_device);
    spd_register(SPD_TYPE_SDRAM, 0x7, 256);
    hwm_values.temperatures[0] += 2; /* CPU offset */
    hwm_values.temperatures[1] += 2; /* System offset */
    hwm_values.temperatures[2] = 0;  /* unused */

    if (sound_card_current == SOUND_INTERNAL)
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

    machine_at_common_init_ex(model, 2);

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
    device_add(&keyboard_ps2_ami_pci_device);
    device_add(&sst_flash_39sf010_device);
    spd_register(SPD_TYPE_SDRAM, 0x7, 256);
    device_add(&via_vt82c686_hwm_device); /* fans: CPU1, Chassis; temperatures: CPU, System, unused */
    hwm_values.temperatures[0] += 2;      /* CPU offset */
    hwm_values.temperatures[1] += 2;      /* System offset */
    hwm_values.temperatures[2] = 0;       /* unused */

    return ret;
}
