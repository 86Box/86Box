/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          ESFMu ESFM emulator.
 * 
 * 
 * Authors: Fred N. van Kempen, <decwiz@yahoo.com>
 *          Miran Grca, <mgrca8@gmail.com>
 *          Alexey Khokholov (Nuke.YKT)
 *          Cacodemon345
 *
 *          Copyright 2017-2020 Fred N. van Kempen.
 *          Copyright 2016-2020 Miran Grca.
 *          Copyright 2013-2018 Alexey Khokholov (Nuke.YKT)
 *          Copyright 2024 Cacodemon345
 */

#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "esfmu/esfm.h"

#define HAVE_STDARG_H
#define NO_SOFTFLOAT_INCLUDE
#include <86box/86box.h>
#include <86box/sound.h>
#include <86box/device.h>
#include <86box/timer.h>
#include <86box/snd_opl.h>

#define RSM_FRAC    10

enum {
    FLAG_CYCLES = 0x02,
    FLAG_OPL3   = 0x01
};

typedef struct {
    esfm_chip opl;
    int8_t  flags;
    int8_t  pad;

    uint16_t port;
    uint8_t  status;
    uint8_t  timer_ctrl;
    uint16_t timer_count[2];
    uint16_t timer_cur_count[2];

    // OPL3L
    int32_t rateratio;
    int32_t samplecnt;
    int32_t oldsamples[2];
    int32_t samples[2];

    int     pos;
    int32_t buffer[SOUNDBUFLEN * 2];
} esfm_drv_t;

void
esfm_drv_generate_resampled(esfm_drv_t *dev, int32_t *bufp)
{
    while (dev->samplecnt >= dev->rateratio) {
        int16_t samples[2] = { 0, 0 };
        dev->oldsamples[0] = dev->samples[0];
        dev->oldsamples[1] = dev->samples[1];
        ESFM_generate(&dev->opl, samples);
        dev->samples[0] = samples[0];
        dev->samples[1] = samples[1];
        dev->samplecnt -= dev->rateratio;
    }

    bufp[0] = (int32_t) ((dev->oldsamples[0] * (dev->rateratio - dev->samplecnt)
                          + dev->samples[0] * dev->samplecnt)
                         / dev->rateratio);
    bufp[1] = (int32_t) ((dev->oldsamples[1] * (dev->rateratio - dev->samplecnt)
                          + dev->samples[1] * dev->samplecnt)
                         / dev->rateratio);

    dev->samplecnt += 1 << RSM_FRAC;
}

void
esfm_drv_generate_stream(esfm_drv_t *dev, int32_t *sndptr, uint32_t num)
{
    for (uint32_t i = 0; i < num; i++) {
        esfm_drv_generate_resampled(dev, sndptr);
        sndptr += 2;
    }
}

static void
esfm_drv_set_do_cycles(void *priv, int8_t do_cycles)
{
    esfm_drv_t *dev = (esfm_drv_t *) priv;

    if (do_cycles)
        dev->flags |= FLAG_CYCLES;
    else
        dev->flags &= ~FLAG_CYCLES;
}

static void *
esfm_drv_init(const device_t *info)
{
    esfm_drv_t *dev = (esfm_drv_t *) calloc(1, sizeof(esfm_drv_t));
    dev->flags       = FLAG_CYCLES | FLAG_OPL3;

    /* Initialize the ESFMu object. */
    ESFM_init(&dev->opl);
    dev->rateratio = (SOUND_FREQ << RSM_FRAC) / 49716;

    return dev;
}

static void
esfm_drv_close(void *priv)
{
    esfm_drv_t *dev = (esfm_drv_t *) priv;
    free(dev);
}

static int32_t *
esfm_drv_update(void *priv)
{
    esfm_drv_t *dev = (esfm_drv_t *) priv;

    if (dev->pos >= sound_pos_global)
        return dev->buffer;

    esfm_drv_generate_stream(dev,
                          &dev->buffer[dev->pos * 2],
                          sound_pos_global - dev->pos);

    for (; dev->pos < sound_pos_global; dev->pos++) {
        dev->buffer[dev->pos * 2] /= 2;
        dev->buffer[(dev->pos * 2) + 1] /= 2;
    }

    return dev->buffer;
}


static void
esfm_drv_reset_buffer(void *priv)
{
    esfm_drv_t *dev = (esfm_drv_t *) priv;

    dev->pos = 0;
}

static uint8_t
esfm_drv_read(uint16_t port, void *priv)
{
    esfm_drv_t *dev = (esfm_drv_t *) priv;

    if (dev->flags & FLAG_CYCLES)
        cycles -= ((int) (isa_timing * 8));

    esfm_drv_update(dev);

    uint8_t ret = 0xff;

    ret = ESFM_read_port(&dev->opl, port & 3);

    return ret;
}

static void
esfm_drv_write(uint16_t port, uint8_t val, void *priv)
{
    esfm_drv_t *dev = (esfm_drv_t *) priv;

    if (dev->flags & FLAG_CYCLES)
        cycles -= ((int) (isa_timing * 8));

    esfm_drv_update(dev);

    if (dev->opl.native_mode) {
        if ((port & 3) == 1) {
            ESFM_write_reg_buffered_fast(&dev->opl, dev->opl.addr_latch, val);
        } else {
            ESFM_write_port(&dev->opl, port & 3, val);
        }
    } else {
        if ((port & 3) == 1 || (port & 3) == 3) {
            ESFM_write_reg_buffered_fast(&dev->opl, dev->opl.addr_latch, val);
        } else {
            ESFM_write_port(&dev->opl, port & 3, val);
        }
    }
}

const device_t esfm_esfmu_device = {
    .name          = "ESS Technology ESFM (ESFMu)",
    .internal_name = "ymf262_nuked",
    .flags         = 0,
    .local         = FM_YMF262,
    .init          = esfm_drv_init,
    .close         = esfm_drv_close,
    .reset         = NULL,
    { .available = NULL },
    .speed_changed = NULL,
    .force_redraw  = NULL,
    .config        = NULL
};

const fm_drv_t nuked_opl_drv = {
    &esfm_drv_read,
    &esfm_drv_write,
    &esfm_drv_update,
    &esfm_drv_reset_buffer,
    &esfm_drv_set_do_cycles,
    NULL,
    NULL,
};
