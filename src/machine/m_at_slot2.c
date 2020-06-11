/*
 * 86Box	A hypervisor and IBM PC system emulator that specializes in
 *		running old operating systems and software designed for IBM
 *		PC systems and compatibles from 1981 through fairly recent
 *		system designs based on the PCI bus.
 *
 *		This file is part of the 86Box distribution.
 *
 *		Implementation of Slot 2 machines.
 *
 *		Slot 2 is quite a rare type of Slot. Used mostly by Pentium II & III Xeons
 *		These boards were also capable to take Slot 1 CPU's using Slot 2 to 1 adapters.
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
#include <86box/intel_flash.h>
#include <86box/sst_flash.h>
#include <86box/intel_sio.h>
#include <86box/piix.h>
#include <86box/sio.h>
#include <86box/intel_sio.h>
#include <86box/hwm.h>
#include <86box/spd.h>
#include <86box/video.h>
#include "cpu.h"
#include <86box/machine.h>

int
machine_at_6gxu_init(const machine_t *model)
{
    int ret;

    ret = bios_load_linear(L"roms/machines/6gxu/6gxu.f1c",
			   0x000c0000, 262144, 0);

    if (bios_only || !ret)
	return ret;

    machine_at_common_init_ex(model, 2);

    pci_init(PCI_CONFIG_TYPE_1);
    pci_register_slot(0x00, PCI_CARD_NORTHBRIDGE, 0, 0, 0, 0);
    pci_register_slot(0x07, PCI_CARD_SOUTHBRIDGE, 1, 2, 3, 4);
    pci_register_slot(0x08, PCI_CARD_NORMAL, 1, 2, 3, 4);
    pci_register_slot(0x09, PCI_CARD_NORMAL, 2, 3, 4, 1);
    pci_register_slot(0x0A, PCI_CARD_NORMAL, 3, 4, 1, 2);
    pci_register_slot(0x0B, PCI_CARD_NORMAL, 4, 1, 2, 3);
    pci_register_slot(0x0C, PCI_CARD_NORMAL, 1, 2, 3, 4); /* On-Board SCSI. Not emulated at the moment */
	pci_register_slot(0x01, PCI_CARD_NORMAL, 1, 2, 3, 4);
	
    device_add(&i440gx_device);
    device_add(&piix4e_device);
    device_add(&keyboard_ps2_pci_device);
    device_add(&w83977ef_device);
    device_add(&sst_flash_39sf020_device);
    spd_register(SPD_TYPE_SDRAM, 0xF, 512);

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
