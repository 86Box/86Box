/*
 * 86Box	A hypervisor and IBM PC system emulator that specializes in
 *		running old operating systems and software designed for IBM
 *		PC systems and compatibles from 1981 through fairly recent
 *		system designs based on the PCI bus.
 *
 *		This file is part of the 86Box distribution.
 *
 *		Implementation of Slot 1 machines.
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
#include <86box/device.h>
#include <86box/pci.h>
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
#include <86box/sound.h>

int
machine_at_p65up5_cpknd_init(const machine_t *model)
{
    int ret;

    ret = bios_load_linear(L"roms/machines/p65up5/ndkn0218.awd",
			   0x000e0000, 131072, 0);

    if (bios_only || !ret)
	return ret;

    machine_at_p65up5_common_init(model, &i440fx_device);

    return ret;
}


int
machine_at_kn97_init(const machine_t *model)
{
    int ret;

    ret = bios_load_linear(L"roms/machines/kn97/0116I.001",
			   0x000e0000, 131072, 0);

    if (bios_only || !ret)
	return ret;

    machine_at_common_init(model);

    pci_init(PCI_CONFIG_TYPE_1);
    pci_register_slot(0x00, PCI_CARD_NORTHBRIDGE, 0, 0, 0, 0);
    pci_register_slot(0x01, PCI_CARD_SOUTHBRIDGE, 0, 0, 0, 0);
    pci_register_slot(0x09, PCI_CARD_NORMAL,      4, 1, 2, 3);
    pci_register_slot(0x0A, PCI_CARD_NORMAL,      3, 4, 1, 2);
    pci_register_slot(0x0B, PCI_CARD_NORMAL,      2, 3, 4, 1);
    pci_register_slot(0x0C, PCI_CARD_NORMAL,      1, 2, 3, 4);
    pci_register_slot(0x0D, PCI_CARD_NORMAL,      4, 1, 2, 3);
    device_add(&i440fx_device);
    device_add(&piix3_device);
    device_add(&keyboard_ps2_pci_device);
    device_add(&w83877f_device);
    device_add(&intel_flash_bxt_device);

    hwm_values_t machine_hwm = {
    	{    /* fan speeds (incorrect divisor for some reason) */
    		6000,	/* Chassis */
    		6000,	/* CPU */
    		6000	/* Power */
    	}, { /* temperatures */
    		30	/* MB */
    	}, { /* voltages */
    		2800,				   /* VCORE (2.8V by default) */
    		0,				   /* unused */
    		3300,				   /* +3.3V */
    		RESISTOR_DIVIDER(5000,   11,  16), /* +5V  (divider values bruteforced) */
    		RESISTOR_DIVIDER(12000,  28,  10), /* +12V (28K/10K divider suggested in the W83781D datasheet) */
    		RESISTOR_DIVIDER(12000, 853, 347), /* -12V (divider values bruteforced) */
    		RESISTOR_DIVIDER(5000,    1,   2)  /* -5V  (divider values bruteforced) */
    	}
    };
    hwm_set_values(machine_hwm);
    device_add(&lm78_device);
	
    return ret;
}


int
machine_at_lx6_init(const machine_t *model)
{
    int ret;

    ret = bios_load_linear(L"roms/machines/lx6/LX6C_PZ.B00",
			   0x000e0000, 131072, 0);

    if (bios_only || !ret)
	return ret;

    machine_at_common_init_ex(model, 2);

    pci_init(PCI_CONFIG_TYPE_1);
    pci_register_slot(0x00, PCI_CARD_NORTHBRIDGE, 0, 0, 0, 0);
    pci_register_slot(0x07, PCI_CARD_SOUTHBRIDGE, 1, 2, 3, 4);
    pci_register_slot(0x09, PCI_CARD_NORMAL,      4, 1, 2, 3);
    pci_register_slot(0x0B, PCI_CARD_NORMAL,      3, 4, 1, 2);
    pci_register_slot(0x0D, PCI_CARD_NORMAL,      2, 3, 4, 1);
    pci_register_slot(0x0F, PCI_CARD_NORMAL,      1, 2, 3, 4);
    pci_register_slot(0x01, PCI_CARD_SPECIAL,     1, 2, 3, 4);
    device_add(&i440lx_device);
    device_add(&piix4e_device);
    device_add(&keyboard_ps2_pci_device);
    device_add(&w83977tf_device);
    device_add(&sst_flash_29ee010_device);
    spd_register(SPD_TYPE_SDRAM, 0xF, 256);
	
    return ret;
}

int
machine_at_spitfire_init(const machine_t *model)
{
    int ret;

    ret = bios_load_linear(L"roms/machines/spitfire/SPIHM.02",
			   0x000c0000, 262144, 0);

    if (bios_only || !ret)
	return ret;

    machine_at_common_init_ex(model, 2);

    pci_init(PCI_CONFIG_TYPE_1);
    pci_register_slot(0x00, PCI_CARD_NORTHBRIDGE, 0, 0, 0, 0);
    pci_register_slot(0x07, PCI_CARD_SOUTHBRIDGE, 0, 0, 0, 0);
    device_add(&i440lx_device);
    device_add(&piix4e_device);
    device_add(&keyboard_ps2_pci_device);
    device_add(&fdc37c935_device);
    device_add(&intel_flash_bxt_device);
	spd_register(SPD_TYPE_SDRAM, 0xF, 256);

    hwm_values_t machine_hwm = {
    	{    /* fan speeds (incorrect divisor for some reason) */
    		6000,	/* Chassis */
    		6000,	/* CPU */
    		6000	/* Power */
    	}, { /* temperatures */
    		30	/* MB */
    	}, { /* voltages */
    		2800,				   /* VCORE (2.8V by default) */
    		0,				   /* unused */
    		3300,				   /* +3.3V */
    		RESISTOR_DIVIDER(5000,   11,  16), /* +5V  (divider values bruteforced) */
    		RESISTOR_DIVIDER(12000,  28,  10), /* +12V (28K/10K divider suggested in the W83781D datasheet) */
    		RESISTOR_DIVIDER(12000, 853, 347), /* -12V (divider values bruteforced) */
    		RESISTOR_DIVIDER(5000,    1,   2)  /* -5V  (divider values bruteforced) */
    	}
    };
    hwm_set_values(machine_hwm);
    device_add(&lm78_device);

    return ret;
}

int
machine_at_p6i440e2_init(const machine_t *model)
{
    int ret;

    ret = bios_load_linear(L"roms/machines/p6i440e2/E2_v14sl.bin",
			   0x000e0000, 131072, 0);

    if (bios_only || !ret)
	return ret;

    machine_at_common_init_ex(model, 2);

    pci_init(PCI_CONFIG_TYPE_1);
    pci_register_slot(0x00, PCI_CARD_NORTHBRIDGE, 0, 0, 0, 0);
    pci_register_slot(0x07, PCI_CARD_SOUTHBRIDGE, 1, 2, 3, 4);
    pci_register_slot(0x09, PCI_CARD_NORMAL,      1, 2, 3, 4);
    pci_register_slot(0x0A, PCI_CARD_NORMAL,      2, 3, 4, 1);
    pci_register_slot(0x01, PCI_CARD_SPECIAL,     1, 2, 3, 4);
    device_add(&i440ex_device);
    device_add(&piix4_device);
    device_add(&keyboard_ps2_pci_device);
    device_add(&w83977tf_device);
    device_add(&sst_flash_29ee010_device);
    spd_register(SPD_TYPE_SDRAM, 0x03, 256);

    hwm_values_t machine_hwm = {
    	{    /* fan speeds */
    		3000,	/* Chassis */
    		3000,	/* CPU */
    		3000	/* Power */
    	}, { /* temperatures */
    		30,	/* MB */
    		0,	/* unused */
    		27	/* CPU */
    	}, { /* voltages */
    		2050,				   /* VCORE (2.05V by default) */
    		0,				   /* unused */
    		3300,				   /* +3.3V */
    		RESISTOR_DIVIDER(5000,   11,  16), /* +5V  (divider values bruteforced) */
    		RESISTOR_DIVIDER(12000,  28,  10), /* +12V (28K/10K divider suggested in the W83781D datasheet) */
    		RESISTOR_DIVIDER(12000, 853, 347), /* -12V (divider values bruteforced) */
    		RESISTOR_DIVIDER(5000,    1,   2)  /* -5V  (divider values bruteforced) */
    	}
    };
    if (model->cpu[cpu_manufacturer].cpus[cpu_effective].cpu_type == CPU_PENTIUM2)
    	machine_hwm.voltages[0] = 2800; /* set higher VCORE (2.8V) for Klamath */
    hwm_set_values(machine_hwm);
    device_add(&w83781d_device);

    return ret;
}


int
machine_at_p2bls_init(const machine_t *model)
{
    int ret;

    ret = bios_load_linear(L"roms/machines/p2bls/1014ls.003",
			   0x000c0000, 262144, 0);

    if (bios_only || !ret)
	return ret;

    machine_at_common_init_ex(model, 2);

    pci_init(PCI_CONFIG_TYPE_1);
    pci_register_slot(0x00, PCI_CARD_NORTHBRIDGE, 0, 0, 0, 0);
    pci_register_slot(0x04, PCI_CARD_SOUTHBRIDGE, 1, 2, 3, 4);
    pci_register_slot(0x06, PCI_CARD_NORMAL,      4, 1, 2, 3);
    pci_register_slot(0x07, PCI_CARD_NORMAL,      3, 4, 1, 2);
    pci_register_slot(0x0B, PCI_CARD_NORMAL,      2, 3, 4, 1);
    pci_register_slot(0x0C, PCI_CARD_NORMAL,      1, 2, 3, 4);
    pci_register_slot(0x09, PCI_CARD_NORMAL,      4, 1, 2, 3);
    pci_register_slot(0x0A, PCI_CARD_NORMAL,      3, 4, 1, 2);
    pci_register_slot(0x01, PCI_CARD_SPECIAL,     1, 2, 3, 4);
    device_add(&i440bx_device);
    device_add(&piix4e_device);
    device_add(&keyboard_ps2_pci_device);
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
    		0,	/* unused */
    		27	/* CPU */
    	}, { /* voltages */
    		2050,				   /* VCORE (2.05V by default) */
    		0,				   /* unused */
    		3300,				   /* +3.3V */
    		RESISTOR_DIVIDER(5000,   11,  16), /* +5V  (divider values bruteforced) */
    		RESISTOR_DIVIDER(12000,  28,  10), /* +12V (28K/10K divider suggested in the W83781D datasheet) */
    		RESISTOR_DIVIDER(12000, 853, 347), /* -12V (divider values bruteforced) */
    		RESISTOR_DIVIDER(5000,    1,   2)  /* -5V  (divider values bruteforced) */
    	}
    };
    if (model->cpu[cpu_manufacturer].cpus[cpu_effective].cpu_type == CPU_PENTIUM2)
    	machine_hwm.voltages[0] = 2800; /* set higher VCORE (2.8V) for Klamath */
    hwm_set_values(machine_hwm);
    device_add(&w83781d_device);

    return ret;
}


int
machine_at_p3bf_init(const machine_t *model)
{
    int ret;

    ret = bios_load_linear(L"roms/machines/p3bf/1008f.004",
			   0x000c0000, 262144, 0);

    if (bios_only || !ret)
	return ret;

    machine_at_common_init_ex(model, 2);

    pci_init(PCI_CONFIG_TYPE_1);
    pci_register_slot(0x00, PCI_CARD_NORTHBRIDGE, 0, 0, 0, 0);
    pci_register_slot(0x04, PCI_CARD_SOUTHBRIDGE, 1, 2, 3, 4);
    pci_register_slot(0x09, PCI_CARD_NORMAL,      4, 1, 2, 3);
    pci_register_slot(0x0A, PCI_CARD_NORMAL,      3, 4, 1, 2);
    pci_register_slot(0x0B, PCI_CARD_NORMAL,      2, 3, 4, 1);
    pci_register_slot(0x0C, PCI_CARD_NORMAL,      1, 2, 3, 4);
    pci_register_slot(0x0D, PCI_CARD_NORMAL,      4, 1, 2, 3);
    pci_register_slot(0x0E, PCI_CARD_NORMAL,      3, 4, 1, 2);
    pci_register_slot(0x01, PCI_CARD_SPECIAL,     1, 2, 3, 4);
    device_add(&i440bx_device);
    device_add(&piix4e_device);
    device_add(&keyboard_ps2_pci_device);
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
    		RESISTOR_DIVIDER(12000,   3,   1), /* +12V (divider values bruteforced) */
    		RESISTOR_DIVIDER(12000,  59,  20), /* -12V (divider values bruteforced) */
    		RESISTOR_DIVIDER(5000,    1,   2)  /* -5V  (divider values bruteforced) */
    	}
    };
    if (model->cpu[cpu_manufacturer].cpus[cpu_effective].cpu_type == CPU_PENTIUM2)
    	machine_hwm.voltages[0] = 2800; /* set higher VCORE (2.8V) for Klamath */
    hwm_set_values(machine_hwm);
    device_add(&as99127f_device);

    return ret;
}


int
machine_at_bf6_init(const machine_t *model)
{
    int ret;

    ret = bios_load_linear(L"roms/machines/bf6/Beh_70.bin",
			   0x000c0000, 262144, 0);

    if (bios_only || !ret)
	return ret;

    machine_at_common_init_ex(model, 2);

    pci_init(PCI_CONFIG_TYPE_1);
    pci_register_slot(0x00, PCI_CARD_NORTHBRIDGE, 0, 0, 0, 0);
    pci_register_slot(0x07, PCI_CARD_SOUTHBRIDGE, 1, 2, 3, 4);
    pci_register_slot(0x08, PCI_CARD_NORMAL,      3, 4, 1, 2);
    pci_register_slot(0x09, PCI_CARD_NORMAL,      2, 1, 4, 3);
    pci_register_slot(0x0B, PCI_CARD_NORMAL,      4, 1, 2, 3);
    pci_register_slot(0x11, PCI_CARD_NORMAL,      1, 2, 3, 4);
    pci_register_slot(0x0D, PCI_CARD_NORMAL,      3, 4, 1, 2);
    pci_register_slot(0x0F, PCI_CARD_NORMAL,      2, 3, 4, 1);
    pci_register_slot(0x13, PCI_CARD_NORMAL,      3, 3, 1, 2);
    pci_register_slot(0x01, PCI_CARD_SPECIAL,     1, 2, 3, 4);
    device_add(&i440bx_device);
    device_add(&piix4e_device);
    device_add(&keyboard_ps2_pci_device);
    device_add(&w83977ef_device);
    device_add(&sst_flash_39sf020_device);
    spd_register(SPD_TYPE_SDRAM, 0x7, 256);    

    return ret;
}


int
machine_at_ax6bc_init(const machine_t *model)
{
    int ret;

    ret = bios_load_linear(L"roms/machines/ax6bc/AX6BC_R2.59.bin",
			   0x000c0000, 262144, 0);

    if (bios_only || !ret)
	return ret;

    machine_at_common_init_ex(model, 2);

    pci_init(PCI_CONFIG_TYPE_1);
    pci_register_slot(0x00, PCI_CARD_NORTHBRIDGE, 0, 0, 0, 0);
    pci_register_slot(0x07, PCI_CARD_SOUTHBRIDGE, 1, 2, 3, 4);
    pci_register_slot(0x09, PCI_CARD_NORMAL,      4, 1, 2, 3);
    pci_register_slot(0x0A, PCI_CARD_NORMAL,      3, 4, 1, 2);
    pci_register_slot(0x0B, PCI_CARD_NORMAL,      2, 3, 4, 1);
    pci_register_slot(0x0C, PCI_CARD_NORMAL,      1, 2, 3, 4);
    pci_register_slot(0x0D, PCI_CARD_NORMAL,      4, 1, 2, 3);
    pci_register_slot(0x01, PCI_CARD_SPECIAL,     1, 2, 3, 4);
    device_add(&i440bx_device);
    device_add(&piix4e_device);
    device_add(&keyboard_ps2_pci_device);
    device_add(&w83977tf_device);
    device_add(&sst_flash_29ee020_device);
    spd_register(SPD_TYPE_SDRAM, 0x7, 256);

    hwm_values_t machine_hwm = {
    	{    /* fan speeds */
    		3000,	/* System */
    		3000	/* CPU */
    	}, { /* temperatures */
    		30	/* CPU */
    	}, { /* voltages */
    		2050,   			  /* VCORE (2.05V by default) */
    		RESISTOR_DIVIDER(12000, 150, 47), /* +12V (15K/4.7K divider suggested in the GL518SM datasheet) */
    		3300				  /* +3.3V */
    	}
    };
    if (model->cpu[cpu_manufacturer].cpus[cpu_effective].cpu_type == CPU_PENTIUM2)
    	machine_hwm.voltages[0] = 2800; /* set higher VCORE (2.8V) for Klamath */
    hwm_set_values(machine_hwm);
    device_add(&gl518sm_2d_device);

    return ret;
}


int
machine_at_atc6310bxii_init(const machine_t *model)
{
    int ret;

    ret = bios_load_linear(L"roms/machines/atc6310bxii/6310s102.bin",
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
    spd_register(SPD_TYPE_SDRAM, 0x7, 256);

    return ret;	
}


int
machine_at_p6sba_init(const machine_t *model)
{
    int ret;

    ret = bios_load_linear(L"roms/machines/p6sba/SBAB21.ROM",
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
    pci_register_slot(0x0E, PCI_CARD_NORMAL,      3, 4, 0, 0);
    pci_register_slot(0x0D, PCI_CARD_NORMAL,      3, 0, 0, 0);
    pci_register_slot(0x01, PCI_CARD_SPECIAL,     1, 2, 0, 0);
    device_add(&i440bx_device);
    device_add(&piix4e_device);
    device_add(&w83977tf_device);
    device_add(&keyboard_ps2_ami_pci_device);
    device_add(&intel_flash_bxt_device);
    spd_register(SPD_TYPE_SDRAM, 0x7, 256);

    hwm_values_t machine_hwm = {
    	{    /* fan speeds */
    		3000,	/* CPU1 */
    		0,	/* CPU2 */
    		3000	/* Thermal Control */
    	}, { /* temperatures */
    		0,	/* unused */
    		30,	/* CPU1 */
    		0	/* unused (CPU2?) */
    	}, { /* voltages */
    		2050,				   /* CPU1 (2.05V by default) */
    		0,				   /* CPU2 */
    		3300,				   /* +3.3V */
    		RESISTOR_DIVIDER(5000,   11,  16), /* +5V  (divider values bruteforced) */
    		RESISTOR_DIVIDER(12000,  28,  10), /* +12V (28K/10K divider suggested in the W83781D datasheet) */
    		RESISTOR_DIVIDER(12000, 853, 347), /* -12V (divider values bruteforced) */
    		RESISTOR_DIVIDER(5000,    1,   2)  /* -5V  (divider values bruteforced) */
    	}
    };
    if (model->cpu[cpu_manufacturer].cpus[cpu_effective].cpu_type == CPU_PENTIUM2)
    	machine_hwm.voltages[0] = 2800; /* set higher VCORE (2.8V) for Klamath */
    hwm_set_values(machine_hwm);
    device_add(&w83781d_device);

    return ret;
}


int
machine_at_tsunamiatx_init(const machine_t *model)
{
    int ret;

    ret = bios_load_linear(L"roms/machines/tsunamiatx/bx46200f.rom",
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
    pci_register_slot(0x01, PCI_CARD_SPECIAL,     1, 2, 3, 4);
    device_add(&i440bx_device);
    device_add(&piix4e_device);

    if (sound_card_current == SOUND_INTERNAL)
    	device_add(&es1371_onboard_device);

    device_add(&pc87309_device);
    device_add(&keyboard_ps2_ami_pci_device);
    device_add(&intel_flash_bxt_device);
    spd_register(SPD_TYPE_SDRAM, 0x7, 256);

    return ret;
}


const device_t *
at_tsunamiatx_get_device(void)
{
    return &es1371_onboard_device;
}
