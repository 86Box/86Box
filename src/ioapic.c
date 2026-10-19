/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          Skeleton I/O APIC implementation, currently housing the MPS
 *          table patcher for machines that require it.
 *
 * Authors: RichardG, <richardg867@gmail.com>
 *
 *          Copyright 2020 RichardG.
 */
#include <stdarg.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <stdlib.h>
#define HAVE_STDARG_H
#include <86box/86box.h>
#include <86box/io.h>
#include <86box/device.h>
#include <86box/machine.h>
#include <86box/mem.h>
#include <86box/chipset.h>
#include <86box/pic.h>
#include <86box/pci.h>
#include <86box/plat_unused.h>
#include "cpu.h"
#include "apic_platform.h"
#include "ioapic_82093aa.h"
#include "lapic.h"

typedef struct ioapic_t {
    /* Config/state gates stay here; apic_platform owns LAPIC topology effects. */
    uint8_t expose_mp_tables;
    uint8_t uniprocessor_apic;
    uint8_t multi_lapic;
    uint8_t multi_lapic_mp_cpus;
    uint8_t ap_startup;
    uint8_t second_cpu_card;
    uint8_t ap_safe_park;
    uint8_t ap_route_parked;
    uint8_t smp_probe;
    uint8_t topology_lapic_slots_max;
    uint8_t mp_table_logged;
    uint8_t mp_table_skip_logged;
    uint8_t pci_level_replay_enabled;
    uint8_t post_trigger; /* the POST code that means "about to boot" */

    uint8_t       lowest_priority_next;
    apic_platform_t *apic;
    ioapic_smiout_handler_t smiout_handler;
    void                   *smiout_priv;

    uint8_t       ioapic_id;
    uint8_t       ioapic_index;
    mem_mapping_t ioapic_mapping;
    uint32_t      ioapic_redir[24 * 2];
    uint32_t      ioapic_pin_raw_lines;
    uint32_t      ioapic_pin_lines;
    uint32_t      ioapic_pin_level;
    uint8_t       ioapic_delivery_pending_valid;
    uint8_t       ioapic_delivery_pending_pin;
    uint8_t       ioapic_intin_poll_next;
    uint8_t       ioapic_smiout_asserted;
    uint8_t       ioapic_arb_id;
    uint8_t       legacy_irq_default_to_pin[IOAPIC_LEGACY_IRQ_COUNT];
    uint8_t       legacy_irq_to_pin[IOAPIC_LEGACY_IRQ_COUNT];
    uint8_t       ioapic_masked_irq_log_count[24];
    uint8_t       ioapic_irq_log_count[24];
    uint8_t       ioapic_deliver_log_count[24];
    uint8_t       ioapic_dest_mismatch_log_count[24];
    uint8_t       ioapic_extint_defer_log_count[24];
    uint8_t       ioapic_extint_deliver_log_count[24];
    uint8_t       ioapic_smi_vector_log_count[24];
    uint8_t       ioapic_eoi_log_count[24];
    uint8_t       ioapic_redir_log_count[24];
    uint8_t       ioapic_mmio_read_log_count[0x40];
    uint8_t       ioapic_mmio_write_log_count[0x40];
} ioapic_t;

#define IOAPIC_MMIO_BASE       0xfec00000
#define IOAPIC_MMIO_SIZE       0x00001000
#define IOAPIC_REDIR_ENTRIES   IOAPIC_82093AA_REDIR_ENTRIES
#define IOAPIC_SMI_PIN         23
#define IOAPIC_ID_RESET        0
#define IOAPIC_VERSION_REG     IOAPIC_82093AA_VERSION_REG
#define IOAPIC_PIN_MASK        IOAPIC_82093AA_PIN_MASK

#define IOAPIC_REDIR_VECTOR    IOAPIC_82093AA_REDIR_VECTOR
#define IOAPIC_REDIR_DELIV     IOAPIC_82093AA_REDIR_DELIV
#define IOAPIC_REDIR_DESTMOD   IOAPIC_82093AA_REDIR_DESTMOD
#define IOAPIC_REDIR_STATUS    IOAPIC_82093AA_REDIR_STATUS
#define IOAPIC_REDIR_POLARITY  IOAPIC_82093AA_REDIR_POLARITY
#define IOAPIC_REDIR_REMOTEIRR IOAPIC_82093AA_REDIR_REMOTEIRR
#define IOAPIC_REDIR_TRIGGER   IOAPIC_82093AA_REDIR_TRIGGER
#define IOAPIC_REDIR_MASK      IOAPIC_82093AA_REDIR_MASK
#define IOAPIC_REDIR_LOW_WRITE_MASK  IOAPIC_82093AA_REDIR_LOW_WRITE_MASK
#define IOAPIC_REDIR_HIGH_WRITE_MASK IOAPIC_82093AA_REDIR_HIGH_WRITE_MASK
#define IOAPIC_REDIR_LOW_RESET       IOAPIC_82093AA_REDIR_LOW_RESET
#define IOAPIC_REDIR_HIGH_RESET      IOAPIC_82093AA_REDIR_HIGH_RESET

#define IOAPIC_DELIVERY_FIXED  IOAPIC_82093AA_DELIVERY_FIXED
#define IOAPIC_DELIVERY_LOWEST IOAPIC_82093AA_DELIVERY_LOWEST
#define IOAPIC_DELIVERY_SMI    IOAPIC_82093AA_DELIVERY_SMI
#define IOAPIC_DELIVERY_NMI    IOAPIC_82093AA_DELIVERY_NMI
#define IOAPIC_DELIVERY_INIT   IOAPIC_82093AA_DELIVERY_INIT
#define IOAPIC_DELIVERY_EXTINT IOAPIC_82093AA_DELIVERY_EXTINT

static ioapic_t *ioapic_global;

static void ioapic_lapic_reset(ioapic_t *dev);
static int  ioapic_chip_state_save(ioapic_t *dev, ioapic_state_t *state);
static int  ioapic_chip_state_load(ioapic_t *dev, const ioapic_state_t *state);
static int  ioapic_compat_state_save(ioapic_t *dev, ioapic_state_t *state);
static int  ioapic_compat_state_load(ioapic_t *dev, const ioapic_state_t *state);
static int  ioapic_redir_select_targets(ioapic_t *dev, uint32_t low, uint32_t high, lapic_target_info_t *targets);
static int  ioapic_pic_pending_adapter(void *priv);
static int  ioapic_pic_interrupt_adapter(void *priv);
static int  ioapic_pin_pending_after_redir_write(const ioapic_t *dev, uint8_t pin);
static void ioapic_deliver_pin(ioapic_t *dev, uint8_t pin, int asserted, int retry);
static void ioapic_eoi(ioapic_t *dev, uint8_t vector);
static void ioapic_update_intin23_smiout(ioapic_t *dev);
static void ioapic_mp_legacy_irq_route(void *priv, uint8_t irq, uint8_t intin);

static void
ioapic_disable_apic_mappings(ioapic_t *dev)
{
    apic_platform_disable_lapic_mappings(dev->apic);
    mem_mapping_disable(&dev->ioapic_mapping);
}

static uint8_t
ioapic_legacy_irq_to_pin(ioapic_t *dev, uint8_t irq)
{
    return dev->legacy_irq_to_pin[irq & 0x0f];
}

static void
ioapic_load_compat_legacy_irq_pins(uint8_t irq_to_pin[IOAPIC_LEGACY_IRQ_COUNT])
{
    static const uint8_t compat_irq_to_pin[IOAPIC_LEGACY_IRQ_COUNT] = {
        2, 1, 2, 3, 4, 5, 6, 7,
        8, 19, 10, 18, 12, 13, 14, 15
    };

    memcpy(irq_to_pin, compat_irq_to_pin, IOAPIC_LEGACY_IRQ_COUNT);
}

static void
ioapic_init_legacy_irq_pins(ioapic_t *dev)
{
    if (!dev->legacy_irq_default_to_pin[0])
        ioapic_load_compat_legacy_irq_pins(dev->legacy_irq_default_to_pin);

    memcpy(dev->legacy_irq_to_pin, dev->legacy_irq_default_to_pin,
           sizeof(dev->legacy_irq_to_pin));
}

#ifdef ENABLE_IOAPIC_LOG
uint8_t ioapic_do_log = ENABLE_IOAPIC_LOG;

static void
ioapic_log(const char *fmt, ...)
{
    va_list ap;

    if (ioapic_do_log) {
        va_start(ap, fmt);
        pclog_ex(fmt, ap);
        va_end(ap);
    }
}

static void
ioapic_log_ex(UNUSED(void *priv), const char *fmt, va_list ap)
{
    if (ioapic_do_log)
        pclog_ex(fmt, ap);
}
#else
#    define ioapic_log(fmt, ...)
#endif

static int
ioapic_log_first(uint8_t *count, uint8_t limit)
{
    if (*count >= limit)
        return 0;

    (*count)++;
    return 1;
}

static apic_bus_result_t
apic_deliver_targets(ioapic_t *dev, const lapic_target_info_t *targets, uint32_t delivery,
                     uint8_t vector, int level)
{
    apic_bus_message_t message = {
        .source   = APIC_BUS_AGENT_IOAPIC,
        .delivery = (apic_bus_delivery_t) delivery,
        .vector   = vector,
        .level    = !!level
    };

    return apic_platform_ioapic_deliver(dev->apic, targets,
                                        &dev->lowest_priority_next, &message);
}

static void
ioapic_apic_bus_message_complete(ioapic_t *dev)
{
    apic_platform_ioapic_bus_message_complete(dev->apic, &dev->ioapic_arb_id);
}

static int
ioapic_pic_pending_irq(void)
{
    uint8_t irq;

    if (!pic.int_pending)
        return -1;

    irq = pic.interrupt & 0x07;
    if ((irq == 2) && pic2.int_pending)
        return 8 + (pic2.interrupt & 0x07);

    return irq;
}

static int
ioapic_redir_claims_legacy_irq(ioapic_t *dev, uint8_t irq)
{
    lapic_target_info_t targets;
    uint8_t  pin;
    uint32_t low;
    uint32_t high;

    if (irq >= 16)
        return 0;

    /* IRQ0 can be claimed by an ExtINT RTE on INTIN0, independent of board IRQ map. */
    low  = dev->ioapic_redir[0];
    high = dev->ioapic_redir[1];
    if ((irq == 0) && !(low & IOAPIC_REDIR_MASK) && ioapic_82093aa_redir_supported(low) &&
        ioapic_82093aa_redir_extint(low) &&
        ioapic_redir_select_targets(dev, low, high, &targets))
        return 1;

    pin  = ioapic_legacy_irq_to_pin(dev, irq);
    low  = dev->ioapic_redir[pin * 2];
    high = dev->ioapic_redir[(pin * 2) + 1];

    return !(low & IOAPIC_REDIR_MASK) && ioapic_82093aa_redir_supported(low) &&
           ioapic_redir_select_targets(dev, low, high, &targets);
}

static int
ioapic_lint0_pic_fallback_pending(ioapic_t *dev)
{
    int irq = ioapic_pic_pending_irq();

    return (irq >= 0) && !ioapic_redir_claims_legacy_irq(dev, irq);
}

static int
ioapic_pic_pending_adapter(void *priv)
{
    return ioapic_lint0_pic_fallback_pending((ioapic_t *) priv);
}

static int
ioapic_pic_interrupt_adapter(void *priv)
{
    (void) priv;

    return picinterrupt();
}

/* Whether interrupts reach the CPU through an APIC at all on this machine.
   The CPU's interrupt checks test this first and, when it is clear, read
   the 8259 exactly as they did before any APIC existed. */
uint8_t ioapic_irq_path_active = 0;

int
ioapic_uniprocessor_apic_enabled(void)
{
    return (ioapic_global != NULL) && ioapic_global->uniprocessor_apic;
}

int
ioapic_external_interrupt_pending(void)
{
    ioapic_t *dev = ioapic_global;

    if ((dev == NULL) || !dev->uniprocessor_apic)
        return 0;

    if (apic_platform_current_base_enabled(dev->apic) &&
        apic_platform_current_software_enabled(dev->apic)) {
        if (!dev->pci_level_replay_enabled) {
            pci_apic_replay_level_irqs();
            dev->pci_level_replay_enabled = 1;
        }
    } else
        dev->pci_level_replay_enabled = 0;

    /* The replay has seen this enable state; until it changes the CPU's
       check may answer "nothing pending" without coming here. */
    cpu_apic_int_fast_ok = 1;

    return apic_platform_current_external_pending(dev->apic,
                                                  ioapic_pic_pending_adapter,
                                                  dev);
}

int
ioapic_pic_fallback_allowed(void)
{
    ioapic_t *dev = ioapic_global;

    if ((dev == NULL) || !dev->uniprocessor_apic)
        return 1;

    return !apic_platform_current_base_enabled(dev->apic) ||
           !apic_platform_current_software_enabled(dev->apic);
}

int
ioapic_lint1_raise(void)
{
    ioapic_t *dev = ioapic_global;

    if ((dev == NULL) || !dev->uniprocessor_apic)
        return 0;

    return apic_platform_current_lint_deliver(dev->apic, LAPIC_REG_LVT_LINT1);
}

int
ioapic_external_interrupt_vector(void)
{
    ioapic_t *dev = ioapic_global;

    if ((dev == NULL) || !dev->uniprocessor_apic)
        return -1;

    return apic_platform_current_external_vector(dev->apic,
                                                 ioapic_pic_pending_adapter,
                                                 ioapic_pic_interrupt_adapter,
                                                 dev);
}

static int
ioapic_redir_remote_irr(uint32_t low)
{
    return !!(low & IOAPIC_REDIR_REMOTEIRR);
}

static int
ioapic_redir_masked(uint32_t low)
{
    return !!(low & IOAPIC_REDIR_MASK);
}

static int
ioapic_redir_delivery_status_valid(uint32_t low)
{
    uint32_t delivery = low & IOAPIC_REDIR_DELIV;

    return (delivery == IOAPIC_DELIVERY_FIXED) ||
           (delivery == IOAPIC_DELIVERY_LOWEST);
}

static int
ioapic_pin_asserted(const ioapic_t *dev, uint8_t pin)
{
    return !!(dev->ioapic_pin_lines & ioapic_82093aa_pin_bit(pin));
}

static int
ioapic_pin_raw_high(const ioapic_t *dev, uint8_t pin)
{
    return !!(dev->ioapic_pin_raw_lines & ioapic_82093aa_pin_bit(pin));
}

static int
ioapic_pin_level_triggered(const ioapic_t *dev, uint8_t pin)
{
    return !!(dev->ioapic_pin_level & ioapic_82093aa_pin_bit(pin));
}

static int
ioapic_redir_select_targets(ioapic_t *dev, uint32_t low, uint32_t high, lapic_target_info_t *targets)
{
    uint8_t dest = high >> 24;
    int     logical = !!(low & IOAPIC_REDIR_DESTMOD);

    /* 82093AA physical mode uses only IOREDTBLx[59:56]. */
    if (!logical)
        dest &= 0x0f;

    return apic_platform_ioapic_select_targets(dev->apic, dest, logical, targets);
}

static int
ioapic_targets_include_current_lapic(const lapic_target_info_t *targets)
{
    uint8_t current;

    if ((targets == NULL) || !targets->count)
        return 0;

    current = lapic_index_source_read(NULL);
    if (current >= APIC_MAX_LAPICS)
        return 0;

    return !!(targets->bitmap & (1u << current));
}

static apic_bus_result_t
ioapic_deliver_current_lapic_direct(const lapic_target_info_t *targets,
                                    void (*deliver)(void))
{
    if (!ioapic_targets_include_current_lapic(targets))
        return apic_bus_result_no_target();

    deliver();
    return apic_bus_result(APIC_BUS_ACCEPTED);
}

static int
ioapic_pin_delivery_pending(const ioapic_t *dev, uint8_t pin)
{
    return dev->ioapic_delivery_pending_valid &&
           (dev->ioapic_delivery_pending_pin == pin);
}

static void
ioapic_latch_pin_delivery_pending(ioapic_t *dev, uint8_t pin)
{
    if (dev->ioapic_delivery_pending_valid &&
        (dev->ioapic_delivery_pending_pin != pin))
        return;

    dev->ioapic_delivery_pending_valid = 1;
    dev->ioapic_delivery_pending_pin = pin;
    dev->ioapic_intin_poll_next = (pin + 1) % IOAPIC_REDIR_ENTRIES;
}

static void
ioapic_clear_pin_delivery_pending(ioapic_t *dev, uint8_t pin)
{
    if (!ioapic_pin_delivery_pending(dev, pin))
        return;

    dev->ioapic_delivery_pending_valid = 0;
    dev->ioapic_delivery_pending_pin = 0;
}

static int
ioapic_delivery_pending_active(const ioapic_t *dev)
{
    return !!dev->ioapic_delivery_pending_valid;
}

static int
ioapic_delivery_failure_retryable(apic_bus_result_t result)
{
    return (result.status == APIC_BUS_NO_TARGET) ||
           (result.status == APIC_BUS_REJECTED);
}

static int
ioapic_pin_poll_candidate(const ioapic_t *dev, uint8_t pin)
{
    uint32_t low = dev->ioapic_redir[pin * 2];

    if (ioapic_redir_masked(low) || !ioapic_82093aa_redir_supported(low))
        return 0;

    return ioapic_pin_pending_after_redir_write(dev, pin);
}

static void
ioapic_retry_pending_pin(ioapic_t *dev, uint8_t pin)
{
    uint32_t low = dev->ioapic_redir[pin * 2];
    int      level = ioapic_82093aa_redir_remote_irr_valid(low);

    if (!ioapic_pin_delivery_pending(dev, pin))
        return;

    if (ioapic_redir_masked(low) || !ioapic_82093aa_redir_supported(low) ||
        !ioapic_redir_delivery_status_valid(low)) {
        ioapic_clear_pin_delivery_pending(dev, pin);
        return;
    }

    if (level) {
        if (!ioapic_pin_asserted(dev, pin)) {
            ioapic_clear_pin_delivery_pending(dev, pin);
            return;
        }
        if (ioapic_redir_remote_irr(low))
            return;
    }

    ioapic_deliver_pin(dev, pin, 1, 1);
}

static void
ioapic_poll_next_intin(ioapic_t *dev)
{
    if (ioapic_delivery_pending_active(dev))
        return;

    for (uint8_t i = 0; i < IOAPIC_REDIR_ENTRIES; i++) {
        uint8_t pin = (dev->ioapic_intin_poll_next + i) % IOAPIC_REDIR_ENTRIES;

        if (!ioapic_pin_poll_candidate(dev, pin))
            continue;

        dev->ioapic_intin_poll_next = (pin + 1) % IOAPIC_REDIR_ENTRIES;
        ioapic_deliver_pin(dev, pin, 1, 0);
        return;
    }
}

static void
ioapic_retry_pending_transaction(ioapic_t *dev)
{
    if (ioapic_delivery_pending_active(dev)) {
        ioapic_retry_pending_pin(dev, dev->ioapic_delivery_pending_pin);
        return;
    }

    ioapic_poll_next_intin(dev);
}

static void
ioapic_set_smiout_asserted(ioapic_t *dev, int asserted)
{
    asserted = !!asserted;
    if (dev->ioapic_smiout_asserted == asserted)
        return;

    dev->ioapic_smiout_asserted = asserted;
    if (dev->smiout_handler != NULL)
        dev->smiout_handler(dev->smiout_priv, asserted);
}

static void
ioapic_update_intin23_smiout(ioapic_t *dev)
{
    uint32_t low = dev->ioapic_redir[IOAPIC_SMI_PIN * 2];

    /* INTIN23/SMI# mask routes the active-low input to SMIOUT# instead of APIC bus. */
    if (ioapic_redir_masked(low)) {
        ioapic_set_smiout_asserted(dev, !ioapic_pin_raw_high(dev, IOAPIC_SMI_PIN));
        return;
    }

    ioapic_set_smiout_asserted(dev, 0);
}

static int
ioapic_pin_pending_after_redir_write(const ioapic_t *dev, uint8_t pin)
{
    uint32_t low = dev->ioapic_redir[pin * 2];

    return (ioapic_82093aa_redir_remote_irr_valid(low) && ioapic_pin_asserted(dev, pin)) ||
           ((pin == IOAPIC_SMI_PIN) && ioapic_82093aa_redir_smi(low) &&
            !ioapic_redir_masked(low) && ioapic_pin_asserted(dev, pin));
}

static void
ioapic_update_asserted_pin_after_polarity_rewrite(ioapic_t *dev, uint8_t pin,
                                                  uint32_t old_low, uint32_t new_low)
{
    uint32_t pin_bit = ioapic_82093aa_pin_bit(pin);
    int asserted;

    if (!((old_low ^ new_low) & IOAPIC_REDIR_POLARITY) ||
        !(ioapic_pin_asserted(dev, pin) || ioapic_pin_level_triggered(dev, pin)))
        return;

    /* Raw physical level is canonical; logical assertion follows current RTE polarity. */
    asserted = ioapic_82093aa_redir_physical_asserted(new_low, ioapic_pin_raw_high(dev, pin));
    if (asserted)
        dev->ioapic_pin_lines |= pin_bit;
    else
        dev->ioapic_pin_lines &= ~pin_bit;
}

static void
ioapic_log_redir_entry(ioapic_t *dev, uint8_t pin, const char *source)
{
    uint32_t low  = dev->ioapic_redir[pin * 2];
    uint32_t high = dev->ioapic_redir[(pin * 2) + 1];

    (void) low;
    (void) high;
    (void) source;
    if (!ioapic_log_first(&dev->ioapic_redir_log_count[pin], 8))
        return;

    ioapic_log("IOAPIC: %s pin=%u vector=%02x delivery=%s dest=%02x mask=%u trigger=%s polarity=%s remoteirr=%u raw=%u line=%u level=%u low=%08x high=%08x\n",
               source, pin, low & IOAPIC_REDIR_VECTOR, ioapic_82093aa_redir_delivery_name(low),
               (high >> 24) & 0xff, !!(low & IOAPIC_REDIR_MASK),
               (low & IOAPIC_REDIR_TRIGGER) ? "level" : "edge",
               ioapic_82093aa_redir_active_low(low) ? "low" : "high",
               ioapic_redir_remote_irr(low),
               ioapic_pin_raw_high(dev, pin),
               ioapic_pin_asserted(dev, pin), ioapic_pin_level_triggered(dev, pin),
               low, high);
}

static void
ioapic_log_masked_irq(ioapic_t *dev, uint8_t irq, uint8_t pin, int level, int set, uint32_t low)
{
    uint8_t *count = &dev->ioapic_masked_irq_log_count[pin];

    if (!set || (*count >= 8))
        return;

    (*count)++;
    ioapic_log("IOAPIC: masked legacy irq=%u -> pin=%u raise mode=%s count=%u vector=%02x delivery=%s low=%08x\n",
               irq, pin, level ? "level" : "edge", *count, low & IOAPIC_REDIR_VECTOR,
               ioapic_82093aa_redir_delivery_name(low), low);
}

static void
ioapic_deliver_pin(ioapic_t *dev, uint8_t pin, int asserted, int retry)
{
    uint32_t *lowp = &dev->ioapic_redir[pin * 2];
    uint32_t  low  = *lowp;
    uint32_t  high = dev->ioapic_redir[(pin * 2) + 1];
    uint8_t   vector = low & IOAPIC_REDIR_VECTOR;
    uint32_t  delivery = low & IOAPIC_REDIR_DELIV;
    int       level  = ioapic_82093aa_redir_remote_irr_valid(low);
    lapic_target_info_t targets;

    if (!retry && ioapic_delivery_pending_active(dev))
        return;

    if (ioapic_redir_masked(low)) {
        ioapic_clear_pin_delivery_pending(dev, pin);
        return;
    }

    if (!ioapic_82093aa_redir_supported(low)) {
        apic_bus_result_t result = apic_bus_result_invalid_mode();

        (void) result;
        ioapic_log("APIC-WARN: IOAPIC pin=%u unsupported delivery mode status=%u low=%08x\n",
                   pin, result.status, low);
        ioapic_clear_pin_delivery_pending(dev, pin);
        return;
    }

    if (!ioapic_redir_delivery_status_valid(low))
        ioapic_clear_pin_delivery_pending(dev, pin);

    if (level) {
        if (!asserted) {
            ioapic_clear_pin_delivery_pending(dev, pin);
            return;
        }
        if (ioapic_redir_remote_irr(low))
            return;
    } else if (!asserted) {
        return;
    }

    if (!ioapic_redir_select_targets(dev, low, high, &targets)) {
        apic_bus_result_t result = apic_bus_result_no_target();

        if (ioapic_redir_delivery_status_valid(low) &&
            ioapic_delivery_failure_retryable(result))
            ioapic_latch_pin_delivery_pending(dev, pin);
        if (!ioapic_log_first(&dev->ioapic_dest_mismatch_log_count[pin], 8))
            return;
        ioapic_log("IOAPIC: pin=%u dest mismatch status=%u source_esr=%08x dest=%02x lapic=%02x low=%08x high=%08x\n",
                   pin, result.status, result.source_esr, high >> 24,
                   apic_platform_current_lapic_id(dev->apic), low, high);
        return;
    }

    /* Accepted special deliveries bypass vector accept but still complete APIC bus arbitration. */
    if (delivery == IOAPIC_DELIVERY_SMI) {
        apic_bus_result_t result =
            ioapic_deliver_current_lapic_direct(&targets, cpu_smi_raise_direct);

        if (!apic_bus_result_accepted(result)) {
            ioapic_clear_pin_delivery_pending(dev, pin);
            if (ioapic_log_first(&dev->ioapic_dest_mismatch_log_count[pin], 8))
                ioapic_log("IOAPIC: pin=%u SMI no current LAPIC target status=%u dest=%02x current_lapic=%02x low=%08x high=%08x\n",
                           pin, result.status, high >> 24,
                           apic_platform_current_lapic_id(dev->apic), low, high);
            return;
        }
        if (vector && ioapic_log_first(&dev->ioapic_smi_vector_log_count[pin], 8))
            ioapic_log("APIC-WARN: IOAPIC SMI pin=%u non-zero vector=%02x preserved for compatibility low=%08x high=%08x\n",
                       pin, vector, low, high);
        ioapic_clear_pin_delivery_pending(dev, pin);
        ioapic_apic_bus_message_complete(dev);
        if (ioapic_log_first(&dev->ioapic_deliver_log_count[pin], 8))
            ioapic_log("IOAPIC: delivered SMI pin=%u count=%u low=%08x high=%08x\n",
                       pin, dev->ioapic_deliver_log_count[pin], low, high);
        return;
    }

    if (delivery == IOAPIC_DELIVERY_NMI) {
        apic_bus_result_t result =
            ioapic_deliver_current_lapic_direct(&targets, cpu_nmi_raise_direct);

        if (!apic_bus_result_accepted(result)) {
            ioapic_clear_pin_delivery_pending(dev, pin);
            if (ioapic_log_first(&dev->ioapic_dest_mismatch_log_count[pin], 8))
                ioapic_log("IOAPIC: pin=%u NMI no current LAPIC target status=%u dest=%02x current_lapic=%02x low=%08x high=%08x\n",
                           pin, result.status, high >> 24,
                           apic_platform_current_lapic_id(dev->apic), low, high);
            return;
        }
        ioapic_clear_pin_delivery_pending(dev, pin);
        ioapic_apic_bus_message_complete(dev);
        if (ioapic_log_first(&dev->ioapic_deliver_log_count[pin], 8))
            ioapic_log("IOAPIC: delivered NMI pin=%u count=%u low=%08x high=%08x\n",
                       pin, dev->ioapic_deliver_log_count[pin], low, high);
        return;
    }

    if (delivery == IOAPIC_DELIVERY_INIT) {
        if (!apic_platform_ioapic_deliver_init(dev->apic, &targets))
            return;
        ioapic_clear_pin_delivery_pending(dev, pin);
        ioapic_apic_bus_message_complete(dev);
        if (ioapic_log_first(&dev->ioapic_deliver_log_count[pin], 8))
            ioapic_log("IOAPIC: delivered INIT pin=%u count=%u low=%08x high=%08x\n",
                       pin, dev->ioapic_deliver_log_count[pin], low, high);
        return;
    }

    /* ExtINT only latches once PIC has a pending IRQ; asserted pin can retry later. */
    if (ioapic_82093aa_redir_extint(low)) {
        if (ioapic_pic_pending_irq() < 0) {
            if (ioapic_log_first(&dev->ioapic_extint_defer_log_count[pin], 8))
                ioapic_log("IOAPIC: deferred ExtINT pin=%u no PIC pending count=%u low=%08x high=%08x\n",
                           pin, dev->ioapic_extint_defer_log_count[pin], low, high);
            return;
        }

        if (!apic_platform_ioapic_mark_extint(dev->apic, &targets, pin))
            return;
        ioapic_clear_pin_delivery_pending(dev, pin);
        ioapic_apic_bus_message_complete(dev);
        if (ioapic_log_first(&dev->ioapic_extint_deliver_log_count[pin], 8))
            ioapic_log("IOAPIC: delivered ExtINT pin=%u count=%u low=%08x high=%08x\n",
                       pin, dev->ioapic_extint_deliver_log_count[pin], low, high);
        return;
    }

    apic_bus_result_t result = apic_deliver_targets(dev, &targets, delivery,
                                                    vector, level);
    if (!apic_bus_result_accepted(result)) {
        if (ioapic_redir_delivery_status_valid(low) &&
            ioapic_delivery_failure_retryable(result))
            ioapic_latch_pin_delivery_pending(dev, pin);
        else
            ioapic_clear_pin_delivery_pending(dev, pin);
        if (ioapic_log_first(&dev->ioapic_dest_mismatch_log_count[pin], 8))
            ioapic_log("IOAPIC: pin=%u delivery rejected status=%u source_esr=%08x target_esr=%08x vector=%02x delivery=%s low=%08x high=%08x\n",
                       pin, result.status, result.source_esr, result.target_esr,
                       vector, ioapic_82093aa_redir_delivery_name(low), low, high);
        return;
    }

    ioapic_clear_pin_delivery_pending(dev, pin);
    ioapic_apic_bus_message_complete(dev);
    if (level) {
        /* RemoteIRR is owned here after accepted level delivery; EOI path clears/retries. */
        *lowp |= IOAPIC_REDIR_REMOTEIRR;
    }

    if (ioapic_log_first(&dev->ioapic_deliver_log_count[pin], 8))
        ioapic_log("IOAPIC: delivered pin=%u vector=%02x trigger=%s count=%u low=%08x high=%08x\n",
                   pin, vector, level ? "level" : "edge", dev->ioapic_deliver_log_count[pin],
                   *lowp, high);
}

static void
ioapic_raw_intin_device(ioapic_t *dev, uint8_t pin, int level, int physical_high)
{
    uint32_t pin_bit = ioapic_82093aa_pin_bit(pin);
    uint32_t was_asserted = dev->ioapic_pin_lines & pin_bit;
    uint32_t low = dev->ioapic_redir[pin * 2];
    int effective_level = level && !ioapic_82093aa_redir_treats_as_edge(low);
    int asserted = ((pin == IOAPIC_SMI_PIN) && ioapic_82093aa_redir_smi(low)) ?
                       !physical_high :
                       ioapic_82093aa_redir_physical_asserted(low, physical_high);

    /* Keep raw physical state separate from polarity-adjusted line state. */
    if (physical_high)
        dev->ioapic_pin_raw_lines |= pin_bit;
    else
        dev->ioapic_pin_raw_lines &= ~pin_bit;

    if (asserted)
        dev->ioapic_pin_lines |= pin_bit;
    else
        dev->ioapic_pin_lines &= ~pin_bit;

    if (effective_level)
        dev->ioapic_pin_level |= pin_bit;
    else
        dev->ioapic_pin_level &= ~pin_bit;

    if (effective_level && !asserted)
        ioapic_clear_pin_delivery_pending(dev, pin);

    if (ioapic_redir_masked(low)) {
        ioapic_clear_pin_delivery_pending(dev, pin);
        if (pin == IOAPIC_SMI_PIN)
            ioapic_update_intin23_smiout(dev);
        return;
    }

    if (effective_level || (asserted && !was_asserted))
        ioapic_deliver_pin(dev, pin, asserted, 0);

    if (pin == IOAPIC_SMI_PIN)
        ioapic_update_intin23_smiout(dev);
}

void
ioapic_raw_intin(uint8_t pin, int level, int physical_high)
{
    ioapic_t *dev = ioapic_global;

    if ((dev == NULL) || !dev->uniprocessor_apic || (pin >= IOAPIC_REDIR_ENTRIES))
        return;

    ioapic_raw_intin_device(dev, pin, level, physical_high);
}

void
ioapic_intin_irq(uint8_t pin, int level, int set)
{
    ioapic_t *dev = ioapic_global;
    uint32_t  low;
    int       effective_level;
    int       physical_high;

    if ((dev == NULL) || !dev->uniprocessor_apic || (pin >= IOAPIC_REDIR_ENTRIES))
        return;

    if (!apic_platform_current_base_enabled(dev->apic) ||
        !apic_platform_current_software_enabled(dev->apic))
        return;

    low             = dev->ioapic_redir[pin * 2];
    effective_level = level && !ioapic_82093aa_redir_treats_as_edge(low);
    physical_high   = ioapic_82093aa_redir_active_low(low) ? !set : !!set;

    if (effective_level || set) {
        if (ioapic_log_first(&dev->ioapic_irq_log_count[pin], 8))
            ioapic_log("IOAPIC: intin pin=%u %s mode=%s redir_mask=%u vector=%02x delivery=%s\n",
                       pin, set ? "raise" : "lower", level ? "level" : "edge",
                       ioapic_redir_masked(low), low & IOAPIC_REDIR_VECTOR,
                       ioapic_82093aa_redir_delivery_name(low));
    }

    ioapic_raw_intin_device(dev, pin, level, physical_high);
    if (!level && set)
        ioapic_raw_intin_device(dev, pin, 0, !physical_high);
}

void
ioapic_legacy_irq(uint16_t mask, int level, int set)
{
    ioapic_t *dev = ioapic_global;

    if ((dev == NULL) || !dev->uniprocessor_apic)
        return;

    for (uint8_t irq = 0; irq < 16; irq++) {
        if (!(mask & (1u << irq)))
            continue;

        uint8_t pin = ioapic_legacy_irq_to_pin(dev, irq);

        if ((irq == 0) && !(dev->ioapic_redir[0] & IOAPIC_REDIR_MASK) &&
            ioapic_82093aa_redir_extint(dev->ioapic_redir[0]))
            pin = 0;

        if (pin >= IOAPIC_REDIR_ENTRIES)
            continue;

        uint32_t low          = dev->ioapic_redir[pin * 2];
        int      effective_level = level && !ioapic_82093aa_redir_treats_as_edge(low);
        int      physical_high = ioapic_82093aa_redir_active_low(low) ? !set : !!set;

        if (ioapic_redir_masked(low) && (effective_level || set))
            ioapic_log_masked_irq(dev, irq, pin, level, set, low);

        if (effective_level || set) {
            if (ioapic_log_first(&dev->ioapic_irq_log_count[pin], 8))
                ioapic_log("IOAPIC: legacy irq=%u -> pin=%u %s mode=%s redir_mask=%u vector=%02x delivery=%s\n",
                           irq, pin, set ? "raise" : "lower", level ? "level" : "edge",
                           ioapic_redir_masked(low), low & IOAPIC_REDIR_VECTOR,
                           ioapic_82093aa_redir_delivery_name(low));
        }

        ioapic_raw_intin_device(dev, pin, level, physical_high);
        /* Legacy edge inputs pulse; do not leave synthetic raw line asserted. */
        if (!level && set)
            ioapic_raw_intin_device(dev, pin, 0, !physical_high);
    }
}

int
ioapic_legacy_irq_enabled(void)
{
    ioapic_t *dev = ioapic_global;

    if ((dev == NULL) || !dev->uniprocessor_apic)
        return 0;

    if (!apic_platform_current_base_enabled(dev->apic) ||
        !apic_platform_current_software_enabled(dev->apic))
        return 0;

    return 1;
}

int
ioapic_legacy_irq_active(uint16_t mask, int level, int set)
{
    if (!ioapic_legacy_irq_enabled())
        return 0;

    ioapic_legacy_irq(mask, level, set);
    return 1;
}

static void
ioapic_sanitize_loaded_redir(ioapic_t *dev)
{
    for (uint8_t pin = 0; pin < IOAPIC_REDIR_ENTRIES; pin++) {
        uint32_t *lowp = &dev->ioapic_redir[pin * 2];
        uint32_t *highp = &dev->ioapic_redir[(pin * 2) + 1];
        uint32_t  pin_bit = ioapic_82093aa_pin_bit(pin);

        /* Loaded RTEs pass through guest-writable-bit policy before delivery uses them. */
        *lowp = ioapic_82093aa_redir_sanitize_low(*lowp);
        *highp &= IOAPIC_REDIR_HIGH_WRITE_MASK;
        if (!ioapic_82093aa_redir_remote_irr_valid(*lowp))
            dev->ioapic_pin_level &= ~pin_bit;
    }
}

int
ioapic_state_save(ioapic_state_t *state)
{
    ioapic_t *dev = ioapic_global;

    if ((dev == NULL) || (state == NULL))
        return 0;

    return ioapic_compat_state_save(dev, state);
}

static int
ioapic_chip_state_save(ioapic_t *dev, ioapic_state_t *state)
{
    if ((dev == NULL) || (state == NULL))
        return 0;

    state->ioapic_id = dev->ioapic_id;
    state->ioapic_index = dev->ioapic_index;
    state->ioapic_arb_id = dev->ioapic_arb_id;
    memcpy(state->ioapic_redir, dev->ioapic_redir, sizeof(state->ioapic_redir));
    state->ioapic_pin_raw_lines = dev->ioapic_pin_raw_lines;
    state->ioapic_pin_lines = dev->ioapic_pin_lines;
    state->ioapic_pin_level = dev->ioapic_pin_level;
    state->ioapic_smiout_asserted = dev->ioapic_smiout_asserted;
    memset(state->ioapic_pin_vector, 0, sizeof(state->ioapic_pin_vector));

    return 1;
}

static int
ioapic_compat_state_save(ioapic_t *dev, ioapic_state_t *state)
{
    memset(state, 0, sizeof(*state));

    state->version = IOAPIC_STATE_VERSION;

    state->expose_mp_tables = dev->expose_mp_tables;
    state->uniprocessor_apic = dev->uniprocessor_apic;
    state->mp_table_logged = dev->mp_table_logged;
    state->mp_table_skip_logged = dev->mp_table_skip_logged;
    state->multi_lapic = dev->multi_lapic;
    state->multi_lapic_mp_cpus = dev->multi_lapic_mp_cpus;
    state->ap_startup = dev->ap_startup;
    state->second_cpu_card = dev->second_cpu_card;
    state->ap_safe_park = dev->ap_safe_park;
    state->ap_route_parked = dev->ap_route_parked;
    state->lowest_priority_next = dev->lowest_priority_next;

    if (!apic_platform_lapic_state_save(dev->apic, state))
        return 0;

    state->pci_intin_route_count =
        pci_apic_save_intin_routes(state->pci_intin_routes, IOAPIC_STATE_PCI_INTIN_ROUTES);

    return ioapic_chip_state_save(dev, state);
}

static void
ioapic_update_apic_platform_policy(ioapic_t *dev)
{
    apic_platform_set_lapic_policy(dev->apic, dev->uniprocessor_apic,
                                   dev->multi_lapic, dev->second_cpu_card,
                                   dev->topology_lapic_slots_max,
                                   dev->ap_startup, dev->ap_safe_park,
                                   dev->ap_route_parked);
}

int
ioapic_state_load(const ioapic_state_t *state)
{
    ioapic_t *dev = ioapic_global;

    if ((dev == NULL) || (state == NULL) ||
        (state->version < IOAPIC_STATE_VERSION_MIN) || (state->version > IOAPIC_STATE_VERSION))
        return 0;

    return ioapic_compat_state_load(dev, state);
}

static int
ioapic_chip_state_load(ioapic_t *dev, const ioapic_state_t *state)
{
    if ((dev == NULL) || (state == NULL))
        return 0;

    dev->ioapic_id = state->ioapic_id & 0x0f;
    dev->ioapic_index = state->ioapic_index;
    dev->ioapic_arb_id = ((state->version >= 9) ? state->ioapic_arb_id : dev->ioapic_id) & 0x0f;
    memcpy(dev->ioapic_redir, state->ioapic_redir, sizeof(dev->ioapic_redir));
    dev->ioapic_pin_lines = state->ioapic_pin_lines & IOAPIC_PIN_MASK;
    /* v11 added raw pin state; older saves reconstruct it from logical lines and RTE polarity. */
    dev->ioapic_pin_raw_lines = ((state->version >= 11) ?
                                     state->ioapic_pin_raw_lines :
                                     ioapic_82093aa_derive_raw_lines_from_logical(dev->ioapic_redir,
                                                                          dev->ioapic_pin_lines)) &
                                IOAPIC_PIN_MASK;
    dev->ioapic_pin_level = state->ioapic_pin_level & IOAPIC_PIN_MASK;
    dev->ioapic_delivery_pending_valid = 0;
    dev->ioapic_delivery_pending_pin = 0;
    dev->ioapic_intin_poll_next = 0;
    dev->ioapic_smiout_asserted = (state->version >= 12) ? !!state->ioapic_smiout_asserted : 0;
    ioapic_sanitize_loaded_redir(dev);
    ioapic_update_intin23_smiout(dev);

    return 1;
}

static int
ioapic_compat_state_load(ioapic_t *dev, const ioapic_state_t *state)
{
    dev->expose_mp_tables = state->expose_mp_tables;
    dev->uniprocessor_apic = state->uniprocessor_apic;
    dev->mp_table_logged = state->mp_table_logged;
    dev->mp_table_skip_logged = state->mp_table_skip_logged;
    if (state->version >= 4) {
        /* Version gates keep old saves from enabling later guarded APIC features. */
        dev->multi_lapic = state->multi_lapic;
        dev->multi_lapic_mp_cpus = state->multi_lapic_mp_cpus;
        dev->ap_startup = (state->version >= 5) ? state->ap_startup : 0;
        dev->second_cpu_card = (state->version >= 6) ? state->second_cpu_card : 0;
        dev->ap_safe_park = (state->version >= 7) ? state->ap_safe_park : 0;
        dev->ap_route_parked = (state->version >= 8) ? state->ap_route_parked : 0;
        dev->lowest_priority_next = state->lowest_priority_next % APIC_MAX_LAPICS;
    } else {
        dev->multi_lapic = 0;
        dev->multi_lapic_mp_cpus = 0;
        dev->ap_startup = 0;
        dev->second_cpu_card = 0;
        dev->ap_safe_park = 0;
        dev->ap_route_parked = 0;
        dev->lowest_priority_next = 0;
    }

    ioapic_update_apic_platform_policy(dev);

    if (!apic_platform_lapic_state_load(dev->apic, state))
        return 0;

    if (!ioapic_chip_state_load(dev, state))
        return 0;

    if (state->version >= 13)
        pci_apic_load_intin_routes(state->pci_intin_routes, state->pci_intin_route_count);

    dev->pci_level_replay_enabled = 0;
    apic_cpu_lapic_set_ioapic_policy(dev->uniprocessor_apic);

    if (dev->uniprocessor_apic) {
        apic_platform_update_lapic_mapping(dev->apic);
    } else {
        apic_platform_stop_lapic_timers(dev->apic);
        ioapic_disable_apic_mappings(dev);
    }

    return 1;
}

static void
ioapic_eoi(ioapic_t *dev, uint8_t vector)
{
    for (uint8_t pin = 0; pin < IOAPIC_REDIR_ENTRIES; pin++) {
        uint32_t *lowp = &dev->ioapic_redir[pin * 2];
        uint32_t  low  = *lowp;

        /* "The Remote IRR bit is set to 0 when an EOI message with a
           matching interrupt vector is received" (82093AA 3.2.4 bit 14):
           the entry's vector as it stands now. */
        if (((low & IOAPIC_REDIR_VECTOR) != vector) || !ioapic_redir_remote_irr(low))
            continue;

        *lowp &= ~IOAPIC_REDIR_REMOTEIRR;
        if (ioapic_log_first(&dev->ioapic_eoi_log_count[pin], 8))
            ioapic_log("IOAPIC: EOI pin=%u vector=%02x line=%u count=%u\n",
                       pin, vector, ioapic_pin_asserted(dev, pin),
                       dev->ioapic_eoi_log_count[pin]);

        if (ioapic_pin_asserted(dev, pin))
            ioapic_deliver_pin(dev, pin, 1, 0);
    }
}

static void
ioapic_lapic_eoi_adapter(void *priv, uint8_t vector)
{
    ioapic_eoi((ioapic_t *) priv, vector);
}

static void
ioapic_lapic_icr_write_adapter(void *priv, uint32_t val)
{
    ioapic_t *dev = (ioapic_t *) priv;

    if (dev == NULL)
        return;

    apic_platform_lapic_icr_write(dev->apic, val);
    ioapic_retry_pending_transaction(dev);
}

static void
ioapic_lapic_reset(ioapic_t *dev)
{
    apic_platform_reset_lapics(dev->apic);
}

static void
ioapic_reset_redir(ioapic_t *dev)
{
    dev->ioapic_delivery_pending_valid = 0;
    dev->ioapic_delivery_pending_pin = 0;
    dev->ioapic_intin_poll_next = 0;
    for (uint8_t i = 0; i < IOAPIC_REDIR_ENTRIES; i++) {
        dev->ioapic_redir[(i * 2) + 0] = IOAPIC_REDIR_LOW_RESET;
        dev->ioapic_redir[(i * 2) + 1] = IOAPIC_REDIR_HIGH_RESET;
    }
}

static uint32_t
ioapic_window_read(ioapic_t *dev)
{
    uint8_t index = dev->ioapic_index;

    if (index == 0x00)
        return ((uint32_t) dev->ioapic_id) << 24;
    if (index == 0x01)
        return IOAPIC_VERSION_REG;
    if (index == 0x02)
        return ((uint32_t) dev->ioapic_arb_id) << 24;
    if ((index >= 0x10) && (index < (0x10 + (IOAPIC_REDIR_ENTRIES * 2)))) {
        uint8_t  redir_index = index - 0x10;
        uint32_t val = dev->ioapic_redir[redir_index];

        if (!(redir_index & 1) && ioapic_pin_delivery_pending(dev, redir_index >> 1))
            val |= IOAPIC_REDIR_STATUS;
        return val;
    }

    return 0;
}

static void
ioapic_window_write(ioapic_t *dev, uint32_t val)
{
    uint8_t index = dev->ioapic_index;

    if (index == 0x00) {
        dev->ioapic_id = (val >> 24) & 0x0f;
        dev->ioapic_arb_id = dev->ioapic_id;
        ioapic_retry_pending_transaction(dev);
        return;
    }

    if ((index >= 0x10) && (index < (0x10 + (IOAPIC_REDIR_ENTRIES * 2)))) {
        uint8_t redir_index = index - 0x10;
        uint8_t pin         = redir_index >> 1;
        uint32_t old_low    = dev->ioapic_redir[pin * 2];

        if (redir_index & 1) {
            val &= IOAPIC_REDIR_HIGH_WRITE_MASK;
        } else {
            val = (val & IOAPIC_REDIR_LOW_WRITE_MASK) |
                  ioapic_82093aa_redir_read_only_low_bits(dev->ioapic_redir[redir_index], val);
        }
        dev->ioapic_redir[redir_index] = val;
        if (!(redir_index & 1)) {
            /* Low-dword rewrites can change line interpretation. */
            ioapic_update_asserted_pin_after_polarity_rewrite(dev, pin, old_low,
                                                              dev->ioapic_redir[pin * 2]);
        }

        ioapic_log_redir_entry(dev, pin, (redir_index & 1) ? "redir high" : "redir low");

        if (pin == IOAPIC_SMI_PIN)
            ioapic_update_intin23_smiout(dev);

        if (ioapic_pin_delivery_pending(dev, pin)) {
            ioapic_retry_pending_pin(dev, pin);
            return;
        }

        if (ioapic_pin_pending_after_redir_write(dev, pin))
            ioapic_deliver_pin(dev, pin, 1, 0);
        return;
    }

    ioapic_log("IOAPIC: ignored window write index=%02x val=%08x\n", index, val);
}

static uint32_t
ioapic_mmio_readl(uint32_t addr, void *priv)
{
    ioapic_t *dev = (ioapic_t *) priv;
    uint32_t  reg = (addr - IOAPIC_MMIO_BASE) & 0xff;
    uint32_t  ret = 0;

    if (reg == 0x00)
        ret = dev->ioapic_index;
    else if (reg == 0x10)
        ret = ioapic_window_read(dev);

    if (ioapic_log_first(&dev->ioapic_mmio_read_log_count[reg >> 2], 8))
        ioapic_log("IOAPIC: read mmio=%02x index=%02x val=%08x count=%u\n",
                   reg, dev->ioapic_index, ret, dev->ioapic_mmio_read_log_count[reg >> 2]);

    return ret;
}

static uint16_t
ioapic_mmio_readw(uint32_t addr, void *priv)
{
    uint32_t ret = ioapic_mmio_readl(addr & ~0x03, priv);

    return ret >> ((addr & 0x02) << 3);
}

static uint8_t
ioapic_mmio_readb(uint32_t addr, void *priv)
{
    uint32_t ret = ioapic_mmio_readl(addr & ~0x03, priv);

    return ret >> ((addr & 0x03) << 3);
}

static void
ioapic_mmio_writel(uint32_t addr, uint32_t val, void *priv)
{
    ioapic_t *dev = (ioapic_t *) priv;
    uint32_t  reg = (addr - IOAPIC_MMIO_BASE) & 0xff;

    if (ioapic_log_first(&dev->ioapic_mmio_write_log_count[reg >> 2], 8))
        ioapic_log("IOAPIC: write mmio=%02x index=%02x val=%08x count=%u\n",
                   reg, dev->ioapic_index, val, dev->ioapic_mmio_write_log_count[reg >> 2]);

    if (reg == 0x00)
        dev->ioapic_index = val & 0xff;
    else if (reg == 0x10)
        ioapic_window_write(dev, val);
}

static void
ioapic_mmio_writew(uint32_t addr, uint16_t val, void *priv)
{
    uint32_t shift = (addr & 0x02) << 3;
    uint32_t cur   = ioapic_mmio_readl(addr & ~0x03, priv);

    cur &= ~(0xffffu << shift);
    cur |= ((uint32_t) val) << shift;
    ioapic_mmio_writel(addr & ~0x03, cur, priv);
}

static void
ioapic_mmio_writeb(uint32_t addr, uint8_t val, void *priv)
{
    uint32_t shift = (addr & 0x03) << 3;
    uint32_t cur   = ioapic_mmio_readl(addr & ~0x03, priv);

    cur &= ~(0xffu << shift);
    cur |= ((uint32_t) val) << shift;
    ioapic_mmio_writel(addr & ~0x03, cur, priv);
}

static void
ioapic_mp_legacy_irq_route(void *priv, uint8_t irq, uint8_t intin)
{
    ioapic_t *dev = (ioapic_t *) priv;

    if ((dev == NULL) || (irq >= IOAPIC_LEGACY_IRQ_COUNT) || (intin >= IOAPIC_REDIR_ENTRIES))
        return;

    if (dev->legacy_irq_default_to_pin[irq] != intin) {
        ioapic_log("IOAPIC: MP learned legacy irq=%u -> pin=%u (old pin=%u)\n",
                   irq, intin, dev->legacy_irq_default_to_pin[irq]);
        dev->legacy_irq_default_to_pin[irq] = intin;
        dev->legacy_irq_to_pin[irq] = intin;
    }
}

static void
ioapic_mp_pci_irq_route(void *priv, uint8_t bus, uint8_t device, uint8_t intpin, uint8_t intin)
{
    ioapic_t *dev = (ioapic_t *) priv;

    if ((dev == NULL) || (intpin >= PCI_INT_PINS_NUM) || (intin >= IOAPIC_REDIR_ENTRIES))
        return;

    ioapic_log("IOAPIC: MP learned PCI bus=%u dev=%u INT%c -> pin=%u\n",
               bus, device, 'A' + intpin, intin);
    pci_apic_set_intin_route(bus, device, intpin, intin);
}

static void
ioapic_write(UNUSED(uint16_t port), uint8_t val, void *priv)
{
    ioapic_t *dev = (ioapic_t *) priv;
    apic_platform_mp_table_policy_t policy;

    if (dev == NULL)
        return;

    policy.expose_mp_tables = dev->expose_mp_tables;
    policy.multi_lapic_mp_cpus = dev->multi_lapic_mp_cpus;
    policy.post_trigger = dev->post_trigger;
    policy.mp_table_logged = &dev->mp_table_logged;
    policy.mp_table_skip_logged = &dev->mp_table_skip_logged;
    policy.legacy_irq_route_priv = dev;
    policy.legacy_irq_route_handler = ioapic_mp_legacy_irq_route;
    policy.pci_irq_route_priv = dev;
    policy.pci_irq_route_handler = ioapic_mp_pci_irq_route;
    /* Port 0x80 hook triggers MP-table patch policy; platform owns BIOS table edits. */
    apic_platform_mp_table_post(dev->apic, val, &policy);
}

static void
ioapic_reset(ioapic_t *dev)
{
    ioapic_lapic_reset(dev);
    dev->ioapic_id      = IOAPIC_ID_RESET;
    dev->ioapic_index   = 0;
    dev->ioapic_arb_id  = 0;
    dev->ioapic_pin_lines = 0;
    dev->ioapic_pin_raw_lines = 0;
    dev->ioapic_pin_level = 0;
    ioapic_set_smiout_asserted(dev, 0);
    dev->lowest_priority_next = 0;
    dev->mp_table_logged = 0;
    dev->mp_table_skip_logged = 0;
    memset(dev->ioapic_masked_irq_log_count, 0, sizeof(dev->ioapic_masked_irq_log_count));
    memset(dev->ioapic_irq_log_count, 0, sizeof(dev->ioapic_irq_log_count));
    memset(dev->ioapic_deliver_log_count, 0, sizeof(dev->ioapic_deliver_log_count));
    memset(dev->ioapic_dest_mismatch_log_count, 0, sizeof(dev->ioapic_dest_mismatch_log_count));
    memset(dev->ioapic_extint_defer_log_count, 0, sizeof(dev->ioapic_extint_defer_log_count));
    memset(dev->ioapic_extint_deliver_log_count, 0, sizeof(dev->ioapic_extint_deliver_log_count));
    memset(dev->ioapic_smi_vector_log_count, 0, sizeof(dev->ioapic_smi_vector_log_count));
    memset(dev->ioapic_eoi_log_count, 0, sizeof(dev->ioapic_eoi_log_count));
    memset(dev->ioapic_redir_log_count, 0, sizeof(dev->ioapic_redir_log_count));
    memset(dev->ioapic_mmio_read_log_count, 0, sizeof(dev->ioapic_mmio_read_log_count));
    memset(dev->ioapic_mmio_write_log_count, 0, sizeof(dev->ioapic_mmio_write_log_count));
    ioapic_init_legacy_irq_pins(dev);

    ioapic_reset_redir(dev);
}

void
ioapic_set_legacy_irq_routing(void *ioapic, const ioapic_legacy_irq_routing_t *routing)
{
    ioapic_t *dev = (ioapic_t *) ioapic;

    if ((dev == NULL) || (routing == NULL))
        return;

    for (uint8_t irq = 0; irq < IOAPIC_LEGACY_IRQ_COUNT; irq++)
        if (routing->irq_to_intin[irq] < IOAPIC_REDIR_ENTRIES) {
            dev->legacy_irq_default_to_pin[irq] = routing->irq_to_intin[irq];
            dev->legacy_irq_to_pin[irq] = routing->irq_to_intin[irq];
        }
}

void
ioapic_set_smiout_handler(ioapic_smiout_handler_t handler, void *priv)
{
    ioapic_t *dev = ioapic_global;

    if (dev == NULL)
        return;

    dev->smiout_handler = handler;
    dev->smiout_priv = priv;
    if (handler != NULL)
        handler(priv, dev->ioapic_smiout_asserted);
}

static void
ioapic_device_reset(void *priv)
{
    ioapic_t *dev = (ioapic_t *) priv;

    ioapic_reset(dev);
    if (dev->uniprocessor_apic)
        apic_platform_update_lapic_mapping(dev->apic);
}

static void
ioapic_close(void *priv)
{
    ioapic_t *dev = (ioapic_t *) priv;

    io_removehandler(0x80, 1,
                     NULL, NULL, NULL, ioapic_write, NULL, NULL, dev);

    if (dev->uniprocessor_apic)
        ioapic_disable_apic_mappings(dev);

    if (ioapic_global == dev) {
        ioapic_global          = NULL;
        ioapic_irq_path_active = 0;
        lapic_bus_ioapic_attach(NULL, NULL);
    }
    apic_cpu_lapic_set_ioapic_policy(0);
    apic_platform_free(dev->apic);

    free(dev);
}

static void *
ioapic_init(UNUSED(const device_t *info))
{
    ioapic_t *dev = (ioapic_t *) calloc(1, sizeof(ioapic_t));
    uintptr_t local = info->local;

    if (dev == NULL)
        return NULL;

    dev->apic = apic_platform_create(dev);
    if (dev->apic == NULL) {
        free(dev);
        return NULL;
    }

    dev->expose_mp_tables = !!device_get_config_int("expose_mp_tables");
    dev->uniprocessor_apic =
        !!device_get_config_int("uniprocessor_apic") ||
        (!!(local & IOAPIC_TOPOLOGY_UP_APIC_DEFAULT) &&
         !device_get_config_int("disable_apic") &&
         !machine_get_config_int("disable_apic"));
    dev->multi_lapic = !!device_get_config_int("multi_lapic");
    dev->multi_lapic_mp_cpus = !!device_get_config_int("multi_lapic_mp_cpus");
    dev->ap_startup = !!device_get_config_int("ap_startup");
    dev->second_cpu_card = !!device_get_config_int("second_cpu_card") ||
                           !!(local & IOAPIC_TOPOLOGY_SECOND_CPU_CARD);
    dev->ap_safe_park = !!device_get_config_int("ap_safe_park");
    dev->ap_route_parked = !!device_get_config_int("ap_route_parked");
    dev->smp_probe = !!device_get_config_int("smp_probe");
    dev->post_trigger = (local & IOAPIC_POST_TRIGGER_AMI) ? 0x00 : 0xff;
    dev->topology_lapic_slots_max = (local >> IOAPIC_TOPOLOGY_LAPIC_SLOTS_SHIFT) &
                                    IOAPIC_TOPOLOGY_LAPIC_SLOTS_MASK;
    apic_platform_set_lapic_count_config(dev->apic, device_get_config_int("lapic_count"));
    if (dev->smp_probe) {
        /* Probe preset enables the guarded two-LAPIC path from one config switch. */
        dev->uniprocessor_apic = 1;
        dev->second_cpu_card = 1;
        dev->multi_lapic = 1;
        dev->multi_lapic_mp_cpus = 1;
        dev->ap_startup = 1;
        dev->ap_safe_park = 1;
        apic_platform_ensure_lapic_count_min(dev->apic, 2);
    }
    if (dev->uniprocessor_apic)
        dev->expose_mp_tables = 1;
    apic_cpu_lapic_set_ioapic_policy(dev->uniprocessor_apic);

    ioapic_update_apic_platform_policy(dev);

#ifdef ENABLE_IOAPIC_LOG
    apic_platform_set_log_handler(dev->apic, ioapic_log_ex);
#else
    apic_platform_set_log_handler(dev->apic, NULL);
#endif
    apic_platform_set_icr_write_handler(dev->apic, ioapic_lapic_icr_write_adapter);

    ioapic_reset(dev);
    ioapic_global          = dev;
    lapic_bus_ioapic_attach(&dev->ioapic_arb_id, &dev->ioapic_id);
    ioapic_irq_path_active = dev->uniprocessor_apic;

    if (dev->uniprocessor_apic) {
        apic_platform_set_external_ioapic(dev->apic, dev, ioapic_lapic_eoi_adapter);
        apic_platform_enable_lapic_mappings(dev->apic);
        mem_mapping_add(&dev->ioapic_mapping, IOAPIC_MMIO_BASE, IOAPIC_MMIO_SIZE,
                        ioapic_mmio_readb, ioapic_mmio_readw, ioapic_mmio_readl,
                        ioapic_mmio_writeb, ioapic_mmio_writew, ioapic_mmio_writel,
                        NULL, MEM_MAPPING_EXTERNAL, dev);

        apic_platform_update_lapic_mapping(dev->apic);
        ioapic_log("IOAPIC: uniprocessor APIC probe mode enabled\n");
    }

    io_sethandler(0x80, 1,
                  NULL, NULL, NULL, ioapic_write, NULL, NULL, dev);

    return dev;
}

static const device_config_t ioapic_config[] = {
  // clang-format off
    {
        .name           = "expose_mp_tables",
        .description    = "Expose BIOS MP tables",
        .type           = CONFIG_BINARY,
        .default_int    = 0,
        .default_string = NULL,
        .file_filter    = NULL,
        .spinner        = { 0 },
        .selection      = { { 0 } },
        .bios           = { { 0 } }
    },
    {
        .name           = "uniprocessor_apic",
        .description    = "Enable uniprocessor APIC probe mode",
        .type           = CONFIG_BINARY,
        .default_int    = 0,
        .default_string = NULL,
        .file_filter    = NULL,
        .spinner        = { 0 },
        .selection      = { { 0 } },
        .bios           = { { 0 } }
    },
    {
        .name           = "multi_lapic",
        .description    = "Enable guarded multi-LAPIC active count",
        .type           = CONFIG_BINARY,
        .default_int    = 0,
        .default_string = NULL,
        .file_filter    = NULL,
        .spinner        = { 0 },
        .selection      = { { 0 } },
        .bios           = { { 0 } }
    },
    {
        .name           = "multi_lapic_mp_cpus",
        .description    = "Enable guarded MP-table AP CPU fixups",
        .type           = CONFIG_BINARY,
        .default_int    = 0,
        .default_string = NULL,
        .file_filter    = NULL,
        .spinner        = { 0 },
        .selection      = { { 0 } },
        .bios           = { { 0 } }
    },
    {
        .name           = "ap_startup",
        .description    = "Enable guarded AP startup requests",
        .type           = CONFIG_BINARY,
        .default_int    = 0,
        .default_string = NULL,
        .file_filter    = NULL,
        .spinner        = { 0 },
        .selection      = { { 0 } },
        .bios           = { { 0 } }
    },
    {
        .name           = "second_cpu_card",
        .description    = "Install second P65UP5 CPU card",
        .type           = CONFIG_BINARY,
        .default_int    = 0,
        .default_string = NULL,
        .file_filter    = NULL,
        .spinner        = { 0 },
        .selection      = { { 0 } },
        .bios           = { { 0 } }
    },
    {
        .name           = "ap_safe_park",
        .description    = "Enable guarded AP safe-park after SIPI",
        .type           = CONFIG_BINARY,
        .default_int    = 0,
        .default_string = NULL,
        .file_filter    = NULL,
        .spinner        = { 0 },
        .selection      = { { 0 } },
        .bios           = { { 0 } }
    },
    {
        .name           = "ap_route_parked",
        .description    = "Allow interrupts to guarded parked APs",
        .type           = CONFIG_BINARY,
        .default_int    = 0,
        .default_string = NULL,
        .file_filter    = NULL,
        .spinner        = { 0 },
        .selection      = { { 0 } },
        .bios           = { { 0 } }
    },
    {
        .name           = "smp_probe",
        .description    = "Enable guarded two-CPU APIC probe preset",
        .type           = CONFIG_BINARY,
        .default_int    = 0,
        .default_string = NULL,
        .file_filter    = NULL,
        .spinner        = { 0 },
        .selection      = { { 0 } },
        .bios           = { { 0 } }
    },
    {
        .name           = "lapic_count",
        .description    = "Local APIC count",
        .type           = CONFIG_SPINNER,
        .default_int    = APIC_UP_LAPIC_COUNT,
        .default_string = NULL,
        .file_filter    = NULL,
        .spinner = {
            .min  = APIC_UP_LAPIC_COUNT,
            .max  = APIC_MAX_LAPICS,
            .step = 1
        },
        .selection      = { { 0 } },
        .bios           = { { 0 } }
    },
    { .name = "", .description = "", .type = CONFIG_END }
  // clang-format on
};

static const device_config_t ioapic_validated_up_config[] = {
  // clang-format off
    {
        .name           = "disable_apic",
        .description    = "Disable APIC",
        .type           = CONFIG_BINARY,
        .default_int    = 0,
        .default_string = NULL,
        .file_filter    = NULL,
        .spinner        = { 0 },
        .selection      = { { 0 } },
        .bios           = { { 0 } }
    },
    { .name = "", .description = "", .type = CONFIG_END }
  // clang-format on
};

const device_t ioapic_device = {
    .name          = "I/O Advanced Programmable Interrupt Controller",
    .internal_name = "ioapic",
    .flags         = DEVICE_ISA16,
    .local         = 0,
    .init          = ioapic_init,
    .close         = ioapic_close,
    .reset         = ioapic_device_reset,
    .available     = NULL,
    .speed_changed = NULL,
    .force_redraw  = NULL,
    .config        = ioapic_config
};

const device_t ioapic_ami_device = {
    .name          = "I/O Advanced Programmable Interrupt Controller (AMI)",
    .internal_name = "ioapic_ami",
    .flags         = DEVICE_ISA16,
    .local         = IOAPIC_POST_TRIGGER_AMI,
    .init          = ioapic_init,
    .close         = ioapic_close,
    .reset         = ioapic_device_reset,
    .available     = NULL,
    .speed_changed = NULL,
    .force_redraw  = NULL,
    .config        = ioapic_config
};

const device_t ioapic_validated_up_device = {
    .name          = "I/O Advanced Programmable Interrupt Controller",
    .internal_name = "ioapic",
    .flags         = DEVICE_ISA16,
    .local         = 0,
    .init          = ioapic_init,
    .close         = ioapic_close,
    .reset         = ioapic_device_reset,
    .available     = NULL,
    .speed_changed = NULL,
    .force_redraw  = NULL,
    .config        = ioapic_validated_up_config
};

const device_t validated_up_apic_board_device = {
    .name          = "Validated UP APIC",
    .internal_name = "validated_up_apic",
    .flags         = 0,
    .local         = 0,
    .init          = NULL,
    .close         = NULL,
    .reset         = NULL,
    .available     = NULL,
    .speed_changed = NULL,
    .force_redraw  = NULL,
    .config        = ioapic_validated_up_config
};
