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
#include <86box/86box.h>
#include <86box/sound.h>
#include <86box/device.h>
#include <86box/timer.h>
#include <86box/snd_opl.h>
#include <86box/plat_unused.h>

#define RSM_FRAC 10

typedef struct {
    esfm_chip opl;
    int8_t    flags;
    int8_t    pad;

    uint8_t  status;
    uint8_t  timer_ctrl;
    uint16_t timer_count[2];
    uint16_t timer_cur_count[2];

    pc_timer_t timers[2];

    int16_t samples[2];

    int     pos;
    int32_t buffer[MUSICBUFLEN * 2];
} esfm_drv_t;

enum {
    FLAG_CYCLES = 0x02,
    FLAG_OPL3   = 0x01
};

enum {
    STAT_TMR_OVER  = 0x60,
    STAT_TMR1_OVER = 0x40,
    STAT_TMR2_OVER = 0x20,
    STAT_TMR_ANY   = 0x80
};

enum {
    CTRL_RESET      = 0x80,
    CTRL_TMR_MASK   = 0x60,
    CTRL_TMR1_MASK  = 0x40,
    CTRL_TMR2_MASK  = 0x20,
    CTRL_TMR2_START = 0x02,
    CTRL_TMR1_START = 0x01
};

#ifdef ENABLE_OPL_LOG
int esfm_do_log = ENABLE_OPL_LOG;

static void
esfm_log(const char *fmt, ...)
{
    va_list ap;

    if (esfm_do_log) {
        va_start(ap, fmt);
        pclog_ex(fmt, ap);
        va_end(ap);
    }
}
#else
#    define esfm_log(fmt, ...)
#endif

void
esfm_generate_raw(esfm_drv_t *dev, int32_t *bufp)
{
    ESFM_generate(&dev->opl, bufp);
}

void
esfm_drv_generate_stream(esfm_drv_t *dev, int32_t *sndptr, uint32_t num)
{
    for (uint32_t i = 0; i < num; i++) {
        esfm_generate_raw(dev, sndptr);
        sndptr += 2;
    }
}

static void
esfm_timer_tick(esfm_drv_t *dev, int tmr)
{
    dev->timer_cur_count[tmr] = (dev->timer_cur_count[tmr] + 1) & 0xff;

    esfm_log("Ticking timer %i, count now %02X...\n", tmr, dev->timer_cur_count[tmr]);

    if (dev->timer_cur_count[tmr] == 0x00) {
        dev->status |= ((STAT_TMR1_OVER >> tmr) & ~dev->timer_ctrl);
        dev->timer_cur_count[tmr] = dev->timer_count[tmr];

        esfm_log("Count wrapped around to zero, reloading timer %i (%02X), status = %02X...\n", tmr, (STAT_TMR1_OVER >> tmr), dev->status);
    }

    timer_on_auto(&dev->timers[tmr], (tmr == 1) ? 320.0 : 80.0);
}

static void
esfm_timer_control(esfm_drv_t *dev, int tmr, int start)
{
    timer_on_auto(&dev->timers[tmr], 0.0);

    if (start) {
        esfm_log("Loading timer %i count: %02X = %02X\n", tmr, dev->timer_cur_count[tmr], dev->timer_count[tmr]);
        dev->timer_cur_count[tmr] = dev->timer_count[tmr];
        if (dev->flags & FLAG_OPL3)
            esfm_timer_tick(dev, tmr); /* Per the YMF 262 datasheet, OPL3 starts counting immediately, unlike OPL2. */
        else
            timer_on_auto(&dev->timers[tmr], (tmr == 1) ? 320.0 : 80.0);
    } else {
        esfm_log("Timer %i stopped\n", tmr);
        if (tmr == 1) {
            dev->status &= ~STAT_TMR2_OVER;
        } else
            dev->status &= ~STAT_TMR1_OVER;
    }
}

static void
esfm_timer_1(void *priv)
{
    esfm_drv_t *dev = (esfm_drv_t *) priv;

    esfm_timer_tick(dev, 0);
}

static void
esfm_timer_2(void *priv)
{
    esfm_drv_t *dev = (esfm_drv_t *) priv;

    esfm_timer_tick(dev, 1);
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
esfm_drv_init(UNUSED(const device_t *info))
{
    esfm_drv_t *dev = (esfm_drv_t *) calloc(1, sizeof(esfm_drv_t));
    dev->flags      = FLAG_CYCLES | FLAG_OPL3;

    /* Initialize the ESFMu object. */
    ESFM_init(&dev->opl);

    timer_add(&dev->timers[0], esfm_timer_1, dev, 0);
    timer_add(&dev->timers[1], esfm_timer_2, dev, 0);

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

    if (dev->pos >= music_pos_global)
        return dev->buffer;

    esfm_drv_generate_stream(dev,
                             &dev->buffer[dev->pos * 2],
                             music_pos_global - dev->pos);

    for (; dev->pos < music_pos_global; dev->pos++) {
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

    switch (port & 0x0003) {
        case 0x0000:
            ret = dev->status;
            if (dev->status & STAT_TMR_OVER)
                ret |= STAT_TMR_ANY;
            break;

        case 0x0001:
            ret = ESFM_read_port(&dev->opl, port & 3);
            switch (dev->opl.addr_latch & 0x5ff) {
                case 0x402:
                    ret = dev->timer_count[0];
                    break;
                case 0x403:
                    ret = dev->timer_count[1];
                    break;
                case 0x404:
                    ret = dev->timer_ctrl;
                    break;
            }
            break;

        case 0x0002:
        case 0x0003:
            ret = 0xff;
            break;
    }

    return ret;
}

static void
esfm_drv_write_buffered(esfm_drv_t *dev, uint8_t val)
{
    uint16_t p = dev->opl.addr_latch & 0x07ff;

    if (dev->opl.native_mode) {
        p -= 0x400;
    }
    p &= 0x1ff;

    switch (p) {
        case 0x002: /* Timer 1 */
            dev->timer_count[0] = val;
            esfm_log("Timer 0 count now: %i\n", dev->timer_count[0]);
            break;

        case 0x003: /* Timer 2 */
            dev->timer_count[1] = val;
            esfm_log("Timer 1 count now: %i\n", dev->timer_count[1]);
            break;

        case 0x004: /* Timer control */
            if (val & CTRL_RESET) {
                esfm_log("Resetting timer status...\n");
                dev->status &= ~STAT_TMR_OVER;
            } else {
                dev->timer_ctrl = val;
                esfm_timer_control(dev, 0, val & CTRL_TMR1_START);
                esfm_timer_control(dev, 1, val & CTRL_TMR2_START);
                esfm_log("Status mask now %02X (val = %02X)\n", (val & ~CTRL_TMR_MASK) & CTRL_TMR_MASK, val);
            }
            break;

        default:
            break;
    }

    ESFM_write_reg_buffered_fast(&dev->opl, dev->opl.addr_latch, val);
}

static void
esfm_drv_write(uint16_t port, uint8_t val, void *priv)
{
    esfm_drv_t *dev = (esfm_drv_t *) priv;

    if (dev->flags & FLAG_CYCLES)
        cycles -= ((int) (isa_timing * 8));

    esfm_drv_update(dev);

    if (dev->opl.native_mode) {
        if ((port & 0x0003) == 0x0001)
            esfm_drv_write_buffered(dev, val);
        else {
            ESFM_write_port(&dev->opl, port & 3, val);
        }
    } else {
        if ((port & 0x0001) == 0x0001)
            esfm_drv_write_buffered(dev, val);
        else {
            ESFM_write_port(&dev->opl, port & 3, val);
        }
    }
}

const device_t esfm_esfmu_device = {
    .name          = "ESS Technology ESFM (ESFMu)",
    .internal_name = "esfm_esfmu",
    .flags         = 0,
    .local         = FM_ESFM,
    .init          = esfm_drv_init,
    .close         = esfm_drv_close,
    .reset         = NULL,
    .available     = NULL,
    .speed_changed = NULL,
    .force_redraw  = NULL,
    .config        = NULL
};

const fm_drv_t esfmu_opl_drv = {
    .read          = &esfm_drv_read,
    .write         = &esfm_drv_write,
    .update        = &esfm_drv_update,
    .reset_buffer  = &esfm_drv_reset_buffer,
    .set_do_cycles = &esfm_drv_set_do_cycles,
    .priv          = NULL,
    .generate      = NULL,
};
