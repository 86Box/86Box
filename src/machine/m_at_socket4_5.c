/*
 * 86Box	A hypervisor and IBM PC system emulator that specializes in
 *		running old operating systems and software designed for IBM
 *		PC systems and compatibles from 1981 through fairly recent
 *		system designs based on the PCI bus.
 *
 *		This file is part of the 86Box distribution.
 *
 *		Implementation of Socket 4 and 5 machines.
 *
 * Version:	@(#)m_at_socket4_5.c	1.0.1	2019/10/20
 *
 * Authors:	Sarah Walker, <http://pcem-emulator.co.uk/>
 *		Miran Grca, <mgrca8@gmail.com>
 *
 *		Copyright 2010-2019 Sarah Walker.
 *		Copyright 2016-2019 Miran Grca.
 */
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>
#include "../86box.h"
#include "../mem.h"
#include "../io.h"
#include "../rom.h"
#include "../pci.h"
#include "../device.h"
#include "../chipset/chipset.h"
#include "../disk/hdc.h"
#include "../disk/hdc_ide.h"
#include "../timer.h"
#include "../floppy/fdd.h"
#include "../floppy/fdc.h"
#include "../keyboard.h"
#include "../intel_flash.h"
#include "../intel_sio.h"
#include "../piix.h"
#include "../sio.h"
#include "../video/video.h"
#include "../video/vid_cl54xx.h"
#include "../video/vid_s3.h"
#include "machine.h"


static void
machine_at_premiere_common_init(const machine_t *model)
{
    machine_at_common_init(model);
    device_add(&ide_pci_2ch_device);

    pci_init(PCI_CONFIG_TYPE_2);
    pci_register_slot(0x00, PCI_CARD_SPECIAL, 0, 0, 0, 0);
    pci_register_slot(0x01, PCI_CARD_SPECIAL, 0, 0, 0, 0);
    pci_register_slot(0x06, PCI_CARD_NORMAL, 3, 2, 1, 4);
    pci_register_slot(0x0E, PCI_CARD_NORMAL, 2, 1, 3, 4);
    pci_register_slot(0x0C, PCI_CARD_NORMAL, 1, 3, 2, 4);
    pci_register_slot(0x02, PCI_CARD_SPECIAL, 0, 0, 0, 0);
    device_add(&keyboard_ps2_ami_pci_device);
    device_add(&sio_zb_device);
    device_add(&fdc37c665_device);
    device_add(&intel_flash_bxt_ami_device);
}


static void
machine_at_award_common_init(const machine_t *model)
{
    machine_at_common_init(model);
    device_add(&ide_pci_2ch_device);

    pci_init(PCI_CONFIG_TYPE_2 | PCI_NO_IRQ_STEERING);
    pci_register_slot(0x00, PCI_CARD_SPECIAL, 0, 0, 0, 0);
    pci_register_slot(0x01, PCI_CARD_SPECIAL, 0, 0, 0, 0);
    pci_register_slot(0x03, PCI_CARD_NORMAL, 1, 2, 3, 4);	/* 03 = Slot 1 */
    pci_register_slot(0x04, PCI_CARD_NORMAL, 2, 3, 4, 1);	/* 04 = Slot 2 */
    pci_register_slot(0x05, PCI_CARD_NORMAL, 3, 4, 1, 2);	/* 05 = Slot 3 */
    pci_register_slot(0x06, PCI_CARD_NORMAL, 4, 1, 2, 3);	/* 06 = Slot 4 */
    pci_register_slot(0x07, PCI_CARD_SCSI, 1, 2, 3, 4);		/* 07 = SCSI */
    pci_register_slot(0x02, PCI_CARD_SPECIAL, 0, 0, 0, 0);
    device_add(&fdc_at_device);
    device_add(&keyboard_ps2_pci_device);
    device_add(&sio_device);
    device_add(&intel_flash_bxt_device);
}


int
machine_at_batman_init(const machine_t *model)
{
    int ret;

    ret = bios_load_linear_combined(L"roms/machines/revenge/1009af2_.bio",
				    L"roms/machines/revenge/1009af2_.bi1", 0x1c000, 128);

    if (bios_only || !ret)
	return ret;

    machine_at_premiere_common_init(model);

    device_add(&i430lx_device);

    return ret;
}

int
machine_at_ambradp60_init(const machine_t *model)
{
    int ret;

    ret = bios_load_linear_combined(L"roms/machines/ambradp60/1004AF1P.BIO",
				    L"roms/machines/ambradp60/1004AF1P.BI1", 0x1c000, 128);

    if (bios_only || !ret)
	return ret;

    machine_at_premiere_common_init(model);

    device_add(&i430lx_device);

    return ret;
}

int
machine_at_586mc1_init(const machine_t *model)
{
    int ret;

    ret = bios_load_linear(L"roms/machines/586mc1/IS.34",
			   0x000e0000, 131072, 0);

    if (bios_only || !ret)
	return ret;

    machine_at_award_common_init(model);

    device_add(&i430lx_device);

    return ret;
}


int
machine_at_plato_init(const machine_t *model)
{
    int ret;

    ret = bios_load_linear_combined(L"roms/machines/plato/1016ax1_.bio",
				    L"roms/machines/plato/1016ax1_.bi1", 0x1d000, 128);

    if (bios_only || !ret)
	return ret;

    machine_at_premiere_common_init(model);

    device_add(&i430nx_device);

    return ret;
}

int
machine_at_gwplato_init(const machine_t *model)
{
    int ret;

    ret = bios_load_linear(L"roms/machines/gwplato/OLDBIOS.BIN",
			   0x000e0000, 131072, 0);

    if (bios_only || !ret)
	return ret;

    machine_at_premiere_common_init(model);

    device_add(&i430nx_device);

    return ret;
}

int
machine_at_ambradp90_init(const machine_t *model)
{
    int ret;

    ret = bios_load_linear_combined(L"roms/machines/ambradp90/1002AX1P.BIO",
				    L"roms/machines/ambradp90/1002AX1P.BI1", 0x1d000, 128);

    if (bios_only || !ret)
	return ret;

    machine_at_premiere_common_init(model);

    device_add(&i430nx_device);

    return ret;
}

int
machine_at_430nx_init(const machine_t *model)
{
    int ret;

    ret = bios_load_linear(L"roms/machines/430nx/IP.20",
			   0x000e0000, 131072, 0);

    if (bios_only || !ret)
	return ret;

    machine_at_award_common_init(model);

    device_add(&i430nx_device);

    return ret;
}


int
machine_at_p54tp4xe_init(const machine_t *model)
{
    int ret;

    ret = bios_load_linear(L"roms/machines/p54tp4xe/t15i0302.awd",
			   0x000e0000, 131072, 0);

    if (bios_only || !ret)
	return ret;

    machine_at_common_init(model);

    /* Award BIOS, SMC FDC37C665. */
    pci_init(PCI_CONFIG_TYPE_1);
    pci_register_slot(0x00, PCI_CARD_SPECIAL, 0, 0, 0, 0);
    pci_register_slot(0x0C, PCI_CARD_NORMAL, 1, 2, 3, 4);
    pci_register_slot(0x0B, PCI_CARD_NORMAL, 2, 3, 4, 1);
    pci_register_slot(0x0A, PCI_CARD_NORMAL, 3, 4, 1, 2);
    pci_register_slot(0x09, PCI_CARD_NORMAL, 4, 1, 2, 3);
    pci_register_slot(0x07, PCI_CARD_SPECIAL, 0, 0, 0, 0);
    device_add(&keyboard_ps2_pci_device);
    device_add(&i430fx_device);
    device_add(&piix_device);
    device_add(&fdc37c665_device);
    device_add(&intel_flash_bxt_device);

    return ret;
}


int
machine_at_endeavor_init(const machine_t *model)
{
    int ret;

    ret = bios_load_linear_combined(L"roms/machines/endeavor/1006cb0_.bio",
				    L"roms/machines/endeavor/1006cb0_.bi1", 0x1d000, 128);

    if (bios_only || !ret)
	return ret;

    machine_at_common_init(model);

    pci_init(PCI_CONFIG_TYPE_1);
    pci_register_slot(0x00, PCI_CARD_SPECIAL, 0, 0, 0, 0);
    pci_register_slot(0x08, PCI_CARD_ONBOARD, 4, 0, 0, 0);
    pci_register_slot(0x0D, PCI_CARD_NORMAL, 1, 2, 3, 4);
    pci_register_slot(0x0E, PCI_CARD_NORMAL, 2, 3, 4, 1);
    pci_register_slot(0x0F, PCI_CARD_NORMAL, 3, 4, 1, 2);
    pci_register_slot(0x10, PCI_CARD_NORMAL, 4, 1, 2, 3);
    pci_register_slot(0x07, PCI_CARD_SPECIAL, 0, 0, 0, 0);

    if (gfxcard == VID_INTERNAL)
	device_add(&s3_phoenix_trio64_onboard_pci_device);

    device_add(&keyboard_ps2_ami_pci_device);
    device_add(&i430fx_device);
    device_add(&piix_device);
    device_add(&pc87306_device);
    device_add(&intel_flash_bxt_ami_device);

    return ret;
}


const device_t *
at_endeavor_get_device(void)
{
    return &s3_phoenix_trio64_onboard_pci_device;
}


int
machine_at_zappa_init(const machine_t *model)
{
    int ret;

    ret = bios_load_linear_combined(L"roms/machines/zappa/1006bs0_.bio",
				    L"roms/machines/zappa/1006bs0_.bi1", 0x20000, 128);

    if (bios_only || !ret)
	return ret;

    machine_at_common_init(model);

    pci_init(PCI_CONFIG_TYPE_1);
    pci_register_slot(0x00, PCI_CARD_SPECIAL, 0, 0, 0, 0);
    pci_register_slot(0x0D, PCI_CARD_NORMAL, 1, 2, 3, 4);
    pci_register_slot(0x0E, PCI_CARD_NORMAL, 3, 4, 1, 2);
    pci_register_slot(0x0F, PCI_CARD_NORMAL, 2, 3, 4, 1);
    pci_register_slot(0x07, PCI_CARD_SPECIAL, 0, 0, 0, 0);
    device_add(&keyboard_ps2_ami_pci_device);
    device_add(&i430fx_device);
    device_add(&piix_device);
    device_add(&pc87306_device);
    device_add(&intel_flash_bxt_ami_device);

    return ret;
}


int
machine_at_mb500n_init(const machine_t *model)
{
    int ret;

    ret = bios_load_linear(L"roms/machines/mb500n/031396s.bin",
			   0x000e0000, 131072, 0);

    if (bios_only || !ret)
	return ret;

    machine_at_common_init(model);

    pci_init(PCI_CONFIG_TYPE_1);
    pci_register_slot(0x00, PCI_CARD_SPECIAL, 0, 0, 0, 0);
    pci_register_slot(0x14, PCI_CARD_NORMAL, 1, 2, 3, 4);
    pci_register_slot(0x13, PCI_CARD_NORMAL, 2, 3, 4, 1);
    pci_register_slot(0x12, PCI_CARD_NORMAL, 3, 4, 1, 2);
    pci_register_slot(0x11, PCI_CARD_NORMAL, 4, 1, 2, 3);
    pci_register_slot(0x07, PCI_CARD_SPECIAL, 0, 0, 0, 0);
    device_add(&keyboard_ps2_pci_device);
    device_add(&i430fx_device);
    device_add(&piix_device);
    device_add(&fdc37c665_device);
    device_add(&intel_flash_bxt_device);

    return ret;
}


int
machine_at_president_init(const machine_t *model)
{
    int ret;

    ret = bios_load_linear(L"roms/machines/president/bios.bin",
			   0x000e0000, 131072, 0);

    if (bios_only || !ret)
	return ret;

    machine_at_common_init(model);

    pci_init(PCI_CONFIG_TYPE_1);
    pci_register_slot(0x00, PCI_CARD_SPECIAL, 0, 0, 0, 0);
    pci_register_slot(0x08, PCI_CARD_NORMAL, 1, 2, 3, 4);
    pci_register_slot(0x09, PCI_CARD_NORMAL, 2, 3, 4, 1);
    pci_register_slot(0x0A, PCI_CARD_NORMAL, 3, 4, 1, 2);
    pci_register_slot(0x0B, PCI_CARD_NORMAL, 4, 1, 2, 3);
    pci_register_slot(0x07, PCI_CARD_SPECIAL, 0, 0, 0, 0);
    device_add(&i430fx_device);
    device_add(&piix_device);
    device_add(&keyboard_ps2_pci_device);
    device_add(&w83877f_president_device);
    device_add(&intel_flash_bxt_device);

    return ret;
}


#if defined(DEV_BRANCH) && defined(USE_VECTRA54)
int
machine_at_vectra54_init(const machine_t *model)
{
    int ret;

    ret = bios_load_linear(L"roms/machines/vectra54/GT0724.22",
			   0x000e0000, 131072, 0);

    if (bios_only || !ret)
	return ret;

    machine_at_common_init(model);

    pci_init(PCI_CONFIG_TYPE_1);
    pci_register_slot(0x00, PCI_CARD_SPECIAL, 0, 0, 0, 0);
    pci_register_slot(0x0C, PCI_CARD_NORMAL, 1, 2, 3, 4);
    pci_register_slot(0x0B, PCI_CARD_NORMAL, 2, 3, 4, 1);
    pci_register_slot(0x0A, PCI_CARD_NORMAL, 3, 4, 1, 2);
    pci_register_slot(0x09, PCI_CARD_NORMAL, 4, 1, 2, 3);
    pci_register_slot(0x07, PCI_CARD_SPECIAL, 0, 0, 0, 0);
    device_add(&keyboard_ps2_ami_pci_device);
    device_add(&i430fx_device);
    device_add(&piix_device);
    device_add(&fdc37c932qf_device);
    device_add(&intel_flash_bxt_device);

    return ret;
}
#endif
