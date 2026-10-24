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
#include <stdlib.h>
#include <string.h>

#include <86box/device.h>
#include <86box/chipset.h>
#include <86box/mem.h>

#include "cpu.h"
#include "lapic.h"
#include "lapic_internal.h"

struct local_apic_attachment_t {
    local_apic_attachment_type_t type;
    void                         *owner;
    local_apic_t                 *apic;
};

typedef struct local_apic_state_save_adapter_t {
    struct ioapic_state_t *state;
} local_apic_state_save_adapter_t;

typedef struct local_apic_state_load_adapter_t {
    const struct ioapic_state_t *state;
    uint8_t                     count;
    lapic_reset_id_source_t     id_source;
    void                       *id_priv;
} local_apic_state_load_adapter_t;

/*
 * CPU-side LAPIC hooks identify a slot by index, not by local_apic_t.  Keep
 * the currently installed LAPIC device here so those hooks can update the
 * right MMIO mirror without exposing lapic_device_t outside this module.
 */
static lapic_device_t *lapic_active_device;
static lapic_index_source_t lapic_index_source;

static void                 lapic_device_init(lapic_device_t *dev, void *owner);
void                        lapic_device_set_count(lapic_device_t *dev, uint8_t count);
static uint8_t              lapic_device_count(const lapic_device_t *dev);
void                        lapic_device_set_count_config(lapic_device_t *dev,
                                                          uint8_t count);
static uint8_t              lapic_device_count_config(const lapic_device_t *dev);
static void                 lapic_device_ensure_count_config_min(lapic_device_t *dev,
                                                                 uint8_t count);
static void                 lapic_device_reset(lapic_device_t *dev, uint8_t count,
                                               lapic_reset_id_source_t id_source,
                                               void *id_priv);
static void                 lapic_device_set_active(lapic_device_t *dev);
static void                 lapic_device_clear_active(lapic_device_t *dev);
static lapic_state_t       *lapic_device_at(lapic_device_t *dev, uint8_t index);
static const lapic_state_t *lapic_device_at_const(const lapic_device_t *dev,
                                                  uint8_t index);
static uint8_t              lapic_device_current_index(lapic_device_t *dev);
lapic_state_t              *lapic_device_current(lapic_device_t *dev);
static const lapic_state_t *lapic_device_current_const(const lapic_device_t *dev);
static uint8_t              lapic_device_current_id(const lapic_device_t *dev);
static int                  lapic_device_current_base_enabled(const lapic_device_t *dev);
static int                  lapic_device_current_software_enabled(const lapic_device_t *dev);
static int                  lapic_device_current_interrupt_pending(const lapic_device_t *dev);
static int                  lapic_device_current_lint0_extint_enabled(
                                                    const lapic_device_t *dev);
static int                  lapic_device_current_lint_deliver(lapic_device_t *dev,
                                                             uint32_t lint_reg);
static int                  lapic_device_current_lvt_source_deliver(
                                                    lapic_device_t *dev,
                                                    uint32_t lvt_reg);
static int                  lapic_device_current_promote_irr(lapic_device_t *dev);
static int                  lapic_device_current_consume_extint(lapic_device_t *dev,
                                                               uint8_t *pin);
lapic_state_t              *lapic_device_bsp(lapic_device_t *dev);
static const lapic_state_t *lapic_device_bsp_const(const lapic_device_t *dev);
static int                  lapic_device_bsp_id(const lapic_device_t *dev, uint8_t *id);
static int                  lapic_device_id_at(const lapic_device_t *dev, uint8_t index,
                                               uint8_t *id);
static void                 lapic_device_add_timer_hosts(lapic_device_t *dev);
static void                 lapic_device_stop_timers(lapic_device_t *dev);
static void                 lapic_device_disable_mappings(lapic_device_t *dev);
static void                 lapic_device_add_mmio_mapping(lapic_device_t *dev);
static void                 lapic_device_update_mapping(lapic_device_t *dev);
static void                 lapic_device_set_log_handler(lapic_device_t *dev,
                                                         lapic_log_handler_t log);
static void                 lapic_device_set_icr_write_handler(
                                                    lapic_device_t *dev,
                                                    lapic_mmio_icr_write_handler_t icr_write);
static void                 lapic_device_set_external_ioapic(
                                                    lapic_device_t *dev,
                                                    void *external_ioapic,
                                                    lapic_external_eoi_handler_t external_eoi);
void                        lapic_device_reset_slot(lapic_device_t *dev, uint8_t index,
                                                   uint8_t id);
static int                  lapic_device_select_targets(lapic_device_t *dev, uint8_t dest,
                                                        int logical,
                                                        int physical_broadcast,
                                                        lapic_target_info_t *targets);
static int                  lapic_device_select_interruptible_targets(
                                                    lapic_device_t *dev, uint8_t dest,
                                                    int logical, int physical_broadcast,
                                                    int route_parked,
                                                    lapic_target_info_t *targets);
static void                 lapic_device_icr_write(lapic_device_t *dev, uint32_t val,
                                                   int route_parked,
                                                   lapic_icr_startup_handler_t startup_handler,
                                                   void *startup_priv);
void                         lapic_mmio_device_init(lapic_mmio_device_t *dev, void *owner);
void                         lapic_mmio_device_set_lapics(lapic_mmio_device_t *dev,
                                                          lapic_state_t *lapics);
void                         lapic_mmio_device_set_active_count(lapic_mmio_device_t *dev,
                                                                uint8_t active_count);
void                         lapic_mmio_device_set_current_index_source(
                                                    lapic_mmio_device_t *dev,
                                                    lapic_current_index_source_t source,
                                                    void *priv);
void                         lapic_mmio_device_set_external_ioapic(
                                                    lapic_mmio_device_t *dev,
                                                    void *external_ioapic,
                                                    lapic_external_eoi_handler_t external_eoi);
void                         lapic_mmio_device_set_log_handler(lapic_mmio_device_t *dev,
                                                               lapic_log_handler_t log);
void                         lapic_mmio_device_set_icr_write_handler(
                                                    lapic_mmio_device_t *dev,
                                                    lapic_mmio_icr_write_handler_t icr_write);
static void                  lapic_reset_regs(lapic_state_t *lapic);
lapic_state_t               *lapic_at(lapic_state_t *lapics, uint8_t count,
                                       uint8_t index);
const lapic_state_t         *lapic_at_const(const lapic_state_t *lapics,
                                            uint8_t count, uint8_t index);
uint8_t                      lapic_current_index(lapic_state_t *lapics, uint8_t count,
                                                 lapic_current_index_source_t source,
                                                 void *priv);
lapic_state_t               *lapic_current(lapic_state_t *lapics, uint8_t count,
                                           lapic_current_index_source_t source,
                                           void *priv);
const lapic_state_t         *lapic_current_const(const lapic_state_t *lapics,
                                                 uint8_t count,
                                                 lapic_current_index_source_t source,
                                                 void *priv);
lapic_state_t               *lapic_bsp(lapic_state_t *lapics, uint8_t count);
const lapic_state_t         *lapic_bsp_const(const lapic_state_t *lapics,
                                             uint8_t count);
uint8_t                      lapic_current_id(const lapic_state_t *lapic);
int                          lapic_base_enabled(const lapic_state_t *lapic);
int                          lapic_software_enabled(const lapic_state_t *lapic);
int                          lapic_interrupt_pending(const lapic_state_t *lapic);
int                          lapic_highest_deliverable_irr(const lapic_state_t *lapic);
int                          lapic_extint_deliverable(const lapic_state_t *lapic);
int                          lapic_lint0_extint_enabled(const lapic_state_t *lapic);
int                          lapic_lint_deliver(lapic_state_t *lapic, uint32_t lint_reg);
int                          lapic_promote_irr(lapic_state_t *lapic);
int                          lapic_consume_extint(lapic_state_t *lapic, uint8_t *pin);
void                         lapic_timer_host_stop(lapic_state_t *lapic);
void                         lapic_timer_stop(lapic_state_t *lapic);
void                         lapic_timer_restore_target(lapic_state_t *lapic,
                                                        uint32_t initial,
                                                        uint32_t current, int enabled);
void                         lapic_timer_callback(void *priv);
uint32_t                     lapic_mmio_base(const lapic_state_t *lapic);
void                         lapic_update_mapping(lapic_mmio_device_t *dev);
uint32_t                     lapic_readl(uint32_t addr, void *priv);
uint16_t                     lapic_readw(uint32_t addr, void *priv);
uint8_t                      lapic_readb(uint32_t addr, void *priv);
void                         lapic_writel(uint32_t addr, uint32_t val, void *priv);
void                         lapic_writew(uint32_t addr, uint16_t val, void *priv);
void                         lapic_writeb(uint32_t addr, uint8_t val, void *priv);
void                         lapic_reset_slot(lapic_state_t *lapic,
                                              lapic_mmio_device_t *mmio,
                                              uint8_t index, uint8_t id);
void                         lapic_icr_write(lapic_state_t *lapic,
                                             lapic_state_t *lapics, uint8_t count,
                                             uint8_t current_index, uint32_t val,
                                             int route_parked,
                                             lapic_icr_startup_handler_t startup_handler,
                                             void *startup_priv);

static void
lapic_device_init(lapic_device_t *dev, void *owner)
{
    if (dev == NULL)
        return;

    lapic_mmio_device_init(&dev->mmio, owner);
    lapic_mmio_device_set_lapics(&dev->mmio, dev->lapics);
    lapic_mmio_device_set_current_index_source(&dev->mmio,
                                               lapic_index_source_read, NULL);
    /* Timer and CPU mirror callbacks depend on the active-device bridge. */
    lapic_device_set_active(dev);
    lapic_device_add_timer_hosts(dev);
}

void
lapic_device_set_count(lapic_device_t *dev, uint8_t count)
{
    if (dev == NULL)
        return;

    dev->count = count;
    lapic_mmio_device_set_active_count(&dev->mmio, count);
}

static uint8_t
lapic_device_count(const lapic_device_t *dev)
{
    return (dev != NULL) ? dev->count : 0;
}

void
lapic_device_set_count_config(lapic_device_t *dev, uint8_t count)
{
    if (dev == NULL)
        return;

    dev->count_config = count;
}

static uint8_t
lapic_device_count_config(const lapic_device_t *dev)
{
    return (dev != NULL) ? dev->count_config : 0;
}

static void
lapic_device_ensure_count_config_min(lapic_device_t *dev, uint8_t count)
{
    if (dev == NULL)
        return;

    if (dev->count_config < count)
        dev->count_config = count;
}

static void
lapic_device_reset(lapic_device_t *dev, uint8_t count,
                   lapic_reset_id_source_t id_source, void *id_priv)
{
    if (dev == NULL)
        return;

    lapic_device_set_count(dev, count);
    /* Reset dormant slots too; count can grow later through config/load paths. */
    for (uint8_t i = 0; i < APIC_MAX_LAPICS; i++) {
        uint8_t id = (id_source != NULL) ? id_source(id_priv, i) : i;
        lapic_device_reset_slot(dev, i, id);
    }
}

static void
lapic_device_set_active(lapic_device_t *dev)
{
    lapic_active_device = dev;
}

static void
lapic_device_clear_active(lapic_device_t *dev)
{
    if (lapic_active_device == dev)
        lapic_active_device = NULL;
}

void
lapic_set_index_source(lapic_index_source_t source)
{
    lapic_index_source = source;
}

uint8_t
lapic_index_source_read(void *priv)
{
    (void) priv;

    return (lapic_index_source != NULL) ? lapic_index_source() : 0;
}

static lapic_device_t *
local_apic_device(local_apic_t *apic)
{
    return (apic != NULL) ? &apic->device : NULL;
}

static const lapic_device_t *
local_apic_device_const(const local_apic_t *apic)
{
    return (apic != NULL) ? &apic->device : NULL;
}

local_apic_t *
local_apic_create(void *owner)
{
    local_apic_t *apic = (local_apic_t *) calloc(1, sizeof(*apic));

    if (apic != NULL)
        local_apic_init(apic, owner);

    return apic;
}

void
local_apic_free(local_apic_t *apic)
{
    if (apic == NULL)
        return;

    local_apic_close(apic);
    free(apic);
}

local_apic_attachment_t *
local_apic_attachment_create_gated(local_apic_attachment_type_t type, void *owner,
                                   int enabled)
{
    local_apic_attachment_t *attachment;

    if (!enabled)
        return NULL;

    if ((type != LOCAL_APIC_ATTACHMENT_INTEGRATED_CPU) &&
        (type != LOCAL_APIC_ATTACHMENT_82489DX))
        return NULL;

    /*
     * Attachment owns the LAPIC allocation; owner remains caller-owned context
     * passed back through MMIO/log callbacks.
     */
    attachment = (local_apic_attachment_t *) calloc(1, sizeof(*attachment));
    if (attachment == NULL)
        return NULL;

    attachment->type = type;
    attachment->owner = owner;
    attachment->apic = local_apic_create(owner);
    if (attachment->apic == NULL) {
        free(attachment);
        return NULL;
    }

    return attachment;
}

local_apic_attachment_t *
local_apic_attachment_create(local_apic_attachment_type_t type, void *owner)
{
    return local_apic_attachment_create_gated(type, owner, 1);
}

void
local_apic_attachment_free(local_apic_attachment_t *attachment)
{
    if (attachment == NULL)
        return;

    local_apic_free(attachment->apic);
    free(attachment);
}

local_apic_t *
local_apic_attachment_apic(local_apic_attachment_t *attachment)
{
    return (attachment != NULL) ? attachment->apic : NULL;
}

void
local_apic_attachment_enable_mappings(local_apic_attachment_t *attachment)
{
    if (attachment == NULL)
        return;

    local_apic_enable_mappings(attachment->apic);
}

void
local_apic_init(local_apic_t *apic, void *owner)
{
    lapic_device_init(local_apic_device(apic), owner);
}

void
local_apic_set_count_config(local_apic_t *apic, uint8_t count)
{
    lapic_device_set_count_config(local_apic_device(apic), count);
}

uint8_t
local_apic_count_config(const local_apic_t *apic)
{
    return lapic_device_count_config(local_apic_device_const(apic));
}

uint8_t
local_apic_configured_count(const local_apic_t *apic, uint8_t limit)
{
    uint8_t count = local_apic_count_config(apic);

    if (!limit)
        limit = APIC_UP_LAPIC_COUNT;

    /* Unconfigured count means "use the platform limit", with UP as fallback. */
    if (!count || (count > limit))
        return limit;

    return count;
}

void
local_apic_ensure_count_config_min(local_apic_t *apic, uint8_t count)
{
    lapic_device_ensure_count_config_min(local_apic_device(apic), count);
}

void
local_apic_reset(local_apic_t *apic, uint8_t count,
                 lapic_reset_id_source_t id_source, void *id_priv)
{
    lapic_device_reset(local_apic_device(apic), count, id_source, id_priv);
}

uint8_t
local_apic_count(const local_apic_t *apic)
{
    return lapic_device_count(local_apic_device_const(apic));
}

uint8_t
local_apic_current_id(const local_apic_t *apic)
{
    return lapic_device_current_id(local_apic_device_const(apic));
}

int
local_apic_id_at(const local_apic_t *apic, uint8_t index, uint8_t *id)
{
    return lapic_device_id_at(local_apic_device_const(apic), index, id);
}

int
local_apic_bsp_id(const local_apic_t *apic, uint8_t *id)
{
    return lapic_device_bsp_id(local_apic_device_const(apic), id);
}

int
local_apic_current_base_enabled(const local_apic_t *apic)
{
    return lapic_device_current_base_enabled(local_apic_device_const(apic));
}

int
local_apic_current_software_enabled(const local_apic_t *apic)
{
    return lapic_device_current_software_enabled(local_apic_device_const(apic));
}

int
local_apic_current_external_pending(const local_apic_t *apic,
                                    local_apic_pic_pending_handler_t pic_pending,
                                    void *pic_priv)
{
    const lapic_device_t *dev = local_apic_device_const(apic);

    if (lapic_device_current_interrupt_pending(dev))
        return 1;

    /* ExtINT through LINT0 is consulted only after LAPIC IRR. */
    return lapic_device_current_lint0_extint_enabled(dev) &&
           (pic_pending != NULL) && pic_pending(pic_priv);
}

int
local_apic_current_external_vector(local_apic_t *apic,
                                   local_apic_pic_pending_handler_t pic_pending,
                                   local_apic_pic_interrupt_handler_t pic_interrupt,
                                   void *pic_priv)
{
    lapic_device_t *dev = local_apic_device(apic);
    lapic_state_t  *lapic;
    uint8_t         pin;
    int             vector;

    vector = lapic_device_current_promote_irr(dev);
    if (vector >= 0)
        return vector;

    /* PIC callbacks are the ownership boundary for ExtINT vector acquisition. */
    if (pic_interrupt == NULL)
        return -1;

    if (lapic_device_current_consume_extint(dev, &pin)) {
        lapic = lapic_device_current(dev);
        vector = pic_interrupt(pic_priv);
        lapic_log(lapic->mmio, "LAPIC: ExtINT pin=%u vector=%02x\n", pin, vector);
        return vector;
    }

    lapic = lapic_device_current(dev);
    if (lapic_lint0_extint_enabled(lapic) &&
        (pic_pending != NULL) && pic_pending(pic_priv)) {
        vector = pic_interrupt(pic_priv);
        lapic_log(lapic->mmio, "LAPIC: LINT0 ExtINT vector=%02x\n", vector);
        return vector;
    }

    return -1;
}

/* A board-wide line (the NMI pin) into the same LINT of every local
   APIC. Claimed if any of them took it. */
int
local_apic_lint_deliver_all(local_apic_t *apic, uint32_t lint_reg)
{
    lapic_device_t *dev     = local_apic_device(apic);
    int             claimed = 0;

    if (dev == NULL)
        return 0;

    for (uint8_t i = 0; i < dev->count; i++)
        claimed |= lapic_lint_deliver(lapic_at(dev->lapics, dev->count, i), lint_reg);

    return claimed;
}

int
local_apic_current_lint_deliver(local_apic_t *apic, uint32_t lint_reg)
{
    return lapic_device_current_lint_deliver(local_apic_device(apic), lint_reg);
}

int
local_apic_current_thermal_deliver(local_apic_t *apic)
{
    return lapic_device_current_lvt_source_deliver(local_apic_device(apic),
                                                   LAPIC_REG_LVT_THERMAL);
}

int
local_apic_current_perf_deliver(local_apic_t *apic)
{
    return lapic_device_current_lvt_source_deliver(local_apic_device(apic),
                                                   LAPIC_REG_LVT_PERF);
}

int
local_apic_select_interruptible_targets(local_apic_t *apic, uint8_t dest,
                                        int logical, int physical_broadcast,
                                        int route_parked,
                                        lapic_target_info_t *targets)
{
    return lapic_device_select_interruptible_targets(local_apic_device(apic), dest,
                                                     logical, physical_broadcast,
                                                     route_parked, targets);
}

apic_bus_result_t
local_apic_targets_accept_fixed(local_apic_t *apic,
                                const lapic_target_info_t *targets,
                                const apic_bus_message_t *message)
{
    (void) apic;

    return lapic_targets_accept_fixed(targets, message);
}

apic_bus_result_t
local_apic_targets_accept_lowest_priority(local_apic_t *apic,
                                          const lapic_target_info_t *targets,
                                          uint8_t *lowest_priority_next,
                                          int route_parked,
                                          const apic_bus_message_t *message)
{
    (void) apic;

    return lapic_targets_accept_lowest_priority(targets, lowest_priority_next,
                                                route_parked, message);
}

int
local_apic_targets_deliver_init(local_apic_t *apic,
                                const lapic_target_info_t *targets)
{
    (void) apic;

    return lapic_targets_deliver_init(targets);
}

void
local_apic_arbitration_id_reset_from_id(local_apic_t *apic)
{
    lapic_device_t *dev = local_apic_device(apic);

    if (dev == NULL)
        return;

    for (uint8_t i = 0; i < dev->count; i++)
        lapic_arbitration_id_reset_from_id(&dev->lapics[i]);
}

void
local_apic_arbitration_rotate_with_ioapic(local_apic_t *apic,
                                          uint8_t *ioapic_arb_id)
{
    lapic_device_t *dev = local_apic_device(apic);

    if (dev == NULL)
        return;

    lapic_arbitration_rotate_with_ioapic(dev->lapics, dev->count,
                                         ioapic_arb_id);
}

int
local_apic_targets_mark_extint(local_apic_t *apic,
                                     const lapic_target_info_t *targets,
                                     uint8_t pin)
{
    (void) apic;

    return lapic_targets_mark_extint(targets, pin);
}

void
local_apic_icr_write(local_apic_t *apic, uint32_t val, int route_parked,
                     lapic_icr_startup_handler_t startup_handler, void *startup_priv)
{
    lapic_device_icr_write(local_apic_device(apic), val, route_parked,
                           startup_handler, startup_priv);
}

void
local_apic_set_log_handler(local_apic_t *apic, lapic_log_handler_t log)
{
    lapic_device_set_log_handler(local_apic_device(apic), log);
}

void
local_apic_set_icr_write_handler(local_apic_t *apic,
                                 lapic_mmio_icr_write_handler_t icr_write)
{
    lapic_device_set_icr_write_handler(local_apic_device(apic), icr_write);
}

void
local_apic_set_external_ioapic(local_apic_t *apic,
                               void *external_ioapic,
                               lapic_external_eoi_handler_t external_eoi)
{
    lapic_device_set_external_ioapic(local_apic_device(apic), external_ioapic,
                                     external_eoi);
}

void
local_apic_enable_mappings(local_apic_t *apic)
{
    lapic_device_add_mmio_mapping(local_apic_device(apic));
}

void
local_apic_update_mapping(local_apic_t *apic)
{
    lapic_device_update_mapping(local_apic_device(apic));
}

void
local_apic_stop_timers(local_apic_t *apic)
{
    lapic_device_stop_timers(local_apic_device(apic));
}

void
local_apic_disable_mappings(local_apic_t *apic)
{
    lapic_device_disable_mappings(local_apic_device(apic));
}

void
local_apic_close(local_apic_t *apic)
{
    lapic_device_t *dev = local_apic_device(apic);

    local_apic_stop_timers(apic);
    lapic_device_disable_mappings(dev);
    lapic_device_clear_active(dev);
}

static lapic_state_t *
lapic_active_at(uint8_t index)
{
    if (lapic_active_device == NULL)
        return NULL;

    /* Index callbacks from CPU helpers route only through the active device. */
    return lapic_device_at(lapic_active_device, index);
}

static lapic_state_t *
lapic_device_at(lapic_device_t *dev, uint8_t index)
{
    return (dev != NULL) ? lapic_at(dev->lapics, dev->count, index) : NULL;
}

static const lapic_state_t *
lapic_device_at_const(const lapic_device_t *dev, uint8_t index)
{
    return (dev != NULL) ? lapic_at_const(dev->lapics, dev->count, index) : NULL;
}

static uint8_t
lapic_device_current_index(lapic_device_t *dev)
{
    if (dev == NULL)
        return 0;

    return lapic_current_index(dev->lapics, dev->count, lapic_index_source_read, NULL);
}

lapic_state_t *
lapic_device_current(lapic_device_t *dev)
{
    return (dev != NULL) ? lapic_current(dev->lapics, dev->count,
                                         lapic_index_source_read, NULL) : NULL;
}

static const lapic_state_t *
lapic_device_current_const(const lapic_device_t *dev)
{
    return (dev != NULL) ? lapic_current_const(dev->lapics, dev->count,
                                               lapic_index_source_read, NULL) : NULL;
}

static uint8_t
lapic_device_current_id(const lapic_device_t *dev)
{
    const lapic_state_t *lapic = lapic_device_current_const(dev);

    return (lapic != NULL) ? lapic_current_id(lapic) : 0xff;
}

static int
lapic_device_current_base_enabled(const lapic_device_t *dev)
{
    return lapic_base_enabled(lapic_device_current_const(dev));
}

static int
lapic_device_current_software_enabled(const lapic_device_t *dev)
{
    return lapic_software_enabled(lapic_device_current_const(dev));
}

static int
lapic_device_current_interrupt_pending(const lapic_device_t *dev)
{
    return lapic_interrupt_pending(lapic_device_current_const(dev));
}

static int
lapic_device_current_lint0_extint_enabled(const lapic_device_t *dev)
{
    return lapic_lint0_extint_enabled(lapic_device_current_const(dev));
}

/* LINT1 on processor index sampled again: a level-triggered entry whose
   EOI found the NMI line still up (cpu_lapic_lint_remote_irr_eoi). */
void
lapic_lint1_redeliver_index(uint8_t index)
{
    if (lapic_active_device != NULL)
        (void) lapic_lint_deliver(lapic_device_at(lapic_active_device, index), LAPIC_REG_LVT_LINT1);
}

/* The IPIs a target had no slot for, sent again from their senders when a
   slot comes free (lapic_slot_freed, lapic_icr_write). */
void
lapic_icr_retry_all(void)
{
    lapic_device_t *dev = lapic_active_device;

    if (dev == NULL)
        return;
    for (uint8_t i = 0; (i < dev->count) && (i < APIC_MAX_LAPICS); i++) {
        if (!lapic_icr_retry_pending[i])
            continue;
        lapic_icr_retry_pending[i] = 0;
        lapic_icr_write(&dev->lapics[i], dev->lapics, dev->count, i, lapic_icr_retry_val[i],
                        lapic_icr_retry_route_parked[i], NULL, NULL);
    }
}

/* A local APIC has room again: an ISR bit retired (EOI), an IRR bit taken
   into the ISR (a vector already pending finds no slot,
   cpu_lapic_slot_free_index), or the APIC reset by an INIT. "All bus
   messages failing during delivery are automatically retried" (Pentium
   Pro Vol. 3, 7.4.4): the sender puts a refused message on the bus again
   and again until it is taken, and these are the only moments one can
   be. So what every local APIC and I/O APIC was refused goes again here. */
void
lapic_slot_freed(void)
{
    lapic_icr_retry_all();
    ioapic_retry_deliveries();
}

static int
lapic_device_current_lint_deliver(lapic_device_t *dev, uint32_t lint_reg)
{
    return lapic_lint_deliver(lapic_device_current(dev), lint_reg);
}

static int
lapic_device_current_lvt_source_deliver(lapic_device_t *dev, uint32_t lvt_reg)
{
    return lapic_lvt_source_deliver(lapic_device_current(dev), lvt_reg);
}

static int
lapic_device_current_promote_irr(lapic_device_t *dev)
{
    return lapic_promote_irr(lapic_device_current(dev));
}

static int
lapic_device_current_consume_extint(lapic_device_t *dev, uint8_t *pin)
{
    return lapic_consume_extint(lapic_device_current(dev), pin);
}

lapic_state_t *
lapic_device_bsp(lapic_device_t *dev)
{
    return (dev != NULL) ? lapic_bsp(dev->lapics, dev->count) : NULL;
}

static const lapic_state_t *
lapic_device_bsp_const(const lapic_device_t *dev)
{
    return (dev != NULL) ? lapic_bsp_const(dev->lapics, dev->count) : NULL;
}

static int
lapic_device_bsp_id(const lapic_device_t *dev, uint8_t *id)
{
    const lapic_state_t *lapic = lapic_device_bsp_const(dev);

    if ((lapic == NULL) || (id == NULL))
        return 0;

    *id = lapic->id;
    return 1;
}

static int
lapic_device_id_at(const lapic_device_t *dev, uint8_t index, uint8_t *id)
{
    const lapic_state_t *lapic = lapic_device_at_const(dev, index);

    if ((lapic == NULL) || (id == NULL))
        return 0;

    *id = lapic->id;
    return 1;
}

static void
lapic_device_add_timer_hosts(lapic_device_t *dev)
{
    if (dev == NULL)
        return;

    /* Host timer slots are fixed by LAPIC index, independent of active count. */
    for (uint8_t i = 0; i < APIC_MAX_LAPICS; i++)
        cpu_lapic_timer_host_add_index(i, lapic_timer_callback, &dev->lapics[i]);
}

static void
lapic_device_stop_timers(lapic_device_t *dev)
{
    if (dev == NULL)
        return;

    for (uint8_t i = 0; i < APIC_MAX_LAPICS; i++)
        lapic_timer_host_stop(&dev->lapics[i]);
}

static void
lapic_device_disable_mappings(lapic_device_t *dev)
{
    if (dev == NULL)
        return;

    for (uint8_t i = 0; i < APIC_MAX_LAPICS; i++) {
        if (dev->lapics[i].mapping_added)
            mem_mapping_disable(&dev->lapics[i].mapping);
    }
}

static void
lapic_device_add_mmio_mapping(lapic_device_t *dev)
{
    if (dev == NULL)
        return;

    for (uint8_t i = 0; i < APIC_MAX_LAPICS; i++) {
        lapic_state_t *lapic = &dev->lapics[i];

        if (lapic->mapping_added)
            continue;

        /*
         * Add each possible MMIO window once, then leave exposure to
         * lapic_update_mapping() so APIC_BASE and enable state stay authoritative.
         * The window is the processor's own: on a P6 "the APIC handles all
         * memory accesses to addresses within the 4-KByte APIC register
         * space and no external bus cycles are produced" (Intel
         * Architecture Vol. 3, 243192, 7.5.7). A bus master's access at
         * that address reaches no APIC (MEM_MAPPING_CPU_ONLY).
         */
        mem_mapping_add(&lapic->mapping, lapic_mmio_base(lapic), LAPIC_MMIO_SIZE,
                        lapic_readb, lapic_readw, lapic_readl,
                        lapic_writeb, lapic_writew, lapic_writel,
                        NULL, MEM_MAPPING_EXTERNAL | MEM_MAPPING_CPU_ONLY, &dev->mmio);
        lapic->mapping_added = 1;
        mem_mapping_disable(&lapic->mapping);
    }

    lapic_device_update_mapping(dev);
}

static void
lapic_device_update_mapping(lapic_device_t *dev)
{
    if (dev == NULL)
        return;

    lapic_update_mapping(&dev->mmio);
}

static void
lapic_device_set_log_handler(lapic_device_t *dev, lapic_log_handler_t log)
{
    if (dev == NULL)
        return;

    lapic_mmio_device_set_log_handler(&dev->mmio, log);
}

static void
lapic_device_set_icr_write_handler(lapic_device_t *dev,
                                   lapic_mmio_icr_write_handler_t icr_write)
{
    if (dev == NULL)
        return;

    lapic_mmio_device_set_icr_write_handler(&dev->mmio, icr_write);
}

static void
lapic_device_set_external_ioapic(lapic_device_t *dev, void *external_ioapic,
                                 lapic_external_eoi_handler_t external_eoi)
{
    if (dev == NULL)
        return;

    lapic_mmio_device_set_external_ioapic(&dev->mmio, external_ioapic, external_eoi);
}

void
lapic_device_reset_slot(lapic_device_t *dev, uint8_t index, uint8_t id)
{
    if ((dev == NULL) || (index >= APIC_MAX_LAPICS))
        return;

    lapic_reset_slot(&dev->lapics[index], &dev->mmio, index, id);
}

static int
lapic_device_select_targets(lapic_device_t *dev, uint8_t dest, int logical,
                            int physical_broadcast, lapic_target_info_t *targets)
{
    if ((dev == NULL) || (targets == NULL))
        return 0;

    return lapic_select_targets(dev->lapics, dev->count, dest, logical,
                                physical_broadcast, targets);
}

static int
lapic_device_select_interruptible_targets(lapic_device_t *dev, uint8_t dest,
                                          int logical, int physical_broadcast,
                                          int route_parked,
                                          lapic_target_info_t *targets)
{
    int count;

    count = lapic_device_select_targets(dev, dest, logical, physical_broadcast, targets);
    if (!count)
        return 0;

    lapic_targets_keep_interruptible(targets, route_parked);
    return targets->count;
}

static void
lapic_device_icr_write(lapic_device_t *dev, uint32_t val, int route_parked,
                       lapic_icr_startup_handler_t startup_handler,
                       void *startup_priv)
{
    if (dev == NULL)
        return;

    lapic_icr_write(lapic_device_current(dev), dev->lapics, dev->count,
                    lapic_device_current_index(dev), val, route_parked,
                    startup_handler, startup_priv);
}

lapic_state_t *
lapic_at(lapic_state_t *lapics, uint8_t count, uint8_t index)
{
    if ((lapics == NULL) || (index >= count))
        return NULL;

    return (index < APIC_MAX_LAPICS) ? &lapics[index] : NULL;
}

const lapic_state_t *
lapic_at_const(const lapic_state_t *lapics, uint8_t count, uint8_t index)
{
    if ((lapics == NULL) || (index >= count))
        return NULL;

    return (index < APIC_MAX_LAPICS) ? &lapics[index] : NULL;
}

uint8_t
lapic_current_index(lapic_state_t *lapics, uint8_t count,
                    lapic_current_index_source_t source, void *priv)
{
    uint8_t index;

    if ((lapics == NULL) || (source == NULL))
        return 0;

    index = source(priv);
    if (index >= count)
        return 0;

    if (index >= APIC_MAX_LAPICS)
        return 0;

    /* Not-started AP slots fall back to BSP for MMIO/callback safety. */
    if (!lapics[index].started)
        return 0;

    return index;
}

lapic_state_t *
lapic_current(lapic_state_t *lapics, uint8_t count,
              lapic_current_index_source_t source, void *priv)
{
    return lapic_at(lapics, count, lapic_current_index(lapics, count, source, priv));
}

const lapic_state_t *
lapic_current_const(const lapic_state_t *lapics, uint8_t count,
                    lapic_current_index_source_t source, void *priv)
{
    return lapic_at_const(lapics, count,
                          lapic_current_index((lapic_state_t *) lapics, count, source, priv));
}

lapic_state_t *
lapic_bsp(lapic_state_t *lapics, uint8_t count)
{
    return lapic_at(lapics, count, 0);
}

const lapic_state_t *
lapic_bsp_const(const lapic_state_t *lapics, uint8_t count)
{
    return lapic_at_const(lapics, count, 0);
}

void
lapic_sync_ppr(lapic_state_t *lapic)
{
    if (lapic == NULL)
        return;

    cpu_lapic_ppr_update_index(lapic->index);
}

uint32_t
lapic_timer_current(const lapic_state_t *lapic)
{
    return cpu_lapic_timer_current_read_index(lapic->index);
}

void
lapic_sync_int_regs(lapic_state_t *lapic)
{
    uint32_t *irr;
    uint32_t *isr;
    uint32_t *tmr;

    if (lapic == NULL)
        return;

    irr = cpu_lapic_irr_words_index(lapic->index);
    isr = cpu_lapic_isr_words_index(lapic->index);
    tmr = cpu_lapic_tmr_words_index(lapic->index);

    /* Interrupt state is CPU-owned, and register reads are served from it
       (cpu_lapic_mmio_read_index); regs[] mirrors it for save states. */
    for (uint8_t i = 0; i < 8; i++) {
        lapic_mirror_write_reg(lapic, LAPIC_REG_ISR_BASE + (((uint32_t) i) << 4), isr[i]);
        lapic_mirror_write_reg(lapic, LAPIC_REG_TMR_BASE + (((uint32_t) i) << 4), tmr[i]);
        lapic_mirror_write_reg(lapic, LAPIC_REG_IRR_BASE + (((uint32_t) i) << 4), irr[i]);
    }
    lapic_sync_ppr(lapic);
    lapic_mirror_write_reg(lapic, LAPIC_REG_CURRENT_CNT, lapic_timer_current(lapic));
}

void
lapic_sync_cpu_owned_regs(lapic_state_t *lapic)
{
    if (lapic == NULL)
        return;

    cpu_lapic_mmio_sync_mirror_index(lapic->index, lapic->regs);
}

void
lapic_load_cpu_owned_regs(lapic_state_t *lapic)
{
    if (lapic == NULL)
        return;

    /* Restore path pushes saved MMIO mirror values back into CPU-owned storage. */
    cpu_lapic_mmio_load_mirror_index(lapic->index, lapic->regs);
}

void
lapic_reg_sync(lapic_state_t *lapic, uint32_t reg, uint32_t val)
{
    lapic_mirror_write_reg(lapic, reg, val);
}

void
lapic_id_sync_index(uint8_t lapic_index, uint8_t id)
{
    lapic_state_t *lapic = lapic_active_at(lapic_index);

    if (lapic == NULL)
        return;

    lapic->id = id;
    lapic_mirror_write_reg(lapic, LAPIC_REG_ID, ((uint32_t) id) << 24);
}

void
lapic_svr_sync_index(uint8_t lapic_index, uint32_t svr)
{
    lapic_reg_sync(lapic_active_at(lapic_index), LAPIC_REG_SVR, svr);
}

void
lapic_tpr_sync_index(uint8_t lapic_index, uint32_t tpr)
{
    lapic_state_t *lapic = lapic_active_at(lapic_index);

    lapic_reg_sync(lapic, LAPIC_REG_TPR, tpr);
    lapic_sync_ppr(lapic);
}

void
lapic_ldr_sync_index(uint8_t lapic_index, uint32_t ldr)
{
    lapic_reg_sync(lapic_active_at(lapic_index), LAPIC_REG_LDR, ldr);
}

void
lapic_dfr_sync_index(uint8_t lapic_index, uint32_t dfr)
{
    lapic_reg_sync(lapic_active_at(lapic_index), LAPIC_REG_DFR, dfr);
}

void
lapic_version_sync_index(uint8_t lapic_index, uint32_t version)
{
    lapic_reg_sync(lapic_active_at(lapic_index), LAPIC_REG_VERSION, version);
}

void
lapic_arb_id_sync_index(uint8_t lapic_index, uint32_t apr)
{
    lapic_reg_sync(lapic_active_at(lapic_index), LAPIC_REG_APR, apr);
}

void
lapic_ppr_sync_index(uint8_t lapic_index, uint32_t ppr)
{
    lapic_reg_sync(lapic_active_at(lapic_index), LAPIC_REG_PPR, ppr);
}

void
lapic_esr_sync_index(uint8_t lapic_index, uint32_t esr)
{
    lapic_reg_sync(lapic_active_at(lapic_index), LAPIC_REG_ESR, esr);
}

void
lapic_icr_lo_sync_index(uint8_t lapic_index, uint32_t icr_lo)
{
    lapic_reg_sync(lapic_active_at(lapic_index), LAPIC_REG_ICR_LO, icr_lo);
}

void
lapic_icr_hi_sync_index(uint8_t lapic_index, uint32_t icr_hi)
{
    lapic_reg_sync(lapic_active_at(lapic_index), LAPIC_REG_ICR_HI, icr_hi);
}

void
lapic_rrr_sync_index(uint8_t lapic_index, uint32_t rrr)
{
    lapic_reg_sync(lapic_active_at(lapic_index), LAPIC_REG_RRR, rrr);
}

static void
lapic_lvt_sync_index(uint8_t lapic_index, uint32_t reg, uint32_t lvt)
{
    lapic_reg_sync(lapic_active_at(lapic_index), reg, lvt);
}

void
lapic_lvt_timer_sync_index(uint8_t lapic_index, uint32_t lvt)
{
    lapic_lvt_sync_index(lapic_index, LAPIC_REG_LVT_TIMER, lvt);
}

void
lapic_lvt_thermal_sync_index(uint8_t lapic_index, uint32_t lvt)
{
    lapic_lvt_sync_index(lapic_index, LAPIC_REG_LVT_THERMAL, lvt);
}

void
lapic_lvt_perf_sync_index(uint8_t lapic_index, uint32_t lvt)
{
    lapic_lvt_sync_index(lapic_index, LAPIC_REG_LVT_PERF, lvt);
}

void
lapic_lvt_lint0_sync_index(uint8_t lapic_index, uint32_t lvt)
{
    lapic_lvt_sync_index(lapic_index, LAPIC_REG_LVT_LINT0, lvt);
}

void
lapic_lvt_lint1_sync_index(uint8_t lapic_index, uint32_t lvt)
{
    lapic_lvt_sync_index(lapic_index, LAPIC_REG_LVT_LINT1, lvt);
}

void
lapic_lvt_error_sync_index(uint8_t lapic_index, uint32_t lvt)
{
    lapic_lvt_sync_index(lapic_index, LAPIC_REG_LVT_ERROR, lvt);
}

void
lapic_timer_initial_sync_index(uint8_t lapic_index, uint32_t initial)
{
    lapic_reg_sync(lapic_active_at(lapic_index), LAPIC_REG_INITIAL_CNT, initial);
}

void
lapic_timer_divide_cfg_sync_index(uint8_t lapic_index, uint32_t divide_cfg)
{
    lapic_reg_sync(lapic_active_at(lapic_index), LAPIC_REG_DIVIDE_CFG, divide_cfg & 0x0b);
}

void
lapic_apic_base_msr_sync_index(uint8_t lapic_index, uint64_t val)
{
    lapic_state_t *lapic = lapic_active_at(lapic_index);

    if (lapic == NULL)
        return;

    lapic->base_msr = (val & APIC_BASE_ADDR_MASK) |
                      (val & APIC_BASE_ENABLE) |
                      (val & APIC_BASE_BSP);
    /* Base MSR writes can move or hide the MMIO window. */
    lapic_update_mapping(&lapic_active_device->mmio);
}

uint8_t
lapic_current_id(const lapic_state_t *lapic)
{
    return cpu_lapic_id_read_index(lapic->index);
}

int
lapic_base_enabled(const lapic_state_t *lapic)
{
    return (lapic != NULL) && cpu_lapic_base_enabled_index(lapic->index);
}

int
lapic_software_enabled(const lapic_state_t *lapic)
{
    return (lapic != NULL) && cpu_lapic_software_enabled_index(lapic->index);
}

int
lapic_enabled(const lapic_state_t *lapic)
{
    return (lapic != NULL) && cpu_lapic_enabled_index(lapic->index, lapic->started);
}

/* An INIT message accepted (an IPI, or an I/O APIC entry): "INIT has the
   same effect on the Local APIC as the power-up Reset, except that the
   APIC ID and Arb ID registers are not affected" (Pentium Vol. 3,
   19.3.1.11.3). A processor other than the first goes to wait for a
   STARTUP. */
void
lapic_init_signal(lapic_state_t *lapic)
{
    if (lapic == NULL)
        return;

    lapic->init_asserted  = 0;
    lapic->extint_pending = 0;
    lapic->extint_pin     = 0;
    cpu_lapic_extint_live_set(lapic->index, 0);
    lapic_reset_regs(lapic);
    if (!lapic->is_bsp) {
        lapic->started       = 0;
        lapic->wait_for_sipi = 1;
        lapic->sipi_vector   = 0;
        lapic->parked        = 0;
    }
    /* Its IRR and ISR are empty now: what others were refused by it goes
       again, and meets it as it stands after the INIT. */
    lapic_slot_freed();
}

/* A message this local APIC put on the bus other than an IPI (an EOI to
   the I/O APIC): the arbitration IDs move as for any. */
void
lapic_bus_sent_by(lapic_state_t *lapic)
{
    if ((lapic_active_device == NULL) || (lapic == NULL))
        return;

    lapic_arbitration_rotate_after_lapic(lapic_active_device->lapics, lapic_active_device->count,
                                         lapic->index);
}

uint64_t
lapic_reset_base_msr(uint8_t index)
{
    if (index == 0)
        return APIC_BASE_DEFAULT;

    return APIC_BASE_DEFAULT & ~APIC_BASE_BSP;
}

void
lapic_reset_startup_state(lapic_state_t *lapic, uint8_t index)
{
    lapic->is_bsp = (index == 0);
    lapic->started = lapic->is_bsp;
    lapic->init_asserted = 0;
    /* AP slots start stopped and waiting for SIPI; slot 0 is the BSP boundary. */
    lapic->wait_for_sipi = !lapic->is_bsp;
    lapic->sipi_vector = 0;
    lapic->parked = 0;
}

static void
lapic_reset_logs(lapic_state_t *lapic)
{
    memset(lapic->accept_log_count, 0, sizeof(lapic->accept_log_count));
    memset(lapic->promote_log_count, 0, sizeof(lapic->promote_log_count));
    memset(lapic->eoi_log_count, 0, sizeof(lapic->eoi_log_count));
    memset(lapic->icr_log_count, 0, sizeof(lapic->icr_log_count));
    lapic->lint_init_log_count = 0;
    memset(lapic->read_log_count, 0, sizeof(lapic->read_log_count));
    memset(lapic->write_log_count, 0, sizeof(lapic->write_log_count));
}

static void
lapic_reset_regs(lapic_state_t *lapic)
{
    memset(lapic->regs, 0, sizeof(lapic->regs));
    /* The ICR is cleared, and with it the message it was still sending. */
    if (lapic->index < APIC_MAX_LAPICS)
        lapic_icr_retry_pending[lapic->index] = 0;
    cpu_lapic_reset_index(lapic->index, lapic->id, lapic->base_msr);
    cpu_lapic_irr_reset_index(lapic->index);
    cpu_lapic_isr_reset_index(lapic->index);
    cpu_lapic_tmr_reset_index(lapic->index);
    lapic_timer_stop(lapic);

    /* CPU helpers own side effects; mirror sync makes regs[] match reset state. */
    lapic_sync_cpu_owned_regs(lapic);
    lapic_sync_int_regs(lapic);
}

void
lapic_reset_slot(lapic_state_t *lapic, lapic_mmio_device_t *mmio, uint8_t index, uint8_t id)
{
    if (lapic == NULL)
        return;

    lapic->index = index;
    lapic->mmio = mmio;
    lapic->id = id;
    lapic->base_msr = lapic_reset_base_msr(index);
    lapic->extint_pending = 0;
    lapic->extint_pin = 0;
    cpu_lapic_extint_live_set(index, 0);
    lapic_reset_startup_state(lapic, index);
    lapic_reset_logs(lapic);
    lapic_reset_regs(lapic);
    lapic_arbitration_id_reset_from_id(lapic);
}

/* The processor's RESET or INIT pin: its local APIC goes through INIT --
   its ID kept, the Arb ID from it, nothing pending. The vendor reset sets
   the registers on RESET, but not what the APIC holds beside them: an
   ExtINT marked, an IPI it was still sending. */
void
lapic_index_board_reset(uint8_t index)
{
    lapic_state_t *lapic;

    if (lapic_active_device == NULL)
        return;

    lapic = lapic_device_at(lapic_active_device, index);
    if (lapic == NULL)
        return;

    lapic->extint_pending = 0;
    lapic->extint_pin     = 0;
    cpu_lapic_extint_live_set(index, 0);
    lapic_reset_regs(lapic);
    lapic_arbitration_id_reset_from_id(lapic);
    lapic_reset_startup_state(lapic, index);
}

void
lapic_timer_host_stop(lapic_state_t *lapic)
{
    cpu_lapic_timer_host_stop_index(lapic->index);
}

void
lapic_timer_stop(lapic_state_t *lapic)
{
    lapic_timer_host_stop(lapic);
    cpu_lapic_timer_stop_index(lapic->index);
    lapic_mirror_write_reg(lapic, LAPIC_REG_CURRENT_CNT, 0);
}

static void
lapic_timer_schedule(lapic_state_t *lapic, uint32_t count, int in_callback)
{
    cpu_lapic_timer_host_schedule_index(lapic->index, count, in_callback);
}

void
lapic_timer_arm(lapic_mmio_device_t *dev, uint32_t count)
{
    lapic_state_t *lapic;

    if (dev == NULL)
        return;

    lapic = lapic_current(dev->lapics, dev->active_count,
                          dev->current_index, dev->current_index_priv);
    if (lapic == NULL)
        return;

    lapic_timer_host_stop(lapic);
    cpu_lapic_timer_arm_index(lapic->index, count);

    if (!count) {
        lapic_mirror_write_reg(lapic, LAPIC_REG_CURRENT_CNT, 0);
        return;
    }

    lapic_mirror_write_reg(lapic, LAPIC_REG_CURRENT_CNT, count);
    lapic_timer_schedule(lapic, count, 0);

    lapic_log(dev, "LAPIC: timer arm count=%08x divisor=%u lvt=%08x\n",
              count, cpu_lapic_timer_divisor_read_index(lapic->index),
              cpu_lapic_lvt_timer_read_index(lapic->index));
}

void
lapic_timer_restore_target(lapic_state_t *lapic, uint32_t initial, uint32_t current, int enabled)
{
    lapic_timer_host_stop(lapic);
    current = cpu_lapic_timer_restore_index(lapic->index, initial, current, enabled);
    if (!current) {
        lapic_mirror_write_reg(lapic, LAPIC_REG_CURRENT_CNT, 0);
        return;
    }

    lapic_mirror_write_reg(lapic, LAPIC_REG_CURRENT_CNT, current);
    lapic_timer_schedule(lapic, current, 0);
}

void
lapic_timer_callback(void *priv)
{
    lapic_state_t *lapic = (lapic_state_t *) priv;
    uint32_t       lvt;
    uint8_t        vector = 0;
    uint32_t       current;
    int            accepted;

    if (lapic == NULL)
        return;

    lvt     = cpu_lapic_lvt_timer_read_index(lapic->index);
    current = lapic_timer_current(lapic);

    cpu_lapic_timer_unschedule_index(lapic->index);

    /* Host callback can arrive before modeled count reaches zero; do not inject. */
    if (current) {
        lapic_timer_schedule(lapic, current, 1);
        return;
    }

    accepted = cpu_lapic_timer_fire_index(lapic->index, lapic->started, &vector);
    if (accepted < 0) {
        lapic_error(lapic, LAPIC_ESR_RECV_ILLEGAL_VECTOR);
        lapic_log(lapic->mmio, "APIC-WARN: LAPIC rejected invalid vector=%02x\n", vector);
    } else if (accepted && lapic_log_first(&lapic->accept_log_count[vector], 8)) {
        lapic_log(lapic->mmio, "LAPIC: accepted vector=%02x trigger=edge count=%u\n",
                  vector, lapic->accept_log_count[vector]);
    }

    if ((lvt & LAPIC_LVT_TIMER_MODE) == LAPIC_LVT_TIMER_PERIODIC) {
        uint32_t count = cpu_lapic_timer_periodic_restart_index(lapic->index);

        /* CPU timer helper owns periodic reload; this layer owns host scheduling. */
        if (count)
            lapic_timer_schedule(lapic, count, 1);
    } else {
        cpu_lapic_timer_oneshot_complete_index(lapic->index);
    }
}
