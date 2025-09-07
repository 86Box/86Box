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
    FM_YM2149    =  0, /* SSG */
    FM_YM3526    =  1, /* OPL */
    FM_Y8950     =  2, /* MSX-Audio (OPL with ADPCM) */
    FM_YM3812    =  3, /* OPL2 */
    FM_YMF262    =  4, /* OPL3 */
    FM_YMF289B   =  5, /* OPL3-L */
    FM_YMF278B   =  6, /* OPL4 */
    FM_YM2413    =  7, /* OPLL */
    FM_YM2423    =  8, /* OPLL-X */
    FM_YMF281    =  9, /* OPLLP */
    FM_DS1001    = 10, /* Konami VRC7 MMC */
    FM_YM2151    = 11, /* OPM */
    FM_YM2203    = 12, /* OPN */
    FM_YM2608    = 13, /* OPNA */
    FM_YMF288    = 14, /* OPN3L */
    FM_YM2610    = 15, /* OPNB */
    FM_YM2610B   = 16, /* OPNB2 */
    FM_YM2612    = 17, /* OPN2 */
    FM_YM3438    = 18, /* OPN2C */
    FM_YMF276    = 19, /* OPN2L */
    FM_YM2164    = 20, /* OPP */
    FM_YM3806    = 21, /* OPQ */
#if 0
    FM_YMF271    = 22, /* OPX */
#endif
    FM_YM2414    = 23, /* OPZ */
    FM_ESFM      = 24, /* ESFM */
    FM_OPL2BOARD = 25, /* OPL2Board (External Device) */
    FM_MAX       = 26
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
extern const fm_drv_t ymfm_opl2board_drv;

#ifdef EMU_DEVICE_H
extern const device_t ym3812_nuked_device;
extern const device_t ymf262_nuked_device;

extern const device_t ym2149_ymfm_device;

/* OPL Series */
extern const device_t ym3526_ymfm_device;
extern const device_t y8950_ymfm_device;
extern const device_t ym3812_ymfm_device;
extern const device_t ymf262_ymfm_device;
extern const device_t ymf289b_ymfm_device;
extern const device_t ymf278b_ymfm_device;
extern const device_t ym2413_ymfm_device;
extern const device_t ym2423_ymfm_device;
extern const device_t ymf281_ymfm_device;
extern const device_t ds1001_ymfm_device;

/* OPM Series */
extern const device_t ym2151_ymfm_device;

/* OPN Series */
extern const device_t ym2203_ymfm_device;
extern const device_t ym2608_ymfm_device;
extern const device_t ymf288_ymfm_device;
extern const device_t ym2610_ymfm_device;
extern const device_t ym2610b_ymfm_device;
extern const device_t ym2612_ymfm_device;
extern const device_t ym3438_ymfm_device;
extern const device_t ymf276_ymfm_device;

/* OPP Series */
extern const device_t ym2164_ymfm_device;

/* OPQ Series */
extern const device_t ym3806_ymfm_device;

/* OPX Series */
#if 0
extern const device_t ymf271_ymfm_device;
#endif

/* OPZ Series */
extern const device_t ym2414_ymfm_device;

extern const device_t esfm_esfmu_device;

#ifdef USE_LIBSERIALPORT
extern const device_t ym_opl2board_device;
#endif

#endif

#endif /*SOUND_OPL_H*/
