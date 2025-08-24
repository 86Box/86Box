/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          Implementation of 386SX machines.
 *
 * Authors: Miran Grca, <mgrca8@gmail.com>
 *          EngiNerd <webmaster.crrc@yahoo.it>
 *
 *          Copyright 2016-2025 Miran Grca.
 *          Copyright 2020-2025 EngiNerd.
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
#include <86box/fdc_ext.h>
#include <86box/hdc.h>
#include <86box/nvr.h>
#include <86box/port_6x.h>
#define USE_SIO_DETECT
#include <86box/sio.h>
#include <86box/serial.h>
#include <86box/video.h>
#include <86box/vid_cga.h>
#include <86box/flash.h>
#include <86box/machine.h>

/* ALi M6117D */
int
machine_at_pja511m_init(const machine_t *model)
{
    int ret;

    ret = bios_load_linear("roms/machines/pja511m/2006915102435734.rom",
                           0x000e0000, 131072, 0);

    if (bios_only || !ret)
        return ret;

    machine_at_common_init(model);

    device_add_inst_params(&fdc37c669_device, 1, (void *) FDC37C6XX_IDE_PRI);
    device_add_inst_params(&fdc37c669_device, 2, (void *) 0);
    device_add_params(machine_get_kbc_device(machine), (void *) model->kbc_params);
    device_add(&ali6117d_device);
    device_add(&sst_flash_29ee010_device);

    return ret;
}

int
machine_at_prox1332_init(const machine_t *model)
{
    int ret;

    ret = bios_load_linear("roms/machines/prox1332/D30B3AC1.BIN",
                           0x000e0000, 131072, 0);

    if (bios_only || !ret)
        return ret;

    machine_at_common_init(model);

    device_add_params(&fdc37c669_device, (void *) FDC37C6XX_370);
    device_add_params(machine_get_kbc_device(machine), (void *) model->kbc_params);
    device_add(&ali6117d_device);
    device_add(&sst_flash_29ee010_device);

    return ret;
}
