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
#include <86box/intel_flash.h>
#include <86box/via_vt82c586b.h>
#include <86box/intel_sio.h>
#include <86box/piix.h>
#include <86box/sio.h>
#include <86box/sst_flash.h>
#include <86box/hwm.h>
#include <86box/spd.h>
#include <86box/video.h>
#include "cpu.h"
#include <86box/machine.h>

int
machine_at_63a_init(const machine_t *model)
{
	
    /* 440ZX Board. 440ZX is basically an underpowered 440BX. There no
       difference between to chipsets other than the name. */
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
    device_add(&piix4e_device);
    device_add(&w83977tf_device);
    device_add(&keyboard_ps2_pci_device);
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
	pci_register_slot(0x0F, PCI_CARD_NORMAL,      1, 2, 3, 4);
	pci_register_slot(0x10, PCI_CARD_NORMAL,      2, 3, 4, 1);
	pci_register_slot(0x13, PCI_CARD_NORMAL,      3, 4, 1, 2);
	pci_register_slot(0x14, PCI_CARD_NORMAL,      4, 1, 2, 3);
    pci_register_slot(0x07, PCI_CARD_SOUTHBRIDGE, 1, 2, 3, 4);
    pci_register_slot(0x01, PCI_CARD_NORMAL, 	  1, 2, 3, 4);
    device_add(&via_apro_device);
    device_add(&via_vt82c586b_device);
    device_add(&fdc37c669_device);
    device_add(&keyboard_ps2_pci_device);
    device_add(&sst_flash_39sf020_device);
    spd_register(SPD_TYPE_SDRAM, 0x7, 256);

    return ret;
}

#if defined(DEV_BRANCH) && defined(USE_I686) && defined(USE_596B)
int
machine_at_bx98_init(const machine_t *model)
{
    int ret;

    ret = bios_load_linear(L"roms/machines/bx98/vc98103e.bin",
			   0x000c0000, 262144, 0);

    if (bios_only || !ret)
	return ret;

    machine_at_common_init_ex(model, 2);

    pci_init(PCI_CONFIG_TYPE_1);
    pci_register_slot(0x00, PCI_CARD_NORTHBRIDGE, 0, 0, 0, 0);
    pci_register_slot(0x07, PCI_CARD_SOUTHBRIDGE, 1, 2, 3, 4);
    pci_register_slot(0x01, PCI_CARD_NORMAL, 	  1, 2, 3, 4);
    device_add(&via_apro_device);
    device_add(&via_vt82c596b_device);
    device_add(&um8669f_device);
    device_add(&keyboard_ps2_ami_pci_device);
    device_add(&sst_flash_39sf020_device);
    spd_register(SPD_TYPE_SDRAM, 0x7, 256);

    return ret;
}
#endif