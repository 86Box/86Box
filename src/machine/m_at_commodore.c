/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          Implementation of the Commodore PC3 system.
 *
 *
 *
 * Authors: Fred N. van Kempen, <decwiz@yahoo.com>
 *          Miran Grca, <mgrca8@gmail.com>
 *          Sarah Walker, <https://pcem-emulator.co.uk/>
 *
 *          Copyright 2017-2018 Fred N. van Kempen.
 *          Copyright 2016-2018 Miran Grca.
 *          Copyright 2008-2018 Sarah Walker.
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
#include <86box/device.h>
#include <86box/timer.h>
#include <86box/io.h>
#include <86box/mem.h>
#include <86box/fdc_ext.h>
#include <86box/lpt.h>
#include <86box/rom.h>
#include <86box/serial.h>
#include <86box/fdd.h>
#include <86box/fdc.h>
#include <86box/machine.h>
#include <86box/plat_unused.h>

static serial_t *cmd_uart;

static void
cbm_io_write(UNUSED(uint16_t port), uint8_t val, UNUSED(void *priv))
{
    lpt1_remove();
    lpt2_remove();

    switch (val & 3) {
        case 1:
            lpt1_setup(LPT_MDA_ADDR);
            break;
        case 2:
            lpt1_setup(LPT1_ADDR);
            break;
        case 3:
            lpt1_setup(LPT2_ADDR);
            break;

        default:
            break;
    }

    switch (val & 0xc) {
        case 0x4:
            serial_setup(cmd_uart, COM2_ADDR, COM2_IRQ);
            break;
        case 0x8:
            serial_setup(cmd_uart, COM1_ADDR, COM1_IRQ);
            break;

        default:
            break;
    }
}

static void
cbm_io_init(void)
{
    io_sethandler(0x0230, 0x0001, NULL, NULL, NULL, cbm_io_write, NULL, NULL, NULL);
}

int
machine_at_cmdpc_init(const machine_t *model)
{
    int ret;

    ret = bios_load_interleaved("roms/machines/cmdpc30/commodore pc 30 iii even.bin",
                                "roms/machines/cmdpc30/commodore pc 30 iii odd.bin",
                                0x000f8000, 32768, 0);

    if (bios_only || !ret)
        return ret;

    machine_at_init(model);

    mem_remap_top(384);

    if (fdc_current[0] == FDC_INTERNAL)
        device_add(&fdc_at_device);

    cmd_uart = device_add(&ns8250_device);

    cbm_io_init();

    return ret;
}
