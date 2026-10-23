/*
 * Board-supplied legacy IRQ -> I/O APIC INTIN routes.
 *
 * The ISA IRQ lines' inputs, and the PCI PIRQ lines' where the board
 * wires them to the I/O APIC.
 */

#ifndef EMU_IOAPIC_ROUTES_H
#define EMU_IOAPIC_ROUTES_H

#include <stdint.h>

/* An IRQ with no pin on this I/O APIC. */
#define IOAPIC_LEGACY_IRQ_NC 0xff

/* The 82093AA's ISA inputs: IRQ n on INTIN n, but IRQ0 on INTIN2 and
   IRQ2, the cascade, on none -- "INTIN[3:11,14,15] ... Typically, these
   signals are connected to the ISA interrupts IRQ[3:7,8#,9:11,14:15]
   respectively", IRQ1 on INTIN1 and IRQ12 on INTIN12 (82093AA 2.0); MP
   spec 1.1, Table 5-2. A PCI interrupt is not among them: the chipset
   steers it to an IRQ inside the 8259 side only (picint_steered). */
#define IOAPIC_LEGACY_IRQ_ROUTE_ISA \
    { 2, 1, IOAPIC_LEGACY_IRQ_NC, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15 }

/* The PCI interrupts on inputs of their own: "INTIN[16:19] ... Typically,
   these signals are connected to the PCI interrupts (PIRQ[0:3])"
   (82093AA 2.0). "The steering of the PCI IRQs to the ISA IRQs is
   accomplished in the IOAPIC by setting the PCI redirection table entry
   to the correct ISA interrupt vector": the P65UP5's and S2DGE's BIOSes
   list each IRQ they steered a PIRQ to on that PIRQ's input. */
#define IOAPIC_PIRQ_ROUTE_82093AA \
    { 16, 17, 18, 19 }

/* The 82374 ESC, per its data book's Figure 25: pin 0 is the 8259's own
   INTR (or SMI), pin 2 is counter 0 of the timer, pin 1 and 3-15 are the
   IRQ of the same number, and IRQ2, the cascade, has no pin. The 54TDP's
   BIOS describes exactly this in its MP table. */
#define IOAPIC_LEGACY_IRQ_ROUTE_ESC \
    { 2, 1, IOAPIC_LEGACY_IRQ_NC, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15 }

/* The 82379AB SIO.A, per its data sheet's Figure 8: the same as the ESC,
   the internal 8259's output on pin 0, counter 0 on pin 2 and no pin for
   IRQ2. The PIRQs have no inputs of their own; they reach the I/O APIC on
   the IRQ the SIO.A steers them to. The P54NP4D's BIOS describes exactly
   this in its MP table. */
#define IOAPIC_LEGACY_IRQ_ROUTE_SIOA IOAPIC_LEGACY_IRQ_ROUTE_ESC

#endif
