/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          Implementation of the common initialization functions for
 *          the PC/AT and copatible machines.
 *
 * Authors: Miran Grca, <mgrca8@gmail.com>
 *          Sarah Walker, <https://pcem-emulator.co.uk/>
 *          Jasmine Iwanek, <jriwanek@gmail.com>
 *          Fred N. van Kempen, <decwiz@yahoo.com>
 *
 *          Copyright 2016-2025 Miran Grca.
 *          Copyright 2008-2025 Sarah Walker.
 *          Copyright 2025      Jasmine Iwanek.
 *          Copyright 2017-2025 Fred N. van Kempen.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free  Software  Foundation; either  version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is  distributed in the hope that it will be useful, but
 * WITHOUT   ANY  WARRANTY;  without  even   the  implied  warranty  of
 * MERCHANTABILITY  or FITNESS  FOR A PARTICULAR  PURPOSE. See  the GNU
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
#include <string.h>
#include <wchar.h>
#include <86box/86box.h>
#include <86box/timer.h>
#include <86box/pic.h>
#include <86box/pit.h>
#include <86box/dma.h>
#include <86box/mem.h>
#include <86box/device.h>
#include <86box/fdd.h>
#include <86box/fdc.h>
#include <86box/fdc_ext.h>
#include <86box/nvr.h>
#include <86box/gameport.h>
#include <86box/keyboard.h>
#include <86box/lpt.h>
#include <86box/rom.h>
#include <86box/hdc.h>
#include <86box/port_6x.h>
#include <86box/machine.h>

void
machine_at_common_init(const machine_t *model)
{
    machine_common_init(model);

    refresh_at_enable = 1;
    pit_devs[0].set_out_func(pit_devs[0].data, 1, pit_refresh_timer_at);
    pic2_init();
    dma16_init();

    if (model->init != machine_at_m290_init)
        device_add(&port_6x_device);

    standalone_gameport_type = &gameport_device;
}

void
machine_at_init(const machine_t *model)
{
    machine_at_common_init(model);

    device_add_params(machine_get_kbc_device(machine), (void *) model->kbc_params);
}

void
machine_at_ps2_init(const machine_t *model)
{
    machine_at_common_init(model);

    device_add_params(machine_get_kbc_device(machine), (void *) model->kbc_params);
}

void
machine_at_common_ide_init(const machine_t *model)
{
    machine_at_common_init(model);

    device_add(&ide_isa_device);
}

void
machine_at_ide_init(const machine_t *model)
{
    machine_at_init(model);

    device_add(&ide_isa_device);
}

void
machine_at_ps2_ide_init(const machine_t *model)
{
    machine_at_ps2_init(model);

    device_add(&ide_isa_device);
}
