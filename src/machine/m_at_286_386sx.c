/*
 * 86Box	A hypervisor and IBM PC system emulator that specializes in
 *		running old operating systems and software designed for IBM
 *		PC systems and compatibles from 1981 through fairly recent
 *		system designs based on the PCI bus.
 *
 *		This file is part of the 86Box distribution.
 *
 *		Implementation of 286 and 386SX machines.
 *
 *
 *
 * Authors:	Sarah Walker, <http://pcem-emulator.co.uk/>
 *		Miran Grca, <mgrca8@gmail.com>
 *
 *		Copyright 2010-2019 Sarah Walker.
 *		Copyright 2016-2019 Miran Grca.
 */
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <wchar.h>
#define HAVE_STDARG_H
#include <86box/86box.h>
#include "cpu.h"
#include <86box/timer.h>
#include <86box/io.h>
#include <86box/device.h>
#include <86box/chipset.h>
#include <86box/keyboard.h>
#include <86box/mem.h>
#include <86box/rom.h>
#include <86box/fdd.h>
#include <86box/fdc.h>
#include <86box/hdc.h>
#include <86box/sio.h>
#include <86box/video.h>
#include <86box/machine.h>

int
machine_at_mr286_init(const machine_t *model)
{
    int ret;

    ret = bios_load_interleaved(L"roms/machines/mr286/V000B200-1",
				L"roms/machines/mr286/V000B200-2",
				0x000f0000, 65536, 0);

    if (bios_only || !ret)
	return ret;

    machine_at_common_ide_init(model);
    device_add(&keyboard_at_device);
    device_add(&fdc_at_device);

    return ret;
}


static void
machine_at_headland_common_init(int ht386)
{
    device_add(&keyboard_at_ami_device);
    device_add(&fdc_at_device);

    if (ht386)
	device_add(&headland_ht18b_device);
    else
	device_add(&headland_gc10x_device);
}


#if defined(DEV_BRANCH) && defined(USE_AMI386SX)
int
machine_at_headland_init(const machine_t *model)
{
    int ret;

    ret = bios_load_linear(L"roms/machines/ami386/ami386.bin",
			   0x000f0000, 65536, 0);

    if (bios_only || !ret)
	return ret;

    machine_at_common_ide_init(model);

    machine_at_headland_common_init(1);

    return ret;
}
#endif


int
machine_at_tg286m_init(const machine_t *model)
{
    int ret;

    ret = bios_load_linear(L"roms/machines/tg286m/ami.bin",
			   0x000f0000, 131072, 0);

    if (bios_only || !ret)
	return ret;

    machine_at_common_ide_init(model);

    machine_at_headland_common_init(0);

    return ret;
}


const device_t *
at_ama932j_get_device(void)
{
    return &oti067_ama932j_device;
}


int
machine_at_ama932j_init(const machine_t *model)
{
    int ret;

    ret = bios_load_linear(L"roms/machines/ama932j/ami.bin",
			   0x000f0000, 131072, 0);

    if (bios_only || !ret)
	return ret;

    machine_at_common_ide_init(model);

    machine_at_headland_common_init(1);

    if (gfxcard == VID_INTERNAL)
	device_add(&oti067_ama932j_device);

    return ret;
}


int
machine_at_quadt286_init(const machine_t *model)
{
    int ret;

    ret = bios_load_interleaved(L"roms/machines/quadt286/QUADT89L.ROM",
				L"roms/machines/quadt286/QUADT89H.ROM",
				0x000f0000, 65536, 0);

    if (bios_only || !ret)
	return ret;

    machine_at_common_init(model);
    device_add(&keyboard_at_device);
    device_add(&fdc_at_device);
    device_add(&headland_gc10x_device);

    return ret;
}


int
machine_at_neat_init(const machine_t *model)
{
    int ret;

    ret = bios_load_linear(L"roms/machines/dtk386/3cto001.bin",
			   0x000f0000, 65536, 0);

    if (bios_only || !ret)
	return ret;

    machine_at_init(model);

    device_add(&neat_device);
    device_add(&fdc_at_device);

    return ret;
}


int
machine_at_neat_ami_init(const machine_t *model)
{
    int ret;

    ret = bios_load_linear(L"roms/machines/ami286/amic206.bin",
			   0x000f0000, 65536, 0);

    if (bios_only || !ret)
	return ret;

    machine_at_common_init(model);

    device_add(&neat_device);
    device_add(&fdc_at_device);

    device_add(&keyboard_at_ami_device);

    return ret;
}


int
machine_at_px286_init(const machine_t *model)
{
    int ret;

	ret = bios_load_linear(L"roms/machines/px286/KENITEC.BIN",
				0x000f0000, 65536, 0);

    if (bios_only || !ret)
	return ret;

    machine_at_common_init(model);
    device_add(&keyboard_at_device);
    device_add(&fdc_at_device);
    device_add(&neat_device);

    return ret;
}


int
machine_at_goldstar386_init(const machine_t *model)
{
    int ret;

    ret = bios_load_interleaved(L"roms/machines/goldstar386/386-Goldstar-E.BIN",
				L"roms/machines/goldstar386/386-Goldstar-O.BIN",
				0x000f0000, 131072, 0);

    if (bios_only || !ret)
	return ret;

    machine_at_init(model);

    device_add(&neat_device);
    device_add(&fdc_at_device);

    return ret;
}

int
machine_at_micronics386_init(const machine_t *model)
{
    int ret;

    ret = bios_load_interleaved(L"roms/machines/micronics386/386-Micronics-09-00021-EVEN.BIN",
				L"roms/machines/micronics386/386-Micronics-09-00021-ODD.BIN",
				0x000f0000, 131072, 0);

    if (bios_only || !ret)
	return ret;

    machine_at_init(model);

    device_add(&neat_device);
    device_add(&fdc_at_device);

    return ret;
}


static void
machine_at_scat_init(const machine_t *model, int is_v4)
{
    machine_at_init(model);
    device_add(&fdc_at_device);

    if (is_v4)
	device_add(&scat_4_device);
    else
	device_add(&scat_device);
}


static void
machine_at_scatsx_init(const machine_t *model)
{
    machine_at_common_init(model);

    device_add(&keyboard_at_ami_device);
    device_add(&fdc_at_device);

    device_add(&scat_sx_device);
}


int
machine_at_award286_init(const machine_t *model)
{
    int ret;

    ret = bios_load_linear(L"roms/machines/award286/award.bin",
			   0x000f0000, 131072, 0);

    if (bios_only || !ret)
	return ret;

    machine_at_scat_init(model, 0);

    return ret;
}

int
machine_at_gdc212m_init(const machine_t *model)
{
    int ret;

    ret = bios_load_linear(L"roms/machines/gdc212m/gdc212m_72h.bin",
			   0x000f0000, 65536, 0);

    if (bios_only || !ret)
	return ret;

    machine_at_scat_init(model, 0);

    return ret;
}

int
machine_at_gw286ct_init(const machine_t *model)
{
    int ret;

    ret = bios_load_linear(L"roms/machines/gw286ct/2ctc001.bin",
			   0x000f0000, 131072, 0);

    if (bios_only || !ret)
	return ret;

    device_add(&f82c710_device);

    machine_at_scat_init(model, 1);

    return ret;
}


int
machine_at_super286tr_init(const machine_t *model)
{
    int ret;

    ret = bios_load_linear(L"roms/machines/super286tr/hyundai_award286.bin",
			   0x000f0000, 131072, 0);

    if (bios_only || !ret)
	return ret;

    machine_at_scat_init(model, 0);

    return ret;
}


int
machine_at_spc4200p_init(const machine_t *model)
{
    int ret;

    ret = bios_load_linear(L"roms/machines/spc4200p/u8.01",
			   0x000f0000, 131072, 0);

    if (bios_only || !ret)
	return ret;

    machine_at_scat_init(model, 0);

    return ret;
}


int
machine_at_spc4216p_init(const machine_t *model)
{
    int ret;

    ret = bios_load_interleaved(L"roms/machines/spc4216p/7101.u8",
				L"roms/machines/spc4216p/ac64.u10",
				0x000f0000, 131072, 0);

    if (bios_only || !ret)
	return ret;

    machine_at_scat_init(model, 1);

    return ret;
}


int
machine_at_kmxc02_init(const machine_t *model)
{
    int ret;

    ret = bios_load_linear(L"roms/machines/kmxc02/3ctm005.bin",
			   0x000f0000, 131072, 0);

    if (bios_only || !ret)
	return ret;

    machine_at_scatsx_init(model);

    return ret;
}


int
machine_at_deskmaster286_init(const machine_t *model)
{
    int ret;

    ret = bios_load_linear(L"roms/machines/deskmaster286/SAMSUNG-DESKMASTER-28612-ROM.BIN",
			   0x000f0000, 131072, 0);

    if (bios_only || !ret)
	return ret;

    machine_at_scat_init(model, 0);

    return ret;
}


int
machine_at_shuttle386sx_init(const machine_t *model)
{
    int ret;

    ret = bios_load_interleaved(L"roms/machines/shuttle386sx/386-Shuttle386SX-Even.BIN",
				L"roms/machines/shuttle386sx/386-Shuttle386SX-Odd.BIN",
			   0x000f0000, 131072, 0);

    if (bios_only || !ret)
	return ret;

    machine_at_common_init(model);

    device_add(&i82335_device);
    device_add(&keyboard_at_ami_device);
    device_add(&fdc_at_device);

    return ret;
}


int
machine_at_adi386sx_init(const machine_t *model)
{
    int ret;

    ret = bios_load_interleaved(L"roms/machines/adi386sx/3iip001l.bin",
				L"roms/machines/adi386sx/3iip001h.bin",
			   0x000f0000, 65536, 0);

    if (bios_only || !ret)
	return ret;

    machine_at_common_init(model);

    device_add(&i82335_device);
    device_add(&keyboard_at_ami_device);
    device_add(&fdc_at_device);

    return ret;
}


int
machine_at_wd76c10_init(const machine_t *model)
{
    int ret;

    ret = bios_load_interleaved(L"roms/machines/megapc/41651-bios lo.u18",
				L"roms/machines/megapc/211253-bios hi.u19",
				0x000f0000, 65536, 0x08000);

    if (bios_only || !ret)
	return ret;

    machine_at_common_ide_init(model);

    device_add(&keyboard_ps2_quadtel_device);

    device_add(&wd76c10_device);

    if (gfxcard == VID_INTERNAL)
	device_add(&paradise_wd90c11_megapc_device);

    return ret;
}


const device_t *
at_commodore_sl386sx_get_device(void)
{
    return &gd5402_onboard_device;
}


int
machine_at_commodore_sl386sx_init(const machine_t *model)
{
    int ret;

    ret = bios_load_interleaved(L"roms/machines/cbm_sl386sx25/cbm-sl386sx-bios-lo-v1.04-390914-04.bin",
				L"roms/machines/cbm_sl386sx25/cbm-sl386sx-bios-hi-v1.04-390915-04.bin",
				0x000f0000, 65536, 0);

    if (bios_only || !ret)
	return ret;

    machine_at_common_ide_init(model);

    device_add(&keyboard_at_device);
    device_add(&fdc_at_device);
    device_add(&vlsi_scamp_device);

    if (gfxcard == VID_INTERNAL)
	device_add(&gd5402_onboard_device);

    return ret;
}


int
machine_at_awardsx_init(const machine_t *model)
{
    int ret;

    ret = bios_load_linear(L"roms/machines/awardsx/Unknown 386SX OPTi291 - Award (original).BIN",
			   0x000f0000, 65536, 0);

    if (bios_only || !ret)
	return ret;

    machine_at_init(model);

    device_add(&opti291_device);
    device_add(&fdc_at_device);

    return ret;
}
