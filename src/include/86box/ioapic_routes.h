/*
 * Board-supplied legacy IRQ -> I/O APIC INTIN routes.
 *
 * These are legacy IRQ routes, not direct PCI PIRQ pin wiring.
 */

#ifndef EMU_IOAPIC_ROUTES_H
#define EMU_IOAPIC_ROUTES_H

#include <stdint.h>

/* An IRQ with no pin on this I/O APIC. */
#define IOAPIC_LEGACY_IRQ_NC 0xff

/* IRQ0 is INTIN2 and IRQ2, the cascade, has no input (MP spec 1.1,
   Table 5-2); an entry for it would share INTIN2 with the timer. */
#define IOAPIC_LEGACY_IRQ_ROUTE_P65UP5 \
    { 2, 1, IOAPIC_LEGACY_IRQ_NC, 3, 4, 5, 6, 7, 8, 19, 10, 18, 12, 13, 14, 15 }

#define IOAPIC_LEGACY_IRQ_ROUTE_S2DGE \
    { 2, 1, IOAPIC_LEGACY_IRQ_NC, 3, 4, 5, 6, 7, 8, 17, 10, 16, 12, 13, 14, 15 }

/* Experimental P/I-P6RP4 route until ROM/runtime MP IRQ entries are decoded. */
#define IOAPIC_LEGACY_IRQ_ROUTE_P6RP4 \
    { 2, 1, IOAPIC_LEGACY_IRQ_NC, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15 }

#endif
