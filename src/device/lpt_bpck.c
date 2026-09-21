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
 *           The BackPack wire protocol is already modelled in lpt_ditto.c,
 *           because the Iomega Ditto rides on the same Micro Solutions
 *           bridge. What differs is everything above it: the Ditto exposes a
 *           tape controller, while a BackPack CD-ROM exposes an ATA task file
 *           with an ATAPI drive behind it. This file carries its own copy of
 *           the wire layer so neither device can break the other; the two are
 *           worth factoring together once both are proven.
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
#include <86box/plat_unused.h>
#include <86box/scsi.h>
#include <86box/scsi_device.h>
#include <86box/hdc_ide.h>
#include <86box/cdrom.h>
#include <86box/scsi_cdrom.h>
#include <86box/log.h>

#define ENABLE_LPT_BPCK_LOG 1
#ifdef ENABLE_LPT_BPCK_LOG
static void
bpck_log(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    pclog_ex(fmt, ap);
    va_end(ap);
}
#else
#    define bpck_log(fmt, ...)
#endif

/* Parallel port lines, named from the point of view of the host. */
#define LPT_CTRL_STROBE   0x01
#define LPT_CTRL_AUTOFD   0x02
#define LPT_CTRL_INIT     0x04
#define LPT_CTRL_SELECT   0x08
#define LPT_CTRL_LINES    0x0f

#define LPT_STAT_ERROR    0x08
#define LPT_STAT_SELECT   0x10
#define LPT_STAT_PAPEROUT 0x20
#define LPT_STAT_ACK      0x40
#define LPT_STAT_BUSY     0x80
#define LPT_STAT_MASK     0xf8
#define LPT_STAT_IDLE     0xd8

/*
 * Protocols the bridge can be programmed for. The host writes the bits into
 * register 0x04.
 */
#define BPCK_PROTO_SPP       0
#define BPCK_PROTO_PS2       1
#define BPCK_PROTO_EPP       2

#define BPCK_PROTO_BITS_PS2  0x10
#define BPCK_PROTO_BITS_EPP  0x18
#define BPCK_PROTO_BITS_MASK 0x18

typedef struct bpck_s {
    void   *lpt;

    /* The port as the host last left it. */
    uint8_t dat;
    uint8_t ctrl;
    uint8_t dat_out;
    uint8_t rd_out;
    uint8_t last_status;

    /* Which pod on the chain this is; the knock carries the address. */
    uint8_t unit;

    int     connected;
    int     knock;
    int     ident;
    int     latching;
    int     latch_commits;

    uint8_t cur_reg;
    uint8_t rd_val;
    int     rd_high;

    int     proto;

    /*
     * The register file. Phase one: every access is logged and a read gives
     * back what was written, so the probe sequence of a real driver
     * identifies the map before any of it is given behaviour.
     */
    uint8_t regs[256];

    /*
     * The 93C46 the pod identifies itself from. Register 0x06 carries the
     * lines and register 0x00 bit 7 is DO, which is why a read of 0x00 is not
     * simply the stored byte. Addressing, opcode and word size are the
     * standard part; only the contents are ours to supply.
     */
    uint8_t  ee_lines;
    uint16_t ee_shift;   /* command being clocked in */
    int      ee_bits;    /* bits clocked since CS rose */
    uint16_t ee_out;     /* word being clocked out */
    int      ee_reading;
    int      ee_do;
    uint16_t ee_data[64];

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
} bpck_t;

#define BPCK_EE_ENABLE 0x08
#define BPCK_EE_CS     0x04
#define BPCK_EE_DI     0x02
#define BPCK_EE_CLK    0x01

/* The ATAPI task file the bridge exposes, and the drive behind it. */
#define BPCK_REG_TASKFILE 0x40 /* measured: the driver forms 0x40 | offset */
#define BPCK_REG_DEVCTL   0x46

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

static const char *bpck_proto_name[] = { "SPP 4-bit", "PS/2 8-bit", "EPP" };

static const char *
bpck_state(const bpck_t *dev)
{
    static char buf[64];

    snprintf(buf, sizeof(buf), "%s reg=%02X knock=%d%s",
             dev->connected ? "CONN" : "----", dev->cur_reg, dev->knock,
             dev->latching ? " latch" : "");

    return buf;
}

/* The ATAPI engine, defined below: the task file is served out of it. */
static uint8_t bpck_reg_read(bpck_t *dev, const uint8_t addr);
static void    bpck_reg_write(bpck_t *dev, const uint8_t addr, const uint8_t val);

/* --------------------------------------------------------------------- */
/* Layer 2: the register file                                            */
/* --------------------------------------------------------------------- */

/*
 * The 93C46. CS falling ends a frame; every rising CLK clocks DI in until a
 * command is recognised, after which each rising edge shifts the addressed
 * word out MSB first. Only READ is implemented: the driver issues EWEN and
 * WRITE as well, but a pod whose identity changed under it would be a fault,
 * not a feature.
 */
static void
bpck_ee_write(bpck_t *dev, uint8_t val)
{
    const uint8_t old = dev->ee_lines;

    dev->ee_lines = val;

    if (!(val & BPCK_EE_CS)) {
        dev->ee_bits    = 0;
        dev->ee_shift   = 0;
        dev->ee_reading = 0;
        dev->ee_do      = 0;
        return;
    }

    /* Everything happens on the rising edge of the clock. */
    if (!((val & BPCK_EE_CLK) && !(old & BPCK_EE_CLK)))
        return;

    if (dev->ee_reading) {
        dev->ee_do  = (dev->ee_out & 0x8000) ? 1 : 0;
        dev->ee_out = (uint16_t) (dev->ee_out << 1);
        return;
    }

    dev->ee_shift = (uint16_t) ((dev->ee_shift << 1) | ((val & BPCK_EE_DI) ? 1 : 0));
    dev->ee_bits++;

    /* START, opcode and six address bits: nine in all. */
    if (dev->ee_bits >= 9) {
        const uint8_t addr = dev->ee_shift & 0x3f;

        if ((dev->ee_shift & 0x180) == 0x180) {
            dev->ee_out     = dev->ee_data[addr];
            dev->ee_reading = 1;
            bpck_log("BPCK: EEPROM read word %02X -> %04X\n", addr, dev->ee_out);
        } else {
            bpck_log("BPCK: EEPROM command %03X ignored\n", dev->ee_shift & 0x1ff);
            dev->ee_bits  = 0;
            dev->ee_shift = 0;
        }
    }
}

static uint8_t
bpck_read_reg(bpck_t *dev, int reg)
{
    uint8_t ret;

    /*
     * The task file the driver forms as 0x40 | offset belongs to the drive,
     * not the bridge, so it is served by the ATAPI engine.
     */
    if (((reg & 0xff) >= BPCK_REG_TASKFILE) &&
        ((reg & 0xff) <= (BPCK_REG_TASKFILE + ATA_STATUS))) {
        ret = bpck_reg_read(dev, (uint8_t) reg);
        bpck_log("BPCK:    RR %02X -> %02X\n", reg & 0xff, ret);
        return ret;
    }

    ret = dev->regs[reg & 0xff];

    /* Register 0x00 bit 7 is the EEPROM data line, not stored state. */
    if ((reg & 0xff) == 0x00)
        ret = (uint8_t) ((ret & 0x7f) | (dev->ee_do ? 0x80 : 0x00));

    bpck_log("BPCK:    RR %02X -> %02X\n", reg & 0xff, ret);

    return ret;
}

static void
bpck_write_reg(bpck_t *dev, int reg, uint8_t val)
{
    bpck_log("BPCK:    WR %02X <- %02X\n", reg & 0xff, val);

    dev->regs[reg & 0xff] = val;

    if (((reg & 0xff) >= BPCK_REG_TASKFILE) &&
        ((reg & 0xff) <= (BPCK_REG_TASKFILE + ATA_STATUS))) {
        bpck_reg_write(dev, (uint8_t) reg, val);
        return;
    }

    if ((reg & 0xff) == 0x06)
        bpck_ee_write(dev, val);

    /*
     * Register 0x04 carries the protocol the bridge is to use. It is acted on
     * here rather than at the next connect: the host switches mode and then
     * talks, with no disconnect in between.
     */
    if ((reg & 0xff) == 0x04) {
        const uint8_t bits = val & BPCK_PROTO_BITS_MASK;
        const int     was  = dev->proto;

        if (bits == BPCK_PROTO_BITS_EPP)
            dev->proto = BPCK_PROTO_EPP;
        else if (bits == BPCK_PROTO_BITS_PS2)
            dev->proto = BPCK_PROTO_PS2;
        else
            dev->proto = BPCK_PROTO_SPP;

        if (dev->proto != was)
            bpck_log("BPCK: protocol now %s\n", bpck_proto_name[dev->proto]);
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
            sc->packet_status = PHASE_NONE;
            bpck_log("BPCK: command done, status %02X\n", dev->tf->atastat);
            break;

        case PHASE_DATA_IN:
        case PHASE_DATA_OUT:
            dev->tf->atastat = READY_STAT | DRQ_STAT | (dev->tf->atastat & ERR_STAT);
            /* PHASE_DATA_IN gives ireason 2 (I/O set), PHASE_DATA_OUT gives 0. */
            dev->tf->phase   = !(sc->packet_status & 0x01) << 1;
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
        return;
    }

    bpck_log("BPCK: PACKET, byte count %u\n", dev->tf->request_length);

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
static uint8_t
bpck_reg_read(bpck_t *dev, const uint8_t addr)
{
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
            return dev->tf->atastat;
        default:
            return dev->tf_regs[addr];
    }
}

static void
bpck_reg_write(bpck_t *dev, const uint8_t addr, const uint8_t val)
{
    dev->tf_regs[addr] = val;

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

/* --------------------------------------------------------------------- */
/* Layer 1: the BackPack wire protocol                                   */
/* --------------------------------------------------------------------- */

/*
 * A nibble across the five status bits the bridge owns: value bits 0-2 into
 * status 3-5, bit 3 into status 7. ACK is left clear so a nibble can never be
 * mistaken for the connect response.
 */
static uint8_t
bpck_encode_nibble(uint8_t val)
{
    return (uint8_t) (((val & 0x07) << 3) | ((val & 0x08) << 4));
}

static uint8_t
bpck_connect_response(const bpck_t *dev)
{
    return (uint8_t) ((dev->proto == BPCK_PROTO_EPP)
                          ? (LPT_STAT_ACK | LPT_STAT_BUSY)
                          : LPT_STAT_ACK);
}

/*
 * The address probe that follows every knock: the unit number, and the
 * complement of it when AUTOFD is low. The host requires the two readings to
 * be complements - an empty port cannot do that - and only then checks the
 * number.
 */
static uint8_t
bpck_ident_response(const bpck_t *dev)
{
    const uint8_t id = (dev->ctrl & LPT_CTRL_AUTOFD)
                           ? dev->unit
                           : (uint8_t) ~dev->unit;

    return (uint8_t) (bpck_connect_response(dev) | ((id & 0x07) << 3));
}

static void
bpck_connect(bpck_t *dev)
{
    dev->connected = 1;
    dev->knock     = 0;
    dev->ident     = 1;
    dev->rd_high   = 0;
    dev->rd_out    = bpck_connect_response(dev);

    bpck_log("BPCK: connected in %s mode\n", bpck_proto_name[dev->proto]);
}

static void
bpck_disconnect(bpck_t *dev)
{
    if (!dev->connected)
        return;

    dev->connected = 0;
    dev->knock     = 0;
    dev->ident     = 0;
    dev->latching  = 0;
    dev->rd_out    = 0x00;

    bpck_log("BPCK: disconnected\n");
}

static void
bpck_advance_read(bpck_t *dev)
{
    uint8_t val;

    if (!dev->connected)
        return;

    /*
     * In byte mode the bridge drives the data lines rather than the status
     * bits, so a whole byte comes back per toggle.
     */
    if (dev->proto == BPCK_PROTO_PS2) {
        val          = bpck_read_reg(dev, dev->cur_reg);
        dev->rd_out  = 0x00;
        dev->dat_out = val;
        if (dev->lpt != NULL)
            lpt_write_to_dat(dev->lpt, val);
        return;
    }

    if (dev->rd_high) {
        dev->rd_out  = bpck_encode_nibble(dev->rd_val >> 4);
        dev->rd_high = 0;
    } else {
        dev->rd_val  = bpck_read_reg(dev, dev->cur_reg);
        dev->rd_out  = bpck_encode_nibble(dev->rd_val & 0x0f);
        dev->rd_high = 1;
    }
}

static void
bpck_epp_write_data(uint8_t is_addr, uint8_t val, void *priv)
{
    bpck_t *dev = (bpck_t *) priv;

    if (is_addr) {
        bpck_log("BPCK: W3 %02X (EPP address)\n", val);
        dev->cur_reg = val;
        dev->rd_high = 0;
    } else {
        bpck_log("BPCK: W4 %02X (EPP data)\n", val);
        bpck_write_reg(dev, dev->cur_reg, val);
    }
}

static void
bpck_epp_request_read(uint8_t is_addr, void *priv)
{
    bpck_t *dev = (bpck_t *) priv;
    uint8_t val = 0xff;

    if (!is_addr)
        val = bpck_read_reg(dev, dev->cur_reg);

    bpck_log("BPCK: R%d -> %02X (EPP %s)\n", is_addr ? 3 : 4, val,
             is_addr ? "address" : "data");

    dev->dat_out = val;
    if (dev->lpt != NULL)
        lpt_write_to_dat(dev->lpt, val);
}

static void
bpck_write_data(uint8_t val, void *priv)
{
    bpck_t *dev = (bpck_t *) priv;

    bpck_log("BPCK: W0 %02X            [%s]\n", val, bpck_state(dev));

    dev->dat = val;

    /*
     * An address on the data lines starts a knock, so toggles counted before
     * it were not part of one. The port calls us for every write, changed or
     * not, so counting from the write is safe where arming on it is not.
     */
    dev->knock = 0;

    /*
     * The bridge drives the data lines only during a PS/2 or EPP read;
     * otherwise the host reads its own latch back, so keep the input register
     * of the port tracking what was written or it sees zeroes and misjudges
     * what the port can do.
     */
    if (dev->lpt != NULL)
        lpt_write_to_dat(dev->lpt, val);
}

static void
bpck_write_ctrl(uint8_t val, void *priv)
{
    bpck_t       *dev       = (bpck_t *) priv;
    const uint8_t old       = dev->ctrl;
    const uint8_t chg       = (uint8_t) ((old ^ val) & LPT_CTRL_LINES);
    const uint8_t lines     = (uint8_t) (val & LPT_CTRL_LINES);
    const uint8_t old_lines = (uint8_t) (old & LPT_CTRL_LINES);

    bpck_log("BPCK: W2 %02X (was %02X)  [%s]\n", val, old, bpck_state(dev));

    dev->ctrl = val;

    /* The bridge latches on edges. Rewriting the same value is nothing. */
    if (chg == 0x00)
        return;

    /*
     * An out-of-band register write, addressed off the data lines with SELECT
     * raised: SELECT latches the register number, each AUTOFD toggle commits
     * the data lines, STROBE dropping ends it. A disconnect opens identically
     * but commits nothing, so this must be tested before anything else looks
     * at SELECT.
     */
    if (dev->latching) {
        if (chg & LPT_CTRL_AUTOFD) {
            bpck_write_reg(dev, dev->cur_reg, dev->dat);
            dev->latch_commits++;
        }

        if (!(lines & LPT_CTRL_STROBE)) {
            dev->latching = 0;
            if (dev->latch_commits == 0)
                bpck_disconnect(dev);
        }

        return;
    }

    if (dev->connected && (lines == (LPT_CTRL_STROBE | LPT_CTRL_SELECT))) {
        dev->latching      = 1;
        dev->latch_commits = 0;
        dev->cur_reg       = dev->dat;
        dev->rd_high       = 0;
        return;
    }

    /*
     * Letting go: INIT dropped leaving AUTOFD alone, then INIT and SELECT
     * raised together. Only the pair in that order ends the link - a nibble
     * read passes through AUTOFD-alone twice a byte.
     */
    if (dev->connected && (old_lines == LPT_CTRL_AUTOFD) &&
        (lines & LPT_CTRL_SELECT)) {
        bpck_disconnect(dev);
        return;
    }

    /*
     * The address probe that closes the knock. Nothing else may look at these
     * two AUTOFD edges: once the link is up an AUTOFD edge latches a register
     * number off the data lines, which still carry the unit number.
     */
    if (dev->ident) {
        if (chg & LPT_CTRL_SELECT)
            dev->ident = 0;
        return;
    }

    /*
     * The connect knock: three toggles of SELECT while the other lines hold
     * INIT alone, with the unit address on the data lines. Taken as a level,
     * so stray counts are thrown out.
     */
    if ((lines & ~LPT_CTRL_SELECT) != LPT_CTRL_INIT)
        dev->knock = 0;
    else if (chg & LPT_CTRL_SELECT) {
        if (++dev->knock >= 3) {
            dev->knock = 0;

            /*
             * The knock is addressed: these chain, so only the pod named on
             * the data lines may take the link. Answering to every address is
             * not harmless - the host decides a unit is present by the status
             * changing across the knock, so a pod that answers everywhere is
             * found nowhere.
             */
            if (dev->dat == dev->unit)
                bpck_connect(dev);
            else
                bpck_log("BPCK: knock for unit %02X, not ours (%02X)\n",
                         dev->dat, dev->unit);
        }

        return;
    }

    if (!dev->connected)
        return;

    if (chg & LPT_CTRL_AUTOFD) {
        dev->cur_reg = dev->dat;
        dev->rd_high = 0;

        /*
         * Addressing a register also puts the connect answer back on the
         * status lines, and it has to be a level the bridge holds: a host that
         * reads the status without clocking anything must keep seeing it.
         */
        dev->rd_out = bpck_connect_response(dev);
        return;
    }

    if (chg & LPT_CTRL_INIT) {
        if (lines & LPT_CTRL_STROBE)
            bpck_write_reg(dev, dev->cur_reg, dev->dat);
        else
            bpck_advance_read(dev);
    }
}

static uint8_t
bpck_read_status(void *priv)
{
    bpck_t *dev = (bpck_t *) priv;
    uint8_t ret;

    if (dev->connected && dev->ident)
        ret = bpck_ident_response(dev);
    else if (dev->connected)
        ret = dev->rd_out;
    else
        /* Not connected: the bridge leaves the status lines alone. */
        ret = LPT_STAT_IDLE;

    ret &= LPT_STAT_MASK;

    /* Idle status reads are how a host spins; trace only what changes. */
    if ((ret != dev->last_status) || dev->connected) {
        bpck_log("BPCK: R1 -> %02X          [%s]\n", ret, bpck_state(dev));
        dev->last_status = ret;
    }

    return ret;
}

/* --------------------------------------------------------------------- */
/* 86Box device plumbing                                                 */
/* --------------------------------------------------------------------- */

static void *
bpck_init(UNUSED(const device_t *info))
{
    bpck_t *dev = calloc(1, sizeof(bpck_t));

    if (dev == NULL)
        return NULL;

    dev->unit  = (uint8_t) device_get_config_int("unit");
    dev->port  = (uint8_t) device_get_config_int("port");

    memcpy(dev->ee_data, bpck_ee_default, sizeof(dev->ee_data));
    dev->proto = BPCK_PROTO_SPP;

    dev->lpt = lpt_attach(bpck_write_data, bpck_write_ctrl, NULL,
                          bpck_read_status, NULL,
                          bpck_epp_write_data, bpck_epp_request_read, dev);
    if (dev->lpt == NULL) {
        /* Another device already has this port. */
        free(dev);
        return NULL;
    }

    timer_add(&dev->busy_timer, bpck_busy_done, dev, 0);
    timer_add(&dev->reset_timer, bpck_reset_done, dev, 0);

    bpck_log("BPCK: attached as unit %02X on LPT port %i\n", dev->unit, dev->port);

    return dev;
}

static void
bpck_close(void *priv)
{
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
    {
        .name           = "port",
        .description    = "LPT port the drive is assigned to",
        .type           = CONFIG_SELECTION,
        .default_string = NULL,
        .default_int    = 0,
        .file_filter    = NULL,
        .spinner        = { 0 },
        .selection      = {
            { .description = "LPT1", .value = 0 },
            { .description = "LPT2", .value = 1 },
            { .description = "LPT3", .value = 2 },
            { .description = "LPT4", .value = 3 },
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
