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

/* IOAPIC arbitrates first; PIC remains only while platform allows fallback. */
static inline int
x86_external_interrupt_pending(void)
{
    if (ioapic_external_interrupt_pending())
        return 1;

    if (!ioapic_pic_fallback_allowed())
        return 0;

    return pic.int_pending;
}

static inline int
x86_external_interrupt_vector(void)
{
    if (ioapic_external_interrupt_pending())
        return ioapic_external_interrupt_vector();

    /* Blocked PIC fallback reports no vector without acknowledging the PIC. */
    if (!ioapic_pic_fallback_allowed())
        return -1;

    return picinterrupt();
}

#endif /*EMU_X86_INTERRUPT_H*/
