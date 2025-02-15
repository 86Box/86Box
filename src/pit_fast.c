/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          Implementation of the Intel 8253/8254 Programmable Interval
 *          Timer.
 *
 *
 *
 * Authors: Miran Grca, <mgrca8@gmail.com>
 *
 *          Copyright 2019 Miran Grca.
 */
#include <inttypes.h>
#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>
#define HAVE_STDARG_H
#include <86box/86box.h>
#include "cpu.h"
#include <86box/device.h>
#include <86box/timer.h>
#include <86box/cassette.h>
#include <86box/dma.h>
#include <86box/io.h>
#include <86box/nmi.h>
#include <86box/pic.h>
#include <86box/timer.h>
#include <86box/pit.h>
#include <86box/pit_fast.h>
#include <86box/ppi.h>
#include <86box/machine.h>
#include <86box/sound.h>
#include <86box/snd_speaker.h>
#include <86box/video.h>

#define PIT_PS2          16  /* The PIT is the PS/2's second PIT. */
#define PIT_EXT_IO       32  /* The PIT has externally specified port I/O. */
#define PIT_CUSTOM_CLOCK 64  /* The PIT uses custom clock inputs provided by another provider. */
#define PIT_SECONDARY    128 /* The PIT is secondary (ports 0048-004B). */

#ifdef ENABLE_PIT_FAST_LOG
int pit_fast_do_log = ENABLE_PIT_FAST_LOG;

static void
pit_fast_log(const char *fmt, ...)
{
    va_list ap;

    if (pit_fast_do_log) {
        va_start(ap, fmt);
        pclog_ex(fmt, ap);
        va_end(ap);
    }
}
#else
#    define pit_fast_log(fmt, ...)
#endif

static void
pitf_ctr_set_out(ctrf_t *ctr, int out, void *priv)
{
    pitf_t *pit = (pitf_t *)priv;

    if (ctr == NULL)
        return;

    if (ctr->out_func != NULL)
        ctr->out_func(out, ctr->out, pit);
    ctr->out = out;
}

static void
pitf_ctr_set_load_func(void *data, int counter_id, void (*func)(uint8_t new_m, int new_count))
{
    if (data == NULL)
        return;

    pitf_t *pit = (pitf_t *) data;
    ctrf_t *ctr = &pit->counters[counter_id];

    ctr->load_func = func;
}

static uint16_t
pitf_ctr_get_count(void *data, int counter_id)
{
    const pitf_t *pit = (pitf_t *) data;
    const ctrf_t *ctr = &pit->counters[counter_id];

    return (uint16_t) ctr->l;
}

void
pitf_ctr_set_out_func(void *data, int counter_id, void (*func)(int new_out, int old_out, void *priv))
{
    if (data == NULL)
        return;

    pitf_t *pit = (pitf_t *) data;
    ctrf_t *ctr = &pit->counters[counter_id];

    ctr->out_func = func;
}

void
pitf_ctr_set_using_timer(void *data, int counter_id, int using_timer)
{
    if (tsc > 0)
        timer_process();

    pitf_t *pit      = (pitf_t *) data;
    ctrf_t *ctr      = &pit->counters[counter_id];
    ctr->using_timer = using_timer;
}

static int
pitf_read_timer(ctrf_t *ctr)
{
    if (ctr->using_timer && !(ctr->m == 3 && !ctr->gate) && timer_is_enabled(&ctr->timer)) {
        int read = (int) ((timer_get_remaining_u64(&ctr->timer)) / ctr->pit_const);
        if (ctr->m == 2)
            read++;
        if (read < 0)
            read = 0;
        if (read > 0x10000)
            read = 0x10000;
        if ((ctr->m == 3) && ctr->using_timer)
            read <<= 1;
        return read;
    }
    if (ctr->m == 2)
        return ctr->count + 1;
    return ctr->count;
}

/*Dump timer count back to pit->count[], and disable timer. This should be used
  when stopping a PIT timer, to ensure the correct value can be read back.*/
static void
pitf_dump_and_disable_timer(ctrf_t *ctr)
{
    if (ctr->using_timer && timer_is_enabled(&ctr->timer)) {
        ctr->count = pitf_read_timer(ctr);
        if (ctr->m == 2)
            ctr->count--; /* Don't store the offset from pitf_read_timer */
        timer_disable(&ctr->timer);
    }
}

static void
pitf_ctr_load(ctrf_t *ctr, void *priv)
{
    pitf_t *pit = (pitf_t *)priv;
    int l = ctr->l ? ctr->l : 0x10000;

    ctr->newcount = 0;
    ctr->disabled = 0;

    switch (ctr->m) {
        case 0: /*Interrupt on terminal count*/
            ctr->count = l;
            if (ctr->using_timer)
                timer_set_delay_u64(&ctr->timer, (uint64_t) (l * ctr->pit_const));
            pitf_ctr_set_out(ctr, 0, pit);
            ctr->thit    = 0;
            ctr->enabled = ctr->gate;
            break;
        case 1: /*Hardware retriggerable one-shot*/
            ctr->enabled = 1;
            break;
        case 2: /*Rate generator*/
            if (ctr->initial) {
                ctr->count = l - 1;
                if (ctr->using_timer)
                    timer_set_delay_u64(&ctr->timer, (uint64_t) ((l - 1) * ctr->pit_const));
                pitf_ctr_set_out(ctr, 1, pit);
                ctr->thit = 0;
            }
            ctr->enabled = ctr->gate;
            break;
        case 3: /*Square wave mode*/
            if (ctr->initial) {
                ctr->count = l;
                if (ctr->using_timer)
                    timer_set_delay_u64(&ctr->timer, (uint64_t) (((l + 1) >> 1) * ctr->pit_const));
                else
                    ctr->newcount = (l & 1);
                pitf_ctr_set_out(ctr, 1, pit);
                ctr->thit = 0;
            }
            ctr->enabled = ctr->gate;
            break;
        case 4: /*Software triggered stobe*/
            if (!ctr->thit && !ctr->initial)
                ctr->newcount = 1;
            else {
                ctr->count = l;
                if (ctr->using_timer)
                    timer_set_delay_u64(&ctr->timer, (uint64_t) (l * ctr->pit_const));
                pitf_ctr_set_out(ctr, 0, pit);
                ctr->thit = 0;
            }
            ctr->enabled = ctr->gate;
            break;
        case 5: /*Hardware triggered stobe*/
            ctr->enabled = 1;
            break;

        default:
            break;
    }

    if (ctr->load_func != NULL)
        ctr->load_func(ctr->m, l);

    ctr->initial = 0;
    ctr->running = ctr->enabled && ctr->using_timer && !ctr->disabled;
    if (ctr->using_timer && !ctr->running)
        pitf_dump_and_disable_timer(ctr);
}

static void
pitf_set_gate_no_timer(ctrf_t *ctr, int gate, void *priv)
{
    pitf_t *pit = (pitf_t *)priv;
    int l = ctr->l ? ctr->l : 0x10000;

    if (ctr->disabled) {
        ctr->gate = gate;
        return;
    }

    switch (ctr->m) {
        case 0: /*Interrupt on terminal count*/
        case 4: /*Software triggered stobe*/
            if (ctr->using_timer && !ctr->running)
                timer_set_delay_u64(&ctr->timer, (uint64_t) (l * ctr->pit_const));
            ctr->enabled = gate;
            break;
        case 1: /*Hardware retriggerable one-shot*/
        case 5: /*Hardware triggered stobe*/
            if (gate && !ctr->gate) {
                ctr->count = l;
                if (ctr->using_timer)
                    timer_set_delay_u64(&ctr->timer, (uint64_t) (l * ctr->pit_const));
                pitf_ctr_set_out(ctr, 0, pit);
                ctr->thit    = 0;
                ctr->enabled = 1;
            }
            break;
        case 2: /*Rate generator*/
            if (gate && !ctr->gate) {
                ctr->count = l - 1;
                if (ctr->using_timer)
                    timer_set_delay_u64(&ctr->timer, (uint64_t) (l * ctr->pit_const));
                pitf_ctr_set_out(ctr, 1, pit);
                ctr->thit = 0;
            }
            ctr->enabled = gate;
            break;
        case 3: /*Square wave mode*/
            if (gate && !ctr->gate) {
                ctr->count = l;
                if (ctr->using_timer)
                    timer_set_delay_u64(&ctr->timer, (uint64_t) (((l + 1) >> 1) * ctr->pit_const));
                else
                    ctr->newcount = (l & 1);
                pitf_ctr_set_out(ctr, 1, pit);
                ctr->thit = 0;
            }
            ctr->enabled = gate;
            break;

        default:
            break;
    }
    ctr->gate    = gate;
    ctr->running = ctr->enabled && ctr->using_timer && !ctr->disabled;
    if (ctr->using_timer && !ctr->running)
        pitf_dump_and_disable_timer(ctr);
}

void
pitf_ctr_set_gate(void *data, int counter_id, int gate)
{
    pitf_t *pit = (pitf_t *) data;
    ctrf_t *ctr = &pit->counters[counter_id];

    if (ctr->disabled) {
        ctr->gate = gate;
        return;
    }

    pitf_set_gate_no_timer(ctr, gate, pit);
}

static void
pitf_over(ctrf_t *ctr, void *priv)
{
    pitf_t *pit = (pitf_t *)priv;
    int l = ctr->l ? ctr->l : 0x10000;
    if (ctr->disabled) {
        ctr->count += 0xffff;
        if (ctr->using_timer)
            timer_advance_u64(&ctr->timer, (uint64_t) (0xffff * ctr->pit_const));
        return;
    }

    switch (ctr->m) {
        case 0: /*Interrupt on terminal count*/
        case 1: /*Hardware retriggerable one-shot*/
            if (!ctr->thit)
                pitf_ctr_set_out(ctr, 1, pit);
            ctr->thit = 1;
            ctr->count += 0xffff;
            if (ctr->using_timer)
                timer_advance_u64(&ctr->timer, (uint64_t) (0xffff * ctr->pit_const));
            break;
        case 2: /*Rate generator*/
            ctr->count += l;
            if (ctr->using_timer)
                timer_advance_u64(&ctr->timer, (uint64_t) (l * ctr->pit_const));
            pitf_ctr_set_out(ctr, 0, pit);
            pitf_ctr_set_out(ctr, 1, pit);
            break;
        case 3: /*Square wave mode*/
            if (ctr->out) {
                pitf_ctr_set_out(ctr, 0, pit);
                if (ctr->using_timer) {
                    ctr->count += (l >> 1);
                    timer_advance_u64(&ctr->timer, (uint64_t) ((l >> 1) * ctr->pit_const));
                } else {
                    ctr->count += l;
                    ctr->newcount = (l & 1);
                }
            } else {
                pitf_ctr_set_out(ctr, 1, pit);
                ctr->count += ((l + 1) >> 1);
                if (ctr->using_timer) {
                    ctr->count += (l >> 1);
                    timer_advance_u64(&ctr->timer, (uint64_t) (((l + 1) >> 1) * ctr->pit_const));
                } else {
                    ctr->count += l;
                    ctr->newcount = (l & 1);
                }
            }
#if 0
            if (!t)
                pclog("pit_over: square wave mode c=%x  %lli  %f\n", pit.c[t], tsc, ctr->pit_const);
#endif
            break;
        case 4: /*Software triggered strove*/
            if (!ctr->thit) {
                pitf_ctr_set_out(ctr, 0, pit);
                pitf_ctr_set_out(ctr, 1, pit);
            }
            if (ctr->newcount) {
                ctr->newcount = 0;
                ctr->count += l;
                if (ctr->using_timer)
                    timer_advance_u64(&ctr->timer, (uint64_t) (l * ctr->pit_const));
            } else {
                ctr->thit = 1;
                ctr->count += 0xffff;
                if (ctr->using_timer)
                    timer_advance_u64(&ctr->timer, (uint64_t) (0xffff * ctr->pit_const));
            }
            break;
        case 5: /*Hardware triggered strove*/
            if (!ctr->thit) {
                pitf_ctr_set_out(ctr, 0, pit);
                pitf_ctr_set_out(ctr, 1, pit);
            }
            ctr->thit = 1;
            ctr->count += 0xffff;
            if (ctr->using_timer)
                timer_advance_u64(&ctr->timer, (uint64_t) (0xffff * ctr->pit_const));
            break;

        default:
            break;
    }
    ctr->running = ctr->enabled && ctr->using_timer && !ctr->disabled;
    if (ctr->using_timer && !ctr->running)
        pitf_dump_and_disable_timer(ctr);
}

static __inline void
pitf_ctr_latch_count(ctrf_t *ctr)
{
    ctr->rl = pitf_read_timer(ctr);
#if 0
    pclog("Timer latch %f %04X %04X\n",pit->c[0],pit->rl[0],pit->l[0]);
    pit->ctrl |= 0x30;
#endif
    ctr->rereadlatch = 0;
    ctr->rm          = 3;
    ctr->latched     = 1;
}

static __inline void
pitf_ctr_latch_status(ctrf_t *ctr)
{
    ctr->read_status    = (ctr->ctrl & 0x3f) | (ctr->out ? 0x80 : 0);
    ctr->do_read_status = 1;
}

static void
pitf_write(uint16_t addr, uint8_t val, void *priv)
{
    pitf_t *dev = (pitf_t *) priv;
    int     t   = (addr & 3);
    ctrf_t *ctr;

    pit_fast_log("[%04X:%08X] pit_write(%04X, %02X, %08X)\n", CS, cpu_state.pc, addr, val, priv);

    cycles -= ISA_CYCLES(8);

    switch (addr & 3) {
        case 3: /* control */
            t = val >> 6;

            if (t == 3) {
                if (dev->flags & PIT_8254) {
                    /* This is 8254-only. */
                    if (!(val & 0x20)) {
                        if (val & 2)
                            pitf_ctr_latch_count(&dev->counters[0]);
                        if (val & 4)
                            pitf_ctr_latch_count(&dev->counters[1]);
                        if (val & 8)
                            pitf_ctr_latch_count(&dev->counters[2]);
                        pit_fast_log("PIT %i: Initiated readback command\n", t);
                    }
                    if (!(val & 0x10)) {
                        if (val & 2)
                            pitf_ctr_latch_status(&dev->counters[0]);
                        if (val & 4)
                            pitf_ctr_latch_status(&dev->counters[1]);
                        if (val & 8)
                            pitf_ctr_latch_status(&dev->counters[2]);
                    }
                }
            } else {
                dev->ctrl = val;
                ctr       = &dev->counters[t];

                if (!(dev->ctrl & 0x30)) {
                    pitf_ctr_latch_count(ctr);
                    dev->ctrl |= 0x30;
                    pit_fast_log("PIT %i: Initiated latched read, %i bytes latched\n",
                            t, ctr->latched);
                } else {
                    ctr->ctrl = val;
                    ctr->rm = ctr->wm = (ctr->ctrl >> 4) & 3;
                    ctr->m            = (val >> 1) & 7;
                    if (ctr->m > 5)
                        ctr->m &= 3;
                    if (!(ctr->rm)) {
                        ctr->rm = 3;
                        ctr->rl = pitf_read_timer(ctr);
                    }
                    ctr->rereadlatch = 1;
                    ctr->initial     = 1;
                    if (!ctr->m)
                        pitf_ctr_set_out(ctr, 0, dev);
                    else
                        pitf_ctr_set_out(ctr, 1, dev);
                    ctr->disabled = 1;

                    pit_fast_log("PIT %i: M = %i, RM/WM = %i, Out = %i\n", t, ctr->m, ctr->rm, ctr->out);
                }
                ctr->thit = 0;
            }
            break;

        case 0:
        case 1:
        case 2: /* the actual timers */
            ctr = &dev->counters[t];

            switch (ctr->wm) {
                case 1:
                    ctr->l = val;
                    pitf_ctr_load(ctr, dev);
                    break;
                case 2:
                    ctr->l = (val << 8);
                    pitf_ctr_load(ctr, dev);
                    break;
                case 0:
                    ctr->l &= 0xFF;
                    ctr->l |= (val << 8);
                    pitf_ctr_load(ctr, dev);
                    ctr->wm = 3;
                    break;
                case 3:
                    ctr->l &= 0xFF00;
                    ctr->l |= val;
                    ctr->wm = 0;
                    break;

                default:
                    break;
            }
            break;

        default:
            break;
    }
}

uint8_t
pitf_read_reg(void *priv, uint8_t reg)
{
    pitf_t *dev = (pitf_t *) priv;
    uint8_t ret = 0xff;

    switch (reg) {
        case 0x00:
        case 0x02:
        case 0x04:
            ret = dev->counters[reg >> 1].l & 0xff;
            break;
        case 0x01:
        case 0x03:
        case 0x05:
            ret = (dev->counters[reg >> 1].l >> 8) & 0xff;
            break;
        case 0x06:
            ret = dev->ctrl;
            break;
        case 0x07:
            /* The SiS 551x datasheet is unclear about how exactly
               this register is structured.
               Update: But the SiS 5571 datasheet is clear. */
            ret = (dev->counters[0].rm & 0x80) ? 0x01 : 0x00;
            ret |= (dev->counters[1].rm & 0x80) ? 0x02 : 0x00;
            ret |= (dev->counters[2].rm & 0x80) ? 0x04 : 0x00;
            ret |= (dev->counters[0].wm & 0x80) ? 0x08 : 0x00;
            ret |= (dev->counters[1].wm & 0x80) ? 0x10 : 0x00;
            ret |= (dev->counters[2].wm & 0x80) ? 0x20 : 0x00;
            break;
    }

    return ret;
}

static uint8_t
pitf_read(uint16_t addr, void *priv)
{
    pitf_t *dev = (pitf_t *) priv;
    uint8_t ret = 0xff;
    int     t   = (addr & 3);
    ctrf_t *ctr;

    cycles -= ISA_CYCLES(8);

    switch (addr & 3) {
        case 3: /* Control. */
            /* This is 8254-only, 8253 returns 0x00. */
            ret = (dev->flags & PIT_8254) ? dev->ctrl : 0x00;
            break;

        case 0:
        case 1:
        case 2: /* The actual timers. */
            ctr = &dev->counters[t];

            if (ctr->do_read_status) {
                ctr->do_read_status = 0;
                ret                 = ctr->read_status;
                break;
            }

            if (ctr->rereadlatch && !ctr->latched) {
                ctr->rereadlatch = 0;
                ctr->rl          = pitf_read_timer(ctr);
            }
            switch (ctr->rm) {
                case 0:
                    ret              = ctr->rl >> 8;
                    ctr->rm          = 3;
                    ctr->latched     = 0;
                    ctr->rereadlatch = 1;
                    break;
                case 1:
                    ret              = (ctr->rl) & 0xFF;
                    ctr->latched     = 0;
                    ctr->rereadlatch = 1;
                    break;
                case 2:
                    ret              = (ctr->rl) >> 8;
                    ctr->latched     = 0;
                    ctr->rereadlatch = 1;
                    break;
                case 3:
                    ret = (ctr->rl) & 0xFF;
                    if (ctr->m & 0x80)
                        ctr->m &= 7;
                    else
                        ctr->rm = 0;
                    break;

                default:
                    break;
            }
            break;

        default:
            break;
    }

    pit_fast_log("[%04X:%08X] pit_read(%04X, %08X) = %02X\n", CS, cpu_state.pc, addr, priv, ret);

    return ret;
}

static void
pitf_timer_over(void *priv)
{
    ctrf_t *ctr = (ctrf_t *) priv;
    pit_t *pit = (pit_t *)ctr->priv;
    pitf_over(ctr, pit);
}

void
pitf_ctr_clock(void *data, int counter_id)
{
    pitf_t *pit = (pitf_t *) data;
    ctrf_t *ctr = &pit->counters[counter_id];

    if (ctr->thit || !ctr->enabled)
        return;

    if (ctr->using_timer)
        return;

    if ((ctr->m == 3) && ctr->newcount) {
        ctr->count -= ctr->out ? 1 : 3;
        ctr->newcount = 0;
    } else
        ctr->count -= (ctr->m == 3) ? 2 : 1;

    if (!ctr->count)
        pitf_over(ctr, pit);
}

static void
ctr_reset(ctrf_t *ctr)
{
    ctr->ctrl        = 0;
    ctr->m           = 0;
    ctr->gate        = 0;
    ctr->l           = 0xffff;
    ctr->thit        = 1;
    ctr->using_timer = 1;
}

static void
pitf_reset(pitf_t *dev)
{
    memset(dev, 0, sizeof(pitf_t));

    for (uint8_t i = 0; i < NUM_COUNTERS; i++)
        ctr_reset(&dev->counters[i]);

    /* Disable speaker gate. */
    dev->counters[2].gate = 0;
}

void
pitf_set_pit_const(void *data, uint64_t pit_const)
{
    pitf_t *pit = (pitf_t *) data;
    ctrf_t *ctr;

    for (uint8_t i = 0; i < NUM_COUNTERS; i++) {
        ctr = &pit->counters[i];
        ctr->pit_const = pit_const;
    }
}

static void
pitf_speed_changed(void *priv)
{
    pitf_set_pit_const(priv, PITCONST);
}

static void
pitf_close(void *priv)
{
    pitf_t *dev = (pitf_t *) priv;

    if (dev == pit_devs[0].data)
        pit_devs[0].data = NULL;

    if (dev == pit_devs[1].data)
        pit_devs[1].data = NULL;

    if (dev != NULL)
        free(dev);
}

void
pitf_handler(int set, uint16_t base, int size, void *priv)
{
    io_handler(set, base, size, pitf_read, NULL, NULL, pitf_write, NULL, NULL, priv);
}

static void *
pitf_init(const device_t *info)
{
    pitf_t *dev = (pitf_t *) malloc(sizeof(pitf_t));

    pitf_reset(dev);

    pitf_set_pit_const(dev, PITCONST);

    dev->flags = info->local;

    if (!(dev->flags & PIT_PS2) && !(dev->flags & PIT_CUSTOM_CLOCK)) {
        for (int i = 0; i < NUM_COUNTERS; i++) {
            ctrf_t *ctr = &dev->counters[i];
            ctr->priv = dev;
            timer_add(&ctr->timer, pitf_timer_over, (void *) ctr, 0);
        }
    }

    if (!(dev->flags & PIT_EXT_IO)) {
        io_sethandler((dev->flags & PIT_SECONDARY) ? 0x0048 : 0x0040, 0x0004,
                      pitf_read, NULL, NULL, pitf_write, NULL, NULL, dev);
    }

    return dev;
}

const device_t i8253_fast_device = {
    .name          = "Intel 8253/8253-5 Programmable Interval Timer",
    .internal_name = "i8253_fast",
    .flags         = DEVICE_ISA | DEVICE_PIT,
    .local         = PIT_8253,
    .init          = pitf_init,
    .close         = pitf_close,
    .reset         = NULL,
    .available     = NULL,
    .speed_changed = pitf_speed_changed,
    .force_redraw  = NULL,
    .config        = NULL
};

const device_t i8254_fast_device = {
    .name          = "Intel 8254 Programmable Interval Timer",
    .internal_name = "i8254_fast",
    .flags         = DEVICE_ISA | DEVICE_PIT,
    .local         = PIT_8254,
    .init          = pitf_init,
    .close         = pitf_close,
    .reset         = NULL,
    .available     = NULL,
    .speed_changed = pitf_speed_changed,
    .force_redraw  = NULL,
    .config        = NULL
};

const device_t i8254_sec_fast_device = {
    .name          = "Intel 8254 Programmable Interval Timer (Secondary)",
    .internal_name = "i8254_sec_fast",
    .flags         = DEVICE_ISA,
    .local         = PIT_8254 | PIT_SECONDARY,
    .init          = pitf_init,
    .close         = pitf_close,
    .reset         = NULL,
    .available     = NULL,
    .speed_changed = pitf_speed_changed,
    .force_redraw  = NULL,
    .config        = NULL
};

const device_t i8254_ext_io_fast_device = {
    .name          = "Intel 8254 Programmable Interval Timer (External I/O)",
    .internal_name = "i8254_ext_io_fast",
    .flags         = DEVICE_ISA,
    .local         = PIT_8254 | PIT_EXT_IO,
    .init          = pitf_init,
    .close         = pitf_close,
    .reset         = NULL,
    .available     = NULL,
    .speed_changed = NULL,
    .force_redraw  = NULL,
    .config        = NULL
};

const device_t i8254_ps2_fast_device = {
    .name          = "Intel 8254 Programmable Interval Timer (PS/2)",
    .internal_name = "i8254_ps2_fast",
    .flags         = DEVICE_ISA,
    .local         = PIT_8254 | PIT_PS2 | PIT_EXT_IO,
    .init          = pitf_init,
    .close         = pitf_close,
    .reset         = NULL,
    .available     = NULL,
    .speed_changed = pitf_speed_changed,
    .force_redraw  = NULL,
    .config        = NULL
};

const pit_intf_t pit_fast_intf = {
    .read            = &pitf_read,
    .write           = &pitf_write,
    .get_count       = &pitf_ctr_get_count,
    .set_gate        = &pitf_ctr_set_gate,
    .set_using_timer = &pitf_ctr_set_using_timer,
    .set_out_func    = &pitf_ctr_set_out_func,
    .set_load_func   = &pitf_ctr_set_load_func,
    .ctr_clock       = &pitf_ctr_clock,
    .set_pit_const   = &pitf_set_pit_const,
    .data            = NULL,
};
