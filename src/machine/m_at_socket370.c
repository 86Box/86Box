/*
 * 86Box	A hypervisor and IBM PC system emulator that specializes in
 *		running old operating systems and software designed for IBM
 *		PC systems and compatibles from 1981 through fairly recent
 *		system designs based on the PCI bus.
 *
 *		This file is part of the 86Box distribution.
 *
 *		Implementation of Socket 370(PGA370) machines.
 *
 *
 *
 * Authors:	Miran Grca, <mgrca8@gmail.com>
 *
 *		Copyright 2016-2019 Miran Grca.
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

int
machine_at_s370slm_init(const machine_t *model)
{
    int ret;

    ret = bios_load_linear(L"roms/machines/s370slm/3LM1202.rom",
			   0x000c0000, 262144, 0);

    if (bios_only || !ret)
	return ret;

    machine_at_common_init_ex(model, 2);

    pci_init(PCI_CONFIG_TYPE_1);
    pci_register_slot(0x00, PCI_CARD_NORTHBRIDGE, 0, 0, 0, 0);
    pci_register_slot(0x07, PCI_CARD_SOUTHBRIDGE, 1, 2, 3, 4);
    pci_register_slot(0x0F, PCI_CARD_NORMAL,      1, 2, 3, 4);
    pci_register_slot(0x10, PCI_CARD_NORMAL,      2, 3, 4, 1);
    pci_register_slot(0x12, PCI_CARD_NORMAL,      3, 4, 1, 2);
    pci_register_slot(0x14, PCI_CARD_NORMAL,      4, 1, 2, 3);
    pci_register_slot(0x01, PCI_CARD_AGPBRIDGE,   1, 2, 3, 4);
    device_add(&i440lx_device);
    device_add(&piix4e_device);
    device_add(&w83977tf_device);
    device_add(&keyboard_ps2_ami_pci_device);
    device_add(&intel_flash_bxt_device);
    spd_register(SPD_TYPE_SDRAM, 0x7, 256);
    device_add(&w83781d_device); /* fans: CPU, Fan 2, Chassis; temperatures: unused, CPU, unused */
    hwm_values.temperatures[0] = 0; /* unused */
    hwm_values.temperatures[2] = 0; /* unused */
	
    return ret;
}

int
machine_at_trinity371_init(const machine_t *model)
{
    int ret;

    ret = bios_load_linear(L"roms/machines/trinity371/BX57200A.ROM",
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
    pci_register_slot(0x01, PCI_CARD_AGPBRIDGE,  1, 2, 3, 4);
    device_add(&i440bx_device);
    device_add(&piix4e_device);
    device_add(&keyboard_ps2_ami_pci_device);
    device_add(&w83977f_370_device);
    device_add(&intel_flash_bxt_device);

    return ret;
}

int
machine_at_p6bap_init(const machine_t *model)
{
    int ret;

    ret = bios_load_linear(L"roms/machines/p6bap/bapa14a.BIN",
			   0x000c0000, 262144, 0);

    if (bios_only || !ret)
	return ret;

    machine_at_common_init_ex(model, 2);

    pci_init(PCI_CONFIG_TYPE_1);
    pci_register_slot(0x00, PCI_CARD_NORTHBRIDGE, 0, 0, 0, 0);
    pci_register_slot(0x07, PCI_CARD_SOUTHBRIDGE, 1, 2, 0, 0);
    pci_register_slot(0x14, PCI_CARD_NORMAL,      2, 3, 4, 1);
    pci_register_slot(0x13, PCI_CARD_NORMAL,      3, 4, 1, 2);
    pci_register_slot(0x12, PCI_CARD_NORMAL,      4, 1, 2, 3);
    pci_register_slot(0x11, PCI_CARD_NORMAL,      1, 2, 3, 4);
    pci_register_slot(0x10, PCI_CARD_NORMAL,      2, 3, 4, 1);
    pci_register_slot(0x01, PCI_CARD_AGPBRIDGE,   1, 2, 3, 4);
    device_add(&via_apro133a_device);  /* Rebranded as ET82C693A */
    device_add(&via_vt82c596b_device); /* Rebranded as ET82C696B */
    device_add(&w83977ef_device);
    device_add(&keyboard_ps2_ami_pci_device);
    device_add(&sst_flash_39sf020_device);
    spd_register(SPD_TYPE_SDRAM, 0x7, 256);
    return ret;
}

int
machine_at_cubx_init(const machine_t *model)
{
    int ret;

    ret = bios_load_linear(L"roms/machines/cubx/1008cu.004",
			   0x000c0000, 262144, 0);

    if (bios_only || !ret)
	return ret;

    machine_at_common_init_ex(model, 2);

    pci_init(PCI_CONFIG_TYPE_1);
    pci_register_slot(0x00, PCI_CARD_NORTHBRIDGE, 0, 0, 0, 0);
    pci_register_slot(0x04, PCI_CARD_SOUTHBRIDGE, 1, 2, 3, 4);
    pci_register_slot(0x07, PCI_CARD_IDE,         2, 3, 4, 1);
    pci_register_slot(0x09, PCI_CARD_NORMAL,      4, 1, 2, 3);
    pci_register_slot(0x0A, PCI_CARD_NORMAL,      3, 4, 1, 2);
    pci_register_slot(0x0B, PCI_CARD_NORMAL,      2, 3, 4, 1);
    pci_register_slot(0x0C, PCI_CARD_NORMAL,      1, 2, 3, 4);
    pci_register_slot(0x0D, PCI_CARD_NORMAL,      4, 1, 2, 3);
    pci_register_slot(0x0E, PCI_CARD_NORMAL,      3, 4, 1, 2);
    pci_register_slot(0x01, PCI_CARD_AGPBRIDGE,   1, 2, 3, 4);
    device_add(&i440bx_device);
    device_add(&piix4e_device);
    device_add(&keyboard_ps2_ami_pci_device);
    device_add(&w83977ef_device);
    device_add(ics9xxx_get(ICS9250_08));
    device_add(&sst_flash_39sf020_device);
    spd_register(SPD_TYPE_SDRAM, 0xF, 256);
    device_add(&as99127f_device); /* fans: Chassis, CPU, Power; temperatures: MB, JTPWR, CPU */

    return ret;
}

int
machine_at_atc7020bxii_init(const machine_t *model)
{
    int ret;

    ret = bios_load_linear(L"roms/machines/atc7020bxii/7020s102.bin",
			   0x000c0000, 262144, 0);

    if (bios_only || !ret)
	return ret;

    machine_at_common_init_ex(model, 2);

    pci_init(PCI_CONFIG_TYPE_1);
    pci_register_slot(0x00, PCI_CARD_NORTHBRIDGE, 0, 0, 0, 0);
    pci_register_slot(0x07, PCI_CARD_SOUTHBRIDGE, 1, 2, 3, 4);
    pci_register_slot(0x0A, PCI_CARD_NORMAL,      1, 2, 3, 4);
    pci_register_slot(0x0B, PCI_CARD_NORMAL,      2, 3, 4, 1);
    pci_register_slot(0x0C, PCI_CARD_NORMAL,      3, 4, 1, 2);
    pci_register_slot(0x0D, PCI_CARD_NORMAL,      4, 1, 2, 3);
    pci_register_slot(0x0E, PCI_CARD_NORMAL,      4, 1, 2, 3);
    pci_register_slot(0x01, PCI_CARD_AGPBRIDGE,   1, 2, 3, 4);
    device_add(&i440bx_device);
    device_add(&slc90e66_device);
    device_add(&keyboard_ps2_pci_device);
    device_add(&w83977ef_device);
    device_add(&sst_flash_39sf020_device);
    spd_register(SPD_TYPE_SDRAM, 0xF, 256);

    return ret;	
}

int
machine_at_ambx133_init(const machine_t *model)
{
    int ret;

    ret = bios_load_linear(L"roms/machines/ambx133/mkbx2vg2.bin",
			   0x000c0000, 262144, 0);

    if (bios_only || !ret)
	return ret;

    machine_at_common_init_ex(model, 2);

    pci_init(PCI_CONFIG_TYPE_1);
    pci_register_slot(0x00, PCI_CARD_NORTHBRIDGE, 0, 0, 0, 0);
    pci_register_slot(0x07, PCI_CARD_SOUTHBRIDGE, 1, 2, 3, 4);
    pci_register_slot(0x09, PCI_CARD_NORMAL, 	  1, 2, 3, 4);
    pci_register_slot(0x0A, PCI_CARD_NORMAL, 	  2, 3, 4, 1);
    pci_register_slot(0x0B, PCI_CARD_NORMAL, 	  3, 4, 1, 2);
    pci_register_slot(0x0C, PCI_CARD_NORMAL, 	  4, 1, 2, 3);
    pci_register_slot(0x0D, PCI_CARD_NORMAL, 	  4, 1, 2, 3);
    pci_register_slot(0x01, PCI_CARD_AGPBRIDGE,   1, 2, 3, 4);
    device_add(&i440bx_device);
    device_add(&piix4e_device);
    device_add(&w83977ef_device);
    device_add(&keyboard_ps2_ami_pci_device);
    device_add(&sst_flash_39sf020_device);
    spd_register(SPD_TYPE_SDRAM, 0x7, 256);

    return ret;
}

int
machine_at_awo671r_init(const machine_t *model)
{
    int ret;

    ret = bios_load_linear(L"roms/machines/awo671r/a08139c.bin",
			   0x000c0000, 262144, 0);

    if (bios_only || !ret)
	return ret;

    machine_at_common_init_ex(model, 2);

    pci_init(PCI_CONFIG_TYPE_1);
    pci_register_slot(0x00, PCI_CARD_NORTHBRIDGE, 0, 0, 0, 0);
    pci_register_slot(0x07, PCI_CARD_SOUTHBRIDGE, 1, 2, 3, 4);
    pci_register_slot(0x09, PCI_CARD_NORMAL, 	  1, 2, 3, 4);
    pci_register_slot(0x0A, PCI_CARD_NORMAL, 	  2, 3, 4, 1);
    pci_register_slot(0x0B, PCI_CARD_NORMAL, 	  3, 4, 1, 2);
    pci_register_slot(0x0C, PCI_CARD_NORMAL, 	  4, 1, 2, 3);
    pci_register_slot(0x0D, PCI_CARD_NORMAL, 	  2, 3, 4, 1);
    pci_register_slot(0x01, PCI_CARD_AGPBRIDGE,   1, 2, 3, 4);
    device_add(&i440bx_device);
    device_add(&piix4e_device);
    device_add_inst(&w83977ef_device, 1);
    device_add_inst(&w83977ef_device, 2);
    device_add(&keyboard_ps2_pci_device);
    device_add(&sst_flash_39sf020_device);
    spd_register(SPD_TYPE_SDRAM, 0x3, 256);

    return ret;
}

int
machine_at_63a_init(const machine_t *model)
{
    int ret;

    ret = bios_load_linear(L"roms/machines/63a1/63a-q3.bin",
			   0x000c0000, 262144, 0);

    if (bios_only || !ret)
	return ret;

    machine_at_common_init_ex(model, 2);

    pci_init(PCI_CONFIG_TYPE_1);
    pci_register_slot(0x00, PCI_CARD_NORTHBRIDGE, 0, 0, 0, 0);
    pci_register_slot(0x07, PCI_CARD_SOUTHBRIDGE, 1, 2, 3, 4);
    pci_register_slot(0x08, PCI_CARD_NORMAL, 	  1, 2, 3, 4);
    pci_register_slot(0x09, PCI_CARD_NORMAL, 	  2, 3, 4, 1);
    pci_register_slot(0x0A, PCI_CARD_NORMAL, 	  3, 4, 1, 2);
    pci_register_slot(0x0B, PCI_CARD_NORMAL, 	  4, 1, 2, 3);
    pci_register_slot(0x0C, PCI_CARD_NORMAL, 	  1, 2, 3, 4); /* Integrated Sound? */
    pci_register_slot(0x01, PCI_CARD_AGPBRIDGE,   1, 2, 3, 4);
    device_add(&i440zx_device);
    device_add(&piix4e_device);
    device_add(&w83977tf_device);
    device_add(&keyboard_ps2_ami_pci_device);
    device_add(&intel_flash_bxt_device);
    spd_register(SPD_TYPE_SDRAM, 0x3, 256);

    return ret;
}

int
machine_at_apas3_init(const machine_t *model)
{
    int ret;

    ret = bios_load_linear(L"roms/machines/apas3/V0218SAG.BIN",
			   0x000c0000, 262144, 0);

    if (bios_only || !ret)
	return ret;

    machine_at_common_init_ex(model, 2);

    pci_init(PCI_CONFIG_TYPE_1);
    pci_register_slot(0x00, PCI_CARD_NORTHBRIDGE, 0, 0, 0, 0);
    pci_register_slot(0x07, PCI_CARD_SOUTHBRIDGE, 1, 2, 0, 0);
    pci_register_slot(0x0F, PCI_CARD_NORMAL,      1, 2, 3, 4);
    pci_register_slot(0x10, PCI_CARD_NORMAL,      2, 3, 4, 1);
    pci_register_slot(0x13, PCI_CARD_NORMAL,      3, 4, 1, 2);
    pci_register_slot(0x14, PCI_CARD_NORMAL,      4, 1, 2, 3);
    pci_register_slot(0x01, PCI_CARD_AGPBRIDGE,   1, 2, 3, 4);
    device_add(&via_apro_device);
    device_add(&via_vt82c586b_device);
    device_add(&fdc37c669_device);
    device_add(&keyboard_ps2_ami_pci_device);
    device_add(&sst_flash_39sf020_device);
    spd_register(SPD_TYPE_SDRAM, 0x7, 256);

    return ret;
}

int
machine_at_wcf681_init(const machine_t *model)
{
    int ret;

    ret = bios_load_linear(L"roms/machines/wcf681/681osda2.bin",
			   0x000c0000, 262144, 0);

    if (bios_only || !ret)
	return ret;

    machine_at_common_init_ex(model, 2);

    pci_init(PCI_CONFIG_TYPE_1);
    pci_register_slot(0x00, PCI_CARD_NORTHBRIDGE, 0, 0, 0, 0);
    pci_register_slot(0x07, PCI_CARD_SOUTHBRIDGE, 1, 2, 0, 0);
    pci_register_slot(0x14, PCI_CARD_NORMAL,      2, 3, 4, 1);
    pci_register_slot(0x13, PCI_CARD_NORMAL,      3, 4, 1, 2);
    pci_register_slot(0x12, PCI_CARD_NORMAL,      4, 1, 2, 3);
    pci_register_slot(0x11, PCI_CARD_NORMAL,      1, 2, 3, 4);
    pci_register_slot(0x10, PCI_CARD_NORMAL,      2, 3, 4, 1);
    pci_register_slot(0x01, PCI_CARD_AGPBRIDGE,   1, 2, 3, 4);
    device_add(&via_apro133a_device);
    device_add(&via_vt82c596b_device);
    device_add(&w83977tf_device);
    device_add(&keyboard_ps2_ami_pci_device);
    device_add(&sst_flash_39sf020_device);
    spd_register(SPD_TYPE_SDRAM, 0x3, 512);
    device_add(&w83781d_device); /* fans: CPU, unused, unused; temperatures: System, unused, CPU */
    hwm_values.voltages[1] = 2500; /* +2.5V */
    hwm_values.fans[1] = 0; /* unused */
    hwm_values.fans[2] = 0; /* unused */
    hwm_values.temperatures[1] = 0; /* unused */

    return ret;
}

int
machine_at_cuv4xls_init(const machine_t *model)
{
    int ret;

    ret = bios_load_linear(L"roms/machines/cuv4xls/1005LS.001",
			   0x000c0000, 262144, 0);

    if (bios_only || !ret)
	return ret;

    machine_at_common_init_ex(model, 2);

    pci_init(PCI_CONFIG_TYPE_1);
    pci_register_slot(0x00, PCI_CARD_NORTHBRIDGE, 0, 0, 0, 0);
    pci_register_slot(0x04, PCI_CARD_SOUTHBRIDGE, 4, 1, 2, 3);
    pci_register_slot(0x05, PCI_CARD_SOUND,       3, 0, 0, 0);
    pci_register_slot(0x06, PCI_CARD_NORMAL,      3, 4, 1, 2);
    pci_register_slot(0x07, PCI_CARD_NORMAL,      2, 3, 0, 0);
    pci_register_slot(0x08, PCI_CARD_NORMAL,      1, 2, 3, 4);
    pci_register_slot(0x09, PCI_CARD_NORMAL,      4, 1, 2, 3);
    pci_register_slot(0x0A, PCI_CARD_NORMAL,      3, 4, 1, 2);
    pci_register_slot(0x0B, PCI_CARD_NORMAL,      2, 3, 4, 1);
    pci_register_slot(0x14, PCI_CARD_NORMAL,      4, 0, 0, 0);
    pci_register_slot(0x01, PCI_CARD_AGPBRIDGE,   1, 2, 3, 4);
    device_add(&via_apro133a_device);
    device_add(&via_vt82c686b_device);
    device_add(&via_vt82c686_sio_device);
    device_add(&keyboard_ps2_ami_pci_device);
    device_add(ics9xxx_get(ICS9250_18));
    device_add(&sst_flash_39sf020_device);
    spd_register(SPD_TYPE_SDRAM, 0xF, 512);
    device_add(&as99127f_device); /* fans: Chassis, CPU, Power; temperatures: MB, JTPWR, CPU */

    return ret;
}

int
machine_at_6via90ap_init(const machine_t *model)
{
    int ret;

    ret = bios_load_linear(L"roms/machines/6via90ap/90ap10.bin",
			   0x000c0000, 262144, 0);

    if (bios_only || !ret)
	return ret;

    machine_at_common_init_ex(model, 2);

    pci_init(PCI_CONFIG_TYPE_1);
    pci_register_slot(0x00, PCI_CARD_NORTHBRIDGE, 0, 0, 0, 0);
    pci_register_slot(0x07, PCI_CARD_SOUTHBRIDGE, 1, 2, 0, 0);
    pci_register_slot(0x09, PCI_CARD_NORMAL,      1, 2, 3, 4);
    pci_register_slot(0x0A, PCI_CARD_NORMAL,      2, 3, 4, 1);
    pci_register_slot(0x0B, PCI_CARD_NORMAL,      3, 4, 1, 2);
    pci_register_slot(0x0C, PCI_CARD_NORMAL,      4, 1, 2, 3);
    pci_register_slot(0x0D, PCI_CARD_NORMAL,      1, 2, 3, 4);
    pci_register_slot(0x01, PCI_CARD_AGPBRIDGE,   1, 2, 3, 4);
    device_add(&via_apro133a_device);
    device_add(&via_vt82c686b_device);
    device_add(&via_vt82c686_sio_device);
    device_add(&keyboard_ps2_ami_pci_device);
    device_add(ics9xxx_get(ICS9250_18));
    device_add(&sst_flash_39sf020_device);
    spd_register(SPD_TYPE_SDRAM, 0x7, 512);
    device_add(&via_vt82c686_hwm_device); /* fans: CPU1, CPU2; temperatures: CPU, System, unused */
    hwm_values.temperatures[0] += 2; /* CPU offset */
    hwm_values.temperatures[1] += 2; /* System offset */
    hwm_values.temperatures[2] = 0; /* unused */

    return ret;
}

int
machine_at_603tcf_init(const machine_t *model)
{
    int ret;

    ret = bios_load_linear(L"roms/machines/603tcf/603tcfA4.BIN",
			   0x000c0000, 262144, 0);

    if (bios_only || !ret)
	return ret;

    machine_at_common_init_ex(model, 2);

    pci_init(PCI_CONFIG_TYPE_1);
    pci_register_slot(0x00, PCI_CARD_NORTHBRIDGE, 0, 0, 0, 0);
    pci_register_slot(0x07, PCI_CARD_SOUTHBRIDGE, 1, 2, 0, 0);
    pci_register_slot(0x08, PCI_CARD_NORMAL,      1, 2, 3, 4);
    pci_register_slot(0x09, PCI_CARD_NORMAL,      2, 3, 4, 1);
    pci_register_slot(0x0A, PCI_CARD_NORMAL,      3, 4, 1, 2);
    pci_register_slot(0x0B, PCI_CARD_NORMAL,      4, 1, 2, 3);
    pci_register_slot(0x01, PCI_CARD_AGPBRIDGE,   1, 2, 3, 4);
    device_add(&via_vt8601_device);
    device_add(&via_vt82c686b_device);
    device_add(&via_vt82c686_sio_device);
    device_add(&keyboard_ps2_ami_pci_device);
    device_add(&sst_flash_39sf020_device);
    spd_register(SPD_TYPE_SDRAM, 0x3, 512);
    device_add(&via_vt82c686_hwm_device); /* fans: 1, 2; temperatures: CPU, System, unused */
    hwm_values.temperatures[0] += 2; /* CPU offset */
    hwm_values.temperatures[1] += 2; /* System offset */
    hwm_values.temperatures[2] = 0; /* unused */

    return ret;
}
