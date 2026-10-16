/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          8018x integrated peripheral control block: the on-chip peripherals
 *          of the 80186/80188 that the CPU core in 808x.c does not provide.
 *          Relocation, the timers, DMA and the interrupt controller work;
 *          the controller leaves raising the interrupt to the board, the
 *          chip selects are stored only and the refresh unit is not modelled.
 *
 * Authors: WNT50
 *
 *          Copyright 2026 WNT50.
 */
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <stdlib.h>
#include <stdarg.h>
#define HAVE_STDARG_H
#include <86box/86box.h>
#include "cpu.h"
#include "x86seg.h"
#include <86box/device.h>
#include <86box/timer.h>
#include <86box/log.h>
#include <86box/io.h>
#include <86box/dma.h>
#include <86box/mem.h>
#include <86box/i8018x.h>

#define I8018X_PCB_BASE_DEFAULT 0xFF00u
#define I8018X_PCB_SIZE         0x100u
#define I8018X_RELOC_OFF        0xFEu

/* Reset values per Intel 270788-001 section 5.1. */
#define I8018X_RELOC_RESET      0x20FFu /* RELO reads 0x20FF after RESET */
#define I8018X_UMCS_RESET       0xFFFBu /* UMCS reads 0xFFFB after RESET */

/* PCB register offsets (word). */
#define I8018X_ICU_BASE         0x20u
#define I8018X_ICU_END          0x3Fu
#define I8018X_TMR_COUNT(i)     (0x50u + ((i) * 8u)) /* T0 0x50 / T1 0x58 / T2 0x60 */
#define I8018X_TMR_MAXA(i)      (0x52u + ((i) * 8u))
#define I8018X_TMR_MAXB(i)      (0x54u + ((i) * 8u))
#define I8018X_TMR_CON(i)       (0x56u + ((i) * 8u))
#define I8018X_TMR_END          0x67u
#define I8018X_CS_BASE          0xA0u /* UMCS/LMCS/PACS/MMCS/MPCS, 0xA0-0xA9 */
#define I8018X_CS_END           0xA9u
#define I8018X_DMA0_BASE        0xC0u /* channel 0 descriptor, 0xC0-0xCE */
#define I8018X_DMA0_END         0xCEu
#define I8018X_DMA1_BASE        0xD0u /* channel 1 descriptor, 0xD0-0xDE */
#define I8018X_DMA1_END         0xDEu

/* Descriptor word indices from a channel base (270788-001 Figure 66): source
   pointer, destination pointer, transfer count, control word. */
#define I8018X_DMA_SRC_LO(i)    ((I8018X_DMA0_BASE + ((i) * 0x10u)) / 2)
#define I8018X_DMA_SRC_HI(i)    ((I8018X_DMA0_BASE + ((i) * 0x10u) + 2u) / 2)
#define I8018X_DMA_DST_LO(i)    ((I8018X_DMA0_BASE + ((i) * 0x10u) + 4u) / 2)
#define I8018X_DMA_DST_HI(i)    ((I8018X_DMA0_BASE + ((i) * 0x10u) + 6u) / 2)
#define I8018X_DMA_COUNT(i)     ((I8018X_DMA0_BASE + ((i) * 0x10u) + 8u) / 2)
#define I8018X_DMA_CTRL(i)      ((I8018X_DMA0_BASE + ((i) * 0x10u) + 10u) / 2)

/* DMA control word (270788-001 Figure 67). The source and destination fields
   are three separate bits each: M/IO selects memory, INC/DEC move the pointer
   after every transfer. SYN (bits 7-6), INT (bit 8) and W/B (bit 0) are not
   modelled: transfers are always byte and the interrupt controller is still
   unwired, so a channel never interrupts. */
#define I8018X_DMA_DST_MIO      0x8000u
#define I8018X_DMA_DST_DEC      0x4000u
#define I8018X_DMA_DST_INC      0x2000u
#define I8018X_DMA_SRC_DEC      0x0800u
#define I8018X_DMA_SRC_INC      0x0400u
#define I8018X_DMA_TC           0x0200u
#define I8018X_DMA_CHG          0x0004u /* write only; always reads back as 0 */
#define I8018X_DMA_ST           0x0002u

/* Interrupt controller, master mode (270788-001 section 9.5, Figures 80-87). */
#define I8018X_ICU_EOI          0x22u /* End of Interrupt Register */
#define I8018X_ICU_POLL         0x24u /* Poll Register, not modelled */
#define I8018X_ICU_POLL_STATUS  0x26u /* Poll Status Register, not modelled */
#define I8018X_ICU_MASK         0x28u /* Interrupt Mask Register */
#define I8018X_ICU_PRIO_MASK    0x2Au /* Priority Mask Register */
#define I8018X_ICU_INSERVICE    0x2Cu /* In-Service Register */
#define I8018X_ICU_REQ          0x2Eu /* Interrupt Request Register */
#define I8018X_ICU_STATUS       0x30u /* Interrupt Controller Status Register */
#define I8018X_ICU_TMR_CON      0x32u /* control for all three timers at once */
#define I8018X_ICU_DMA_CON(i)   (0x34u + ((i) * 2u)) /* DMA0 at 34h, DMA1 at 36h */
#define I8018X_ICU_INT_CON(i)   (0x38u + ((i) * 2u)) /* INT0 at 38h .. INT3 at 3Eh */

/* One bit per source in the request, mask and in-service registers. Unlike
   slave mode these are not the interrupt types: the three timers share TMR
   and the four external inputs have bits of their own. */
#define I8018X_ICU_SRC_TMR      0x0001u
#define I8018X_ICU_SRC_DMA(i)   (0x0004u << (i))
#define I8018X_ICU_SRC_INT(i)   (0x0010u << (i))
#define I8018X_ICU_SRC_ALL      0x00FDu /* everything but bit 1, which is unused */

/* Control register (270788-001 Figure 81): PRL (bits 2-0) is the source's
   priority level, stored but not arbitrated, and MSK (bit 3) gates it. Bit 4
   is the LTM bit of the INT0-INT3 registers, which asks for level-triggered
   operation -- the mode this model implements, the board driving a level
   through i8018x_pcb_icu_int(); the SFNM and C bits above it (INT0 and
   INT1 only) affect cascading and are stored but not acted on. */
#define I8018X_ICU_CON_MSK      0x0008u
#define I8018X_ICU_CON_PRL      0x0007u

/* EOI register: bit 15 asks for a non-specific acknowledgement, the low
   five bits name an interrupt type (Table 19) for a specific one. */
#define I8018X_ICU_EOI_NSPEC    0x8000u
#define I8018X_ICU_EOI_TYPE     0x001Fu

#define I8018X_ICU_SOURCES      9

struct i8018x_pcb_t {
    uint16_t base;        /* current PCB I/O base */
    uint16_t reloc;       /* Relocation Register value */
    uint8_t  handler_set; /* whether our I/O handler is currently registered */
    uint16_t regs[I8018X_PCB_SIZE / 2];
    uint64_t tmr_base[3];   /* tsc when the timer last (re)started its count cycle */
    uint64_t tmr_src[3];    /* clock-source events already counted at that moment */
    uint64_t tmr_done[3];   /* maximum-count cycles completed so far */
    uint16_t tmr_req;       /* sticky timer request latches: bit i = timer i */
    uint16_t icu_ext;       /* external input levels, INT0-INT3 in bits 4-7 */
    uint16_t icu_isr;       /* in-service register bits */
    uint8_t  tmr_alt[3];    /* MAX COUNT register in use: 0 = A, 1 = B (ALT) */
    int      dma_ch[2];       /* 86Box DMA channel the on-chip channel drives, -1 = unused */
    uint8_t  dma_drq[2];      /* request pin state, recorded only */
    uint8_t  dma_adv_pend[2]; /* pointer step owed to the read_only/advance pair */
};

static uint16_t i8018x_pcb_inw(uint16_t port, void *priv);
static void     i8018x_pcb_outw(uint16_t port, uint16_t val, void *priv);

/* The three timers count at one fourth of the CPU clock (270788-001 section
   6.2). 86Box advances tsc three times per CPU cycle here: pit.c:1868 sets
   xt_cpu_multi = 3, pit.c:1893-1894 clocks this 12 MHz part at 36 MHz, and
   808x.c:218 applies it as tsc += diff * (xt_cpu_multi >> 32). One CPU/4
   event is therefore 12 tsc. */
#define I8018X_TMR_TSC_PER_EVENT   12u

/* Maximum count of the register currently selected; ALT alternates between
   MAX COUNT register A and B on timers 0 and 1. */
static uint64_t
i8018x_pcb_tmr_max(struct i8018x_pcb_t *dev, int i)
{
    uint16_t reg = (dev->tmr_alt[i] && (i != 2)) ? I8018X_TMR_MAXB(i) : I8018X_TMR_MAXA(i);

    uint64_t m = (uint64_t) dev->regs[reg / 2];

    /* A maximum count register of 0 means 65536, not 1. Treating it as 1 makes
       a prescaled timer see a huge jump in its source-event count the moment
       the MAX register is written, latching a spurious request (the ROM's
       timer test treats an early request as a failure). */
    return m ? (m + 1u) : 0x10000u;
}

/* Clock events counted by a timer since it was last started. Timer 2 always
   runs from the internal clock; timers 0 and 1 run from timer 2's
   maximum-count output when the Prescaler bit (bit 3) is set. */
static uint64_t
i8018x_pcb_tmr_events(struct i8018x_pcb_t *dev, int i)
{
    uint64_t src;

    if ((i != 2) && (dev->regs[I8018X_TMR_CON(i) / 2] & 0x0008u))
        src = i8018x_pcb_tmr_events(dev, 2) / i8018x_pcb_tmr_max(dev, 2);
    else
        src = (tsc - dev->tmr_base[i]) / I8018X_TMR_TSC_PER_EVENT;

    return src - dev->tmr_src[i];
}

/* (Re)start a timer's count cycle from the value in its count register. */
static void
i8018x_pcb_tmr_restart(struct i8018x_pcb_t *dev, int i)
{
    dev->tmr_base[i] = tsc;
    dev->tmr_src[i]  = ((i != 2) && (dev->regs[I8018X_TMR_CON(i) / 2] & 0x0008u))
                           ? (i8018x_pcb_tmr_events(dev, 2) / i8018x_pcb_tmr_max(dev, 2))
                           : 0u;
    dev->tmr_done[i] = 0;
    dev->tmr_alt[i]  = 0;
}

/* Advance the timers to the current tsc. A timer that reaches its maximum
   count latches a request (when its interrupt is enabled) and, unless
   CONT is set, stops counting until the program enables it again. */
static void
i8018x_pcb_tmr_update(struct i8018x_pcb_t *dev)
{
    for (int i = 0; i < 3; i++) {
        uint16_t con = dev->regs[I8018X_TMR_CON(i) / 2];

        if (!(con & 0x8000u))
            continue;

        dev->tmr_alt[i] = (uint8_t) (((con & 0x0002u) && (i != 2)) ? (dev->tmr_done[i] & 1u) : 0u);

        uint64_t max = i8018x_pcb_tmr_max(dev, i);
        uint64_t cyc = i8018x_pcb_tmr_events(dev, i) / max;

        if (cyc <= dev->tmr_done[i])
            continue;

        dev->tmr_done[i] = cyc;

        if (con & 0x2000u) /* INT: the timer drives its interrupt request */
            dev->tmr_req |= (uint16_t) (1u << i);

        if (!(con & 0x0001u)) /* CONT = 0: the timer halts on maximum count */
            dev->regs[I8018X_TMR_CON(i) / 2] &= (uint16_t) ~0x8000u;
    }
}

/* A source's interrupt type (270788-001 Figure 80 / Table 19) and the bit it
   owns in the request, mask and in-service registers. The order doubles as the
   servicing order for a non-specific EOI: the manual's priority table is not
   modelled, and the guest never has two sources in service at once here. */
static const uint8_t i8018x_icu_type[I8018X_ICU_SOURCES] = {
    8,  18, 19,          /* timers 0-2 */
    10, 11,              /* DMA channels */
    12, 13, 14, 15       /* INT0-INT3 */
};

static const uint16_t i8018x_icu_src[I8018X_ICU_SOURCES] = {
    I8018X_ICU_SRC_TMR, I8018X_ICU_SRC_TMR, I8018X_ICU_SRC_TMR,
    I8018X_ICU_SRC_DMA(0), I8018X_ICU_SRC_DMA(1),
    I8018X_ICU_SRC_INT(0), I8018X_ICU_SRC_INT(1), I8018X_ICU_SRC_INT(2), I8018X_ICU_SRC_INT(3)
};

/* Control register holding a source's MSK bit; master mode merges the three
   timers into one register, so they share theirs. */
static uint16_t
i8018x_pcb_icu_con(uint16_t bit)
{
    if (bit == I8018X_ICU_SRC_TMR)
        return I8018X_ICU_TMR_CON;

    for (int i = 0; i < 2; i++) {
        if (bit == I8018X_ICU_SRC_DMA(i))
            return I8018X_ICU_DMA_CON(i);
    }

    for (int i = 0; i < 4; i++) {
        if (bit == I8018X_ICU_SRC_INT(i))
            return I8018X_ICU_INT_CON(i);
    }

    return 0x00u;
}

/* Everything the sources are asking for right now: the timer latch, the
   external input levels and whatever the guest forced through the request
   register. */
static uint16_t
i8018x_pcb_icu_requests(struct i8018x_pcb_t *dev)
{
    return (uint16_t) (dev->regs[I8018X_ICU_REQ / 2] | dev->icu_ext |
                       (dev->tmr_req ? I8018X_ICU_SRC_TMR : 0x0000u));
}

/* Interrupt type of the highest priority request that is neither masked nor
   already in service, 0 when there is none. That source moves into service so
   the guest's EOI has something to clear; delivering the interrupt itself is
   the board's business, as this controller reaches the CPU through the
   machine's own interrupt controller. */
int
i8018x_pcb_icu_acknowledge(struct i8018x_pcb_t *pcb)
{
    if (pcb == NULL)
        return 0;

    i8018x_pcb_tmr_update(pcb);

    uint16_t pending = (uint16_t) (i8018x_pcb_icu_requests(pcb) &
                                   (uint16_t) ~pcb->regs[I8018X_ICU_MASK / 2] & I8018X_ICU_SRC_ALL);

    for (int i = 0; i < I8018X_ICU_SOURCES; i++) {
        if (!(pending & i8018x_icu_src[i]) || (pcb->icu_isr & i8018x_icu_src[i]))
            continue;

        pcb->icu_isr |= i8018x_icu_src[i];

        return i8018x_icu_type[i];
    }

    return 0;
}

/* Drive one of the external interrupt inputs from the board. */
void
i8018x_pcb_icu_int(struct i8018x_pcb_t *pcb, int line, int level)
{
    if ((pcb == NULL) || (line < 0) || (line > 3))
        return;

    if (level)
        pcb->icu_ext |= I8018X_ICU_SRC_INT(line);
    else
        pcb->icu_ext &= (uint16_t) ~I8018X_ICU_SRC_INT(line);
}

/* The offsets this block decodes. io.c ANDs the value of every handler on a
   port, so an offset we do not claim has to leave the bus alone rather than
   return a stored zero, which would mask a device sharing the window. */
static int
i8018x_pcb_decoded(uint16_t off)
{
    if ((off >= I8018X_CS_BASE) && (off <= I8018X_CS_END))
        return 1;
    if ((off >= I8018X_ICU_BASE) && (off <= I8018X_ICU_END))
        return 1;
    if ((off >= I8018X_DMA0_BASE) && (off <= I8018X_DMA1_END))
        return 1;
    if ((off >= I8018X_TMR_COUNT(0)) && (off <= I8018X_TMR_END))
        return 1;

    return (off == I8018X_RELOC_OFF);
}

static uint16_t
i8018x_pcb_read_word(struct i8018x_pcb_t *dev, uint16_t off)
{
    if (off == I8018X_RELOC_OFF)
        return dev->reloc;

    /* The count registers free-run while their timer is enabled (270788-001
       section 6.1: the count resets to zero when it equals the maximum). */
    i8018x_pcb_tmr_update(dev);

    for (int i = 0; i < 3; i++) {
        if (off == I8018X_TMR_COUNT(i)) {
            if (dev->regs[I8018X_TMR_CON(i) / 2] & 0x8000u)
                return (uint16_t) (i8018x_pcb_tmr_events(dev, i) % i8018x_pcb_tmr_max(dev, i));
            return dev->regs[off / 2];
        }
    }

    /* The request register shows every source that is asking, the in-service
       register the ones being serviced, and the status register holds the
       three timers individually in bits 0-2. */
    if (off == I8018X_ICU_REQ)
        return i8018x_pcb_icu_requests(dev);
    if (off == I8018X_ICU_INSERVICE)
        return dev->icu_isr;
    if (off == I8018X_ICU_STATUS)
        return (uint16_t) (dev->regs[off / 2] | (dev->tmr_req & 0x0007u));

    /* CHG/NOCHG is write only (270788-001 section 8.2). */
    if ((off == I8018X_DMA_CTRL(0)) || (off == I8018X_DMA_CTRL(1)))
        return (uint16_t) (dev->regs[off / 2] & ~I8018X_DMA_CHG);

    return i8018x_pcb_decoded(off) ? dev->regs[off / 2] : 0xFFFF;
}

static void
i8018x_pcb_write_word(struct i8018x_pcb_t *dev, uint16_t off, uint16_t val)
{
    if (off == I8018X_RELOC_OFF) {
        /* Manual 270788-001 section 5.1 / Figure 53: bits 11:0 are address
           bits A19:A8 of the new base, bit 12 selects memory (1) or I/O (0)
           space, bits 15:13 carry the ESC-trap and master/slave controls. */
        uint16_t new_base = (uint16_t) ((val & 0x0FFFu) << 8);

        dev->reloc        = val;
        dev->regs[off / 2] = val;

        /* Only the I/O-space PCB is modelled; a memory-space PCB is stored
           but left where it is. */
        if (!(val & 0x1000u) && (new_base != dev->base)) {
            io_removehandler(dev->base, I8018X_PCB_SIZE, NULL, i8018x_pcb_inw, NULL, NULL, i8018x_pcb_outw, NULL, dev);
            dev->base = new_base;
            io_sethandler(dev->base, I8018X_PCB_SIZE, NULL, i8018x_pcb_inw, NULL, NULL, i8018x_pcb_outw, NULL, dev);
        }
        return;
    }

    /* CHG/NOCHG gates the ST/STOP bit: a control word write that leaves it
       clear keeps the channel running as it was (270788-001 section 8.2). */
    if (((off == I8018X_DMA_CTRL(0)) || (off == I8018X_DMA_CTRL(1))) && !(val & I8018X_DMA_CHG))
        val = (uint16_t) ((val & ~I8018X_DMA_ST) | (dev->regs[off / 2] & I8018X_DMA_ST));

    dev->regs[off / 2] = val;

    /* A control or count-register write (re)starts the count cycle; the request
       latch itself is only touched through the interrupt controller registers. */
    for (int i = 0; i < 3; i++) {
        if ((off == I8018X_TMR_CON(i)) || (off == I8018X_TMR_COUNT(i)) ||
            (off == I8018X_TMR_MAXA(i)) || (off == I8018X_TMR_MAXB(i)))
            i8018x_pcb_tmr_restart(dev, i);
    }

    /* Writing the status register sets or clears the individual timer request
       bits (270788-001 Figure 26) -- the ROM forces a request with 0x8001 and
       clears them again with 0x8000. */
    if (off == I8018X_ICU_STATUS) {
        dev->regs[off / 2] = (uint16_t) (val & ~0x0007u);
        dev->tmr_req       = (uint16_t) ((dev->tmr_req & ~0x0007u) | (val & 0x0007u));
    }

    /* The in-service register only holds source bits. */
    if (off == I8018X_ICU_INSERVICE) {
        dev->regs[off / 2] = 0x0000u;
        dev->icu_isr       = (uint16_t) (val & I8018X_ICU_SRC_ALL);
    }

    /* An EOI clears the source it names: bit 15 acknowledges the source in
       service with the highest priority, otherwise bits 4-0 carry its interrupt
       type (270788-001 Figure 87, Table 19). The register itself is write only
       (section 9.5.2.9), so a read hands back whatever the last write left
       here, which the manual does not define. */
    if (off == I8018X_ICU_EOI) {
        uint16_t clear = 0x0000u;

        if (val & I8018X_ICU_EOI_NSPEC) {
            for (int i = 0; i < I8018X_ICU_SOURCES; i++) {
                if (dev->icu_isr & i8018x_icu_src[i]) {
                    clear = i8018x_icu_src[i];
                    break;
                }
            }
        } else {
            for (int i = 0; i < I8018X_ICU_SOURCES; i++) {
                if (i8018x_icu_type[i] == (val & I8018X_ICU_EOI_TYPE)) {
                    clear = i8018x_icu_src[i];
                    break;
                }
            }
        }

        dev->icu_isr &= (uint16_t) ~clear;
    }

    /* A source's mask bit sits in its control register as well as in the mask
       register, and a write to either one updates the other (270788-001 section
       9.5.2.5). The three timers share one control register, so their three
       entries here write the same bit with the same value. */
    if (off == I8018X_ICU_MASK) {
        for (int i = 0; i < I8018X_ICU_SOURCES; i++) {
            uint16_t con = i8018x_pcb_icu_con(i8018x_icu_src[i]);

            if (con == 0x00u)
                continue;

            if (val & i8018x_icu_src[i])
                dev->regs[con / 2] |= I8018X_ICU_CON_MSK;
            else
                dev->regs[con / 2] &= (uint16_t) ~I8018X_ICU_CON_MSK;
        }
    } else if ((off >= I8018X_ICU_TMR_CON) && (off <= I8018X_ICU_INT_CON(3))) {
        for (int i = 0; i < I8018X_ICU_SOURCES; i++) {
            if (i8018x_pcb_icu_con(i8018x_icu_src[i]) != off)
                continue;

            if (val & I8018X_ICU_CON_MSK)
                dev->regs[I8018X_ICU_MASK / 2] |= i8018x_icu_src[i];
            else
                dev->regs[I8018X_ICU_MASK / 2] &= (uint16_t) ~i8018x_icu_src[i];
        }
    }

    /* Writing a zero to the request register's timer bit clears it; the ROM
       does `in 0x2E / and 0xFFFE / out 0x2E` before its timer test. */
    if ((off == I8018X_ICU_REQ) && !(val & 0x0001u))
        dev->tmr_req = 0;
}

static uint16_t
i8018x_pcb_inw(uint16_t port, void *priv)
{
    struct i8018x_pcb_t *dev = (struct i8018x_pcb_t *) priv;
    uint16_t  off = (uint16_t) ((port - dev->base) & 0xFEu);

    return i8018x_pcb_read_word(dev, off);
}

static void
i8018x_pcb_outw(uint16_t port, uint16_t val, void *priv)
{
    i8018x_pcb_write_word((struct i8018x_pcb_t *) priv, (uint16_t) ((port - ((struct i8018x_pcb_t *) priv)->base) & 0xFEu), val);
}

/* The on-chip channels reach the rest of the emulator through the controller
   interface in dma.h, so a board only has to route its devices to the same
   channel numbers it hands over here. A transfer is fetch + deposit, but the
   slave on the other side performs the half it owns: dma_channel_write() is a
   deposit from the slave into memory, dma_channel_read() a fetch out of it. */

/* PCB channel driving an 86Box DMA channel, -1 when none does. */
static int
i8018x_pcb_dma_slot(struct i8018x_pcb_t *dev, int channel)
{
    for (int i = 0; i < 2; i++) {
        if ((dev->dma_ch[i] >= 0) && (dev->dma_ch[i] == channel))
            return i;
    }

    return -1;
}

/* A channel transfers only while ST/STOP is set. */
static int
i8018x_pcb_dma_armed(struct i8018x_pcb_t *dev, int slot)
{
    return !!(dev->regs[I8018X_DMA_CTRL(slot)] & I8018X_DMA_ST);
}

/* 20-bit pointer the channel transfers through. */
static uint32_t
i8018x_pcb_dma_ptr(struct i8018x_pcb_t *dev, int slot, int dst)
{
    uint16_t lo = (uint16_t) (dst ? I8018X_DMA_DST_LO(slot) : I8018X_DMA_SRC_LO(slot));
    uint16_t hi = (uint16_t) (dst ? I8018X_DMA_DST_HI(slot) : I8018X_DMA_SRC_HI(slot));

    return ((uint32_t) dev->regs[lo]) | ((uint32_t) dev->regs[hi] << 16);
}

/* Move one pointer on as its INC/DEC bits ask; all 20 bits are affected. The
   manual's third state, leaving the pointer alone, is what the ROM asks for
   with both bits set, to keep a channel on the FDC's port (section 8.2). */
static void
i8018x_pcb_dma_step_ptr(struct i8018x_pcb_t *dev, int slot, int dst)
{
    uint16_t ctrl = dev->regs[I8018X_DMA_CTRL(slot)];
    uint16_t inc  = dst ? I8018X_DMA_DST_INC : I8018X_DMA_SRC_INC;
    uint16_t dec  = dst ? I8018X_DMA_DST_DEC : I8018X_DMA_SRC_DEC;
    uint16_t lo   = (uint16_t) (dst ? I8018X_DMA_DST_LO(slot) : I8018X_DMA_SRC_LO(slot));
    uint16_t hi   = (uint16_t) (dst ? I8018X_DMA_DST_HI(slot) : I8018X_DMA_SRC_HI(slot));
    uint32_t ptr  = i8018x_pcb_dma_ptr(dev, slot, dst);

    if ((ctrl & inc) && !(ctrl & dec))
        ptr++;
    else if ((ctrl & dec) && !(ctrl & inc))
        ptr--;

    ptr &= 0x000FFFFFu;

    dev->regs[lo] = (uint16_t) (ptr & 0xFFFFu);
    dev->regs[hi] = (uint16_t) ((ptr >> 16) & 0x0Fu);
}

/* Drop the transfer count by one and answer whether this transfer ended the
   channel. A programmed count of zero means 65536 transfers, so the count may
   wrap; with TC clear it keeps going instead of stopping (270788-001 section
   8.2). INT + TC would interrupt the CPU, but the interrupt controller is
   still unwired, so a terminal count only reaches the slave as DMA_OVER. */
static int
i8018x_pcb_dma_count_down(struct i8018x_pcb_t *dev, int slot)
{
    dev->regs[I8018X_DMA_COUNT(slot)]--;

    if ((dev->regs[I8018X_DMA_COUNT(slot)] != 0) ||
        !(dev->regs[I8018X_DMA_CTRL(slot)] & I8018X_DMA_TC))
        return 0;

    dev->regs[I8018X_DMA_CTRL(slot)] &= (uint16_t) ~I8018X_DMA_ST;

    return 1;
}

/* The 80186 has no transfer mode register, so a channel that is ours is always
   able to move data in either direction; this answers both the readable and the
   writable query. */
static int
i8018x_pcb_dma_readable(int channel, void *priv)
{
    return i8018x_pcb_dma_slot((struct i8018x_pcb_t *) priv, channel) >= 0;
}

static int
i8018x_pcb_dma_read_only(int channel, void *priv)
{
    struct i8018x_pcb_t *dev  = (struct i8018x_pcb_t *) priv;
    int           slot = i8018x_pcb_dma_slot(dev, channel);

    if ((slot < 0) || !i8018x_pcb_dma_armed(dev, slot))
        return DMA_NODATA;

    dev->dma_adv_pend[slot] = 1;

    return mem_readb_phys(i8018x_pcb_dma_ptr(dev, slot, 0));
}

static int
i8018x_pcb_dma_advance(int channel, void *priv)
{
    struct i8018x_pcb_t *dev  = (struct i8018x_pcb_t *) priv;
    int           slot = i8018x_pcb_dma_slot(dev, channel);

    if ((slot < 0) || !dev->dma_adv_pend[slot])
        return 0;

    dev->dma_adv_pend[slot] = 0;
    i8018x_pcb_dma_step_ptr(dev, slot, 0);

    return i8018x_pcb_dma_count_down(dev, slot) ? DMA_OVER : 0;
}

static int
i8018x_pcb_dma_read(int channel, void *priv)
{
    struct i8018x_pcb_t *dev  = (struct i8018x_pcb_t *) priv;
    int           slot = i8018x_pcb_dma_slot(dev, channel);
    int           ret;

    if ((slot < 0) || !i8018x_pcb_dma_armed(dev, slot))
        return DMA_NODATA;

    ret = mem_readb_phys(i8018x_pcb_dma_ptr(dev, slot, 0));
    i8018x_pcb_dma_step_ptr(dev, slot, 0);

    return ret | (i8018x_pcb_dma_count_down(dev, slot) ? DMA_OVER : 0);
}

static int
i8018x_pcb_dma_write(int channel, uint16_t val, void *priv)
{
    struct i8018x_pcb_t *dev  = (struct i8018x_pcb_t *) priv;
    int           slot = i8018x_pcb_dma_slot(dev, channel);

    if ((slot < 0) || !i8018x_pcb_dma_armed(dev, slot))
        return DMA_NODATA;

    mem_writeb_phys(i8018x_pcb_dma_ptr(dev, slot, 1), (uint8_t) val);
    i8018x_pcb_dma_step_ptr(dev, slot, 1);

    return i8018x_pcb_dma_count_down(dev, slot) ? DMA_OVER : 0;
}

static void
i8018x_pcb_dma_set_drq(int channel, int set, void *priv)
{
    struct i8018x_pcb_t *dev  = (struct i8018x_pcb_t *) priv;
    int           slot = i8018x_pcb_dma_slot(dev, channel);

    if (slot >= 0)
        dev->dma_drq[slot] = !!set;
}

static int
i8018x_pcb_dma_get_drq(int channel, void *priv)
{
    struct i8018x_pcb_t *dev  = (struct i8018x_pcb_t *) priv;
    int           slot = i8018x_pcb_dma_slot(dev, channel);

    return (slot >= 0) ? dev->dma_drq[slot] : 0;
}

/* The 80186 has no verify mode, so report the direction the descriptor moves
   memory: the slave's verify check looks for the all-zero mode. */
static int
i8018x_pcb_dma_mode(int channel, void *priv)
{
    struct i8018x_pcb_t *dev  = (struct i8018x_pcb_t *) priv;
    int           slot = i8018x_pcb_dma_slot(dev, channel);

    if (slot < 0)
        return 0x00;

    /* Destination in memory: the byte is deposited, otherwise it is fetched. */
    return (dev->regs[I8018X_DMA_CTRL(slot)] & I8018X_DMA_DST_MIO) ? 0x04 : 0x08;
}

/* set_service_handler stays out: with no request scheduling there is nothing
   that could ever call the handler back. */
static const dma_ctrl_t i8018x_pcb_dma_ops = {
    .channel_readable    = i8018x_pcb_dma_readable,
    .channel_writable    = i8018x_pcb_dma_readable,
    .channel_read_only   = i8018x_pcb_dma_read_only,
    .channel_advance     = i8018x_pcb_dma_advance,
    .channel_read        = i8018x_pcb_dma_read,
    .channel_write       = i8018x_pcb_dma_write,
    .set_service_handler = NULL,
    .set_drq             = i8018x_pcb_dma_set_drq,
    .get_drq             = i8018x_pcb_dma_get_drq,
    .mode                = i8018x_pcb_dma_mode
};

/* The board wires the two on-chip channels onto 86Box channel numbers (-1
   leaves one unused) and calls this once, after the device exists and after
   the machine's dma_init(), which drops every channel owner first. */
void
i8018x_pcb_set_dma_channels(struct i8018x_pcb_t *dev, int dma0_ch, int dma1_ch)
{
    if (dev == NULL)
        return;

    dma_set_controller(NULL, NULL);

    dev->dma_ch[0] = dma0_ch;
    dev->dma_ch[1] = dma1_ch;

    /* An 8018x board has no separate 8237, so these are the machine's only DMA
       channels and the controller answers for the whole space. */
    dma_set_controller(&i8018x_pcb_dma_ops, dev);
}

static void
i8018x_pcb_reset(void *priv)
{
    struct i8018x_pcb_t *dev = (struct i8018x_pcb_t *) priv;

    if (dev->handler_set)
        io_removehandler(dev->base, I8018X_PCB_SIZE, NULL, i8018x_pcb_inw, NULL, NULL, i8018x_pcb_outw, NULL, dev);

    memset(dev->regs, 0x00, sizeof(dev->regs));
    dev->tmr_req = 0;
    dev->icu_ext = 0;
    dev->icu_isr = 0;

    /* The 86Box channel numbers survive a reset: the controller keeps owning
       them until the device is closed. */
    memset(dev->dma_drq, 0x00, sizeof(dev->dma_drq));
    memset(dev->dma_adv_pend, 0x00, sizeof(dev->dma_adv_pend));

    for (int i = 0; i < 3; i++) {
        dev->tmr_base[i] = 0;
        dev->tmr_src[i]  = 0;
        dev->tmr_done[i] = 0;
        dev->tmr_alt[i]  = 0;
    }

    /* Reset values from 270788-001 section 5.3. */
    dev->regs[I8018X_CS_BASE / 2]   = I8018X_UMCS_RESET; /* UMCS */
    dev->reloc                      = I8018X_RELOC_RESET;
    dev->regs[I8018X_RELOC_OFF / 2] = dev->reloc;
    dev->base                       = I8018X_PCB_BASE_DEFAULT;

    /* Interrupt controller reset values (270788-001 section 5.3):
       every source comes up masked, at priority level 111, and the priority
       mask has all three of its bits set, which leaves no level masked. */
    for (uint16_t off = I8018X_ICU_TMR_CON; off <= I8018X_ICU_INT_CON(3); off += 2)
        dev->regs[off / 2] = I8018X_ICU_CON_PRL | I8018X_ICU_CON_MSK;

    dev->regs[I8018X_ICU_MASK / 2]      = I8018X_ICU_SRC_ALL;
    dev->regs[I8018X_ICU_PRIO_MASK / 2] = 0x0007u;

    io_sethandler(dev->base, I8018X_PCB_SIZE, NULL, i8018x_pcb_inw, NULL, NULL, i8018x_pcb_outw, NULL, dev);
    dev->handler_set = 1;
}

static void
i8018x_pcb_close(void *priv)
{
    struct i8018x_pcb_t *dev = (struct i8018x_pcb_t *) priv;

    if (dev->handler_set)
        io_removehandler(dev->base, I8018X_PCB_SIZE, NULL, i8018x_pcb_inw, NULL, NULL, i8018x_pcb_outw, NULL, dev);

    dma_set_controller(NULL, NULL);

    free(dev);
}

static void *
i8018x_pcb_init(const device_t *info)
{
    struct i8018x_pcb_t *dev = (struct i8018x_pcb_t *) calloc(1, sizeof(struct i8018x_pcb_t));

    /* The board wires the channels up after this and a reset must not drop
       them, so they only start unclaimed here. */
    dev->dma_ch[0] = dev->dma_ch[1] = -1;

    i8018x_pcb_reset(dev);

    return dev;
}

const device_t i8018x_pcb_device = {
    .name          = "Intel 8018x Integrated Peripheral Control Block",
    .internal_name = "i8018x_pcb",
    .flags         = 0,
    .local         = 0,
    .init          = i8018x_pcb_init,
    .close         = i8018x_pcb_close,
    .reset         = i8018x_pcb_reset,
    .available     = NULL,
    .speed_changed = NULL,
    .force_redraw  = NULL,
    .config        = NULL
};
