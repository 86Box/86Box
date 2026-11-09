/*
 * 86Box     A hypervisor and IBM PC system emulator.
 *
 *           Micro Solutions BackPack Bantam Portable CD-ROM.
 *
 *           Modelled from a real unit: model 180100, E/N 00749, made in the
 *           USA, serial 17627007, a 10X drive with a Toshiba XM-1502B
 *           mechanism inside it. Its identity EEPROM is reproduced verbatim
 *           below, read off the drive over the parallel port.
 *
 *           The BackPack wire protocol is shared with the Iomega Ditto,
 *           which rides on the same Micro Solutions bridge, in
 *           lpt_bpck_link.c. What differs is everything above it: the Ditto
 *           exposes a tape controller, while a BackPack CD-ROM exposes an ATA
 *           task file with an ATAPI drive behind it.
 *
 *           Authors: Mike Lycett
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
#include <86box/lpt_bpck_link.h>
#include <86box/plat_unused.h>
#include <86box/scsi.h>
#include <86box/scsi_device.h>
#include <86box/hdc_ide.h>
#include <86box/cdrom.h>
#include <86box/scsi_cdrom.h>
#include <86box/hdd.h>
#include <86box/log.h>

#ifdef ENABLE_LPT_BPCK_LOG
static void
bpck_log(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    pclog_ex(fmt, ap);
    va_end(ap);
}
#    define BPCK_LINK_LOG bpck_log
#else
#    define bpck_log(fmt, ...)
#    define BPCK_LINK_LOG NULL
#endif

/*
 * Protocols the bridge can be programmed for. The host writes the bits into
 * register 0x04.
 */
#define BPCK_PROTO_BITS_PS2  0x10
#define BPCK_PROTO_BITS_EPP  0x18
#define BPCK_PROTO_BITS_MASK 0x18

typedef struct bpck_s {
    /* The wire, and which pod on the chain this is. */
    bpck_link_t link;

    /*
     * The register file. Phase one: every access is logged and a read gives
     * back what was written, so the probe sequence of a real driver
     * identifies the map before any of it is given behaviour.
     */
    uint8_t regs[256];

    /*
     * The 93C46 the pod identifies itself from; register 0x00 bit 7 is its
     * DO, which is why a read of 0x00 is not simply the stored byte.
     */
    bpck_ee_t ee;

    /* The ATAPI drive behind the bridge, and the task file it presents. */
    scsi_device_t *sd;
    ide_tf_t      *tf;
    void          *log;
    uint8_t        port;
    uint8_t        no_drive;
    uint8_t        tf_regs[0x100];
    pc_timer_t     busy_timer;
    int            busy_us;
    int            busy_pending;
    uint64_t       last_media_ts;
    int            spinning;
    pc_timer_t     reset_timer;
    int            reset_us;
    int            in_reset;
    int            reset_abrt;
    uint8_t        intrq; /* the drive's INTRQ, until status is read */

    /*
     * A hard disk behind the bridge (bus LPT, bridge BackPack), on an IDE
     * board of its own: the task file, device control and the data stream go
     * to the IDE emulation, a byte at a time, two of them to a 16-bit cycle.
     */
    void    *ide;
    uint16_t ide_rd;      /* the data word being read out */
    uint8_t  ide_rd_half; /* its high byte is next */
    uint8_t  ide_wr_lo;   /* the low byte of a data word being written */
    uint8_t  ide_wr_half; /* the high byte is next */
} bpck_t;


/* The ATAPI task file the bridge exposes, and the drive behind it. */
#define BPCK_REG_TASKFILE 0x40 /* measured: the driver forms 0x40 | offset */
#define BPCK_REG_DEVCTL   0x46
/* With a hard disk: 0x48 | offset is the IDE control block, as in Linux's
   bpck6.c (cont 1), so 0x4E is device control and alternate status. */
#define BPCK_IDE_CTL       0x08
#define BPCK_IDE_CTL_END   0x0f
#define BPCK_REG_IRQ       0x0b
#define BPCK_IRQ_INTRQ     0x80

/*
 * The EEPROM of a hard disk pod, where it differs from the CD-ROM pod's,
 * as BPHDDRV.SYS reads it: word 2 is the device type (0x0080 a hard disk,
 * 0x0082 a CD-ROM), word 0x11 the geometry the driver addresses the drive
 * in (sectors per track high, heads low), words 3-0x10 the drive's name (low byte first),
 * which must not be empty. Words 0x2F-0x36 are the password, zero for none.
 */
#define BPCK_EE_TYPE      0x02
#define BPCK_EE_TYPE_HD   0x0080
#define BPCK_EE_NAME      0x03
#define BPCK_EE_NAME_LEN  14
#define BPCK_EE_GEOMETRY  0x11

#define ATA_DATA     0
#define ATA_ERROR    1
#define ATA_IREASON  2
#define ATA_BCLO     4
#define ATA_BCHI     5
#define ATA_DRVHD    6
#define ATA_STATUS   7

#define ATA_CMD_PACKET 0xA0
#define ATA_CDB_LEN    12

#define ATA_ST_DRDY 0x40
#define ATA_ST_DSC  0x10

#define BPCK_SPINDOWN_TSC  ((uint64_t) 2000000 * TIMER_USEC)
#define BPCK_SPINNING_DIV  50

/* The ATAPI signature a packet device puts in the byte-count registers. */
#define ATAPI_SIG_LO 0x14
#define ATAPI_SIG_HI 0xEB

/*
 * Carried over from the EPAT engine, which serves its own bridge registers
 * from the same read path. A BackPack has neither, and both numbers are in
 * use for something else here, so they are put out of reach rather than
 * deleted - the engine is easier to keep in step with its original if the
 * shape of it is left alone.
 */
#define BPCK_REG_BLOCK     0xFE
#define BPCK_REG_VERSION   0xFD
#define BPCK_CHIP_VERSION  0xC0

/*
 * The identity EEPROM of a real pod, read off the hardware with
 * tools/gen_bpckee.py: a BackPack Bantam, model 180100, E/N 00749, serial
 * 17627007. The mechanism inside reports itself as "TOSHIBA CD-ROM
 * XM-1502B/2696"; the pod is what the label on the case names.
 * The driver reads all 64 words before it will accept the pod, so the whole
 * array matters, not just the identifying part.
 */
static const uint16_t bpck_ee_default[64] = {
    0x07F8, 0x0000, 0x0082, 0x4F54, 0x4853, 0x4249, 0x2041, 0x4443,
    0x522D, 0x4D4F, 0x5820, 0x2D4D, 0x3531, 0x3230, 0x2F42, 0x3632,
    0x3639, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000,
    0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000,
    0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000,
    0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x3204, 0x0000,
    0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x3731,
    0x3236, 0x3037, 0x3730, 0x0C08, 0x07CD, 0x0C08, 0x07CD, 0x0000,
};


/* The ATAPI engine, defined below: the task file is served out of it. */
static uint8_t bpck_reg_read(bpck_t *dev, const uint8_t addr);
static void    bpck_reg_write(bpck_t *dev, const uint8_t addr, const uint8_t val);

/* --------------------------------------------------------------------- */
/* Layer 2: the register file                                            */
/* --------------------------------------------------------------------- */


static uint8_t
bpck_read_reg(void *priv, int reg)
{
    bpck_t *dev = (bpck_t *) priv;
    uint8_t ret;

    /*
     * The task file the driver forms as 0x40 | offset belongs to the drive,
     * not the bridge, so it is served by the ATAPI engine.
     */
    if (((reg & 0xff) >= BPCK_REG_TASKFILE) &&
        ((reg & 0xff) <= (BPCK_REG_TASKFILE + ((dev->ide != NULL) ? BPCK_IDE_CTL_END : ATA_STATUS)))) {
        ret = bpck_reg_read(dev, (uint8_t) reg);
        bpck_log("BPCK:    RR %02X -> %02X\n", reg & 0xff, ret);
        return ret;
    }

    ret = dev->regs[reg & 0xff];
    /* Register 0x0B bit 7 is the drive's INTRQ: BPHDDRV.SYS polls it after
       each command, BPCDDRV.SYS 2.13 before it reads a data phase. */
    if ((reg & 0xff) == BPCK_REG_IRQ) {
        const int intrq = (dev->ide != NULL) ? ide_lpt_intrq(dev->port) : dev->intrq;

        ret = (uint8_t) ((ret & ~BPCK_IRQ_INTRQ) | (intrq ? BPCK_IRQ_INTRQ : 0x00));
    }

    /* Register 0x00 bit 7 is the EEPROM data line, not stored state. */
    if ((reg & 0xff) == 0x00)
        ret = (uint8_t) ((ret & 0x7f) | (dev->ee.dout ? 0x80 : 0x00));

    bpck_log("BPCK:    RR %02X -> %02X\n", reg & 0xff, ret);

    return ret;
}

static void
bpck_write_reg(void *priv, int reg, uint8_t val)
{
    bpck_t *dev = (bpck_t *) priv;

    bpck_log("BPCK:    WR %02X <- %02X\n", reg & 0xff, val);

    dev->regs[reg & 0xff] = val;

    if (((reg & 0xff) >= BPCK_REG_TASKFILE) &&
        ((reg & 0xff) <= (BPCK_REG_TASKFILE + ((dev->ide != NULL) ? BPCK_IDE_CTL_END : ATA_STATUS)))) {
        bpck_reg_write(dev, (uint8_t) reg, val);
        return;
    }

    if ((reg & 0xff) == 0x06)
        bpck_ee_write(&dev->ee, val, "BPCK", BPCK_LINK_LOG);

    /*
     * Register 0x04 carries the protocol the bridge is to use. It is acted on
     * here rather than at the next connect: the host switches mode and then
     * talks, with no disconnect in between.
     */
    if ((reg & 0xff) == 0x04) {
        const uint8_t bits = val & BPCK_PROTO_BITS_MASK;
        const int     was  = dev->link.proto;

        if (bits == BPCK_PROTO_BITS_EPP)
            dev->link.proto = BPCK_PROTO_EPP8;
        else if (bits == BPCK_PROTO_BITS_PS2)
            dev->link.proto = BPCK_PROTO_PS2;
        else
            dev->link.proto = BPCK_PROTO_SPP;

        if (dev->link.proto != was)
            bpck_log("BPCK: protocol now %s\n", bpck_proto_name[dev->link.proto]);
    }
}

static int
bpck_attach_drive(bpck_t *dev)
{
    if (dev->sd != NULL)
        return 1;

    if (dev->no_drive)
        return 0;

    dev->sd = cdrom_get_lpt_device(dev->port);

    if (dev->sd == NULL) {
        dev->no_drive = 1;
        bpck_log("BPCK: no removable disk assigned to LPT%i - "
                           "answering from the bridge's own registers\n",
                 dev->port + 1);
        return 0;
    }

    dev->tf = ((scsi_cdrom_t *) dev->sd->sc)->tf;
    bpck_log("BPCK: LPT%i drive attached\n", dev->port + 1);

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
static void bpck_pio_request(bpck_t *dev, int out);
static void bpck_atapi_callback(bpck_t *dev);
static void bpck_device_reset(bpck_t *dev);

/* The reset settle has expired - the drive is now genuinely ready. */
static void
bpck_reset_done(void *priv)
{
    bpck_t *dev = (bpck_t *) priv;

    dev->in_reset   = 0;
    dev->reset_abrt = 0;
    bpck_device_reset(dev);
}

/* The BSY window has expired - finish the command the drive was sitting on. */
static void
bpck_busy_done(void *priv)
{
    bpck_t *dev = (bpck_t *) priv;

    bpck_atapi_callback(dev);
}

/*
 * Does this command move the mechanism? INQUIRY, REQUEST SENSE and MODE SENSE are
 * answered out of drive firmware and return promptly; anything that reads or
 * positions the medium pays the spin-up. Charging one latency to every command
 * would stall enumeration, which a real drive does not do.
 */
static int
bpck_cdb_touches_media(uint8_t op)
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

static void
bpck_atapi_callback(bpck_t *dev)
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
             * Whatever delay the drive asked for is discarded and the phase it
             * moved to is acted on now: there is no bus master behind a parallel
             * bridge to run a deferred callback.
             */
            sc->callback = 0.0;
            if (sc->packet_status != PHASE_COMMAND)
                bpck_atapi_callback(dev);
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
                bpck_cdb_touches_media(sc->atapi_cdb[0])) {
                const uint64_t now  = tsc;
                const uint64_t idle = (dev->last_media_ts == 0) ? ~0ULL
                                                                : (now - dev->last_media_ts);
                int            us;

                if (!dev->spinning || (idle > BPCK_SPINDOWN_TSC)) {
                    us            = dev->busy_us;           /* cold - full spin-up */
                    dev->spinning = 1;
                } else
                    us = dev->busy_us / BPCK_SPINNING_DIV;  /* already turning */

                dev->last_media_ts = now;
                dev->busy_pending  = 1;
                dev->tf->atastat   = BUSY_STAT | (dev->tf->atastat & ERR_STAT);
                timer_set_delay_u64(&dev->busy_timer, (uint64_t) us * TIMER_USEC);
                bpck_log("BPCK: drive busy %d us (%s)\n", us,
                         (us == dev->busy_us) ? "spin-up" : "spinning");
                break;
            }
            dev->busy_pending = 0;
            dev->tf->atastat = READY_STAT;
            if (sc->packet_status == PHASE_ERROR)
                dev->tf->atastat |= ERR_STAT;
            dev->tf->phase    = 3;
            dev->intrq        = 1;
            sc->packet_status = PHASE_NONE;
            bpck_log("BPCK: command done, status %02X\n", dev->tf->atastat);
            break;

        case PHASE_DATA_IN:
        case PHASE_DATA_OUT:
            dev->tf->atastat = READY_STAT | DRQ_STAT | (dev->tf->atastat & ERR_STAT);
            /* PHASE_DATA_IN gives ireason 2 (I/O set), PHASE_DATA_OUT gives 0. */
            dev->tf->phase   = !(sc->packet_status & 0x01) << 1;
            dev->intrq       = 1;
            bpck_log("BPCK: data phase %s, %u bytes, request length %u\n",
                     (sc->packet_status == PHASE_DATA_IN) ? "in" : "out",
                     sc->packet_len, dev->tf->request_length);
            break;
    }
}

/* A transfer has reached the end of a block, or of the whole command. */
static void
bpck_pio_request(bpck_t *dev, const int out)
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
         * A failed data-out phase leaves PHASE_ERROR without calling
         * command_stop(). Completing on PHASE_ERROR as well as PHASE_COMPLETE is
         * what stops a failure stranding the host: otherwise DRQ stays asserted
         * and the driver polls status forever.
         */
        if ((sc->packet_status == PHASE_COMPLETE) ||
            (sc->packet_status == PHASE_ERROR)) {
            bpck_log("BPCK: data phase ended in %s\n",
                     (sc->packet_status == PHASE_ERROR) ? "ERROR" : "COMPLETE");
            bpck_atapi_callback(dev);
        }
    } else {
        /* Short tail: tell the host how much is actually left. */
        if ((sc->packet_len - dev->tf->pos) < sc->max_transfer_len) {
            sc->max_transfer_len    = (uint16_t) (sc->packet_len - dev->tf->pos);
            dev->tf->request_length = sc->max_transfer_len;
        }

        sc->packet_status = PHASE_DATA_IN | out;
        bpck_atapi_callback(dev);
        sc->request_pos = 0;
    }
}

/*
 * The data register. The bridge is a byte-wide link, so unlike the IDE path
 * this moves one byte per access rather than a word.
 */
static uint8_t
bpck_data_read(bpck_t *dev)
{
    scsi_common_t *sc = dev->sd->sc;
    uint8_t        ret;

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
        bpck_pio_request(dev, 0);

    return ret;
}

static void
bpck_data_write(bpck_t *dev, const uint8_t val)
{
    scsi_common_t *sc  = dev->sd->sc;
    uint8_t       *buf = NULL;

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
            bpck_pio_request(dev, 1);
    } else if (dev->tf->pos >= ATA_CDB_LEN) {
        bpck_log("BPCK: CDB %02X %02X %02X %02X %02X %02X "
                           "%02X %02X %02X %02X %02X %02X\n",
                 buf[0], buf[1], buf[2],  buf[3],  buf[4],  buf[5],
                 buf[6], buf[7], buf[8],  buf[9],  buf[10], buf[11]);

        dev->tf->pos      = 0;
        dev->tf->atastat  = BUSY_STAT;
        sc->packet_status = PHASE_COMMAND;
        bpck_atapi_callback(dev);
    }
}

/* A write to the command register. PACKET is the only one that means anything. */
static void
bpck_command(bpck_t *dev, const uint8_t cmd)
{
    scsi_common_t *sc = dev->sd->sc;

    if (dev->in_reset) {
        bpck_log("BPCK: command %02X inside the reset settle, aborting\n", cmd);
        dev->reset_abrt = 1;
        return;
    }

    if (cmd != ATA_CMD_PACKET) {
        /*
         * A packet device aborts anything that is not PACKET and leaves the
         * ATAPI signature in the byte-count registers. That is how a host
         * tells an ATAPI drive from an ATA one: it issues IDENTIFY DEVICE,
         * expects it to fail, and reads 14/EB back. Without the signature the
         * abort looks like a broken ATA disk and the host gives up.
         */
        bpck_log("BPCK: command %02X is not PACKET, aborting with the signature\n", cmd);
        dev->tf->atastat        = READY_STAT | ERR_STAT | ATA_ST_DSC;
        dev->tf->error          = ABRT_ERR;
        dev->tf->request_length = (ATAPI_SIG_HI << 8) | ATAPI_SIG_LO;
        dev->tf->phase          = 1;
        dev->intrq              = 1;
        return;
    }

    bpck_log("BPCK: PACKET, byte count %u\n", dev->tf->request_length);

    dev->tf->pos      = 0;
    sc->packet_status = PHASE_IDLE;
    dev->intrq        = 0;
    dev->tf->phase    = 1; /* ireason 1: the drive wants the CDB */
    dev->tf->atastat  = READY_STAT | DRQ_STAT;
}

/*
 * The ATA task file belongs to the drive, not to the bridge. These map the
 * eight task-file offsets onto ide_tf_t, which is the same structure the IDE
 * controller drives the drive through. Offsets outside the task file (device
 * control, and the bridge's own registers) stay in regs[].
 */
/* The data register of the hard disk: a byte of a 16-bit IDE cycle. */
static uint8_t
bpck_ide_data_read(bpck_t *dev)
{
    uint8_t ret;

    if (!dev->ide_rd_half)
        dev->ide_rd = ide_readw(0, dev->ide);
    ret               = dev->ide_rd_half ? (uint8_t) (dev->ide_rd >> 8) : (uint8_t) dev->ide_rd;
    dev->ide_rd_half ^= 1;

    return ret;
}

static void
bpck_ide_data_write(bpck_t *dev, const uint8_t val)
{
    if (!dev->ide_wr_half)
        dev->ide_wr_lo = val;
    else
        ide_writew(0, (uint16_t) (dev->ide_wr_lo | (val << 8)), dev->ide);
    dev->ide_wr_half ^= 1;
}

static uint8_t
bpck_ide_reg_read(bpck_t *dev, const uint8_t addr)
{
    const uint8_t off = addr - BPCK_REG_TASKFILE;

    if ((addr == BPCK_REG_BLOCK) || (off == ATA_DATA))
        return bpck_ide_data_read(dev);
    if (off >= BPCK_IDE_CTL)
        return (off == (BPCK_IDE_CTL + 6)) ? ide_read_alt_status(0, dev->ide) : 0xff;

    dev->ide_rd_half = dev->ide_wr_half = 0;
    return ide_readb(off, dev->ide);
}

static void
bpck_ide_reg_write(bpck_t *dev, const uint8_t addr, const uint8_t val)
{
    const uint8_t off = addr - BPCK_REG_TASKFILE;

    if (off == ATA_DATA)
        bpck_ide_data_write(dev, val);
    else if (off == (BPCK_IDE_CTL + 6))
        ide_write_devctl(0, val, dev->ide);
    else if (off < BPCK_IDE_CTL) {
        dev->ide_rd_half = dev->ide_wr_half = 0;
        ide_writeb(off, val, dev->ide);
    }
}

static uint8_t
bpck_reg_read(bpck_t *dev, const uint8_t addr)
{
    if ((dev->ide != NULL) &&
        ((addr == BPCK_REG_BLOCK) ||
         ((addr >= BPCK_REG_TASKFILE) && (addr <= (BPCK_REG_TASKFILE + BPCK_IDE_CTL_END)))))
        return bpck_ide_reg_read(dev, addr);

    bpck_attach_drive(dev);

    /*
     * Inside the settle window only status and error mean anything, and they
     * describe the mechanism rather than tf, which still holds pre-reset
     * state. C1/04 is what the bench returns for a command issued too early.
     */
    if (dev->in_reset) {
        if (addr == (BPCK_REG_TASKFILE + ATA_STATUS))
            return dev->reset_abrt ? (BUSY_STAT | READY_STAT | ERR_STAT) : BUSY_STAT;
        if (addr == (BPCK_REG_TASKFILE + ATA_ERROR))
            return dev->reset_abrt ? ABRT_ERR : 0x00;
    }

    if ((dev->tf != NULL) && (addr == BPCK_REG_BLOCK))
        return bpck_data_read(dev);

    if ((dev->tf == NULL) || (addr < BPCK_REG_TASKFILE) ||
        (addr > (BPCK_REG_TASKFILE + ATA_STATUS)))
        return dev->tf_regs[addr];

    switch (addr - BPCK_REG_TASKFILE) {
        case ATA_DATA:
            return bpck_data_read(dev);
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
            return dev->tf->atastat;
        default:
            return dev->tf_regs[addr];
    }
}

static void
bpck_reg_write(bpck_t *dev, const uint8_t addr, const uint8_t val)
{
    dev->tf_regs[addr] = val;

    if (dev->ide != NULL) {
        if ((addr >= BPCK_REG_TASKFILE) && (addr <= (BPCK_REG_TASKFILE + BPCK_IDE_CTL_END)))
            bpck_ide_reg_write(dev, addr, val);
        return;
    }

    bpck_attach_drive(dev);

    if ((dev->tf == NULL) || (addr < BPCK_REG_TASKFILE) ||
        (addr > (BPCK_REG_TASKFILE + ATA_STATUS)))
        return;

    switch (addr - BPCK_REG_TASKFILE) {
        case ATA_DATA:
            bpck_data_write(dev, val);
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
            bpck_command(dev, val);
            break;
        default:
            break;
    }
}

/* Present the drive as it is immediately after a reset. */
static void
bpck_device_reset(bpck_t *dev)
{
    memset(dev->tf_regs, 0x00, sizeof(dev->tf_regs));
    dev->tf_regs[BPCK_REG_VERSION]               = BPCK_CHIP_VERSION;
    dev->tf_regs[BPCK_REG_TASKFILE + ATA_STATUS] = ATA_ST_DRDY | ATA_ST_DSC;
    dev->tf_regs[BPCK_REG_TASKFILE + ATA_BCLO]   = ATAPI_SIG_LO;
    dev->tf_regs[BPCK_REG_TASKFILE + ATA_BCHI]   = ATAPI_SIG_HI;

    /*
     * A real drive sets its own signature here; the drive's reset writes it
     * into request_length, so the values above only apply when the bridge is
     * running with no drive attached.
     */
    if (bpck_attach_drive(dev)) {
        dev->sd->reset(dev->sd->sc);

        /*
         * A real drive raises a unit attention when it is reset. The drive's
         * own reset clears the flag, so without this the emulated drive would
         * be more forgiving than hardware, and a driver that mishandles the
         * condition would pass here and fail on a real one.
         */
        ((scsi_cdrom_t *) dev->sd->sc)->unit_attention = 1;
    }

    bpck_log("BPCK: device reset: status %02X, signature %02X %02X\n",
             dev->tf_regs[BPCK_REG_TASKFILE + ATA_STATUS], ATAPI_SIG_LO, ATAPI_SIG_HI);
}

static const bpck_link_ops_t bpck_link_ops = {
    .read_reg  = bpck_read_reg,
    .write_reg = bpck_write_reg
};

/* --------------------------------------------------------------------- */
/* 86Box device plumbing                                                 */
/* --------------------------------------------------------------------- */

/* The EEPROM of a hard disk pod: type, name and the geometry of the disk. */
static void
bpck_ee_hard_disk(bpck_t *dev, const hard_disk_t *hd)
{
    static const char name[BPCK_EE_NAME_LEN * 2 + 1] = "86BOX HARD DISK             ";

    dev->ee.data[BPCK_EE_TYPE] = BPCK_EE_TYPE_HD;
    for (int i = 0; i < BPCK_EE_NAME_LEN; i++)
        dev->ee.data[BPCK_EE_NAME + i] = (uint16_t) ((name[(i * 2) + 1] << 8) | name[i * 2]);
    dev->ee.data[BPCK_EE_GEOMETRY] = (uint16_t) (((hd->spt & 0xff) << 8) | (hd->hpc & 0xff));
}

static void *
bpck_init(UNUSED(const device_t *info))
{
    bpck_t *dev = calloc(1, sizeof(bpck_t));

    if (dev == NULL)
        return NULL;

    dev->link.unit      = (uint8_t) device_get_config_int("unit");
    dev->link.proto     = BPCK_PROTO_SPP;
    dev->link.max_proto = BPCK_PROTO_EPP32;
    dev->port           = (uint8_t) (device_get_instance() - 1);

    memcpy(dev->ee.data, bpck_ee_default, sizeof(dev->ee.data));

    if (!bpck_link_attach(&dev->link, &bpck_link_ops, dev, "BPCK", BPCK_LINK_LOG)) {
        /* Another device already has this port. */
        free(dev);
        return NULL;
    }

    timer_add(&dev->busy_timer, bpck_busy_done, dev, 0);
    timer_add(&dev->reset_timer, bpck_reset_done, dev, 0);

    /* A hard disk on this port with the BackPack as its bridge. */
    for (int c = 0; c < HDD_NUM; c++) {
        if (hdd_is_valid(c) && (hdd[c].bus_type == HDD_BUS_LPT) &&
            (hdd[c].channel == dev->port) && (hdd[c].lpt_bridge == HDD_LPT_BPCK)) {
            dev->ide = ide_lpt_init(dev->port);
            if (dev->ide != NULL)
                bpck_ee_hard_disk(dev, &hdd[c]);
            bpck_log("BPCK: LPT%i hard disk attached%s\n", dev->port + 1,
                     (dev->ide == NULL) ? " - no IDE board free" : "");
            break;
        }
    }

    bpck_log("BPCK: attached as unit %02X on LPT port %i\n", dev->link.unit, dev->port);

    return dev;
}

static void
bpck_close(void *priv)
{
    bpck_t *dev = (bpck_t *) priv;

    if ((dev != NULL) && (dev->ide != NULL))
        ide_lpt_close(dev->port);
    free(priv);
}

// clang-format off
static const device_config_t bpck_config[] = {
    {
        .name           = "unit",
        .description    = "Chain address",
        .type           = CONFIG_SELECTION,
        .default_string = NULL,
        .default_int    = 7,
        .file_filter    = NULL,
        .spinner        = { 0 },
        .selection      = {
            { .description = "Unit 0", .value = 0 },
            { .description = "Unit 1", .value = 1 },
            { .description = "Unit 2", .value = 2 },
            { .description = "Unit 3", .value = 3 },
            { .description = "Unit 7", .value = 7 },
            { .description = "" }
        },
        .bios           = { { 0 } }
    },
    { .name = "", .description = "", .type = CONFIG_END }
};
// clang-format on

const device_t lpt_bpck_device = {
    .name          = "Micro Solutions BackPack Bantam CD-ROM",
    .internal_name = "bpck",
    .flags         = DEVICE_LPT,
    .local         = 0,
    .init          = bpck_init,
    .close         = bpck_close,
    .reset         = NULL,
    .available     = NULL,
    .speed_changed = NULL,
    .force_redraw  = NULL,
    .config        = bpck_config
};
