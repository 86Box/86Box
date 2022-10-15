/*
 * 86Box	A hypervisor and IBM PC system emulator that specializes in
 *		running old operating systems and software designed for IBM
 *		PC systems and compatibles from 1981 through fairly recent
 *		system designs based on the PCI bus.
 *
 *		This file is part of the 86Box distribution.
 *
 *		Definitions for the OPL interface.
 *
 * Authors:	Fred N. van Kempen, <decwiz@yahoo.com>
 *		Miran Grca, <mgrca8@gmail.com>
 *
 *		Copyright 2017-2020 Fred N. van Kempen.
 *		Copyright 2016-2020 Miran Grca.
 */
#ifndef SOUND_OPL_H
#define SOUND_OPL_H

enum fm_type {
    FM_YM3812 = 0,
    FM_YMF262,
    FM_YMF289B,
    FM_MAX
};

enum fm_driver {
    FM_DRV_NUKED = 0,
    FM_DRV_YMFM,
    FM_DRV_MAX
};

typedef struct {
    uint8_t (*read)(uint16_t port, void *priv);
    void (*write)(uint16_t port, uint8_t val, void *priv);
    int32_t *(*update)(void *priv);
    void (*reset_buffer)(void *priv);
    void (*set_do_cycles)(void *priv, int8_t do_cycles);
    void *priv;
} fm_drv_t;

extern uint8_t fm_driver_get(int chip_id, fm_drv_t *drv);

extern const fm_drv_t nuked_opl_drv;
extern const fm_drv_t ymfm_drv;

#ifdef EMU_DEVICE_H
extern const device_t ym3812_nuked_device;
extern const device_t ymf262_nuked_device;

extern const device_t ym3812_ymfm_device;
extern const device_t ymf262_ymfm_device;
extern const device_t ymf289b_ymfm_device;
#endif

#endif /*SOUND_OPL_H*/
