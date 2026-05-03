/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          tndlpt emulation.
 *
 * Authors: Jasmine Iwanek, <jriwanek@gmail.com>
 *
 *          Copyright 2025-2026 Jasmine Iwanek.
 */
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <wchar.h>
#define HAVE_STDARG_H
#include <86box/86box.h>
#include <86box/device.h>
#include <86box/io.h>
#include <86box/sound.h>
#include <86box/snd_sn76489.h>
#include <86box/timer.h>
#include <86box/lpt.h>
#include <86box/plat_unused.h>

#ifdef ENABLE_TNDLPT_LOG
uint8_t tndlpt_do_log = ENABLE_TNDLPT_LOG;

static void
tndlpt_log(const char *fmt, ...)
{
    va_list ap;

    if (tndlpt_do_log) {
        va_start(ap, fmt);
        pclog_ex(fmt, ap);
        va_end(ap);
    }
}
#else
#    define tndlpt_log(fmt, ...)
#endif

typedef struct tndlpt_s {
    void      *lpt;
    sn76489_t *sn76489;
    uint8_t    control;
    uint8_t    data_latch;
    uint8_t    status;
    pc_timer_t ready_timer;
} tndlpt_t;

static void
tndlpt_close(void *priv)
{
    tndlpt_t *tndlpt = (tndlpt_t *) priv;

    tndlpt_log("tndlpt_close\n");

    free(tndlpt->sn76489);
    timer_disable(&tndlpt->ready_timer);
    free(tndlpt);
}

static void
tndlpt_write_data(const uint8_t val, void *priv)
{
    tndlpt_t *const tndlpt = (tndlpt_t *) priv;

    tndlpt_log("tndlpt_write_data: val=%02x\n", val);

    tndlpt->data_latch = val;
}

static void
tndlpt_write_ctrl(const uint8_t val, UNUSED(void *priv))
{
    tndlpt_t *const tndlpt = (tndlpt_t *) priv;
    const uint8_t   prev   = tndlpt->control;

    tndlpt_log("tndlpt_write_ctrl: val=%02x\n", val);

    tndlpt->control = val;

    tndlpt->status &= ~0x40; /* busy */
    timer_set_delay_u64(&tndlpt->ready_timer, 2ULL * TIMER_USEC);

    if (!(prev & 0x01) && (val & 0x01)) {
        tndlpt_log("tndlpt_write_ctrl: Triggering PSG write (Data=%02x)\n", tndlpt->data_latch);
        sn76489_write(0, tndlpt->data_latch, tndlpt->sn76489);
    }
}

static void
tndlpt_ready_cb(void *priv)
{
    tndlpt_t *const tndlpt = (tndlpt_t *) priv;

    tndlpt->status |= 0x40;
    lpt_irq(tndlpt->lpt, 1);
}

static uint8_t
tndlpt_read_status(void *priv)
{
    const tndlpt_t *const tndlpt = (tndlpt_t *) priv;

    return (tndlpt->status & 0xf8) | 0x0f;
}

static void *
tndlpt_init(UNUSED(const device_t *info))
{
    tndlpt_t *const tndlpt = calloc(1, sizeof(tndlpt_t));

    tndlpt->lpt = lpt_attach(tndlpt_write_data,
                             tndlpt_write_ctrl,
                             NULL,
                             tndlpt_read_status,
                             NULL,
                             NULL,
                             NULL,
                             tndlpt);

    tndlpt_log("tndlpt_init\n");

    tndlpt->sn76489 = calloc(1, sizeof(sn76489_t));

    /* Tandy/PCjr standard clock and type */
    sn76489_init(tndlpt->sn76489, 0, 0, SN76496, 3579545);

    tndlpt->status = 0x40;
    memset(&tndlpt->ready_timer, 0x00, sizeof(pc_timer_t));
    timer_add(&tndlpt->ready_timer, tndlpt_ready_cb, tndlpt, 0);

    return tndlpt;
}

const device_t lpt_tnd_device = {
    .name          = "Tandy-on-LPT (TNDLPT)",
    .internal_name = "lpt_tndlpt",
    .flags         = DEVICE_LPT,
    .local         = 0,
    .init          = tndlpt_init,
    .close         = tndlpt_close,
    .reset         = NULL,
    .available     = NULL,
    .speed_changed = NULL,
    .force_redraw  = NULL,
    .config        = NULL
};
