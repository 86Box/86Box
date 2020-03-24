/*
 * 86Box	A hypervisor and IBM PC system emulator that specializes in
 *		running old operating systems and software designed for IBM
 *		PC systems and compatibles from 1981 through fairly recent
 *		system designs based on the PCI bus.
 *
 *		This file is part of the 86Box distribution.
 *
 *		Implementation of Socket 8 and Slot 1 machines.
 *
 * Version:	@(#)m_at_socket8.c	1.0.0	2019/05/16
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
#include "86box.h"
#include "mem.h"
#include "86box_io.h"
#include "rom.h"
#include "pci.h"
#include "device.h"
#include "chipset.h"
#include "hdc.h"
#include "hdc_ide.h"
#include "keyboard.h"
#include "intel_flash.h"
#include "intel_sio.h"
#include "piix.h"
#include "sio.h"
#include "sst_flash.h"
#include "hwm.h"
#include "video.h"
#include "cpu.h"
#include "machine.h"


#if defined(DEV_BRANCH) && defined(USE_I686)


int
machine_at_i440fx_init(const machine_t *model)
{
    int ret;

    ret = bios_load_linear(L"roms/machines/440fx/ntmaw501.bin",
			   0x000e0000, 131072, 0);

    if (bios_only || !ret)
	return ret;

    machine_at_common_init(model);

    pci_init(PCI_CONFIG_TYPE_1);
    pci_register_slot(0x00, PCI_CARD_NORTHBRIDGE, 0, 0, 0, 0);
    pci_register_slot(0x0E, PCI_CARD_NORMAL, 1, 2, 3, 4);
    pci_register_slot(0x0D, PCI_CARD_NORMAL, 2, 3, 4, 1);
    pci_register_slot(0x0C, PCI_CARD_NORMAL, 3, 4, 1, 2);
    pci_register_slot(0x0B, PCI_CARD_NORMAL, 4, 1, 2, 3);
    pci_register_slot(0x0A, PCI_CARD_NORMAL, 1, 2, 3, 4);
    pci_register_slot(0x07, PCI_CARD_SOUTHBRIDGE, 0, 0, 0, 0);
    device_add(&i440fx_device);
    device_add(&piix3_device);
    device_add(&keyboard_ps2_pci_device);
    device_add(&fdc37c665_device);
    device_add(&intel_flash_bxt_device);

    return ret;
}


int
machine_at_s1668_init(const machine_t *model)
{
    int ret;

    ret = bios_load_linear(L"roms/machines/tpatx/s1668p.rom",
			   0x000e0000, 131072, 0);

    if (bios_only || !ret)
	return ret;

    machine_at_common_init(model);

    pci_init(PCI_CONFIG_TYPE_1);
    pci_register_slot(0x00, PCI_CARD_NORTHBRIDGE, 0, 0, 0, 0);
    pci_register_slot(0x07, PCI_CARD_SOUTHBRIDGE, 0, 0, 0, 0);
    pci_register_slot(0x0E, PCI_CARD_NORMAL, 1, 2, 3, 4);
    pci_register_slot(0x0D, PCI_CARD_NORMAL, 2, 3, 4, 1);
    pci_register_slot(0x0C, PCI_CARD_NORMAL, 3, 4, 1, 2);
    pci_register_slot(0x0B, PCI_CARD_NORMAL, 4, 1, 2, 3);
    pci_register_slot(0x0A, PCI_CARD_NORMAL, 1, 2, 3, 4);
    device_add(&i440fx_device);
    device_add(&piix3_device);
    device_add(&keyboard_ps2_ami_pci_device);
    device_add(&fdc37c665_device);
    device_add(&intel_flash_bxt_device);

    return ret;
}

#endif
int
machine_at_6abx3_init(const machine_t *model)
{
    int ret;

    ret = bios_load_linear(L"roms/machines/6abx3/6abx3h1.bin",
			   0x000c0000, 262144, 0);

    if (bios_only || !ret)
	return ret;

    machine_at_common_init_ex(model, 2);

    pci_init(PCI_CONFIG_TYPE_1);
    pci_register_slot(0x00, PCI_CARD_NORTHBRIDGE, 0, 0, 0, 0);
    pci_register_slot(0x08, PCI_CARD_NORMAL, 1, 2, 3, 4);
    pci_register_slot(0x09, PCI_CARD_NORMAL, 2, 3, 4, 1);
    pci_register_slot(0x0B, PCI_CARD_NORMAL, 4, 1, 2, 3);
    pci_register_slot(0x0A, PCI_CARD_NORMAL, 3, 4, 1, 2);
    pci_register_slot(0x07, PCI_CARD_SOUTHBRIDGE, 1, 2, 3, 4);
    pci_register_slot(0x01, PCI_CARD_NORMAL, 1, 2, 3, 4);
    device_add(&i440bx_device);
    device_add(&piix4e_device);
    device_add(&keyboard_ps2_pci_device);
    device_add(&w83877tf_device);
    // device_add(&w83977tf_device);
    // device_add(&intel_flash_bxt_device);
    // device_add(&sst_flash_29ee020_device);
    device_add(&intel_flash_bxt_device);

    return ret;
}
#if defined(DEV_BRANCH) && defined(USE_I686)
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
    pci_register_slot(0x06, PCI_CARD_NORMAL, 4, 1, 2, 3);
    pci_register_slot(0x07, PCI_CARD_NORMAL, 3, 4, 1, 2);
    pci_register_slot(0x0B, PCI_CARD_NORMAL, 2, 3, 4, 1);
    pci_register_slot(0x0C, PCI_CARD_NORMAL, 1, 2, 3, 4);
    pci_register_slot(0x09, PCI_CARD_NORMAL, 4, 1, 2, 3);
    pci_register_slot(0x0A, PCI_CARD_NORMAL, 3, 4, 1, 2);
    pci_register_slot(0x04, PCI_CARD_SOUTHBRIDGE, 1, 2, 3, 4);
    pci_register_slot(0x01, PCI_CARD_NORMAL, 1, 2, 3, 4);
    device_add(&i440bx_device);
    device_add(&piix4e_device);
    device_add(&keyboard_ps2_pci_device);
    device_add(&w83977ef_device);
    device_add(&sst_flash_39sf020_device);

    hwm_values_t machine_hwm = {
    	{    /* fan speeds */
    		3000,	/* Chassis */
    		3000,	/* CPU */
    		3000,	/* Power */
    		0
    	}, { /* temperatures */
    		30,	/* MB */
    		0,	/* unused */
    		27,	/* CPU */
    		0
    	}, { /* voltages (divisors other than 16 = unclear how that number was achieved) */
    		2800 / 16,		  /* VCORE (2.8V by default) */
    		0,			  /* unused */
    		3300 / 16,		  /* +3.3V */
    		5000 / 27,		  /* +5V */
    		VDIV(12000, 28, 10) / 16, /* +12V (with 28K/10K resistor divider suggested in the W83781D datasheet) */
    		12000 / 55,		  /* -12V */
    		5000 / 24,		  /* -5V */
    		0   
    	}
    };
    if (model->cpu[cpu_manufacturer].cpus[cpu_effective].cpu_type == CPU_PENTIUM2D)
    	machine_hwm.voltages[0] = 2050 / 16; /* set lower VCORE (2.05V) for Deschutes */
    hwm_set_values(machine_hwm);
    device_add(&as99127f_device);

    return ret;
}


int
machine_at_p6bxt_init(const machine_t *model)
{
    int ret;

    ret = bios_load_linear(L"roms/machines/p6bxt/bxt53s.BIN",
			   0x000c0000, 262144, 0);

    if (bios_only || !ret)
	return ret;

    machine_at_common_init_ex(model, 2);

    pci_init(PCI_CONFIG_TYPE_1);
    pci_register_slot(0x00, PCI_CARD_NORTHBRIDGE, 0, 0, 0, 0);
	pci_register_slot(0x09, PCI_CARD_NORMAL, 	  1, 2, 3, 4);
	pci_register_slot(0x0A, PCI_CARD_NORMAL, 	  2, 3, 4, 1);
	pci_register_slot(0x0B, PCI_CARD_NORMAL, 	  3, 4, 1, 2);
	pci_register_slot(0x0C, PCI_CARD_NORMAL, 	  4, 1, 2, 3);
	pci_register_slot(0x0D, PCI_CARD_NORMAL, 	  4, 3, 2, 1); // Slot 5: Probably the integrated sound chip
    pci_register_slot(0x07, PCI_CARD_SOUTHBRIDGE, 1, 2, 3, 4);
	pci_register_slot(0x01, PCI_CARD_NORMAL, 	  1, 2, 3, 4);
    device_add(&i440bx_device);
    device_add(&piix4_device);
	device_add(&w83977tf_device);
    device_add(&keyboard_ps2_pci_device);
    device_add(&intel_flash_bxt_device);

    return ret;
}


int
machine_at_63a_init(const machine_t *model)
{
	
// 440ZX Board. 440ZX is basically an underpowered 440BX. There no
// difference between to chipsets other than the name.
    int ret;

    ret = bios_load_linear(L"roms/machines/63a/63a-q3.bin",
			   0x000c0000, 262144, 0);

    if (bios_only || !ret)
	return ret;

    machine_at_common_init_ex(model, 2);

    pci_init(PCI_CONFIG_TYPE_1);
    pci_register_slot(0x00, PCI_CARD_NORTHBRIDGE, 0, 0, 0, 0);
	pci_register_slot(0x08, PCI_CARD_NORMAL, 	  1, 2, 3, 4);
	pci_register_slot(0x09, PCI_CARD_NORMAL, 	  2, 3, 4, 1);
	pci_register_slot(0x0A, PCI_CARD_NORMAL, 	  3, 4, 1, 2);
	pci_register_slot(0x0B, PCI_CARD_NORMAL, 	  4, 1, 2, 3);
	pci_register_slot(0x0C, PCI_CARD_NORMAL, 	  1, 2, 3, 4); // Integrated Sound?
    pci_register_slot(0x07, PCI_CARD_SOUTHBRIDGE, 1, 2, 3, 4);
	pci_register_slot(0x01, PCI_CARD_NORMAL, 	  1, 2, 3, 4);
    device_add(&i440zx_device);
    device_add(&piix4_device);
	device_add(&w83977tf_device);
    device_add(&keyboard_ps2_pci_device);
    device_add(&intel_flash_bxt_device);

    return ret;
}
#endif
