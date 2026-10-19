/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles.
 *
 *          This file is part of the 86Box distribution.
 *
 *          Copyright 2026 skiretic.
 */
#include "ioapic_82093aa.h"

int
ioapic_82093aa_redir_level(uint32_t low)
{
    return !!(low & IOAPIC_82093AA_REDIR_TRIGGER);
}

int
ioapic_82093aa_redir_remote_irr_valid(uint32_t low)
{
    uint32_t delivery = low & IOAPIC_82093AA_REDIR_DELIV;

    /* RemoteIRR is meaningful only for level-triggered LAPIC deliveries that await EOI. */
    return ioapic_82093aa_redir_level(low) &&
           ((delivery == IOAPIC_82093AA_DELIVERY_FIXED) ||
            (delivery == IOAPIC_82093AA_DELIVERY_LOWEST));
}

int
ioapic_82093aa_redir_supported(uint32_t low)
{
    uint32_t delivery = low & IOAPIC_82093AA_REDIR_DELIV;

    return (delivery == IOAPIC_82093AA_DELIVERY_FIXED) ||
           (delivery == IOAPIC_82093AA_DELIVERY_LOWEST) ||
           (delivery == IOAPIC_82093AA_DELIVERY_SMI) ||
           (delivery == IOAPIC_82093AA_DELIVERY_NMI) ||
           (delivery == IOAPIC_82093AA_DELIVERY_INIT) ||
           (delivery == IOAPIC_82093AA_DELIVERY_EXTINT);
}

int
ioapic_82093aa_redir_active_low(uint32_t low)
{
    return !!(low & IOAPIC_82093AA_REDIR_POLARITY);
}

int
ioapic_82093aa_redir_smi(uint32_t low)
{
    return (low & IOAPIC_82093AA_REDIR_DELIV) == IOAPIC_82093AA_DELIVERY_SMI;
}

int
ioapic_82093aa_redir_extint(uint32_t low)
{
    return (low & IOAPIC_82093AA_REDIR_DELIV) == IOAPIC_82093AA_DELIVERY_EXTINT;
}

int
ioapic_82093aa_redir_physical_asserted(uint32_t low, int physical_high)
{
    /* Callers keep raw pin level; redirection polarity maps it to logical asserted. */
    return ioapic_82093aa_redir_active_low(low) ? !physical_high : !!physical_high;
}

uint32_t
ioapic_82093aa_redir_read_only_low_bits(uint32_t old_low, uint32_t new_low)
{
    uint32_t bits = 0;

    /* Guest writes cannot create stale RemoteIRR when new mode has no EOI owner. */
    if (ioapic_82093aa_redir_remote_irr_valid(new_low))
        bits |= old_low & IOAPIC_82093AA_REDIR_REMOTEIRR;

    return bits;
}

uint32_t
ioapic_82093aa_redir_sanitize_low(uint32_t low)
{
    uint32_t read_only = 0;

    /* Save/load sanitize mirrors MMIO writes, with valid in-service state preserved. */
    if (ioapic_82093aa_redir_remote_irr_valid(low))
        read_only |= low & IOAPIC_82093AA_REDIR_REMOTEIRR;

    return (low & IOAPIC_82093AA_REDIR_LOW_WRITE_MASK) | read_only;
}

uint32_t
ioapic_82093aa_pin_bit(uint8_t pin)
{
    return 1u << pin;
}

uint32_t
ioapic_82093aa_derive_raw_lines_from_logical(const uint32_t *redir, uint32_t pin_lines)
{
    uint32_t raw_lines = 0;

    /* Older saves kept logical pin assertion; rebuild raw physical levels via polarity. */
    for (uint8_t pin = 0; pin < IOAPIC_82093AA_REDIR_ENTRIES; pin++) {
        uint32_t pin_bit = ioapic_82093aa_pin_bit(pin);
        uint32_t low = redir[pin * 2];
        int asserted = !!(pin_lines & pin_bit);
        int physical_high = ioapic_82093aa_redir_active_low(low) ? !asserted : asserted;

        if (physical_high)
            raw_lines |= pin_bit;
    }

    return raw_lines & IOAPIC_82093AA_PIN_MASK;
}

const char *
ioapic_82093aa_redir_delivery_name(uint32_t low)
{
    switch (low & IOAPIC_82093AA_REDIR_DELIV) {
        case IOAPIC_82093AA_DELIVERY_FIXED:
            return "fixed";
        case IOAPIC_82093AA_DELIVERY_LOWEST:
            return "lowest";
        case IOAPIC_82093AA_DELIVERY_SMI:
            return "smi";
        case IOAPIC_82093AA_DELIVERY_NMI:
            return "nmi";
        case IOAPIC_82093AA_DELIVERY_INIT:
            return "init";
        case IOAPIC_82093AA_DELIVERY_EXTINT:
            return "extint";
        default:
            return "reserved";
    }
}
