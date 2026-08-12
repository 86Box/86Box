/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          QIC-117: the parts of the command set that belong to the bus
 *          rather than to any one drive.
 *
 *          Two drives here speak it - the cable-attached Colorado Jumbo
 *          in floppy/fdd_tape.c and the parallel-port Iomega Ditto behind
 *          a BackPack bridge in device/lpt_ditto.c - and until now they
 *          had a copy each. The copies drifted, which is not a
 *          theoretical worry: they came to disagree about how many
 *          nibbles a skip count carries, and the one that was wrong read
 *          the second nibble as a command and stopped every backup at the
 *          same segment. Command numbering, the bits a drive reports and
 *          the width of each command's parameters are properties of the
 *          bus, so they live here where they cannot drift again.
 *
 *          What deliberately does NOT live here is anything a drive gets
 *          to decide: which of these commands it implements, what it does
 *          with one, its identity, its geometry, and how it is wired to
 *          the host. The two engines differ in all of those - one is fed
 *          by 86Box's floppy controller a sector at a time, the other has
 *          a controller of its own inside the bridge and moves whole
 *          segments - and merging those would trade a duplication that is
 *          now visible for a shared path that would not be.
 *
 * Authors: Dmitry Brant, <me@dmitrybrant.com>
 *
 *          Copyright 2026 Dmitry Brant
 */
#ifndef TAPE_QIC117_H
#define TAPE_QIC117_H

/*
   The command set. A command is sent as that many step pulses, so these
   numbers are the wire protocol rather than an enumeration of it. The
   gaps are codes the standard reserves or leaves to the vendor; what a
   drive makes of those is its own business and is defined with the drive.
 */
enum {
    QIC_RESET                       =  1,  /* soft reset */
    QIC_REPORT_NEXT_BIT             =  2,  /* report next bit */
    QIC_PAUSE                       =  3,  /* pause */
    QIC_MICRO_STEP_PAUSE            =  4,  /* micro step pause */
    QIC_ALTERNATE_TIMEOUT           =  5,  /* alternate command timeout */
    QIC_REPORT_DRIVE_STATUS         =  6,  /* report drive status */
    QIC_REPORT_ERROR_CODE           =  7,  /* report error code */
    QIC_REPORT_DRIVE_CONFIG         =  8,  /* report drive configuration */
    QIC_REPORT_ROM_VERSION          =  9,  /* report ROM version */
    QIC_LOGICAL_FORWARD             = 10,  /* logical forward */
    QIC_PHYSICAL_REVERSE            = 11,  /* physical reverse */
    QIC_PHYSICAL_FORWARD            = 12,  /* physical forward */
    QIC_SEEK_HEAD_TO_TRACK          = 13,  /* seek head to track */
    QIC_SEEK_LOAD_POINT             = 14,  /* seek load point */
    QIC_ENTER_FORMAT_MODE           = 15,  /* enter format mode */
    QIC_WRITE_REFERENCE_BURST       = 16,  /* write reference burst */
    QIC_ENTER_VERIFY_MODE           = 17,  /* enter verify mode */
    QIC_STOP_TAPE                   = 18,  /* stop tape */
    QIC_MICRO_STEP_HEAD_UP          = 21,  /* micro step head up */
    QIC_MICRO_STEP_HEAD_DOWN        = 22,  /* micro step head down */
    QIC_SOFT_SELECT                 = 23,  /* soft select */
    QIC_SOFT_DESELECT               = 24,  /* soft deselect */
    QIC_SKIP_REVERSE                = 25,  /* skip segments reverse */
    QIC_SKIP_FORWARD                = 26,  /* skip segments forward */
    QIC_SELECT_RATE                 = 27,  /* select rate or format */
    QIC_ENTER_DIAGNOSTIC_1          = 28,  /* enter diagnostic 1 */
    QIC_ENTER_DIAGNOSTIC_2          = 29,  /* enter diagnostic 2 */
    QIC_ENTER_PRIMARY_MODE          = 30,  /* enter primary mode */
    QIC_REPORT_VENDOR_ID            = 32,  /* report vendor ID */
    QIC_REPORT_TAPE_STATUS          = 33,  /* report tape status */
    QIC_SKIP_EXTENDED_REVERSE       = 34,  /* skip extended reverse */
    QIC_SKIP_EXTENDED_FORWARD       = 35,  /* skip extended forward */
    QIC_CALIBRATE_TAPE_LENGTH       = 36,  /* calibrate tape length */
    QIC_REPORT_FORMAT_SEGMENTS      = 37,  /* report format segments */
    QIC_SET_FORMAT_SEGMENTS         = 38,  /* set format segments */
    QIC_PHANTOM_SELECT              = 46,  /* phantom select */
    QIC_PHANTOM_DESELECT            = 47,  /* phantom deselect */
    QIC_EXT_SELECT_RATE             = 50,  /* extended select rate */
    QIC_EXT_REPORT_DRIVE_CONFIG     = 51,  /* extended report drive config */
    QIC_LOADER_PARTITION_STATUS     = 54,  /* loader partition status */
    QIC_SEEK_TO_PARTITION           = 55   /* seek to partition */
};

/* Drive status, as reported by command 6. */
#define QIC_STATUS_READY             0x01
#define QIC_STATUS_ERROR             0x02
#define QIC_STATUS_CARTRIDGE_PRESENT 0x04
#define QIC_STATUS_WRITE_PROTECT     0x08
#define QIC_STATUS_NEW_CARTRIDGE     0x10
#define QIC_STATUS_REFERENCED        0x20
#define QIC_STATUS_AT_BOT            0x40
#define QIC_STATUS_AT_EOT            0x80

/* Drive configuration, as reported by command 8. */
#define QIC_CONFIG_RATE_SHIFT        3
#define QIC_CONFIG_RATE_MASK         0x18
#define QIC_CONFIG_LONG              0x40
#define QIC_CONFIG_80                0x80

/*
   Rate codes, as they sit in that field and as the argument to Select
   Rate. Not in numeric order: 250 kbit/s came first, and 2 Mbit/s was
   fitted into the spare code afterwards.

   What a drive reports here is what it calibrates to - a QIC-80 drive
   runs at 500 kbit/s by default - but it is not necessarily what the
   transfer is clocked at: that follows whatever the host programs into
   the controller.
 */
#define QIC_RATE_250                 0
#define QIC_RATE_2000                1
#define QIC_RATE_500                 2
#define QIC_RATE_1000                3

/*
   Cartridge status, as reported by command 33: a length in the high
   nibble and a format in the low one. VAR_LEN_900 and FLEX are two
   readings of the same code, kept under both names because the two
   drives were documented from different sources.
 */
#define QIC_TAPE_STD_MASK            0x0f
#define QIC_TAPE_LEN_MASK            0x70
#define QIC_TAPE_QIC40               0x01
#define QIC_TAPE_QIC80               0x02
#define QIC_TAPE_QIC3020             0x03
#define QIC_TAPE_QIC3010             0x04
#define QIC_TAPE_205FT               0x10
#define QIC_TAPE_307FT               0x20
#define QIC_TAPE_VAR_LEN_550         0x30
#define QIC_TAPE_1100FT              0x40
#define QIC_TAPE_FLEX                0x60
#define QIC_TAPE_VAR_LEN_900         0x60
#define QIC_TAPE_WIDE                0x80

/*
   Format codes, as they appear in the cartridge header segment. Select
   Rate takes one of these instead of a rate code when its argument is
   above the rate codes: the argument is then (format * 4) + increment,
   where increment 1 is ordinary quarter inch media and 3 is 8 mm wide
   tape.
 */
#define QIC_FORMAT_QIC3010           ((4 << 2) | 1)
#define QIC_FORMAT_QIC3020           ((3 << 2) | 1)
#define QIC_FORMAT_QIC40             ((1 << 2) | 1)
#define QIC_FORMAT_QIC80             ((2 << 2) | 1)

/*
   Error codes. Which of these a drive can raise depends on its firmware;
   the numbering does not.
 */
#define QIC_ERROR_NONE               0
#define QIC_ERROR_NOT_READY          1
#define QIC_ERROR_NO_CARTRIDGE       2
#define QIC_ERROR_WRITE_PROTECTED    5
#define QIC_ERROR_UNDEFINED_COMMAND  6
#define QIC_ERROR_ILLEGAL_SEEK_TRACK 7
#define QIC_ERROR_ILLEGAL_IN_REPORT  8
#define QIC_ERROR_ILLEGAL_DIAG_ENTRY 9
#define QIC_ERROR_PENDING_ERROR      12
#define QIC_ERROR_NEW_CARTRIDGE      13
#define QIC_ERROR_ILLEGAL_IN_PRIMARY 14
#define QIC_ERROR_ILLEGAL_IN_FORMAT  15
#define QIC_ERROR_ILLEGAL_IN_VERIFY  16
#define QIC_ERROR_NOT_REFERENCED     19
#define QIC_ERROR_DIAGNOSTIC_FAILED  20
#define QIC_ERROR_POWER_ON_RESET     26
#define QIC_ERROR_SOFT_RESET         27
#define QIC_ERROR_DURING_NON_INTR    30
#define QIC_ERROR_RATE_SELECTION     31
#define QIC_ERROR_ILLEGAL_IN_HIGH_SPEED 32

/*
   How many parameter nibbles a command carries, low nibble first, each
   biased by two on the wire so that a parameter of zero cannot be
   mistaken for the empty pulse train that means nothing was sent.

   The width is fixed per command rather than chosen to fit the value,
   which is the part that is easy to get wrong: ftape's
   ftape_skip_segments() picks "count > 255 ? 3 : 2" and then always sends
   that many. Reading fewer than were sent is silent while the counts stay
   small - the high nibble is zero, and zero arrives as the two pulse
   train that a drive with nothing armed discards as a request for the
   next bit - and then fails on the first long skip, where the leftover
   nibble is read as a command.

   Two codes are deliberately absent, because the drives here disagree
   about them and neither can prove the other wrong: phantom select,
   which takes a drive number on a cable but does not exist on a bus with
   one drive wired permanently selected, and enter diagnostic 2, which
   the Ditto's firmware was read as wanting two parameters for and which
   no host has ever sent. Each drive answers for those itself, so that
   the disagreement is written down rather than settled by whichever
   file was edited last.
 */
static inline int
qic117_command_params(int command)
{
    switch (command) {
        /* Three nibbles, for a count of up to 4095 (table 2b). */
        case QIC_SKIP_EXTENDED_REVERSE:
        case QIC_SKIP_EXTENDED_FORWARD:
        case QIC_SET_FORMAT_SEGMENTS:
            return 3;

        case QIC_SKIP_REVERSE:
        case QIC_SKIP_FORWARD:
            return 2;

        case QIC_ALTERNATE_TIMEOUT:
        case QIC_SEEK_HEAD_TO_TRACK:
        case QIC_SOFT_SELECT:
        case QIC_SELECT_RATE:
        case QIC_EXT_SELECT_RATE:
        case QIC_SEEK_TO_PARTITION:
            return 1;

        default:
            return 0;
    }
}

#endif /*TAPE_QIC117_H*/
