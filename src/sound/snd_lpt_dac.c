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
#ifdef ENABLE_LPT_DAC_LOG
#include <stdarg.h>
#endif
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <86box/86box.h>
#include <86box/filters.h>
#include <86box/timer.h>
#include <86box/device.h>
#include <86box/lpt.h>
#include <86box/sound.h>
#include <86box/plat_unused.h>
#include <86box/log.h>

#ifdef ENABLE_LPT_DAC_LOG
uint8_t lpt_dac_do_log = ENABLE_LPT_DAC_LOG;

static void
lpt_dac_log(void *priv, const char *fmt, ...)
{
    va_list ap;

    if (lpt_dac_do_log) {
        va_start(ap, fmt);
        log_out(priv, fmt, ap);
        va_end(ap);
    }
}
#else
#    define lpt_dac_log(priv, fmt, ...)
#endif

enum {
    DAC_TYPE_COVOX   = 0,
    DAC_TYPE_STEREO  = 1,
    DAC_TYPE_FTL     = 2,
    DAC_TYPE_SOUNDJR = 3
};

enum {
    DAC_CHANNEL_LEFT  = 0,
    DAC_CHANNEL_RIGHT = 1,
    DAC_CHANNEL_BOTH  = 2
};

typedef struct lpt_dac_s {
    void *lpt;

    uint8_t dac_val_l;
    uint8_t dac_val_r;

    uint8_t type;
    uint8_t channel;
    uint8_t ctrl;
    float   gain;

    int16_t  buffer[2][SOUNDBUFLEN];
    uint16_t pos;

    void *log;
} lpt_dac_t;

static void
dac_update(lpt_dac_t *const lpt_dac)
{
    const float vol = (lpt_dac->type == DAC_TYPE_SOUNDJR) ? lpt_dac->gain : 1.0f;

    const int16_t sample_l = (int16_t) ((int8_t) (lpt_dac->dac_val_l ^ 0x80) * 0x40 * vol);
    const int16_t sample_r = (int16_t) ((int8_t) (lpt_dac->dac_val_r ^ 0x80) * 0x40 * vol);

    for (; lpt_dac->pos < sound_pos_global; lpt_dac->pos++) {
        lpt_dac->buffer[DAC_CHANNEL_LEFT][lpt_dac->pos] = sample_l;
        lpt_dac->buffer[DAC_CHANNEL_RIGHT][lpt_dac->pos] = sample_r;
    }
}

static void
dac_write_data(const uint8_t val, void *priv)
{
    lpt_dac_t *const lpt_dac = (lpt_dac_t *) priv;

    if (lpt_dac->channel == DAC_CHANNEL_RIGHT) {
        lpt_dac->dac_val_r = val;
        lpt_dac_log(lpt_dac->log, "write_data: 0x%02x to right channel (channel=%u)\n", val, lpt_dac->channel);
    } else if (lpt_dac->channel == DAC_CHANNEL_LEFT) {
        lpt_dac->dac_val_l = val;
        lpt_dac_log(lpt_dac->log, "write_data: 0x%02x to left channel (channel=%u)\n", val, lpt_dac->channel);
    } else {
        lpt_dac->dac_val_l = lpt_dac->dac_val_r = val;
        lpt_dac_log(lpt_dac->log, "write_data: 0x%02x to both channels (channel=%u)\n", val, lpt_dac->channel);
    }

    dac_update(lpt_dac);
}

static void
dac_strobe(UNUSED(const uint8_t old), const uint8_t val, void *priv)
{
    lpt_dac_t *const lpt_dac = (lpt_dac_t *) priv;

    if (lpt_dac->type == DAC_TYPE_STEREO) {
        const uint8_t new_channel = val & 0x01;

#ifdef ENABLE_LPT_DAC_LOG
        lpt_dac->channel = val & 0x01;
        lpt_dac_log(lpt_dac->log, "strobe: ctrl %02x->%02x channel %u->%u\n",
                    old, val, lpt_dac->channel, new_channel);
#endif
        lpt_dac->channel = new_channel;
    }
}

static void
dac_write_ctrl(const uint8_t val, void *priv)
{
    lpt_dac_t *const lpt_dac = (lpt_dac_t *) priv;

    lpt_dac_log(lpt_dac->log, "write_ctrl: 0x%02x\n", val);

    if ((lpt_dac->type == DAC_TYPE_FTL) || (lpt_dac->type == DAC_TYPE_SOUNDJR))
        lpt_dac->ctrl = val;

    if (lpt_dac->type == DAC_TYPE_SOUNDJR) {
        /* SoundJr volume is bits 1, 2 & 3 of the Control Register.
           We extract them to get a value 0-7. */
        const uint8_t vol_bits = (val >> 1) & 0x07;

        /* Map 0-7 to a gain multiplier. */
        lpt_dac->gain = (float) vol_bits / 7.0f;

        lpt_dac_log(lpt_dac->log, "SoundJr vol_bits=%u gain=%f\n", vol_bits, lpt_dac->gain);

        dac_update(lpt_dac);
    }

    if (lpt_dac->type == DAC_TYPE_STEREO) {
        lpt_dac->channel = val & 0x01;
        lpt_dac_log(lpt_dac->log, "Channel set to %u\n", lpt_dac->channel);
    }
}

static uint8_t
dac_read_status(void *priv)
{
    const lpt_dac_t *const lpt_dac = (lpt_dac_t *) priv;

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
dac_get_buffer(int32_t *const buffer, const uint16_t len, void *priv)
{
    lpt_dac_t *const lpt_dac = (lpt_dac_t *) priv;

    dac_update(lpt_dac);

    for (uint16_t c = 0; c < len; c++) {
        const int32_t filtered_left = (int32_t) dac_iir(DAC_CHANNEL_LEFT, lpt_dac->buffer[DAC_CHANNEL_LEFT][c]);
        const int32_t filtered_right = (int32_t) dac_iir(DAC_CHANNEL_RIGHT, lpt_dac->buffer[DAC_CHANNEL_RIGHT][c]);

        buffer[c * 2] += filtered_left;
        buffer[c * 2 + 1] += filtered_right;
    }
    lpt_dac->pos = 0;
}

static void *
dac_init(const device_t *info)
{
    lpt_dac_t *const lpt_dac = calloc(1, sizeof(lpt_dac_t));

    lpt_dac->type = (uint8_t)info->local;
    lpt_dac->gain = 1.0f; // Initial volume for SoundJr/Covox
    if (lpt_dac->type != DAC_TYPE_STEREO)
        lpt_dac->channel = (uint8_t) device_get_config_int("channel");
    else
        lpt_dac->channel = DAC_CHANNEL_BOTH;

    lpt_dac->lpt = lpt_attach(dac_write_data,
                              dac_write_ctrl,
                              dac_strobe,
                              dac_read_status,
                              NULL,
                              NULL,
                              NULL,
                              lpt_dac);

    sound_add_handler(dac_get_buffer, lpt_dac);

    lpt_dac->log = log_open("LPT DAC");
    lpt_dac_log(lpt_dac->log, "Init device=%s channel=%u\n",
                (lpt_dac->type == DAC_TYPE_COVOX) ? "Covox" :
                (lpt_dac->type == DAC_TYPE_STEREO) ? "Stereo DAC" :
                (lpt_dac->type == DAC_TYPE_FTL) ? "FTL Sound Adapter" :
                                                  "SoundJr",
                lpt_dac->channel);

    return lpt_dac;
}

static void
dac_close(void *priv)
{
    lpt_dac_t *const lpt_dac = (lpt_dac_t *) priv;

    lpt_dac_log(lpt_dac->log, "Close\n");
    if (lpt_dac->log)
        log_close(lpt_dac->log);
    free(lpt_dac);
}

static const device_config_t lpt_dac_config[] = {
  // clang-format off
    {
        .name           = "channel",
        .description    = "Channel",
        .type           = CONFIG_INT,
        .default_string = NULL,
        .default_int    = DAC_CHANNEL_BOTH,
        .file_filter    = NULL,
        .spinner        = { 0 },
        .selection      = {
            { .description = "Both Channels", .value = DAC_CHANNEL_BOTH  },
            { .description = "Left Channel",  .value = DAC_CHANNEL_LEFT  },
            { .description = "Right Channel", .value = DAC_CHANNEL_RIGHT },
            { .description = ""                                      }
        },
        .bios           = { { 0 } }
    },
    { .name = "", .description = "", .type = CONFIG_END }
  // clang-format on
};

const device_t lpt_dac_device = {
    .name          = "LPT DAC / Covox Speech Thing",
    .internal_name = "lpt_dac",
    .flags         = DEVICE_LPT | DEVICE_HOTPLUG_IN,
    .local         = DAC_TYPE_COVOX,
    .init          = dac_init,
    .close         = dac_close,
    .reset         = NULL,
    .available     = NULL,
    .speed_changed = NULL,
    .force_redraw  = NULL,
    .config        = lpt_dac_config
};

const device_t lpt_dac_stereo_device = {
    .name          = "Stereo LPT DAC",
    .internal_name = "lpt_dac_stereo",
    .flags         = DEVICE_LPT | DEVICE_HOTPLUG_IN,
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
    .flags         = DEVICE_LPT | DEVICE_HOTPLUG_IN,
    .local         = DAC_TYPE_FTL,
    .init          = dac_init,
    .close         = dac_close,
    .reset         = NULL,
    .available     = NULL,
    .speed_changed = NULL,
    .force_redraw  = NULL,
    .config        = lpt_dac_config
};

const device_t lpt_dac_soundjr_device = {
    .name          = "SiliconSoft SoundJr",
    .internal_name = "lpt_dac_soundjr",
    .flags         = DEVICE_LPT | DEVICE_HOTPLUG_IN,
    .local         = DAC_TYPE_SOUNDJR,
    .init          = dac_init,
    .close         = dac_close,
    .reset         = NULL,
    .available     = NULL,
    .speed_changed = NULL,
    .force_redraw  = NULL,
    .config        = lpt_dac_config
};
