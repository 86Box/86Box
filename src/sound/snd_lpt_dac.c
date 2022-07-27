#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>

#include "cpu.h"
#include <86box/86box.h>
#include <86box/filters.h>
#include <86box/lpt.h>
#include <86box/machine.h>
#include <86box/sound.h>
#include <86box/timer.h>

typedef struct lpt_dac_t {
    void *lpt;

    uint8_t dac_val_l, dac_val_r;

    int is_stereo;
    int channel;

    int16_t buffer[2][SOUNDBUFLEN];
    int     pos;
} lpt_dac_t;

static void
dac_update(lpt_dac_t *lpt_dac)
{
    for (; lpt_dac->pos < sound_pos_global; lpt_dac->pos++) {
        lpt_dac->buffer[0][lpt_dac->pos] = (int8_t) (lpt_dac->dac_val_l ^ 0x80) * 0x40;
        lpt_dac->buffer[1][lpt_dac->pos] = (int8_t) (lpt_dac->dac_val_r ^ 0x80) * 0x40;
    }
}

static void
dac_write_data(uint8_t val, void *p)
{
    lpt_dac_t *lpt_dac = (lpt_dac_t *) p;

    if (lpt_dac->is_stereo) {
        if (lpt_dac->channel)
            lpt_dac->dac_val_r = val;
        else
            lpt_dac->dac_val_l = val;
    } else
        lpt_dac->dac_val_l = lpt_dac->dac_val_r = val;
    dac_update(lpt_dac);
}

static void
dac_write_ctrl(uint8_t val, void *p)
{
    lpt_dac_t *lpt_dac = (lpt_dac_t *) p;

    if (lpt_dac->is_stereo)
        lpt_dac->channel = val & 0x01;
}

static uint8_t
dac_read_status(void *p)
{
    return 0x0f;
}

static void
dac_get_buffer(int32_t *buffer, int len, void *p)
{
    lpt_dac_t *lpt_dac = (lpt_dac_t *) p;
    int        c;

    dac_update(lpt_dac);

    for (c = 0; c < len; c++) {
        buffer[c * 2] += dac_iir(0, lpt_dac->buffer[0][c]);
        buffer[c * 2 + 1] += dac_iir(1, lpt_dac->buffer[1][c]);
    }
    lpt_dac->pos = 0;
}

static void *
dac_init(void *lpt)
{
    lpt_dac_t *lpt_dac = malloc(sizeof(lpt_dac_t));
    memset(lpt_dac, 0, sizeof(lpt_dac_t));

    lpt_dac->lpt = lpt;

    sound_add_handler(dac_get_buffer, lpt_dac);

    return lpt_dac;
}

static void *
dac_stereo_init(void *lpt)
{
    lpt_dac_t *lpt_dac = dac_init(lpt);

    lpt_dac->is_stereo = 1;

    return lpt_dac;
}
static void
dac_close(void *p)
{
    lpt_dac_t *lpt_dac = (lpt_dac_t *) p;

    free(lpt_dac);
}

const lpt_device_t lpt_dac_device = {
    .name = "LPT DAC / Covox Speech Thing",
    .internal_name = "lpt_dac",
    .init = dac_init,
    .close = dac_close,
    .write_data = dac_write_data,
    .write_ctrl = dac_write_ctrl,
    .read_data = NULL,
    .read_status = dac_read_status,
    .read_ctrl = NULL
};

const lpt_device_t lpt_dac_stereo_device = {
    .name = "Stereo LPT DAC",
    .internal_name = "lpt_dac_stereo",
    .init = dac_stereo_init,
    .close = dac_close,
    .write_data = dac_write_data,
    .write_ctrl = dac_write_ctrl,
    .read_data = NULL,
    .read_status = dac_read_status,
    .read_ctrl = NULL
};
