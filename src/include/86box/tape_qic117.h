/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          QIC-117: common command set and definitions for floppy tape
 *          drives and related components.
 *
 * Authors: Dmitry Brant, <me@dmitrybrant.com>
 *
 *          Copyright 2026 Dmitry Brant
 */
#ifndef TAPE_QIC117_H
#define TAPE_QIC117_H

/*
   The command set, with each command being that many step pulses on the FDC.
 */
enum {
    QIC_NO_COMMAND                  =  0,
    QIC_RESET                       =  1,
    QIC_REPORT_NEXT_BIT             =  2,
    QIC_PAUSE                       =  3,
    QIC_MICRO_STEP_PAUSE            =  4,
    QIC_ALTERNATE_TIMEOUT           =  5,
    QIC_REPORT_DRIVE_STATUS         =  6,
    QIC_REPORT_ERROR_CODE           =  7,
    QIC_REPORT_DRIVE_CONFIG         =  8,
    QIC_REPORT_ROM_VERSION          =  9,
    QIC_LOGICAL_FORWARD             = 10,
    QIC_PHYSICAL_REVERSE            = 11,
    QIC_PHYSICAL_FORWARD            = 12,
    QIC_SEEK_HEAD_TO_TRACK          = 13,
    QIC_SEEK_LOAD_POINT             = 14,
    QIC_ENTER_FORMAT_MODE           = 15,
    QIC_WRITE_REFERENCE_BURST       = 16,
    QIC_ENTER_VERIFY_MODE           = 17,
    QIC_STOP_TAPE                   = 18,
    QIC_MICRO_STEP_HEAD_UP          = 21,
    QIC_MICRO_STEP_HEAD_DOWN        = 22,
    QIC_SOFT_SELECT                 = 23,
    QIC_SOFT_DESELECT               = 24,
    QIC_SKIP_REVERSE                = 25,
    QIC_SKIP_FORWARD                = 26,
    QIC_SELECT_RATE                 = 27,
    QIC_ENTER_DIAGNOSTIC_1          = 28,
    QIC_ENTER_DIAGNOSTIC_2          = 29,
    QIC_ENTER_PRIMARY_MODE          = 30,
    QIC_REPORT_VENDOR_ID            = 32,
    QIC_REPORT_TAPE_STATUS          = 33,
    QIC_SKIP_EXTENDED_REVERSE       = 34,
    QIC_SKIP_EXTENDED_FORWARD       = 35,
    QIC_CALIBRATE_TAPE_LENGTH       = 36,
    QIC_REPORT_FORMAT_SEGMENTS      = 37,
    QIC_SET_FORMAT_SEGMENTS         = 38,
    QIC_PHANTOM_SELECT              = 46,
    QIC_PHANTOM_DESELECT            = 47,
    QIC_EXT_SELECT_RATE             = 50,
    QIC_EXT_REPORT_DRIVE_CONFIG     = 51,
    QIC_LOADER_PARTITION_STATUS     = 54,
    QIC_SEEK_TO_PARTITION           = 55
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
