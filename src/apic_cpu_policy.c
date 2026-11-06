/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM PC
 *          systems and compatibles.
 *
 *          This file is part of the 86Box distribution.
 *
 *          Copyright 2026 skiretic.
 */
#include <stdint.h>

#define HAVE_STDARG_H
#include <86box/86box.h>
#include <86box/device.h>
#include <86box/chipset.h>

static uint8_t apic_cpu_lapic_ioapic_policy;

void
apic_cpu_lapic_set_ioapic_policy(int enabled)
{
    apic_cpu_lapic_ioapic_policy = !!enabled;
}

int
apic_cpu_lapic_ioapic_policy_enabled(void)
{
    return apic_cpu_lapic_ioapic_policy;
}

int
apic_cpu_lapic_enabled(void)
{
    return apic_cpu_lapic_ioapic_policy;
}

int
apic_cpu_feature_enabled_for_model(int model_capable)
{
    return !!model_capable && apic_cpu_lapic_enabled();
}
