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
 *          All four are implemented here. Iomega's own Windows 95 tape
 *          software drives it end to end - format, backup and restore -
 *          as does DOS QBACKUP.
 *
 *          Three things about the host are worth knowing before changing
 *          anything, because each of them broke a whole operation and
 *          none of them is guessable from the specifications:
 *
 *            - a controller reset empties the QIC-117 command channel.
 *              The channel is a count of step pulses measured from the
 *              cylinder the controller believes it is on, and the reset
 *              moves that belief to zero, so the host starts its next
 *              command from a standing start (see fdc_write_reg);
 *            - the skip counts are a fixed number of nibbles, two for
 *              the plain skips and three for the extended ones, whatever
 *              the distance (see qic117_command_params in
 *              tape_qic117.h);
 *            - a transfer has to take as long as the tape it covers
 *              takes to pass the head. Finishing it the instant the last
 *              command byte lands reads to the host as a refusal (see
 *              fdc_begin_exec).
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
#include <86box/fdd_tape.h>
#include <86box/plat.h>
#include <86box/tape_qic117.h>

/*
   For logging the BackPack wire protocol into the emulator log. Turn it
   on while working out what a driver does: run 86Box with -L <file> and
   the whole conversation with the port lands in it, which is the only
   way to see what a host actually does rather than what it was expected
   to do.

   It is cheaper than it looks and the arithmetic is worth writing down,
   because guessing at it wasted a lot of time here. A line costs a
   little under two microseconds - an allocation, a copy and a flushed
   write. The host moves a byte in about two and a half port accesses, so
   a backup at its fastest logs a few thousand lines a second and the
   trace costs on the order of one percent. Judge it by lines per second,
   not by how slow the emulator feels; those are only the same number
   when the host is busy.

   What it does cost is disk: a whole backup is several hundred megabytes
   of trace.
 */
#define ENABLE_LPT_DITTO_LOG 0

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
   What the status lines read as when the bridge is not driving them.

   This matters more than it looks. Until the host has knocked, the pod
   leaves the lines alone and the host reads the idle state of the port
   itself - the same thing 86Box hands back for a port with nothing on
   it at all. Answering all zeroes instead says every line is asserted at
   once: busy, out of paper, off line and faulted. A host that looks at
   the port before it tries to talk can reasonably decide from that that
   there is nothing here worth talking to, and never knock at all.
 */
#define LPT_STAT_IDLE     0xd8

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

/* Register 0x34, the ECC coprocessor. */
#define BP_ECC_GEN   0x80 /* work the parity out when writing */
#define BP_ECC_CHECK 0x40 /* only say whether it is right when reading */

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
#define FDC_ST1_MISSING_AM   0x01
#define FDC_ST1_WRITE_PROTECT 0x02
#define FDC_ST1_NO_DATA      0x04

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
    FDC_PHASE_EXEC,
    FDC_PHASE_RESULT
};

/*
   Layer 4: the QIC-117 command set, as this drive's firmware implements
   it. Commands arrive as that many step pulses, parameters as the value
   plus two, and everything the drive says back goes out one bit at a
   time on the answer line.
 */
#define QIC_NO_COMMAND                  0

/* This firmware refuses anything past the extended set outright. */
#define QIC_LAST_COMMAND                56





/*
   The operating mode byte. Commands carry an interlock mask that is
   tested against it, so that a command which makes no sense in the mode
   the drive is in is refused rather than half done.
 */
#define QIC_MODE_HIGH_SPEED  0x01
#define QIC_MODE_NON_INTR    0x02
#define QIC_MODE_FORMAT      0x04
#define QIC_MODE_VERIFY      0x08
#define QIC_MODE_PRIMARY     0x10
#define QIC_MODE_DIAG_FAILED 0x80

/* The three mode commands are exclusive: each clears the other two. */
#define QIC_MODE_EXCLUSIVE (QIC_MODE_FORMAT | QIC_MODE_VERIFY | QIC_MODE_PRIMARY)


/*
   What this drive says it is. Every host reads this once, with Report
   Vendor ID, and keys its model-specific behaviour off it. The low six
   bits are the model and the rest is a make code, so all 0x888x values
   are Iomega: 0 is the Ditto 800, 3 the Ditto 2GB, 5 the Ditto Max.

   0x8883 - Ditto 2GB - rather than 0x8885 because the Max is the newer
   drive and the older one is the more widely understood. Note that
   ftape marks *both* with FT_CANT_FORMAT, its flag for drives that
   cannot format a cartridge - its changelog says they enter format mode
   and write garbage, the cartridges having been sold pre-formatted. If
   a host refuses to format for us, the two identities worth trying next
   are 0x8886, the "Iomega MerKat Formatter" that carries the Ditto Max
   extensions *without* that flag, and 0x014e, the Conner C250MQ that
   fdd_tape.c reports and that DOS QBACKUP is known to format against.
 */
#define DITTO_VENDOR_ID  0x8883
#define DITTO_ROM_VERSION 0x41

/*
   The cartridge, chosen rather than guessed.

   The size of a tape is not something a drive works out - the mechanism
   fixes how many tracks the head can reach, and the cartridge fixes how
   much tape runs past it, and between them they settle everything else.
   Reading it back off the image instead means a blank cartridge has no
   size at all until something formats it, and an image written by one
   host quietly changes the drive's idea of its own geometry when it is
   loaded into another. So it is configured, and the image is checked
   against it rather than consulted for it.

   This drive takes a wide range of media - a Ditto 2GB will read a
   QIC-80 cartridge - so the setting names a cartridge rather than a
   capacity. What it does NOT change is the drive: the vendor ID and the
   configuration byte say Ditto 2GB whatever is loaded, because that is
   what the mechanism is. Only the geometry and what Report Tape Status
   says about the media follow the setting. Hosts pick the highest format
   common to drive and media, which is what makes that the right way
   round.

   Config values are never reused or renumbered: a saved machine stores
   the number, so changing one silently repoints an existing VM at a
   different geometry. The first three are the capacities the Ditto's own
   cartridges were sold under, at the 2:1 its compression was rated at -
   a "2 GB" cartridge holds a gigabyte of actual bytes.
 */
#define DITTO_CAPACITY_2GB 2
#define DITTO_CAPACITY_3GB 3
#define DITTO_CAPACITY_5GB 5

#define DITTO_CART_QIC80_205  10
#define DITTO_CART_QIC80_307  11
#define DITTO_CART_QIC80_425  12
#define DITTO_CART_QIC80_1100 13
#define DITTO_CART_QIC3010    14
#define DITTO_CART_QIC3020    15
#define DITTO_CART_TR1        16
#define DITTO_CART_TR2        17
#define DITTO_CART_TR3        18

/*
   Format codes for the QIC-113 header segment (ftape-header-segment.h).
   Not the same thing as the QIC-117 format codes in tape_qic117.h, which
   are an argument to Select Rate: these are what a cartridge says about
   itself, and ftape reads this byte to decide how long the tape is.

   Nothing here writes a header segment - the host does that - but the
   image generator in docs/mkdittotape.py does, and it reads these from
   this table so that a cartridge it makes describes itself the way this
   drive would expect.
 */
#define FT_FMT_NORMAL 2 /* QIC-80 post rev. B, 205 or 307.5 ft */
#define FT_FMT_1100FT 3
#define FT_FMT_VAR    4 /* QIC-80 post rev. B, variable length */
#define FT_FMT_425FT  5
/*
   Variable length, and over 65535 segments a tape - which is the part
   that matters: a header in this format states its segment numbers in
   four bytes each, at their own offsets, because the ordinary two byte
   fields cannot hold them. A cartridge this long written any other way
   describes itself with a truncated last segment.
 */
#define FT_FMT_BIG    6

typedef struct ditto_cartridge_t {
    const char *name;        /* as the settings dialog offers it */
    int         value;       /* config value; never reused or renumbered */
    int         tracks;
    int         spt;         /* segments on each track */
    uint8_t     tape_status; /* what command 33 says: format, length, wide */
    uint8_t     format_code; /* what a header segment written here says */
} ditto_cartridge_t;

/*
   The cartridges, declared once and expanded twice: into the table the
   drive works from, and into the list the settings dialog offers. Two
   copies of a list like this drift, and here a drift would offer the
   user a cartridge the drive does not have and silently give them the
   default instead.

   Where the numbers come from, because a plausible-looking constant is
   how this drive has gone wrong before. Three tiers:

   Tier one, agreed by two or more sources - the QIC-80 family. ftape's
   own fallbacks (ftape-read.c), the Colorado 250 firmware's internal
   geometry table, and the QIC-117 rev. J calibration override table all
   give the same segments per track, and fdd_tape.c already carries
   28 x 150 for the same reason.

   Tier two, from Iomega's own driver and cross-checked - QIC-3010 and
   QIC-3020. QBWQ117.DLL carries a geometry table whose capacities are in
   decimal megabytes; read that way, QIC-3010 comes out at 255.8 MB and
   QIC-3020 at 502.1 MB, which are the figures the standard names them by
   ("QIC-3010-MC (255 MB)"). Both work out to about 295 feet against
   ftape's unrelated density constants, which is as much agreement as two
   independent sources can give.

   Tier three, DERIVED and evidenced nowhere - the Travan rows. What is
   evidenced is only the naming: ftape reads TR-n as the wide bit plus a
   QIC standard, so TR-1 is wide QIC-80, TR-2 wide QIC-3010 and TR-3 wide
   QIC-3020, and the status bytes below are exact. The segment counts are
   worked back from ftape's density figures (488, 730 and 1430 segments
   per 1000 feet) at the published tape length, with the track count
   picked to land near the published capacity. Treat them as a starting
   point: if a real cartridge's header segment disagrees, it wins.
 */
#define DITTO_ST_DITTO (QIC_TAPE_QIC3020 | QIC_TAPE_FLEX | QIC_TAPE_WIDE)

#define DITTO_CARTRIDGE_LIST                                                   \
    /* The Ditto's own, unchanged from when these were the only three. */      \
    X("Ditto 2 GB",  DITTO_CAPACITY_2GB, 72,  502, DITTO_ST_DITTO, FT_FMT_VAR) \
    X("Ditto 3 GB",  DITTO_CAPACITY_3GB, 72,  753, DITTO_ST_DITTO, FT_FMT_VAR) \
    /* Over 65535 segments a tape, so its header has to be the wide             variant - see FT_FMT_BIG. */                                            X("Ditto 5 GB",  DITTO_CAPACITY_5GB, 72, 1256, DITTO_ST_DITTO, FT_FMT_BIG)\
    /* Tier one. */                                                            \
    X("QIC-80, DC-2080 (205 ft)",   DITTO_CART_QIC80_205,  28, 100,            \
      QIC_TAPE_QIC80 | QIC_TAPE_205FT,         FT_FMT_NORMAL)                  \
    X("QIC-80, DC-2120 (307.5 ft)", DITTO_CART_QIC80_307,  28, 150,            \
      QIC_TAPE_QIC80 | QIC_TAPE_307FT,         FT_FMT_NORMAL)                  \
    X("QIC-80, 425 ft",             DITTO_CART_QIC80_425,  28, 207,            \
      QIC_TAPE_QIC80 | QIC_TAPE_205FT,         FT_FMT_425FT)                   \
    X("QIC-80, 1100 ft",            DITTO_CART_QIC80_1100, 28, 537,            \
      QIC_TAPE_QIC80 | QIC_TAPE_1100FT,        FT_FMT_1100FT)                  \
    /* Tier two. */                                                            \
    X("QIC-3010 (255 MB)",          DITTO_CART_QIC3010,    40, 215,            \
      QIC_TAPE_QIC3010 | QIC_TAPE_VAR_LEN_550, FT_FMT_BIG)                     \
    X("QIC-3020 (500 MB)",          DITTO_CART_QIC3020,    40, 422,            \
      QIC_TAPE_QIC3020 | QIC_TAPE_VAR_LEN_550, FT_FMT_BIG)                     \
    /* Tier three - derived, see above. */                                     \
    X("Travan TR-1 (400 MB)",       DITTO_CART_TR1,        36, 366,            \
      QIC_TAPE_QIC80 | QIC_TAPE_FLEX | QIC_TAPE_WIDE,   FT_FMT_VAR)            \
    X("Travan TR-2 (800 MB)",       DITTO_CART_TR2,        40, 540,            \
      QIC_TAPE_QIC3010 | QIC_TAPE_FLEX | QIC_TAPE_WIDE, FT_FMT_BIG)            \
    X("Travan TR-3 (1.6 GB)",       DITTO_CART_TR3,        40, 1060,           \
      QIC_TAPE_QIC3020 | QIC_TAPE_FLEX | QIC_TAPE_WIDE, FT_FMT_BIG)

static const ditto_cartridge_t ditto_cartridges[] = {
#define X(nm, val, trk, sgs, stat, fmt) { nm, val, trk, sgs, stat, fmt },
    DITTO_CARTRIDGE_LIST
#undef X
};

#define DITTO_CARTRIDGES (sizeof(ditto_cartridges) / sizeof(ditto_cartridges[0]))

/* The cartridge a machine with nothing configured comes up with. */
#define DITTO_CAPACITY_DEFAULT DITTO_CAPACITY_2GB

/*
   How long a wind is modelled to take. These need not match the real
   mechanism - they need only keep the drive not ready for long enough
   that the host sees it go busy and come back, which is the transition
   its completion checks wait on.
 */
#define DITTO_WIND_PER_SEG_US 3000ULL
#define DITTO_WIND_MIN_US     60000ULL
#define DITTO_WIND_FULL_US    750000ULL
#define DITTO_HEAD_SEEK_US    60000ULL

/* The buffer is 128 KB and its address register wraps at that boundary. */
#define DITTO_BUFFER_SIZE 0x20000
#define DITTO_BUFFER_MASK (DITTO_BUFFER_SIZE - 1)

#define DITTO_CRC_INIT    0xffff

typedef struct ditto_t {
    void   *lpt;

    /* Configuration. */
    int     max_proto;  /* fastest protocol the bridge will admit to */
    int     capacity;   /* cartridge the drive is loaded with, in GB */
    uint8_t unit;       /* address this pod answers a knock at */
    char    image_fn[1024];
    int     readonly;

    /* Layer 1: the wire. */
    uint8_t ctrl;       /* our shadow of the host's control register */
    uint8_t dat;        /* last byte the host put on the data lines */
    int     connected;
    int     proto;      /* protocol the bridge is programmed for */
    uint8_t proto_bits;

    int     knock;       /* SELECT edges seen since it began */
    int     ident;        /* answering the post-knock address probe */
    int     latching;     /* an out-of-band register write is under way */
    int     latch_commits; /* bytes it has committed so far */

    /* Layer 1: the byte being handed back to the host. */
    int     cur_reg;    /* register the host has addressed */
    int     xfer_idx;   /* bytes moved since that register was addressed -
                           the multi-byte registers (0x22, 0x24, 0x28, 0x2c,
                           0x30, 0x34) are written as a run of bytes to the
                           one address, and index themselves off this */
    uint8_t rd_val;     /* byte being shifted out */
    int     rd_high;    /* the low nibble has gone, the high one is next */
    uint8_t rd_out;     /* status bits presenting the current nibble */
    uint8_t last_status; /* last value handed back, to quieten poll loops */

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
    /* Kept for the conformance harness, which checks the step decoding
       by counting trains rather than by their effect. */
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
    /* Recorded when the host selects one, but nothing here is paced by
       it - the geometry comes from the cartridge setting instead. */
    uint8_t  qic_format_code;
    uint8_t  qic_partition;
    uint8_t  qic_mode_flags;
    int      qic_track;    /* logical tape track the head sits on */
    int      qic_segment;  /* absolute segment under the head */
    int      qic_sector;   /* and how far into it, for READ ID */
    int      qic_reverse;  /* this track is laid down back to front */
    int      qic_running;  /* tape is streaming past the head */
    int      qic_busy;     /* a wind is under way */
    pc_timer_t busy_timer;
    pc_timer_t motion_timer; /* the cartridge running past the head */
    pc_timer_t xfer_timer;   /* the tape passing under the head, mid transfer */
    int        xfer_res_len; /* result bytes waiting for it to finish */
    int        run_off;      /* segments of slack pulled past the end */
    uint16_t qic_vendor_id;
    uint16_t qic_format_segments;

    /* The cartridge. */
    FILE    *fp;
    uint32_t image_size;
    int      image_loaded;
    int      segs_per_cyl;
    int      segs_per_head;
    int      tape_tracks;   /* tracks the head can reach */
    int      tape_spt;      /* segments on each of them */

    int      image_dirty;   /* written but not yet flushed to disk */
    int      epp_warned;    /* the EPP-vs-port mismatch has been reported */
    uint32_t last_port_ms;  /* when the host last touched the port */
    uint8_t  dat_out;       /* last byte we put on the data lines */

    /* Counted for the format-rate summary, not used by the emulation. */

    /* The ECC coprocessor. */
    uint8_t  ecc_mul[256];
    int      ecc_error;

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
static const char *ditto_proto_name[] = {
    "SPP 4-bit", "PS/2 8-bit", "EPP-8", "EPP-16", "EPP-32"
};
#endif

/*
   How long the host has to go quiet before it counts as being away
   rather than just between operations.
 */
#define DITTO_IDLE_MS 20

/*
   A handful of lines a session, on whatever the trace is set to. The
   wire trace costs a formatted line and a flushed write per port access,
   which is forty of them for every byte that reaches the tape - fine for
   reading the protocol, useless for asking why something is slow,
   because turning it on is most of the answer. This says where the time
   goes without disturbing it.
 */
static void
ditto_stat(const char *fmt, ...)
{
    va_list ap;

    va_start(ap, fmt);
    pclog_ex(fmt, ap);
    va_end(ap);
}

/*
   Marks the places where the host stops talking to us. A wire trace says
   what was asked for but not when, and a wait spread over a few hundred
   accesses is invisible in it - the operation before a ten second pause
   looks exactly like the operation before a ten microsecond one. This
   names the pause, so the line above it is the thing being waited on.

   It is how the ten second wait after every transfer was pinned to the
   command that caused it.

   Trace-gated, so it costs a clock read per port access only when the
   trace is on. It earned its keep on its own while the host was still
   stalling; it is not worth a call on the hot path now that it is not.
 */
#ifdef ENABLE_LPT_DITTO_LOG
static void
ditto_note_idle(ditto_t *dev)
{
    const uint32_t now = plat_get_ticks();

    if (!lpt_ditto_do_log)
        return;

    if ((dev->last_port_ms != 0) &&
        ((now - dev->last_port_ms) >= DITTO_IDLE_MS))
        ditto_stat("Ditto: --- host away %u ms ---\n",
                   now - dev->last_port_ms);

    dev->last_port_ms = now;
}
#else
#    define ditto_note_idle(dev)
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

    /*
       Being connected is not a reason to hold the interrupt back. The
       thought was that ACK cannot be an interrupt while the same lines
       are carrying a transfer - but the host does not let go between
       operations. It connects once and stays connected for thousands of
       commands, so a link-gated interrupt is one that never fires.

       That cost ten seconds a segment while formatting: the host issued
       a format, waited on an interrupt that could not arrive, gave up on
       its own timeout, and only then polled and found the interrupt had
       been pending the whole time. Whether it wants one at all is
       already answered by the mode it programmed - SOFTEN to read the
       flag out of a register, HARDEN to be told.
     */
    if (dev->irq_pending &&
        (test_poke || ((dev->irq_ctrl & BP_IRQ_MASK) == BP_IRQ_HARDEN)))
        lpt_irq(dev->lpt, 1);
    else
        lpt_irq(dev->lpt, 0);
}

/* --------------------------------------------------------------------- */
/* The ECC coprocessor                                                   */
/* --------------------------------------------------------------------- */

/*
   The bridge carries the QIC-80 Reed-Solomon coprocessor, which works
   down the columns of a segment: the three ECC sectors at the end hold,
   for each byte position, the parity of that byte across every data
   sector.

   Generating them here rather than leaving the sectors blank is what
   keeps a cartridge written through this drive readable by anything
   else - including the same image mounted on the floppy-cable drive,
   whose host computes and checks these bytes itself.

   The arithmetic is over GF(256) with x^8 + x^7 + x^2 + x + 1, and the
   only multiplication needed is by the constant r^105, which is 0xc0.
 */
#define DITTO_ECC_POLY    0x187
#define DITTO_ECC_FACTOR  0xc0

static void
ditto_ecc_build_table(uint8_t *tab)
{
    uint16_t v = DITTO_ECC_FACTOR;

    tab[0] = 0x00;

    /* The powers of two, by repeated doubling in the field ... */
    for (unsigned int bit = 1; bit < 0x100; bit <<= 1) {
        tab[bit] = (uint8_t) v;
        v <<= 1;
        if (v & 0x100)
            v ^= DITTO_ECC_POLY;
    }

    /* ... and everything else follows, the map being linear. */
    for (unsigned int i = 3; i < 0x100; i++) {
        const unsigned int low = i & (~i + 1);

        if (i != low)
            tab[i] = tab[low] ^ tab[i ^ low];
    }
}

/*
   Works out the three parity sectors for nblocks of data. The recurrence
   runs down each byte column in turn, holding three running values:

     p0 = p1 + r^105 * (m - p0)
     p1 = p2 + r^105 * (m - p0)
     p2 =               m - p0

   with addition being exclusive or, as it is throughout the field.
 */
static void
ditto_ecc_parity(const ditto_t *dev, uint32_t addr, int nblocks, uint8_t *parity)
{
    for (int col = 0; col < FDD_TAPE_SECTOR_SIZE; col++) {
        uint8_t p0 = 0;
        uint8_t p1 = 0;
        uint8_t p2 = 0;

        for (int i = 0; i < nblocks; i++) {
            const uint32_t off = (addr + (uint32_t) i * FDD_TAPE_SECTOR_SIZE +
                                  (uint32_t) col) & DITTO_BUFFER_MASK;
            const uint8_t  t1  = dev->buffer[off] ^ p0;
            const uint8_t  t2  = dev->ecc_mul[t1];

            p0 = t2 ^ p1;
            p1 = t2 ^ p2;
            p2 = t1;
        }

        parity[col]                              = p0;
        parity[FDD_TAPE_SECTOR_SIZE + col]       = p1;
        parity[(2 * FDD_TAPE_SECTOR_SIZE) + col] = p2;
    }
}

/* Splits the region register 0x34 was programmed with into its parts. */
static int
ditto_ecc_region(const ditto_t *dev, uint32_t *addr, int *blocks)
{
    *addr   = (uint32_t) dev->ecc_ctrl[1] << 10;
    *blocks = (dev->ecc_ctrl[0] & 0x3f) + 1;

    /* Three of the blocks are the parity, so there must be data as well. */
    return (*blocks > FDD_TAPE_ECC_SECTORS);
}

static void
ditto_ecc_generate(ditto_t *dev)
{
    uint8_t  parity[3 * FDD_TAPE_SECTOR_SIZE];
    uint32_t addr;
    int      blocks;

    if (!ditto_ecc_region(dev, &addr, &blocks))
        return;

    ditto_ecc_parity(dev, addr, blocks - FDD_TAPE_ECC_SECTORS, parity);

    for (int i = 0; i < (3 * FDD_TAPE_SECTOR_SIZE); i++) {
        const uint32_t off = (addr +
                              ((uint32_t) (blocks - FDD_TAPE_ECC_SECTORS) *
                               FDD_TAPE_SECTOR_SIZE) + (uint32_t) i) &
                             DITTO_BUFFER_MASK;

        dev->buffer[off] = parity[i];
    }

    dev->ecc_error = 0;

    ditto_log("Ditto: ECC generated over %i blocks at %05X\n", blocks, addr);
}

static void
ditto_ecc_check(ditto_t *dev)
{
    uint8_t  parity[3 * FDD_TAPE_SECTOR_SIZE];
    uint32_t addr;
    int      blocks;

    dev->ecc_error = 0;

    if (!ditto_ecc_region(dev, &addr, &blocks))
        return;

    ditto_ecc_parity(dev, addr, blocks - FDD_TAPE_ECC_SECTORS, parity);

    /*
       A systematic code, so the data is good exactly when the parity we
       work out matches the parity that came off the tape. The host only
       needs to be told whether to bother correcting - this coprocessor
       was never able to do that part itself.
     */
    for (int i = 0; i < (3 * FDD_TAPE_SECTOR_SIZE); i++) {
        const uint32_t off = (addr +
                              ((uint32_t) (blocks - FDD_TAPE_ECC_SECTORS) *
                               FDD_TAPE_SECTOR_SIZE) + (uint32_t) i) &
                             DITTO_BUFFER_MASK;

        if (dev->buffer[off] != parity[i]) {
            dev->ecc_error = 1;
            ditto_log("Ditto: ECC error at %05X\n", addr);
            return;
        }
    }
}

/* --------------------------------------------------------------------- */
/* The cartridge                                                         */
/* --------------------------------------------------------------------- */

static void
ditto_image_close(ditto_t *dev)
{
    if (dev->fp != NULL) {
        fclose(dev->fp); /* flushes whatever the last segment left */
        dev->fp = NULL;
    }

    dev->image_dirty  = 0;

    dev->image_size   = 0;
    dev->image_loaded = 0;
}

/*
   The cartridge a configured value names, or the default if it names
   none - which is what an older machine saved with a setting that has
   since gone, or a hand-edited configuration file, will hand us.
 */
static const ditto_cartridge_t *
ditto_cartridge(int value)
{
    for (size_t i = 0; i < DITTO_CARTRIDGES; i++)
        if (ditto_cartridges[i].value == value)
            return &ditto_cartridges[i];

    for (size_t i = 0; i < DITTO_CARTRIDGES; i++)
        if (ditto_cartridges[i].value == DITTO_CAPACITY_DEFAULT)
            return &ditto_cartridges[i];

    return &ditto_cartridges[0];
}

/* Bytes of data a whole cartridge of this geometry holds. */
static uint64_t
ditto_tape_capacity(const ditto_t *dev)
{
    const uint64_t segments = (uint64_t) dev->tape_tracks * dev->tape_spt;

    return segments * (FDD_TAPE_SECTORS_PER_SEG - FDD_TAPE_ECC_SECTORS) *
           FDD_TAPE_SECTOR_SIZE;
}

/* Every byte the image holds once the ECC sectors are counted in too. */
static uint64_t
ditto_image_extent(const ditto_t *dev)
{
    return (uint64_t) dev->tape_tracks * dev->tape_spt * FDD_TAPE_SEGMENT_SIZE;
}

/*
   Settles everything the loaded cartridge decides: how much tape there
   is, and what the drive says about it when asked.

   Segments per cylinder and per head are not among them. They are the
   grid the host lays over the tape to address it through a floppy
   controller, not anything the tape decides, so they stay at the figures
   every host driver uses for this class of drive. That is also why
   QIC-40 is not offered here - it packs 255 cylinders to a head rather
   than 150, so the grid would have to move with it.
 */
static void
ditto_set_geometry(ditto_t *dev)
{
    const ditto_cartridge_t *cart = ditto_cartridge(dev->capacity);

    dev->segs_per_cyl  = FDD_TAPE_SEGS_PER_CYL;
    dev->segs_per_head = FDD_TAPE_SEGS_PER_HEAD;
    dev->tape_tracks   = cart->tracks;
    dev->tape_spt      = cart->spt;
    dev->qic_tape_status = cart->tape_status;

    /*
       The extra length bit is the drive saying the cartridge is a long
       one, so it follows the cartridge rather than sitting on whatever
       the drive powered up with. Everything else in the configuration
       byte is the mechanism, and does not move.
     */
    if ((cart->tape_status & QIC_TAPE_LEN_MASK) > QIC_TAPE_205FT)
        dev->qic_config |= QIC_CONFIG_LONG;
    else
        dev->qic_config &= (uint8_t) ~QIC_CONFIG_LONG;

    ditto_log("Ditto: %s - %i tracks of %i segments, tape status %02X, "
              "%llu bytes of data in a %llu byte image\n",
              cart->name, dev->tape_tracks, dev->tape_spt,
              dev->qic_tape_status,
              (unsigned long long) ditto_tape_capacity(dev),
              (unsigned long long) ditto_image_extent(dev));
}

/*
   Checks that a loaded image is the size the configured cartridge calls
   for. A shorter one is not refused - a cartridge is allowed to be
   blank, and one that is part written is how an image grows as the host
   fills it - but a mismatch is worth saying out loud, because the usual
   cause is an image made for a different cartridge being loaded under
   this one, and every segment address past the end of it will fail.

   Said through ditto_stat() rather than the trace, so that it is there
   when the trace is not. With a dozen cartridges to choose between,
   loading an image against the wrong one is the easy mistake to make,
   and it is not one the host reports in any legible way.
 */
static void
ditto_check_image(const ditto_t *dev)
{
    const uint64_t want = ditto_image_extent(dev);

    if (!dev->image_loaded || (dev->image_size == 0))
        return;

    if (dev->image_size == want)
        return;

    ditto_stat("Ditto: image is %u bytes, a %s cartridge is %llu - %s\n",
               dev->image_size, ditto_cartridge(dev->capacity)->name,
               (unsigned long long) want,
               (dev->image_size < want) ? "the tape reads as part written"
                                        : "the excess is unreachable");
}

static void
ditto_image_load(ditto_t *dev, const char *fn)
{
    FILE *fp;

    ditto_image_close(dev);

    if ((fn == NULL) || (fn[0] == 0x00))
        return;

    /* The configuration marks write-protected images with a wp:// prefix. */
    if (strstr(fn, "wp://") == fn) {
        fn += 5;
        dev->readonly = 1;
    }

    fp = dev->readonly ? NULL : plat_fopen((char *) fn, "rb+");
    if (fp == NULL) {
        fp = plat_fopen((char *) fn, "rb");
        if (fp != NULL)
            dev->readonly = 1;
    }

    /* A cartridge that is not there yet is a blank one. */
    if (fp == NULL) {
        if (dev->readonly)
            return;

        fp = plat_fopen((char *) fn, "wb+");
        if (fp == NULL) {
            ditto_log("Ditto: unable to open image %s\n", fn);
            return;
        }
    }

    if (fseek(fp, 0, SEEK_END) == 0)
        dev->image_size = (uint32_t) ftell(fp);

    dev->fp           = fp;
    dev->image_loaded = 1;

    ditto_check_image(dev);

    ditto_log("Ditto: loaded %s (%u bytes%s)\n", fn, dev->image_size,
              dev->readonly ? ", read-only" : "");
}

/* Anything past the end of the image is unwritten tape, and reads blank. */
static void
ditto_image_read(ditto_t *dev, uint32_t offset, uint8_t *buf, uint32_t len)
{
    size_t got = 0;

    memset(buf, 0x00, len);

    if ((dev->fp == NULL) || (offset >= dev->image_size))
        return;

    if (fseek(dev->fp, (long) offset, SEEK_SET) != 0)
        return;

    got = fread(buf, 1, len, dev->fp);
    if (got < len)
        memset(buf + got, 0x00, len - got);
}

static int
ditto_image_write(ditto_t *dev, uint32_t offset, const uint8_t *buf, uint32_t len)
{
    if ((dev->fp == NULL) || dev->readonly)
        return 0;

    if (fseek(dev->fp, (long) offset, SEEK_SET) != 0)
        return 0;

    if (fwrite(buf, 1, len, dev->fp) != len)
        return 0;

    /*
       Not flushed here. A format writes the cartridge a sector at a
       time, so flushing each one turns one segment into thirty-two
       forced writes, and a whole cartridge into nearly three million of
       them. The callers flush once they have finished a segment
       instead, which keeps what is on disk no more than a segment
       behind what the host thinks it wrote.
     */
    dev->image_dirty = 1;

    if ((offset + len) > dev->image_size)
        dev->image_size = offset + len;

    return 1;
}

/* Puts a finished segment on disk. Cheap when nothing was written. */
static void
ditto_image_sync(ditto_t *dev)
{
    if ((dev->fp == NULL) || !dev->image_dirty)
        return;

    fflush(dev->fp);
    dev->image_dirty = 0;
}

/*
   Turns one of the controller's C/H/R addresses into an offset into the
   cartridge. The head field is not a physical head here - it is just the
   high digits of the segment number, and runs well past the two a floppy
   would have.
 */
static int
ditto_sector_offset(const ditto_t *dev, int cyl, int head, int sector,
                    uint32_t *offset)
{
    int      segment;
    int      sector_in_segment;
    uint64_t off;

    if ((sector < 1) || (sector > (dev->segs_per_cyl * FDD_TAPE_SECTORS_PER_SEG)))
        return 0;
    if ((cyl < 0) || (cyl > FDD_TAPE_MAX_TRACK) || (head < 0) || (head > 0xff))
        return 0;

    segment = (head * dev->segs_per_head) + (cyl * dev->segs_per_cyl) +
              ((sector - 1) / FDD_TAPE_SECTORS_PER_SEG);
    sector_in_segment = (sector - 1) % FDD_TAPE_SECTORS_PER_SEG;

    off = ((uint64_t) segment * FDD_TAPE_SEGMENT_SIZE) +
          ((uint64_t) sector_in_segment * FDD_TAPE_SECTOR_SIZE);
    if (off > UINT32_MAX)
        return 0;

    *offset = (uint32_t) off;

    return 1;
}

/* --------------------------------------------------------------------- */
/* Layer 4: the QIC-117 drive                                            */
/* --------------------------------------------------------------------- */

static int
ditto_qic_answer(const ditto_t *dev)
{
    return dev->qic_ack;
}

/*
   Every command is checked against a three byte record before it runs:
   which status bits matter, what they have to be, and what the command
   conflicts with. These are the tables the drive's own firmware carries,
   and they agree byte for byte with the ones in the host driver - which
   is the strongest evidence available that both were read correctly.
 */
/* Command attribute flags, the third byte of each record. */
#define QIC_ATTR_IMMEDIATE 0x20 /* runs at once, ahead of any queue */
#define QIC_ATTR_RESERVED  0x40 /* not a command at all */
#define QIC_ATTR_MOTION    0x80 /* sets the tape going */
#define QIC_ATTR_MODE_MASK 0x1f /* the rest interlock against the mode byte */

typedef struct qic_attr_t {
    uint8_t mask;  /* status bits this command cares about */
    uint8_t state; /* what those bits have to be */
    uint8_t flags;
} qic_attr_t;

/* Commands 1 to 38. */
static const qic_attr_t qic_attr_normal[] = {
    { 0x00, 0x00, 0x20 }, /*  1 soft reset                 */
    { 0x00, 0x00, 0x20 }, /*  2 report next bit            */
    { 0x36, 0x24, 0x86 }, /*  3 pause                      */
    { 0x36, 0x24, 0x86 }, /*  4 micro step pause           */
    { 0x00, 0x00, 0x20 }, /*  5 alternate command timeout  */
    { 0x00, 0x00, 0x20 }, /*  6 report drive status        */
    { 0x01, 0x01, 0x00 }, /*  7 report error code          */
    { 0x00, 0x00, 0x20 }, /*  8 report drive configuration */
    { 0x00, 0x00, 0x20 }, /*  9 report ROM version         */
    { 0x37, 0x25, 0x80 }, /* 10 logical forward            */
    { 0x17, 0x05, 0x80 }, /* 11 physical reverse           */
    { 0x17, 0x05, 0x80 }, /* 12 physical forward           */
    { 0x37, 0x25, 0x80 }, /* 13 seek head to track         */
    { 0x17, 0x05, 0x80 }, /* 14 seek load point            */
    { 0x1f, 0x05, 0x00 }, /* 15 enter format mode          */
    { 0x1f, 0x05, 0x98 }, /* 16 write reference burst      */
    { 0x37, 0x25, 0x00 }, /* 17 enter verify mode          */
    { 0x00, 0x00, 0x82 }, /* 18 stop tape                  */
    { 0x00, 0x00, 0x40 }, /* 19 reserved                   */
    { 0x00, 0x00, 0x40 }, /* 20 reserved                   */
    { 0x02, 0x00, 0x87 }, /* 21 micro step head up         */
    { 0x02, 0x00, 0x87 }, /* 22 micro step head down       */
    { 0x00, 0x00, 0x20 }, /* 23 soft select                */
    { 0x00, 0x00, 0x20 }, /* 24 soft deselect              */
    { 0x36, 0x24, 0x86 }, /* 25 skip segments reverse      */
    { 0x36, 0x24, 0x86 }, /* 26 skip segments forward      */
    { 0x03, 0x01, 0x00 }, /* 27 select rate or format      */
    { 0x01, 0x01, 0x00 }, /* 28 enter diagnostic 1         */
    { 0x1f, 0x05, 0x00 }, /* 29 enter diagnostic 2         */
    { 0x00, 0x00, 0x20 }, /* 30 enter primary mode         */
    { 0x00, 0x00, 0x40 }, /* 31 vendor unique              */
    { 0x00, 0x00, 0x20 }, /* 32 report vendor ID           */
    { 0x04, 0x04, 0x20 }, /* 33 report tape status         */
    { 0x36, 0x24, 0x86 }, /* 34 skip extended reverse      */
    { 0x36, 0x24, 0x86 }, /* 35 skip extended forward      */
    { 0x17, 0x05, 0x80 }, /* 36 calibrate tape length      */
    { 0x17, 0x05, 0x00 }, /* 37 report format segments     */
    { 0x17, 0x05, 0x00 }  /* 38 set format segments        */
};

/* Commands 50 to 56. */
static const qic_attr_t qic_attr_extended[] = {
    { 0x03, 0x01, 0x00 }, /* 50 extended select rate       */
    { 0x00, 0x00, 0x20 }, /* 51 ext report drive config    */
    { 0x00, 0x00, 0x00 }, /* 52 load/unload                */
    { 0x01, 0x01, 0x00 }, /* 53 toggle lock                */
    { 0x00, 0x00, 0x20 }, /* 54 loader partition status    */
    { 0x17, 0x05, 0x82 }, /* 55 seek to partition          */
    { 0x17, 0x05, 0x8c }  /* 56 vendor identity switch     */
};

static const qic_attr_t *
qic_attr(int cmd)
{
    if ((cmd >= 1) && (cmd <= 38))
        return &qic_attr_normal[cmd - 1];
    if ((cmd >= 50) && (cmd <= 56))
        return &qic_attr_extended[cmd - 50];

    return NULL;
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

/*
   How many segments a tape track holds. The cartridge settles this, but
   a host laying down a format states the figure it is writing to and is
   taken at its word for as long as that format lasts - refusing would
   make the drive unable to be formatted to anything but its own idea of
   itself, which is not how the mechanism behaves.
 */
static int
qic_segments_per_track(const ditto_t *dev)
{
    return dev->qic_format_segments ? dev->qic_format_segments
                                    : dev->tape_spt;
}

static int
qic_track_start(const ditto_t *dev)
{
    return dev->qic_track * qic_segments_per_track(dev);
}

/*
   Recomputes the bits of the status byte that follow from where the head
   is and what the drive is doing.
 */
static void
qic_update_status(ditto_t *dev)
{
    int offset;

    dev->qic_status &= (uint8_t) ~(QIC_STATUS_CARTRIDGE_PRESENT |
                                   QIC_STATUS_WRITE_PROTECT |
                                   QIC_STATUS_REFERENCED |
                                   QIC_STATUS_AT_BOT | QIC_STATUS_AT_EOT);

    /*
       Ready means nothing is in progress, and a cartridge in motion
       counts: the host starts a pass and waits for ready to come back as
       the sign that it finished. A drive that claims to be idle the
       moment it was asked to move is taken to have failed outright.
     */
    if (dev->qic_running || dev->qic_busy || (dev->qic_params_left > 0))
        dev->qic_status &= (uint8_t) ~QIC_STATUS_READY;
    else
        dev->qic_status |= QIC_STATUS_READY;

    if (!dev->image_loaded)
        return;

    dev->qic_status |= QIC_STATUS_CARTRIDGE_PRESENT;

    if (dev->readonly)
        dev->qic_status |= QIC_STATUS_WRITE_PROTECT;

    /* A cartridge with anything on it has a reference burst, which the
       host reads as "formatted". A blank one is offered for formatting. */
    if (dev->image_size > 0)
        dev->qic_status |= QIC_STATUS_REFERENCED;

    /*
       Beginning and end of tape are physical places, and only the even
       tracks run in segment order from one to the other - an odd track is
       laid down back to front, so its first segment sits at the far end.
     */
    offset = dev->qic_segment - qic_track_start(dev);

    if (offset <= 0)
        dev->qic_status |= dev->qic_reverse ? QIC_STATUS_AT_EOT
                                            : QIC_STATUS_AT_BOT;
    else if (offset >= (qic_segments_per_track(dev) - 1))
        dev->qic_status |= dev->qic_reverse ? QIC_STATUS_AT_BOT
                                            : QIC_STATUS_AT_EOT;
}

/* Holds the drive not ready for as long as a wind would take. */
static void
qic_busy_done(void *priv)
{
    ditto_t *dev = (ditto_t *) priv;

    dev->qic_busy    = 0;
    dev->qic_running = 0;
    qic_update_status(dev);

    ditto_log("Ditto: wind complete, drive ready\n");
}

/*
   How long a segment takes to pass the head while the tape is streaming.

   This is the data rate, not a wind: the tape is being written or read as
   it goes, so it runs at exactly the speed the bits are moving. Winding
   is the fast case and keeps its own constants.

   Getting this wrong is not a matter of pacing. A host writes a segment,
   does its own housekeeping, writes the next, and expects the tape it is
   writing to still be under the head; the drive reaching the end of the
   track early reports end of tape, and the host takes that as the pass
   being over and the backup failing. At the wind rate a whole 502 segment
   track goes by in a second and a half, so the tape ran out from under
   the host after a few hundred kilobytes. At 500 kbit/s the same track
   takes four and a half minutes, which is what the cartridge really does.
 */
static uint64_t
qic_stream_period_us(const ditto_t *dev)
{
    /* Indexed by the drive configuration's rate field, as ftape reads it. */
    static const unsigned int kbit[4] = { 250, 2000, 500, 1000 };
    const unsigned int rate =
        kbit[(dev->qic_config >> QIC_CONFIG_RATE_SHIFT) & 0x03];

    return ((uint64_t) FDD_TAPE_SEGMENT_SIZE * 8 * 1000) / rate;
}

/* Stops the cartridge wherever it has got to, and lets the drive answer
   ready again. */
static void
qic_stop_motion(ditto_t *dev)
{
    timer_disable(&dev->motion_timer);

    dev->qic_running = 0;
    dev->run_off     = 0;
    qic_update_status(dev);
}

/*
   Runs the cartridge past the head, a segment at a time.

   The pass has to end by itself, because the host does not stop it: it
   starts the tape, writes or reads what it wants, and then waits for
   ready to come back as the sign that the pass is over. So the tape runs
   to the end of the track and stops there.

   It stops a segment late, on purpose. The host polls the status a few
   milliseconds after handing over the last segment of a track and has to
   find the drive still busy - a drive that went idle the instant the last
   segment went by would be claiming the pass was over while the host was
   still working on it, which QBACKUP's format reads as a failure outright
   rather than as an early finish.
 */
static void
qic_motion_tick(void *priv)
{
    ditto_t  *dev = (ditto_t *) priv;
    const int end = qic_track_start(dev) + qic_segments_per_track(dev) - 1;

    if (!dev->qic_running) {
        qic_stop_motion(dev);
        return;
    }

    timer_advance_u64(&dev->motion_timer, qic_stream_period_us(dev) * TIMER_USEC);

    if (dev->qic_segment < end) {
        dev->qic_segment++;
        dev->qic_sector = 0;
        dev->run_off = 0;
        qic_update_status(dev);
        return;
    }

    /* The track has run out; carry on into the slack, then come to rest. */
    if (++dev->run_off >= 1) {
        ditto_log("Ditto: end of track %i, stopping\n", dev->qic_track);
        qic_stop_motion(dev);
    }
}

/* Sets the cartridge streaming under the head. */
static void
qic_start_motion(ditto_t *dev)
{
    dev->qic_running = 1;
    dev->run_off     = 0;
    qic_update_status(dev);

    timer_set_delay_u64(&dev->motion_timer, qic_stream_period_us(dev) * TIMER_USEC);
}

static void
qic_begin_busy(ditto_t *dev, uint64_t us)
{
    if (us == 0)
        return;

    dev->qic_busy = 1;
    qic_update_status(dev);

    timer_set_delay_u64(&dev->busy_timer, us * TIMER_USEC);

    ditto_log("Ditto: busy for %llu us\n", (unsigned long long) us);
}

/* Moves the head to a segment, and stays busy for as long as getting
   there would have taken. */
static void
qic_seek_segment(ditto_t *dev, int segment)
{
    const int start = qic_track_start(dev);
    const int end   = start + qic_segments_per_track(dev) - 1;
    int       dist;
    uint64_t  us;

    if (segment < start)
        segment = start;
    if (segment > end)
        segment = end;

    dist = segment - dev->qic_segment;
    if (dist < 0)
        dist = -dist;

    dev->qic_segment = segment;
    dev->qic_sector = 0;
    qic_update_status(dev);

    if (dist == 0)
        return;

    us = (uint64_t) dist * DITTO_WIND_PER_SEG_US;
    if (us < DITTO_WIND_MIN_US)
        us = DITTO_WIND_MIN_US;
    if (us > DITTO_WIND_FULL_US)
        us = DITTO_WIND_FULL_US;

    qic_begin_busy(dev, us);
}

/*
   Checks a command against its attribute record. Returns zero if it may
   run; otherwise it has already latched the error that says why not.
 */
static int
qic_check_command(ditto_t *dev, int cmd)
{
    const qic_attr_t *attr = qic_attr(cmd);
    uint8_t           diff;
    uint8_t           conflict;

    if (attr == NULL) {
        qic_set_error(dev, QIC_ERROR_UNDEFINED_COMMAND, (uint8_t) cmd);
        return -1;
    }

    if (attr->flags & QIC_ATTR_RESERVED) {
        qic_set_error(dev, QIC_ERROR_UNDEFINED_COMMAND, (uint8_t) cmd);
        return -1;
    }

    qic_update_status(dev);

    /*
       The lowest status bit that is not what the command needs picks the
       error, so the host is told the first thing standing in its way
       rather than the last.
     */
    diff = (uint8_t) ((dev->qic_status & attr->mask) ^ attr->state);

    /*
       Except that a referenced cartridge cannot be asked for in Format
       mode. Outside it, the drive needs the load point burst to know
       where the segments lie and refuses to stream tape without one -
       but in Format mode that burst is the thing being written, so
       requiring it first means a blank cartridge can never become a
       formatted one.
     */
    if ((dev->qic_mode_flags & QIC_MODE_FORMAT) &&
        (diff & QIC_STATUS_REFERENCED))
        diff &= (uint8_t) ~QIC_STATUS_REFERENCED;

    if (diff != 0) {
        uint8_t err;

        if (diff & QIC_STATUS_READY)
            err = QIC_ERROR_NOT_READY;
        else if (diff & QIC_STATUS_ERROR)
            err = QIC_ERROR_PENDING_ERROR;
        else if (diff & QIC_STATUS_CARTRIDGE_PRESENT)
            err = QIC_ERROR_NO_CARTRIDGE;
        else if (diff & QIC_STATUS_WRITE_PROTECT)
            err = QIC_ERROR_WRITE_PROTECTED;
        else if (diff & QIC_STATUS_NEW_CARTRIDGE)
            err = QIC_ERROR_NEW_CARTRIDGE;
        else
            err = QIC_ERROR_NOT_REFERENCED;

        qic_set_error(dev, err, (uint8_t) cmd);
        return -1;
    }

    conflict = (uint8_t) (attr->flags & QIC_ATTR_MODE_MASK & dev->qic_mode_flags);
    if (conflict != 0) {
        uint8_t err;

        if (conflict & QIC_MODE_HIGH_SPEED)
            err = QIC_ERROR_ILLEGAL_IN_HIGH_SPEED;
        else if (conflict & QIC_MODE_NON_INTR)
            err = QIC_ERROR_DURING_NON_INTR;
        else if (conflict & QIC_MODE_FORMAT)
            err = QIC_ERROR_ILLEGAL_IN_FORMAT;
        else if (conflict & QIC_MODE_VERIFY)
            err = QIC_ERROR_ILLEGAL_IN_VERIFY;
        else
            err = QIC_ERROR_ILLEGAL_IN_PRIMARY;

        qic_set_error(dev, err, (uint8_t) cmd);
        return -1;
    }

    return 0;
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
   The drive configuration byte, rate field and all.

   It used to be masked out, on the reasoning that this drive reports its
   rate through the extended command and a host that knows the model asks
   there. That left every read of this register saying rate code zero -
   the slowest the field can name - however fast the host had just set
   the drive to run. A host that works out how long an operation should
   take from the rate the drive claims will wait far longer than it needs
   to, and this one waits ten seconds for every segment it formats,
   deterministically, with the port interrupt switched off so nothing can
   cut the wait short.

   fdd_tape.c reports the rate here, and there is no reason this drive
   should be the one that hides it: whatever the extended command is for,
   it is not a reason to answer this one wrongly.
 */
static uint8_t
qic_drive_config(const ditto_t *dev)
{
    return dev->qic_config;
}


/*
   Parameter widths are shared (tape_qic117.h); this drive answers for the
   one command the shared table leaves to it. The disassembly of this
   firmware was read as wanting two parameters for enter diagnostic 2.
   Nothing has ever sent it, so the reading is carried here rather than
   imposed on the drive on the floppy cable, which has always assumed
   none.
 */
static int
qic_param_count(int cmd)
{
    if (cmd == QIC_ENTER_DIAGNOSTIC_2)
        return 2;

    return qic117_command_params(cmd);
}

/* Reassembles a multi-nibble parameter, which arrives low nibble first. */
static int
qic_param_value(const ditto_t *dev, int cmd)
{
    const int count = qic_param_count(cmd);
    int       value = 0;

    for (int i = count - 1; i >= 0; i--)
        value = (value << 4) | (dev->qic_param[i] & 0x0f);

    return value;
}

/* Runs a command that has all the parameters it needs. */
static void
qic_run_command(ditto_t *dev, int cmd)
{
    dev->qic_last_cmd = (uint8_t) cmd;

    /*
       Only errors and finished reports were ever logged, which leaves
       every command that works invisible - and it is the ones that work
       that a host waits on.
     */
    ditto_log("Ditto: QIC-117 command %i (params %i)\n", cmd,
              qic_param_count(cmd));

    /*
       Soft reset is the way out of a latched error, so it is the one
       command that has to go through whatever state the drive is in.
     */
    if ((cmd != QIC_RESET) && (qic_check_command(dev, cmd) < 0))
        return;

    switch (cmd) {
        case QIC_RESET:
            /*
               Back to how the drive came up, which includes the power on
               reset the host must read out before it will be believed.
             */
            dev->qic_status     = QIC_STATUS_READY;
            dev->qic_mode_flags = QIC_MODE_PRIMARY;
            dev->qic_running    = 0;
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

        case QIC_REPORT_DRIVE_CONFIG:
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
        case QIC_ENTER_FORMAT_MODE:
        case QIC_ENTER_VERIFY_MODE:
            /* The three are exclusive: entering one leaves the others. */
            dev->qic_mode_flags &= (uint8_t) ~QIC_MODE_EXCLUSIVE;
            if (cmd == QIC_ENTER_FORMAT_MODE)
                dev->qic_mode_flags |= QIC_MODE_FORMAT;
            else if (cmd == QIC_ENTER_VERIFY_MODE)
                dev->qic_mode_flags |= QIC_MODE_VERIFY;
            else
                dev->qic_mode_flags |= QIC_MODE_PRIMARY;
            break;

        case QIC_SEEK_LOAD_POINT:
            /*
               Winds to the load point: the first segment of the track,
               which on an odd track is at the far physical end.
             */
            qic_stop_motion(dev);
            qic_seek_segment(dev, qic_track_start(dev));
            break;

        case QIC_LOGICAL_FORWARD:
            /*
               Sets the cartridge streaming forward under the head. It
               runs to the end of the track and stops there by itself -
               the host never stops it, it just waits for ready to come
               back as the sign that the pass is over.
             */
            qic_start_motion(dev);
            break;

        case QIC_PHYSICAL_REVERSE:
        case QIC_PHYSICAL_FORWARD:
            /*
               A high speed wind to one physical end. Which end of the
               track that is depends on which way the track runs.
             */
            qic_stop_motion(dev);
            if ((cmd == QIC_PHYSICAL_REVERSE) != (dev->qic_reverse != 0))
                qic_seek_segment(dev, qic_track_start(dev));
            else
                qic_seek_segment(dev, qic_track_start(dev) +
                                      qic_segments_per_track(dev) - 1);
            break;

        case QIC_STOP_TAPE:
        case QIC_PAUSE:
        case QIC_MICRO_STEP_PAUSE:
            /*
               Stopping means stopping: a wind already under way is
               halted where it stands and the drive answers ready again
               straight away, rather than seeing the wind out. A host
               that stops a wind and is still told the drive is busy
               gets its next command refused for the wrong reason.
             */
            qic_stop_motion(dev);
            if (dev->qic_busy) {
                timer_disable(&dev->busy_timer);
                dev->qic_busy = 0;
            }
            qic_update_status(dev);
            break;

        case QIC_SEEK_HEAD_TO_TRACK:
            /*
               A lateral move rather than a wind, so it settles quickly
               whatever track it starts from. Odd tracks run backwards.

               How far the head reaches is fixed by the mechanism, so a
               track beyond the cartridge is refused and the head stays
               where it is rather than addressing tape that is not there.
             */
            if (dev->qic_param[0] >= dev->tape_tracks) {
                qic_set_error(dev, QIC_ERROR_ILLEGAL_SEEK_TRACK, (uint8_t) cmd);
                break;
            }
            dev->qic_track   = dev->qic_param[0];
            dev->qic_reverse = dev->qic_track & 1;
            qic_stop_motion(dev);
            dev->qic_segment = qic_track_start(dev);
            dev->qic_sector = 0;
            qic_begin_busy(dev, DITTO_HEAD_SEEK_US);
            break;

        case QIC_SKIP_REVERSE:
        case QIC_SKIP_FORWARD:
        case QIC_SKIP_EXTENDED_REVERSE:
        case QIC_SKIP_EXTENDED_FORWARD: {
            const int back = (cmd == QIC_SKIP_REVERSE) ||
                             (cmd == QIC_SKIP_EXTENDED_REVERSE);
            /*
               The extended forms are not a coarser step, only a wider
               count: three nibbles instead of two, reaching 4096 gaps
               rather than 256. Scaling them was a stand-in for the third
               nibble that was not being read, and now that it is, it
               would multiply every long skip by 32.
             */
            const int count = qic_param_value(dev, cmd) + 1;

            qic_stop_motion(dev);
            qic_seek_segment(dev, dev->qic_segment + (back ? -count : count));
            break;
        }

        case QIC_WRITE_REFERENCE_BURST:
            /*
               Laying the reference burst down is a whole tape operation
               and takes the longest of any command in the set.

               It is also the thing that makes a blank cartridge
               referenced - that burst at the load point is what the
               drive later finds to know where the segments lie, and
               until it exists the cartridge reads as unformatted. A
               drive that winds the tape and records nothing leaves the
               status unchanged afterwards, so a host that writes the
               burst and checks for it writes it again, and then gives
               up: that is the unrecoverable error preformatting.

               Any nonzero extent will do, the tape being referenced by
               its first byte rather than its length; the real segments
               arrive as the format writes them.
             */
            qic_stop_motion(dev);
            dev->qic_segment = qic_track_start(dev);
            dev->qic_sector = 0;
            if (dev->image_size == 0)
                dev->image_size = 1;
            qic_begin_busy(dev, DITTO_WIND_FULL_US);
            break;

        case QIC_CALIBRATE_TAPE_LENGTH:
            /*
               Calibrating is the drive running the cartridge end to end
               to find out how much tape there is. What it comes back
               with is the segments per track, and that is the answer
               Report Format Segments gives from here on - before a
               calibration it has nothing to report and says zero.
               Winding without recording anything leaves it saying zero
               afterwards too, which a host reads as a cartridge with no
               capacity: it is what made Ditto Tools call a perfectly
               good tape unwriteable.
             */
            dev->qic_running         = 0;
            dev->qic_format_segments = (uint16_t) dev->tape_spt;
            qic_begin_busy(dev, DITTO_WIND_FULL_US);
            break;

        case QIC_SELECT_RATE:
            /*
               The plain rate selection knows the three rates this drive
               can run at. Everything else it is offered is a format
               selector, and a value that is neither is refused.

               The codes are not in any order that reads sensibly: 1 is
               2 Mbit/s, 2 is 500 kbit/s and 3 is 1 Mbit/s, which is why
               the slow one looks out of place sitting between the two
               fast ones. Leaving 500 out is not a small omission - it
               is the rate a host drops to for the format pass, so a
               drive that refuses it takes the error at the moment
               formatting begins and never lays a segment down.
             */
            switch (dev->qic_param[0]) {
                case 2:
                    dev->qic_config   = (uint8_t) ((dev->qic_config & ~QIC_CONFIG_RATE_MASK) |
                                                   (QIC_RATE_500 << QIC_CONFIG_RATE_SHIFT));
                    /* Reported in whole Mbit/s, and this is under one. */
                    dev->qic_ext_rate = 0;
                    break;

                case 1:
                    dev->qic_config   = (uint8_t) ((dev->qic_config & ~QIC_CONFIG_RATE_MASK) |
                                                   (QIC_RATE_2000 << QIC_CONFIG_RATE_SHIFT));
                    dev->qic_ext_rate = 2;
                    break;

                case 3:
                    dev->qic_config   = (uint8_t) ((dev->qic_config & ~QIC_CONFIG_RATE_MASK) |
                                                   (QIC_RATE_1000 << QIC_CONFIG_RATE_SHIFT));
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
            /* Three nibbles, low first, for a count of up to 4095. */
            dev->qic_format_segments = (uint16_t) qic_param_value(dev, cmd);
            break;

        /*
           Drive selection off the command channel rather than the
           floppy controller's own select lines, for a drive sharing a
           cable with something else. There is nothing to arbitrate
           here, so the address is accepted and nothing moves - but the
           commands have to be answered, because a host that is refused
           takes it as the drive not being the one it is looking for.
         */
        case QIC_SOFT_SELECT:
        case QIC_SOFT_DESELECT:
            break;

        /*
           How long the drive may take over a command before the host
           gives up on it. Nothing here is paced by that - the timings
           come from the tape - so it is taken and forgotten. The Windows
           host sends it a dozen or more times a session, so leaving it
           to the default arm below only filled the trace with claims
           that it was unimplemented.
         */
        case QIC_ALTERNATE_TIMEOUT:
            break;

        default:
            /*
               Whatever is left is a command the set has but this drive
               does not model. Accept it quietly rather than refusing: a
               host that is refused latches the error and stops. Codes
               that are not in the set at all never reach here -
               qic_check_command() has already turned them away with
               "undefined command".
             */
            ditto_log("Ditto: QIC-117 command %i accepted and ignored\n", cmd);
            break;
    }

    qic_update_status(dev);
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
        qic_set_error(dev, QIC_ERROR_ILLEGAL_IN_REPORT, (uint8_t) steps);
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

/* The tape has finished going past the head: hand over the result. */
static void
fdc_xfer_done(void *priv)
{
    ditto_t *dev = (ditto_t *) priv;

    timer_disable(&dev->xfer_timer);

    if (dev->fdc_phase != FDC_PHASE_EXEC)
        return;

    fdc_begin_result(dev, dev->xfer_res_len);
    fdc_raise_irq(dev);
}

/*
   A transfer takes as long as the tape it covers takes to pass the head.
   The bytes have already been moved - the bridge buffers them, so there
   is nothing to clock out - but the controller has to look busy for that
   long, because the host's whole idea of a transfer is built around it.

   Finishing instantly is not a harmless shortcut. The host writes the
   command, finds the result waiting before the tape could possibly have
   moved, and reads that as a refusal.
 */
static void
fdc_begin_exec(ditto_t *dev, int sectors, int res_len)
{
    uint64_t us;

    dev->xfer_res_len = res_len;
    dev->fdc_res_pos  = 0;
    dev->fdc_res_len  = 0;

    if (sectors <= 0) {
        fdc_begin_result(dev, res_len);
        fdc_raise_irq(dev);
        return;
    }

    dev->fdc_phase = FDC_PHASE_EXEC;

    us = (qic_stream_period_us(dev) * (uint64_t) sectors) /
         FDD_TAPE_SECTORS_PER_SEG;
    if (us < 1)
        us = 1;

    timer_disable(&dev->xfer_timer);
    timer_set_delay_u64(&dev->xfer_timer, us * TIMER_USEC);
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

/*
   A data transfer between the cartridge and the bridge's buffer.

   There is no host DMA controller anywhere in this - the bridge has its
   own 128 KB of memory and the controller moves sectors straight into
   it, at the address the host programmed as if it were a DMA base. The
   host collects the data afterwards, over the wire, at its own pace. So
   a transfer needs no clocking here at all: it is a copy, and then an
   interrupt.
 */
static void
fdc_transfer(ditto_t *dev, uint8_t unit, int writing)
{
    const int cyl   = dev->fdc_cmd[2];
    const int head  = dev->fdc_cmd[3];
    const int first = dev->fdc_cmd[4];
    const int last  = dev->fdc_cmd[6];
    uint32_t  addr  = dev->dma_addr & DITTO_BUFFER_MASK;
    uint32_t  left  = dev->dma_count + 1;
    uint8_t   sec[FDD_TAPE_SECTOR_SIZE];
    int       sector;
    uint32_t  offset;

    if (writing && (dev->readonly || (dev->fp == NULL))) {
        fdc_data_result(dev, (uint8_t) (FDC_ST0_ABNORMAL | unit),
                        FDC_ST1_WRITE_PROTECT, 0x00);
        fdc_raise_irq(dev);
        return;
    }

    if (!dev->image_loaded) {
        fdc_data_result(dev, (uint8_t) (FDC_ST0_ABNORMAL | unit),
                        FDC_ST1_MISSING_AM | FDC_ST1_NO_DATA, 0x00);
        fdc_raise_irq(dev);
        return;
    }

    /*
       What ends a transfer is the DMA count running out, not the last
       sector number. EOT stops it too if the count is generous enough to
       reach it, and running off the end of the cylinder stops it in the
       same way - but the count comes first, because that is the terminal
       count a controller of this family is wired to obey.

       Treating EOT as the only limit made a whole class of command move
       nothing at all and report that it had. This host issues every read
       and every write twice: once addressed properly, and then - after
       waiting on the first, giving up and restarting the controller -
       again with EOT set one below the first sector, which is its way of
       saying "as much as the count allows". Fifty-three of the writes in
       a single backup take that form. On a write it went unnoticed,
       because the first attempt had already put the data down; on a read
       it handed back a buffer of whatever was in it before, which the
       host read as having run off the end of the volume.
     */
    for (sector = first; left >= FDD_TAPE_SECTOR_SIZE; sector++) {
        if (!ditto_sector_offset(dev, cyl, head, sector, &offset)) {
            /* Off the end of the cylinder is where the tape ran out, not
               a bad address - unless it was the address we were given. */
            if (sector != first)
                break;

            ditto_log("Ditto: C %i H %i R %i is not on this cartridge\n",
                      cyl, head, sector);
            fdc_data_result(dev, (uint8_t) (FDC_ST0_ABNORMAL | unit),
                            FDC_ST1_NO_DATA, 0x00);
            fdc_raise_irq(dev);
            return;
        }

        if (writing) {
            for (int i = 0; i < FDD_TAPE_SECTOR_SIZE; i++)
                sec[i] = dev->buffer[(addr + (uint32_t) i) & DITTO_BUFFER_MASK];

            if (!ditto_image_write(dev, offset, sec, sizeof(sec))) {
                fdc_data_result(dev, (uint8_t) (FDC_ST0_ABNORMAL | unit),
                                FDC_ST1_WRITE_PROTECT, 0x00);
                fdc_raise_irq(dev);
                return;
            }
        } else {
            ditto_image_read(dev, offset, sec, sizeof(sec));

            for (int i = 0; i < FDD_TAPE_SECTOR_SIZE; i++)
                dev->buffer[(addr + (uint32_t) i) & DITTO_BUFFER_MASK] = sec[i];
        }

        /*
           The head is wherever the data is. Moving it with the transfer
           rather than leaving it to the free running clock is what keeps
           the drive's idea of its position and the host's in step: the
           clock advances a segment every few milliseconds whether or not
           anything is being written, so a host that writes a segment,
           polls, writes the next and so on would watch the head run away
           from it and reach the end of the track within a second - which
           it reads as the pass being over after a few hundred kilobytes.
         */
        dev->qic_segment = (head * dev->segs_per_head) +
                           (cyl * dev->segs_per_cyl) +
                           ((sector - 1) / FDD_TAPE_SECTORS_PER_SEG);
        dev->qic_sector  = (sector - 1) % FDD_TAPE_SECTORS_PER_SEG;

        addr += FDD_TAPE_SECTOR_SIZE;
        left -= FDD_TAPE_SECTOR_SIZE;

        if (sector == last) {
            sector++;
            break;
        }
    }

    /*
       Let the clock carry on from where the transfer left the head, so a
       pass that the host stops feeding still runs out and ends, but one
       it keeps feeding is paced by the writing rather than by the timer.
     */
    if (dev->qic_running) {
        timer_disable(&dev->motion_timer);
        timer_set_delay_u64(&dev->motion_timer,
                            qic_stream_period_us(dev) * TIMER_USEC);
    }

    qic_update_status(dev);

    ditto_image_sync(dev);

    /*
       A normal end leaves the address one past the last sector moved,
       which is how the host works out how much actually went across.
     */
    dev->fdc_res[0] = unit;
    dev->fdc_res[1] = 0x00;
    dev->fdc_res[2] = 0x00;
    dev->fdc_res[3] = (uint8_t) cyl;
    dev->fdc_res[4] = (uint8_t) head;
    dev->fdc_res[5] = (uint8_t) sector;
    dev->fdc_res[6] = dev->fdc_cmd[5];

    fdc_begin_exec(dev, sector - first, 7);
}

/*
   Formatting. The buffer holds four bytes of sector address for each
   sector to be laid down, and every one of them gets written out full of
   the filler byte.
 */
/*
   READ ID: hands back the address of the sector under the head, which is
   how the host works out where on the tape it is.

   Refusing it is not a small gap. The host driver reads an ID to locate
   the head and keeps the C/H/R it gets back as the origin for its seek
   arithmetic, so a drive that will not answer leaves it with no idea
   where anything is - which it reports as the drive having been given an
   invalid command, because that is what it was told.

   A stopped tape answers with its real position rather than a fixed zero,
   for the same reason: a host told the head sits at segment 0 wherever it
   really is computes every later seek from the wrong place.
 */
static void
fdc_read_id(ditto_t *dev, uint8_t unit)
{
    int segment;
    int cyl;
    int head;
    int sector;

    if (!dev->image_loaded) {
        fdc_data_result(dev, (uint8_t) (FDC_ST0_ABNORMAL | unit),
                        FDC_ST1_MISSING_AM | FDC_ST1_NO_DATA, 0x00);
        fdc_raise_irq(dev);
        return;
    }

    segment = dev->qic_segment;
    head    = segment / dev->segs_per_head;
    cyl     = (segment % dev->segs_per_head) / dev->segs_per_cyl;
    sector  = ((segment % dev->segs_per_cyl) * FDD_TAPE_SECTORS_PER_SEG) +
              dev->qic_sector + 1;

    ditto_log("Ditto: read ID -> segment %i (c=%i h=%i r=%i)%s\n",
              segment, cyl, head, sector, dev->qic_running ? "" : " [stopped]");

    dev->fdc_res[0] = unit;
    dev->fdc_res[1] = 0x00;
    dev->fdc_res[2] = 0x00;
    dev->fdc_res[3] = (uint8_t) cyl;
    dev->fdc_res[4] = (uint8_t) head;
    dev->fdc_res[5] = (uint8_t) sector;
    dev->fdc_res[6] = 3; /* 1024 bytes to a sector */
    fdc_begin_result(dev, 7);

    /*
       A search is clocked by the host, not by us: reading an ID carries
       the head on one sector, so a host reading IDs to find a segment
       walks forward a sector per read and stops exactly on its target
       rather than overshooting between polls. Only while the tape is
       actually moving - a stopped cartridge stays where it was put.
     */
    if (dev->qic_running) {
        const int end = qic_track_start(dev) + qic_segments_per_track(dev) - 1;

        if (++dev->qic_sector >= FDD_TAPE_SECTORS_PER_SEG) {
            dev->qic_sector = 0;
            if (dev->qic_segment < end)
                dev->qic_segment++;
        }
        qic_update_status(dev);
    }

    fdc_raise_irq(dev);
}

static void
fdc_format(ditto_t *dev, uint8_t unit)
{
    const int count  = dev->fdc_cmd[3];
    const uint8_t fill = dev->fdc_cmd[5];
    uint32_t  addr   = dev->dma_addr & DITTO_BUFFER_MASK;
    uint8_t   sec[FDD_TAPE_SECTOR_SIZE];
    uint32_t  offset;
    int       wrote   = 0;
    int       skipped = 0;

    if (dev->readonly || (dev->fp == NULL)) {
        fdc_data_result(dev, (uint8_t) (FDC_ST0_ABNORMAL | unit),
                        FDC_ST1_WRITE_PROTECT, 0x00);
        fdc_raise_irq(dev);
        return;
    }

    memset(sec, fill, sizeof(sec));

    for (int i = 0; i < count; i++) {
        const int cyl    = dev->buffer[(addr + 0) & DITTO_BUFFER_MASK];
        const int head   = dev->buffer[(addr + 1) & DITTO_BUFFER_MASK];
        const int sector = dev->buffer[(addr + 2) & DITTO_BUFFER_MASK];

        addr += 4;

        if (!ditto_sector_offset(dev, cyl, head, sector, &offset)) {
            skipped++;
            continue;
        }

        if (!ditto_image_write(dev, offset, sec, sizeof(sec))) {
            fdc_data_result(dev, (uint8_t) (FDC_ST0_ABNORMAL | unit),
                            FDC_ST1_WRITE_PROTECT, 0x00);
            fdc_raise_irq(dev);
            return;
        }
        wrote++;
    }

    ditto_image_sync(dev);

    /*
       A format writes its segment far faster than tape runs, so the head
       is carried along by the writing rather than by the free running
       clock - left to itself the cartridge would reach the end of the
       track long before the host had finished laying it down, stop, and
       report the pass over while the host was still working.

       The drive stays in motion throughout: the host polls the status
       just after handing over the last segment of a track and has to
       find it busy there. Coming to rest is the end-of-track case in
       qic_motion_tick, which the last segment of the track reaches here.
     */
    if (dev->qic_running) {
        const int end = qic_track_start(dev) + qic_segments_per_track(dev) - 1;

        if (dev->qic_segment < end) {
            dev->qic_segment++;
            dev->qic_sector = 0;
            dev->run_off = 0;
            qic_update_status(dev);
        }

        /*
           Re-armed from now, never stopped here. The pass has to outlast
           the writing by a segment: the host polls the status a few
           milliseconds after handing over the last segment of a track
           and has to find the drive still busy, then waits for ready.
           Coming to rest on the same call that wrote the last segment
           would answer ready to that poll, which QBACKUP's format driver
           reads as the pass having failed rather than finished. Letting
           the clock stop it a segment-time later is what leaves the
           window open.
         */
        timer_disable(&dev->motion_timer);
        timer_set_delay_u64(&dev->motion_timer,
                            qic_stream_period_us(dev) * TIMER_USEC);
    }

    ditto_log("Ditto: formatted %i sectors, %i not on this cartridge\n",
              wrote, skipped);

    dev->fdc_res[0] = unit;
    dev->fdc_res[1] = 0x00;
    dev->fdc_res[2] = 0x00;
    dev->fdc_res[3] = 0x00;
    dev->fdc_res[4] = 0x00;
    dev->fdc_res[5] = 0x00;
    dev->fdc_res[6] = dev->fdc_cmd[2];
    fdc_begin_result(dev, 7);

    fdc_raise_irq(dev);
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
                case 0x06: case 0x0c:
                    fdc_transfer(dev, unit, 0);
                    break;

                case 0x05: case 0x09:
                    fdc_transfer(dev, unit, 1);
                    break;

                case 0x0d:
                    fdc_format(dev, unit);
                    break;

                case 0x0a:
                    fdc_read_id(dev, unit);
                    break;

                case 0x02:
                case 0x11: case 0x16:
                    /*
                       Read a whole track and compare. Nothing the host
                       does on this drive needs them.
                     */
                    ditto_log("Ditto: FDC command %02X not implemented\n", cmd);
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

        case FDC_PHASE_EXEC:
            /*
               Mid transfer the controller is busy and has nothing to say:
               command byte register busy, but no request for service,
               because in DMA mode the bytes go past the host entirely.

               Showing the result phase here instead - which is what
               finishing a transfer the instant the last command byte
               lands amounts to - reads to this host as the command
               having been thrown straight back at it. It polls the
               status forty times, restarts the controller and tries
               again, on every single transfer in both directions.
             */
            ret = FDC_MSR_CB;
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

            /*
               Reading the first result byte is what takes the interrupt
               down on a controller of this family - only seek and reset
               interrupts wait to be sensed. Leaving it up meant every
               later "is there anything outstanding?" answered yes.
             */
            if (dev->fdc_res_pos == 0)
                fdc_clear_irq(dev);

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
                timer_disable(&dev->xfer_timer);
                dev->xfer_res_len = 0;
                fdc_clear_irq(dev);

                /*
                   Anything half said down the command channel is dropped
                   with it. The channel is a count of step pulses measured
                   from where the controller believes the head is, and the
                   reset moves that belief to cylinder zero - so the host
                   starts its next sentence from a standing start and
                   expects to be heard from the beginning.

                   Leaving a parameter or a report outstanding here is not
                   a quiet kind of wrong. The host re-initialises the
                   controller in the middle of a backup, roughly once per
                   pair of segments, and follows every reset with a soft
                   select; a drive still owed a skip count reads that
                   select as the count instead, winds two dozen segments
                   off target, and then reads the select's own parameter
                   as a command - which is command 20, which does not
                   exist, which the host reports as the drive having been
                   given an invalid command and gives up.
                 */
                dev->report_pending  = 0;
                dev->qic_ack         = 0;
                dev->qic_params_left = 0;
                dev->qic_params_got  = 0;
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
            if (dev->ecc_error)
                ret |= BP_STAT_ECC_ERROR;
            break;

        case BP_REG_IRQ:
            ret = (uint8_t) (dev->irq_ctrl & BP_IRQ_MASK);
            if (dev->irq_pending)
                ret |= BP_IRQ_PENDING;
            break;

        case BP_REG_TEST:
            /*
               The protocol self test. Writing this register restarts the
               count, whatever value is written, and every read hands
               back the next number: 1, 2, 3 and so on. Hosts read it
               back dozens or hundreds of times to satisfy themselves
               that the wire protocol and its timing are sound.

               The absolute values matter. One host only checks that each
               read is one more than the last and would be content with
               any starting point, but another writes 0x7f and then
               requires exactly 1 through 16 - so the count cannot begin
               from whatever was written.
             */
            ret = ++dev->test_ctr;
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
            ditto_log("Ditto: ?? read of unhandled register %02X\n", reg);
            break;
    }

    ditto_log("Ditto:    RR %02X[%i] -> %02X\n", reg, idx, ret);

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

    ditto_log("Ditto:    WR %02X[%i] <- %02X\n", reg, idx, val);

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
            /* Any write restarts the count; the value itself is ignored. */
            dev->test_ctr = 0;
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
            /*
               Two bytes: what to do and how big the region is, then
               where it starts. The second one sets it going.
             */
            if (idx < 2)
                dev->ecc_ctrl[idx] = val;
            if (idx == 1) {
                if (dev->ecc_ctrl[0] & BP_ECC_GEN)
                    ditto_ecc_generate(dev);
                else if (dev->ecc_ctrl[0] & BP_ECC_CHECK)
                    ditto_ecc_check(dev);
            }
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
            ditto_log("Ditto: ?? write of unhandled register %02X\n", reg);
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

static uint8_t
ditto_connect_response(const ditto_t *dev)
{
    return (uint8_t) ((dev->proto >= DITTO_PROTO_EPP8)
                          ? (LPT_STAT_ACK | LPT_STAT_BUSY)
                          : LPT_STAT_ACK);
}

/*
   The pod's answer to the address probe that follows every knock: its
   own unit number, in the three status bits the host can see, and the
   complement of it when AUTOFD is low.

   Both halves are needed. The host reads the status twice with AUTOFD
   toggled between, and requires the two readings to be complements of
   one another across those three bits - a line stuck high or low, or a
   port with nothing on it, cannot produce that. Only then does it look
   at the reading itself and check it names the unit it knocked for.

   The address rides alongside the protocol family answer rather than
   replacing it, because the other host driver reads the same status at
   the same moment and wants only that. The two fit: one looks at ACK
   and BUSY and ignores the address bits, the other looks at the address
   bits and reads BUSY as which framing to use. Unit zero - the only one
   a single pod on a chain is ever asked for - leaves both readings
   exactly what the first driver already expected.
 */
static uint8_t
ditto_ident_response(const ditto_t *dev)
{
    const uint8_t id = (dev->ctrl & LPT_CTRL_AUTOFD)
                           ? dev->unit
                           : (uint8_t) ~dev->unit;

    return (uint8_t) (ditto_connect_response(dev) | ((id & 0x07) << 3));
}

static void
ditto_connect(ditto_t *dev)
{
    dev->connected   = 1;
    dev->knock       = 0;
    dev->ident       = 1;
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

    /* The bridge answers with which family of protocols it is set for. */
    dev->rd_out = ditto_connect_response(dev);

    ditto_log("Ditto: connected in %s mode\n", ditto_proto_name[dev->proto]);

    /*
       A transfer mode the port cannot carry is worth saying out loud
       once. The host can program the bridge into EPP and never issue a
       single EPP cycle to it, because the super I/O maps base+3 and
       base+4 only when it has EPP switched on - so the mode looks live
       from here while nothing whatsoever reaches us through it, and the
       host quietly falls back after its protocol self test reads all
       ones. Said once per session rather than per connect: the host
       connects tens of thousands of times.
     */
    if ((dev->proto >= DITTO_PROTO_EPP8) && !dev->epp_warned) {
        dev->epp_warned = 1;
        ditto_stat("Ditto: EPP selected; the port %s carry EPP cycles\n",
                   lpt_port_offers_epp(dev->lpt) ? "does" : "DOES NOT");
    }
}

static void
ditto_disconnect(ditto_t *dev)
{
    if (!dev->connected)
        return;

    dev->connected    = 0;

    dev->knock        = 0;
    dev->ident        = 0;
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
        dev->dat_out = 0xff;
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
        val          = backpack_read_reg(dev, dev->cur_reg);
        dev->rd_out  = 0x00;
        dev->dat_out = val;
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
        ditto_log("Ditto: W3 %02X (EPP address)\n", val);
        dev->cur_reg   = val;
        dev->xfer_idx  = 0;
        dev->rd_high   = 0;
        dev->mem_burst = 0;
    } else {
        ditto_log("Ditto: W4 %02X (EPP data)\n", val);
        backpack_write_reg(dev, dev->cur_reg, val);
    }
}

static void
ditto_epp_request_read(uint8_t is_addr, void *priv)
{
    ditto_t *dev = (ditto_t *) priv;
    uint8_t  val = 0xff;

    ditto_log("Ditto: R%d (EPP %s)\n", is_addr ? 3 : 4,
              is_addr ? "address" : "data");

    if (!is_addr && ditto_proto_usable(dev) && (dev->proto >= DITTO_PROTO_EPP8))
        val = backpack_read_reg(dev, dev->cur_reg);

    dev->dat_out = val;
    if (dev->lpt != NULL)
        lpt_write_to_dat(dev->lpt, val);
}

/*
   A one line summary of where the conversation has got to, appended to
   every traced access. Reading a trace of a driver that will not talk to
   us, the question is always the same: did it knock, did the knock take,
   and which register did it think it was addressing.
 */
static const char *
ditto_state(const ditto_t *dev)
{
    static char buf[64];

    snprintf(buf, sizeof(buf), "%s reg=%02X knock=%d%s",
             dev->connected ? "CONN" : "----", dev->cur_reg, dev->knock,
             dev->latching ? " latch" : "");

    return buf;
}

static void
ditto_write_data(uint8_t val, void *priv)
{
    ditto_t      *dev = (ditto_t *) priv;
    const uint8_t old = dev->dat;

    ditto_note_idle(dev);

    ditto_log("Ditto: W0 %02X            [%s]\n", val, ditto_state(dev));

    dev->dat = val;

    /*
       Putting an address on the data lines is what starts a knock, so
       any toggles counted before it were not part of one.

       Without this the setup write that precedes the address gets
       counted whenever it happens to move SELECT - which it does from
       most starting states - and the knock then completes an edge early,
       on the wrong half of the toggle. The link comes up, and the very
       next edge is read as the host finishing with us instead of as the
       first half of the address probe.

       Counting from a data write is safe in a way that arming on one is
       not: the port calls us for every write, whether or not the value
       changed, so a host that writes an address equal to what was
       already there still starts a knock here.
     */
    dev->knock = 0;

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
    const uint8_t chg       = (uint8_t) ((old ^ val) & LPT_CTRL_LINES);
    const uint8_t lines     = (uint8_t) (val & LPT_CTRL_LINES);
    const uint8_t old_lines = (uint8_t) (old & LPT_CTRL_LINES);

    ditto_note_idle(dev);


    ditto_log("Ditto: W2 %02X (was %02X)  [%s]\n", val, old, ditto_state(dev));

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
       Letting go: the host drops INIT leaving AUTOFD alone, and then
       raises INIT and SELECT together. It is that pair of steps taken in
       order that ends the link - neither half means anything by itself.

       Both halves look ordinary on their own, which is the trap. SELECT
       rising cannot be the signal: one host driver never touches SELECT
       once the link is up, but another toggles it throughout as a matter
       of course, and reading that as a disconnect drops the link within
       a few writes of every connect. Nor can the AUTOFD-alone state be
       the signal: the first driver's nibble reads pass through it twice
       for every byte they fetch. Only the transition between them is
       unambiguous, and neither driver makes it by accident.
     */
    if (dev->connected && (old_lines == LPT_CTRL_AUTOFD) &&
        (lines & LPT_CTRL_SELECT)) {
        ditto_disconnect(dev);
        return;
    }

    /*
       The address probe that closes the knock. The host has the link but
       has not accepted it yet: it toggles AUTOFD once and reads the
       status either side of the toggle, and the pod answers with its own
       address and then the complement of it. Toggling SELECT afterwards
       is the host satisfied, and ordinary register traffic begins.

       Nothing else may look at these two AUTOFD edges. Once the link is
       up an AUTOFD edge means "latch the register number off the data
       lines", and the data lines are still carrying the unit number from
       the knock - so without this the probe silently addresses register
       zero and the host reads a nibble of it where it expected an
       address, twice, and concludes the port is empty. That is precisely
       what both host drivers were doing: connecting, finding no pod, and
       giving up without ever touching a register.
     */
    if (dev->ident) {
        if (chg & LPT_CTRL_SELECT)
            dev->ident = 0;
        return;
    }

    /*
       The connect knock: three toggles of SELECT while the other three
       lines hold INIT and nothing else, with the unit address sitting on
       the data lines.

       What makes an edge count is the state of the other lines, not some
       earlier write that "armed" it. A host is entitled to arrive in the
       knocking state without writing anything - if the control register
       already holds INIT then its write of INIT moves nothing, and there
       is no event for an arming rule to catch, so it misses the knock
       altogether. Reading it as a level also throws out the stray
       counts: everything either host does between knocks passes through
       states that are not this one, and each of those clears the count.
     */
    if ((lines & ~LPT_CTRL_SELECT) != LPT_CTRL_INIT)
        dev->knock = 0;
    else if (chg & LPT_CTRL_SELECT) {
        if (++dev->knock >= 3) {
            dev->knock = 0;

            /*
               The knock is addressed. The host puts a unit number on the
               data lines just before the toggles, and only the pod that
               answers to it may take the link - these things chain, so
               several may be listening.

               Answering to every address looks harmless and is not. The
               host decides a unit is present by the status changing
               between a reading taken before the knock and one taken
               after, so a pod that answers to all of them presents the
               same thing both times and is found at none of them. That
               is exactly what happened: 260 knocks, every one accepted,
               every unit reported empty.

               A knock for somebody else changes nothing here, which is
               what leaves the reading before and after identical and
               tells the host that address is empty.
             */
            if (dev->dat == dev->unit)
                ditto_connect(dev);
            else
                ditto_log("Ditto: knock for unit %02X, not ours (%02X)\n",
                          dev->dat, dev->unit);
        }

        return;
    }

    if (!dev->connected)
        return;

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

        /*
           Addressing a register also puts the bridge's own answer back
           on the status lines. That is what the host reads straight
           after a connect - and it has to be a level the bridge holds,
           not a one-shot: a host that reads the status without clocking
           anything, as one does thousands of times over while it waits,
           must keep seeing the same answer rather than have it expire
           under it.
         */
        dev->rd_out = ditto_connect_response(dev);
        return;
    }

    if (chg & LPT_CTRL_INIT) {
        if (lines & LPT_CTRL_STROBE)
            backpack_write_reg(dev, dev->cur_reg, dev->dat);
        else
            ditto_advance_read(dev);
    }
}

/*
   The host reading the data lines. The bridge only drives them in byte
   and EPP modes, so there is usually nothing to do here beyond noting
   that it happened - but that half of the conversation is invisible
   otherwise, and a driver that reads data where it was expected to read
   status looks, from this side, like a driver doing nothing at all.
 */
static void
ditto_read_data(void *priv)
{
    ditto_t *dev = (ditto_t *) priv;

    ditto_note_idle(dev);

    ditto_log("Ditto: R0 -> %02X (dir %s)  [%s]\n", dev->dat_out,
              (dev->ctrl & LPT_CTRL_DIR) ? "in" : "out", ditto_state(dev));
}

static uint8_t
ditto_read_status(void *priv)
{
    ditto_t *dev = (ditto_t *) priv;
    uint8_t  ret;

    ditto_note_idle(dev);

    if (dev->connected && dev->ident)
        ret = ditto_ident_response(dev);
    else if (dev->connected)
        ret = dev->rd_out;
    else
        /* Not connected: the bridge leaves the status lines alone. */
        ret = LPT_STAT_IDLE;

    ret &= LPT_STAT_MASK;

    /*
       Status reads while idle are how a host spins waiting for something,
       so only trace them when the answer changes - otherwise a poll loop
       buries everything else.
     */
    if ((ret != dev->last_status) || dev->connected) {
        ditto_log("Ditto: R1 -> %02X          [%s]\n", ret, ditto_state(dev));
        dev->last_status = ret;
    }

    return ret;
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
    dev->unit     = (uint8_t) device_get_config_int("unit");

    /*
       The drive's own identity, which the cartridge does not change: a
       Ditto 2GB reading a QIC-80 tape is still a Ditto 2GB, and a host
       settles on the highest format the two have in common. It powers up
       at the slow rate, as fdd_tape.c's drive does, until the host
       selects one. Set before the geometry, because that is what fills
       in the parts of this the cartridge does decide.
     */
    dev->qic_vendor_id  = DITTO_VENDOR_ID;
    dev->qic_config     = (uint8_t) (QIC_CONFIG_80 |
                                     (QIC_RATE_500 << QIC_CONFIG_RATE_SHIFT));
    dev->qic_ext_rate   = 2;

    dev->capacity = ditto_cartridge(device_get_config_int("capacity"))->value;

    /* The geometry has to be settled before an image is measured
       against it. */
    ditto_set_geometry(dev);

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

    ditto_ecc_build_table(dev->ecc_mul);
    ditto_image_load(dev, dev->image_fn);

    /*
       The state the drive powers up in. Its identity, and everything the
       cartridge settles, are already in place above. A cartridge that was
       already in the drive is not a new one, but the power on reset
       itself has to be read out before the drive will do anything else -
       that is how the host learns it has been reset.
     */
    dev->qic_mode_flags = QIC_MODE_PRIMARY;
    dev->qic_status     = QIC_STATUS_READY;

    timer_add(&dev->busy_timer, qic_busy_done, dev, 0);
    timer_add(&dev->motion_timer, qic_motion_tick, dev, 0);
    timer_add(&dev->xfer_timer, fdc_xfer_done, dev, 0);
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

    lpt_set_read_data(device_get_instance() - 1, ditto_read_data);

    ditto_log("Ditto: attached, protocol ceiling %s, image \"%s\"%s\n",
              ditto_proto_name[dev->max_proto], dev->image_fn,
              dev->readonly ? " (write protected)" : "");

    return dev;
}

static void
ditto_close(void *priv)
{
    ditto_t *dev = (ditto_t *) priv;

    timer_disable(&dev->busy_timer);
    ditto_image_close(dev);
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
        /*
           Which address this pod answers a knock at. These chain, so the
           host puts a unit number on the data lines and only the one it
           names may reply. Zero unless the drive's own jumpers say
           otherwise, and the host works through the rest looking for
           anything else on the cable.
         */
        .name           = "unit",
        .description    = "Unit address",
        .type           = CONFIG_SPINNER,
        .default_string = NULL,
        .default_int    = 0,
        .file_filter    = NULL,
        .spinner        = { .min = 0, .max = 7, .step = 1 },
        .selection      = { { 0 } },
        .bios           = { { 0 } }
    },
    {
        /*
           Which cartridge is in the drive. This settles the tape geometry
           outright rather than it being guessed from whatever image
           happens to be loaded, and it settles what Report Tape Status
           says about the media. It does not change the drive: that is a
           Ditto 2GB whatever is loaded into it.

           The list is the one in DITTO_CARTRIDGE_LIST, expanded here so
           that the dialog cannot come to offer something the drive does
           not have. The key is still called "capacity" and the first
           three still have their original values, so a machine saved
           before the rest existed comes up with the cartridge it had.
         */
        .name           = "capacity",
        .description    = "Cartridge",
        .type           = CONFIG_SELECTION,
        .default_string = NULL,
        .default_int    = DITTO_CAPACITY_DEFAULT,
        .file_filter    = NULL,
        .spinner        = { 0 },
        .selection      = {
/* The parameters may not be named after the members they
   initialise: a designator is a plain token, so a parameter
   called "value" would be substituted into ".value" itself. */
#define X(nm, val, trk, sgs, stat, fmt) { .description = nm, .value = val },
            DITTO_CARTRIDGE_LIST
#undef X
            { .description = "" }
        },
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
    .name          = "Iomega Ditto 2GB",
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
