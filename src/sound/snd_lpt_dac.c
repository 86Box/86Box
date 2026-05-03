/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          Emulation of Covox and Covox-like LPT sound devices.
 *
 * Authors: Sarah Walker, <https://pcem-emulator.co.uk/>
 *          Miran Grca, <mgrca8@gmail.com>
 *          Jasmine Iwanek, <jriwanek@gmail.com>
 *
 *          Copyright 2008-2018 Sarah Walker.
 *          Copyright 2016-2025 Miran Grca.
 *          Copyright 2025-2026 Jasmine Iwanek.
 */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>

#include "cpu.h"
#include <86box/86box.h>
#include <86box/filters.h>
#include <86box/timer.h>
#include <86box/device.h>
#include <86box/lpt.h>
#include <86box/machine.h>
#include <86box/sound.h>
#include <86box/plat_unused.h>

enum {
    DAC_TYPE_COVOX   = 0,
    DAC_TYPE_STEREO  = 1,
    DAC_TYPE_FTL     = 2,
    DAC_TYPE_SOUNDJR = 3
};

typedef struct lpt_dac_t {
    void *lpt;

    uint8_t dac_val_l;
    uint8_t dac_val_r;

    uint8_t type;
    uint8_t channel;
    uint8_t ctrl;
    float   gain;

    int16_t  buffer[2][SOUNDBUFLEN];
    uint16_t pos;
} lpt_dac_t;

static void
dac_update(lpt_dac_t *const lpt_dac)
{
    float vol = (lpt_dac->type == DAC_TYPE_SOUNDJR) ? lpt_dac->gain : 1.0f;

    const int16_t sample_l = (int8_t) (lpt_dac->dac_val_l ^ 0x80) * 0x40 * vol;
    const int16_t sample_r = (int8_t) (lpt_dac->dac_val_r ^ 0x80) * 0x40 * vol;

    for (; lpt_dac->pos < sound_pos_global; lpt_dac->pos++) {
        lpt_dac->buffer[0][lpt_dac->pos] = sample_l;
        lpt_dac->buffer[1][lpt_dac->pos] = sample_r;
    }
}

static void
dac_write_data(uint8_t val, void *priv)
{
    lpt_dac_t *const lpt_dac = (lpt_dac_t *) priv;

    if (lpt_dac->type == DAC_TYPE_STEREO) {
        if (lpt_dac->channel)
            lpt_dac->dac_val_r = val;
        else
            lpt_dac->dac_val_l = val;
    } else
        lpt_dac->dac_val_l = lpt_dac->dac_val_r = val;

    dac_update(lpt_dac);
}

static void
dac_strobe(UNUSED(uint8_t old), uint8_t val, void *priv)
{
    lpt_dac_t *const lpt_dac = (lpt_dac_t *) priv;

    if (lpt_dac->type == DAC_TYPE_STEREO)
        lpt_dac->channel = val;
}

static void
dac_write_ctrl(uint8_t val, void *priv)
{
    lpt_dac_t *const lpt_dac = (lpt_dac_t *) priv;

    if ((lpt_dac->type == DAC_TYPE_FTL) || (lpt_dac->type == DAC_TYPE_SOUNDJR))
        lpt_dac->ctrl = val;

    if (lpt_dac->type == DAC_TYPE_SOUNDJR) {
        /* SoundJr volume is bits 1, 2 & 3 of the Control Register.
           We extract them to get a value 0-7. */
        uint8_t vol_bits = (val >> 1) & 0x07;

        /* Map 0-7 to a gain multiplier. */
        lpt_dac->gain = (float) vol_bits / 7.0f;

        dac_update(lpt_dac);
    }

    if (lpt_dac->type == DAC_TYPE_STEREO)
        lpt_dac->channel = val & 0x01;
}

static uint8_t
dac_read_status(void *priv)
{
    lpt_dac_t *const lpt_dac = (lpt_dac_t *) priv;

    uint8_t status = 0x0f;

    /* Loopback Pin 17 -> Pin 12
       Control bit 3 (Select In) is inverted. 
       If ctrl bit 3 is 0, physical Pin 17 is High (+5v).
       If Pin 17 is High, Pin 12 (Paper Out) reads as High (Status bit 5).
    */
    if (lpt_dac->type == DAC_TYPE_FTL) {
        if (!(lpt_dac->ctrl & 0x08))
            status |= 0x20; // Set bit 5 (Paper Out)
        else
            status &= ~0x20; // Clear bit 5
    }

    return status;
}

static void
dac_get_buffer(int32_t *buffer, int len, void *priv)
{
    lpt_dac_t *const lpt_dac = (lpt_dac_t *) priv;

    dac_update(lpt_dac);

    for (int c = 0; c < len; c++) {
        buffer[c * 2] += dac_iir(0, lpt_dac->buffer[0][c]);
        buffer[c * 2 + 1] += dac_iir(1, lpt_dac->buffer[1][c]);
    }
    lpt_dac->pos = 0;
}

static void *
dac_init(UNUSED(const device_t *info))
{
    lpt_dac_t *const lpt_dac = calloc(1, sizeof(lpt_dac_t));

    lpt_dac->type = info->local;
    lpt_dac->gain = 1.0f; // Initial volume for SoundJr/Covox
#if 0
    switch (info->local) {
        case DAC_TYPE_STEREO:
            lpt_dac->is_stereo = 1;
            lpt_dac->is_ftl    = 0;
            break;
        case DAC_TYPE_FTL:
            lpt_dac->is_stereo = 0;
            lpt_dac->is_ftl    = 1;
            break;
        case DAC_TYPE_SOUNDJR:
            lpt_dac->is_soundjr = 1;
            break;
        case DAC_TYPE_COVOX:
        default:
            lpt_dac->is_stereo = 0;
            lpt_dac->is_ftl    = 0;
            break;
    }
#endif

    lpt_dac->lpt = lpt_attach(dac_write_data, dac_write_ctrl, dac_strobe, dac_read_status, NULL, NULL, NULL, lpt_dac);

    sound_add_handler(dac_get_buffer, lpt_dac);

    return lpt_dac;
}

static void
dac_close(void *priv)
{
    lpt_dac_t *const lpt_dac = (lpt_dac_t *) priv;

    free(lpt_dac);
}

const device_t lpt_dac_device = {
    .name          = "LPT DAC / Covox Speech Thing",
    .internal_name = "lpt_dac",
    .flags         = DEVICE_LPT,
    .local         = DAC_TYPE_COVOX,
    .init          = dac_init,
    .close         = dac_close,
    .reset         = NULL,
    .available     = NULL,
    .speed_changed = NULL,
    .force_redraw  = NULL,
    .config        = NULL
};

const device_t lpt_dac_stereo_device = {
    .name          = "Stereo LPT DAC",
    .internal_name = "lpt_dac_stereo",
    .flags         = DEVICE_LPT,
    .local         = DAC_TYPE_STEREO,
    .init          = dac_init,
    .close         = dac_close,
    .reset         = NULL,
    .available     = NULL,
    .speed_changed = NULL,
    .force_redraw  = NULL,
    .config        = NULL
};

const device_t lpt_dac_ftl_device = {
    .name          = "FTL Sound Adapter",
    .internal_name = "lpt_dac_ftl",
    .flags         = DEVICE_LPT,
    .local         = DAC_TYPE_FTL,
    .init          = dac_init,
    .close         = dac_close,
    .reset         = NULL,
    .available     = NULL,
    .speed_changed = NULL,
    .force_redraw  = NULL,
    .config        = NULL
};

const device_t lpt_dac_soundjr_device = {
    .name          = "SiliconSoft SoundJr",
    .internal_name = "lpt_dac_soundjr",
    .flags         = DEVICE_LPT,
    .local         = DAC_TYPE_SOUNDJR,
    .init          = dac_init,
    .close         = dac_close,
    .reset         = NULL,
    .available     = NULL,
    .speed_changed = NULL,
    .force_redraw  = NULL,
    .config        = NULL
};
