/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          Emulation of Disney Sound Source.
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

typedef struct dss_t {
    void *lpt;

    uint8_t fifo[16];
    uint8_t read_idx;
    uint8_t write_idx;

    uint8_t dac_val;
    uint8_t status;

    pc_timer_t timer;

    int16_t  buffer[SOUNDBUFLEN];
    uint16_t pos;
} dss_t;

static void
dss_update(dss_t *const dss)
{
    for (; dss->pos < sound_pos_global; dss->pos++)
        dss->buffer[dss->pos] = (int8_t) (dss->dac_val ^ 0x80) * 0x40;
}

static void
dss_update_status(dss_t *dss)
{
    const uint8_t old = dss->status;

    dss->status &= ~0x40;

    if ((dss->write_idx - dss->read_idx) >= 16)
        dss->status |= 0x40;

    if ((old & 0x40) && !(dss->status & 0x40))
        lpt_irq(dss->lpt, 1);
}

static void
dss_write_data(uint8_t val, void *priv)
{
    dss_t *const dss = (dss_t *) priv;

    if ((dss->write_idx - dss->read_idx) < 16) {
        dss->fifo[dss->write_idx & 15] = val;
        dss->write_idx++;
        dss_update_status(dss);
    }
}

static void
dss_write_ctrl(UNUSED(uint8_t val), UNUSED(void *priv))
{
    //
}

static uint8_t
dss_read_status(void *priv)
{
    const dss_t *const dss = (dss_t *) priv;

    return dss->status | 0x0f;
}

static void
dss_get_buffer(int32_t *buffer, int len, void *priv)
{
    dss_t  *const dss = (dss_t *) priv;

    dss_update(dss);

    for (int c = 0; c < len * 2; c += 2) {
        int16_t fval = dss_iir((float) dss->buffer[c >> 1]);
        float val  = fval;

        buffer[c] += val;
        buffer[c + 1] += val;
    }

    dss->pos = 0;
}

static void
dss_callback(void *priv)
{
    dss_t *const dss = (dss_t *) priv;

    dss_update(dss);

    if ((dss->write_idx - dss->read_idx) > 0) {
        dss->dac_val = dss->fifo[dss->read_idx & 15];
        dss->read_idx++;
        dss_update_status(dss);
    }

    timer_advance_u64(&dss->timer, (TIMER_USEC * (1000000.0 / 7000.0)));
}

static void *
dss_init(UNUSED(const device_t *info))
{
    dss_t *const dss = calloc(1, sizeof(dss_t));

    dss->lpt = lpt_attach(dss_write_data, dss_write_ctrl, NULL, dss_read_status, NULL, NULL, NULL, dss);

    sound_add_handler(dss_get_buffer, dss);
    timer_add(&dss->timer, dss_callback, dss, 1);

    return dss;
}
static void
dss_close(void *priv)
{
    dss_t *const dss = (dss_t *) priv;

    free(dss);
}

const device_t dss_device = {
    .name          = "Disney Sound Source",
    .internal_name = "dss",
    .flags         = DEVICE_LPT,
    .local         = 0,
    .init          = dss_init,
    .close         = dss_close,
    .reset         = NULL,
    .available     = NULL,
    .speed_changed = NULL,
    .force_redraw  = NULL,
    .config        = NULL
};
