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
   Table 5-2); an entry for it would share INTIN2 with the timer.
   The P65UP5 and S2DGE wire IRQ 10 and 11 to INTIN 16-19 as each board's
   own BIOS describes it in its MP table (Award on the P65UP5: IRQ10
   INTIN19, IRQ11 INTIN18; AMI on the S2DGE: IRQ10 INTIN17, IRQ11
   INTIN16), and IRQ 9 to INTIN9. These had IRQ 9 where IRQ 10 is, so a
   PCI card steered to IRQ 10 raised an input the operating system had
   masked. */
#define IOAPIC_LEGACY_IRQ_ROUTE_P65UP5 \
    { 2, 1, IOAPIC_LEGACY_IRQ_NC, 3, 4, 5, 6, 7, 8, 9, 19, 18, 12, 13, 14, 15 }

#define IOAPIC_LEGACY_IRQ_ROUTE_S2DGE \
    { 2, 1, IOAPIC_LEGACY_IRQ_NC, 3, 4, 5, 6, 7, 8, 9, 17, 16, 12, 13, 14, 15 }

/* Experimental P/I-P6RP4 route until ROM/runtime MP IRQ entries are decoded. */
#define IOAPIC_LEGACY_IRQ_ROUTE_P6RP4 \
    { 2, 1, IOAPIC_LEGACY_IRQ_NC, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15 }

/* The 82374 ESC, per its data book's Figure 25: pin 0 is the 8259's own
   INTR (or SMI), pin 2 is counter 0 of the timer, pin 1 and 3-15 are the
   IRQ of the same number, and IRQ2, the cascade, has no pin. The 54TDP's
   BIOS describes exactly this in its MP table. */
#define IOAPIC_LEGACY_IRQ_ROUTE_ESC \
    { 2, 1, IOAPIC_LEGACY_IRQ_NC, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15 }

#endif
