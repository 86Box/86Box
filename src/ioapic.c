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
#include <86box/ioapic_routes.h>
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
    uint8_t mp_table_logged;
    uint8_t mp_table_skip_logged;
    uint8_t post_trigger; /* the POST code that means "about to boot" */

    /* What kind of I/O APIC this is. An 82093AA unless the board says
       otherwise; the 82374 ESC's has sixteen pins, reports version
       000F0011h, has no SMI# input and sits wherever APICBASE puts it. */
    uint8_t  pins;
    uint8_t  smi_pin; /* 0xff: none */
    uint8_t  is_esc;
    uint8_t  lapic_only; /* no I/O APIC: the local APIC's path alone */
    uint8_t  is_via;     /* the VT82C686A/B's own (IOAPIC_VARIANT_VIA) */
    uint8_t  window_off; /* the chipset decodes no window (ioapic_set_window_enabled) */
    uint8_t  intr_masked; /* the ESC's INTRC: the 8259's INTR held inactive */
    uint32_t version_reg;
    uint32_t mmio_base;
    uint32_t mmio_size;

    uint8_t       lowest_priority_next;
    apic_platform_t *apic;

    uint8_t       ioapic_id;
    uint8_t       ioapic_index;
    mem_mapping_t ioapic_mapping;
    uint32_t      ioapic_redir[24 * 2];
    uint32_t      ioapic_pin_raw_lines;
    uint32_t      ioapic_pin_lines;
    uint32_t      ioapic_pin_level;
    /* Each input is one wire, asserted while any of its sources drives it:
       the ISA/EISA IRQ lines through the 8259 side, PCI interrupts wired
       to their own inputs, and the 8259's INTR on INTIN0. */
    uint32_t      wire_legacy;
    uint32_t      wire_pci;
    uint32_t      wire_intr;
    /* The southbridge's own interrupt outputs on inputs of their own (the
       VIA 686A/B's Rx58, ioapic_chip_irq). */
    uint32_t      wire_chip;
    uint8_t       pic_intr; /* the 8259's INTR output */
    uint8_t       pin0_smi; /* ESC PAC SMIRC: INTIN0 carries SMI, not INTR */
    uint8_t       legacy_routes_set;
    uint8_t       legacy_routes_wired; /* the board's wiring, which a table cannot change */
    /* Per entry, a message refused and still to send (DELIVS, 82093AA
       3.2.4 bit 12): "the IOAPIC continually polls the 24 interrupts in a
       rotating fashion" (1.0), so each is tried again, none blocking the
       others. */
    uint32_t      ioapic_send_pending;
    uint8_t       ioapic_intin_poll_next;
    uint8_t       ioapic_smiout_asserted;
    uint8_t       ioapic_arb_id;
    uint8_t       legacy_irq_default_to_pin[IOAPIC_LEGACY_IRQ_COUNT];
    uint8_t       legacy_irq_to_pin[IOAPIC_LEGACY_IRQ_COUNT];
    uint8_t       pirq_to_intin[4]; /* the board's PIRQ wiring, 0xff for none */
    uint8_t       ioapic_masked_irq_log_count[24];
    uint8_t       ioapic_irq_log_count[24];
    uint8_t       ioapic_deliver_log_count[24];
    uint8_t       ioapic_dest_mismatch_log_count[24];
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
static void ioapic_deliver_pin(ioapic_t *dev, uint8_t pin, int asserted);
static void ioapic_eoi(ioapic_t *dev, uint8_t vector);
static void ioapic_update_intin23_smiout(ioapic_t *dev);
static void ioapic_mp_legacy_irq_route(void *priv, uint8_t irq, uint8_t intin);

static void
ioapic_disable_apic_mappings(ioapic_t *dev)
{
    apic_platform_disable_lapic_mappings(dev->apic);
    if (!dev->lapic_only)
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
    /* Until the board says otherwise, the 82093AA's ISA inputs. */
    static const uint8_t compat_irq_to_pin[IOAPIC_LEGACY_IRQ_COUNT] = IOAPIC_LEGACY_IRQ_ROUTE_ISA;

    memcpy(irq_to_pin, compat_irq_to_pin, IOAPIC_LEGACY_IRQ_COUNT);
}

static void
ioapic_init_legacy_irq_pins(ioapic_t *dev)
{
    if (!dev->legacy_routes_set)
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

/* LINT0 sees the 8259's INTR whatever the I/O APIC is programmed to do:
   "it is the programmer's responsibility to make sure that the same
   interrupt input signal is not handled by both interrupt controllers"
   (82093AA 1.0). Only the ESC's INTRC holds it off. */
static int
ioapic_lint0_pic_fallback_pending(ioapic_t *dev)
{
    if (dev->intr_masked)
        return 0;

    return pic.int_pending;
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
ioapic_external_interrupt_pending(void)
{
    ioapic_t *dev = ioapic_global;

    if ((dev == NULL) || !dev->uniprocessor_apic)
        return 0;

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

    /* INTRC "forces INTR to the inactive state" (ESC 3.1.17), on this
       path as on LINT0. */
    if (dev->intr_masked)
        return 0;

    /* Only an APIC that is off altogether (APIC_BASE bit 11) leaves the
       pins as plain INTR and NMI. A software-disabled one "automatically
       sets all mask bits in the LVT entries" (Pentium Vol. 3, 19.3.1.11),
       LINT0 with the rest: no INTR, as LINT1 takes no NMI. */
    return !apic_platform_current_base_enabled(dev->apic);
}

int
ioapic_lint1_raise(void)
{
    ioapic_t *dev = ioapic_global;

    if ((dev == NULL) || !dev->uniprocessor_apic)
        return 0;

    /* An APIC that is off altogether leaves the pin a plain NMI, as it
       leaves LINT0 a plain INTR (ioapic_pic_fallback_allowed). */
    if (!apic_platform_current_base_enabled(dev->apic))
        return 0;

    /* The NMI line reaches LINT1 on every socket, and only there: an APIC
       with LINT1 masked (software disabled masks every LVT, Pentium Vol. 3,
       19.3.1.11) takes nothing from it. */
    (void) apic_platform_lint_deliver_all(dev->apic, LAPIC_REG_LVT_LINT1);
    return 1;
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

/* An NMI entry's message: from the APIC bus, so port 70h does not gate it
   (NMI_TAKEABLE). */
static void
ioapic_nmi_apic(void)
{
    cpu_nmi_raise_core(1);
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
    return !!(dev->ioapic_send_pending & ioapic_82093aa_pin_bit(pin));
}

static void
ioapic_latch_pin_delivery_pending(ioapic_t *dev, uint8_t pin)
{
    dev->ioapic_send_pending |= ioapic_82093aa_pin_bit(pin);
}

static void
ioapic_clear_pin_delivery_pending(ioapic_t *dev, uint8_t pin)
{
    dev->ioapic_send_pending &= ~ioapic_82093aa_pin_bit(pin);
}

static int
ioapic_delivery_failure_retryable(apic_bus_result_t result)
{
    return (result.status == APIC_BUS_NO_TARGET) ||
           (result.status == APIC_BUS_REJECTED) ||
           (result.status == APIC_BUS_RETRY);
}

static int
ioapic_pin_poll_candidate(const ioapic_t *dev, uint8_t pin)
{
    uint32_t low = dev->ioapic_redir[pin * 2];

    if (ioapic_redir_masked(low) || !ioapic_82093aa_redir_supported(low))
        return 0;

    if (ioapic_pin_delivery_pending(dev, pin))
        return 1;

    return ioapic_pin_pending_after_redir_write(dev, pin);
}

static void
ioapic_retry_pending_pin(ioapic_t *dev, uint8_t pin)
{
    uint32_t low = dev->ioapic_redir[pin * 2];
    int      level = ioapic_82093aa_redir_remote_irr_valid(low);

    if (!ioapic_pin_delivery_pending(dev, pin))
        return;

    if (ioapic_redir_masked(low) || !ioapic_82093aa_redir_supported(low)) {
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

    ioapic_deliver_pin(dev, pin, 1);
}

/* One round of the rotating poll: every entry with a message to send (a
   refused one, or a level input asserted with no Remote IRR) tries once,
   starting after the last one sent. */
static void
ioapic_retry_pending_transaction(ioapic_t *dev)
{
    uint8_t start = dev->ioapic_intin_poll_next;

    for (uint8_t i = 0; i < dev->pins; i++) {
        uint8_t pin = (start + i) % dev->pins;

        if (!ioapic_pin_poll_candidate(dev, pin))
            continue;

        if (ioapic_pin_delivery_pending(dev, pin))
            ioapic_retry_pending_pin(dev, pin);
        else
            ioapic_deliver_pin(dev, pin, 1);
        if (!ioapic_pin_delivery_pending(dev, pin))
            dev->ioapic_intin_poll_next = (pin + 1) % dev->pins;
    }
}

/* A local APIC became able to take what it refused (enabled, addressed
   anew): the I/O APIC's refused messages go again. */
void
ioapic_retry_deliveries(void)
{
    ioapic_t *dev = ioapic_global;

    if ((dev != NULL) && dev->uniprocessor_apic && dev->ioapic_send_pending)
        ioapic_retry_pending_transaction(dev);
}

static void
ioapic_set_smiout_asserted(ioapic_t *dev, int asserted)
{
    asserted = !!asserted;
    if (dev->ioapic_smiout_asserted == asserted)
        return;

    dev->ioapic_smiout_asserted = asserted;
}

static void
ioapic_update_intin23_smiout(ioapic_t *dev)
{
    uint32_t low = dev->ioapic_redir[dev->smi_pin * 2];

    /* INTIN23/SMI# mask routes the active-low input to SMIOUT# instead of APIC bus. */
    if (ioapic_redir_masked(low)) {
        ioapic_set_smiout_asserted(dev, !ioapic_pin_raw_high(dev, dev->smi_pin));
        return;
    }

    ioapic_set_smiout_asserted(dev, 0);
}

static int
ioapic_pin_pending_after_redir_write(const ioapic_t *dev, uint8_t pin)
{
    uint32_t low = dev->ioapic_redir[pin * 2];

    return (ioapic_82093aa_redir_remote_irr_valid(low) && ioapic_pin_asserted(dev, pin)) ||
           ((pin == dev->smi_pin) && ioapic_82093aa_redir_smi(low) &&
            !ioapic_redir_masked(low) && ioapic_pin_asserted(dev, pin));
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
               ioapic_pin_asserted(dev, pin), !!(dev->ioapic_pin_level & ioapic_82093aa_pin_bit(pin)),
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

/* The entry's message sent. An edge on an entry whose message is still
   to send is that same message: "a new edge on that Interrupt input pin
   will not be recognized until" it is accepted (82093AA 3.2.4 bit 12).
   Sending it again is its retry. */
static void
ioapic_deliver_pin(ioapic_t *dev, uint8_t pin, int asserted)
{
    uint32_t *lowp = &dev->ioapic_redir[pin * 2];
    uint32_t  low  = *lowp;
    uint32_t  high = dev->ioapic_redir[(pin * 2) + 1];
    uint8_t   vector = low & IOAPIC_REDIR_VECTOR;
    uint32_t  delivery = low & IOAPIC_REDIR_DELIV;
    int       level  = ioapic_82093aa_redir_remote_irr_valid(low);
    lapic_target_info_t targets;

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

        (void) result; /* the log's */
        /* Nobody to take it: held Send Pending (82093AA 3.2.4 bit 12). */
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
            ioapic_deliver_current_lapic_direct(&targets, ioapic_nmi_apic);

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
        if (!apic_platform_ioapic_deliver_init(dev->apic, &targets)) {
            ioapic_latch_pin_delivery_pending(dev, pin);
            return;
        }
        ioapic_clear_pin_delivery_pending(dev, pin);
        ioapic_apic_bus_message_complete(dev);
        if (ioapic_log_first(&dev->ioapic_deliver_log_count[pin], 8))
            ioapic_log("IOAPIC: delivered INIT pin=%u count=%u low=%08x high=%08x\n",
                       pin, dev->ioapic_deliver_log_count[pin], low, high);
        return;
    }

    /* ExtINT goes out whatever the 8259 holds: the INTA that follows gets
       its answer, the spurious vector if it has nothing (82093AA, delivery
       mode 111). */
    if (ioapic_82093aa_redir_extint(low)) {
        if (!apic_platform_ioapic_mark_extint(dev->apic, &targets, pin)) {
            ioapic_latch_pin_delivery_pending(dev, pin);
            return;
        }
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
        /* Every entry's message waits in Send Pending until a processor
           takes it (82093AA 3.2.4 bit 12), whatever its delivery mode. */
        if (ioapic_delivery_failure_retryable(result))
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
ioapic_raw_intin_device(ioapic_t *dev, uint8_t pin, int physical_high)
{
    uint32_t pin_bit = ioapic_82093aa_pin_bit(pin);
    uint32_t was_asserted = dev->ioapic_pin_lines & pin_bit;
    uint32_t low = dev->ioapic_redir[pin * 2];
    /* "The trigger mode field indicates the type of signal on the
       interrupt pin that triggers an interrupt" (82093AA 3.2.4 bit 15):
       the entry decides, not the source. An edge entry sends once per
       inactive-to-active transition. */
    int effective_level = ioapic_82093aa_redir_remote_irr_valid(low);
    int asserted = ((pin == dev->smi_pin) && ioapic_82093aa_redir_smi(low)) ?
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
        if (pin == dev->smi_pin)
            ioapic_update_intin23_smiout(dev);
        return;
    }

    if (effective_level || (asserted && !was_asserted))
        ioapic_deliver_pin(dev, pin, asserted);

    if (pin == dev->smi_pin)
        ioapic_update_intin23_smiout(dev);
}

/* The pin's wire, the OR of its sources, as the input sees it through the
   entry's polarity. */
static void
ioapic_wire_drive(ioapic_t *dev, uint8_t pin)
{
    int      asserted = !!((dev->wire_legacy | dev->wire_pci | dev->wire_intr | dev->wire_chip) & ioapic_82093aa_pin_bit(pin));
    uint32_t low      = dev->ioapic_redir[pin * 2];

    /* INTIN23/SMI# has no source here: it stays at its idle level. */
    if (pin == dev->smi_pin)
        return;
    ioapic_raw_intin_device(dev, pin, ioapic_82093aa_redir_active_low(low) ? !asserted : asserted);
}

/* One source of a pin: a level source holds its bit, an edge source
   pulses it (the emulated edge devices raise and may never lower). */
static void
ioapic_wire_set(ioapic_t *dev, uint32_t *src, uint8_t pin, int level, int set)
{
    uint32_t bit = ioapic_82093aa_pin_bit(pin);

    if (level) {
        if (!!(*src & bit) == !!set)
            return;
        *src ^= bit;
        ioapic_wire_drive(dev, pin);
    } else if (set) {
        *src |= bit;
        ioapic_wire_drive(dev, pin);
        *src &= ~bit;
        ioapic_wire_drive(dev, pin);
    } else if (*src & bit) {
        *src &= ~bit;
        ioapic_wire_drive(dev, pin);
    }
}

/* A source wired to an input of its own: a PCI interrupt learned from the
   MP table (counted in pci.c), or the ESC's SMI on INTIN0. The pin is
   sampled whatever any local APIC is doing. */
void
ioapic_intin_irq(uint8_t pin, int level, int set)
{
    ioapic_t *dev = ioapic_global;
    uint32_t  low;

    if ((dev == NULL) || !dev->uniprocessor_apic || (pin >= dev->pins))
        return;

    low = dev->ioapic_redir[pin * 2];
    (void) low;
    if (ioapic_log_first(&dev->ioapic_irq_log_count[pin], 8))
        ioapic_log("IOAPIC: intin pin=%u %s mode=%s redir_mask=%u vector=%02x delivery=%s\n",
                   pin, set ? "raise" : "lower", level ? "level" : "edge",
                   ioapic_redir_masked(low), low & IOAPIC_REDIR_VECTOR,
                   ioapic_82093aa_redir_delivery_name(low));

    ioapic_wire_set(dev, &dev->wire_pci, pin, level, set);
}

/* An ISA/EISA IRQ line changed (picint_common: for a level source, the
   wired-OR of every source on it). */
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

        if (pin >= dev->pins)
            continue;

        uint32_t low = dev->ioapic_redir[pin * 2];

        if (ioapic_redir_masked(low) && set)
            ioapic_log_masked_irq(dev, irq, pin, level, set, low);
        if (set && ioapic_log_first(&dev->ioapic_irq_log_count[pin], 8))
            ioapic_log("IOAPIC: legacy irq=%u -> pin=%u raise mode=%s redir_mask=%u vector=%02x delivery=%s\n",
                       irq, pin, level ? "level" : "edge",
                       ioapic_redir_masked(low), low & IOAPIC_REDIR_VECTOR,
                       ioapic_82093aa_redir_delivery_name(low));

        ioapic_wire_set(dev, &dev->wire_legacy, pin, level, set);
    }
}

/* INTIN0 is the 8259's INTR (MP spec 1.1, Table 5-2; 82093AA 2.4; ESC
   Fig. 25: the internal 8259's output), replaced by SMI while the ESC's
   SMIRC routes SMI there. INTRC masks the ESC's external INTR pin only
   (ESC 3.1.17), not this input. */
static void
ioapic_wire_intr_update(ioapic_t *dev)
{
    int active = dev->pic_intr && !dev->pin0_smi;

    if (dev->pins && (!!(dev->wire_intr & 1) != active)) {
        dev->wire_intr = active ? 1 : 0;
        ioapic_wire_drive(dev, 0);
    }
}

void
ioapic_pic_intr(int active)
{
    ioapic_t *dev = ioapic_global;

    if ((dev == NULL) || !dev->uniprocessor_apic)
        return;

    dev->pic_intr = !!active;
    ioapic_wire_intr_update(dev);
}

/* The input PIRQ pirq (0-3, A-D) is wired to on this board, with the APIC
   path on; 0xff where it is not wired here, or the path is off. */
uint8_t
ioapic_pirq_intin(uint8_t pirq)
{
    const ioapic_t *dev = ioapic_global;

    if ((dev == NULL) || !dev->uniprocessor_apic || (pirq >= 4))
        return IOAPIC_LEGACY_IRQ_NC;

    return dev->pirq_to_intin[pirq];
}

/* Whether a PCI interrupt the MP table puts on intin reaches the I/O APIC
   on a wire of its own, apart from the IRQ line it is steered to
   (irq_line, 0xff for none). On the ESC the I/O APIC's inputs are the IRQ
   lines themselves (ESC 10.0: devices "inject interrupts into the system
   by asserting one of the interrupt lines to the I/O APIC"), and a PCI
   interrupt learned onto the pin of the IRQ it is steered to is that
   same wire, already driven through the 8259 side. */
int
ioapic_pci_intin_own_wire(uint8_t intin, uint8_t irq_line)
{
    ioapic_t *dev = ioapic_global;

    if ((dev == NULL) || !dev->uniprocessor_apic || dev->is_esc)
        return 0;
    if ((irq_line < IOAPIC_LEGACY_IRQ_COUNT) && (ioapic_legacy_irq_to_pin(dev, irq_line) == intin))
        return 0;
    return 1;
}

void
ioapic_set_pin0_smi(void *ioapic, int smi)
{
    ioapic_t *dev = (ioapic_t *) ioapic;

    if (dev == NULL)
        return;

    dev->pin0_smi = !!smi;
    ioapic_wire_intr_update(dev);
}

int
ioapic_legacy_irq_enabled(void)
{
    ioapic_t *dev = ioapic_global;

    return (dev != NULL) && dev->uniprocessor_apic;
}

static void
ioapic_sanitize_loaded_redir(ioapic_t *dev)
{
    for (uint8_t pin = 0; pin < dev->pins; pin++) {
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
    apic_platform_set_lapic_policy(dev->apic, dev->uniprocessor_apic);
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
    dev->ioapic_send_pending = 0;
    dev->ioapic_intin_poll_next = 0;
    dev->ioapic_smiout_asserted = (state->version >= 12) ? !!state->ioapic_smiout_asserted : 0;
    ioapic_sanitize_loaded_redir(dev);
    if (dev->smi_pin < dev->pins)
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
    dev->lowest_priority_next = (state->version >= 4) ? (state->lowest_priority_next % APIC_MAX_LAPICS) : 0;

    ioapic_update_apic_platform_policy(dev);

    if (!apic_platform_lapic_state_load(dev->apic, state))
        return 0;
    /* Loaded routes are not the ones the remembered table taught. */
    apic_platform_mp_forget(dev->apic);

    if (!ioapic_chip_state_load(dev, state))
        return 0;

    if (state->version >= 13)
        pci_apic_load_intin_routes(state->pci_intin_routes, state->pci_intin_route_count);

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
    for (uint8_t pin = 0; pin < dev->pins; pin++) {
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
            ioapic_deliver_pin(dev, pin, 1);
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
    dev->ioapic_send_pending = 0;
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
        return dev->version_reg;
    /* The VT82C686B's "APIC Arbitration ID ... always reads 00h" (data
       sheet 1.8, p. 116). */
    if (index == 0x02)
        return dev->is_via ? 0 : (((uint32_t) dev->ioapic_arb_id) << 24);
    if ((index >= 0x10) && (index < (0x10 + (dev->pins * 2)))) {
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

    if ((index >= 0x10) && (index < (0x10 + (dev->pins * 2)))) {
        uint8_t redir_index = index - 0x10;
        uint8_t pin         = redir_index >> 1;

        if (redir_index & 1) {
            val &= IOAPIC_REDIR_HIGH_WRITE_MASK;
        } else {
            val = (val & IOAPIC_REDIR_LOW_WRITE_MASK) |
                  ioapic_82093aa_redir_read_only_low_bits(dev->ioapic_redir[redir_index], val);
        }
        dev->ioapic_redir[redir_index] = val;
        if (!(redir_index & 1)) {
            /* Polarity and trigger mode are the entry's: the wire is seen
               through them afresh. */
            ioapic_wire_drive(dev, pin);
        }

        ioapic_log_redir_entry(dev, pin, (redir_index & 1) ? "redir high" : "redir low");

        if (pin == dev->smi_pin)
            ioapic_update_intin23_smiout(dev);

        if (ioapic_pin_delivery_pending(dev, pin)) {
            ioapic_retry_pending_pin(dev, pin);
            return;
        }

        if (ioapic_pin_pending_after_redir_write(dev, pin))
            ioapic_deliver_pin(dev, pin, 1);
        return;
    }

    ioapic_log("IOAPIC: ignored window write index=%02x val=%08x\n", index, val);
}

static uint32_t
ioapic_mmio_readl(uint32_t addr, void *priv)
{
    ioapic_t *dev = (ioapic_t *) priv;
    uint32_t  reg = (addr - dev->mmio_base) & 0xff;
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
    uint32_t  reg = (addr - dev->mmio_base) & 0xff;

    if (ioapic_log_first(&dev->ioapic_mmio_write_log_count[reg >> 2], 8))
        ioapic_log("IOAPIC: write mmio=%02x index=%02x val=%08x count=%u\n",
                   reg, dev->ioapic_index, val, dev->ioapic_mmio_write_log_count[reg >> 2]);

    if (reg == 0x00)
        dev->ioapic_index = val & 0xff;
    else if (reg == 0x10)
        ioapic_window_write(dev, val);
    else if (dev->is_via && (reg == 0x20)) {
        /* "APIC IRQ Pin Assertion ... IRQ # for this interrupt. Valid
           values are 0-23 only" (VT82C686B data sheet 1.8, p. 116): an
           interrupt on that pin, sent as its entry says. */
        if ((val & 0x1f) < dev->pins)
            ioapic_deliver_pin(dev, val & 0x1f, 1);
    } else if (dev->is_via && (reg == 0x40)) {
        /* "When a write is issued to this register, the APIC will check this
           field and compare it with the vector field for each entry ...
           When a match is found, the Remote_IRR bit for that I/O
           Redirection Entry will be cleared" (p. 116): an EOI, as a local
           APIC's would be. */
        ioapic_eoi(dev, val & 0xff);
    }
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

    if ((dev == NULL) || (irq >= IOAPIC_LEGACY_IRQ_COUNT) || (intin >= dev->pins))
        return;

    /* Where the inputs are the board's wiring, a table cannot re-wire
       them: the software is told, the model keeps the wires. */
    if (dev->legacy_routes_wired)
        return;

    if (dev->legacy_irq_default_to_pin[irq] != intin) {
        ioapic_log("IOAPIC: MP learned legacy irq=%u -> pin=%u (old pin=%u)\n",
                   irq, intin, dev->legacy_irq_default_to_pin[irq]);
        dev->legacy_irq_default_to_pin[irq] = intin;
        dev->legacy_irq_to_pin[irq] = intin;
    }
    dev->legacy_routes_set = 1;
}

static void
ioapic_mp_pci_irq_route(void *priv, uint8_t bus, uint8_t device, uint8_t intpin, uint8_t intin)
{
    ioapic_t *dev = (ioapic_t *) priv;

    if ((dev == NULL) || (intpin >= PCI_INT_PINS_NUM) || (intin >= dev->pins))
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

    /* The trigger is an ordinary POST value -- AMI's is 00h, which DOS and
       Linux write to port 80h all the time as a delay -- so everything else
       is turned away here, and the platform remembers where the table is. */
    if ((dev == NULL) || (val != dev->post_trigger))
        return;
    /* The tables are built by the BIOS's POST, in real mode; an operating
       system's port 80h delays come from protected mode, Windows 2000's by
       the thousand a second, and each walked the whole MP table again. */
    if (cr0 & 1)
        return;

    policy.expose_mp_tables = dev->expose_mp_tables;
    policy.post_trigger = dev->post_trigger;
    policy.mp_table_logged = &dev->mp_table_logged;
    policy.mp_table_skip_logged = &dev->mp_table_skip_logged;
    policy.legacy_irq_route_priv = dev;
    policy.legacy_irq_route_handler = ioapic_mp_legacy_irq_route;
    policy.pci_irq_route_priv = dev;
    policy.pci_irq_route_handler = ioapic_mp_pci_irq_route;
    /* Port 0x80 hook triggers MP-table patch policy; platform owns BIOS table edits. */
    (void) apic_platform_mp_table_post(dev->apic, val, &policy);
}

static void
ioapic_reset(ioapic_t *dev)
{
    ioapic_lapic_reset(dev);
    dev->intr_masked = 0; /* the ESC's PAC comes up 00h too */
    dev->ioapic_id      = IOAPIC_ID_RESET;
    dev->ioapic_index   = 0;
    dev->ioapic_arb_id  = 0;
    dev->ioapic_pin_lines = 0;
    dev->wire_legacy = dev->wire_pci = dev->wire_intr = dev->wire_chip = 0;
    dev->pic_intr = dev->pin0_smi = 0;
    /* Every input idle; INTIN23/SMI# is active low, so idle is high
       (82093AA 4.1). */
    dev->ioapic_pin_raw_lines = (dev->smi_pin < dev->pins) ? ioapic_82093aa_pin_bit(dev->smi_pin) : 0;
    dev->ioapic_pin_level = 0;
    ioapic_set_smiout_asserted(dev, 0);
    dev->lowest_priority_next = 0;
    dev->mp_table_logged = 0;
    dev->mp_table_skip_logged = 0;
    memset(dev->ioapic_masked_irq_log_count, 0, sizeof(dev->ioapic_masked_irq_log_count));
    memset(dev->ioapic_irq_log_count, 0, sizeof(dev->ioapic_irq_log_count));
    memset(dev->ioapic_deliver_log_count, 0, sizeof(dev->ioapic_deliver_log_count));
    memset(dev->ioapic_dest_mismatch_log_count, 0, sizeof(dev->ioapic_dest_mismatch_log_count));
    memset(dev->ioapic_extint_deliver_log_count, 0, sizeof(dev->ioapic_extint_deliver_log_count));
    memset(dev->ioapic_smi_vector_log_count, 0, sizeof(dev->ioapic_smi_vector_log_count));
    memset(dev->ioapic_eoi_log_count, 0, sizeof(dev->ioapic_eoi_log_count));
    memset(dev->ioapic_redir_log_count, 0, sizeof(dev->ioapic_redir_log_count));
    memset(dev->ioapic_mmio_read_log_count, 0, sizeof(dev->ioapic_mmio_read_log_count));
    memset(dev->ioapic_mmio_write_log_count, 0, sizeof(dev->ioapic_mmio_write_log_count));
    ioapic_init_legacy_irq_pins(dev);
    apic_platform_mp_forget(dev->apic);

    ioapic_reset_redir(dev);
}

/* The board reset its processor (RESET, or the INIT pin the keyboard
   controller or port 92h pulses), not a device reset: the hook's
   remembered MP table is forgotten as at a device reset, so the table the
   BIOS builds next is looked for wherever it is. */
void
ioapic_processor_reset(void)
{
    if (ioapic_global != NULL)
        apic_platform_mp_forget(ioapic_global->apic);
}

void
ioapic_set_legacy_irq_routing(void *ioapic, const ioapic_legacy_irq_routing_t *routing)
{
    ioapic_t *dev = (ioapic_t *) ioapic;

    if ((dev == NULL) || (routing == NULL))
        return;

    /* A value past the last pin is kept as it is: that IRQ is not wired
       to this I/O APIC at all (IRQ2, the cascade, has no pin). */
    for (uint8_t irq = 0; irq < IOAPIC_LEGACY_IRQ_COUNT; irq++) {
        uint8_t pin = routing->irq_to_intin[irq];

        if ((pin < dev->pins) || (pin == IOAPIC_LEGACY_IRQ_NC)) {
            dev->legacy_irq_default_to_pin[irq] = pin;
            dev->legacy_irq_to_pin[irq]         = pin;
        }
    }
    for (uint8_t pirq = 0; pirq < 4; pirq++) {
        uint8_t pin = routing->pirq_to_intin[pirq];

        dev->pirq_to_intin[pirq] = ((pin != 0) && (pin < dev->pins)) ? pin : IOAPIC_LEGACY_IRQ_NC;
    }
    dev->legacy_routes_set   = 1;
    dev->legacy_routes_wired = 1;
}

/* The window where it is, while the APIC path is on. */
static void
ioapic_apply_mapping(ioapic_t *dev)
{
    if (!dev->uniprocessor_apic || dev->lapic_only)
        return;
    if (dev->window_off)
        mem_mapping_disable(&dev->ioapic_mapping);
    else
        mem_mapping_set_addr(&dev->ioapic_mapping, dev->mmio_base, dev->mmio_size);
}

/* The ESC moves its I/O APIC with APICBASE; this is where it says so. The
   window is only mapped while the APIC path is on, but the address is
   kept either way. */
void
ioapic_set_mmio_base(void *ioapic, uint32_t base)
{
    ioapic_t *dev = (ioapic_t *) ioapic;

    if (dev == NULL)
        return;

    dev->mmio_base = base;
    ioapic_apply_mapping(dev);
}

/* The chipset holding the 8259's INTR output inactive (the ESC's INTRC). */
void
ioapic_set_intr_masked(void *ioapic, int masked)
{
    ioapic_t *dev = (ioapic_t *) ioapic;

    if (dev == NULL)
        return;

    dev->intr_masked = !!masked;
    ioapic_wire_intr_update(dev);
}

/* Whether the chipset decodes the window at all (the VT82C686A/B's Internal
   APIC Enable, Rx77[4]). The I/O APIC keeps its state either way. */
void
ioapic_set_window_enabled(void *ioapic, int enabled)
{
    ioapic_t *dev = (ioapic_t *) ioapic;

    if (dev == NULL)
        return;

    dev->window_off = !enabled;
    ioapic_apply_mapping(dev);
}

/* A level output of the southbridge itself onto an input of its own: the
   VIA 686A/B steer their on-chip interrupts to APIC[23:16] through Rx58.
   It is one more source of that input's wire, apart from whatever PCI or
   ISA line also drives it. */
void
ioapic_chip_irq(void *ioapic, uint8_t pin, int set)
{
    ioapic_t *dev = (ioapic_t *) ioapic;

    if ((dev == NULL) || !dev->uniprocessor_apic || (pin >= dev->pins))
        return;

    ioapic_wire_set(dev, &dev->wire_chip, pin, 1, set);
}

int
ioapic_apic_path_active(const void *ioapic)
{
    const ioapic_t *dev = (const ioapic_t *) ioapic;

    return (dev != NULL) && dev->uniprocessor_apic;
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

    if (!dev->lapic_only)
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
    /* The board's default: an APIC path, unless disabled or the processor
       fitted has no local APIC to answer at FEE00000h (a K5, K6, 6x86 or
       WinChip in a Pentium socket), which the board then treats as the
       APIC disabled. */
    dev->uniprocessor_apic =
        !!device_get_config_int("uniprocessor_apic") ||
        (!!(local & IOAPIC_TOPOLOGY_UP_APIC_DEFAULT) &&
         !device_get_config_int("disable_apic") &&
         !machine_get_config_int("disable_apic") &&
         cpu_apic_model_capable());
    dev->post_trigger = (local & IOAPIC_POST_TRIGGER_AMI) ? 0x00 : 0xff;
    dev->is_esc       = !!(local & IOAPIC_VARIANT_ESC);
    dev->lapic_only   = !!(local & IOAPIC_VARIANT_NONE);
    dev->is_via       = !!(local & IOAPIC_VARIANT_VIA);
    if (dev->lapic_only) {
        dev->pins        = 0;
        dev->smi_pin     = 0xff;
        dev->version_reg = 0;
        dev->mmio_size   = 0;
    } else if (dev->is_esc) {
        dev->pins        = 16;
        dev->smi_pin     = 0xff;
        dev->version_reg = 0x000f0011;
        dev->mmio_size   = 0x400;
    } else if (dev->is_via) {
        /* Twenty-four inputs, IRQ0-IRQ23 all of them interrupts, and
           version 00170011h (VT82C686B data sheet 1.8, p. 116). */
        dev->pins        = IOAPIC_REDIR_ENTRIES;
        dev->smi_pin     = 0xff;
        dev->version_reg = IOAPIC_VERSION_REG;
        dev->mmio_size   = IOAPIC_MMIO_SIZE;
    } else {
        dev->pins        = IOAPIC_REDIR_ENTRIES;
        dev->smi_pin     = IOAPIC_SMI_PIN;
        dev->version_reg = IOAPIC_VERSION_REG;
        dev->mmio_size   = IOAPIC_MMIO_SIZE;
    }
    dev->mmio_base = IOAPIC_MMIO_BASE;
    memset(dev->pirq_to_intin, IOAPIC_LEGACY_IRQ_NC, sizeof(dev->pirq_to_intin));
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
    if (!dev->lapic_only)
        lapic_bus_ioapic_attach(&dev->ioapic_arb_id, &dev->ioapic_id);
    ioapic_irq_path_active = dev->uniprocessor_apic;

    if (dev->uniprocessor_apic) {
        apic_platform_enable_lapic_mappings(dev->apic);
        if (!dev->lapic_only) {
            apic_platform_set_external_ioapic(dev->apic, dev, ioapic_lapic_eoi_adapter);
            mem_mapping_add(&dev->ioapic_mapping, dev->mmio_base, dev->mmio_size,
                            ioapic_mmio_readb, ioapic_mmio_readw, ioapic_mmio_readl,
                            ioapic_mmio_writeb, ioapic_mmio_writew, ioapic_mmio_writel,
                            NULL, MEM_MAPPING_EXTERNAL, dev);
            ioapic_apply_mapping(dev);
        }

        apic_platform_update_lapic_mapping(dev->apic);
        ioapic_log("IOAPIC: uniprocessor APIC probe mode enabled\n");
    }

    /* The MP table hook: a board with no I/O APIC has no table to keep. */
    if (!dev->lapic_only)
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

/* The I/O APIC inside the 82374 ESC. The ESC keeps the configuration side
   (APICBASE, PAC) and hands the register window over while the APIC path
   is on. */
const device_t ioapic_esc_device = {
    .name          = "I/O Advanced Programmable Interrupt Controller (82374 ESC)",
    .internal_name = "ioapic_esc",
    .flags         = DEVICE_ISA16,
    .local         = IOAPIC_VARIANT_ESC,
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

/* The processor's own local APIC on a board with no I/O APIC (the
   P6SBA): its timer, error and self interrupts, LINT0 the 8259's INTR and
   LINT1 the board's NMI take the same path as on a board with one. */
const device_t integrated_cpu_lapic_device = {
    .name          = "Integrated CPU Local APIC",
    .internal_name = "integrated_cpu_lapic",
    .flags         = 0,
    .local         = IOAPIC_VARIANT_NONE | IOAPIC_TOPOLOGY_UP_APIC_DEFAULT,
    .init          = ioapic_init,
    .close         = ioapic_close,
    .reset         = ioapic_device_reset,
    .available     = NULL,
    .speed_changed = NULL,
    .force_redraw  = NULL,
    .config        = NULL
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
