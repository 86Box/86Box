// SPDX-License-Identifier: GPL-2.0-or-later
#include <cstdint>
#include <gtest/gtest.h>

extern "C" {
#include "x86_interrupt.h"

int i8018x_pcb_cascade;
uint8_t ioapic_irq_path_active;
uint32_t cpu_lapic_irr_live;
uint8_t cpu_lapic_extint_live[1];

static int apic_pending;
static int fallback_allowed;
static int deliverable_index;
static unsigned apic_queries;

int ioapic_external_interrupt_pending(void)
{
    apic_queries++;
    return apic_pending;
}

int ioapic_pic_fallback_allowed(void) { return fallback_allowed; }
int cpu_lapic_highest_deliverable_irr_index(uint8_t, int) { return deliverable_index; }
}

TEST(CpuInterrupt, PicOnlyMatchesControllerForEveryOutputValue)
{
    ioapic_irq_path_active = 0;
    apic_queries = 0;
    for (cascade = 0; cascade <= 1; cascade++)
        for (i8018x_pcb_cascade = 0; i8018x_pcb_cascade <= 1; i8018x_pcb_cascade++)
            for (unsigned master = 0; master <= 255; master++)
                for (unsigned slave = 0; slave <= 255; slave++) {
                    pic.int_pending = master;
                    pic2.int_pending = slave;
                    ASSERT_EQ(x86_external_interrupt_pending(), pic_any_pending())
                        << cascade << ':' << i8018x_pcb_cascade << ':' << master << ':' << slave;
                }
    EXPECT_EQ(apic_queries, 0u);
}

TEST(CpuInterrupt, ApicArbitrationPreservesLiveSourceAndFallbackChecks)
{
    ioapic_irq_path_active = 1;
    i8018x_pcb_cascade = 0;
    for (cascade = 0; cascade <= 1; cascade++)
        for (unsigned master = 0; master <= 1; master++)
            for (unsigned slave = 0; slave <= 1; slave++)
                for (unsigned extint = 0; extint <= 1; extint++)
                    for (unsigned irr = 0; irr <= 2; irr++)
                        for (deliverable_index = -1; deliverable_index <= 0; deliverable_index++)
                            for (apic_pending = 0; apic_pending <= 1; apic_pending++)
                                for (fallback_allowed = 0; fallback_allowed <= 1; fallback_allowed++) {
                                    pic.int_pending = master;
                                    pic2.int_pending = slave;
                                    cpu_lapic_extint_live[0] = extint;
                                    cpu_lapic_irr_live = irr;
                                    const bool legacy = master || (!cascade && slave);
                                    const bool live = legacy || extint ||
                                                      ((irr & 1) && deliverable_index >= 0);
                                    const bool expected = live &&
                                                          (apic_pending || (fallback_allowed && legacy));
                                    EXPECT_EQ(!!x86_external_interrupt_pending(), expected);
                                }
}
