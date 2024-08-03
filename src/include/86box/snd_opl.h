/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          Definitions for the OPL interface.
 *
 * Authors: Fred N. van Kempen, <decwiz@yahoo.com>
 *          Miran Grca, <mgrca8@gmail.com>
 *
 *          Copyright 2017-2020 Fred N. van Kempen.
 *          Copyright 2016-2020 Miran Grca.
 */
#ifndef SOUND_OPL_H
#define SOUND_OPL_H

enum fm_type {
    FM_YM3812  = 0, /* OPL2 */
    FM_YMF262  = 1, /* OPL3 */
    FM_YMF289B = 2, /* OPL3-L */
    FM_YMF278B = 3, /* OPL 4 */
    FM_ESFM    = 4, /* ESFM */
    FM_MAX     = 5
};

enum fm_driver {
    FM_DRV_NUKED = 0,
    FM_DRV_YMFM  = 1,
    FM_DRV_MAX   = 2
};

typedef struct fm_drv_t {
    uint8_t  (*read)(uint16_t port, void *priv);
    void     (*write)(uint16_t port, uint8_t val, void *priv);
    int32_t *(*update)(void *priv);
    void     (*reset_buffer)(void *priv);
    void     (*set_do_cycles)(void *priv, int8_t do_cycles);
    void      *priv;
    void     (*generate)(void *priv, int32_t *data, uint32_t num_samples); /* daughterboard only. */
} fm_drv_t;

extern uint8_t fm_driver_get(int chip_id, fm_drv_t *drv);

extern const fm_drv_t nuked_opl_drv;
extern const fm_drv_t ymfm_drv;
extern const fm_drv_t esfmu_opl_drv;

#ifdef EMU_DEVICE_H
extern const device_t ym3812_nuked_device;
extern const device_t ymf262_nuked_device;

extern const device_t ym3812_ymfm_device;
extern const device_t ymf262_ymfm_device;
extern const device_t ymf289b_ymfm_device;
extern const device_t ymf278b_ymfm_device;

extern const device_t esfm_esfmu_device;
#endif

#endif /*SOUND_OPL_H*/
