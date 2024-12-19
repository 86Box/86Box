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
        case FM_YM2149: /* SSG */
            *drv      = ymfm_drv;
            drv->priv = device_add_inst(&ym2149_ymfm_device, fm_dev_inst[fm_driver][chip_id]++);
            break;

        case FM_YM3526: /* OPL */
            *drv      = ymfm_drv;
            drv->priv = device_add_inst(&ym3526_ymfm_device, fm_dev_inst[fm_driver][chip_id]++);
            break;

        case FM_Y8950: /* MSX-Audio (OPL with ADPCM) */
            *drv      = ymfm_drv;
            drv->priv = device_add_inst(&y8950_ymfm_device, fm_dev_inst[fm_driver][chip_id]++);
            break;

        case FM_YM3812: /* OPL2 */
            if (fm_driver == FM_DRV_NUKED) {
                *drv      = nuked_opl_drv;
                drv->priv = device_add_inst(&ym3812_nuked_device, fm_dev_inst[fm_driver][chip_id]++);
            } else {
                *drv      = ymfm_drv;
                drv->priv = device_add_inst(&ym3812_ymfm_device, fm_dev_inst[fm_driver][chip_id]++);
            }
            break;

        case FM_YMF262: /* OPL3 */
            if (fm_driver == FM_DRV_NUKED) {
                *drv      = nuked_opl_drv;
                drv->priv = device_add_inst(&ymf262_nuked_device, fm_dev_inst[fm_driver][chip_id]++);
            } else {
                *drv      = ymfm_drv;
                drv->priv = device_add_inst(&ymf262_ymfm_device, fm_dev_inst[fm_driver][chip_id]++);
            }
            break;

        case FM_YMF289B: /* OPL3-L */
            *drv      = ymfm_drv;
            drv->priv = device_add_inst(&ymf289b_ymfm_device, fm_dev_inst[fm_driver][chip_id]++);
            break;

        case FM_YMF278B: /* OPL4 */
            *drv      = ymfm_drv;
            drv->priv = device_add_inst(&ymf278b_ymfm_device, fm_dev_inst[fm_driver][chip_id]++);
            break;

        case FM_YM2413: /* OPLL */
            *drv      = ymfm_drv;
            drv->priv = device_add_inst(&ym2413_ymfm_device, fm_dev_inst[fm_driver][chip_id]++);
            break;

        case FM_YM2423: /* OPLL-X */
            *drv      = ymfm_drv;
            drv->priv = device_add_inst(&ym2423_ymfm_device, fm_dev_inst[fm_driver][chip_id]++);
            break;

        case FM_YMF281: /* OPLLP */
            *drv      = ymfm_drv;
            drv->priv = device_add_inst(&ymf281_ymfm_device, fm_dev_inst[fm_driver][chip_id]++);
            break;

        case FM_DS1001: /* Konami VRC7 MMC */
            *drv      = ymfm_drv;
            drv->priv = device_add_inst(&ds1001_ymfm_device, fm_dev_inst[fm_driver][chip_id]++);
            break;

        case FM_YM2151: /* OPM */
            *drv      = ymfm_drv;
            drv->priv = device_add_inst(&ym2151_ymfm_device, fm_dev_inst[fm_driver][chip_id]++);
            break;

        case FM_YM2203: /* OPN */
            *drv      = ymfm_drv;
            drv->priv = device_add_inst(&ym2203_ymfm_device, fm_dev_inst[fm_driver][chip_id]++);
            break;

        case FM_YM2608: /* OPNA */
            *drv      = ymfm_drv;
            drv->priv = device_add_inst(&ym2608_ymfm_device, fm_dev_inst[fm_driver][chip_id]++);
            break;

        case FM_YMF288: /* OPN3L */
            *drv      = ymfm_drv;
            drv->priv = device_add_inst(&ymf288_ymfm_device, fm_dev_inst[fm_driver][chip_id]++);
            break;

        case FM_YM2610: /* OPNB */
            *drv      = ymfm_drv;
            drv->priv = device_add_inst(&ym2610_ymfm_device, fm_dev_inst[fm_driver][chip_id]++);
            break;

        case FM_YM2610B: /* OPNB2 */
            *drv      = ymfm_drv;
            drv->priv = device_add_inst(&ym2610b_ymfm_device, fm_dev_inst[fm_driver][chip_id]++);
            break;

        case FM_YM2612: /* OPN2 */
            *drv      = ymfm_drv;
            drv->priv = device_add_inst(&ym2612_ymfm_device, fm_dev_inst[fm_driver][chip_id]++);
            break;

        case FM_YM3438: /* OPN2C */
            *drv      = ymfm_drv;
            drv->priv = device_add_inst(&ym3438_ymfm_device, fm_dev_inst[fm_driver][chip_id]++);
            break;

        case FM_YMF276: /* OPN2L */
            *drv      = ymfm_drv;
            drv->priv = device_add_inst(&ymf276_ymfm_device, fm_dev_inst[fm_driver][chip_id]++);
            break;

        case FM_YM2164: /* OPP */
            *drv      = ymfm_drv;
            drv->priv = device_add_inst(&ym2164_ymfm_device, fm_dev_inst[fm_driver][chip_id]++);
            break;

        case FM_YM3806: /* OPQ */
            *drv      = ymfm_drv;
            drv->priv = device_add_inst(&ym3806_ymfm_device, fm_dev_inst[fm_driver][chip_id]++);
            break;

#if 0
        case FM_YMF271: /* OPX */
            *drv      = ymfm_drv;
            drv->priv = device_add_inst(&ymf271_ymfm_device, fm_dev_inst[fm_driver][chip_id]++);
            break;
#endif

        case FM_YM2414: /* OPZ */
            *drv      = ymfm_drv;
            drv->priv = device_add_inst(&ym2414_ymfm_device, fm_dev_inst[fm_driver][chip_id]++);
            break;
        
        case FM_ESFM:
            *drv      = esfmu_opl_drv;
            drv->priv = device_add_inst(&esfm_esfmu_device, fm_dev_inst[fm_driver][chip_id]++);
            break;

#ifdef USE_LIBSERIALPORT
        case FM_OPL2BOARD:
            *drv      = ymfm_opl2board_drv;
            drv->priv = device_add_inst(&ym_opl2board_device, fm_dev_inst[fm_driver][chip_id]++);  
            break;
#endif

        default:
            return 0;
    }

    return 1;
};
