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

/*
   The four bits that actually reach the connector. INTEN and DIRECTION
   never leave the port, and bits 6 and 7 do not exist at all - but the
   host reads the control register back in the middle of an EPP write and
   keeps the result as its shadow, and 86Box hands back 0xc0 in those
   bits. Every control write it makes from then on carries them, so the
   bridge has to look past them.
 */
#define LPT_CTRL_LINES    0x0f

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

/*
   Layer 3: the 765 the bridge puts behind the 0x40 window. The seven
   registers appear in the order they do on a real controller.
 */
#define FDC_REG_SRA  0
#define FDC_REG_SRB  1
#define FDC_REG_DOR  2
#define FDC_REG_TDR  3
#define FDC_REG_MSR  4 /* reading; writing it is the DSR */
#define FDC_REG_FIFO 5
#define FDC_REG_DIR  7 /* reading; writing it is the CCR */
#define FDC_REG_COUNT 8

/* Main status register. */
#define FDC_MSR_RQM 0x80 /* ready to move a byte */
#define FDC_MSR_DIO 0x40 /* set while the direction is controller to host */
#define FDC_MSR_EXM 0x20
#define FDC_MSR_CB  0x10 /* a command is in progress */

/* Digital output register. */
#define FDC_DOR_RESET_NOT 0x04

/* ST0. */
#define FDC_ST0_SEEK_END     0x20
#define FDC_ST0_ABNORMAL     0x40
#define FDC_ST0_INVALID      0x80
#define FDC_ST0_READY_CHANGE 0xc0

/* ST1. */
#define FDC_ST1_MISSING_AM 0x01
#define FDC_ST1_NO_DATA    0x04

/*
   ST3. Bit 4 is nominally "the head is at track zero", but on a QIC-117
   drive it is the answer line the drive shifts its replies out on - see
   the report machinery in layer 4.
 */
#define FDC_ST3_TWO_SIDE 0x08
#define FDC_ST3_TRACK_0  0x10
#define FDC_ST3_READY    0x20
#define FDC_ST3_WP       0x40

/* What the bridge's controller answers a VERSION command with. */
#define FDC_VERSION_82077 0x90

/* A reset makes the controller poll all four drive slots in turn. */
#define FDC_POLL_DRIVES 4

enum {
    FDC_PHASE_IDLE = 0,
    FDC_PHASE_COMMAND,
    FDC_PHASE_RESULT
};

/*
   Layer 4: the QIC-117 command set, as this drive's firmware implements
   it. Commands arrive as that many step pulses, parameters as the value
   plus two, and everything the drive says back goes out one bit at a
   time on the answer line.
 */
#define QIC_NO_COMMAND                  0
#define QIC_SOFT_RESET                  1
#define QIC_REPORT_NEXT_BIT             2
#define QIC_REPORT_DRIVE_STATUS         6
#define QIC_REPORT_ERROR_CODE           7
#define QIC_REPORT_DRIVE_CONFIGURATION  8
#define QIC_REPORT_ROM_VERSION          9
#define QIC_SEEK_HEAD_TO_TRACK          13
#define QIC_ENTER_FORMAT_MODE           15
#define QIC_ENTER_VERIFY_MODE           17
#define QIC_SKIP_REVERSE                25
#define QIC_SKIP_FORWARD                26
#define QIC_SELECT_RATE                 27
#define QIC_ENTER_DIAGNOSTIC_2          29
#define QIC_ENTER_PRIMARY_MODE          30
#define QIC_REPORT_VENDOR_ID            32
#define QIC_REPORT_TAPE_STATUS          33
#define QIC_SKIP_EXTENDED_REVERSE       34
#define QIC_SKIP_EXTENDED_FORWARD       35
#define QIC_REPORT_FORMAT_SEGMENTS      37
#define QIC_SET_FORMAT_SEGMENTS         38
#define QIC_EXT_SELECT_RATE             50
#define QIC_EXT_REPORT_DRIVE_CONFIG     51
#define QIC_LOADER_PARTITION_STATUS     54
#define QIC_SEEK_TO_PARTITION           55

/* This firmware refuses anything past the extended set outright. */
#define QIC_LAST_COMMAND                56

/* Drive status, as command 6 reports it. */
#define QIC_STATUS_READY             0x01
#define QIC_STATUS_ERROR             0x02
#define QIC_STATUS_CARTRIDGE_PRESENT 0x04
#define QIC_STATUS_WRITE_PROTECT     0x08
#define QIC_STATUS_NEW_CARTRIDGE     0x10
#define QIC_STATUS_REFERENCED        0x20
#define QIC_STATUS_AT_BOT            0x40
#define QIC_STATUS_AT_EOT            0x80

/* Drive configuration, as command 8 reports it. */
#define QIC_CONFIG_RATE_MASK  0x18
#define QIC_CONFIG_RATE_SHIFT 3
#define QIC_CONFIG_RATE_500   2
#define QIC_CONFIG_RATE_1000  3
#define QIC_CONFIG_RATE_2000  1
#define QIC_CONFIG_LONG       0x40
#define QIC_CONFIG_80         0x80

/* Tape status, as command 33 reports it. */
#define QIC_TAPE_QIC3020 0x03
#define QIC_TAPE_QIC3010 0x04
#define QIC_TAPE_FLEX    0x60
#define QIC_TAPE_WIDE    0x80

/* Error codes this firmware raises. */
#define QIC_ERROR_UNDEFINED_COMMAND         6
#define QIC_ERROR_ILLEGAL_COMMAND_IN_REPORT 8
#define QIC_ERROR_POWER_ON_RESET            26
#define QIC_ERROR_RATE_SELECTION            31

/* Operating modes, tracked so the host's mode commands mean something. */
enum {
    QIC_MODE_PRIMARY = 0,
    QIC_MODE_FORMAT,
    QIC_MODE_VERIFY
};

/*
   What this drive says it is. The vendor identifier is the one ftape's
   table names "Iomega DITTO MAX", and is the single value the host keys
   its model-specific behaviour off.
 */
#define DITTO_VENDOR_ID  0x8885
#define DITTO_ROM_VERSION 0x41

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
    int     latching;     /* an out-of-band register write is under way */
    int     latch_commits; /* bytes it has committed so far */

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

    /* Layer 3: the bridge's own 765. */
    uint8_t  fdc_dor;
    uint8_t  fdc_tdr;
    uint8_t  fdc_dsr;
    uint8_t  fdc_ccr;
    uint8_t  fdc_specify[2];
    uint8_t  fdc_config[3];
    uint8_t  fdc_perp;
    int      fdc_locked;
    int      fdc_phase;
    uint8_t  fdc_cmd[16];
    int      fdc_cmd_len;
    int      fdc_cmd_pos;
    uint8_t  fdc_res[16];
    int      fdc_res_len;
    int      fdc_res_pos;
    uint8_t  fdc_st0;
    uint8_t  fdc_pcn;    /* cylinder the controller believes it is on */
    int      fdc_int;    /* the controller has an interrupt outstanding */
    int      fdc_poll;   /* drive-poll results a reset still owes */

    /* Layer 4: the drive itself. */
    int      qic_pulses;    /* length of the last pulse train */
    int      qic_trains;    /* how many trains have arrived */
    int      qic_ack;       /* state of the answer line */
    uint8_t  qic_status;
    uint8_t  qic_error;
    uint8_t  qic_error_cmd;
    uint8_t  qic_last_cmd;
    uint8_t  qic_config;
    uint8_t  qic_tape_status;
    uint8_t  qic_ext_rate;  /* the selected rate, in Mbit/s */
    uint8_t  qic_format_code;
    uint8_t  qic_partition;
    uint8_t  qic_mode;
    uint16_t qic_vendor_id;
    uint16_t qic_format_segments;
    int      image_loaded;

    /* Command and parameter decoding. */
    int      qic_params_left;
    int      qic_params_got;
    uint8_t  qic_param[3];
    uint8_t  qic_param_cmd;

    /* The report shifter. */
    int      report_pending;
    int      report_bits;   /* payload bits left, plus the stop bit */
    int      report_len;
    uint8_t  report_shift;
    uint8_t  report_hi;
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

/* --------------------------------------------------------------------- */
/* Layer 4: the QIC-117 drive                                            */
/* --------------------------------------------------------------------- */

static int
ditto_qic_answer(const ditto_t *dev)
{
    return dev->qic_ack;
}

static void
qic_set_error(ditto_t *dev, uint8_t error, uint8_t command)
{
    /* Only the first error is kept: the host has to read it to clear it. */
    if (dev->qic_status & QIC_STATUS_ERROR)
        return;

    dev->qic_status   |= QIC_STATUS_ERROR;
    dev->qic_error     = error;
    dev->qic_error_cmd = command;

    ditto_log("Ditto: QIC-117 error %i on command %i\n", error, command);
}

/*
   Arms a report. The drive raises the answer line as an acknowledge, and
   the host - which has been polling ST3 waiting for exactly that - then
   clocks the payload out of it a bit at a time.
 */
static void
qic_report_arm(ditto_t *dev, uint16_t value, int len)
{
    dev->report_len   = len;
    dev->report_shift = (uint8_t) (value & 0xff);
    dev->report_hi    = (uint8_t) (value >> 8);

    /* One more than the payload: the report closes with a stop bit. */
    dev->report_bits    = len + 1;
    dev->report_pending = 1;

    dev->qic_ack = 1;

    ditto_log("Ditto: QIC-117 report of %04X, %i bits\n", value, len);
}

/*
   Hands over the next bit. The payload goes out least significant bit
   first, a sixteen bit report as two bytes with the low one first, and
   the whole thing closes with a one - which is how the host tells a
   finished report from a drive that has stopped answering. One further
   request after that puts the line back down.
 */
static void
qic_report_next_bit(ditto_t *dev)
{
    if (!dev->report_pending) {
        dev->qic_ack = 0;
        return;
    }

    if (dev->report_bits <= 0) {
        dev->report_pending = 0;
        dev->qic_ack        = 0;
        return;
    }

    dev->report_bits--;
    dev->qic_ack       = dev->report_shift & 0x01;
    dev->report_shift >>= 1;

    if ((dev->report_bits == 9) && (dev->report_len == 16))
        dev->report_shift = dev->report_hi;
    else if (dev->report_bits == 1)
        dev->report_shift = 0x01; /* the stop bit */
}

/* The drive status byte, as command 6 reports it. */
static uint8_t
qic_drive_status(const ditto_t *dev)
{
    uint8_t ret = dev->qic_status;

    /* A drive that is not ready reports only what it is busy doing. */
    if (!(ret & QIC_STATUS_READY))
        ret &= 0x3c;

    return ret;
}

/*
   The drive configuration byte. The rate field is deliberately masked
   out of it - this drive reports its rate through the extended command
   instead, and a host that knows the model asks there.
 */
static uint8_t
qic_drive_config(const ditto_t *dev)
{
    return (uint8_t) (dev->qic_config & ~QIC_CONFIG_RATE_MASK);
}

/* How many parameters a command expects after it. */
static int
qic_param_count(int cmd)
{
    switch (cmd) {
        case QIC_SEEK_HEAD_TO_TRACK:
        case QIC_ENTER_DIAGNOSTIC_2:
            return 2;

        case QIC_SKIP_REVERSE:
        case QIC_SKIP_FORWARD:
        case QIC_SELECT_RATE:
        case QIC_SKIP_EXTENDED_REVERSE:
        case QIC_SKIP_EXTENDED_FORWARD:
        case QIC_SET_FORMAT_SEGMENTS:
        case QIC_EXT_SELECT_RATE:
        case QIC_SEEK_TO_PARTITION:
            return 1;

        default:
            break;
    }

    return 0;
}

/* Runs a command that has all the parameters it needs. */
static void
qic_run_command(ditto_t *dev, int cmd)
{
    dev->qic_last_cmd = (uint8_t) cmd;

    switch (cmd) {
        case QIC_SOFT_RESET:
            /*
               Back to how the drive came up, which includes the power on
               reset the host must read out before it will be believed.
             */
            dev->qic_status     = QIC_STATUS_READY;
            dev->report_pending = 0;
            dev->qic_ack        = 0;
            if (dev->image_loaded)
                dev->qic_status |= QIC_STATUS_CARTRIDGE_PRESENT;
            if (dev->readonly)
                dev->qic_status |= QIC_STATUS_WRITE_PROTECT;
            dev->qic_error     = 0;
            dev->qic_error_cmd = 0;
            qic_set_error(dev, QIC_ERROR_POWER_ON_RESET, QIC_NO_COMMAND);
            break;

        case QIC_REPORT_DRIVE_STATUS:
            qic_report_arm(dev, qic_drive_status(dev), 8);
            break;

        case QIC_REPORT_ERROR_CODE:
            /*
               Sixteen bits: the code, then the command that provoked it.
               Reading it is what clears the error - and the new cartridge
               flag with it.
             */
            qic_report_arm(dev,
                           (uint16_t) (dev->qic_error |
                                       (dev->qic_error_cmd << 8)), 16);
            dev->qic_status &= (uint8_t) ~(QIC_STATUS_ERROR |
                                           QIC_STATUS_NEW_CARTRIDGE);
            dev->qic_error     = 0;
            dev->qic_error_cmd = 0;
            break;

        case QIC_REPORT_DRIVE_CONFIGURATION:
            qic_report_arm(dev, qic_drive_config(dev), 8);
            break;

        case QIC_REPORT_ROM_VERSION:
            qic_report_arm(dev, DITTO_ROM_VERSION, 8);
            break;

        case QIC_REPORT_VENDOR_ID:
            qic_report_arm(dev, dev->qic_vendor_id, 16);
            break;

        case QIC_REPORT_TAPE_STATUS:
            qic_report_arm(dev, dev->qic_tape_status, 8);
            break;

        case QIC_REPORT_FORMAT_SEGMENTS:
            qic_report_arm(dev, dev->qic_format_segments, 16);
            break;

        case QIC_ENTER_PRIMARY_MODE:
            dev->qic_mode = QIC_MODE_PRIMARY;
            break;

        case QIC_ENTER_FORMAT_MODE:
            dev->qic_mode = QIC_MODE_FORMAT;
            break;

        case QIC_ENTER_VERIFY_MODE:
            dev->qic_mode = QIC_MODE_VERIFY;
            break;

        case QIC_SELECT_RATE:
            /*
               The plain rate selection only knows the two slow rates.
               Everything else it is offered is a format selector, and a
               value that is neither is refused.
             */
            switch (dev->qic_param[0]) {
                case 1:
                    dev->qic_config   = (uint8_t) ((dev->qic_config & ~QIC_CONFIG_RATE_MASK) |
                                                   (QIC_CONFIG_RATE_2000 << QIC_CONFIG_RATE_SHIFT));
                    dev->qic_ext_rate = 2;
                    break;

                case 3:
                    dev->qic_config   = (uint8_t) ((dev->qic_config & ~QIC_CONFIG_RATE_MASK) |
                                                   (QIC_CONFIG_RATE_1000 << QIC_CONFIG_RATE_SHIFT));
                    dev->qic_ext_rate = 1;
                    break;

                case 13: case 15: case 17: case 19:
                    dev->qic_format_code = dev->qic_param[0];
                    break;

                default:
                    qic_set_error(dev, QIC_ERROR_RATE_SELECTION, (uint8_t) cmd);
                    break;
            }
            break;

        case QIC_EXT_SELECT_RATE:
            if ((dev->qic_param[0] >= 1) && (dev->qic_param[0] <= 5))
                dev->qic_ext_rate = dev->qic_param[0];
            else
                qic_set_error(dev, QIC_ERROR_RATE_SELECTION, (uint8_t) cmd);
            break;

        case QIC_EXT_REPORT_DRIVE_CONFIG:
            /*
               The reason this drive needs the extended command set at
               all: the rate it is running at, in Mbit/s, which the plain
               configuration report masks away.
             */
            qic_report_arm(dev, dev->qic_ext_rate, 8);
            break;

        case QIC_LOADER_PARTITION_STATUS:
            qic_report_arm(dev, dev->qic_partition, 8);
            break;

        case QIC_SET_FORMAT_SEGMENTS:
            dev->qic_format_segments = dev->qic_param[0];
            break;

        default:
            /*
               Everything that moves tape belongs to the next milestone.
               Refusing them outright would be worse than accepting them
               quietly: the host would latch an error and stop.
             */
            ditto_log("Ditto: QIC-117 command %i not implemented yet\n", cmd);
            break;
    }
}

/*
   A pulse train has arrived. Unlike a drive on the floppy cable, this one
   is handed each train whole - the bridge's own controller generates the
   pulses, so there is no gap for the host to interleave anything into and
   nothing to time out waiting for.
 */
static void
ditto_qic_step(ditto_t *dev, int steps)
{
    if (steps <= 0)
        return;

    dev->qic_pulses = steps;
    dev->qic_trains++;

    /*
       While a report is being clocked out, the only thing the drive will
       hear is a request for the next bit. Anything else is the host
       losing its place, and says so.
     */
    if (dev->report_pending) {
        if (steps == QIC_REPORT_NEXT_BIT) {
            qic_report_next_bit(dev);
            return;
        }

        dev->report_pending = 0;
        dev->qic_ack        = 0;
        qic_set_error(dev, QIC_ERROR_ILLEGAL_COMMAND_IN_REPORT, (uint8_t) steps);
        return;
    }

    /*
       Parameters arrive biased by two, so that a parameter of zero
       cannot be mistaken for the empty pulse train that means nothing at
       all was sent.
     */
    if (dev->qic_params_left > 0) {
        if (steps < 2) {
            dev->qic_params_left = 0;
            qic_set_error(dev, QIC_ERROR_UNDEFINED_COMMAND, dev->qic_param_cmd);
            return;
        }

        if (dev->qic_params_got < (int) sizeof(dev->qic_param))
            dev->qic_param[dev->qic_params_got] = (uint8_t) (steps - 2);
        dev->qic_params_got++;
        dev->qic_params_left--;

        if (dev->qic_params_left == 0)
            qic_run_command(dev, dev->qic_param_cmd);

        return;
    }

    /* A request for the next bit with nothing armed just drops the line. */
    if (steps == QIC_REPORT_NEXT_BIT) {
        dev->qic_ack = 0;
        return;
    }

    if (steps > QIC_LAST_COMMAND) {
        qic_set_error(dev, QIC_ERROR_UNDEFINED_COMMAND, (uint8_t) steps);
        return;
    }

    dev->qic_params_left = qic_param_count(steps);
    if (dev->qic_params_left > 0) {
        dev->qic_param_cmd  = (uint8_t) steps;
        dev->qic_params_got = 0;
        return;
    }

    qic_run_command(dev, steps);
}

/* --------------------------------------------------------------------- */
/* Layer 3: the 765 behind the register window                           */
/* --------------------------------------------------------------------- */

/* The controller's interrupt is reported the same way every other one is:
   through the bridge's status register, and out on the wire only once the
   host has let go. */
static void
fdc_raise_irq(ditto_t *dev)
{
    dev->fdc_int     = 1;
    dev->irq_pending = 1;

    ditto_update_irq(dev, 0);
}

static void
fdc_clear_irq(ditto_t *dev)
{
    dev->fdc_int     = 0;
    dev->irq_pending = 0;

    ditto_update_irq(dev, 0);
}

/*
   How many bytes a command takes. The read and write family carry MT,
   MFM and SK in their top bits so they have to be masked, but the rest
   are whole opcodes - and some of them differ only in those same top
   bits, LOCK and UNLOCK being the pair that matters here.

   A command that is not listed is not one this controller has, and gets
   the invalid-command answer.
 */
static int
fdc_cmd_length(uint8_t cmd)
{
    switch (cmd) {
        case 0x03: /* SPECIFY */
            return 3;
        case 0x04: /* SENSE DRIVE STATUS */
        case 0x07: /* RECALIBRATE */
            return 2;
        case 0x08: /* SENSE INTERRUPT STATUS */
        case 0x0e: /* DUMPREG */
        case 0x10: /* VERSION */
        case 0x14: /* UNLOCK */
        case 0x94: /* LOCK */
            return 1;
        case 0x0f: /* SEEK */
            return 3;
        case 0x12: /* PERPENDICULAR MODE */
            return 2;
        case 0x13: /* CONFIGURE */
            return 4;

        default:
            break;
    }

    switch (cmd & 0x1f) {
        case 0x02:              /* READ TRACK */
        case 0x05: case 0x09:   /* WRITE DATA, WRITE DELETED DATA */
        case 0x06: case 0x0c:   /* READ DATA, READ DELETED DATA */
        case 0x11: case 0x16:   /* SCAN EQUAL, VERIFY */
            return 9;
        case 0x0a:              /* READ ID */
            return 2;
        case 0x0d:              /* FORMAT TRACK */
            return 6;

        default:
            break;
    }

    return -1;
}

static uint8_t
fdc_read_st3(const ditto_t *dev)
{
    uint8_t ret = FDC_ST3_TWO_SIDE | FDC_ST3_READY;

    ret |= (uint8_t) (dev->fdc_cmd[1] & 0x03);

    /*
       The drive answers a QIC-117 report on this line rather than
       telling the controller where its head is. Everything the host
       ever reads back out of the drive comes through here, one bit at
       a time.
     */
    if (ditto_qic_answer(dev))
        ret |= FDC_ST3_TRACK_0;

    if (dev->readonly)
        ret |= FDC_ST3_WP;

    return ret;
}

static void
fdc_begin_result(ditto_t *dev, int len)
{
    dev->fdc_res_len = len;
    dev->fdc_res_pos = 0;
    dev->fdc_phase   = (len > 0) ? FDC_PHASE_RESULT : FDC_PHASE_IDLE;
}

/* Fills in the seven bytes a data command ends with. */
static void
fdc_data_result(ditto_t *dev, uint8_t st0, uint8_t st1, uint8_t st2)
{
    dev->fdc_res[0] = st0;
    dev->fdc_res[1] = st1;
    dev->fdc_res[2] = st2;
    dev->fdc_res[3] = dev->fdc_cmd[2]; /* C */
    dev->fdc_res[4] = dev->fdc_cmd[3]; /* H */
    dev->fdc_res[5] = dev->fdc_cmd[4]; /* R */
    dev->fdc_res[6] = dev->fdc_cmd[5]; /* N */

    fdc_begin_result(dev, 7);
}

static void
fdc_execute(ditto_t *dev)
{
    const uint8_t cmd  = dev->fdc_cmd[0];
    const uint8_t unit = (uint8_t) (dev->fdc_cmd[1] & 0x03);
    int           steps;

    switch (cmd) {
        case 0x03: /* SPECIFY */
            dev->fdc_specify[0] = dev->fdc_cmd[1];
            dev->fdc_specify[1] = dev->fdc_cmd[2];
            fdc_begin_result(dev, 0);
            break;

        case 0x04: /* SENSE DRIVE STATUS */
            dev->fdc_res[0] = fdc_read_st3(dev);
            fdc_begin_result(dev, 1);
            break;

        case 0x07: /* RECALIBRATE */
            /*
               A real controller would step until the drive raised TRACK
               0, but on this bus that line is the answer line and the
               steps would be read as a command. So take the head home
               on paper only. The host does the same: it reads the new
               cylinder back out of the interrupt status.
             */
            ditto_log("Ditto: FDC recalibrate, no pulses sent\n");
            dev->fdc_pcn = 0;
            dev->fdc_st0 = (uint8_t) (FDC_ST0_SEEK_END | unit);
            fdc_begin_result(dev, 0);
            fdc_raise_irq(dev);
            break;

        case 0x08: /* SENSE INTERRUPT STATUS */
            if (dev->fdc_poll > 0) {
                /* A reset polls each drive slot in turn. */
                dev->fdc_res[0] = (uint8_t) (FDC_ST0_READY_CHANGE |
                                             (FDC_POLL_DRIVES - dev->fdc_poll));
                dev->fdc_res[1] = dev->fdc_pcn;
                dev->fdc_poll--;
                if (dev->fdc_poll == 0)
                    fdc_clear_irq(dev);
                fdc_begin_result(dev, 2);
            } else if (dev->fdc_int) {
                dev->fdc_res[0] = dev->fdc_st0;
                dev->fdc_res[1] = dev->fdc_pcn;
                fdc_clear_irq(dev);
                fdc_begin_result(dev, 2);
            } else {
                /* Nothing outstanding. The host reads this as "no more". */
                dev->fdc_res[0] = FDC_ST0_INVALID;
                fdc_begin_result(dev, 1);
            }
            break;

        case 0x0e: /* DUMPREG */
            dev->fdc_res[0] = dev->fdc_pcn;
            dev->fdc_res[1] = 0x00;
            dev->fdc_res[2] = 0x00;
            dev->fdc_res[3] = 0x00;
            dev->fdc_res[4] = dev->fdc_specify[0];
            dev->fdc_res[5] = dev->fdc_specify[1];
            dev->fdc_res[6] = 0x00; /* sectors per cylinder, last used */
            dev->fdc_res[7] = (uint8_t) ((dev->fdc_locked ? 0x80 : 0x00) |
                                         (dev->fdc_perp & 0x7f));
            dev->fdc_res[8] = dev->fdc_config[1];
            dev->fdc_res[9] = dev->fdc_config[2];
            fdc_begin_result(dev, 10);
            break;

        case 0x0f: /* SEEK */
            /*
               This is the QIC-117 command channel. A seek of N cylinders
               steps the drive N times, and the drive counts the pulses
               to work out what it has been asked to do - so the distance
               is the message, and where the head ends up is beside the
               point.

               Nothing here models the time the pulses take: the bridge
               generates them itself, out of the host's sight, so there
               is no train for the host to interleave anything with.
             */
            steps = (int) dev->fdc_cmd[2] - (int) dev->fdc_pcn;
            if (steps < 0)
                steps = -steps;

            ditto_qic_step(dev, steps);

            dev->fdc_pcn = dev->fdc_cmd[2];
            dev->fdc_st0 = (uint8_t) (FDC_ST0_SEEK_END | unit);
            fdc_begin_result(dev, 0);
            fdc_raise_irq(dev);
            break;

        case 0x10: /* VERSION */
            dev->fdc_res[0] = FDC_VERSION_82077;
            fdc_begin_result(dev, 1);
            break;

        case 0x12: /* PERPENDICULAR MODE */
            dev->fdc_perp = dev->fdc_cmd[1];
            fdc_begin_result(dev, 0);
            break;

        case 0x13: /* CONFIGURE */
            dev->fdc_config[0] = dev->fdc_cmd[1];
            dev->fdc_config[1] = dev->fdc_cmd[2];
            dev->fdc_config[2] = dev->fdc_cmd[3];
            fdc_begin_result(dev, 0);
            break;

        case 0x14: /* UNLOCK */
        case 0x94: /* LOCK */
            dev->fdc_locked = (cmd & 0x80) ? 1 : 0;
            dev->fdc_res[0] = (uint8_t) (dev->fdc_locked ? 0x10 : 0x00);
            fdc_begin_result(dev, 1);
            break;

        default:
            switch (cmd & 0x1f) {
                case 0x02:
                case 0x05: case 0x09:
                case 0x06: case 0x0c:
                case 0x11: case 0x16:
                case 0x0a:
                case 0x0d:
                    /*
                       The data commands. Moving the data between the
                       cartridge and the bridge's buffer is milestone 8;
                       until then they end the only honest way they can,
                       which is by reporting that there was nothing there
                       to read or write.
                     */
                    ditto_log("Ditto: FDC data command %02X, no medium yet\n", cmd);
                    fdc_data_result(dev,
                                    (uint8_t) (FDC_ST0_ABNORMAL | unit),
                                    FDC_ST1_MISSING_AM | FDC_ST1_NO_DATA,
                                    0x00);
                    fdc_raise_irq(dev);
                    break;

                default:
                    dev->fdc_res[0] = FDC_ST0_INVALID;
                    fdc_begin_result(dev, 1);
                    break;
            }
            break;
    }
}

static uint8_t
fdc_read_msr(const ditto_t *dev)
{
    uint8_t ret;

    /* Held in reset, the controller answers nothing at all. */
    if (!(dev->fdc_dor & FDC_DOR_RESET_NOT))
        return 0x00;

    ret = FDC_MSR_RQM;

    switch (dev->fdc_phase) {
        case FDC_PHASE_COMMAND:
            ret |= FDC_MSR_CB;
            break;

        case FDC_PHASE_RESULT:
            ret |= FDC_MSR_CB | FDC_MSR_DIO;
            break;

        default:
            break;
    }

    return ret;
}

static uint8_t
fdc_read_reg(ditto_t *dev, int reg)
{
    uint8_t ret = 0xff;

    switch (reg) {
        case FDC_REG_MSR:
            ret = fdc_read_msr(dev);
            break;

        case FDC_REG_FIFO:
            if (dev->fdc_phase != FDC_PHASE_RESULT) {
                ditto_log("Ditto: FDC data read outside a result phase\n");
                break;
            }

            ret = dev->fdc_res[dev->fdc_res_pos++];
            if (dev->fdc_res_pos >= dev->fdc_res_len) {
                dev->fdc_phase   = FDC_PHASE_IDLE;
                dev->fdc_res_pos = 0;
                dev->fdc_res_len = 0;
            }
            break;

        case FDC_REG_DOR:
            ret = dev->fdc_dor;
            break;

        case FDC_REG_TDR:
            ret = dev->fdc_tdr;
            break;

        case FDC_REG_DIR:
            /* No disk was ever changed in here. */
            ret = 0x00;
            break;

        default:
            ret = 0x00;
            break;
    }

    return ret;
}

static void
fdc_write_reg(ditto_t *dev, int reg, uint8_t val)
{
    int len;

    switch (reg) {
        case FDC_REG_DOR:
            /*
               Taking the reset line down and back up restarts the
               controller, which then reports a ready change for each of
               the four drive slots before it will do anything else.
             */
            if ((dev->fdc_dor & FDC_DOR_RESET_NOT) && !(val & FDC_DOR_RESET_NOT)) {
                dev->fdc_phase   = FDC_PHASE_IDLE;
                dev->fdc_cmd_pos = dev->fdc_cmd_len = 0;
                dev->fdc_res_pos = dev->fdc_res_len = 0;
                fdc_clear_irq(dev);
            } else if (!(dev->fdc_dor & FDC_DOR_RESET_NOT) && (val & FDC_DOR_RESET_NOT)) {
                dev->fdc_pcn  = 0;
                dev->fdc_poll = FDC_POLL_DRIVES;
                dev->fdc_st0  = FDC_ST0_READY_CHANGE;
                fdc_raise_irq(dev);
                ditto_log("Ditto: FDC out of reset, polling drives\n");
            }
            dev->fdc_dor = val;
            break;

        case FDC_REG_TDR:
            dev->fdc_tdr = val;
            break;

        case FDC_REG_MSR: /* the DSR when written */
            dev->fdc_dsr = val;
            break;

        case FDC_REG_DIR: /* the CCR when written */
            dev->fdc_ccr = val;
            break;

        case FDC_REG_FIFO:
            if (dev->fdc_phase == FDC_PHASE_RESULT) {
                ditto_log("Ditto: FDC command byte during a result phase\n");
                break;
            }

            if (dev->fdc_phase == FDC_PHASE_IDLE) {
                len = fdc_cmd_length(val);
                if (len < 0) {
                    /*
                       An opcode this controller does not have. It
                       answers with the single byte the host recognises
                       as "I do not know that one".
                     */
                    dev->fdc_cmd[0]  = val;
                    dev->fdc_res[0]  = FDC_ST0_INVALID;
                    dev->fdc_cmd_pos = dev->fdc_cmd_len = 0;
                    fdc_begin_result(dev, 1);
                    break;
                }

                dev->fdc_cmd_len = len;
                dev->fdc_cmd_pos = 0;
                dev->fdc_phase   = FDC_PHASE_COMMAND;
            }

            if (dev->fdc_cmd_pos < (int) sizeof(dev->fdc_cmd))
                dev->fdc_cmd[dev->fdc_cmd_pos] = val;
            dev->fdc_cmd_pos++;

            if (dev->fdc_cmd_pos >= dev->fdc_cmd_len) {
                dev->fdc_phase = FDC_PHASE_IDLE;
                fdc_execute(dev);
            }
            break;

        default:
            break;
    }
}

/* --------------------------------------------------------------------- */

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

    if ((reg >= BP_REG_FDC) && (reg < (BP_REG_FDC + FDC_REG_COUNT)))
        return fdc_read_reg(dev, reg - BP_REG_FDC);

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

    if ((reg >= BP_REG_FDC) && (reg < (BP_REG_FDC + FDC_REG_COUNT))) {
        fdc_write_reg(dev, reg - BP_REG_FDC, val);
        return;
    }

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
    dev->latching     = 0;
    dev->rd_out       = 0x00;
    dev->mem_burst    = 0;

    /* Letting go is what lets a pending interrupt reach the host. */
    ditto_update_irq(dev, 0);

    ditto_log("Ditto: disconnected\n");
}

/*
   Hands the next nibble of the addressed register to the host. Every
   second toggle starts a fresh byte, so a host that keeps toggling
   streams the register out without re-addressing it.
 */
/*
   Whether the bridge will talk in the mode it is currently programmed
   for. A mode held back by the configured ceiling answers with all ones
   instead, which is enough to break the host's protocol self test - and
   that is exactly how the host is meant to find out and settle on a
   slower mode it can rely on.
 */
static int
ditto_proto_usable(const ditto_t *dev)
{
    return dev->connected && (dev->proto <= dev->max_proto);
}

static void
ditto_advance_read(ditto_t *dev)
{
    uint8_t val;

    if (!ditto_proto_usable(dev)) {
        ditto_log("Ditto: read refused in %s mode\n", ditto_proto_name[dev->proto]);
        dev->rd_out  = ditto_encode_nibble(0x0f);
        dev->rd_high = !dev->rd_high;
        if (dev->lpt != NULL)
            lpt_write_to_dat(dev->lpt, 0xff);
        return;
    }

    /*
       In byte mode the bridge drives the data lines instead of the four
       status bits, so a whole byte comes back per toggle rather than a
       nibble. The host has already turned the port around.
     */
    if (dev->proto == DITTO_PROTO_PS2) {
        val         = backpack_read_reg(dev, dev->cur_reg);
        dev->rd_out = 0x00;
        if (dev->lpt != NULL)
            lpt_write_to_dat(dev->lpt, val);
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

/*
   EPP mode moves the addressing and the data off the control lines
   altogether and onto the port's own EPP address and data registers, so
   none of the toggling above applies. A wide EPP read arrives here as
   consecutive byte reads, which is what the bridge would see too.
 */
static void
ditto_epp_write_data(uint8_t is_addr, uint8_t val, void *priv)
{
    ditto_t *dev = (ditto_t *) priv;

    if (!ditto_proto_usable(dev) || (dev->proto < DITTO_PROTO_EPP8)) {
        ditto_log("Ditto: EPP write ignored in %s mode\n",
                  ditto_proto_name[dev->proto]);
        return;
    }

    if (is_addr) {
        dev->cur_reg   = val;
        dev->xfer_idx  = 0;
        dev->rd_high   = 0;
        dev->mem_burst = 0;
    } else
        backpack_write_reg(dev, dev->cur_reg, val);
}

static void
ditto_epp_request_read(uint8_t is_addr, void *priv)
{
    ditto_t *dev = (ditto_t *) priv;
    uint8_t  val = 0xff;

    if (!is_addr && ditto_proto_usable(dev) && (dev->proto >= DITTO_PROTO_EPP8))
        val = backpack_read_reg(dev, dev->cur_reg);

    if (dev->lpt != NULL)
        lpt_write_to_dat(dev->lpt, val);
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
    ditto_t      *dev   = (ditto_t *) priv;
    const uint8_t old   = dev->ctrl;
    const uint8_t chg   = (uint8_t) ((old ^ val) & LPT_CTRL_LINES);
    const uint8_t lines = (uint8_t) (val & LPT_CTRL_LINES);

    dev->ctrl = val;

    /* The bridge latches on edges. Rewriting the same value is nothing. */
    if (chg == 0x00)
        return;

    /*
       An out-of-band register write, addressed off the data lines while
       SELECT is raised. The host uses it for two quite different things
       and they turn out to be the same mechanism:

         - forcing a protocol when the two ends disagree about the
           current one, which it does by writing the protocol bits to
           control register 0x04 this way rather than the ordinary way;
         - reaching register 0x13 in EPP mode to seed the protocol self
           test, where the ordinary EPP addressing is what is in doubt.

       Raising SELECT latches the register number that is on the data
       lines; each toggle of AUTOFD afterwards commits whatever is on
       them now; and STROBE dropping ends it, leaving that register
       addressed so the reads that follow come from it.

       An EPP disconnect opens the very same way but never commits
       anything, so a close with nothing committed is the host leaving
       rather than writing. All of this has to be tested before anything
       else looks at SELECT.
     */
    if (dev->latching) {
        if (chg & LPT_CTRL_AUTOFD) {
            backpack_write_reg(dev, dev->cur_reg, dev->dat);
            dev->latch_commits++;
        }

        if (!(lines & LPT_CTRL_STROBE)) {
            dev->latching = 0;
            if (dev->latch_commits == 0)
                ditto_disconnect(dev);
        }

        return;
    }

    if (dev->connected && (lines == (LPT_CTRL_STROBE | LPT_CTRL_SELECT))) {
        dev->latching      = 1;
        dev->latch_commits = 0;
        dev->cur_reg       = dev->dat;
        dev->xfer_idx      = 0;
        dev->rd_high       = 0;
        dev->mem_burst     = 0;
        return;
    }

    /*
       The connect knock. An absolute write of INIT on its own is the
       host squaring up, and the three SELECT edges that follow latch the
       link up. The count guards against re-arming part way through: the
       second of those three edges also lands on INIT alone, and reading
       that as a fresh start would leave the bridge one edge short.
     */
    if (!dev->connected && (lines == LPT_CTRL_INIT) && (dev->knock == 0)) {
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
            if (lines & LPT_CTRL_SELECT)
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
    if ((chg & LPT_CTRL_STROBE) && !(lines & LPT_CTRL_STROBE))
        dev->mem_burst = 0;

    if (chg & LPT_CTRL_AUTOFD) {
        dev->cur_reg   = dev->dat;
        dev->xfer_idx  = 0;
        dev->rd_high   = 0;
        dev->mem_burst = 0;
        return;
    }

    if (chg & LPT_CTRL_INIT) {
        if (lines & LPT_CTRL_STROBE)
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

    /* The controller comes up out of reset, as the host expects to find it. */
    dev->fdc_dor = FDC_DOR_RESET_NOT;

    /*
       The drive's identity, and the state it powers up in. A cartridge
       that was already in the drive is not a new one, but the power on
       reset itself has to be read out before the drive will do anything
       else - that is how the host learns it has been reset.
     */
    dev->qic_vendor_id   = DITTO_VENDOR_ID;
    dev->qic_config      = QIC_CONFIG_80 | QIC_CONFIG_LONG;
    dev->qic_tape_status = QIC_TAPE_QIC3020 | QIC_TAPE_FLEX | QIC_TAPE_WIDE;
    dev->qic_ext_rate    = 2;
    dev->qic_mode        = QIC_MODE_PRIMARY;
    dev->qic_status      = QIC_STATUS_READY;
    if (dev->image_loaded)
        dev->qic_status |= QIC_STATUS_CARTRIDGE_PRESENT;
    if (dev->readonly)
        dev->qic_status |= QIC_STATUS_WRITE_PROTECT;

    qic_set_error(dev, QIC_ERROR_POWER_ON_RESET, QIC_NO_COMMAND);

    dev->lpt = lpt_attach(ditto_write_data, ditto_write_ctrl, NULL,
                          ditto_read_status, NULL,
                          ditto_epp_write_data, ditto_epp_request_read, dev);
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
        /*
           A ceiling, not a choice: the host probes upwards from SPP and
           keeps the fastest mode that works, so this only caps how far
           it gets. Lower it to make a trace readable, or to work around
           a port whose faster modes misbehave.
         */
        .name           = "protocol",
        .description    = "Maximum transfer protocol",
        .type           = CONFIG_SELECTION,
        .default_string = NULL,
        .default_int    = DITTO_PROTO_EPP8,
        .file_filter    = NULL,
        .spinner        = { 0 },
        .selection      = {
            { .description = "EPP",          .value = DITTO_PROTO_EPP8  },
            { .description = "PS/2 (8-bit)", .value = DITTO_PROTO_PS2   },
            { .description = "SPP (4-bit)",  .value = DITTO_PROTO_SPP   },
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
