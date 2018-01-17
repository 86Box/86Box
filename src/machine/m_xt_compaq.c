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
 * Version:	@(#)m_xt_compaq.c	1.0.2	2018/01/16
 *
 * Authors:	Sarah Walker, <http://pcem-emulator.co.uk/>
 *		Miran Grca, <mgrca8@gmail.com>
 *		TheCollector1995, <mariogplayer@gmail.com>
 *
 *		Copyright 2008-2018 Sarah Walker.
 *		Copyright 2016-2018 Miran Grca.
 */
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <wchar.h>
#include "../86box.h"
#include "../cpu/cpu.h"
#include "../nmi.h"
#include "../pit.h"
#include "../mem.h"
#include "../rom.h"
#include "../device.h"
#include "../floppy/fdd.h"
#include "../floppy/fdc.h"
#include "../game/gameport.h"
#include "../keyboard.h"
#include "machine.h"


void
machine_xt_compaq_init(machine_t *model)
{
    machine_common_init(model);

    pit_set_out_func(&pit, 1, pit_refresh_timer_xt);

    device_add(&keyboard_xt_device);
    device_add(&fdc_xt_device);
    nmi_init();
    if (joystick_type != 7)
	device_add(&gameport_device);

    switch(model->id) {
	case ROM_PORTABLE:
		break;
    }
}
