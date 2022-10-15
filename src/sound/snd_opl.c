/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          Interface to the actual OPL emulator.
 *
 * TODO:    Finish re-working this into a device_t, which requires a
 *          poll-like function for "update" so the sound card can call
 *          that and get a buffer-full of sample data.
 *
 * Authors: Fred N. van Kempen, <decwiz@yahoo.com>
 *          Miran Grca, <mgrca8@gmail.com>
 *
 *          Copyright 2017-2020 Fred N. van Kempen.
 *          Copyright 2016-2020 Miran Grca.
 */
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>
#define HAVE_STDARG_H

#include "cpu.h"
#include <86box/86box.h>
#include <86box/device.h>
#include <86box/io.h>
#include <86box/sound.h>
#include <86box/snd_opl.h>

static uint32_t fm_dev_inst[FM_DRV_MAX][FM_MAX];

uint8_t
fm_driver_get(int chip_id, fm_drv_t *drv)
{
    switch (chip_id) {
        case FM_YM3812:
            if (fm_driver == FM_DRV_NUKED) {
                *drv      = nuked_opl_drv;
                drv->priv = device_add_inst(&ym3812_nuked_device, fm_dev_inst[fm_driver][chip_id]++);
            } else {
                *drv      = ymfm_drv;
                drv->priv = device_add_inst(&ym3812_ymfm_device, fm_dev_inst[fm_driver][chip_id]++);
            }
            break;

        case FM_YMF262:
            if (fm_driver == FM_DRV_NUKED) {
                *drv      = nuked_opl_drv;
                drv->priv = device_add_inst(&ymf262_nuked_device, fm_dev_inst[fm_driver][chip_id]++);
            } else {
                *drv      = ymfm_drv;
                drv->priv = device_add_inst(&ymf262_ymfm_device, fm_dev_inst[fm_driver][chip_id]++);
            }
            break;

        case FM_YMF289B:
            *drv      = ymfm_drv;
            drv->priv = device_add_inst(&ymf289b_ymfm_device, fm_dev_inst[fm_driver][chip_id]++);
            break;

        default:
            return 0;
    }

    return 1;
};
