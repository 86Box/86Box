/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          The processors' local APICs: registers, priority, delivery,
 *          timers and start-up, one set per processor. Every processor's
 *          APIC is reachable from the I/O APIC and from the others' ICRs,
 *          so this is the machine's state, kept outside the processor
 *          core.
 *
 *          Copyright 2026 skiretic.
 */
#include <math.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <wchar.h>

#include <86box/86box.h>
#include "cpu.h"
#include <86box/device.h>
#include <86box/machine.h>
#include <86box/nmi.h>
#include <86box/chipset.h>
#include <86box/timer.h>
#include <86box/video.h> /* cpuclock */
#include "../lapic_policy.h"

/* CPU owns LAPIC state that drives priority, delivery, timers, and save/load. */
static uint8_t               cpu_lapic_ids[IOAPIC_STATE_MAX_LAPICS];
static uint32_t              cpu_lapic_svrs[IOAPIC_STATE_MAX_LAPICS];
static uint32_t              cpu_lapic_tprs[IOAPIC_STATE_MAX_LAPICS];
static uint32_t              cpu_lapic_ldrs[IOAPIC_STATE_MAX_LAPICS];
static uint32_t              cpu_lapic_dfrs[IOAPIC_STATE_MAX_LAPICS];
static uint32_t              cpu_lapic_versions[IOAPIC_STATE_MAX_LAPICS];
/* The Arb ID, which INIT keeps and the bus model holds in lapic->arb_id;
   not the APR, the arbitration priority register (Vol. 3 19.3.1.9.4),
   which is worked out at each read (cpu_lapic_apr_value_index). */
static uint32_t              cpu_lapic_arb_ids[IOAPIC_STATE_MAX_LAPICS];
static uint32_t              cpu_lapic_pprs[IOAPIC_STATE_MAX_LAPICS];
static uint32_t              cpu_lapic_esrs[IOAPIC_STATE_MAX_LAPICS];
/* The errors a P6's local APIC has detected since its ESR was last
   written, which the next write makes readable. */
static uint32_t              cpu_lapic_esr_errors[IOAPIC_STATE_MAX_LAPICS];
static uint32_t              cpu_lapic_icr_los[IOAPIC_STATE_MAX_LAPICS];
static uint32_t              cpu_lapic_icr_his[IOAPIC_STATE_MAX_LAPICS];
static uint32_t              cpu_lapic_rrrs[IOAPIC_STATE_MAX_LAPICS];
static uint32_t              cpu_lapic_lvt_timers[IOAPIC_STATE_MAX_LAPICS];
static uint32_t              cpu_lapic_lvt_thermals[IOAPIC_STATE_MAX_LAPICS];
static uint32_t              cpu_lapic_lvt_perfs[IOAPIC_STATE_MAX_LAPICS];
static uint32_t              cpu_lapic_lvt_lint0s[IOAPIC_STATE_MAX_LAPICS];
static uint32_t              cpu_lapic_lvt_lint1s[IOAPIC_STATE_MAX_LAPICS];
static uint32_t              cpu_lapic_lvt_errors[IOAPIC_STATE_MAX_LAPICS];
static uint32_t              cpu_lapic_irrs[IOAPIC_STATE_MAX_LAPICS][IOAPIC_STATE_LAPIC_BIT_WORDS];
/* Kept beside IRR for the interrupt check's fast path; see
   cpu_lapic_irr_live_sync(). */
uint32_t                     cpu_lapic_irr_live;
uint8_t                      cpu_lapic_extint_live[IOAPIC_STATE_MAX_LAPICS];
static uint32_t              cpu_lapic_isrs[IOAPIC_STATE_MAX_LAPICS][IOAPIC_STATE_LAPIC_BIT_WORDS];
static uint32_t              cpu_lapic_tmrs[IOAPIC_STATE_MAX_LAPICS][IOAPIC_STATE_LAPIC_BIT_WORDS];
static uint32_t              cpu_lapic_timer_initials[IOAPIC_STATE_MAX_LAPICS];
static uint32_t              cpu_lapic_timer_reload_counts[IOAPIC_STATE_MAX_LAPICS];
static uint64_t              cpu_lapic_timer_start_tscs[IOAPIC_STATE_MAX_LAPICS];
static uint8_t               cpu_lapic_timer_enableds[IOAPIC_STATE_MAX_LAPICS];
static uint32_t              cpu_lapic_timer_divide_cfgs[IOAPIC_STATE_MAX_LAPICS];
static pc_timer_t            cpu_lapic_timer_hosts[IOAPIC_STATE_MAX_LAPICS];
static uint64_t              cpu_apic_base_msrs[IOAPIC_STATE_MAX_LAPICS];

typedef struct cpu_lapic_context_t {
    cpu_state_t *state;
    uint8_t      lapic_index;
} cpu_lapic_context_t;

static cpu_lapic_context_t cpu_lapic_contexts[IOAPIC_STATE_MAX_LAPICS] = {
    { &cpu_state, 0 }
};

/* Constants below match the CPU-owned mirror format exposed through LAPIC MMIO. */
#define CPU_APIC_BASE_DEFAULT   0x00000000fee00900ULL
#define CPU_APIC_BASE_BSP       0x0000000000000100ULL
#define CPU_APIC_BASE_ENABLE    0x0000000000000800ULL
#define CPU_APIC_BASE_ADDR_MASK 0x00000000fffff000ULL
#define CPU_LAPIC_SVR_DEFAULT   0x000000ff
#define CPU_LAPIC_SVR_ENABLE    0x00000100
#define CPU_LAPIC_SVR_READ_ONES 0x0000000f
#define CPU_LAPIC_TPR_DEFAULT   0x00000000
#define CPU_LAPIC_TPR_MASK      0x000000ff
#define CPU_LAPIC_LDR_DEFAULT   0x00000000
#define CPU_LAPIC_DFR_DEFAULT   0xffffffff
#define CPU_LAPIC_LDR_MASK      0xff000000
#define CPU_LAPIC_DFR_MASK      0xffffffff
#define CPU_LAPIC_VERSION_DEFAULT 0x00040010
#define CPU_LAPIC_LVT_DEFAULT   0x00010000
#define CPU_LAPIC_TIMER_DIVIDE_CFG_DEFAULT 0x00000000 /* divide by 2: reset to 0 (Pentium Vol. 3, 19.3.1.11.3) */
#define CPU_LAPIC_TIMER_DIVIDE_CFG_MASK    0x0000000b
#define CPU_LAPIC_TIMER_MAX_DELAY          0x7fffffffULL

#define CPU_LAPIC_REG_ID           0x020
#define CPU_LAPIC_REG_VERSION      0x030
#define CPU_LAPIC_REG_TPR          0x080
#define CPU_LAPIC_REG_APR          0x090
#define CPU_LAPIC_REG_PPR          0x0a0
#define CPU_LAPIC_REG_EOI          0x0b0
#define CPU_LAPIC_REG_RRR          0x0c0
#define CPU_LAPIC_REG_LDR          0x0d0
#define CPU_LAPIC_REG_DFR          0x0e0
#define CPU_LAPIC_REG_SVR          0x0f0
#define CPU_LAPIC_REG_ISR_BASE     0x100
#define CPU_LAPIC_REG_TMR_BASE     0x180
#define CPU_LAPIC_REG_IRR_BASE     0x200
#define CPU_LAPIC_REG_ESR          0x280
#define CPU_LAPIC_REG_ICR_LO       0x300
#define CPU_LAPIC_REG_ICR_HI       0x310
#define CPU_LAPIC_REG_LVT_TIMER    0x320
#define CPU_LAPIC_REG_LVT_THERMAL  0x330
#define CPU_LAPIC_REG_LVT_PERF     0x340
#define CPU_LAPIC_REG_LVT_LINT0    0x350
#define CPU_LAPIC_REG_LVT_LINT1    0x360
#define CPU_LAPIC_REG_LVT_ERROR    0x370
#define CPU_LAPIC_REG_INITIAL_CNT  0x380
#define CPU_LAPIC_REG_CURRENT_CNT  0x390
#define CPU_LAPIC_REG_DIVIDE_CFG   0x3e0

#define CPU_LAPIC_LVT_VECTOR       0x000000ff
#define CPU_LAPIC_LVT_DELIV        0x00000700
#define CPU_LAPIC_LVT_DELIV_FIXED  0x00000000
#define CPU_LAPIC_LVT_DELIV_SMI    0x00000200
#define CPU_LAPIC_LVT_DELIV_NMI    0x00000400
#define CPU_LAPIC_LVT_DELIV_INIT   0x00000500
#define CPU_LAPIC_LVT_DELIV_EXTINT 0x00000700
#define CPU_LAPIC_LVT_STATUS       0x00001000
#define CPU_LAPIC_LVT_REMOTE_IRR   0x00004000
#define CPU_LAPIC_LVT_POLARITY     0x00002000
#define CPU_LAPIC_LVT_TRIGGER      0x00008000
#define CPU_LAPIC_LVT_MASK         0x00010000
#define CPU_LAPIC_LVT_TIMER_MODE   0x00060000
#define CPU_LAPIC_ESR_SEND_CHECKSUM       0x00000001
#define CPU_LAPIC_ESR_RECV_CHECKSUM       0x00000002
#define CPU_LAPIC_ESR_SEND_ACCEPT         0x00000004
#define CPU_LAPIC_ESR_RECV_ACCEPT         0x00000008
#define CPU_LAPIC_ESR_RECV_ILLEGAL_VECTOR 0x00000040

#define CPU_LAPIC_ICR_VECTOR       0x000000ff
#define CPU_LAPIC_ICR_DELIV        0x00000700
#define CPU_LAPIC_ICR_DESTMOD      0x00000800
#define CPU_LAPIC_ICR_STATUS       0x00001000
#define CPU_LAPIC_ICR_LEVEL        0x00004000
#define CPU_LAPIC_ICR_TRIGGER      0x00008000
#define CPU_LAPIC_ICR_RR_MASK      0x00030000
#define CPU_LAPIC_ICR_RR_INPROG    0x00010000
#define CPU_LAPIC_ICR_RR_VALID     0x00020000
#define CPU_LAPIC_ICR_DEST_SHORT   0x000c0000
#define CPU_LAPIC_ICR_LO_WRITE_MASK \
    (CPU_LAPIC_ICR_VECTOR | CPU_LAPIC_ICR_DELIV | CPU_LAPIC_ICR_DESTMOD | \
     CPU_LAPIC_ICR_LEVEL | CPU_LAPIC_ICR_TRIGGER | CPU_LAPIC_ICR_DEST_SHORT)
#define CPU_LAPIC_ICR_HI_DEST_MASK 0xff000000

/* Whether the processor fitted has an on-chip local APIC. */
int
cpu_apic_model_capable(void)
{
    if (cpu_s == NULL)
        return 0;

    switch (cpu_s->cpu_type) {
        case CPU_PENTIUM:
        case CPU_PENTIUMMMX:
        case CPU_PENTIUMPRO:
        case CPU_PENTIUM2:
        case CPU_PENTIUM2D:
            return 1;

        default:
            return 0;
    }
}

int
cpu_apic_feature_enabled(void)
{
    return apic_cpu_feature_enabled_for_model(cpu_apic_model_capable());
}

static uint8_t
cpu_current_lapic_index(void)
{
    /* Only registered CPU contexts map directly; clamped callers use slot 0 fallback. */
    cpu_state_t *current = &cpu_state;

    for (uint8_t i = 0; i < IOAPIC_STATE_MAX_LAPICS; i++) {
        if (cpu_lapic_contexts[i].state == current)
            return cpu_lapic_contexts[i].lapic_index;
    }

    return 0;
}

static uint8_t
cpu_current_lapic_index_clamped(void)
{
    uint8_t lapic_index = cpu_current_lapic_index();

    return (lapic_index < IOAPIC_STATE_MAX_LAPICS) ? lapic_index : 0;
}

uint8_t
cpu_lapic_index_source(void)
{
    return cpu_current_lapic_index_clamped();
}

static uint8_t
cpu_lapic_id_reset_value(uint8_t lapic_index)
{
    return lapic_index;
}

static void
cpu_lapic_ids_reset(void)
{
    for (uint8_t i = 0; i < IOAPIC_STATE_MAX_LAPICS; i++)
        cpu_lapic_ids[i] = cpu_lapic_id_reset_value(i);
}

static void
cpu_lapic_ids_sync(void)
{
    for (uint8_t i = 0; i < IOAPIC_STATE_MAX_LAPICS; i++)
        lapic_id_sync_index(i, cpu_lapic_ids[i]);
}

uint8_t
cpu_lapic_id_read_index(uint8_t lapic_index)
{
    if (lapic_index >= IOAPIC_STATE_MAX_LAPICS)
        return 0;

    return cpu_lapic_ids[lapic_index];
}

void
cpu_lapic_id_load_index(uint8_t lapic_index, uint8_t id)
{
    /* Load helpers rebuild CPU-owned state; runtime writes also sync LAPIC mirrors. */
    if (lapic_index >= IOAPIC_STATE_MAX_LAPICS)
        return;

    cpu_lapic_ids[lapic_index] = id;
}

static void
cpu_lapic_id_write_index(uint8_t lapic_index, uint32_t val)
{
    if (lapic_index >= IOAPIC_STATE_MAX_LAPICS)
        return;

    /* Four bits, 27:24; 31:28 are reserved (Pentium Vol. 3, Fig. 19-3; the
       P6's APIC ID is four bits too). */
    cpu_lapic_ids[lapic_index] = (val >> 24) & 0x0f;
    lapic_id_sync_index(lapic_index, cpu_lapic_ids[lapic_index]);
}

/* CPUID 1's EBX. On the Pentium Pro and Pentium II, its only callers, it
   is "EBX <- Reserved" (Pentium Pro Vol. 2, 242691-001, CPUID): the initial
   APIC ID in bits 31:24 came with later processors, none modelled here. */
uint32_t
cpu_cpuid_apic_id_ebx(void)
{
    return 0;
}

static uint32_t
cpu_lapic_svr_reset_value(uint8_t lapic_index)
{
    (void) lapic_index;

    return CPU_LAPIC_SVR_DEFAULT;
}

static uint32_t
cpu_lapic_svr_sanitize(uint32_t val)
{
    return lapic_svr_sanitize(val);
}

static void
cpu_lapic_svrs_reset(void)
{
    for (uint8_t i = 0; i < IOAPIC_STATE_MAX_LAPICS; i++)
        cpu_lapic_svrs[i] = cpu_lapic_svr_reset_value(i);
}

static void
cpu_lapic_svrs_sync(void)
{
    for (uint8_t i = 0; i < IOAPIC_STATE_MAX_LAPICS; i++)
        lapic_svr_sync_index(i, cpu_lapic_svrs[i]);
}

void
cpu_lapic_svr_load_index(uint8_t lapic_index, uint32_t val)
{
    if (lapic_index >= IOAPIC_STATE_MAX_LAPICS)
        return;

    cpu_lapic_svrs[lapic_index] = cpu_lapic_svr_sanitize(val);
}

static uint32_t
cpu_lapic_tpr_reset_value(uint8_t lapic_index)
{
    (void) lapic_index;

    return CPU_LAPIC_TPR_DEFAULT;
}

static void
cpu_lapic_tprs_reset(void)
{
    for (uint8_t i = 0; i < IOAPIC_STATE_MAX_LAPICS; i++)
        cpu_lapic_tprs[i] = cpu_lapic_tpr_reset_value(i);
}

static void
cpu_lapic_tprs_sync(void)
{
    for (uint8_t i = 0; i < IOAPIC_STATE_MAX_LAPICS; i++)
        lapic_tpr_sync_index(i, cpu_lapic_tprs[i]);
}

uint32_t
cpu_lapic_tpr_read_index(uint8_t lapic_index)
{
    if (lapic_index >= IOAPIC_STATE_MAX_LAPICS)
        return 0;

    return cpu_lapic_tprs[lapic_index];
}

void
cpu_lapic_tpr_load_index(uint8_t lapic_index, uint32_t val)
{
    if (lapic_index >= IOAPIC_STATE_MAX_LAPICS)
        return;

    cpu_lapic_tprs[lapic_index] = lapic_tpr_sanitize(val);
    cpu_lapic_ppr_update_index(lapic_index);
}

static uint32_t
cpu_lapic_ldr_reset_value(uint8_t lapic_index)
{
    (void) lapic_index;

    return CPU_LAPIC_LDR_DEFAULT;
}

static void
cpu_lapic_ldrs_reset(void)
{
    for (uint8_t i = 0; i < IOAPIC_STATE_MAX_LAPICS; i++)
        cpu_lapic_ldrs[i] = cpu_lapic_ldr_reset_value(i);
}

static void
cpu_lapic_ldrs_sync(void)
{
    for (uint8_t i = 0; i < IOAPIC_STATE_MAX_LAPICS; i++)
        lapic_ldr_sync_index(i, cpu_lapic_ldrs[i]);
}

uint32_t
cpu_lapic_ldr_read_index(uint8_t lapic_index)
{
    if (lapic_index >= IOAPIC_STATE_MAX_LAPICS)
        return 0;

    return cpu_lapic_ldrs[lapic_index];
}

void
cpu_lapic_ldr_load_index(uint8_t lapic_index, uint32_t val)
{
    if (lapic_index >= IOAPIC_STATE_MAX_LAPICS)
        return;

    cpu_lapic_ldrs[lapic_index] = lapic_ldr_sanitize(val);
}

static uint32_t
cpu_lapic_dfr_reset_value(uint8_t lapic_index)
{
    (void) lapic_index;

    return CPU_LAPIC_DFR_DEFAULT;
}

static void
cpu_lapic_dfrs_reset(void)
{
    for (uint8_t i = 0; i < IOAPIC_STATE_MAX_LAPICS; i++)
        cpu_lapic_dfrs[i] = cpu_lapic_dfr_reset_value(i);
}

static void
cpu_lapic_dfrs_sync(void)
{
    for (uint8_t i = 0; i < IOAPIC_STATE_MAX_LAPICS; i++)
        lapic_dfr_sync_index(i, cpu_lapic_dfrs[i]);
}

uint32_t
cpu_lapic_dfr_read_index(uint8_t lapic_index)
{
    if (lapic_index >= IOAPIC_STATE_MAX_LAPICS)
        return CPU_LAPIC_DFR_DEFAULT;

    return cpu_lapic_dfrs[lapic_index];
}

void
cpu_lapic_dfr_load_index(uint8_t lapic_index, uint32_t val)
{
    if (lapic_index >= IOAPIC_STATE_MAX_LAPICS)
        return;

    cpu_lapic_dfrs[lapic_index] = lapic_dfr_sanitize(val);
}

/* The on-chip APIC of the P54C and P55C is the first of its line: version
   1xh, four LVT entries (timer, LINT0, LINT1, error -- MaxLVT 3), and no
   thermal or performance-counter entries (Pentium Vol. 3, Fig. 19-14 and
   Table 19-6). 10h is the P54C's; 11h is what the P55C is reported to
   answer. Later parts keep the P6 value. */
static uint32_t
cpu_lapic_version_for_model(void)
{
    if (cpu_s != NULL) {
        if (cpu_s->cpu_type == CPU_PENTIUM)
            return 0x00030010;
        if (cpu_s->cpu_type == CPU_PENTIUMMMX)
            return 0x00030011;
    }

    return CPU_LAPIC_VERSION_DEFAULT;
}

static void
cpu_lapic_versions_reset(void)
{
    for (uint8_t i = 0; i < IOAPIC_STATE_MAX_LAPICS; i++)
        cpu_lapic_versions[i] = cpu_lapic_version_for_model();
}

static void
cpu_lapic_versions_sync(void)
{
    for (uint8_t i = 0; i < IOAPIC_STATE_MAX_LAPICS; i++)
        lapic_version_sync_index(i, cpu_lapic_versions[i]);
}

static void
cpu_lapic_arb_ids_reset(void)
{
    memset(cpu_lapic_arb_ids, 0, sizeof(cpu_lapic_arb_ids));
}

static void
cpu_lapic_arb_ids_sync(void)
{
    for (uint8_t i = 0; i < IOAPIC_STATE_MAX_LAPICS; i++)
        cpu_lapic_arb_id_load_index(i, cpu_lapic_arb_ids[i]);
}

void
cpu_lapic_arb_id_load_index(uint8_t lapic_index, uint32_t val)
{
    if (lapic_index >= IOAPIC_STATE_MAX_LAPICS)
        return;

    cpu_lapic_arb_ids[lapic_index] = val & 0x0f000000;
    lapic_arb_id_sync_index(lapic_index, cpu_lapic_arb_ids[lapic_index]);
}

void
cpu_lapic_version_load_index(uint8_t lapic_index, uint32_t val)
{
    if (lapic_index >= IOAPIC_STATE_MAX_LAPICS)
        return;

    cpu_lapic_versions[lapic_index] = val;
}

static void
cpu_lapic_pprs_reset(void)
{
    memset(cpu_lapic_pprs, 0, sizeof(cpu_lapic_pprs));
}

static void
cpu_lapic_pprs_sync(void)
{
    for (uint8_t i = 0; i < IOAPIC_STATE_MAX_LAPICS; i++)
        lapic_ppr_sync_index(i, cpu_lapic_pprs[i]);
}

void
cpu_lapic_ppr_write_index(uint8_t lapic_index, uint32_t val)
{
    if (lapic_index >= IOAPIC_STATE_MAX_LAPICS)
        return;

    cpu_lapic_pprs[lapic_index] = val;
    lapic_ppr_sync_index(lapic_index, cpu_lapic_pprs[lapic_index]);
}

void
cpu_lapic_ppr_load_index(uint8_t lapic_index, uint32_t val)
{
    if (lapic_index >= IOAPIC_STATE_MAX_LAPICS)
        return;

    cpu_lapic_pprs[lapic_index] = val;
}

static void
cpu_lapic_esrs_reset(void)
{
    memset(cpu_lapic_esrs, 0, sizeof(cpu_lapic_esrs));
    memset(cpu_lapic_esr_errors, 0, sizeof(cpu_lapic_esr_errors));
}

static void
cpu_lapic_esrs_sync(void)
{
    for (uint8_t i = 0; i < IOAPIC_STATE_MAX_LAPICS; i++)
        lapic_esr_sync_index(i, cpu_lapic_esrs[i]);
}

/* An error the local APIC detected. The P54C's ESR shows it at once. The
   P6's "is reset after being written to by the processor. A write to the
   ESR must be done just prior to reading the ESR to allow the register to
   be updated. ... Error bits are cumulative" (Pentium Pro Vol. 3,
   7.4.14): it gathers its errors, and a write moves them into the
   register (cpu_lapic_mmio_write_index), so software that wants the
   errors logged so far writes and then reads it, and two writes in a row
   clear it. */
void
cpu_lapic_esr_or_index(uint8_t lapic_index, uint32_t error)
{
    if (lapic_index >= IOAPIC_STATE_MAX_LAPICS)
        return;

    if (!cpu_is_p5()) {
        cpu_lapic_esr_errors[lapic_index] |= error;
        return;
    }
    cpu_lapic_esrs[lapic_index] |= error;
    lapic_esr_sync_index(lapic_index, cpu_lapic_esrs[lapic_index]);
}

/* "The ESR is a read-only register and is reset after being read by
   either the CPU or through the Remote Read message" (Pentium Vol. 3,
   19.3.3): the P54C's. The P6's is updated by a write instead. */
void
cpu_lapic_esr_read_clear_index(uint8_t lapic_index)
{
    if ((lapic_index >= IOAPIC_STATE_MAX_LAPICS) || !cpu_is_p5())
        return;

    cpu_lapic_esrs[lapic_index] = 0;
    lapic_esr_sync_index(lapic_index, 0);
}

void
cpu_lapic_esr_load_index(uint8_t lapic_index, uint32_t val)
{
    if (lapic_index >= IOAPIC_STATE_MAX_LAPICS)
        return;

    cpu_lapic_esrs[lapic_index]       = lapic_esr_sanitize(val);
    cpu_lapic_esr_errors[lapic_index] = 0;
}

static void
cpu_lapic_icrs_reset(void)
{
    memset(cpu_lapic_icr_los, 0, sizeof(cpu_lapic_icr_los));
    memset(cpu_lapic_icr_his, 0, sizeof(cpu_lapic_icr_his));
}

static void
cpu_lapic_icrs_sync(void)
{
    for (uint8_t i = 0; i < IOAPIC_STATE_MAX_LAPICS; i++) {
        lapic_icr_lo_sync_index(i, cpu_lapic_icr_los[i]);
        lapic_icr_hi_sync_index(i, cpu_lapic_icr_his[i]);
    }
}

uint32_t
cpu_lapic_icr_lo_read_index(uint8_t lapic_index)
{
    if (lapic_index >= IOAPIC_STATE_MAX_LAPICS)
        return 0;

    return cpu_lapic_icr_los[lapic_index];
}

void
cpu_lapic_icr_lo_load_index(uint8_t lapic_index, uint32_t val)
{
    if (lapic_index >= IOAPIC_STATE_MAX_LAPICS)
        return;

    cpu_lapic_icr_los[lapic_index] = lapic_icr_lo_load_sanitize(val);
}

uint32_t
cpu_lapic_icr_hi_read_index(uint8_t lapic_index)
{
    if (lapic_index >= IOAPIC_STATE_MAX_LAPICS)
        return 0;

    return cpu_lapic_icr_his[lapic_index];
}

void
cpu_lapic_icr_hi_load_index(uint8_t lapic_index, uint32_t val)
{
    if (lapic_index >= IOAPIC_STATE_MAX_LAPICS)
        return;

    cpu_lapic_icr_his[lapic_index] = lapic_icr_hi_sanitize(val);
}

int
cpu_lapic_icr_delivery_write_index(uint8_t lapic_index, uint32_t val,
                                   cpu_lapic_icr_delivery_t *delivery)
{
    /* ICR low writes latch send-pending state before delivery consumes decoded fields. */
    if ((lapic_index >= IOAPIC_STATE_MAX_LAPICS) || (delivery == NULL))
        return 0;

    cpu_lapic_icr_los[lapic_index] =
        ((val & CPU_LAPIC_ICR_LO_WRITE_MASK) | CPU_LAPIC_ICR_STATUS) &
        ~CPU_LAPIC_ICR_RR_MASK;
    lapic_icr_lo_sync_index(lapic_index, cpu_lapic_icr_los[lapic_index]);

    delivery->vector = val & CPU_LAPIC_ICR_VECTOR;
    delivery->dest = cpu_lapic_icr_his[lapic_index] >> 24;
    delivery->delivery = val & CPU_LAPIC_ICR_DELIV;
    delivery->shorthand = val & CPU_LAPIC_ICR_DEST_SHORT;
    delivery->destmod = !!(val & CPU_LAPIC_ICR_DESTMOD);
    delivery->level = !!(val & CPU_LAPIC_ICR_LEVEL);
    delivery->trigger = !!(val & CPU_LAPIC_ICR_TRIGGER);

    return 1;
}

void
cpu_lapic_icr_delivery_complete_index(uint8_t lapic_index)
{
    if (lapic_index >= IOAPIC_STATE_MAX_LAPICS)
        return;

    cpu_lapic_icr_los[lapic_index] &= ~CPU_LAPIC_ICR_STATUS;
    lapic_icr_lo_sync_index(lapic_index, cpu_lapic_icr_los[lapic_index]);
}

void
cpu_lapic_icr_remote_read_in_progress_index(uint8_t lapic_index)
{
    /* Remote-read status lives in ICR low; payload lives in RRR for save/load. */
    if (lapic_index >= IOAPIC_STATE_MAX_LAPICS)
        return;

    cpu_lapic_icr_los[lapic_index] =
        (cpu_lapic_icr_los[lapic_index] & ~CPU_LAPIC_ICR_RR_MASK) |
        CPU_LAPIC_ICR_RR_INPROG;
    lapic_icr_lo_sync_index(lapic_index, cpu_lapic_icr_los[lapic_index]);
}

void
cpu_lapic_icr_remote_read_valid_index(uint8_t lapic_index)
{
    if (lapic_index >= IOAPIC_STATE_MAX_LAPICS)
        return;

    cpu_lapic_icr_los[lapic_index] =
        (cpu_lapic_icr_los[lapic_index] & ~CPU_LAPIC_ICR_RR_MASK) |
        CPU_LAPIC_ICR_RR_VALID;
    lapic_icr_lo_sync_index(lapic_index, cpu_lapic_icr_los[lapic_index]);
}

void
cpu_lapic_icr_remote_read_complete_index(uint8_t lapic_index, uint32_t val)
{
    cpu_lapic_rrr_write_index(lapic_index, val);
    cpu_lapic_icr_remote_read_valid_index(lapic_index);
}

void
cpu_lapic_icr_remote_read_clear_index(uint8_t lapic_index)
{
    if (lapic_index >= IOAPIC_STATE_MAX_LAPICS)
        return;

    cpu_lapic_icr_los[lapic_index] &= ~CPU_LAPIC_ICR_RR_MASK;
    lapic_icr_lo_sync_index(lapic_index, cpu_lapic_icr_los[lapic_index]);
}

static void
cpu_lapic_rrrs_reset(void)
{
    memset(cpu_lapic_rrrs, 0, sizeof(cpu_lapic_rrrs));
}

static void
cpu_lapic_rrrs_sync(void)
{
    for (uint8_t i = 0; i < IOAPIC_STATE_MAX_LAPICS; i++)
        lapic_rrr_sync_index(i, cpu_lapic_rrrs[i]);
}

void
cpu_lapic_rrr_write_index(uint8_t lapic_index, uint32_t val)
{
    if (lapic_index >= IOAPIC_STATE_MAX_LAPICS)
        return;

    cpu_lapic_rrrs[lapic_index] = val;
    lapic_rrr_sync_index(lapic_index, cpu_lapic_rrrs[lapic_index]);
}

void
cpu_lapic_rrr_load_index(uint8_t lapic_index, uint32_t val)
{
    if (lapic_index >= IOAPIC_STATE_MAX_LAPICS)
        return;

    cpu_lapic_rrrs[lapic_index] = val;
}

static void
cpu_lapic_lvts_reset(uint32_t *lvts)
{
    for (uint8_t i = 0; i < IOAPIC_STATE_MAX_LAPICS; i++)
        lvts[i] = CPU_LAPIC_LVT_DEFAULT;
}

static uint32_t
cpu_lapic_lvt_sanitize(uint32_t reg, uint32_t val)
{
    uint32_t v = lapic_lvt_sanitize(reg, val);

    /* The P54C's LINT entries know "000 Fixed, 100 NMI, 111 ExtINT, All
       other combinations are <Reserved>" (Pentium Vol. 3, Fig. 19-6); SMI
       and INIT there are the P6's. A reserved encoding is masked, as the
       other reserved ones are (lapic_lvt_sanitize). */
    if (((reg == CPU_LAPIC_REG_LVT_LINT0) || (reg == CPU_LAPIC_REG_LVT_LINT1)) && cpu_is_p5() &&
        (((v & CPU_LAPIC_LVT_DELIV) == CPU_LAPIC_LVT_DELIV_SMI) ||
         ((v & CPU_LAPIC_LVT_DELIV) == CPU_LAPIC_LVT_DELIV_INIT)))
        v = (v & ~CPU_LAPIC_LVT_DELIV) | CPU_LAPIC_LVT_MASK;
    return v;
}

static uint32_t *
cpu_lapic_lvt_reg_index(uint8_t lapic_index, uint32_t reg)
{
    if (lapic_index >= IOAPIC_STATE_MAX_LAPICS)
        return NULL;

    switch (reg) {
        case CPU_LAPIC_REG_LVT_TIMER:
            return &cpu_lapic_lvt_timers[lapic_index];
        case CPU_LAPIC_REG_LVT_THERMAL:
            return &cpu_lapic_lvt_thermals[lapic_index];
        case CPU_LAPIC_REG_LVT_PERF:
            return &cpu_lapic_lvt_perfs[lapic_index];
        case CPU_LAPIC_REG_LVT_LINT0:
            return &cpu_lapic_lvt_lint0s[lapic_index];
        case CPU_LAPIC_REG_LVT_LINT1:
            return &cpu_lapic_lvt_lint1s[lapic_index];
        case CPU_LAPIC_REG_LVT_ERROR:
            return &cpu_lapic_lvt_errors[lapic_index];
        default:
            return NULL;
    }
}

static void
cpu_lapic_lvt_sync_index(uint8_t lapic_index, uint32_t reg, uint32_t val)
{
    switch (reg) {
        case CPU_LAPIC_REG_LVT_TIMER:
            lapic_lvt_timer_sync_index(lapic_index, val);
            break;
        case CPU_LAPIC_REG_LVT_THERMAL:
            lapic_lvt_thermal_sync_index(lapic_index, val);
            break;
        case CPU_LAPIC_REG_LVT_PERF:
            lapic_lvt_perf_sync_index(lapic_index, val);
            break;
        case CPU_LAPIC_REG_LVT_LINT0:
            lapic_lvt_lint0_sync_index(lapic_index, val);
            break;
        case CPU_LAPIC_REG_LVT_LINT1:
            lapic_lvt_lint1_sync_index(lapic_index, val);
            break;
        case CPU_LAPIC_REG_LVT_ERROR:
            lapic_lvt_error_sync_index(lapic_index, val);
            break;
        default:
            break;
    }
}

/* The APIC disabled in software: every LVT entry masked. */
static void
cpu_lapic_lvt_mask_all_index(uint8_t lapic_index)
{
    static const uint32_t regs[] = { CPU_LAPIC_REG_LVT_TIMER, CPU_LAPIC_REG_LVT_THERMAL, CPU_LAPIC_REG_LVT_PERF,
                                     CPU_LAPIC_REG_LVT_LINT0, CPU_LAPIC_REG_LVT_LINT1, CPU_LAPIC_REG_LVT_ERROR };

    for (unsigned i = 0; i < (sizeof(regs) / sizeof(regs[0])); i++) {
        uint32_t *lvt = cpu_lapic_lvt_reg_index(lapic_index, regs[i]);

        if ((lvt != NULL) && !(*lvt & CPU_LAPIC_LVT_MASK)) {
            *lvt |= CPU_LAPIC_LVT_MASK;
            cpu_lapic_lvt_sync_index(lapic_index, regs[i], *lvt);
        }
    }
}

static void
cpu_lapic_lvt_load_index(uint32_t *lvts, uint8_t lapic_index, uint32_t reg,
                         uint32_t val)
{
    if (lapic_index >= IOAPIC_STATE_MAX_LAPICS)
        return;

    lvts[lapic_index] = cpu_lapic_lvt_sanitize(reg, val) |
                         (val & (CPU_LAPIC_LVT_STATUS | CPU_LAPIC_LVT_REMOTE_IRR));
}

static void
cpu_lapic_lvt_write_index(uint32_t *lvts, uint8_t lapic_index, uint32_t reg,
                          uint32_t val)
{
    if (lapic_index >= IOAPIC_STATE_MAX_LAPICS)
        return;

    /* "A disabled APIC automatically sets all mask bits in the LVT
       entries. Trying to reset these bits in the local vector table will
       be ignored." (Pentium Vol. 3, 19.3.1.11) */
    if (!(cpu_lapic_svrs[lapic_index] & CPU_LAPIC_SVR_ENABLE))
        val |= CPU_LAPIC_LVT_MASK;
    lvts[lapic_index] = cpu_lapic_lvt_sanitize(reg, val) |
                         (lvts[lapic_index] & (CPU_LAPIC_LVT_STATUS | CPU_LAPIC_LVT_REMOTE_IRR));
}

int
cpu_lapic_mmio_reg_read_only(uint32_t reg)
{
    return lapic_mmio_reg_read_only(reg);
}

static void
cpu_lapic_lvt_delivery_status_index(uint8_t lapic_index, uint32_t reg, int pending)
{
    uint32_t *lvt = cpu_lapic_lvt_reg_index(lapic_index, reg);

    if (lvt == NULL)
        return;

    if (pending)
        *lvt |= CPU_LAPIC_LVT_STATUS;
    else
        *lvt &= ~CPU_LAPIC_LVT_STATUS;

    cpu_lapic_lvt_sync_index(lapic_index, reg, *lvt);
}

void
cpu_lapic_lvt_delivery_complete_index(uint8_t lapic_index, uint32_t reg)
{
    cpu_lapic_lvt_delivery_status_index(lapic_index, reg, 0);
}

/* The vector each LINT's Remote IRR was set for: its EOI is the one that
   clears it, whatever the entry holds by then. */
static uint8_t cpu_lapic_lint_rirr_vector[IOAPIC_STATE_MAX_LAPICS][2];

/* A level-triggered LINT interrupt accepted: "For level triggered
   interrupts, the bit is set when the logic of the Local APIC accepts the
   interrupt. The remote IRR bit is reset when an EOI command is received"
   (Pentium Vol. 3, p. 19-13). */
void
cpu_lapic_lint_remote_irr_set_index(uint8_t lapic_index, uint32_t reg, uint8_t vector)
{
    if (lapic_index >= IOAPIC_STATE_MAX_LAPICS)
        return;
    if (reg == CPU_LAPIC_REG_LVT_LINT0) {
        cpu_lapic_lvt_lint0s[lapic_index] |= CPU_LAPIC_LVT_REMOTE_IRR;
        cpu_lapic_lint_rirr_vector[lapic_index][0] = vector;
        cpu_lapic_lvt_sync_index(lapic_index, reg, cpu_lapic_lvt_lint0s[lapic_index]);
    } else if (reg == CPU_LAPIC_REG_LVT_LINT1) {
        cpu_lapic_lvt_lint1s[lapic_index] |= CPU_LAPIC_LVT_REMOTE_IRR;
        cpu_lapic_lint_rirr_vector[lapic_index][1] = vector;
        cpu_lapic_lvt_sync_index(lapic_index, reg, cpu_lapic_lvt_lint1s[lapic_index]);
    }
}

/* The EOI for vector: the LINT entries it retires. */
static void
cpu_lapic_lint_remote_irr_eoi(uint8_t lapic_index, int vector)
{
    uint32_t      *lvt[2] = { &cpu_lapic_lvt_lint0s[lapic_index], &cpu_lapic_lvt_lint1s[lapic_index] };
    const uint32_t reg[2] = { CPU_LAPIC_REG_LVT_LINT0, CPU_LAPIC_REG_LVT_LINT1 };

    for (int i = 0; i < 2; i++)
        if ((*lvt[i] & CPU_LAPIC_LVT_REMOTE_IRR) && ((int) cpu_lapic_lint_rirr_vector[lapic_index][i] == vector)) {
            *lvt[i] &= ~CPU_LAPIC_LVT_REMOTE_IRR;
            cpu_lapic_lvt_sync_index(lapic_index, reg[i], *lvt[i]);
            /* Remote IRR held a level source off until this EOI; one
               still asserted comes in again, as an I/O APIC input does
               (82093AA 3.2.4). LINT1 is the NMI line, whose level the
               board may know. */
            if ((i == 1) && (*lvt[i] & CPU_LAPIC_LVT_TRIGGER) && (nmi_line_level != NULL) && nmi_line_level())
                lapic_lint1_redeliver_index(lapic_index);
        }
}

static void
cpu_lapic_lvt_timers_reset(void)
{
    cpu_lapic_lvts_reset(cpu_lapic_lvt_timers);
}

static void
cpu_lapic_lvt_timers_sync(void)
{
    for (uint8_t i = 0; i < IOAPIC_STATE_MAX_LAPICS; i++)
        lapic_lvt_timer_sync_index(i, cpu_lapic_lvt_timers[i]);
}

static void
cpu_lapic_lvt_thermals_reset(void)
{
    cpu_lapic_lvts_reset(cpu_lapic_lvt_thermals);
}

static void
cpu_lapic_lvt_thermals_sync(void)
{
    for (uint8_t i = 0; i < IOAPIC_STATE_MAX_LAPICS; i++)
        lapic_lvt_thermal_sync_index(i, cpu_lapic_lvt_thermals[i]);
}

static void
cpu_lapic_lvt_thermal_load_index(uint8_t lapic_index, uint32_t val)
{
    cpu_lapic_lvt_load_index(cpu_lapic_lvt_thermals, lapic_index,
                             CPU_LAPIC_REG_LVT_THERMAL, val);
}

static void
cpu_lapic_lvt_perfs_reset(void)
{
    cpu_lapic_lvts_reset(cpu_lapic_lvt_perfs);
}

static void
cpu_lapic_lvt_perfs_sync(void)
{
    for (uint8_t i = 0; i < IOAPIC_STATE_MAX_LAPICS; i++)
        lapic_lvt_perf_sync_index(i, cpu_lapic_lvt_perfs[i]);
}

static void
cpu_lapic_lvt_perf_load_index(uint8_t lapic_index, uint32_t val)
{
    cpu_lapic_lvt_load_index(cpu_lapic_lvt_perfs, lapic_index,
                             CPU_LAPIC_REG_LVT_PERF, val);
}

uint32_t
cpu_lapic_lvt_timer_read_index(uint8_t lapic_index)
{
    if (lapic_index >= IOAPIC_STATE_MAX_LAPICS)
        return CPU_LAPIC_LVT_DEFAULT;

    return cpu_lapic_lvt_timers[lapic_index];
}

void
cpu_lapic_lvt_timer_load_index(uint8_t lapic_index, uint32_t val)
{
    cpu_lapic_lvt_load_index(cpu_lapic_lvt_timers, lapic_index,
                             CPU_LAPIC_REG_LVT_TIMER, val);
}

static void
cpu_lapic_lvt_lint0s_reset(void)
{
    cpu_lapic_lvts_reset(cpu_lapic_lvt_lint0s);
}

static void
cpu_lapic_lvt_lint0s_sync(void)
{
    for (uint8_t i = 0; i < IOAPIC_STATE_MAX_LAPICS; i++)
        lapic_lvt_lint0_sync_index(i, cpu_lapic_lvt_lint0s[i]);
}

uint32_t
cpu_lapic_lvt_lint0_read_index(uint8_t lapic_index)
{
    if (lapic_index >= IOAPIC_STATE_MAX_LAPICS)
        return CPU_LAPIC_LVT_DEFAULT;

    return cpu_lapic_lvt_lint0s[lapic_index];
}

void
cpu_lapic_lvt_lint0_load_index(uint8_t lapic_index, uint32_t val)
{
    cpu_lapic_lvt_load_index(cpu_lapic_lvt_lint0s, lapic_index,
                             CPU_LAPIC_REG_LVT_LINT0, val);
    /* A save state holds no vector for Remote IRR: the entry's own is the
       one it delivered. */
    if ((lapic_index < IOAPIC_STATE_MAX_LAPICS) && (val & CPU_LAPIC_LVT_REMOTE_IRR))
        cpu_lapic_lint_rirr_vector[lapic_index][0] = val & 0xff;
}

static void
cpu_lapic_lvt_lint1s_reset(void)
{
    cpu_lapic_lvts_reset(cpu_lapic_lvt_lint1s);
}

static void
cpu_lapic_lvt_lint1s_sync(void)
{
    for (uint8_t i = 0; i < IOAPIC_STATE_MAX_LAPICS; i++)
        lapic_lvt_lint1_sync_index(i, cpu_lapic_lvt_lint1s[i]);
}

void
cpu_lapic_lvt_lint1_load_index(uint8_t lapic_index, uint32_t val)
{
    cpu_lapic_lvt_load_index(cpu_lapic_lvt_lint1s, lapic_index,
                             CPU_LAPIC_REG_LVT_LINT1, val);
    if ((lapic_index < IOAPIC_STATE_MAX_LAPICS) && (val & CPU_LAPIC_LVT_REMOTE_IRR))
        cpu_lapic_lint_rirr_vector[lapic_index][1] = val & 0xff;
}

static void
cpu_lapic_lvt_errors_reset(void)
{
    cpu_lapic_lvts_reset(cpu_lapic_lvt_errors);
}

static void
cpu_lapic_lvt_errors_sync(void)
{
    for (uint8_t i = 0; i < IOAPIC_STATE_MAX_LAPICS; i++)
        lapic_lvt_error_sync_index(i, cpu_lapic_lvt_errors[i]);
}

void
cpu_lapic_lvt_error_load_index(uint8_t lapic_index, uint32_t val)
{
    cpu_lapic_lvt_load_index(cpu_lapic_lvt_errors, lapic_index,
                             CPU_LAPIC_REG_LVT_ERROR, val);
}

static void
cpu_lapic_irrs_reset(void)
{
    memset(cpu_lapic_irrs, 0, sizeof(cpu_lapic_irrs));
    cpu_lapic_irr_live = 0;
}

/* The interrupt check runs after every block (x86_interrupt.h). These
   let it answer "nothing" from three loads while nothing is pending:
   bit n of cpu_lapic_irr_live is set while local APIC n's IRR holds any
   vector, and cpu_lapic_extint_live[n] while it holds an ExtINT. */
static void
cpu_lapic_irr_live_set(uint8_t lapic_index, int any)
{
    if (any)
        cpu_lapic_irr_live |= 1u << lapic_index;
    else
        cpu_lapic_irr_live &= ~(1u << lapic_index);
}

static void
cpu_lapic_irr_live_sync(uint8_t lapic_index)
{
    uint32_t any = 0;

    for (int w = 0; w < IOAPIC_STATE_LAPIC_BIT_WORDS; w++)
        any |= cpu_lapic_irrs[lapic_index][w];

    cpu_lapic_irr_live_set(lapic_index, !!any);
}

void
cpu_lapic_extint_live_set(uint8_t lapic_index, int pending)
{
    if (lapic_index < IOAPIC_STATE_MAX_LAPICS)
        cpu_lapic_extint_live[lapic_index] = !!pending;
}

uint32_t *
cpu_lapic_irr_words_index(uint8_t lapic_index)
{
    /* Invalid bitmap reads return zeroed scratch while preserving pointer-style access. */
    static uint32_t invalid[IOAPIC_STATE_LAPIC_BIT_WORDS];

    if (lapic_index >= IOAPIC_STATE_MAX_LAPICS) {
        memset(invalid, 0, sizeof(invalid));
        return invalid;
    }

    return cpu_lapic_irrs[lapic_index];
}

void
cpu_lapic_irr_reset_index(uint8_t lapic_index)
{
    if (lapic_index >= IOAPIC_STATE_MAX_LAPICS)
        return;

    memset(cpu_lapic_irrs[lapic_index], 0, sizeof(cpu_lapic_irrs[lapic_index]));
    cpu_lapic_irr_live_sync(lapic_index);
}

void
cpu_lapic_irr_load_index(uint8_t lapic_index, const uint32_t words[IOAPIC_STATE_LAPIC_BIT_WORDS])
{
    if (lapic_index >= IOAPIC_STATE_MAX_LAPICS)
        return;

    memcpy(cpu_lapic_irrs[lapic_index], words, sizeof(cpu_lapic_irrs[lapic_index]));
    cpu_lapic_irr_live_sync(lapic_index);
}

void
cpu_lapic_irr_save_index(uint8_t lapic_index, uint32_t words[IOAPIC_STATE_LAPIC_BIT_WORDS])
{
    if (lapic_index >= IOAPIC_STATE_MAX_LAPICS) {
        memset(words, 0, sizeof(uint32_t) * IOAPIC_STATE_LAPIC_BIT_WORDS);
        return;
    }

    memcpy(words, cpu_lapic_irrs[lapic_index], sizeof(cpu_lapic_irrs[lapic_index]));
}

static void
cpu_lapic_isrs_reset(void)
{
    memset(cpu_lapic_isrs, 0, sizeof(cpu_lapic_isrs));
}

uint32_t *
cpu_lapic_isr_words_index(uint8_t lapic_index)
{
    static uint32_t invalid[IOAPIC_STATE_LAPIC_BIT_WORDS];

    if (lapic_index >= IOAPIC_STATE_MAX_LAPICS) {
        memset(invalid, 0, sizeof(invalid));
        return invalid;
    }

    return cpu_lapic_isrs[lapic_index];
}

void
cpu_lapic_isr_reset_index(uint8_t lapic_index)
{
    if (lapic_index >= IOAPIC_STATE_MAX_LAPICS)
        return;

    memset(cpu_lapic_isrs[lapic_index], 0, sizeof(cpu_lapic_isrs[lapic_index]));
}

void
cpu_lapic_isr_load_index(uint8_t lapic_index, const uint32_t words[IOAPIC_STATE_LAPIC_BIT_WORDS])
{
    if (lapic_index >= IOAPIC_STATE_MAX_LAPICS)
        return;

    memcpy(cpu_lapic_isrs[lapic_index], words, sizeof(cpu_lapic_isrs[lapic_index]));
}

void
cpu_lapic_isr_save_index(uint8_t lapic_index, uint32_t words[IOAPIC_STATE_LAPIC_BIT_WORDS])
{
    if (lapic_index >= IOAPIC_STATE_MAX_LAPICS) {
        memset(words, 0, sizeof(uint32_t) * IOAPIC_STATE_LAPIC_BIT_WORDS);
        return;
    }

    memcpy(words, cpu_lapic_isrs[lapic_index], sizeof(cpu_lapic_isrs[lapic_index]));
}

static void
cpu_lapic_tmrs_reset(void)
{
    memset(cpu_lapic_tmrs, 0, sizeof(cpu_lapic_tmrs));
}

uint32_t *
cpu_lapic_tmr_words_index(uint8_t lapic_index)
{
    static uint32_t invalid[IOAPIC_STATE_LAPIC_BIT_WORDS];

    if (lapic_index >= IOAPIC_STATE_MAX_LAPICS) {
        memset(invalid, 0, sizeof(invalid));
        return invalid;
    }

    return cpu_lapic_tmrs[lapic_index];
}

void
cpu_lapic_tmr_reset_index(uint8_t lapic_index)
{
    if (lapic_index >= IOAPIC_STATE_MAX_LAPICS)
        return;

    memset(cpu_lapic_tmrs[lapic_index], 0, sizeof(cpu_lapic_tmrs[lapic_index]));
}

void
cpu_lapic_tmr_load_index(uint8_t lapic_index, const uint32_t words[IOAPIC_STATE_LAPIC_BIT_WORDS])
{
    if (lapic_index >= IOAPIC_STATE_MAX_LAPICS)
        return;

    memcpy(cpu_lapic_tmrs[lapic_index], words, sizeof(cpu_lapic_tmrs[lapic_index]));
}

void
cpu_lapic_tmr_save_index(uint8_t lapic_index, uint32_t words[IOAPIC_STATE_LAPIC_BIT_WORDS])
{
    if (lapic_index >= IOAPIC_STATE_MAX_LAPICS) {
        memset(words, 0, sizeof(uint32_t) * IOAPIC_STATE_LAPIC_BIT_WORDS);
        return;
    }

    memcpy(words, cpu_lapic_tmrs[lapic_index], sizeof(cpu_lapic_tmrs[lapic_index]));
}

static int
cpu_lapic_highest_vector(const uint32_t bits[IOAPIC_STATE_LAPIC_BIT_WORDS])
{
    for (int word = IOAPIC_STATE_LAPIC_BIT_WORDS - 1; word >= 0; word--) {
        if (!bits[word])
            continue;

        for (int bit = 31; bit >= 0; bit--) {
            if (bits[word] & (1u << bit))
                return (word << 5) | bit;
        }
    }

    return -1;
}

int
cpu_lapic_base_enabled_index(uint8_t lapic_index)
{
    if (lapic_index >= IOAPIC_STATE_MAX_LAPICS)
        return 0;

    return !!(cpu_apic_base_msrs[lapic_index] & CPU_APIC_BASE_ENABLE);
}

int
cpu_lapic_software_enabled_index(uint8_t lapic_index)
{
    if (lapic_index >= IOAPIC_STATE_MAX_LAPICS)
        return 0;

    return !!(cpu_lapic_svrs[lapic_index] & CPU_LAPIC_SVR_ENABLE);
}

int
cpu_lapic_enabled_index(uint8_t lapic_index, int started)
{
    return started &&
           cpu_lapic_base_enabled_index(lapic_index) &&
           cpu_lapic_software_enabled_index(lapic_index);
}

static int
cpu_lapic_vector_valid(uint8_t vector)
{
    return lapic_vector_valid(vector);
}

int
cpu_lapic_error_delivery_action_index(uint8_t lapic_index, int started,
                                      uint32_t error, uint8_t *vector)
{
    /* ESR records attempted error delivery before enable/mask checks can reject it. */
    uint32_t lvt;
    uint8_t  error_vector;

    if (vector != NULL)
        *vector = 0;

    if (lapic_index >= IOAPIC_STATE_MAX_LAPICS)
        return CPU_LAPIC_FIXED_REJECT;

    cpu_lapic_esr_or_index(lapic_index, error);

    if (!cpu_lapic_enabled_index(lapic_index, started))
        return CPU_LAPIC_FIXED_REJECT;

    lvt = cpu_lapic_lvt_errors[lapic_index];
    if (lvt & CPU_LAPIC_LVT_MASK)
        return CPU_LAPIC_FIXED_REJECT;

    error_vector = lvt & CPU_LAPIC_LVT_VECTOR;
    if (!cpu_lapic_vector_valid(error_vector)) {
        cpu_lapic_esr_or_index(lapic_index, CPU_LAPIC_ESR_RECV_ILLEGAL_VECTOR);
        return CPU_LAPIC_FIXED_REJECT;
    }

    if (vector != NULL)
        *vector = error_vector;

    return CPU_LAPIC_FIXED_ACCEPT;
}

int
cpu_lapic_fixed_delivery_action_index(uint8_t lapic_index, int started,
                                      uint8_t vector, uint32_t invalid_error,
                                      uint8_t *error_vector)
{
    /* Invalid fixed vectors can report through Error LVT without accepting the vector. */
    if (error_vector != NULL)
        *error_vector = 0;

    if ((lapic_index >= IOAPIC_STATE_MAX_LAPICS) ||
        !cpu_lapic_enabled_index(lapic_index, started))
        return CPU_LAPIC_FIXED_REJECT;

    if (cpu_lapic_vector_valid(vector))
        return CPU_LAPIC_FIXED_ACCEPT;

    if (invalid_error &&
        (cpu_lapic_error_delivery_action_index(lapic_index, started, invalid_error,
                                               error_vector) == CPU_LAPIC_FIXED_ACCEPT))
        return CPU_LAPIC_FIXED_ERROR;

    return CPU_LAPIC_FIXED_REJECT;
}

/* The processor priority from TPR and the highest ISR vector, as a read
   of the register gives it. */
static uint32_t
cpu_lapic_ppr_value_index(uint8_t lapic_index)
{
    uint32_t tpr = cpu_lapic_tprs[lapic_index] & CPU_LAPIC_TPR_MASK;
    int      isr = cpu_lapic_highest_vector(cpu_lapic_isrs[lapic_index]);

    return lapic_processor_priority(tpr, isr, !cpu_is_p5());
}

/* The arbitration priority, stored nowhere either (Pentium Vol. 3,
   19.3.1.9.4): "If (TPR[7:4] >= IRRV[7:4]) and (TPR[7:4] > ISRV[7:4]) then
   APR[7:0] = TPR[7:0], else APR[7:4] = max(TPR[7:4], ISRV[7:4],
   IRRV[7:4]), APR[3:0] = 0", IRRV and ISRV zero with nothing set. Lowest-
   priority delivery goes to the lowest (19.3.2.1: "All 8 bits of the APR
   are used"), and it is what 0FEE0_0090 reads. */
uint32_t
cpu_lapic_apr_value_index(uint8_t lapic_index)
{
    uint32_t tpr;
    int      irr;
    int      isr;
    uint32_t irrv;
    uint32_t isrv;
    uint32_t cls;

    if (lapic_index >= IOAPIC_STATE_MAX_LAPICS)
        return 0;
    tpr  = cpu_lapic_tprs[lapic_index] & CPU_LAPIC_TPR_MASK;
    irr  = cpu_lapic_highest_vector(cpu_lapic_irrs[lapic_index]);
    isr  = cpu_lapic_highest_vector(cpu_lapic_isrs[lapic_index]);
    irrv = (irr < 0) ? 0 : (uint32_t) irr;
    isrv = (isr < 0) ? 0 : (uint32_t) isr;

    if (((tpr & 0xf0) >= (irrv & 0xf0)) && ((tpr & 0xf0) > (isrv & 0xf0)))
        return tpr & 0xff;
    cls = tpr & 0xf0;
    if ((isrv & 0xf0) > cls)
        cls = isrv & 0xf0;
    if ((irrv & 0xf0) > cls)
        cls = irrv & 0xf0;
    return cls;
}

uint32_t
cpu_lapic_ppr_update_index(uint8_t lapic_index)
{
    if (lapic_index >= IOAPIC_STATE_MAX_LAPICS)
        return 0;

    cpu_lapic_pprs[lapic_index] = cpu_lapic_ppr_value_index(lapic_index);
    lapic_ppr_sync_index(lapic_index, cpu_lapic_pprs[lapic_index]);

    return cpu_lapic_pprs[lapic_index];
}

static int
cpu_lapic_priority_deliverable_index(uint8_t lapic_index, uint8_t vector)
{
    uint32_t ppr = cpu_lapic_ppr_value_index(lapic_index);

    return lapic_priority_deliverable(vector, ppr);
}

static void
cpu_lapic_clear_vector(uint32_t bits[IOAPIC_STATE_LAPIC_BIT_WORDS], uint8_t vector)
{
    bits[vector >> 5] &= ~(1u << (vector & 0x1f));
}

static void
cpu_lapic_set_vector(uint32_t bits[IOAPIC_STATE_LAPIC_BIT_WORDS], uint8_t vector)
{
    bits[vector >> 5] |= 1u << (vector & 0x1f);
}

int
cpu_lapic_eoi_index(uint8_t lapic_index, int *level)
{
    /* Return retired ISR vector and its pre-clear trigger mode. */
    int vector;

    if (level != NULL)
        *level = 0;

    if (lapic_index >= IOAPIC_STATE_MAX_LAPICS)
        return -1;

    vector = cpu_lapic_highest_vector(cpu_lapic_isrs[lapic_index]);
    if (vector >= 0) {
        if (level != NULL)
            *level = !!(cpu_lapic_tmrs[lapic_index][vector >> 5] &
                         (1u << (vector & 0x1f)));
        /* Only acceptance changes the TMR (Pentium Vol. 3, 19.3.1.9). */
        cpu_lapic_clear_vector(cpu_lapic_isrs[lapic_index], vector);
        cpu_lapic_lint_remote_irr_eoi(lapic_index, vector);
        cpu_lapic_ppr_update_index(lapic_index);
    }

    return vector;
}

/* Whether a fixed interrupt with this vector finds a slot. The P5 and P6
   local APIC holds two interrupts per priority class: "a 2-deep interrupt
   FIFO for each priority level" (Pentium Vol. 3, 19.3.1.1); "each IAU only
   keeps pending two interrupts in a given priority class ... if a slot is
   not available, the IAU will put 'retry' status on the bus" (Intel, US
   5,555,420); "the IRR and ISR registers can queue no more than two ...
   and will reject other interrupts" (IA SDM 10.8.4, P6 family and
   Pentium). Counted over IRR and ISR in the vector's class. The IRR holds
   one of each vector, so a vector already pending finds no slot either:
   it is retried rather than merged and lost. */
int
cpu_lapic_slot_free_index(uint8_t lapic_index, uint8_t vector)
{
    const unsigned cls  = vector >> 4;
    const uint32_t mask = 0xffffu << ((cls & 1) * 16);
    int            n;

    if (lapic_index >= IOAPIC_STATE_MAX_LAPICS)
        return 1;
    if (cpu_lapic_irrs[lapic_index][vector >> 5] & (1u << (vector & 0x1f)))
        return 0;
    n = __builtin_popcount(cpu_lapic_irrs[lapic_index][cls >> 1] & mask) +
        __builtin_popcount(cpu_lapic_isrs[lapic_index][cls >> 1] & mask);
    return n < 2;
}

int
cpu_lapic_accept_interrupt_index(uint8_t lapic_index, uint8_t vector, int level)
{
    if (lapic_index >= IOAPIC_STATE_MAX_LAPICS)
        return 0;

    cpu_lapic_set_vector(cpu_lapic_irrs[lapic_index], vector);
    cpu_lapic_irr_live_set(lapic_index, 1);
    if (level)
        cpu_lapic_set_vector(cpu_lapic_tmrs[lapic_index], vector);
    else
        cpu_lapic_clear_vector(cpu_lapic_tmrs[lapic_index], vector);

    return 1;
}

int
cpu_lapic_promote_irr_index(uint8_t lapic_index, uint8_t vector)
{
    if (lapic_index >= IOAPIC_STATE_MAX_LAPICS)
        return 0;

    cpu_lapic_clear_vector(cpu_lapic_irrs[lapic_index], vector);
    cpu_lapic_irr_live_sync(lapic_index);
    cpu_lapic_set_vector(cpu_lapic_isrs[lapic_index], vector);
    cpu_lapic_ppr_update_index(lapic_index);

    return 1;
}

int
cpu_lapic_highest_deliverable_irr_index(uint8_t lapic_index, int started)
{
    /* Scan high to low, but leave blocked IRR bits pending for later priority changes. */
    if ((lapic_index >= IOAPIC_STATE_MAX_LAPICS) ||
        !cpu_lapic_enabled_index(lapic_index, started))
        return -1;

    /* Only the highest pending vector can be deliverable: one is taken only
       when its priority class is above the processor priority's, and every
       lower vector's class is lower still (as are the invalid ones below
       10h). The processor priority is worked out once. */
    for (int word = IOAPIC_STATE_LAPIC_BIT_WORDS - 1; word >= 0; word--) {
        uint32_t pending = cpu_lapic_irrs[lapic_index][word];

        if (pending) {
            int vector = (word << 5) | (31 - __builtin_clz(pending));

            if (cpu_lapic_vector_valid(vector) &&
                cpu_lapic_priority_deliverable_index(lapic_index, vector))
                return vector;
            return -1;
        }
    }

    return -1;
}

int
cpu_lapic_extint_deliverable_index(uint8_t lapic_index, int started, int pending)
{
    if ((lapic_index >= IOAPIC_STATE_MAX_LAPICS) || !pending ||
        !cpu_lapic_enabled_index(lapic_index, started))
        return 0;

    /* ExtINT is not IRR/ISR priority-class delivery; INTA supplies the vector. */
    return 1;
}

int
cpu_lapic_lint_delivery_index(uint8_t lapic_index, int started, uint8_t lint,
                              uint8_t *vector, int *level)
{
    /* LINT decode returns actions; delivery layer owns SMI/NMI/ExtINT side effects. */
    uint32_t lvt;

    if (vector != NULL)
        *vector = 0;
    if (level != NULL)
        *level = 0;

    if ((lapic_index >= IOAPIC_STATE_MAX_LAPICS) ||
        !cpu_lapic_enabled_index(lapic_index, started))
        return CPU_LAPIC_LINT_ACTION_NONE;

    lvt = (lint == CPU_LAPIC_LINT1) ?
          cpu_lapic_lvt_lint1s[lapic_index] :
          cpu_lapic_lvt_lint0s[lapic_index];
    if (lvt & CPU_LAPIC_LVT_MASK)
        return CPU_LAPIC_LINT_ACTION_HANDLED;

    switch (lvt & CPU_LAPIC_LVT_DELIV) {
        case CPU_LAPIC_LVT_DELIV_FIXED:
            /* A level interrupt accepted and not yet EOIed holds the line
               off (Remote IRR, Pentium Vol. 3, p. 19-13). */
            if ((lvt & CPU_LAPIC_LVT_TRIGGER) && (lvt & CPU_LAPIC_LVT_REMOTE_IRR))
                return CPU_LAPIC_LINT_ACTION_HANDLED;
            if (vector != NULL)
                *vector = lvt & CPU_LAPIC_LVT_VECTOR;
            if (level != NULL)
                *level = !!(lvt & CPU_LAPIC_LVT_TRIGGER);
            return CPU_LAPIC_LINT_ACTION_FIXED;

        case CPU_LAPIC_LVT_DELIV_SMI:
            return CPU_LAPIC_LINT_ACTION_SMI;

        case CPU_LAPIC_LVT_DELIV_NMI:
            return CPU_LAPIC_LINT_ACTION_NMI;

        case CPU_LAPIC_LVT_DELIV_INIT:
            return CPU_LAPIC_LINT_ACTION_INIT;

        case CPU_LAPIC_LVT_DELIV_EXTINT:
            return CPU_LAPIC_LINT_ACTION_EXTINT;

        default:
            return CPU_LAPIC_LINT_ACTION_HANDLED;
    }
}

int
cpu_lapic_lvt_source_delivery_index(uint8_t lapic_index, int started,
                                    uint8_t source, uint8_t *vector)
{
    uint32_t lvt;

    if (vector != NULL)
        *vector = 0;

    if ((lapic_index >= IOAPIC_STATE_MAX_LAPICS) ||
        !cpu_lapic_enabled_index(lapic_index, started))
        return CPU_LAPIC_LINT_ACTION_NONE;

    if (source == CPU_LAPIC_LVT_SOURCE_THERMAL)
        lvt = cpu_lapic_lvt_thermals[lapic_index];
    else if (source == CPU_LAPIC_LVT_SOURCE_PERF)
        lvt = cpu_lapic_lvt_perfs[lapic_index];
    else
        return CPU_LAPIC_LINT_ACTION_NONE;

    if (lvt & CPU_LAPIC_LVT_MASK)
        return CPU_LAPIC_LINT_ACTION_HANDLED;

    switch (lvt & CPU_LAPIC_LVT_DELIV) {
        case CPU_LAPIC_LVT_DELIV_FIXED:
            if (vector != NULL)
                *vector = lvt & CPU_LAPIC_LVT_VECTOR;
            return CPU_LAPIC_LINT_ACTION_FIXED;

        case CPU_LAPIC_LVT_DELIV_SMI:
            return CPU_LAPIC_LINT_ACTION_SMI;

        case CPU_LAPIC_LVT_DELIV_NMI:
            return CPU_LAPIC_LINT_ACTION_NMI;

        default:
            return CPU_LAPIC_LINT_ACTION_HANDLED;
    }
}

int
cpu_lapic_timer_fire_index(uint8_t lapic_index, int started, uint8_t *vector)
{
    uint32_t lvt;
    uint8_t  timer_vector;

    if (vector != NULL)
        *vector = 0;

    if ((lapic_index >= IOAPIC_STATE_MAX_LAPICS) ||
        !cpu_lapic_enabled_index(lapic_index, started))
        return 0;

    lvt = cpu_lapic_lvt_timers[lapic_index];
    if (lvt & CPU_LAPIC_LVT_MASK)
        return 0;

    timer_vector = lvt & CPU_LAPIC_LVT_VECTOR;
    if (vector != NULL)
        *vector = timer_vector;

    if (!cpu_lapic_vector_valid(timer_vector))
        return -1;

    return cpu_lapic_accept_interrupt_index(lapic_index, timer_vector, 0);
}

void
cpu_lapic_reset_index(uint8_t lapic_index, uint8_t id, uint64_t base_msr)
{
    if (lapic_index >= IOAPIC_STATE_MAX_LAPICS)
        return;

    cpu_lapic_id_load_index(lapic_index, id);
    cpu_lapic_svr_load_index(lapic_index, CPU_LAPIC_SVR_DEFAULT);
    cpu_lapic_tpr_load_index(lapic_index, CPU_LAPIC_TPR_DEFAULT);
    cpu_lapic_ldr_load_index(lapic_index, CPU_LAPIC_LDR_DEFAULT);
    cpu_lapic_dfr_load_index(lapic_index, CPU_LAPIC_DFR_DEFAULT);
    cpu_lapic_version_load_index(lapic_index, cpu_lapic_version_for_model());
    cpu_lapic_arb_id_load_index(lapic_index, 0);
    cpu_lapic_ppr_load_index(lapic_index, 0);
    cpu_lapic_esr_load_index(lapic_index, 0);
    cpu_lapic_icr_lo_load_index(lapic_index, 0);
    cpu_lapic_icr_hi_load_index(lapic_index, 0);
    cpu_lapic_rrr_load_index(lapic_index, 0);
    cpu_lapic_lvt_timer_load_index(lapic_index, CPU_LAPIC_LVT_DEFAULT);
    cpu_lapic_lvt_thermal_load_index(lapic_index, CPU_LAPIC_LVT_DEFAULT);
    cpu_lapic_lvt_perf_load_index(lapic_index, CPU_LAPIC_LVT_DEFAULT);
    cpu_lapic_lvt_lint0_load_index(lapic_index, CPU_LAPIC_LVT_DEFAULT);
    cpu_lapic_lvt_lint1_load_index(lapic_index, CPU_LAPIC_LVT_DEFAULT);
    cpu_lapic_lvt_error_load_index(lapic_index, CPU_LAPIC_LVT_DEFAULT);
    cpu_lapic_timer_initial_write_index(lapic_index, 0);
    cpu_lapic_timer_divide_cfg_write_index(lapic_index, CPU_LAPIC_TIMER_DIVIDE_CFG_DEFAULT);
    cpu_apic_base_msr_load_index(lapic_index, base_msr);
}

static void
cpu_lapic_timer_initials_reset(void)
{
    for (uint8_t i = 0; i < IOAPIC_STATE_MAX_LAPICS; i++)
        cpu_lapic_timer_initials[i] = 0;
}

static void
cpu_lapic_timer_initials_sync(void)
{
    for (uint8_t i = 0; i < IOAPIC_STATE_MAX_LAPICS; i++)
        lapic_timer_initial_sync_index(i, cpu_lapic_timer_initials[i]);
}

uint32_t
cpu_lapic_timer_initial_read_index(uint8_t lapic_index)
{
    if (lapic_index >= IOAPIC_STATE_MAX_LAPICS)
        return 0;

    return cpu_lapic_timer_initials[lapic_index];
}

void
cpu_lapic_timer_initial_write_index(uint8_t lapic_index, uint32_t val)
{
    if (lapic_index >= IOAPIC_STATE_MAX_LAPICS)
        return;

    cpu_lapic_timer_initials[lapic_index] = val;
    lapic_timer_initial_sync_index(lapic_index, cpu_lapic_timer_initials[lapic_index]);
}

static void
cpu_lapic_timer_reload_counts_reset(void)
{
    for (uint8_t i = 0; i < IOAPIC_STATE_MAX_LAPICS; i++)
        cpu_lapic_timer_reload_counts[i] = 0;
}

uint32_t
cpu_lapic_timer_reload_read_index(uint8_t lapic_index)
{
    if (lapic_index >= IOAPIC_STATE_MAX_LAPICS)
        return 0;

    return cpu_lapic_timer_reload_counts[lapic_index];
}

static void
cpu_lapic_timer_start_tscs_reset(void)
{
    for (uint8_t i = 0; i < IOAPIC_STATE_MAX_LAPICS; i++)
        cpu_lapic_timer_start_tscs[i] = 0;
}

static void
cpu_lapic_timer_enableds_reset(void)
{
    for (uint8_t i = 0; i < IOAPIC_STATE_MAX_LAPICS; i++)
        cpu_lapic_timer_enableds[i] = 0;
}

int
cpu_lapic_timer_enabled_read_index(uint8_t lapic_index)
{
    if (lapic_index >= IOAPIC_STATE_MAX_LAPICS)
        return 0;

    return !!cpu_lapic_timer_enableds[lapic_index];
}

static void
cpu_lapic_timer_divide_cfgs_reset(void)
{
    for (uint8_t i = 0; i < IOAPIC_STATE_MAX_LAPICS; i++)
        cpu_lapic_timer_divide_cfgs[i] = CPU_LAPIC_TIMER_DIVIDE_CFG_DEFAULT;
}

static void
cpu_lapic_timer_divide_cfgs_sync(void)
{
    for (uint8_t i = 0; i < IOAPIC_STATE_MAX_LAPICS; i++)
        lapic_timer_divide_cfg_sync_index(i, cpu_lapic_timer_divide_cfgs[i]);
}

uint32_t
cpu_lapic_timer_divide_cfg_read_index(uint8_t lapic_index)
{
    if (lapic_index >= IOAPIC_STATE_MAX_LAPICS)
        return CPU_LAPIC_TIMER_DIVIDE_CFG_DEFAULT;

    return cpu_lapic_timer_divide_cfgs[lapic_index];
}

void
cpu_lapic_timer_divide_cfg_write_index(uint8_t lapic_index, uint32_t val)
{
    if (lapic_index >= IOAPIC_STATE_MAX_LAPICS)
        return;

    cpu_lapic_timer_divide_cfgs[lapic_index] = lapic_divide_cfg_sanitize(val);
    lapic_timer_divide_cfg_sync_index(lapic_index, cpu_lapic_timer_divide_cfgs[lapic_index]);
}

void
cpu_lapic_timer_unschedule_index(uint8_t lapic_index)
{
    if (lapic_index >= IOAPIC_STATE_MAX_LAPICS)
        return;

    cpu_lapic_timer_enableds[lapic_index] = 0;
}

/* Processor clocks (the emulator's timer unit) per count of the timer:
   "The time base is derived from the CPU bus clock, divided by the
   'divide' value" (Pentium Vol. 3, 19.3.4.1). */
static double
cpu_lapic_timer_clocks_per_count(uint8_t lapic_index)
{
    double ratio = ((cpu_busspeed > 0.0) && (cpuclock > cpu_busspeed)) ? (cpuclock / cpu_busspeed) : 1.0;

    return ratio * (double) cpu_lapic_timer_divisor_read_index(lapic_index);
}

/* The delay for count timer counts, in processor clocks, 32.32. */
uint64_t
cpu_lapic_timer_schedule_delay_index(uint8_t lapic_index, uint32_t count)
{
    double delay;

    if (lapic_index >= IOAPIC_STATE_MAX_LAPICS)
        return 0;

    cpu_lapic_timer_enableds[lapic_index] = 1;
    delay = (double) count * cpu_lapic_timer_clocks_per_count(lapic_index);
    if (delay > (double) CPU_LAPIC_TIMER_MAX_DELAY)
        return CPU_LAPIC_TIMER_MAX_DELAY << 32;
    /* Whole clocks, rounded up: the count reads zero when it fires. */
    return ((uint64_t) ceil(delay)) << 32;
}

void
cpu_lapic_timer_stop_index(uint8_t lapic_index)
{
    if (lapic_index >= IOAPIC_STATE_MAX_LAPICS)
        return;

    cpu_lapic_timer_reload_counts[lapic_index] = 0;
    cpu_lapic_timer_start_tscs[lapic_index] = 0;
    cpu_lapic_timer_initial_write_index(lapic_index, 0);
    cpu_lapic_timer_enableds[lapic_index] = 0;
}

void
cpu_lapic_timer_arm_index(uint8_t lapic_index, uint32_t count)
{
    if (lapic_index >= IOAPIC_STATE_MAX_LAPICS)
        return;

    cpu_lapic_timer_reload_counts[lapic_index] = count;
    cpu_lapic_timer_initial_write_index(lapic_index, count);
    cpu_lapic_timer_start_tscs[lapic_index] = count ? tsc : 0;
}

void
cpu_lapic_timer_rebase_current_index(uint8_t lapic_index, uint32_t current)
{
    /* Rebase keeps the host TSC anchor in CPU state while policy computes counters. */
    if (lapic_index >= IOAPIC_STATE_MAX_LAPICS)
        return;

    lapic_timer_rebase_state(cpu_lapic_timer_initials[lapic_index], current, tsc,
                             &cpu_lapic_timer_reload_counts[lapic_index],
                             &cpu_lapic_timer_start_tscs[lapic_index],
                             &cpu_lapic_timer_enableds[lapic_index]);
}

uint32_t
cpu_lapic_timer_periodic_restart_index(uint8_t lapic_index)
{
    uint64_t old_start;
    uint32_t old_count;
    uint32_t count;

    if (lapic_index >= IOAPIC_STATE_MAX_LAPICS)
        return 0;

    old_start = cpu_lapic_timer_start_tscs[lapic_index];
    old_count = cpu_lapic_timer_reload_counts[lapic_index];

    count = lapic_timer_periodic_restart_state(cpu_lapic_timer_initials[lapic_index], tsc,
                                               &cpu_lapic_timer_reload_counts[lapic_index],
                                               &cpu_lapic_timer_start_tscs[lapic_index]);
    /* The new period starts at the old one's end, not when the callback
       ran: the count reads, and the next deadline falls, where a free
       running timer's would. */
    if (count && old_count && old_start && (old_start <= tsc)) {
        const uint64_t end = old_start + (uint64_t) ceil((double) old_count * cpu_lapic_timer_clocks_per_count(lapic_index));

        if (end <= tsc)
            cpu_lapic_timer_start_tscs[lapic_index] = end;
    }
    return count;
}

void
cpu_lapic_timer_oneshot_complete_index(uint8_t lapic_index)
{
    if (lapic_index >= IOAPIC_STATE_MAX_LAPICS)
        return;

    cpu_lapic_timer_reload_counts[lapic_index] = 0;
    cpu_lapic_timer_start_tscs[lapic_index] = 0;
}

uint32_t
cpu_lapic_timer_divisor_read_index(uint8_t lapic_index)
{
    static const uint32_t divisor[8] = {
        2, 4, 8, 16, 32, 64, 128, 1
    };
    uint32_t cfg = cpu_lapic_timer_divide_cfg_read_index(lapic_index);
    uint8_t  idx = (cfg & 0x03) | ((cfg >> 1) & 0x04);

    return divisor[idx];
}

void
cpu_lapic_timer_host_add_index(uint8_t lapic_index, void (*callback)(void *priv), void *priv)
{
    if (lapic_index >= IOAPIC_STATE_MAX_LAPICS)
        return;

    timer_add(&cpu_lapic_timer_hosts[lapic_index], callback, priv, 0);
}

void
cpu_lapic_timer_host_stop_index(uint8_t lapic_index)
{
    if (lapic_index >= IOAPIC_STATE_MAX_LAPICS)
        return;

    timer_stop(&cpu_lapic_timer_hosts[lapic_index]);
    cpu_lapic_timer_unschedule_index(lapic_index);
}

void
cpu_lapic_timer_host_schedule_index(uint8_t lapic_index, uint32_t count, int in_callback)
{
    uint64_t delay;

    if (lapic_index >= IOAPIC_STATE_MAX_LAPICS)
        return;

    delay = cpu_lapic_timer_schedule_delay_index(lapic_index, count);

    if (in_callback)
        timer_advance_u64(&cpu_lapic_timer_hosts[lapic_index], delay);
    else
        timer_set_delay_u64(&cpu_lapic_timer_hosts[lapic_index], delay);
}

uint32_t
cpu_lapic_timer_current_read_index(uint8_t lapic_index)
{
    uint32_t count;

    if (lapic_index >= IOAPIC_STATE_MAX_LAPICS)
        return 0;

    count = cpu_lapic_timer_reload_counts[lapic_index];
    return lapic_timer_current_from_state(count, cpu_lapic_timer_start_tscs[lapic_index],
                                          tsc, cpu_lapic_timer_clocks_per_count(lapic_index));
}

uint32_t
cpu_lapic_timer_restore_index(uint8_t lapic_index, uint32_t initial, uint32_t current, int enabled)
{
    if (lapic_index >= IOAPIC_STATE_MAX_LAPICS)
        return 0;

    cpu_lapic_timer_initial_write_index(lapic_index, initial);

    return lapic_timer_restore_state(initial, current, enabled, tsc,
                                     &cpu_lapic_timer_reload_counts[lapic_index],
                                     &cpu_lapic_timer_start_tscs[lapic_index],
                                     &cpu_lapic_timer_enableds[lapic_index]);
}

static int
cpu_lapic_mmio_bitmap_read_index(uint8_t lapic_index, uint32_t reg, uint32_t *val)
{
    const uint32_t *words = NULL;
    uint32_t        base = 0;
    uint32_t        word;

    if ((lapic_index >= IOAPIC_STATE_MAX_LAPICS) || (val == NULL))
        return 0;

    if ((reg >= CPU_LAPIC_REG_ISR_BASE) && (reg < (CPU_LAPIC_REG_ISR_BASE + 0x80))) {
        words = cpu_lapic_isrs[lapic_index];
        base = CPU_LAPIC_REG_ISR_BASE;
    } else if ((reg >= CPU_LAPIC_REG_TMR_BASE) && (reg < (CPU_LAPIC_REG_TMR_BASE + 0x80))) {
        words = cpu_lapic_tmrs[lapic_index];
        base = CPU_LAPIC_REG_TMR_BASE;
    } else if ((reg >= CPU_LAPIC_REG_IRR_BASE) && (reg < (CPU_LAPIC_REG_IRR_BASE + 0x80))) {
        words = cpu_lapic_irrs[lapic_index];
        base = CPU_LAPIC_REG_IRR_BASE;
    } else {
        return 0;
    }

    word = (reg - base) >> 4;
    if (word >= IOAPIC_STATE_LAPIC_BIT_WORDS)
        return 0;

    *val = words[word];
    return 1;
}

int
cpu_lapic_mmio_read_index(uint8_t lapic_index, uint32_t reg, uint32_t *val)
{
    if ((lapic_index >= IOAPIC_STATE_MAX_LAPICS) || (val == NULL))
        return 0;

    if (cpu_lapic_mmio_bitmap_read_index(lapic_index, reg, val))
        return 1;

    switch (reg) {
        case CPU_LAPIC_REG_ID:
            *val = ((uint32_t) cpu_lapic_ids[lapic_index]) << 24;
            return 1;
        case CPU_LAPIC_REG_VERSION:
            *val = cpu_lapic_versions[lapic_index];
            return 1;
        case CPU_LAPIC_REG_APR:
            /* The architectural APR, worked out at the read (19.3.1.9.4). */
            *val = cpu_lapic_apr_value_index(lapic_index);
            return 1;
        case CPU_LAPIC_REG_PPR:
            /* Worked out from TPR and ISR at the read; the cached copy is
               for the mirror. */
            *val = cpu_lapic_ppr_value_index(lapic_index);
            return 1;
        case CPU_LAPIC_REG_RRR:
            *val = cpu_lapic_rrrs[lapic_index];
            return 1;
        case CPU_LAPIC_REG_SVR:
            *val = cpu_lapic_svrs[lapic_index];
            return 1;
        case CPU_LAPIC_REG_TPR:
            *val = cpu_lapic_tprs[lapic_index];
            return 1;
        case CPU_LAPIC_REG_LDR:
            *val = cpu_lapic_ldrs[lapic_index];
            return 1;
        case CPU_LAPIC_REG_DFR:
            *val = cpu_lapic_dfrs[lapic_index];
            return 1;
        case CPU_LAPIC_REG_LVT_TIMER:
            *val = cpu_lapic_lvt_timers[lapic_index];
            return 1;
        case CPU_LAPIC_REG_LVT_THERMAL:
            *val = cpu_lapic_lvt_thermals[lapic_index];
            return 1;
        case CPU_LAPIC_REG_LVT_PERF:
            *val = cpu_lapic_lvt_perfs[lapic_index];
            return 1;
        case CPU_LAPIC_REG_LVT_LINT0:
            *val = cpu_lapic_lvt_lint0s[lapic_index];
            return 1;
        case CPU_LAPIC_REG_LVT_LINT1:
            *val = cpu_lapic_lvt_lint1s[lapic_index];
            return 1;
        case CPU_LAPIC_REG_LVT_ERROR:
            *val = cpu_lapic_lvt_errors[lapic_index];
            return 1;
        case CPU_LAPIC_REG_ESR:
            *val = cpu_lapic_esrs[lapic_index];
            return 1;
        case CPU_LAPIC_REG_ICR_LO:
            *val = cpu_lapic_icr_los[lapic_index];
            return 1;
        case CPU_LAPIC_REG_ICR_HI:
            *val = cpu_lapic_icr_his[lapic_index];
            return 1;
        case CPU_LAPIC_REG_INITIAL_CNT:
            *val = cpu_lapic_timer_initial_read_index(lapic_index);
            return 1;
        case CPU_LAPIC_REG_CURRENT_CNT:
            *val = cpu_lapic_timer_current_read_index(lapic_index);
            return 1;
        case CPU_LAPIC_REG_DIVIDE_CFG:
            *val = cpu_lapic_timer_divide_cfg_read_index(lapic_index);
            return 1;
        default:
            return 0;
    }
}

int
cpu_lapic_mmio_write_index(uint8_t lapic_index, uint32_t reg, uint32_t val)
{
    if (lapic_index >= IOAPIC_STATE_MAX_LAPICS)
        return 0;

    switch (reg) {
        case CPU_LAPIC_REG_ID:
            cpu_lapic_id_write_index(lapic_index, val);
            return 1;
        case CPU_LAPIC_REG_ESR:
            /* The P6's takes the errors gathered since the last write
               (cpu_lapic_esr_or_index); the P54C's has no side effects
               (cpu_lapic_esr_read_clear_index). */
            if (!cpu_is_p5()) {
                cpu_lapic_esrs[lapic_index]       = cpu_lapic_esr_errors[lapic_index];
                cpu_lapic_esr_errors[lapic_index] = 0;
                lapic_esr_sync_index(lapic_index, cpu_lapic_esrs[lapic_index]);
            }
            return 1;
        case CPU_LAPIC_REG_SVR:
            cpu_lapic_svrs[lapic_index] = cpu_lapic_svr_sanitize(val);
            if (!(cpu_lapic_svrs[lapic_index] & CPU_LAPIC_SVR_ENABLE))
                cpu_lapic_lvt_mask_all_index(lapic_index);
            lapic_svr_sync_index(lapic_index, cpu_lapic_svrs[lapic_index]);
            return 1;
        case CPU_LAPIC_REG_TPR:
            cpu_lapic_tprs[lapic_index] = lapic_tpr_sanitize(val);
            lapic_tpr_sync_index(lapic_index, cpu_lapic_tprs[lapic_index]);
            cpu_lapic_ppr_update_index(lapic_index);
            return 1;
        case CPU_LAPIC_REG_LDR:
            cpu_lapic_ldrs[lapic_index] = lapic_ldr_sanitize(val);
            lapic_ldr_sync_index(lapic_index, cpu_lapic_ldrs[lapic_index]);
            return 1;
        case CPU_LAPIC_REG_DFR:
            cpu_lapic_dfrs[lapic_index] = lapic_dfr_sanitize(val);
            lapic_dfr_sync_index(lapic_index, cpu_lapic_dfrs[lapic_index]);
            return 1;
        case CPU_LAPIC_REG_ICR_HI:
            cpu_lapic_icr_his[lapic_index] = val & CPU_LAPIC_ICR_HI_DEST_MASK;
            lapic_icr_hi_sync_index(lapic_index, cpu_lapic_icr_his[lapic_index]);
            return 1;
        case CPU_LAPIC_REG_LVT_TIMER:
            cpu_lapic_lvt_write_index(cpu_lapic_lvt_timers, lapic_index, reg, val);
            lapic_lvt_timer_sync_index(lapic_index, cpu_lapic_lvt_timers[lapic_index]);
            return 1;
        case CPU_LAPIC_REG_LVT_THERMAL:
            cpu_lapic_lvt_write_index(cpu_lapic_lvt_thermals, lapic_index, reg, val);
            lapic_lvt_thermal_sync_index(lapic_index, cpu_lapic_lvt_thermals[lapic_index]);
            return 1;
        case CPU_LAPIC_REG_LVT_PERF:
            cpu_lapic_lvt_write_index(cpu_lapic_lvt_perfs, lapic_index, reg, val);
            lapic_lvt_perf_sync_index(lapic_index, cpu_lapic_lvt_perfs[lapic_index]);
            return 1;
        case CPU_LAPIC_REG_LVT_LINT0:
            cpu_lapic_lvt_write_index(cpu_lapic_lvt_lint0s, lapic_index, reg, val);
            lapic_lvt_lint0_sync_index(lapic_index, cpu_lapic_lvt_lint0s[lapic_index]);
            return 1;
        case CPU_LAPIC_REG_LVT_LINT1:
            cpu_lapic_lvt_write_index(cpu_lapic_lvt_lint1s, lapic_index, reg, val);
            lapic_lvt_lint1_sync_index(lapic_index, cpu_lapic_lvt_lint1s[lapic_index]);
            return 1;
        case CPU_LAPIC_REG_LVT_ERROR:
            cpu_lapic_lvt_write_index(cpu_lapic_lvt_errors, lapic_index, reg, val);
            lapic_lvt_error_sync_index(lapic_index, cpu_lapic_lvt_errors[lapic_index]);
            return 1;
        case CPU_LAPIC_REG_DIVIDE_CFG:
            cpu_lapic_timer_divide_cfg_write_index(lapic_index, val);
            return 1;
        default:
            return 0;
    }
}

int
cpu_lapic_mmio_write_action_index(uint8_t lapic_index, uint32_t reg,
                                  uint32_t val, uint32_t *mirror_val)
{
    /* LAPIC MMIO wrapper handles EOI/ICR side effects after this CPU-owned dispatch. */
    if (lapic_index >= IOAPIC_STATE_MAX_LAPICS)
        return CPU_LAPIC_MMIO_WRITE_UNHANDLED;

    if (reg == CPU_LAPIC_REG_EOI)
        return CPU_LAPIC_MMIO_WRITE_EOI;

    if (cpu_lapic_mmio_reg_read_only(reg))
        return CPU_LAPIC_MMIO_WRITE_READ_ONLY;

    if (reg == CPU_LAPIC_REG_ICR_LO)
        return CPU_LAPIC_MMIO_WRITE_ICR_LO;

    if (cpu_lapic_mmio_write_index(lapic_index, reg, val))
        return CPU_LAPIC_MMIO_WRITE_CPU_OWNED;

    if (mirror_val != NULL)
        *mirror_val = val;

    return CPU_LAPIC_MMIO_WRITE_MIRROR;
}

int
cpu_lapic_mmio_load_index(uint8_t lapic_index, uint32_t reg, uint32_t val)
{
    if (lapic_index >= IOAPIC_STATE_MAX_LAPICS)
        return 0;

    switch (reg) {
        case CPU_LAPIC_REG_SVR:
            cpu_lapic_svr_load_index(lapic_index, val);
            return 1;
        case CPU_LAPIC_REG_TPR:
            cpu_lapic_tpr_load_index(lapic_index, val);
            return 1;
        case CPU_LAPIC_REG_LDR:
            cpu_lapic_ldr_load_index(lapic_index, val);
            return 1;
        case CPU_LAPIC_REG_DFR:
            cpu_lapic_dfr_load_index(lapic_index, val);
            return 1;
        case CPU_LAPIC_REG_VERSION:
            cpu_lapic_version_load_index(lapic_index, val);
            return 1;
        case CPU_LAPIC_REG_APR:
            cpu_lapic_arb_id_load_index(lapic_index, val);
            return 1;
        case CPU_LAPIC_REG_PPR:
            cpu_lapic_ppr_load_index(lapic_index, val);
            return 1;
        case CPU_LAPIC_REG_ESR:
            cpu_lapic_esr_load_index(lapic_index, val);
            return 1;
        case CPU_LAPIC_REG_ICR_LO:
            cpu_lapic_icr_lo_load_index(lapic_index, val);
            return 1;
        case CPU_LAPIC_REG_ICR_HI:
            cpu_lapic_icr_hi_load_index(lapic_index, val);
            return 1;
        case CPU_LAPIC_REG_RRR:
            cpu_lapic_rrr_load_index(lapic_index, val);
            return 1;
        case CPU_LAPIC_REG_LVT_TIMER:
            cpu_lapic_lvt_timer_load_index(lapic_index, val);
            return 1;
        case CPU_LAPIC_REG_LVT_THERMAL:
            cpu_lapic_lvt_thermal_load_index(lapic_index, val);
            return 1;
        case CPU_LAPIC_REG_LVT_PERF:
            cpu_lapic_lvt_perf_load_index(lapic_index, val);
            return 1;
        case CPU_LAPIC_REG_LVT_LINT0:
            cpu_lapic_lvt_lint0_load_index(lapic_index, val);
            return 1;
        case CPU_LAPIC_REG_LVT_LINT1:
            cpu_lapic_lvt_lint1_load_index(lapic_index, val);
            return 1;
        case CPU_LAPIC_REG_LVT_ERROR:
            cpu_lapic_lvt_error_load_index(lapic_index, val);
            return 1;
        case CPU_LAPIC_REG_INITIAL_CNT:
            cpu_lapic_timer_initial_write_index(lapic_index, val);
            return 1;
        case CPU_LAPIC_REG_DIVIDE_CFG:
            cpu_lapic_timer_divide_cfg_write_index(lapic_index, val);
            return 1;
        default:
            return 0;
    }
}

void
cpu_lapic_mmio_sync_mirror_index(uint8_t lapic_index, uint32_t regs[])
{
    /* Sync only CPU-owned registers; LAPIC module owns the backing MMIO array. */
    static const uint32_t sync_regs[] = {
        CPU_LAPIC_REG_ID,        CPU_LAPIC_REG_VERSION,   CPU_LAPIC_REG_TPR,
        CPU_LAPIC_REG_APR,       CPU_LAPIC_REG_PPR,       CPU_LAPIC_REG_RRR,
        CPU_LAPIC_REG_LDR,
        CPU_LAPIC_REG_DFR,       CPU_LAPIC_REG_SVR,       CPU_LAPIC_REG_ESR,
        CPU_LAPIC_REG_ICR_LO,    CPU_LAPIC_REG_ICR_HI,    CPU_LAPIC_REG_LVT_TIMER,
        CPU_LAPIC_REG_LVT_THERMAL, CPU_LAPIC_REG_LVT_PERF, CPU_LAPIC_REG_LVT_LINT0,
        CPU_LAPIC_REG_LVT_LINT1, CPU_LAPIC_REG_LVT_ERROR,
        CPU_LAPIC_REG_INITIAL_CNT, CPU_LAPIC_REG_CURRENT_CNT, CPU_LAPIC_REG_DIVIDE_CFG
    };
    uint32_t val;

    if ((lapic_index >= IOAPIC_STATE_MAX_LAPICS) || (regs == NULL))
        return;

    for (uint8_t i = 0; i < (sizeof(sync_regs) / sizeof(sync_regs[0])); i++) {
        if (cpu_lapic_mmio_read_index(lapic_index, sync_regs[i], &val))
            regs[sync_regs[i] >> 4] = val;
    }
}

void
cpu_lapic_mmio_load_mirror_index(uint8_t lapic_index, const uint32_t regs[])
{
    static const uint32_t load_regs[] = {
        CPU_LAPIC_REG_VERSION,   CPU_LAPIC_REG_TPR,       CPU_LAPIC_REG_APR,
        CPU_LAPIC_REG_PPR,       CPU_LAPIC_REG_RRR,       CPU_LAPIC_REG_LDR,
        CPU_LAPIC_REG_DFR,
        CPU_LAPIC_REG_SVR,       CPU_LAPIC_REG_ESR,       CPU_LAPIC_REG_ICR_LO,
        CPU_LAPIC_REG_ICR_HI,    CPU_LAPIC_REG_LVT_TIMER, CPU_LAPIC_REG_LVT_THERMAL,
        CPU_LAPIC_REG_LVT_PERF,  CPU_LAPIC_REG_LVT_LINT0, CPU_LAPIC_REG_LVT_LINT1,
        CPU_LAPIC_REG_LVT_ERROR, CPU_LAPIC_REG_INITIAL_CNT,
        CPU_LAPIC_REG_DIVIDE_CFG
    };

    if ((lapic_index >= IOAPIC_STATE_MAX_LAPICS) || (regs == NULL))
        return;

    for (uint8_t i = 0; i < (sizeof(load_regs) / sizeof(load_regs[0])); i++)
        cpu_lapic_mmio_load_index(lapic_index, load_regs[i], regs[load_regs[i] >> 4]);
}

static uint64_t
cpu_apic_base_msr_reset_value(uint8_t lapic_index)
{
    return (lapic_index == 0) ? CPU_APIC_BASE_DEFAULT : (CPU_APIC_BASE_DEFAULT & ~CPU_APIC_BASE_BSP);
}

static void
cpu_apic_base_msrs_reset(void)
{
    for (uint8_t i = 0; i < IOAPIC_STATE_MAX_LAPICS; i++)
        cpu_apic_base_msrs[i] = cpu_apic_base_msr_reset_value(i);
}

static void
cpu_apic_base_msrs_sync(void)
{
    for (uint8_t i = 0; i < IOAPIC_STATE_MAX_LAPICS; i++)
        lapic_apic_base_msr_sync_index(i, cpu_apic_base_msrs[i]);
}

static uint64_t
cpu_apic_base_msr_sanitize(uint8_t lapic_index, uint64_t val, uint64_t cur)
{
    (void) lapic_index;

    /* Guest writes may move/enable the window; BSP ownership stays from reset/load. */
    return (val & CPU_APIC_BASE_ADDR_MASK) |
           (val & CPU_APIC_BASE_ENABLE) |
           (cur & CPU_APIC_BASE_BSP);
}

uint64_t
cpu_apic_base_msr_read_current(void)
{
    uint8_t lapic_index = cpu_current_lapic_index_clamped();

    return cpu_apic_base_msrs[lapic_index];
}

uint64_t
cpu_apic_base_msr_read_index(uint8_t lapic_index)
{
    if (lapic_index >= IOAPIC_STATE_MAX_LAPICS)
        return 0;

    return cpu_apic_base_msrs[lapic_index];
}

uint32_t
cpu_lapic_mmio_base_read_index(uint8_t lapic_index)
{
    return cpu_apic_base_msr_read_index(lapic_index) & CPU_APIC_BASE_ADDR_MASK;
}

int
cpu_lapic_mmio_mapping_enabled_index(uint8_t lapic_index)
{
    return !!(cpu_apic_base_msr_read_index(lapic_index) & CPU_APIC_BASE_ENABLE);
}

int
cpu_lapic_bsp_index(uint8_t lapic_index)
{
    return !!(cpu_apic_base_msr_read_index(lapic_index) & CPU_APIC_BASE_BSP);
}

void
cpu_apic_base_msr_write_current(uint64_t val)
{
    uint8_t lapic_index = cpu_current_lapic_index_clamped();
    uint64_t cur;

    cur = cpu_apic_base_msrs[lapic_index];
    cpu_apic_base_msrs[lapic_index] = cpu_apic_base_msr_sanitize(lapic_index, val, cur);
    lapic_apic_base_msr_sync_index(lapic_index, cpu_apic_base_msrs[lapic_index]);
}

void
cpu_apic_base_msr_load_index(uint8_t lapic_index, uint64_t val)
{
    if (lapic_index >= IOAPIC_STATE_MAX_LAPICS)
        return;

    cpu_apic_base_msrs[lapic_index] = (val & CPU_APIC_BASE_ADDR_MASK) |
                                      (val & (CPU_APIC_BASE_BSP | CPU_APIC_BASE_ENABLE));
}

/* The processor core's resets reach the local APICs through these. */
void
cpu_lapic_reset_all(void)
{
    cpu_lapic_ids_reset();
    cpu_lapic_svrs_reset();
    cpu_lapic_tprs_reset();
    cpu_lapic_ldrs_reset();
    cpu_lapic_dfrs_reset();
    cpu_lapic_versions_reset();
    cpu_lapic_arb_ids_reset();
    cpu_lapic_pprs_reset();
    cpu_lapic_esrs_reset();
    cpu_lapic_icrs_reset();
    cpu_lapic_rrrs_reset();
    cpu_lapic_lvt_timers_reset();
    cpu_lapic_lvt_thermals_reset();
    cpu_lapic_lvt_perfs_reset();
    cpu_lapic_lvt_lint0s_reset();
    cpu_lapic_lvt_lint1s_reset();
    cpu_lapic_lvt_errors_reset();
    cpu_lapic_irrs_reset();
    cpu_lapic_isrs_reset();
    cpu_lapic_tmrs_reset();
    cpu_lapic_timer_initials_reset();
    cpu_lapic_timer_reload_counts_reset();
    cpu_lapic_timer_start_tscs_reset();
    cpu_lapic_timer_enableds_reset();
    cpu_lapic_timer_divide_cfgs_reset();
    cpu_apic_base_msrs_reset();
}

void
cpu_lapic_ven_reset(void)
{
    cpu_lapic_ids_reset();
    cpu_lapic_ids_sync();
    cpu_lapic_svrs_reset();
    cpu_lapic_svrs_sync();
    cpu_lapic_tprs_reset();
    cpu_lapic_tprs_sync();
    cpu_lapic_ldrs_reset();
    cpu_lapic_ldrs_sync();
    cpu_lapic_dfrs_reset();
    cpu_lapic_dfrs_sync();
    cpu_lapic_versions_reset();
    cpu_lapic_versions_sync();
    cpu_lapic_arb_ids_reset();
    cpu_lapic_arb_ids_sync();
    cpu_lapic_pprs_reset();
    cpu_lapic_pprs_sync();
    cpu_lapic_esrs_reset();
    cpu_lapic_esrs_sync();
    cpu_lapic_icrs_reset();
    cpu_lapic_icrs_sync();
    cpu_lapic_rrrs_reset();
    cpu_lapic_rrrs_sync();
    cpu_lapic_lvt_timers_reset();
    cpu_lapic_lvt_timers_sync();
    cpu_lapic_lvt_thermals_reset();
    cpu_lapic_lvt_thermals_sync();
    cpu_lapic_lvt_perfs_reset();
    cpu_lapic_lvt_perfs_sync();
    cpu_lapic_lvt_lint0s_reset();
    cpu_lapic_lvt_lint0s_sync();
    cpu_lapic_lvt_lint1s_reset();
    cpu_lapic_lvt_lint1s_sync();
    cpu_lapic_lvt_errors_reset();
    cpu_lapic_lvt_errors_sync();
    cpu_lapic_irrs_reset();
    cpu_lapic_isrs_reset();
    cpu_lapic_tmrs_reset();
    cpu_lapic_timer_initials_reset();
    cpu_lapic_timer_initials_sync();
    cpu_lapic_timer_reload_counts_reset();
    cpu_lapic_timer_start_tscs_reset();
    cpu_lapic_timer_enableds_reset();
    cpu_lapic_timer_divide_cfgs_reset();
    cpu_lapic_timer_divide_cfgs_sync();
    cpu_apic_base_msrs_reset();
    cpu_apic_base_msrs_sync();
}
