/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          Implementation of the Intel 386EX DMA Controller.
 *
 *          Two channels, each with a 26-bit requester address, a 26-bit
 *          target address and a 24-bit byte count, programmed through 8-bit
 *          registers in the expanded I/O space; the 8237A-compatible ones
 *          also appear at their PC/AT addresses in the DOS I/O space while
 *          REMAPCFG's DR bit is clear (Intel386 EX Embedded Microprocessor
 *          User's Manual, order number 272485, chapter 12).
 *
 *          A channel writes the base and current copies of its addresses
 *          and byte count together, as an 8237A does, except while it is
 *          chaining: then they go to the base copy only, which writing the
 *          target address's top byte marks as loaded, and the channel moves
 *          on to it when its current buffer ends. Autoinitialize reloads
 *          the current copy from the base copy instead. A buffer ends when
 *          its byte count goes past zero (byte count + 1 bytes move).
 *
 *          The chip's channels are the machine's only DMA channels, as a
 *          board with a 386EX inits without the AT's 8237As
 *          (MACHINE_NONSTANDARD_DMA): its
 *          DRQ0 and DRQ1 pins are wired to whichever 86Box channels the
 *          board passes to i386ex_set_dma_channels(), and a device on one
 *          of them transfers through the target address in the direction
 *          the channel is programmed for, as the requester. Software
 *          requests run the channel itself, moving data between the
 *          requester and target addresses, in memory or I/O, in two-cycle
 *          transfers, which allows memory-to-memory transfers. In fly-by
 *          ones nothing moves, as no device drives or takes the data: on a
 *          TS-3100, a fly-by software request writing memory left whatever
 *          was on the bus there. The channel counts and steps its
 *          addresses all the same.
 *
 *          DMACFG can instead connect a channel's request input to one of
 *          the chip's own units: an SIO's transmit buffer empty or receive
 *          buffer full signal, which i386ex_device drives from the UARTs'
 *          line status, a timer output, or one of the SSIO's, which no unit
 *          drives yet. The channel runs those requests itself too, as
 *          two-cycle transfers between the requester and target addresses:
 *          DMAACK# doesn't reach the units (section 5.2.1.1), so a fly-by
 *          transfer moves nothing. The request is looked at again before
 *          every transfer, as the last one can have taken it away: reading
 *          a UART's receive buffer takes RBF down. The manual has a timer
 *          output start a transfer with a pulse, and suggests its periodic
 *          modes for it (section 5.2.1.3), where OUT is high all but one
 *          clock in mode 2 (and 86Box's fast timer has that clock take no
 *          time at all), so a timer output's rising edge makes one request.
 *          It is held in a flip-flop on the path from the timer (Intel386 EX
 *          specification update, erratum 5) until a channel takes it: on a
 *          TS-3100, an edge that came while the channel was masked showed in
 *          DMASTS and was transferred as soon as the channel was unmasked.
 *          The flip-flop is outside the DMA unit's registers, so programming
 *          them, DMACFG included, leaves it as it is.
 *
 *          A buffer transfer's end sets its channel's TC bits in DMASTS and,
 *          while DMAIEN asks, DMAIS, and raises DMAINT. Writing DMACLRTC
 *          takes DMAINT down but, as on a TS-3100, leaves DMAIS's TC bit
 *          set; reading DMASTS clears both.
 *
 *          Not emulated: the EOP# input; cascade mode; bus priority
 *          arbitration (DMACMD1's PRE and DMACMD2 are stored only); DREQ
 *          and EOP# sampling; the bus being held for a whole block
 *          transfer, as the channels give the CPU a turn between batches
 *          of bus cycles; and the exact bus cycles of the 4-byte temporary
 *          buffer, so a two-cycle transfer between different bus widths
 *          moves bytes, and every request takes its data through to the
 *          target, where a read transfer would gather up to four bytes in
 *          the buffer first.
 *
 * Authors: Nathan Lewis, <git@nrlewis.dev>
 *
 *          Copyright 2026 Nathan Lewis.
 */
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>
#define HAVE_STDARG_H
#include <86box/86box.h>
#include "cpu.h"
#include <86box/device.h>
#include <86box/io.h>
#include <86box/mem.h>
#include <86box/pic.h>
#include <86box/timer.h>
#include <86box/dma.h>
#include <86box/i386ex.h>
#include <86box/plat_unused.h>

/* Register addresses in the expanded I/O space (Table 12-3); the DOS I/O
   space has the 8237A-compatible ones at the same low address bits. */
#define DMA0TAR      0xf000 /* TAR1:0, by BP */
#define DMA0BYC      0xf001 /* BYC1:0, by BP */
#define DMA1TAR      0xf002
#define DMA1BYC      0xf003
#define DMACMD1      0xf008 /* write; DMASTS on read */
#define DMASRR       0xf009
#define DMAMSK       0xf00a
#define DMAMOD1      0xf00b
#define DMACLRBP     0xf00c
#define DMACLR       0xf00d
#define DMACLRMSK    0xf00e
#define DMAGRPMSK    0xf00f
#define DMA0REQL     0xf010 /* REQ1:0, by BP */
#define DMA0REQH     0xf011 /* REQ3:2, by BP */
#define DMA1REQL     0xf012
#define DMA1REQH     0xf013
#define DMABSR       0xf018
#define DMACHR       0xf019 /* write; DMAIS on read */
#define DMACMD2      0xf01a
#define DMAMOD2      0xf01b
#define DMAIEN       0xf01c
#define DMAOVFE      0xf01d
#define DMACLRTC     0xf01e
#define DMA1TAR2     0xf083
#define DMA1TAR3     0xf085
#define DMA0TAR3     0xf086
#define DMA0TAR2     0xf087
#define DMA0BYC2     0xf098
#define DMA1BYC2     0xf099

#define DMA_DOS_MASK 0x0fff

/* DMACMD1 */
#define CMD1_CE      0x04 /* set disables both channels */
/* DMASTS */
#define STS_R(n)     (0x10 << (n))
#define STS_TC(n)    (0x01 << (n))
/* DMAIS */
#define IS_TC(n)     (0x10 << (n))
#define IS_CI(n)     (0x01 << (n))
/* The registers that pick a channel with bit 0, and set or clear bit 2. */
#define SEL_CS       0x01
#define SEL_BIT      0x04
/* DMAMOD1 */
#define MOD1_DTM     0xc0
#define MOD1_DEMAND  0x00
#define MOD1_SINGLE  0x40
#define MOD1_BLOCK   0x80
#define MOD1_CASCADE 0xc0
#define MOD1_TI      0x20 /* target address decrements */
#define MOD1_AI      0x10
#define MOD1_TD      0x0c
#define MOD1_TD_VER  0x00 /* target read, nothing written */
#define MOD1_TD_R2T  0x04 /* requester to target */
#define MOD1_TD_T2R  0x08 /* target to requester */
/* DMAMOD2 */
#define MOD2_BCO     0x80 /* two-cycle */
#define MOD2_RD      0x40 /* requester in I/O */
#define MOD2_TD      0x20 /* target in I/O */
#define MOD2_RH      0x10 /* requester address held */
#define MOD2_RI      0x08 /* requester address decrements */
#define MOD2_TH      0x04 /* target address held */
/* DMABSR */
#define BSR_RBS      0x40 /* requester 8-bit */
#define BSR_TBS      0x10 /* target 8-bit */
/* DMAOVFE */
#define OVFE_TOV(n)  (0x01 << ((n) << 1)) /* all target and byte count bits */
#define OVFE_ROV(n)  (0x02 << ((n) << 1)) /* all requester bits */
/* DMACFG */
#define CFG_REQ(n)   (0x07 << ((n) << 2)) /* 0 is the DRQn pin */
/* INTCFG */
#define INTCFG_SWAP  0x10 /* DMAINT to the slave's IR5 rather than IR4 */
#define INTCFG_IR5   0x04 /* the slave's IR5 is connected */

#define ADDR_MASK    0x03ffffff
#define BYC_MASK     0x00ffffff

/* Bus cycles the channels run in one go, before letting the CPU run. */
#define BATCH        16

/* The on-chip signal each DMACFG request connection picks, for each channel; -1 is the DRQn pin (Figure 5-3). */
static const int8_t dma_source[2][8] = {
    { -1, I386EX_DMA_RBF0, I386EX_DMA_TXE1, I386EX_DMA_SSTBE,
      I386EX_DMA_OUT1, I386EX_DMA_RBF1, I386EX_DMA_TXE0, I386EX_DMA_SSRBF },
    { -1, I386EX_DMA_RBF1, I386EX_DMA_TXE0, I386EX_DMA_SSRBF,
      I386EX_DMA_OUT2, I386EX_DMA_RBF0, I386EX_DMA_TXE1, I386EX_DMA_SSTBE }
};

typedef struct i386ex_dma_chan_t {
    uint32_t req; /* Current requester address. */
    uint32_t tar; /* Current target address. */
    uint32_t byc; /* Current byte count. */
    uint32_t req_base;
    uint32_t tar_base;
    uint32_t byc_base;
    uint8_t  base_loaded; /* Chaining: the base copy holds the next buffer. */

    uint8_t mod1;
    uint8_t mod2;
    uint8_t bsr;
    uint8_t chain; /* DMACHR's CE. */
    uint8_t mask;  /* Hardware requests masked. */
    uint8_t sreq;  /* A software request is pending. */
    uint8_t drq;   /* The DRQ pin, as the device on it drives it. */
    uint8_t burst; /* A block transfer a hardware request started, running to the buffer's end. */
    uint8_t adv;   /* A read_only fetch is owed its advance. */
    int     dma_ch; /* The 86Box channel the DRQ pin is wired to, -1 for none. */
} i386ex_dma_chan_t;

struct i386ex_dma_t {
    i386ex_dma_chan_t ch[2];

    uint8_t bp; /* Byte pointer flip-flop. */
    uint8_t cmd1;
    uint8_t cmd2;
    uint8_t sts;    /* TC bits; the R bits come from the pins. */
    uint8_t is;     /* DMAIS. */
    uint8_t tc_int; /* The transfer complete signals DMAINT sees, by channel; DMACLRTC clears them. */
    uint8_t ien;
    uint8_t ovfe;
    uint8_t cfg;    /* DMACFG, which the chip's configuration registers keep. */
    uint8_t intcfg; /* INTCFG, for where DMAINT goes. */
    int     irq;    /* The IRQ DMAINT is raised on, 0 for none. */

    uint8_t src[I386EX_DMA_SOURCES]; /* The on-chip request signals, as their units drive them. */
    uint8_t edge;                    /* Timer requests held in their flip-flops, by source bit. */
    uint8_t running;                 /* Inside i386ex_dma_run(), which reschedules itself. */

    pc_timer_t timer;
};

#ifdef ENABLE_I386EX_DMA_LOG
int i386ex_dma_do_log = ENABLE_I386EX_DMA_LOG;

static void
i386ex_dma_log(const char *fmt, ...)
{
    va_list ap;

    if (i386ex_dma_do_log) {
        va_start(ap, fmt);
        pclog_ex(fmt, ap);
        va_end(ap);
    }
}
#else
#    define i386ex_dma_log(fmt, ...)
#endif

/* ---- DMAINT ---------------------------------------------------------------------------------- */

/* The slave's IR4 (IRQ 12), or with INTCFG's SWAP its IR5 (IRQ 13) if that is connected (Table 5-2). */
static int
i386ex_dma_int_irq(const i386ex_dma_t *dev)
{
    if (!(dev->intcfg & INTCFG_SWAP))
        return 12;

    return (dev->intcfg & INTCFG_IR5) ? 13 : 0;
}

/*
   DMAINT is the OR of each channel's transfer complete signal, while its
   DMAIEN bit is set and it isn't chaining, and its chaining signal, while
   it is (section 12.2.9). DMACLRTC clears the transfer complete signals
   but not DMAIS's TC bits.
 */
static void
i386ex_dma_int_update(i386ex_dma_t *dev)
{
    int active = 0;
    int irq;

    for (int n = 0; n < 2; n++) {
        if (dev->ch[n].chain)
            active |= !!(dev->is & IS_CI(n));
        else
            active |= !!(dev->tc_int & (1 << n));
    }

    /* DMAINT holds its IR up while active; a change of IR takes it down on the old one. */
    irq = active ? i386ex_dma_int_irq(dev) : 0;

    if (dev->irq && (dev->irq != irq))
        picintc(1 << dev->irq);
    if (irq && (irq != dev->irq))
        picint(1 << irq);

    dev->irq = irq;
}

/* ---- Addresses and counts -------------------------------------------------------------------- */

/* Moves an address by step, over all 26 bits or only the low 16 as DMAOVFE says. */
static uint32_t
i386ex_dma_step_addr(uint32_t addr, int step, int full)
{
    uint32_t next = (addr + (uint32_t) step) & ADDR_MASK;

    if (!full)
        next = (addr & ~0xffff) | (next & 0xffff);

    return next;
}

/* Takes size off the byte count, over 24 bits or only the low 16; returns whether it expired. */
static int
i386ex_dma_count_down(i386ex_dma_t *dev, int n, int size)
{
    i386ex_dma_chan_t *ch   = &dev->ch[n];
    int                full = !!(dev->ovfe & OVFE_TOV(n));
    uint32_t           mask = full ? BYC_MASK : 0xffff;
    uint32_t           low  = ch->byc & mask;
    int                expired;

    /* The count expires when it goes past zero: it stood at fewer bytes than this moves. */
    expired = (low < (uint32_t) size);
    low     = (low - (uint32_t) size) & mask;
    ch->byc = (ch->byc & ~mask) | low;

    return expired;
}

/*
   The width of one bus cycle on a side, from DMABSR. The channel can't
   decrement by words (section 12.5), so a side that decrements moves bytes.
 */
static int
i386ex_dma_width(const i386ex_dma_chan_t *ch, int requester)
{
    int eight = requester ? !!(ch->bsr & BSR_RBS) : !!(ch->bsr & BSR_TBS);
    int dec   = requester ? ((ch->mod2 & (MOD2_RH | MOD2_RI)) == MOD2_RI)
                          : (!(ch->mod2 & MOD2_TH) && (ch->mod1 & MOD1_TI));

    return (eight || dec) ? 1 : 2;
}

static void
i386ex_dma_step_target(i386ex_dma_t *dev, int n, int size)
{
    i386ex_dma_chan_t *ch = &dev->ch[n];

    if (ch->mod2 & MOD2_TH)
        return;

    ch->tar = i386ex_dma_step_addr(ch->tar, (ch->mod1 & MOD1_TI) ? -size : size, !!(dev->ovfe & OVFE_TOV(n)));
}

static void
i386ex_dma_step_requester(i386ex_dma_t *dev, int n, int size)
{
    i386ex_dma_chan_t *ch = &dev->ch[n];

    if (ch->mod2 & MOD2_RH)
        return;

    ch->req = i386ex_dma_step_addr(ch->req, (ch->mod2 & MOD2_RI) ? -size : size, !!(dev->ovfe & OVFE_ROV(n)));
}

/* ---- Buffer transfers ------------------------------------------------------------------------ */

/*
   A buffer transfer has ended: the byte count expired. Autoinitialize
   reloads the channel from its base copy; chaining moves on to the base
   copy if software loaded one in time, and asks for the next one; otherwise
   the channel goes idle, masking itself (section 12.3.11).
 */
static void
i386ex_dma_buffer_end(i386ex_dma_t *dev, int n)
{
    i386ex_dma_chan_t *ch = &dev->ch[n];

    i386ex_dma_log("386EX DMA: channel %i buffer transfer complete\n", n);

    dev->sts |= STS_TC(n);
    if ((dev->ien & (1 << n)) && !ch->chain) {
        dev->is |= IS_TC(n);
        dev->tc_int |= 1 << n;
    }

    ch->sreq  = 0;
    ch->burst = 0;

    if (ch->mod1 & MOD1_AI) {
        ch->req = ch->req_base;
        ch->tar = ch->tar_base;
        ch->byc = ch->byc_base;
    } else if (ch->chain && ch->base_loaded) {
        ch->req         = ch->req_base;
        ch->tar         = ch->tar_base;
        ch->byc         = ch->byc_base;
        ch->base_loaded = 0;
        dev->is |= IS_CI(n);
    } else
        ch->mask = 1;

    i386ex_dma_int_update(dev);
}

/* The channel can run: both channels enabled, and not in cascade mode, which isn't emulated. */
static int
i386ex_dma_enabled(const i386ex_dma_t *dev, int n)
{
    return !(dev->cmd1 & CMD1_CE) && ((dev->ch[n].mod1 & MOD1_DTM) != MOD1_CASCADE);
}

/* A device on the DRQ pin can be served: the pin is the channel's request source, and isn't masked. */
static int
i386ex_dma_pin_served(const i386ex_dma_t *dev, int n)
{
    return i386ex_dma_enabled(dev, n) && !(dev->cfg & CFG_REQ(n)) && !dev->ch[n].mask;
}

/* The on-chip signal DMACFG connects to the channel's request input, -1 for the DRQn pin. */
static int
i386ex_dma_source(const i386ex_dma_t *dev, int n)
{
    return dma_source[n][(dev->cfg >> (n << 2)) & 0x07];
}

static int
i386ex_dma_source_edge(int src)
{
    return (src == I386EX_DMA_OUT1) || (src == I386EX_DMA_OUT2);
}

/* The channel's request input, DREQn, which DMASTS reports whether the channel is masked or not. */
static int
i386ex_dma_dreq(const i386ex_dma_t *dev, int n)
{
    int src = i386ex_dma_source(dev, n);

    if (src < 0)
        return dev->ch[n].drq;

    return i386ex_dma_source_edge(src) ? !!(dev->edge & (1 << src)) : dev->src[src];
}

/* The channel has a hardware request of its own to run: from an on-chip unit, and not masked. */
static int
i386ex_dma_hw_pending(const i386ex_dma_t *dev, int n)
{
    const i386ex_dma_chan_t *ch = &dev->ch[n];

    return (i386ex_dma_source(dev, n) >= 0) && !ch->mask && (ch->burst || i386ex_dma_dreq(dev, n));
}

/* The channel has a transfer to run itself, for a software request or an on-chip unit. */
static int
i386ex_dma_pending(const i386ex_dma_t *dev, int n)
{
    return i386ex_dma_enabled(dev, n) && (dev->ch[n].sreq || i386ex_dma_hw_pending(dev, n));
}

static int
i386ex_dma_space_read(uint32_t addr, int io, int size)
{
    if (io)
        return (size == 2) ? inw(addr & 0xffff) : inb(addr & 0xffff);

    return (size == 2) ? mem_readw_phys(addr) : mem_readb_phys(addr);
}

static void
i386ex_dma_space_write(uint32_t addr, int io, int size, uint16_t val)
{
    if (io) {
        if (size == 2)
            outw(addr & 0xffff, val);
        else
            outb(addr & 0xffff, val & 0xff);
    } else if (size == 2)
        mem_writew_phys(addr, val);
    else
        mem_writeb_phys(addr, val & 0xff);
}

/* The target is only in I/O for a two-cycle transfer; a fly-by one always has it in memory. */
static int
i386ex_dma_target_io(const i386ex_dma_chan_t *ch)
{
    return (ch->mod2 & MOD2_BCO) && (ch->mod2 & MOD2_TD);
}

static int
i386ex_dma_requester_io(const i386ex_dma_chan_t *ch)
{
    return (ch->mod2 & MOD2_BCO) && (ch->mod2 & MOD2_RD);
}

/*
   One data transfer the channel runs itself, between the requester and
   target addresses, for a software request or an on-chip unit's (hw).
   With the two sides of different widths, it moves a byte; see the
   header. Returns whether the buffer ended.
 */
static int
i386ex_dma_transfer(i386ex_dma_t *dev, int n, int hw)
{
    i386ex_dma_chan_t *ch    = &dev->ch[n];
    int                rsize = i386ex_dma_width(ch, 1);
    int                tsize = i386ex_dma_width(ch, 0);
    int                size  = (rsize == tsize) ? rsize : 1;
    int                val;

    /*
       In a fly-by transfer the requester is a device answering DMAACK#, and
       neither an on-chip unit (which DMAACK# doesn't reach) nor a software
       request has one, so nothing moves; the channel goes on regardless.
     */
    if (!(ch->mod2 & MOD2_BCO)) {
        i386ex_dma_log("386EX DMA: channel %i in fly-by mode with no device on it (%s)\n",
                       n, hw ? "on-chip requester" : "software request");
    } else {
        switch (ch->mod1 & MOD1_TD) {
            case MOD1_TD_R2T:
                val = i386ex_dma_space_read(ch->req, i386ex_dma_requester_io(ch), size);
                i386ex_dma_space_write(ch->tar, i386ex_dma_target_io(ch), size, val);
                break;
            case MOD1_TD_T2R:
                val = i386ex_dma_space_read(ch->tar, i386ex_dma_target_io(ch), size);
                i386ex_dma_space_write(ch->req, i386ex_dma_requester_io(ch), size, val);
                break;
            default: /* Verify: the target is read, and nothing written. */
                (void) i386ex_dma_space_read(ch->tar, i386ex_dma_target_io(ch), size);
                break;
        }
    }

    i386ex_dma_step_requester(dev, n, size);
    i386ex_dma_step_target(dev, n, size);

    if (i386ex_dma_count_down(dev, n, size)) {
        i386ex_dma_buffer_end(dev, n);
        return 1;
    }

    return 0;
}

/* Clocks one bus cycle takes, at the least: T1 and T2. */
static uint64_t
i386ex_dma_cycle_time(void)
{
    return (uint64_t) (2.0 * (double) TIMER_USEC * 1000000.0 / (double) cpu_s->rspeed);
}

/*
   Runs the transfers the channels run themselves, a batch of bus cycles at
   a time, channel 0 first, as its default priority puts it.

   A software request stays active until its buffer ends, so every
   data-transfer mode moves the whole buffer (section 12.3.10). An on-chip
   unit's request moves one transfer in single mode, and the channel gives
   the bus up before the next; in block mode it starts a buffer transfer,
   which goes on to the buffer's end whatever the request does; and in
   demand mode the channel transfers for as long as the request stays up
   (section 12.2.7).
 */
static void
i386ex_dma_run(void *priv)
{
    i386ex_dma_t *dev        = (i386ex_dma_t *) priv;
    int           bus_cycles = 0;

    dev->running = 1;

    for (int n = 0; n < 2; n++) {
        i386ex_dma_chan_t *ch = &dev->ch[n];

        while ((bus_cycles < BATCH) && i386ex_dma_pending(dev, n)) {
            int     hw  = !ch->sreq;
            uint8_t dtm = ch->mod1 & MOD1_DTM;

            /* Taking a new request clears a timer output's flip-flop. */
            if (hw && !ch->burst) {
                if (i386ex_dma_source_edge(i386ex_dma_source(dev, n)))
                    dev->edge &= ~(1 << i386ex_dma_source(dev, n));
                ch->burst = (dtm == MOD1_BLOCK);
            }

            bus_cycles += (ch->mod2 & MOD2_BCO) ? 2 : 1;
            if (i386ex_dma_transfer(dev, n, hw) || (hw && (dtm == MOD1_SINGLE)))
                break;
        }
    }

    dev->running = 0;

    if (i386ex_dma_pending(dev, 0) || i386ex_dma_pending(dev, 1))
        timer_set_delay_u64(&dev->timer, (uint64_t) (bus_cycles ? bus_cycles : 1) * i386ex_dma_cycle_time());
}

/* Starts the channels on whatever they have to run, a bus cycle from now. */
static void
i386ex_dma_kick(i386ex_dma_t *dev)
{
    if (dev->running || timer_is_enabled(&dev->timer))
        return;

    if (i386ex_dma_pending(dev, 0) || i386ex_dma_pending(dev, 1))
        timer_set_delay_u64(&dev->timer, i386ex_dma_cycle_time());
}

/* Masking a channel leaves a timer request in its flip-flop, for when it is unmasked. */
static void
i386ex_dma_set_mask(i386ex_dma_t *dev, int n, int mask)
{
    dev->ch[n].mask = !!mask;
}

/* ---- Registers ------------------------------------------------------------------------------- */

/* A write to one byte of an address or count, to the current and base copies, or while chaining the base only. */
static void
i386ex_dma_write_byte(i386ex_dma_chan_t *ch, uint32_t *cur, uint32_t *base, int byte, uint8_t val)
{
    uint32_t mask = 0xffu << (byte << 3);
    uint32_t bits = (uint32_t) val << (byte << 3);

    *base = (*base & ~mask) | bits;
    if (!ch->chain)
        *cur = (*cur & ~mask) | bits;
}

/* The byte pointer picks the low or high byte, and toggles on every access through it. */
static int
i386ex_dma_bp(i386ex_dma_t *dev)
{
    int bp = dev->bp;

    dev->bp ^= 1;

    return bp;
}

static void
i386ex_dma_reset_regs(i386ex_dma_t *dev)
{
    timer_disable(&dev->timer);

    dev->bp   = 0;
    dev->cmd1 = 0x00;
    dev->cmd2 = 0x08;
    dev->sts    = 0x00;
    dev->is     = 0x00;
    dev->tc_int = 0x00;
    dev->ien  = 0x00;
    dev->ovfe = 0x0a; /* 8237A-compatible: 16-bit target address and byte count. */

    for (int n = 0; n < 2; n++) {
        i386ex_dma_chan_t *ch = &dev->ch[n];

        ch->mod1        = 0x00;
        ch->mod2        = 0x00;
        ch->bsr         = BSR_RBS | BSR_TBS;
        ch->chain       = 0;
        ch->mask        = 1;
        ch->sreq        = 0;
        ch->burst       = 0;
        ch->adv         = 0;
        ch->base_loaded = 0;
    }

    i386ex_dma_int_update(dev);
}

static uint8_t
i386ex_dma_read(uint16_t port, void *priv)
{
    i386ex_dma_t *dev = (i386ex_dma_t *) priv;
    uint8_t       ret = 0xff;
    int           n;

    /* The DOS I/O space has the 8237A-compatible registers at the same low bits. */
    if (port < 0x1000)
        port |= 0xf000;

    switch (port) {
        case DMA0TAR:
        case DMA1TAR:
            n   = (port == DMA1TAR);
            ret = dev->ch[n].tar >> (i386ex_dma_bp(dev) << 3);
            break;
        case DMA0BYC:
        case DMA1BYC:
            n   = (port == DMA1BYC);
            ret = dev->ch[n].byc >> (i386ex_dma_bp(dev) << 3);
            break;
        case DMA0REQL:
        case DMA0REQH:
        case DMA1REQL:
        case DMA1REQH:
            n   = (port >= DMA1REQL);
            ret = dev->ch[n].req >> ((((port & 1) << 1) | i386ex_dma_bp(dev)) << 3);
            break;
        case DMA0TAR2:
        case DMA1TAR2:
            ret = dev->ch[port == DMA1TAR2].tar >> 16;
            break;
        case DMA0TAR3:
        case DMA1TAR3:
            ret = (dev->ch[port == DMA1TAR3].tar >> 24) & 0x03;
            break;
        case DMA0BYC2:
        case DMA1BYC2:
            ret = dev->ch[port == DMA1BYC2].byc >> 16;
            break;

        case DMACMD1: /* DMASTS: reading it clears the TC bits, there and in DMAIS. */
            ret = dev->sts;
            for (n = 0; n < 2; n++) {
                if (i386ex_dma_dreq(dev, n))
                    ret |= STS_R(n);
            }
            dev->sts &= ~(STS_TC(0) | STS_TC(1));
            dev->is &= ~(IS_TC(0) | IS_TC(1));
            dev->tc_int = 0x00;
            i386ex_dma_int_update(dev);
            break;
        case DMASRR:
            ret = (dev->ch[0].sreq ? 0x01 : 0x00) | (dev->ch[1].sreq ? 0x02 : 0x00);
            break;
        case DMAGRPMSK:
            ret = (dev->ch[0].mask ? 0x01 : 0x00) | (dev->ch[1].mask ? 0x02 : 0x00);
            break;
        case DMACHR: /* DMAIS */
            ret = dev->is;
            break;
        case DMAIEN:
            ret = dev->ien;
            break;
        case DMAOVFE:
            ret = dev->ovfe;
            break;

        default:
            break;
    }

    return ret;
}

static void
i386ex_dma_write(uint16_t port, uint8_t val, void *priv)
{
    i386ex_dma_t      *dev = (i386ex_dma_t *) priv;
    i386ex_dma_chan_t *ch;
    int                n;

    if (port < 0x1000)
        port |= 0xf000;

    i386ex_dma_log("386EX DMA: [%04X] = %02X\n", port, val);

    switch (port) {
        case DMA0TAR:
        case DMA1TAR:
            ch = &dev->ch[port == DMA1TAR];
            i386ex_dma_write_byte(ch, &ch->tar, &ch->tar_base, i386ex_dma_bp(dev), val);
            break;
        case DMA0BYC:
        case DMA1BYC:
            ch = &dev->ch[port == DMA1BYC];
            i386ex_dma_write_byte(ch, &ch->byc, &ch->byc_base, i386ex_dma_bp(dev), val);
            break;
        case DMA0REQL:
        case DMA0REQH:
        case DMA1REQL:
        case DMA1REQH:
            /* REQ1:0 at the even address and REQ3:2 at the odd one, by BP; REQ3 has the top 2 of 26 bits. */
            ch = &dev->ch[port >= DMA1REQL];
            n  = ((port & 1) << 1) | i386ex_dma_bp(dev);
            i386ex_dma_write_byte(ch, &ch->req, &ch->req_base, n, (n == 3) ? (val & 0x03) : val);
            break;
        case DMA0TAR2:
        case DMA1TAR2:
            ch = &dev->ch[port == DMA1TAR2];
            i386ex_dma_write_byte(ch, &ch->tar, &ch->tar_base, 2, val);
            break;
        case DMA0TAR3:
        case DMA1TAR3:
            /* The top byte: while chaining, this is what loads the next buffer. */
            n  = (port == DMA1TAR3);
            ch = &dev->ch[n];
            i386ex_dma_write_byte(ch, &ch->tar, &ch->tar_base, 3, val & 0x03);
            if (ch->chain) {
                ch->base_loaded = 1;
                dev->is &= ~IS_CI(n);
                i386ex_dma_int_update(dev);
            }
            break;
        case DMA0BYC2:
        case DMA1BYC2:
            ch = &dev->ch[port == DMA1BYC2];
            i386ex_dma_write_byte(ch, &ch->byc, &ch->byc_base, 2, val);
            break;

        case DMACMD1:
            dev->cmd1 = val;
            i386ex_dma_kick(dev);
            break;
        case DMASRR:
            ch = &dev->ch[val & SEL_CS];
            ch->sreq = !!(val & SEL_BIT);
            i386ex_dma_kick(dev);
            break;
        case DMAMSK:
            i386ex_dma_set_mask(dev, val & SEL_CS, val & SEL_BIT);
            i386ex_dma_kick(dev);
            break;
        case DMAMOD1:
            dev->ch[val & SEL_CS].mod1 = val & ~(SEL_CS | 0x02);
            i386ex_dma_kick(dev);
            break;
        case DMACLRBP:
            dev->bp = 0;
            break;
        case DMACLR:
            i386ex_dma_reset_regs(dev);
            break;
        case DMACLRMSK:
            i386ex_dma_set_mask(dev, 0, 0);
            i386ex_dma_set_mask(dev, 1, 0);
            i386ex_dma_kick(dev);
            break;
        case DMAGRPMSK:
            i386ex_dma_set_mask(dev, 0, val & 0x01);
            i386ex_dma_set_mask(dev, 1, val & 0x02);
            i386ex_dma_kick(dev);
            break;
        case DMABSR:
            dev->ch[val & SEL_CS].bsr = val & (BSR_RBS | BSR_TBS);
            break;
        case DMACHR:
            /* Entering chaining, the base copy is empty, and asks for its first load (section 12.2.6.3). */
            n  = val & SEL_CS;
            ch = &dev->ch[n];
            ch->chain       = !!(val & SEL_BIT);
            ch->base_loaded = 0;
            if (ch->chain)
                dev->is |= IS_CI(n);
            else
                dev->is &= ~IS_CI(n);
            i386ex_dma_int_update(dev);
            break;
        case DMACMD2:
            dev->cmd2 = val & 0x0f;
            break;
        case DMAMOD2:
            dev->ch[val & SEL_CS].mod2 = val & ~(SEL_CS | 0x02);
            break;
        case DMAIEN:
            dev->ien = val & 0x03;
            break;
        case DMAOVFE:
            dev->ovfe = val & 0x0f;
            break;
        case DMACLRTC:
            dev->tc_int = 0x00;
            i386ex_dma_int_update(dev);
            break;

        default:
            break;
    }
}

/* ---- The device side, through dma.h -------------------------------------------------------- */

/* The chip channel whose DRQ pin a device's 86Box channel is wired to, -1 for none. */
static int
i386ex_dma_slot(const i386ex_dma_t *dev, int channel)
{
    for (int n = 0; n < 2; n++) {
        if ((dev->ch[n].dma_ch >= 0) && (dev->ch[n].dma_ch == channel))
            return n;
    }

    return -1;
}

/* The 8237A mode byte for a channel, which devices check for the direction (and verify). */
static int
i386ex_dma_mode(int channel, void *priv)
{
    i386ex_dma_t *dev = (i386ex_dma_t *) priv;
    int           n   = i386ex_dma_slot(dev, channel);
    uint8_t       mod1;
    int           ret;

    if (n < 0)
        return 0x00;

    mod1 = dev->ch[n].mod1;
    ret  = (mod1 & (MOD1_DTM | MOD1_TI | MOD1_AI)) | n;

    switch (mod1 & MOD1_TD) {
        case MOD1_TD_R2T:
            ret |= 0x04; /* the device writes memory */
            break;
        case MOD1_TD_T2R:
            ret |= 0x08; /* the device reads memory */
            break;
        default:
            break;
    }

    return ret;
}

static int
i386ex_dma_readable(int channel, void *priv)
{
    i386ex_dma_t *dev = (i386ex_dma_t *) priv;
    int           n   = i386ex_dma_slot(dev, channel);
    uint8_t       td;

    if ((n < 0) || !i386ex_dma_pin_served(dev, n))
        return 0;

    td = dev->ch[n].mod1 & MOD1_TD;
    return (td == MOD1_TD_T2R) || (td == MOD1_TD_VER);
}

static int
i386ex_dma_writable(int channel, void *priv)
{
    i386ex_dma_t *dev = (i386ex_dma_t *) priv;
    int           n   = i386ex_dma_slot(dev, channel);
    uint8_t       td;

    if ((n < 0) || !i386ex_dma_pin_served(dev, n))
        return 0;

    td = dev->ch[n].mod1 & MOD1_TD;
    return (td == MOD1_TD_R2T) || (td == MOD1_TD_VER);
}

/* Moves the channel past one data transfer of the device's; returns DMA_OVER if that ended the buffer. */
static int
i386ex_dma_device_advance(i386ex_dma_t *dev, int n, int size)
{
    i386ex_dma_step_target(dev, n, size);

    if (i386ex_dma_count_down(dev, n, size)) {
        i386ex_dma_buffer_end(dev, n);
        return DMA_OVER;
    }

    return 0;
}

/* What the target holds for a device reading through the channel, or DMA_VERIFY. */
static int
i386ex_dma_device_fetch(i386ex_dma_t *dev, int n, int *size)
{
    i386ex_dma_chan_t *ch = &dev->ch[n];

    *size = i386ex_dma_width(ch, 0);

    if ((ch->mod1 & MOD1_TD) == MOD1_TD_VER)
        return DMA_VERIFY;

    return i386ex_dma_space_read(ch->tar, i386ex_dma_target_io(ch), *size);
}

/* Returns 1 if the advance ended the buffer, as dma.c's 8237A does. */
static int
i386ex_dma_advance(int channel, void *priv)
{
    i386ex_dma_t *dev = (i386ex_dma_t *) priv;
    int           n   = i386ex_dma_slot(dev, channel);

    if ((n < 0) || !dev->ch[n].adv)
        return 0;

    dev->ch[n].adv = 0;

    return !!i386ex_dma_device_advance(dev, n, i386ex_dma_width(&dev->ch[n], 0));
}

/*
   A read_only fetch leaves the channel where it was until the device calls
   advance; as with dma.c's 8237A, a device that goes on without calling it
   gets the advance first.
 */
static int
i386ex_dma_read_only(int channel, void *priv)
{
    i386ex_dma_t *dev = (i386ex_dma_t *) priv;
    int           n   = i386ex_dma_slot(dev, channel);
    int           size;

    (void) i386ex_dma_advance(channel, priv);

    if ((n < 0) || !i386ex_dma_readable(channel, priv))
        return DMA_NODATA;

    dev->ch[n].adv = 1;

    return i386ex_dma_device_fetch(dev, n, &size);
}

static int
i386ex_dma_read_ch(int channel, void *priv)
{
    i386ex_dma_t *dev = (i386ex_dma_t *) priv;
    int           n   = i386ex_dma_slot(dev, channel);
    int           size;
    int           ret;

    (void) i386ex_dma_advance(channel, priv);

    if ((n < 0) || !i386ex_dma_readable(channel, priv))
        return DMA_NODATA;

    ret = i386ex_dma_device_fetch(dev, n, &size);

    return ret | i386ex_dma_device_advance(dev, n, size);
}

static int
i386ex_dma_write_ch(int channel, uint16_t val, void *priv)
{
    i386ex_dma_t      *dev = (i386ex_dma_t *) priv;
    int                n   = i386ex_dma_slot(dev, channel);
    i386ex_dma_chan_t *ch;
    int                size;

    (void) i386ex_dma_advance(channel, priv);

    if ((n < 0) || !i386ex_dma_writable(channel, priv))
        return DMA_NODATA;

    ch   = &dev->ch[n];
    size = i386ex_dma_width(ch, 0);

    if ((ch->mod1 & MOD1_TD) == MOD1_TD_R2T)
        i386ex_dma_space_write(ch->tar, i386ex_dma_target_io(ch), size, val);

    return i386ex_dma_device_advance(dev, n, size);
}

static void
i386ex_dma_set_drq(int channel, int set, void *priv)
{
    i386ex_dma_t *dev = (i386ex_dma_t *) priv;
    int           n   = i386ex_dma_slot(dev, channel);

    if (n >= 0)
        dev->ch[n].drq = !!set;
}

static int
i386ex_dma_get_drq(int channel, void *priv)
{
    i386ex_dma_t *dev = (i386ex_dma_t *) priv;
    int           n   = i386ex_dma_slot(dev, channel);

    return (n >= 0) ? dev->ch[n].drq : 0;
}

/* set_service_handler stays out: nothing schedules a device's transfers here. */
static const dma_ctrl_t i386ex_dma_ops = {
    .channel_readable    = i386ex_dma_readable,
    .channel_writable    = i386ex_dma_writable,
    .channel_read_only   = i386ex_dma_read_only,
    .channel_advance     = i386ex_dma_advance,
    .channel_read        = i386ex_dma_read_ch,
    .channel_write       = i386ex_dma_write_ch,
    .set_service_handler = NULL,
    .set_drq             = i386ex_dma_set_drq,
    .get_drq             = i386ex_dma_get_drq,
    .mode                = i386ex_dma_mode
};

/* ---- The chip's side ------------------------------------------------------------------------- */

/* Maps the registers in the expanded I/O space, or the 8237A-compatible ones in the DOS I/O space. */
void
i386ex_dma_handler(i386ex_dma_t *dev, int set, int expanded)
{
    uint16_t base = expanded ? 0xf000 : 0x0000;

    io_handler(set, base, 4, i386ex_dma_read, NULL, NULL, i386ex_dma_write, NULL, NULL, dev);
    io_handler(set, base + 0x08, 8, i386ex_dma_read, NULL, NULL, i386ex_dma_write, NULL, NULL, dev);
    io_handler(set, base + 0x83, 1, i386ex_dma_read, NULL, NULL, i386ex_dma_write, NULL, NULL, dev);
    io_handler(set, base + 0x87, 1, i386ex_dma_read, NULL, NULL, i386ex_dma_write, NULL, NULL, dev);

    if (expanded) {
        io_handler(set, DMA0REQL, 4, i386ex_dma_read, NULL, NULL, i386ex_dma_write, NULL, NULL, dev);
        io_handler(set, DMABSR, 7, i386ex_dma_read, NULL, NULL, i386ex_dma_write, NULL, NULL, dev);
        io_handler(set, DMA1TAR3, 2, i386ex_dma_read, NULL, NULL, i386ex_dma_write, NULL, NULL, dev);
        io_handler(set, DMA0BYC2, 2, i386ex_dma_read, NULL, NULL, i386ex_dma_write, NULL, NULL, dev);
    }
}

/*
   A channel connected to another request source ends a block transfer the
   old one started. A timer request held in its flip-flop stays there, for
   whichever channel takes it.
 */
void
i386ex_dma_set_cfg(i386ex_dma_t *dev, uint8_t dmacfg)
{
    for (int n = 0; n < 2; n++) {
        if (((dev->cfg ^ dmacfg) >> (n << 2)) & 0x07)
            dev->ch[n].burst = 0;
    }

    dev->cfg = dmacfg;
    i386ex_dma_kick(dev);
}

/*
   An on-chip unit's request signal changes. A timer output's rising edge
   sets its flip-flop, whatever the channels are doing, and it holds the
   request until a channel connected to the output takes it.
 */
void
i386ex_dma_set_source(i386ex_dma_t *dev, int source, int level)
{
    int old;
    int kick = 0;

    if ((dev == NULL) || (source < 0) || (source >= I386EX_DMA_SOURCES))
        return;

    old              = dev->src[source];
    dev->src[source] = !!level;
    if (dev->src[source] == old)
        return;

    if (i386ex_dma_source_edge(source) && level)
        dev->edge |= 1 << source;

    for (int n = 0; n < 2; n++) {
        if (i386ex_dma_source(dev, n) == source)
            kick = 1;
    }

    if (kick)
        i386ex_dma_kick(dev);
}

void
i386ex_dma_set_intcfg(i386ex_dma_t *dev, uint8_t intcfg)
{
    dev->intcfg = intcfg;
    i386ex_dma_int_update(dev);
}

/*
   Wires the DRQ0 and DRQ1 pins to 86Box DMA channels (-1 leaves one
   unwired). The chip's channels are the machine's only ones, so the
   controller takes every 86Box channel, and one that isn't wired to it has
   nothing to answer.
 */
void
i386ex_dma_set_channels(i386ex_dma_t *dev, int drq0, int drq1)
{
    dev->ch[0].dma_ch = drq0;
    dev->ch[1].dma_ch = drq1;

    dma_set_controller(&i386ex_dma_ops, dev);
}

/* A reset also clears the timer request flip-flops, which a master clear (DMACLR) leaves alone. */
static void
i386ex_dma_reset(void *priv)
{
    i386ex_dma_t *dev = (i386ex_dma_t *) priv;

    dev->edge = 0x00;
    i386ex_dma_reset_regs(dev);
}

static void *
i386ex_dma_init(UNUSED(const device_t *info))
{
    i386ex_dma_t *dev = (i386ex_dma_t *) calloc(1, sizeof(i386ex_dma_t));

    dev->ch[0].dma_ch = dev->ch[1].dma_ch = -1;

    timer_add(&dev->timer, i386ex_dma_run, dev, 0);
    i386ex_dma_reset_regs(dev);

    /* The chip's channels are the machine's only ones: a board with a 386EX doesn't have the AT's 8237As. */
    dma_set_controller(&i386ex_dma_ops, dev);

    return dev;
}

static void
i386ex_dma_close(void *priv)
{
    i386ex_dma_t *dev = (i386ex_dma_t *) priv;

    timer_disable(&dev->timer);
    dma_set_controller(NULL, NULL);
    free(dev);
}

const device_t i386ex_dma_device = {
    .name          = "Intel 386EX DMA Controller",
    .internal_name = "i386ex_dma",
    .flags         = 0,
    .local         = 0,
    .init          = i386ex_dma_init,
    .close         = i386ex_dma_close,
    .reset         = i386ex_dma_reset,
    .available     = NULL,
    .speed_changed = NULL,
    .force_redraw  = NULL,
    .config        = NULL
};
