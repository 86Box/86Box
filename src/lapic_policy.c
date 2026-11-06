/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          Copyright 2026 skiretic.
 */
#include <stddef.h>

#include "lapic.h"
#include "lapic_policy.h"

#define LAPIC_TPR_WRITE_MASK 0x000000ff
#define LAPIC_LDR_WRITE_MASK 0xff000000
#define LAPIC_DFR_MODEL_MASK 0xf0000000
#define LAPIC_DFR_RESERVED_ONES 0x0fffffff
#define LAPIC_DIVIDE_CFG_WRITE_MASK 0x0000000b
#define LAPIC_SVR_FOCUS_PROCESSOR_CHECKING_DISABLE 0x00000200
#define LAPIC_SVR_WRITE_MASK 0x000003f0
#define LAPIC_SVR_READ_ONES  0x0000000f
#define LAPIC_ESR_VALID_MASK \
    (LAPIC_ESR_SEND_CHECKSUM | LAPIC_ESR_RECV_CHECKSUM | \
     LAPIC_ESR_SEND_ACCEPT | LAPIC_ESR_RECV_ACCEPT | \
     LAPIC_ESR_SEND_ILLEGAL_VECTOR | LAPIC_ESR_RECV_ILLEGAL_VECTOR | \
     LAPIC_ESR_ILLEGAL_REG_ADDR)
#define LAPIC_ICR_LO_LOAD_MASK \
    (LAPIC_ICR_VECTOR | LAPIC_ICR_DELIV | LAPIC_ICR_DESTMOD | \
     LAPIC_ICR_LEVEL | LAPIC_ICR_TRIGGER | LAPIC_ICR_RR_MASK | \
     LAPIC_ICR_DEST_SHORT)
#define LAPIC_ICR_HI_DEST_MASK 0xff000000

/* Pure policy helpers: no device ownership, host timers, or CPU-backend side effects here. */

int
lapic_reg_valid(uint32_t reg)
{
    return reg < 0x400;
}

extern int cpu_is_p5(void);

int
lapic_mmio_reg_defined(uint32_t reg)
{
    if (!lapic_reg_valid(reg))
        return 0;

    /* ISR/TMR/IRR banks occupy sparse dword slots inside wider MMIO windows. */
    if (((reg >= LAPIC_REG_ISR_BASE) && (reg < (LAPIC_REG_ISR_BASE + 0x80))) ||
        ((reg >= LAPIC_REG_TMR_BASE) && (reg < (LAPIC_REG_TMR_BASE + 0x80))) ||
        ((reg >= LAPIC_REG_IRR_BASE) && (reg < (LAPIC_REG_IRR_BASE + 0x80))))
        return ((reg & 0x0f) == 0);

    switch (reg) {
        case LAPIC_REG_ID:
        case LAPIC_REG_VERSION:
        case LAPIC_REG_TPR:
        case LAPIC_REG_APR:
        case LAPIC_REG_PPR:
        case LAPIC_REG_EOI:
        case LAPIC_REG_LDR:
        case LAPIC_REG_DFR:
        case LAPIC_REG_SVR:
        case LAPIC_REG_ESR:
        case LAPIC_REG_ICR_LO:
        case LAPIC_REG_ICR_HI:
            return 1;
        case LAPIC_REG_RRR:
            /* The Remote Read register is the P54C's; the P6 lists FEE0
               00C0H as Reserved (Pentium Pro Vol. 3, Table 7-1). */
            return cpu_is_p5();
        case LAPIC_REG_LVT_THERMAL:
            /* FEE0 0330H is Reserved on the P54C and on the P6 family
               (Pentium Pro Vol. 3, Table 7-1; IA SDM Vol. 3, 243192-002):
               the thermal LVT came later. */
            return 0;
        case LAPIC_REG_LVT_PERF:
            /* P6 and later; reserved on the Pentium's on-chip APIC. */
            return !cpu_is_p5();
        case LAPIC_REG_LVT_TIMER:
        case LAPIC_REG_LVT_LINT0:
        case LAPIC_REG_LVT_LINT1:
        case LAPIC_REG_LVT_ERROR:
        case LAPIC_REG_INITIAL_CNT:
        case LAPIC_REG_CURRENT_CNT:
        case LAPIC_REG_DIVIDE_CFG:
            return 1;
        default:
            return 0;
    }
}

int
lapic_vector_valid(uint8_t vector)
{
    return vector >= 0x10;
}

uint32_t
lapic_priority_class(uint32_t priority)
{
    return priority & 0xf0;
}

int
lapic_priority_deliverable(uint8_t vector, uint32_t ppr)
{
    return lapic_priority_class(vector) > lapic_priority_class(ppr);
}

uint32_t
lapic_processor_priority(uint32_t tpr, int isr_vector, int p6)
{
    uint32_t tpr_class = lapic_priority_class(tpr);
    uint32_t isr_class;

    /* Pentium Vol. 3, 19.3.1.9.3: "If TPR[7:4] > ISRV[7:4] then PPR[7:0] =
       TPR[7:0], else PPR[7:4] = ISRV[7:4], PPR[3:0] = 0", ISRV "zero if no
       ISR bit is set": with nothing in service a TPR of class 0 gives 0.
       The P6's is "IF TPR[7:4] >= ISRV[7:4] THEN PPR[7:0] = TPR[7:0]"
       (Pentium Pro Vol. 3, 7.4.10.3): with the classes equal it is the
       TPR. Either way the class, which decides delivery, is the same. */
    isr_class = (isr_vector < 0) ? 0 : lapic_priority_class((uint32_t) isr_vector);
    if ((tpr_class > isr_class) || (p6 && (tpr_class == isr_class)))
        return tpr & LAPIC_TPR_WRITE_MASK;

    return isr_class;
}

uint32_t
lapic_tpr_sanitize(uint32_t val)
{
    return val & LAPIC_TPR_WRITE_MASK;
}

uint32_t
lapic_ldr_sanitize(uint32_t val)
{
    return val & LAPIC_LDR_WRITE_MASK;
}

uint32_t
lapic_dfr_sanitize(uint32_t val)
{
    return (val & LAPIC_DFR_MODEL_MASK) | LAPIC_DFR_RESERVED_ONES;
}

uint32_t
lapic_divide_cfg_sanitize(uint32_t val)
{
    return val & LAPIC_DIVIDE_CFG_WRITE_MASK;
}

uint32_t
lapic_svr_sanitize(uint32_t val)
{
    return (val & LAPIC_SVR_WRITE_MASK) | LAPIC_SVR_READ_ONES;
}

int
lapic_svr_focus_processor_checking_disabled(uint32_t svr)
{
    return !!(svr & LAPIC_SVR_FOCUS_PROCESSOR_CHECKING_DISABLE);
}

uint32_t
lapic_esr_sanitize(uint32_t val)
{
    return val & LAPIC_ESR_VALID_MASK;
}

uint32_t
lapic_icr_lo_load_sanitize(uint32_t val)
{
    val &= LAPIC_ICR_LO_LOAD_MASK;
    /* Within RR bits, only modelled RR_VALID state survives into the loadable ICR shadow. */
    if ((val & LAPIC_ICR_RR_MASK) != LAPIC_ICR_RR_VALID)
        val &= ~LAPIC_ICR_RR_MASK;

    return val;
}

uint32_t
lapic_icr_hi_sanitize(uint32_t val)
{
    return val & LAPIC_ICR_HI_DEST_MASK;
}

uint32_t
lapic_lvt_write_mask(uint32_t reg)
{
    switch (reg) {
        case LAPIC_REG_LVT_TIMER:
            return LAPIC_LVT_VECTOR | LAPIC_LVT_MASK | LAPIC_LVT_TIMER_PERIODIC;
        case LAPIC_REG_LVT_LINT0:
        case LAPIC_REG_LVT_LINT1:
            return LAPIC_LVT_VECTOR | LAPIC_LVT_DELIV | LAPIC_LVT_POLARITY |
                   LAPIC_LVT_TRIGGER | LAPIC_LVT_MASK;
        case LAPIC_REG_LVT_ERROR:
            return LAPIC_LVT_VECTOR | LAPIC_LVT_MASK;
        default:
            return LAPIC_LVT_VECTOR | LAPIC_LVT_DELIV | LAPIC_LVT_MASK;
    }
}

static int
lapic_lvt_lint_reg(uint32_t reg)
{
    return (reg == LAPIC_REG_LVT_LINT0) || (reg == LAPIC_REG_LVT_LINT1);
}

static int
lapic_lvt_thermal_perf_reg(uint32_t reg)
{
    return (reg == LAPIC_REG_LVT_THERMAL) || (reg == LAPIC_REG_LVT_PERF);
}

static int
lapic_lvt_delivery_supported(uint32_t reg, uint32_t delivery)
{
    if (lapic_lvt_lint_reg(reg))
        return (delivery == LAPIC_LVT_DELIV_FIXED) ||
               (delivery == LAPIC_LVT_DELIV_SMI) ||
               (delivery == LAPIC_LVT_DELIV_NMI) ||
               (delivery == LAPIC_LVT_DELIV_INIT) ||
               (delivery == LAPIC_LVT_DELIV_EXTINT);

    if (lapic_lvt_thermal_perf_reg(reg))
        return (delivery == LAPIC_LVT_DELIV_FIXED) ||
               (delivery == LAPIC_LVT_DELIV_SMI) ||
               (delivery == LAPIC_LVT_DELIV_NMI);

    return 1;
}

uint32_t
lapic_lvt_sanitize(uint32_t reg, uint32_t val)
{
    uint32_t sanitized = val & lapic_lvt_write_mask(reg);

    /* Unsupported delivery encodings become masked LVT entries, keeping caller handling uniform. */
    if ((lapic_lvt_lint_reg(reg) || lapic_lvt_thermal_perf_reg(reg)) &&
        !lapic_lvt_delivery_supported(reg, sanitized & LAPIC_LVT_DELIV))
        sanitized = (sanitized & ~LAPIC_LVT_DELIV) | LAPIC_LVT_MASK;

    return sanitized;
}

int
lapic_mmio_reg_read_only(uint32_t reg)
{
    return (reg == LAPIC_REG_VERSION) ||
           (reg == LAPIC_REG_APR) ||
           (reg == LAPIC_REG_PPR) ||
           (reg == LAPIC_REG_RRR) ||
           ((reg >= LAPIC_REG_ISR_BASE) && (reg < (LAPIC_REG_ISR_BASE + 0x80))) ||
           ((reg >= LAPIC_REG_TMR_BASE) && (reg < (LAPIC_REG_TMR_BASE + 0x80))) ||
           ((reg >= LAPIC_REG_IRR_BASE) && (reg < (LAPIC_REG_IRR_BASE + 0x80))) ||
           (reg == LAPIC_REG_CURRENT_CNT);
}

uint32_t
lapic_timer_current_from_state(uint32_t active_count, uint64_t start_tsc,
                               uint64_t now_tsc, double clocks_per_count)
{
    uint64_t elapsed;

    if (!active_count)
        return 0;

    if (clocks_per_count < 1.0)
        clocks_per_count = 1.0;

    /* Backward time means no elapsed ticks; avoid unsigned underflow after restore/rebase. */
    if (now_tsc < start_tsc)
        return active_count;

    /* The tolerance keeps the delay's rounding from leaving a count of 1. */
    elapsed = (uint64_t) (((double) (now_tsc - start_tsc) / clocks_per_count) + 1e-6);
    if (elapsed >= active_count)
        return 0;

    return active_count - elapsed;
}

void
lapic_timer_rebase_state(uint32_t initial, uint32_t current, uint64_t now_tsc,
                         uint32_t *active_count, uint64_t *start_tsc,
                         uint8_t *enabled)
{
    if ((active_count == NULL) || (start_tsc == NULL) || (enabled == NULL))
        return;

    if (!initial || !current) {
        *active_count = 0;
        *start_tsc = 0;
        *enabled = 0;
        return;
    }

    /* Clamp persisted/current state before making it new countdown authority. */
    if (current > initial)
        current = initial;

    *active_count = current;
    *start_tsc = now_tsc;
    *enabled = 1;
}

uint32_t
lapic_timer_restore_state(uint32_t initial, uint32_t current, int restore_enabled,
                          uint64_t now_tsc, uint32_t *active_count,
                          uint64_t *start_tsc, uint8_t *enabled)
{
    if ((active_count == NULL) || (start_tsc == NULL) || (enabled == NULL))
        return 0;

    if (!initial || !current || !restore_enabled) {
        *active_count = 0;
        *start_tsc = 0;
        *enabled = 0;
        return 0;
    }

    /* Save/load compatibility: restored current values may exceed initial. */
    if (current > initial)
        current = initial;

    *active_count = current;
    *start_tsc = now_tsc;
    *enabled = 1;
    return current;
}

uint32_t
lapic_timer_periodic_restart_state(uint32_t initial, uint64_t now_tsc,
                                   uint32_t *active_count, uint64_t *start_tsc)
{
    if ((active_count == NULL) || (start_tsc == NULL))
        return 0;

    *active_count = initial;
    if (initial)
        *start_tsc = now_tsc;
    else
        *start_tsc = 0;

    return initial;
}
