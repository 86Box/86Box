/*
 * 86Box	A hypervisor and IBM PC system emulator that specializes in
 *		running old operating systems and software designed for IBM
 *		PC systems and compatibles from 1981 through fairly recent
 *		system designs based on the PCI bus.
 *
 *		This file is part of the 86Box distribution.
 *
 *		Implementation of Socket 7 and Super Socket 7 machines.
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
#include <86box/intel_flash.h>
#include <86box/intel_sio.h>
#include <86box/piix.h>
#include <86box/sio.h>
#include <86box/sst_flash.h>
#include <86box/via_vt82c586b.h>
#include <86box/hwm.h>
#include <86box/video.h>
#include "cpu.h"
#include <86box/machine.h>


static void
machine_at_thor_common_init(const machine_t *model, int mr)
{
    machine_at_common_init_ex(model, mr);

    pci_init(PCI_CONFIG_TYPE_1);
    pci_register_slot(0x00, PCI_CARD_NORTHBRIDGE, 0, 0, 0, 0);
    pci_register_slot(0x08, PCI_CARD_ONBOARD, 4, 0, 0, 0);
    pci_register_slot(0x0D, PCI_CARD_NORMAL, 1, 2, 3, 4);
    pci_register_slot(0x0E, PCI_CARD_NORMAL, 2, 3, 4, 1);
    pci_register_slot(0x0F, PCI_CARD_NORMAL, 3, 4, 2, 1);
    pci_register_slot(0x10, PCI_CARD_NORMAL, 4, 3, 2, 1);
    pci_register_slot(0x07, PCI_CARD_SOUTHBRIDGE, 0, 0, 0, 0);
    device_add(&i430fx_device);
    device_add(&piix_device);
    device_add(&keyboard_ps2_ami_pci_device);
    device_add(&pc87306_device);
    device_add(&intel_flash_bxt_ami_device);
}


int
machine_at_thor_init(const machine_t *model)
{
    int ret;

    ret = bios_load_linear_combined(L"roms/machines/thor/1006cn0_.bio",
				    L"roms/machines/thor/1006cn0_.bi1", 0x20000, 128);

    if (bios_only || !ret)
	return ret;

    machine_at_thor_common_init(model, 0);

    return ret;
}


#if defined(DEV_BRANCH) && defined(USE_MRTHOR)
int
machine_at_mrthor_init(const machine_t *model)
{
    int ret;

    ret = bios_load_linear(L"roms/machines/mrthor/mr_atx.bio",
			   0x000e0000, 131072, 0);

    if (bios_only || !ret)
	return ret;

    machine_at_thor_common_init(model, 1);

    return ret;
}
#endif


int
machine_at_pb640_init(const machine_t *model)
{
    int ret;

    ret = bios_load_linear_combined(L"roms/machines/pb640/1007CP0R.BIO",
				    L"roms/machines/pb640/1007CP0R.BI1", 0x1d000, 128);

    if (bios_only || !ret)
	return ret;

    machine_at_common_init(model);

    pci_init(PCI_CONFIG_TYPE_1);
    pci_register_slot(0x00, PCI_CARD_NORTHBRIDGE, 0, 0, 0, 0);
    pci_register_slot(0x08, PCI_CARD_ONBOARD, 4, 0, 0, 0);
    pci_register_slot(0x11, PCI_CARD_NORMAL, 1, 2, 3, 4);
    pci_register_slot(0x13, PCI_CARD_NORMAL, 2, 1, 3, 4);
    pci_register_slot(0x0B, PCI_CARD_NORMAL, 3, 2, 1, 4);
    pci_register_slot(0x07, PCI_CARD_SOUTHBRIDGE, 0, 0, 0, 0);
    device_add(&i430fx_pb640_device);
    device_add(&piix_device);
    ide_board_set_force_ata3(0, 1);
    ide_board_set_force_ata3(1, 1);

    if (gfxcard == VID_INTERNAL)
	device_add(&gd5440_onboard_pci_device);

    device_add(&keyboard_ps2_ami_pci_device);
    device_add(&pc87306_device);
    device_add(&intel_flash_bxt_ami_device);

    return ret;
}


const device_t *
at_pb640_get_device(void)
{
    return &gd5440_onboard_pci_device;
}


int
machine_at_acerm3a_init(const machine_t *model)
{
    int ret;

    ret = bios_load_linear(L"roms/machines/acerm3a/r01-b3.bin",
			   0x000e0000, 131072, 0);

    if (bios_only || !ret)
	return ret;

    machine_at_common_init(model);

    pci_init(PCI_CONFIG_TYPE_1);
    pci_register_slot(0x00, PCI_CARD_NORTHBRIDGE, 0, 0, 0, 0);
    pci_register_slot(0x07, PCI_CARD_SOUTHBRIDGE, 0, 0, 0, 0);
    pci_register_slot(0x0C, PCI_CARD_NORMAL, 1, 2, 3, 4);
    pci_register_slot(0x0D, PCI_CARD_NORMAL, 2, 3, 4, 1);
    pci_register_slot(0x0E, PCI_CARD_NORMAL, 3, 4, 1, 2);
    pci_register_slot(0x0F, PCI_CARD_NORMAL, 4, 1, 2, 3);
    pci_register_slot(0x10, PCI_CARD_ONBOARD, 4, 0, 0, 0);
    device_add(&i430hx_device);
    device_add(&piix3_device);
    device_add(&keyboard_ps2_pci_device);
    device_add(&fdc37c932fr_device);
    device_add(&acerm3a_device);

    device_add(&intel_flash_bxb_device);

    return ret;
}


int
machine_at_acerv35n_init(const machine_t *model)
{
    int ret;

    ret = bios_load_linear(L"roms/machines/acerv35n/v35nd1s1.bin",
			   0x000e0000, 131072, 0);

    if (bios_only || !ret)
	return ret;

    machine_at_common_init(model);

    pci_init(PCI_CONFIG_TYPE_1);
    pci_register_slot(0x00, PCI_CARD_NORTHBRIDGE, 0, 0, 0, 0);
    pci_register_slot(0x07, PCI_CARD_SOUTHBRIDGE, 0, 0, 0, 0);
    pci_register_slot(0x11, PCI_CARD_NORMAL, 1, 2, 3, 4);
    pci_register_slot(0x12, PCI_CARD_NORMAL, 2, 3, 4, 1);
    pci_register_slot(0x13, PCI_CARD_NORMAL, 3, 4, 1, 2);
    pci_register_slot(0x14, PCI_CARD_NORMAL, 4, 1, 2, 3);
    pci_register_slot(0x0D, PCI_CARD_NORMAL, 1, 2, 3, 4);
    device_add(&i430hx_device);
    device_add(&piix3_device);
    device_add(&keyboard_ps2_pci_device);
    device_add(&fdc37c932fr_device);
    device_add(&acerm3a_device);

    device_add(&intel_flash_bxb_device);

    return ret;
}


int
machine_at_ap53_init(const machine_t *model)
{
    int ret;

    ret = bios_load_linear(L"roms/machines/ap53/ap53r2c0.rom",
			   0x000e0000, 131072, 0);

    if (bios_only || !ret)
	return ret;

    machine_at_common_init(model);

    pci_init(PCI_CONFIG_TYPE_1);
    pci_register_slot(0x00, PCI_CARD_NORTHBRIDGE, 0, 0, 0, 0);
    pci_register_slot(0x11, PCI_CARD_NORMAL, 1, 2, 3, 4);
    pci_register_slot(0x12, PCI_CARD_NORMAL, 2, 3, 4, 1);
    pci_register_slot(0x13, PCI_CARD_NORMAL, 3, 4, 1, 2);
    pci_register_slot(0x14, PCI_CARD_NORMAL, 4, 1, 2, 3);
    pci_register_slot(0x07, PCI_CARD_SOUTHBRIDGE, 0, 0, 0, 0);
    pci_register_slot(0x06, PCI_CARD_ONBOARD, 1, 2, 3, 4);
    device_add(&i430hx_device);
    device_add(&piix3_device);
    device_add(&keyboard_ps2_ami_pci_device);
    device_add(&fdc37c669_device);
    device_add(&intel_flash_bxt_device);

    return ret;
}


int
machine_at_p55t2p4_init(const machine_t *model)
{
    int ret;

    ret = bios_load_linear(L"roms/machines/p55t2p4/0207_j2.bin",
			   0x000e0000, 131072, 0);

    if (bios_only || !ret)
	return ret;

    machine_at_common_init(model);

    pci_init(PCI_CONFIG_TYPE_1);
    pci_register_slot(0x00, PCI_CARD_NORTHBRIDGE, 0, 0, 0, 0);
    pci_register_slot(0x0C, PCI_CARD_NORMAL, 1, 2, 3, 4);
    pci_register_slot(0x0B, PCI_CARD_NORMAL, 2, 3, 4, 1);
    pci_register_slot(0x0A, PCI_CARD_NORMAL, 3, 4, 1, 2);
    pci_register_slot(0x09, PCI_CARD_NORMAL, 4, 1, 2, 3);
    pci_register_slot(0x07, PCI_CARD_SOUTHBRIDGE, 0, 0, 0, 0);
    device_add(&i430hx_device);
    device_add(&piix3_device);
    device_add(&keyboard_ps2_pci_device);
    device_add(&w83877f_device);
    device_add(&intel_flash_bxt_device);

    return ret;
}


int
machine_at_p55t2s_init(const machine_t *model)
{
    int ret;

    ret = bios_load_linear(L"roms/machines/p55t2s/s6y08t.rom",
			   0x000e0000, 131072, 0);

    if (bios_only || !ret)
	return ret;

    machine_at_common_init(model);

    pci_init(PCI_CONFIG_TYPE_1);
    pci_register_slot(0x00, PCI_CARD_NORTHBRIDGE, 0, 0, 0, 0);
    pci_register_slot(0x12, PCI_CARD_NORMAL, 1, 2, 3, 4);
    pci_register_slot(0x13, PCI_CARD_NORMAL, 4, 1, 2, 3);
    pci_register_slot(0x14, PCI_CARD_NORMAL, 3, 4, 1, 2);
    pci_register_slot(0x11, PCI_CARD_NORMAL, 2, 3, 4, 1);
    pci_register_slot(0x07, PCI_CARD_SOUTHBRIDGE, 0, 0, 0, 0);
    device_add(&i430hx_device);
    device_add(&piix3_device);
    device_add(&keyboard_ps2_ami_pci_device);
    device_add(&pc87306_device);
    device_add(&intel_flash_bxt_device);

    return ret;
}


int
machine_at_tc430hx_init(const machine_t *model)
{
    int ret;

    ret = bios_load_linear_combined2(L"roms/machines/tc430hx/1007dh0_.bio",
				     L"roms/machines/tc430hx/1007dh0_.bi1",
				     L"roms/machines/tc430hx/1007dh0_.bi2",
				     L"roms/machines/tc430hx/1007dh0_.bi3",
				     L"roms/machines/tc430hx/1007dh0_.rcv",
				     0x3a000, 128);

    if (bios_only || !ret)
	return ret;

    machine_at_common_init(model);

    pci_init(PCI_CONFIG_TYPE_1);
    pci_register_slot(0x00, PCI_CARD_NORTHBRIDGE, 0, 0, 0, 0);
    pci_register_slot(0x08, PCI_CARD_ONBOARD, 4, 0, 0, 0);
    pci_register_slot(0x0D, PCI_CARD_NORMAL, 1, 2, 3, 4);
    pci_register_slot(0x0E, PCI_CARD_NORMAL, 2, 3, 4, 1);
    pci_register_slot(0x0F, PCI_CARD_NORMAL, 3, 4, 1, 2);
    pci_register_slot(0x10, PCI_CARD_NORMAL, 4, 1, 2, 3);
    pci_register_slot(0x07, PCI_CARD_SOUTHBRIDGE, 0, 0, 0, 0);
    device_add(&i430hx_device);
    device_add(&piix3_device);
    device_add(&keyboard_ps2_ami_pci_device);
    device_add(&pc87306_device);
    device_add(&intel_flash_bxt_ami_device);

    return ret;
}


int
machine_at_equium5200_init(const machine_t *model) // Information about that machine on machine.h
{
    int ret;

    ret = bios_load_linear_combined2(L"roms/machines/equium5200/1003DK08.BIO",
				     L"roms/machines/equium5200/1003DK08.BI1",
				     L"roms/machines/equium5200/1003DK08.BI2",
				     L"roms/machines/equium5200/1003DK08.BI3",
				     L"roms/machines/equium5200/1003DK08.RCV",
				     0x3a000, 128);

    if (bios_only || !ret)
	return ret;

    machine_at_common_init(model);

    pci_init(PCI_CONFIG_TYPE_1);
    pci_register_slot(0x00, PCI_CARD_NORTHBRIDGE, 0, 0, 0, 0);
    pci_register_slot(0x08, PCI_CARD_ONBOARD, 4, 0, 0, 0);
    pci_register_slot(0x11, PCI_CARD_NORMAL, 1, 2, 3, 4);
    pci_register_slot(0x13, PCI_CARD_NORMAL, 2, 3, 4, 1);
    pci_register_slot(0x0B, PCI_CARD_NORMAL, 3, 4, 1, 2);
    pci_register_slot(0x07, PCI_CARD_SOUTHBRIDGE, 0, 0, 0, 0);
    pci_register_slot(0x0A, PCI_CARD_NORMAL, 3, 0, 0, 0); // riser
    device_add(&i430hx_device);
    device_add(&piix3_device);
    device_add(&keyboard_ps2_ami_pci_device);
    device_add(&pc87306_device);
    device_add(&intel_flash_bxt_ami_device);

    return ret;
}


int
machine_at_p55tvp4_init(const machine_t *model)
{
    int ret;

    ret = bios_load_linear(L"roms/machines/p55tvp4/tv5i0204.awd",
			   0x000e0000, 131072, 0);

    if (bios_only || !ret)
	return ret;

    machine_at_common_init(model);

    pci_init(PCI_CONFIG_TYPE_1);
    pci_register_slot(0x00, PCI_CARD_NORTHBRIDGE, 0, 0, 0, 0);
    pci_register_slot(0x0C, PCI_CARD_NORMAL, 1, 2, 3, 4);
    pci_register_slot(0x0B, PCI_CARD_NORMAL, 2, 3, 4, 1);
    pci_register_slot(0x0A, PCI_CARD_NORMAL, 3, 4, 1, 2);
    pci_register_slot(0x09, PCI_CARD_NORMAL, 4, 1, 2, 3);
    pci_register_slot(0x07, PCI_CARD_SOUTHBRIDGE, 0, 0, 0, 0);
    device_add(&i430vx_device);
    device_add(&piix3_device);
    device_add(&keyboard_ps2_pci_device);
    device_add(&w83877f_device);
    device_add(&intel_flash_bxt_device);

    return ret;
}


int
machine_at_i430vx_init(const machine_t *model)
{
    int ret;

    ret = bios_load_linear(L"roms/machines/430vx/55xwuq0e.bin",
			   0x000e0000, 131072, 0);

    if (bios_only || !ret)
	return ret;

    machine_at_common_init(model);

    pci_init(PCI_CONFIG_TYPE_1);
    pci_register_slot(0x00, PCI_CARD_NORTHBRIDGE, 0, 0, 0, 0);
    pci_register_slot(0x11, PCI_CARD_NORMAL, 2, 3, 4, 1);
    pci_register_slot(0x12, PCI_CARD_NORMAL, 1, 2, 3, 4);
    pci_register_slot(0x14, PCI_CARD_NORMAL, 3, 4, 1, 2);
    pci_register_slot(0x13, PCI_CARD_NORMAL, 4, 1, 2, 3);
    pci_register_slot(0x07, PCI_CARD_SOUTHBRIDGE, 0, 0, 0, 0);
    device_add(&i430vx_device);
    device_add(&piix3_device);
    device_add(&keyboard_ps2_pci_device);
    device_add(&um8669f_device);
    device_add(&intel_flash_bxt_device);

    return ret;
}


int
machine_at_p55va_init(const machine_t *model)
{
    int ret;

    ret = bios_load_linear(L"roms/machines/p55va/va021297.bin",
			   0x000e0000, 131072, 0);

    if (bios_only || !ret)
	return ret;

    machine_at_common_init(model);

    pci_init(PCI_CONFIG_TYPE_1);
    pci_register_slot(0x00, PCI_CARD_NORTHBRIDGE, 0, 0, 0, 0);
    pci_register_slot(0x08, PCI_CARD_NORMAL, 1, 2, 3, 4);
    pci_register_slot(0x09, PCI_CARD_NORMAL, 2, 3, 4, 1);
    pci_register_slot(0x0A, PCI_CARD_NORMAL, 3, 4, 1, 2);
    pci_register_slot(0x0B, PCI_CARD_NORMAL, 4, 1, 2, 3);
    pci_register_slot(0x07, PCI_CARD_SOUTHBRIDGE, 0, 0, 0, 0);
    device_add(&i430vx_device);
    device_add(&piix3_device);
    device_add(&keyboard_ps2_pci_device);
    device_add(&fdc37c932fr_device);
    device_add(&intel_flash_bxt_device);

    return ret;
}


int
machine_at_j656vxd_init(const machine_t *model)
{
    int ret;

    ret = bios_load_linear(L"roms/machines/j656vxd/J656VXD.BIN",
			   0x000e0000, 131072, 0);

    if (bios_only || !ret)
	return ret;

    machine_at_common_init(model);

    pci_init(PCI_CONFIG_TYPE_1);
    pci_register_slot(0x00, PCI_CARD_NORTHBRIDGE, 0, 0, 0, 0);
    pci_register_slot(0x11, PCI_CARD_NORMAL, 1, 2, 3, 4);
    pci_register_slot(0x12, PCI_CARD_NORMAL, 2, 3, 4, 1);
    pci_register_slot(0x13, PCI_CARD_NORMAL, 3, 4, 1, 2);
    pci_register_slot(0x07, PCI_CARD_SOUTHBRIDGE, 0, 0, 0, 0);
    device_add(&i430vx_device);
    device_add(&piix3_device);
    device_add(&keyboard_ps2_pci_device);
    device_add(&fdc37c669_device);
    device_add(&intel_flash_bxt_device);

    return ret;
}
int
machine_at_mb520n_init(const machine_t *model)
{
    int ret;

    ret = bios_load_linear(L"roms/machines/mb520n/520n503s.rom",
			   0x000e0000, 131072, 0);

    if (bios_only || !ret)
	return ret;

    machine_at_common_init(model);

    pci_init(PCI_CONFIG_TYPE_1);
    pci_register_slot(0x11, PCI_CARD_NORMAL, 4, 1, 2, 3);
    pci_register_slot(0x12, PCI_CARD_NORMAL, 3, 4, 1, 2);
    pci_register_slot(0x13, PCI_CARD_NORMAL, 2, 3, 4, 1);
    pci_register_slot(0x14, PCI_CARD_NORMAL, 1, 2, 3, 4);
    pci_register_slot(0x07, PCI_CARD_SOUTHBRIDGE, 0, 0, 0, 0);
    pci_register_slot(0x00, PCI_CARD_NORTHBRIDGE, 0, 0, 0, 0);
    device_add(&i430vx_device);
    device_add(&piix3_device);
    device_add(&keyboard_ps2_ami_pci_device);
    device_add(&fdc37c669_device);
    device_add(&intel_flash_bxt_device);

    return ret;
}


int
machine_at_p55xb2_init(const machine_t *model)
{
    int ret;

    ret = bios_load_linear(L"roms/machines/p55xb2/XB20721.BIN",
			   0x000e0000, 131072, 0);

    if (bios_only || !ret)
	return ret;

    machine_at_common_init_ex(model, 2);

    pci_init(PCI_CONFIG_TYPE_1);
    pci_register_slot(0x00, PCI_CARD_NORTHBRIDGE, 0, 0, 0, 0);
    pci_register_slot(0x09, PCI_CARD_NORMAL, 1, 2, 3, 4);
    pci_register_slot(0x0A, PCI_CARD_NORMAL, 2, 3, 4, 1);
    pci_register_slot(0x0B, PCI_CARD_NORMAL, 3, 4, 1, 2);
    pci_register_slot(0x0C, PCI_CARD_NORMAL, 4, 1, 2, 3);
    pci_register_slot(0x07, PCI_CARD_SOUTHBRIDGE, 0, 0, 0, 4);	/* PIIX4 */
    device_add(&i430tx_device);
    device_add(&piix4_device);
    device_add(&keyboard_ps2_pci_device);
//  device_add(&ali_m513x_device);
    device_add(&intel_flash_bxt_device);

    return ret;
}


int
machine_at_tx97_init(const machine_t *model)
{
    int ret;

    ret = bios_load_linear(L"roms/machines/tx97/0112.001",
			   0x000e0000, 131072, 0);

    if (bios_only || !ret)
	return ret;

    machine_at_common_init_ex(model, 2);

    pci_init(PCI_CONFIG_TYPE_1);
    pci_register_slot(0x00, PCI_CARD_NORTHBRIDGE, 0, 0, 0, 0);
    pci_register_slot(0x0C, PCI_CARD_NORMAL, 1, 2, 3, 4);
    pci_register_slot(0x0B, PCI_CARD_NORMAL, 2, 3, 4, 1);
    pci_register_slot(0x0A, PCI_CARD_NORMAL, 3, 4, 1, 2);
    pci_register_slot(0x09, PCI_CARD_NORMAL, 4, 1, 2, 3);
    pci_register_slot(0x01, PCI_CARD_SOUTHBRIDGE, 1, 2, 3, 4);	/* PIIX4 */
    pci_register_slot(0x0D, PCI_CARD_NORMAL, 1, 2, 3, 4);
    pci_register_slot(0x08, PCI_CARD_NORMAL, 1, 2, 3, 4);
    device_add(&i430tx_device);
    device_add(&piix4_device);
    device_add(&keyboard_ps2_pci_device);
    device_add(&w83977tf_device);
    device_add(&intel_flash_bxt_device);

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
    	}, { /* voltages */
    		3300,				   /* VCORE (3.3V by default) */
    		0,				   /* unused */
    		3300,				   /* +3.3V */
    		RESISTOR_DIVIDER(5000,   11,  16), /* +5V  (divider values bruteforced) */
    		RESISTOR_DIVIDER(12000,  28,  10), /* +12V (28K/10K divider suggested in the W83781D datasheet) */
    		RESISTOR_DIVIDER(12000, 853, 347), /* -12V (divider values bruteforced) */
    		RESISTOR_DIVIDER(5000,    1,   2), /* -5V  (divider values bruteforced) */
    		0
    	}
    };
    /* Pentium, Pentium OverDrive MMX, Pentium Mobile MMX: 3.3V (real Pentium Mobile MMX is 2.45V).
       Pentium MMX: 2.8 V.
       AMD K6 Model 6: 2.9 V for 166/200, 3.2 V for 233.
       AMD K6 Model 7: 2.2 V. */
    if (model->cpu[cpu_manufacturer].cpus[cpu_effective].cpu_type == CPU_PENTIUMMMX)
	machine_hwm.voltages[0] = 2800; /* set higher VCORE (2.8V) for Pentium MMX */
    else if (model->cpu[cpu_manufacturer].cpus[cpu_effective].cpu_type == CPU_K6)
	machine_hwm.voltages[0] = 2200; /* set higher VCORE (2.8V) for Pentium MMX */
    else if (model->cpu[cpu_manufacturer].cpus[cpu_effective].cpu_type == CPU_K6_2)
	machine_hwm.voltages[0] = 2200; /* set higher VCORE (2.8V) for Pentium MMX */
    hwm_set_values(machine_hwm);
    device_add(&w83781d_device);

    return ret;
}


int
machine_at_ym430tx_init(const machine_t *model)
{
    int ret;

    ret = bios_load_linear(L"roms/machines/ym430tx/YM430TX.003",
			   0x000e0000, 131072, 0);

    if (bios_only || !ret)
	return ret;

    machine_at_common_init_ex(model, 2);

    pci_init(PCI_CONFIG_TYPE_1);
    pci_register_slot(0x00, PCI_CARD_NORTHBRIDGE, 0, 0, 0, 0);
    pci_register_slot(0x0C, PCI_CARD_NORMAL, 1, 2, 3, 4);
    pci_register_slot(0x0B, PCI_CARD_NORMAL, 2, 3, 4, 1);
    pci_register_slot(0x0A, PCI_CARD_NORMAL, 3, 4, 1, 2);
    pci_register_slot(0x09, PCI_CARD_NORMAL, 4, 1, 2, 3);
    pci_register_slot(0x01, PCI_CARD_SOUTHBRIDGE, 1, 2, 3, 4);	/* PIIX4 */
    pci_register_slot(0x0D, PCI_CARD_NORMAL, 1, 2, 3, 4);
    pci_register_slot(0x08, PCI_CARD_NORMAL, 1, 2, 3, 4);
    device_add(&i430tx_device);
    device_add(&piix4_device);
    device_add(&keyboard_ps2_pci_device);
    device_add(&w83977tf_device);
    device_add(&intel_flash_bxt_device);

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
    	}, { /* voltages */
    		2050,				   /* VCORE (2.05V by default) */
    		0,				   /* unused */
    		3300,				   /* +3.3V */
    		RESISTOR_DIVIDER(5000,   11,  16), /* +5V  (divider values bruteforced) */
    		RESISTOR_DIVIDER(12000,  28,  10), /* +12V (28K/10K divider suggested in the W83781D datasheet) */
    		RESISTOR_DIVIDER(12000, 853, 347), /* -12V (divider values bruteforced) */
    		RESISTOR_DIVIDER(5000,    1,   2), /* -5V  (divider values bruteforced) */
    		0
    	}
    };
    hwm_set_values(machine_hwm);
    device_add(&w83781d_device);

    return ret;
}


int
machine_at_586t2_init(const machine_t *model)
{
    int ret;

    ret = bios_load_linear(L"roms/machines/586t2/5itw001.bin",
			   0x000e0000, 131072, 0);

    if (bios_only || !ret)
	return ret;

    machine_at_common_init_ex(model, 2);

    pci_init(PCI_CONFIG_TYPE_1);
    pci_register_slot(0x00, PCI_CARD_NORTHBRIDGE, 0, 0, 0, 0);
    pci_register_slot(0x09, PCI_CARD_NORMAL, 1, 2, 3, 4);
    pci_register_slot(0x0A, PCI_CARD_NORMAL, 2, 3, 4, 1);
    pci_register_slot(0x0B, PCI_CARD_NORMAL, 3, 4, 1, 2);
    pci_register_slot(0x0C, PCI_CARD_NORMAL, 4, 1, 2, 3);
    pci_register_slot(0x07, PCI_CARD_SOUTHBRIDGE, 1, 2, 3, 4);	/* PIIX4 */
    device_add(&i430tx_device);
    device_add(&piix4_device);
    device_add(&keyboard_ps2_pci_device);
    device_add(&um8669f_device); /*Placeholder for ITE 8679*/
    device_add(&sst_flash_29ee010_device);

    return ret;
}


int
machine_at_807ds_init(const machine_t *model)
{
    int ret;

    ret = bios_load_linear(L"roms/machines/807ds/Tx0212g.rom",
			   0x000e0000, 131072, 0);

    if (bios_only || !ret)
	return ret;

    machine_at_common_init_ex(model, 2);

    pci_init(PCI_CONFIG_TYPE_1);
    pci_register_slot(0x00, PCI_CARD_NORTHBRIDGE, 0, 0, 0, 0);
    pci_register_slot(0x07, PCI_CARD_SOUTHBRIDGE, 1, 2, 3, 4);	/* PIIX4 */
    pci_register_slot(0x14, PCI_CARD_NORMAL, 1, 2, 3, 4);
    pci_register_slot(0x13, PCI_CARD_NORMAL, 2, 3, 4, 1);
    pci_register_slot(0x12, PCI_CARD_NORMAL, 3, 4, 1, 2);
    pci_register_slot(0x11, PCI_CARD_NORMAL, 4, 1, 2, 3);
    device_add(&i430tx_device);
    device_add(&piix4_device);
    device_add(&keyboard_ps2_ami_pci_device);
    device_add(&um8669f_device); /*Placeholder for ITE 8679*/
    device_add(&intel_flash_bxt_device);

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
    	}, { /* voltages */
    		3300,				   /* VCORE (3.3V by default) */
    		0,				   /* unused */
    		3300,				   /* +3.3V */
    		RESISTOR_DIVIDER(5000,   11,  16), /* +5V  (divider values bruteforced) */
    		RESISTOR_DIVIDER(12000,  28,  10), /* +12V (28K/10K divider suggested in the W83781D datasheet) */
    		RESISTOR_DIVIDER(12000, 853, 347), /* -12V (divider values bruteforced) */
    		RESISTOR_DIVIDER(5000,    1,   2), /* -5V  (divider values bruteforced) */
    		0
    	}
    };
    /* Pentium, Pentium OverDrive MMX, Pentium Mobile MMX: 3.3V (real Pentium Mobile MMX is 2.45V).
       Pentium MMX: 2.8 V.
       AMD K6 Model 6: 2.9 V for 166/200, 3.2 V for 233.
       AMD K6 Model 7: 2.2 V. */
    if (model->cpu[cpu_manufacturer].cpus[cpu_effective].cpu_type == CPU_PENTIUMMMX)
	machine_hwm.voltages[0] = 2800; /* set higher VCORE (2.8V) for Pentium MMX */
    else if (model->cpu[cpu_manufacturer].cpus[cpu_effective].cpu_type == CPU_K6)
	machine_hwm.voltages[0] = 2200; /* set higher VCORE (2.8V) for Pentium MMX */
    else if (model->cpu[cpu_manufacturer].cpus[cpu_effective].cpu_type == CPU_K6_2)
	machine_hwm.voltages[0] = 2200; /* set higher VCORE (2.8V) for Pentium MMX */
    hwm_set_values(machine_hwm);
    device_add(&w83781d_device);

    return ret;
}


int
machine_at_p5mms98_init(const machine_t *model)
{
    int ret;

    ret = bios_load_linear(L"roms/machines/p5mms98/s981182.rom",
			   0x000e0000, 131072, 0);

    if (bios_only || !ret)
	return ret;

    machine_at_common_init_ex(model, 2);

    pci_init(PCI_CONFIG_TYPE_1);
    pci_register_slot(0x00, PCI_CARD_NORTHBRIDGE, 0, 0, 0, 0);
    pci_register_slot(0x07, PCI_CARD_SOUTHBRIDGE, 1, 2, 3, 4);	/* PIIX4 */
    pci_register_slot(0x11, PCI_CARD_NORMAL, 1, 2, 3, 4);
    pci_register_slot(0x12, PCI_CARD_NORMAL, 2, 3, 4, 1);
    pci_register_slot(0x13, PCI_CARD_NORMAL, 3, 4, 1, 2);
    pci_register_slot(0x14, PCI_CARD_NORMAL, 4, 1, 2, 3);
    device_add(&i430tx_device);
    device_add(&piix4_device);
    device_add(&keyboard_ps2_ami_pci_device);
    device_add(&w83977tf_device);
    device_add(&intel_flash_bxt_device);

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
    	}, { /* voltages */
    		3300,				   /* VCORE (3.3V by default) */
    		0,				   /* unused */
    		3300,				   /* +3.3V */
    		RESISTOR_DIVIDER(5000,   11,  16), /* +5V  (divider values bruteforced) */
    		RESISTOR_DIVIDER(12000,  28,  10), /* +12V (28K/10K divider suggested in the W83781D datasheet) */
    		RESISTOR_DIVIDER(12000, 853, 347), /* -12V (divider values bruteforced) */
    		RESISTOR_DIVIDER(5000,    1,   2), /* -5V  (divider values bruteforced) */
    		0
    	}
    };
    /* Pentium, Pentium OverDrive MMX, Pentium Mobile MMX: 3.3V (real Pentium Mobile MMX is 2.45V).
       Pentium MMX: 2.8 V.
       AMD K6 Model 6: 2.9 V for 166/200, 3.2 V for 233.
       AMD K6 Model 7: 2.2 V. */
    if (model->cpu[cpu_manufacturer].cpus[cpu_effective].cpu_type == CPU_PENTIUMMMX)
	machine_hwm.voltages[0] = 2800; /* set higher VCORE (2.8V) for Pentium MMX */
    else if (model->cpu[cpu_manufacturer].cpus[cpu_effective].cpu_type == CPU_K6)
	machine_hwm.voltages[0] = 2200; /* set higher VCORE (2.8V) for Pentium MMX */
    else if (model->cpu[cpu_manufacturer].cpus[cpu_effective].cpu_type == CPU_K6_2)
	machine_hwm.voltages[0] = 2200; /* set higher VCORE (2.8V) for Pentium MMX */
    hwm_set_values(machine_hwm);
    device_add(&w83781d_device);

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
    pci_register_slot(0x08, PCI_CARD_NORMAL, 1, 2, 3, 4);
    pci_register_slot(0x09, PCI_CARD_NORMAL, 2, 3, 4, 1);
    pci_register_slot(0x0a, PCI_CARD_NORMAL, 3, 4, 1, 2);
    pci_register_slot(0x01, PCI_CARD_NORMAL, 1, 2, 3, 4);
    pci_register_slot(0x07, PCI_CARD_SOUTHBRIDGE, 1, 2, 3, 4);
    device_add(&via_mvp3_device);
    device_add(&via_vt82c586b_device);
    device_add(&keyboard_ps2_pci_device);
    device_add(&w83877tf_device);
    device_add(&sst_flash_39sf010_device);

    return ret;
}
