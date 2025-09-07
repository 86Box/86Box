/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          Definitions for AD1848 / CS4248 / CS4231 (Windows Sound System) codec emulation.
 *
 *
 *
 * Authors: Sarah Walker, <https://pcem-emulator.co.uk/>
 *          TheCollector1995, <mariogplayer@gmail.com>
 *          RichardG, <richardg867@gmail.com>
 *
 *          Copyright 2008-2020 Sarah Walker.
 *          Copyright 2018-2020 TheCollector1995.
 *          Copyright 2021-2025 RichardG.
 */

#ifndef SOUND_AD1848_H
#define SOUND_AD1848_H

enum {
    AD1848_TYPE_DEFAULT = 0,
    AD1848_TYPE_CS4248  = 1,
    AD1848_TYPE_CS4231  = 2,
    AD1848_TYPE_CS4232  = 3,
    AD1848_TYPE_CS4236  = 4,
    AD1848_TYPE_CS4236B = 5,
    AD1848_TYPE_CS4235  = 6
};

typedef struct ad1848_t {
    uint8_t type;
    uint8_t index;
    uint8_t xindex;
    uint8_t regs[32];
    uint8_t xregs[32];
    uint8_t status; /* 16 original registers + 16 CS4231A extensions + 32 CS4236 extensions */

    int     count;
    uint8_t trd;
    uint8_t mce;
    uint8_t wten : 1;

    int16_t out_l;
    int16_t out_r;
    double  cd_vol_l;
    double  cd_vol_r;
    int     fm_vol_l;
    int     fm_vol_r;
    uint8_t fmt_mask;
    uint8_t wave_vol_mask;

    uint8_t enable : 1;
    uint8_t irq    : 4;
    uint8_t dma    : 3;
    uint8_t adpcm_ref;
    int8_t  adpcm_step;
    int     freq;
    int     adpcm_data;
    int     adpcm_pos;

    uint8_t  dma_ff;
    uint32_t dma_data;

    pc_timer_t timer_count;
    uint64_t   timer_latch;

    int16_t buffer[SOUNDBUFLEN * 2];
    int     pos;

    void   *cram_priv;
    void   (*cram_write)(uint16_t addr, uint8_t val, void *priv);
    uint8_t (*cram_read)(uint16_t addr, void *priv);
} ad1848_t;

extern void ad1848_setirq(ad1848_t *ad1848, int irq);
extern void ad1848_setdma(ad1848_t *ad1848, int dma);
extern void ad1848_updatevolmask(ad1848_t *ad1848);

extern uint8_t ad1848_read(uint16_t addr, void *priv);
extern void    ad1848_write(uint16_t addr, uint8_t val, void *priv);

extern void ad1848_update(ad1848_t *ad1848);
extern void ad1848_speed_changed(ad1848_t *ad1848);
extern void ad1848_filter_cd_audio(int channel, double *buffer, void *priv);
extern void ad1848_filter_aux2(void* priv, double* out_l, double* out_r);

extern void ad1848_init(ad1848_t *ad1848, uint8_t type);

#endif /*SOUND_AD1848_H*/
