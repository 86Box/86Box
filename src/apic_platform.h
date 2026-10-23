/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM PC
 *          systems and compatibles.
 *
 *          This file is part of the 86Box distribution.
 *
 *          Copyright 2026 skiretic.
 */
#ifndef EMU_APIC_PLATFORM_H
#define EMU_APIC_PLATFORM_H

#include <stdint.h>

#include "lapic.h"

typedef struct apic_platform_t apic_platform_t;
typedef void (*apic_platform_legacy_irq_route_handler_t)(void *priv,
                                                         uint8_t irq,
                                                         uint8_t intin);
typedef void (*apic_platform_pci_irq_route_handler_t)(void *priv,
                                                      uint8_t bus,
                                                      uint8_t device,
                                                      uint8_t intpin,
                                                      uint8_t intin);

extern apic_platform_t *apic_platform_create(void *owner);
extern void apic_platform_free(apic_platform_t *platform);
extern void apic_platform_set_log_handler(apic_platform_t *platform,
                                          lapic_log_handler_t log);
extern void apic_platform_set_icr_write_handler(apic_platform_t *platform,
                                                lapic_mmio_icr_write_handler_t icr_write);
extern void apic_platform_set_lapic_policy(apic_platform_t *platform,
                                           int uniprocessor_apic);
extern uint8_t apic_platform_requested_lapic_count(apic_platform_t *platform);
extern void apic_platform_reset_lapics(apic_platform_t *platform);
extern int  apic_platform_lapic_state_save(apic_platform_t *platform,
                                           struct ioapic_state_t *state);
extern int  apic_platform_lapic_state_load(apic_platform_t *platform,
                                           const struct ioapic_state_t *state);
extern void apic_platform_enable_lapic_mappings(apic_platform_t *platform);
extern void apic_platform_update_lapic_mapping(apic_platform_t *platform);
extern void apic_platform_stop_lapic_timers(apic_platform_t *platform);
extern void apic_platform_disable_lapic_mappings(apic_platform_t *platform);
extern void apic_platform_set_external_ioapic(apic_platform_t *platform,
                                              void *external_ioapic,
                                              lapic_external_eoi_handler_t external_eoi);
extern void apic_platform_lapic_icr_write(apic_platform_t *platform, uint32_t val);
extern uint8_t apic_platform_lapic_count(apic_platform_t *platform);
extern int  apic_platform_lapic_id_at(apic_platform_t *platform, uint8_t index,
                                      uint8_t *id);
extern int  apic_platform_bsp_lapic_id(apic_platform_t *platform, uint8_t *id);
extern int  apic_platform_current_external_pending(apic_platform_t *platform,
                                                   local_apic_pic_pending_handler_t pic_pending,
                                                   void *pic_priv);
extern int  apic_platform_current_external_vector(apic_platform_t *platform,
                                                  local_apic_pic_pending_handler_t pic_pending,
                                                  local_apic_pic_interrupt_handler_t pic_interrupt,
                                                  void *pic_priv);
extern int  apic_platform_current_base_enabled(apic_platform_t *platform);
extern int  apic_platform_current_software_enabled(apic_platform_t *platform);
extern int  apic_platform_lint_deliver_all(apic_platform_t *platform, uint32_t lint_reg);
extern int  apic_platform_current_lint_deliver(apic_platform_t *platform,
                                               uint32_t lint_reg);

/* Caller owns policy storage; platform may set logged flags to make POST logs one-shot. */
typedef struct apic_platform_mp_table_policy_t {
    uint8_t  expose_mp_tables;
    uint8_t  post_trigger;
    uint8_t *mp_table_logged;
    uint8_t *mp_table_skip_logged;
    void    *legacy_irq_route_priv;
    apic_platform_legacy_irq_route_handler_t legacy_irq_route_handler;
    void    *pci_irq_route_priv;
    apic_platform_pci_irq_route_handler_t pci_irq_route_handler;
} apic_platform_mp_table_policy_t;

extern void apic_platform_mp_forget(apic_platform_t *platform);
extern int  apic_platform_mp_table_post(apic_platform_t *platform, uint8_t val,
                                        const apic_platform_mp_table_policy_t *policy);

/* IOAPIC-facing bridge: select/deliver through LAPIC without exposing LAPIC internals. */
extern int  apic_platform_ioapic_select_targets(apic_platform_t *platform, uint8_t dest,
                                                int logical,
                                                lapic_target_info_t *targets);
extern apic_bus_result_t apic_platform_ioapic_deliver(apic_platform_t *platform,
                                         const lapic_target_info_t *targets,
                                         uint8_t *lowest_priority_next,
                                         const apic_bus_message_t *message);
extern int  apic_platform_ioapic_deliver_init(apic_platform_t *platform,
                                              const lapic_target_info_t *targets);
extern int  apic_platform_ioapic_mark_extint(apic_platform_t *platform,
                                             const lapic_target_info_t *targets,
                                             uint8_t pin);
extern uint8_t apic_platform_current_lapic_id(apic_platform_t *platform);
extern void apic_platform_ioapic_bus_message_complete(apic_platform_t *platform,
                                                      uint8_t *ioapic_arb_id);

#endif /* EMU_APIC_PLATFORM_H */
