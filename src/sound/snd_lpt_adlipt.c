/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          ADLiPT emulation.
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
#include <wchar.h>
#define HAVE_STDARG_H
#include <86box/86box.h>
#include <86box/device.h>
#include <86box/io.h>
#include <86box/mca.h>
#include <86box/sound.h>
#include <86box/snd_opl.h>
#include <86box/snd_adlib.h>
#include <86box/timer.h>
#include <86box/lpt.h>
#include <86box/snd_opl.h>
#include <86box/plat_unused.h>

#ifdef ENABLE_ADLIPT_LOG
uint8_t adlipt_do_log = ENABLE_ADLIPT_LOG;

static void
adlipt_log(const char *fmt, ...)
{
    va_list ap;

    if (adlipt_do_log) {
        va_start(ap, fmt);
        pclog_ex(fmt, ap);
        va_end(ap);
    }
}
#else
#    define adlipt_log(fmt, ...)
#endif

typedef struct adlipt_s {
    void      *lpt;
    adlib_t   *adlib;
    uint8_t    control;
    uint8_t    data_latch;
    uint8_t    status;
    pc_timer_t ready_timer;
} adlipt_t;

static void
adlipt_write_data(const uint8_t val, void *priv)
{
    adlipt_t *const adlipt = (adlipt_t *) priv;

    adlipt_log("adlipt_write_data: val=%02x\n", val);

    adlipt->data_latch = val;
}

static void
adlipt_write_ctrl(const uint8_t val, void *priv)
{
    adlipt_t *const adlipt = (adlipt_t *) priv;

    const uint8_t prev = adlipt->control;

#ifdef ENABLE_ADLIPT_LOG
    const int strobe = val & 0x01;
    const int autofeed = (val & 0x02) >> 1;
    const int init = (val & 0x04) >> 2;
#endif
    const int select = (val & 0x08) >> 3;

    adlipt_log("adlipt_write_ctrl: val=%02x (STROBE=%d AUTOFD=%d INIT=%d SELECT=%d)\n",
               val, strobe, autofeed, init, select);

    adlipt->control = val;

    adlipt->status &= ~0x40; /* busy */
    timer_set_delay_u64(&adlipt->ready_timer, 2ULL * TIMER_USEC);

    /* Trigger OPL write on INIT falling edge
       Use STROBE bit to select address vs data as encoded by client TSRs. */
    if ((prev & 0x04) && !(val & 0x04)) {
        adlib_t *const adlib = adlipt->adlib;
        const int is_addr = val & 0x01;
        uint16_t addr;

        /* PP_NOT_SELECT (bit3) set => low bank (OPL2) at 0x388/0x389.
           PP_NOT_SELECT cleared => high bank (OPL3) at 0x38A/0x38B. */
        if (select)
            addr = is_addr ? 0x388 : 0x389;
        else
            addr = is_addr ? 0x38A : 0x38B;

        adlipt_log("adlipt_write_ctrl: INIT->0 trigger OPL write. Addr=%04x Data=%02x Control=%02x\n",
                   addr, adlipt->data_latch, val);

        adlib->opl.write(addr, adlipt->data_latch, adlib->opl.priv);
    }
}

static void
adlipt_ready_cb(void *priv)
{
    adlipt_t *const adlipt = (adlipt_t *) priv;

    adlipt->status |= 0x40; /* ready */
    adlipt_log("adlipt_ready_cb: status=0x%02x\n", adlipt->status);
    lpt_irq(adlipt->lpt, 1);
}

static uint8_t
adlipt_read_status(void *priv)
{
    const adlipt_t *const adlipt = (adlipt_t *) priv;

    const uint8_t ret = (adlipt->status & 0xf8) | 0x0f;
    adlipt_log("adlipt_read_status: returning 0x%02x\n", ret);
    return ret;
}

static void
adlipt_reset_opl(adlipt_t *adlipt)
{
    adlib_t *const adlib = adlipt->adlib;

    adlipt_log("adlipt_reset_opl: resetting OPL registers\n");

    for (uint16_t i = 0; i < 256; i++) {
        /* Clear register i (OPL2 range) */
        adlib->opl.write(0x388, (uint8_t) i, adlib->opl.priv);
        adlib->opl.write(0x389, 0, adlib->opl.priv);

        /* Clear register 0x100+i (OPL3 range - harmless on OPL2) */
        adlib->opl.write(0x38A, (uint8_t) i, adlib->opl.priv);
        adlib->opl.write(0x38B, 0, adlib->opl.priv);
    }

    adlipt_log("adlipt_reset_opl: OPL chip reset complete\n");
}

static void *
adlipt_init(const device_t *info)
{
    adlipt_t *const adlipt = calloc(1, sizeof(adlipt_t));
    adlipt->lpt = lpt_attach(adlipt_write_data,
                             adlipt_write_ctrl,
                             NULL,
                             adlipt_read_status,
                             NULL,
                             NULL,
                             NULL,
                             adlipt);

    adlipt_log("adlipt_init\n");

    adlipt->adlib = calloc(1, sizeof(adlib_t));

    int use_opl3 = 0;
    if (info && info->local == 1)
        use_opl3 = 1;

    if (!fm_driver_get(use_opl3 ? FM_YMF262 : FM_YM3812, &adlipt->adlib->opl)) {
        /* Fallback to OPL2 if requested driver not available. */
        fm_driver_get(FM_YM3812, &adlipt->adlib->opl);
    }

    music_add_handler(adlib_get_buffer, adlipt->adlib);

    /* Initialize OPL chip to clean state */
    adlipt_reset_opl(adlipt);

    /* Initialize ready status and timer used to simulate READY toggles. */
    adlipt->status = 0x40;
    memset(&adlipt->ready_timer, 0x00, sizeof(pc_timer_t));
    timer_add(&adlipt->ready_timer, adlipt_ready_cb, adlipt, 0);

    return adlipt;
}

static void
adlipt_close(void *priv)
{
    adlipt_t *const adlipt = (adlipt_t *) priv;

    adlipt_log("adlipt_close\n");

    if (adlipt->adlib) {
        adlipt_reset_opl(adlipt);
        free(adlipt->adlib);
    }

    timer_disable(&adlipt->ready_timer);
    free(adlipt);
}

const device_t lpt_adlipt_device = {
    .name          = "AdLib-on-LPT (adlipt/OPL2LPT)",
    .internal_name = "lpt_adlipt",
    .flags         = DEVICE_LPT,
    .local         = 0,
    .init          = adlipt_init,
    .close         = adlipt_close,
    .reset         = NULL,
    .available     = NULL,
    .speed_changed = NULL,
    .force_redraw  = NULL,
    .config        = NULL
};

const device_t lpt_opl3_device = {
    .name          = "AdLib-on-LPT (OPL3LPT)",
    .internal_name = "lpt_opl3",
    .flags         = DEVICE_LPT,
    .local         = 1,
    .init          = adlipt_init,
    .close         = adlipt_close,
    .reset         = NULL,
    .available     = NULL,
    .speed_changed = NULL,
    .force_redraw  = NULL,
    .config        = NULL
};
