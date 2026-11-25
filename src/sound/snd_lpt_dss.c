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
#ifdef ENABLE_LPT_DSS_LOG
#include <stdarg.h>
#endif
#include <math.h>
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
#include <86box/fifo.h>
#include <86box/fifo8.h>
#include <86box/log.h>

#ifdef ENABLE_LPT_DSS_LOG
uint8_t lpt_dss_do_log = ENABLE_LPT_DSS_LOG;

static void
lpt_dss_log(void *priv, const char *fmt, ...)
{
    va_list ap;

    if (lpt_dss_do_log) {
        va_start(ap, fmt);
        log_out(priv, fmt, ap);
        va_end(ap);
    }
}
#else
#    define lpt_dss_log(priv, fmt, ...)
#endif

typedef struct dss_s {
    void *lpt;

    Fifo8 dss_fifo;

    uint8_t dac_val;

    uint8_t status;

    pc_timer_t timer;

    int16_t  buffer[SOUNDBUFLEN];
    uint16_t pos;

    void *log;

    void      *source;
    const int *clock;

    /* 3.2 kHz low-pass, designed for the rate the source runs at. */
    int    iir_rate;
    float  iir_a0;
    float  iir_a1;
    float  iir_b1;
    float  iir_b2;
    float  iir_x[2];
    float  iir_y[2];
} dss_t;

#ifdef ENABLE_LPT_DSS_LOG
static uint32_t
dss_fifo_level(const dss_t *dss)
{
    return fifo8_num_used((Fifo8 *) &dss->dss_fifo);
}
#endif

static void
dss_update(dss_t *const dss)
{
    for (; dss->pos < SOUND_CLOCK(dss->clock, sound_pos_global); dss->pos++)
        dss->buffer[dss->pos] = (int8_t) (dss->dac_val ^ 0x80) * 0x40;
}

static void
dss_update_status(dss_t *dss)
{
    const uint8_t  old        = dss->status;
#ifdef ENABLE_LPT_DSS_LOG
    const uint32_t fifo_level = dss_fifo_level(dss);
#endif

    dss->status &= ~0x40;

    if (fifo8_is_full(&dss->dss_fifo)) {
        dss->status |= 0x40;
        lpt_dss_log(dss->log, "FIFO full (level=%u)\n", fifo_level);
    }

    if ((old & 0x40) && !(dss->status & 0x40)) {
        lpt_dss_log(dss->log, "DSS: IRQ assert (level=%u)\n", fifo_level);
        lpt_irq(dss->lpt, 1);
    }
}

static void
dss_write_data(const uint8_t val, void *priv)
{
    dss_t *const dss = (dss_t *) priv;

#ifdef ENABLE_LPT_DSS_LOG
    const uint32_t fifo_level = dss_fifo_level(dss);
#endif

    if (!fifo8_is_full(&dss->dss_fifo)) {
        fifo8_push(&dss->dss_fifo, val);
        lpt_dss_log(dss->log, "Write 0x%02x to FIFO (level=%u)\n",
                    val, dss_fifo_level(dss));
        dss_update_status(dss);
    } else {
        lpt_dss_log(dss->log, "Write dropped (FIFO full level=%u) val=0x%02x\n", fifo_level, val);
    }
}

static void
dss_write_ctrl(UNUSED(const uint8_t val), UNUSED(void *priv))
{
#ifdef ENABLE_LPT_DSS_LOG
    const dss_t *const dss = (const dss_t *) priv;

    lpt_dss_log(dss->log, "write_ctrl: ignored val=0x%02x\n", val);
#endif
}

static uint8_t
dss_read_status(void *priv)
{
    const dss_t *const dss = (dss_t *) priv;

    return dss->status | 0x0f;
}

static void
dss_iir_design(dss_t *const dss, const int rate)
{
    const double k = tan(M_PI * 3200.0 / (double) rate);
    const double q = 1.4142135623730951;
    const double n = 1.0 / (1.0 + (q * k) + (k * k));

    dss->iir_rate = rate;
    dss->iir_a0   = (float) (k * k * n);
    dss->iir_a1   = (float) (2.0 * k * k * n);
    dss->iir_b1   = (float) (2.0 * ((k * k) - 1.0) * n);
    dss->iir_b2   = (float) ((1.0 - (q * k) + (k * k)) * n);
}

static float
dss_filter(dss_t *const dss, const float in)
{
    const float out = (dss->iir_a0 * in) + (dss->iir_a1 * dss->iir_x[0]) + (dss->iir_a0 * dss->iir_x[1]) -
                      (dss->iir_b1 * dss->iir_y[0]) - (dss->iir_b2 * dss->iir_y[1]);

    dss->iir_x[1] = dss->iir_x[0];
    dss->iir_x[0] = in;
    dss->iir_y[1] = dss->iir_y[0];
    dss->iir_y[0] = out;

    return out;
}

static void
dss_get_buffer(int32_t *const buffer, const uint16_t len, void *priv)
{
    dss_t *const dss = (dss_t *) priv;

    const int rate = sound_source_get_rate(dss->source);

    dss_update(dss);

    if (rate != dss->iir_rate)
        dss_iir_design(dss, rate);

    for (uint16_t c = 0; c < len * 2; c += 2) {
        const float sample = dss_filter(dss, (float) dss->buffer[c >> 1]);

        buffer[c] += (int32_t) sample;
        buffer[c + 1] += (int32_t) sample;
    }

    dss->pos = 0;
}

static void
dss_callback(void *priv)
{
    dss_t *const dss = (dss_t *) priv;
    const uint64_t sample_period = (TIMER_USEC * 1000000ULL) / 7000ULL;

    dss_update(dss);

    if (!fifo8_is_empty(&dss->dss_fifo)) {
        dss->dac_val = fifo8_pop(&dss->dss_fifo);
        lpt_dss_log(dss->log, "Output dac_val=0x%02x from FIFO (level=%u)\n",
                    dss->dac_val, dss_fifo_level(dss));
        dss_update_status(dss);

        /* Let the host source run at the fixed 7 kHz sample rate while playing. */
        sound_source_set_rate(dss->source, 7000.0);
    }

    timer_advance_u64(&dss->timer, sample_period);
}

static void *
dss_init(UNUSED(const device_t *info))
{
    dss_t *const dss = calloc(1, sizeof(dss_t));

    dss->lpt = lpt_attach(dss_write_data,
                          dss_write_ctrl,
                          NULL,
                          dss_read_status,
                          NULL,
                          NULL,
                          NULL,
                          dss);

    dss_iir_design(dss, SOUND_FREQ);
    dss->source = sound_source_add_stream(I_NORMAL, dss_get_buffer, dss);
    dss->clock  = sound_source_get_pos_ptr(dss->source);
    timer_add(&dss->timer, dss_callback, dss, 1);
    fifo8_create(&dss->dss_fifo, 16);

    dss->log = log_open("DSS");
    lpt_dss_log(dss->log, "Init sample_hz=7000 fifo_depth=%u\n", dss->dss_fifo.capacity);

    return dss;
}
static void
dss_close(void *priv)
{
    dss_t *const dss = (dss_t *) priv;

    fifo8_destroy(&dss->dss_fifo);
    lpt_dss_log(dss->log, "Close\n");
    if (dss->log)
        log_close(dss->log);

    sound_source_remove(dss->source);
    free(dss);
}

const device_t dss_device = {
    .name          = "Disney Sound Source",
    .internal_name = "dss",
    .flags         = DEVICE_LPT | DEVICE_HOTPLUG_IN,
    .local         = 0,
    .init          = dss_init,
    .close         = dss_close,
    .reset         = NULL,
    .available     = NULL,
    .speed_changed = NULL,
    .force_redraw  = NULL,
    .config        = NULL
};
