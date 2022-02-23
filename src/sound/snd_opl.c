/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          Interface to the actual OPL emulator.
 *
 * TODO:    Finish re-working this into a device_t, which requires a
 *          poll-like function for "update" so the sound card can call
 *          that and get a buffer-full of sample data.
 *
 * Authors: Fred N. van Kempen, <decwiz@yahoo.com>
 *          Miran Grca, <mgrca8@gmail.com>
 *
 *          Copyright 2017-2020 Fred N. van Kempen.
 *          Copyright 2016-2020 Miran Grca.
 */
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>
#define HAVE_STDARG_H

#include "cpu.h"
#include <86box/86box.h>
#include <86box/io.h>
#include <86box/timer.h>
#include <86box/sound.h>
#include <86box/snd_opl.h>
#include <86box/snd_opl_nuked.h>

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
int opl_do_log = ENABLE_OPL_LOG;

static void
opl_log(const char *fmt, ...)
{
    va_list ap;

    if (opl_do_log) {
        va_start(ap, fmt);
        pclog_ex(fmt, ap);
        va_end(ap);
    }
}
#else
#    define opl_log(fmt, ...)
#endif

static void
timer_tick(opl_t *dev, int tmr)
{
    dev->timer_cur_count[tmr] = (dev->timer_cur_count[tmr] + 1) & 0xff;

    opl_log("Ticking timer %i, count now %02X...\n", tmr, dev->timer_cur_count[tmr]);

    if (dev->timer_cur_count[tmr] == 0x00) {
        dev->status |= ((STAT_TMR1_OVER >> tmr) & ~dev->timer_ctrl);
        dev->timer_cur_count[tmr] = dev->timer_count[tmr];

        opl_log("Count wrapped around to zero, reloading timer %i (%02X), status = %02X...\n", tmr, (STAT_TMR1_OVER >> tmr), dev->status);
    }

    timer_on_auto(&dev->timers[tmr], (tmr == 1) ? 320.0 : 80.0);
}

static void
timer_control(opl_t *dev, int tmr, int start)
{
    timer_on_auto(&dev->timers[tmr], 0.0);

    if (start) {
        opl_log("Loading timer %i count: %02X = %02X\n", tmr, dev->timer_cur_count[tmr], dev->timer_count[tmr]);
        dev->timer_cur_count[tmr] = dev->timer_count[tmr];
        if (dev->flags & FLAG_OPL3)
            timer_tick(dev, tmr); /* Per the YMF 262 datasheet, OPL3 starts counting immediately, unlike OPL2. */
        else
            timer_on_auto(&dev->timers[tmr], (tmr == 1) ? 320.0 : 80.0);
    } else {
        opl_log("Timer %i stopped\n", tmr);
        if (tmr == 1) {
            dev->status &= ~STAT_TMR2_OVER;
        } else
            dev->status &= ~STAT_TMR1_OVER;
    }
}

static void
timer_1(void *priv)
{
    opl_t *dev = (opl_t *) priv;

    timer_tick(dev, 0);
}

static void
timer_2(void *priv)
{
    opl_t *dev = (opl_t *) priv;

    timer_tick(dev, 1);
}

static uint8_t
opl_read(opl_t *dev, uint16_t port)
{
    uint8_t ret = 0xff;

    if ((port & 0x0003) == 0x0000) {
        ret = dev->status;
        if (dev->status & STAT_TMR_OVER)
            ret |= STAT_TMR_ANY;
    }

    opl_log("OPL statret = %02x, status = %02x\n", ret, dev->status);

    return ret;
}

static void
opl_write(opl_t *dev, uint16_t port, uint8_t val)
{
    if ((port & 0x0001) == 0x0001) {
        nuked_write_reg_buffered(dev->opl, dev->port, val);

        switch (dev->port) {
            case 0x02: /* Timer 1 */
                dev->timer_count[0] = val;
                opl_log("Timer 0 count now: %i\n", dev->timer_count[0]);
                break;

            case 0x03: /* Timer 2 */
                dev->timer_count[1] = val;
                opl_log("Timer 1 count now: %i\n", dev->timer_count[1]);
                break;

            case 0x04: /* Timer control */
                if (val & CTRL_RESET) {
                    opl_log("Resetting timer status...\n");
                    dev->status &= ~STAT_TMR_OVER;
                } else {
                    dev->timer_ctrl = val;
                    timer_control(dev, 0, val & CTRL_TMR1_START);
                    timer_control(dev, 1, val & CTRL_TMR2_START);
                    opl_log("Status mask now %02X (val = %02X)\n", (val & ~CTRL_TMR_MASK) & CTRL_TMR_MASK, val);
                }
                break;
        }
    } else {
        dev->port = nuked_write_addr(dev->opl, port, val) & 0x01ff;

        if (!(dev->flags & FLAG_OPL3))
            dev->port &= 0x00ff;
    }
}

void
opl_set_do_cycles(opl_t *dev, int8_t do_cycles)
{
    if (do_cycles)
        dev->flags |= FLAG_CYCLES;
    else
        dev->flags &= ~FLAG_CYCLES;
}

static void
opl_init(opl_t *dev, int is_opl3)
{
    memset(dev, 0x00, sizeof(opl_t));

    dev->flags = FLAG_CYCLES;
    if (is_opl3)
        dev->flags |= FLAG_OPL3;
    else
        dev->status = 0x06;

    /* Create a NukedOPL object. */
    dev->opl = nuked_init(48000);

    timer_add(&dev->timers[0], timer_1, dev, 0);
    timer_add(&dev->timers[1], timer_2, dev, 0);
}

void
opl_close(opl_t *dev)
{
    /* Release the NukedOPL object. */
    if (dev->opl) {
        nuked_close(dev->opl);
        dev->opl = NULL;
    }
}

uint8_t
opl2_read(uint16_t port, void *priv)
{
    opl_t *dev = (opl_t *) priv;

    if (dev->flags & FLAG_CYCLES)
        cycles -= ((int) (isa_timing * 8));

    opl2_update(dev);
    opl_log("OPL2 port read = %04x\n", port);

    return (opl_read(dev, port));
}

void
opl2_write(uint16_t port, uint8_t val, void *priv)
{
    opl_t *dev = (opl_t *) priv;

    opl2_update(dev);

    opl_log("OPL2 port write = %04x\n", port);
    opl_write(dev, port, val);
}

void
opl2_init(opl_t *dev)
{
    opl_init(dev, 0);
}

void
opl2_update(opl_t *dev)
{
    if (dev->pos >= sound_pos_global) {
        return;
    }

    nuked_generate_stream(dev->opl,
                          &dev->buffer[dev->pos * 2],
                          sound_pos_global - dev->pos);

    for (; dev->pos < sound_pos_global; dev->pos++) {
        dev->buffer[dev->pos * 2] /= 2;
        dev->buffer[(dev->pos * 2) + 1] = dev->buffer[dev->pos * 2];
    }
}

uint8_t
opl3_read(uint16_t port, void *priv)
{
    opl_t *dev = (opl_t *) priv;

    if (dev->flags & FLAG_CYCLES)
        cycles -= ((int) (isa_timing * 8));

    opl3_update(dev);

    return (opl_read(dev, port));
}

void
opl3_write(uint16_t port, uint8_t val, void *priv)
{
    opl_t *dev = (opl_t *) priv;

    opl3_update(dev);

    opl_write(dev, port, val);
}

void
opl3_init(opl_t *dev)
{
    opl_init(dev, 1);
}

/* API to sound interface. */
void
opl3_update(opl_t *dev)
{
    if (dev->pos >= sound_pos_global)
        return;

    nuked_generate_stream(dev->opl,
                          &dev->buffer[dev->pos * 2],
                          sound_pos_global - dev->pos);

    for (; dev->pos < sound_pos_global; dev->pos++) {
        dev->buffer[dev->pos * 2] /= 2;
        dev->buffer[(dev->pos * 2) + 1] /= 2;
    }
}
