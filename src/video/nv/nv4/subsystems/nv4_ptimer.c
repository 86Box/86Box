/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          NVIDIA RIVA TNT / TNT2 -- PTIMER.
 *
 *          A 61-bit time counter. Every NVCLK it advances by
 *          32 * DENOMINATOR / NUMERATOR; drivers program the ratio as
 *          NVCLK / 31.25 MHz so that it counts nanoseconds. TIME_0 holds
 *          bits 31:5 and TIME_1 bits 60:32. The alarm interrupt fires
 *          when the low word passes ALARM_0.
 *
 *          The counter runs on emulated time, so it stops while the
 *          emulator is paused and keeps pace with a slow guest.
 *
 * Authors: Marcos Alves, <xeon4d@gmail.com>
 *
 *          Copyright 2026 Marcos Alves
 */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>
#include <86box/86box.h>
#include <86box/device.h>
#include <86box/mem.h>
#include <86box/pci.h>
#include <86box/rom.h>
#include <86box/timer.h>
#include <86box/video.h>
#include <86box/vid_svga.h>
#include "cpu.h"
#include <86box/nv/vid_nv4.h>

#define NV4_PTIMER_POLL_US 50.0

static void
nv4_ptimer_update(nv4_t *nv4)
{
    uint64_t now    = tsc;
    uint64_t elapsed = (now >= nv4->ptimer_last_tsc) ? (now - nv4->ptimer_last_tsc) : 0;
    double   ns;
    uint64_t add;
    uint32_t old_low;

    nv4->ptimer_last_tsc = now;
    if (!nv4->ptimer_num || !nv4->ptimer_den || (cpuclock <= 0.0) || !elapsed)
        return;

    ns = ((double) elapsed / cpuclock) * nv4->nvclk_hz * 32.0
         * (double) nv4->ptimer_den / (double) nv4->ptimer_num
         + nv4->ptimer_frac;
    add              = (uint64_t) ns;
    nv4->ptimer_frac = ns - (double) add;
    if (!add)
        return;

    old_low = (uint32_t) nv4->ptimer_time;
    nv4->ptimer_time = (nv4->ptimer_time + add) & 0x1fffffffffffffffULL;
    /* the alarm is in (old, old + add], modulo 2^32 */
    if ((add >= 0x100000000ULL) || ((uint32_t) (nv4->ptimer_alarm - old_low - 1) < (uint32_t) add)) {
        nv4->ptimer_intr |= 1;
        nv4_pmc_update_irq(nv4);
    }
}

/* Keeps TIME moving, so the alarm fires with nobody reading it. */
static void
nv4_ptimer_poll(void *priv)
{
    nv4_t *nv4 = (nv4_t *) priv;

    nv4_ptimer_update(nv4);
    timer_advance_u64(&nv4->ptimer_timer, (uint64_t) (NV4_PTIMER_POLL_US * TIMER_USEC));
}

void
nv4_ptimer_init(nv4_t *nv4)
{
    timer_add(&nv4->ptimer_timer, nv4_ptimer_poll, nv4, 1);
}

void
nv4_ptimer_reset(nv4_t *nv4)
{
    nv4->ptimer_intr     = 0;
    nv4->ptimer_intr_en  = 0;
    nv4->ptimer_num      = 0;
    nv4->ptimer_den      = 0;
    nv4->ptimer_time     = 0;
    nv4->ptimer_alarm    = 0;
    nv4->ptimer_frac     = 0.0;
    nv4->ptimer_last_tsc = tsc;
}

uint32_t
nv4_ptimer_read(nv4_t *nv4, uint32_t addr)
{
    switch (addr) {
        case NV4_PTIMER_INTR_0:
            return nv4->ptimer_intr;
        case NV4_PTIMER_INTR_EN_0:
            return nv4->ptimer_intr_en;
        case NV4_PTIMER_NUMERATOR:
            return nv4->ptimer_num;
        case NV4_PTIMER_DENOMINATOR:
            return nv4->ptimer_den;
        case NV4_PTIMER_TIME_0:
            nv4_ptimer_update(nv4);
            return (uint32_t) nv4->ptimer_time & 0xffffffe0;
        case NV4_PTIMER_TIME_1:
            nv4_ptimer_update(nv4);
            return (uint32_t) (nv4->ptimer_time >> 32) & 0x1fffffff;
        case NV4_PTIMER_ALARM_0:
            return nv4->ptimer_alarm;
        default:
            return 0;
    }
}

void
nv4_ptimer_write(nv4_t *nv4, uint32_t addr, uint32_t val)
{
    switch (addr) {
        case NV4_PTIMER_INTR_0:
            nv4->ptimer_intr &= ~val;
            nv4_pmc_update_irq(nv4);
            break;
        case NV4_PTIMER_INTR_EN_0:
            nv4->ptimer_intr_en = val & 1;
            nv4_pmc_update_irq(nv4);
            break;
        case NV4_PTIMER_NUMERATOR:
            nv4_ptimer_update(nv4);
            nv4->ptimer_num = val & 0xffff;
            break;
        case NV4_PTIMER_DENOMINATOR:
            nv4_ptimer_update(nv4);
            nv4->ptimer_den = val & 0xffff;
            break;
        case NV4_PTIMER_TIME_0:
            nv4_ptimer_update(nv4);
            nv4->ptimer_time = (nv4->ptimer_time & 0xffffffff00000000ULL) | (val & 0xffffffe0);
            nv4->ptimer_frac = 0.0;
            break;
        case NV4_PTIMER_TIME_1:
            nv4_ptimer_update(nv4);
            nv4->ptimer_time = (nv4->ptimer_time & 0xffffffffULL) | ((uint64_t) (val & 0x1fffffff) << 32);
            nv4->ptimer_frac = 0.0;
            break;
        case NV4_PTIMER_ALARM_0:
            nv4_ptimer_update(nv4);
            nv4->ptimer_alarm = val & 0xffffffe0;
            break;
        default:
            break;
    }
}
