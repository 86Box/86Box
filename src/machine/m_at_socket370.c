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
    pci_register_slot(0x01, PCI_CARD_SPECIAL,     1, 2, 3, 4);
    device_add(&i440lx_device);
    device_add(&piix4e_device);
    device_add(&w83977tf_device);
    device_add(&keyboard_ps2_ami_pci_device);
    device_add(&intel_flash_bxt_device);
    spd_register(SPD_TYPE_SDRAM, 0x7, 256);

    hwm_values_t machine_hwm = {
    	{    /* fan speeds */
    		3000,	/* CPU */
    		3000,	/* Fan 2 */
    		3000	/* Chassis */
    	}, { /* temperatures */
    		0,	/* unused */
    		30,	/* CPU */
    		0	/* unused */
    	}, { /* voltages */
    		2050,				   /* CPU1 (2.05V by default) */
    		0,				   /* unused */
    		3300,				   /* +3.3V */
    		RESISTOR_DIVIDER(5000,   11,  16), /* +5V  (divider values bruteforced) */
    		RESISTOR_DIVIDER(12000,  28,  10), /* +12V (28K/10K divider suggested in the W83781D datasheet) */
    		RESISTOR_DIVIDER(12000, 853, 347), /* -12V (divider values bruteforced) */
    		RESISTOR_DIVIDER(5000,    1,   2)  /* -5V  (divider values bruteforced) */
    	}
    };
    hwm_set_values(machine_hwm);
    device_add(&w83781d_device);
	
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
    pci_register_slot(0x01, PCI_CARD_SPECIAL,     1, 2, 3, 4);
    device_add(&i440bx_device);
    device_add(&piix4e_device);
    device_add(&keyboard_ps2_ami_pci_device);
    // device_add(&keyboard_ps2_pci_device);
    device_add(&w83977ef_device);
    device_add(&sst_flash_39sf020_device);
    spd_register(SPD_TYPE_SDRAM, 0xF, 256);

    hwm_values_t machine_hwm = {
    	{    /* fan speeds */
    		3000,	/* Chassis */
    		3000,	/* CPU */
    		3000	/* Power */
    	}, { /* temperatures */
    		30,	/* MB */
    		30,	/* JTPWR */
    		30	/* CPU */
    	}, { /* voltages */
    		2050,				   /* VCORE (2.05V by default) */
    		0,				   /* unused */
    		3300,				   /* +3.3V */
    		RESISTOR_DIVIDER(5000,   11,  16), /* +5V  (divider values bruteforced) */
    		RESISTOR_DIVIDER(12000,  28,  10), /* +12V (28K/10K divider suggested in the W83781D datasheet) */
    		RESISTOR_DIVIDER(12000,  59,  20), /* -12V (divider values bruteforced) */
    		RESISTOR_DIVIDER(5000,    1,   2)  /* -5V  (divider values bruteforced) */
    	}
    };
    hwm_set_values(machine_hwm);
    device_add(&as99127f_device);

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
    pci_register_slot(0x01, PCI_CARD_SPECIAL,     1, 2, 3, 4);
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
    pci_register_slot(0x01, PCI_CARD_SPECIAL, 	  1, 2, 3, 4);
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
    pci_register_slot(0x01, PCI_CARD_SPECIAL,     1, 2, 3, 4);
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
    pci_register_slot(0x01, PCI_CARD_SPECIAL,     1, 2, 3, 4);
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
    pci_register_slot(0x01, PCI_CARD_SPECIAL,     1, 2, 3, 4);
    device_add(&via_apro_device);
    device_add(&via_vt82c586b_device);
    device_add(&fdc37c669_device);
    device_add(&keyboard_ps2_ami_pci_device);
    device_add(&sst_flash_39sf020_device);
    spd_register(SPD_TYPE_SDRAM, 0x7, 256);

    return ret;
}
