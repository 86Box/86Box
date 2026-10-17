/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          NV3 PTIMER - PIT emulation
 *
 *
 *
 * Authors: Connor Hyde, <mario64crashed@gmail.com> I need a better email address ;^)
 *          Marcos Alves, <xeon4d@gmail.com>
 *
 *          Copyright 2024-2025 starfrost
 *          Copyright 2026 Marcos Alves
 */

#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
#include <86box/86box.h>
#include <86box/plat_unused.h>
#include <86box/timer.h>
#include <86box/device.h>
#include <86box/mem.h>
#include <86box/pci.h>
#include <86box/rom.h> // DEPENDENT!!!
#include <86box/video.h>
#include <86box/nv/vid_nv.h>
#include <86box/nv/vid_nv3.h>

// Handles the PTIMER alarm interrupt
void nv3_ptimer_interrupt(uint32_t num)
{
    nv3->ptimer.intr |= (1 << num);
    nv3_pmc_handle_interrupts(true);
}

/* Bring TIME up to date with guest time. The chip adds 32 * DENOMINATOR / NUMERATOR ns to TIME
   (whose bits 4:0 always read 0) every memory clock, and the resman programs NUMERATOR/DENOMINATOR =
   MCLK / 31.25 MHz, so TIME counts nanoseconds. It runs on the emulated clock (tsc),
   so it stops while the emulator is paused and keeps pace with the guest when emulation is slow.
   (It used to run on host time in the wrong units -- some 30000 times too slow on Windows -- and
   compared ALARM against all 64 bits of TIME.) ALARM fires when TIME_0 passes it. */
void nv3_ptimer_update(void)
{
    uint64_t now    = tsc;
    uint64_t cycles = (now >= nv3->ptimer.last_tsc) ? (now - nv3->ptimer.last_tsc) : 0; /* tsc restarts on a hard reset */

    nv3->ptimer.last_tsc = now;

    // Stopped until the driver programs the ratio
    if (!nv3->ptimer.clock_numerator || !nv3->ptimer.clock_denominator || (cpuclock <= 0.0) || !cycles)
        return;

    double ns = ((double) cycles / cpuclock) * nv3->nvbase.memory_clock_frequency * 32.0
                    * (double) nv3->ptimer.clock_denominator / (double) nv3->ptimer.clock_numerator
                + nv3->ptimer.frac_ns;
    uint64_t add = (uint64_t) ns;

    nv3->ptimer.frac_ns = ns - (double) add;
    if (!add)
        return;

    uint32_t old_low = (uint32_t) nv3->ptimer.time;
    nv3->ptimer.time += add;

    /* ALARM in (old, old + add], modulo 2^32 */
    if ((add >= 0x100000000ULL) || ((uint32_t) (nv3->ptimer.alarm - old_low - 1) < (uint32_t) add))
    {
        nv_log_verbose_only("PTIMER alarm interrupt fired (if interrupts enabled) because we reached TIME value 0x%08x\n", nv3->ptimer.alarm);
        nv3_ptimer_interrupt(NV3_PTIMER_INTR_ALARM);
    }
}

// Ticks the timer (the memory clock poll): the alarm needs TIME kept moving even when nobody reads it
void nv3_ptimer_tick(UNUSED(double real_time))
{
    nv3_ptimer_update();
}

uint32_t nv3_ptimer_read(uint32_t address) 
{ 
    // always enabled

    uint32_t ret = 0x00;

    switch (address)
    {
        case NV3_PTIMER_INTR:
            ret = nv3->ptimer.intr;
            break;
        case NV3_PTIMER_INTR_EN:
            ret = nv3->ptimer.intr_en;
            break;
        case NV3_PTIMER_NUMERATOR:
            ret = nv3->ptimer.clock_numerator; // 15:0
            break;
        case NV3_PTIMER_DENOMINATOR:
            ret = nv3->ptimer.clock_denominator ; //15:0
            break;
        // 64-bit value: TIME_0 the low half (bits 31:5), TIME_1 the high half (bits 28:0)
        case NV3_PTIMER_TIME_0_NSEC:
            nv3_ptimer_update();
            ret = (uint32_t) nv3->ptimer.time & 0xFFFFFFE0;
            break;
        case NV3_PTIMER_TIME_1_NSEC:
            nv3_ptimer_update();
            ret = (uint32_t) (nv3->ptimer.time >> 32) & 0x1FFFFFFF;
            break;
        case NV3_PTIMER_ALARM_NSEC: 
            ret = nv3->ptimer.alarm; // 31:5
            break;
    }

    return ret;
}

void nv3_ptimer_write(uint32_t address, uint32_t value) 
{
    // before doing anything, check the subsystem enablement

    switch (address)
    {
        // Interrupt state:
        // Bit 0 - Alarm

        case NV3_PTIMER_INTR:
            nv3->ptimer.intr &= ~value;
            nv3_pmc_handle_interrupts(true);
            break;
        // Interrupt enablement state
        case NV3_PTIMER_INTR_EN:
            nv3->ptimer.intr_en = value & 0x1;
            break;
        // The ratio: time so far runs at the old one (0 in either stops the timer)
        case NV3_PTIMER_NUMERATOR:
            nv3_ptimer_update();
            nv3->ptimer.clock_numerator = value & 0xFFFF; // 15:0
            break;
        case NV3_PTIMER_DENOMINATOR:
            nv3_ptimer_update();
            nv3->ptimer.clock_denominator = value & 0xFFFF; //15:0
            break;
        // 64-bit value: a write replaces its half (it used to be ORed in, so TIME could never go back)
        case NV3_PTIMER_TIME_0_NSEC:
            nv3_ptimer_update();
            nv3->ptimer.time    = (nv3->ptimer.time & 0xFFFFFFFF00000000ULL) | (value & 0xFFFFFFE0);
            nv3->ptimer.frac_ns = 0.0;
            break;
        case NV3_PTIMER_TIME_1_NSEC:
            nv3_ptimer_update();
            nv3->ptimer.time    = (nv3->ptimer.time & 0xFFFFFFFFULL) | ((uint64_t) (value & 0x1FFFFFFF) << 32);
            nv3->ptimer.frac_ns = 0.0;
            break;
        case NV3_PTIMER_ALARM_NSEC: 
            nv3->ptimer.alarm = value & 0xFFFFFFE0; // 31:5
            break;
    }
}