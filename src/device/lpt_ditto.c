/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          Iomega Ditto Max parallel port tape drive.
 *
 *          The Ditto Max is an ordinary QIC-117 floppy interface tape
 *          drive with a MicroSolutions "BackPack" parallel port bridge
 *          (a 50772D ASIC) bolted in front of it. Four protocol layers
 *          sit between the host's LPT registers and the tape:
 *
 *            1. the BackPack wire protocol, spoken over the LPT port in
 *               one of SPP nibble, PS/2 byte or EPP mode;
 *            2. the BackPack register file at 0x00..0xff, holding the
 *               128 KB buffer, its CRC-16 and ECC engines, and the IRQ
 *               control;
 *            3. an NEC 765 style floppy controller, whose registers the
 *               bridge exposes at 0x40..0x47 of that register file;
 *            4. the QIC-117 command set, commanded by counting the step
 *               pulses that controller emits - the same command set the
 *               cable-attached drive in fdd_tape.c speaks.
 *
 *          This file implements layers 1 to 3. Only layer 1 and the
 *          plumbing of layer 2 are present so far; see the milestone
 *          notes on backpack_read_reg().
 *
 * Authors: Dmitry Brant, <me@dmitrybrant.com>
 *
 *          Copyright 2026 Dmitry Brant
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
#include <86box/timer.h>
#include <86box/lpt.h>
#include <86box/plat.h>

/* For logging the BackPack wire protocol into the emulator log. */
#if 0
#    define ENABLE_LPT_DITTO_LOG 1
#endif

/*
   Bits of the parallel port's own registers, at their literal values in
   the ISA register - the inversion of nStrobe, nAutoFd and nSelectIn at
   the connector applies on top of these, and both ends of the link work
   in terms of the register rather than the wire.
 */
#define LPT_CTRL_STROBE   0x01
#define LPT_CTRL_AUTOFD   0x02
#define LPT_CTRL_INIT     0x04
#define LPT_CTRL_SELECT   0x08
#define LPT_CTRL_INTEN    0x10
#define LPT_CTRL_DIR      0x20

#define LPT_STAT_ERROR    0x08
#define LPT_STAT_SELECT   0x10
#define LPT_STAT_PAPEROUT 0x20
#define LPT_STAT_ACK      0x40
#define LPT_STAT_BUSY     0x80

/* Only the top five bits of the status register belong to the device -
   the parallel port makes the low three up for itself. */
#define LPT_STAT_MASK     0xf8

/*
   Transfer protocols the bridge can be programmed for. The ordering is
   the host driver's: anything below EPP-8 is addressed and transferred
   through the SPP registers, everything from EPP-8 up through the EPP
   address and data registers at base+3 and base+4.
 */
enum {
    DITTO_PROTO_SPP = 0,
    DITTO_PROTO_PS2,
    DITTO_PROTO_EPP8,
    DITTO_PROTO_EPP16,
    DITTO_PROTO_EPP32
};

/*
   Protocol bits, as written to the bridge's control register 0x04 (and,
   deferred to the next connect, to register 0x24). The two low bits are
   always set by the host driver and their meaning is unknown.
 */
#define DITTO_PROTO_BITS_SPP  0x00
#define DITTO_PROTO_BITS_PS2  0x10
#define DITTO_PROTO_BITS_EPP  0x18
#define DITTO_PROTO_BITS_MASK 0x18
#define DITTO_PROTO_BITS_FIXED 0x24

/* BackPack register file (layer 2). */
#define BP_REG_STAT    0x00 /* interrupt and ECC status */
#define BP_REG_CTRL    0x04 /* transfer mode, DMA direction, memory write arm */
#define BP_REG_0x05    0x05
#define BP_REG_0x06    0x06 /* EEPROM, where fitted; also pokes the IRQ line */
#define BP_REG_0x07    0x07 /* protocol switch helper */
#define BP_REG_CLEAR   0x0b /* writing its own number clears the CRC register */
#define BP_REG_IRQ     0x0f
#define BP_REG_TEST    0x13 /* protocol self test counter */
#define BP_REG_0x1a    0x1a
#define BP_REG_CRC     0x22 /* CRC-16 shift register, two bytes */
#define BP_REG_PROTO   0x24 /* deferred protocol program, { bits, 0xa4 } */
#define BP_REG_MEMADDR 0x28 /* buffer address, three bytes little endian */
#define BP_REG_DMAADDR 0x2c
#define BP_REG_DMASIZE 0x30 /* count - 1 */
#define BP_REG_ECC     0x34
#define BP_REG_FDC     0x40 /* the 765's registers, seven of them */
#define BP_REG_MEMORY  0xa0 /* buffer data FIFO */

/* Register 0x00. */
#define BP_STAT_IRQ       0x01
#define BP_STAT_ALWAYS    0x10 /* always reads back set */
#define BP_STAT_ECC_ERROR 0x20

/* Register 0x04. */
#define BP_CTRL_DMA_WRITE 0x02
#define BP_CTRL_DMA_READ  0x03
#define BP_CTRL_DMA_MASK  0x03
#define BP_CTRL_MEM_WRITE 0x80 /* arms a non-EPP write to the buffer */

/* Register 0x06: the poke sequence 0x08, 0x00, 0x80 arms a test interrupt. */
#define BP_0x06_IRQ_ARM 0x80

/* Register 0x0b clears the CRC register when written with its own number. */
#define BP_CLEAR_CLEAR 0x0b

/* Register 0x0f. */
#define BP_IRQ_PENDING 0x01
#define BP_IRQ_SOFTEN  0x10 /* let register 0x00 report pending interrupts */
#define BP_IRQ_HARDEN  0x18 /* additionally drive ACK, raising a real IRQ */
#define BP_IRQ_MASK    0x18

/* Register 0x24 only programs the protocol if this follows the bits. */
#define BP_PROTO_MAGIC 0xa4

/* The buffer is 128 KB and its address register wraps at that boundary. */
#define DITTO_BUFFER_SIZE 0x20000
#define DITTO_BUFFER_MASK (DITTO_BUFFER_SIZE - 1)

#define DITTO_CRC_INIT    0xffff

typedef struct ditto_t {
    void   *lpt;

    /* Configuration. */
    int     max_proto;  /* fastest protocol the bridge will admit to */
    char    image_fn[1024];
    int     readonly;

    /* Layer 1: the wire. */
    uint8_t ctrl;       /* our shadow of the host's control register */
    uint8_t dat;        /* last byte the host put on the data lines */
    int     connected;
    int     proto;      /* protocol the bridge is programmed for */
    uint8_t proto_bits;

    int     knock_armed; /* the host has begun the connect sequence */
    int     knock;       /* SELECT edges seen since it began */
    int     connect_resp; /* status reads still owed the connect response */
    int     force_proto;  /* a force-protocol (or EPP disconnect) is under way */

    uint8_t saved_ctrl;
    uint8_t saved_dat;

    /* Layer 1: the byte being handed back to the host. */
    int     cur_reg;    /* register the host has addressed */
    int     xfer_idx;   /* bytes moved since that register was addressed -
                           the multi-byte registers (0x22, 0x24, 0x28, 0x2c,
                           0x30, 0x34) are written as a run of bytes to the
                           one address, and index themselves off this */
    uint8_t rd_val;     /* byte being shifted out */
    int     rd_high;    /* the low nibble has gone, the high one is next */
    uint8_t rd_out;     /* status bits presenting the current nibble */

    /* Layer 2: the register file. */
    uint8_t  reg[256];
    uint8_t  test_ctr;    /* register 0x13 */
    uint8_t  irq_ctrl;    /* register 0x0f, the enable bits only */
    int      irq_pending;
    int      irq_arm;     /* the register 0x06 poke has armed a test IRQ */
    uint8_t  deferred_proto_bits; /* register 0x24, applied on next connect */
    int      deferred_valid;
    uint16_t crc;         /* register 0x22 */
    uint32_t mem_addr;    /* register 0x28, and the FIFO's own pointer */
    uint8_t *buffer;      /* the 128 KB the FIFO reads and writes */
    int      mem_write_armed; /* register 0x04 bit 7 */
    int      mem_burst;   /* a burst write into the buffer has been primed */
    uint32_t dma_addr;    /* register 0x2c */
    uint32_t dma_count;   /* register 0x30, held as written - one less */
    uint8_t  ecc_ctrl[2]; /* register 0x34 */
} ditto_t;

#ifdef ENABLE_LPT_DITTO_LOG
int lpt_ditto_do_log = ENABLE_LPT_DITTO_LOG;

static void
ditto_log(const char *fmt, ...)
{
    va_list ap;

    if (lpt_ditto_do_log) {
        va_start(ap, fmt);
        pclog_ex(fmt, ap);
        va_end(ap);
    }
}
static const char *ditto_proto_name[] = {
    "SPP 4-bit", "PS/2 8-bit", "EPP-8", "EPP-16", "EPP-32"
};
#else
#    define ditto_log(fmt, ...)
#endif

/*
   Turns the protocol bits the host writes to control register 0x04 (and,
   deferred to the next connect, to register 0x24) back into a protocol.
 */
static int
ditto_decode_proto_bits(uint8_t bits)
{
    switch (bits & DITTO_PROTO_BITS_MASK) {
        case DITTO_PROTO_BITS_PS2:
            return DITTO_PROTO_PS2;
        case DITTO_PROTO_BITS_EPP:
            return DITTO_PROTO_EPP8;
        default:
            break;
    }

    return DITTO_PROTO_SPP;
}

/* --------------------------------------------------------------------- */
/* Layer 2: the BackPack register file                                   */
/* --------------------------------------------------------------------- */

/*
   CRC-16-CCITT, polynomial 0x1021, seeded with all ones and shifting
   left - the bridge multiplies by x towards the high end, so this is the
   MSB-first form. Every byte that passes through the buffer FIFO goes
   through here, in either direction.
 */
static uint16_t
ditto_crc_byte(uint16_t crc, uint8_t val)
{
    crc ^= (uint16_t) val << 8;

    for (uint8_t i = 0; i < 8; i++)
        crc = (crc & 0x8000) ? (uint16_t) ((crc << 1) ^ 0x1021)
                             : (uint16_t) (crc << 1);

    return crc;
}

/*
   The buffer FIFO. One address register serves both directions and both
   advance it, wrapping at the top of the 128 KB.

   Reading here is also how the host runs a "check" pass: it frames one
   exactly as it frames a read and simply throws the nibbles away, so
   from this side the two are the same operation - the pointer moves and
   the CRC turns over either way, which is precisely what the check is
   for.
 */
static uint8_t
ditto_mem_read_byte(ditto_t *dev)
{
    const uint8_t ret = dev->buffer[dev->mem_addr & DITTO_BUFFER_MASK];

    dev->mem_addr = (dev->mem_addr + 1) & DITTO_BUFFER_MASK;
    dev->crc      = ditto_crc_byte(dev->crc, ret);

    return ret;
}

/*
   Whether the data lines are currently being streamed into the buffer.

   This is a level, not an edge: the host arms the write in register
   0x04, addresses the FIFO, and raises STROBE - but STROBE is usually
   already up, left there by the very register write that armed it, so
   there is no edge to catch. What ends the burst is STROBE going back
   down, which the host does before addressing anything else. Without
   that the two data writes that address the next register would be
   taken for buffer data.
 */
static int
ditto_mem_burst_active(const ditto_t *dev)
{
    return dev->connected && (dev->cur_reg == BP_REG_MEMORY) &&
           dev->mem_write_armed && (dev->ctrl & LPT_CTRL_STROBE);
}

static void
ditto_mem_write_byte(ditto_t *dev, uint8_t val)
{
    dev->buffer[dev->mem_addr & DITTO_BUFFER_MASK] = val;

    dev->mem_addr = (dev->mem_addr + 1) & DITTO_BUFFER_MASK;
    dev->crc      = ditto_crc_byte(dev->crc, val);
}

/*
   Raises or drops the interrupt the bridge presents to the host.

   The bridge does not interrupt while the host is connected - the host
   polls register 0x00 instead - so the ACK line is only driven once the
   host has let go, and then only if it has asked for a hard interrupt.
   The one exception is the test interrupt the register 0x06 poke arms,
   which is a diagnostic and fires where it is asked to.
 */
static void
ditto_update_irq(ditto_t *dev, int test_poke)
{
    if (dev->lpt == NULL)
        return;

    if (dev->irq_pending &&
        (test_poke || (!dev->connected && ((dev->irq_ctrl & BP_IRQ_MASK) == BP_IRQ_HARDEN))))
        lpt_irq(dev->lpt, 1);
    else
        lpt_irq(dev->lpt, 0);
}

/*
   Reads one byte from the addressed register. Consecutive reads without
   re-addressing keep coming from the same register, which is how the
   host streams the buffer out of 0xa0, walks a multi-byte register such
   as the CRC at 0x22, and tests the protocol against the counter in
   0x13. The index of the byte within such a run is xfer_idx.

   Milestones 4 onwards fill in the 128 KB buffer and its CRC engine, and
   the 765 window at 0x40.
 */
static uint8_t
backpack_read_reg(ditto_t *dev, int reg)
{
    const int idx = dev->xfer_idx++;
    uint8_t   ret;

    switch (reg) {
        case BP_REG_STAT:
            /*
               Bit 4 always reads back set. Pending interrupts only show
               here once the host has enabled the soft indication - that
               is what makes its polling during a transfer work.
             */
            ret = BP_STAT_ALWAYS;
            if (dev->irq_pending && (dev->irq_ctrl & BP_IRQ_SOFTEN))
                ret |= BP_STAT_IRQ;
            break;

        case BP_REG_IRQ:
            ret = (uint8_t) (dev->irq_ctrl & BP_IRQ_MASK);
            if (dev->irq_pending)
                ret |= BP_IRQ_PENDING;
            break;

        case BP_REG_TEST:
            /*
               The protocol self test. The host seeds this register and
               then reads it several hundred times over, insisting that
               every read comes back one higher than the last - that is
               how it satisfies itself that the wire protocol and its
               timing are sound.
             */
            ret = dev->test_ctr++;
            break;

        case BP_REG_CRC:
            ret = (idx == 0) ? (uint8_t) (dev->crc & 0xff)
                             : (uint8_t) (dev->crc >> 8);
            break;

        case BP_REG_MEMORY:
            ret = ditto_mem_read_byte(dev);
            break;

        default:
            /*
               The write-only registers read back what was last put in
               them, which is all the host ever needs of them.
             */
            ret = dev->reg[reg & 0xff];
            ditto_log("Ditto: read of unhandled register %02X[%i] -> %02X\n",
                      reg, idx, ret);
            break;
    }

    return ret;
}

/* Loads one byte of a little-endian multi-byte address register. */
static void
ditto_set_addr_byte(uint32_t *addr, int idx, uint8_t val)
{
    if ((idx < 0) || (idx > 2))
        return;

    *addr &= ~((uint32_t) 0xff << (idx * 8));
    *addr |= (uint32_t) val << (idx * 8);
}

static void
backpack_write_reg(ditto_t *dev, int reg, uint8_t val)
{
    const int idx = dev->xfer_idx++;

    dev->reg[reg & 0xff] = val;

    switch (reg) {
        case BP_REG_CTRL:
            /*
               The transfer mode written here takes effect at once, for
               the connection in progress - unlike register 0x24, which
               is remembered for the next one.
             */
            dev->proto_bits      = val;
            dev->proto           = ditto_decode_proto_bits(val);
            dev->mem_write_armed = !!(val & BP_CTRL_MEM_WRITE);

            /*
               The host works out which interrupt line the bridge is on
               by poking register 0x06 and then writing here. Nobody
               knows why this is what raises the interrupt, but it is.
             */
            if (dev->irq_arm) {
                dev->irq_pending = 1;
                ditto_update_irq(dev, 1);
            }
            break;

        case BP_REG_0x06:
            dev->irq_arm = (val == BP_0x06_IRQ_ARM);
            if (!dev->irq_arm && (val == 0x00)) {
                dev->irq_pending = 0;
                ditto_update_irq(dev, 0);
            }
            break;

        case BP_REG_CLEAR:
            /*
               A register that wants its own number written to it before
               it will do anything. It clears the CRC shift register.
             */
            if (val == BP_CLEAR_CLEAR)
                dev->crc = DITTO_CRC_INIT;
            break;

        case BP_REG_IRQ:
            dev->irq_ctrl = (uint8_t) (val & BP_IRQ_MASK);
            ditto_update_irq(dev, 0);
            break;

        case BP_REG_TEST:
            dev->test_ctr = val;
            break;

        case BP_REG_PROTO:
            /*
               Two bytes: the protocol bits, then a magic 0xa4 that has
               to be there for the program to take. Detection writes the
               same register with a different second byte and expects
               nothing to come of it, so the magic is what tells a real
               protocol switch from that.
             */
            if (idx == 0) {
                dev->deferred_proto_bits = val;
                dev->deferred_valid      = 0;
            } else if ((idx == 1) && (val == BP_PROTO_MAGIC)) {
                dev->deferred_valid = 1;
                ditto_log("Ditto: protocol %s programmed for the next connect\n",
                          ditto_proto_name[ditto_decode_proto_bits(dev->deferred_proto_bits)]);
            }
            break;

        case BP_REG_MEMADDR:
            ditto_set_addr_byte(&dev->mem_addr, idx, val);
            break;

        case BP_REG_DMAADDR:
            ditto_set_addr_byte(&dev->dma_addr, idx, val);
            break;

        case BP_REG_DMASIZE:
            ditto_set_addr_byte(&dev->dma_count, idx, val);
            break;

        case BP_REG_ECC:
            if (idx < 2)
                dev->ecc_ctrl[idx] = val;
            break;

        case BP_REG_MEMORY:
            /* An INIT toggle is the host committing the data lines. */
            ditto_mem_write_byte(dev, val);
            break;

        /* Stored and read back, but of no consequence to us. */
        case BP_REG_0x05:
        case BP_REG_0x07:
        case BP_REG_0x1a:
            break;

        default:
            ditto_log("Ditto: write of unhandled register %02X[%i] <- %02X\n",
                      reg, idx, val);
            break;
    }
}

/* --------------------------------------------------------------------- */
/* Layer 1: the BackPack wire protocol                                   */
/* --------------------------------------------------------------------- */

/*
   Spreads a nibble across the five status bits the device owns, in the
   arrangement the host's reassembly expects: value bits 0 to 2 land in
   status bits 3 to 5, and value bit 3 in status bit 7. Status bit 6 -
   ACK - is deliberately left clear, so that a nibble can never be
   mistaken for the connect response.
 */
static uint8_t
ditto_encode_nibble(uint8_t val)
{
    return (uint8_t) (((val & 0x07) << 3) | ((val & 0x08) << 4));
}

static void
ditto_connect(ditto_t *dev)
{
    dev->connected   = 1;
    dev->knock_armed = 0;
    dev->knock       = 0;
    dev->rd_high     = 0;
    dev->rd_out      = 0x00;

    /*
       A protocol programmed into register 0x24 is remembered rather than
       acted on, and comes into force here - which is why the host always
       disconnects and reconnects around a protocol switch.
     */
    if (dev->deferred_valid) {
        dev->proto_bits     = dev->deferred_proto_bits;
        dev->proto          = ditto_decode_proto_bits(dev->deferred_proto_bits);
        dev->deferred_valid = 0;
    }

    /* Connecting takes the interrupt line down; the host polls instead. */
    ditto_update_irq(dev, 0);

    /*
       The host now reads the status register to find out which family of
       protocols the bridge is currently programmed for, and refuses to
       go on until it agrees with its own idea. ACK alone means a non-EPP
       mode, ACK together with BUSY means EPP. It reads the answer once,
       between two toggles of AUTOFD.
     */
    dev->connect_resp = 2;

    ditto_log("Ditto: connected in %s mode\n", ditto_proto_name[dev->proto]);
}

static void
ditto_disconnect(ditto_t *dev)
{
    if (!dev->connected)
        return;

    dev->connected    = 0;
    dev->knock_armed  = 0;
    dev->knock        = 0;
    dev->connect_resp = 0;
    dev->force_proto  = 0;
    dev->rd_out       = 0x00;

    /* Letting go is what lets a pending interrupt reach the host. */
    ditto_update_irq(dev, 0);

    ditto_log("Ditto: disconnected\n");
}

/*
   Hands the next nibble of the addressed register to the host. Every
   second toggle starts a fresh byte, so a host that keeps toggling
   streams the register out without re-addressing it.
 */
static void
ditto_advance_read(ditto_t *dev)
{
    /*
       A mode the bridge will not do - either not implemented here yet,
       or held back by the configured ceiling - answers with all ones.
       That is enough to break the host's protocol self test, which is
       exactly how it is meant to find out and fall back to a slower
       mode it can rely on.
     */
    if ((dev->proto != DITTO_PROTO_SPP) || (dev->proto > dev->max_proto)) {
        ditto_log("Ditto: read refused in %s mode\n", ditto_proto_name[dev->proto]);
        dev->rd_out  = ditto_encode_nibble(0x0f);
        dev->rd_high = !dev->rd_high;
        return;
    }

    if (dev->rd_high) {
        dev->rd_out  = ditto_encode_nibble(dev->rd_val >> 4);
        dev->rd_high = 0;
    } else {
        dev->rd_val  = backpack_read_reg(dev, dev->cur_reg);
        dev->rd_out  = ditto_encode_nibble(dev->rd_val & 0x0f);
        dev->rd_high = 1;
    }
}

static void
ditto_write_data(uint8_t val, void *priv)
{
    ditto_t      *dev = (ditto_t *) priv;
    const uint8_t old = dev->dat;

    dev->dat = val;

    /*
       Streaming into the buffer, a byte is committed by whichever of the
       two things the host does to signal it: a toggle of INIT, or a
       change of the data lines. The host picks between them because it
       cannot put the same value on the lines twice and have the bridge
       notice - so a byte equal to the one before it is sent as a bare
       INIT toggle with no data write at all.

       That is why the two paths both have to commit. Sampling the data
       lines only on toggles quietly swallows runs of repeated bytes;
       committing on every data write instead duplicates the first byte,
       since the write that opens the burst is only loading the lines for
       the toggle that follows it.
     */
    if (!ditto_mem_burst_active(dev))
        dev->mem_burst = 0;
    else if (!dev->mem_burst)
        dev->mem_burst = 1; /* the write that primes the lines */
    else if (val != old)
        ditto_mem_write_byte(dev, val);

    /*
       The bridge only drives the data lines during a PS/2 or EPP read.
       The rest of the time the host reads its own latch back, so keep
       the port's input register tracking what was last written - without
       this a host probing for a bidirectional port sees zeroes and draws
       the wrong conclusion about what the port can do.
     */
    if (dev->lpt != NULL)
        lpt_write_to_dat(dev->lpt, val);
}

static void
ditto_write_ctrl(uint8_t val, void *priv)
{
    ditto_t      *dev = (ditto_t *) priv;
    const uint8_t old = dev->ctrl;
    const uint8_t chg = old ^ val;

    dev->ctrl = val;

    /* The bridge latches on edges. Rewriting the same value is nothing. */
    if (chg == 0x00)
        return;

    /*
       A force-protocol sequence, which the host uses when the two ends
       disagree about the current mode: it addresses control register
       0x04 the long way round, puts the new protocol bits on the data
       lines, and latches them with AUTOFD. An EPP disconnect opens the
       same way but never gets as far as the latch, so a bare return to
       an empty control register means the host was leaving instead.

       This has to be tested before anything else looks at SELECT, since
       the sequence opens by raising it.
     */
    if (dev->force_proto) {
        if (chg & LPT_CTRL_AUTOFD) {
            dev->proto_bits  = dev->dat;
            dev->proto       = ditto_decode_proto_bits(dev->dat);
            dev->force_proto = 0;
            ditto_log("Ditto: protocol forced to %s (bits %02X)\n",
                      ditto_proto_name[dev->proto], dev->proto_bits);
        } else if (val == 0x00)
            ditto_disconnect(dev);

        return;
    }

    if (dev->connected && (val == (LPT_CTRL_STROBE | LPT_CTRL_SELECT))) {
        dev->force_proto = 1;
        return;
    }

    /*
       The connect knock. An absolute write of INIT on its own is the
       host squaring up, and the three SELECT edges that follow latch the
       link up. The count guards against re-arming part way through: the
       second of those three edges also lands on INIT alone, and reading
       that as a fresh start would leave the bridge one edge short.
     */
    if (!dev->connected && (val == LPT_CTRL_INIT) && (dev->knock == 0)) {
        dev->knock_armed = 1;
        dev->saved_ctrl  = old;
        dev->saved_dat   = dev->dat;
        return;
    }

    if (chg & LPT_CTRL_SELECT) {
        /*
           Once the link is up the host drives nothing but STROBE,
           AUTOFD, INIT and DIRECTION - SELECT is left alone for as long
           as the conversation lasts. So SELECT rising is the host
           letting go: it ends both the ordinary disconnect, which raises
           it together with INIT, and the state machine reset that
           follows a protocol query.

           This has to be a signal the host cannot give by accident, and
           a plain value match is not one: the control register passes
           through AUTOFD-alone in the ordinary course of addressing
           registers, so reading that as a disconnect drops the link in
           the middle of a transfer.
         */
        if (dev->connected) {
            if (val & LPT_CTRL_SELECT)
                ditto_disconnect(dev);
        } else if (dev->knock_armed && (++dev->knock >= 3))
            ditto_connect(dev);

        return;
    }

    if (!dev->connected)
        return;

    /* The connect response is read out between two toggles of AUTOFD. */
    if (dev->connect_resp > 0) {
        if (chg & LPT_CTRL_AUTOFD)
            dev->connect_resp--;
        return;
    }

    /*
       With the link up, three edges carry everything: AUTOFD latches the
       register number off the data lines, and INIT clocks a byte - into
       the bridge when STROBE is set, out of it when STROBE is clear.
     */
    /*
       STROBE dropping closes a burst write into the buffer, so that the
       next one has to prime its lines afresh. Only the implicit,
       data-change half of the commit rule is gated this way: an INIT
       toggle is the host committing a byte outright, and is honoured
       whenever the FIFO is the addressed register.
     */
    if ((chg & LPT_CTRL_STROBE) && !(val & LPT_CTRL_STROBE))
        dev->mem_burst = 0;

    if (chg & LPT_CTRL_AUTOFD) {
        dev->cur_reg   = dev->dat;
        dev->xfer_idx  = 0;
        dev->rd_high   = 0;
        dev->mem_burst = 0;
        return;
    }

    if (chg & LPT_CTRL_INIT) {
        if (val & LPT_CTRL_STROBE)
            backpack_write_reg(dev, dev->cur_reg, dev->dat);
        else
            ditto_advance_read(dev);
    }
}

static uint8_t
ditto_read_status(void *priv)
{
    const ditto_t *dev = (const ditto_t *) priv;
    uint8_t        ret;

    if (dev->connect_resp > 0)
        ret = (dev->proto >= DITTO_PROTO_EPP8) ? (LPT_STAT_ACK | LPT_STAT_BUSY)
                                               : LPT_STAT_ACK;
    else if (dev->connected)
        ret = dev->rd_out;
    else
        /* Not connected: the bridge leaves the status lines alone. */
        ret = 0x00;

    return ret & LPT_STAT_MASK;
}

/* --------------------------------------------------------------------- */
/* 86Box device plumbing                                                 */
/* --------------------------------------------------------------------- */

static void *
ditto_init(UNUSED(const device_t *info))
{
    ditto_t    *dev = calloc(1, sizeof(ditto_t));
    const char *fn;

    if (dev == NULL)
        return NULL;

    dev->max_proto = device_get_config_int("protocol");
    if ((dev->max_proto < DITTO_PROTO_SPP) || (dev->max_proto > DITTO_PROTO_EPP32))
        dev->max_proto = DITTO_PROTO_SPP;

    dev->readonly = device_get_config_int("writeprot");

    fn = device_get_config_string("image");
    if (fn != NULL)
        strncpy(dev->image_fn, fn, sizeof(dev->image_fn) - 1);

    dev->buffer = calloc(1, DITTO_BUFFER_SIZE);
    if (dev->buffer == NULL) {
        free(dev);
        return NULL;
    }

    /* The bridge powers up in SPP, the one mode every port can manage. */
    dev->proto      = DITTO_PROTO_SPP;
    dev->proto_bits = DITTO_PROTO_BITS_SPP | DITTO_PROTO_BITS_FIXED;
    dev->crc        = DITTO_CRC_INIT;

    dev->lpt = lpt_attach(ditto_write_data, ditto_write_ctrl, NULL,
                          ditto_read_status, NULL, NULL, NULL, dev);
    if (dev->lpt == NULL) {
        /* Another device already has this port. */
        free(dev->buffer);
        free(dev);
        return NULL;
    }

    ditto_log("Ditto: attached, protocol ceiling %s, image \"%s\"%s\n",
              ditto_proto_name[dev->max_proto], dev->image_fn,
              dev->readonly ? " (write protected)" : "");

    return dev;
}

static void
ditto_close(void *priv)
{
    ditto_t *dev = (ditto_t *) priv;

    free(dev->buffer);
    free(dev);
}

// clang-format off
#define DITTO_IMAGE_FILTER "Tape images (*.tap *.dat *.img)|*.tap,*.dat,*.img"

static const device_config_t ditto_config[] = {
    {
        .name           = "image",
        .description    = "Cartridge image",
        .type           = CONFIG_FNAME,
        .default_string = NULL,
        .default_int    = 0,
        .file_filter    = DITTO_IMAGE_FILTER,
        .spinner        = { 0 },
        .selection      = { { 0 } },
        .bios           = { { 0 } }
    },
    {
        .name           = "writeprot",
        .description    = "Write protect cartridge",
        .type           = CONFIG_BINARY,
        .default_string = NULL,
        .default_int    = 0,
        .file_filter    = NULL,
        .spinner        = { 0 },
        .selection      = { { 0 } },
        .bios           = { { 0 } }
    },
    {
        .name           = "protocol",
        .description    = "Maximum transfer protocol",
        .type           = CONFIG_SELECTION,
        .default_string = NULL,
        .default_int    = DITTO_PROTO_SPP,
        .file_filter    = NULL,
        .spinner        = { 0 },
        .selection      = {
            { .description = "SPP (4-bit)",  .value = DITTO_PROTO_SPP   },
            { .description = "PS/2 (8-bit)", .value = DITTO_PROTO_PS2   },
            { .description = "EPP",          .value = DITTO_PROTO_EPP8  },
            { .description = ""                                         }
        },
        .bios           = { { 0 } }
    },
    { .name = "", .description = "", .type = CONFIG_END }
};
// clang-format on

const device_t lpt_ditto_device = {
    .name          = "Iomega Ditto Drive",
    .internal_name = "ditto",
    .flags         = DEVICE_LPT | DEVICE_HOTPLUG,
    .local         = 0,
    .init          = ditto_init,
    .close         = ditto_close,
    .reset         = NULL,
    .available     = NULL,
    .speed_changed = NULL,
    .force_redraw  = NULL,
    .config        = ditto_config
};
