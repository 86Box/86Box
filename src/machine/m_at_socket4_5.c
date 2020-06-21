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
 *
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
#include <86box/86box.h>
#include <86box/mem.h>
#include <86box/io.h>
#include <86box/rom.h>
#include <86box/pci.h>
#include <86box/device.h>
#include <86box/chipset.h>
#include <86box/hdc.h>
#include <86box/hdc_ide.h>
#include <86box/timer.h>
#include <86box/fdd.h>
#include <86box/fdc.h>
#include <86box/keyboard.h>
#include <86box/flash.h>
#include <86box/nvr.h>
#include <86box/sio.h>
#include <86box/video.h>
#include <86box/machine.h>
int
machine_at_excalibur_init(const machine_t *model)
{
    int ret;

    ret = bios_load_linear_inverted(L"roms/machines/excalibur/S75P.ROM",
			   0x000e0000, 131072, 0);

    if (bios_only || !ret)
	return ret;

    machine_at_common_init(model);

    device_add(&ide_vlb_device);
    device_add(&opti5x7_device);
    device_add(&fdc37c663_device);
    device_add(&keyboard_at_ami_device);

    return ret;
}


static void
machine_at_premiere_common_init(const machine_t *model, int pci_switch)
{
    machine_at_common_init(model);
    device_add(&ide_pci_2ch_device);

    pci_init(PCI_CONFIG_TYPE_2 | pci_switch);
    pci_register_slot(0x00, PCI_CARD_NORTHBRIDGE, 0, 0, 0, 0);
    pci_register_slot(0x01, PCI_CARD_SPECIAL, 0, 0, 0, 0);
    pci_register_slot(0x06, PCI_CARD_NORMAL, 3, 2, 1, 4);
    pci_register_slot(0x0E, PCI_CARD_NORMAL, 2, 1, 3, 4);
    pci_register_slot(0x0C, PCI_CARD_NORMAL, 1, 3, 2, 4);
    pci_register_slot(0x02, PCI_CARD_SOUTHBRIDGE, 0, 0, 0, 0);
    device_add(&keyboard_ps2_intel_ami_pci_device);
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
    pci_register_slot(0x00, PCI_CARD_NORTHBRIDGE, 0, 0, 0, 0);
    pci_register_slot(0x01, PCI_CARD_SPECIAL, 0, 0, 0, 0);
    pci_register_slot(0x03, PCI_CARD_NORMAL, 1, 2, 3, 4);	/* 03 = Slot 1 */
    pci_register_slot(0x04, PCI_CARD_NORMAL, 2, 3, 4, 1);	/* 04 = Slot 2 */
    pci_register_slot(0x05, PCI_CARD_NORMAL, 3, 4, 1, 2);	/* 05 = Slot 3 */
    pci_register_slot(0x06, PCI_CARD_NORMAL, 4, 1, 2, 3);	/* 06 = Slot 4 */
    pci_register_slot(0x07, PCI_CARD_SCSI, 1, 2, 3, 4);		/* 07 = SCSI */
    pci_register_slot(0x02, PCI_CARD_SOUTHBRIDGE, 0, 0, 0, 0);
    device_add(&fdc_at_device);
    device_add(&keyboard_ps2_pci_device);
}


int
machine_at_batman_init(const machine_t *model)
{
    int ret;

    ret = bios_load_linear_combined(L"roms/machines/revenge/1009af2_.bio",
				    L"roms/machines/revenge/1009af2_.bi1", 0x1c000, 128);

    if (bios_only || !ret)
	return ret;

    machine_at_premiere_common_init(model, 0);

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

    machine_at_premiere_common_init(model, 0);

    device_add(&i430lx_device);

    return ret;
}


#if defined(DEV_BRANCH) && defined(USE_VPP60)
int
machine_at_valuepointp60_init(const machine_t *model)
{
    int ret;

    ret = bios_load_linear_combined(L"roms/machines/valuepointp60/1006AV0M.BIO",
				    L"roms/machines/valuepointp60/1006AV0M.BI1", 0x1d000, 128);

    if (bios_only || !ret)
	return ret;

    machine_at_premiere_common_init(model, 0);

    device_add(&i430lx_device);

    return ret;
}
#endif


int
machine_at_p5mp3_init(const machine_t *model)
{
    int ret;

    ret = bios_load_linear(L"roms/machines/p5mp3/0205.bin",
			   0x000e0000, 131072, 0);

    if (bios_only || !ret)
	return ret;

    machine_at_common_init(model);
    device_add(&ide_pci_device);

    pci_init(PCI_CONFIG_TYPE_2 | PCI_NO_IRQ_STEERING);
    pci_register_slot(0x00, PCI_CARD_NORTHBRIDGE, 0, 0, 0, 0);
    pci_register_slot(0x05, PCI_CARD_NORMAL, 1, 2, 3, 4);	/* 05 = Slot 1 */
    pci_register_slot(0x04, PCI_CARD_NORMAL, 2, 3, 4, 1);	/* 04 = Slot 2 */
    pci_register_slot(0x03, PCI_CARD_NORMAL, 3, 4, 1, 2);	/* 03 = Slot 3 */
    pci_register_slot(0x02, PCI_CARD_SOUTHBRIDGE, 0, 0, 0, 0);
    device_add(&fdc_at_device);
    device_add(&keyboard_ps2_pci_device);

    device_add(&sio_zb_device);
    device_add(&catalyst_flash_device);
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

    device_add(&sio_device);
    device_add(&intel_flash_bxt_device);
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

    machine_at_premiere_common_init(model, PCI_CAN_SWITCH_TYPE);

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

    machine_at_premiere_common_init(model, PCI_CAN_SWITCH_TYPE);

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

    device_add(&sio_device);
    device_add(&intel_flash_bxt_device);
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
    pci_register_slot(0x00, PCI_CARD_NORTHBRIDGE, 0, 0, 0, 0);
    pci_register_slot(0x0C, PCI_CARD_NORMAL, 1, 2, 3, 4);
    pci_register_slot(0x0B, PCI_CARD_NORMAL, 2, 3, 4, 1);
    pci_register_slot(0x0A, PCI_CARD_NORMAL, 3, 4, 1, 2);
    pci_register_slot(0x09, PCI_CARD_NORMAL, 4, 1, 2, 3);
    pci_register_slot(0x07, PCI_CARD_SOUTHBRIDGE, 0, 0, 0, 0);
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
    pci_register_slot(0x00, PCI_CARD_NORTHBRIDGE, 0, 0, 0, 0);
    pci_register_slot(0x08, PCI_CARD_ONBOARD, 4, 0, 0, 0);
    pci_register_slot(0x0D, PCI_CARD_NORMAL, 1, 2, 3, 4);
    pci_register_slot(0x0E, PCI_CARD_NORMAL, 2, 3, 4, 1);
    pci_register_slot(0x0F, PCI_CARD_NORMAL, 3, 4, 1, 2);
    pci_register_slot(0x10, PCI_CARD_NORMAL, 4, 1, 2, 3);
    pci_register_slot(0x07, PCI_CARD_SOUTHBRIDGE, 0, 0, 0, 0);

    if (gfxcard == VID_INTERNAL)
	device_add(&s3_phoenix_trio64_onboard_pci_device);

    device_add(&keyboard_ps2_intel_ami_pci_device);
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
    pci_register_slot(0x00, PCI_CARD_NORTHBRIDGE, 0, 0, 0, 0);
    pci_register_slot(0x0D, PCI_CARD_NORMAL, 1, 2, 3, 4);
    pci_register_slot(0x0E, PCI_CARD_NORMAL, 3, 4, 1, 2);
    pci_register_slot(0x0F, PCI_CARD_NORMAL, 2, 3, 4, 1);
    pci_register_slot(0x07, PCI_CARD_SOUTHBRIDGE, 0, 0, 0, 0);
    device_add(&keyboard_ps2_intel_ami_pci_device);
    device_add(&i430fx_device);
    device_add(&piix_device);
    device_add(&pc87306_device);
    device_add(&intel_flash_bxt_ami_device);

    return ret;
}


int
machine_at_gw2kzp_init(const machine_t *model)
{
    int ret;

    ret = bios_load_linear_combined(L"roms/machines/gw2k_zp/1011bs0t.bio",
				    L"roms/machines/gw2k_zp/1011bs0t.bi1", 0x20000, 128);

    if (bios_only || !ret)
	return ret;

    machine_at_common_init(model);

    pci_init(PCI_CONFIG_TYPE_1);
    pci_register_slot(0x00, PCI_CARD_NORTHBRIDGE, 0, 0, 0, 0);
    pci_register_slot(0x0D, PCI_CARD_NORMAL, 1, 2, 3, 4);
    pci_register_slot(0x0E, PCI_CARD_NORMAL, 2, 3, 4, 1);
    pci_register_slot(0x0F, PCI_CARD_NORMAL, 3, 4, 1, 2);
    pci_register_slot(0x07, PCI_CARD_SOUTHBRIDGE, 0, 0, 0, 0);
    device_add(&keyboard_ps2_intel_ami_pci_device);
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
    pci_register_slot(0x00, PCI_CARD_NORTHBRIDGE, 0, 0, 0, 0);
    pci_register_slot(0x14, PCI_CARD_NORMAL, 1, 2, 3, 4);
    pci_register_slot(0x13, PCI_CARD_NORMAL, 2, 3, 4, 1);
    pci_register_slot(0x12, PCI_CARD_NORMAL, 3, 4, 1, 2);
    pci_register_slot(0x11, PCI_CARD_NORMAL, 4, 1, 2, 3);
    pci_register_slot(0x07, PCI_CARD_SOUTHBRIDGE, 0, 0, 0, 0);
    device_add(&keyboard_ps2_pci_device);
    device_add(&i430fx_device);
    device_add(&piix_device);
    device_add(&fdc37c665_device);
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

    machine_at_common_init_ex(model, 2);

    pci_init(PCI_CONFIG_TYPE_1);
    pci_register_slot(0x00, PCI_CARD_NORTHBRIDGE, 0, 0, 0, 0);
    pci_register_slot(0x0C, PCI_CARD_NORMAL, 1, 2, 3, 4);
    pci_register_slot(0x0B, PCI_CARD_NORMAL, 2, 3, 4, 1);
    pci_register_slot(0x0A, PCI_CARD_NORMAL, 3, 4, 1, 2);
    pci_register_slot(0x09, PCI_CARD_NORMAL, 4, 1, 2, 3);
    pci_register_slot(0x0F, PCI_CARD_SOUTHBRIDGE, 0, 0, 0, 0);
    device_add(&keyboard_ps2_ami_pci_device);
    device_add(&i430fx_device);
    device_add(&piix_device);
    device_add(&fdc37c932qf_device);
    device_add(&intel_flash_bxt_device);

    return ret;
}
#endif


int
machine_at_powermate_v_init(const machine_t *model)
{
    int ret;

    ret = bios_load_linear(L"roms/machines/powermate_v/BIOS.ROM",
			   0x000e0000, 131072, 0);

    if (bios_only || !ret)
	return ret;

    machine_at_common_init(model);

    pci_init(PCI_CONFIG_TYPE_1);
    pci_register_slot(0x00, PCI_CARD_NORTHBRIDGE, 0, 0, 0, 0);
    pci_register_slot(0x07, PCI_CARD_SOUTHBRIDGE, 0, 0, 0, 0);
    pci_register_slot(0x08, PCI_CARD_NORMAL, 0, 0, 0, 0);
    pci_register_slot(0x11, PCI_CARD_NORMAL, 1, 2, 3, 4);
    pci_register_slot(0x13, PCI_CARD_NORMAL, 2, 3, 4, 1);
    device_add(&keyboard_ps2_ami_pci_device);
    device_add(&i430fx_device);
    device_add(&piix_device);
    device_add(&fdc37c665_device);
    device_add(&intel_flash_bxt_device);

    return ret;
}


int
machine_at_acerv30_init(const machine_t *model)
{
    int ret;

    ret = bios_load_linear(L"roms/machines/acerv30/V30R01N9.BIN",
			   0x000e0000, 131072, 0);

    if (bios_only || !ret)
	return ret;

    machine_at_common_init(model);

    pci_init(PCI_CONFIG_TYPE_1);
    pci_register_slot(0x00, PCI_CARD_NORTHBRIDGE, 0, 0, 0, 0);
    pci_register_slot(0x07, PCI_CARD_SOUTHBRIDGE, 0, 0, 0, 0);
    pci_register_slot(0x12, PCI_CARD_NORMAL, 1, 2, 3, 4);
    pci_register_slot(0x11, PCI_CARD_NORMAL, 2, 3, 4, 1);
    pci_register_slot(0x14, PCI_CARD_NORMAL, 3, 4, 1, 2);
    pci_register_slot(0x13, PCI_CARD_NORMAL, 4, 1, 2, 3);
    device_add(&i430fx_device);
    device_add(&piix_device);
    device_add(&keyboard_ps2_acer_pci_device);
    device_add(&fdc37c665_device);

    device_add(&sst_flash_29ee010_device);

    return ret;
}
