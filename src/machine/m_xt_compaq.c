/*
 * 86Box	A hypervisor and IBM PC system emulator that specializes in
 *		running old operating systems and software designed for IBM
 *		PC systems and compatibles from 1981 through fairly recent
 *		system designs based on the PCI bus.
 *
 *		This file is part of the 86Box distribution.
 *
 *		Emulation of various Compaq XT-class PC's.
 *
 *
 *
 * Authors:	Sarah Walker, <http://pcem-emulator.co.uk/>
 *		Miran Grca, <mgrca8@gmail.com>
 *		TheCollector1995, <mariogplayer@gmail.com>
 *
 *		Copyright 2008-2019 Sarah Walker.
 *		Copyright 2016-2019 Miran Grca.
 */
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <wchar.h>
#include <86box/86box.h>
#include "cpu.h"
#include <86box/nmi.h>
#include <86box/timer.h>
#include <86box/pit.h>
#include <86box/mem.h>
#include <86box/rom.h>
#include <86box/device.h>
#include <86box/fdd.h>
#include <86box/fdc.h>
#include <86box/fdc_ext.h>
#include <86box/gameport.h>
#include <86box/keyboard.h>
#include <86box/lpt.h>
#include <86box/machine.h>

int
machine_xt_compaq_deskpro_init(const machine_t *model)
{
    int ret;

    ret = bios_load_linear("roms/machines/deskpro/Compaq - BIOS - Revision J - 106265-002.bin",
			   0x000fe000, 8192, 0);

    if (bios_only || !ret)
	return ret;

    machine_common_init(model);

    pit_ctr_set_out_func(&pit->counters[1], pit_refresh_timer_xt);

    device_add(&keyboard_xt_compaq_device);
    if (fdc_type == FDC_INTERNAL)
	device_add(&fdc_xt_device);
    nmi_init();
    standalone_gameport_type = &gameport_device;

    lpt1_remove();
    lpt1_init(0x03bc);

    return ret;
}


int
machine_xt_compaq_portable_init(const machine_t *model)
{
    int ret;

    ret = bios_load_linear("roms/machines/portable/compaq portable plus 100666-001 rev c u47.bin",
			   0x000fe000, 8192, 0);

    if (bios_only || !ret)
	return ret;

    machine_common_init(model);

    pit_ctr_set_out_func(&pit->counters[1], pit_refresh_timer_xt);

    device_add(&keyboard_xt_compaq_device);
    if (fdc_type == FDC_INTERNAL)
	device_add(&fdc_xt_device);
    nmi_init();
    if (joystick_type)
	device_add(&gameport_device);

    lpt1_remove();
    lpt1_init(0x03bc);

    return ret;
}
