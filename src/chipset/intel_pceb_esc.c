/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          Intel's PCI-EISA bridge chip set: the 82375EB/82375SB
 *          PCI-EISA Bridge (PCEB) and the 82374EB/82374SB EISA System
 *          Component (ESC).
 *
 *          The two are one bridge in two packages. The PCEB faces the
 *          PCI bus, is the only half with a PCI configuration header,
 *          and owns the address paths: what memory and I/O the EISA side
 *          may reach on PCI, and what PCI may reach on EISA. The ESC
 *          faces the EISA bus and is everything a south bridge normally
 *          is -- two interrupt controllers, two DMA controllers, the
 *          timers, the NMI logic -- with the EISA extensions layered on
 *          top: thirty-two bit addresses, scatter-gather, per channel
 *          stop registers, and the slot decode that lets firmware find a
 *          card without probing for it.
 *
 *          On a board with no PIIX, and the AIR 54TDP is one, the ESC is
 *          the south bridge outright and its PIRQ route registers are the
 *          only thing steering PCI interrupts.
 *
 *          The ESC has no PCI presence at all. Its configuration space is
 *          reached through an index and data pair at 0022h and 0023h, and
 *          stays shut after reset until 0Fh is written to the ID register
 *          at index 02h; other devices of the day indexed the same ports,
 *          and that byte is how the ESC knows it is the one being spoken
 *          to.
 *
 * Authors: Michael Pratte, <mpratte@makefox.group>
 *
 *          Copyright 2026 Michael Pratte.
 */
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>
#define HAVE_STDARG_H
#include <86box/86box.h>
#include <86box/device.h>
#include <86box/io.h>
#include <86box/mem.h>
#include <86box/timer.h>
#include "cpu.h"
#include <86box/pci.h>
#include <86box/dma.h>
#include <86box/pic.h>
#include <86box/pit.h>
#include <86box/pit_fast.h>
#include <86box/flash.h>
#include <86box/apm.h>
#include <86box/nmi.h>
#include <86box/port_92.h>
#include <86box/eisa.h>
#include <86box/esc.h>
#include <86box/nvr.h>
#include <86box/machine.h>
#include <86box/plat_unused.h>

#ifdef ENABLE_ESC_LOG
int esc_do_log = ENABLE_ESC_LOG;

static void
esc_log(const char *fmt, ...)
{
    va_list ap;

    if (esc_do_log) {
        va_start(ap, fmt);
        pclog_ex(fmt, ap);
        va_end(ap);
    }
}
#else
#    define esc_log(fmt, ...)
#endif

typedef struct esc_t {
    uint8_t index;    /* what 0022h last selected */
    uint8_t unlocked; /* 0Fh has been written to the ID register */
    uint8_t regs[256];

    pc_timer_t fast_off; /* the Fast Off timer, one minute a tick */
    uint8_t    nmi_esc;  /* 0461h, extended NMI status and control */
    uint8_t    serr_nmi; /* SERR# is what is holding NMI up */
    uint8_t    last_mst; /* 0464h, last EISA bus master granted */

    void   *apm;
    uint8_t fast_off_count; /* what is left of FTMR's minutes */
    uint8_t cram_decoded;
    uint8_t port_92_decoded;

    uint8_t board_id[4];
    uint8_t strap_id[4];    /* what the board itself identifies as */
    uint8_t embedded_id[4]; /* a device soldered to the board */

    /* The EISA configuration RAM: thirty-two pages of two hundred and
       fifty-six bytes, seen through a window at 0800h with the page
       chosen by CONFRAMP at 0C00h. This is where the configuration
       utility leaves what it worked out and where the BIOS looks at
       every power on, so it has to outlive the machine. */
    uint8_t cram[ESC_CRAM_PAGES * 256];
    uint8_t cram_page;
    uint8_t cram_auto; /* start an empty store when there is none */
    char    cram_file[128];

    /* The I/O APIC: a select register and a window onto the identifier,
       the version, the arbitration priority and sixteen redirection
       entries of two words each. */
    mem_mapping_t apic_mapping;
    uint32_t      apic_sel;
    uint32_t      apic_id;
    uint32_t      apic_redir[16][2];

    port_92_t *port_92;
} esc_t;

typedef struct pceb_t {
    uint8_t pci_slot;
    uint8_t irq_state;
    uint8_t regs[256];

    /* The BIOS timer, the one internal resource this half maps into PCI
       I/O space. */
    uint16_t   btmr_base;
    pc_timer_t bios_timer;
} pceb_t;

/* The timer is clocked from BCLK divided by eight -- 8.33 MHz over eight
   is 1.04 MHz, so a count is a shade under a microsecond. */
/* Eight BCLKs a tick, and BCLK is 8.33 MHz whatever the board runs at:
   CLKDIV's divisor is picked to keep it there, "000 -> 4 (33.33 MHz) ->
   8.33 MHz, 001 -> 3 (25 MHz) -> 8.33 MHz". So there is nothing for that
   register to change here, and the tick is a shade under a microsecond.

   The PCEB's own book says 1.03 MHz for this clock and "derived from the
   8.25 MHz/8.33 MHz BCLK", which is the same number worked from a PCI
   clock of 33.0 rather than the 33.33 its sibling tabulates -- 8.25 over
   eight against 8.33 over eight, a percent apart. The ESC's table is the
   one that says what BCLK actually is, so it is the one followed. Left
   alone: the timer is documented to ±1 ms and this is a rounding in the
   fourth digit. */
#define PCEB_BIOS_TICK_US (8.0 / 8.33)

static esc_t *esc_inst = NULL;

/* ------------------------------------------------------------------ */
/* The BIOS timer                                                      */
/* ------------------------------------------------------------------ */

/* "After data is written into BIOS Timer Register the BIOS timer starts
   decrementing until it reaches zero. It freezes at zero until the new
   count value is written." Sixteen bits of count; the top half of the
   dword is reserved and reads zero. */
static uint16_t
pceb_bios_timer_count(pceb_t *dev)
{
    double left = timer_get_remaining_us(&dev->bios_timer);

    if (left <= 0.0)
        return 0;
    return (uint16_t) (left / PCEB_BIOS_TICK_US);
}

static void
pceb_bios_timer_set(pceb_t *dev, uint16_t count)
{
    if (count == 0)
        timer_disable(&dev->bios_timer);
    else
        timer_set_delay_u64(&dev->bios_timer,
                            (uint64_t) (((double) count) * PCEB_BIOS_TICK_US * ((double) TIMER_USEC)));
}

static void
pceb_bios_timer_tick(UNUSED(void *priv))
{
    /* Nothing to do when it runs out: it simply reads zero from then on. */
}

static uint8_t
pceb_btmr_readb(uint16_t port, void *priv)
{
    pceb_t  *dev = (pceb_t *) priv;
    uint32_t v   = pceb_bios_timer_count(dev);

    return (uint8_t) (v >> (((port - dev->btmr_base) & 3) * 8));
}

static uint16_t
pceb_btmr_readw(uint16_t port, void *priv)
{
    pceb_t *dev = (pceb_t *) priv;

    return ((port - dev->btmr_base) & 2) ? 0x0000
                                         : pceb_bios_timer_count(dev);
}

static uint32_t
pceb_btmr_readl(UNUSED(uint16_t port), void *priv)
{
    return pceb_bios_timer_count((pceb_t *) priv);
}

static void
pceb_btmr_writeb(uint16_t port, uint8_t val, void *priv)
{
    pceb_t  *dev = (pceb_t *) priv;
    uint16_t c   = pceb_bios_timer_count(dev);

    if (((port - dev->btmr_base) & 3) >= 2)
        return; /* the top half is reserved */
    if ((port - dev->btmr_base) & 1)
        c = (uint16_t) ((c & 0x00ff) | (val << 8));
    else
        c = (uint16_t) ((c & 0xff00) | val);
    pceb_bios_timer_set(dev, c);
}

static void
pceb_btmr_writew(uint16_t port, uint16_t val, void *priv)
{
    pceb_t *dev = (pceb_t *) priv;

    if (!((port - dev->btmr_base) & 2))
        pceb_bios_timer_set(dev, val);
}

static void
pceb_btmr_writel(UNUSED(uint16_t port), uint32_t val, void *priv)
{
    pceb_bios_timer_set((pceb_t *) priv, (uint16_t) val);
}

/* "Bit 0 of BTMR must be 1 to enable access"; the rest of it is a Dword
   aligned address in PCI I/O space. The decode is off out of reset. */
static void
pceb_bios_timer_remap(pceb_t *dev)
{
    uint16_t btmr = (uint16_t) (dev->regs[0x80] | (dev->regs[0x81] << 8));

    if (dev->btmr_base != 0x0000) {
        io_removehandler(dev->btmr_base, 0x0004, pceb_btmr_readb,
                         pceb_btmr_readw, pceb_btmr_readl, pceb_btmr_writeb,
                         pceb_btmr_writew, pceb_btmr_writel, dev);
        dev->btmr_base = 0x0000;
    }

    if (btmr & 0x0001) {
        dev->btmr_base = btmr & 0xfffc;
        io_sethandler(dev->btmr_base, 0x0004, pceb_btmr_readb,
                      pceb_btmr_readw, pceb_btmr_readl, pceb_btmr_writeb,
                      pceb_btmr_writew, pceb_btmr_writel, dev);
    }
}

/* ------------------------------------------------------------------ */
/* The fail-safe timer                                                 */
/* ------------------------------------------------------------------ */

/* Timer 2, counter 0 is the fail-safe timer: software is meant to keep
   reloading it, and if it ever runs out the board takes that as the
   machine having wedged and raises an NMI. Bit 2 of the extended NMI
   control register is what lets it through, and the data book is explicit
   that the status bit only sets when it is enabled. */
/* One minute a tick: "The count time interval is one minute." */
#define ESC_FAST_OFF_TICK (60.0 * 1000000.0)

/* A system event, which is what keeps the machine from powering down:
   "Anytime the corresponding hardware event occurs (signal is asserted),
   the Fast Off Timer is re-loaded with its initial count." SEE says which
   events count, and two of them are the ESC's own -- bit 31 for an SMI,
   bit 29 for an NMI -- so those are the two this can honour. The rest of
   the register is Fast Off IRQ[15:3], which wants to see the interrupt
   lines themselves, and 86Box's interrupt controller offers nowhere to
   watch them from. SEE is thirty-two bits at A4h, so bit 31 is bit 7 of
   A7h and bit 29 is bit 5 of it. */
static void
esc_system_event(esc_t *dev, uint8_t see_bit)
{
    if (!(dev->regs[0xa7] & (1 << (see_bit - 24))))
        return;
    dev->fast_off_count = dev->regs[0xa8];
}

/* An SMI source firing. SMIEN says whether the event is one the part
   watches, SMIREQ records it, and SMICNTL bit 0 decides only whether the
   pin moves: it "does not effect the detection/recording of SMI events
   (i.e., This bit does not effect the SMI status bits in the SMIREQ
   Register). Thus, SMI conditions can be pending when this bit is set to
   1."

   One indication per active event, which the book spells out for the
   interrupt sources and applies to all of them: "If the SMI event is
   still active when the corresponding SMIREQ bit is set to 0, the ESC
   does not set the status bit back to a 1." */
static void
esc_smi_event(esc_t *dev, uint8_t bit)
{
    const uint8_t mask = (uint8_t) (1 << bit);

    if (!(dev->regs[0xa2] & mask) || (dev->regs[0xaa] & mask))
        return;

    esc_log("ESC: SMI source %u\n", bit);
    dev->regs[0xaa] |= mask;

    /* FSMIEN: an SMI is itself activity. */
    esc_system_event(dev, 31);

    if (dev->regs[0xa0] & 0x01)
        smi_raise();
}

/* Bit 3 of SMICNTL stops the count -- "When this bit is 1, the Fast Off
   timer stops counting. This prevents time-outs from occurring while
   executing SMM code" -- and it comes up set, the register defaulting to
   08h. */
static void
esc_fast_off_remap(esc_t *dev)
{
    if (dev->regs[0xa0] & 0x08) {
        timer_stop(&dev->fast_off);
        return;
    }
    if (!timer_is_enabled(&dev->fast_off))
        timer_on_auto(&dev->fast_off, ESC_FAST_OFF_TICK);
}

/* "When the Fast Off Timer reaches 00h, an SMI is generated and the timer
   is re-load with the value programmed into this register." The register
   holds the starting count; this counts it down a minute at a time. */
static void
esc_fast_off_tick(void *priv)
{
    esc_t *dev = (esc_t *) priv;

    if (dev->regs[0xa0] & 0x08)
        return;

    if (dev->fast_off_count > 0)
        dev->fast_off_count--;

    if (dev->fast_off_count == 0) {
        esc_smi_event(dev, 5);
        dev->fast_off_count = dev->regs[0xa8];
    }

    timer_on_auto(&dev->fast_off, ESC_FAST_OFF_TICK);
}

/* A write to APMC at 0B2h. The power management device beside us keeps
   the byte; what the ESC adds is that the write is an SMI source, which
   is SMIEN bit 7: "This bit enables SMI for writes to the APMC Register."
   Its own do_smi is left clear so that the raising is ours to gate. */
static void
esc_apmc_write(UNUSED(uint16_t port), UNUSED(uint8_t val), void *priv)
{
    esc_smi_event((esc_t *) priv, 7);
}

static void
esc_fail_safe_timer(int new_out, int old_out, UNUSED(void *priv))
{
    esc_t *dev = esc_inst;

    if ((dev == NULL) || !new_out || old_out)
        return;

    if (!(dev->nmi_esc & 0x04))
        return;

    esc_log("ESC: the fail-safe timer ran out\n");
    dev->nmi_esc |= 0x80;
    nmi            = 1;
    nmi_auto_clear = 1;
    /* FNMIEN: and so is an NMI. */
    esc_system_event(dev, 29);
}

/* ------------------------------------------------------------------ */
/* PCI interrupt steering                                              */
/* ------------------------------------------------------------------ */

/* A PIRQ reaches an IRQ only when its own route register says so and the
   mode select register has the PIRQ pins switched on at all: bit 6 there
   is what makes them PIRQs rather than MREQs. */
/* SERR# pulsing active. MS bit 3 is the whole of its control: "This bit
   is used to disable (0) or enable (1) the generation of NMI based on
   SERR# signal pulsing active. When this bit = 1 (and NMIs are enabled
   via the NMIERTC Register) and SERR# is asserted, the NMI signal is
   asserted." The NMIERTC half of that is the nmi_mask the processor
   already tests, so the mode select bit is all there is to do here.

   It has no status bit to go with it anywhere, which the book is careful
   to say twice -- NMISC bit 7 "does not reflect status of an NMI caused
   by SERR#, which is enabled and disabled/cleared via the MS Register" --
   so a handler distinguishes this from the other sources by their status
   bits all reading zero.

   Nothing in 86Box pulses SERR# today: no device asserts it and there is
   no bus error to raise it. This is the part of that path the ESC owns,
   and it works the moment something does. */
void
esc_serr(void)
{
    esc_t *dev = esc_inst;

    if ((dev == NULL) || !(dev->regs[0x40] & 0x08))
        return;

    esc_log("ESC: SERR#\n");
    dev->serr_nmi  = 1;
    nmi            = 1;
    nmi_auto_clear = 1;
    esc_system_event(dev, 29);
}

static void
esc_pirq_update(esc_t *dev)
{
    for (uint8_t i = 0; i < 4; i++) {
        uint8_t route = dev->regs[0x60 + i];

        if ((dev->regs[0x40] & 0x40) && !(route & 0x80))
            pci_set_irq_routing(PCI_INTA + i, route & 0x0f);
        else
            pci_set_irq_routing(PCI_INTA + i, PCI_IRQ_DISABLED);
    }
}

/* ------------------------------------------------------------------ */
/* The I/O APIC                                                        */
/* ------------------------------------------------------------------ */

/* The register file is all here and behaves as the data book says. What
   is not here is anywhere for a message to go: the emulator has no local
   APIC and no APIC bus, so an unmasked entry is remembered and nothing
   more. A machine with one processor runs through the 8259s, which is
   what INTRC = 0 in the PAC register means, and never notices. */
#define ESC_APIC_VER     0x000f0011
#define ESC_APIC_LO_MASK 0x0001afff /* bits 12 and 14 are the chip's */
#define ESC_APIC_HI_MASK 0xff000000

static uint32_t
esc_apic_reg_read(esc_t *dev)
{
    uint8_t reg = dev->apic_sel & 0xff;

    switch (reg) {
        case 0x00: /* APICID */
        case 0x02: /* APICARB, which a write to the identifier also loads */
            /* Both are the four bit identifier in 27:24 and nothing else;
               the arbitration register "is loaded whenever the I/O APIC
               ID Register is written".

               Its printed default, 000F0011h, is not to be taken for the
               value it should read: that is the version register's
               contents one entry above, and it contradicts this
               register's own bit table, which has 31:28 and 23:0
               reserved. The table is self-consistent and the default is
               not, so the table is what is followed here. */
            return dev->apic_id;

        case 0x01: /* APICVER */
            return ESC_APIC_VER;

        case 0x10 ... 0x2f:
            return dev->apic_redir[(reg - 0x10) >> 1][reg & 1];

        default:
            break;
    }

    return 0x00000000;
}

static void
esc_apic_reg_write(esc_t *dev, uint32_t val)
{
    uint8_t reg = dev->apic_sel & 0xff;

    switch (reg) {
        case 0x00:
            dev->apic_id = val & 0x0f000000;
            break;

        case 0x10 ... 0x2f:
            if (reg & 1)
                dev->apic_redir[(reg - 0x10) >> 1][1] = val & ESC_APIC_HI_MASK;
            else {
                uint32_t *lo = &dev->apic_redir[(reg - 0x10) >> 1][0];

                *lo = (*lo & ~ESC_APIC_LO_MASK) | (val & ESC_APIC_LO_MASK);
                esc_log("ESC: I/O APIC entry %i = %08x%s\n", (reg - 0x10) >> 1,
                        *lo, (*lo & 0x00010000) ? "" : " (unmasked, and no "
                                                       "local APIC to take it)");
            }
            break;

        default:
            break;
    }
}

static uint32_t
esc_apic_readl(uint32_t addr, void *priv)
{
    esc_t *dev = (esc_t *) priv;

    switch (addr & 0x3fc) {
        case 0x000:
            return dev->apic_sel;
        case 0x010:
            return esc_apic_reg_read(dev);
        default:
            break;
    }

    return 0xffffffff;
}

static void
esc_apic_writel(uint32_t addr, uint32_t val, void *priv)
{
    esc_t *dev = (esc_t *) priv;

    switch (addr & 0x3fc) {
        case 0x000:
            dev->apic_sel = val & 0x000000ff;
            break;
        case 0x010:
            esc_apic_reg_write(dev, val);
            break;
        default:
            break;
    }
}

/* Software is told to use doubleword accesses. Narrower ones are put
   together from those rather than refused. */
static uint8_t
esc_apic_readb(uint32_t addr, void *priv)
{
    return (uint8_t) (esc_apic_readl(addr, priv) >> ((addr & 3) * 8));
}

static uint16_t
esc_apic_readw(uint32_t addr, void *priv)
{
    return (uint16_t) (esc_apic_readl(addr, priv) >> ((addr & 2) * 8));
}

static void
esc_apic_writeb(uint32_t addr, uint8_t val, void *priv)
{
    uint32_t cur   = esc_apic_readl(addr, priv);
    int      shift = (addr & 3) * 8;

    cur = (cur & ~(0xffU << shift)) | ((uint32_t) val << shift);
    esc_apic_writel(addr, cur, priv);
}

static void
esc_apic_writew(uint32_t addr, uint16_t val, void *priv)
{
    uint32_t cur   = esc_apic_readl(addr, priv);
    int      shift = (addr & 2) * 8;

    cur = (cur & ~(0xffffU << shift)) | ((uint32_t) val << shift);
    esc_apic_writel(addr, cur, priv);
}

/* APICBASE moves the unit in 1 KB steps: bits 5:2 are address bits 15:12
   and, on the SB part, bits 1:0 are address bits 11:10. */
static void
esc_apic_remap(esc_t *dev)
{
    uint32_t base = 0xfec00000 | (((uint32_t) (dev->regs[0x59] >> 2) & 0x0f) << 12) | (((uint32_t) dev->regs[0x59] & 0x03) << 10);

    mem_mapping_set_addr(&dev->apic_mapping, base, 0x400);
    esc_log("ESC: I/O APIC at %08x\n", base);
}

static void
esc_apic_reset(esc_t *dev)
{
    dev->apic_sel = 0x00000000;
    dev->apic_id  = 0x00000000;

    /* Every pin starts masked. */
    for (uint8_t i = 0; i < 16; i++) {
        dev->apic_redir[i][0] = 0x00010000;
        dev->apic_redir[i][1] = 0x00000000;
    }
}

/* ------------------------------------------------------------------ */
/* The EISA configuration RAM                                          */
/* ------------------------------------------------------------------ */

/* An empty store, laid out the way the board's own firmware lays one out
   when it finds nothing worth keeping. It holds no configuration: that is
   the utility's to write, and until it has, every slot reads back as not
   configured. */
static void
esc_cram_generate(esc_t *dev)
{
    uint8_t *c    = dev->cram;
    uint16_t sum  = 0;
    uint8_t  csum = 0;

    memset(c, 0, sizeof(dev->cram));

    /* The layout the board's own firmware writes, which is the only one it
       will accept. Everything it cares about is reached through a base it
       keeps at offset eight, so the words below that are a wrapper and the
       configuration proper starts at 50h.

       Read out of a store the BIOS had just written, and confirmed by
       disassembling its run-time module: the byte reader puts the page in
       CONFRAMP and the offset in the window, the block reader adds 50h to
       every offset it is given, and the check it runs is a plain byte sum
       over the region whose length sits at base+4. */
    c[0x00] = 0x55;
    c[0x01] = 0xaa;
    /* c[4] is the store's own checksum and is filled in at the end. */
    c[0x06] = 0x42;
    c[0x08] = ESC_CRAM_BASE; /* where the block starts   */
    c[0x0a] = 0x40;          /* where the ESCD starts    */
    c[0x0b] = 0x01;
    c[0x0c] = 0x05;
    c[0x0d] = 0x13;
    c[0x0e] = 0x10;
    c[0x0f] = 0xac;
    c[0x11] = 0x58;
    c[0x40] = 0x0b;
    c[0x41] = 0x02;

    /* The configuration block. base+4 is how much of it is summed, and the
       sum goes in base+2. */
    c[ESC_CRAM_BASE + 4]  = 0xb0; /* length, 1fb0h */
    c[ESC_CRAM_BASE + 5]  = 0x1f;
    c[ESC_CRAM_BASE + 6]  = 0xb4;
    c[ESC_CRAM_BASE + 7]  = 0x1e;
    c[ESC_CRAM_BASE + 10] = 0xfc;

    /* Whose it is. */
    memcpy(&c[0xf0], "AMI", 3);

    /* The extended configuration data, exactly the fourteen bytes the
       firmware copies in when it starts a store from nothing. */
    {
        static const uint8_t escd[14] = {
            0x0e, 0x00, 'A', 'C', 'F', 'G', 0x01, 0x02,
            0x00, 0x00, 0x00, 0x00, 0xde, 0xfe
        };
        memcpy(&c[ESC_CRAM_ESCD], escd, sizeof(escd));
    }

    /* A device soldered to the board is recorded even with nothing in any
       slot: on this one that is the Adaptec, which the firmware writes as
       ADP7880 whatever the board identifier says. */
    c[0x2e] = 0x05;
    memcpy(&c[0x2f], dev->embedded_id, 4);
    c[0x33] = 0x50;

    /* And that is the whole of it. What a slot holds, which interrupt it
       was given, where its option ROM was put and every other resource it
       asked for are the configuration utility's to work out and to write
       here; a record count of zero is what a store that has never been
       through one looks like, and the firmware answers "not configured"
       for every slot until the utility has run. Writing records here
       instead would be inventing an allocation nobody computed, and the
       cards would be honouring a configuration that never existed. */

    /* The block keeps a sixteen bit sum of itself, from base+4 to the end
       of the region whose length sits at base+4. */
    for (uint16_t i = ESC_CRAM_BASE + 4; i < sizeof(dev->cram); i++)
        sum = (uint16_t) (sum + c[i]);
    c[ESC_CRAM_BASE + 2] = (uint8_t) (sum & 0xff);
    c[ESC_CRAM_BASE + 3] = (uint8_t) (sum >> 8);

    /* And the store as a whole carries one byte at offset four chosen so
       that every byte in it adds up to nothing. The firmware zeroes that
       byte, sums the lot and writes back the negation; anything else and
       it throws the store away and builds its own. */
    c[0x04] = 0x00;
    for (uint16_t i = 0; i < sizeof(dev->cram); i++)
        csum = (uint8_t) (csum + c[i]);
    c[0x04] = (uint8_t) (-csum);

    esc_log("ESC: configuration RAM built, block sum %04x, store byte %02x\n",
            sum, c[0x04]);
}

/* Eight bytes after the pages saying who wrote the file. Nothing but us
   reads them -- the window is only ever 8 KB wide, so the guest never sees
   them -- and they exist so that a store left behind by a version that
   wrote configuration records of its own is recognised and thrown away
   rather than passed off to the firmware as the utility's work. */
static const uint8_t esc_cram_tag[8] = { 'E', 'I', 'S', 'A', 'C', 'F', 'G', 0x02 };

static void
esc_cram_load(esc_t *dev)
{
    FILE   *fp;
    uint8_t tag[sizeof(esc_cram_tag)];

    fp = nvr_fopen(dev->cram_file, "rb");
    if (fp == NULL) {
        /* Nothing saved yet. With automatic configuration off the guest
           gets an empty store and may do as it likes with it. */
        if (dev->cram_auto)
            esc_cram_generate(dev);
        return;
    }

    if ((fread(dev->cram, 1, sizeof(dev->cram), fp) != sizeof(dev->cram)) || (fread(tag, 1, sizeof(tag), fp) != sizeof(tag)) || memcmp(tag, esc_cram_tag, sizeof(tag))) {
        fclose(fp);
        memset(dev->cram, 0, sizeof(dev->cram));
        esc_log("ESC: the saved configuration RAM is not one of ours, "
                "starting again\n");
        if (dev->cram_auto)
            esc_cram_generate(dev);
        return;
    }

    fclose(fp);
}

/* Whatever the guest left in the store goes back to the file, every time
   and whichever way the machine's automatic setting is turned: the utility
   writing a configuration and finding it gone at the next power on is the
   one thing a real board never does. */
static void
esc_cram_save(esc_t *dev)
{
    FILE *fp;

    fp = nvr_fopen(dev->cram_file, "wb");
    if (fp == NULL)
        return;

    fwrite(dev->cram, 1, sizeof(dev->cram), fp);
    fwrite(esc_cram_tag, 1, sizeof(esc_cram_tag), fp);
    fclose(fp);
}

/* ------------------------------------------------------------------ */
/* The board identifier                                               */
/* ------------------------------------------------------------------ */

void
esc_set_embedded_id(const char *mfg, uint16_t product, uint8_t rev)
{
    if (esc_inst != NULL)
        eisa_make_id(esc_inst->embedded_id, mfg, product, rev);
}

void
esc_set_board_id(const char *mfg, uint16_t product, uint8_t rev)
{
    uint8_t id[4];

    eisa_make_id(id, mfg, product, rev);

    /* What the board is, for anything walking the slots, and what the
       identifier registers come up holding.

       The book has them start at zero -- "On power up these bits default
       to 00h. These bit are written with the ID value during
       configuration" -- and that is what this used to do. It is wrong for
       this machine. Its BIOS reaches the configuration registers through
       exactly one pair of helpers, read at F000:EA2B and write at
       F000:EA32, and every call site in the ROM is accounted for: six
       writes, carrying indices 40h, 42h, 43h, 43h, 40h and 4Eh. EISAID is
       not among them, and neither the packed identifier nor any inline
       write to 0022h/0023h appears anywhere else in the image. This
       firmware never writes these registers at all, so left at zero they
       stay at zero and 0C80h-0C83h answer a system board with no identity
       for the whole life of the machine.

       A board that does not program its own identifier has it strapped,
       which is what this models: the registers still read and write as
       the book describes, they simply come up holding the board's own
       identifier rather than nothing. */
    if (esc_inst != NULL) {
        memcpy(esc_inst->strap_id, id, 4);
        memcpy(esc_inst->board_id, id, 4);
        for (uint8_t i = 0; i < 4; i++)
            esc_inst->regs[0x50 + i] = id[i];
    }

    eisa_set_board_id(id);
}

/* ------------------------------------------------------------------ */
/* ESC configuration space                                            */
/* ------------------------------------------------------------------ */

static uint8_t esc_read(uint16_t port, void *priv);
static void    esc_write(uint16_t port, uint8_t val, void *priv);

/* PCSB says what the part decodes for itself. Bit 7 is the configuration
   RAM window and its page register, bit 6 is Port 92. */
/* Which page of the configuration RAM an access to 0800h-08FFh lands on.

   Mode Select bit 5 looks like it should have a say. LA[31:27] and
   CPG[4:0] are one set of pins that "behave as the EISA address bus under
   all conditions except during access cycle to the Configuration RAM",
   where "the ESC drives these signals with the configuration page address
   (the value contained in register 0C00h). The Configuration RAM Page
   Address function can be disabled by setting Mode Select register bit
   5 = 0." Disabled, the pins carry the cycle's own LA[31:27], and for an
   I/O cycle at 0800h-08FFh those are zero -- so an SRAM taking its top
   address lines from the ESC would see page zero and nothing else.

   This board's does not take them from the ESC, and its own firmware
   proves it rather than leaving it to be guessed:

     - it clears the bit. MS is written as (old & 80h) | 4Ch, at
       F000:E810 and F000:EA6C, and those are the only two writes to it
       in the boot block -- the configuration registers are reached
       through one pair of helpers at F000:EA2B and F000:EA32, and every
       near call and jump to them in the F000 segment is accounted for.
       Only that segment can be read this way: the E000 half is
       compressed AMI modules, so a runtime module that touches these
       registers does not appear in that enumeration at all, and an
       earlier note here that called it exhaustive over the whole image
       was claiming more than the image gives up;
     - and it then uses all thirty-two pages, which a POST sweep walks
       end to end;
     - over a store whose own block length is 1FB0h and whose checksum
       runs to the end of the eight kilobytes.

   A store that is checksummed across 8K, by firmware that has just
   switched off the ESC's page address, can only work if the page address
   comes from somewhere else -- the board latching 0C00h itself, which it
   is free to do since the ESC decodes that port in plain sight. So the
   page register is what selects, and the bit is stored and read back
   without changing where an access lands.

   An earlier pass here implemented the collapse to page zero on the
   strength of the pin description alone. That is what the ESC's pins do;
   it is not what this board does, and the firmware above is the evidence
   that settles which. */
static uint8_t
esc_cram_page(const esc_t *dev)
{
    return dev->cram_page;
}

static void
esc_cram_decode(esc_t *dev)
{
    uint8_t cram = !!(dev->regs[0x4f] & 0x80);
    uint8_t p92  = !!(dev->regs[0x4f] & 0x40);

    esc_log("ESC: [%04X:%08X] PCSB %02X -> cram %u (was %u), port 92 %u "
            "(was %u), page %02X\n",
            CS, cpu_state.pc, dev->regs[0x4f],
            cram, dev->cram_decoded, p92, dev->port_92_decoded,
            dev->cram_page);

    if (p92 != dev->port_92_decoded) {
        dev->port_92_decoded = p92;
        if (dev->port_92 != NULL) {
            if (p92)
                port_92_add(dev->port_92);
            else
                port_92_remove(dev->port_92);
        }
    }

    /* Bit 7 names the accesses themselves, not the strobes that reach
       the part behind them: "This bit is used to enable (1) or disable
       (0) I/O write accesses to location 0C00h and I/O read/write
       accesses to locations 0800h - 08FFh." Bit 6 just below it is
       worded the same way for Port 92, and Port 92 really is this
       part's, so that one is acted on.

       This one is not, for the same reason Mode Select bit 5 above it is
       not. Something in this machine takes the bit away during POST, and
       the firmware then reads and checksums the whole eight kilobytes of
       the store through 0800h-08FFh afterwards -- the same thing it does
       after clearing the page address. A window that had really gone
       away could not answer that. So on this board 0800h-08FFh and 0C00h
       are the board's own, decoded and latched beside the ESC rather
       than by it, and clearing this bit is how the part is told to stop
       answering for something it does not own.

       Acting on it is what an earlier pass here did, and the machine
       came up saying "EISA NVRAM Bad" with a perfectly good store behind
       a window nothing could reach. The bit is tracked and reported --
       which is what the trace above is for, since what writes it is not
       visible in the plain half of the image -- and the window stays. */
    if (cram != dev->cram_decoded) {
        dev->cram_decoded = cram;
        esc_log("ESC: PCSB says the configuration RAM is %s; the board "
                "decodes it either way\n",
                cram ? "decoded" : "not decoded");
    }
}

static void
esc_conf_write(esc_t *dev, uint8_t index, uint8_t val)
{
    /* Every write, with the address that made it. Which of the several
       things in this machine reaches a given configuration register is
       not answerable from the ROM -- the E000 half is compressed, so a
       runtime module, an option ROM and the configuration utility are
       all invisible until they run -- and it is the question whenever a
       decode changes without the boot block having asked. */
    esc_log("ESC: [%04X:%08X] conf wr %02X = %02X (was %02X)%s\n",
            CS, cpu_state.pc, index, val, dev->regs[index],
            (!dev->unlocked && (index != 0x02)) ? " DROPPED, locked" : "");

    /* Until the ID register has been given 0Fh the part is deaf to
       everything else. */
    if (!dev->unlocked && (index != 0x02))
        return;

    switch (index) {
        case 0x02: /* ESCID */
            dev->regs[0x02] = val;
            dev->unlocked   = (val == ESC_UNLOCK_KEY);
            esc_log("ESC: id %02x, %s\n", val,
                    dev->unlocked ? "unlocked" : "locked");
            break;

        case 0x08: /* RID, read only */
            break;

        case 0x40: /* MS, mode select */
            /* Bits 1:0 choose how many slots the part decodes for, which
               is what decides whether the address enables are direct or
               encoded. Everything here is the direct four slot case. */
            dev->regs[0x40] = val & 0x7f;
            esc_log("ESC: mode select %02x, PIRQs %s\n", val,
                    (val & 0x40) ? "on" : "off");
            /* Bit 3 is the other direction of the SERR# gate, and taking
               it away is how a handler clears that NMI -- it is the only
               control SERR# has: "When this bit = 0, the NMI signal is
               negated and SERR# is disabled from generating an NMI."
               Only negate what SERR# itself raised; the other sources
               have their own enables and are not ours to drop. */
            if (!(val & 0x08) && dev->serr_nmi) {
                dev->serr_nmi = 0;
                nmi           = 0;
            }
            esc_pirq_update(dev);
            break;

        case 0x42: /* BIOSCSA */
        case 0x43: /* BIOSCSB */
            /* Which BIOS ranges the part answers for, and whether it
               answers writes as well as reads: "if the range has been
               enabled the LBIOSCS# signal is always asserted for memory
               reads in the enabled BIOS range. If the BIOS Write Enable
               bit is set in the configuration register BIOSCSB, the
               LBIOSCS# is also asserted for memory write cycles."

               The write half is enforced at the flash, which is where
               the chip select is: esc_bios_read_gate and its write
               counterpart are the flash's gates for as long as this part
               exists, and read these two registers live. They compose
               with the north bridge rather than fighting it -- the PAM
               decides whether a cycle leaves the processor for the bus,
               and only then does this part select the BIOS. */
            dev->regs[index] = val;
            esc_log("ESC: BIOSCS%c %02X, BIOS writes %s\n",
                    (index == 0x42) ? 'A' : 'B', val,
                    (dev->regs[0x43] & 0x08) ? "enabled" : "disabled");
            /* What the flash may be fetched from changed, and so may what
               a cached read of it would find. */
            flash_bios_decode_changed();
            flushmmucache_nopc();
            break;

        case 0x4d: /* CLKDIV */
            dev->regs[0x4d] = val & 0x73;
            break;

        case 0x4e: /* PCSA */
        case 0x4f: /* PCSB */
            dev->regs[index] = val;
            /* Bit 7 "is used to enable (1) or disable (0) I/O write
               accesses to location 0C00h and I/O read/write accesses to
               locations 0800h-08FFh", which is the configuration RAM
               window and its page register. */
            esc_cram_decode(dev);
            break;

        case 0x50: /* EISAID1..4 */
        case 0x51:
        case 0x52:
        case 0x53:
            dev->regs[index]            = val;
            dev->board_id[index - 0x50] = val;
            eisa_set_board_id(dev->board_id);
            break;

        case 0x57: /* SGRBA, where the scatter-gather registers live */
            dev->regs[0x57] = val;
            dma_remove_sg();
            dma_set_sg_base(val);
            break;

        case 0x59: /* APICBA */
            dev->regs[0x59] = val & 0x3f;
            esc_apic_remap(dev);
            break;

        case 0x60: /* PIRQRC0..3 */
        case 0x61:
        case 0x62:
        case 0x63:
            dev->regs[index] = val & 0x8f;
            esc_pirq_update(dev);
            esc_log("ESC: PIRQ %c -> %02x\n", 'A' + (index & 3), val);
            break;

        /* Three general purpose chip selects, each a low and high address
           and a mask. Nothing on this side of the bridge acts on them;
           they drive pins. */
        /* The three general purpose chip selects -- a base address, a
           mask, and in GPXBC whether the X-Bus transceiver goes with
           them. They decode a window and pull a pin low over it, for
           whatever a board hangs there. This one hangs nothing: the
           pins go nowhere, and a chip select with no part on the end of
           it has nothing to do but read back. */
        case 0x64:
        case 0x65:
        case 0x66:
        case 0x68:
        case 0x69:
        case 0x6a:
        case 0x6c:
        case 0x6d:
        case 0x6e:
        case 0x6f: /* GPXBC */
            dev->regs[index] = val;
            break;

        case 0x70: /* PAC, PCI/APIC control: INTR and SMI routing */
            dev->regs[0x70] = val & 0x03;
            break;

        case 0x88: /* TSTC, test control */
            dev->regs[0x88] = val;
            break;

        case 0xa0: /* SMICNTL */
            dev->regs[0xa0] = val & 0x0f;
            /* Bit 3 starts and stops the Fast Off timer. */
            esc_fast_off_remap(dev);
            /* And bit 0 is only the pin: "If an SMI is pending when this
               bit is set to 1, the SMI# signal is asserted." */
            if ((val & 0x01) && dev->regs[0xaa])
                smi_raise();
            break;
        case 0xa2: /* SMIEN */
        case 0xa3:
            dev->regs[index] = val;
            break;
        case 0xa4: /* SEE */
        case 0xa5:
        case 0xa6:
        case 0xa7:
            dev->regs[index] = val;
            break;
        case 0xa8: /* FTMR */
            /* "A read from the FTMR Register returns the value last
               written", and the count restarts from it. The book asks
               for the timer to be stopped first and says not to write
               00h; neither is enforced here, and a zero simply means the
               reload never leaves zero. */
            dev->regs[0xa8]     = val;
            dev->fast_off_count = val;
            break;
        case 0xaa: /* SMIREQ, status */
        case 0xab:
            /* Not write one to clear, which is what this was: "Software
               sets the status bits to 0 by writing a 0 to them. Only the
               ESC hardware can set status bits to a 1. Software writing a
               1 to any of the status bits has no effect." So a written
               bit keeps what is there and a written zero takes it away. */
            dev->regs[index] &= val;
            break;
        case 0xac: /* CTLTMRL */
        case 0xae: /* CTLTMRH */
            /* How long STPCLK# is held low and high when SMICNTL bit 2
               has handed its timing to these. STPCLK# is a pin into the
               processor that throttles it, and 86Box's does not have
               one -- nor the stop grant cycle the book has it waiting
               for. The same absence leaves SMICNTL bits 2 and 1 stored
               and no more, bit 1 being what makes a read of APMC assert
               the signal in the first place. */
            dev->regs[index] = val;
            break;

        default:
            /* Reserved. Writes have no effect. */
            break;
    }
}

static uint8_t esc_conf_read_reg(esc_t *dev, uint8_t index);

static uint8_t
esc_conf_read(esc_t *dev, uint8_t index)
{
    uint8_t ret = esc_conf_read_reg(dev, index);

    esc_log("ESC: [%04X:%08X] conf rd %02X = %02X%s\n", CS, cpu_state.pc,
            index, ret, dev->unlocked ? "" : " (locked)");

    return ret;
}

static uint8_t
esc_conf_read_reg(esc_t *dev, uint8_t index)
{
    if (!dev->unlocked && (index != 0x02))
        return 0x00;

    switch (index) {
        case 0x02:
        case 0x08:
        case 0x40:
        case 0x42:
        case 0x43:
        case 0x4d:
        case 0x4e:
        case 0x4f:
        case 0x50:
        case 0x51:
        case 0x52:
        case 0x53:
        case 0x57:
        case 0x59:
        case 0x60:
        case 0x61:
        case 0x62:
        case 0x63:
        case 0x64:
        case 0x65:
        case 0x66:
        case 0x68:
        case 0x69:
        case 0x6a:
        case 0x6c:
        case 0x6d:
        case 0x6e:
        case 0x6f:
        case 0x70:
        case 0x88:
        case 0xa0:
        case 0xa2:
        case 0xa3:
        case 0xa4:
        case 0xa5:
        case 0xa6:
        case 0xa7:
        case 0xa8:
        case 0xaa:
        case 0xab:
        case 0xac:
        case 0xae:
            return dev->regs[index];

        default:
            /* A reserved location reads as zero and still completes. */
            return 0x00;
    }
}

/* ------------------------------------------------------------------ */
/* ESC I/O                                                            */
/* ------------------------------------------------------------------ */

static void
esc_write(uint16_t port, uint8_t val, void *priv)
{
    esc_t *dev = (esc_t *) priv;

    switch (port) {
        case ESC_CONF_INDEX:
            dev->index = val;
            break;
        case ESC_CONF_DATA:
            esc_conf_write(dev, dev->index, val);
            break;

        case 0x0461: /* NMIESC, extended NMI status and control */
            /* Bits 7:4 are status and read only; the rest are controls,
               and clearing an enable also clears the condition it
               reported. */
            /* Bits 6 and 4, the bus timeout statuses, are never set
               here. They report an EISA master that kept the bus more
               than 64 BCLKs after the ESC took MACKx# away -- "EISA
               Masters must release the bus within 64 BCLKs (8 ms) after
               the ESC negates MACKx#. If the bus master attempts to start
               a new bus cycle after this timeout period, a bus timeout
               (NMI) is generated." 86Box has no interval for that to be
               measured over: a bus master here runs its whole transfer
               inside one timer callback, so the bus is never held across
               an arbitration cycle and MACKx# is never negated under a
               master still running. The bits read back, and the enable
               below clears them, but nothing can raise them. */
            dev->nmi_esc = (uint8_t) ((dev->nmi_esc & 0xf0) | (val & 0x0f));
            /* The book states this clearing for the fail-safe and bus
               timeout enables below and not for this one; it is done the
               same way for consistency with them. */
            if (!(val & 0x02))
                dev->nmi_esc &= ~0x20;
            if (!(val & 0x04))
                dev->nmi_esc &= ~0x80;
            if (!(val & 0x08))
                dev->nmi_esc &= ~0x50;
            /* Bit 0 drives RSTDRV, which resets everything on the bus.
               Software holds it for a few clocks and takes it away again;
               the cards see the edge, so reset them as it goes on. */
            if (val & 0x01) {
                esc_log("ESC: RSTDRV, resetting the bus\n");
                eisa_reset();
            }
            break;

        case 0x0462: /* SOFTNMI, a write of any value is the event */
            if (dev->nmi_esc & 0x02) {
                dev->nmi_esc |= 0x20;
                nmi = 1;
                esc_system_event(dev, 29);
            }
            break;

        case 0x0c00: /* CONFRAMP, which page of the configuration RAM */
            /* Latched beside the ESC rather than by it -- see
               esc_cram_decode -- so PCSB bit 7 does not take it away.
               Reported with the bit's state so a trace says both. */
            esc_log("ESC: [%04X:%08X] CONFRAMP = %02X (PCSB %02X, MS %02X)\n",
                    CS, cpu_state.pc, val & 0x1f, dev->regs[0x4f],
                    dev->regs[0x40]);
            dev->cram_page = val & 0x1f;
            break;

        case 0x0800 ... 0x08ff: /* the configuration RAM itself */
            esc_log("ESC: [%04X:%08X] cram wr %02x:%02x = %02x\n", CS,
                    cpu_state.pc, esc_cram_page(dev), port & 0xff, val);
            dev->cram[(esc_cram_page(dev) * 256) + (port & 0xff)] = val;
            break;

        case 0x0c80: /* the board identifier is read only from here */
        case 0x0c81:
        case 0x0c82:
        case 0x0c83:
            break;

        default:
            break;
    }
}

static uint8_t
esc_read(uint16_t port, void *priv)
{
    const esc_t *dev = (esc_t *) priv;

    switch (port) {
        case ESC_CONF_INDEX:
            return dev->index;
        case ESC_CONF_DATA:
            return esc_conf_read((esc_t *) dev, dev->index);

        case 0x0461:
            return dev->nmi_esc;

        case 0x0c00:
            esc_log("ESC: [%04X:%08X] CONFRAMP read %02X\n", CS,
                    cpu_state.pc, dev->cram_page);
            return dev->cram_page;

        case 0x0800 ... 0x08ff:
            esc_log("ESC: [%04X:%08X] cram rd %02x:%02x = %02x\n", CS,
                    cpu_state.pc, esc_cram_page(dev), port & 0xff,
                    dev->cram[(esc_cram_page(dev) * 256) + (port & 0xff)]);
#ifdef ENABLE_ESC_LOG
            {
                /* Every 512th read, the return addresses on the guest stack:
                   the primitive at F000:69D7 has pushed flags, BX and DX by
                   the time it reads, so its caller is at SS:SP+6 and the
                   caller's caller a little above that. */
                static uint32_t n = 0;

                if ((n++ % 512) == 0) {
                    uint32_t sp = cpu_state.seg_ss.base + SP;

                    esc_log("ESC:   stack %04X %04X %04X %04X %04X %04X %04X %04X\n",
                            mem_readw_phys(sp + 6), mem_readw_phys(sp + 8), mem_readw_phys(sp + 10),
                            mem_readw_phys(sp + 12), mem_readw_phys(sp + 14), mem_readw_phys(sp + 16),
                            mem_readw_phys(sp + 18), mem_readw_phys(sp + 20));
                }
            }
#endif
            return dev->cram[(esc_cram_page(dev) * 256) + (port & 0xff)];

        case 0x0464:
            /* Which EISA master was granted the bus last. Nothing here
               arbitrates, so it is whatever was recorded. */
            return dev->last_mst;

        case 0x0c80:
        case 0x0c81:
        case 0x0c82:
        case 0x0c83:
            /* Whoever is asking what board this is -- the firmware during
               POST, and an operating system deciding whether there is an
               EISA bus here at all. Worth seeing, since the answer used
               to be nothing. */
            esc_log("ESC: [%04X:%08X] board id %d = %02x\n", CS,
                    cpu_state.pc, port & 3, dev->board_id[port & 3]);
            return dev->board_id[port & 3];

        default:
            return 0xff;
    }
}

static void
esc_reset_hard(esc_t *dev)
{
    memset(dev->regs, 0, sizeof(dev->regs));

    dev->index    = 0;
    dev->unlocked = 0;

    /* B-0 stepping. The EB is 02h. */
    dev->regs[0x08] = 0x03;
    /* MS, mode select. Bit 5, the configuration RAM page address
       generation, is a one out of reset: "If this bit is set to 1,
       accesses to the configuration RAM space will generate the RAM page
       address ... The default for this bit is 1." Bit 6, which is what
       makes the pins PIRQs rather than MREQs, is not. */
    dev->regs[0x40] = 0x20;
    /* BIOSCSA, BIOS chip select A: High BIOS alone. */
    dev->regs[0x42] = 0x10;
    dev->regs[0x43] = 0x00;
    flash_bios_decode_changed();
    /* CLKDIV, the EISA clock divisor: xx001000b. */
    dev->regs[0x4d] = 0x08;
    /* PCSA, peripheral chip select A: x0000111b. */
    dev->regs[0x4e] = 0x07;
    /* PCSB, peripheral chip select B. CRAM decode and Port 92 decode are
       both enabled out of reset, and the parallel port is decoded at
       LPT1 -- CFh. Coming up at zero told anything that reads it back
       that the configuration RAM is not decoded at all. */
    dev->regs[0x4f] = 0xcf;
    /* The scatter-gather registers default to the 04xx page. */
    dev->regs[0x57] = 0x04;
    /* Every PCI interrupt starts unrouted. */
    dev->regs[0x60] = dev->regs[0x61] = 0x80;
    dev->regs[0x62] = dev->regs[0x63] = 0x80;
    /* GPCSH[2:0], the general purpose chip select high addresses. */
    dev->regs[0x65] = dev->regs[0x69] = dev->regs[0x6d] = 0xc0;
    /* SMICNTL, SMI control. */
    dev->regs[0xa0] = 0x08;
    /* FTMR, the fast off timer. */
    dev->regs[0xa8] = 0x0f;

    dev->nmi_esc  = 0x00;
    dev->last_mst = 0x00;

    /* SMICNTL comes up 08h, which is the Fast Off timer frozen, and FTMR
       0Fh, which is the count it will start from once it is let go. */
    dev->fast_off_count = dev->regs[0xa8];
    esc_fast_off_remap(dev);

    /* EISAID1..4, which 0C80h-0C83h answer from. The book starts them at
       zero for firmware to fill in during configuration; this board's
       firmware never writes them, so what comes back is what the board
       straps -- see esc_set_board_id. Before the machine has said what it
       is, that is still zero. */
    for (uint8_t i = 0; i < 4; i++) {
        dev->regs[0x50 + i] = dev->strap_id[i];
        dev->board_id[i]    = dev->strap_id[i];
    }
    eisa_set_board_id(dev->board_id);

    dma_remove_sg();
    dma_set_sg_base(0x04);

    esc_pirq_update(dev);

    esc_log("ESC: hard reset, MS %02X PCSB %02X BIOSCSA %02X BIOSCSB %02X "
            "PCSA %02X, board %02X %02X %02X %02X\n",
            dev->regs[0x40],
            dev->regs[0x4f], dev->regs[0x42], dev->regs[0x43],
            dev->regs[0x4e], dev->board_id[0], dev->board_id[1],
            dev->board_id[2], dev->board_id[3]);

    esc_cram_decode(dev);

    esc_apic_reset(dev);
    esc_apic_remap(dev);

    eisa_reset();
}

static void
esc_reset(void *priv)
{
    esc_reset_hard((esc_t *) priv);
}

/* LBIOSCS#: the flash is selected when the address is in a BIOS range
   BIOSCSA or BIOSCSB enables (82374EB 3.1.4, 3.1.5), for reads and, with
   BIOSCSB bit 3, BIOS Write Enable, for writes as well. The north bridge
   has already decided whether the cycle left the processor for the bus at
   all; this is the chip select downstream of it, so the two compose. A
   range not enabled is not decoded, and the bus reads as open there:
   PhoenixBIOS on the D823 relies on that, dropping Low BIOS 1 and 2 (the
   Symbios SCSI module at E0000h) before its adapter ROM scan so the scan
   does not find and run it. */
static int
esc_bios_read_gate(uint32_t addr, void *priv)
{
    const esc_t  *dev = (const esc_t *) priv;
    const uint8_t a   = dev->regs[0x42];
    const uint8_t b   = dev->regs[0x43];
    uint32_t      low;

    /* High BIOS: 0F0000h-0FFFFFh, FF0000h-FFFFFFh, FFFF0000h-FFFFFFFFh. */
    if ((a & 0x10) && (((addr >= 0x000f0000) && (addr <= 0x000fffff)) || ((addr >= 0x00ff0000) && (addr <= 0x00ffffff)) || (addr >= 0xffff0000)))
        return 1;
    /* Enlarged BIOS: FFF80000h-FFFDFFFFh. */
    if ((a & 0x20) && (addr >= 0xfff80000) && (addr <= 0xfffdffff))
        return 1;
    /* Low BIOS 1-4: 16 KB each from 0E0000h, and the same at FFEE0000h and
       FFFE0000h. */
    if (((addr >= 0x000e0000) && (addr <= 0x000effff)) || ((addr >= 0xffee0000) && (addr <= 0xffeeffff)) || ((addr >= 0xfffe0000) && (addr <= 0xfffeffff))) {
        low = (addr >> 14) & 0x03;
        if (a & (1 << low))
            return 1;
    }
    /* 16 Meg BIOS: FF0000h-FFFFFFh. */
    if ((b & 0x04) && (addr >= 0x00ff0000) && (addr <= 0x00ffffff))
        return 1;
    /* Low and High VGA BIOS: 0C0000h-0C3FFFh and 0C4000h-0C7FFFh. */
    if ((b & 0x01) && (addr >= 0x000c0000) && (addr <= 0x000c3fff))
        return 1;
    if ((b & 0x02) && (addr >= 0x000c4000) && (addr <= 0x000c7fff))
        return 1;

    return 0;
}

static int
esc_bios_write_gate(uint32_t addr, void *priv)
{
    const esc_t *dev = (const esc_t *) priv;

    if (!(dev->regs[0x43] & 0x08))
        return 0;

    return esc_bios_read_gate(addr, priv);
}

static void
esc_close(void *priv)
{
    esc_t *dev = (esc_t *) priv;

    esc_cram_save(dev);

    if (flash_bios_write_gate_priv == dev) {
        flash_bios_write_gate      = NULL;
        flash_bios_write_gate_priv = NULL;
    }
    if (flash_bios_read_gate_priv == dev) {
        flash_bios_read_gate      = NULL;
        flash_bios_read_gate_priv = NULL;
    }
    if (esc_inst == dev)
        esc_inst = NULL;
    free(dev);
}

static void *
esc_init(UNUSED(const device_t *info))
{
    esc_t *dev = (esc_t *) calloc(1, sizeof(esc_t));

    esc_inst = dev;

    /* The BIOS flash's chip select is ours, reads and writes; see
       esc_bios_read_gate. */
    flash_bios_write_gate      = esc_bios_write_gate;
    flash_bios_write_gate_priv = dev;
    flash_bios_read_gate       = esc_bios_read_gate;
    flash_bios_read_gate_priv  = dev;

    /* The compatible half of the part. 86Box already models the pieces;
       what the ESC adds is that they are all in one place and that the
       EISA extensions are switched on. */
    dma_set_params(1, 0xffffffff);
    dma_ext_mode_init();
    dma_high_page_init();
    dma_eisa_init();
    dma_set_sg_base(0x04);

    /* Two interrupt controllers whose trigger can be chosen per line,
       which is what EISA needs and what ISA never had. */
    pic_elcr_set_enabled(1);
    pic_elcr_io_handler(1);

    /* A second timer: counter 0 is the fail-safe timer that can raise
       NMI, counter 2 drives CPU speed control. Adding the device does not
       fill in the interface table the way the first timer's init does, so
       that is done here, or nothing could attach to its output; it is the
       same kind of timer as the first, which the machine chose. */
    if (pit_devs[0].set_out_func == pit_fast_intf.set_out_func) {
        pit_devs[1]      = pit_fast_intf;
        pit_devs[1].data = device_add(&i8254_sec_fast_device);
    } else {
        pit_devs[1]      = pit_classic_intf;
        pit_devs[1].data = device_add(&i8254_sec_device);
    }

    /* Timer 2's first counter is the fail-safe timer, and its output is
       an NMI source rather than an interrupt. Its gate is tied high; a
       fresh timer has every gate low, and mode 0 does not count without
       it. */
    pit_devs[1].set_gate(pit_devs[1].data, 0, 1);
    pit_devs[1].set_out_func(pit_devs[1].data, 0, esc_fail_safe_timer);
    /* And it counts a quarter as fast as the system timer: "Clock In
       ... 0.298 MHz (OSC/48)" against 1.193 MHz (OSC/12) for Timer 1
       (82374EB, Table 20, Interval Timer Functions). */
    pit_devs[1].set_clock_div(pit_devs[1].data, 0, 4);

    /* The 82374SB's two power management ports, APMC at 0B2h and APMS at
       0B3h. The data book puts them in normal I/O space rather than in
       the configuration registers with the rest of the power management,
       and they are eight bit accesses only: "This register passes data
       (APM Commands) between the OS and the SMI handler." */
    dev->apm = device_add(&apm_pci_device);
    /* Its do_smi stays clear: whether a write to APMC raises an SMI is
       SMIEN bit 7's to say and SMICNTL bit 0's to gate, so the ESC
       watches the port itself rather than letting the power management
       device raise one of its own. */
    io_sethandler(0x00b2, 0x0001, NULL, NULL, NULL, esc_apmc_write,
                  NULL, NULL, dev);
    timer_add(&dev->fast_off, esc_fast_off_tick, dev, 0);

    dev->port_92 = device_add(&port_92_pci_device);

    io_sethandler(ESC_CONF_INDEX, 0x0002, esc_read, NULL, NULL, esc_write,
                  NULL, NULL, dev);
    io_sethandler(0x0461, 0x0002, esc_read, NULL, NULL, esc_write, NULL, NULL,
                  dev);
    io_sethandler(0x0464, 0x0001, esc_read, NULL, NULL, esc_write, NULL, NULL,
                  dev);
    io_sethandler(0x0c80, 0x0004, esc_read, NULL, NULL, esc_write, NULL, NULL,
                  dev);
    dev->cram_decoded    = 1;
    dev->port_92_decoded = 1;
    io_sethandler(0x0800, 0x0100, esc_read, NULL, NULL, esc_write, NULL, NULL,
                  dev);
    io_sethandler(0x0c00, 0x0001, esc_read, NULL, NULL, esc_write, NULL, NULL,
                  dev);

    mem_mapping_add(&dev->apic_mapping, 0xfec00000, 0x400,
                    esc_apic_readb, esc_apic_readw, esc_apic_readl,
                    esc_apic_writeb, esc_apic_writew, esc_apic_writel,
                    NULL, MEM_MAPPING_EXTERNAL, dev);

    /* The machine owns this now: "Auto EISA Config" on its options page.
       On, the store is built fresh whenever it is missing or the cards
       have changed. Off, nothing here writes to it at all. */
    dev->cram_auto = (uint8_t) machine_get_config_int("auto_eisa_config");
    snprintf(dev->cram_file, sizeof(dev->cram_file), "%s_eisa.nvr",
             machine_get_internal_name());
    esc_cram_load(dev);

    esc_reset_hard(dev);

    return dev;
}

const device_t esc_device = {
    .name          = "Intel 82374SB (ESC)",
    .internal_name = "esc",
    .flags         = DEVICE_EISA,
    .local         = 0x03,
    .init          = esc_init,
    .close         = esc_close,
    .reset         = esc_reset,
    .available     = NULL,
    .speed_changed = NULL,
    .force_redraw  = NULL,
    .config        = NULL
};

/* ------------------------------------------------------------------ */
/* PCEB — the PCI facing half                                         */
/* ------------------------------------------------------------------ */

/* The four memory and four I/O windows that let an EISA bus master reach
   PCI. Each memory region is four bytes: a base and a size, both in the
   units the register defines. */
static void
pceb_recalc_regions(UNUSED(pceb_t *dev))
{
    /* The forwarding decision is made when a cycle is run, not here; the
       registers are kept so firmware reads back what it wrote and so the
       decode can consult them. Memory forwarding on this bridge is a
       positive decode and 86Box's EISA masters already reach memory
       through the same physical address space, so nothing has to be
       remapped for it to work. */
}

static void
pceb_write(int func, int addr, UNUSED(int len), uint8_t val, void *priv)
{
    pceb_t *dev = (pceb_t *) priv;

    if (func > 0)
        return;

    switch (addr) {
        case 0x04: /* PCICMD */
            dev->regs[0x04] = val & 0x47;
            break;
        case 0x05:
            dev->regs[0x05] = val & 0x01;
            break;

        case 0x07: /* PCISTS, write one to clear */
            dev->regs[0x07] &= (uint8_t) ~(val & 0xf9);
            break;

        case 0x0d: /* MLTIM */
            dev->regs[0x0d] = val & 0xf8;
            break;

        case 0x40: /* PCICON */
            dev->regs[0x40] = val;
            break;
        case 0x41: /* ARBCON */
            dev->regs[0x41] = val;
            break;
        case 0x42: /* ARBPRI */
            dev->regs[0x42] = val;
            break;
        case 0x43: /* ARBPRIX */
            dev->regs[0x43] = val;
            break;

        case 0x44: /* MCSCON */
        case 0x45: /* MCSBOH */
        case 0x46: /* MCSTOH */
        case 0x47: /* MCSTOM */
            dev->regs[addr] = val;
            break;

        case 0x48: /* EADC1 */
        case 0x49:
            dev->regs[addr] = val;
            break;

        case 0x4c: /* IORTC */
            dev->regs[0x4c] = val;
            break;

        case 0x54: /* MAR1..3 */
        case 0x55:
        case 0x56:
            dev->regs[addr] = val;
            break;

        case 0x58: /* PDCON */
            dev->regs[0x58] = val;
            break;

        case 0x5a: /* EADC2 */
            dev->regs[0x5a] = val;
            break;

        case 0x5c: /* EPMRA */
            dev->regs[0x5c] = val;
            break;

        /* EISA-to-PCI memory regions 1 through 4, four bytes each. */
        case 0x60 ... 0x6f:
            dev->regs[addr] = val;
            pceb_recalc_regions(dev);
            break;

        /* EISA-to-PCI I/O regions 1 through 4. */
        case 0x70 ... 0x7f:
            dev->regs[addr] = val;
            pceb_recalc_regions(dev);
            break;

        case 0x80: /* BTMR, the BIOS timer's base address */
        case 0x81:
            dev->regs[addr] = val;
            pceb_bios_timer_remap(dev);
            break;

        case 0x84: /* ELTCR */
            dev->regs[0x84] = val;
            break;

        /* 88-8B is the test control register, which the datasheet says
           plainly must not be written. Reads are allowed. */
        default:
            break;
    }
}

static uint8_t
pceb_read(int func, int addr, UNUSED(int len), void *priv)
{
    const pceb_t *dev = (pceb_t *) priv;

    if (func > 0)
        return 0xff;

    return dev->regs[addr & 0xff];
}

static void
pceb_reset_hard(pceb_t *dev)
{
    memset(dev->regs, 0, sizeof(dev->regs));

    dev->regs[0x00] = 0x86; /* Intel */
    dev->regs[0x01] = 0x80;
    dev->regs[0x02] = 0x82; /* 82375EB/SB */
    dev->regs[0x03] = 0x04;
    /* PCICMD, 0007h: I/O space, memory space and bus mastering are all
       enabled out of reset on this part. */
    dev->regs[0x04] = 0x07;
    dev->regs[0x05] = 0x00;
    /* PCISTS, 0200h: medium DEVSEL timing and nothing else. */
    dev->regs[0x06] = 0x00;
    dev->regs[0x07] = 0x02;
    dev->regs[0x08] = 0x04; /* revision */
    dev->regs[0x09] = 0x00;
    /* 09h-0Ch are Reserved on this part -- the whole of what later became
       the class code. The 82375EB is a PCI 1.0 device and its register
       table has no class code in it at all, so those bytes read zero.
       Software knows what this is from the vendor and device identifiers,
       8086h and 0482h, and nothing else. */
    dev->regs[0x0a] = 0x00;
    dev->regs[0x0b] = 0x00;

    dev->regs[0x40] = 0x00; /* PCICON */
    dev->regs[0x41] = 0x80; /* ARBCON */
    dev->regs[0x42] = 0x04; /* ARBPRI */
    dev->regs[0x43] = 0x00; /* ARBPRIX */
    dev->regs[0x44] = 0x00; /* MCSCON */
    dev->regs[0x45] = 0x10; /* MCSBOH, the bottom of the hole */
    dev->regs[0x46] = 0x0f; /* MCSTOH, the top of it */
    dev->regs[0x47] = 0x00; /* MCSTOM */
    dev->regs[0x48] = 0x01; /* EADC1, 0001h */
    dev->regs[0x49] = 0x00;
    dev->regs[0x4c] = 0x56; /* IORTC, the ISA I/O recovery time */
    /* MEMREGN[4:1] come up as 0000FFFFh and IOREGN[4:1] as 0000FFFCh:
       a base of zero and a size field of all ones, which is how a region
       that has not been programmed reads. */
    for (uint8_t i = 0; i < 4; i++) {
        dev->regs[0x60 + (i * 4)] = 0xff;
        dev->regs[0x61 + (i * 4)] = 0xff;
        dev->regs[0x70 + (i * 4)] = 0xfc;
        dev->regs[0x71 + (i * 4)] = 0xff;
    }
    dev->regs[0x84] = 0x7f; /* ELTCR, the EISA latency timer */
    /* BTMR, 0078h. Bit 0 is the enable and it is clear, which is what the
       book means by the BIOS timer's decode being off after reset. */
    dev->regs[0x80] = 0x78;
    dev->regs[0x81] = 0x00;
    pceb_bios_timer_remap(dev);
}

static void
pceb_reset(void *priv)
{
    pceb_reset_hard((pceb_t *) priv);
}

static void
pceb_close(void *priv)
{
    free(priv);
}

static void *
pceb_init(UNUSED(const device_t *info))
{
    pceb_t *dev = (pceb_t *) calloc(1, sizeof(pceb_t));

    pceb_reset_hard(dev);

    timer_add(&dev->bios_timer, pceb_bios_timer_tick, dev, 0);

    pci_add_card(PCI_ADD_SOUTHBRIDGE, pceb_read, pceb_write, dev, &dev->pci_slot);

    return dev;
}

const device_t pceb_device = {
    .name          = "Intel 82375SB (PCEB)",
    .internal_name = "pceb",
    .flags         = DEVICE_PCI,
    .local         = 0x04,
    .init          = pceb_init,
    .close         = pceb_close,
    .reset         = pceb_reset,
    .available     = NULL,
    .speed_changed = NULL,
    .force_redraw  = NULL,
    .config        = NULL
};
