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


int
machine_at_ax59pro_init(const machine_t *model)
{
    int ret;

    ret = bios_load_linear(L"roms/machines/ax59pro/AX59P236.BIN",
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
    pci_register_slot(0x01, PCI_CARD_SPECIAL,     1, 2, 3, 4);

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

    ret = bios_load_linear(L"roms/machines/ficva503p/je4333.bin",
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
    pci_register_slot(0x01, PCI_CARD_SPECIAL,     1, 2, 3, 4);

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

    ret = bios_load_linear(L"roms/machines/ficva503a/jo4116.bin",
			   0x000c0000, 262144, 0);

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
    pci_register_slot(0x01, PCI_CARD_SPECIAL,     1, 2, 3, 4);

    device_add(&via_mvp3_device);
    device_add(&via_vt82c686a_device);
    device_add(&keyboard_ps2_pci_device);
    device_add(&via_vt82c686_sio_device);
    device_add(&sst_flash_39sf020_device);
    spd_register(SPD_TYPE_SDRAM, 0x7, 256);

    hwm_values_t machine_hwm = {
    	{    /* fan speeds */
    		3000,	/* CPUFAN1 */
    		3000	/* ChassisFAN */
    	}, { /* temperatures */
    		32,	/* CPU */
    		32,	/* System */
    		0	/* unused */
    	}, { /* voltages */
    		3300,				   /* Vcore (3.3V by default) */
    		2500,				   /* 2.5V (unused) */
    		3300,				   /* 3.3V */
    		RESISTOR_DIVIDER(5000,    9,  16), /* 5V  (divider values bruteforced) */
    		RESISTOR_DIVIDER(12000,  28,  10)  /* 12V (28K/10K divider applies to W83781D but is close enough) */
    	}
    };
    /* Pentium, Pentium OverDrive MMX, Pentium Mobile MMX: 3.3V (real Pentium Mobile MMX is 2.45V).
       Pentium MMX: 2.8 V.
       AMD K6 Model 6: 2.9 V for 166/200, 3.2 V for 233.
       AMD K6 Model 7: 2.2 V. */
    switch (model->cpu[cpu_manufacturer].cpus[cpu_effective].cpu_type) {
    	case CPU_WINCHIP:
    	case CPU_WINCHIP2:
#if (defined(USE_NEW_DYNAREC) || (defined(DEV_BRANCH) && defined(USE_CYRIX_6X86)))
    	case CPU_Cx6x86:
#endif
#if defined(DEV_BRANCH) && defined(USE_AMD_K5)
    	case CPU_K5:
    	case CPU_5K86:
#endif
    		machine_hwm.voltages[0] = 3500;
    		break;
#if (defined(USE_NEW_DYNAREC) || (defined(DEV_BRANCH) && defined(USE_CYRIX_6X86)))
    	case CPU_Cx6x86MX:
    		machine_hwm.voltages[0] = 2900;
    		break;
#endif
    	case CPU_PENTIUMMMX:
    		machine_hwm.voltages[0] = 2800;
    		break;
    	case CPU_K6:
    	case CPU_K6_2:
    		machine_hwm.voltages[0] = 2200;
    		break;
    }
    machine_hwm.voltages[0] *= 1.32; /* multiplier bruteforced */
    hwm_set_values(machine_hwm);
    device_add(&via_vt82c686_hwm_device);

    return ret;
}
