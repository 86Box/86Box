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
#ifndef EMU_LAPIC_INTERNAL_H
#define EMU_LAPIC_INTERNAL_H

#include <stdint.h>

#include <86box/mem.h>

#include "apic_bus.h"
#include "lapic.h"

struct lapic_state_t {
    uint8_t       id;
    uint8_t       index;
    lapic_mmio_device_t *mmio;
    uint64_t      base_msr;
    mem_mapping_t mapping;
    uint8_t       mapping_added;
    /* MMIO-visible mirror; CPU-owned registers sync through cpu_lapic_* helpers. */
    uint32_t      regs[0x400 >> 4];
    /* ExtINT is latched here until the current CPU asks the PIC for a vector. */
    uint8_t       extint_pending;
    uint8_t       extint_pin;
    /* Startup state belongs to the LAPIC slot, not to the platform wrapper. */
    uint8_t       is_bsp;
    uint8_t       started;
    uint8_t       init_asserted;
    uint8_t       wait_for_sipi;
    uint8_t       sipi_vector;
    /* Delivery filters decide whether parked AP slots can still be targeted. */
    uint8_t       parked;
    uint8_t       arb_id;
    /* Per-vector rate limits keep repeated APIC logs useful during guest loops. */
    uint8_t       accept_log_count[256];
    uint8_t       promote_log_count[256];
    uint8_t       eoi_log_count[256];
    uint8_t       icr_log_count[256];
    uint8_t       lint_init_log_count;
    uint8_t       read_log_count[0x40];
    uint8_t       write_log_count[0x40];
};

struct lapic_mmio_device_t {
    uint8_t                        enabled;
    uint8_t                        active_count;
    uint8_t                        external_ioapic_present;
    /* owner is callback context; lapics points at the containing device slots. */
    void                          *owner;
    lapic_state_t                 *lapics;
    /* External EOI is optional; IOAPIC integration registers it explicitly. */
    void                          *external_ioapic;
    lapic_external_eoi_handler_t   external_eoi;
    /* Current-index source is the CPU/LAPIC ownership boundary for MMIO access. */
    lapic_current_index_source_t   current_index;
    void                          *current_index_priv;
    lapic_log_handler_t            log;
    lapic_mmio_icr_write_handler_t icr_write;
};

typedef struct lapic_device_t {
    /* count is runtime-active slots; count_config is platform/config intent. */
    uint8_t             count;
    uint8_t             count_config;
    lapic_state_t       lapics[APIC_MAX_LAPICS];
    lapic_mmio_device_t mmio;
} lapic_device_t;

struct local_apic_t {
    lapic_device_t device;
};

typedef enum lapic_startup_action_t {
    LAPIC_STARTUP_ACTION_NONE = 0,
    LAPIC_STARTUP_ACTION_INIT,
    LAPIC_STARTUP_ACTION_SIPI
} lapic_startup_action_t;

extern void                 lapic_mmio_device_init(lapic_mmio_device_t *dev,
                                                  void *owner);
extern void                 lapic_mmio_device_set_lapics(lapic_mmio_device_t *dev,
                                                         lapic_state_t *lapics);
extern void                 lapic_mmio_device_set_enabled(lapic_mmio_device_t *dev,
                                                          int enabled);
extern int                  lapic_mmio_device_enabled(
                                                const lapic_mmio_device_t *dev);
extern void                 lapic_mmio_device_set_active_count(
                                                lapic_mmio_device_t *dev,
                                                uint8_t active_count);
extern uint8_t              lapic_mmio_device_active_count(
                                                const lapic_mmio_device_t *dev);
extern void                 lapic_mmio_device_set_current_index_source(
                                                lapic_mmio_device_t *dev,
                                                lapic_current_index_source_t source,
                                                void *priv);
extern uint8_t              lapic_mmio_device_current_index(
                                                const lapic_mmio_device_t *dev);
extern void                 lapic_mmio_device_set_external_ioapic(
                                                lapic_mmio_device_t *dev,
                                                void *external_ioapic,
                                                lapic_external_eoi_handler_t external_eoi);
extern int                  lapic_mmio_device_has_external_ioapic(
                                                const lapic_mmio_device_t *dev);
extern void                 lapic_mmio_device_external_eoi(lapic_mmio_device_t *dev,
                                                           uint8_t vector);
extern void                 lapic_mmio_device_set_log_handler(
                                                lapic_mmio_device_t *dev,
                                                lapic_log_handler_t log);
extern void                 lapic_mmio_device_set_icr_write_handler(
                                                lapic_mmio_device_t *dev,
                                                lapic_mmio_icr_write_handler_t icr_write);
extern void                 lapic_log(lapic_mmio_device_t *dev,
                                      const char *fmt, ...);
extern int                  lapic_log_first(uint8_t *count, uint8_t limit);

extern lapic_state_t       *lapic_at(lapic_state_t *lapics, uint8_t count,
                                     uint8_t index);
extern const lapic_state_t *lapic_at_const(const lapic_state_t *lapics,
                                           uint8_t count, uint8_t index);
extern uint8_t              lapic_current_index(lapic_state_t *lapics,
                                                uint8_t count,
                                                lapic_current_index_source_t source,
                                                void *priv);
extern lapic_state_t       *lapic_current(lapic_state_t *lapics, uint8_t count,
                                          lapic_current_index_source_t source,
                                          void *priv);
extern const lapic_state_t *lapic_current_const(const lapic_state_t *lapics,
                                                uint8_t count,
                                                lapic_current_index_source_t source,
                                                void *priv);
extern lapic_state_t       *lapic_bsp(lapic_state_t *lapics, uint8_t count);
extern const lapic_state_t *lapic_bsp_const(const lapic_state_t *lapics,
                                            uint8_t count);
extern void                 lapic_device_set_count(lapic_device_t *dev,
                                                   uint8_t count);
extern lapic_state_t       *lapic_device_current(lapic_device_t *dev);
extern lapic_state_t       *lapic_device_bsp(lapic_device_t *dev);
extern void                 lapic_device_reset_slot(lapic_device_t *dev,
                                                    uint8_t index, uint8_t id);
extern int                  lapic_device_state_save(lapic_device_t *dev,
                                                    struct ioapic_state_t *state);
extern int                  lapic_device_state_load(lapic_device_t *dev,
                                                    const struct ioapic_state_t *state,
                                                    uint8_t count,
                                                    lapic_reset_id_source_t id_source,
                                                    void *id_priv);

extern uint8_t              lapic_current_id(const lapic_state_t *lapic);
extern int                  lapic_base_enabled(const lapic_state_t *lapic);
extern int                  lapic_software_enabled(const lapic_state_t *lapic);
extern int                  lapic_enabled(const lapic_state_t *lapic);
extern int                  lapic_vector_valid(uint8_t vector);
extern int                  lapic_interrupt_pending(const lapic_state_t *lapic);
extern int                  lapic_highest_deliverable_irr(
                                                const lapic_state_t *lapic);
extern int                  lapic_extint_deliverable(const lapic_state_t *lapic);
extern int                  lapic_lint0_extint_enabled(
                                                const lapic_state_t *lapic);
extern int                  lapic_lint_deliver(lapic_state_t *lapic,
                                               uint32_t lint_reg);
extern int                  lapic_lvt_source_deliver(lapic_state_t *lapic,
                                                     uint32_t lvt_reg);
extern int                  lapic_promote_irr(lapic_state_t *lapic);
extern void                 lapic_mark_extint_pending(lapic_state_t *lapic,
                                                      uint8_t pin);
extern int                  lapic_consume_extint(lapic_state_t *lapic,
                                                 uint8_t *pin);
extern apic_bus_result_t    lapic_accept_interrupt_target(lapic_state_t *lapic,
                                                          const apic_bus_message_t *message);
extern void                 lapic_init_signal(lapic_state_t *lapic);
extern void                 lapic_error(lapic_state_t *lapic, uint32_t error);
extern void                 lapic_targets_clear(lapic_target_info_t *targets);
extern void                 lapic_targets_keep_interruptible(
                                                lapic_target_info_t *targets,
                                                int route_parked);
extern apic_bus_result_t    lapic_targets_accept_fixed(
                                                const lapic_target_info_t *targets,
                                                const apic_bus_message_t *message);
extern apic_bus_result_t    lapic_targets_accept_lowest_priority(
                                                const lapic_target_info_t *targets,
                                                uint8_t *lowest_priority_next,
                                                int route_parked,
                                                const apic_bus_message_t *message);
extern int                  lapic_targets_deliver_init(
                                                const lapic_target_info_t *targets);
extern void                 lapic_arbitration_id_reset_from_id(
                                                lapic_state_t *lapic);
extern void                 lapic_arbitration_rotate_after_lapic(lapic_state_t *lapics,
                                                                 uint8_t count,
                                                                 uint8_t winner_index);
extern void                 lapic_bus_sent_by(lapic_state_t *lapic);
extern void                 lapic_arbitration_rotate_with_ioapic(
                                                lapic_state_t *lapics,
                                                uint8_t count,
                                                uint8_t *ioapic_arb_id);
extern int                  lapic_targets_mark_extint(
                                                const lapic_target_info_t *targets,
                                                uint8_t pin);
extern int                  lapic_select_targets(lapic_state_t *lapics,
                                                 uint8_t count, uint8_t dest,
                                                 int logical,
                                                 int physical_broadcast,
                                                 lapic_target_info_t *targets);
extern int                  lapic_select_icr_targets(lapic_state_t *lapics,
                                                     uint8_t count,
                                                     uint8_t current_index,
                                                     uint8_t dest, int logical,
                                                     uint32_t shorthand,
                                                     lapic_target_info_t *targets);
extern lapic_state_t       *lapic_choose_lowest_priority_target(
                                                const lapic_target_info_t *targets,
                                                uint8_t *lowest_priority_next,
                                                int route_parked);

extern void                 lapic_sync_ppr(lapic_state_t *lapic);
extern uint32_t             lapic_timer_current(const lapic_state_t *lapic);
extern void                 lapic_sync_int_regs(lapic_state_t *lapic);
extern void                 lapic_sync_cpu_owned_regs(lapic_state_t *lapic);
extern void                 lapic_load_cpu_owned_regs(lapic_state_t *lapic);
extern uint32_t             lapic_mirror_read_reg(const lapic_state_t *lapic,
                                                  uint32_t reg);
extern void                 lapic_mirror_write_reg(lapic_state_t *lapic,
                                                   uint32_t reg, uint32_t val);
extern void                 lapic_reg_sync(lapic_state_t *lapic, uint32_t reg,
                                           uint32_t val);

extern uint64_t             lapic_reset_base_msr(uint8_t index);
extern void                 lapic_reset_startup_state(lapic_state_t *lapic,
                                                      uint8_t index);
extern void                 lapic_reset_slot(lapic_state_t *lapic,
                                             lapic_mmio_device_t *mmio,
                                             uint8_t index, uint8_t id);
extern void                 lapic_startup_mark_parked(lapic_state_t *lapic);

extern void                 lapic_timer_host_stop(lapic_state_t *lapic);
extern void                 lapic_timer_stop(lapic_state_t *lapic);
extern void                 lapic_timer_arm(lapic_mmio_device_t *dev,
                                            uint32_t count);
extern void                 lapic_timer_restore_target(lapic_state_t *lapic,
                                                       uint32_t initial,
                                                       uint32_t current,
                                                       int enabled);
extern void                 lapic_timer_callback(void *priv);

extern uint32_t             lapic_mmio_base(const lapic_state_t *lapic);
extern void                 lapic_update_mapping(lapic_mmio_device_t *dev);
extern uint32_t             lapic_readl(uint32_t addr, void *priv);
extern uint16_t             lapic_readw(uint32_t addr, void *priv);
extern uint8_t              lapic_readb(uint32_t addr, void *priv);
extern void                 lapic_writel(uint32_t addr, uint32_t val, void *priv);
extern void                 lapic_writew(uint32_t addr, uint16_t val, void *priv);
extern void                 lapic_writeb(uint32_t addr, uint8_t val, void *priv);

extern void                 lapic_icr_write(lapic_state_t *lapic,
                                            lapic_state_t *lapics, uint8_t count,
                                            uint8_t current_index, uint32_t val,
                                            int route_parked,
                                            lapic_icr_startup_handler_t startup_handler,
                                            void *startup_priv);

/* An IPI a target had no slot for, per sender (lapic_delivery.c). */
extern uint32_t             lapic_icr_retry_val[APIC_MAX_LAPICS];
extern uint8_t              lapic_icr_retry_pending[APIC_MAX_LAPICS];
extern int                  lapic_icr_retry_route_parked[APIC_MAX_LAPICS];
extern void                 lapic_icr_retry_all(void);
extern void                 lapic_slot_freed(void);

#endif /* EMU_LAPIC_INTERNAL_H */
