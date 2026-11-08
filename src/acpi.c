/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          ACPI emulation.
 *
 * Authors: Miran Grca, <mgrca8@gmail.com>
 *
 *          Copyright 2020 Miran Grca.
 */
#include <stdarg.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>
#include <stdbool.h>
#define HAVE_STDARG_H
#include <86box/86box.h>
#include "cpu.h"
#include <86box/device.h>
#include <86box/mem.h>
#include <86box/io.h>
#include <86box/pci.h>
#include <86box/pic.h>
#include <86box/plat.h>
#include <86box/timer.h>
#include <86box/keyboard.h>
#include <86box/nvr.h>
#include <86box/pit.h>
#include <86box/apm.h>
#include <86box/acpi.h>
#include <86box/dma.h>
#include <86box/machine.h>
#include <86box/sound.h>
#include <86box/i2c.h>
#include <86box/video.h>
#include <86box/smbus.h>
#include <86box/flash.h>
#include <86box/hdc.h>
#include <86box/hdc_ide.h>
#include <86box/hdc_ide_sff8038i.h>
#include <86box/sis_55xx.h>

int        acpi_rtc_status     = 0;
atomic_int acpi_pwrbut_pressed = 0;
int        acpi_enabled        = 0;
int        acpi_sleeping       = 0;

/* The machine's ACPI device, for the global power button, sleep and GPE
   calls; a hold request from another thread. */
static acpi_t    *acpi_active          = NULL;
static atomic_int acpi_pwrbut_hold_req = 0;

/* How long the power button must be held for the override (ACPI 1.0
   4.7.2.2.1.3, p. 4-49), and how long a hold lasts where the chip has the
   override turned off. */
#define ACPI_PWRBTN_OVERRIDE_MS 4000
#define ACPI_PWRBTN_HOLD_MS     5000

static double cpu_to_acpi;
static int    acpi_pmtmr_read_cycles;

#ifdef ENABLE_ACPI_LOG
int acpi_do_log = ENABLE_ACPI_LOG;

static void
acpi_log(const char *fmt, ...)
{
    va_list ap;

    if (acpi_do_log) {
        va_start(ap, fmt);
        pclog_ex(fmt, ap);
        va_end(ap);
    }
}
#else
#    define acpi_log(fmt, ...)
#endif

static uint64_t
acpi_clock_get(void)
{
    return tsc * cpu_to_acpi;
}

static uint32_t
acpi_timer_get(acpi_t *dev)
{
    uint64_t clock = acpi_clock_get();
    if (dev->regs.timer32)
        return clock & 0xffffffff;
    else
        return clock & 0xffffff;
}

static uint8_t
acpi_gp_timer_get(acpi_t *dev)
{
    uint64_t clock = acpi_clock_get();
    clock -= dev->gp_last_clock;
    if (clock >= dev->gp_count)
        clock = 0x00;
    else
        clock &= 0xff;
    return clock;
}

/* Time to the next change of the counter's top bit (bit 23, or bit 31 with
   the 32-bit timer), when TMR_STS is set (ACPI 1.0 Table 4-9). */
static double
acpi_get_overflow_period(acpi_t *dev)
{
    uint64_t timer = acpi_clock_get();
    uint64_t half  = dev->regs.timer32 ? 0x80000000ULL : 0x00800000ULL;
    uint64_t next  = (timer | (half - 1)) + 1;

    /* A callback that runs a hair before the toggle it was set for has
       already reported it: aim for the next one. */
    if ((next - timer) < 0x100)
        next += half;

    return ((double) (next - timer) / (double) ACPI_TIMER_FREQ) * 1000000.0;
}

static void
acpi_timer_update(acpi_t *dev, bool enable)
{
    if (enable)
        timer_on_auto(&dev->timer, acpi_get_overflow_period(dev));
    else
        timer_stop(&dev->timer);
}

/* TMR_STS is set at every change of the top bit, whatever TMR_EN says; the
   enable only decides whether it raises SCI. So the timer always runs. */
static void
acpi_timer_overflow(void *priv)
{
    acpi_t *dev = (acpi_t *) priv;
    dev->regs.pmsts |= TMROF_STS;
    acpi_update_irq(dev);
    acpi_timer_update(dev, true);
}

static void
acpi_gp_timer_update(acpi_t *dev, bool enable, int count)
{
    if (enable) {
        dev->gp_last_clock = acpi_clock_get();
        dev->gp_count      = count;
        timer_on_auto(&dev->gp_timer, (1000000.0 / (double) ACPI_TIMER_FREQ) * ((double) count));
    } else
        timer_stop(&dev->gp_timer);
}

void
acpi_sis5595_smi_raise(void *priv)
{
    acpi_t *dev = (acpi_t *) priv;

    /* SMI_STS remains set until acknowledged by the SMI handler.
       Do not queue another SMI while the previous one is pending. */
    if ((dev->regs.leg_en & 0x20) && !(dev->regs.leg_sts & 0x20)) {
        dev->regs.leg_sts |= 0x20;
        smi_raise();
    }
}

static void
acpi_gp_timer(void *priv)
{
    acpi_t *dev = (acpi_t *) priv;
    if (dev->vendor == VEN_SIS_5595_1997) {
        dev->regs.gpe_sts |= 0x20000000;
        acpi_gp_timer_update(dev, (dev->regs.gpe_en & 0x20000000), dev->gp_count);
        acpi_sis5595_smi_raise(dev);
    } else if (dev->vendor == VEN_SIS_5595) {
        dev->regs.gpe_sts |= 0x00000400;
        acpi_gp_timer_update(dev, (dev->regs.gpe_en & 0x00000400), dev->gp_count);
        acpi_sis5595_smi_raise(dev);
    } else {
        dev->regs.reg_14 |= 0x2000;
        acpi_gp_timer_update(dev, (dev->regs.reg_16 & 0x2000), dev->gp_count);
        smi_raise();
    }
}

static void
acpi_per_timer(void *priv)
{
    acpi_t *dev = (acpi_t *) priv;
    if (dev->vendor >= VEN_SIS_5595_1997) {
        dev->regs.leg_sts |= 0x04;
        acpi_sis5595_smi_raise(dev);
    } else {
        dev->regs.reg_25 |= 0x04;
        smi_raise();
    }
    timer_on_auto(&dev->per_timer, 16000000.0);
}

void
acpi_update_irq(acpi_t *dev)
{
    uint16_t pmsts     = dev->regs.pmsts | (acpi_rtc_status ? RTC_STS : 0);
    int      sci_level = (pmsts & dev->regs.pmen) & (RTC_EN | PWRBTN_EN | GBL_EN | TMROF_EN);
    sis_55xx_common_t *sis = (sis_55xx_common_t *) dev->priv;

    switch (dev->vendor) {
        case VEN_SMC:
            sci_level |= (dev->regs.pmsts & BM_STS);
            break;
        case VEN_SIS_5582:
            sci_level |= !!(dev->regs.reg_14 & dev->regs.reg_16 & 0x4000);
            break;
        case VEN_SIS_5595_1997:
        case VEN_SIS_5595:
            if ((sis != NULL) && (sis->pmu_regs != NULL)) {
                sci_level |= (sis->pmu_regs[0x80] | sis->pmu_regs[0x81] |
                              sis->pmu_regs[0x82] | sis->pmu_regs[0x83]);
            }
            sci_level |= !!(dev->regs.gpe_sts & dev->regs.gpe_en &
                            ((dev->vendor == VEN_SIS_5595_1997) ? 0x40000000 : 0x00000100));
            break;
        case VEN_INTEL:
            /* THRM_STS, GPI_STS and LID_STS raise SCI with their GPEN
               bits; RI_STS and USB_STS only resume the system (PIIX4 7.2.5-
               7.2.6, p. 139-140, and 11.6.1, p. 268). */
            sci_level |= !!(dev->regs.gpsts & dev->regs.gpen & 0x0a01);
            break;
        case VEN_VIA:
        case VEN_VIA_596B:
            /* A GP_STS bit raises SCI with its bit in the General
               Purpose SCI Enable register (VT82C586B p. 48 / PDF 54;
               VT82C596B p. 73 / PDF 79). */
            sci_level |= !!(dev->regs.gpsts & dev->regs.gpscien);
            break;
        case VEN_ALI:
            /* A GPE0_STS bit with its GPE0_EN bit (M1543C p. 199-200 /
               PDF 200-201). */
            sci_level |= !!(dev->regs.gpsts & dev->regs.gpen);
            break;
        default:
            break;
    }

    /* The southbridge's view of SCI, apart from the line it is steered to
       (acpi_set_sci_hook). */
    if (dev->sci_hook)
        dev->sci_hook(dev->sci_hook_priv, (dev->regs.pmcntrl & 0x01) && sci_level);

    if ((dev->regs.pmcntrl & 0x01) && sci_level)  switch (dev->irq_mode) {
        default:
            if (dev->irq_line != 0)
                picintlevel(1 << dev->irq_line, &dev->irq_state);
            else
                dev->irq_state = 1;
            break;
        case 1:
            pci_set_irq(dev->slot, dev->irq_pin, &dev->irq_state);
            break;
        case 2:
            pci_set_mirq(5, dev->mirq_is_level, &dev->irq_state);
            break;
        case -1:
            break;
    } else  switch (dev->irq_mode) {
        default:
            if (dev->irq_line != 0)
                picintclevel(1 << dev->irq_line, &dev->irq_state);
            else
                dev->irq_state = 0;
            break;
        case 1:
            pci_clear_irq(dev->slot, dev->irq_pin, &dev->irq_state);
            break;
        case 2:
            pci_clear_mirq(5, dev->mirq_is_level, &dev->irq_state);
            break;
        case -1:
            break;
    }
}

void
acpi_raise_smi(void *priv, int do_smi)
{
    acpi_t *dev = (acpi_t *) priv;

    if (dev->regs.glbctl & 0x01) {
        if ((dev->vendor == VEN_VIA) || (dev->vendor == VEN_VIA_596B)) {
            if (!dev->regs.smi_lock || !dev->regs.smi_active) {
                if (do_smi)
                    smi_raise();
                dev->regs.smi_active = 1;
            }
        } else if ((dev->vendor == VEN_INTEL) || (dev->vendor == VEN_ALI)) {
            if (do_smi) {
                smi_raise();

                /* Clear bit 16 of GLBCTL. */
                if (dev->vendor == VEN_INTEL)
                    dev->regs.glbctl &= ~0x00010000;
                else
                    dev->regs.ali_soft_smi = 1;
            }
        } else if (dev->vendor == VEN_SMC) {
            if (do_smi)
                smi_raise();
        }
    }
}

/* The enabled events that assert SMI# instead of SCI, chiefly while
   SCI_EN is clear (legacy mode):
   - PIIX4: PWRBTN_STS, and THRM_STS, GPI_STS and LID_STS ("The SMI# or SCI is
     selectable with the [SCI_EN] bit", PIIX4 11.5.2, p. 263, and 11.6.1,
     p. 268).
   - VIA: PB_STS, RTC_STS and SB_STS; TMR_STS and GBL_STS always raise SCI
     (VT82C586B p. 43 and 47 / PDF 49 and 53; VT82C596B p. 71 / PDF 77). The
     GP_STS bits have their own SMI enables, whatever SCI_EN says (VT82C596B
     p. 73 / PDF 79).
   - ALi M7101: PWRBTN_STS, RTC_STS and the GPE0_STS bits (M1543C p. 194-195
     and 200 / PDF 195-196 and 201).
   - SiS: PWRBTN_STS and RTC_STS (SiS 5595 p. 26 / PDF 32; SiS 5581/5582
     p. 51 / PDF 51). */
static int
acpi_legacy_pending(const acpi_t *dev)
{
    const int      legacy = !(dev->regs.pmcntrl & 0x0001);
    const uint16_t pm     = (dev->regs.pmsts | (acpi_rtc_status ? RTC_STS : 0)) & dev->regs.pmen;

    switch (dev->vendor) {
        case VEN_INTEL:
            return legacy && ((pm & PWRBTN_EN) || (dev->regs.gpsts & dev->regs.gpen & 0x0a01));
        case VEN_VIA:
        case VEN_VIA_596B:
            return (legacy && (pm & (RTC_EN | 0x0200 | PWRBTN_EN))) ||
                   (dev->regs.gpsts & dev->regs.gpsmien);
        case VEN_ALI:
            return legacy && ((pm & (RTC_EN | PWRBTN_EN)) || (dev->regs.gpsts & dev->regs.gpen));
        case VEN_SIS_5582:
        case VEN_SIS_5595_1997:
        case VEN_SIS_5595:
            return legacy && (pm & (RTC_EN | PWRBTN_EN));
        default:
            return 0;
    }
}

/* The VIA blocks generate SMI# from the Global Status bits, not from the
   events: SMI_EN set and a status bit set with its Global Enable bit
   (VT82C586B p. 50-51 / PDF 56-57). A status left pending while SMI_EN or
   its enable was clear raises SMI# as soon as both are set again. The PM1
   and GP events routed to SMI# count too. */
static int
acpi_via_smi_level(const acpi_t *dev)
{
    return (dev->regs.glbctl & 0x0001) &&
           ((dev->regs.glbsts & dev->regs.glben & 0xffff) || acpi_legacy_pending(dev));
}

/* Whether an event routed to SMI# is pending, VIA's whole SMI# level. */
static int
acpi_smi_level(const acpi_t *dev)
{
    if ((dev->vendor == VEN_VIA) || (dev->vendor == VEN_VIA_596B))
        return acpi_via_smi_level(dev);

    return acpi_legacy_pending(dev);
}

/* Asserts SMI# when an event routed to it became pending, each chip
   through its own SMI# enables. */
static void
acpi_smi_edge(acpi_t *dev, int old_level)
{
    if (old_level || !acpi_smi_level(dev))
        return;

    switch (dev->vendor) {
        case VEN_INTEL:
        case VEN_VIA:
        case VEN_VIA_596B:
            acpi_raise_smi(dev, 1);
            break;
        case VEN_ALI:
            if (dev->event_hook != NULL)
                dev->event_hook(dev->event_hook_priv, ACPI_HOOK_SMI);
            break;
        case VEN_SIS_5582:
            smi_raise();
            break;
        case VEN_SIS_5595_1997:
        case VEN_SIS_5595:
            acpi_sis5595_smi_raise(dev);
            break;
        default:
            break;
    }
}

/* Sets PM1 status bits, then raises SCI or SMI# as the chip routes
   them. */
static void
acpi_pm1_event(acpi_t *dev, uint16_t sts)
{
    const int old = acpi_smi_level(dev);

    dev->regs.pmsts |= sts;
    acpi_update_irq(dev);
    acpi_smi_edge(dev, old);
}

/* The status bit each chip has for a general-purpose event source,
   0 for one it lacks:
   - PIIX4 GPSTS: THRM_STS 0, USB_STS 8, GPI_STS 9, RI_STS 10, LID_STS 11
     (PIIX4 7.2.5, p. 139-140).
   - VT82C586B GP_STS: RI_STS 8, USB_STS 9 (p. 48 / PDF 54); VT82C596B adds
     THRM_STS 10, LID_STS 11 and PME_STS 1 (p. 73 / PDF 79).
   - M7101 GPE0_STS: THER_STS 0, USBE_STS 2, RING_STS 11 (M1543C p. 199 /
     PDF 200).
   - SiS 5595: RI_STS bit 1 (November 1998, p. 141 / PDF 147). */
static uint32_t
acpi_gpe_bit(const acpi_t *dev, int source)
{
    static const uint16_t intel[ACPI_GPE_MAX]  = { 0x0400, 0x0100, 0x0800, 0x0001, 0x0200 };
    static const uint16_t via[ACPI_GPE_MAX]    = { 0x0100, 0x0200, 0x0000, 0x0000, 0x0000 };
    static const uint16_t via596[ACPI_GPE_MAX] = { 0x0100, 0x0200, 0x0800, 0x0400, 0x0002 };
    static const uint16_t ali[ACPI_GPE_MAX]    = { 0x0800, 0x0004, 0x0000, 0x0001, 0x0000 };

    if ((source < 0) || (source >= ACPI_GPE_MAX))
        return 0;

    switch (dev->vendor) {
        case VEN_INTEL:
            return intel[source];
        case VEN_VIA:
            return via[source];
        case VEN_VIA_596B:
            return via596[source];
        case VEN_ALI:
            return ali[source];
        case VEN_SIS_5595:
            return (source == ACPI_GPE_RI) ? 0x00000002 : 0;
        default:
            return 0;
    }
}

/* An enabled event that resumes the system from a sleeping state:
   - RTC_STS with RTC_EN (ACPI 1.0 4.7.2.4, p. 4-52).
   - PIIX4: LID, RI, GPI and USB with their GPEN bits; THRM_STS does not
     resume (PIIX4 7.2.6, p. 140, and Table 36, p. 230).
   - VIA: a GP_STS bit enabled for SCI or SMI# (VT82C586B Table 7, p. 43 /
     PDF 49).
   - ALi: RING, ACPWR, DOCK and USBE with their GPE0_EN bits (M1543C p. 200 /
     PDF 201).
   - SiS 5595: a GPE_STS bit with its GPE_EN bit.
   The power button always wakes the machine (acpi_pwrbtn_press()). */
static int
acpi_wake_source(const acpi_t *dev)
{
    if (acpi_rtc_status && (dev->regs.pmen & RTC_EN))
        return 1;

    switch (dev->vendor) {
        case VEN_INTEL:
            return !!(dev->regs.gpsts & dev->regs.gpen & 0x0f00);
        case VEN_VIA:
        case VEN_VIA_596B:
            return !!(dev->regs.gpsts & (dev->regs.gpscien | dev->regs.gpsmien));
        case VEN_ALI:
            return !!(dev->regs.gpsts & dev->regs.gpen & 0x0d04);
        case VEN_SIS_5595_1997:
        case VEN_SIS_5595:
            return !!(dev->regs.gpe_sts & dev->regs.gpe_en);
        default:
            return 0;
    }
}

void
acpi_gp_timer_timeout(void *priv, int n)
{
    acpi_t *dev = (acpi_t *) priv;

    dev->regs.glbsts |= (0x04 << n);          /* bit 2 = GP0, bit 3 = GP1 */
    if (dev->regs.glben & (0x04 << n))
        acpi_raise_smi(dev, 1);
}

/* Processor clock control (ACPI 1.0 4.7.2.6, p. 4-54).  P_CNT is
   the processor control register (ACPI 1.0 4.7.3.5.1, p. 4-64): THT_EN
   (bit 4) modulates STPCLK# with the duty cycle in its CLK_VAL field. */
static uint32_t
acpi_p_cnt(const acpi_t *dev)
{
    switch (dev->vendor) {
        case VEN_INTEL:
        case VEN_ALI:
        case VEN_VIA:
        case VEN_VIA_596B:
            return dev->regs.pcntrl;
        case VEN_SIS_5582:
        case VEN_SIS_5595_1997:
        case VEN_SIS_5595:
            return dev->regs.reg_0c;
        default:
            /* The SMC has no processor block. */
            return 0x00000000;
    }
}

/* The PIIX4's and the M7101's CC_EN/CLK_EN (P_CNT bit 9): without it neither
   throttling nor a P_LVL2/P_LVL3 read do anything (PIIX4 7.2.7, p. 141;
   M1543C P_CNTRL, p. 197).  The VIA and SiS chips have no such bit. */
static int
acpi_clock_control_enabled(const acpi_t *dev)
{
    if ((dev->vendor == VEN_INTEL) || (dev->vendor == VEN_ALI))
        return !!(dev->regs.pcntrl & 0x00000200);

    return 1;
}

/* Throttling: the share of the time the processor runs.  THT_EN on, or, on
   the PIIX4 before the PIIX4E, a P_LVL2 read with THT_EN on (start), starts
   it.  The duty field is CLK_VAL bits 3:1 (bits 3:0 on the VT82C596B), and
   what it means differs:
   - PIIX4E, M7101, SiS 5595: the running share, as ACPI has it
     ("%Performance = Dutyset / 2^dutywidth"; PIIX4 specification update
     no. 13, p. 16; M1543C P_CNTRL, p. 197; SiS 5595 ACPI 0Ch, pp. 137-138);
   - PIIX4, VT82C586B, VT82C596B, SiS 5582: the share STPCLK# is asserted
     (PIIX4 7.2.7, p. 141; VT82C586B p. 48 / PDF 54; VT82C596B p. 72 /
     PDF 78), or STPCLK# high : low = 7 : 1 for 001 (SiS 5582 ACPI 0Ch,
     p. 162).
   The PIIX4 ends it at a break event; the PIIX4E throttles until THT_EN or
   CC_EN is cleared (specification update no. 14, p. 17). */
static void
acpi_throttle_update(acpi_t *dev, int start)
{
    uint32_t p_cnt  = acpi_p_cnt(dev);
    int      width  = (dev->vendor == VEN_VIA_596B) ? 4 : 3;
    int      period = 1 << width;
    int      duty   = (dev->vendor == VEN_VIA_596B) ? (p_cnt & 0x0f) : ((p_cnt >> 1) & 0x07);
    int      run;

    if (!(p_cnt & 0x10) || !acpi_clock_control_enabled(dev) || (duty == 0)) {
        /* Off, or the reserved duty value 0 (100% on the SiS 5595). */
        cpu_throttle_run = 0;
        return;
    }

    if (dev->piix4_ab && !start && !cpu_throttle_run)
        return;

    if ((dev->vendor == VEN_VIA) || (dev->vendor == VEN_VIA_596B) ||
        (dev->vendor == VEN_SIS_5582) || dev->piix4_ab)
        run = period - duty;
    else
        run = duty;

    cpu_throttle_period = period;
    cpu_throttle_break  = dev->piix4_ab;
    cpu_throttle_run    = run;
    acpi_log("ACPI: throttling, running %i/%i\n", run, period);
}

/* A byte read of P_LVL2 (c3 = 0) or P_LVL3 (c3 = 1): STPCLK# is asserted
   and the processor stops until a break event (ACPI 1.0 4.7.3.5.2-3, p. 4-65;
   PIIX4 7.2.8-9 and 11.2.1, pp. 142 and 204; VT82C586B p. 48 / PDF 54;
   VT82C596B p. 72 / PDF 78; M1543C LVL2/LVL3, p. 198; SiS 5582 ACPI 10h-11h,
   p. 163; SiS 5595 ACPI 10h-11h, p. 138).  The read returns 00h.  C3 differs
   from C2 in the bus masters the processor cannot snoop; the emulated one
   needs no snooping, and a bus master request (BM_RLD) is not a break event
   here: the interrupt that ends the transfer is. */
static void
acpi_enter_cx(acpi_t *dev, int c3)
{
    if (!acpi_clock_control_enabled(dev))
        return;

    /* The PIIX4 before the PIIX4E: a P_LVL2 read with THT_EN on throttles
       instead, until a break event (PIIX4 11.2.1, p. 204; specification
       update no. 14, p. 17). */
    if (!c3 && dev->piix4_ab && (dev->regs.pcntrl & 0x10)) {
        acpi_throttle_update(dev, 1);
        if (cpu_throttle_run)
            return;
    }

    cpu_stpclk = 1;
}

static uint32_t
acpi_reg_read_common_regs(UNUSED(int size), uint16_t addr, void *priv)
{
    acpi_t  *dev = (acpi_t *) priv;
    uint32_t ret = 0x00000000;
    int      shift16;
    int      shift32;

    addr &= 0x3f;
    shift16 = (addr & 1) << 3;
    shift32 = (addr & 3) << 3;

    switch (addr) {
        case 0x00:
        case 0x01:
            /* PMSTS - Power Management Status Register (IO) */
            ret = (dev->regs.pmsts >> shift16) & 0xff;
            if (addr == 0x01)
                ret |= (acpi_rtc_status << 2);
            break;
        case 0x02:
        case 0x03:
            /* PMEN - Power Management Resume Enable Register (IO) */
            ret = (dev->regs.pmen >> shift16) & 0xff;
            break;
        case 0x04:
        case 0x05:
            /* PMCNTRL - Power Management Control Register (IO) */
            ret = (dev->regs.pmcntrl >> shift16) & 0xff;
            if (addr == 0x05)
                ret = (ret & 0xdf); /* Bit 5 is write-only. */
            else if (dev->vendor == VEN_VIA_596B)
                ret &= ~0x04; /* GBL_RLS is write-only (VT82C596B p. 71 / PDF 77). */
            break;
        case 0x08:
        case 0x09:
        case 0x0a:
        case 0x0b:
            /* PMTMR - Power Management Timer Register (IO) */
            ret = (dev->pmtmr_latch >> shift32) & 0xff;
#ifdef USE_DYNAREC
            if (cpu_use_dynarec)
                update_tsc();
#endif
            break;

        default:
            break;
    }

#ifdef ENABLE_ACPI_LOG
    if (size != 1)
        acpi_log("(%i) ACPI Read  (%i) %02X: %02X\n", in_smm, size, addr, ret);
#endif
    return ret;
}

static uint32_t
acpi_reg_read_ali(int size, uint16_t addr, void *priv)
{
    const acpi_t  *dev = (acpi_t *) priv;
    uint32_t       ret = 0x00000000;
    int            shift16;
    int            shift32;

    addr &= 0x3f;
    shift16 = (addr & 1) << 3;
    shift32 = (addr & 3) << 3;

    switch (addr) {
        case 0x10:
        case 0x11:
        case 0x12:
        case 0x13:
            /* PCNTRL - Processor Control Register (IO) */
            ret = (dev->regs.pcntrl >> shift32) & 0xff;
            break;
        case 0x14:
        case 0x15:
            /* LVL2/LVL3 - Processor Level 2/3 Register: read only, a read
               enters C2/C3 (M1543C p. 198). */
            if (size == 1)
                acpi_enter_cx((acpi_t *) dev, addr & 1);
            break;
        case 0x18:
        case 0x19:
            /* GPE0_STS - General Purpose Event0 Status Register */
            ret = (dev->regs.gpsts >> shift16) & 0xff;
            break;
        case 0x1a:
        case 0x1b:
            /* GPE0_EN - General Purpose Event0 Enable Register */
            ret = (dev->regs.gpen >> shift16) & 0xff;
            break;
        case 0x1d:
        case 0x1c:
            /* GPE1_STS - General Purpose Event1 Status Register */
            ret = (dev->regs.gpsts1 >> shift16) & 0xff;
            break;
        case 0x1f:
        case 0x1e:
            /* GPE1_EN - General Purpose Event1 Enable Register */
            ret = (dev->regs.gpen1 >> shift16) & 0xff;
            break;
        case 0x20 ... 0x27:
            /* GPE1_CTL / BIOS Release Control: BIOS_RLS (bit 1) always reads 0,
               the rest is reserved (M1543C p. 203 / PDF 204). */
            ret = 0x00;
            break;
        case 0x30:
            /* PM2_CNTRL - Power Management 2 Control Register */
            ret = dev->regs.pm2_cntrl;
            break;
        default:
            ret = acpi_reg_read_common_regs(size, addr, priv);
            break;
    }

#ifdef ENABLE_ACPI_LOG
    if (size != 1)
        acpi_log("(%i) ACPI Read  (%i) %02X: %02X\n", in_smm, size, addr, ret);
#endif
    return ret;
}

static uint32_t
acpi_reg_read_intel(int size, uint16_t addr, void *priv)
{
    const acpi_t  *dev = (acpi_t *) priv;
    uint32_t       ret = 0x00000000;
    int            shift16;
    int            shift32;

    addr &= 0x3f;
    shift16 = (addr & 1) << 3;
    shift32 = (addr & 3) << 3;

    switch (addr) {
        case 0x0c:
        case 0x0d:
            /* GPSTS - General Purpose Status Register (IO) */
            ret = (dev->regs.gpsts >> shift16) & 0xff;
            break;
        case 0x0e:
        case 0x0f:
            /* GPEN - General Purpose Enable Register (IO) */
            ret = (dev->regs.gpen >> shift16) & 0xff;
            break;
        case 0x10:
        case 0x11:
        case 0x12:
        case 0x13:
            /* PCNTRL - Processor Control Register (IO) */
            ret = (dev->regs.pcntrl >> shift32) & 0xff;
            break;
        case 0x14:
        case 0x15:
            /* PLVL2/PLVL3 - Processor Level 2/3 Register (IO): byte
               readable only, 00h, a read enters C2/C3 (PIIX4 7.2.8-9,
               p. 142). */
            if (size == 1)
                acpi_enter_cx((acpi_t *) dev, addr & 1);
            break;
        case 0x18:
        case 0x19:
            /* GLBSTS - Global Status Register (IO) */
            ret = (dev->regs.glbsts >> shift16) & 0xff;
            if (addr == 0x18) {
                ret &= 0x27;
                if (dev->regs.gpsts != 0x0000)
                    ret |= 0x80;
                if (dev->regs.pmsts != 0x0000)
                    ret |= 0x40;
                if (dev->regs.devsts != 0x00000000)
                    ret |= 0x10;
            }
            break;
        case 0x1c:
        case 0x1d:
        case 0x1e:
        case 0x1f:
            /* DEVSTS - Device Status Register (IO) */
            ret = (dev->regs.devsts >> shift32) & 0xff;
            break;
        case 0x20:
        case 0x21:
            /* GLBEN - Global Enable Register (IO) */
            ret = (dev->regs.glben >> shift16) & 0xff;
            break;
        case 0x28:
        case 0x29:
        case 0x2a:
        case 0x2b:
            /* GLBCTL - Global Control Register (IO) */
            ret = (dev->regs.glbctl >> shift32) & 0xff;
            break;
        case 0x2c:
        case 0x2d:
        case 0x2e:
        case 0x2f:
            /* DEVCTL - Device Control Register (IO) */
            ret = (dev->regs.devctl >> shift32) & 0xff;
            break;
        case 0x30:
        case 0x31:
        case 0x32:
            /* GPIREG - General Purpose Input Register (IO) */
            ret = dev->regs.gpireg[addr & 3];
            break;
        case 0x34:
        case 0x35:
        case 0x36:
        case 0x37:
            /* GPOREG - General Purpose Output Register (IO) */
            ret = dev->regs.gporeg[addr & 3];
            break;
        default:
            ret = acpi_reg_read_common_regs(size, addr, priv);
            break;
    }

#ifdef ENABLE_ACPI_LOG
        // if (size != 1)
        // acpi_log("(%i) ACPI Read  (%i) %02X: %02X\n", in_smm, size, addr, ret);
#endif
    return ret;
}

static uint32_t
acpi_reg_read_via_common(int size, uint16_t addr, void *priv)
{
    const acpi_t  *dev = (acpi_t *) priv;
    uint32_t       ret = 0x00000000;
    int            shift16;
    int            shift32;

    addr &= 0xff;
    shift16 = (addr & 1) << 3;
    shift32 = (addr & 3) << 3;

    switch (addr) {
        case 0x10:
        case 0x11:
        case 0x12:
        case 0x13:
            /* PCNTRL - Processor Control Register (IO) */
            ret = (dev->regs.pcntrl >> shift32) & 0xff;
            break;
        case 0x14:
        case 0x15:
            /* P_LVL2/P_LVL3 - Processor Level 2/3: always 00h, a read
               enters C2/C3 (VT82C586B p. 48 / PDF 54; VT82C596B p. 72 /
               PDF 78). */
            if (size == 1)
                acpi_enter_cx((acpi_t *) dev, addr & 1);
            break;
        case 0x20:
        case 0x21:
            /* GPSTS - General Purpose Status Register (IO) */
            ret = (dev->regs.gpsts >> shift16) & 0xff;
            break;
        case 0x22:
        case 0x23:
            /* General Purpose SCI Enable */
            ret = (dev->regs.gpscien >> shift16) & 0xff;
            break;
        case 0x24:
        case 0x25:
            /* General Purpose SMI Enable */
            ret = (dev->regs.gpsmien >> shift16) & 0xff;
            break;
        case 0x26:
        case 0x27:
            /* Power Supply Control */
            ret = (dev->regs.pscntrl >> shift16) & 0xff;
            break;
        case 0x28:
        case 0x29:
            /* GLBSTS - Global Status Register (IO) */
            ret = (dev->regs.glbsts >> shift16) & 0xff;
            break;
        case 0x2a:
        case 0x2b:
            /* GLBEN - Global Enable Register (IO) */
            ret = (dev->regs.glben >> shift16) & 0xff;
            break;
        case 0x2c:
        case 0x2d:
            /* GLBCTL - Global Control Register (IO) */
            ret = (dev->regs.glbctl >> shift16) & 0xff;
            ret &= ~0x0110;
            ret |= (dev->regs.smi_lock ? 0x10 : 0x00);
            ret |= (dev->regs.smi_active ? 0x01 : 0x00);
            break;
        case 0x2f:
            /* SMI Command */
            if (size == 1)
                ret = dev->regs.smicmd & 0xff;
            break;
        case 0x30:
        case 0x31:
        case 0x32:
        case 0x33:
            /* Primary Activity Detect Status */
            ret = (dev->regs.padsts >> shift32) & 0xff;
            break;
        case 0x34:
        case 0x35:
        case 0x36:
        case 0x37:
            /* Primary Activity Detect Enable */
            ret = (dev->regs.paden >> shift32) & 0xff;
            break;
        case 0x38:
        case 0x39:
        case 0x3a:
        case 0x3b:
            /* GP Timer Reload Enable */
            ret = (dev->regs.gptren >> shift32) & 0xff;
            break;
        default:
            ret = acpi_reg_read_common_regs(size, addr, priv);
            break;
    }

#ifdef ENABLE_ACPI_LOG
    if (size != 1)
        acpi_log("(%i) ACPI Read  (%i) %02X: %02X\n", in_smm, size, addr, ret);
#endif
    return ret;
}

static uint32_t
acpi_reg_read_via(int size, uint16_t addr, void *priv)
{
    acpi_t  *dev = (acpi_t *) priv;
    uint32_t ret = 0x00000000;
    int      shift16;

    addr &= 0xff;
    shift16 = (addr & 1) << 3;

    switch (addr) {
        case 0x40:
            /* GPIO Direction Control */
            if (size == 1)
                ret = dev->regs.gpio_dir & 0xff;
            break;
        case 0x42:
            /* GPIO port Output Value */
            if (size == 1)
                ret = dev->regs.gpio_val & 0x13;
            break;
        case 0x44:
            /* GPIO port Input Value */
            if (size == 1) {
                ret = dev->regs.extsmi_val & 0xff;

                if (dev->i2c) {
                    ret &= 0xf9;
                    if (!(dev->regs.gpio_dir & 0x02) && i2c_gpio_get_scl(dev->i2c))
                        ret |= 0x02;
                    if (!(dev->regs.gpio_dir & 0x04) && i2c_gpio_get_sda(dev->i2c))
                        ret |= 0x04;
                }
            }
            break;
        case 0x46:
        case 0x47:
            /* GPO Port Output Value */
            ret = (dev->regs.gpo_val >> shift16) & 0xff;
            break;
        case 0x48:
        case 0x49:
            /* GPO Port Input Value */
            ret = (dev->regs.gpi_val >> shift16) & 0xff;
            break;
        default:
            ret = acpi_reg_read_via_common(size, addr, priv);
            break;
    }

#ifdef ENABLE_ACPI_LOG
    if (size != 1)
        acpi_log("(%i) ACPI Read  (%i) %02X: %02X\n", in_smm, size, addr, ret);
#endif
    return ret;
}

static uint32_t
acpi_reg_read_via_596b(int size, uint16_t addr, void *priv)
{
    const acpi_t  *dev = (acpi_t *) priv;
    uint32_t       ret = 0x00000000;
    int            shift16;
    int            shift32;

    addr &= 0x7f;
    shift16 = (addr & 1) << 3;
    shift32 = (addr & 3) << 3;

    switch (addr) {
        case 0x40: /* Extended I/O Trap Status (686A/B) */
            ret = dev->regs.extiotrapsts;
            break;
        case 0x42: /* Extended I/O Trap Enable (686A/B) */
            ret = dev->regs.extiotrapen;
            break;
        case 0x44:
        case 0x45:
            /* External SMI Input Value */
            ret = (dev->regs.extsmi_val >> shift16) & 0xff;
            break;
        case 0x48:
        case 0x49:
        case 0x4a:
        case 0x4b:
            /* GPI Port Input Value */
            ret = (dev->regs.gpi_val >> shift32) & 0xff;
            break;
        case 0x4c:
        case 0x4d:
        case 0x4e:
        case 0x4f:
            /* GPO Port Output Value */
            ret = (dev->regs.gpo_val >> shift32) & 0xff;
            break;
        default:
            ret = acpi_reg_read_via_common(size, addr, priv);
            break;
    }

#ifdef ENABLE_ACPI_LOG
    if (size != 1)
        acpi_log("(%i) ACPI Read  (%i) %02X: %02X\n", in_smm, size, addr, ret);
#endif
    return ret;
}

static uint32_t
acpi_reg_read_smc(int size, uint16_t addr, void *priv)
{
    uint32_t ret = 0x00000000;

    addr &= 0x0f;

    ret = acpi_reg_read_common_regs(size, addr, priv);

#ifdef ENABLE_ACPI_LOG
    if (size != 1)
        acpi_log("(%i) ACPI Read  (%i) %02X: %02X\n", in_smm, size, addr, ret);
#endif
    return ret;
}

static uint32_t
acpi_aux_reg_read_smc(UNUSED(int size), uint16_t addr, void *priv)
{
    const acpi_t  *dev = (acpi_t *) priv;
    uint32_t       ret = 0x00000000;
    int            shift16;

    addr &= 0x07;
    shift16 = (addr & 1) << 3;

    switch (addr) {
        case 0x00:
        case 0x01:
            /* SCI Status Register */
            ret = (dev->regs.pcntrl >> shift16) & 0xff;
            break;
        case 0x02:
        case 0x03:
            /* SCI Enable Register */
            ret = (dev->regs.gpscien >> shift16) & 0xff;
            break;
        case 0x04:
        case 0x05:
            /* Miscellaneous Status Register */
            ret = (dev->regs.glbsts >> shift16) & 0xff;
            break;
        case 0x06:
            /* Miscellaneous Enable Register */
            ret = dev->regs.glben & 0xff;
            break;
        case 0x07:
            /* Miscellaneous Control Register */
            ret = dev->regs.glbctl & 0xff;
            break;

        default:
            break;
    }

    acpi_log("(%i) ACPI Read  (%i) %02X: %02X\n", in_smm, size, addr, ret);
    return ret;
}

static uint32_t
acpi_reg_read_sis_5582(int size, uint16_t addr, void *priv)
{
    const acpi_t  *dev = (acpi_t *) priv;
    uint32_t       ret = 0x00000000;
    int            shift16;
    int            shift32;

    addr &= 0x3f;
    shift16 = (addr & 1) << 3;
    shift32 = (addr & 3) << 3;

    switch (addr) {
        case 0x0c:
        case 0x0d:
        case 0x0e:
        case 0x0f:
            ret = (dev->regs.reg_0c >> shift32) & 0xff;
            break;
        case 0x10:
        case 0x11:
            /* P_LVL2/P_LVL3: 00h, a read enters C2/C3 (SiS 5582 p. 163;
               SiS 5595 p. 138). */
            if (size == 1)
                acpi_enter_cx((acpi_t *) dev, addr & 1);
            break;
        case 0x12:
            ret = dev->regs.reg_12;
            break;
        case 0x13:
#ifdef USE_DYNAREC
            if (cpu_use_dynarec)
                update_tsc();
#endif
            ret = acpi_gp_timer_get((acpi_t *) dev) & 0xff;
            break;
        case 0x14:
        case 0x15:
            ret = (dev->regs.reg_14 >> shift16) & 0xff;
            break;
        case 0x16:
        case 0x17:
            ret = (dev->regs.reg_16 >> shift16) & 0xff;
            break;
        case 0x18:
        case 0x19:
            ret = (dev->regs.reg_18 >> shift16) & 0xff;
            break;
        case 0x1a:
        case 0x1b:
            ret = (dev->regs.reg_1a >> shift16) & 0xff;
            break;
        case 0x1c:
        case 0x1d:
            ret = (dev->regs.reg_1c >> shift16) & 0xff;
            break;
        case 0x20:
            ret = dev->regs.smi_cmd;
            break;
        case 0x24:
            ret = dev->regs.reg_24;
            break;
        case 0x25:
            ret = dev->regs.reg_25;
            break;
        case 0x26:
            ret = dev->regs.reg_26;
            break;
        case 0x28:
            ret = dev->regs.smi_en_val;
            break;
        case 0x29:
            ret = dev->regs.smi_dis_val;
            break;
        case 0x2a:
            ret = dev->regs.mail_box;
            break;
        case 0x2b:
            ret = dev->regs.reg_2b;
            break;
        default:
            ret = acpi_reg_read_common_regs(size, addr, priv);
            break;
    }

#ifdef ENABLE_ACPI_LOG
        // if (size != 1)
        // acpi_log("(%i) ACPI Read  (%i) %02X: %02X\n", in_smm, size, addr, ret);
#endif
    return ret;
}

static uint32_t
acpi_reg_read_sis_5595(int size, uint16_t addr, void *priv)
{
    const acpi_t  *dev = (acpi_t *) priv;
    uint32_t       ret = 0x00000000;
    int            shift16;
    int            shift32;

    addr &= 0x3f;
    shift16 = (addr & 1) << 3;
    shift32 = (addr & 3) << 3;

    switch (addr) {
        case 0x0c:
        case 0x0d:
        case 0x0e:
        case 0x0f:
            ret = (dev->regs.reg_0c >> shift32) & 0xff;
            break;
        case 0x10:
        case 0x11:
            /* P_LVL2/P_LVL3: 00h, a read enters C2/C3 (SiS 5582 p. 163;
               SiS 5595 p. 138). */
            if (size == 1)
                acpi_enter_cx((acpi_t *) dev, addr & 1);
            break;
        case 0x12:
            ret = dev->regs.reg_12;
            break;
        case 0x13:
            ret = dev->regs.reg_13;
            break;
        case 0x14:
        case 0x15:
        case 0x16:
        case 0x17:
            ret = (dev->regs.gpe_sts >> shift32) & 0xff;
            break;
        case 0x18:
        case 0x19:
        case 0x1a:
        case 0x1b:
            ret = (dev->regs.gpe_en >> shift32) & 0xff;
            break;
        case 0x1c:
        case 0x1d:
        case 0x1e:
            ret = (dev->regs.gpe_pin >> shift32) & 0xff;
            break;
        case 0x1f:
#ifdef USE_DYNAREC
            if (cpu_use_dynarec)
                update_tsc();
#endif
            ret = acpi_gp_timer_get((acpi_t *) dev) & 0xff;
            break;
        case 0x20:
        case 0x21:
        case 0x22:
        case 0x23:
            ret = (dev->regs.gpe_io >> shift32) & 0xff;
            break;
        case 0x24:
        case 0x25:
        case 0x26:
        case 0x27:
            ret = (dev->regs.gpe_pol >> shift32) & 0xff;
            break;
        case 0x28:
        case 0x29:
            ret = (dev->regs.gpe_mul >> shift16) & 0xff;
            break;
        case 0x2a:
        case 0x2b:
            ret = (dev->regs.gpe_ctl >> shift16) & 0xff;
            break;
        case 0x2c:
        case 0x2d:
            ret = (dev->regs.gpe_smi >> shift16) & 0xff;
            break;
        case 0x2e:
        case 0x2f:
            ret = (dev->regs.gpe_rl >> shift16) & 0xff;
            break;
        case 0x30:
            ret = dev->regs.leg_sts;
            break;
        case 0x31:
            ret = dev->regs.leg_en;
            break;
        case 0x32:
            if (dev->vendor == VEN_SIS_5595_1997)
                ret = dev->regs.smi_cmd;
            else
                ret = 0x00;
            break;
        case 0x33:
            ret = dev->regs.tst_ctl;
            break;
        case 0x34:
            if (dev->vendor == VEN_SIS_5595_1997)
                ret = dev->regs.smi_en_val;
            else
                ret = dev->regs.reg_34;
            break;
        case 0x35:
            if (dev->vendor == VEN_SIS_5595_1997)
                ret = dev->regs.smi_dis_val;
            else
                ret = dev->regs.smi_cmd;
            break;
        case 0x36:
            ret = dev->regs.mail_box;
            break;
        case 0x38:
            if (dev->vendor == VEN_SIS_5595)
                ret = smbus_sis5595_read_index(dev->smbus);
            break;
        case 0x39:
            if (dev->vendor == VEN_SIS_5595)
                ret = smbus_sis5595_read_data(dev->smbus);
            break;
        default:
            ret = acpi_reg_read_common_regs(size, addr, priv);
            break;
    }

#ifdef ENABLE_ACPI_LOG
        // if (size != 1)
        // acpi_log("(%i) ACPI Read  (%i) %02X: %02X\n", in_smm, size, addr, ret);
#endif
    return ret;
}

static void
acpi_reg_write_common_regs(UNUSED(int size), uint16_t addr, uint8_t val, void *priv)
{
    acpi_t   *dev     = (acpi_t *) priv;
    const int smi_old = acpi_smi_level(dev);
    int       shift16;
    int       sus_typ;
    uint8_t   old;

    addr &= 0x3f;
#ifdef ENABLE_ACPI_LOG
    if (size != 1)
        acpi_log("(%i) ACPI Write (%i) %02X: %02X\n", in_smm, size, addr, val);
#endif
    shift16 = (addr & 1) << 3;

    switch (addr) {
        case 0x00:
        case 0x01:
            /* PMSTS - Power Management Status Register (IO) */
            dev->regs.pmsts &= ~((val << shift16) & 0x8d31);
            if ((addr == 0x01) && (val & 0x04))
                acpi_rtc_status = 0;
            acpi_update_irq(dev);
            break;
        case 0x02:
        case 0x03:
            /* PMEN - Power Management Resume Enable Register (IO) */
            dev->regs.pmen = ((dev->regs.pmen & ~(0xff << shift16)) | (val << shift16)) & 0x0521;
            acpi_update_irq(dev);
            acpi_smi_edge(dev, smi_old);
            break;
        case 0x04:
        case 0x05:
            /* PMCNTRL - Power Management Control Register (IO) */
            old = dev->regs.pmcntrl & 0xff;
            if ((addr == 0x05) && (val & 0x20)) {
                sus_typ = dev->suspend_types[(val >> 2) & 7];
                acpi_log("ACPI suspend type %d flags %02X\n", (val >> 2) & 7, sus_typ);

                if (sus_typ & SUS_POWER_OFF) {
                    /* Soft power off. */
                    plat_power_off();
                    return;
                }

                if (sus_typ & SUS_SUSPEND) {
                    if (sus_typ & SUS_NVR) {
                        /* Suspend to RAM. */
                        nvr_reg_write(0x000f, 0xff, dev->nvr);
                    }

                    /* The machine sleeps: from the end
                       of this block of CPU time, the CPU no longer runs and
                       the timers, RTC and devices do, until a wake event
                       (acpi_sleep_run()). The resets the sleeping state
                       implies happen on the way out (acpi_wake_now()). */
                    dev->sleeping     = 1;
                    dev->sleep_typ    = (val >> 2) & 7;
                    dev->sleep_flags  = sus_typ;
                    dev->wake_pending = 0;
                    acpi_sleeping     = 1;
                    acpi_log("ACPI: sleeping (SLP_TYP %i)\n", dev->sleep_typ);
                }
            }
            dev->regs.pmcntrl = ((dev->regs.pmcntrl & ~(0xff << shift16)) | (val << shift16)) & 0x3f07 /* 0x3c07 */;
            if ((addr == 0x04) && ((old ^ val) & 0x01)) {
                acpi_update_irq(dev);
                acpi_smi_edge(dev, smi_old);
            }
            break;

        default:
            break;
    }
}

static void
acpi_reg_write_ali(int size, uint16_t addr, uint8_t val, void *priv)
{
    acpi_t *dev = (acpi_t *) priv;
    int     shift16;
    int     shift32;
    int     smi_old;

    addr &= 0x3f;
#ifdef ENABLE_ACPI_LOG
    if (size != 1)
        acpi_log("(%i) ACPI Write (%i) %02X: %02X\n", in_smm, size, addr, val);
#endif
    shift16 = (addr & 1) << 3;
    shift32 = (addr & 3) << 3;

    switch (addr) {
        case 0x10:
        case 0x11:
        case 0x12:
        case 0x13:
            /* PCNTRL - Processor Control Register (IO) */
            dev->regs.pcntrl = ((dev->regs.pcntrl & ~(0xff << shift32)) | (val << shift32)) & 0x00023e1e;
            acpi_throttle_update(dev, 0);
            break;
        case 0x14:
        case 0x15:
            /* LVL2/LVL3 - Processor Level 2/3 Register: read only. */
            break;
        case 0x18:
        case 0x19:
            /* GPE0_STS - General Purpose Event0 Status Register */
            dev->regs.gpsts &= ~((val << shift16) & 0x0d07);
            acpi_update_irq(dev);
            break;
        case 0x1a:
        case 0x1b:
            /* GPE0_EN - General Purpose Event0 Enable Register */
            smi_old        = acpi_smi_level(dev);
            dev->regs.gpen = ((dev->regs.gpen & ~(0xff << shift16)) | (val << shift16)) & 0x0d07;
            acpi_update_irq(dev);
            acpi_smi_edge(dev, smi_old);
            break;
        case 0x1d:
        case 0x1c:
            /* GPE1_STS - General Purpose Event1 Status Register */
            dev->regs.gpsts1 &= ~((val << shift16) & 0x0c01);
            break;
        case 0x1f:
        case 0x1e:
            /* GPE1_EN - General Purpose Event1 Enable Register */
            dev->regs.gpen1 = ((dev->regs.gpen1 & ~(0xff << shift16)) | (val << shift16)) & 0x0c01;
            break;
        case 0x20 ... 0x27:
            /* GPE1_CTL / BIOS Release Control: a 1 written to BIOS_RLS (20h
               bit 1) sets GBL_STS, an SCI if GBL_EN is set; nothing is stored
               (M1543C p. 203 / PDF 204). */
            if ((addr == 0x20) && (val & 0x02)) {
                dev->regs.pmsts |= GBL_STS;
                acpi_update_irq(dev);
            }
            break;
        case 0x30:
            /* PM2_CNTRL - Power Management 2 Control Register: ARB_DIS */
            dev->regs.pm2_cntrl = val & 0x01;
            break;
        default:
            acpi_reg_write_common_regs(size, addr, val, priv);
            /* Setting GBL_RLS also sets BIOS_STS and generates SMI. */
            if ((addr == 0x04) && (dev->regs.pmcntrl & 0x0004)) {
                dev->regs.gpsts1 |= 0x01;
                if (dev->regs.gpen1 & 0x01)
                    acpi_raise_smi(dev, 1);
            }
    }
}

static void
acpi_reg_write_intel(int size, uint16_t addr, uint8_t val, void *priv)
{
    acpi_t  *dev = (acpi_t *) priv;
    int      shift16;
    int      shift32;
    int      smi_old;

    addr &= 0x3f;
#ifdef ENABLE_ACPI_LOG
    if (size != 1)
        acpi_log("(%i) ACPI Write (%i) %02X: %02X\n", in_smm, size, addr, val);
#endif
    shift16 = (addr & 1) << 3;
    shift32 = (addr & 3) << 3;

    switch (addr) {
        case 0x0c:
        case 0x0d:
            /* GPSTS - General Purpose Status Register (IO) */
            dev->regs.gpsts &= ~((val << shift16) & 0x0f81);
            acpi_update_irq(dev);
            break;
        case 0x0e:
        case 0x0f:
            /* GPEN - General Purpose Enable Register (IO) */
            smi_old        = acpi_smi_level(dev);
            dev->regs.gpen = ((dev->regs.gpen & ~(0xff << shift16)) | (val << shift16)) & 0x0f01;
            acpi_update_irq(dev);
            acpi_smi_edge(dev, smi_old);
            break;
        case 0x10:
        case 0x11:
        case 0x13:
            /* PCNTRL - Processor Control Register (IO) */
            dev->regs.pcntrl = ((dev->regs.pcntrl & ~(0xff << shift32)) | (val << shift32)) & 0x00023e1e;
            acpi_throttle_update(dev, 0);
            break;
        case 0x12:
            /* PCNTRL - Processor Control Register (IO): CC_STS (bit 17) is
               read-only (PIIX4 7.2.7, p. 141 / PDF 141). */
            dev->regs.pcntrl = ((dev->regs.pcntrl & ~(0xfd << shift32)) | ((val & 0xfd) << shift32)) & 0x00023e1e;
            acpi_throttle_update(dev, 0);
            break;
        case 0x18:
        case 0x19:
            /* GLBSTS - Global Status Register (IO) */
            /* On the PIIX4, LEGACY_USB_STS (bit 1) clears only through its
               child status bits (PIIX4 7.2.10, p. 143 / PDF 143). On the
               SLC90E66 the same bit, USB_STS, is write-1-to-clear (SMSC
               SLC90E66 7.3.10, p. 143 / PDF 143). */
            dev->regs.glbsts &= ~((val << shift16) & (dev->usb_sts_w1c ? 0x0d27 : 0x0d25));
            break;
        case 0x1c:
        case 0x1d:
        case 0x1e:
        case 0x1f:
            /* DEVSTS - Device Status Register (IO) */
            dev->regs.devsts &= ~((val << shift32) & 0x3fff0fff);
            break;
        case 0x20:
        case 0x21:
            /* GLBEN - Global Enable Register (IO) */
            dev->regs.glben = ((dev->regs.glben & ~(0xff << shift16)) | (val << shift16)) & 0x8d1f;
            break;
        case 0x28:
        case 0x29:
        case 0x2a:
        case 0x2b:
            /* GLBCTL - Global Control Register (IO) */
            /* BIOS_RLS (bit 1) always reads 0 (PIIX4 7.2.13, p. 145 / PDF 145). */
            dev->regs.glbctl = ((dev->regs.glbctl & ~(0xff << shift32)) | (val << shift32)) & 0x0701ff05;
            /* Check if there's anything still pending and re-assert SMI# if it is. */
            /* A pending legacy USB status needs its GLBEN bit 0 only on the
               SLC90E66; the PIIX4 model raises it without LEGACY_USB_EN, as
               in intel_piix.c. */
            /* So does an event routed to SMI# (acpi_legacy_pending()). */
            if (((dev->regs.glbctl & 0x00010001) == 0x00010001) &&
                (((dev->regs.glbsts & 0x20) && dev->apm->do_smi) ||
                 ((dev->regs.glbsts & 0x01) && (dev->regs.glben & 0x02)) ||
                 ((dev->regs.glbsts & 0x02) && (!dev->usb_sts_w1c || (dev->regs.glben & 0x01))) ||
                 acpi_legacy_pending(dev)))
                acpi_raise_smi(dev, 1);
            /* A 1 written to BIOS_RLS sets GBL_STS, an SCI if GBL_EN is set. */
            if ((addr == 0x28) && (val & 0x02)) {
                dev->regs.pmsts |= GBL_STS;
                acpi_update_irq(dev);
            }
            break;
        case 0x2c:
        case 0x2d:
        case 0x2e:
        case 0x2f:
            /* DEVCTL - Device Control Register (IO) */
            dev->regs.devctl = ((dev->regs.devctl & ~(0xff << shift32)) | (val << shift32)) & 0x0fffffff;
            if (dev->trap_update)
                dev->trap_update(dev->trap_priv);
            break;
        case 0x34:
        case 0x35:
        case 0x36:
        case 0x37:
            /* GPOREG - General Purpose Output Register (IO) */
            dev->regs.gporeg[addr & 3] = val;
            if ((addr == 0x34) && (machines[machine].init == machine_at_cubx_init))
                hdc_onboard_enabled = (val & 0x01);
            else if ((addr == 0x37) && (machines[machine].init == machine_at_acerv66ma_init))
                sst_set_xor(!(val & 0x20));
            break;
        default:
            acpi_reg_write_common_regs(size, addr, val, priv);
            /* A 1 written to GBL_RLS sets BIOS_STS and generates SMI; the bit
               reads back (PIIX4 7.2.3, p. 139 / PDF 139). */
            if ((addr == 0x04) && (dev->regs.pmcntrl & 0x0004)) {
                dev->regs.glbsts |= 0x01;
                if (dev->regs.glben & 0x02)
                    acpi_raise_smi(dev, 1);
            }
            break;
    }
}

static void
acpi_reg_write_via_common(int size, uint16_t addr, uint8_t val, void *priv)
{
    acpi_t  *dev = (acpi_t *) priv;
    int      shift16;
    int      shift32;
    int      level;
    uint32_t old;
    /* The VT82C596B's GP_STS also has THRM_STS, LID_STS, BL_STS and
       UW_STS (VT82C596B p. 73 / PDF 79); the VT82C586B's stops at bit 9
       (p. 48 / PDF 54). */
    const uint16_t gp_mask = (dev->vendor == VEN_VIA_596B) ? 0x5fff : 0x03ff;

    addr &= 0xff;
    acpi_log("(%i) ACPI Write (%i) %02X: %02X\n", in_smm, size, addr, val);
    shift16 = (addr & 1) << 3;
    shift32 = (addr & 3) << 3;

    switch (addr) {
        case 0x10:
        case 0x11:
        case 0x12:
        case 0x13:
            /* PCNTRL - Processor Control Register (IO): bits 4-1 on the
               VT82C586B (p. 48 / PDF 54), 11-8 and 4-0 on the VT82C596B
               (p. 72 / PDF 78). */
            dev->regs.pcntrl = ((dev->regs.pcntrl & ~(0xff << shift32)) | (val << shift32)) &
                               ((dev->vendor == VEN_VIA_596B) ? 0x00000f1f : 0x0000001e);
            acpi_throttle_update(dev, 0);
            break;
        case 0x20:
        case 0x21:
            /* GPSTS - General Purpose Status Register (IO) */
            dev->regs.gpsts &= ~((val << shift16) & gp_mask);
            acpi_update_irq(dev);
            break;
        case 0x22:
        case 0x23:
            /* General Purpose SCI Enable */
            dev->regs.gpscien = ((dev->regs.gpscien & ~(0xff << shift16)) | (val << shift16)) & gp_mask;
            acpi_update_irq(dev);
            break;
        case 0x24:
        case 0x25:
            /* General Purpose SMI Enable */
            level             = acpi_via_smi_level(dev);
            dev->regs.gpsmien = ((dev->regs.gpsmien & ~(0xff << shift16)) | (val << shift16)) & gp_mask;
            acpi_smi_edge(dev, level);
            break;
        case 0x26:
        case 0x27:
            /* Power Supply Control */
            dev->regs.pscntrl = ((dev->regs.pscntrl & ~(0xff << shift16)) | (val << shift16)) & 0x0701;
            break;
        case 0x2c:
            /* GLBCTL - Global Control Register (IO) */
            level              = acpi_via_smi_level(dev);
            old                = dev->regs.glbctl;
            dev->regs.glbctl   = (dev->regs.glbctl & ~0xff) | (val & 0xff);
            dev->regs.smi_lock = !!(dev->regs.glbctl & 0x0010);
            if (!level && acpi_via_smi_level(dev))
                acpi_raise_smi(dev, 1);
            /* Setting BIOS_RLS sets GBL_STS (an SCI if GBL_EN is set); it stays
               set until GBL_STS is cleared (VT82C586B p. 51 / PDF 57). */
            if (!(old & 0x0002) && (dev->regs.glbctl & 0x0002)) {
                dev->regs.pmsts |= GBL_STS;
                acpi_update_irq(dev);
            }
            break;
        case 0x2d:
            /* GLBCTL - Global Control Register (IO) */
            dev->regs.glbctl &= ~((val << 8) & 0x0100);
            if (val & 0x01)
                dev->regs.smi_active = 0;
            break;
        case 0x2f:
            /* SMI Command */
            if (size == 1) {
                dev->regs.smicmd = val & 0xff;
                dev->regs.glbsts |= 0x40;
                if (dev->regs.glben & 0x40)
                    acpi_raise_smi(dev, 1);
            }
            break;
        case 0x38:
        case 0x39:
        case 0x3a:
        case 0x3b:
            /* GP Timer Reload Enable */
            dev->regs.gptren = ((dev->regs.gptren & ~(0xff << shift32)) | (val << shift32)) & 0x000000d9;
            break;
        default:
            old = dev->regs.pmcntrl;
            acpi_reg_write_common_regs(size, addr, val, priv);
            /* Clearing GBL_STS clears BIOS_RLS. Setting GBL_RLS sets BIOS_STS
               and, with BIOS_EN, generates SMI; it stays set until BIOS_STS is
               cleared, so writing it back set raises nothing (VT82C586B p. 47
               and 50 / PDF 53 and 56). */
            if ((addr == 0x00) && !(dev->regs.pmsts & GBL_STS))
                dev->regs.glbctl &= ~0x0002;
            else if ((addr == 0x04) && !(old & 0x0004) && (dev->regs.pmcntrl & 0x0004)) {
                dev->regs.glbsts |= 0x20;
                if (dev->regs.glben & 0x20)
                    acpi_raise_smi(dev, 1);
            }
            break;
    }
}

/* Clearing BIOS_STS clears GBL_RLS (VT82C586B p. 50 / PDF 56). */
static void
acpi_via_bios_sts_cleared(acpi_t *dev)
{
    if (!(dev->regs.glbsts & 0x20))
        dev->regs.pmcntrl &= ~0x0004;
}

static void
acpi_i2c_set(acpi_t *dev)
{
    if (dev->i2c)
        i2c_gpio_set(dev->i2c, !(dev->regs.gpio_dir & 0x02) || (dev->regs.gpio_val & 0x02), !(dev->regs.gpio_dir & 0x04) || (dev->regs.gpio_val & 0x04));
}

static void
acpi_reg_write_via(int size, uint16_t addr, uint8_t val, void *priv)
{
    acpi_t *dev = (acpi_t *) priv;
    int     shift16;
    int     shift32;
    int     level;

    addr &= 0xff;
    acpi_log("(%i) ACPI Write (%i) %02X: %02X\n", in_smm, size, addr, val);
    shift16 = (addr & 1) << 3;
    shift32 = (addr & 3) << 3;

    switch (addr) {
        case 0x28:
        case 0x29:
            /* GLBSTS - Global Status Register (IO) */
            dev->regs.glbsts &= ~((val << shift16) & 0x007f);
            acpi_via_bios_sts_cleared(dev);
            break;
        case 0x2a:
        case 0x2b:
            /* GLBEN - Global Enable Register (IO) */
            level           = acpi_via_smi_level(dev);
            dev->regs.glben = ((dev->regs.glben & ~(0xff << shift16)) | (val << shift16)) & 0x007f;
            if (!level && acpi_via_smi_level(dev))
                acpi_raise_smi(dev, 1);
            break;
        case 0x30:
        case 0x31:
        case 0x32:
        case 0x33:
            /* Primary Activity Detect Status */
            dev->regs.padsts &= ~((val << shift32) & 0x000000fd);
            break;
        case 0x34:
        case 0x35:
        case 0x36:
        case 0x37:
            /* Primary Activity Detect Enable */
            dev->regs.paden = ((dev->regs.paden & ~(0xff << shift32)) | (val << shift32)) & 0x000000fd;
            if (dev->trap_update)
                dev->trap_update(dev->trap_priv);
            break;
        case 0x40:
            /* GPIO Direction Control */
            if (size == 1) {
                dev->regs.gpio_dir = val & 0x7f;
                acpi_i2c_set(dev);
            }
            break;
        case 0x42:
            /* GPIO port Output Value */
            if (size == 1) {
                int old = dev->regs.gpio_val;

                dev->regs.gpio_val = val & 0x1f; /* GPIO0-4 on the 586B */
                acpi_i2c_set(dev);

                /* The TX100 BIOS resets the machine by driving GPIO3 low, then spins. */
                if ((machines[machine].init == machine_at_tx100_init) &&
                    (dev->regs.gpio_dir & 0x08) &&
                    (old & 0x08) && !(val & 0x08)) {
                    acpi_log("ACPI: GPIO3 driven low, resetting the machine\n");
                    pci_write(0xcf9, 0x00, NULL);
                    pci_write(0xcf9, 0x06, NULL);
                }
            }
            break;
        case 0x46:
        case 0x47:
            /* GPO Port Output Value */
            dev->regs.gpo_val = ((dev->regs.gpo_val & ~(0xff << shift16)) | (val << shift16)) & 0xffff;
            break;
        default:
            acpi_reg_write_via_common(size, addr, val, priv);
            break;
    }
}

static void
acpi_reg_write_via_596b(int size, uint16_t addr, uint8_t val, void *priv)
{
    acpi_t *dev = (acpi_t *) priv;
    int     shift16;
    int     shift32;
    int     level;

    addr &= 0x7f;
    acpi_log("(%i) ACPI Write (%i) %02X: %02X\n", in_smm, size, addr, val);
    shift16 = (addr & 1) << 3;
    shift32 = (addr & 3) << 3;

    switch (addr) {
        case 0x28:
        case 0x29:
            /* GLBSTS - Global Status Register (IO) */
            dev->regs.glbsts &= ~((val << shift16) & 0xfdff);
            acpi_via_bios_sts_cleared(dev);
            break;
        case 0x2a:
        case 0x2b:
            /* GLBEN - Global Enable Register (IO) */
            level           = acpi_via_smi_level(dev);
            dev->regs.glben = ((dev->regs.glben & ~(0xff << shift16)) | (val << shift16)) & 0xfdff;
            if (!level && acpi_via_smi_level(dev))
                acpi_raise_smi(dev, 1);
            break;
        case 0x30:
        case 0x31:
        case 0x32:
        case 0x33:
            /* Primary Activity Detect Status */
            dev->regs.padsts &= ~((val << shift32) & 0x000007ff);
            break;
        case 0x34:
        case 0x35:
        case 0x36:
        case 0x37:
            /* Primary Activity Detect Enable */
            dev->regs.paden = ((dev->regs.paden & ~(0xff << shift32)) | (val << shift32)) & 0x000007ff;
            if (dev->trap_update)
                dev->trap_update(dev->trap_priv);
            break;
        case 0x40: /* Extended I/O Trap Status (686A/B) */
            dev->regs.extiotrapsts &= ~(val & 0x13);
            break;
        case 0x42: /* Extended I/O Trap Enable (686A/B) */
            dev->regs.extiotrapen = val & 0x13;
            break;
        case 0x4c:
        case 0x4d:
        case 0x4e:
        case 0x4f:
            /* GPO Port Output Value */
            dev->regs.gpo_val = ((dev->regs.gpo_val & ~(0xff << shift32)) | (val << shift32)) & 0x7fffffff;
            break;
        default:
            acpi_reg_write_via_common(size, addr, val, priv);
            break;
    }
}

static void
acpi_reg_write_smc(int size, uint16_t addr, uint8_t val, void *priv)
{
    acpi_t *dev = (acpi_t *) priv;

    addr &= 0x0f;
    acpi_log("(%i) ACPI Write (%i) %02X: %02X\n", in_smm, size, addr, val);

    acpi_reg_write_common_regs(size, addr, val, priv);
    /* Setting GBL_RLS also sets BIOS_STS and generates SMI. */
    if ((addr == 0x00) && !(dev->regs.pmsts & 0x20))
        dev->regs.glbctl &= ~0x0001;
    else if ((addr == 0x04) && (dev->regs.pmcntrl & 0x0004)) {
        dev->regs.glbsts |= 0x01;
        if (dev->regs.glben & 0x01)
            acpi_raise_smi(dev, 1);
    }
}

static void
acpi_aux_reg_write_smc(UNUSED(int size), uint16_t addr, uint8_t val, void *priv)
{
    acpi_t *dev = (acpi_t *) priv;
    int     shift16;

    addr &= 0x07;
    acpi_log("(%i) ACPI Write (%i) %02X: %02X\n", in_smm, size, addr, val);
    shift16 = (addr & 1) << 3;

    switch (addr) {
        case 0x00:
        case 0x01:
            /* SCI Status Register */
            dev->regs.gpscists &= ~((val << shift16) & 0x000c);
            break;
        case 0x02:
        case 0x03:
            /* SCI Enable Register */
            dev->regs.gpscien = ((dev->regs.gpscien & ~(0xff << shift16)) | (val << shift16)) & 0x3fff;
            break;
        case 0x04:
        case 0x05:
            /* Miscellanous Status Register */
            dev->regs.glbsts &= ~((val << shift16) & 0x001f);
            break;
        case 0x06:
            /* Miscellaneous Enable Register */
            dev->regs.glben = (uint16_t) (val & 0x03);
            break;
        case 0x07:
            /* Miscellaneous Control Register */
            dev->regs.glbctl = (uint16_t) (val & 0x03);
            /* Setting BIOS_RLS also sets GBL_STS and generates SMI. */
            if (dev->regs.glbctl & 0x0001) {
                dev->regs.pmsts |= 0x20;
                if (dev->regs.pmen & 0x20)
                    acpi_update_irq(dev);
            }
            if (dev->regs.glbctl & 0x0002) {
                dev->regs.pmsts |= 0x10;
                if (dev->regs.pmcntrl & 0x02)
                    acpi_update_irq(dev);
            }
            break;

        default:
            break;
    }
}

void
acpi_sis5582_pmu_event(void *priv)
{
    acpi_t *dev = (acpi_t *) priv;

    dev->regs.reg_25 |= 0x02;
    if (dev->regs.reg_26 & 0x02)
        smi_raise();
}

static void
acpi_reg_write_sis_5582(int size, uint16_t addr, uint8_t val, void *priv)
{
    acpi_t *dev = (acpi_t *) priv;
    int     shift16;
    int     shift32;
    uint8_t old;

    addr &= 0x3f;
#ifdef ENABLE_ACPI_LOG
    if (size != 1)
        acpi_log("(%i) ACPI Write (%i) %02X: %02X\n", in_smm, size, addr, val);
#endif
    shift16 = (addr & 1) << 3;
    shift32 = (addr & 3) << 3;

    switch (addr) {
        case 0x0c:
        case 0x0d:
        case 0x0e:
        case 0x0f:
            /* P_CNT: bits 6-1 read/write (SiS 5582 ACPI 0Ch, p. 162). */
            dev->regs.reg_0c = ((dev->regs.reg_0c & ~(0xff << shift32)) | (val << shift32)) & 0x0000007e;
            acpi_throttle_update(dev, 0);
            break;
        case 0x10:
        case 0x11:
            /* P_LVL2/P_LVL3: writes have no effect. */
            break;
        case 0x12:
            dev->regs.reg_12 = val & 0x01;
            break;
        case 0x13:
            dev->regs.reg_13 = val;
            acpi_gp_timer_update(dev, (val != 0x00) && (dev->regs.reg_16 & 0x2000), val);
            break;
        case 0x14:
        case 0x15:
            dev->regs.reg_14 &= ~((val << shift32) & 0xff9f);
            acpi_update_irq(dev);
            break;
        case 0x16:
        case 0x17:
            dev->regs.reg_16 = ((dev->regs.reg_16 & ~(0xff << shift16)) | (val << shift16)) & 0xff1f;
            acpi_update_irq(dev);
            break;
        case 0x18:
        case 0x19:
            dev->regs.reg_18 = ((dev->regs.reg_18 & ~(0xff << shift16)) | (val << shift16)) & 0x07ff;
            break;
        case 0x1a:
        case 0x1b:
            dev->regs.reg_1a = ((dev->regs.reg_1a & ~(0xff << shift16)) | (val << shift16)) & 0x0387;
            break;
        case 0x1c:
        case 0x1d:
            dev->regs.reg_1c = ((dev->regs.reg_1c & ~(0xff << shift16)) | (val << shift16)) & 0x3f7f;
            /* Setting BIOS_RLS also sets GBL_STS and generates SMI. */
            if (dev->regs.reg_1c & 0x0400) {
                dev->regs.pmsts |= 0x20;
                if (dev->regs.pmen & 0x20)
                    acpi_update_irq(dev);
            }
            break;
        case 0x20:
            /* SMI Command Port */
            dev->regs.smi_cmd = val;
            if (val == dev->regs.smi_en_val) {
                dev->regs.reg_25 |= 0x08;
                if (dev->regs.reg_26 & 0x08)
                    smi_raise();
            } else if (val == dev->regs.smi_dis_val) {
                dev->regs.reg_25 |= 0x10;
                if (dev->regs.reg_26 & 0x10)
                    smi_raise();
            }
            break;
        case 0x24:
            dev->regs.reg_24 = val & 0x43;
            break;
        case 0x25:
            dev->regs.reg_25 &= ~(val & 0x1f);
            break;
        case 0x26:
            old = dev->regs.reg_26;
            dev->regs.reg_26 = val & 0x3f;
            if (!(old & 0x04) && (val & 0x04))
                timer_on_auto(&dev->per_timer, 16000000.0);
            else if ((old & 0x04) && !(val & 0x04))
                timer_stop(&dev->per_timer);
            break;
        case 0x28:
            dev->regs.smi_en_val = val;
            break;
        case 0x29:
            dev->regs.smi_dis_val = val;
            break;
        case 0x2a:
            dev->regs.mail_box = val;
            break;
        case 0x2b:
            dev->regs.reg_2b = val & 0x01;
            break;
        default:
            acpi_reg_write_common_regs(size, addr, val, priv);
            /* Setting GBL_RLS also sets BIOS_STS and generates SMI. */
            if ((addr == 0x00) && !(dev->regs.pmsts & 0x20))
                dev->regs.reg_1c &= ~0x0400;
            else if ((addr == 0x04) && (dev->regs.pmcntrl & 0x0004)) {
                dev->regs.reg_25 |= 0x01;
                if (dev->regs.reg_26 & 0x01)
                    acpi_raise_smi(dev, 1);
            }
            break;
    }
}

/* A PMU event enabled in PMU 60h-63h raises SMI# with only SMI_EN set (SiS
   5595 November 1998, p. 121 / PDF 127). PMU_EN, GPE_EN bit 12, gates the
   SCI side, from PMU 80h-83h (p. 141 and 143 / PDF 147 and 149). */
void
acpi_sis5595_pmu_event(void *priv)
{
    acpi_sis5595_smi_raise(priv);
}

/* USB_STS and USB_EN: ACPI 14h and 16h bit 14 on the 5582 (SiS 5581/5582
   p. 163 and 165 / PDF 163 and 165); GPE_STS and GPE_EN bit 30 in the
   December 1997 5595 data sheet (p. 115 and 117 / PDF 115 and 117), bit 8 in
   the November 1998 one (p. 141 and 143 / PDF 147 and 149). */
static uint32_t
acpi_sis_usb_pme_bit(const acpi_t *dev)
{
    switch (dev->vendor) {
        case VEN_SIS_5582:
            return 0x00004000;
        case VEN_SIS_5595_1997:
            return 0x40000000;
        case VEN_SIS_5595:
            return 0x00000100;
        default:
            return 0;
    }
}

/* The USB power-management event: an interrupt or SMI output of the USB
   function sets USB_STS, and with USB_EN set it raises SCI, or SMI# while
   SCI_EN is clear (SiS 5581/5582 p. 47 and 54 / PDF 47 and 54; SiS 5595
   November 1998, p. 26-27 / PDF 32-33). */
void
acpi_sis_usb_event(acpi_t *dev)
{
    uint32_t bit = acpi_sis_usb_pme_bit(dev);

    if (bit == 0)
        return;
    if (dev->vendor == VEN_SIS_5582) {
        dev->regs.reg_14 |= bit;
        if (!(dev->regs.reg_16 & bit))
            return;
    } else {
        dev->regs.gpe_sts |= bit;
        if (!(dev->regs.gpe_en & bit))
            return;
    }

    if (dev->regs.pmcntrl & 0x01)
        acpi_update_irq(dev);
    else if (dev->vendor == VEN_SIS_5582)
        smi_raise();
    else
        acpi_sis5595_smi_raise(dev);
}

/* USB SMI#: with USBSMI_EN (LEG_EN bit 6) set, a rising edge of the OHCI's
   SMI output sets USBSMI_STS (LEG_STS bit 6) and raises SMI#, subject to
   SMI_EN (LEG_EN bit 5) (SiS 5595 November 1998, p. 150-151 /
   PDF 156-157). */
void
acpi_sis5595_usb_smi(void *priv, int level)
{
    acpi_t *dev = (acpi_t *) priv;

    if (level && !dev->usb_smi_level && (dev->regs.leg_en & 0x40)) {
        dev->regs.leg_sts |= 0x40;
        acpi_sis5595_smi_raise(dev);
    }
    dev->usb_smi_level = !!level;
}

/* The OHCI holds its SMI output while an enabled event is pending (OpenHCI
   1.0a s6.5, p. 105 / PDF 119), and the handler clears USBSMI_STS before it
   exits (SiS 5595 November 1998, p. 150 / PDF 156). Clearing USBSMI_STS, or
   setting SMI_EN (LEG_EN bit 5), while the output is still high sets the
   status again and raises SMI#. */
static void
acpi_sis5595_usb_smi_held(acpi_t *dev, uint8_t old_en)
{
    if (!dev->usb_smi_level || !(dev->regs.leg_en & 0x40))
        return;
    if (!(dev->regs.leg_sts & 0x40) || !(old_en & 0x20)) {
        dev->regs.leg_sts |= 0x40;
        acpi_sis5595_smi_raise(dev);
    }
}

void
acpi_sis5595_smbus_event(void *priv)
{
    acpi_t *dev = (acpi_t *) priv;

    if (dev->regs.gpe_en & 0x00000800) {
        dev->regs.gpe_sts |= 0x00000800;
        acpi_sis5595_smi_raise(dev);
    }
}

void
acpi_sis5595_software_smi(void *priv)
{
    acpi_t *dev = (acpi_t *) priv;

    if (dev->regs.leg_en & 0x01) {
        dev->regs.leg_sts |= 0x01;
        acpi_sis5595_smi_raise(dev);
    }
}

static void
acpi_reg_write_sis_5595(int size, uint16_t addr, uint8_t val, void *priv)
{
    acpi_t *dev = (acpi_t *) priv;
    int     shift16;
    int     shift32;
    uint8_t old;
    uint8_t do_smi = 0;

    addr &= 0x3f;
#ifdef ENABLE_ACPI_LOG
    if (size != 1)
        acpi_log("(%i) ACPI Write (%i) %02X: %02X\n", in_smm, size, addr, val);
#endif
    shift16 = (addr & 1) << 3;
    shift32 = (addr & 3) << 3;

    switch (addr) {
        case 0x0c:
        case 0x0d:
        case 0x0e:
        case 0x0f:
            /* P_CTL: bits 4-1 read/write (SiS 5595 ACPI 0Ch, pp. 137-138). */
            dev->regs.reg_0c = ((dev->regs.reg_0c & ~(0xff << shift32)) | (val << shift32)) & 0x0000001e;
            acpi_throttle_update(dev, 0);
            break;
        case 0x10:
        case 0x11:
            /* P_LVL2/P_LVL3: writes have no effect. */
            break;
        case 0x12:
            dev->regs.reg_12 = val & 0x01;
            break;
        case 0x13:
            dev->regs.reg_13 = val;
            /* Setting BIOS_RLS also sets GBL_STS and generates SMI. */
            if (dev->regs.reg_13 & 0x02) {
                dev->regs.pmsts |= 0x20;
                if (dev->regs.pmen & 0x20)
                    acpi_update_irq(dev);
            }
            break;
        case 0x14:
        case 0x15:
        case 0x16:
        case 0x17:
            if (dev->vendor == VEN_SIS_5595_1997)
                dev->regs.gpe_sts &= ~((val << shift32) & 0xff03ffbf);
            else
                dev->regs.gpe_sts &= ~((val << shift32) & 0xff83ffff);
            acpi_update_irq(dev);
            break;
        case 0x18:
        case 0x19:
        case 0x1a:
        case 0x1b:
            if (dev->vendor == VEN_SIS_5595_1997)
                dev->regs.gpe_en = ((dev->regs.gpe_en & ~(0xff << shift32)) | (val << shift32));
            else
                dev->regs.gpe_en = ((dev->regs.gpe_en & ~(0xff << shift32)) |
                                   (val << shift32)) & 0xff83ffff;
            acpi_update_irq(dev);
            break;
        case 0x1c:
            dev->regs.gpe_pin = ((dev->regs.gpe_pin & ~(0xff << shift32)) | ((val & 0xff) << shift32));
            if ((machines[machine].init == machine_at_m747_init) && (val & 0x10) &&
                !(dev->regs.gpe_io & 0x00000010))
                resetx86();
            break;
        case 0x1d:
            dev->regs.gpe_pin = ((dev->regs.gpe_pin & ~(0x0f << shift32)) | ((val & 0x0f) << shift32));
            if ((machines[machine].init == machine_at_m748mr_init) && (val & 0x02) &&
                !(dev->regs.gpe_io & 0x00000200)) {
                acpi_log("ACPI: GPIO9 driven high, resetting the machine\n");
                pci_write(0xcf9, 0x06, NULL);
                }
            break;
        case 0x1e:
            dev->regs.gpe_pin = ((dev->regs.gpe_pin & ~(0x03 << shift32)) | ((val & 0x03) << shift32));
            break;
        case 0x1f:
            dev->regs.gp_tmr = val;
            acpi_gp_timer_update(dev, (val != 0x00) && (dev->regs.gpe_en & 0x00000400), val);
            break;
        case 0x20:
            dev->regs.gpe_io = ((dev->regs.gpe_io & ~(0x9f << shift32)) | ((val & 0x9f) << shift32));
            break;
        case 0x21:
            dev->regs.gpe_io = ((dev->regs.gpe_io & ~(0x0f << shift32)) | ((val & 0x0f) << shift32));
            break;
        case 0x22:
            dev->regs.gpe_io = ((dev->regs.gpe_io & ~(0x03 << shift32)) | ((val & 0x03) << shift32));
            break;
        case 0x24:
            dev->regs.gpe_pol = ((dev->regs.gpe_pol & ~(0xbf << shift32)) | ((val & 0xbf) << shift32));
            break;
        case 0x25:
            dev->regs.gpe_pol = ((dev->regs.gpe_pol & ~(0x0f << shift32)) | ((val & 0x0f) << shift32));
            break;
        case 0x26:
            dev->regs.gpe_pol = ((dev->regs.gpe_pol & ~(0x03 << shift32)) | ((val & 0x03) << shift32));
            break;
        case 0x27:
            dev->regs.gpe_pol = ((dev->regs.gpe_pol & ~(0x10 << shift32)) | ((val & 0x10) << shift32));
            break;
        case 0x28:
            dev->regs.gpe_mul = ((dev->regs.gpe_mul & ~(0xf9 << shift16)) | ((val & 0xf9) << shift16));
            break;
        case 0x29:
            dev->regs.gpe_mul = ((dev->regs.gpe_mul & ~(0x13 << shift16)) | ((val & 0x13) << shift16));
            break;
        case 0x2a:
        case 0x2b:
            dev->regs.gpe_ctl = ((dev->regs.gpe_ctl & ~(0xff << shift16)) | ((val & 0xff) << shift16));
            break;
        case 0x2c:
        case 0x2d:
            dev->regs.gpe_smi = ((dev->regs.gpe_smi & ~(0xff << shift16)) | ((val & 0xff) << shift16));
            break;
        case 0x2e:
        case 0x2f:
            dev->regs.gpe_rl = ((dev->regs.gpe_rl & ~(0xff << shift16)) | ((val & 0xff) << shift16));
            break;
        case 0x30:
            dev->regs.leg_sts &= ~val;
            acpi_sis5595_usb_smi_held(dev, dev->regs.leg_en);
            break;
        case 0x31:
            old = dev->regs.leg_en;
            dev->regs.leg_en = val;
            acpi_sis5595_usb_smi_held(dev, old);
            if (!(old & 0x04) && (val & 0x04))
                timer_on_auto(&dev->per_timer, 16000000.0);
            else if ((old & 0x04) && !(val & 0x04))
                timer_stop(&dev->per_timer);
            break;
        case 0x32:
            if (dev->vendor == VEN_SIS_5595_1997) {
                /* SMI Command Port */
                dev->regs.smi_cmd = val;
                if (val == dev->regs.smi_en_val) {
                    dev->regs.leg_sts |= 0x08;
                    if (dev->regs.leg_en & 0x08)
                        acpi_sis5595_smi_raise(dev);
                } else if (val == dev->regs.smi_dis_val) {
                    dev->regs.leg_sts |= 0x10;
                    if (dev->regs.leg_en & 0x10)
                        acpi_sis5595_smi_raise(dev);
                }
            }
            break;
        case 0x33:
            dev->regs.tst_ctl = val & 0x01;
            break;
        case 0x34:
            if (dev->vendor == VEN_SIS_5595_1997)
                dev->regs.smi_en_val = val;
            else
                dev->regs.reg_34 = val;
            break;
        case 0x35:
            if (dev->vendor == VEN_SIS_5595_1997)
                dev->regs.smi_dis_val = val;
            else {
                /* SMI Command Port */
                dev->regs.smi_cmd = val;
                dev->regs.leg_sts |= 0x10;
                if (dev->regs.leg_en & 0x10)
                    acpi_sis5595_smi_raise(dev);
            }
            break;
        case 0x36:
            dev->regs.mail_box = val;
            break;
        case 0x38:
            if (dev->vendor == VEN_SIS_5595) {
                dev->regs.index = val;
                smbus_sis5595_write_index(dev->smbus, val);
            }
            break;
        case 0x39:
            if (dev->vendor == VEN_SIS_5595) {
                dev->regs.reg_ff = val & 0x3f;
                smbus_sis5595_write_data(dev->smbus, val);
                if (val & 0x20) {           /* Set GPIO5_STS of GPE_STS */
                    dev->regs.gpe_sts |= 0x00000008;
                    do_smi |= (dev->regs.gpe_en & 0x00000004);
                } else if (val & 0x10) {    /* Set GPIO10_STS of GPE_STS */
                    dev->regs.gpe_sts |= 0x00000004;
                    do_smi |= (dev->regs.gpe_en & 0x00000008);
                } else if (val & 0x08) {    /* Set RI_STS in GPE_STS */
                    dev->regs.gpe_sts |= 0x00000002;
                    do_smi |= (dev->regs.gpe_en & 0x00000002);
                } else if (val & 0x04)      /* Set WAK_STS of PM1_STS */
                    dev->regs.pmsts |= 0x8000;
                else if (val & 0x02) {      /* Set RTC_STS of PM1_STS */
                    dev->regs.pmsts |= 0x0400;
                    do_smi |= (dev->regs.pmen & 0x0400);
                } else if (val & 0x01) {    /* Set PWRBTN_STS of PM1_STS */
                    dev->regs.pmsts |= 0x0100;
                    do_smi |= (dev->regs.pmen & 0x0100);
                }

                if (do_smi)
                    acpi_sis5595_smi_raise(dev);
            }
            break;
        default:
            acpi_reg_write_common_regs(size, addr, val, priv);
            /* Setting GBL_RLS also sets BIOS_STS and generates SMI. */
            if ((addr == 0x00) && !(dev->regs.pmsts & 0x20))
                dev->regs.reg_13 &= ~0x02;
            else if ((addr == 0x04) && (dev->regs.pmcntrl & 0x0004)) {
                dev->regs.leg_sts |= 0x01;
                if (dev->regs.leg_en & 0x01)
                    acpi_sis5595_smi_raise(dev);
            }
            break;
    }
}

static uint32_t
acpi_reg_read_common(int size, uint16_t addr, void *priv)
{
    const acpi_t *dev = (acpi_t *) priv;
    uint8_t ret = 0xff;

    switch (dev->vendor) {
        case VEN_ALI:
            ret = acpi_reg_read_ali(size, addr, priv);
            break;
        case VEN_VIA:
            ret = acpi_reg_read_via(size, addr, priv);
            break;
        case VEN_VIA_596B:
            ret = acpi_reg_read_via_596b(size, addr, priv);
            break;
        case VEN_INTEL:
            ret = acpi_reg_read_intel(size, addr, priv);
            break;
        case VEN_SMC:
            ret = acpi_reg_read_smc(size, addr, priv);
            break;
        case VEN_SIS_5582:
            ret = acpi_reg_read_sis_5582(size, addr, priv);
            break;
        case VEN_SIS_5595_1997:
        case VEN_SIS_5595:
            ret = acpi_reg_read_sis_5595(size, addr, priv);
            break;
    }

    return ret;
}

static void
acpi_reg_write_common(int size, uint16_t addr, uint8_t val, void *priv)
{
    const acpi_t *dev = (acpi_t *) priv;

    switch (dev->vendor) {
        case VEN_ALI:
            acpi_reg_write_ali(size, addr, val, priv);
            break;
        case VEN_VIA:
            acpi_reg_write_via(size, addr, val, priv);
            break;
        case VEN_VIA_596B:
            acpi_reg_write_via_596b(size, addr, val, priv);
            break;
        case VEN_INTEL:
            acpi_reg_write_intel(size, addr, val, priv);
            break;
        case VEN_SMC:
            acpi_reg_write_smc(size, addr, val, priv);
            break;
        case VEN_SIS_5582:
            acpi_reg_write_sis_5582(size, addr, val, priv);
            break;
        case VEN_SIS_5595_1997:
        case VEN_SIS_5595:
            acpi_reg_write_sis_5595(size, addr, val, priv);
            break;
    }
}

static uint32_t
acpi_aux_reg_read_common(int size, uint16_t addr, void *priv)
{
    const acpi_t *dev = (acpi_t *) priv;
    uint8_t ret = 0xff;

    if (dev->vendor == VEN_SMC)
        ret = acpi_aux_reg_read_smc(size, addr, priv);

    return ret;
}

static void
acpi_aux_reg_write_common(int size, uint16_t addr, uint8_t val, void *priv)
{
    const acpi_t *dev = (acpi_t *) priv;

    if (dev->vendor == VEN_SMC)
        acpi_aux_reg_write_smc(size, addr, val, priv);
}

static void
acpi_latch_pmtmr(acpi_t *dev)
{
#ifdef USE_DYNAREC
    /* The compiled block's cycles so far reach the TSC only through
       update_tsc(): before the read, as RDTSC does. */
    if (cpu_use_dynarec)
        update_tsc();
#endif
    dev->pmtmr_latch = acpi_timer_get(dev);

    /* On real hardware, reading the timer takes longer than one of its ticks, so
       two reads never return the same value, which Windows Vista and 7 rely on to
       avoid a division by zero. Charge the read one tick, rather than returning the
       value plus one when it has not changed: that made the timer go backwards, on
       a third read within the same tick. */
    cycles -= acpi_pmtmr_read_cycles;
}

static uint32_t
acpi_reg_readl(uint16_t addr, void *priv)
{
    acpi_t *       dev = (acpi_t *) priv;
    const uint16_t reg = addr - dev->io_base;

    uint32_t       ret = 0x00000000;

    if ((reg >= 0x0008) && (reg <= 0x000b))
        acpi_latch_pmtmr(dev);

    ret = acpi_reg_read_common(4, addr, priv);
    ret |= (acpi_reg_read_common(4, addr + 1, priv) << 8);
    ret |= (acpi_reg_read_common(4, addr + 2, priv) << 16);
    ret |= (acpi_reg_read_common(4, addr + 3, priv) << 24);

    acpi_log("ACPI: Read L %08X from %04X\n", ret, addr);

    return ret;
}

static uint16_t
acpi_reg_readw(uint16_t addr, void *priv)
{
    acpi_t *       dev = (acpi_t *) priv;
    const uint16_t reg = addr - dev->io_base;

    uint16_t       ret = 0x0000;

    if ((reg >= 0x0008) && (reg <= 0x000b))
        acpi_latch_pmtmr(dev);

    ret = acpi_reg_read_common(2, addr, priv);
    ret |= (acpi_reg_read_common(2, addr + 1, priv) << 8);

    acpi_log("ACPI: Read W %08X from %04X\n", ret, addr);

    return ret;
}

static uint8_t
acpi_reg_read(uint16_t addr, void *priv)
{
    acpi_t *       dev = (acpi_t *) priv;
    const uint16_t reg = addr - dev->io_base;

    uint8_t        ret = 0x00;

    if ((reg >= 0x0008) && (reg <= 0x000b))
        acpi_latch_pmtmr(dev);

    ret = acpi_reg_read_common(1, addr, priv);

    acpi_log("ACPI: Read B %02X from %04X\n", ret, addr);

    return ret;
}

static uint32_t
acpi_aux_reg_readl(uint16_t addr, void *priv)
{
    uint32_t ret = 0x00000000;

    ret = acpi_aux_reg_read_common(4, addr, priv);
    ret |= (acpi_aux_reg_read_common(4, addr + 1, priv) << 8);
    ret |= (acpi_aux_reg_read_common(4, addr + 2, priv) << 16);
    ret |= (acpi_aux_reg_read_common(4, addr + 3, priv) << 24);

    acpi_log("ACPI: Read Aux L %08X from %04X\n", ret, addr);

    return ret;
}

static uint16_t
acpi_aux_reg_readw(uint16_t addr, void *priv)
{
    uint16_t ret = 0x0000;

    ret = acpi_aux_reg_read_common(2, addr, priv);
    ret |= (acpi_aux_reg_read_common(2, addr + 1, priv) << 8);

    acpi_log("ACPI: Read Aux W %04X from %04X\n", ret, addr);

    return ret;
}

static uint8_t
acpi_aux_reg_read(uint16_t addr, void *priv)
{
    uint8_t ret = 0x00;

    ret = acpi_aux_reg_read_common(1, addr, priv);

    acpi_log("ACPI: Read Aux B %02X from %04X\n", ret, addr);

    return ret;
}

static void
acpi_reg_writel(uint16_t addr, uint32_t val, void *priv)
{
    acpi_log("ACPI: Write L %08X to %04X\n", val, addr);

    acpi_reg_write_common(4, addr, val & 0xff, priv);
    acpi_reg_write_common(4, addr + 1, (val >> 8) & 0xff, priv);
    acpi_reg_write_common(4, addr + 2, (val >> 16) & 0xff, priv);
    acpi_reg_write_common(4, addr + 3, (val >> 24) & 0xff, priv);
}

static void
acpi_reg_writew(uint16_t addr, uint16_t val, void *priv)
{
    acpi_log("ACPI: Write W %04X to %04X\n", val, addr);

    acpi_reg_write_common(2, addr, val & 0xff, priv);
    acpi_reg_write_common(2, addr + 1, (val >> 8) & 0xff, priv);
}

static void
acpi_reg_write(uint16_t addr, uint8_t val, void *priv)
{
    acpi_log("ACPI: Write B %02X to %04X\n", val, addr);

    acpi_reg_write_common(1, addr, val, priv);
}

static void
acpi_aux_reg_writel(uint16_t addr, uint32_t val, void *priv)
{
    acpi_log("ACPI: Write Aux L %08X to %04X\n", val, addr);

    acpi_aux_reg_write_common(4, addr, val & 0xff, priv);
    acpi_aux_reg_write_common(4, addr + 1, (val >> 8) & 0xff, priv);
    acpi_aux_reg_write_common(4, addr + 2, (val >> 16) & 0xff, priv);
    acpi_aux_reg_write_common(4, addr + 3, (val >> 24) & 0xff, priv);
}

static void
acpi_aux_reg_writew(uint16_t addr, uint16_t val, void *priv)
{
    acpi_log("ACPI: Write Aux W %04X to %04X\n", val, addr);

    acpi_aux_reg_write_common(2, addr, val & 0xff, priv);
    acpi_aux_reg_write_common(2, addr + 1, (val >> 8) & 0xff, priv);
}

static void
acpi_aux_reg_write(uint16_t addr, uint8_t val, void *priv)
{
    acpi_log("ACPI: Write Aux B %02X to %04X\n", val, addr);

    acpi_aux_reg_write_common(1, addr, val, priv);
}

void
acpi_update_io_mapping(acpi_t *dev, uint32_t base, int chipset_en)
{
    int size;

    switch (dev->vendor) {
        default:
        case VEN_ALI:
        case VEN_INTEL:
        case VEN_SIS_5582:
        case VEN_SIS_5595_1997:
        case VEN_SIS_5595:
            size = 0x040;
            break;
        case VEN_SMC:
            size = 0x010;
            break;
        case VEN_VIA:
            size = 0x100;
            break;
        case VEN_VIA_596B:
            size = 0x080;
            break;
    }

    acpi_log("ACPI: Update I/O %04X to %04X (%sabled)\n", dev->io_base, base, chipset_en ? "en" : "dis");

    if (dev->io_base != 0x0000) {
        io_removehandler(dev->io_base, size,
                         acpi_reg_read, acpi_reg_readw, acpi_reg_readl,
                         acpi_reg_write, acpi_reg_writew, acpi_reg_writel, dev);
    }

    dev->io_base = base;

    if (chipset_en && (dev->io_base != 0x0000)) {
        io_sethandler(dev->io_base, size,
                      acpi_reg_read, acpi_reg_readw, acpi_reg_readl,
                      acpi_reg_write, acpi_reg_writew, acpi_reg_writel, dev);
    }
}

void
acpi_update_aux_io_mapping(acpi_t *dev, uint32_t base, int chipset_en)
{
    int size;

    switch (dev->vendor) {
        case VEN_SMC:
            size = 0x008;
            break;
        default:
            size = 0x000;
            break;
    }

    acpi_log("ACPI: Update Aux I/O %04X to %04X (%sabled)\n", dev->aux_io_base, base, chipset_en ? "en" : "dis");

    if (dev->aux_io_base != 0x0000) {
        io_removehandler(dev->aux_io_base, size,
                         acpi_aux_reg_read, acpi_aux_reg_readw, acpi_aux_reg_readl,
                         acpi_aux_reg_write, acpi_aux_reg_writew, acpi_aux_reg_writel, dev);
    }

    dev->aux_io_base = base;

    if (chipset_en && (dev->aux_io_base != 0x0000)) {
        io_sethandler(dev->aux_io_base, size,
                      acpi_aux_reg_read, acpi_aux_reg_readw, acpi_aux_reg_readl,
                      acpi_aux_reg_write, acpi_aux_reg_writew, acpi_aux_reg_writel, dev);
    }
}

static void
acpi_timer_resume(void *priv)
{
    acpi_t *dev = (acpi_t *) priv;

    dev->regs.pmsts |= 0x8000;

    /* Nasty workaround for ASUS P2B-LS and potentially others, where the PMCNTRL
       SMI trap handler clears the resume bit before returning control to the OS. */
    if (in_smm)
        timer_set_delay_u64(&dev->resume_timer, 50 * TIMER_USEC);
}

void
acpi_init_gporeg(acpi_t *dev, uint8_t val0, uint8_t val1, uint8_t val2, uint8_t val3)
{
    dev->regs.gporeg[0] = dev->gporeg_default[0] = val0;
    dev->regs.gporeg[1] = dev->gporeg_default[1] = val1;
    dev->regs.gporeg[2] = dev->gporeg_default[2] = val2;
    dev->regs.gporeg[3] = dev->gporeg_default[3] = val3;
    acpi_log("acpi_init_gporeg(): %02X %02X %02X %02X\n", dev->regs.gporeg[0], dev->regs.gporeg[1], dev->regs.gporeg[2], dev->regs.gporeg[3]);
}

void
acpi_set_timer32(acpi_t *dev, uint8_t timer32)
{
    dev->regs.timer32 = timer32;
    /* The top bit moves: re-aim the TMR_STS timer. */
    if (timer_is_enabled(&dev->timer))
        acpi_timer_update(dev, true);
}

void
acpi_set_piix4_ab(acpi_t *dev, int ab)
{
    dev->piix4_ab = ab;
}

void
acpi_set_slot(acpi_t *dev, int slot)
{
    dev->slot = slot;
}

void
acpi_set_irq_mode(acpi_t *dev, int irq_mode)
{
    dev->irq_mode = irq_mode;
}

void
acpi_set_irq_pin(acpi_t *dev, int irq_pin)
{
    dev->irq_pin = irq_pin;
}

void
acpi_set_irq_line(acpi_t *dev, int irq_line)
{
    dev->irq_line = irq_line;
}

void
acpi_set_mirq_is_level(acpi_t *dev, int mirq_is_level)
{
    dev->mirq_is_level = mirq_is_level;
}

void
acpi_set_usb_sts_w1c(acpi_t *dev, int w1c)
{
    dev->usb_sts_w1c = w1c;
}

void
acpi_set_gpireg2_default(acpi_t *dev, uint8_t gpireg2_default)
{
    dev->gpireg2_default = gpireg2_default;
    dev->regs.gpireg[2]  = dev->gpireg2_default;
}

void
acpi_set_nvr(acpi_t *dev, nvr_t *nvr)
{
    dev->nvr = nvr;
}

void
acpi_set_trap_update(acpi_t *dev, void (*update)(void *priv), void *priv)
{
    dev->trap_update = update;
    dev->trap_priv   = priv;
}

void
acpi_set_sci_hook(acpi_t *dev, void (*hook)(void *priv, int set), void *priv)
{
    dev->sci_hook      = hook;
    dev->sci_hook_priv = priv;
}

uint8_t
acpi_ali_soft_smi_status_read(acpi_t *dev)
{
    return dev->regs.ali_soft_smi;
}

void
acpi_ali_soft_smi_status_write(acpi_t *dev, uint8_t soft_smi)
{
    dev->regs.ali_soft_smi = soft_smi;
}

void
acpi_set_event_hook(acpi_t *dev, int (*hook)(void *priv, int event), void *priv)
{
    dev->event_hook      = hook;
    dev->event_hook_priv = priv;
}

void
acpi_pwrbut_press(void)
{
    acpi_pwrbut_pressed = 1;
}

void
acpi_pwrbut_hold(void)
{
    acpi_pwrbut_hold_req = 1;
}

/* A press of the power button. It always sets PWRBTN_STS; PWRBTN_EN
   only decides whether it raises SCI (ACPI 1.0 Table 4-9), or SMI# in legacy
   mode (acpi_legacy_pending()). While the machine sleeps a press wakes it,
   whatever PWRBTN_EN says (ACPI 1.0 4.7.2.2.1.1, p. 4-48; PIIX4 7.2.2,
   p. 138: "The PWRBTN# signal is always enabled to generate resume
   events"). */
static void
acpi_pwrbtn_press(acpi_t *dev)
{
    if (dev->event_hook != NULL)
        dev->event_hook(dev->event_hook_priv, ACPI_HOOK_PWRBTN);

    if (dev->sleeping) {
        dev->regs.pmsts |= PWRBTN_STS;
        dev->wake_pending = 1;
    } else
        acpi_pm1_event(dev, PWRBTN_STS);
}

/* The power button held for four seconds: the hardware clears
   PWRBTN_STS and goes to soft off whatever software does (ACPI 1.0
   4.7.2.2.1.3, p. 4-49). The PIIX4 always does this (PIIX4 7.2.1, p. 137,
   and specification update 9, p. 41) and sets PWRBTNOR_STS; the VT82C586B
   sets PBOR_STS (p. 46 / PDF 52), the VT82C596B bit 11 (PWR_STS, "set by
   abnormal power off", p. 70 / PDF 76). The M7101 has it turned off by
   B4h bit 2 (M1543C p. 158 / PDF 159), the SiS 5595 on with ACPI 13h bit 5
   (SiS 5595 p. 25 / PDF 31). Soft off, a state no one sees, ends the
   emulation. */
static int
acpi_pwrbtn_override(acpi_t *dev)
{
    if ((dev->event_hook != NULL) && !dev->event_hook(dev->event_hook_priv, ACPI_HOOK_OVERRIDE))
        return 0;
    if (((dev->vendor == VEN_SIS_5595_1997) || (dev->vendor == VEN_SIS_5595)) &&
        !(dev->regs.reg_13 & 0x20))
        return 0;

    dev->regs.pmsts &= ~PWRBTN_STS;
    if ((dev->vendor == VEN_INTEL) || (dev->vendor == VEN_VIA) || (dev->vendor == VEN_VIA_596B))
        dev->regs.pmsts |= PWRBTNOR_STS;
    acpi_update_irq(dev);

    acpi_log("ACPI: power button override, soft off\n");
    plat_power_off();
    return 1;
}

void
acpi_pwrbtn_timer(void *priv)
{
    acpi_t *dev = (acpi_t *) priv;

    timer_on_auto(&dev->pwrbtn_timer, 16. * 1000.);
    if (atomic_exchange(&acpi_pwrbut_hold_req, 0) && !dev->pwrbtn_held) {
        acpi_pwrbut_pressed = 0;
        dev->pwrbtn_held    = 1;
        acpi_pwrbtn_press(dev);
    } else if (acpi_pwrbut_pressed) {
        acpi_pwrbut_pressed = 0;
        acpi_pwrbtn_press(dev);
    }
    if (dev->pwrbtn_held) {
        dev->pwrbtn_held += 16;
        if (dev->pwrbtn_held >= ACPI_PWRBTN_OVERRIDE_MS) {
            if (acpi_pwrbtn_override(dev) || (dev->pwrbtn_held >= ACPI_PWRBTN_HOLD_MS))
                dev->pwrbtn_held = 0;
        }
    }
    /* RTC_STS is set from the PIC's IRQ 8 handling (pic.c), where raising
       SCI would re-enter the PIC: raise it here instead. */
    if (acpi_rtc_status != dev->rtc_sts_seen) {
        const int old = acpi_smi_level(dev);

        dev->rtc_sts_seen = acpi_rtc_status;
        acpi_update_irq(dev);
        acpi_smi_edge(dev, old);
    }
    if (dev->sleeping && acpi_wake_source(dev))
        dev->wake_pending = 1;
}

/* Leaves the sleeping state. WAK_STS is set and the system goes back
   to work (ACPI 1.0 4.7.2.3, p. 4-51); from the states that lost the CPU's
   context it starts at the reset vector, the BIOS's resume path, with the
   PCI bus reset as the state's suspend_types[] entry says. The power
   management registers are in the resume well and keep their values. */
static void
acpi_wake_now(acpi_t *dev)
{
    const int sus_typ = dev->sleep_flags;

    dev->sleeping     = 0;
    dev->wake_pending = 0;
    acpi_sleeping     = 0;

    if (sus_typ & SUS_RESET_PCI) {
        const acpi_regs_t regs    = dev->regs;
        const int         rtc_sts = acpi_rtc_status;

        /* DMA is part of the southbridge so it responds to PCI reset. */
        dma_reset();
        dma_set_at(1);

        /* The AGP slot shares the bus RST# signal. */
        device_reset_all(DEVICE_PCI | DEVICE_AGP);
        dev->regs       = regs;
        acpi_rtc_status = rtc_sts;
    }

    if (sus_typ & SUS_RESET_CPU)
        cpu_alt_reset = 0;

    if (sus_typ & SUS_RESET_PCI) {
        pci_reset();

        mem_a20_alt = 0;
        mem_a20_recalc();
    }

    if (sus_typ & (SUS_RESET_CPU | SUS_RESET_CACHE))
        flushmmucache();

    if (sus_typ & SUS_RESET_CPU)
        resetx86();

    acpi_log("ACPI: woke from SLP_TYP %i (PM1 status %04X)\n", dev->sleep_typ,
             dev->regs.pmsts | (acpi_rtc_status ? RTC_STS : 0));

    dev->regs.pmsts |= WAK_STS;
    /* Nasty workaround for ASUS P2B-LS and potentially others, where the
       PMCNTRL SMI trap handler clears the resume bit before returning control
       to the OS: set it again once out of SMM (acpi_timer_resume()). */
    if (in_smm)
        timer_set_delay_u64(&dev->resume_timer, 50 * TIMER_USEC);

    acpi_update_irq(dev);
    acpi_smi_edge(dev, 0);
}

void
acpi_sleep_run(int32_t cycs)
{
    acpi_t        *dev = acpi_active;
    const uint64_t end = tsc + (uint64_t) ((cycs > 0) ? cycs : 0);

    /* The CPU is stopped: time passes for the timers alone. */
    while ((dev != NULL) && dev->sleeping) {
        if (dev->wake_pending) {
            acpi_wake_now(dev);
            return;
        }
        if (TIMER_VAL_LESS_THAN_VAL(timer_target, end)) {
            if (!TIMER_VAL_LESS_THAN_VAL(timer_target, tsc))
                tsc = timer_target;
            timer_process();
        } else {
            tsc = end;
            break;
        }
    }

    if ((dev == NULL) || !dev->sleeping)
        acpi_sleeping = 0;
}

/* An event on one of the chip's general-purpose inputs: its status
   bit is set, and SCI, SMI# or a wake follows as the enables say. */
void
acpi_gpe_event(int source)
{
    acpi_t  *dev = acpi_active;
    uint32_t bit;
    int      old;

    if (dev == NULL)
        return;

    if ((source == ACPI_GPE_USB) &&
        ((dev->vendor == VEN_SIS_5582) || (dev->vendor == VEN_SIS_5595_1997) || (dev->vendor == VEN_SIS_5595))) {
        acpi_sis_usb_event(dev);
        if (dev->sleeping && acpi_wake_source(dev))
            dev->wake_pending = 1;
        return;
    }

    bit = acpi_gpe_bit(dev, source);
    if (bit == 0)
        return;

    old = acpi_smi_level(dev);
    if (dev->vendor == VEN_SIS_5595)
        dev->regs.gpe_sts |= bit;
    else
        dev->regs.gpsts |= bit;

    if (dev->sleeping) {
        if (acpi_wake_source(dev))
            dev->wake_pending = 1;
        return;
    }

    acpi_update_irq(dev);
    if ((dev->vendor == VEN_SIS_5595) && (dev->regs.gpe_en & bit) && !(dev->regs.pmcntrl & 0x0001))
        acpi_sis5595_smi_raise(dev);
    else
        acpi_smi_edge(dev, old);
}

static void
acpi_apm_out(uint16_t port, uint8_t val, void *priv)
{
    acpi_t *dev = (acpi_t *) priv;

    acpi_log("[%04X:%08X] APM write: %04X = %02X (AX = %04X, BX = %04X, CX = %04X)\n", CS, cpu_state.pc, port, val, AX, BX, CX);

    port &= (dev->vendor == VEN_ALI) ? 0x0003 : 0x0001;

    if (dev->vendor == VEN_ALI) {
        if (port == 0x0001) {
            acpi_log("ALi SOFT SMI# status set (%i)\n", dev->apm->do_smi);
            dev->apm->cmd = val;
#if 0
            acpi_raise_smi(dev, dev->apm->do_smi);
#endif
            if (dev->apm->do_smi)
                smi_raise();
            dev->regs.ali_soft_smi = 1;
        } else if (port == 0x0003)
            dev->apm->stat = val;
    } else {
        if (port == 0x0000) {
            dev->apm->cmd = val;
            if (dev->vendor == VEN_INTEL)
                dev->regs.glbsts |= 0x20;
            acpi_raise_smi(dev, dev->apm->do_smi);
        } else
            dev->apm->stat = val;
    }
}

static uint8_t
acpi_apm_in(uint16_t port, void *priv)
{
    const acpi_t *dev = (acpi_t *) priv;
    uint8_t       ret = 0xff;

    port &= (dev->vendor == VEN_ALI) ? 0x0003 : 0x0001;

    if (dev->vendor == VEN_ALI) {
        if (port == 0x0001)
            ret = dev->apm->cmd;
        else if (port == 0x0003)
            ret = dev->apm->stat;
    } else {
        if (port == 0x0000)
            ret = dev->apm->cmd;
        else
            ret = dev->apm->stat;
    }

    acpi_log("[%04X:%08X] APM read: %04X = %02X\n", CS, cpu_state.pc, port, ret);

    return ret;
}

static void
acpi_reset(void *priv)
{
    acpi_t *dev = (acpi_t *) priv;

    memset(&dev->regs, 0x00, sizeof(acpi_regs_t));
    /* PC Chips M773:
       - Bit 3: 80-conductor cable on unknown IDE channel (active low)
       - Bit 1: 80-conductor cable on unknown IDE channel (active low) */
    dev->regs.gpireg[0] = (machines[machine].init == machine_at_m773_init) ? 0xf5 : 0xff;
    /* Dell OptiPlex E1 and GX1:
       - Bit 6: Chassis intrusion switch - must be cleared as otherwise POST complains that the chassis was opened
       - Bit 3: ??? - must be cleared as otherwise POST complains about regulator failure */    
    dev->regs.gpireg[0] = ((machines[machine].init == machine_at_optiplexe1_init) ||
                           (machines[machine].init == machine_at_optiplexgx1_init)) ? 0xb7 : dev->regs.gpireg[0];
    /* AST Bravo MS-T 6233:
       - Bit 6: Setup access - must be cleared as otherwise F2 is ignored, the F2 prompts are not shown,
                and the on-board LANDesk boot agent is not initialized
       - Bit 5: Recovery mode - must be cleared as otherwise the machine enters recovery flash mode */
    dev->regs.gpireg[0] = (machines[machine].init == machine_at_bravomst6233_init) ? 0x9f : dev->regs.gpireg[0];
    /* AST Bravo MS-T 6233:
       - Bits 7-5 (GPI15-13): On-board audio populated when all clear - the BIOS only configures the
                              ES1869 (through its PnP bypass key) then
       - Bit 1 (GPI9): Powered on by AC power restore rather than the power button - must be cleared as
                       otherwise POST applies the AC power loss policy, which powers off on invalid CMOS */
    if (machines[machine].init == machine_at_bravomst6233_init)
        dev->regs.gpireg[1] = (sound_card_current[0] == SOUND_INTERNAL) ? 0x1d : 0xfd;
    else
        dev->regs.gpireg[1] = 0xff;
    /* A-Trend ATC7020BXII:
       - Bit 3: 80-conductor cable on secondary IDE channel (active low)
       - Bit 2: 80-conductor cable on primary IDE channel (active low)
       Gigabyte GA-686BX:
       - Bit 1: CMOS battery low (active high) */
    if ((machines[machine].init == machine_at_al440lx_init) ||
        (machines[machine].init == machine_at_se440bx2_init) ||
        (machines[machine].init == machine_at_rc440bx_init) ||
        (machines[machine].init == machine_at_bl440zx_init))
        /* ED = Normal, DD (2-3) - Maintenance, BD, FD (none) - Recovery. */
        dev->regs.gpireg[2] = 0xed;
    else if ((machines[machine].init == machine_at_in440ex_init) ||
             (machines[machine].init == machine_at_in440exd_init))
        /* Bit 5: CMOS clear jumper(?) - must be set as otherwise CMOS is not saved */
        dev->regs.gpireg[2] = 0xfd;
    else if (machines[machine].init == machine_at_em440_init)
        /* Bit 4: Recovery mode - must be set as otherwise the machine enters recovery flash mode */
        dev->regs.gpireg[2] = 0xff;
    else
        dev->regs.gpireg[2] = dev->gpireg2_default;
    for (uint8_t i = 0; i < 4; i++)
        dev->regs.gporeg[i] = dev->gporeg_default[i];
    if (machines[machine].init == machine_at_acerv66ma_init) {
        /* Bit 4 must be cleared to avoid the password violated error. */
        dev->regs.gpireg[2] = 0xcd;
        /* Bit 5 must be set at reset to disable the A17 xor for the Flash. */
        dev->regs.gporeg[3] |= 0x20;
    } else if (machines[machine].init == machine_at_bx6_init)
        /*
           Bit 3 must be cleared to enable the CPU Soft Menu on the ABIT AB-BX6
           BIOS revision EG.
         */
        dev->regs.gpireg[2] = 0xd5;
    if (dev->vendor == VEN_VIA_596B) {
        dev->regs.gpo_val = 0x7fffffff;
        /*
           - FIC VA-503A:
               - Bit 11: ATX power (active high);
               - Bit  4: 80-conductor cable on primary IDE channel (active low);
               - Bit  3: 80-conductor cable on secondary IDE channel (active low);
               - Bit  2: password cleared (active low).
           - ASUS P3V4X:
               - Bit 15: 80-conductor cable on secondary IDE channel (active low);
               - Bit  5: 80-conductor cable on primary IDE channel (active low).
           - BCM GT694VA:
               - Bit 19: 80-conductor cable on secondary IDE channel (active low);
               - Bit 17: 80-conductor cable on primary IDE channel (active low).
           - ASUS CUV4X-LS:
               - Bit  2: 80-conductor cable on secondary IDE channel (active low);
               - Bit  1: 80-conductor cable on primary IDE channel (active low).
           - Acorp 6VIA90AP:
               - Bit  3: 80-conductor cable on secondary IDE channel (active low);
               - Bit  1: 80-conductor cable on primary IDE channel (active low).
           - FIC KA-6130:
               - Bit 19: password cleared (active low).
         */
        dev->regs.gpi_val = 0xfff57fc1;
        if ((machines[machine].init == machine_at_ficva503a_init) || (machines[machine].init == machine_at_6via90ap_init))
            dev->regs.gpi_val |= 0x00000004;
        else if ((machines[machine].init == machine_at_ficka6130_init))
            dev->regs.gpi_val |= 0x00080000;
         /*
            TriGem Delhi-III second GPI word:
                - Bit 7 = Save CMOS (must be set);
                - Bit 6 = Password jumper (must be set);
                - Bit 5 = Enable Setup (must be set).
         */
        else if (machines[machine].init == machine_at_delhi3_init)
            dev->regs.gpi_val |= 0x00008000;
    }

    if (dev->power_on) {
        /* Power on always generates a resume event, except on the SiS 5582
           and 5595. There only the RTC and the APC that powers the board up
           run from RTCVDD; the ACPI registers are powered with the rest of the
           board, so they come up cleared (SiS 5581/5582 p. 39 and 41 / PDF 39
           and 41, where they are in the 3.3V OVDD well; SiS 5595 3.6.1.3,
           p. 44, and PM1_STS, p. 134). The press that powers the board up
           goes to the APC, and the ACPI block sees presses only once PWRGD is
           high (SiS 5595 3.7.2.2, p. 47). A PWRBTN_STS set here becomes an
           SMI# as soon as the BIOS sets PWRBTN_EN in legacy mode, which the
           Compaq Mustang-S BIOS does early in POST, before it has installed
           an SMI handler. */
        if ((dev->vendor != VEN_SIS_5582) && (dev->vendor != VEN_SIS_5595_1997) &&
            (dev->vendor != VEN_SIS_5595))
            dev->regs.pmsts |= 0x8100;
        dev->power_on = 0;
    }

    /* The Gateway Tomahawk requires the LID polarity bit to be set. */
    if (machines[machine].init == machine_at_tomahawk_init)
        dev->regs.glbctl |= 0x02000000;

    /* The VT82C596B's PB_EN is set at reset (VT82C596B p. 70 / PDF 76). */
    if (dev->vendor == VEN_VIA_596B)
        dev->regs.pmen |= PWRBTN_EN;

    acpi_rtc_status = 0;

    /* A reset ends any sleeping state and power button hold. */
    if (dev->sleeping)
        acpi_sleeping = 0;
    dev->sleeping     = 0;
    dev->wake_pending = 0;
    dev->pwrbtn_held  = 0;

    acpi_update_irq(dev);
    dev->irq_state = 0;

    acpi_timer_update(dev, true);

    timer_disable(&dev->gp_timer);

    dev->gp_last_clock = 0ULL;
    dev->gp_count      = 0;

    /* Clock control back to full speed. */
    cpu_stpclk       = 0;
    cpu_throttle_run = 0;

    timer_disable(&dev->per_timer);

    if ((dev->vendor == VEN_SIS_5595_1997) || (dev->vendor == VEN_SIS_5595)) {
        dev->regs.reg_13 = 0x20;
        dev->regs.gp_tmr = 0xff;
        dev->regs.gpe_io = 0x00030b9f;
        dev->regs.gpe_mul = 0x1001;

        /* SiS 5595 GPIO pin defaults. */
        uint32_t gpio = machine_get_gpio_acpi_default();

        if (gpio != 0xffffffff)
            dev->regs.gpe_pin = gpio & 0x0003ffff;
    }

    if (dev->vendor == VEN_ALI) {
        /* SLP_TYP 100b, "Working" (M1543C p. 195 / PDF 196). */
        dev->regs.pmcntrl = 0x1000;
    }

    if (dev->vendor == VEN_SIS_5582) {
        /* SiS Proprietary Bit, must be 1 */
        dev->regs.pmcntrl |= 0x0200;

        if (machines[machine].init == machine_at_sp97xv_init)
            dev->regs.reg_18  |= 0x0480;
    }
}

static void
acpi_speed_changed(void *priv)
{
    acpi_t *dev            = (acpi_t *) priv;
    cpu_to_acpi            = ACPI_TIMER_FREQ / cpuclock;
    acpi_pmtmr_read_cycles = (int) (cpuclock / ACPI_TIMER_FREQ) + 1;
    bool timer_enabled     = timer_is_enabled(&dev->timer);
    timer_stop(&dev->timer);

    if (timer_enabled)
        timer_on_auto(&dev->timer, acpi_get_overflow_period(dev));

    if ((dev->vendor & 0xffff) == 0x1039) {
        if (timer_is_on(&dev->gp_timer)) {
            timer_stop(&dev->gp_timer);

            if (dev->vendor == VEN_SIS_5595_1997)
                acpi_gp_timer_update(dev, (dev->regs.gpe_en & 0x20000000), dev->gp_count);
            else if (dev->vendor == VEN_SIS_5595)
                acpi_gp_timer_update(dev, (dev->regs.gpe_en & 0x00000400), dev->gp_count);
            else
                acpi_gp_timer_update(dev, (dev->regs.reg_16 & 0x2000), dev->gp_count);
        }

        if (timer_is_on(&dev->per_timer)) {
            timer_stop(&dev->per_timer);

            timer_on_auto(&dev->per_timer, 16000000.0);
        }
    }
}

void *
acpi_get_smbus(void *priv)
{
    acpi_t *dev        = (acpi_t *) priv;

    return dev->smbus;
}

static void
acpi_close(void *priv)
{
    acpi_t *dev = (acpi_t *) priv;

    if (dev->i2c) {
        if (i2c_smbus == i2c_gpio_get_bus(dev->i2c))
            i2c_smbus = NULL;
        i2c_gpio_close(dev->i2c);
    }

    timer_stop(&dev->timer);

    cpu_stpclk       = 0;
    cpu_throttle_run = 0;

    if (acpi_active == dev) {
        acpi_active   = NULL;
        acpi_sleeping = 0;
    }

    free(dev);
}

static void *
acpi_init(const device_t *info)
{
    acpi_t *dev;

    dev = (acpi_t *) calloc(1, sizeof(acpi_t));
    if (dev == NULL)
        return NULL;

    cpu_to_acpi            = ACPI_TIMER_FREQ / cpuclock;
    acpi_pmtmr_read_cycles = (int) (cpuclock / ACPI_TIMER_FREQ) + 1;
    dev->vendor            = info->local;

    dev->irq_line = 9;

    if ((dev->vendor == VEN_INTEL) || (dev->vendor == VEN_ALI)) {
        if (dev->vendor == VEN_ALI)
            dev->irq_mode = 2;
        dev->apm = device_add(&apm_pci_acpi_device);
        if (dev->vendor == VEN_ALI) {
            acpi_log("Setting I/O handler at port B1\n");
            io_sethandler(0x00b1, 0x0003, acpi_apm_in, NULL, NULL, acpi_apm_out, NULL, NULL, dev);
        } else
            io_sethandler(0x00b2, 0x0002, acpi_apm_in, NULL, NULL, acpi_apm_out, NULL, NULL, dev);
    } else if (dev->vendor == VEN_VIA) {
        dev->i2c  = i2c_gpio_init("smbus_vt82c586b");
        i2c_smbus = i2c_gpio_get_bus(dev->i2c);
    }

    switch (dev->vendor) {
        case VEN_ALI:
            dev->suspend_types[0] = SUS_POWER_OFF;
            dev->suspend_types[1] = SUS_POWER_OFF;
            dev->suspend_types[2] = SUS_SUSPEND | SUS_NVR | SUS_RESET_CPU | SUS_RESET_PCI;
            dev->suspend_types[3] = SUS_SUSPEND;
            dev->suspend_types[5] = SUS_POWER_OFF; /* undocumented, used for S4/S5 by ASUS P5A ACPI table */
            dev->suspend_types[7] = SUS_POWER_OFF; /* undocumented, used for S5 by Gigabyte GA-5AX ACPI table */
            break;

        case VEN_VIA:
            dev->suspend_types[0] = SUS_POWER_OFF;
            dev->suspend_types[2] = SUS_SUSPEND;
            break;

        case VEN_VIA_596B:
            dev->suspend_types[1] = SUS_SUSPEND | SUS_NVR | SUS_RESET_CPU | SUS_RESET_PCI;
            dev->suspend_types[2] = SUS_POWER_OFF;
            /* SLP_TYP 011b is reserved too (VT82C596B p. 71 / PDF 77), but
               the ASUS P3V4X ACPI tables use it for S1: taken as Power On
               Suspend. */
            dev->suspend_types[3] = SUS_SUSPEND;
            dev->suspend_types[4] = SUS_SUSPEND;
            dev->suspend_types[5] = SUS_SUSPEND | SUS_RESET_CPU;
            dev->suspend_types[6] = SUS_SUSPEND | SUS_RESET_CPU | SUS_RESET_PCI;
            /* SLP_TYP 111b is reserved (VT82C596B p. 71 / PDF 77), but the
               ASUS P3V4X ACPI tables use it for S4 and S5. */
            dev->suspend_types[7] = SUS_POWER_OFF;
            break;

        case VEN_INTEL:
            dev->suspend_types[0] = SUS_POWER_OFF;
            dev->suspend_types[1] = SUS_SUSPEND | SUS_NVR | SUS_RESET_CPU | SUS_RESET_PCI;
            dev->suspend_types[2] = SUS_SUSPEND | SUS_RESET_CPU;
            dev->suspend_types[3] = SUS_SUSPEND | SUS_RESET_CACHE;
            dev->suspend_types[4] = SUS_SUSPEND;
            break;

        case VEN_SIS_5582:
            dev->suspend_types[0] = SUS_SUSPEND;      /* S1 */
            dev->suspend_types[4] = SUS_POWER_OFF;    /* S5 */

            timer_add(&dev->gp_timer, acpi_gp_timer, dev, 0);
            timer_add(&dev->per_timer, acpi_per_timer, dev, 0);
            break;

        case VEN_SIS_5595_1997:
        case VEN_SIS_5595:
            dev->suspend_types[1] = SUS_SUSPEND;
            dev->suspend_types[2] = SUS_SUSPEND;
            dev->suspend_types[3] = SUS_SUSPEND | SUS_NVR | SUS_RESET_CPU | SUS_RESET_PCI;
            dev->suspend_types[4] = SUS_POWER_OFF;
            dev->suspend_types[5] = SUS_POWER_OFF;

            timer_add(&dev->gp_timer, acpi_gp_timer, dev, 0);
            timer_add(&dev->per_timer, acpi_per_timer, dev, 0);

            dev->smbus = device_add(&sis5595_smbus_device);
            break;

        default:
            break;
    }

    timer_add(&dev->timer, acpi_timer_overflow, dev, 0);
    timer_add(&dev->resume_timer, acpi_timer_resume, dev, 0);
    timer_add(&dev->pwrbtn_timer, acpi_pwrbtn_timer, dev, 0);

    timer_on_auto(&dev->pwrbtn_timer, 16. * 1000.);

    acpi_reset(dev);

    acpi_enabled  = 1;
    dev->power_on = 1;
    acpi_active   = dev;

    return dev;
}

const device_t acpi_ali_device = {
    .name          = "ALi M7101 ACPI",
    .internal_name = "acpi_ali",
    .flags         = DEVICE_PCI,
    .local         = VEN_ALI,
    .init          = acpi_init,
    .close         = acpi_close,
    .reset         = acpi_reset,
    .available     = NULL,
    .speed_changed = acpi_speed_changed,
    .force_redraw  = NULL,
    .config        = NULL
};

const device_t acpi_intel_device = {
    .name          = "Intel ACPI",
    .internal_name = "acpi_intel",
    .flags         = DEVICE_PCI,
    .local         = VEN_INTEL,
    .init          = acpi_init,
    .close         = acpi_close,
    .reset         = acpi_reset,
    .available     = NULL,
    .speed_changed = acpi_speed_changed,
    .force_redraw  = NULL,
    .config        = NULL
};

const device_t acpi_via_device = {
    .name          = "VIA ACPI",
    .internal_name = "acpi_via",
    .flags         = DEVICE_PCI,
    .local         = VEN_VIA,
    .init          = acpi_init,
    .close         = acpi_close,
    .reset         = acpi_reset,
    .available     = NULL,
    .speed_changed = acpi_speed_changed,
    .force_redraw  = NULL,
    .config        = NULL
};

const device_t acpi_via_596b_device = {
    .name          = "VIA VT82C596 ACPI",
    .internal_name = "acpi_via_596b",
    .flags         = DEVICE_PCI,
    .local         = VEN_VIA_596B,
    .init          = acpi_init,
    .close         = acpi_close,
    .reset         = acpi_reset,
    .available     = NULL,
    .speed_changed = acpi_speed_changed,
    .force_redraw  = NULL,
    .config        = NULL
};

const device_t acpi_smc_device = {
    .name          = "SMC FDC73C931APM ACPI",
    .internal_name = "acpi_smc",
    .flags         = DEVICE_PCI,
    .local         = VEN_SMC,
    .init          = acpi_init,
    .close         = acpi_close,
    .reset         = acpi_reset,
    .available     = NULL,
    .speed_changed = acpi_speed_changed,
    .force_redraw  = NULL,
    .config        = NULL
};

const device_t acpi_sis_5582_device = {
    .name          = "SiS 5582 ACPI",
    .internal_name = "acpi_sis_5582",
    .flags         = DEVICE_PCI,
    .local         = VEN_SIS_5582,
    .init          = acpi_init,
    .close         = acpi_close,
    .reset         = acpi_reset,
    .available     = NULL,
    .speed_changed = acpi_speed_changed,
    .force_redraw  = NULL,
    .config        = NULL
};

const device_t acpi_sis_5595_1997_device = {
    .name          = "SiS 5595 (1997) ACPI",
    .internal_name = "acpi_sis_5595_1997",
    .flags         = DEVICE_PCI,
    .local         = VEN_SIS_5595_1997,
    .init          = acpi_init,
    .close         = acpi_close,
    .reset         = acpi_reset,
    .available     = NULL,
    .speed_changed = acpi_speed_changed,
    .force_redraw  = NULL,
    .config        = NULL
};

const device_t acpi_sis_5595_device = {
    .name          = "SiS 5595 ACPI",
    .internal_name = "acpi_sis_5595",
    .flags         = DEVICE_PCI,
    .local         = VEN_SIS_5595,
    .init          = acpi_init,
    .close         = acpi_close,
    .reset         = acpi_reset,
    .available     = NULL,
    .speed_changed = acpi_speed_changed,
    .force_redraw  = NULL,
    .config        = NULL
};
