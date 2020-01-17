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
 * Version:	@(#)m_xt_compaq.c	1.0.5	2019/11/15
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
#include "../86box.h"
#include "../cpu/cpu.h"
#include "../nmi.h"
#include "../timer.h"
#include "../pit.h"
#include "../mem.h"
#include "../rom.h"
#include "../device.h"
#include "../floppy/fdd.h"
#include "../floppy/fdc.h"
#include "../game/gameport.h"
#include "../keyboard.h"
#include "../lpt.h"
#include "machine.h"


int
machine_xt_compaq_init(const machine_t *model)
{
    int ret;

    ret = bios_load_linear(L"roms/machines/portable/compaq portable plus 100666-001 rev c u47.bin",
			   0x000fe000, 8192, 0);

    if (bios_only || !ret)
	return ret;

    machine_common_init(model);

    pit_ctr_set_out_func(&pit->counters[1], pit_refresh_timer_xt);

    device_add(&keyboard_xt_compaq_device);
    device_add(&fdc_xt_device);
    nmi_init();
    if (joystick_type != JOYSTICK_TYPE_NONE)
	device_add(&gameport_device);

    lpt1_remove();
    lpt1_init(0x03bc);

    return ret;
}
