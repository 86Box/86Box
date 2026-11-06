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
#ifndef EMU_LAPIC_POLICY_H
#define EMU_LAPIC_POLICY_H

#include <stdint.h>

/* Shared LAPIC policy surface; callers keep ownership of state/backend side effects. */
extern int      lapic_reg_valid(uint32_t reg);
extern int      lapic_mmio_reg_defined(uint32_t reg);
extern int      lapic_vector_valid(uint8_t vector);
extern uint32_t lapic_priority_class(uint32_t priority);
extern int      lapic_priority_deliverable(uint8_t vector, uint32_t ppr);
extern uint32_t lapic_processor_priority(uint32_t tpr, int isr_vector, int p6);
extern uint32_t lapic_tpr_sanitize(uint32_t val);
extern uint32_t lapic_ldr_sanitize(uint32_t val);
extern uint32_t lapic_dfr_sanitize(uint32_t val);
extern uint32_t lapic_divide_cfg_sanitize(uint32_t val);
extern uint32_t lapic_svr_sanitize(uint32_t val);
extern int      lapic_svr_focus_processor_checking_disabled(uint32_t svr);
extern uint32_t lapic_esr_sanitize(uint32_t val);
extern uint32_t lapic_icr_lo_load_sanitize(uint32_t val);
extern uint32_t lapic_icr_hi_sanitize(uint32_t val);
extern uint32_t lapic_lvt_write_mask(uint32_t reg);
extern uint32_t lapic_lvt_sanitize(uint32_t reg, uint32_t val);
extern int      lapic_mmio_reg_read_only(uint32_t reg);
extern uint32_t lapic_timer_current_from_state(uint32_t active_count,
                                               uint64_t start_tsc,
                                               uint64_t now_tsc,
                                               double clocks_per_count);
extern void     lapic_timer_rebase_state(uint32_t initial, uint32_t current,
                                         uint64_t now_tsc,
                                         uint32_t *active_count,
                                         uint64_t *start_tsc,
                                         uint8_t *enabled);
extern uint32_t lapic_timer_restore_state(uint32_t initial, uint32_t current,
                                          int restore_enabled, uint64_t now_tsc,
                                          uint32_t *active_count,
                                          uint64_t *start_tsc,
                                          uint8_t *enabled);
extern uint32_t lapic_timer_periodic_restart_state(uint32_t initial,
                                                   uint64_t now_tsc,
                                                   uint32_t *active_count,
                                                   uint64_t *start_tsc);

#endif /* EMU_LAPIC_POLICY_H */
