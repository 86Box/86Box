/*
 * 86Box     A hypervisor and IBM PC system emulator.
 *
 *           Shuttle Technology EPAT / EPATRM parallel-port to ATAPI bridge.
 *
 *           This is the bridge used by the Imation SuperDisk (LS-120)
 *           parallel-port drive. 86Box already models the drive itself in
 *           rdisk.c ("IMATION / SUPERDISK 120 ATAPI"); what was missing was
 *           the bridge that carries ATAPI over a parallel port. RDISK_BUS_LPT
 *           exists in rdisk.h and is referenced by no source file.
 *
 *           Protocol source: reverse-engineered from Imation's own DOS driver
 *           and VERIFIED ON REAL HARDWARE (a Shuttle EPATRM bridge with a
 *           Matsushita LS-120 COSM 04, on an IBM 5160). Cross-checked against
 *           Linux's drivers/block/paride/epat.c. Every constant below has a
 *           captured hardware reading behind it; see the references in each
 *           comment.
 *
 * Authors:  Mike Lycett and contributors
 *
 *           Copyright 2026 Mike Lycett.
 */
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>
#define HAVE_STDARG_H
#include <86box/86box.h>
#include <86box/timer.h>
#include <86box/device.h>
#include <86box/lpt.h>
#include <86box/scsi.h>
#include <86box/scsi_device.h>
#include <86box/hdc_ide.h>
#include <86box/rdisk.h>
#include <86box/plat_unused.h>
#include <86box/log.h>

#ifdef ENABLE_EPAT_LOG
int epat_do_log = ENABLE_EPAT_LOG;

static void
epat_log(void *priv, const char *fmt, ...)
{
    if (epat_do_log) {
        va_list ap;
        va_start(ap, fmt);
        log_out(priv, fmt, ap);
        va_end(ap);
    }
}
#else
#    define epat_log(priv, fmt, ...)
#endif

/*
 * The unlock frame. Eight bytes to the data port, each written TWICE - the
 * second write is the I/O delay, not a retry - then committed by pulsing
 * nINIT. The eighth byte is the command: 0xE0 connect, 0x30 disconnect.
 *
 * TRANSPORT_SPEC.md section 3, replayed byte-for-byte on the real machine
 * with all three checkpoints matching, reproducibly, twice.
 */
static const uint8_t epat_unlock[7] = { 0x22, 0xAA, 0x55, 0x00, 0xFF, 0x87, 0x78 };

#define EPAT_CPP_CONNECT    0xE0
#define EPAT_CPP_DISCONNECT 0x30

/*
 * Checkpoint values the real bridge returns on the status port part-way
 * through the unlock frame. The driver masks these; the raw readings are what
 * the hardware gave, so returning them exactly makes a capture comparison
 * meaningful rather than approximate.
 *
 *   after 22 AA 55 00 FF : status & 0xF0 == 0xB0   hardware read 0xB8
 *   after 87             : status & 0xF0 == 0x50   hardware read 0x58
 *   after 78             : status & 0xB0 == 0xB0   hardware read 0xF0
 */
#define EPAT_CHK_1 0xB8
#define EPAT_CHK_2 0x58
#define EPAT_CHK_3 0xF0

/* Idle status when connected but not mid-transfer. */
#define EPAT_STAT_IDLE 0x38

typedef enum {
    EPAT_UNLOCK_IDLE = 0, /* not in an unlock frame           */
    EPAT_UNLOCK_RUN,      /* matching the seven-byte preamble */
    EPAT_UNLOCK_CMD       /* preamble matched, command byte next */
} epat_unlock_state_t;

typedef struct epat_s {
    void *lpt;
    void *log;

    /*
     * The drive this bridge fronts, or NULL when no removable disk is
     * assigned to this parallel port. With no drive the bridge still speaks
     * the protocol against its own regs[], which is useful for testing the
     * wire format on its own.
     */
    scsi_device_t *sd;
    ide_tf_t      *tf;
    uint8_t        port;
    uint8_t        no_drive; /* looked up once, and there was none */

    /*
     * Block mode. The CDB and all sector data move through register 7,
     * which is NOT the task file's data register - it is the bridge's own
     * streaming port, addressed without cont_map, and it has a protocol and
     * an alternating phase bit of its own. epat.c's read_block/write_block.
     */
    int     block;       /* EPAT_BLOCK_NONE / _READ / _WRITE */
    /*
     * The ECP/EPP fast path is framed differently from the nibble one: a
     * single ADDRESS cycle carries the command (0x80 read, 0xC0 write, 0xA0
     * last byte of a read) and the data cycles follow. Kept separate from
     * `block` above so leaving ECP mode needs no teardown - the SPP register
     * path never consults it.
     */
    uint8_t ecp_cmd;
    /*
     * CPP commands that return data. The host addresses the chain with
     * CPP(0x10 | unit) and reads a 16-bit id back as four nibbles through the
     * status register; a unit answers 0xFFAA when it is there. CPP(0x08 |
     * unit) returns one byte the same way. Without this a driver scanning the
     * chain finds nothing and reports no adapter at all.
     */
    uint8_t cpp_id[2];
    int     cpp_id_len;     /* bytes still to hand back */
    int     cpp_id_pos;     /* which one is being shifted out */
    int     cpp_id_hi;      /* 1 = the next read is its HIGH nibble */
    int     cpp_init;       /* data writes left to swallow after CPP(0x00) */
    int     block_arm;   /* register 7 addressed, awaiting the w2 that starts it */
    int     block_half;  /* 0 = the next status read returns the low nibble */
    uint8_t block_latch; /* the byte being shifted out as two nibbles */
    int     data_held;   /* a register byte is on the lines, no strobe since */
    int     reg_fetched; /* the addressed register was read since the last strobe */
    int     irq_test;    /* the host's interrupt test is holding PE */
    uint8_t chip[0x20];  /* internal registers behind the 0x0E/0x0F pair */
    uint8_t alt[8];      /* what 0x18-0x1F address while the window is open */
    uint8_t ident[512];  /* IDENTIFY PACKET DEVICE reply being read out */
    int     ident_left;  /* bytes of it still to go; 0 = none pending */
    int     intrq;       /* the drive's INTRQ, as the bridge latches it */
    int     irq_armed;   /* CPP(0x48): forward INTRQ to the port's interrupt */
    int     irq_out;     /* the level presented to lpt_irq() */
    int     irq_ready;   /* the arm-to-interrupt latency has elapsed */
    pc_timer_t irq_timer;
    int     unit_pending; /* a CPP(0x08|unit) answer waits for the next status read */
    uint8_t unit_byte;
    uint8_t data_last;

    /* Parallel-port pin state as the host last wrote it. */
    uint8_t data;    /* w0 */
    uint8_t ctrl;    /* w2 */
    uint8_t status;  /* what r1 will return */

    /* Unlock-frame recogniser. */
    epat_unlock_state_t ustate;
    int                 upos;     /* how much of the preamble has matched  */
    uint8_t             ulast;    /* last data byte, to fold the double-write */
    int                 udup;     /* 1 = the next identical byte is the delay */
    uint8_t             ucmd;     /* the command byte once the preamble matched */
    int                 ucmd_pending; /* a frame is waiting for its nINIT commit */

    int connected;

    /*
     * Register access. The host addresses a register by writing its number to
     * the DATA port - directly, as regr + cont_map[cont]; there is no index/data
     * pair. A write tags the number with 0x60; a read sends it bare. The value
     * then comes back as two nibbles, each arriving in the TOP four bits of the
     * status port across two control-port strobes.
     *
     *   read : w0(r); w2(1); w2(3); a = r1(); w2(4); b = r1();
     *          value = ((a >> 4) & 0x0f) | (b & 0xf0)
     *   write: w0(0x60 + r); w2(1); w0(val); w2(4)
     */
    uint8_t reg_addr;   /* register the host last addressed */
    uint8_t reg_latch;  /* value fetched when the low nibble was asked for */
    int     reg_write;  /* the 0x60 tag was set - a value byte is coming */
    int     nibble_hi;  /* 0 = next status read returns the low nibble */

    /*
     * cont_map = { 0x18, 0x10, 0 }: the ATA task file lives at 0x18, device
     * control at 0x16, and the bridge's own registers at 0x00. One flat array
     * is simpler than three and the addresses do not overlap.
     */
    uint8_t regs[0x20];

    /*
     * Drive latency. The real MATSHITA LS-120 goes BSY when a command
     * completes and stays there for a while: measured 2026-09-12 on the 5160,
     * an INQUIRY leaves status 80 with a clean error register, still set after
     * the probe's ~0.5 s wait, and settled to 50 by the next command. The
     * emulated drive answers instantly, which is why a driver that works here
     * can still time out on hardware - see the LS-120 miniport, Init Success
     * in the bed against Init Failure on the bench.
     *
     * busy_us 0 keeps the original instant behaviour.
     */
    pc_timer_t busy_timer;
    int        busy_us;
    int        busy_pending; /* the BSY window has been served for this command */
    /*
     * A real LS-120 is slow only on the FIRST media access after it has spun
     * down, and answers in tens of milliseconds while still turning. Charging
     * the spin-up to every command made this bed slower than the real machine
     * and buried any transport difference under ~3,000 status polls per
     * command - measured 2026-09-13, where SPP and ECP came out
     * indistinguishable despite 132,176 bytes moving through the ECP FIFO.
     * Model the spindle, not a constant.
     */
    uint64_t   last_media_ts;
    int        spinning;

    /*
     * Reset settle. Releasing SRST does not hand back a ready drive: the real
     * LS-120 holds BSY for seconds, and a command issued inside that window
     * comes back status C1 / error 04 (ABRT) rather than being queued. A bench
     * probe bounded at ~0.29 s aborted every command after the reset, while
     * the same script at ~2.3 s read LBA 0 first time - so the settle decides
     * whether bring-up works at all. Presenting a ready drive the instant SRST
     * is released lets a driver with too short a reset timeout pass here and
     * fail on the bench.
     *
     * reset_us 0 keeps the original instant behaviour.
     */
    pc_timer_t reset_timer;
    int        reset_us;
    int        in_reset;
    int        reset_abrt; /* a command was issued inside the settle window */
} epat_t;

#define EPAT_REG_TASKFILE 0x18 /* cont 0 */
#define EPAT_REG_DEVCTL   0x16 /* cont 1 + 6 */
#define EPAT_WRITE_TAG    0x60
/*
 * PS/2 byte-mode read tag - epat.c mode 2. NOTE it is a SUBSET of
 * EPAT_WRITE_TAG's bits, so any test for a write must be exact
 * ((val & EPAT_WRITE_TAG) == EPAT_WRITE_TAG) and must come first.
 */
#define EPAT_BYTE_TAG     0x20

/* ATA/ATAPI task-file offsets, relative to EPAT_REG_TASKFILE. */
#define ATA_DATA     0
#define ATA_ERROR    1
#define ATA_IREASON  2
#define ATA_BCLO     4
#define ATA_BCHI     5
#define ATA_DRVHD    6
#define ATA_STATUS   7

/* The only ATA command an ATAPI drive behind this bridge is given. */
#define ATA_CMD_PACKET 0xA0
#define ATA_CMD_IDENTIFY_PACKET 0xA1

/*
 * epat.c addresses the streaming data port as a bare 7 to read and 0x67 to
 * write, with no cont_map offset applied. It never moves data through the
 * task file's own data register.
 */
#define EPAT_REG_BLOCK  0x07

/*
 * Commands carried by an ECP address cycle (or the EPP address register on a
 * mode-3 host). Read out of SD120PPD.SYS: the ECP block read writes 0x80 to
 * base+0 at rva 0x4736 and the block write writes 0xC0 at rva 0x4DB9, which
 * are the same two bytes Linux's epat.c sends as w3(0x80) / w3(0xc0) in EPP
 * mode. 0xA0 marks the last byte of a read, the fast-path equivalent of the
 * nibble path's w0(0xFD).
 */
/*
 * CPP command groups, from SD120PPD.SYS's own decoder at 0x2767: the byte is
 * masked with 0xF8, so 0x08-0x0F and 0x10-0x17 are "unit N", and 0x00 is a
 * chip init that is followed by seven data-port writes. Everything else is a
 * bare strobe with no response - which is why 0x40 and 0x50 need nothing.
 */
/*
 * Bridge register 0x0B is the chip version. epat.c reads it as
 * WR(0x0A, 0x38); ver = RR(0x0B) and prints it as "Shuttle EPAT chip %x";
 * SD120PPD.SYS reads it at rva 0x2D6D and requires (ver & 0xF8) == 0xC0
 * before it will believe an EPAT is there at all. Returning zero, as this
 * model did, makes the vendor's own driver decide there is no adapter.
 *
 * It also branches on the version later: >= 0xC3 programs a byte count into
 * this register before each block transfer (rva 0x4D50), which is a path
 * nothing here implements. 0xC0 is the lowest value that identifies as an
 * EPAT and keeps the simple path.
 *
 * ⚠ NOT a captured value - the real EPATRM's version has never been read.
 */
#define EPAT_REG_VERSION   0x0B

/*
 * Register 8 bit 6 arms the interrupt test: with it set, 0x80 on the data
 * lines pulses nACK, raising the port's interrupt, and holds PE until either
 * goes away. SD120PPD.SYS loads with /IRQ unless told /di, counts twenty of
 * these in its handler (data bit 7, PE, nACK already released again) and
 * reports no devices without them.
 */
#define EPAT_REG_IRQCTL    0x08

/*
 * Registers 0x0E/0x0F are an index/data pair into the chip's own registers
 * (epat.c: WR(0xe, idx); WR(0xf, val)). Internal register 0x0C bit 0 turns
 * 0x18-0x1F into a chip view instead of the drive's task file: SD120PPD.SYS
 * reads a revision id from 0x1F there, accepting only 0x00 or 0xE2, and writes
 * a transfer-direction code to 0x18 - which must not reach the drive, since
 * 0x18 is otherwise its data register. 0xE2 is what the real EPATRM returns.
 */
/*
 * Register 0x12 bit 5 is the drive's INTRQ. SD120PPD.SYS waits for a command
 * by polling for bits 4 (its own enable) and 5 together, then reads status,
 * which is what clears INTRQ on the drive.
 */
#define EPAT_REG_INTR       0x12
#define EPAT_INTR_INTRQ     0x20

#define EPAT_REG_CHIP_INDEX 0x0E
#define EPAT_REG_CHIP_DATA  0x0F
#define EPAT_CHIP_ALT_VIEW  0x0C
/* Internal register 2: XOR of every byte a block transfer moved, checked by the host after it. */
#define EPAT_CHIP_CHECKSUM  0x02
#define EPAT_CHIP_ALT_ID    0xE2
#define EPAT_IRQ_TEST      0x40
#define EPAT_IRQ_TEST_DATA 0x80
#define EPAT_CHIP_VERSION  0xC0

#define EPAT_CPP_INIT      0x00
#define EPAT_CPP_UNIT_BYTE 0x08  /* 0x08 | unit -> one byte  */
/*
 * Interrupt forwarding. SD120PPD.SYS disconnects with CPP(0x48) and sets the
 * port's interrupt enable; the drive's INTRQ then raises the port interrupt.
 * Its handler requires nACK high (status bit 6) and asks CPP(0x08|unit), taking
 * the one status byte that follows as bit 7 = this chip interrupted, bits 6-4 =
 * the unit, bit 3 clear. CPP(0x40) and a connect stop the forwarding.
 *
 * The unit byte must not depend on the forwarding: the driver's unit scan
 * sends CPP(0x40) before every CPP(0x08|unit), then CPP(0x50|unit) to
 * acknowledge it, and CPP(0x48) once the scan is over.
 */
#define EPAT_CPP_IRQ_ARM   0x48
#define EPAT_CPP_IRQ_OFF   0x40
#define EPAT_CPP_UNIT_ACK  0x50  /* 0x50 | unit, no reply */
#define EPAT_IRQ_LATENCY_US 1000
#define EPAT_CPP_UNIT_ID   0x10  /* 0x10 | unit -> two bytes */
#define EPAT_CPP_ID_PRESENT 0xFFAA  /* what a populated unit answers */
#define EPAT_CPP_INIT_WRITES 7

#define EPAT_ECP_READ   0x80
#define EPAT_ECP_LAST   0xa0
#define EPAT_ECP_WRITE  0xc0

#define EPAT_BLOCK_NONE  0
#define EPAT_BLOCK_READ  1
#define EPAT_BLOCK_WRITE 2
#define ATA_CDB_LEN    12

/* Two seconds idle is taken as spun down; a turning drive answers ~50x quicker. */
#define EPAT_SPINDOWN_TSC  ((uint64_t) 2000000 * TIMER_USEC)
#define EPAT_SPINNING_DIV  50

#define ATA_ST_DRDY 0x40
#define ATA_ST_DSC  0x10

/* The ATAPI signature a device reports after a reset: 14 EB in LBA mid/high. */
#define ATAPI_SIG_LO 0x14
#define ATAPI_SIG_HI 0xEB

/*
 * The preamble is written as pairs. Fold a repeat of the byte we just saw
 * into a single logical step, which is what the bridge does - it is an I/O
 * delay, not data.
 */
static int
epat_unlock_feed(epat_t *dev, uint8_t val)
{
    if (dev->udup && (val == dev->ulast)) {
        dev->udup = 0;
        return 0;
    }

    dev->ulast = val;
    dev->udup  = 1;

    if (dev->ustate == EPAT_UNLOCK_CMD) {
        dev->ucmd         = val;
        dev->ucmd_pending = 1;
        dev->ustate       = EPAT_UNLOCK_IDLE;
        return 1; /* a complete frame is pending its nINIT commit */
    }

    if (val == epat_unlock[dev->upos]) {
        if (dev->upos == 0)
            dev->unit_pending = 0;
        dev->upos++;
        dev->ustate = EPAT_UNLOCK_RUN;

        /* Publish the checkpoint the driver is about to read. */
        if (dev->upos == 5)
            dev->status = EPAT_CHK_1;
        else if (dev->upos == 6)
            dev->status = EPAT_CHK_2;
        else if (dev->upos == 7) {
            dev->status = EPAT_CHK_3;
            dev->ustate = EPAT_UNLOCK_CMD;
        }
        return 0;
    }

    /*
     * A mismatch restarts the match rather than aborting it - the first byte
     * of a new frame can arrive immediately after a failed one.
     */
    if (dev->ustate == EPAT_UNLOCK_RUN)
        epat_log(dev->log, "unlock frame broken at byte %i: got %02X, expected %02X\n",
                 dev->upos, val, epat_unlock[dev->upos]);
    dev->upos   = (val == epat_unlock[0]) ? 1 : 0;
    dev->ustate = dev->upos ? EPAT_UNLOCK_RUN : EPAT_UNLOCK_IDLE;
    return 0;
}

/*
 * Chain scan. The id is on the status lines as soon as the command byte is:
 * SD120PPD.SYS and SD120PPD.MPD end this frame with w2(4) and read the first
 * nibble before any strobe, so waiting for an nINIT commit answers one nibble
 * late and the driver never sees 0xFFAA. Only unit 0 is populated here.
 */
static void
epat_cpp_unit_id(epat_t *dev)
{
    uint16_t id = ((dev->ucmd & 0x07) == 0) ? EPAT_CPP_ID_PRESENT : 0x0000;

    dev->cpp_id[0]    = (uint8_t) (id >> 8);
    dev->cpp_id[1]    = (uint8_t) (id & 0xff);
    dev->cpp_id_len   = 2;
    dev->cpp_id_pos   = 0;
    dev->cpp_id_hi    = 1;
    dev->ucmd_pending = 0;
    epat_log(dev->log, "CPP unit %i id -> %04X\n", dev->ucmd & 0x07, id);
}

/*
 * Find the drive assigned to this port.
 *
 * This cannot be done at init: 86box.c runs lpt_devices_init() well before
 * rdisk_hard_reset(), so at the time this device is created no removable disk
 * exists yet. Moving rdisk_hard_reset() earlier is not an option either - it
 * attaches SCSI drives and so must follow scsi_card_init(), which is itself
 * after the parallel devices. So the lookup is done on first use and cached.
 *
 * With no drive assigned the bridge answers from its own regs[], which keeps
 * the wire protocol testable on its own.
 */
static int
epat_attach_drive(epat_t *dev)
{
    if (dev->sd != NULL)
        return 1;

    if (dev->no_drive)
        return 0;

    dev->sd = rdisk_get_lpt_device(dev->port);

    if (dev->sd == NULL) {
        dev->no_drive = 1;
        epat_log(dev->log, "no removable disk assigned to LPT%i - "
                           "answering from the bridge's own registers\n",
                 dev->port + 1);
        return 0;
    }

    dev->tf = ((rdisk_t *) dev->sd->sc)->tf;
    epat_log(dev->log, "LPT%i drive attached\n", dev->port + 1);

    return 1;
}

/*
 * The packet phase engine, transliterated from ide_atapi_callback() and
 * ide_atapi_pio_request() in hdc_ide.c. The drive is an ordinary ATAPI device;
 * all that differs is that its task file and data register are reached a nibble
 * at a time down a parallel cable instead of over the ISA bus.
 *
 * Two deliberate departures from the IDE path:
 *
 *   - No interrupts. The Shuttle bridges here run polled - the real machine
 *     measures dmaEn = 0 - and the driver polls BSY and DRQ.
 *   - No callback timing. The IDE controller arms a timer for the delay the
 *     drive asks for; the bridge completes immediately, because one byte over
 *     this link costs far more than any seek that delay models. This does mean
 *     a spin-up wait is not reproduced, which is worth remembering when using
 *     the bridge to test a driver's timeouts.
 */
static void epat_pio_request(epat_t *dev, int out);
static void epat_atapi_callback(epat_t *dev);
static void epat_device_reset(epat_t *dev);

/* The reset settle has expired - the drive is now genuinely ready. */
static void
epat_reset_done(void *priv)
{
    epat_t *dev = (epat_t *) priv;

    dev->in_reset   = 0;
    dev->reset_abrt = 0;
    epat_device_reset(dev);
}

/* The BSY window has expired - finish the command the drive was sitting on. */
static void
epat_busy_done(void *priv)
{
    epat_t *dev = (epat_t *) priv;

    epat_atapi_callback(dev);
}

/*
 * Does this command move the mechanism? INQUIRY, REQUEST SENSE and MODE SENSE
 * are answered out of drive firmware and come back promptly on the real LS-120;
 * anything that reads, writes or positions the medium pays the spin-up. Modelling
 * one latency for every command stalls enumeration, which the drive does not do -
 * on the real 5160 it enumerates and gets a drive letter, then hangs on the first
 * read. Keeping INQUIRY fast is what reproduces that.
 */
static int
epat_cdb_touches_media(uint8_t op)
{
    switch (op) {
        case 0x08: /* READ(6)        */
        case 0x0a: /* WRITE(6)       */
        case 0x1b: /* START STOP UNIT*/
        case 0x25: /* READ CAPACITY  */
        case 0x28: /* READ(10)       */
        case 0x2a: /* WRITE(10)      */
        case 0x2f: /* VERIFY(10)     */
        case 0x35: /* SYNCHRONIZE CACHE */
            return 1;
        default:
            return 0;
    }
}

static void epat_update_irq(epat_t *dev);

static void
epat_irq_ready(void *priv)
{
    epat_t *dev = (epat_t *) priv;

    dev->irq_ready = 1;
    epat_update_irq(dev);
}

/* ATA: INTRQ is asserted when the drive wants the host, unless nIEN is set. */
static void
epat_raise_intrq(epat_t *dev)
{
    if (!(dev->regs[EPAT_REG_DEVCTL] & 0x02))
        dev->intrq = 1;
    epat_update_irq(dev);
}

static void
epat_atapi_callback(epat_t *dev)
{
    scsi_common_t *sc = dev->sd->sc;

    switch (sc->packet_status) {
        default:
            break;

        case PHASE_IDLE:
            dev->tf->pos     = 0;
            dev->tf->phase   = 1;
            dev->tf->atastat = READY_STAT | DRQ_STAT | (dev->tf->atastat & ERR_STAT);
            break;

        case PHASE_COMMAND:
            dev->tf->atastat = BUSY_STAT | (dev->tf->atastat & ERR_STAT);
            dev->sd->command(sc, sc->atapi_cdb);
            /*
             * Whatever delay the drive asked for is discarded, and the phase it
             * moved to is acted on now. rdisk_set_callback() is a no-op for an
             * LPT drive, so nothing else would ever run this.
             */
            sc->callback = 0.0;
            if (sc->packet_status != PHASE_COMMAND)
                epat_atapi_callback(dev);
            break;

        case PHASE_COMPLETE:
        case PHASE_ERROR:
            /*
             * Hold BSY for the configured drive latency before reporting the
             * result, the way the real drive does. Without this the bridge
             * never goes busy at all and a driver's ready-poll is never
             * exercised.
             */
            if ((dev->busy_us > 0) && !dev->busy_pending &&
                epat_cdb_touches_media(sc->atapi_cdb[0])) {
                const uint64_t now  = tsc;
                const uint64_t idle = (dev->last_media_ts == 0) ? ~0ULL
                                                                : (now - dev->last_media_ts);
                int            us;

                if (!dev->spinning || (idle > EPAT_SPINDOWN_TSC)) {
                    us            = dev->busy_us;           /* cold - full spin-up */
                    dev->spinning = 1;
                } else
                    us = dev->busy_us / EPAT_SPINNING_DIV;  /* already turning */

                dev->last_media_ts = now;
                dev->busy_pending  = 1;
                dev->tf->atastat   = BUSY_STAT | (dev->tf->atastat & ERR_STAT);
                timer_set_delay_u64(&dev->busy_timer, (uint64_t) us * TIMER_USEC);
                epat_log(dev->log, "drive busy %d us (%s)\n", us,
                         (us == dev->busy_us) ? "spin-up" : "spinning");
                break;
            }
            dev->busy_pending = 0;
            dev->tf->atastat = READY_STAT;
            if (sc->packet_status == PHASE_ERROR)
                dev->tf->atastat |= ERR_STAT;
            dev->tf->phase    = 3;
            sc->packet_status = PHASE_NONE;
            epat_raise_intrq(dev);
            epat_log(dev->log, "command done, status %02X\n", dev->tf->atastat);
            break;

        case PHASE_DATA_IN:
        case PHASE_DATA_OUT:
            dev->tf->atastat = READY_STAT | DRQ_STAT | (dev->tf->atastat & ERR_STAT);
            /* PHASE_DATA_IN gives ireason 2 (I/O set), PHASE_DATA_OUT gives 0. */
            dev->tf->phase   = !(sc->packet_status & 0x01) << 1;
            epat_raise_intrq(dev);
            epat_log(dev->log, "data phase %s, %u bytes, request length %u\n",
                     (sc->packet_status == PHASE_DATA_IN) ? "in" : "out",
                     sc->packet_len, dev->tf->request_length);
            break;
    }
}

/* A transfer has reached the end of a block, or of the whole command. */
static void
epat_pio_request(epat_t *dev, const int out)
{
    scsi_common_t *sc = dev->sd->sc;

    dev->tf->atastat = BUSY_STAT;

    if (dev->tf->pos >= sc->packet_len) {
        dev->tf->pos    = 0;
        sc->request_pos = 0;

        if (out)
            dev->sd->phase_data_out(sc);
        else
            dev->sd->command_stop(sc);

        sc->callback = 0.0;
        /*
         * rdisk_phase_data_out() calls command_stop() itself and returns 1 on
         * success, but its failure path returns 0 having left the phase at
         * PHASE_ERROR. Firing the callback only for PHASE_COMPLETE therefore
         * stranded every failed write: the data moved, the device never
         * reported, DRQ stayed asserted and the driver polled status forever.
         * Observed 2026-09-13 - a WRITE(10) to LBA 1 followed by 11,000+
         * status reads of 48h (DRDY|DRQ) and no completion.
         */
        if ((sc->packet_status == PHASE_COMPLETE) ||
            (sc->packet_status == PHASE_ERROR)) {
            epat_log(dev->log, "data phase ended in %s\n",
                     (sc->packet_status == PHASE_ERROR) ? "ERROR" : "COMPLETE");
            epat_atapi_callback(dev);
        }
    } else {
        /* Short tail: tell the host how much is actually left. */
        if ((sc->packet_len - dev->tf->pos) < sc->max_transfer_len) {
            sc->max_transfer_len    = (uint16_t) (sc->packet_len - dev->tf->pos);
            dev->tf->request_length = sc->max_transfer_len;
        }

        sc->packet_status = PHASE_DATA_IN | out;
        epat_atapi_callback(dev);
        sc->request_pos = 0;
    }
}

/*
 * The data register. The bridge is a byte-wide link, so unlike the IDE path
 * this moves one byte per access rather than a word.
 */
/*
 * The drive is the master and there is no slave. With device 1 selected the
 * master answers task-file reads with 00 and ignores commands and data, as
 * ATA requires - otherwise a driver probing both positions finds the one
 * drive twice.
 */
static int
epat_slave_selected(const epat_t *dev)
{
    return (dev->tf != NULL) && (dev->tf->drvsel & 0x10);
}

static uint8_t
epat_data_read(epat_t *dev)
{
    scsi_common_t *sc = dev->sd->sc;
    uint8_t        ret;

    if (epat_slave_selected(dev))
        return 0x00;

    if (dev->ident_left > 0) {
        ret = dev->ident[sizeof(dev->ident) - dev->ident_left];
        if (--dev->ident_left == 0) {
            dev->tf->phase   = 3; /* ireason 3: complete */
            dev->tf->atastat = READY_STAT | ATA_ST_DSC;
        }
        return ret;
    }

    if ((sc->temp_buffer == NULL) || (sc->packet_status != PHASE_DATA_IN))
        return 0;

    /*
     * Reading past the buffer returns zero rather than reading off the end:
     * a command with an allocation length below one sector leaves the host
     * asking for more than the drive prepared.
     */
    ret = (dev->tf->pos < sc->packet_len) ? sc->temp_buffer[dev->tf->pos] : 0;
    dev->tf->pos++;
    sc->request_pos++;


    if ((sc->request_pos >= sc->max_transfer_len) || (dev->tf->pos >= sc->packet_len))
        epat_pio_request(dev, 0);

    return ret;
}

static void
epat_data_write(epat_t *dev, const uint8_t val)
{
    scsi_common_t *sc  = dev->sd->sc;
    uint8_t       *buf = NULL;

    if (epat_slave_selected(dev))
        return;

    /* Before a command is assembled the data register carries the CDB. */
    if (sc->packet_status == PHASE_IDLE)
        buf = sc->atapi_cdb;
    else if (sc->packet_status == PHASE_DATA_OUT)
        buf = sc->temp_buffer;

    if (buf == NULL)
        return;

    buf[dev->tf->pos] = val;
    dev->tf->pos++;
    sc->request_pos++;


    if (sc->packet_status == PHASE_DATA_OUT) {
        if ((sc->request_pos >= sc->max_transfer_len) ||
            (dev->tf->pos >= sc->packet_len))
            epat_pio_request(dev, 1);
    } else if (dev->tf->pos >= ATA_CDB_LEN) {
        epat_log(dev->log, "CDB %02X %02X %02X %02X %02X %02X "
                           "%02X %02X %02X %02X %02X %02X\n",
                 buf[0], buf[1], buf[2],  buf[3],  buf[4],  buf[5],
                 buf[6], buf[7], buf[8],  buf[9],  buf[10], buf[11]);

        dev->tf->pos      = 0;
        dev->tf->atastat  = BUSY_STAT;
        sc->packet_status = PHASE_COMMAND;
        epat_atapi_callback(dev);
    }
}

/*
 * IDENTIFY PACKET DEVICE, as the real LS-120 (MATSHITA LS-120 COSM 04)
 * answers it. SD120PPD.SYS identifies the drive with this before any PACKET
 * command and reports no devices if it is refused.
 */
static void
epat_ata_string(uint8_t *buf, const int word, const int words, const char *str,
                const int swap)
{
    const size_t len = strlen(str);

    for (int i = 0; i < (words * 2); i++)
        buf[(word * 2) + (swap ? (i ^ 1) : i)] = (i < (int) len) ? str[i] : ' ';
}

static void
epat_build_identify(epat_t *dev)
{
    static const struct {
        uint8_t  word;
        uint16_t val;
    } words[] = {
        {   0, 0x8080 }, {   1, 0x03c3 }, {   3, 0x0008 }, {   4, 0x0031 },
        {   5, 0x0200 }, {   6, 0x0020 }, {  49, 0x0a00 }, {  51, 0x0200 },
        {  53, 0x0003 }, {  54, 0x03c3 }, {  55, 0x0008 }, {  56, 0x0020 },
        {  57, 0x0003 }, {  58, 0xc858 }, {  60, 0x0003 }, {  61, 0xc300 },
        {  67, 0x00f0 }, {  68, 0x00f0 }, { 128, 0x0100 }, { 129, 0x0001 }
    };

    memset(dev->ident, 0x00, sizeof(dev->ident));
    for (size_t i = 0; i < (sizeof(words) / sizeof(words[0])); i++) {
        dev->ident[words[i].word * 2]       = words[i].val & 0xff;
        dev->ident[(words[i].word * 2) + 1] = words[i].val >> 8;
    }
    /* This drive stores its serial unswapped, unlike the other two strings. */
    epat_ata_string(dev->ident, 10, 10, "X713CA0B4594", 0);
    epat_ata_string(dev->ident, 23, 4, "0270M09T", 1);
    epat_ata_string(dev->ident, 27, 20, "LS-120 COSM   04              UHD Floppy", 1);
}

/* A write to the command register. PACKET and IDENTIFY PACKET DEVICE are answered. */
static void
epat_command(epat_t *dev, const uint8_t cmd)
{
    scsi_common_t *sc = dev->sd->sc;

    dev->ident_left = 0;
    dev->intrq      = 0;
    epat_update_irq(dev);

    if (dev->in_reset) {
        epat_log(dev->log, "command %02X inside the reset settle, aborting\n", cmd);
        dev->reset_abrt = 1;
        return;
    }

    if (cmd == ATA_CMD_IDENTIFY_PACKET) {
        epat_build_identify(dev);
        dev->ident_left  = sizeof(dev->ident);
        dev->tf->error   = 0;
        dev->tf->phase   = 2; /* ireason 2: data in */
        dev->tf->atastat = READY_STAT | ATA_ST_DSC | DRQ_STAT;
        epat_raise_intrq(dev);
        epat_log(dev->log, "IDENTIFY PACKET DEVICE\n");
        return;
    }

    if (cmd != ATA_CMD_PACKET) {
        epat_log(dev->log, "command %02X is not PACKET, aborting\n", cmd);
        dev->tf->atastat = READY_STAT | ERR_STAT | ATA_ST_DSC;
        dev->tf->error   = ABRT_ERR;
        epat_raise_intrq(dev); /* an aborted command still completes */
        return;
    }

    epat_log(dev->log, "PACKET, byte count %u\n", dev->tf->request_length);

    dev->tf->pos      = 0;
    sc->packet_status = PHASE_IDLE;
    dev->tf->phase    = 1; /* ireason 1: the drive wants the CDB */
    dev->tf->atastat  = READY_STAT | DRQ_STAT;
}

/*
 * The ATA task file belongs to the drive, not to the bridge. These map the
 * eight task-file offsets onto ide_tf_t, which is the same structure the IDE
 * controller drives the drive through. Offsets outside the task file (device
 * control, and the bridge's own registers) stay in regs[].
 */
static int
epat_alt_view(const epat_t *dev, const uint8_t addr)
{
    return (dev->chip[EPAT_CHIP_ALT_VIEW] & 0x01) && (addr >= EPAT_REG_TASKFILE) &&
           (addr <= (EPAT_REG_TASKFILE + 7));
}

static uint8_t
epat_reg_read(epat_t *dev, const uint8_t addr)
{
    epat_attach_drive(dev);

    if (epat_alt_view(dev, addr))
        return (addr == (EPAT_REG_TASKFILE + 7)) ? EPAT_CHIP_ALT_ID :
                                                   dev->alt[addr - EPAT_REG_TASKFILE];

    if (addr == EPAT_REG_CHIP_DATA)
        return dev->chip[dev->regs[EPAT_REG_CHIP_INDEX] & 0x1f];

    if (addr == EPAT_REG_INTR)
        return (dev->regs[addr] & ~EPAT_INTR_INTRQ) | (dev->intrq ? EPAT_INTR_INTRQ : 0x00);

    /*
     * Inside the settle window only status and error mean anything, and they
     * describe the mechanism rather than tf, which still holds pre-reset
     * state. C1/04 is what the bench returns for a command issued too early.
     */
    if (dev->in_reset) {
        if (addr == (EPAT_REG_TASKFILE + ATA_STATUS))
            return dev->reset_abrt ? (BUSY_STAT | READY_STAT | ERR_STAT) : BUSY_STAT;
        if (addr == (EPAT_REG_TASKFILE + ATA_ERROR))
            return dev->reset_abrt ? ABRT_ERR : 0x00;
    }

    if (epat_slave_selected(dev) && (addr >= EPAT_REG_TASKFILE) &&
        (addr <= (EPAT_REG_TASKFILE + ATA_STATUS)) && (addr != (EPAT_REG_TASKFILE + ATA_DRVHD)))
        return 0x00;

    if ((dev->tf != NULL) && (addr == EPAT_REG_BLOCK))
        return epat_data_read(dev);

    if ((dev->tf == NULL) || (addr < EPAT_REG_TASKFILE) ||
        (addr > (EPAT_REG_TASKFILE + ATA_STATUS)))
        return dev->regs[addr];

    switch (addr - EPAT_REG_TASKFILE) {
        case ATA_DATA:
            return epat_data_read(dev);
        case ATA_ERROR:
            return dev->tf->error;
        case ATA_IREASON:
            return dev->tf->phase;
        case ATA_BCLO:
            return dev->tf->request_length & 0xff;
        case ATA_BCHI:
            return (dev->tf->request_length >> 8) & 0xff;
        case ATA_DRVHD:
            return dev->tf->drvsel;
        case ATA_STATUS:
            dev->intrq = 0;
            epat_update_irq(dev);
            return dev->tf->atastat;
        default:
            return dev->regs[addr];
    }
}

static void
epat_reg_write(epat_t *dev, const uint8_t addr, const uint8_t val)
{
    if (epat_alt_view(dev, addr)) {
        dev->alt[addr - EPAT_REG_TASKFILE] = val;
        return;
    }

    dev->regs[addr] = val;

    if (addr == EPAT_REG_CHIP_DATA)
        dev->chip[dev->regs[EPAT_REG_CHIP_INDEX] & 0x1f] = val;

    epat_attach_drive(dev);

    if ((dev->tf == NULL) || (addr < EPAT_REG_TASKFILE) ||
        (addr > (EPAT_REG_TASKFILE + ATA_STATUS)))
        return;

    switch (addr - EPAT_REG_TASKFILE) {
        case ATA_DATA:
            epat_data_write(dev, val);
            break;
        case ATA_ERROR:
            dev->tf->features = val;
            break;
        case ATA_IREASON:
            dev->tf->phase = val;
            break;
        case ATA_BCLO:
            dev->tf->request_length = (dev->tf->request_length & 0xff00) | val;
            break;
        case ATA_BCHI:
            dev->tf->request_length = (dev->tf->request_length & 0x00ff) |
                                      ((uint16_t) val << 8);
            break;
        case ATA_DRVHD:
            dev->tf->drvsel = val;
            break;
        case ATA_STATUS: /* the command register, on a write */
            if (epat_slave_selected(dev))
                epat_log(dev->log, "command %02X for the absent slave, ignored\n", val);
            else
                epat_command(dev, val);
            break;
        default:
            break;
    }
}

/* Present the drive as it is immediately after a reset. */
static void
epat_device_reset(epat_t *dev)
{
    /* An ATA reset reaches the drive only; the bridge's own registers keep their values. */
    memset(&dev->regs[EPAT_REG_TASKFILE], 0x00, 8);
    dev->regs[EPAT_REG_TASKFILE + ATA_STATUS] = ATA_ST_DRDY | ATA_ST_DSC;
    dev->regs[EPAT_REG_TASKFILE + ATA_BCLO]   = ATAPI_SIG_LO;
    dev->regs[EPAT_REG_TASKFILE + ATA_BCHI]   = ATAPI_SIG_HI;

    /*
     * A real drive sets its own signature here. rdisk_reset() writes
     * request_length = 0xEB14, which is that signature, so the values above
     * only apply when the bridge is running without a drive.
     */
    if (epat_attach_drive(dev)) {
        dev->sd->reset(dev->sd->sc);

        /*
         * A real drive raises a unit attention when it is reset, and the
         * physical LS-120 demonstrably does: after this same SRST it answers
         * the next READ with sense key 6. rdisk_reset() clears the flag, so
         * without this the emulated drive is more forgiving than the real one
         * and a driver that mishandles the condition would pass here and fail
         * on hardware. rdisk already implements the rest, ALLOW_UA included.
         */
        ((rdisk_t *) dev->sd->sc)->unit_attention = 1;
    }

    epat_log(dev->log, "device reset: status %02X, signature %02X %02X\n",
             dev->regs[EPAT_REG_TASKFILE + ATA_STATUS], ATAPI_SIG_LO, ATAPI_SIG_HI);
}

static void
epat_update_irq(epat_t *dev)
{
    const int want = (dev->regs[EPAT_REG_IRQCTL] & EPAT_IRQ_TEST) &&
                     (dev->data == EPAT_IRQ_TEST_DATA);
    const int out  = want || (dev->irq_armed && dev->irq_ready && dev->intrq);

    dev->irq_test = want;
    if (out != dev->irq_out) {
        dev->irq_out = out;
        lpt_irq(dev->lpt, out);
    }
}

static void
epat_write_data(uint8_t val, void *priv)
{
    epat_t *dev = (epat_t *) priv;

    dev->data = val;
    epat_update_irq(dev);

    /*
     * NEVER feed block payload to the unlock recogniser. Inside a block the
     * bytes are user data and may legitimately be anything - including 0x22,
     * the first byte of an unlock frame. Feeding them starts a spurious match,
     * and the early return below then SWALLOWS the byte, so the stream loses
     * one byte per 0x22 in the payload, never reaches the expected length, and
     * the command never completes: DRQ stays asserted and the driver polls
     * forever.
     *
     * Reads could not hit this because the host only ever writes control bytes
     * (0xFF turnaround, 0xFD last-byte, 0x00 end) on that path - only a WRITE
     * pushes arbitrary payload through the data port. Found 2026-09-13 on the
     * first real WRITE(10) this bridge has ever carried:
     *   "unlock frame broken at byte 1: got 00, expected AA" mid-block.
     */
    /*
     * Nor a register's value, or its repeated copies: the direction code
     * SD120PPD.SYS writes to 0x18 for an OUT transfer is 0x22, which is
     * unlock byte 0. A frame always follows a control write, so it cannot
     * arrive while a value is pending or being repeated.
     */
    const int reg_value = (dev->reg_write && (dev->reg_addr < sizeof(dev->regs))) ||
                          (dev->data_held && (val == dev->data_last));

    if ((dev->block == EPAT_BLOCK_NONE) && !reg_value && epat_unlock_feed(dev, val)) {
        if ((dev->ucmd & 0xf8) == EPAT_CPP_UNIT_ID)
            epat_cpp_unit_id(dev);
        else if ((dev->ucmd & 0xf8) == EPAT_CPP_UNIT_BYTE) {
            /* Read once, straight after the frame, with no strobe. Only unit 0 exists. */
            const int unit    = dev->ucmd & 0x07;
            dev->unit_byte    = (dev->intrq && (unit == 0)) ?
                                    (uint8_t) (0x80 | (unit << 4)) : 0x00;
            dev->unit_pending = 1;
            dev->ucmd_pending = 0;
            epat_log(dev->log, "CPP unit %i byte -> %02X\n", unit, dev->unit_byte);
        }
    }

    /*
     * While an unlock frame is being matched, or one is committed but waiting
     * for its nINIT pulse, these bytes are frame content and not register
     * addresses. A register number that happens to equal 0x22 would start the
     * recogniser spuriously; epat_write_ctrl cancels a partial match the moment
     * it sees w2(1), which an unlock frame never issues.
     */
    if ((dev->ustate != EPAT_UNLOCK_IDLE) || dev->ucmd_pending)
        return;

    /*
     * Chip-init filler - see EPAT_CPP_INIT. The vendor's CPP(0x00) walks the
     * data port 1..7 as part of the command itself; a driver that does not
     * bother still writes real register addresses here, so match the ascending
     * pattern rather than swallowing a fixed count. Getting this wrong eats
     * seven register writes and the drive is never found.
     */
    if ((dev->cpp_init > 0) &&
        (val == (uint8_t) (EPAT_CPP_INIT_WRITES + 1 - dev->cpp_init))) {
        dev->cpp_init--;
        return;
    }
    dev->cpp_init = 0;

    if (!dev->connected)
        return;

    /*
     * Mid-block every data byte belongs to the stream. On the read side the
     * host writes 0xFF to turn the bus around and 0xFD to flag the last byte;
     * neither is data, and 0x00 ends the block.
     */
    if (dev->block == EPAT_BLOCK_WRITE) {
        /* Payload is doubled the same way in the slower modes; the strobe separates real bytes. */
        if (dev->data_held && (val == dev->data_last))
            return;
        dev->data_held = 1;
        dev->data_last = val;

        dev->chip[EPAT_CHIP_CHECKSUM] ^= val;
        epat_data_write(dev, val);
        return;
    }

    if (dev->block == EPAT_BLOCK_READ) {
        if (val == 0x00) {
            epat_log(dev->log, "block read done\n");
            dev->block = EPAT_BLOCK_NONE;
        }
        return;
    }

    /*
     * The bridge latches on control strobes, so a byte written again with no
     * strobe in between is the same byte still on the lines. SD120PPD.SYS
     * writes every byte twice in its slower modes; acting on the second copy
     * takes a register's address as its value.
     */
    if (dev->data_held && (val == dev->data_last))
        return;
    dev->data_held = 1;
    dev->data_last = val;

    if (dev->reg_write) {
        /* The value for the register addressed by the previous write. */
        dev->reg_write = 0;
        if (dev->reg_addr < sizeof(dev->regs)) {
            const uint8_t was = dev->regs[dev->reg_addr];

            epat_reg_write(dev, dev->reg_addr, val);
            epat_log(dev->log, "W reg %02X = %02X\n", dev->reg_addr, val);

            /*
             * The drive resets when SRST is released, not on every write with
             * it clear: SD120PPD.SYS writes device control without ever
             * setting SRST, and that must not reset the drive.
             */
            if ((dev->reg_addr == EPAT_REG_DEVCTL) && (was & 0x04) && !(val & 0x04)) {
                if (dev->reset_us > 0) {
                    dev->in_reset   = 1;
                    dev->reset_abrt = 0;
                    timer_set_delay_u64(&dev->reset_timer,
                                        (uint64_t) dev->reset_us * TIMER_USEC);
                    epat_log(dev->log, "SRST released, settling for %d us\n",
                             dev->reset_us);
                } else
                    epat_device_reset(dev);
            }
        } else
            epat_log(dev->log, "W reg %02X out of range\n", dev->reg_addr);
        return;
    }

    /* Register 7, tagged or bare, arms a block transfer rather than a register. */
    if (val == (EPAT_WRITE_TAG | EPAT_REG_BLOCK)) {
        dev->block_arm = EPAT_BLOCK_WRITE;
        return;
    }

    if (val == EPAT_REG_BLOCK) {
        dev->block_arm = EPAT_BLOCK_READ;
        return;
    }

    /*
     * PS/2 BYTE MODE - epat.c mode 2, and the reason the vendor parks the ECR
     * at 34h rather than dropping to mode 000:
     *
     *   w0(0x20 + r); w2(1); w2(0x25); a = r0(); w2(4)
     *
     * The value comes back on the DATA port in ONE access instead of two
     * status reads and a nibble merge. MEASURED on the real 5160 2026-09-18:
     * byte mode and nibble return the same byte for the same register, and a
     * whole ATAPI INQUIRY through this path is byte-identical to the nibble
     * one.
     *
     * ORDER MATTERS. EPAT_WRITE_TAG is 0x60 and EPAT_BYTE_TAG is 0x20, so a
     * loose `val & EPAT_WRITE_TAG` is TRUE for a byte-mode tag as well and
     * every byte-mode read would be mistaken for a register write.
     */
    if ((val & EPAT_WRITE_TAG) == EPAT_WRITE_TAG) {
        dev->reg_addr  = val & ~EPAT_WRITE_TAG;
        dev->reg_write = 1;
    } else if (val & EPAT_BYTE_TAG) {
        dev->reg_addr = val & ~EPAT_BYTE_TAG;
        if (dev->reg_addr < sizeof(dev->regs)) {
            const uint8_t b = epat_reg_read(dev, dev->reg_addr);
            /*
             * lpt.c returns in_dat for a data-port read whenever control bit 5
             * is set, which w2(0x25) does. Push the byte there now: the host's
             * read comes two control writes later and nothing clears it.
             */
            lpt_write_to_dat(dev->lpt, b);
            epat_log(dev->log, "R reg %02X = %02X (byte mode)\n", dev->reg_addr, b);
        } else
            epat_log(dev->log, "R reg %02X out of range (byte mode)\n", dev->reg_addr);
    } else {
        dev->reg_addr  = val;
        dev->nibble_hi = 0;
    }
}

static void
epat_write_ctrl(uint8_t val, void *priv)
{
    epat_t *dev = (epat_t *) priv;

    dev->data_held   = 0;
    dev->reg_fetched = 0;

    /*
     * The frame is committed by pulsing nINIT: 0x04 -> 0x05 -> 0x04. Act on
     * the rising edge of bit 0 while a command byte is pending.
     */
    if (!(dev->ctrl & 0x01) && (val & 0x01) && (dev->ustate == EPAT_UNLOCK_IDLE) &&
        dev->ucmd_pending) {
        if (dev->ucmd == EPAT_CPP_CONNECT) {
            dev->connected    = 1;
            dev->status       = EPAT_STAT_IDLE;
            dev->irq_armed    = 0;
            dev->irq_ready    = 0;
            timer_disable(&dev->irq_timer);
            dev->unit_pending = 0;
            epat_update_irq(dev);
            dev->ecp_cmd   = 0x00;  /* a fresh connect is not mid-block */
            epat_log(dev->log, "CONNECT\n");
        } else if (dev->ucmd == EPAT_CPP_DISCONNECT) {
            dev->connected = 0;
            dev->status    = EPAT_STAT_IDLE;
            epat_log(dev->log, "DISCONNECT\n");
        } else if (dev->ucmd == EPAT_CPP_INIT) {
            /*
             * Seven data-port writes (1..7) follow, each strobed. They are
             * chip init, not register addresses - and 7 is the block-transfer
             * register, so letting them through arms a block read.
             */
            dev->cpp_init = EPAT_CPP_INIT_WRITES;
            epat_log(dev->log, "CPP init\n");
        } else if (dev->ucmd == EPAT_CPP_IRQ_ARM) {
            /*
             * SD120PPD.SYS arms, then sets the port's interrupt enable, and the
             * port drops a raise while that bit is clear. A drive that has
             * already finished must therefore not interrupt until after it is
             * set - as on hardware, where the drive completes after the
             * disconnect, not during it.
             */
            dev->irq_armed = 1;
            dev->irq_ready = 0;
            timer_set_delay_u64(&dev->irq_timer, EPAT_IRQ_LATENCY_US * TIMER_USEC);
            epat_update_irq(dev);
            epat_log(dev->log, "interrupt armed\n");
        } else if (dev->ucmd == EPAT_CPP_IRQ_OFF) {
            dev->irq_armed = 0;
            dev->irq_ready = 0;
            timer_disable(&dev->irq_timer);
            epat_update_irq(dev);
        } else if ((dev->ucmd & 0xf0) == EPAT_CPP_UNIT_ACK) {
            /* The scan runs its unit counter to 8, so 0x58 arrives as well. */
        } else
            epat_log(dev->log, "unlock frame committed with unknown command %02X\n",
                     dev->ucmd);
        dev->ucmd         = 0;
        dev->ucmd_pending = 0;
        dev->upos = 0;
    }

    /*
     * A register read strobes w2(1) then w2(3), then w2(4) for the second
     * nibble. An unlock frame never writes 0x01, so seeing it both cancels any
     * partial frame match and starts the nibble sequence.
     */
    /*
     * w0(0x67); w2(1); w2(5)  starts a write block.
     * w0(7);    w2(1); w2(3)  starts a read block.
     * A write block ends on w2(7), a read block on w0(0) above.
     */
    if (dev->connected && (dev->block_arm != EPAT_BLOCK_NONE)) {
        if ((dev->block_arm == EPAT_BLOCK_WRITE) && (val == 0x05)) {
            dev->block     = EPAT_BLOCK_WRITE;
            dev->block_arm = EPAT_BLOCK_NONE;
            dev->chip[EPAT_CHIP_CHECKSUM] = 0x00;
            epat_log(dev->log, "block write start\n");
            dev->ctrl = val;
            return;
        }

        if ((dev->block_arm == EPAT_BLOCK_READ) && (val == 0x03)) {
            dev->block      = EPAT_BLOCK_READ;
            dev->block_arm  = EPAT_BLOCK_NONE;
            dev->block_half = 0;
            dev->chip[EPAT_CHIP_CHECKSUM] = 0x00;
            epat_log(dev->log, "block read start\n");
            dev->ctrl = val;
            return;
        }
    }

    if ((dev->block == EPAT_BLOCK_WRITE) && (val == 0x07)) {
        epat_log(dev->log, "block write done\n");
        dev->block = EPAT_BLOCK_NONE;
        dev->ctrl  = val;
        return;
    }

    if (dev->block != EPAT_BLOCK_NONE) {
        /*
         * In a read block the strobe, not the read, moves the data: w2(6+ph)
         * puts the next byte's low nibble on the status lines and w2(4+ph) its
         * high nibble. SD120PPD.SYS reads status twice per strobe in its slower
         * modes, so advancing on reads would slip a nibble every time.
         */
        if ((dev->block == EPAT_BLOCK_READ) && (val != dev->ctrl)) {
            if (val & 0x02) {
                dev->block_latch = epat_data_read(dev);
                dev->chip[EPAT_CHIP_CHECKSUM] ^= dev->block_latch;
                dev->block_half  = 0;
            } else
                dev->block_half = 1;
        }
        dev->ctrl = val;
        return;
    }

    /*
     * Each nibble is clocked out by a 5 -> 4 strobe on the control port. Two
     * per byte, then on to the next.
     */
    if ((dev->cpp_id_len > 0) && (dev->ctrl == 0x05) && (val == 0x04)) {
        if (dev->cpp_id_hi)
            dev->cpp_id_hi = 0;
        else {
            dev->cpp_id_hi = 1;
            if (++dev->cpp_id_pos >= dev->cpp_id_len)
                dev->cpp_id_len = 0;
        }
        dev->ctrl = val;
        return;
    }

    if (dev->connected && (val == 0x01)) {
        dev->ustate    = EPAT_UNLOCK_IDLE;
        dev->upos      = 0;
        dev->nibble_hi = 0;
    } else if (dev->connected && (val == 0x03))
        dev->nibble_hi = 0; /* first read returns the LOW nibble */
    else if (dev->connected && (val == 0x04) && !dev->ucmd_pending)
        dev->nibble_hi = 1; /* second read returns the HIGH nibble */

    dev->ctrl = val;
}

static uint8_t
epat_read_status(void *priv)
{
    epat_t *dev = (epat_t *) priv;
    uint8_t val;
    uint8_t ret;

    if (dev->irq_test)
        return (uint8_t) (dev->status | 0x60);

    /*
     * A CPP response outranks everything: the host is mid-scan and has not
     * connected yet, so the usual "not connected, return status" rule would
     * hide it. High nibble first, then low, both in the top four bits -
     * SD120PPD.SYS combines them as (first & 0xF0) | (second >> 4).
     */
    if (dev->cpp_id_len > 0) {
        uint8_t b   = dev->cpp_id[dev->cpp_id_pos];
        uint8_t ret = dev->cpp_id_hi ? (uint8_t) (b & 0xF0)
                                     : (uint8_t) ((b & 0x0F) << 4);

        return ret;
    }

    if (dev->unit_pending) {
        dev->unit_pending = 0;
        return dev->unit_byte;
    }

    if (dev->irq_armed && !dev->connected && (dev->ustate == EPAT_UNLOCK_IDLE))
        return dev->status | 0x40;

    /* Mid-handshake the checkpoints take priority over any register value. */
    if ((dev->ustate != EPAT_UNLOCK_IDLE) || !dev->connected)
        return dev->status;

    if (dev->block == EPAT_BLOCK_READ) {
        /* j44 takes the high nibble from the second read, unshifted. */
        if (dev->block_half)
            return dev->block_latch & 0xF0;

        /*
         * The low nibble is returned in the TOP four bits, and bit 3 MUST be
         * clear: epat_read_block treats a set bit 3 as "that read carried the
         * whole byte" and skips the second one, which would corrupt every
         * byte read. EPAT_STAT_IDLE has bit 3 set, so it cannot be used here.
         */
        return (uint8_t) ((dev->block_latch & 0x0F) << 4);
    }

    if (dev->reg_addr >= sizeof(dev->regs))
        return dev->status;

    /*
     * A byte is fetched once and shifted out as two nibbles. Fetching it again
     * for the high nibble would advance the data register twice per byte.
     */
    /* Once per strobe: a second read of the same nibble must not re-read the register. */
    if (!dev->nibble_hi && !dev->reg_fetched) {
        dev->reg_latch   = epat_reg_read(dev, dev->reg_addr);
        dev->reg_fetched = 1;
    }

    val = dev->reg_latch;

    /*
     * j44(a, b) = ((a >> 4) & 0x0f) | (b & 0xf0), so each nibble must arrive in
     * the TOP four bits. The low four are not used by the combine; the bridge
     * drives them from its own state and the driver ignores them.
     */
    if (dev->nibble_hi)
        ret = (val & 0xF0) | (EPAT_STAT_IDLE & 0x0F);
    else
        ret = (uint8_t) ((val & 0x0F) << 4) | (EPAT_STAT_IDLE & 0x0F);

    /* No trace here. This fires on every half of every register read, so a
       status poll alone produces thousands of lines a second and the log
       becomes the slowest thing in the machine. Trace at command level. */

    return ret;
}

static uint8_t
epat_read_ctrl(void *priv)
{
    const epat_t *dev = (epat_t *) priv;

    return dev->ctrl;
}

static uint8_t
epat_ecp_read_data(void *priv)
{
    epat_t *dev = (epat_t *) priv;

    /*
     * ECP reverse transfer. Exactly the byte the nibble path would shift out
     * as two halves through the status register - same source, so the ATAPI
     * layer cannot tell the two transports apart, which is the point of
     * having both. The host has already put the port in reverse ECP mode;
     * the LPT layer only calls this when it has no FIFO content of its own.
     */
    if (!dev->connected)
        return 0xFF;

    /*
     * The data cycles are meaningless without the address cycle that selects
     * what they carry, so refuse them. A model that streamed anyway would pass
     * a driver that has no framing and would then fail on the real chip.
     */
    if ((dev->ecp_cmd != EPAT_ECP_READ) && (dev->ecp_cmd != EPAT_ECP_LAST)) {
        epat_log(dev->log, "ECP read with no block command (cmd %02X)\n", dev->ecp_cmd);
        return 0xFF;
    }

    return epat_data_read(dev);
}

/*
 * An ECP ADDRESS cycle: the host wrote base+0 with the ECR in an ECP mode.
 * This is how the fast path is commanded - see EPAT_ECP_READ above.
 */
static void
epat_ecp_write_addr(uint8_t val, void *priv)
{
    epat_t *dev = (epat_t *) priv;

    if (!dev->connected)
        return;

    epat_log(dev->log, "ECP command %02X\n", val);
    dev->ecp_cmd = val;
}

static void
epat_ecp_write_data(uint8_t val, void *priv)
{
    epat_t *dev = (epat_t *) priv;

    /* The payload half of an ECP transfer - same sink the nibble block
       write feeds, so the ATAPI layer sees one stream either way. */
    if (!dev->connected)
        return;

    if (dev->ecp_cmd != EPAT_ECP_WRITE) {
        epat_log(dev->log, "ECP write with no block command (cmd %02X, byte %02X)\n",
                 dev->ecp_cmd, val);
        return;
    }

    epat_data_write(dev, val);
}

static void *
epat_init(UNUSED(const device_t *info))
{
    epat_t *dev = (epat_t *) calloc(1, sizeof(epat_t));

    if (dev == NULL)
        return NULL;

    dev->status = EPAT_STAT_IDLE;
    /* Before any device reset, a host may identify the chip - see above. */
    dev->regs[EPAT_REG_VERSION] = EPAT_CHIP_VERSION;
    dev->log    = log_open("EPAT");
    dev->port   = (uint8_t) (device_get_instance() - 1);
    dev->busy_us = device_get_config_int("busy_ms") * 1000;
    dev->reset_us = device_get_config_int("reset_ms") * 1000;

    timer_add(&dev->busy_timer, epat_busy_done, dev, 0);
    timer_add(&dev->reset_timer, epat_reset_done, dev, 0);
    timer_add(&dev->irq_timer, epat_irq_ready, dev, 0);

    /* The drive does not exist yet - see epat_attach_drive(). */

    dev->lpt = lpt_attach_ex(dev->port,
                             epat_write_data, epat_write_ctrl, NULL,
                             epat_read_status, epat_read_ctrl,
                             NULL, NULL, dev);

    /*
     * The bridge sits behind an ECP-capable card on the real machine (the
     * Intek21 TK9901 at 0x378/IRQ 7) and the vendor's DOS driver selects
     * "ECP Read"/"ECP Write" there. Offer the reverse-transfer route so a
     * guest driver that picks ECP can actually receive.
     */
    lpt_set_ecp_read_data(dev->lpt, epat_ecp_read_data);
    lpt_set_ecp_write_data(dev->lpt, epat_ecp_write_data);
    lpt_set_ecp_write_addr(dev->lpt, epat_ecp_write_addr);

    return dev;
}

static void
epat_close(void *priv)
{
    epat_t *dev = (epat_t *) priv;

    if (dev != NULL) {
        if (dev->log != NULL)
            log_close(dev->log);
        free(dev);
    }
}

static const device_config_t epat_config[] = {
    {
        /*
         * How long the drive holds BSY after a command. 0 is the original
         * instant behaviour and is NOT faithful - the real LS-120 was measured
         * still BSY after 0.5 s. Use a realistic value when testing a driver's
         * timeouts, and 0 when testing its logic.
         */
        .name           = "busy_ms",
        .description    = "Drive busy time after a command",
        .type           = CONFIG_SELECTION,
        .default_string = NULL,
        .default_int    = 0,
        .file_filter    = NULL,
        .spinner        = { 0 },
        .selection      = {
            { .description = "Instant (not faithful)", .value =    0 },
            { .description = "50 ms",                  .value =   50 },
            { .description = "250 ms",                 .value =  250 },
            { .description = "750 ms (measured)",      .value =  750 },
            { .description = "2 s",                    .value = 2000 },
            { .description = ""                                      }
        },
        .bios           = { { 0 } }
    },
    {
        /*
         * How long the drive holds BSY after SRST is released. 0 is the
         * original instant behaviour and is NOT faithful: a bench probe
         * bounded at ~0.29 s aborted every command after the reset, and the
         * same script at ~2.3 s read LBA 0 first time. Use a realistic value
         * when testing a driver's bring-up, and 0 when testing its logic.
         */
        .name           = "reset_ms",
        .description    = "Drive settle time after SRST",
        .type           = CONFIG_SELECTION,
        .default_string = NULL,
        .default_int    = 0,
        .file_filter    = NULL,
        .spinner        = { 0 },
        .selection      = {
            { .description = "Instant (not faithful)", .value =    0 },
            { .description = "250 ms",                 .value =  250 },
            { .description = "1 s",                    .value = 1000 },
            { .description = "2.5 s (measured)",       .value = 2500 },
            { .description = "6 s",                    .value = 6000 },
            { .description = ""                                      }
        },
        .bios           = { { 0 } }
    },
    { .name = "", .description = "", .type = CONFIG_END }
};

const device_t lpt_epat_device = {
    .name          = "Shuttle EPAT parallel-port ATAPI bridge",
    .internal_name = "lpt_epat",
    .flags         = DEVICE_LPT,
    .local         = 0,
    .init          = epat_init,
    .close         = epat_close,
    .reset         = NULL,
    .available     = NULL,
    .speed_changed = NULL,
    .force_redraw  = NULL,
    .config        = epat_config
};
