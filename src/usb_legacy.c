/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          UHCI legacy keyboard support, written from the Intel Universal
 *          Host Controller Interface (UHCI) Design Guide Revision 1.1
 *          (March 1996) s5 and the Intel 82371AB PCI-to-ISA/IDE
 *          Xcelerator (PIIX4) datasheet, order number 290562-001
 *          (preliminary, April 1997): the LEGSUP traps on the keyboard
 *          controller ports and the A20GATE pass-through sequence,
 *          shared by the chipsets that carry a UHCI.
 *
 * Authors: skiretic, <skiretic@proton.me>
 *
 *          Copyright 2026 skiretic.
 */
#include <stdarg.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <wchar.h>
#define HAVE_STDARG_H
#include <86box/86box.h>
#include <86box/mem.h>
#include <86box/device.h>
#include <86box/usb.h>

/* LEGSUP, PCI configuration C0h-C1h of the UHCI function (UHCI 1.1
   s5.2.1, p. 39-40 / PDF 45-46; PIIX4 290562-001 s6.1.12, p. 107-108 /
   PDF 107-108). C0h bits 0-7: 60REN, 60WEN, 64REN, 64WEN, USBSMIEN,
   A20PTEN, PSS, SMIEPTE. C1h bits 0-7: TBY60R, TBY60W, TBY64R, TBY64W,
   USBIRQS, USBPIRQDEN, reserved, A20PTS. Each trap enable in C0h bits 0-3
   has its status bit at the same position in C1h. */
#define LEGSUP_USBSMIEN 0x10
#define LEGSUP_A20PTEN  0x20
#define LEGSUP_PSS      0x40
#define LEGSUP_SMIEPTE  0x80
#define LEGSUP_A20PTS   0x80

/* Position in the A20GATE pass-through sequence, kept by the caller. */
enum {
    A20_WAIT_DATA = 0, /* after 64h <- D1h */
    A20_WAIT_END       /* after the data byte to 60h */
};

/* The sequence has ended: PSS clears and A20PTS is set. */

static void
uhci_legsup_a20_end(uint8_t *legsup)
{
    legsup[0] &= ~LEGSUP_PSS;
    legsup[1] |= LEGSUP_A20PTS;
}

int
uhci_legsup_kbc_cycle(uint8_t *legsup, uint8_t *a20, uint16_t port, int write, uint8_t val, int opts)
{
    int bit = ((port == 0x64) ? 2 : 0) | (write ? 1 : 0);

    /* While A20PTEN is set, the A20GATE pass-through sequence reaches the
       8042 with no trap and no status bit: 64h <- D1h (one or more),
       60h <- data, 64h reads (zero or more), 64h <- FFh. Any other cycle
       ends the sequence and is then handled as a normal cycle (UHCI 1.1
       s5.2.1, p. 40 / PDF 46; PIIX4 290562-001, p. 108 / PDF 108). The
       spec's table lists 64h reads only after the 60h write. Here a 64h
       read never ends the sequence at any point, because A20 routines such
       as HIMEM's and Linux's poll the 64h status before every write of the
       sequence, including the 60h write. The caller can choose (opts) to
       handle the final FFh write as a normal cycle instead of passing it. */
    if (legsup[0] & LEGSUP_A20PTEN) {
        if (legsup[0] & LEGSUP_PSS) {
            if (write && (port == 0x64) && (val == 0xd1) && (*a20 == A20_WAIT_DATA))
                return 0;
            if (write && (port == 0x60) && (*a20 == A20_WAIT_DATA)) {
                *a20 = A20_WAIT_END;
                return 0;
            }
            if (!write && (port == 0x64))
                return 0;
            uhci_legsup_a20_end(legsup);
            if (write && (port == 0x64) && (val == 0xff) && (*a20 == A20_WAIT_END) && !(opts & UHCI_LEGSUP_A20_FF_TRAPPED))
                return 0;
        } else if (write && (port == 0x64) && (val == 0xd1)) {
            legsup[0] |= LEGSUP_PSS;
            *a20 = A20_WAIT_DATA;
            return 0;
        }
    }

    /* An enabled trap sets its status bit and keeps the cycle from the 8042
       (UHCI 1.1 s5.1.1, p. 39 / PDF 45). With UHCI_LEGSUP_STATUS_ALWAYS the
       chipset sets the status bit for every cycle, trapped or not. */
    if (legsup[0] & (1 << bit)) {
        legsup[1] |= (1 << bit);
        return 1;
    }

    if (opts & UHCI_LEGSUP_STATUS_ALWAYS)
        legsup[1] |= (1 << bit);

    return 0;
}

/* Keyboard controller cycles matter only while a trap or the pass-through
   is enabled, or when the chipset sets trap status without the enables. */
int
uhci_legsup_kbc_armed(const uint8_t *legsup, int opts)
{
    return (legsup[0] & (0x0f | LEGSUP_A20PTEN)) || (opts & UHCI_LEGSUP_STATUS_ALWAYS);
}

/* The SMI# sources that are active, each gated by its enable: a trap status
   (bits 0-3), the USB IRQ with USBSMIEN (bit 4), and the end of the
   pass-through (A20PTS) with SMIEPTE (bit 7) (UHCI 1.1 s5.2.1, p. 39-40 /
   PDF 45-46). */
uint8_t
uhci_legsup_event(const uint8_t *legsup, int irq)
{
    uint8_t src = legsup[1] & legsup[0] & 0x0f;

    if (irq && (legsup[0] & LEGSUP_USBSMIEN))
        src |= 0x10;
    if ((legsup[1] & LEGSUP_A20PTS) && (legsup[0] & LEGSUP_SMIEPTE))
        src |= 0x80;

    return src;
}

void
uhci_legsup_write_c0(uint8_t *legsup, uint8_t val)
{
    /* PSS is read-only. Writing A20PTEN = 0 ends a running sequence at once
       (UHCI 1.1 s5.2.1, p. 40 / PDF 46), and this end sets A20PTS like any
       other. */
    legsup[0] = (legsup[0] & LEGSUP_PSS) | (val & ~LEGSUP_PSS);
    if (!(val & LEGSUP_A20PTEN) && (legsup[0] & LEGSUP_PSS))
        uhci_legsup_a20_end(legsup);
}
