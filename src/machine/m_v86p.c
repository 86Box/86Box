/*
 * 86Box	A hypervisor and IBM PC system emulator that specializes in
 *		running old operating systems and software designed for IBM
 *		PC systems and compatibles from 1981 through fairly recent
 *		system designs based on the PCI bus.
 *
 *		This file is part of the 86Box distribution.
 *
 *		Victor V86P portable computer emulation.
 *
 * Author:	Lubomir Rintel, <lkundrak@v3.sk>
 *
 *		Copyright 2021 Lubomir Rintel.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free  Software  Foundation; either  version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is  distributed in the hope that it will be useful, but
 * WITHOUT   ANY  WARRANTY;  without  even   the  implied  warranty  of
 * MERCHANTABILITY  or FITNESS	FOR A PARTICULAR  PURPOSE. See	the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the:
 *
 *   Free Software Foundation, Inc.
 *   59 Temple Place - Suite 330
 *   Boston, MA 02111-1307
 *   USA.
 */
#include <stdio.h>
#include <stdint.h>
#include <86box/86box.h>
#include "cpu.h"
#include <86box/mem.h>
#include <86box/timer.h>
#include <86box/rom.h>
#include <86box/machine.h>
#include <86box/device.h>
#include <86box/fdd.h>
#include <86box/fdc.h>
#include <86box/fdc_ext.h>
#include <86box/keyboard.h>
#include <86box/chipset.h>
#include <86box/sio.h>
#include <86box/video.h>

int
machine_v86p_init(const machine_t *model)
{
	int ret, rom = 0;

	ret = bios_load_interleavedr("roms/machines/v86p/INTEL8086AWD_BIOS_S3.1_V86P_122089_Even.rom",
				     "roms/machines/v86p/INTEL8086AWD_BIOS_S3.1_V86P_122089_Odd.rom",
				     0x000f8000, 65536, 0);

	if (!ret) {
		/* Try an older version of the BIOS. */
		rom = 1;
		ret = bios_load_interleavedr("roms/machines/v86p/INTEL8086AWD_BIOS_S3.1_V86P_090489_Even.rom",
					     "roms/machines/v86p/INTEL8086AWD_BIOS_S3.1_V86P_090489_Odd.rom",
					     0x000f8000, 65536, 0);
	}

	if (!ret) {
		/* Try JVERNET's BIOS. */
		rom = 2;
		ret = bios_load_linear("roms/machines/v86p/V86P.ROM",
				       0x000f0000, 65536, 0);

	}

	if (bios_only || !ret)
		return ret;

	if (rom == 2)
		loadfont("roms/machines/v86p/V86P.FON", 8);
	else
		loadfont("roms/machines/v86p/v86pfont.rom", 8);

	machine_common_init(model);

	device_add(&ct_82c100_device);
	device_add(&f82c606_device);

	device_add(&keyboard_xt_device);

	if (fdc_type == FDC_INTERNAL)
		device_add(&fdc_xt_device);

	if (gfxcard == VID_INTERNAL)
		device_add(&f82c425_video_device);

	return ret;
}
