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
#include <stdint.h>
#include <string.h>

#include "cpu.h"
#include "lapic.h"
#include "lapic_internal.h"
#include "lapic_policy.h"

/* Model-only tie cursor for lowest-priority ICR delivery, not LAPIC register state. */
static uint8_t lapic_icr_lowest_priority_next;

#define LAPIC_PHYS_BROADCAST_4BIT 0x01
#define LAPIC_PHYS_BROADCAST_8BIT 0x02

static void lapic_arbitration_id_set(lapic_state_t *lapic, uint8_t arb_id);
static lapic_state_t *lapic_choose_focus_processor_target(
                                const lapic_target_info_t *targets,
                                uint8_t vector, int route_parked);

static apic_bus_result_t
lapic_accept_fixed_interrupt(lapic_state_t *lapic,
                             const apic_bus_message_t *message,
                             uint32_t invalid_error)
{
    uint8_t error_vector;
    int     action;

    if ((lapic == NULL) || (message == NULL))
        return apic_bus_result_no_target();

    action = cpu_lapic_fixed_delivery_action_index(lapic->index, lapic->started,
                                                   message->vector, invalid_error,
                                                   &error_vector);
    /* The Receive Accept Error is the bus's verdict, not this APIC's: its
       callers decide (lapic_targets_accept_fixed). */
    if (action == CPU_LAPIC_FIXED_REJECT)
        return apic_bus_result_rejected_recv_accept();

    if (action == CPU_LAPIC_FIXED_ERROR) {
        apic_bus_message_t error_message = *message;

        /*
         * CPU LAPIC code supplies the error-vector candidate. Retry through the
         * fixed path with invalid_error clear so bad original vectors cannot
         * recurse into another error-vector injection.
         */
        error_message.vector = error_vector;
        error_message.level = 0;
        lapic_accept_fixed_interrupt(lapic, &error_message, 0);
        lapic_log(lapic->mmio, "APIC-WARN: LAPIC rejected invalid vector=%02x\n",
                  message->vector);
        return apic_bus_result_invalid_recv_vector();
    }

    cpu_lapic_accept_interrupt_index(lapic->index, message->vector, message->level);

    /* Register reads come from the CPU-owned state (cpu_lapic_mmio_read_index);
       the mirror is brought up to date only for save states. */
    lapic_sync_ppr(lapic);
    if (lapic_log_first(&lapic->accept_log_count[message->vector], 8))
        lapic_log(lapic->mmio, "LAPIC: accepted vector=%02x trigger=%s count=%u\n",
                  message->vector, message->level ? "level" : "edge",
                  lapic->accept_log_count[message->vector]);

    return apic_bus_result(APIC_BUS_ACCEPTED);
}

/* A message on the bus to this APIC alone: refused, nobody took it. */
apic_bus_result_t
lapic_accept_interrupt_target(lapic_state_t *lapic,
                              const apic_bus_message_t *message)
{
    apic_bus_result_t result = lapic_accept_fixed_interrupt(lapic, message,
                                                            LAPIC_ESR_RECV_ILLEGAL_VECTOR);

    if (result.target_esr == LAPIC_ESR_RECV_ACCEPT)
        lapic_error(lapic, LAPIC_ESR_RECV_ACCEPT);
    return result;
}

int
lapic_targets_deliver_init(const lapic_target_info_t *targets)
{
    int delivered = 0;

    if (targets == NULL)
        return 0;

    for (uint8_t i = 0; i < targets->count; i++) {
        lapic_state_t *target = targets->lapics[i];

        if (target == NULL)
            continue;

        lapic_init_signal(target);
        delivered = 1;
    }

    return delivered;
}

int
lapic_lint0_extint_enabled(const lapic_state_t *lapic)
{
    uint32_t lvt;

    if (lapic == NULL)
        return 0;

    lvt = cpu_lapic_lvt_lint0_read_index(lapic->index);

    return lapic_enabled(lapic) &&
           !(lvt & LAPIC_LVT_MASK) &&
           ((lvt & LAPIC_LVT_DELIV) == LAPIC_LVT_DELIV_EXTINT) &&
           cpu_lapic_extint_deliverable_index(lapic->index, lapic->started, 1);
}

int
lapic_interrupt_pending(const lapic_state_t *lapic)
{
    if (lapic == NULL)
        return 0;

    return (lapic_highest_deliverable_irr(lapic) >= 0) ||
           lapic_extint_deliverable(lapic);
}

int
lapic_highest_deliverable_irr(const lapic_state_t *lapic)
{
    return cpu_lapic_highest_deliverable_irr_index(lapic->index, lapic->started);
}

int
lapic_promote_irr(lapic_state_t *lapic)
{
    int vector;

    if (lapic == NULL)
        return -1;

    vector = lapic_highest_deliverable_irr(lapic);
    if (vector < 0)
        return -1;

    cpu_lapic_promote_irr_index(lapic->index, vector);
    /* The vector is no longer pending: one refused because it was goes
       again, into the IRR behind this one (lapic_slot_freed). */
    lapic_slot_freed();

    if (lapic_log_first(&lapic->promote_log_count[vector], 8))
        lapic_log(lapic->mmio, "LAPIC: vector=%02x promoted IRR->ISR count=%u\n",
                  vector, lapic->promote_log_count[vector]);

    return vector;
}

int
lapic_extint_deliverable(const lapic_state_t *lapic)
{
    return cpu_lapic_extint_deliverable_index(lapic->index, lapic->started,
                                              lapic->extint_pending);
}

void
lapic_mark_extint_pending(lapic_state_t *lapic, uint8_t pin)
{
    if (lapic == NULL)
        return;

    lapic->extint_pending = 1;
    lapic->extint_pin = pin;
    cpu_lapic_extint_live_set(lapic->index, 1);
}

int
lapic_consume_extint(lapic_state_t *lapic, uint8_t *pin)
{
    if ((lapic == NULL) || !lapic_extint_deliverable(lapic))
        return 0;

    if (pin != NULL)
        *pin = lapic->extint_pin;
    /* Keep the pin visible until the LAPIC path actually accepts EXTINT. */
    lapic->extint_pending = 0;
    cpu_lapic_extint_live_set(lapic->index, 0);

    return 1;
}

void
lapic_error(lapic_state_t *lapic, uint32_t error)
{
    uint8_t vector;

    if (lapic == NULL)
        return;

    if (cpu_lapic_error_delivery_action_index(lapic->index, lapic->started,
                                              error, &vector) == CPU_LAPIC_FIXED_ACCEPT) {
        apic_bus_message_t message = {
            .source   = APIC_BUS_AGENT_LAPIC,
            .delivery = APIC_BUS_DELIVERY_FIXED,
            .vector   = vector
        };

        /* Reuse fixed delivery so error-vector acceptance obeys normal CPU gates. */
        lapic_accept_fixed_interrupt(lapic, &message, 0);
    }
}

static int
lapic_lint_init_deliver(lapic_state_t *lapic, uint32_t lint_reg)
{
    if ((lapic == NULL) || (lint_reg != LAPIC_REG_LVT_LINT1))
        return 1;

    /*
       LINT INIT has CPU reset/startup semantics. Keep the current UP APIC path
       behavior-preserving and leave the real action to the later AP startup
       ownership work.
     */
    if (lapic_log_first(&lapic->lint_init_log_count, 4))
        lapic_log(lapic->mmio, "LAPIC: LINT INIT deferred id=%02x lint=%03x count=%u\n",
                  lapic_current_id(lapic), lint_reg, lapic->lint_init_log_count);

    return 1;
}

int
lapic_lint_deliver(lapic_state_t *lapic, uint32_t lint_reg)
{
    uint8_t vector;
    uint8_t lint;
    int     level;
    int     action;

    if (lapic == NULL)
        return 0;

    lint   = (lint_reg == LAPIC_REG_LVT_LINT1) ? CPU_LAPIC_LINT1 : CPU_LAPIC_LINT0;
    action = cpu_lapic_lint_delivery_index(lapic->index, lapic->started, lint, &vector, &level);

    switch (action) {
        case CPU_LAPIC_LINT_ACTION_NONE:
            return 0;

        case CPU_LAPIC_LINT_ACTION_HANDLED:
            return 1;

        case CPU_LAPIC_LINT_ACTION_FIXED:
        {
            apic_bus_message_t message = {
                .source   = APIC_BUS_AGENT_LAPIC,
                .delivery = APIC_BUS_DELIVERY_FIXED,
                .vector   = vector,
                .level    = !!level
            };
            apic_bus_result_t result =
                lapic_accept_fixed_interrupt(lapic, &message, LAPIC_ESR_RECV_ILLEGAL_VECTOR);

            /* Refused (an illegal vector, reported in the ESR), it is
               dropped: nothing sends it again, so it is not left pending. */
            cpu_lapic_lvt_delivery_complete_index(lapic->index, lint_reg);
            if (apic_bus_result_accepted(result) && level)
                cpu_lapic_lint_remote_irr_set_index(lapic->index, lint_reg, vector);
            return 1;
        }

        case CPU_LAPIC_LINT_ACTION_SMI:
            cpu_smi_raise_direct();
            return 1;

        case CPU_LAPIC_LINT_ACTION_NMI:
            /* LINT1 is the board's NMI line, which port 70h bit 7 gates
               before it reaches the pin (in the model at the processor,
               NMI_TAKEABLE); LINT0 carries INTR, which port 70h does not
               gate. */
            if (lint_reg == LAPIC_REG_LVT_LINT1)
                cpu_nmi_raise_core(0);
            else
                cpu_nmi_raise_core(1);
            return 1;

        case CPU_LAPIC_LINT_ACTION_INIT:
            return lapic_lint_init_deliver(lapic, lint_reg);

        case CPU_LAPIC_LINT_ACTION_EXTINT:
            return (lint_reg == LAPIC_REG_LVT_LINT0) ? 0 : 1;

        default:
            return 1;
    }
}

int
lapic_lvt_source_deliver(lapic_state_t *lapic, uint32_t lvt_reg)
{
    uint8_t vector;
    uint8_t source;
    int     action;

    if (lapic == NULL)
        return 0;

    if (lvt_reg == LAPIC_REG_LVT_THERMAL)
        source = CPU_LAPIC_LVT_SOURCE_THERMAL;
    else if (lvt_reg == LAPIC_REG_LVT_PERF)
        source = CPU_LAPIC_LVT_SOURCE_PERF;
    else
        return 0;

    action = cpu_lapic_lvt_source_delivery_index(lapic->index, lapic->started,
                                                 source, &vector);
    switch (action) {
        case CPU_LAPIC_LINT_ACTION_NONE:
            return 0;

        case CPU_LAPIC_LINT_ACTION_HANDLED:
            return 1;

        case CPU_LAPIC_LINT_ACTION_FIXED:
        {
            apic_bus_message_t message = {
                .source   = APIC_BUS_AGENT_LAPIC,
                .delivery = APIC_BUS_DELIVERY_FIXED,
                .vector   = vector
            };
            /* Refused, it is dropped, as a LINT one is. */
            (void) lapic_accept_fixed_interrupt(lapic, &message, LAPIC_ESR_RECV_ILLEGAL_VECTOR);
            cpu_lapic_lvt_delivery_complete_index(lapic->index, lvt_reg);
            return 1;
        }

        case CPU_LAPIC_LINT_ACTION_SMI:
            cpu_smi_raise_direct();
            return 1;

        case CPU_LAPIC_LINT_ACTION_NMI:
            cpu_nmi_raise_core(1);
            return 1;

        default:
            return 1;
    }
}

static int
lapic_icr_remote_read_write(lapic_state_t *lapic, const lapic_target_info_t *targets,
                            uint8_t vector)
{
    lapic_state_t *target;
    uint32_t       val;
    uint32_t       reg = ((uint32_t) vector) << 4;

    if ((lapic == NULL) || (targets == NULL))
        return 0;

    if (!targets->count) {
        lapic_error(lapic, apic_bus_result_no_target().source_esr);
        return 0;
    }

    if (!lapic_mmio_reg_defined(reg)) {
        lapic_error(lapic, apic_bus_result_invalid_register().source_esr);
        return 0;
    }

    target = targets->lapics[0];
    if (target == NULL)
        return 0;

    /*
     * Remote read returns one selected LAPIC register image. Mirror fallback
     * covers state owned outside the CPU LAPIC MMIO helper.
     */
    if (!cpu_lapic_mmio_read_index(target->index, reg, &val))
        val = lapic_mirror_read_reg(target, reg);
    /* "reset after being read by either the CPU or through the Remote
       Read message" (Pentium Vol. 3, 19.3.3). */
    if (reg == LAPIC_REG_ESR)
        cpu_lapic_esr_read_clear_index(target->index);
    cpu_lapic_icr_remote_read_complete_index(lapic->index, val);
    lapic_log(lapic->mmio, "LAPIC: remote read slot=%u reg=%03x val=%08x\n",
              targets->indices[0], reg, val);

    return 1;
}

/* An IPI a target had no slot for: sent again when a slot comes free
   anywhere (lapic_slot_freed), with the sender's delivery status left
   pending until then (Pentium Vol. 3, 19.3.1.8: "Send Pending"). */
#define LAPIC_ICR_RETRY 2
uint32_t lapic_icr_retry_val[APIC_MAX_LAPICS];
uint8_t  lapic_icr_retry_pending[APIC_MAX_LAPICS];
int      lapic_icr_retry_route_parked[APIC_MAX_LAPICS];

static int
lapic_icr_fixed_write(lapic_state_t *lapic, const lapic_target_info_t *targets,
                      uint8_t vector, int route_parked, int self)
{
    lapic_target_info_t filtered;
    apic_bus_message_t  message = {
        .source   = APIC_BUS_AGENT_LAPIC,
        .delivery = APIC_BUS_DELIVERY_FIXED,
        .vector   = vector
    };
    apic_bus_result_t   result;

    if (lapic == NULL)
        return 0;

    /* A self interrupt is no bus message: an illegal vector is found on
       receipt (Pentium Vol. 3, 19.3.3), and it is taken even while the
       APIC is software disabled (19.3.1.11), held in the IRR then. */
    if (!lapic_vector_valid(vector)) {
        lapic_error(lapic, self ? LAPIC_ESR_RECV_ILLEGAL_VECTOR :
                                  apic_bus_result_invalid_send_vector().source_esr);
        return 0;
    }
    if (self) {
        /* No slot check: "Self interrupts do not generate bus messages"
           (19.3.1.8), so there is no status cycle to answer retry on, and
           the sender waiting on its own delivery status could never see
           it clear. It goes into the IRR, merging with one pending. */
        cpu_lapic_accept_interrupt_index(lapic->index, vector, 0);
        lapic_sync_ppr(lapic);
        return 1;
    }

    if (targets == NULL)
        return 0;

    filtered = *targets;
    lapic_targets_keep_interruptible(&filtered, route_parked);
    result = lapic_targets_accept_fixed(&filtered, &message);
    if (result.status == APIC_BUS_RETRY)
        return LAPIC_ICR_RETRY;
    if (result.source_esr)
        lapic_error(lapic, result.source_esr);

    return apic_bus_result_accepted(result);
}

static int
lapic_icr_lowest_priority_write(lapic_state_t *lapic, const lapic_target_info_t *targets,
                                uint8_t vector, int route_parked)
{
    lapic_target_info_t filtered;

    if (lapic == NULL)
        return 0;

    if (!lapic_vector_valid(vector)) {
        lapic_error(lapic, apic_bus_result_invalid_send_vector().source_esr);
        return 0;
    }

    if (targets == NULL)
        return 0;

    filtered = *targets;
    lapic_targets_keep_interruptible(&filtered, route_parked);

    apic_bus_message_t message = {
        .source   = APIC_BUS_AGENT_LAPIC,
        .delivery = APIC_BUS_DELIVERY_LOWEST,
        .vector   = vector
    };
    apic_bus_result_t result =
        lapic_targets_accept_lowest_priority(&filtered, &lapic_icr_lowest_priority_next,
                                             route_parked, &message);

    if (apic_bus_result_accepted(result))
        return 1;
    if (result.status == APIC_BUS_RETRY)
        return LAPIC_ICR_RETRY;

    if (result.source_esr)
        lapic_error(lapic, result.source_esr);
    return 0;
}

static int
lapic_icr_smi_write(lapic_state_t *lapic, const lapic_target_info_t *targets,
                    int route_parked)
{
    lapic_target_info_t filtered;
    int                 delivered = 0;

    if (targets == NULL)
        return 0;

    filtered = *targets;
    lapic_targets_keep_interruptible(&filtered, route_parked);
    for (uint8_t i = 0; i < filtered.count; i++) {
        cpu_smi_raise_direct();
        delivered = 1;
    }
    if (!delivered)
        lapic_error(lapic, apic_bus_result_no_target().source_esr);

    return delivered;
}

static int
lapic_icr_nmi_write(lapic_state_t *lapic, const lapic_target_info_t *targets,
                    int route_parked)
{
    lapic_target_info_t filtered;
    int                 delivered = 0;

    if (targets == NULL)
        return 0;

    filtered = *targets;
    lapic_targets_keep_interruptible(&filtered, route_parked);
    for (uint8_t i = 0; i < filtered.count; i++) {
        cpu_nmi_raise_core(1);
        delivered = 1;
    }
    if (!delivered)
        lapic_error(lapic, apic_bus_result_no_target().source_esr);

    return delivered;
}

static lapic_startup_action_t
lapic_icr_startup_state_write(lapic_state_t *target, uint32_t delivery, uint8_t vector)
{
    if (target == NULL)
        return LAPIC_STARTUP_ACTION_NONE;

    /* "In the APIC, NMI and INIT (except for INIT Deassert) are always
       treated as edge triggered interrupts" (Pentium Vol. 3, 19.3.6): an
       INIT is over as soon as it is sent. */
    if (delivery == LAPIC_ICR_DELIV_INIT) {
        lapic_init_signal(target);
        return LAPIC_STARTUP_ACTION_INIT;
    }

    if (delivery == LAPIC_ICR_DELIV_SIPI) {
        if (!target->is_bsp && target->wait_for_sipi) {
            target->sipi_vector = vector;
            return LAPIC_STARTUP_ACTION_SIPI;
        }
    }

    return LAPIC_STARTUP_ACTION_NONE;
}

void
lapic_startup_mark_parked(lapic_state_t *lapic)
{
    if (lapic == NULL)
        return;

    /*
     * Parked APs have accepted SIPI and moved to startup ownership. Normal
     * interrupt routing skips them unless the caller explicitly routes parked.
     */
    lapic->started = 1;
    lapic->wait_for_sipi = 0;
    lapic->init_asserted = 0;
    lapic->parked = 1;
}

/* The I/O APIC is an agent on the same bus: its Arbitration ID moves with
   the processors' (ioapic.c attaches it). */
static uint8_t       *lapic_bus_ioapic_arb;
static const uint8_t *lapic_bus_ioapic_id;

void
lapic_bus_ioapic_attach(uint8_t *arb_id, const uint8_t *id)
{
    lapic_bus_ioapic_arb = arb_id;
    lapic_bus_ioapic_id  = id;
}

static uint8_t
lapic_bus_arb_next(uint8_t arb, uint8_t winner)
{
    return (arb == 15) ? ((winner + 1) & 0x0f) : ((arb + 1) & 0x0f);
}

/* A message won the bus: "The winner of the arbitration becomes the lowest
   priority agent and assumes an arbitration ID of 0. All other agents,
   except the agent whose arbitration ID is 15, increment their arbitration
   IDs by one. The agent whose ID was 15 takes the winner's arbitration ID
   and increments it by one" (82093AA, 3.2.3; Pentium Vol. 3, 19.3.2.1).
   The winner is one local APIC, or the I/O APIC when winner_lapic is
   NULL. */
static void
lapic_bus_rotate(lapic_state_t *lapics, uint8_t count, lapic_state_t *winner_lapic)
{
    uint8_t winner;

    if (winner_lapic != NULL)
        winner = winner_lapic->arb_id & 0x0f;
    else if (lapic_bus_ioapic_arb != NULL)
        winner = *lapic_bus_ioapic_arb & 0x0f;
    else
        return;

    for (uint8_t i = 0; (lapics != NULL) && (i < count); i++) {
        lapic_state_t *lapic = lapic_at(lapics, count, i);

        if ((lapic != NULL) && (lapic != winner_lapic))
            lapic_arbitration_id_set(lapic, lapic_bus_arb_next(lapic->arb_id & 0x0f, winner));
    }
    if ((lapic_bus_ioapic_arb != NULL) && (winner_lapic != NULL))
        *lapic_bus_ioapic_arb = lapic_bus_arb_next(*lapic_bus_ioapic_arb & 0x0f, winner);

    if (winner_lapic != NULL)
        lapic_arbitration_id_set(winner_lapic, 0);
    else
        *lapic_bus_ioapic_arb = 0;
}

void
lapic_arbitration_rotate_after_lapic(lapic_state_t *lapics, uint8_t count,
                                     uint8_t winner_index)
{
    lapic_state_t *winner_lapic = lapic_at(lapics, count, winner_index);

    if (winner_lapic != NULL)
        lapic_bus_rotate(lapics, count, winner_lapic);
}

static int
lapic_icr_startup_write(lapic_state_t *lapic, const lapic_target_info_t *targets,
                        uint32_t delivery, uint8_t vector,
                        lapic_icr_startup_handler_t startup_handler, void *startup_priv)
{
    int updated = 0;

    if (targets == NULL)
        return 0;

    if (!targets->count) {
        lapic_error(lapic, apic_bus_result_no_target().source_esr);
        return 0;
    }

    for (uint8_t i = 0; i < targets->count; i++) {
        lapic_state_t *target = targets->lapics[i];
        uint8_t        target_index = targets->indices[i];
        lapic_startup_action_t action;

        if (target == NULL)
            continue;

        action = lapic_icr_startup_state_write(target, delivery, vector);
        if (action == LAPIC_STARTUP_ACTION_INIT) {
            updated = 1;
        } else if (action == LAPIC_STARTUP_ACTION_SIPI) {
            if ((startup_handler != NULL) &&
                startup_handler(startup_priv, target_index, vector))
                lapic_startup_mark_parked(target);
            updated = 1;
        }
    }

    return updated;
}

/* The INIT Level De-assert: "All Local APICs, whether they are targeted
   or not, load their Arbitration ID register with values from their APIC
   ID registers" (Pentium Vol. 3, 19.3.1.11.3). It resets nothing. */
static void
lapic_init_deassert_all(lapic_state_t *lapics, uint8_t count)
{
    for (uint8_t i = 0; i < count; i++) {
        lapic_state_t *l = lapic_at(lapics, count, i);

        if (l == NULL)
            continue;
        l->init_asserted = 0;
        lapic_arbitration_id_reset_from_id(l);
    }
    /* "The APICARB Register is always loaded with IOAPIC ID during a 'level
       triggered INIT with de-assert' message" (82093AA, 3.2.3). */
    if ((lapic_bus_ioapic_arb != NULL) && (lapic_bus_ioapic_id != NULL))
        *lapic_bus_ioapic_arb = *lapic_bus_ioapic_id & 0x0f;
}

void
lapic_icr_write(lapic_state_t *lapic, lapic_state_t *lapics, uint8_t count,
                uint8_t current_index, uint32_t val, int route_parked,
                lapic_icr_startup_handler_t startup_handler, void *startup_priv)
{
    cpu_lapic_icr_delivery_t icr;
    lapic_target_info_t      targets;
    int                      delivered = 0;

    if ((lapic == NULL) || (lapics == NULL))
        return;

    /* The ICR holds one message: this one replaces any still waiting to be
       sent again, before its delivery can free a slot and send that one. */
    if (lapic->index < APIC_MAX_LAPICS)
        lapic_icr_retry_pending[lapic->index] = 0;

    if (!cpu_lapic_icr_delivery_write_index(lapic->index, val, &icr))
        return;

    lapic_select_icr_targets(lapics, count, current_index, icr.dest, icr.destmod,
                             icr.shorthand, &targets);

    /* A level-triggered message with the level clear (Pentium Vol. 3,
       Table 19-7): an INIT is the INIT Level De-assert, "sent to all
       agents, regardless of the destination field value" (19.3.1.8), and
       every other is ignored. */
    if (icr.trigger && !icr.level) {
        if (icr.delivery == LAPIC_ICR_DELIV_INIT)
            lapic_init_deassert_all(lapics, count);
        cpu_lapic_icr_delivery_complete_index(lapic->index);
        return;
    }

    if (icr.delivery == LAPIC_ICR_DELIV_FIXED) {
        delivered = lapic_icr_fixed_write(lapic, &targets, icr.vector, route_parked,
                                          icr.shorthand == LAPIC_ICR_DEST_SELF);
    } else if (icr.delivery == LAPIC_ICR_DELIV_LOWEST) {
        delivered = lapic_icr_lowest_priority_write(lapic, &targets, icr.vector,
                                                    route_parked);
    } else if (icr.delivery == LAPIC_ICR_DELIV_SMI) {
        delivered = lapic_icr_smi_write(lapic, &targets, route_parked);
    } else if ((icr.delivery == LAPIC_ICR_DELIV_REMOTE_READ) && cpu_is_p5()) {
        /* The P6 has no Remote Read: delivery mode "011 (Reserved)"
           (Pentium Pro Vol. 3, 7.4.9). */
        cpu_lapic_icr_remote_read_in_progress_index(lapic->index);
        delivered = lapic_icr_remote_read_write(lapic, &targets, icr.vector);
        if (!delivered)
            cpu_lapic_icr_remote_read_clear_index(lapic->index);
    } else if (icr.delivery == LAPIC_ICR_DELIV_NMI) {
        delivered = lapic_icr_nmi_write(lapic, &targets, route_parked);
    } else if ((icr.delivery == LAPIC_ICR_DELIV_INIT) || (icr.delivery == LAPIC_ICR_DELIV_SIPI)) {
        delivered = lapic_icr_startup_write(lapic, &targets, icr.delivery, icr.vector,
                                            startup_handler, startup_priv);
    }

    /* No slot for it: the delivery status stays Send Pending and the
       message goes again when a slot comes free (lapic_icr_retry_all). */
    if (delivered == LAPIC_ICR_RETRY) {
        if (lapic->index < APIC_MAX_LAPICS) {
            lapic_icr_retry_val[lapic->index]          = val;
            lapic_icr_retry_route_parked[lapic->index] = route_parked;
            lapic_icr_retry_pending[lapic->index]      = 1;
        }
        return;
    }

    cpu_lapic_icr_delivery_complete_index(lapic->index);

    /* "Self interrupts do not generate bus messages" (19.3.1.8), so the
       arbitration IDs stay where they are. They move for a message sent
       successfully, and for a lowest priority one either way (82093AA,
       3.2.3). */
    if ((delivered || (icr.delivery == LAPIC_ICR_DELIV_LOWEST)) &&
        (icr.shorthand != LAPIC_ICR_DEST_SELF))
        lapic_arbitration_rotate_after_lapic(lapics, count, current_index);

    if (lapic_log_first(&lapic->icr_log_count[icr.vector], 8))
        lapic_log(lapic->mmio,
                  "LAPIC: ICR write vector=%02x delivery=%03x destmod=%u shorthand=%x dest=%02x level=%u trigger=%u deliver=%u count=%u\n",
                  icr.vector, icr.delivery >> 8, icr.destmod, icr.shorthand >> 18,
                  icr.dest, icr.level, icr.trigger, delivered,
                  lapic->icr_log_count[icr.vector]);
}

static int
lapic_logical_dest_match(const lapic_state_t *lapic, uint8_t dest)
{
    uint8_t  logical_id = cpu_lapic_ldr_read_index(lapic->index) >> 24;
    uint32_t dfr        = cpu_lapic_dfr_read_index(lapic->index);

    if (!dest)
        return 0;

    /* All ones is the broadcast in either model, not a decoded address:
       flat, "Broadcast to all units is achieved by setting all 8 bits of
       the MDA to ones"; cluster, "setting all destination bits to one ...
       guarantees a match on all clusters, and selects all APICs in each
       cluster" (Pentium Vol. 3, 19.3.1.5; Pentium Pro Vol. 3, 7.4.6.3 and
       7.4.6.4). The LDR plays no part: Linux 2.2 clears every LDR to 0 and
       sends each I/O APIC interrupt, lowest priority, to logical FFh. */
    if (dest == 0xff)
        return 1;

    /* Flat: the MDA "is interpreted as a decoded address", each APIC
       selected by "setting the member's bit to one in the corresponding
       LDR" -- short of the broadcast, an LDR of 0 is selected by nothing. */
    if ((dfr & 0xf0000000) == 0xf0000000)
        return !!(dest & logical_id);

    /* Cluster: clusters 0 through 14 are addressed, cluster 15 is every
       one. */
    return (((dest & 0xf0) == 0xf0) || ((dest & 0xf0) == (logical_id & 0xf0))) &&
           !!(dest & logical_id & 0x0f);
}

static int
lapic_dest_match(const lapic_state_t *lapic, uint8_t dest, int logical, int physical_broadcast)
{
    if (logical)
        return lapic_logical_dest_match(lapic, dest);

    /* The P54C's APIC ID is four bits, "bits 56-59 contain the APIC ID"
       (Pentium Vol. 3, 19.3.1.8, 19.3.6), and the P6's: the destination's
       high nibble is not looked at. */
    if (physical_broadcast & LAPIC_PHYS_BROADCAST_4BIT)
        return ((dest & 0x0f) == (lapic_current_id(lapic) & 0x0f)) || ((dest & 0x0f) == 0x0f);

    return (dest == lapic_current_id(lapic)) ||
           ((physical_broadcast & LAPIC_PHYS_BROADCAST_8BIT) && (dest == 0xff));
}

void
lapic_targets_clear(lapic_target_info_t *targets)
{
    memset(targets, 0, sizeof(*targets));
}

static void
lapic_targets_add(lapic_target_info_t *targets, uint8_t index, lapic_state_t *lapic)
{
    if ((lapic == NULL) || (index >= APIC_MAX_LAPICS) ||
        (targets->count >= (sizeof(targets->lapics) / sizeof(targets->lapics[0]))))
        return;

    targets->bitmap |= 1u << index;
    targets->indices[targets->count] = index;
    targets->lapics[targets->count++] = lapic;
}

static int
lapic_target_interruptible(const lapic_state_t *lapic, int route_parked)
{
    if ((lapic == NULL) || !lapic->started)
        return 0;

    /* Parked LAPICs are startup-owned; only explicit route_parked paths see them. */
    return !lapic->parked || route_parked;
}

void
lapic_targets_keep_interruptible(lapic_target_info_t *targets, int route_parked)
{
    lapic_target_info_t filtered;

    lapic_targets_clear(&filtered);
    for (uint8_t i = 0; i < targets->count; i++) {
        if (lapic_target_interruptible(targets->lapics[i], route_parked))
            lapic_targets_add(&filtered, targets->indices[i], targets->lapics[i]);
    }

    *targets = filtered;
}

apic_bus_result_t
lapic_targets_accept_fixed(const lapic_target_info_t *targets,
                           const apic_bus_message_t *message)
{
    apic_bus_result_t result = apic_bus_result_no_target();

    if ((targets == NULL) || (message == NULL))
        return result;

    /* One target with no slot for the class puts retry status on the bus,
       and no agent accepts the message: the sender sends it again
       (cpu_lapic_slot_free_index). */
    for (uint8_t i = 0; i < targets->count; i++) {
        const lapic_state_t *t = targets->lapics[i];

        if ((t != NULL) &&
            (cpu_lapic_fixed_delivery_action_index(t->index, t->started, message->vector, 0, NULL) == CPU_LAPIC_FIXED_ACCEPT) &&
            !cpu_lapic_slot_free_index(t->index, message->vector))
            return apic_bus_result_retry();
    }

    uint32_t refused = 0;

    for (uint8_t i = 0; i < targets->count; i++) {
        apic_bus_result_t target_result =
            lapic_accept_fixed_interrupt(targets->lapics[i], message,
                                         LAPIC_ESR_RECV_ILLEGAL_VECTOR);

        if (apic_bus_result_accepted(target_result)) {
            result = target_result;
            continue;
        }
        if (target_result.target_esr == LAPIC_ESR_RECV_ACCEPT)
            refused |= 1u << (i & 31);
        /*
         * Multi-target delivery keeps trying every LAPIC. Preserve target ESR
         * only when no LAPIC accepted, so one bad receiver cannot poison a
         * successful send.
         */
        if (!apic_bus_result_accepted(result) && (target_result.target_esr != 0))
            result = target_result;
    }

    /* "Receive Accept Error: ... the message which was received by it was
       not accepted by any agent on the bus, including itself" (Pentium
       Vol. 3, 19.3.3): the status cycle is wired-OR, so a refusal is an
       error only when every agent refused. */
    if (!apic_bus_result_accepted(result)) {
        for (uint8_t i = 0; i < targets->count; i++)
            if (refused & (1u << (i & 31)))
                lapic_error(targets->lapics[i], LAPIC_ESR_RECV_ACCEPT);
    }

    return result;
}

apic_bus_result_t
lapic_targets_accept_lowest_priority(const lapic_target_info_t *targets,
                                     uint8_t *lowest_priority_next,
                                     int route_parked,
                                     const apic_bus_message_t *message)
{
    lapic_state_t      *target;
    lapic_target_info_t free_slots;

    if ((message == NULL) || (targets == NULL))
        return apic_bus_result_no_target();

    /* The focus processor takes it first, slot or no slot: "In lowest
       priority mode, if a focus processor exists, it may accept the
       interrupt, regardless of its priority. A processor is said to be the
       focus of an interrupt if it is currently servicing that interrupt or
       if it has a pending request for that interrupt" (Pentium Pro Vol. 3,
       7.4.7); "Am I Focus? Yes -> Accept Message" comes before "Is
       Interrupt Slot Available?" (Pentium Vol. 3, Figure 19-9). */
    target = lapic_choose_focus_processor_target(targets, message->vector, route_parked);
    if (target != NULL)
        return lapic_accept_interrupt_target(target, message);

    /* "Only the local APICs having 'free interrupt slots' will participate
       in the lowest priority arbitration. If no such APIC exists, the
       message will be rejected, and will have to be tried later" (Pentium
       Vol. 3, 19.3.2.2). */
    lapic_targets_clear(&free_slots);
    for (uint8_t i = 0; i < targets->count; i++)
        if ((targets->lapics[i] != NULL) && cpu_lapic_slot_free_index(targets->lapics[i]->index, message->vector))
            lapic_targets_add(&free_slots, targets->indices[i], targets->lapics[i]);
    if (targets->count && !free_slots.count)
        return apic_bus_result_retry();

    target = lapic_choose_lowest_priority_target(&free_slots, lowest_priority_next,
                                                 route_parked);
    if (target == NULL)
        return apic_bus_result_no_target();

    return lapic_accept_interrupt_target(target, message);
}

static void
lapic_arbitration_id_set(lapic_state_t *lapic, uint8_t arb_id)
{
    if (lapic == NULL)
        return;

    lapic->arb_id = arb_id & 0x0f;
    cpu_lapic_arb_id_load_index(lapic->index, ((uint32_t) lapic->arb_id) << 24);
}

void
lapic_arbitration_id_reset_from_id(lapic_state_t *lapic)
{
    if (lapic == NULL)
        return;

    lapic_arbitration_id_set(lapic, lapic->id);
}

void
lapic_arbitration_rotate_with_ioapic(lapic_state_t *lapics, uint8_t count,
                                     uint8_t *ioapic_arb_id)
{
    if ((ioapic_arb_id == NULL) || (ioapic_arb_id != lapic_bus_ioapic_arb))
        return;

    lapic_bus_rotate(lapics, count, NULL);
}

int
lapic_targets_mark_extint(const lapic_target_info_t *targets, uint8_t pin)
{
    int marked = 0;

    if ((targets == NULL) || !targets->count)
        return 0;

    /* "Deliver the signal to the INTR signal of all processor cores
       listed in the destination" (82093AA, delivery mode 111), each that
       takes it: "A software disabled Local APIC unit responds only to
       self-interrupts and INIT, NMI, SMI, Remote Read and Start-Up
       messages" (Pentium Vol. 3, 19.3.1.11). With none, the entry waits
       in Send Pending, sent again when an APIC is enabled (SVR). */
    for (uint8_t i = 0; i < targets->count; i++) {
        if (!lapic_enabled(targets->lapics[i]))
            continue;
        lapic_mark_extint_pending(targets->lapics[i], pin);
        marked = 1;
    }
    return marked;
}

int
lapic_select_targets(lapic_state_t *lapics, uint8_t count, uint8_t dest, int logical,
                     int physical_broadcast, lapic_target_info_t *targets)
{
    lapic_targets_clear(targets);
    for (uint8_t i = 0; i < count; i++) {
        lapic_state_t *lapic = lapic_at(lapics, count, i);

        if ((lapic != NULL) && lapic_dest_match(lapic, dest, logical, physical_broadcast))
            lapic_targets_add(targets, i, lapic);
    }

    return targets->count;
}

static int
lapic_icr_physical_broadcast_mode(lapic_state_t *lapics, uint8_t count,
                                  uint8_t current_index)
{
    const lapic_state_t *source = lapic_at(lapics, count, current_index);
    uint32_t             version;

    if (source == NULL)
        return LAPIC_PHYS_BROADCAST_4BIT;

    version = lapic_mirror_read_reg(source, LAPIC_REG_VERSION);
    return ((version & 0xff) >= 0x14) ?
               LAPIC_PHYS_BROADCAST_8BIT :
               LAPIC_PHYS_BROADCAST_4BIT;
}

int
lapic_select_icr_targets(lapic_state_t *lapics, uint8_t count, uint8_t current_index,
                         uint8_t dest, int logical, uint32_t shorthand,
                         lapic_target_info_t *targets)
{
    lapic_targets_clear(targets);

    switch (shorthand) {
        case 0:
            return lapic_select_targets(lapics, count, dest, logical,
                                        lapic_icr_physical_broadcast_mode(lapics, count,
                                                                         current_index),
                                        targets);

        case LAPIC_ICR_DEST_SELF:
            lapic_targets_add(targets, current_index, lapic_at(lapics, count, current_index));
            return targets->count;

        case LAPIC_ICR_DEST_ALL_INCL:
        case LAPIC_ICR_DEST_ALL_EXCL:
            for (uint8_t i = 0; i < count; i++) {
                if ((shorthand == LAPIC_ICR_DEST_ALL_EXCL) && (i == current_index))
                    continue;

                lapic_targets_add(targets, i, lapic_at(lapics, count, i));
            }
            return targets->count;
    }

    return 0;
}

static uint8_t
lapic_arbitration_id(const lapic_state_t *lapic)
{
    return (lapic != NULL) ? (lapic->arb_id & 0x0f) : 0x0f;
}

/* A software-disabled local APIC would only reject what it won, leaving
   the sender to retry while an enabled processor could have taken it; it
   takes no part in lowest-priority arbitration. */
static int
lapic_lowest_priority_candidate(const lapic_state_t *lapic, int route_parked)
{
    return lapic_target_interruptible(lapic, route_parked) && lapic_enabled(lapic);
}

static int
lapic_lowest_priority_best(const lapic_target_info_t *targets, int route_parked,
                           uint32_t *best_apr, uint8_t *best_arb)
{
    int found = 0;

    for (uint8_t i = 0; i < targets->count; i++) {
        lapic_state_t *candidate = targets->lapics[i];
        uint32_t       apr;

        if (!lapic_lowest_priority_candidate(candidate, route_parked))
            continue;

        /* The arbitration priority (Pentium Vol. 3, 19.3.2.1), not the
           processor priority: it counts what waits in the IRR too. */
        apr = cpu_lapic_apr_value_index(candidate->index);

        if (!found || (apr < *best_apr) ||
            ((apr == *best_apr) && (lapic_arbitration_id(candidate) > *best_arb))) {
            *best_apr = apr;
            *best_arb = lapic_arbitration_id(candidate);
            found = 1;
        }
    }

    return found;
}

static int
lapic_vector_word_has_bit(const uint32_t *words, uint8_t vector)
{
    return !!(words[vector >> 5] & (1u << (vector & 0x1f)));
}

static int
lapic_target_focus_checking_enabled(const lapic_state_t *lapic)
{
    uint32_t svr = 0;

    if (lapic == NULL)
        return 0;

    if (!cpu_lapic_mmio_read_index(lapic->index, LAPIC_REG_SVR, &svr))
        svr = lapic_mirror_read_reg(lapic, LAPIC_REG_SVR);

    return !lapic_svr_focus_processor_checking_disabled(svr);
}

static int
lapic_target_has_focus_vector(const lapic_state_t *lapic, uint8_t vector,
                              int route_parked)
{
    if (!lapic_target_interruptible(lapic, route_parked) ||
        !lapic_enabled(lapic) ||
        !lapic_vector_valid(vector) ||
        !lapic_target_focus_checking_enabled(lapic))
        return 0;

    return lapic_vector_word_has_bit(cpu_lapic_isr_words_index(lapic->index), vector) ||
           lapic_vector_word_has_bit(cpu_lapic_irr_words_index(lapic->index), vector);
}

static lapic_state_t *
lapic_choose_focus_processor_target(const lapic_target_info_t *targets, uint8_t vector,
                                    int route_parked)
{
    if (targets == NULL)
        return NULL;

    for (uint8_t i = 0; i < targets->count; i++) {
        if (lapic_target_has_focus_vector(targets->lapics[i], vector, route_parked))
            return targets->lapics[i];
    }

    return NULL;
}

lapic_state_t *
lapic_choose_lowest_priority_target(const lapic_target_info_t *targets,
                                    uint8_t *lowest_priority_next,
                                    int route_parked)
{
    uint32_t best_apr = 0;
    uint8_t  best_arb = 0;

    if ((targets == NULL) || (lowest_priority_next == NULL) ||
        !lapic_lowest_priority_best(targets, route_parked, &best_apr, &best_arb))
        return NULL;

    /*
     * First pass picks best APR/highest-arb tuple; this pass rotates only
     * among targets still matching that tuple, preserving deterministic ties.
     */
    for (uint8_t offset = 0; offset < APIC_MAX_LAPICS; offset++) {
        uint8_t index = (*lowest_priority_next + offset) % APIC_MAX_LAPICS;

        if (!(targets->bitmap & (1u << index)))
            continue;

        for (uint8_t i = 0; i < targets->count; i++) {
            lapic_state_t *candidate = targets->lapics[i];

            if ((targets->indices[i] == index) &&
                lapic_lowest_priority_candidate(candidate, route_parked) &&
                (cpu_lapic_apr_value_index(candidate->index) == best_apr) &&
                (lapic_arbitration_id(candidate) == best_arb)) {
                *lowest_priority_next = (index + 1) % APIC_MAX_LAPICS;
                return candidate;
            }
        }
    }

    return NULL;
}
