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
#include <stdint.h>
#include <string.h>

#include <86box/device.h>
#include <86box/chipset.h>

#include "cpu.h"
#include "lapic.h"
#include "lapic_internal.h"
#include "lapic_policy.h"

typedef struct local_apic_state_save_adapter_t {
    struct ioapic_state_t *state;
} local_apic_state_save_adapter_t;

typedef struct local_apic_state_load_adapter_t {
    const struct ioapic_state_t *state;
    uint8_t                     count;
    lapic_reset_id_source_t     id_source;
    void                       *id_priv;
} local_apic_state_load_adapter_t;

static void lapic_state_save_slot(lapic_state_t *lapic,
                                  struct ioapic_state_t *state,
                                  uint8_t index);
static void lapic_state_load_slot(lapic_state_t *lapic,
                                  lapic_mmio_device_t *mmio,
                                  const struct ioapic_state_t *state,
                                  uint8_t index);

static uint64_t
lapic_state_base_msr_sanitize(uint8_t index, uint64_t base_msr)
{
    /* Slot index owns BSP role; savefile MSR keeps only base address/enable. */
    uint64_t sanitized = (base_msr & APIC_BASE_ADDR_MASK) |
                         (base_msr & APIC_BASE_ENABLE);

    if (index == 0)
        sanitized |= APIC_BASE_BSP;

    return sanitized;
}

static void
lapic_state_sanitize_loaded_regs(uint32_t regs[0x400 >> 4])
{
    regs[LAPIC_REG_TPR >> 4] = lapic_tpr_sanitize(regs[LAPIC_REG_TPR >> 4]);
    regs[LAPIC_REG_LDR >> 4] = lapic_ldr_sanitize(regs[LAPIC_REG_LDR >> 4]);
    regs[LAPIC_REG_DFR >> 4] = lapic_dfr_sanitize(regs[LAPIC_REG_DFR >> 4]);
    regs[LAPIC_REG_SVR >> 4] = lapic_svr_sanitize(regs[LAPIC_REG_SVR >> 4]);
    regs[LAPIC_REG_ESR >> 4] = lapic_esr_sanitize(regs[LAPIC_REG_ESR >> 4]);
    /* EOI has no persistent model state to replay from a savefile. */
    regs[LAPIC_REG_EOI >> 4] = 0;
    regs[LAPIC_REG_ICR_LO >> 4] =
        lapic_icr_lo_load_sanitize(regs[LAPIC_REG_ICR_LO >> 4]);
    regs[LAPIC_REG_ICR_HI >> 4] =
        lapic_icr_hi_sanitize(regs[LAPIC_REG_ICR_HI >> 4]);
    regs[LAPIC_REG_LVT_TIMER >> 4] =
        lapic_lvt_sanitize(LAPIC_REG_LVT_TIMER, regs[LAPIC_REG_LVT_TIMER >> 4]);
    regs[LAPIC_REG_LVT_THERMAL >> 4] =
        lapic_lvt_sanitize(LAPIC_REG_LVT_THERMAL, regs[LAPIC_REG_LVT_THERMAL >> 4]);
    regs[LAPIC_REG_LVT_PERF >> 4] =
        lapic_lvt_sanitize(LAPIC_REG_LVT_PERF, regs[LAPIC_REG_LVT_PERF >> 4]);
    /* Delivery status (bit 12) and Remote IRR (bit 14) are state, kept. */
    regs[LAPIC_REG_LVT_LINT0 >> 4] =
        lapic_lvt_sanitize(LAPIC_REG_LVT_LINT0, regs[LAPIC_REG_LVT_LINT0 >> 4]) |
        (regs[LAPIC_REG_LVT_LINT0 >> 4] & 0x00005000);
    regs[LAPIC_REG_LVT_LINT1 >> 4] =
        lapic_lvt_sanitize(LAPIC_REG_LVT_LINT1, regs[LAPIC_REG_LVT_LINT1 >> 4]) |
        (regs[LAPIC_REG_LVT_LINT1 >> 4] & 0x00005000);
    regs[LAPIC_REG_LVT_ERROR >> 4] =
        lapic_lvt_sanitize(LAPIC_REG_LVT_ERROR, regs[LAPIC_REG_LVT_ERROR >> 4]);
    regs[LAPIC_REG_DIVIDE_CFG >> 4] =
        lapic_divide_cfg_sanitize(regs[LAPIC_REG_DIVIDE_CFG >> 4]);
}

static void
lapic_state_sanitize_loaded_interrupt_state(
    uint32_t irr[IOAPIC_STATE_LAPIC_BIT_WORDS],
    uint32_t isr[IOAPIC_STATE_LAPIC_BIT_WORDS],
    uint32_t tmr[IOAPIC_STATE_LAPIC_BIT_WORDS])
{
    /* 86Box LAPIC state never restores vectors below 0x10 from savefiles. */
    irr[0] &= 0xffff0000u;
    isr[0] &= 0xffff0000u;
    tmr[0] &= 0xffff0000u;

    /* TMR bits without a queued/in-service vector are detached state; drop them. */
    for (uint8_t i = 0; i < IOAPIC_STATE_LAPIC_BIT_WORDS; i++)
        tmr[i] &= irr[i] | isr[i];
}

static int
local_apic_state_save_adapter(local_apic_t *apic,
                              const local_apic_state_save_adapter_t *adapter)
{
    return ((adapter != NULL) && (adapter->state != NULL)) ?
           lapic_device_state_save((apic != NULL) ? &apic->device : NULL,
                                   adapter->state) : 0;
}

static void
local_apic_state_save_adapter_init(local_apic_state_save_adapter_t *adapter,
                                   struct ioapic_state_t *state)
{
    if (adapter == NULL)
        return;

    adapter->state = state;
}

int
local_apic_state_save(local_apic_t *apic, struct ioapic_state_t *state)
{
    local_apic_state_save_adapter_t adapter;

    local_apic_state_save_adapter_init(&adapter, state);
    return local_apic_state_save_adapter(apic, &adapter);
}

static void
local_apic_state_load_adapter_init(local_apic_state_load_adapter_t *adapter,
                                   const struct ioapic_state_t *state,
                                   uint8_t count_limit,
                                   lapic_reset_id_source_t id_source,
                                   void *id_priv)
{
    uint8_t loaded_count;

    if (adapter == NULL)
        return;

    memset(adapter, 0, sizeof(*adapter));
    if (state == NULL)
        return;

    /* Pre-v2 saves lack saved LAPIC counts; load them as the old UP image. */
    loaded_count = (state->version >= 2) ? state->lapic_count :
                                          APIC_UP_LAPIC_COUNT;

    adapter->state     = state;
    adapter->count     = loaded_count;
    adapter->id_source = id_source;
    adapter->id_priv   = id_priv;

    /* Caller owns topology cap; zero preserves legacy local_apic_* UP behavior. */
    if (!count_limit)
        count_limit = APIC_UP_LAPIC_COUNT;
    if (!adapter->count || (adapter->count > count_limit))
        adapter->count = count_limit;
}

static int
local_apic_state_load_adapter(local_apic_t *apic,
                              const local_apic_state_load_adapter_t *adapter)
{
    return ((adapter != NULL) && (adapter->state != NULL)) ?
           lapic_device_state_load((apic != NULL) ? &apic->device : NULL,
                                   adapter->state,
                                   adapter->count, adapter->id_source,
                                   adapter->id_priv) : 0;
}

int
local_apic_state_load(local_apic_t *apic, const struct ioapic_state_t *state,
                      uint8_t count_limit, lapic_reset_id_source_t id_source,
                      void *id_priv)
{
    local_apic_state_load_adapter_t adapter;

    local_apic_state_load_adapter_init(&adapter, state, count_limit,
                                       id_source, id_priv);
    return local_apic_state_load_adapter(apic, &adapter);
}

static void
lapic_state_save_up(lapic_state_t *lapic, struct ioapic_state_t *state)
{
    state->lapic_id = lapic->id;
    state->lapic_extint_pending = lapic->extint_pending;
    state->lapic_extint_pin = lapic->extint_pin;
    state->lapic_timer_enabled = cpu_lapic_timer_enabled_read_index(lapic->index);
    state->lapic_base_msr = lapic->base_msr;
    /* regs[] is a mirror; pull CPU-owned regs before snapshot. */
    lapic_sync_cpu_owned_regs(lapic);
    memcpy(state->lapic_regs, lapic->regs, sizeof(state->lapic_regs));
    /* The APR slot carries the Arb ID, which software never reads: the
       register read gives the architectural APR (cpu_lapic_apr_value_index). */
    state->lapic_regs[LAPIC_REG_APR >> 4] = ((uint32_t) lapic->arb_id) << 24;
    cpu_lapic_irr_save_index(lapic->index, state->lapic_irr);
    cpu_lapic_isr_save_index(lapic->index, state->lapic_isr);
    cpu_lapic_tmr_save_index(lapic->index, state->lapic_tmr);
    state->lapic_timer_initial = cpu_lapic_timer_initial_read_index(lapic->index);
    state->lapic_timer_current = lapic_timer_current(lapic);
}

int
lapic_device_state_save(lapic_device_t *dev, struct ioapic_state_t *state)
{
    lapic_state_t *lapic;

    if ((dev == NULL) || (state == NULL))
        return 0;

    lapic = lapic_device_bsp(dev);
    if (lapic == NULL)
        return 0;

    lapic_sync_int_regs(lapic);
    state->lapic_count = dev->count;
    /* Keep legacy UP fields populated alongside per-slot arrays. */
    lapic_state_save_up(lapic, state);

    /* Persist every allocated slot; load path clamps active topology later. */
    for (uint8_t i = 0; i < APIC_MAX_LAPICS; i++) {
        lapic_sync_int_regs(&dev->lapics[i]);
        lapic_state_save_slot(&dev->lapics[i], state, i);
    }

    return 1;
}

static void
lapic_state_load_up(lapic_state_t *lapic, lapic_mmio_device_t *mmio,
                    const struct ioapic_state_t *state)
{
    lapic->index = 0;
    lapic->mmio = mmio;
    lapic->id = state->lapic_id;
    /* Pre-v10 saves derive arb_id from id, not saved APR. */
    lapic->arb_id = (state->version >= 10) ?
                    ((state->lapic_regs[LAPIC_REG_APR >> 4] >> 24) & 0x0f) :
                    (lapic->id & 0x0f);
    lapic->base_msr = lapic_state_base_msr_sanitize(0, state->lapic_base_msr);
    cpu_lapic_id_load_index(0, lapic->id);
    cpu_apic_base_msr_load_index(0, lapic->base_msr);
    lapic->extint_pending = state->lapic_extint_pending;
    lapic->extint_pin = state->lapic_extint_pin;
    cpu_lapic_extint_live_set(0, lapic->extint_pending);
    memcpy(lapic->regs, state->lapic_regs, sizeof(lapic->regs));
    /* Sanitize savefile mirror before pushing it into CPU-owned storage. */
    lapic_state_sanitize_loaded_regs(lapic->regs);
    lapic_load_cpu_owned_regs(lapic);
    cpu_lapic_arb_id_load_index(lapic->index, ((uint32_t) lapic->arb_id) << 24);
    {
        uint32_t irr[IOAPIC_STATE_LAPIC_BIT_WORDS];
        uint32_t isr[IOAPIC_STATE_LAPIC_BIT_WORDS];
        uint32_t tmr[IOAPIC_STATE_LAPIC_BIT_WORDS];

        memcpy(irr, state->lapic_irr, sizeof(irr));
        memcpy(isr, state->lapic_isr, sizeof(isr));
        memcpy(tmr, state->lapic_tmr, sizeof(tmr));
        lapic_state_sanitize_loaded_interrupt_state(irr, isr, tmr);
        cpu_lapic_irr_load_index(0, irr);
        cpu_lapic_isr_load_index(0, isr);
        cpu_lapic_tmr_load_index(0, tmr);
    }
    lapic_sync_cpu_owned_regs(lapic);
}

static void
lapic_state_save_slot(lapic_state_t *lapic, struct ioapic_state_t *state, uint8_t index)
{
    state->lapic_ids[index] = lapic->id;
    state->lapic_extint_pending_arr[index] = lapic->extint_pending;
    state->lapic_extint_pin_arr[index] = lapic->extint_pin;
    state->lapic_timer_enabled_arr[index] = cpu_lapic_timer_enabled_read_index(index);
    state->lapic_started[index] = lapic->started;
    state->lapic_init_asserted[index] = lapic->init_asserted;
    state->lapic_wait_for_sipi[index] = lapic->wait_for_sipi;
    state->lapic_sipi_vector[index] = lapic->sipi_vector;
    state->lapic_parked[index] = lapic->parked;
    state->lapic_base_msrs[index] = lapic->base_msr;
    lapic_sync_cpu_owned_regs(lapic);
    memcpy(state->lapic_regs_arr[index], lapic->regs, sizeof(lapic->regs));
    state->lapic_regs_arr[index][LAPIC_REG_APR >> 4] = ((uint32_t) lapic->arb_id) << 24;
    cpu_lapic_irr_save_index(index, state->lapic_irr_arr[index]);
    cpu_lapic_isr_save_index(index, state->lapic_isr_arr[index]);
    cpu_lapic_tmr_save_index(index, state->lapic_tmr_arr[index]);
    state->lapic_timer_initial_arr[index] = cpu_lapic_timer_initial_read_index(index);
    state->lapic_timer_current_arr[index] = lapic_timer_current(lapic);
}

static void
lapic_state_load_slot(lapic_state_t *lapic, lapic_mmio_device_t *mmio,
                      const struct ioapic_state_t *state, uint8_t index)
{
    lapic->index = index;
    lapic->mmio = mmio;
    lapic->id = state->lapic_ids[index];
    /* Pre-v10 saves derive arb_id from id, not saved APR. */
    lapic->arb_id = (state->version >= 10) ?
                    ((state->lapic_regs_arr[index][LAPIC_REG_APR >> 4] >> 24) & 0x0f) :
                    (lapic->id & 0x0f);
    lapic->base_msr = lapic_state_base_msr_sanitize(index, state->lapic_base_msrs[index]);
    cpu_lapic_id_load_index(index, lapic->id);
    cpu_apic_base_msr_load_index(index, lapic->base_msr);
    lapic->extint_pending = state->lapic_extint_pending_arr[index];
    lapic->extint_pin = state->lapic_extint_pin_arr[index];
    cpu_lapic_extint_live_set(index, lapic->extint_pending);
    lapic->is_bsp = (index == 0);
    lapic->started = state->lapic_started[index];
    lapic->init_asserted = state->lapic_init_asserted[index];
    lapic->wait_for_sipi = state->lapic_wait_for_sipi[index];
    lapic->sipi_vector = state->lapic_sipi_vector[index];
    /* Pre-v7 load path defines parked as zero for compat. */
    lapic->parked = (state->version >= 7) ? state->lapic_parked[index] : 0;
    memcpy(lapic->regs, state->lapic_regs_arr[index], sizeof(lapic->regs));
    /* Sanitize savefile mirror before pushing it into CPU-owned storage. */
    lapic_state_sanitize_loaded_regs(lapic->regs);
    lapic_load_cpu_owned_regs(lapic);
    cpu_lapic_arb_id_load_index(lapic->index, ((uint32_t) lapic->arb_id) << 24);
    {
        uint32_t irr[IOAPIC_STATE_LAPIC_BIT_WORDS];
        uint32_t isr[IOAPIC_STATE_LAPIC_BIT_WORDS];
        uint32_t tmr[IOAPIC_STATE_LAPIC_BIT_WORDS];

        memcpy(irr, state->lapic_irr_arr[index], sizeof(irr));
        memcpy(isr, state->lapic_isr_arr[index], sizeof(isr));
        memcpy(tmr, state->lapic_tmr_arr[index], sizeof(tmr));
        lapic_state_sanitize_loaded_interrupt_state(irr, isr, tmr);
        cpu_lapic_irr_load_index(index, irr);
        cpu_lapic_isr_load_index(index, isr);
        cpu_lapic_tmr_load_index(index, tmr);
    }
    lapic_timer_restore_target(lapic, state->lapic_timer_initial_arr[index],
                               state->lapic_timer_current_arr[index],
                               state->lapic_timer_enabled_arr[index]);
    lapic_sync_int_regs(lapic);
    lapic_sync_cpu_owned_regs(lapic);
}

int
lapic_device_state_load(lapic_device_t *dev, const struct ioapic_state_t *state,
                        uint8_t count, lapic_reset_id_source_t id_source, void *id_priv)
{
    lapic_state_t *lapic;

    if ((dev == NULL) || (state == NULL))
        return 0;

    lapic_device_set_count(dev, count);
    lapic = lapic_device_bsp(dev);
    if (lapic == NULL)
        return 0;

    if (state->version >= 3) {
        /* v3+ savefiles carry per-slot LAPIC arrays. */
        for (uint8_t i = 0; i < APIC_MAX_LAPICS; i++) {
            lapic_state_load_slot(&dev->lapics[i], &dev->mmio, state, i);
            /* Inactive loaded slots drop armed timers after state restore. */
            if (i >= dev->count)
                lapic_timer_stop(&dev->lapics[i]);
        }
    } else {
        /* Older savefiles carry only BSP/UP state; rebuild AP slots locally. */
        lapic_state_load_up(lapic, &dev->mmio, state);
        lapic_reset_startup_state(lapic, 0);

        for (uint8_t i = 1; i < APIC_MAX_LAPICS; i++) {
            uint8_t id = (id_source != NULL) ? id_source(id_priv, i) : i;
            lapic_device_reset_slot(dev, i, id);
        }

        lapic_timer_restore_target(lapic_device_current(dev), state->lapic_timer_initial,
                                   state->lapic_timer_current, state->lapic_timer_enabled);
    }

    lapic_sync_int_regs(lapic);
    return 1;
}
