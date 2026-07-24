/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          Emulation of a QIC-117 "floppy tape" drive, modelled after the
 *          Colorado Jumbo 250 (a QIC-80 drive).
 *
 *          These drives share the floppy ribbon cable with the regular
 *          floppy drives and occupy one of the four drive select lines.
 *          Bulk data moves with plain FDC READ DATA / WRITE DATA commands
 *          at 500 kbps, but the drive itself has no way of receiving
 *          commands over the floppy interface, so QIC-117 abuses the step
 *          line instead: the host issues a SEEK a given number of
 *          cylinders away, and the drive counts the resulting step pulses.
 *          The pulse count is the command. Results travel back over the
 *          TRACK 0 line, one bit per "report next bit" command, which the
 *          host samples with SENSE DRIVE STATUS.
 *
 *          The cartridge is a flat image addressed by segment. A segment
 *          is 32 sectors of 1024 bytes and is mapped onto the controller's
 *          C/H/R space at four segments per cylinder, 1020 segments per
 *          head - the same fixed geometry the QIC-40/80 format specifies.
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
#include <86box/timer.h>
#include <86box/plat.h>
#include <86box/path.h>
#include <86box/ui.h>
#include <86box/dma.h>
#include <86box/fdd.h>
#include <86box/fdd_tape.h>
#include <86box/fdc.h>

/* QIC-117 rev. J command set. Only the ones the drive acts on are named. */
enum {
    QIC_NO_COMMAND             = 0,
    QIC_RESET                  = 1,
    QIC_REPORT_NEXT_BIT        = 2,
    QIC_PAUSE                  = 3,
    QIC_MICRO_STEP_PAUSE       = 4,
    QIC_ALTERNATE_TIMEOUT      = 5,
    QIC_REPORT_DRIVE_STATUS    = 6,
    QIC_REPORT_ERROR_CODE      = 7,
    QIC_REPORT_DRIVE_CONFIG    = 8,
    QIC_REPORT_ROM_VERSION     = 9,
    QIC_LOGICAL_FORWARD        = 10,
    QIC_PHYSICAL_REVERSE       = 11,
    QIC_PHYSICAL_FORWARD       = 12,
    QIC_SEEK_HEAD_TO_TRACK     = 13,
    QIC_SEEK_LOAD_POINT        = 14,
    QIC_ENTER_FORMAT_MODE      = 15,
    QIC_WRITE_REFERENCE_BURST  = 16,
    QIC_ENTER_VERIFY_MODE      = 17,
    QIC_STOP_TAPE              = 18,
    QIC_MICRO_STEP_HEAD_UP     = 21,
    QIC_MICRO_STEP_HEAD_DOWN   = 22,
    QIC_SOFT_SELECT            = 23,
    QIC_SOFT_DESELECT          = 24,
    QIC_SKIP_REVERSE           = 25,
    QIC_SKIP_FORWARD           = 26,
    QIC_SELECT_RATE            = 27,
    QIC_ENTER_DIAGNOSTIC_1     = 28,
    QIC_ENTER_DIAGNOSTIC_2     = 29,
    QIC_ENTER_PRIMARY_MODE     = 30,
    QIC_REPORT_VENDOR_ID       = 32,
    QIC_REPORT_TAPE_STATUS     = 33,
    QIC_SKIP_EXTENDED_REVERSE  = 34,
    QIC_SKIP_EXTENDED_FORWARD  = 35,
    QIC_CALIBRATE_TAPE_LENGTH  = 36,
    QIC_REPORT_FORMAT_SEGMENTS = 37,
    QIC_SET_FORMAT_SEGMENTS    = 38,
    QIC_PHANTOM_SELECT         = 46,
    QIC_PHANTOM_DESELECT       = 47,
    QIC_EXT_SELECT_RATE        = 50,
    QIC_EXT_REPORT_DRIVE_CONFIG = 51,
    QIC_MAX_COMMAND            = 55
};

/* Drive status bits, as returned by QIC_REPORT_DRIVE_STATUS. */
#define QIC_STATUS_READY             0x01
#define QIC_STATUS_ERROR             0x02
#define QIC_STATUS_CARTRIDGE_PRESENT 0x04
#define QIC_STATUS_WRITE_PROTECT     0x08
#define QIC_STATUS_NEW_CARTRIDGE     0x10
#define QIC_STATUS_REFERENCED        0x20
#define QIC_STATUS_AT_BOT            0x40
#define QIC_STATUS_AT_EOT            0x80

/* Drive configuration bits, as returned by QIC_REPORT_DRIVE_CONFIG. */
#define QIC_CONFIG_RATE_SHIFT        3
#define QIC_CONFIG_LONG              0x40
#define QIC_CONFIG_80                0x80

/*
   Rate codes, as they appear in bits 4-3 of the drive configuration and as
   the argument to Select Rate. A QIC-80 drive only runs at the first two.
 */
#define QIC_RATE_250                 0
#define QIC_RATE_2000                1
#define QIC_RATE_500                 2
#define QIC_RATE_1000                3

/*
   Arguments to Select Rate above the rate codes name a tape format instead,
   as (Tape Format * 4) + Increment, where increment 1 is standard quarter
   inch media and 3 is 8 mm wide tape. This drive handles the two QIC-80
   family formats on standard media and nothing else.
 */
#define QIC_FORMAT_QIC40             ((1 << 2) | 1)
#define QIC_FORMAT_QIC80             ((2 << 2) | 1)

/* Tape status bits, as returned by QIC_REPORT_TAPE_STATUS. */
#define QIC_TAPE_QIC80               0x02
#define QIC_TAPE_307FT               0x20

/* Error codes, from the QIC-117 rev. J error code list (3.5). */
#define QIC_ERROR_NONE               0
#define QIC_ERROR_NOT_READY          1
#define QIC_ERROR_NO_CARTRIDGE       2
#define QIC_ERROR_WRITE_PROTECTED    5
#define QIC_ERROR_UNDEFINED_COMMAND  6
#define QIC_ERROR_ILLEGAL_SEEK_TRACK 7
#define QIC_ERROR_ILLEGAL_IN_REPORT  8
#define QIC_ERROR_NOT_REFERENCED     19
#define QIC_ERROR_RATE_SELECTION     31

/* Undefined codes at or above Report Vendor ID are ignored rather than
   faulted, so that a host recalibrating its controller - which produces a
   long pulse train - provokes no reaction from the drive (3.0). */
#define QIC_IGNORE_ABOVE             QIC_REPORT_VENDOR_ID

/* Identity of the emulated drive: "Colorado DJ-10/DJ-20 (new)". */
#define QIC_VENDOR_ID                0x011c4
#define QIC_ROM_VERSION              0x54

/* The head parks at cylinder 0 while idle, so TRACK 0 doubles as the
   result line. Bits are handed back one at a time, framed by a leading
   acknowledge bit and a trailing stop bit. */
#define TAPE_REPORT_IDLE             (-1)

/* A blank cartridge holds this many segments (QIC-80, 307.5 ft, 28 tracks
   of 150 segments), which is what a Jumbo 250 ships with. */
#define TAPE_SEGMENTS_PER_TRACK      150
#define TAPE_TRACKS                  28
#define TAPE_TOTAL_SEGMENTS          (TAPE_SEGMENTS_PER_TRACK * TAPE_TRACKS)

/* Transfer states of the read/write engine. */
enum {
    TAPE_XFER_IDLE = 0,
    TAPE_XFER_READ,
    TAPE_XFER_WRITE,
    TAPE_XFER_COMPARE,
    TAPE_XFER_FORMAT,
    TAPE_XFER_READID
};

typedef struct tape_t {
    int      attached;      /* drive is fitted to the cable */
    int      drive;         /* drive select line it answers to */

    FILE    *fp;
    int      readonly;
    uint32_t image_size;

    /* How the cartridge's format maps segments onto C/H/R addresses. */
    int      segs_per_cyl;
    int      segs_per_head;

    /* Step pulse decoding. */
    int      pulse_count;   /* pulses received since the last command gap */
    int      bit_presented; /* the train so far has been answered as a
                               Report Next Bit, without waiting it out */
    int      params_left;   /* parameters the pending command still wants */
    uint8_t  param_cmd;     /* command those parameters belong to */
    uint8_t  param[3];
    int      params_got;

    /* Result reporting over the TRACK 0 line. */
    uint32_t report_value;
    int      report_len;
    int      report_pos;    /* TAPE_REPORT_IDLE, or 0..report_len inclusive */
    int      ack;           /* current state of the TRACK 0 line */

    /* Drive state. */
    uint8_t  status;
    uint8_t  error;
    uint8_t  error_cmd;
    uint8_t  last_command;
    int      selected;
    int      running;       /* tape is streaming past the head */
    int      reverse;
    int      format_mode;
    int      verify_mode;
    int      track;         /* logical tape track the head sits on */
    int      head_sector;   /* absolute sector passing the head: the segment
                               in the controller's C/H/R address space times
                               32, plus the sector within it */
    uint16_t format_segments;
    uint8_t  rate_code;     /* data rate, as a drive configuration code */
    uint8_t  format_code;   /* tape format the host selected for formatting */

    /* Read/write engine, driven one byte at a time by the transfer clock. */
    int      xfer_state;
    int      xfer_pos;
    int      xfer_len;
    uint32_t xfer_offset;   /* image offset the buffer came from/goes to */
    uint8_t  buffer[FDD_TAPE_SECTOR_SIZE];

    /* Formatting engine. */
    int      format_datac;
    int      format_count;
    uint32_t format_offset;
} tape_t;

static tape_t  tape;
static fdc_t  *tape_fdc = NULL;

/*
   The drive runs its own capstan motor under QIC-117 control and pays no
   attention to the controller's motor line, so the transfer clock is ours
   rather than the floppy drive poll.

   This lives outside tape_t deliberately: detaching the drive wipes the
   rest of the state, and a registered timer must never be memset while it
   might still be linked into the timer list.
 */
static pc_timer_t tape_timer;
static int        tape_timer_added = 0;

/*
   QIC-117 delimits commands by time, not by seek boundaries: a pulse train
   ends when no further STEP pulse arrives for TTIMEOUT, and only then does
   the drive act on the count. This timer measures that gap. It lives
   outside tape_t for the same reason as the transfer clock above.
 */
static pc_timer_t tape_cmd_timer;
static int        tape_cmd_timer_added = 0;

/* QIC-117 rev. J table 1: command time-out, nominal 2.5 ms. */
#define TAPE_TTIMEOUT (2500ULL * TIMER_USEC)

/*
   While the cartridge is moving, sectors stream past the head whether or not
   the host is reading them - and the host tracks its position by watching
   the sector IDs go by with READ ID. This timer keeps that position moving.
 */
static pc_timer_t tape_motion_timer;
static int        tape_motion_timer_added = 0;

int  fdd_tape_enabled = 0;
int  fdd_tape_unit    = 1;
char fdd_tape_fn[MAX_IMAGE_PATH_LEN];

#ifdef ENABLE_FDD_TAPE_LOG
int fdd_tape_do_log = ENABLE_FDD_TAPE_LOG;

static void
fdd_tape_log(const char *fmt, ...)
{
    va_list ap;

    if (fdd_tape_do_log) {
        va_start(ap, fmt);
        pclog_ex(fmt, ap);
        va_end(ap);
    }
}
#else
#    define fdd_tape_log(fmt, ...)
#endif

/* --------------------------------------------------------------------- */
/* Cartridge image access                                                */
/* --------------------------------------------------------------------- */

static int
tape_has_cartridge(void)
{
    return tape.fp != NULL;
}

/* Reads a sector's worth of image, zero-filling past the end of the file.
   A short or missing image simply reads back as blank tape. */
static void
tape_image_read(uint32_t offset, uint8_t *buf, uint32_t len)
{
    uint32_t got = 0;

    memset(buf, 0x00, len);

    if ((tape.fp == NULL) || (offset >= tape.image_size))
        return;

    if (fseek(tape.fp, (long) offset, SEEK_SET) != 0)
        return;

    if ((offset + len) > tape.image_size)
        len = tape.image_size - offset;

    got = (uint32_t) fread(buf, 1, len, tape.fp);
    if (got < len)
        memset(buf + got, 0x00, len - got);
}

static void
tape_image_write(uint32_t offset, const uint8_t *buf, uint32_t len)
{
    if ((tape.fp == NULL) || tape.readonly)
        return;

    /* Writing past the end grows the image, so a fresh cartridge can start
       out as an empty file. */
    if (fseek(tape.fp, (long) offset, SEEK_SET) != 0)
        return;

    if (fwrite(buf, 1, len, tape.fp) != len)
        return;

    fflush(tape.fp);

    if ((offset + len) > tape.image_size)
        tape.image_size = offset + len;
}

/* --------------------------------------------------------------------- */
/* QIC-117 command set                                                   */
/* --------------------------------------------------------------------- */

/* Forward references: the clocks are driven from command handling. */
static void tape_start_clock(void);
static void tape_stop_clock(void);
static void tape_start_motion(void);
static void tape_stop_motion_clock(void);

/* The segment currently under the head. */
static int
tape_head_segment(void)
{
    return tape.head_sector / FDD_TAPE_SECTORS_PER_SEG;
}

/* Segments per tape track. The real figure lives in the cartridge's header
   segment, which only the host has read, so this is the QIC-80 default
   unless the host has told us otherwise. */
static int
tape_segments_per_track(void)
{
    return tape.format_segments ? tape.format_segments : TAPE_SEGMENTS_PER_TRACK;
}

/* The first segment of the track the head currently sits on. */
static int
tape_track_start(void)
{
    return tape.track * tape_segments_per_track();
}

static void
tape_update_status(void)
{
    tape.status &= ~(QIC_STATUS_CARTRIDGE_PRESENT | QIC_STATUS_WRITE_PROTECT |
                     QIC_STATUS_REFERENCED | QIC_STATUS_AT_BOT | QIC_STATUS_AT_EOT);

    if (!tape_has_cartridge())
        return;

    tape.status |= QIC_STATUS_CARTRIDGE_PRESENT;

    if (tape.readonly)
        tape.status |= QIC_STATUS_WRITE_PROTECT;

    /* A cartridge with a reference burst written to it is "referenced",
       which is what the host takes as "formatted". An empty image is
       reported as unformatted so the host offers to format it. */
    if (tape.image_size > 0)
        tape.status |= QIC_STATUS_REFERENCED;

    /* Beginning and end of tape are relative to the current track. */
    const int offset = tape_head_segment() - tape_track_start();

    if (offset <= 0)
        tape.status |= QIC_STATUS_AT_BOT;
    else if (offset >= (tape_segments_per_track() - 1))
        tape.status |= QIC_STATUS_AT_EOT;
}

static void
tape_set_error(uint8_t error, uint8_t command)
{
    fdd_tape_log("Tape: error %i on command %i\n", error, command);

    tape.error     = error;
    tape.error_cmd = command;
    tape.status |= QIC_STATUS_ERROR;
}

/* Loads the shift register the host will clock out over the TRACK 0 line.
   The drive first raises TRACK 0 as an acknowledge, then hands over one
   bit per "report next bit", least significant first, and finally raises
   the line once more as a stop bit. */
static void
tape_start_report(uint32_t value, int length)
{
    fdd_tape_log("Tape: reporting %i bits: %0*x\n", length, length / 4, value);

    tape.report_value = value;
    tape.report_len   = length;
    tape.report_pos   = 0;
    tape.ack          = 1;
}

static void
tape_report_next_bit(void)
{
    if (tape.report_pos == TAPE_REPORT_IDLE) {
        /* Not reporting - the line returns to its idle state. */
        tape.ack = 0;
        return;
    }

    if (tape.report_pos < tape.report_len) {
        tape.ack = (tape.report_value >> tape.report_pos) & 1;
        tape.report_pos++;
    } else {
        /*
           The final bit of a report is always true, and presenting it
           exits the report subcontext (1.4.2). TRACK 0 stays asserted
           until the drive receives another command - which may be a
           Report Next Bit, ignored outside the subcontext but serving to
           clear the line.
         */
        tape.ack        = 1;
        tape.report_pos = TAPE_REPORT_IDLE;
    }
}

static void
tape_stop_motion(void)
{
    tape.running = 0;
    tape_stop_motion_clock();
    tape.status |= QIC_STATUS_READY;
}

/* Moves the head to an absolute segment, clamped to the cartridge. */
static void
tape_seek_to_segment(int segment)
{
    const int last = (tape.segs_per_head * 8) - 1;

    if (segment < 0)
        segment = 0;
    if (segment > last)
        segment = last;

    tape.head_sector = segment * FDD_TAPE_SECTORS_PER_SEG;
}

/* Handles a command that takes parameters once the last one has arrived. */
static void
tape_finish_parameters(void)
{
    uint32_t count;

    switch (tape.param_cmd) {
        case QIC_SEEK_HEAD_TO_TRACK:
            tape.track = tape.param[0];
            /* Odd tracks are written back to front. */
            tape.reverse = tape.track & 1;
            tape_seek_to_segment(tape_track_start());
            break;

        case QIC_SKIP_FORWARD:
        case QIC_SKIP_EXTENDED_FORWARD:
        case QIC_SKIP_REVERSE:
        case QIC_SKIP_EXTENDED_REVERSE:
            /* The count arrives as 2 or 3 nibbles, low nibble first, and
               is biased by one - zero means "skip one gap". */
            count = 0;
            for (int i = tape.params_got - 1; i >= 0; i--)
                count = (count << 4) | (tape.param[i] & 0x0f);
            count++;
            if ((tape.param_cmd == QIC_SKIP_REVERSE) ||
                (tape.param_cmd == QIC_SKIP_EXTENDED_REVERSE))
                tape_seek_to_segment(tape_head_segment() - (int) count);
            else
                tape_seek_to_segment(tape_head_segment() + (int) count);
            break;

        case QIC_SET_FORMAT_SEGMENTS:
            tape.format_segments = tape.param[0];
            break;

        case QIC_SELECT_RATE:
            /*
               The one argument selects either a data rate or a tape format.
               Whatever the drive cannot do has to be refused rather than
               quietly ignored: a host that asks for something and gets no
               complaint will read the configuration back, find nothing
               changed, and conclude the drive cannot reach the capacity it
               wants.
             */
            switch (tape.param[0]) {
                case QIC_RATE_250:
                case QIC_RATE_500:
                    tape.rate_code = tape.param[0];
                    break;

                case QIC_FORMAT_QIC40:
                case QIC_FORMAT_QIC80:
                    /* Only consulted when the host goes on to format. */
                    tape.format_code = tape.param[0];
                    break;

                default:
                    /* 1 and 2 Mbps, the QIC-3010 and QIC-3020 formats, and
                       8 mm wide media are all beyond a QIC-80 drive. */
                    tape_set_error(QIC_ERROR_RATE_SELECTION, tape.param_cmd);
                    break;
            }
            break;

        case QIC_EXT_SELECT_RATE:
            /* The extended form only names rates beyond this drive's reach. */
            tape_set_error(QIC_ERROR_RATE_SELECTION, tape.param_cmd);
            break;

        default:
            /* Drive selection and timeouts have no effect on an emulated
               drive - the argument is simply accepted. */
            break;
    }

    tape_stop_motion();
}

/* Whether the code is part of the QIC-117 rev. J command set at all. */
static int
tape_command_defined(uint8_t command)
{
    switch (command) {
        case QIC_RESET ... QIC_STOP_TAPE:
        case QIC_MICRO_STEP_HEAD_UP ... QIC_ENTER_PRIMARY_MODE:
        case QIC_REPORT_VENDOR_ID ... QIC_SET_FORMAT_SEGMENTS:
        case QIC_PHANTOM_SELECT:
        case QIC_PHANTOM_DESELECT:
        case QIC_EXT_SELECT_RATE:
        case QIC_EXT_REPORT_DRIVE_CONFIG:
            return 1;

        default:
            return 0;
    }
}

/* How many parameters a command expects, if any. */
static int
tape_command_params(uint8_t command)
{
    switch (command) {
        case QIC_SKIP_EXTENDED_FORWARD:
        case QIC_SKIP_EXTENDED_REVERSE:
            return 3;

        case QIC_SKIP_FORWARD:
        case QIC_SKIP_REVERSE:
            return 2;

        case QIC_ALTERNATE_TIMEOUT:
        case QIC_SEEK_HEAD_TO_TRACK:
        case QIC_SOFT_SELECT:
        case QIC_SELECT_RATE:
        case QIC_SET_FORMAT_SEGMENTS:
        case QIC_PHANTOM_SELECT:
        case QIC_EXT_SELECT_RATE:
            return 1;

        default:
            return 0;
    }
}

#ifdef ENABLE_FDD_TAPE_LOG
static const char *
tape_command_name(uint8_t command)
{
    static const char *const names[] = {
        "no command",         "soft reset",          "report next bit",
        "pause",              "micro step pause",    "alternate timeout",
        "report drive status","report error code",   "report drive config",
        "report rom version", "logical forward",     "physical reverse",
        "physical forward",   "seek head to track",  "seek load point",
        "enter format mode",  "write reference burst","enter verify mode",
        "stop tape",          "reserved (19)",       "reserved (20)",
        "micro step head up", "micro step head down","soft select",
        "soft deselect",      "skip reverse",        "skip forward",
        "select rate",        "enter diag mode 1",   "enter diag mode 2",
        "enter primary mode", "vendor unique (31)",  "report vendor id",
        "report tape status", "skip extended reverse","skip extended forward",
        "calibrate tape length","report format segments","set format segments"
    };

    if (command < (sizeof(names) / sizeof(names[0])))
        return names[command];

    switch (command) {
        case QIC_PHANTOM_SELECT:
            return "phantom select";
        case QIC_PHANTOM_DESELECT:
            return "phantom deselect";
        case QIC_EXT_SELECT_RATE:
            return "extended select rate";
        case QIC_EXT_REPORT_DRIVE_CONFIG:
            return "ext report drive config";
        default:
            return "unknown";
    }
}
#endif

static void
tape_command(uint8_t command)
{
    fdd_tape_log("Tape: QIC-117 command %i (%s)\n", command, tape_command_name(command));

    /* "Report next bit" is the one command that never disturbs the drive
       state, so it is handled before anything else. */
    if (command == QIC_REPORT_NEXT_BIT) {
        tape_report_next_bit();
        return;
    }

    /*
       An unsupported code above Report Vendor ID is ignored outright: it
       does not even disturb a report in progress, whose current bit is
       simply repeated to the next Report Next Bit (3.0).
     */
    if (!tape_command_defined(command) && (command >= QIC_IGNORE_ABOVE)) {
        fdd_tape_log("Tape: ignoring %i pulse(s)\n", command);
        return;
    }

    /*
       QIC-117 rev. J 3.4: during a report, any command other than Report
       Next Bit terminates the report subcontext with an "illegal command
       during report subcontext" error, and the final report bit - normally
       a one - comes back as a zero to tell the host the status it just
       read is invalid. Reset is the one exception.
     */
    if ((tape.report_pos != TAPE_REPORT_IDLE) && (command != QIC_RESET)) {
        fdd_tape_log("Tape: %s during report subcontext\n", tape_command_name(command));
        tape_set_error(QIC_ERROR_ILLEGAL_IN_REPORT, command);
    }

    tape.report_pos = TAPE_REPORT_IDLE;
    tape.ack        = 0;

    tape.last_command = command;

    switch (command) {
        case QIC_RESET:
            tape.selected    = 0;
            tape.running     = 0;
            tape.reverse     = 0;
            tape.format_mode = 0;
            tape.verify_mode = 0;
            tape.track       = 0;
            tape.head_sector = 0;
            tape.error       = QIC_ERROR_NONE;
            tape.error_cmd   = QIC_NO_COMMAND;
            tape.status      = QIC_STATUS_READY;
            tape.xfer_state  = TAPE_XFER_IDLE;
            break;

        case QIC_REPORT_DRIVE_STATUS:
            tape_update_status();
            tape_start_report(tape.status, 8);
            return;

        case QIC_REPORT_ERROR_CODE:
            /* The error code comes back with the offending command in the
               high byte. Reading it clears the error condition. */
            tape_start_report(tape.error | (tape.error_cmd << 8), 16);
            tape.error     = QIC_ERROR_NONE;
            tape.error_cmd = QIC_NO_COMMAND;
            tape.status &= ~(QIC_STATUS_ERROR | QIC_STATUS_NEW_CARTRIDGE);
            return;

        case QIC_REPORT_DRIVE_CONFIG:
            tape_start_report((tape.rate_code << QIC_CONFIG_RATE_SHIFT) |
                              QIC_CONFIG_LONG | QIC_CONFIG_80, 8);
            return;

        case QIC_REPORT_ROM_VERSION:
            tape_start_report(QIC_ROM_VERSION, 8);
            return;

        case QIC_REPORT_VENDOR_ID:
            tape_start_report(QIC_VENDOR_ID & 0xffff, 16);
            return;

        case QIC_REPORT_TAPE_STATUS:
            if (!tape_has_cartridge()) {
                tape_set_error(QIC_ERROR_NO_CARTRIDGE, command);
                break;
            }
            tape_start_report(QIC_TAPE_QIC80 | QIC_TAPE_307FT, 8);
            return;

        case QIC_REPORT_FORMAT_SEGMENTS:
            tape_start_report(tape.format_segments ? tape.format_segments
                                                   : TAPE_SEGMENTS_PER_TRACK, 16);
            return;

        case QIC_EXT_REPORT_DRIVE_CONFIG:
            tape_start_report(0x00, 8);
            return;

        case QIC_LOGICAL_FORWARD:
        case QIC_PHYSICAL_FORWARD:
            if (!tape_has_cartridge()) {
                tape_set_error(QIC_ERROR_NO_CARTRIDGE, command);
                break;
            }
            /* The tape is now streaming; the command itself is done, so the
               drive stays ready for whatever comes next. Any transfer the
               host armed beforehand starts flowing now. */
            tape.running = 1;
            tape.reverse = 0;
            tape_start_motion();
            if (tape.xfer_state != TAPE_XFER_IDLE)
                tape_start_clock();
            break;

        case QIC_PHYSICAL_REVERSE:
            if (!tape_has_cartridge()) {
                tape_set_error(QIC_ERROR_NO_CARTRIDGE, command);
                break;
            }
            tape_seek_to_segment(tape_track_start());
            tape_stop_motion();
            break;

        case QIC_SEEK_LOAD_POINT:
            if (!tape_has_cartridge()) {
                tape_set_error(QIC_ERROR_NO_CARTRIDGE, command);
                break;
            }
            tape.track = 0;
            tape_seek_to_segment(0);
            tape_stop_motion();
            break;

        case QIC_STOP_TAPE:
        case QIC_PAUSE:
        case QIC_MICRO_STEP_PAUSE:
            tape.xfer_state = TAPE_XFER_IDLE;
            tape_stop_motion();
            break;

        case QIC_ENTER_FORMAT_MODE:
            if (tape.readonly) {
                tape_set_error(QIC_ERROR_WRITE_PROTECTED, command);
                break;
            }
            tape.format_mode = 1;
            tape.verify_mode = 0;
            break;

        case QIC_ENTER_VERIFY_MODE:
            tape.verify_mode = 1;
            tape.format_mode = 0;
            break;

        case QIC_ENTER_PRIMARY_MODE:
        case QIC_ENTER_DIAGNOSTIC_1:
        case QIC_ENTER_DIAGNOSTIC_2:
            tape.format_mode = 0;
            tape.verify_mode = 0;
            break;

        case QIC_WRITE_REFERENCE_BURST:
            /* Laying down the reference burst is what makes a blank
               cartridge "referenced". */
            if (tape.readonly) {
                tape_set_error(QIC_ERROR_WRITE_PROTECTED, command);
                break;
            }
            if (tape.image_size == 0)
                tape.image_size = 1;
            break;

        case QIC_CALIBRATE_TAPE_LENGTH:
        case QIC_MICRO_STEP_HEAD_UP:
        case QIC_MICRO_STEP_HEAD_DOWN:
            break;

        case QIC_SOFT_DESELECT:
        case QIC_PHANTOM_DESELECT:
            tape.selected = 0;
            break;

        default:
            /* Defined commands with no immediate effect: their work
               happens when their arguments arrive, or they only change a
               mode the emulation does not model. */
            if (tape_command_defined(command))
                break;

            /*
               QIC-117 rev. J 3.0: only undefined codes below Report
               Vendor ID raise an error. Longer pulse trains are ignored,
               which is what lets a host recalibrate its controller
               without provoking the drive.
             */
            if (command < QIC_IGNORE_ABOVE)
                tape_set_error(QIC_ERROR_UNDEFINED_COMMAND, command);
            break;
    }

    /* Commands taking parameters stay busy until the last one lands. */
    tape.params_left = tape_command_params(command);
    tape.params_got  = 0;
    tape.param_cmd   = command;

    if (tape.params_left == 0)
        tape.status |= QIC_STATUS_READY;
    else
        tape.status &= ~QIC_STATUS_READY;

    /* Selection is only complete once the unit address argument arrives. */
    if ((command == QIC_SOFT_SELECT) || (command == QIC_PHANTOM_SELECT))
        tape.selected = 1;
}

/* Decodes one burst of step pulses into a command or a parameter. */
static void
tape_step_pulses(int steps)
{
    if (steps <= 0)
        return;

    if (tape.params_left > 0) {
        /* Parameters arrive biased by two, so that a parameter of zero
           can't be mistaken for the "no command" pulse count. */
        if (steps < 2) {
            tape.params_left = 0;
            tape_set_error(QIC_ERROR_UNDEFINED_COMMAND, tape.param_cmd);
            tape.status |= QIC_STATUS_READY;
            return;
        }

        if (tape.params_got < (int) (sizeof(tape.param) / sizeof(tape.param[0])))
            tape.param[tape.params_got] = (uint8_t) (steps - 2);
        tape.params_got++;
        tape.params_left--;

        fdd_tape_log("Tape: QIC-117 parameter %i (%i left)\n", steps - 2, tape.params_left);

        if (tape.params_left == 0)
            tape_finish_parameters();

        return;
    }

    tape_command((uint8_t) steps);
}

/* --------------------------------------------------------------------- */
/* Floppy drive interface                                                */
/* --------------------------------------------------------------------- */

/* Turns a floppy C/H/R address into an offset into the cartridge image.
   Returns 0 if the address doesn't name a sector that can exist on tape. */
static int
tape_sector_offset(int track, int side, int sector, uint32_t *offset)
{
    int segment;
    int sector_in_segment;

    if ((sector < 1) || (sector > (tape.segs_per_cyl * FDD_TAPE_SECTORS_PER_SEG)))
        return 0;

    /*
       The head field is not a physical head here, just the high digits of
       the segment number, so it ranges well beyond the two a floppy has:
       a full QIC-80 cartridge holds over 4000 segments, which is head 4 at
       1020 segments per head.
     */
    if ((track < 0) || (track > FDD_TAPE_MAX_TRACK) || (side < 0) || (side > 0xff))
        return 0;

    segment = (side * tape.segs_per_head) + (track * tape.segs_per_cyl) +
              ((sector - 1) / FDD_TAPE_SECTORS_PER_SEG);
    sector_in_segment = (sector - 1) % FDD_TAPE_SECTORS_PER_SEG;

    const uint64_t off64 = ((uint64_t) segment * FDD_TAPE_SEGMENT_SIZE) +
                           ((uint64_t) sector_in_segment * FDD_TAPE_SECTOR_SIZE);
    if (off64 > UINT32_MAX)
        return 0;
    *offset = (uint32_t) off64;

    return 1;
}

/* Interval between STEP pulses, from the controller's SPECIFY and data
   rate. QIC-117 hosts program this to TSTEP, nominally 2 ms. */
static int
tape_step_interval_us(void)
{
    int srt;
    int bit_rate;

    if (tape_fdc == NULL)
        return 2000;

    srt      = tape_fdc->specify[0] >> 4;
    bit_rate = fdc_get_bit_rate(tape_fdc);
    if (bit_rate <= 0)
        bit_rate = 250;

    return ((16 - srt) * 1000 * 500) / bit_rate;
}

/*
   Adds a burst of step pulses to the train in progress and reports how
   long the controller will take to clock them out, so the seek completes
   at a realistic time rather than instantly.
 */
int
fdd_tape_step(int drive, int steps)
{
    if (drive != tape.drive)
        return 0;

    tape.pulse_count += steps;

    /*
       Report Next Bit is the one command exempt from the command
       time-out: TRACK 0 must be valid within TBIT of the second step
       pulse (1.4.2), and hosts sample it as soon as the seek finishes,
       long before a TTIMEOUT gap could elapse. So answer it as the pulses
       land. A longer train is a different command, and takes the report
       subcontext down with it when it is eventually decoded.
     */
    if ((tape.pulse_count == QIC_REPORT_NEXT_BIT) && (tape.params_left == 0)) {
        tape_report_next_bit();
        tape.bit_presented = 1;
    }

    return steps * tape_step_interval_us();
}

/*
   Called once the burst has been delivered, immediately before the seek
   complete interrupt goes back to the host. The command is not decoded
   yet - the host may still be part way through a longer pulse train, and
   only a TTIMEOUT gap marks the end of it.
 */
static void
tape_seek(int drive, UNUSED(int track))
{
    if (drive != tape.drive)
        return;

    if (tape_cmd_timer_added)
        timer_set_delay_u64(&tape_cmd_timer, TAPE_TTIMEOUT);
}

/* Fires TTIMEOUT after the last step pulse: the pulse train is complete. */
static void
tape_command_timeout(UNUSED(void *priv))
{
    int steps     = tape.pulse_count;
    int presented = tape.bit_presented;

    tape.pulse_count   = 0;
    tape.bit_presented = 0;

    if (steps <= 0)
        return;

    /* A bare Report Next Bit was already answered as its pulses arrived. */
    if ((steps == QIC_REPORT_NEXT_BIT) && presented)
        return;

    fdd_tape_log("Tape: pulse train complete, %i pulse(s)\n", steps);

    tape_step_pulses(steps);
}

/* QIC-80 streams at 500 kbps, which is 16 microseconds per byte. */
#define TAPE_BYTE_PERIOD (16ULL * TIMER_USEC)

static void
tape_start_clock(void)
{
    if (tape_timer_added)
        timer_set_delay_u64(&tape_timer, TAPE_BYTE_PERIOD);
}

static void
tape_stop_clock(void)
{
    if (tape_timer_added)
        timer_disable(&tape_timer);
}

/* One sector of tape passes the head every this many microseconds. */
#define TAPE_SECTOR_PERIOD (FDD_TAPE_SECTOR_SIZE * 16ULL * TIMER_USEC)

static void
tape_start_motion(void)
{
    if (tape_motion_timer_added)
        timer_set_delay_u64(&tape_motion_timer, TAPE_SECTOR_PERIOD);
}

static void
tape_stop_motion_clock(void)
{
    if (tape_motion_timer_added)
        timer_disable(&tape_motion_timer);
}

/* Advances the head past one more sector of moving tape. */
static void
tape_motion_tick(UNUSED(void *priv))
{
    timer_advance_u64(&tape_motion_timer, TAPE_SECTOR_PERIOD);

    if (!tape.running) {
        tape_stop_motion_clock();
        return;
    }

    const int min_sector = tape_track_start() * FDD_TAPE_SECTORS_PER_SEG;
    const int max_sector = (tape_track_start() + tape_segments_per_track()) * FDD_TAPE_SECTORS_PER_SEG - 1;

    if (tape.reverse) {
        if (tape.head_sector > min_sector)
            tape.head_sector--;
    } else {
        if (tape.head_sector < max_sector)
            tape.head_sector++;
    }
}

static void
tape_setup_transfer(int state, int sector, int track, int side, int sector_size)
{
    uint32_t offset = 0;

    tape.xfer_state = TAPE_XFER_IDLE;
    tape_stop_clock();

    if (tape_fdc == NULL)
        return;

    fdd_tape_log("Tape: %s c=%i h=%i r=%i n=%i (running=%i)\n",
                 (state == TAPE_XFER_WRITE) ? "write" :
                 ((state == TAPE_XFER_COMPARE) ? "compare" : "read"),
                 track, side, sector, sector_size, tape.running);

    if (!tape_has_cartridge()) {
        fdd_tape_log("Tape: ... no cartridge\n");
        fdc_nosector(tape_fdc);
        return;
    }

    /* The tape format only ever uses 1024-byte sectors. */
    if (sector_size != 3) {
        fdd_tape_log("Tape: ... bad sector size %i\n", sector_size);
        fdc_nosector(tape_fdc);
        return;
    }

    if (!tape_sector_offset(track, side, sector, &offset)) {
        fdd_tape_log("Tape: ... address is not on tape\n");
        fdc_nosector(tape_fdc);
        return;
    }

    if ((state == TAPE_XFER_WRITE) && tape.readonly) {
        fdd_tape_log("Tape: ... cartridge is write protected\n");
        fdc_writeprotect(tape_fdc);
        return;
    }

    tape.xfer_offset = offset;
    tape.xfer_pos    = 0;
    tape.xfer_len    = FDD_TAPE_SECTOR_SIZE;
    tape.xfer_state  = state;

    if (state != TAPE_XFER_WRITE)
        tape_image_read(offset, tape.buffer, FDD_TAPE_SECTOR_SIZE);
    else
        memset(tape.buffer, 0x00, FDD_TAPE_SECTOR_SIZE);

    /* The segment under the head advances as sectors stream past. */
    tape.head_sector = (int) (offset / FDD_TAPE_SECTOR_SIZE);

    /*
       The host arms the controller before it starts the tape, exactly as
       it would for a streaming device: nothing reaches the head until the
       cartridge is actually moving. If the tape is still stopped, the
       transfer waits here and a motion command sets it going.
     */
    if (tape.running)
        tape_start_clock();
    else
        fdd_tape_log("Tape: ... armed, waiting for tape motion\n");
}

static void
tape_readsector(int drive, int sector, int track, int side, UNUSED(int density), int sector_size)
{
    if (drive != tape.drive)
        return;

    /* Read A Track isn't meaningful on tape. */
    if (sector < 0) {
        fdc_nosector(tape_fdc);
        return;
    }

    tape_setup_transfer(TAPE_XFER_READ, sector, track, side, sector_size);
}

static void
tape_writesector(int drive, int sector, int track, int side, UNUSED(int density), int sector_size)
{
    if (drive != tape.drive)
        return;

    tape_setup_transfer(TAPE_XFER_WRITE, sector, track, side, sector_size);
}

static void
tape_comparesector(int drive, int sector, int track, int side, UNUSED(int density), int sector_size)
{
    if (drive != tape.drive)
        return;

    tape_setup_transfer(TAPE_XFER_COMPARE, sector, track, side, sector_size);
}

/* Hands back the ID of the sector currently under the head. */
static void
tape_finish_readaddress(void)
{
    int segment;
    int cylinder;
    int sector;

    if (!tape.running) {
        /* A stopped tape has no sector passing the head. The host reads
           this back as sector zero and goes looking for the drive status. */
        fdc_sectorid(tape_fdc, 0, 0, 0, 3, 0, 0);
        return;
    }

    segment  = tape_head_segment();
    cylinder = (segment % tape.segs_per_head) / tape.segs_per_cyl;
    /* The ID of the sector actually under the head, not just the start of
       the segment - this is what tells a streaming host where it is. */
    sector   = ((segment % tape.segs_per_cyl) * FDD_TAPE_SECTORS_PER_SEG) +
               (tape.head_sector % FDD_TAPE_SECTORS_PER_SEG) + 1;

    fdd_tape_log("Tape: read ID -> segment %i (c=%i h=%i r=%i)\n", segment, cylinder,
                 segment / tape.segs_per_head, sector);

    fdc_sectorid(tape_fdc, (uint8_t) cylinder, (uint8_t) (segment / tape.segs_per_head),
                 (uint8_t) sector, 3, 0, 0);
}

/* The host uses READ ID to find out where on the tape it currently is. */
static void
tape_readaddress(int drive, UNUSED(int side), UNUSED(int density))
{
    if ((drive != tape.drive) || (tape_fdc == NULL))
        return;

    if (!tape_has_cartridge()) {
        fdc_nosector(tape_fdc);
        return;
    }

    /*
       The result has to come back from the transfer clock rather than from
       here: the controller finishes setting up its own state after this
       call returns, and would overwrite the result phase we just set up.
     */
    tape.xfer_state = TAPE_XFER_READID;
    tape_start_clock();
}

static void
tape_format(int drive, UNUSED(int side), UNUSED(int density), UNUSED(uint8_t fill))
{
    if ((drive != tape.drive) || (tape_fdc == NULL))
        return;

    fdd_tape_log("Tape: format track, %i sectors\n", fdc_get_format_sectors(tape_fdc));

    if (!tape_has_cartridge() || tape.readonly) {
        fdc_writeprotect(tape_fdc);
        return;
    }

    tape.xfer_state   = TAPE_XFER_FORMAT;
    tape.format_datac = 0;
    tape.format_count = 0;

    tape_start_clock();
}

static void
tape_stop(int drive)
{
    if (drive != tape.drive)
        return;

    tape.xfer_state = TAPE_XFER_IDLE;
    tape_stop_clock();
}

static int
tape_hole(UNUSED(int drive))
{
    /* QIC-80 streams at 500 kbps, same as a high density floppy. */
    return 1;
}

static uint64_t
tape_byteperiod(UNUSED(int drive))
{
    return TAPE_BYTE_PERIOD;
}

/* Moves one byte of the current transfer, once per byte period. */
static void
tape_clock(UNUSED(void *priv))
{
    int data;

    timer_advance_u64(&tape_timer, TAPE_BYTE_PERIOD);

    if (tape_fdc == NULL) {
        tape_stop_clock();
        return;
    }

    switch (tape.xfer_state) {
        case TAPE_XFER_READID:
            tape.xfer_state = TAPE_XFER_IDLE;
            tape_stop_clock();
            tape_finish_readaddress();
            break;

        case TAPE_XFER_READ:
            if (!fdc_is_verify(tape_fdc)) {
                /*
                   A -1 here means the host's DMA transfer has hit its
                   terminal count, which is how a multi-sector read
                   normally ends - the controller raises TC and finishes
                   the command itself. Treating it as an overrun would
                   turn every successful transfer into an error.
                 */
                (void) fdc_data(tape_fdc, tape.buffer[tape.xfer_pos],
                                tape.xfer_pos == (tape.xfer_len - 1));
            }

            tape.xfer_pos++;
            if (tape.xfer_pos >= tape.xfer_len) {
                fdd_tape_log("Tape: ... read %i bytes from offset %u, starting %02x %02x %02x %02x\n",
                             tape.xfer_len, tape.xfer_offset,
                             tape.buffer[0], tape.buffer[1], tape.buffer[2], tape.buffer[3]);
                tape.xfer_state = TAPE_XFER_IDLE;
                tape_stop_clock();
                fdc_sector_finishread(tape_fdc);
            }
            break;

        case TAPE_XFER_WRITE:
            /* Terminal count can be indicated via DMA_OVER; when it happens, treat missing bytes as 0. */
            data = fdc_getdata(tape_fdc, tape.xfer_pos == (tape.xfer_len - 1));
            if ((data & DMA_OVER) || (data == -1))
                 data = 0;

            tape.buffer[tape.xfer_pos] = (uint8_t) (data & 0xff);
            tape.xfer_pos++;
            if (tape.xfer_pos >= tape.xfer_len) {
                fdd_tape_log("Tape: ... wrote %i bytes to offset %u, starting %02x %02x %02x %02x\n",
                             tape.xfer_len, tape.xfer_offset,
                             tape.buffer[0], tape.buffer[1], tape.buffer[2], tape.buffer[3]);
                tape.xfer_state = TAPE_XFER_IDLE;
                tape_stop_clock();
                tape_image_write(tape.xfer_offset, tape.buffer, tape.xfer_len);
                fdc_sector_finishread(tape_fdc);
            }
            break;

        case TAPE_XFER_COMPARE: {
            static int satisfying_bytes = 0;
            if (tape.xfer_pos == 0)
                satisfying_bytes = 0;

            data = fdc_getdata(tape_fdc, tape.xfer_pos == (tape.xfer_len - 1));
            if ((data & DMA_OVER) || (data == -1))
                data = 0;

            const uint8_t received_byte = (uint8_t) (data & 0xff);
            const uint8_t tape_byte     = tape.buffer[tape.xfer_pos];

            switch (fdc_get_compare_condition(tape_fdc)) {
                case 0: /* SCAN EQUAL */
                    if ((received_byte == tape_byte) || (received_byte == 0xFF))
                        satisfying_bytes++;
                    break;
                case 1: /* SCAN LOW OR EQUAL */
                    if ((received_byte <= tape_byte) || (received_byte == 0xFF))
                        satisfying_bytes++;
                    break;
                case 2: /* SCAN HIGH OR EQUAL */
                    if ((received_byte >= tape_byte) || (received_byte == 0xFF))
                        satisfying_bytes++;
                    break;
                default:
                    break;
            }

            tape.xfer_pos++;
            if (tape.xfer_pos >= tape.xfer_len) {
                tape.xfer_state = TAPE_XFER_IDLE;
                tape_stop_clock();
                fdc_sector_finishcompare(tape_fdc, satisfying_bytes >= tape.xfer_len);
            }
            break;
        }

        case TAPE_XFER_FORMAT:
            /* The host feeds four ID bytes per sector through DMA; the
               sector itself is laid down blank. */
            if (tape.format_datac <= 3) {
                data = fdc_getdata(tape_fdc, 0);
                if (data == -1)
                    data = 0;
                tape_fdc->format_sector_id.byte_array[tape.format_datac] = data & 0xff;

                if (tape.format_datac == 3) {
                    fdc_stop_id_request(tape_fdc);
                    if (!tape_sector_offset(tape_fdc->format_sector_id.id.c,
                                            tape_fdc->format_sector_id.id.h,
                                            tape_fdc->format_sector_id.id.r,
                                            &tape.format_offset))
                        tape.format_offset = UINT32_MAX;
                }
            } else if (tape.format_datac == 4) {
                if (tape.format_offset != UINT32_MAX) {
                    memset(tape.buffer, 0x00, FDD_TAPE_SECTOR_SIZE);
                    tape_image_write(tape.format_offset, tape.buffer, FDD_TAPE_SECTOR_SIZE);
                }
                tape.format_count++;
            }

            tape.format_datac++;

            if (tape.format_datac == 6) {
                tape.format_datac = 0;

                if (tape.format_count < fdc_get_format_sectors(tape_fdc))
                    fdc_request_next_sector_id(tape_fdc);
                else {
                    tape.xfer_state = TAPE_XFER_IDLE;
                    tape_stop_clock();
                    fdc_sector_finishread(tape_fdc);
                }
            }
            break;

        default:
            tape_stop_clock();
            break;
    }
}

/* --------------------------------------------------------------------- */
/* Public interface                                                      */
/* --------------------------------------------------------------------- */

int
fdd_tape_present(int drive)
{
    return tape.attached && (drive == tape.drive);
}

int
fdd_tape_track0(int drive)
{
    if (!fdd_tape_present(drive))
        return 0;

    /* TRACK 0 carries the result of the last "report" command instead of
       the head position. */
    return tape.ack;
}

int
fdd_tape_get_flags(int drive)
{
    if (!fdd_tape_present(drive))
        return 0;

    /* Enough of a drive for the controller to talk to it: 300 rpm, double
       sided, and both double and high density media. */
    return 0x01 | 0x08 | 0x10 | 0x20;
}

void
fdd_tape_set_fdc(void *fdc)
{
    tape_fdc = (fdc_t *) fdc;
}

/*
   Works out how the cartridge maps segments onto C/H/R addresses.

   A real drive knows nothing about the format - the host writes whatever
   sector IDs it likes during formatting, and the drive just streams tape.
   This emulation serves data straight from the address the host asks for,
   though, so it has to agree with the host on the mapping. The cartridge
   states it in its own header segment: sectors per cylinder comes from the
   maximum sector number, and cylinders per head from the maximum track.

   QIC-80 cartridges are typically 150 cylinders per head rather than the
   255 the defaults assume, so getting this wrong corrupts every read past
   the first head boundary.
 */
static void
tape_read_geometry(void)
{
    uint8_t hdr[FDD_TAPE_SECTOR_SIZE];

    tape.segs_per_cyl  = FDD_TAPE_SEGS_PER_CYL;
    tape.segs_per_head = FDD_TAPE_SEGS_PER_HEAD;

    if (tape.fp == NULL)
        return;

    tape_image_read(0, hdr, sizeof(hdr));

    /* Header segment signature, little endian 0xaa55aa55. */
    if ((hdr[0] != 0x55) || (hdr[1] != 0xaa) || (hdr[2] != 0x55) || (hdr[3] != 0xaa))
        return;

    const int max_track  = hdr[28];
    const int max_sector = hdr[29];

    if ((max_sector < FDD_TAPE_SECTORS_PER_SEG) || (max_track < 1))
        return;

    tape.segs_per_cyl  = max_sector / FDD_TAPE_SECTORS_PER_SEG;
    tape.segs_per_head = (max_track + 1) * tape.segs_per_cyl;

    fdd_tape_log("Tape: geometry from header - %i segments/cylinder, "
                 "%i segments/head\n", tape.segs_per_cyl, tape.segs_per_head);
}

void
fdd_tape_eject(void)
{
    if (tape.fp != NULL) {
        fclose(tape.fp);
        tape.fp = NULL;
    }

    tape.image_size = 0;
    tape.readonly   = 0;
    tape.xfer_state = TAPE_XFER_IDLE;

    if (tape.attached)
        writeprot[tape.drive] = 1;
}

void
fdd_tape_load(const char *fn)
{
    FILE *fp;

    fdd_tape_eject();

    if ((fn == NULL) || (fn[0] == 0x00))
        return;

    tape.readonly = 0;

    /* The configuration marks write-protected images with a wp:// prefix. */
    if (strstr(fn, "wp://") == fn) {
        fn += 5;
        tape.readonly = 1;
    }

    if (tape.readonly)
        fp = NULL;
    else
        fp = plat_fopen((char *) fn, "rb+");
    if (fp == NULL) {
        fp = plat_fopen((char *) fn, "rb");
        if (fp != NULL)
            tape.readonly = 1;
    }

    /* A cartridge that isn't there yet is a blank one - create it (unless write-protected). */
    if (fp == NULL) {
        if (tape.readonly) {
            fdd_tape_log("Tape: image %s not found (write-protected)\n", fn);
            return;
        }

        fp = plat_fopen((char *) fn, "wb+");
        if (fp == NULL) {
            fdd_tape_log("Tape: unable to open image %s\n", fn);
            return;
        }
    }

    if (fseek(fp, 0, SEEK_END) == 0)
        tape.image_size = (uint32_t) ftell(fp);
    else
        tape.image_size = 0;

    tape.fp = fp;

    tape_read_geometry();

    if (tape.attached)
        writeprot[tape.drive] = tape.readonly;

    fdd_tape_log("Tape: loaded %s (%u bytes%s)\n", fn, tape.image_size,
                 tape.readonly ? ", read-only" : "");
}

void
fdd_tape_init(void)
{
    int drive;

    fdd_tape_close();

    if (!fdd_tape_enabled)
        return;

    drive = fdd_tape_unit;
    if ((drive < 0) || (drive >= FDD_NUM))
        drive = 1;

    memset(&tape, 0x00, sizeof(tape));

    tape.attached      = 1;
    tape.drive         = drive;
    tape.segs_per_cyl  = FDD_TAPE_SEGS_PER_CYL;
    tape.segs_per_head = FDD_TAPE_SEGS_PER_HEAD;
    tape.rate_code     = QIC_RATE_500;
    tape.format_code   = QIC_FORMAT_QIC80;
    tape.status     = QIC_STATUS_READY;
    tape.report_pos = TAPE_REPORT_IDLE;
    tape.error_cmd  = QIC_NO_COMMAND;
    tape.xfer_state = TAPE_XFER_IDLE;

    /* timer_add() clears the struct, which also clears any stale enabled
       flag left behind by a timer_close() during a hard reset. */
    timer_add(&tape_timer, tape_clock, NULL, 0);
    tape_timer_added = 1;

    timer_add(&tape_cmd_timer, tape_command_timeout, NULL, 0);
    tape_cmd_timer_added = 1;

    timer_add(&tape_motion_timer, tape_motion_tick, NULL, 0);
    tape_motion_timer_added = 1;

    fdd_tape_log("Tape: attached to drive %i\n", drive);

    /* The tape drive takes over this drive select line from whatever
       floppy drive may have been configured on it. Note that it has no
       poll handler - the transfer clock is the drive's own. */
    drives[drive].seek          = tape_seek;
    drives[drive].readsector    = tape_readsector;
    drives[drive].writesector   = tape_writesector;
    drives[drive].comparesector = tape_comparesector;
    drives[drive].readaddress   = tape_readaddress;
    drives[drive].format        = tape_format;
    drives[drive].hole          = tape_hole;
    drives[drive].byteperiod    = tape_byteperiod;
    drives[drive].stop          = tape_stop;
    drives[drive].poll          = NULL;

    drive_empty[drive] = 0;
    fdd_changed[drive] = 0;

    fdd_tape_load(fdd_tape_fn);
}

void
fdd_tape_close(void)
{
    const int drive        = tape.drive;
    const int was_attached = tape.attached;

    fdd_tape_eject();

    /*
       The timer is left alone on purpose. It lives outside tape_t, so the
       wipe below cannot corrupt it, and it may legitimately still be in
       the timer list here or have been orphaned by a hard reset - calling
       timer_disable() in the latter case would trip the timer layer's own
       consistency check. Should it fire before the drive is re-attached,
       the idle branch of tape_clock() takes it back out of the list.
     */
    if (was_attached) {
        drives[drive].seek          = NULL;
        drives[drive].readsector    = NULL;
        drives[drive].writesector   = NULL;
        drives[drive].comparesector = NULL;
        drives[drive].readaddress   = NULL;
        drives[drive].format        = NULL;
        drives[drive].hole          = NULL;
        drives[drive].byteperiod    = NULL;
        drives[drive].stop          = NULL;
        drives[drive].poll          = NULL;

        drive_empty[drive] = 1;
    }

    memset(&tape, 0x00, sizeof(tape));
    tape.report_pos = TAPE_REPORT_IDLE;

    if (was_attached)
        fdd_load(drive, floppyfns[drive]);
}
