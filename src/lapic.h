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
#ifndef EMU_LAPIC_H
#define EMU_LAPIC_H

#include <stdarg.h>
#include <stdint.h>

#include "apic_bus.h"

#define APIC_MAX_LAPICS        16
#define APIC_UP_LAPIC_COUNT    1
#define APIC_MAX_ACTIVE_LAPICS APIC_MAX_LAPICS

#define APIC_BASE_DEFAULT      0x00000000fee00900ULL
#define APIC_BASE_BSP          0x0000000000000100ULL
#define APIC_BASE_ENABLE       0x0000000000000800ULL
#define APIC_BASE_ADDR_MASK    0x00000000fffff000ULL
#define LAPIC_MMIO_SIZE        0x00001000

#define LAPIC_REG_ID           0x020
#define LAPIC_REG_VERSION      0x030
#define LAPIC_REG_TPR          0x080
#define LAPIC_REG_APR          0x090
#define LAPIC_REG_PPR          0x0a0
#define LAPIC_REG_EOI          0x0b0
#define LAPIC_REG_RRR          0x0c0
#define LAPIC_REG_LDR          0x0d0
#define LAPIC_REG_DFR          0x0e0
#define LAPIC_REG_SVR          0x0f0
#define LAPIC_REG_ISR_BASE     0x100
#define LAPIC_REG_TMR_BASE     0x180
#define LAPIC_REG_IRR_BASE     0x200
#define LAPIC_REG_ESR          0x280
#define LAPIC_REG_ICR_LO       0x300
#define LAPIC_REG_ICR_HI       0x310
#define LAPIC_REG_LVT_TIMER    0x320
#define LAPIC_REG_LVT_THERMAL  0x330
#define LAPIC_REG_LVT_PERF     0x340
#define LAPIC_REG_LVT_LINT0    0x350
#define LAPIC_REG_LVT_LINT1    0x360
#define LAPIC_REG_LVT_ERROR    0x370
#define LAPIC_REG_INITIAL_CNT  0x380
#define LAPIC_REG_CURRENT_CNT  0x390
#define LAPIC_REG_DIVIDE_CFG   0x3e0

#define LAPIC_SVR_ENABLE       0x00000100
#define LAPIC_LVT_VECTOR       0x000000ff
#define LAPIC_LVT_DELIV        0x00000700
#define LAPIC_LVT_DELIV_FIXED  0x00000000
#define LAPIC_LVT_DELIV_SMI    0x00000200
#define LAPIC_LVT_DELIV_NMI    0x00000400
#define LAPIC_LVT_DELIV_INIT   0x00000500
#define LAPIC_LVT_DELIV_EXTINT 0x00000700
#define LAPIC_LVT_POLARITY     0x00002000
#define LAPIC_LVT_TRIGGER      0x00008000
#define LAPIC_LVT_MASK         0x00010000
#define LAPIC_LVT_TIMER_MODE   0x00060000
#define LAPIC_LVT_TIMER_PERIODIC 0x00020000
#define LAPIC_ESR_SEND_CHECKSUM       0x00000001
#define LAPIC_ESR_RECV_CHECKSUM       0x00000002
#define LAPIC_ESR_SEND_ACCEPT         0x00000004
#define LAPIC_ESR_RECV_ACCEPT         0x00000008
#define LAPIC_ESR_SEND_ILLEGAL_VECTOR 0x00000020
#define LAPIC_ESR_RECV_ILLEGAL_VECTOR 0x00000040
#define LAPIC_ESR_ILLEGAL_REG_ADDR    0x00000080

#define LAPIC_ICR_VECTOR       0x000000ff
#define LAPIC_ICR_DELIV        0x00000700
#define LAPIC_ICR_DELIV_FIXED  0x00000000
#define LAPIC_ICR_DELIV_LOWEST 0x00000100
#define LAPIC_ICR_DELIV_SMI    0x00000200
#define LAPIC_ICR_DELIV_REMOTE_READ 0x00000300
#define LAPIC_ICR_DELIV_NMI    0x00000400
#define LAPIC_ICR_DELIV_INIT   0x00000500
#define LAPIC_ICR_DELIV_SIPI   0x00000600
#define LAPIC_ICR_DESTMOD      0x00000800
#define LAPIC_ICR_STATUS       0x00001000
#define LAPIC_ICR_LEVEL        0x00004000
#define LAPIC_ICR_TRIGGER      0x00008000
#define LAPIC_ICR_RR_MASK      0x00030000
#define LAPIC_ICR_RR_INVALID   0x00000000
#define LAPIC_ICR_RR_INPROG    0x00010000
#define LAPIC_ICR_RR_VALID     0x00020000
#define LAPIC_ICR_DEST_SHORT   0x000c0000
#define LAPIC_ICR_DEST_SELF    0x00040000
#define LAPIC_ICR_DEST_ALL_INCL 0x00080000
#define LAPIC_ICR_DEST_ALL_EXCL 0x000c0000

typedef struct lapic_mmio_device_t lapic_mmio_device_t;
typedef struct lapic_state_t lapic_state_t;
struct ioapic_state_t;

typedef struct lapic_target_info_t {
    uint32_t       bitmap;
    uint8_t        count;
    /* Delivery code needs both quick membership tests and stable target order. */
    uint8_t        indices[APIC_MAX_LAPICS];
    lapic_state_t *lapics[APIC_MAX_LAPICS];
} lapic_target_info_t;

/* Callback typedefs keep LAPIC core independent of CPU/PIC/IOAPIC owners. */
typedef void (*lapic_external_eoi_handler_t)(void *priv, uint8_t vector);
typedef uint8_t (*lapic_current_index_source_t)(void *priv);
typedef uint8_t (*lapic_index_source_t)(void);
typedef uint8_t (*lapic_reset_id_source_t)(void *priv, uint8_t index);
typedef void (*lapic_log_handler_t)(void *priv, const char *fmt, va_list ap);
typedef void (*lapic_mmio_icr_write_handler_t)(void *priv, uint32_t val);
typedef int (*lapic_icr_startup_handler_t)(void *priv, uint8_t target_index,
                                           uint8_t vector);
typedef int (*local_apic_pic_pending_handler_t)(void *priv);
typedef int (*local_apic_pic_interrupt_handler_t)(void *priv);

typedef struct local_apic_t local_apic_t;
typedef struct local_apic_attachment_t local_apic_attachment_t;

typedef enum local_apic_attachment_type_t {
    LOCAL_APIC_ATTACHMENT_INTEGRATED_CPU = 0,
    LOCAL_APIC_ATTACHMENT_82489DX
} local_apic_attachment_type_t;

extern void    lapic_set_index_source(lapic_index_source_t source);
extern uint8_t lapic_index_source_read(void *priv);

extern local_apic_t         *local_apic_create(void *owner);
extern void                  local_apic_free(local_apic_t *apic);
extern local_apic_attachment_t *local_apic_attachment_create_gated(
                                                local_apic_attachment_type_t type,
                                                void *owner,
                                                int enabled);
extern local_apic_attachment_t *local_apic_attachment_create(
                                                local_apic_attachment_type_t type,
                                                void *owner);
extern void                  local_apic_attachment_free(
                                                local_apic_attachment_t *attachment);
extern local_apic_t         *local_apic_attachment_apic(
                                                local_apic_attachment_t *attachment);
extern void                  local_apic_attachment_enable_mappings(
                                                local_apic_attachment_t *attachment);
extern void                  local_apic_init(local_apic_t *apic, void *owner);
extern void                  local_apic_set_count_config(local_apic_t *apic,
                                                         uint8_t count);
extern uint8_t               local_apic_count_config(const local_apic_t *apic);
extern uint8_t               local_apic_configured_count(const local_apic_t *apic,
                                                         uint8_t limit);
extern void                  local_apic_ensure_count_config_min(local_apic_t *apic,
                                                                uint8_t count);
extern void                  local_apic_reset(local_apic_t *apic, uint8_t count,
                                              lapic_reset_id_source_t id_source,
                                              void *id_priv);
extern int                   local_apic_state_save(
                                                local_apic_t *apic,
                                                struct ioapic_state_t *state);
extern int                   local_apic_state_load(
                                                local_apic_t *apic,
                                                const struct ioapic_state_t *state,
                                                uint8_t count_limit,
                                                lapic_reset_id_source_t id_source,
                                                void *id_priv);
extern uint8_t               local_apic_count(const local_apic_t *apic);
extern uint8_t               local_apic_current_id(const local_apic_t *apic);
extern int                   local_apic_id_at(const local_apic_t *apic, uint8_t index,
                                              uint8_t *id);
extern int                   local_apic_bsp_id(const local_apic_t *apic, uint8_t *id);
extern int                   local_apic_current_base_enabled(const local_apic_t *apic);
extern int                   local_apic_current_software_enabled(const local_apic_t *apic);
extern int                   local_apic_current_external_pending(
                                                const local_apic_t *apic,
                                                local_apic_pic_pending_handler_t pic_pending,
                                                void *pic_priv);
/*
 * External vector path arbitrates LAPIC IRR, latched ExtINT, then PIC fallback;
 * callers that do not provide PIC service pass NULL callbacks.
 */
extern int                   local_apic_current_external_vector(
                                                local_apic_t *apic,
                                                local_apic_pic_pending_handler_t pic_pending,
                                                local_apic_pic_interrupt_handler_t pic_interrupt,
                                                void *pic_priv);
extern int                   local_apic_lint_deliver_all(local_apic_t *apic, uint32_t lint_reg);
extern int                   local_apic_current_lint_deliver(local_apic_t *apic,
                                                             uint32_t lint_reg);
extern int                   local_apic_current_thermal_deliver(local_apic_t *apic);
extern int                   local_apic_current_perf_deliver(local_apic_t *apic);
extern int                   local_apic_select_interruptible_targets(
                                                local_apic_t *apic, uint8_t dest,
                                                int logical, int physical_broadcast,
                                                int route_parked,
                                                lapic_target_info_t *targets);
extern apic_bus_result_t     local_apic_targets_accept_fixed(
                                                local_apic_t *apic,
                                                const lapic_target_info_t *targets,
                                                const apic_bus_message_t *message);
/* Lowest-priority arbitration state is caller-owned for LAPIC/IOAPIC sharing. */
extern apic_bus_result_t     local_apic_targets_accept_lowest_priority(
                                                local_apic_t *apic,
                                                const lapic_target_info_t *targets,
                                                uint8_t *lowest_priority_next,
                                                int route_parked,
                                                const apic_bus_message_t *message);
extern int                   local_apic_targets_deliver_init(
                                                local_apic_t *apic,
                                                const lapic_target_info_t *targets);
extern void                  local_apic_arbitration_id_reset_from_id(
                                                local_apic_t *apic);
extern void                  lapic_bus_ioapic_attach(uint8_t *arb_id, const uint8_t *id);
extern void                  local_apic_arbitration_rotate_with_ioapic(
                                                local_apic_t *apic,
                                                uint8_t *ioapic_arb_id);
extern int                   local_apic_targets_mark_extint(
                                                local_apic_t *apic,
                                                const lapic_target_info_t *targets,
                                                uint8_t pin);
extern void                  local_apic_icr_write(local_apic_t *apic, uint32_t val,
                                                  int route_parked,
                                                  lapic_icr_startup_handler_t startup_handler,
                                                  void *startup_priv);
extern void                  local_apic_set_log_handler(local_apic_t *apic,
                                                        lapic_log_handler_t log);
extern void                  local_apic_set_icr_write_handler(
                                                local_apic_t *apic,
                                                lapic_mmio_icr_write_handler_t icr_write);
extern void                  local_apic_set_external_ioapic(
                                                local_apic_t *apic,
                                                void *external_ioapic,
                                                lapic_external_eoi_handler_t external_eoi);
extern void                  local_apic_enable_mappings(local_apic_t *apic);
extern void                  local_apic_update_mapping(local_apic_t *apic);
extern void                  local_apic_stop_timers(local_apic_t *apic);
extern void                  local_apic_disable_mappings(local_apic_t *apic);
extern void                  local_apic_close(local_apic_t *apic);

#endif /* EMU_LAPIC_H */
