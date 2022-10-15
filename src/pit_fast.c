/*
 * 86Box	A hypervisor and IBM PC system emulator that specializes in
 *		running old operating systems and software designed for IBM
 *		PC systems and compatibles from 1981 through fairly recent
 *		system designs based on the PCI bus.
 *
 *		This file is part of the 86Box distribution.
 *
 *		Implementation of the Intel 8253/8254 Programmable Interval
 *		Timer.
 *
 *
 *
 * Author:	Miran Grca, <mgrca8@gmail.com>
 *		Copyright 2019 Miran Grca.
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

#ifdef ENABLE_PIT_LOG
int pit_do_log = ENABLE_PIT_LOG;

static void
pit_log(const char *fmt, ...)
{
    va_list ap;

    if (pit_do_log) {
        va_start(ap, fmt);
        pclog_ex(fmt, ap);
        va_end(ap);
    }
}
#else
#    define pit_log(fmt, ...)
#endif

static void
pitf_ctr_set_out(ctrf_t *ctr, int out)
{
    if (ctr == NULL)
        return;

    if (ctr->out_func != NULL)
        ctr->out_func(out, ctr->out);
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
    pitf_t *pit = (pitf_t *) data;
    ctrf_t *ctr = &pit->counters[counter_id];
    return (uint16_t) ctr->l;
}

static void
pitf_ctr_set_out_func(void *data, int counter_id, void (*func)(int new_out, int old_out))
{
    if (data == NULL)
        return;

    pitf_t *pit = (pitf_t *) data;
    ctrf_t *ctr = &pit->counters[counter_id];

    ctr->out_func = func;
}

static void
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
        int read = (int) ((timer_get_remaining_u64(&ctr->timer)) / PITCONST);
        if (ctr->m == 2)
            read++;
        if (read < 0)
            read = 0;
        if (read > 0x10000)
            read = 0x10000;
        if (ctr->m == 3)
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
pitf_ctr_load(ctrf_t *ctr)
{
    int l = ctr->l ? ctr->l : 0x10000;

    ctr->newcount = 0;
    ctr->disabled = 0;

    switch (ctr->m) {
        case 0: /*Interrupt on terminal count*/
            ctr->count = l;
            if (ctr->using_timer)
                timer_set_delay_u64(&ctr->timer, (uint64_t) (l * PITCONST));
            pitf_ctr_set_out(ctr, 0);
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
                    timer_set_delay_u64(&ctr->timer, (uint64_t) ((l - 1) * PITCONST));
                pitf_ctr_set_out(ctr, 1);
                ctr->thit = 0;
            }
            ctr->enabled = ctr->gate;
            break;
        case 3: /*Square wave mode*/
            if (ctr->initial) {
                ctr->count = l;
                if (ctr->using_timer)
                    timer_set_delay_u64(&ctr->timer, (uint64_t) (((l + 1) >> 1) * PITCONST));
                pitf_ctr_set_out(ctr, 1);
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
                    timer_set_delay_u64(&ctr->timer, (uint64_t) (l * PITCONST));
                pitf_ctr_set_out(ctr, 0);
                ctr->thit = 0;
            }
            ctr->enabled = ctr->gate;
            break;
        case 5: /*Hardware triggered stobe*/
            ctr->enabled = 1;
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
pitf_set_gate_no_timer(ctrf_t *ctr, int gate)
{
    int l = ctr->l ? ctr->l : 0x10000;

    if (ctr->disabled) {
        ctr->gate = gate;
        return;
    }

    switch (ctr->m) {
        case 0: /*Interrupt on terminal count*/
        case 4: /*Software triggered stobe*/
            if (ctr->using_timer && !ctr->running)
                timer_set_delay_u64(&ctr->timer, (uint64_t) (l * PITCONST));
            ctr->enabled = gate;
            break;
        case 1: /*Hardware retriggerable one-shot*/
        case 5: /*Hardware triggered stobe*/
            if (gate && !ctr->gate) {
                ctr->count = l;
                if (ctr->using_timer)
                    timer_set_delay_u64(&ctr->timer, (uint64_t) (l * PITCONST));
                pitf_ctr_set_out(ctr, 0);
                ctr->thit    = 0;
                ctr->enabled = 1;
            }
            break;
        case 2: /*Rate generator*/
            if (gate && !ctr->gate) {
                ctr->count = l - 1;
                if (ctr->using_timer)
                    timer_set_delay_u64(&ctr->timer, (uint64_t) (l * PITCONST));
                pitf_ctr_set_out(ctr, 1);
                ctr->thit = 0;
            }
            ctr->enabled = gate;
            break;
        case 3: /*Square wave mode*/
            if (gate && !ctr->gate) {
                ctr->count = l;
                if (ctr->using_timer)
                    timer_set_delay_u64(&ctr->timer, (uint64_t) (((l + 1) >> 1) * PITCONST));
                pitf_ctr_set_out(ctr, 1);
                ctr->thit = 0;
            }
            ctr->enabled = gate;
            break;
    }
    ctr->gate    = gate;
    ctr->running = ctr->enabled && ctr->using_timer && !ctr->disabled;
    if (ctr->using_timer && !ctr->running)
        pitf_dump_and_disable_timer(ctr);
}

static void
pitf_ctr_set_gate(void *data, int counter_id, int gate)
{
    pitf_t *pit = (pitf_t *) data;
    ctrf_t *ctr = &pit->counters[counter_id];

    if (ctr->disabled) {
        ctr->gate = gate;
        return;
    }

    pitf_set_gate_no_timer(ctr, gate);
}

static void
pitf_over(ctrf_t *ctr)
{
    int l = ctr->l ? ctr->l : 0x10000;
    if (ctr->disabled) {
        ctr->count += 0xffff;
        if (ctr->using_timer)
            timer_advance_u64(&ctr->timer, (uint64_t) (0xffff * PITCONST));
        return;
    }

    switch (ctr->m) {
        case 0: /*Interrupt on terminal count*/
        case 1: /*Hardware retriggerable one-shot*/
            if (!ctr->thit)
                pitf_ctr_set_out(ctr, 1);
            ctr->thit = 1;
            ctr->count += 0xffff;
            if (ctr->using_timer)
                timer_advance_u64(&ctr->timer, (uint64_t) (0xffff * PITCONST));
            break;
        case 2: /*Rate generator*/
            ctr->count += l;
            if (ctr->using_timer)
                timer_advance_u64(&ctr->timer, (uint64_t) (l * PITCONST));
            pitf_ctr_set_out(ctr, 0);
            pitf_ctr_set_out(ctr, 1);
            break;
        case 3: /*Square wave mode*/
            if (ctr->out) {
                pitf_ctr_set_out(ctr, 0);
                ctr->count += (l >> 1);
                if (ctr->using_timer)
                    timer_advance_u64(&ctr->timer, (uint64_t) ((l >> 1) * PITCONST));
            } else {
                pitf_ctr_set_out(ctr, 1);
                ctr->count += ((l + 1) >> 1);
                if (ctr->using_timer)
                    timer_advance_u64(&ctr->timer, (uint64_t) (((l + 1) >> 1) * PITCONST));
            }
            //                if (!t) pclog("pit_over: square wave mode c=%x  %lli  %f\n", pit.c[t], tsc, PITCONST);
            break;
        case 4: /*Software triggered strove*/
            if (!ctr->thit) {
                pitf_ctr_set_out(ctr, 0);
                pitf_ctr_set_out(ctr, 1);
            }
            if (ctr->newcount) {
                ctr->newcount = 0;
                ctr->count += l;
                if (ctr->using_timer)
                    timer_advance_u64(&ctr->timer, (uint64_t) (l * PITCONST));
            } else {
                ctr->thit = 1;
                ctr->count += 0xffff;
                if (ctr->using_timer)
                    timer_advance_u64(&ctr->timer, (uint64_t) (0xffff * PITCONST));
            }
            break;
        case 5: /*Hardware triggered strove*/
            if (!ctr->thit) {
                pitf_ctr_set_out(ctr, 0);
                pitf_ctr_set_out(ctr, 1);
            }
            ctr->thit = 1;
            ctr->count += 0xffff;
            if (ctr->using_timer)
                timer_advance_u64(&ctr->timer, (uint64_t) (0xffff * PITCONST));
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
    //                        pclog("Timer latch %f %04X %04X\n",pit->c[0],pit->rl[0],pit->l[0]);
    // pit->ctrl |= 0x30;
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

    pit_log("[%04X:%08X] pit_write(%04X, %02X, %08X)\n", CS, cpu_state.pc, addr, val, priv);

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
                        pit_log("PIT %i: Initiated readback command\n", t);
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
                    pit_log("PIT %i: Initiated latched read, %i bytes latched\n",
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
                        pitf_ctr_set_out(ctr, 0);
                    else
                        pitf_ctr_set_out(ctr, 1);
                    ctr->disabled = 1;

                    pit_log("PIT %i: M = %i, RM/WM = %i, State = %i, Out = %i\n", t, ctr->m, ctr->rm, ctr->state, ctr->out);
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
                    pitf_ctr_load(ctr);
                    break;
                case 2:
                    ctr->l = (val << 8);
                    pitf_ctr_load(ctr);
                    break;
                case 0:
                    ctr->l &= 0xFF;
                    ctr->l |= (val << 8);
                    pitf_ctr_load(ctr);
                    ctr->wm = 3;
                    break;
                case 3:
                    ctr->l &= 0xFF00;
                    ctr->l |= val;
                    ctr->wm = 0;
                    break;
            }
            break;
    }
}

static uint8_t
pitf_read(uint16_t addr, void *priv)
{
    pitf_t *dev = (pitf_t *) priv;
    uint8_t ret = 0xff;
    int     t   = (addr & 3);
    ctrf_t *ctr;

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
            }
            break;
    }

    pit_log("[%04X:%08X] pit_read(%04X, %08X) = %02X\n", CS, cpu_state.pc, addr, priv, ret);

    return ret;
}

static void
pitf_timer_over(void *p)
{
    ctrf_t *ctr = (ctrf_t *) p;
    pitf_over(ctr);
}

static void
pitf_ctr_clock(void *data, int counter_id)
{
    pitf_t *pit = (pitf_t *) data;
    ctrf_t *ctr = &pit->counters[counter_id];

    if (ctr->thit || !ctr->enabled)
        return;

    if (ctr->using_timer)
        return;

    ctr->count -= (ctr->m == 3) ? 2 : 1;
    if (!ctr->count)
        pitf_over(ctr);
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
    int i;

    memset(dev, 0, sizeof(pitf_t));

    for (i = 0; i < 3; i++)
        ctr_reset(&dev->counters[i]);

    /* Disable speaker gate. */
    dev->counters[2].gate = 0;
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

static void *
pitf_init(const device_t *info)
{
    pitf_t *dev = (pitf_t *) malloc(sizeof(pitf_t));
    pitf_reset(dev);

    dev->flags = info->local;

    if (!(dev->flags & PIT_PS2) && !(dev->flags & PIT_CUSTOM_CLOCK)) {
        for (int i = 0; i < 3; i++) {
            ctrf_t *ctr = &dev->counters[i];
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
    .flags         = DEVICE_ISA,
    .local         = PIT_8253,
    .init          = pitf_init,
    .close         = pitf_close,
    .reset         = NULL,
    { .available = NULL },
    .speed_changed = NULL,
    .force_redraw  = NULL,
    .config        = NULL
};

const device_t i8254_fast_device = {
    .name          = "Intel 8254 Programmable Interval Timer",
    .internal_name = "i8254_fast",
    .flags         = DEVICE_ISA,
    .local         = PIT_8254,
    .init          = pitf_init,
    .close         = pitf_close,
    .reset         = NULL,
    { .available = NULL },
    .speed_changed = NULL,
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
    { .available = NULL },
    .speed_changed = NULL,
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
    { .available = NULL },
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
    { .available = NULL },
    .speed_changed = NULL,
    .force_redraw  = NULL,
    .config        = NULL
};

const pit_intf_t pit_fast_intf = {
    &pitf_read,
    &pitf_write,
    &pitf_ctr_get_count,
    &pitf_ctr_set_gate,
    &pitf_ctr_set_using_timer,
    &pitf_ctr_set_out_func,
    &pitf_ctr_set_load_func,
    &pitf_ctr_clock,
    NULL,
};