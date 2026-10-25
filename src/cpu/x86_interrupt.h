/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          CPU-facing external interrupt arbitration helpers.
 */
#ifndef EMU_X86_INTERRUPT_H
#define EMU_X86_INTERRUPT_H

#include <86box/pic.h>

extern int ioapic_external_interrupt_pending(void);
extern int ioapic_external_interrupt_vector(void);
extern int ioapic_pic_fallback_allowed(void);
extern uint8_t ioapic_irq_path_active;

extern uint32_t cpu_lapic_irr_live;
extern int      cpu_lapic_highest_deliverable_irr_index(uint8_t lapic_index, int started);
extern uint8_t  cpu_lapic_extint_live[];

/* IOAPIC arbitrates first; PIC remains only while platform allows fallback. */
static inline int
x86_external_interrupt_pending(void)
{
    if (!ioapic_irq_path_active)
        return pic_any_pending();

    /* Nothing from the 8259, nothing in the processor's IRR (local APIC
       0's), no ExtINT: the full answer below would be no, whatever the
       APIC's enable state. */
    if (!pic_any_pending() && !cpu_lapic_extint_live[0]) {
        if (!(cpu_lapic_irr_live & 1u))
            return 0;

        /* Something is in IRR but may be held back by TPR/PPR, as it
           often is at a raised IRQL: the one scan the full path would end
           in answers it. Asking as if started can only find more, never
           less, than the full path would. */
        if (cpu_lapic_highest_deliverable_irr_index(0, 1) < 0)
            return 0;
    }

    if (ioapic_external_interrupt_pending())
        return 1;

    if (!ioapic_pic_fallback_allowed())
        return 0;

    return pic_any_pending();
}

static inline int
x86_external_interrupt_vector(void)
{
    if (!ioapic_irq_path_active)
        return picinterrupt();

    if (ioapic_external_interrupt_pending())
        return ioapic_external_interrupt_vector();

    /* Blocked PIC fallback reports no vector without acknowledging the PIC. */
    if (!ioapic_pic_fallback_allowed())
        return -1;

    return picinterrupt();
}

#endif /*EMU_X86_INTERRUPT_H*/
