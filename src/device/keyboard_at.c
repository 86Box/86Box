/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          Intel 8042 (AT keyboard controller) emulation.
 *
 *
 *
 * Authors: Sarah Walker, <http://pcem-emulator.co.uk/>
 *          Miran Grca, <mgrca8@gmail.com>
 *          Fred N. van Kempen, <decwiz@yahoo.com>
 *          EngiNerd <webmaster.crrc@yahoo.it>
 *
 *          Copyright 2008-2020 Sarah Walker.
 *          Copyright 2016-2020 Miran Grca.
 *          Copyright 2017-2020 Fred N. van Kempen.
 *          Copyright 2020 EngiNerd.
 */
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#define HAVE_STDARG_H
#include <wchar.h>
#include <86box/86box.h>
#include "cpu.h"
#include <86box/timer.h>
#include <86box/io.h>
#include <86box/pic.h>
#include <86box/pit.h>
#include <86box/ppi.h>
#include <86box/mem.h>
#include <86box/device.h>
#include <86box/machine.h>
#include <86box/m_xt_xi8088.h>
#include <86box/m_at_t3100e.h>
#include <86box/fdd.h>
#include <86box/fdc.h>
#include <86box/sound.h>
#include <86box/snd_speaker.h>
#include <86box/video.h>
#include <86box/keyboard.h>

#define STAT_PARITY      0x80
#define STAT_RTIMEOUT    0x40
#define STAT_TTIMEOUT    0x20
#define STAT_MFULL       0x20
#define STAT_UNLOCKED    0x10
#define STAT_CD          0x08
#define STAT_SYSFLAG     0x04
#define STAT_IFULL       0x02
#define STAT_OFULL       0x01

#define RESET_DELAY_TIME 1000 /* 100 ms */

#define CCB_UNUSED       0x80
#define CCB_TRANSLATE    0x40
#define CCB_PCMODE       0x20
#define CCB_ENABLEKBD    0x10
#define CCB_IGNORELOCK   0x08
#define CCB_SYSTEM       0x04
#define CCB_ENABLEMINT   0x02
#define CCB_ENABLEKINT   0x01

#define CCB_MASK         0x68
#define MODE_MASK        0x6c

#define KBC_TYPE_ISA     0x00 /* AT ISA-based chips */
#define KBC_TYPE_PS2_1   0x04 /* PS2 type, no refresh */
/* This only differs in that translation is forced off. */
#define KBC_TYPE_PS2_2 0x05 /* PS2 on PS/2, type 2 */
#define KBC_TYPE_MASK  0x07

#define KBC_FLAG_PS2   0x04

/* We need to redefine this:
        Currently, we use bits 3-7 for vendor, we should instead use bits 4-7
        for vendor, 0-3 for revision/variant, and have a dev->ps2 flag controlling
        controller mode, normally set according to the flags, but togglable on
        AMIKey:
                0000 0000	0x00	IBM, AT
                0000 0001	0x01	MR
                0000 0010	0x02	Xi8088, clone of IBM PS/2 type 1
                0001 0000	0x10	Olivetti
                0010 0000	0x20	Toshiba
                0011 0000	0x30	Quadtel
                0100 0000	0x40	Phoenix MultiKey/42
                0101 0000	0x50	AMI KF
                0101 0001	0x51	AMI KH
                0101 0010	0x52	AMIKey
                0101 0011	0x53	AMIKey-2
                0101 0100	0x54	JetKey (clone of AMI KF/AMIKey)
                0110 0000	0x60	Award
                0110 0001	0x61	Award 286 (has some AMI commands apparently)
                0111 0000	0x70	Siemens
*/

/* Standard IBM controller */
#define KBC_VEN_GENERIC 0x00
/* All commands are standard PS/2 */
#define KBC_VEN_IBM_MCA 0x08
/* Standard IBM commands, differs in input port bits */
#define KBC_VEN_IBM_PS1 0x10
/* Olivetti - proprietary commands and port 62h with switches
   readout */
#define KBC_VEN_OLIVETTI 0x20
/* Samsung - TODO */
#define KBC_VEN_SAMSUNG 0x24
/* Toshiba T3100e - has a bunch of proprietary commands, also sets
   IFULL on command AA */
#define KBC_VEN_TOSHIBA 0x28
/* Standard IBM commands, uses input port as a switches readout */
#define KBC_VEN_NCR 0x30
/* Xi8088 - standard IBM commands, has a turbo bit on port 61h, and the
   polarity of the video type bit in the input port is inverted */
#define KBC_VEN_XI8088 0x38
/* QuadtelKey - currently guesswork */
#define KBC_VEN_QUADTEL 0x40
/* Phoenix MultiKey/42 - not yet implemented */
#define KBC_VEN_PHOENIX 0x48
/* Generic commands, XI8088-like input port handling of video type,
   maybe we just need a flag for that? */
#define KBC_VEN_ACER 0x50
/* AMI KF/KH/AMIKey/AMIKey-2 */
#define KBC_VEN_AMI 0xf0
/* Standard AMI commands, differs in input port bits */
#define KBC_VEN_INTEL_AMI 0xf8
#define KBC_VEN_MASK      0xf8

/* Flags should be fully 32-bit:
        Bits  7- 0: Vendor and revision/variant;
        Bits 15- 8: Input port mask;
        Bits 23-16: Input port bits that are always on;
        Bits 31-24: Flags:
                Bit 0: Invert P1 video type bit polarity;
                Bit 1: Is PS/2;
                Bit 2: Translation forced always off.

        So for example, the IBM PS/2 type 1 controller flags would be: 00000010 00000000 11111111 00000000 = 0200ff00 . */

typedef struct {
    uint8_t status, ib, ob, p1, p2, old_p2, p2_locked, fast_a20_phase,
        secr_phase, mem_index, ami_stat, ami_mode,
        kbc_in, kbc_cmd, kbc_in_cmd, kbc_poll_phase, kbc_to_send,
        kbc_send_pending, kbc_channel, kbc_stat_hi, kbc_wait_for_response, inhibit,
        kbd_in, kbd_cmd, kbd_in_cmd, kbd_written, kbd_data, kbd_poll_phase, kbd_inhibit,
        mouse_in, mouse_cmd, mouse_in_cmd, mouse_written, mouse_data, mouse_poll_phase, mouse_inhibit,
        kbc_written[3], kbc_data[3];

    uint8_t mem_int[0x40], mem[0x240];

    uint16_t last_irq, kbc_phase, kbd_phase, mouse_phase;

    uint32_t flags;

    pc_timer_t pulse_cb, send_delay_timer;

    uint8_t (*write60_ven)(void *p, uint8_t val);
    uint8_t (*write64_ven)(void *p, uint8_t val);
} atkbd_t;

enum {
    CHANNEL_KBC = 0,
    CHANNEL_KBD,
    CHANNEL_MOUSE
};

enum {
    KBD_MAIN_LOOP = 0,
    KBD_CMD_PROCESS
};

enum {
    MOUSE_MAIN_LOOP_1 = 0,
    MOUSE_CMD_PROCESS,
    MOUSE_CMD_END,
    MOUSE_MAIN_LOOP_2
};

enum {
    KBC_MAIN_LOOP = 0,
    KBC_RESET     = 1,
    KBC_WAIT      = 4,
    KBC_WAIT_FOR_KBD,
    KBC_WAIT_FOR_MOUSE,
    KBC_WAIT_FOR_BOTH
};

static void kbd_cmd_process(atkbd_t *dev);

static void kbc_wait(atkbd_t *dev, uint8_t flags);

/* bit 0 = repeat, bit 1 = makes break code? */
uint8_t keyboard_set3_flags[512];
uint8_t keyboard_set3_all_repeat;
uint8_t keyboard_set3_all_break;

/* Bits 0 - 1 = scan code set, bit 6 = translate or not. */
uint8_t keyboard_mode = 0x42;

uint8_t *ami_copr = (uint8_t *) "(C)1994 AMI";

static uint8_t key_queue[16];
static int     key_queue_start = 0, key_queue_end = 0;
uint8_t        mouse_queue[16];
int            mouse_queue_start = 0, mouse_queue_end = 0;
static uint8_t kbd_last_scan_code;
#ifdef ENABLE_MOUSE
static void (*mouse_write)(uint8_t val, void *priv) = NULL;
static void *mouse_p                                = NULL;
#endif
static uint8_t  sc_or    = 0;
static atkbd_t *SavedKbd = NULL; // FIXME: remove!!! --FvK

/* Non-translated to translated scan codes. */
static const uint8_t nont_to_t[256] = {
    0xff, 0x43, 0x41, 0x3f, 0x3d, 0x3b, 0x3c, 0x58,
    0x64, 0x44, 0x42, 0x40, 0x3e, 0x0f, 0x29, 0x59,
    0x65, 0x38, 0x2a, 0x70, 0x1d, 0x10, 0x02, 0x5a,
    0x66, 0x71, 0x2c, 0x1f, 0x1e, 0x11, 0x03, 0x5b,
    0x67, 0x2e, 0x2d, 0x20, 0x12, 0x05, 0x04, 0x5c,
    0x68, 0x39, 0x2f, 0x21, 0x14, 0x13, 0x06, 0x5d,
    0x69, 0x31, 0x30, 0x23, 0x22, 0x15, 0x07, 0x5e,
    0x6a, 0x72, 0x32, 0x24, 0x16, 0x08, 0x09, 0x5f,
    0x6b, 0x33, 0x25, 0x17, 0x18, 0x0b, 0x0a, 0x60,
    0x6c, 0x34, 0x35, 0x26, 0x27, 0x19, 0x0c, 0x61,
    0x6d, 0x73, 0x28, 0x74, 0x1a, 0x0d, 0x62, 0x6e,
    0x3a, 0x36, 0x1c, 0x1b, 0x75, 0x2b, 0x63, 0x76,
    0x55, 0x56, 0x77, 0x78, 0x79, 0x7a, 0x0e, 0x7b,
    0x7c, 0x4f, 0x7d, 0x4b, 0x47, 0x7e, 0x7f, 0x6f,
    0x52, 0x53, 0x50, 0x4c, 0x4d, 0x48, 0x01, 0x45,
    0x57, 0x4e, 0x51, 0x4a, 0x37, 0x49, 0x46, 0x54,
    0x80, 0x81, 0x82, 0x41, 0x54, 0x85, 0x86, 0x87,
    0x88, 0x89, 0x8a, 0x8b, 0x8c, 0x8d, 0x8e, 0x8f,
    0x90, 0x91, 0x92, 0x93, 0x94, 0x95, 0x96, 0x97,
    0x98, 0x99, 0x9a, 0x9b, 0x9c, 0x9d, 0x9e, 0x9f,
    0xa0, 0xa1, 0xa2, 0xa3, 0xa4, 0xa5, 0xa6, 0xa7,
    0xa8, 0xa9, 0xaa, 0xab, 0xac, 0xad, 0xae, 0xaf,
    0xb0, 0xb1, 0xb2, 0xb3, 0xb4, 0xb5, 0xb6, 0xb7,
    0xb8, 0xb9, 0xba, 0xbb, 0xbc, 0xbd, 0xbe, 0xbf,
    0xc0, 0xc1, 0xc2, 0xc3, 0xc4, 0xc5, 0xc6, 0xc7,
    0xc8, 0xc9, 0xca, 0xcb, 0xcc, 0xcd, 0xce, 0xcf,
    0xd0, 0xd1, 0xd2, 0xd3, 0xd4, 0xd5, 0xd6, 0xd7,
    0xd8, 0xd9, 0xda, 0xdb, 0xdc, 0xdd, 0xde, 0xdf,
    0xe0, 0xe1, 0xe2, 0xe3, 0xe4, 0xe5, 0xe6, 0xe7,
    0xe8, 0xe9, 0xea, 0xeb, 0xec, 0xed, 0xee, 0xef,
    0xf0, 0xf1, 0xf2, 0xf3, 0xf4, 0xf5, 0xf6, 0xf7,
    0xf8, 0xf9, 0xfa, 0xfb, 0xfc, 0xfd, 0xfe, 0xff
};

#ifdef USE_SET1
static const scancode scancode_set1[512] = {
  // clang-format off
    { {          0},{               0} }, { {     0x01,0},{          0x81,0} }, { {     0x02,0},{          0x82,0} }, { {     0x03,0},{          0x83,0} },        /*000*/
    { {     0x04,0},{          0x84,0} }, { {     0x05,0},{          0x85,0} }, { {     0x06,0},{          0x86,0} }, { {     0x07,0},{          0x87,0} },        /*004*/
    { {     0x08,0},{          0x88,0} }, { {     0x09,0},{          0x89,0} }, { {     0x0a,0},{          0x8a,0} }, { {     0x0b,0},{          0x8b,0} },        /*008*/
    { {     0x0c,0},{          0x8c,0} }, { {     0x0d,0},{          0x8d,0} }, { {     0x0e,0},{          0x8e,0} }, { {     0x0f,0},{          0x8f,0} },        /*00c*/
    { {     0x10,0},{          0x90,0} }, { {     0x11,0},{          0x91,0} }, { {     0x12,0},{          0x92,0} }, { {     0x13,0},{          0x93,0} },        /*010*/
    { {     0x14,0},{          0x94,0} }, { {     0x15,0},{          0x95,0} }, { {     0x16,0},{          0x96,0} }, { {     0x17,0},{          0x97,0} },        /*014*/
    { {     0x18,0},{          0x98,0} }, { {     0x19,0},{          0x99,0} }, { {     0x1a,0},{          0x9a,0} }, { {     0x1b,0},{          0x9b,0} },        /*018*/
    { {     0x1c,0},{          0x9c,0} }, { {     0x1d,0},{          0x9d,0} }, { {     0x1e,0},{          0x9e,0} }, { {     0x1f,0},{          0x9f,0} },        /*01c*/
    { {     0x20,0},{          0xa0,0} }, { {     0x21,0},{          0xa1,0} }, { {     0x22,0},{          0xa2,0} }, { {     0x23,0},{          0xa3,0} },        /*020*/
    { {     0x24,0},{          0xa4,0} }, { {     0x25,0},{          0xa5,0} }, { {     0x26,0},{          0xa6,0} }, { {     0x27,0},{          0xa7,0} },        /*024*/
    { {     0x28,0},{          0xa8,0} }, { {     0x29,0},{          0xa9,0} }, { {     0x2a,0},{          0xaa,0} }, { {     0x2b,0},{          0xab,0} },        /*028*/
    { {     0x2c,0},{          0xac,0} }, { {     0x2d,0},{          0xad,0} }, { {     0x2e,0},{          0xae,0} }, { {     0x2f,0},{          0xaf,0} },        /*02c*/
    { {     0x30,0},{          0xb0,0} }, { {     0x31,0},{          0xb1,0} }, { {     0x32,0},{          0xb2,0} }, { {     0x33,0},{          0xb3,0} },        /*030*/
    { {     0x34,0},{          0xb4,0} }, { {     0x35,0},{          0xb5,0} }, { {     0x36,0},{          0xb6,0} }, { {     0x37,0},{          0xb7,0} },        /*034*/
    { {     0x38,0},{          0xb8,0} }, { {     0x39,0},{          0xb9,0} }, { {     0x3a,0},{          0xba,0} }, { {     0x3b,0},{          0xbb,0} },        /*038*/
    { {     0x3c,0},{          0xbc,0} }, { {     0x3d,0},{          0xbd,0} }, { {     0x3e,0},{          0xbe,0} }, { {     0x3f,0},{          0xbf,0} },        /*03c*/
    { {     0x40,0},{          0xc0,0} }, { {     0x41,0},{          0xc1,0} }, { {     0x42,0},{          0xc2,0} }, { {     0x43,0},{          0xc3,0} },        /*040*/
    { {     0x44,0},{          0xc4,0} }, { {     0x45,0},{          0xc5,0} }, { {     0x46,0},{          0xc6,0} }, { {     0x47,0},{          0xc7,0} },        /*044*/
    { {     0x48,0},{          0xc8,0} }, { {     0x49,0},{          0xc9,0} }, { {     0x4a,0},{          0xca,0} }, { {     0x4b,0},{          0xcb,0} },        /*048*/
    { {     0x4c,0},{          0xcc,0} }, { {     0x4d,0},{          0xcd,0} }, { {     0x4e,0},{          0xce,0} }, { {     0x4f,0},{          0xcf,0} },        /*04c*/
    { {     0x50,0},{          0xd0,0} }, { {     0x51,0},{          0xd1,0} }, { {     0x52,0},{          0xd2,0} }, { {     0x53,0},{          0xd3,0} },        /*050*/
    { {     0x54,0},{          0xd4,0} }, { {     0x55,0},{          0xd5,0} }, { {     0x56,0},{          0xd6,0} }, { {     0x57,0},{          0xd7,0} },        /*054*/
    { {     0x58,0},{          0xd8,0} }, { {     0x59,0},{          0xd9,0} }, { {     0x5a,0},{          0xda,0} }, { {     0x5b,0},{          0xdb,0} },        /*058*/
    { {     0x5c,0},{          0xdc,0} }, { {     0x5d,0},{          0xdd,0} }, { {     0x5e,0},{          0xde,0} }, { {     0x5f,0},{          0xdf,0} },        /*05c*/
    { {     0x60,0},{          0xe0,0} }, { {     0x61,0},{          0xe1,0} }, { {     0x62,0},{          0xe2,0} }, { {     0x63,0},{          0xe3,0} },        /*060*/
    { {     0x64,0},{          0xe4,0} }, { {     0x65,0},{          0xe5,0} }, { {     0x66,0},{          0xe6,0} }, { {     0x67,0},{          0xe7,0} },        /*064*/
    { {     0x68,0},{          0xe8,0} }, { {     0x69,0},{          0xe9,0} }, { {     0x6a,0},{          0xea,0} }, { {     0x6b,0},{          0xeb,0} },        /*068*/
    { {     0x6c,0},{          0xec,0} }, { {     0x6d,0},{          0xed,0} }, { {     0x6e,0},{          0xee,0} }, { {     0x6f,0},{          0xef,0} },        /*06c*/
    { {     0x70,0},{          0xf0,0} }, { {     0x71,0},{          0xf1,0} }, { {     0x72,0},{          0xf2,0} }, { {     0x73,0},{          0xf3,0} },        /*070*/
    { {     0x74,0},{          0xf4,0} }, { {     0x75,0},{          0xf5,0} }, { {     0x76,0},{          0xf6,0} }, { {     0x77,0},{          0xf7,0} },        /*074*/
    { {     0x78,0},{          0xf8,0} }, { {     0x79,0},{          0xf9,0} }, { {     0x7a,0},{          0xfa,0} }, { {     0x7b,0},{          0xfb,0} },        /*078*/
    { {     0x7c,0},{          0xfc,0} }, { {     0x7d,0},{          0xfd,0} }, { {     0x7e,0},{          0xfe,0} }, { {     0x7f,0},{          0xff,0} },        /*07c*/

    { {     0x80,0},{               0} }, { {     0x81,0},{               0} }, { {     0x82,0},{               0} }, { {          0},{               0} },        /*080*/
    { {          0},{               0} }, { {     0x85,0},{               0} }, { {     0x86,0},{               0} }, { {     0x87,0},{               0} },        /*084*/
    { {     0x88,0},{               0} }, { {     0x89,0},{               0} }, { {     0x8a,0},{               0} }, { {     0x8b,0},{               0} },        /*088*/
    { {     0x8c,0},{               0} }, { {     0x8d,0},{               0} }, { {     0x8e,0},{               0} }, { {     0x8f,0},{               0} },        /*08c*/
    { {     0x90,0},{               0} }, { {     0x91,0},{               0} }, { {     0x92,0},{               0} }, { {     0x93,0},{               0} },        /*090*/
    { {     0x94,0},{               0} }, { {     0x95,0},{               0} }, { {     0x96,0},{               0} }, { {     0x97,0},{               0} },        /*094*/
    { {     0x98,0},{               0} }, { {     0x99,0},{               0} }, { {     0x9a,0},{               0} }, { {     0x9b,0},{               0} },        /*098*/
    { {     0x9c,0},{               0} }, { {     0x9d,0},{               0} }, { {     0x9e,0},{               0} }, { {     0x9f,0},{               0} },        /*09c*/
    { {     0xa0,0},{               0} }, { {     0xa1,0},{               0} }, { {     0xa2,0},{               0} }, { {     0xa3,0},{               0} },        /*0a0*/
    { {     0xa4,0},{               0} }, { {     0xa5,0},{               0} }, { {     0xa6,0},{               0} }, { {     0xa7,0},{               0} },        /*0a4*/
    { {     0xa8,0},{               0} }, { {     0xa9,0},{               0} }, { {     0xaa,0},{               0} }, { {     0xab,0},{               0} },        /*0a8*/
    { {     0xac,0},{               0} }, { {     0xad,0},{               0} }, { {     0xae,0},{               0} }, { {     0xaf,0},{               0} },        /*0ac*/
    { {     0xb0,0},{               0} }, { {     0xb1,0},{               0} }, { {     0xb2,0},{               0} }, { {     0xb3,0},{               0} },        /*0b0*/
    { {     0xb4,0},{               0} }, { {     0xb5,0},{               0} }, { {     0xb6,0},{               0} }, { {     0xb7,0},{               0} },        /*0b4*/
    { {     0xb8,0},{               0} }, { {     0xb9,0},{               0} }, { {     0xba,0},{               0} }, { {     0xbb,0},{               0} },        /*0b8*/
    { {     0xbc,0},{               0} }, { {     0xbd,0},{               0} }, { {     0xbe,0},{               0} }, { {     0xbf,0},{               0} },        /*0bc*/
    { {     0xc0,0},{               0} }, { {     0xc1,0},{               0} }, { {     0xc2,0},{               0} }, { {     0xc3,0},{               0} },        /*0c0*/
    { {     0xc4,0},{               0} }, { {     0xc5,0},{               0} }, { {     0xc6,0},{               0} }, { {     0xc7,0},{               0} },        /*0c4*/
    { {     0xc8,0},{               0} }, { {     0xc9,0},{               0} }, { {     0xca,0},{               0} }, { {     0xcb,0},{               0} },        /*0c8*/
    { {     0xcc,0},{               0} }, { {     0xcd,0},{               0} }, { {     0xce,0},{               0} }, { {     0xcf,0},{               0} },        /*0cc*/
    { {     0xd0,0},{               0} }, { {     0xd1,0},{               0} }, { {     0xd2,0},{               0} }, { {     0xd3,0},{               0} },        /*0d0*/
    { {     0xd4,0},{               0} }, { {     0xd5,0},{               0} }, { {     0xd6,0},{               0} }, { {     0xd7,0},{               0} },        /*0d4*/
    { {     0xd8,0},{               0} }, { {     0xd9,0},{               0} }, { {     0xda,0},{               0} }, { {     0xdb,0},{               0} },        /*0d8*/
    { {     0xdc,0},{               0} }, { {     0xdd,0},{               0} }, { {     0xde,0},{               0} }, { {     0xdf,0},{               0} },        /*0dc*/
    { {     0xe0,0},{               0} }, { {     0xe1,0},{               0} }, { {     0xe2,0},{               0} }, { {     0xe3,0},{               0} },        /*0e0*/
    { {     0xe4,0},{               0} }, { {     0xe5,0},{               0} }, { {     0xe6,0},{               0} }, { {     0xe7,0},{               0} },        /*0e4*/
    { {     0xe8,0},{               0} }, { {     0xe9,0},{               0} }, { {     0xea,0},{               0} }, { {     0xeb,0},{               0} },        /*0e8*/
    { {     0xec,0},{               0} }, { {     0xed,0},{               0} }, { {     0xee,0},{               0} }, { {     0xef,0},{               0} },        /*0ec*/
    { {          0},{               0} }, { {     0xf1,0},{               0} }, { {     0xf2,0},{               0} }, { {     0xf3,0},{               0} },        /*0f0*/
    { {     0xf4,0},{               0} }, { {     0xf5,0},{               0} }, { {     0xf6,0},{               0} }, { {     0xf7,0},{               0} },        /*0f4*/
    { {     0xf8,0},{               0} }, { {     0xf9,0},{               0} }, { {     0xfa,0},{               0} }, { {     0xfb,0},{               0} },        /*0f8*/
    { {     0xfc,0},{               0} }, { {     0xfd,0},{               0} }, { {     0xfe,0},{               0} }, { {     0xff,0},{               0} },        /*0fc*/

    { {0xe1,0x1d,0},{0xe1,     0x9d,0} }, { {0xe0,0x01,0},{0xe0,     0x81,0} }, { {0xe0,0x02,0},{0xe0,     0x82,0} }, { {0xe0,0x03,0},{0xe0,     0x83,0} },        /*100*/
    { {0xe0,0x04,0},{0xe0,     0x84,0} }, { {0xe0,0x05,0},{0xe0,     0x85,0} }, { {0xe0,0x06,0},{0xe0,     0x86,0} }, { {0xe0,0x07,0},{0xe0,     0x87,0} },        /*104*/
    { {0xe0,0x08,0},{0xe0,     0x88,0} }, { {0xe0,0x09,0},{0xe0,     0x89,0} }, { {0xe0,0x0a,0},{0xe0,     0x8a,0} }, { {0xe0,0x0b,0},{0xe0,     0x8b,0} },        /*108*/
    { {0xe0,0x0c,0},{0xe0,     0x8c,0} }, { {          0},{               0} }, { {0xe0,0x0e,0},{0xe0,     0x8e,0} }, { {0xe0,0x0f,0},{0xe0,     0x8f,0} },        /*10c*/
    { {0xe0,0x10,0},{0xe0,     0x90,0} }, { {0xe0,0x11,0},{0xe0,     0x91,0} }, { {0xe0,0x12,0},{0xe0,     0x92,0} }, { {0xe0,0x13,0},{0xe0,     0x93,0} },        /*110*/
    { {0xe0,0x14,0},{0xe0,     0x94,0} }, { {0xe0,0x15,0},{0xe0,     0x95,0} }, { {0xe0,0x16,0},{0xe0,     0x96,0} }, { {0xe0,0x17,0},{0xe0,     0x97,0} },        /*114*/
    { {0xe0,0x18,0},{0xe0,     0x98,0} }, { {0xe0,0x19,0},{0xe0,     0x99,0} }, { {0xe0,0x1a,0},{0xe0,     0x9a,0} }, { {0xe0,0x1b,0},{0xe0,     0x9b,0} },        /*118*/
    { {0xe0,0x1c,0},{0xe0,     0x9c,0} }, { {0xe0,0x1d,0},{0xe0,     0x9d,0} }, { {0xe0,0x1e,0},{0xe0,     0x9e,0} }, { {0xe0,0x1f,0},{0xe0,     0x9f,0} },        /*11c*/
    { {0xe0,0x20,0},{0xe0,     0xa0,0} }, { {0xe0,0x21,0},{0xe0,     0xa1,0} }, { {0xe0,0x22,0},{0xe0,     0xa2,0} }, { {0xe0,0x23,0},{0xe0,     0xa3,0} },        /*120*/
    { {0xe0,0x24,0},{0xe0,     0xa4,0} }, { {0xe0,0x25,0},{0xe0,     0xa5,0} }, { {0xe0,0x26,0},{0xe0,     0xa6,0} }, { {          0},{               0} },        /*124*/
    { {          0},{               0} }, { {          0},{               0} }, { {          0},{               0} }, { {          0},{               0} },        /*128*/
    { {0xe0,0x2c,0},{0xe0,     0xac,0} }, { {0xe0,0x2d,0},{0xe0,     0xad,0} }, { {0xe0,0x2e,0},{0xe0,     0xae,0} }, { {0xe0,0x2f,0},{0xe0,     0xaf,0} },        /*12c*/
    { {0xe0,0x30,0},{0xe0,     0xb0,0} }, { {0xe0,0x31,0},{0xe0,     0xb1,0} }, { {0xe0,0x32,0},{0xe0,     0xb2,0} }, { {          0},{               0} },        /*130*/
    { {0xe0,0x34,0},{0xe0,     0xb4,0} }, { {0xe0,0x35,0},{0xe0,     0xb5,0} }, { {          0},{               0} }, { {0xe0,0x37,0},{0xe0,     0xb7,0} },        /*134*/
    { {0xe0,0x38,0},{0xe0,     0xb8,0} }, { {          0},{               0} }, { {0xe0,0x3a,0},{0xe0,     0xba,0} }, { {0xe0,0x3b,0},{0xe0,     0xbb,0} },        /*138*/
    { {0xe0,0x3c,0},{0xe0,     0xbc,0} }, { {0xe0,0x3d,0},{0xe0,     0xbd,0} }, { {0xe0,0x3e,0},{0xe0,     0xbe,0} }, { {0xe0,0x3f,0},{0xe0,     0xbf,0} },        /*13c*/
    { {0xe0,0x40,0},{0xe0,     0xc0,0} }, { {0xe0,0x41,0},{0xe0,     0xc1,0} }, { {0xe0,0x42,0},{0xe0,     0xc2,0} }, { {0xe0,0x43,0},{0xe0,     0xc3,0} },        /*140*/
    { {0xe0,0x44,0},{0xe0,     0xc4,0} }, { {          0},{               0} }, { {0xe0,0x46,0},{0xe0,     0xc6,0} }, { {0xe0,0x47,0},{0xe0,     0xc7,0} },        /*144*/
    { {0xe0,0x48,0},{0xe0,     0xc8,0} }, { {0xe0,0x49,0},{0xe0,     0xc9,0} }, { {          0},{               0} }, { {0xe0,0x4b,0},{0xe0,     0xcb,0} },        /*148*/
    { {0xe0,0x4c,0},{0xe0,     0xcc,0} }, { {0xe0,0x4d,0},{0xe0,     0xcd,0} }, { {0xe0,0x4e,0},{0xe0,     0xce,0} }, { {0xe0,0x4f,0},{0xe0,     0xcf,0} },        /*14c*/
    { {0xe0,0x50,0},{0xe0,     0xd0,0} }, { {0xe0,0x51,0},{0xe0,     0xd1,0} }, { {0xe0,0x52,0},{0xe0,     0xd2,0} }, { {0xe0,0x53,0},{0xe0,     0xd3,0} },        /*150*/
    { {          0},{               0} }, { {0xe0,0x55,0},{0xe0,     0xd5,0} }, { {          0},{               0} }, { {0xe0,0x57,0},{0xe0,     0xd7,0} },        /*154*/
    { {0xe0,0x58,0},{0xe0,     0xd8,0} }, { {0xe0,0x59,0},{0xe0,     0xd9,0} }, { {0xe0,0x5a,0},{0xe0,     0xaa,0} }, { {0xe0,0x5b,0},{0xe0,     0xdb,0} },        /*158*/
    { {0xe0,0x5c,0},{0xe0,     0xdc,0} }, { {0xe0,0x5d,0},{0xe0,     0xdd,0} }, { {0xe0,0x5e,0},{0xe0,     0xee,0} }, { {0xe0,0x5f,0},{0xe0,     0xdf,0} },        /*15c*/
    { {          0},{               0} }, { {0xe0,0x61,0},{0xe0,     0xe1,0} }, { {0xe0,0x62,0},{0xe0,     0xe2,0} }, { {0xe0,0x63,0},{0xe0,     0xe3,0} },        /*160*/
    { {0xe0,0x64,0},{0xe0,     0xe4,0} }, { {0xe0,0x65,0},{0xe0,     0xe5,0} }, { {0xe0,0x66,0},{0xe0,     0xe6,0} }, { {0xe0,0x67,0},{0xe0,     0xe7,0} },        /*164*/
    { {0xe0,0x68,0},{0xe0,     0xe8,0} }, { {0xe0,0x69,0},{0xe0,     0xe9,0} }, { {0xe0,0x6a,0},{0xe0,     0xea,0} }, { {0xe0,0x6b,0},{0xe0,     0xeb,0} },        /*168*/
    { {0xe0,0x6c,0},{0xe0,     0xec,0} }, { {0xe0,0x6d,0},{0xe0,     0xed,0} }, { {0xe0,0x6e,0},{0xe0,     0xee,0} }, { {          0},{               0} },        /*16c*/
    { {0xe0,0x70,0},{0xe0,     0xf0,0} }, { {0xe0,0x71,0},{0xe0,     0xf1,0} }, { {0xe0,0x72,0},{0xe0,     0xf2,0} }, { {0xe0,0x73,0},{0xe0,     0xf3,0} },        /*170*/
    { {0xe0,0x74,0},{0xe0,     0xf4,0} }, { {0xe0,0x75,0},{0xe0,     0xf5,0} }, { {          0},{               0} }, { {0xe0,0x77,0},{0xe0,     0xf7,0} },        /*174*/
    { {0xe0,0x78,0},{0xe0,     0xf8,0} }, { {0xe0,0x79,0},{0xe0,     0xf9,0} }, { {0xe0,0x7a,0},{0xe0,     0xfa,0} }, { {0xe0,0x7b,0},{0xe0,     0xfb,0} },        /*178*/
    { {0xe0,0x7c,0},{0xe0,     0xfc,0} }, { {0xe0,0x7d,0},{0xe0,     0xfd,0} }, { {0xe0,0x7e,0},{0xe0,     0xfe,0} }, { {0xe0,0x7f,0},{0xe0,     0xff,0} },        /*17c*/

    { {          0},{               0} }, { {          0},{               0} }, { {          0},{               0} }, { {          0},{               0} },        /*180*/
    { {          0},{               0} }, { {          0},{               0} }, { {          0},{               0} }, { {          0},{               0} },        /*184*/
    { {          0},{               0} }, { {          0},{               0} }, { {          0},{               0} }, { {          0},{               0} },        /*188*/
    { {          0},{               0} }, { {          0},{               0} }, { {          0},{               0} }, { {          0},{               0} },        /*18c*/
    { {          0},{               0} }, { {          0},{               0} }, { {          0},{               0} }, { {          0},{               0} },        /*190*/
    { {          0},{               0} }, { {          0},{               0} }, { {          0},{               0} }, { {          0},{               0} },        /*194*/
    { {          0},{               0} }, { {          0},{               0} }, { {          0},{               0} }, { {          0},{               0} },        /*198*/
    { {          0},{               0} }, { {          0},{               0} }, { {          0},{               0} }, { {          0},{               0} },        /*19c*/
    { {          0},{               0} }, { {          0},{               0} }, { {          0},{               0} }, { {          0},{               0} },        /*1a0*/
    { {          0},{               0} }, { {          0},{               0} }, { {          0},{               0} }, { {          0},{               0} },        /*1a4*/
    { {          0},{               0} }, { {          0},{               0} }, { {          0},{               0} }, { {          0},{               0} },        /*1a8*/
    { {          0},{               0} }, { {          0},{               0} }, { {          0},{               0} }, { {          0},{               0} },        /*1ac*/
    { {          0},{               0} }, { {          0},{               0} }, { {          0},{               0} }, { {          0},{               0} },        /*1c0*/
    { {          0},{               0} }, { {          0},{               0} }, { {          0},{               0} }, { {          0},{               0} },        /*1c4*/
    { {          0},{               0} }, { {          0},{               0} }, { {          0},{               0} }, { {          0},{               0} },        /*1c8*/
    { {          0},{               0} }, { {          0},{               0} }, { {          0},{               0} }, { {          0},{               0} },        /*1cc*/
    { {          0},{               0} }, { {          0},{               0} }, { {          0},{               0} }, { {          0},{               0} },        /*1d0*/
    { {          0},{               0} }, { {          0},{               0} }, { {          0},{               0} }, { {          0},{               0} },        /*1d4*/
    { {          0},{               0} }, { {          0},{               0} }, { {          0},{               0} }, { {          0},{               0} },        /*1d8*/
    { {          0},{               0} }, { {          0},{               0} }, { {          0},{               0} }, { {          0},{               0} },        /*1dc*/
    { {          0},{               0} }, { {0xe0,0xe1,0},{               0} }, { {          0},{               0} }, { {          0},{               0} },        /*1e0*/
    { {          0},{               0} }, { {          0},{               0} }, { {          0},{               0} }, { {          0},{               0} },        /*1e4*/
    { {          0},{               0} }, { {          0},{               0} }, { {          0},{               0} }, { {          0},{               0} },        /*1e8*/
    { {          0},{               0} }, { {          0},{               0} }, { {0xe0,0xee,0},{               0} }, { {          0},{               0} },        /*1ec*/
    { {          0},{               0} }, { {0xe0,0xf1,0},{               0} }, { {          0},{               0} }, { {          0},{               0} },        /*1f0*/
    { {          0},{               0} }, { {          0},{               0} }, { {          0},{               0} }, { {          0},{               0} },        /*1f4*/
    { {          0},{               0} }, { {          0},{               0} }, { {          0},{               0} }, { {          0},{               0} },        /*1f8*/
    { {          0},{               0} }, { {          0},{               0} }, { {0xe0,0xfe,0},{               0} }, { {0xe0,0xff,0},{               0} }         /*1fc*/
  // clang-format on
};
#endif

static const scancode scancode_set2[512] = {
  // clang-format off
    { {          0},{               0} }, { {     0x76,0},{     0xF0,0x76,0} }, { {     0x16,0},{     0xF0,0x16,0} }, { {     0x1E,0},{     0xF0,0x1E,0} },        /*000*/
    { {     0x26,0},{     0xF0,0x26,0} }, { {     0x25,0},{     0xF0,0x25,0} }, { {     0x2E,0},{     0xF0,0x2E,0} }, { {     0x36,0},{     0xF0,0x36,0} },        /*004*/
    { {     0x3D,0},{     0xF0,0x3D,0} }, { {     0x3E,0},{     0xF0,0x3E,0} }, { {     0x46,0},{     0xF0,0x46,0} }, { {     0x45,0},{     0xF0,0x45,0} },        /*008*/
    { {     0x4E,0},{     0xF0,0x4E,0} }, { {     0x55,0},{     0xF0,0x55,0} }, { {     0x66,0},{     0xF0,0x66,0} }, { {     0x0D,0},{     0xF0,0x0D,0} },        /*00c*/
    { {     0x15,0},{     0xF0,0x15,0} }, { {     0x1D,0},{     0xF0,0x1D,0} }, { {     0x24,0},{     0xF0,0x24,0} }, { {     0x2D,0},{     0xF0,0x2D,0} },        /*010*/
    { {     0x2C,0},{     0xF0,0x2C,0} }, { {     0x35,0},{     0xF0,0x35,0} }, { {     0x3C,0},{     0xF0,0x3C,0} }, { {     0x43,0},{     0xF0,0x43,0} },        /*014*/
    { {     0x44,0},{     0xF0,0x44,0} }, { {     0x4D,0},{     0xF0,0x4D,0} }, { {     0x54,0},{     0xF0,0x54,0} }, { {     0x5B,0},{     0xF0,0x5B,0} },        /*018*/
    { {     0x5A,0},{     0xF0,0x5A,0} }, { {     0x14,0},{     0xF0,0x14,0} }, { {     0x1C,0},{     0xF0,0x1C,0} }, { {     0x1B,0},{     0xF0,0x1B,0} },        /*01c*/
    { {     0x23,0},{     0xF0,0x23,0} }, { {     0x2B,0},{     0xF0,0x2B,0} }, { {     0x34,0},{     0xF0,0x34,0} }, { {     0x33,0},{     0xF0,0x33,0} },        /*020*/
    { {     0x3B,0},{     0xF0,0x3B,0} }, { {     0x42,0},{     0xF0,0x42,0} }, { {     0x4B,0},{     0xF0,0x4B,0} }, { {     0x4C,0},{     0xF0,0x4C,0} },        /*024*/
    { {     0x52,0},{     0xF0,0x52,0} }, { {     0x0E,0},{     0xF0,0x0E,0} }, { {     0x12,0},{     0xF0,0x12,0} }, { {     0x5D,0},{     0xF0,0x5D,0} },        /*028*/
    { {     0x1A,0},{     0xF0,0x1A,0} }, { {     0x22,0},{     0xF0,0x22,0} }, { {     0x21,0},{     0xF0,0x21,0} }, { {     0x2A,0},{     0xF0,0x2A,0} },        /*02c*/
    { {     0x32,0},{     0xF0,0x32,0} }, { {     0x31,0},{     0xF0,0x31,0} }, { {     0x3A,0},{     0xF0,0x3A,0} }, { {     0x41,0},{     0xF0,0x41,0} },        /*030*/
    { {     0x49,0},{     0xF0,0x49,0} }, { {     0x4A,0},{     0xF0,0x4A,0} }, { {     0x59,0},{     0xF0,0x59,0} }, { {     0x7C,0},{     0xF0,0x7C,0} },        /*034*/
    { {     0x11,0},{     0xF0,0x11,0} }, { {     0x29,0},{     0xF0,0x29,0} }, { {     0x58,0},{     0xF0,0x58,0} }, { {     0x05,0},{     0xF0,0x05,0} },        /*038*/
    { {     0x06,0},{     0xF0,0x06,0} }, { {     0x04,0},{     0xF0,0x04,0} }, { {     0x0C,0},{     0xF0,0x0C,0} }, { {     0x03,0},{     0xF0,0x03,0} },        /*03c*/
    { {     0x0B,0},{     0xF0,0x0B,0} }, { {     0x83,0},{     0xF0,0x83,0} }, { {     0x0A,0},{     0xF0,0x0A,0} }, { {     0x01,0},{     0xF0,0x01,0} },        /*040*/
    { {     0x09,0},{     0xF0,0x09,0} }, { {     0x77,0},{     0xF0,0x77,0} }, { {     0x7E,0},{     0xF0,0x7E,0} }, { {     0x6C,0},{     0xF0,0x6C,0} },        /*044*/
    { {     0x75,0},{     0xF0,0x75,0} }, { {     0x7D,0},{     0xF0,0x7D,0} }, { {     0x7B,0},{     0xF0,0x7B,0} }, { {     0x6B,0},{     0xF0,0x6B,0} },        /*048*/
    { {     0x73,0},{     0xF0,0x73,0} }, { {     0x74,0},{     0xF0,0x74,0} }, { {     0x79,0},{     0xF0,0x79,0} }, { {     0x69,0},{     0xF0,0x69,0} },        /*04c*/
    { {     0x72,0},{     0xF0,0x72,0} }, { {     0x7A,0},{     0xF0,0x7A,0} }, { {     0x70,0},{     0xF0,0x70,0} }, { {     0x71,0},{     0xF0,0x71,0} },        /*050*/
    { {     0x84,0},{     0xF0,0x84,0} }, { {     0x60,0},{     0xF0,0x60,0} }, { {     0x61,0},{     0xF0,0x61,0} }, { {     0x78,0},{     0xF0,0x78,0} },        /*054*/
    { {     0x07,0},{     0xF0,0x07,0} }, { {     0x0F,0},{     0xF0,0x0F,0} }, { {     0x17,0},{     0xF0,0x17,0} }, { {     0x1F,0},{     0xF0,0x1F,0} },        /*058*/
    { {     0x27,0},{     0xF0,0x27,0} }, { {     0x2F,0},{     0xF0,0x2F,0} }, { {     0x37,0},{     0xF0,0x37,0} }, { {     0x3F,0},{     0xF0,0x3F,0} },        /*05c*/
    { {     0x47,0},{     0xF0,0x47,0} }, { {     0x4F,0},{     0xF0,0x4F,0} }, { {     0x56,0},{     0xF0,0x56,0} }, { {     0x5E,0},{     0xF0,0x5E,0} },        /*060*/
    { {     0x08,0},{     0xF0,0x08,0} }, { {     0x10,0},{     0xF0,0x10,0} }, { {     0x18,0},{     0xF0,0x18,0} }, { {     0x20,0},{     0xF0,0x20,0} },        /*064*/
    { {     0x28,0},{     0xF0,0x28,0} }, { {     0x30,0},{     0xF0,0x30,0} }, { {     0x38,0},{     0xF0,0x38,0} }, { {     0x40,0},{     0xF0,0x40,0} },        /*068*/
    { {     0x48,0},{     0xF0,0x48,0} }, { {     0x50,0},{     0xF0,0x50,0} }, { {     0x57,0},{     0xF0,0x57,0} }, { {     0x6F,0},{     0xF0,0x6F,0} },        /*06c*/
    { {     0x13,0},{     0xF0,0x13,0} }, { {     0x19,0},{     0xF0,0x19,0} }, { {     0x39,0},{     0xF0,0x39,0} }, { {     0x51,0},{     0xF0,0x51,0} },        /*070*/
    { {     0x53,0},{     0xF0,0x53,0} }, { {     0x5C,0},{     0xF0,0x5C,0} }, { {     0x5F,0},{     0xF0,0x5F,0} }, { {     0x62,0},{     0xF0,0x62,0} },        /*074*/
    { {     0x63,0},{     0xF0,0x63,0} }, { {     0x64,0},{     0xF0,0x64,0} }, { {     0x65,0},{     0xF0,0x65,0} }, { {     0x67,0},{     0xF0,0x67,0} },        /*078*/
    { {     0x68,0},{     0xF0,0x68,0} }, { {     0x6A,0},{     0xF0,0x6A,0} }, { {     0x6D,0},{     0xF0,0x6D,0} }, { {     0x6E,0},{     0xF0,0x6E,0} },        /*07c*/

    { {     0x80,0},{     0xf0,0x80,0} }, { {     0x81,0},{     0xf0,0x81,0} }, { {     0x82,0},{     0xf0,0x82,0} }, { {          0},{               0} },        /*080*/
    { {          0},{               0} }, { {     0x85,0},{     0xf0,0x54,0} }, { {     0x86,0},{     0xf0,0x86,0} }, { {     0x87,0},{     0xf0,0x87,0} },        /*084*/
    { {     0x88,0},{     0xf0,0x88,0} }, { {     0x89,0},{     0xf0,0x89,0} }, { {     0x8a,0},{     0xf0,0x8a,0} }, { {     0x8b,0},{     0xf0,0x8b,0} },        /*088*/
    { {     0x8c,0},{     0xf0,0x8c,0} }, { {     0x8d,0},{     0xf0,0x8d,0} }, { {     0x8e,0},{     0xf0,0x8e,0} }, { {     0x8f,0},{     0xf0,0x8f,0} },        /*08c*/
    { {     0x90,0},{     0xf0,0x90,0} }, { {     0x91,0},{     0xf0,0x91,0} }, { {     0x92,0},{     0xf0,0x92,0} }, { {     0x93,0},{     0xf0,0x93,0} },        /*090*/
    { {     0x94,0},{     0xf0,0x94,0} }, { {     0x95,0},{     0xf0,0x95,0} }, { {     0x96,0},{     0xf0,0x96,0} }, { {     0x97,0},{     0xf0,0x97,0} },        /*094*/
    { {     0x98,0},{     0xf0,0x98,0} }, { {     0x99,0},{     0xf0,0x99,0} }, { {     0x9a,0},{     0xf0,0x9a,0} }, { {     0x9b,0},{     0xf0,0x9b,0} },        /*098*/
    { {     0x9c,0},{     0xf0,0x9c,0} }, { {     0x9d,0},{     0xf0,0x9d,0} }, { {     0x9e,0},{     0xf0,0x9e,0} }, { {     0x9f,0},{     0xf0,0x9f,0} },        /*09c*/
    { {     0xa0,0},{     0xf0,0xa0,0} }, { {     0xa1,0},{     0xf0,0xa1,0} }, { {     0xa2,0},{     0xf0,0xa2,0} }, { {     0xa3,0},{     0xf0,0xa3,0} },        /*0a0*/
    { {     0xa4,0},{     0xf0,0xa4,0} }, { {     0xa5,0},{     0xf0,0xa5,0} }, { {     0xa6,0},{     0xf0,0xa6,0} }, { {     0xa7,0},{     0xf0,0xa7,0} },        /*0a4*/
    { {     0xa8,0},{     0xf0,0xa8,0} }, { {     0xa9,0},{     0xf0,0xa9,0} }, { {     0xaa,0},{     0xf0,0xaa,0} }, { {     0xab,0},{     0xf0,0xab,0} },        /*0a8*/
    { {     0xac,0},{     0xf0,0xac,0} }, { {     0xad,0},{     0xf0,0xad,0} }, { {     0xae,0},{     0xf0,0xae,0} }, { {     0xaf,0},{     0xf0,0xaf,0} },        /*0ac*/
    { {     0xb0,0},{     0xf0,0xb0,0} }, { {     0xb1,0},{     0xf0,0xb1,0} }, { {     0xb2,0},{     0xf0,0xb2,0} }, { {     0xb3,0},{     0xf0,0xb3,0} },        /*0b0*/
    { {     0xb4,0},{     0xf0,0xb4,0} }, { {     0xb5,0},{     0xf0,0xb5,0} }, { {     0xb6,0},{     0xf0,0xb6,0} }, { {     0xb7,0},{     0xf0,0xb7,0} },        /*0b4*/
    { {     0xb8,0},{     0xf0,0xb8,0} }, { {     0xb9,0},{     0xf0,0xb9,0} }, { {     0xba,0},{     0xf0,0xba,0} }, { {     0xbb,0},{     0xf0,0xbb,0} },        /*0b8*/
    { {     0xbc,0},{     0xf0,0xbc,0} }, { {     0xbd,0},{     0xf0,0xbd,0} }, { {     0xbe,0},{     0xf0,0xbe,0} }, { {     0xbf,0},{     0xf0,0xbf,0} },        /*0bc*/
    { {     0xc0,0},{     0xf0,0xc0,0} }, { {     0xc1,0},{     0xf0,0xc1,0} }, { {     0xc2,0},{     0xf0,0xc2,0} }, { {     0xc3,0},{     0xf0,0xc3,0} },        /*0c0*/
    { {     0xc4,0},{     0xf0,0xc4,0} }, { {     0xc5,0},{     0xf0,0xc5,0} }, { {     0xc6,0},{     0xf0,0xc6,0} }, { {     0xc7,0},{     0xf0,0xc7,0} },        /*0c4*/
    { {     0xc8,0},{     0xf0,0xc8,0} }, { {     0xc9,0},{     0xf0,0xc9,0} }, { {     0xca,0},{     0xf0,0xca,0} }, { {     0xcb,0},{     0xf0,0xcb,0} },        /*0c8*/
    { {     0xcc,0},{     0xf0,0xcc,0} }, { {     0xcd,0},{     0xf0,0xcd,0} }, { {     0xce,0},{     0xf0,0xce,0} }, { {     0xcf,0},{     0xf0,0xcf,0} },        /*0cc*/
    { {     0xd0,0},{     0xf0,0xd0,0} }, { {     0xd1,0},{     0xf0,0xd0,0} }, { {     0xd2,0},{     0xf0,0xd2,0} }, { {     0xd3,0},{     0xf0,0xd3,0} },        /*0d0*/
    { {     0xd4,0},{     0xf0,0xd4,0} }, { {     0xd5,0},{     0xf0,0xd5,0} }, { {     0xd6,0},{     0xf0,0xd6,0} }, { {     0xd7,0},{     0xf0,0xd7,0} },        /*0d4*/
    { {     0xd8,0},{     0xf0,0xd8,0} }, { {     0xd9,0},{     0xf0,0xd9,0} }, { {     0xda,0},{     0xf0,0xda,0} }, { {     0xdb,0},{     0xf0,0xdb,0} },        /*0d8*/
    { {     0xdc,0},{     0xf0,0xdc,0} }, { {     0xdd,0},{     0xf0,0xdd,0} }, { {     0xde,0},{     0xf0,0xde,0} }, { {     0xdf,0},{     0xf0,0xdf,0} },        /*0dc*/
    { {     0xe0,0},{     0xf0,0xe0,0} }, { {     0xe1,0},{     0xf0,0xe1,0} }, { {     0xe2,0},{     0xf0,0xe2,0} }, { {     0xe3,0},{     0xf0,0xe3,0} },        /*0e0*/
    { {     0xe4,0},{     0xf0,0xe4,0} }, { {     0xe5,0},{     0xf0,0xe5,0} }, { {     0xe6,0},{     0xf0,0xe6,0} }, { {     0xe7,0},{     0xf0,0xe7,0} },        /*0e4*/
    { {     0xe8,0},{     0xf0,0xe8,0} }, { {     0xe9,0},{     0xf0,0xe9,0} }, { {     0xea,0},{     0xf0,0xea,0} }, { {     0xeb,0},{     0xf0,0xeb,0} },        /*0e8*/
    { {     0xec,0},{     0xf0,0xec,0} }, { {     0xed,0},{     0xf0,0xed,0} }, { {     0xee,0},{     0xf0,0xee,0} }, { {     0xef,0},{     0xf0,0xef,0} },        /*0ec*/
    { {          0},{               0} }, { {     0xf1,0},{     0xf0,0xf1,0} }, { {     0xf2,0},{     0xf0,0xf2,0} }, { {     0xf3,0},{     0xf0,0xf3,0} },        /*0f0*/
    { {     0xf4,0},{     0xf0,0xf4,0} }, { {     0xf5,0},{     0xf0,0xf5,0} }, { {     0xf6,0},{     0xf0,0xf6,0} }, { {     0xf7,0},{     0xf0,0xf7,0} },        /*0f4*/
    { {     0xf8,0},{     0xf0,0xf8,0} }, { {     0xf9,0},{     0xf0,0xf9,0} }, { {     0xfa,0},{     0xf0,0xfa,0} }, { {     0xfb,0},{     0xf0,0xfb,0} },        /*0f8*/
    { {     0xfc,0},{     0xf0,0xfc,0} }, { {     0xfd,0},{     0xf0,0xfd,0} }, { {     0xfe,0},{     0xf0,0xfe,0} }, { {     0xff,0},{     0xf0,0xff,0} },        /*0fc*/

    { {0xe1,0x14,0},{0xe1,0xf0,0x14,0} }, { {0xe0,0x76,0},{0xe0,0xF0,0x76,0} }, { {0xe0,0x16,0},{0xe0,0xF0,0x16,0} }, { {0xe0,0x1E,0},{0xe0,0xF0,0x1E,0} },        /*100*/
    { {0xe0,0x26,0},{0xe0,0xF0,0x26,0} }, { {0xe0,0x25,0},{0xe0,0xF0,0x25,0} }, { {0xe0,0x2E,0},{0xe0,0xF0,0x2E,0} }, { {0xe0,0x36,0},{0xe0,0xF0,0x36,0} },        /*104*/
    { {0xe0,0x3D,0},{0xe0,0xF0,0x3D,0} }, { {0xe0,0x3E,0},{0xe0,0xF0,0x3E,0} }, { {0xe0,0x46,0},{0xe0,0xF0,0x46,0} }, { {0xe0,0x45,0},{0xe0,0xF0,0x45,0} },        /*108*/
    { {0xe0,0x4E,0},{0xe0,0xF0,0x4E,0} }, { {          0},{               0} }, { {0xe0,0x66,0},{0xe0,0xF0,0x66,0} }, { {0xe0,0x0D,0},{0xe0,0xF0,0x0D,0} },        /*10c*/
    { {0xe0,0x15,0},{0xe0,0xF0,0x15,0} }, { {0xe0,0x1D,0},{0xe0,0xF0,0x1D,0} }, { {0xe0,0x24,0},{0xe0,0xF0,0x24,0} }, { {0xe0,0x2D,0},{0xe0,0xF0,0x2D,0} },        /*110*/
    { {0xe0,0x2C,0},{0xe0,0xF0,0x2C,0} }, { {0xe0,0x35,0},{0xe0,0xF0,0x35,0} }, { {0xe0,0x3C,0},{0xe0,0xF0,0x3C,0} }, { {0xe0,0x43,0},{0xe0,0xF0,0x43,0} },        /*114*/
    { {0xe0,0x44,0},{0xe0,0xF0,0x44,0} }, { {0xe0,0x4D,0},{0xe0,0xF0,0x4D,0} }, { {0xe0,0x54,0},{0xe0,0xF0,0x54,0} }, { {0xe0,0x5B,0},{0xe0,0xF0,0x5B,0} },        /*118*/
    { {0xe0,0x5A,0},{0xe0,0xF0,0x5A,0} }, { {0xe0,0x14,0},{0xe0,0xF0,0x14,0} }, { {0xe0,0x1C,0},{0xe0,0xF0,0x1C,0} }, { {0xe0,0x1B,0},{0xe0,0xF0,0x1B,0} },        /*11c*/
    { {0xe0,0x23,0},{0xe0,0xF0,0x23,0} }, { {0xe0,0x2B,0},{0xe0,0xF0,0x2B,0} }, { {0xe0,0x34,0},{0xe0,0xF0,0x34,0} }, { {0xe0,0x33,0},{0xe0,0xF0,0x33,0} },        /*120*/
    { {0xe0,0x3B,0},{0xe0,0xF0,0x3B,0} }, { {0xe0,0x42,0},{0xe0,0xF0,0x42,0} }, { {0xe0,0x4B,0},{0xe0,0xF0,0x4B,0} }, { {          0},{               0} },        /*124*/
    { {          0},{               0} }, { {          0},{               0} }, { {          0},{               0} }, { {          0},{               0} },        /*128*/
    { {0xe0,0x1A,0},{0xe0,0xF0,0x1A,0} }, { {0xe0,0x22,0},{0xe0,0xF0,0x22,0} }, { {0xe0,0x21,0},{0xe0,0xF0,0x21,0} }, { {0xe0,0x2A,0},{0xe0,0xF0,0x2A,0} },        /*12c*/
    { {0xe0,0x32,0},{0xe0,0xF0,0x32,0} }, { {0xe0,0x31,0},{0xe0,0xF0,0x31,0} }, { {0xe0,0x3A,0},{0xe0,0xF0,0x3A,0} }, { {          0},{               0} },        /*130*/
    { {0xe0,0x49,0},{0xe0,0xF0,0x49,0} }, { {0xe0,0x4A,0},{0xe0,0xF0,0x4A,0} }, { {          0},{               0} }, { {0xe0,0x7C,0},{0xe0,0xF0,0x7C,0} },        /*134*/
    { {0xe0,0x11,0},{0xe0,0xF0,0x11,0} }, { {          0},{               0} }, { {0xe0,0x58,0},{0xe0,0xF0,0x58,0} }, { {0xe0,0x05,0},{0xe0,0xF0,0x05,0} },        /*138*/
    { {0xe0,0x06,0},{0xe0,0xF0,0x06,0} }, { {0xe0,0x04,0},{0xe0,0xF0,0x04,0} }, { {0xe0,0x0C,0},{0xe0,0xF0,0x0C,0} }, { {0xe0,0x03,0},{0xe0,0xF0,0x03,0} },        /*13c*/
    { {0xe0,0x0B,0},{0xe0,0xF0,0x0B,0} }, { {0xe0,0x02,0},{0xe0,0xF0,0x02,0} }, { {0xe0,0x0A,0},{0xe0,0xF0,0x0A,0} }, { {0xe0,0x01,0},{0xe0,0xF0,0x01,0} },        /*140*/
    { {0xe0,0x09,0},{0xe0,0xF0,0x09,0} }, { {          0},{               0} }, { {0xe0,0x7E,0},{0xe0,0xF0,0x7E,0} }, { {0xe0,0x6C,0},{0xe0,0xF0,0x6C,0} },        /*144*/
    { {0xe0,0x75,0},{0xe0,0xF0,0x75,0} }, { {0xe0,0x7D,0},{0xe0,0xF0,0x7D,0} }, { {          0},{               0} }, { {0xe0,0x6B,0},{0xe0,0xF0,0x6B,0} },        /*148*/
    { {0xe0,0x73,0},{0xe0,0xF0,0x73,0} }, { {0xe0,0x74,0},{0xe0,0xF0,0x74,0} }, { {0xe0,0x79,0},{0xe0,0xF0,0x79,0} }, { {0xe0,0x69,0},{0xe0,0xF0,0x69,0} },        /*14c*/
    { {0xe0,0x72,0},{0xe0,0xF0,0x72,0} }, { {0xe0,0x7A,0},{0xe0,0xF0,0x7A,0} }, { {0xe0,0x70,0},{0xe0,0xF0,0x70,0} }, { {0xe0,0x71,0},{0xe0,0xF0,0x71,0} },        /*150*/
    { {          0},{               0} }, { {0xe0,0x60,0},{0xe0,0xF0,0x60,0} }, { {          0},{               0} }, { {0xe0,0x78,0},{0xe0,0xF0,0x78,0} },        /*154*/
    { {0xe0,0x07,0},{0xe0,0xF0,0x07,0} }, { {0xe0,0x0F,0},{0xe0,0xF0,0x0F,0} }, { {0xe0,0x17,0},{0xe0,0xF0,0x17,0} }, { {0xe0,0x1F,0},{0xe0,0xF0,0x1F,0} },        /*158*/
    { {0xe0,0x27,0},{0xe0,0xF0,0x27,0} }, { {0xe0,0x2F,0},{0xe0,0xF0,0x2F,0} }, { {0xe0,0x37,0},{0xe0,0xF0,0x37,0} }, { {0xe0,0x3F,0},{0xe0,0xF0,0x3F,0} },        /*15c*/
    { {          0},{               0} }, { {0xe0,0x4F,0},{0xe0,0xF0,0x4F,0} }, { {0xe0,0x56,0},{0xe0,0xF0,0x56,0} }, { {0xe0,0x5E,0},{0xe0,0xF0,0x5E,0} },        /*160*/
    { {0xe0,0x08,0},{0xe0,0xF0,0x08,0} }, { {0xe0,0x10,0},{0xe0,0xF0,0x10,0} }, { {0xe0,0x18,0},{0xe0,0xF0,0x18,0} }, { {0xe0,0x20,0},{0xe0,0xF0,0x20,0} },        /*164*/
    { {0xe0,0x28,0},{0xe0,0xF0,0x28,0} }, { {0xe0,0x30,0},{0xe0,0xF0,0x30,0} }, { {0xe0,0x38,0},{0xe0,0xF0,0x38,0} }, { {0xe0,0x40,0},{0xe0,0xF0,0x40,0} },        /*168*/
    { {0xe0,0x48,0},{0xe0,0xF0,0x48,0} }, { {0xe0,0x50,0},{0xe0,0xF0,0x50,0} }, { {0xe0,0x57,0},{0xe0,0xF0,0x57,0} }, { {          0},{               0} },        /*16c*/
    { {0xe0,0x13,0},{0xe0,0xF0,0x13,0} }, { {0xe0,0x19,0},{0xe0,0xF0,0x19,0} }, { {0xe0,0x39,0},{0xe0,0xF0,0x39,0} }, { {0xe0,0x51,0},{0xe0,0xF0,0x51,0} },        /*170*/
    { {0xe0,0x53,0},{0xe0,0xF0,0x53,0} }, { {0xe0,0x5C,0},{0xe0,0xF0,0x5C,0} }, { {          0},{               0} }, { {0xe0,0x62,0},{0xe0,0xF0,0x62,0} },        /*174*/
    { {0xe0,0x63,0},{0xe0,0xF0,0x63,0} }, { {0xe0,0x64,0},{0xe0,0xF0,0x64,0} }, { {0xe0,0x65,0},{0xe0,0xF0,0x65,0} }, { {0xe0,0x67,0},{0xe0,0xF0,0x67,0} },        /*178*/
    { {0xe0,0x68,0},{0xe0,0xF0,0x68,0} }, { {0xe0,0x6A,0},{0xe0,0xF0,0x6A,0} }, { {0xe0,0x6D,0},{0xe0,0xF0,0x6D,0} }, { {0xe0,0x6E,0},{0xe0,0xF0,0x6E,0} },        /*17c*/

    { {          0},{               0} }, { {          0},{               0} }, { {          0},{               0} }, { {          0},{               0} },        /*180*/
    { {          0},{               0} }, { {          0},{               0} }, { {          0},{               0} }, { {          0},{               0} },        /*184*/
    { {          0},{               0} }, { {          0},{               0} }, { {          0},{               0} }, { {          0},{               0} },        /*188*/
    { {          0},{               0} }, { {          0},{               0} }, { {          0},{               0} }, { {          0},{               0} },        /*18c*/
    { {          0},{               0} }, { {          0},{               0} }, { {          0},{               0} }, { {          0},{               0} },        /*190*/
    { {          0},{               0} }, { {          0},{               0} }, { {          0},{               0} }, { {          0},{               0} },        /*194*/
    { {          0},{               0} }, { {          0},{               0} }, { {          0},{               0} }, { {          0},{               0} },        /*198*/
    { {          0},{               0} }, { {          0},{               0} }, { {          0},{               0} }, { {          0},{               0} },        /*19c*/
    { {          0},{               0} }, { {          0},{               0} }, { {          0},{               0} }, { {          0},{               0} },        /*1a0*/
    { {          0},{               0} }, { {          0},{               0} }, { {          0},{               0} }, { {          0},{               0} },        /*1a4*/
    { {          0},{               0} }, { {          0},{               0} }, { {          0},{               0} }, { {          0},{               0} },        /*1a8*/
    { {          0},{               0} }, { {          0},{               0} }, { {          0},{               0} }, { {          0},{               0} },        /*1ac*/
    { {          0},{               0} }, { {          0},{               0} }, { {          0},{               0} }, { {          0},{               0} },        /*1c0*/
    { {          0},{               0} }, { {          0},{               0} }, { {          0},{               0} }, { {          0},{               0} },        /*1c4*/
    { {          0},{               0} }, { {          0},{               0} }, { {          0},{               0} }, { {          0},{               0} },        /*1c8*/
    { {          0},{               0} }, { {          0},{               0} }, { {          0},{               0} }, { {          0},{               0} },        /*1cc*/
    { {          0},{               0} }, { {          0},{               0} }, { {          0},{               0} }, { {          0},{               0} },        /*1d0*/
    { {          0},{               0} }, { {          0},{               0} }, { {          0},{               0} }, { {          0},{               0} },        /*1d4*/
    { {          0},{               0} }, { {          0},{               0} }, { {          0},{               0} }, { {          0},{               0} },        /*1d8*/
    { {          0},{               0} }, { {          0},{               0} }, { {          0},{               0} }, { {          0},{               0} },        /*1dc*/
    { {          0},{               0} }, { {0xe0,0xe1,0},{0xe0,0xF0,0xE1,0} }, { {          0},{               0} }, { {          0},{               0} },        /*1e0*/
    { {          0},{               0} }, { {          0},{               0} }, { {          0},{               0} }, { {          0},{               0} },        /*1e4*/
    { {          0},{               0} }, { {          0},{               0} }, { {          0},{               0} }, { {          0},{               0} },        /*1e8*/
    { {          0},{               0} }, { {          0},{               0} }, { {0xe0,0xee,0},{0xe0,0xF0,0xEE,0} }, { {          0},{               0} },        /*1ec*/
    { {          0},{               0} }, { {0xe0,0xf1,0},{0xe0,0xF0,0xF1,0} }, { {          0},{               0} }, { {          0},{               0} },        /*1f0*/
    { {          0},{               0} }, { {          0},{               0} }, { {          0},{               0} }, { {          0},{               0} },        /*1f4*/
    { {          0},{               0} }, { {          0},{               0} }, { {          0},{               0} }, { {          0},{               0} },        /*1f8*/
    { {          0},{               0} }, { {          0},{               0} }, { {0xe0,0xfe,0},{0xe0,0xF0,0xFE,0} }, { {0xe0,0xff,0},{0xe0,0xF0,0xFF,0} }         /*1fc*/
  // clang-format on
};

static const scancode scancode_set3[512] = {
  // clang-format off
    { {          0},{               0} }, { {     0x08,0},{     0xf0,0x08,0} }, { {     0x16,0},{     0xf0,0x16,0} }, { {     0x1E,0},{     0xf0,0x1E,0} },        /*000*/
    { {     0x26,0},{     0xf0,0x26,0} }, { {     0x25,0},{     0xf0,0x25,0} }, { {     0x2E,0},{     0xf0,0x2E,0} }, { {     0x36,0},{     0xf0,0x36,0} },        /*004*/
    { {     0x3D,0},{     0xf0,0x3D,0} }, { {     0x3E,0},{     0xf0,0x3E,0} }, { {     0x46,0},{     0xf0,0x46,0} }, { {     0x45,0},{     0xf0,0x45,0} },        /*008*/
    { {     0x4E,0},{     0xf0,0x4E,0} }, { {     0x55,0},{     0xf0,0x55,0} }, { {     0x66,0},{     0xf0,0x66,0} }, { {     0x0D,0},{     0xf0,0x0D,0} },        /*00c*/
    { {     0x15,0},{     0xf0,0x15,0} }, { {     0x1D,0},{     0xf0,0x1D,0} }, { {     0x24,0},{     0xf0,0x24,0} }, { {     0x2D,0},{     0xf0,0x2D,0} },        /*010*/
    { {     0x2C,0},{     0xf0,0x2C,0} }, { {     0x35,0},{     0xf0,0x35,0} }, { {     0x3C,0},{     0xf0,0x3C,0} }, { {     0x43,0},{     0xf0,0x43,0} },        /*014*/
    { {     0x44,0},{     0xf0,0x44,0} }, { {     0x4D,0},{     0xf0,0x4D,0} }, { {     0x54,0},{     0xf0,0x54,0} }, { {     0x5B,0},{     0xf0,0x5B,0} },        /*018*/
    { {     0x5A,0},{     0xf0,0x5A,0} }, { {     0x11,0},{     0xf0,0x11,0} }, { {     0x1C,0},{     0xf0,0x1C,0} }, { {     0x1B,0},{     0xf0,0x1B,0} },        /*01c*/
    { {     0x23,0},{     0xf0,0x23,0} }, { {     0x2B,0},{     0xf0,0x2B,0} }, { {     0x34,0},{     0xf0,0x34,0} }, { {     0x33,0},{     0xf0,0x33,0} },        /*020*/
    { {     0x3B,0},{     0xf0,0x3B,0} }, { {     0x42,0},{     0xf0,0x42,0} }, { {     0x4B,0},{     0xf0,0x4B,0} }, { {     0x4C,0},{     0xf0,0x4C,0} },        /*024*/
    { {     0x52,0},{     0xf0,0x52,0} }, { {     0x0E,0},{     0xf0,0x0E,0} }, { {     0x12,0},{     0xf0,0x12,0} }, { {     0x5C,0},{     0xf0,0x5C,0} },        /*028*/
    { {     0x1A,0},{     0xf0,0x1A,0} }, { {     0x22,0},{     0xf0,0x22,0} }, { {     0x21,0},{     0xf0,0x21,0} }, { {     0x2A,0},{     0xf0,0x2A,0} },        /*02c*/
    { {     0x32,0},{     0xf0,0x32,0} }, { {     0x31,0},{     0xf0,0x31,0} }, { {     0x3A,0},{     0xf0,0x3A,0} }, { {     0x41,0},{     0xf0,0x41,0} },        /*030*/
    { {     0x49,0},{     0xf0,0x49,0} }, { {     0x4A,0},{     0xf0,0x4A,0} }, { {     0x59,0},{     0xf0,0x59,0} }, { {     0x7E,0},{     0xf0,0x7E,0} },        /*034*/
    { {     0x19,0},{     0xf0,0x19,0} }, { {     0x29,0},{     0xf0,0x29,0} }, { {     0x14,0},{     0xf0,0x14,0} }, { {     0x07,0},{     0xf0,0x07,0} },        /*038*/
    { {     0x0F,0},{     0xf0,0x0F,0} }, { {     0x17,0},{     0xf0,0x17,0} }, { {     0x1F,0},{     0xf0,0x1F,0} }, { {     0x27,0},{     0xf0,0x27,0} },        /*03c*/
    { {     0x2F,0},{     0xf0,0x2F,0} }, { {     0x37,0},{     0xf0,0x37,0} }, { {     0x3F,0},{     0xf0,0x3F,0} }, { {     0x47,0},{     0xf0,0x47,0} },        /*040*/
    { {     0x4F,0},{     0xf0,0x4F,0} }, { {     0x76,0},{     0xf0,0x76,0} }, { {     0x5F,0},{     0xf0,0x5F,0} }, { {     0x6C,0},{     0xf0,0x6C,0} },        /*044*/
    { {     0x75,0},{     0xf0,0x75,0} }, { {     0x7D,0},{     0xf0,0x7D,0} }, { {     0x84,0},{     0xf0,0x84,0} }, { {     0x6B,0},{     0xf0,0x6B,0} },        /*048*/
    { {     0x73,0},{     0xf0,0x73,0} }, { {     0x74,0},{     0xf0,0x74,0} }, { {     0x7C,0},{     0xf0,0x7C,0} }, { {     0x69,0},{     0xf0,0x69,0} },        /*04c*/
    { {     0x72,0},{     0xf0,0x72,0} }, { {     0x7A,0},{     0xf0,0x7A,0} }, { {     0x70,0},{     0xf0,0x70,0} }, { {     0x71,0},{     0xf0,0x71,0} },        /*050*/
    { {     0x57,0},{     0xf0,0x57,0} }, { {     0x60,0},{     0xf0,0x60,0} }, { {          0},{               0} }, { {     0x56,0},{     0xf0,0x56,0} },        /*054*/
    { {     0x5E,0},{     0xf0,0x5E,0} }, { {          0},{               0} }, { {          0},{               0} }, { {          0},{               0} },        /*058*/
    { {          0},{               0} }, { {          0},{               0} }, { {          0},{               0} }, { {          0},{               0} },        /*05c*/
    { {          0},{               0} }, { {          0},{               0} }, { {          0},{               0} }, { {          0},{               0} },        /*060*/
    { {          0},{               0} }, { {     0x10,0},{     0xf0,0x10,0} }, { {     0x18,0},{     0xf0,0x18,0} }, { {     0x20,0},{     0xf0,0x20,0} },        /*064*/
    { {     0x28,0},{     0xf0,0x28,0} }, { {     0x30,0},{     0xf0,0x30,0} }, { {     0x38,0},{     0xf0,0x38,0} }, { {     0x40,0},{     0xf0,0x40,0} },        /*068*/
    { {     0x48,0},{     0xf0,0x48,0} }, { {     0x50,0},{     0xf0,0x50,0} }, { {          0},{               0} }, { {          0},{               0} },        /*06c*/
    { {     0x87,0},{     0xf0,0x87,0} }, { {          0},{               0} }, { {          0},{               0} }, { {     0x51,0},{     0xf0,0x51,0} },        /*070*/
    { {     0x53,0},{     0xf0,0x53,0} }, { {     0x5C,0},{     0xf0,0x5C,0} }, { {          0},{               0} }, { {     0x62,0},{     0xf0,0x62,0} },        /*074*/
    { {     0x63,0},{     0xf0,0x63,0} }, { {     0x86,0},{     0xf0,0x86,0} }, { {          0},{               0} }, { {     0x85,0},{     0xf0,0x85,0} },        /*078*/
    { {     0x68,0},{     0xf0,0x68,0} }, { {     0x13,0},{     0xf0,0x13,0} }, { {          0},{               0} }, { {          0},{               0} },        /*07c*/

    { {     0x80,0},{     0xf0,0x80,0} }, { {     0x81,0},{     0xf0,0x81,0} }, { {     0x82,0},{     0xf0,0x82,0} }, { {          0},{               0} },        /*080*/
    { {          0},{               0} }, { {     0x85,0},{     0xf0,0x54,0} }, { {     0x86,0},{     0xf0,0x86,0} }, { {     0x87,0},{     0xf0,0x87,0} },        /*084*/
    { {     0x88,0},{     0xf0,0x88,0} }, { {     0x89,0},{     0xf0,0x89,0} }, { {     0x8a,0},{     0xf0,0x8a,0} }, { {     0x8b,0},{     0xf0,0x8b,0} },        /*088*/
    { {          0},{               0} }, { {          0},{               0} }, { {     0x8e,0},{     0xf0,0x8e,0} }, { {     0x8f,0},{     0xf0,0x8f,0} },        /*08c*/
    { {     0x90,0},{     0xf0,0x90,0} }, { {     0x91,0},{     0xf0,0x91,0} }, { {     0x92,0},{     0xf0,0x92,0} }, { {     0x93,0},{     0xf0,0x93,0} },        /*090*/
    { {     0x94,0},{     0xf0,0x94,0} }, { {     0x95,0},{     0xf0,0x95,0} }, { {     0x96,0},{     0xf0,0x96,0} }, { {     0x97,0},{     0xf0,0x97,0} },        /*094*/
    { {     0x98,0},{     0xf0,0x98,0} }, { {     0x99,0},{     0xf0,0x99,0} }, { {     0x9a,0},{     0xf0,0x9a,0} }, { {     0x9b,0},{     0xf0,0x9b,0} },        /*098*/
    { {     0x9c,0},{     0xf0,0x9c,0} }, { {     0x9d,0},{     0xf0,0x9d,0} }, { {     0x9e,0},{     0xf0,0x9e,0} }, { {     0x9f,0},{     0xf0,0x9f,0} },        /*09c*/
    { {     0xa0,0},{     0xf0,0xa0,0} }, { {     0xa1,0},{     0xf0,0xa1,0} }, { {     0xa2,0},{     0xf0,0xa2,0} }, { {     0xa3,0},{     0xf0,0xa3,0} },        /*0a0*/
    { {     0xa4,0},{     0xf0,0xa4,0} }, { {     0xa5,0},{     0xf0,0xa5,0} }, { {     0xa6,0},{     0xf0,0xa6,0} }, { {     0xa7,0},{     0xf0,0xa7,0} },        /*0a4*/
    { {     0xa8,0},{     0xf0,0xa8,0} }, { {     0xa9,0},{     0xf0,0xa9,0} }, { {     0xaa,0},{     0xf0,0xaa,0} }, { {     0xab,0},{     0xf0,0xab,0} },        /*0a8*/
    { {     0xac,0},{     0xf0,0xac,0} }, { {     0xad,0},{     0xf0,0xad,0} }, { {     0xae,0},{     0xf0,0xae,0} }, { {     0xaf,0},{     0xf0,0xaf,0} },        /*0ac*/
    { {     0xb0,0},{     0xf0,0xb0,0} }, { {     0xb1,0},{     0xf0,0xb1,0} }, { {     0xb2,0},{     0xf0,0xb2,0} }, { {     0xb3,0},{     0xf0,0xb3,0} },        /*0b0*/
    { {     0xb4,0},{     0xf0,0xb4,0} }, { {     0xb5,0},{     0xf0,0xb5,0} }, { {     0xb6,0},{     0xf0,0xb6,0} }, { {     0xb7,0},{     0xf0,0xb7,0} },        /*0b4*/
    { {     0xb8,0},{     0xf0,0xb8,0} }, { {     0xb9,0},{     0xf0,0xb9,0} }, { {     0xba,0},{     0xf0,0xba,0} }, { {     0xbb,0},{     0xf0,0xbb,0} },        /*0b8*/
    { {     0xbc,0},{     0xf0,0xbc,0} }, { {     0xbd,0},{     0xf0,0xbd,0} }, { {     0xbe,0},{     0xf0,0xbe,0} }, { {     0xbf,0},{     0xf0,0xbf,0} },        /*0bc*/
    { {     0xc0,0},{     0xf0,0xc0,0} }, { {     0xc1,0},{     0xf0,0xc1,0} }, { {     0xc2,0},{     0xf0,0xc2,0} }, { {     0xc3,0},{     0xf0,0xc3,0} },        /*0c0*/
    { {     0xc4,0},{     0xf0,0xc4,0} }, { {     0xc5,0},{     0xf0,0xc5,0} }, { {     0xc6,0},{     0xf0,0xc6,0} }, { {     0xc7,0},{     0xf0,0xc7,0} },        /*0c4*/
    { {     0xc8,0},{     0xf0,0xc8,0} }, { {     0xc9,0},{     0xf0,0xc9,0} }, { {     0xca,0},{     0xf0,0xca,0} }, { {     0xcb,0},{     0xf0,0xcb,0} },        /*0c8*/
    { {     0xcc,0},{     0xf0,0xcc,0} }, { {     0xcd,0},{     0xf0,0xcd,0} }, { {     0xce,0},{     0xf0,0xce,0} }, { {     0xcf,0},{     0xf0,0xcf,0} },        /*0cc*/
    { {     0xd0,0},{     0xf0,0xd0,0} }, { {     0xd1,0},{     0xf0,0xd0,0} }, { {     0xd2,0},{     0xf0,0xd2,0} }, { {     0xd3,0},{     0xf0,0xd3,0} },        /*0d0*/
    { {     0xd4,0},{     0xf0,0xd4,0} }, { {     0xd5,0},{     0xf0,0xd5,0} }, { {     0xd6,0},{     0xf0,0xd6,0} }, { {     0xd7,0},{     0xf0,0xd7,0} },        /*0d4*/
    { {     0xd8,0},{     0xf0,0xd8,0} }, { {     0xd9,0},{     0xf0,0xd9,0} }, { {     0xda,0},{     0xf0,0xda,0} }, { {     0xdb,0},{     0xf0,0xdb,0} },        /*0d8*/
    { {     0xdc,0},{     0xf0,0xdc,0} }, { {     0xdd,0},{     0xf0,0xdd,0} }, { {     0xde,0},{     0xf0,0xde,0} }, { {     0xdf,0},{     0xf0,0xdf,0} },        /*0dc*/
    { {     0xe0,0},{     0xf0,0xe0,0} }, { {     0xe1,0},{     0xf0,0xe1,0} }, { {     0xe2,0},{     0xf0,0xe2,0} }, { {     0xe3,0},{     0xf0,0xe3,0} },        /*0e0*/
    { {     0xe4,0},{     0xf0,0xe4,0} }, { {     0xe5,0},{     0xf0,0xe5,0} }, { {     0xe6,0},{     0xf0,0xe6,0} }, { {     0xe7,0},{     0xf0,0xe7,0} },        /*0e4*/
    { {     0xe8,0},{     0xf0,0xe8,0} }, { {     0xe9,0},{     0xf0,0xe9,0} }, { {     0xea,0},{     0xf0,0xea,0} }, { {     0xeb,0},{     0xf0,0xeb,0} },        /*0e8*/
    { {     0xec,0},{     0xf0,0xec,0} }, { {     0xed,0},{     0xf0,0xed,0} }, { {     0xee,0},{     0xf0,0xee,0} }, { {     0xef,0},{     0xf0,0xef,0} },        /*0ec*/
    { {          0},{               0} }, { {     0xf1,0},{     0xf0,0xf1,0} }, { {     0xf2,0},{     0xf0,0xf2,0} }, { {     0xf3,0},{     0xf0,0xf3,0} },        /*0f0*/
    { {     0xf4,0},{     0xf0,0xf4,0} }, { {     0xf5,0},{     0xf0,0xf5,0} }, { {     0xf6,0},{     0xf0,0xf6,0} }, { {     0xf7,0},{     0xf0,0xf7,0} },        /*0f4*/
    { {     0xf8,0},{     0xf0,0xf8,0} }, { {     0xf9,0},{     0xf0,0xf9,0} }, { {     0xfa,0},{     0xf0,0xfa,0} }, { {     0xfb,0},{     0xf0,0xfb,0} },        /*0f8*/
    { {     0xfc,0},{     0xf0,0xfc,0} }, { {     0xfd,0},{     0xf0,0xfd,0} }, { {     0xfe,0},{     0xf0,0xfe,0} }, { {     0xff,0},{     0xf0,0xff,0} },        /*0fc*/

    { {     0x62,0},{     0xF0,0x62,0} }, { {0xe0,0x76,0},{0xe0,0xF0,0x76,0} }, { {0xe0,0x16,0},{0xe0,0xF0,0x16,0} }, { {0xe0,0x1E,0},{0xe0,0xF0,0x1E,0} },        /*100*/
    { {0xe0,0x26,0},{0xe0,0xF0,0x26,0} }, { {0xe0,0x25,0},{0xe0,0xF0,0x25,0} }, { {0xe0,0x2E,0},{0xe0,0xF0,0x2E,0} }, { {0xe0,0x36,0},{0xe0,0xF0,0x36,0} },        /*104*/
    { {0xe0,0x3D,0},{0xe0,0xF0,0x3D,0} }, { {0xe0,0x3E,0},{0xe0,0xF0,0x3E,0} }, { {0xe0,0x46,0},{0xe0,0xF0,0x46,0} }, { {0xe0,0x45,0},{0xe0,0xF0,0x45,0} },        /*108*/
    { {0xe0,0x4E,0},{0xe0,0xF0,0x4E,0} }, { {          0},{               0} }, { {0xe0,0x66,0},{0xe0,0xF0,0x66,0} }, { {0xe0,0x0D,0},{0xe0,0xF0,0x0D,0} },        /*10c*/
    { {0xe0,0x15,0},{0xe0,0xF0,0x15,0} }, { {0xe0,0x1D,0},{0xe0,0xF0,0x1D,0} }, { {0xe0,0x24,0},{0xe0,0xF0,0x24,0} }, { {0xe0,0x2D,0},{0xe0,0xF0,0x2D,0} },        /*110*/
    { {0xe0,0x2C,0},{0xe0,0xF0,0x2C,0} }, { {0xe0,0x35,0},{0xe0,0xF0,0x35,0} }, { {0xe0,0x3C,0},{0xe0,0xF0,0x3C,0} }, { {0xe0,0x43,0},{0xe0,0xF0,0x43,0} },        /*114*/
    { {0xe0,0x44,0},{0xe0,0xF0,0x44,0} }, { {0xe0,0x4D,0},{0xe0,0xF0,0x4D,0} }, { {0xe0,0x54,0},{0xe0,0xF0,0x54,0} }, { {0xe0,0x5B,0},{0xe0,0xF0,0x5B,0} },        /*118*/
    { {     0x79,0},{     0xf0,0x79,0} }, { {     0x58,0},{     0xf0,0x58,0} }, { {0xe0,0x1C,0},{0xe0,0xF0,0x1C,0} }, { {0xe0,0x1B,0},{0xe0,0xF0,0x1B,0} },        /*11c*/
    { {0xe0,0x23,0},{0xe0,0xF0,0x23,0} }, { {0xe0,0x2B,0},{0xe0,0xF0,0x2B,0} }, { {0xe0,0x34,0},{0xe0,0xF0,0x34,0} }, { {0xe0,0x33,0},{0xe0,0xF0,0x33,0} },        /*120*/
    { {0xe0,0x3B,0},{0xe0,0xF0,0x3B,0} }, { {0xe0,0x42,0},{0xe0,0xF0,0x42,0} }, { {0xe0,0x4B,0},{0xe0,0xF0,0x4B,0} }, { {          0},{               0} },        /*124*/
    { {          0},{               0} }, { {          0},{               0} }, { {          0},{               0} }, { {          0},{               0} },        /*128*/
    { {0xe0,0x1A,0},{0xe0,0xF0,0x1A,0} }, { {0xe0,0x22,0},{0xe0,0xF0,0x22,0} }, { {0xe0,0x21,0},{0xe0,0xF0,0x21,0} }, { {0xe0,0x2A,0},{0xe0,0xF0,0x2A,0} },        /*12c*/
    { {0xe0,0x32,0},{0xe0,0xF0,0x32,0} }, { {0xe0,0x31,0},{0xe0,0xF0,0x31,0} }, { {0xe0,0x3A,0},{0xe0,0xF0,0x3A,0} }, { {          0},{               0} },        /*130*/
    { {0xe0,0x49,0},{0xe0,0xF0,0x49,0} }, { {     0x77,0},{     0xf0,0x77,0} }, { {          0},{               0} }, { {     0x57,0},{     0xf0,0x57,0} },        /*134*/
    { {     0x39,0},{     0xf0,0x39,0} }, { {          0},{               0} }, { {0xe0,0x58,0},{0xe0,0xF0,0x58,0} }, { {0xe0,0x05,0},{0xe0,0xF0,0x05,0} },        /*138*/
    { {0xe0,0x06,0},{0xe0,0xF0,0x06,0} }, { {0xe0,0x04,0},{0xe0,0xF0,0x04,0} }, { {0xe0,0x0C,0},{0xe0,0xF0,0x0C,0} }, { {0xe0,0x03,0},{0xe0,0xF0,0x03,0} },        /*13c*/
    { {0xe0,0x0B,0},{0xe0,0xF0,0x0B,0} }, { {0xe0,0x02,0},{0xe0,0xF0,0x02,0} }, { {0xe0,0x0A,0},{0xe0,0xF0,0x0A,0} }, { {0xe0,0x01,0},{0xe0,0xF0,0x01,0} },        /*140*/
    { {0xe0,0x09,0},{0xe0,0xF0,0x09,0} }, { {          0},{               0} }, { {0xe0,0x7E,0},{0xe0,0xF0,0x7E,0} }, { {     0x6E,0},{     0xf0,0x6E,0} },        /*144*/
    { {     0x63,0},{     0xf0,0x63,0} }, { {     0x6F,0},{     0xf0,0x6F,0} }, { {          0},{               0} }, { {     0x61,0},{     0xf0,0x61,0} },        /*148*/
    { {0xe0,0x73,0},{0xe0,0xF0,0x73,0} }, { {     0x6A,0},{     0xf0,0x6A,0} }, { {0xe0,0x79,0},{0xe0,0xF0,0x79,0} }, { {     0x65,0},{     0xf0,0x65,0} },        /*14c*/
    { {     0x60,0},{     0xf0,0x60,0} }, { {     0x6D,0},{     0xf0,0x6D,0} }, { {     0x67,0},{     0xf0,0x67,0} }, { {     0x64,0},{     0xf0,0x64,0} },        /*150*/
    { {     0xd4,0},{     0xf0,0xD4,0} }, { {0xe0,0x60,0},{0xe0,0xF0,0x60,0} }, { {          0},{               0} }, { {0xe0,0x78,0},{0xe0,0xF0,0x78,0} },        /*154*/
    { {0xe0,0x07,0},{0xe0,0xF0,0x07,0} }, { {0xe0,0x0F,0},{0xe0,0xF0,0x0F,0} }, { {0xe0,0x17,0},{0xe0,0xF0,0x17,0} }, { {     0x8B,0},{     0xf0,0x8B,0} },        /*158*/
    { {     0x8C,0},{     0xf0,0x8C,0} }, { {     0x8D,0},{     0xf0,0x8D,0} }, { {          0},{               0} }, { {     0x7F,0},{     0xf0,0x7F,0} },        /*15c*/
    { {          0},{               0} }, { {0xe0,0x4F,0},{0xe0,0xF0,0x4F,0} }, { {0xe0,0x56,0},{0xe0,0xF0,0x56,0} }, { {          0},{               0} },        /*160*/
    { {0xe0,0x08,0},{0xe0,0xF0,0x08,0} }, { {0xe0,0x10,0},{0xe0,0xF0,0x10,0} }, { {0xe0,0x18,0},{0xe0,0xF0,0x18,0} }, { {0xe0,0x20,0},{0xe0,0xF0,0x20,0} },        /*164*/
    { {0xe0,0x28,0},{0xe0,0xF0,0x28,0} }, { {0xe0,0x30,0},{0xe0,0xF0,0x30,0} }, { {0xe0,0x38,0},{0xe0,0xF0,0x38,0} }, { {0xe0,0x40,0},{0xe0,0xF0,0x40,0} },        /*168*/
    { {0xe0,0x48,0},{0xe0,0xF0,0x48,0} }, { {0xe0,0x50,0},{0xe0,0xF0,0x50,0} }, { {0xe0,0x57,0},{0xe0,0xF0,0x57,0} }, { {          0},{               0} },        /*16c*/
    { {0xe0,0x13,0},{0xe0,0xF0,0x13,0} }, { {0xe0,0x19,0},{0xe0,0xF0,0x19,0} }, { {0xe0,0x39,0},{0xe0,0xF0,0x39,0} }, { {0xe0,0x51,0},{0xe0,0xF0,0x51,0} },        /*170*/
    { {0xe0,0x53,0},{0xe0,0xF0,0x53,0} }, { {0xe0,0x5C,0},{0xe0,0xF0,0x5C,0} }, { {          0},{               0} }, { {0xe0,0x62,0},{0xe0,0xF0,0x62,0} },        /*174*/
    { {0xe0,0x63,0},{0xe0,0xF0,0x63,0} }, { {0xe0,0x64,0},{0xe0,0xF0,0x64,0} }, { {0xe0,0x65,0},{0xe0,0xF0,0x65,0} }, { {0xe0,0x67,0},{0xe0,0xF0,0x67,0} },        /*178*/
    { {0xe0,0x68,0},{0xe0,0xF0,0x68,0} }, { {0xe0,0x6A,0},{0xe0,0xF0,0x6A,0} }, { {0xe0,0x6D,0},{0xe0,0xF0,0x6D,0} }, { {0xe0,0x6E,0},{0xe0,0xF0,0x6E,0} },        /*17c*/

    { {          0},{               0} }, { {          0},{               0} }, { {          0},{               0} }, { {          0},{               0} },        /*180*/
    { {          0},{               0} }, { {          0},{               0} }, { {          0},{               0} }, { {          0},{               0} },        /*184*/
    { {          0},{               0} }, { {          0},{               0} }, { {          0},{               0} }, { {          0},{               0} },        /*188*/
    { {          0},{               0} }, { {          0},{               0} }, { {          0},{               0} }, { {          0},{               0} },        /*18c*/
    { {          0},{               0} }, { {          0},{               0} }, { {          0},{               0} }, { {          0},{               0} },        /*190*/
    { {          0},{               0} }, { {          0},{               0} }, { {          0},{               0} }, { {          0},{               0} },        /*194*/
    { {          0},{               0} }, { {          0},{               0} }, { {          0},{               0} }, { {          0},{               0} },        /*198*/
    { {          0},{               0} }, { {          0},{               0} }, { {          0},{               0} }, { {          0},{               0} },        /*19c*/
    { {          0},{               0} }, { {          0},{               0} }, { {          0},{               0} }, { {          0},{               0} },        /*1a0*/
    { {          0},{               0} }, { {          0},{               0} }, { {          0},{               0} }, { {          0},{               0} },        /*1a4*/
    { {          0},{               0} }, { {          0},{               0} }, { {          0},{               0} }, { {          0},{               0} },        /*1a8*/
    { {          0},{               0} }, { {          0},{               0} }, { {          0},{               0} }, { {          0},{               0} },        /*1ac*/
    { {          0},{               0} }, { {          0},{               0} }, { {          0},{               0} }, { {          0},{               0} },        /*1c0*/
    { {          0},{               0} }, { {          0},{               0} }, { {          0},{               0} }, { {          0},{               0} },        /*1c4*/
    { {          0},{               0} }, { {          0},{               0} }, { {          0},{               0} }, { {          0},{               0} },        /*1c8*/
    { {          0},{               0} }, { {          0},{               0} }, { {          0},{               0} }, { {          0},{               0} },        /*1cc*/
    { {          0},{               0} }, { {          0},{               0} }, { {          0},{               0} }, { {          0},{               0} },        /*1d0*/
    { {          0},{               0} }, { {          0},{               0} }, { {          0},{               0} }, { {          0},{               0} },        /*1d4*/
    { {          0},{               0} }, { {          0},{               0} }, { {          0},{               0} }, { {          0},{               0} },        /*1d8*/
    { {          0},{               0} }, { {          0},{               0} }, { {          0},{               0} }, { {          0},{               0} },        /*1dc*/
    { {          0},{               0} }, { {0xe0,0xe1,0},{0xe0,0xF0,0xE1,0} }, { {          0},{               0} }, { {          0},{               0} },        /*1e0*/
    { {          0},{               0} }, { {          0},{               0} }, { {          0},{               0} }, { {          0},{               0} },        /*1e4*/
    { {          0},{               0} }, { {          0},{               0} }, { {          0},{               0} }, { {          0},{               0} },        /*1e8*/
    { {          0},{               0} }, { {          0},{               0} }, { {0xe0,0xee,0},{0xe0,0xF0,0xEE,0} }, { {          0},{               0} },        /*1ec*/
    { {          0},{               0} }, { {0xe0,0xf1,0},{0xe0,0xF0,0xF1,0} }, { {          0},{               0} }, { {          0},{               0} },        /*1f0*/
    { {          0},{               0} }, { {          0},{               0} }, { {          0},{               0} }, { {          0},{               0} },        /*1f4*/
    { {          0},{               0} }, { {          0},{               0} }, { {          0},{               0} }, { {          0},{               0} },        /*1f8*/
    { {          0},{               0} }, { {          0},{               0} }, { {0xe0,0xfe,0},{0xe0,0xF0,0xFE,0} }, { {0xe0,0xff,0},{0xe0,0xF0,0xFF,0} }         /*1fc*/
  // clang-format on
};

#define UISTR_LEN 256
static void add_data_kbd(uint16_t val);

#ifdef ENABLE_KEYBOARD_AT_LOG
int keyboard_at_do_log = ENABLE_KEYBOARD_AT_LOG;

static void
kbd_log(const char *fmt, ...)
{
    va_list ap;

    if (keyboard_at_do_log) {
        va_start(ap, fmt);
        pclog_ex(fmt, ap);
        va_end(ap);
    }
}
#else
#    define kbd_log(fmt, ...)
#endif

static void
set_scancode_map(atkbd_t *dev)
{
    switch (keyboard_mode & 3) {
#ifdef USE_SET1
        case 1:
        default:
            keyboard_set_table(scancode_set1);
            break;
#else
        default:
#endif
        case 2:
            keyboard_set_table(scancode_set2);
            break;

        case 3:
            keyboard_set_table(scancode_set3);
            break;
    }

    if (keyboard_mode & 0x20)
#ifdef USE_SET1
        keyboard_set_table(scancode_set1);
#else
        keyboard_set_table(scancode_set2);
#endif
}

static void
kbc_queue_reset(uint8_t channel)
{
    if (channel == 2) {
        mouse_queue_start = mouse_queue_end = 0;
        memset(mouse_queue, 0x00, sizeof(mouse_queue));
    } else if (channel == 1) {
        key_queue_start = key_queue_end = 0;
        memset(key_queue, 0x00, sizeof(key_queue));
    }
}

static void
kbc_queue_add(atkbd_t *dev, uint8_t val, uint8_t channel, uint8_t stat_hi)
{
    if (channel == 2) {
        kbd_log("ATkbc: mouse_queue[%02X] = %02X;\n", mouse_queue_end, val);
        mouse_queue[mouse_queue_end] = val;
        mouse_queue_end              = (mouse_queue_end + 1) & 0xf;
    } else if (channel == 1) {
        kbd_log("ATkbc: key_queue[%02X] = %02X;\n", key_queue_end, val);
        key_queue[key_queue_end] = val;
        key_queue_end            = (key_queue_end + 1) & 0xf;
    } else
        fatal("Adding %02X to invalid channel %02X\n", val, channel);
}

static void
add_data_kbd_queue(atkbd_t *dev, int direct, uint8_t val)
{
    if ((!keyboard_scan && !direct) || (key_queue_end >= 16)) {
        kbd_log("ATkbc: Unable to add to queue, conditions: %i, %i\n", !keyboard_scan, (key_queue_end >= 16));
        return;
    }

    kbd_log("ATkbc: key_queue[%02X] = %02X;\n", key_queue_end, val);
    kbc_queue_add(dev, val, 1, 0x00);
    kbd_last_scan_code = val;
}

static void
kbc_send(atkbd_t *dev, uint8_t val, uint8_t channel)
{
    dev->kbc_written[channel] = 1;
    dev->kbc_data[channel]    = val;
}

static void
kbd_send_to_host(atkbd_t *dev, uint8_t val)
{
    kbc_send(dev, val, CHANNEL_KBD);
}

static void
kbd_chip_reset(atkbd_t *dev)
{
    kbc_queue_reset(1);
    dev->kbc_written[1] = 0x00;
    kbd_last_scan_code  = 0x00;

    /* Set scan code set to 2. */
    keyboard_mode = (keyboard_mode & 0xfc) | 0x02;
    set_scancode_map(dev);

    dev->kbd_phase = 0;
    dev->kbd_in    = 0;
}

static void
kbd_command(atkbd_t *dev)
{
    uint8_t val = dev->kbd_data;

    if ((dev->kbd_phase > 0) && (dev->kbd_cmd == 0xff)) {
        dev->kbd_phase++;
        if (dev->kbd_phase == RESET_DELAY_TIME) {
            kbd_send_to_host(dev, 0xaa);
            dev->kbd_phase = 0;
            dev->kbd_cmd   = 0x00;
        }
        return;
    }

    if (dev->kbd_phase == 2) {
        dev->kbd_phase = 0;

        switch (dev->kbd_cmd) {
            case 0xf2:
                kbd_send_to_host(dev, 0x83);
                break;
            default:
                fatal("Invalid command for phase 2: %02X\n", dev->kbd_cmd);
                break;
        }

        /* Keyboard command is now done. */
        if (dev->kbd_phase == 0)
            dev->kbd_cmd = 0x00;
        return;
    } else if (dev->kbd_phase == 1) {
        dev->kbd_phase = 0;

        switch (dev->kbd_cmd) {
            case 0xf0:
                kbd_log("Get scan code set: %02X\n", keyboard_mode & 3);
                kbd_send_to_host(dev, keyboard_mode & 3);
                break;
            case 0xf2:
                kbd_send_to_host(dev, 0xab);
                dev->kbd_phase = 2;
                break;
            default:
                fatal("Invalid command for phase 1: %02X\n", dev->kbd_cmd);
                break;
        }

        /* Keyboard command is now done. */
        if (dev->kbd_phase == 0)
            dev->kbd_cmd = 0x00;
        return;
    }

    if (dev->kbd_in && (val < 0xed)) {
        dev->kbd_in    = 0;
        dev->kbd_phase = 0;

        switch (dev->kbd_cmd) {
            case 0xed: /* set/reset LEDs */
                kbd_log("ATkbd: set LEDs [%02x]\n", val);
                kbd_send_to_host(dev, 0xfa);
                break;

            case 0xf0: /* get/set scancode set */
                kbd_send_to_host(dev, 0xfa);
                if (val == 0)
                    dev->kbd_phase = 1;
                else {
                    if ((val <= 3) && (val != 1)) {
                        keyboard_mode &= 0xfc;
                        keyboard_mode |= (val & 3);
                        kbd_log("Scan code set now: %02X\n", val);
                    }
                    set_scancode_map(dev);
                }
                break;

            case 0xf3: /* set typematic rate/delay */
                kbd_send_to_host(dev, 0xfa);
                break;

            default:
                kbd_log("ATkbd: bad keyboard 0060 write %02X command %02X\n", val, dev->kbd_cmd);
                kbd_send_to_host(dev, 0xfe);
                break;
        }

        /* Keyboard command is now done. */
        if (dev->kbd_phase == 0)
            dev->kbd_cmd = 0x00;
    } else {
        /* No keyboard command in progress. */
        dev->kbd_in    = 0;
        dev->kbd_cmd   = 0x00;
        dev->kbd_phase = 0;

        switch (val) {
            case 0x00:
                kbd_log("ATkbd: command 00\n");
                kbd_send_to_host(dev, 0xfa);
                break;

            case 0x05: /*??? - sent by NT 4.0*/
                kbd_log("ATkbd: command 05 (NT 4.0)\n");
                kbd_send_to_host(dev, 0xfe);
                break;

            case 0xed: /* set/reset LEDs */
                kbd_log("ATkbd: set/reset leds\n");
                kbd_send_to_host(dev, 0xfa);

                dev->kbd_in = 1;
                break;

            case 0xee: /* diagnostic echo */
                kbd_log("ATkbd: ECHO\n");
                kbd_send_to_host(dev, 0xee);
                break;

            case 0xef: /* NOP (reserved for future use) */
                kbd_log("ATkbd: NOP\n");
                break;

            case 0xf0: /* get/set scan code set */
                kbd_log("ATkbd: scan code set\n");
                kbd_send_to_host(dev, 0xfa);
                dev->kbd_in = 1;
                break;

            case 0xf2: /* read ID */
                       /* Fixed as translation will be done in add_data_kbd(). */
                kbd_log("ATkbd: read keyboard id\n");
                /* TODO: After keyboard type selection is implemented, make this
                         return the correct keyboard ID for the selected type. */
                kbd_send_to_host(dev, 0xfa);
                dev->kbd_phase = 1;
                break;

            case 0xf3: /* set typematic rate/delay */
                kbd_log("ATkbd: set typematic rate/delay\n");
                kbd_send_to_host(dev, 0xfa);
                dev->kbd_in = 1;
                break;

            case 0xf4: /* enable keyboard */
                kbd_log("ATkbd: enable keyboard\n");
                kbd_send_to_host(dev, 0xfa);
                keyboard_scan = 1;
                break;

            case 0xf5: /* set defaults and disable keyboard */
            case 0xf6: /* set defaults */
                kbd_log("ATkbd: set defaults%s\n", (val == 0xf6) ? "" : " and disable keyboard");
                keyboard_scan = (val == 0xf6);
                kbd_log("val = %02X, keyboard_scan = %i, dev->mem[0x20] = %02X\n",
                        val, keyboard_scan, dev->mem[0x20]);
                kbd_send_to_host(dev, 0xfa);

                keyboard_set3_all_break  = 0;
                keyboard_set3_all_repeat = 0;
                memset(keyboard_set3_flags, 0, 512);
                keyboard_mode = (keyboard_mode & 0xfc) | 0x02;
                set_scancode_map(dev);
                break;

            case 0xf7: /* set all keys to repeat */
                kbd_log("ATkbd: set all keys to repeat\n");
                kbd_send_to_host(dev, 0xfa);
                keyboard_set3_all_break = 1;
                break;

            case 0xf8: /* set all keys to give make/break codes */
                kbd_log("ATkbd: set all keys to give make/break codes\n");
                kbd_send_to_host(dev, 0xfa);
                keyboard_set3_all_break = 1;
                break;

            case 0xf9: /* set all keys to give make codes only */
                kbd_log("ATkbd: set all keys to give make codes only\n");
                kbd_send_to_host(dev, 0xfa);
                keyboard_set3_all_break = 0;
                break;

            case 0xfa: /* set all keys to repeat and give make/break codes */
                kbd_log("ATkbd: set all keys to repeat and give make/break codes\n");
                kbd_send_to_host(dev, 0xfa);
                keyboard_set3_all_repeat = 1;
                keyboard_set3_all_break  = 1;
                break;

            case 0xfe: /* resend last scan code */
                kbd_log("ATkbd: reset last scan code\n");
                kbd_send_to_host(dev, kbd_last_scan_code);
                break;

            case 0xff: /* reset */
                kbd_log("ATkbd: kbd reset\n");
                kbd_chip_reset(dev);
                kbd_send_to_host(dev, 0xfa);
                dev->kbd_phase = 1;
                break;

            default:
                kbd_log("ATkbd: bad keyboard command %02X\n", val);
                kbd_send_to_host(dev, 0xfe);
        }

        /* If command needs data, remember command. */
        if ((dev->kbd_in == 1) || (dev->kbd_phase > 0))
            dev->kbd_cmd = val;
    }
}

static void
kbd_do_command(atkbd_t *dev)
{
    kbd_command(dev);
    if (dev->kbd_written)
        dev->kbd_poll_phase = KBD_CMD_PROCESS;
    else if ((dev->kbd_phase == 0) && !dev->kbd_in) {
        dev->kbd_in_cmd = 0;
        if (dev->kbd_data != 0xf5)
            keyboard_scan = 1;
        dev->kbd_poll_phase = KBD_MAIN_LOOP;
    } else {
        keyboard_scan       = 0;
        dev->kbd_in_cmd     = 1;
        dev->kbd_poll_phase = KBD_CMD_PROCESS;
    }
}

static void
kbd_nack(atkbd_t *dev)
{
    kbd_send_to_host(dev, 0xfe);
    dev->kbd_poll_phase = KBD_MAIN_LOOP;
}

static void
kbd_main_loop(atkbd_t *dev)
{
    uint8_t scan = !dev->kbd_inhibit && keyboard_scan;

    if (dev->kbd_written) {
        dev->kbd_written = 0;
        kbd_cmd_process(dev);
    } else if (scan && (key_queue_start != key_queue_end)) {
        /* Scan here. */
        kbd_log("ATkbd: Get %02X from FIFO\n", key_queue[key_queue_start]);
        kbd_send_to_host(dev, key_queue[key_queue_start]);
        key_queue_start = (key_queue_start + 1) & 0xf;
    }
}

static void
kbd_cmd_process(atkbd_t *dev)
{
    uint8_t written = dev->kbd_written;

    /* We want data, nothing has been written yet, return. */
    if (dev->kbd_in && !dev->kbd_written)
        return;

    dev->kbd_written = 0;

    if (!written && !keyboard_scan && dev->kbd_in_cmd && (dev->kbd_phase > 0)) {
        kbd_log("ATkbd: Keyboard not written, not scanning, in command, and phase > 0\n");
        kbd_do_command(dev);
    } else if (dev->kbd_data == 0xfe) {
        kbd_log("ATkbd: Send last byte %02X\n", kbd_last_scan_code);
        kbd_send_to_host(dev, kbd_last_scan_code);
        dev->kbd_poll_phase = KBD_MAIN_LOOP;
    } else if (dev->kbd_data == 0xee) {
        kbd_log("ATkbd: Echo EE\n");
        kbd_send_to_host(dev, 0xee);
        dev->kbd_poll_phase = KBD_MAIN_LOOP;
    } else if (dev->kbd_data >= 0xed) {
        kbd_log("ATkbd: Command %02X\n", dev->kbd_data);
        if (!keyboard_scan && dev->kbd_in_cmd && (dev->kbd_cmd == 0xed)) {
            kbd_log("ATkbd: Not scanning, in command, old command is ED\n");
            keyboard_scan   = 1;
            dev->kbd_in_cmd = 0;
        }
        kbd_do_command(dev);
    } else {
        if (!keyboard_scan && dev->kbd_in_cmd) {
            if ((dev->kbd_cmd == 0xf3) && (dev->kbd_data & 0x80)) {
                kbd_log("ATkbd: Command F3 data %02X has bit 7 set\n", dev->kbd_data);
                kbd_nack(dev);
            } else {
                kbd_log("ATkbd: Command %02X data %02X\n", dev->kbd_cmd, dev->kbd_data);
                kbd_do_command(dev);
            }
        } else {
            kbd_log("ATkbd: Scanning or not in command, NACK\n");
            kbd_nack(dev);
        }
    }
}

/* Keyboard processing */
static void
kbd_process(atkbd_t *dev)
{
    /* The real 8048 keyboard firmware stops transmitting if host wants to transmit. */
    if (dev->kbc_written[1] && dev->kbd_written)
        dev->kbc_written[1] = 0;

    /* The host has either acknowledged the transmitted byte or we have not transmitted anything (yet). */
    if (!dev->kbc_written[1])
        switch (dev->kbd_poll_phase) {
            case KBD_MAIN_LOOP:
                kbd_main_loop(dev);
                break;
            case KBD_CMD_PROCESS:
                kbd_cmd_process(dev);
                break;
        }
}

static void
kbc_send_to_ob(atkbd_t *dev, uint8_t val, uint8_t channel, uint8_t stat_hi)
{
    uint8_t ch        = (channel > 0) ? channel : 1;
    uint8_t do_irq    = (dev->mem[0x20] & ch);
    int     translate = (channel == 1) && (keyboard_mode & 0x60);

    if ((channel == 2) && !(dev->flags & KBC_FLAG_PS2))
        return;

    stat_hi |= dev->inhibit;

    if (!dev->kbc_send_pending) {
        dev->kbc_send_pending = 1;
        dev->kbc_to_send      = val;
        dev->kbc_channel      = channel;
        dev->kbc_stat_hi      = stat_hi;
        return;
    }

    if (translate) {
        /* Allow for scan code translation. */
        if (val == 0xf0) {
            kbd_log("ATkbd: translate is on, F0 prefix detected\n");
            sc_or = 0x80;
            return;
        }

        /* Skip break code if translated make code has bit 7 set. */
        if ((sc_or == 0x80) && (val & 0x80)) {
            kbd_log("ATkbd: translate is on, skipping scan code: %02X (original: F0 %02X)\n", nont_to_t[val], val);
            sc_or = 0;
            return;
        }
    }

    dev->last_irq = (ch == 2) ? 0x1000 : 0x0002;
    if (do_irq) {
        kbd_log("[%04X:%08X] ATKbc: IRQ %i\n", CS, cpu_state.pc, (ch == 2) ? 12 : 1);
        picint(dev->last_irq);
    }
    kbd_log("ATkbc: %02X coming from channel %i (%i)\n", val, channel, do_irq);
    dev->ob = translate ? (nont_to_t[val] | sc_or) : val;

    dev->status = (dev->status & 0x0f) | (stat_hi | (dev->mem[0x20] & STAT_SYSFLAG) | STAT_OFULL);
    if (ch == 2)
        dev->status |= STAT_MFULL;

    if (translate && (sc_or == 0x80))
        sc_or = 0;
}

static void
write_output(atkbd_t *dev, uint8_t val)
{
    uint8_t old = dev->p2;

    kbd_log("ATkbc: write output port: %02X (old: %02X)\n", val, dev->p2);

    if (!(dev->flags & KBC_FLAG_PS2))
        val |= ((dev->mem[0x20] << 4) & 0x10);

    dev->kbd_inhibit   = (val & 0x40);
    dev->mouse_inhibit = (val & 0x08);

    /* IRQ 12 */
    if ((old ^ val) & 0x20) {
        if (val & 0x20) {
            kbd_log("ATkbc: write_output(): IRQ 12\n");
            picint(1 << 12);
        } else
            picintc(1 << 12);
    }

    /* IRQ 1 */
    if ((old ^ val) & 0x10) {
        if (val & 0x10) {
            kbd_log("ATkbc: write_output(): IRQ  1\n");
            picint(1 << 1);
        } else
            picintc(1 << 1);
    }

    /* A20 enable change */
    if ((old ^ val) & 0x02) {
        mem_a20_key = val & 0x02;
        mem_a20_recalc();
        flushmmucache();
    }

    /* 0 holds the CPU in the RESET state, 1 releases it. To simplify this,
       we just do everything on release. */
    if ((dev->p2 ^ val) & 0x01) { /*Reset*/
        if (!(val & 0x01)) {      /* Pin 0 selected. */
            /* Pin 0 selected. */
            pclog("write_output(): Pulse reset!\n");
            softresetx86(); /*Pulse reset!*/
            cpu_set_edx();
            flushmmucache();
        }
    }

    /* Do this here to avoid an infinite reset loop. */
    dev->p2 = val;
}

static void
write_cmd(atkbd_t *dev, uint8_t val)
{
    uint8_t kbc_ven = dev->flags & KBC_VEN_MASK;
    kbd_log("ATkbc: write command byte: %02X (old: %02X)\n", val, dev->mem[0x20]);

    /* PS/2 type 2 keyboard controllers always force the XLAT bit to 0. */
    if ((dev->flags & KBC_TYPE_MASK) == KBC_TYPE_PS2_2)
        val &= ~CCB_TRANSLATE;

    dev->mem[0x20] = val;

    /* Scan code translate ON/OFF. */
    keyboard_mode &= 0x93;
    keyboard_mode |= (val & MODE_MASK);

    kbd_log("ATkbc: keyboard interrupt is now %s\n", (val & 0x01) ? "enabled" : "disabled");

    /* ISA AT keyboard controllers use bit 5 for keyboard mode (1 = PC/XT, 2 = AT);
       PS/2 (and EISA/PCI) keyboard controllers use it as the PS/2 mouse enable switch.
       The AMIKEY firmware apparently uses this bit for something else. */
    if ((kbc_ven == KBC_VEN_AMI) || (dev->flags & KBC_FLAG_PS2)) {
        keyboard_mode &= ~CCB_PCMODE;

        kbd_log("ATkbc: mouse interrupt is now %s\n", (val & 0x02) ? "enabled" : "disabled");
    }

    if (!(dev->flags & KBC_FLAG_PS2)) {
        /* Update the output port to mirror the KBD DIS and AUX DIS bits, if active. */
        write_output(dev, dev->p2);
    }

    kbd_log("Command byte now: %02X (%02X)\n", dev->mem[0x20], val);

    dev->status = (dev->status & ~STAT_SYSFLAG) | (val & STAT_SYSFLAG);
}

static void
pulse_output(atkbd_t *dev, uint8_t mask)
{
    if (mask != 0x0f) {
        dev->old_p2 = dev->p2 & ~(0xf0 | mask);
        kbd_log("pulse_output(): Output port now: %02X\n", dev->p2 & (0xf0 | mask | (dev->mem[0x20] & 0x30)));
        write_output(dev, dev->p2 & (0xf0 | mask | (dev->mem[0x20] & 0x30)));
        timer_set_delay_u64(&dev->pulse_cb, 6ULL * TIMER_USEC);
    }
}

static void
set_enable_kbd(atkbd_t *dev, uint8_t enable)
{
    dev->mem[0x20] &= 0xef;
    dev->mem[0x20] |= (enable ? 0x00 : 0x10);
}

static void
set_enable_mouse(atkbd_t *dev, uint8_t enable)
{
    dev->mem[0x20] &= 0xdf;
    dev->mem[0x20] |= (enable ? 0x00 : 0x20);
}

static void
kbc_transmit(atkbd_t *dev, uint8_t val)
{
    kbc_send_to_ob(dev, val, 0, 0x00);
}

static void
kbc_command(atkbd_t *dev)
{
    uint8_t mask, val = dev->ib;
    uint8_t kbc_ven = dev->flags & KBC_VEN_MASK;
#ifdef ENABLE_KEYBOARD_AT_LOG
    int bad = 1;
#endif

    if ((dev->kbc_phase > 0) && (dev->kbc_cmd == 0xac)) {
        if (dev->kbc_phase < 16)
            kbc_transmit(dev, dev->mem[dev->kbc_phase]);
        else if (dev->kbc_phase == 16)
            kbc_transmit(dev, (dev->p1 & 0xf0) | 0x80);
        else if (dev->kbc_phase == 17)
            kbc_transmit(dev, dev->p2);
        else if (dev->kbc_phase == 18)
            kbc_transmit(dev, dev->status);

        dev->kbc_phase++;
        if (dev->kbc_phase == 19) {
            dev->kbc_phase = 0;
            dev->kbc_cmd   = 0x00;
        }
        return;
    } else if ((dev->kbc_phase > 0) && (dev->kbc_cmd == 0xa0) && (kbc_ven >= KBC_VEN_AMI)) {
        val = ami_copr[dev->kbc_phase];
        kbc_transmit(dev, val);
        if (val == 0x00) {
            dev->kbc_phase = 0;
            dev->kbc_cmd   = 0x00;
        } else
            dev->kbc_phase++;
        return;
    } else if ((dev->kbc_in > 0) && (dev->kbc_cmd == 0xa5) && (dev->flags & KBC_FLAG_PS2)) {
        /* load security */
        kbd_log("ATkbc: load security\n");
        dev->mem[0x50 + dev->kbc_in - 0x01] = val;
        if ((dev->kbc_in == 0x80) && (val != 0x00)) {
            /* Security string too long, set it to 0x00. */
            dev->mem[0x50] = 0x00;
            dev->kbc_in    = 0;
            dev->kbc_cmd   = 0;
        } else if (val == 0x00) {
            /* Security string finished. */
            dev->kbc_in  = 0;
            dev->kbc_cmd = 0;
        } else /* Increase pointer and request another byte. */
            dev->kbc_in++;
        return;
    }

    /* If the written port is 64, go straight to the beginning of the command. */
    if (!(dev->status & STAT_CD) && dev->kbc_in) {
        /* Write data to controller. */
        dev->kbc_in    = 0;
        dev->kbc_phase = 0;

        switch (dev->kbc_cmd) {
            case 0x60 ... 0x7f:
                if (dev->kbc_cmd == 0x60)
                    write_cmd(dev, val);
                else
                    dev->mem[(dev->kbc_cmd & 0x1f) + 0x20] = val;
                break;

            case 0xc7: /* or input port with system data */
                dev->p1 |= val;
                break;

            case 0xcb: /* set keyboard mode */
                kbd_log("New AMIKey mode: %02X\n", val);
                dev->ami_mode = val;
#ifdef NON_ALI
                dev->flags &= ~KBC_FLAG_PS2;
                if (val & 1)
                    dev->flags |= KBC_FLAG_PS2;
#endif
                break;

            case 0xd1: /* write output port */
                kbd_log("ATkbc: write output port\n");
                if (dev->p2_locked) {
                    /*If keyboard controller lines P22-P23 are blocked,
                      we force them to remain unchanged.*/
                    val &= ~0x0c;
                    val |= (dev->p2 & 0x0c);
                }
                write_output(dev, val);
                break;

            case 0xd2: /* write to keyboard output buffer */
                kbd_log("ATkbc: write to keyboard output buffer\n");
                // kbc_send_to_ob(dev, val, 1, 0x00);
                /* Should be channel 1, but we send to 0 to avoid translation,
                   since bytes output using this command do *NOT* get translated. */
                kbc_send_to_ob(dev, val, 0, 0x00);
                break;

            case 0xd3: /* write to mouse output buffer */
                kbd_log("ATkbc: write to mouse output buffer\n");
                if (dev->flags & KBC_FLAG_PS2)
                    kbc_send_to_ob(dev, val, 2, 0x00);
                break;

            case 0xd4: /* write to mouse */
                kbd_log("ATkbc: write to mouse (%02X)\n", val);

                if (dev->flags & KBC_FLAG_PS2) {
                    set_enable_mouse(dev, 1);
                    dev->mem[0x20] &= ~0x20;
#ifdef ENABLE_MOUSE
                    if (mouse_write && !dev->kbc_written[2]) {
                        kbd_log("ATkbc: Sending %02X to mouse...\n", dev->ib);
                        dev->mouse_data            = val;
                        dev->mouse_written         = 1;
                        dev->kbc_wait_for_response = 2;
                    } else
                        kbc_send_to_ob(dev, 0xfe, 2, 0x40);
#else
                    kbc_send_to_ob(dev, 0xfe, 2, 0x40);
#endif
                }
                break;

            default:
                /*
                 * Run the vendor-specific handler
                 * if we have one. Otherwise, or if
                 * it returns an error, log a bad
                 * controller command.
                 */
#ifdef ENABLE_KEYBOARD_AT_LOG
                if (dev->write60_ven)
                    bad = dev->write60_ven(dev, val);

                if (bad)
                    kbd_log("ATkbc: bad controller command %02x data %02x\n", dev->kbc_cmd, val);
#else
                if (dev->write60_ven)
                    (void) dev->write60_ven(dev, val);
#endif
        }
    } else {
        /* Controller command. */
        kbd_log("ATkbc: Controller command: %02X\n", val);
        dev->kbc_in    = 0;
        dev->kbc_phase = 0;

        switch (val) {
            /* Read data from KBC memory. */
            case 0x20 ... 0x3f:
                kbc_transmit(dev, dev->mem[(val & 0x1f) + 0x20]);
                break;

            /* Write data to KBC memory. */
            case 0x60 ... 0x7f:
                dev->kbc_in = 1;
                break;

            case 0xaa: /* self-test */
                kbd_log("ATkbc: self-test\n");
                write_output(dev, (dev->flags & KBC_FLAG_PS2) ? 0x4b : 0xcf);

                /* Always reinitialize all queues - the real hardware pulls keyboard and mouse
                   clocks high, which stops keyboard scanning. */
                kbd_log("ATkbc: self-test reinitialization\n");
                dev->kbd_in_cmd = dev->mouse_in_cmd = 0;
                dev->status &= ~STAT_OFULL;
                dev->last_irq  = 0;
                dev->kbc_phase = 0;

                /* Phoenix MultiKey should have 0x60 | STAT_SYSFLAG. */
                if (dev->flags & KBC_FLAG_PS2)
                    write_cmd(dev, 0x30 | STAT_SYSFLAG);
                else
                    write_cmd(dev, 0x10 | STAT_SYSFLAG);
                kbc_transmit(dev, 0x55);
                break;

            case 0xab: /* interface test */
                kbd_log("ATkbc: interface test\n");
                /* No error. */
                kbc_transmit(dev, 0x00);
                break;

            case 0xac: /* diagnostic dump */
                kbd_log("ATkbc: diagnostic dump\n");
                kbc_transmit(dev, dev->mem[0x20]);
                dev->kbc_phase = 1;
                break;

            case 0xad: /* disable keyboard */
                kbd_log("ATkbc: disable keyboard\n");
                set_enable_kbd(dev, 0);
                break;

            case 0xae: /* enable keyboard */
                kbd_log("ATkbc: enable keyboard\n");
                set_enable_kbd(dev, 1);
                break;

            case 0xc7: /* or input port with system data */
                kbd_log("ATkbc: Phoenix - or input port with system data\n");
                dev->kbc_in = 1;
                break;

            case 0xca: /* read keyboard mode */
                kbd_log("ATkbc: AMI - read keyboard mode\n");
                kbc_transmit(dev, dev->ami_mode);
                break;

            /* NOTE: Not present on the ALi M148x on-chip keyboard controller, only 0xCA exists there.
                     This is confirmed by the BIOS' keyboard controller revision string where the
                     output of command 0x20 (read command byte from memory) gets erroneouly read by
                     the BIOS, thinking it's the readout of command 0xA1 (get controller version).
                     With this command disabled, the AMI WinBIOS 486 PCI still shows 'U', but the MSI
                     MS-4145 shows 'E', matching exactly the observations on the real boards. */
            case 0xcb: /* set keyboard mode */
                kbd_log("ATkbc: AMI - set keyboard mode\n");
                dev->kbc_in = 1;
                break;

            case 0xd0: /* read output port */
                kbd_log("ATkbc: read output port\n");
                mask = 0xff;
                if (!(dev->flags & KBC_FLAG_PS2) && (dev->mem[0x20] & 0x10))
                    mask &= 0xbf;
                kbc_transmit(dev, dev->p2 & mask);
                break;

            case 0xd1: /* write output port */
                kbd_log("ATkbc: write output port\n");
                dev->kbc_in = 1;
                break;

            case 0xd2: /* write keyboard output buffer */
                kbd_log("ATkbc: write keyboard output buffer\n");
                if (dev->flags & KBC_FLAG_PS2)
                    dev->kbc_in = 1;
                else
                    kbc_transmit(dev, 0x00); /* NCR */
                break;

            case 0xdd: /* disable A20 address line */
            case 0xdf: /* enable A20 address line */
                kbd_log("ATkbc: %sable A20\n", (val == 0xdd) ? "dis" : "en");
                write_output(dev, (dev->p2 & 0xfd) | (val & 0x02));
                break;

            case 0xe0: /* read test inputs */
                kbd_log("ATkbc: read test inputs\n");
                kbc_transmit(dev, 0x00);
                break;

            case 0xe1:
            case 0xea:
                kbd_log("ATkbc: setting P23-P21 to %01X\n", val & 0x0e);
                write_output(dev, (dev->p2 & 0xf1) | (val & 0x0e));
                break;

            default:
                /*
                 * Unrecognized controller command.
                 *
                 * If we have a vendor-specific handler, run
                 * that. Otherwise, or if that handler fails,
                 * log a bad command.
                 */
#ifdef ENABLE_KEYBOARD_AT_LOG
                if (dev->write64_ven)
                    bad = dev->write64_ven(dev, val);

                kbd_log(bad ? "ATkbc: bad controller command %02X\n" : "", val);
#else
                if (dev->write64_ven)
                    (void) dev->write64_ven(dev, val);
#endif
        }

        /* If the command needs data, remember the command. */
        if (dev->kbc_in || (dev->kbc_phase > 0))
            dev->kbc_cmd = val;
    }
}

static void
kbc_dev_data_to_ob(atkbd_t *dev, uint8_t channel)
{
    dev->kbc_written[channel] = 0;
    kbd_log("ATkbd: Forwarding %02X from channel %i...\n", dev->kbc_data[channel], channel);
    kbc_send_to_ob(dev, dev->kbc_data[channel], channel, 0x00);
}

static void
kbc_main_loop_scan(atkbd_t *dev)
{
    uint8_t port_dis = dev->mem[0x20] & 0x30;
    uint8_t ps2      = (dev->flags & KBC_FLAG_PS2);

    if (!ps2)
        port_dis |= 0x20;

    if (!(dev->status & STAT_OFULL)) {
        if (port_dis & 0x20) {
            if (!(port_dis & 0x10)) {
                kbd_log("ATkbc: kbc_process()\n"
                        "ATkbc:     Main loop\n"
                        "ATkbc:         Scan: AUX DIS, KBD EN\n");
                // kbd_log("ATkbc:         Scan: AUX DIS, KBD EN\n");
                /* Enable communication with keyboard. */
                dev->p2 &= 0xbf;
                dev->kbd_inhibit = 0;
                kbc_wait(dev, 1);
            }
#ifdef ENABLE_KEYBOARD_AT_LOG
            else {
                kbd_log("ATkbc: kbc_process()\n"
                        "ATkbc:     Main loop\n"
                        "ATkbc:         Scan: AUX DIS, KBD DIS\n");
                // kbd_log("ATkbc:         Scan: AUX DIS, KBD DIS\n");
            }
#endif
        } else {
            /* Enable communication with mouse. */
            dev->p2 &= 0xf7;
            dev->mouse_inhibit = 0;
            if (dev->mem[0x20] & 0x10) {
                kbd_log("ATkbc: kbc_process()\n"
                        "ATkbc:     Main loop\n"
                        "ATkbc:         Scan: AUX EN , KBD DIS\n");
                // kbd_log("ATkbc:         Scan: AUX EN , KBD DIS\n");
                kbc_wait(dev, 2);
            } else {
                /* Enable communication with keyboard. */
                kbd_log("ATkbc: kbc_process()\n"
                        "ATkbc:     Main loop\n"
                        "ATkbc:         Scan: AUX EN , KBD EN\n");
                // kbd_log("ATkbc:         Scan: AUX EN , KBD EN\n");
                dev->p2 &= 0xbf;
                dev->kbd_inhibit = 0;
                kbc_wait(dev, 3);
            }
        }
    }
#ifdef ENABLE_KEYBOARD_AT_LOG
    else {
        kbd_log("ATkbc: kbc_process()\n"
                "ATkbc:     Main loop\n"
                "ATkbc:         Scan: IBF not full and OBF full, do nothing\n");
        // kbd_log("ATkbc:         Scan: IBF not full and OBF full, do nothing\n");
    }
#endif
}

static void
kbc_process_ib(atkbd_t *dev)
{
    dev->status &= ~STAT_IFULL;

    if (dev->status & STAT_CD) {
        dev->kbc_in_cmd = 1;
        kbc_command(dev);

        if ((dev->kbc_phase == 0) && !dev->kbc_in)
            dev->kbc_in_cmd = 0;
        else
            return;
    } else {
        dev->mem[0x20] &= ~0x10;
        dev->kbd_data              = dev->ib;
        dev->kbd_written           = 1;
        dev->kbc_wait_for_response = 1;
    }

    dev->kbc_poll_phase = KBC_MAIN_LOOP;
    if (!dev->kbc_wait_for_response)
        kbc_main_loop_scan(dev);
}

static void
kbc_wait(atkbd_t *dev, uint8_t flags)
{
    if ((flags & 1) && dev->kbc_written[1]) {
        /* Disable communication with mouse. */
        dev->p2 |= 0x08;
        dev->mouse_inhibit = 1;
        /* Send keyboard byte to host. */
        kbc_dev_data_to_ob(dev, CHANNEL_KBD);
        dev->kbc_poll_phase = KBC_MAIN_LOOP;
    } else if ((flags & 2) && dev->kbc_written[2]) {
        /* Disable communication with keyboard. */
        dev->p2 |= 0x40;
        dev->kbd_inhibit = 1;
        /* Send mouse byte to host. */
        kbc_dev_data_to_ob(dev, CHANNEL_MOUSE);
        dev->kbc_poll_phase = KBC_MAIN_LOOP;
    } else if (dev->status & STAT_IFULL) {
        /* Disable communication with keyboard and mouse. */
        dev->p2 |= 0x48;
        dev->kbd_inhibit = dev->mouse_inhibit = 1;
        kbc_process_ib(dev);
    } else
        dev->kbc_poll_phase = KBC_WAIT | flags;
}

/* Controller processing */
static void
kbc_process(atkbd_t *dev)
{
    // kbd_log("ATkbc: kbc_process()\n");

    /* If we're waiting for the response from the keyboard or mouse, do nothing
       until the device has repsonded back. */
    if (dev->kbc_wait_for_response > 0) {
        if (dev->kbc_written[dev->kbc_wait_for_response])
            dev->kbc_wait_for_response = 0;
        else
            return;
    }

    if (dev->kbc_send_pending) {
        kbd_log("ATkbc: Sending delayed %02X on channel %i with high status %02X\n",
                dev->kbc_to_send, dev->kbc_channel, dev->kbc_stat_hi);
        kbc_send_to_ob(dev, dev->kbc_to_send, dev->kbc_channel, dev->kbc_stat_hi);
        dev->kbc_send_pending = 0;
    }

    if (dev->kbc_poll_phase == KBC_RESET) {
        kbd_log("ATkbc: kbc_process()\n"
                "ATkbc:     Reset loop()\n");

        if (dev->status & STAT_IFULL) {
            dev->status &= ~STAT_IFULL;

            if ((dev->status & STAT_CD) && (dev->ib == 0xaa)) {
                dev->kbc_in_cmd = 1;
                kbc_command(dev);

                if ((dev->kbc_phase == 0) && !dev->kbc_in)
                    dev->kbc_in_cmd = 0;

                dev->kbc_poll_phase = KBC_MAIN_LOOP;
            }
        }

        return;
    }

    if (dev->kbc_in_cmd || (dev->kbc_phase > 0) || dev->kbc_in) {
        kbd_log("ATkbc: kbc_process()\n"
                "ATkbc:     In a command\n");
        if (!dev->kbc_in && (dev->status & STAT_OFULL)) {
            kbd_log("ATkbc:         !dev->kbc_in && (dev->status & STAT_OFULL)\n");
            return; /* We do not want input and we're waiting for the host to read the data
                       we transmitted, but it has not done that yet, do nothing. */
        } else if (dev->kbc_in && !(dev->status & STAT_IFULL)) {
            kbd_log("ATkbc:         dev->kbc_in && !(dev->status & STAT_IFULL)\n");
            return; /* We want input and the host has not provided us with any yet, do nothing. */
        }
#ifdef ENABLE_KEYBOARD_AT_LOG
        else
            kbd_log("ATkbc:         Normal condition\n");
#endif

        if (dev->status & STAT_IFULL) {
            dev->status &= ~STAT_IFULL;

            if (dev->status & STAT_CD) {
                kbd_log("ATkbc:         Resetting command\n");
                dev->kbc_phase = 0;
                dev->kbc_in    = 0;
            }
        }

        /* Process command. */
        kbc_command(dev);

        if ((dev->kbc_phase == 0) && !dev->kbc_in)
            dev->kbc_in_cmd = 0;
        else
            return;

        if (!(dev->status & STAT_OFULL))
            kbc_main_loop_scan(dev);
        /* Make absolutely sure to do nothing if OBF is full and IBF is empty. */
    } else if (!(dev->status & STAT_OFULL) || (dev->status & STAT_IFULL))
        switch (dev->kbc_poll_phase) {
            case KBC_MAIN_LOOP:
                // kbd_log("ATkbc:     Main loop\n");
                if (dev->status & STAT_IFULL) {
                    kbd_log("ATkbc: kbc_process()\n"
                            "ATkbc:     Main loop\n"
                            "ATkbc:         IBF full, process\n");
                    kbc_process_ib(dev);
                } else
                    kbc_main_loop_scan(dev);
                break;
            case KBC_WAIT_FOR_KBD:
            case KBC_WAIT_FOR_MOUSE:
            case KBC_WAIT_FOR_BOTH:
                kbd_log("ATkbc: kbc_process()\n"
                        "ATkbc:     Scan: Phase %i\n",
                        dev->kbc_poll_phase);
                kbc_wait(dev, dev->kbc_poll_phase & 3);
                break;
            default:
                kbd_log("ATkbc: kbc_process()\n"
                        "ATkbc:     Scan: Invalid phase %i\n",
                        dev->kbc_poll_phase);
                break;
        }
}

static void
kbd_poll(void *priv)
{
    atkbd_t *dev = (atkbd_t *) priv;

    timer_advance_u64(&dev->send_delay_timer, (100ULL * TIMER_USEC));

    /* We process all three devices at the same time, in an arbitrary order. */

    /* Keyboard processing */
    kbd_process(dev);

    /* TODO: Mouse processing */
    // mouse_process(dev);

    /* Controller processing */
    kbc_process(dev);
}

static void
add_data_vals(atkbd_t *dev, uint8_t *val, uint8_t len)
{
    int i;

    for (i = 0; i < len; i++)
        add_data_kbd_queue(dev, 0, val[i]);
}

static void
add_data_kbd(uint16_t val)
{
    atkbd_t *dev = SavedKbd;
    uint8_t  fake_shift[4];
    uint8_t  num_lock = 0, shift_states = 0;
    uint8_t  kbc_ven = dev->flags & KBC_VEN_MASK;

    if (dev->kbd_in || (dev->kbd_phase > 0))
        return;

    keyboard_get_states(NULL, &num_lock, NULL);
    shift_states = keyboard_get_shift() & STATE_SHIFT_MASK;

    /* Test for T3100E 'Fn' key (Right Alt / Right Ctrl) */
    if ((dev != NULL) && (kbc_ven == KBC_VEN_TOSHIBA) && (keyboard_recv(0x138) || keyboard_recv(0x11d)))
        switch (val) {
            case 0x4f:
                t3100e_notify_set(0x01);
                break; /* End */
            case 0x50:
                t3100e_notify_set(0x02);
                break; /* Down */
            case 0x51:
                t3100e_notify_set(0x03);
                break; /* PgDn */
            case 0x52:
                t3100e_notify_set(0x04);
                break; /* Ins */
            case 0x53:
                t3100e_notify_set(0x05);
                break; /* Del */
            case 0x54:
                t3100e_notify_set(0x06);
                break; /* SysRQ */
            case 0x45:
                t3100e_notify_set(0x07);
                break; /* NumLock */
            case 0x46:
                t3100e_notify_set(0x08);
                break; /* ScrLock */
            case 0x47:
                t3100e_notify_set(0x09);
                break; /* Home */
            case 0x48:
                t3100e_notify_set(0x0a);
                break; /* Up */
            case 0x49:
                t3100e_notify_set(0x0b);
                break; /* PgUp */
            case 0x4A:
                t3100e_notify_set(0x0c);
                break; /* Keypad -*/
            case 0x4B:
                t3100e_notify_set(0x0d);
                break; /* Left */
            case 0x4C:
                t3100e_notify_set(0x0e);
                break; /* KP 5 */
            case 0x4D:
                t3100e_notify_set(0x0f);
                break; /* Right */
        }

    switch (val) {
        case FAKE_LSHIFT_ON:
            kbd_log("fake left shift on, scan code: ");
            if (num_lock) {
                if (shift_states) {
                    kbd_log("N/A (one or both shifts on)\n");
                    break;
                } else {
                    /* Num lock on and no shifts are pressed, send non-inverted fake shift. */
                    switch (keyboard_mode & 0x02) {
                        case 1:
                            fake_shift[0] = 0xe0;
                            fake_shift[1] = 0x2a;
                            add_data_vals(dev, fake_shift, 2);
                            break;

                        case 2:
                            fake_shift[0] = 0xe0;
                            fake_shift[1] = 0x12;
                            add_data_vals(dev, fake_shift, 2);
                            break;

                        default:
                            kbd_log("N/A (scan code set %i)\n", keyboard_mode & 0x02);
                            break;
                    }
                }
            } else {
                if (shift_states & STATE_LSHIFT) {
                    /* Num lock off and left shift pressed. */
                    switch (keyboard_mode & 0x02) {
                        case 1:
                            fake_shift[0] = 0xe0;
                            fake_shift[1] = 0xaa;
                            add_data_vals(dev, fake_shift, 2);
                            break;

                        case 2:
                            fake_shift[0] = 0xe0;
                            fake_shift[1] = 0xf0;
                            fake_shift[2] = 0x12;
                            add_data_vals(dev, fake_shift, 3);
                            break;

                        default:
                            kbd_log("N/A (scan code set %i)\n", keyboard_mode & 0x02);
                            break;
                    }
                }
                if (shift_states & STATE_RSHIFT) {
                    /* Num lock off and right shift pressed. */
                    switch (keyboard_mode & 0x02) {
                        case 1:
                            fake_shift[0] = 0xe0;
                            fake_shift[1] = 0xb6;
                            add_data_vals(dev, fake_shift, 2);
                            break;

                        case 2:
                            fake_shift[0] = 0xe0;
                            fake_shift[1] = 0xf0;
                            fake_shift[2] = 0x59;
                            add_data_vals(dev, fake_shift, 3);
                            break;

                        default:
                            kbd_log("N/A (scan code set %i)\n", keyboard_mode & 0x02);
                            break;
                    }
                }
                kbd_log(shift_states ? "" : "N/A (both shifts off)\n");
            }
            break;

        case FAKE_LSHIFT_OFF:
            kbd_log("fake left shift on, scan code: ");
            if (num_lock) {
                if (shift_states) {
                    kbd_log("N/A (one or both shifts on)\n");
                    break;
                } else {
                    /* Num lock on and no shifts are pressed, send non-inverted fake shift. */
                    switch (keyboard_mode & 0x02) {
                        case 1:
                            fake_shift[0] = 0xe0;
                            fake_shift[1] = 0xaa;
                            add_data_vals(dev, fake_shift, 2);
                            break;

                        case 2:
                            fake_shift[0] = 0xe0;
                            fake_shift[1] = 0xf0;
                            fake_shift[2] = 0x12;
                            add_data_vals(dev, fake_shift, 3);
                            break;

                        default:
                            kbd_log("N/A (scan code set %i)\n", keyboard_mode & 0x02);
                            break;
                    }
                }
            } else {
                if (shift_states & STATE_LSHIFT) {
                    /* Num lock off and left shift pressed. */
                    switch (keyboard_mode & 0x02) {
                        case 1:
                            fake_shift[0] = 0xe0;
                            fake_shift[1] = 0x2a;
                            add_data_vals(dev, fake_shift, 2);
                            break;

                        case 2:
                            fake_shift[0] = 0xe0;
                            fake_shift[1] = 0x12;
                            add_data_vals(dev, fake_shift, 2);
                            break;

                        default:
                            kbd_log("N/A (scan code set %i)\n", keyboard_mode & 0x02);
                            break;
                    }
                }
                if (shift_states & STATE_RSHIFT) {
                    /* Num lock off and right shift pressed. */
                    switch (keyboard_mode & 0x02) {
                        case 1:
                            fake_shift[0] = 0xe0;
                            fake_shift[1] = 0x36;
                            add_data_vals(dev, fake_shift, 2);
                            break;

                        case 2:
                            fake_shift[0] = 0xe0;
                            fake_shift[1] = 0x59;
                            add_data_vals(dev, fake_shift, 2);
                            break;

                        default:
                            kbd_log("N/A (scan code set %i)\n", keyboard_mode & 0x02);
                            break;
                    }
                }
                kbd_log(shift_states ? "" : "N/A (both shifts off)\n");
            }
            break;

        default:
            add_data_kbd_queue(dev, 0, val);
            break;
    }

    if (sc_or == 0x80)
        sc_or = 0;
}

static void
pulse_poll(void *priv)
{
    atkbd_t *dev = (atkbd_t *) priv;

    kbd_log("pulse_poll(): Output port now: %02X\n", dev->p2 | dev->old_p2);
    write_output(dev, dev->p2 | dev->old_p2);
}

static uint8_t
write64_generic(void *priv, uint8_t val)
{
    atkbd_t *dev = (atkbd_t *) priv;
    uint8_t  current_drive, fixed_bits;
    uint8_t  kbc_ven = dev->flags & KBC_VEN_MASK;

    switch (val) {
        case 0xa4: /* check if password installed */
            if (dev->flags & KBC_FLAG_PS2) {
                kbd_log("ATkbc: check if password installed\n");
                kbc_transmit(dev, (dev->mem[0x50] == 0x00) ? 0xf1 : 0xfa);
                return 0;
            }
            break;

        case 0xa5: /* load security */
            if (dev->flags & KBC_FLAG_PS2) {
                kbd_log("ATkbc: load security\n");
                dev->kbc_in = 1;
                return 0;
            }
            break;

        case 0xa7: /* disable mouse port */
            if (dev->flags & KBC_FLAG_PS2) {
                kbd_log("ATkbc: disable mouse port\n");
                // kbc_transmit(dev, 0);
                return 0;
            }
            break;

        case 0xa8: /*Enable mouse port*/
            if (dev->flags & KBC_FLAG_PS2) {
                kbd_log("ATkbc: enable mouse port\n");
                // kbc_transmit(dev, 1);
                return 0;
            }
            break;

        case 0xa9: /*Test mouse port*/
            kbd_log("ATkbc: test mouse port\n");
            if (dev->flags & KBC_FLAG_PS2) {
                /* No error, this is testing the channel 2 interface. */
                kbc_transmit(dev, 0x00);
                return 0;
            }
            break;

        case 0xaf: /* read keyboard version */
            kbd_log("ATkbc: read keyboard version\n");
            kbc_transmit(dev, 0x00);
            return 0;

        case 0xc0: /* read input port */
            /* IBM PS/1:
                    Bit 2 and 4 ignored (we return always 0),
                    Bit 6 must 1 for 5.25" floppy drive, 0 for 3.5".
               Intel AMI:
                    Bit 2 ignored (we return always 1),
                    Bit 4 must be 1,
                    Bit 6 must be 1 or else error in SMM.
               Acer:
                    Bit 2 must be 0 (and Acer V10 disables CMOS setup if it's 1),
                    Bit 4 must be 0,
                    Bit 6 ignored.
               Packard Bell PB450:
                    Bit 2 must be 1.
               P6RP4:
                    Bit 2 must be 1 or CMOS setup is disabled.
                    Bit 5 must be 1 or the BIOS ends in an infinite reset loop. */
            kbd_log("ATkbc: read input port\n");
            fixed_bits = 4;
            /* The SMM handlers of Intel AMI Pentium BIOS'es expect bit 6 to be set. */
            if (kbc_ven == KBC_VEN_INTEL_AMI)
                fixed_bits |= 0x40;
            if (kbc_ven == KBC_VEN_IBM_PS1) {
                current_drive = fdc_get_current_drive();
                kbc_transmit(dev, dev->p1 | fixed_bits | (fdd_is_525(current_drive) ? 0x40 : 0x00));
                dev->p1 = ((dev->p1 + 1) & 3) | (dev->p1 & 0xfc) | (fdd_is_525(current_drive) ? 0x40 : 0x00);
            } else if (kbc_ven == KBC_VEN_NCR) {
                /* switch settings
                 * bit 7: keyboard disable
                 * bit 6: display type (0 color, 1 mono)
                 * bit 5: power-on default speed (0 high, 1 low)
                 * bit 4: sense RAM size (0 unsupported, 1 512k on system board)
                 * bit 3: coprocessor detect
                 * bit 2: unused
                 * bit 1: high/auto speed
                 * bit 0: dma mode
                 */
                kbc_transmit(dev, (dev->p1 | fixed_bits | (video_is_mda() ? 0x40 : 0x00) | (hasfpu ? 0x08 : 0x00)) & 0xdf);
                dev->p1 = ((dev->p1 + 1) & 3) | (dev->p1 & 0xfc);
            } else {
                pclog("[%04X:%08X] Reading %02X from input port\n", CS, cpu_state.pc, ((dev->p1 | fixed_bits) & 0xf0) | 0x0c);
                if ((dev->flags & KBC_FLAG_PS2) && ((dev->flags & KBC_VEN_MASK) != KBC_VEN_INTEL_AMI))
                    // kbc_transmit(dev, ((dev->p1 | fixed_bits) & 0xf0) | 0x0c);
                    kbc_transmit(dev, ((dev->p1 | fixed_bits) & 0xf0) | 0x08);
                // kbc_transmit(dev, (dev->p1 | fixed_bits) & (((dev->flags & KBC_VEN_MASK) == KBC_VEN_ACER) ? 0xeb : 0xef));
                else
                    kbc_transmit(dev, dev->p1 | fixed_bits);
                dev->p1 = ((dev->p1 + 1) & 3) | (dev->p1 & 0xfc);
            }
            return 0;

        case 0xd3: /* write mouse output buffer */
            if (dev->flags & KBC_FLAG_PS2) {
                kbd_log("ATkbc: write mouse output buffer\n");
                dev->kbc_in = 1;
                return 0;
            }
            break;

        case 0xd4: /* write to mouse */
            kbd_log("ATkbc: write to mouse\n");
            dev->kbc_in = 1;
            return 0;

        case 0xf0 ... 0xff:
            kbd_log("ATkbc: pulse %01X\n", val & 0x0f);
            pulse_output(dev, val & 0x0f);
            return 0;
    }

    kbd_log("ATkbc: bad command %02X\n", val);
    return 1;
}

static uint8_t
write60_ami(void *priv, uint8_t val)
{
    atkbd_t *dev   = (atkbd_t *) priv;
    uint16_t index = 0x00c0;

    switch (dev->kbc_cmd) {
        /* 0x40 - 0x5F are aliases for 0x60 - 0x7F */
        case 0x40 ... 0x5f:
            kbd_log("ATkbc: AMI - alias write to %08X\n", dev->kbc_cmd);
            if (dev->kbc_cmd == 0x40)
                write_cmd(dev, val);
            else
                dev->mem[(dev->kbc_cmd & 0x1f) + 0x20] = val;
            return 0;

        case 0xaf: /* set extended controller RAM */
            kbd_log("ATkbc: AMI - set extended controller RAM, input phase %i\n", dev->secr_phase);
            if (dev->secr_phase == 0) {
                dev->mem_index = val;
                dev->kbc_in    = 1;
                dev->secr_phase++;
            } else if (dev->secr_phase == 1) {
                if (dev->mem_index == 0x20)
                    write_cmd(dev, val);
                else
                    dev->mem[dev->mem_index] = val;
                dev->secr_phase = 0;
            }
            return 0;

        case 0xb8:
            kbd_log("ATkbc: AMI MegaKey - memory index %02X\n", val);
            dev->mem_index = val;
            return 0;

        case 0xbb:
            kbd_log("ATkbc: AMI MegaKey - write %02X to memory index %02X\n", val, dev->mem_index);
            if (dev->mem_index >= 0x80) {
                switch (dev->mem[0x9b] & 0xc0) {
                    case 0x00:
                        index = 0x0080;
                        break;
                    case 0x40:
                    case 0x80:
                        index = 0x0000;
                        break;
                    case 0xc0:
                        index = 0x0100;
                        break;
                }
                dev->mem[index + dev->mem_index] = val;
            } else if (dev->mem_index == 0x60)
                write_cmd(dev, val);
            else if (dev->mem_index == 0x42)
                dev->status = val;
            else if (dev->mem_index >= 0x40)
                dev->mem[dev->mem_index - 0x40] = val;
            else
                dev->mem_int[dev->mem_index] = val;
            return 0;

        case 0xbd:
            kbd_log("ATkbc: AMI MegaKey - write %02X to config index %02X\n", val, dev->mem_index);
            switch (dev->mem_index) {
                case 0x00: /* STAT8042 */
                    dev->status = val;
                    break;
                case 0x01: /* Password_ptr */
                    dev->mem[0x1c] = val;
                    break;
                case 0x02: /* Wakeup_Tsk_Reg */
                    dev->mem[0x1e] = val;
                    break;
                case 0x03: /* CCB */
                    write_cmd(dev, val);
                    break;
                case 0x04: /* Debounce_time */
                    dev->mem[0x4d] = val;
                    break;
                case 0x05: /* Pulse_Width */
                    dev->mem[0x4e] = val;
                    break;
                case 0x06: /* Pk_sel_byte */
                    dev->mem[0x4c] = val;
                    break;
                case 0x07: /* Func_Tsk_Reg */
                    dev->mem[0x7e] = val;
                    break;
                case 0x08: /* TypematicRate */
                    dev->mem[0x80] = val;
                    break;
                case 0x09: /* Led_Flag_Byte */
                    dev->mem[0x81] = val;
                    break;
                case 0x0a: /* Kbms_Command_St */
                    dev->mem[0x87] = val;
                    break;
                case 0x0b: /* Delay_Count_Byte */
                    dev->mem[0x86] = val;
                    break;
                case 0x0c: /* KBC_Flags */
                    dev->mem[0x9b] = val;
                    break;
                case 0x0d: /* SCODE_HK1 */
                    dev->mem[0x50] = val;
                    break;
                case 0x0e: /* SCODE_HK2 */
                    dev->mem[0x51] = val;
                    break;
                case 0x0f: /* SCODE_HK3 */
                    dev->mem[0x52] = val;
                    break;
                case 0x10: /* SCODE_HK4 */
                    dev->mem[0x53] = val;
                    break;
                case 0x11: /* SCODE_HK5 */
                    dev->mem[0x54] = val;
                    break;
                case 0x12: /* SCODE_HK6 */
                    dev->mem[0x55] = val;
                    break;
                case 0x13: /* TASK_HK1 */
                    dev->mem[0x56] = val;
                    break;
                case 0x14: /* TASK_HK2 */
                    dev->mem[0x57] = val;
                    break;
                case 0x15: /* TASK_HK3 */
                    dev->mem[0x58] = val;
                    break;
                case 0x16: /* TASK_HK4 */
                    dev->mem[0x59] = val;
                    break;
                case 0x17: /* TASK_HK5 */
                    dev->mem[0x5a] = val;
                    break;
                /* The next 4 bytes have uncertain correspondences. */
                case 0x18: /* Batt_Poll_delay_Time */
                    dev->mem[0x5b] = val;
                    break;
                case 0x19: /* Batt_Alarm_Reg1 */
                    dev->mem[0x5c] = val;
                    break;
                case 0x1a: /* Batt_Alarm_Reg2 */
                    dev->mem[0x5d] = val;
                    break;
                case 0x1b: /* Batt_Alarm_Tsk_Reg */
                    dev->mem[0x5e] = val;
                    break;
                case 0x1c: /* Kbc_State1 */
                    dev->mem[0x9d] = val;
                    break;
                case 0x1d: /* Aux_Config */
                    dev->mem[0x75] = val;
                    break;
                case 0x1e: /* Kbc_State3 */
                    dev->mem[0x73] = val;
                    break;
            }
            return 0;

        case 0xc1: /* write input port */
            kbd_log("ATkbc: AMI MegaKey - write %02X to input port\n", val);
            dev->p1 = val;
            return 0;

        case 0xcb: /* set keyboard mode */
            kbd_log("ATkbc: AMI - set keyboard mode\n");
            return 0;
    }

    return 1;
}

static uint8_t
write64_ami(void *priv, uint8_t val)
{
    atkbd_t *dev     = (atkbd_t *) priv;
    uint16_t index   = 0x00c0;
    uint8_t  kbc_ven = dev->flags & KBC_VEN_MASK;

    switch (val) {
        case 0x00 ... 0x1f:
            kbd_log("ATkbc: AMI - alias read from %08X\n", val);
            kbc_transmit(dev, dev->mem[val + 0x20]);
            return 0;

        case 0x40 ... 0x5f:
            kbd_log("ATkbc: AMI - alias write to %08X\n", dev->kbc_cmd);
            dev->kbc_in = 1;
            return 0;

        case 0xa0: /* copyright message */
            kbc_transmit(dev, ami_copr[0]);
            dev->kbc_phase = 1;
            return 0;

        case 0xa1: /* get controller version */
            kbd_log("ATkbc: AMI - get controller version\n");
            /*	ASCII		Character	Controller
                    ------------------------------------------
                    N/A		N/A		Keyboard controllers not implementing this command, including
                                                    the Access Methods AMI keyboard controller;
                    0x00		N/A		Various early keyboard controllers, including Award;
                    0x1D		<->*		Ju-Jet keyboard controller, this characters appears on a
                                                    photo of an AMIBIOS screen where a CTRL key press has been
                                                    mistaken for this command's output;
                    0x30		0*		Not actually returned by a keyboard controller, seen on AMI
                                                    Color or later BIOS'es when the command 0xA1 has not returned
                                                    any character;
                    0x35		5		AMI MEGAKEY keyboard controller, 1994 revision, the variant
                                                    found on National Semiconductors and SM(S)C Super I/O chips,
                                                    actually returns 1994 copyright;
                    0x37		7		Unknown keyboard controller, possibly AMI;
                    0x38		8		The earliest American Megatrends AMI keyboard controller,
                                                    also has a later revision that adds command 0xA0;
                    0x39		9		Mentioned in an AMI fax to IBM regarding OS/2 compatibility
                                                    with AMI keyboard controller - called non-standard but not
                                                    non-AMI (perhaps earlier TriGem before 'Z'?);
                    0x41		A		Likely the keyboard controller on the HiNT chipset;
                    0x42		B		AMI B keyboard controller;
                    0x44		D		AMI D keyboard controller;
                    0x45		E		AMI E keyboard controller, presumably from 1989, also seen on
                                                    some ALi M148x AMIBIOS'es due to the keyboard controller's
                                                    command byte from the preceding read command having been
                                                    mixed in;
                    0x46		F		AMI F / AMIKEY keyboard controller, from 1990, also used by
                                                    clones;
                    0x47		G		Unknown keyboard controller, known from an AMIBIOS string;
                    0x48		H		AMI H / AMIKEY-2 keyboard controller, from 1992, also used by
                                                    clones;
                    0x4C		L		Unknown keyboard controller, known from an AMIBIOS string;
                    0x4D		M		MR (Microid Research) keyboard controller from 1991, a later
                                                    one seems to actually be AMIKEY-2;
                    0x4E		N		Unknown keyboard controller;
                    0x50		P		AMI MEGAKEY keyboard controller, original 1993 version;
                    0x51		Q*		Seen on 86Box on some ALi M148x AMIBIOS'es due to the keyboard
                                                    controller emulation being incorrect and causing an even worse
                                                    input mix-up than on real hardware;
                    0x52		R		AMI MEGAKEY keyboard controller, 1994 revision, still returns
                                                    1993 copyright;
                    0x53		S		Unknown keyboard controller, known from an AMIBIOS string;
                    0x54		T*		Seen on one screenshot from PCem, due to the command not
                                                    being implemented and some other output having been mixed in;
                    0x55		U		SARC 6042 keyboard controller, also seen on some ALi M148x
                                                    AMIBIOS'es due to the keyboard controller's command byte from
                                                    the preceding read command having been mixed in;
                    0x5A		Z		TriGem keyboard controller, usually on a square 8742, is a
                                                    customized AMI keyboard controller from 1990.

                    * Never returned by an actual keyboard controller, or at least has not been seen returned by
                      one thus far.
             */
            kbc_transmit(dev, (kbc_ven == KBC_VEN_INTEL_AMI) ? '5' : 'H');
            return 0;

        case 0xa2: /* clear keyboard controller lines P22/P23 */
            if (!(dev->flags & KBC_FLAG_PS2)) {
                kbd_log("ATkbc: AMI - clear KBC lines P22 and P23\n");
                write_output(dev, dev->p2 & 0xf3);
                kbc_transmit(dev, 0x00);
                return 0;
            }
            break;

        case 0xa3: /* set keyboard controller lines P22/P23 */
            if (!(dev->flags & KBC_FLAG_PS2)) {
                kbd_log("ATkbc: AMI - set KBC lines P22 and P23\n");
                write_output(dev, dev->p2 | 0x0c);
                kbc_transmit(dev, 0x00);
                return 0;
            }
            break;

        case 0xa4: /* write clock = low */
            if (!(dev->flags & KBC_FLAG_PS2)) {
                kbd_log("ATkbc: AMI - write clock = low\n");
                dev->ami_stat &= 0xfe;
                return 0;
            }
            break;

        case 0xa5: /* write clock = high */
            if (!(dev->flags & KBC_FLAG_PS2)) {
                kbd_log("ATkbc: AMI - write clock = high\n");
                dev->ami_stat |= 0x01;
                return 0;
            }
            break;

        case 0xa6: /* read clock */
            if (!(dev->flags & KBC_FLAG_PS2)) {
                kbd_log("ATkbc: AMI - read clock\n");
                kbc_transmit(dev, !!(dev->ami_stat & 1));
                return 0;
            }
            break;

        case 0xa7: /* write cache bad */
            if (!(dev->flags & KBC_FLAG_PS2)) {
                kbd_log("ATkbc: AMI - write cache bad\n");
                dev->ami_stat &= 0xfd;
                return 0;
            }
            break;

        case 0xa8: /* write cache good */
            if (!(dev->flags & KBC_FLAG_PS2)) {
                kbd_log("ATkbc: AMI - write cache good\n");
                dev->ami_stat |= 0x02;
                return 0;
            }
            break;

        case 0xa9: /* read cache */
            if (!(dev->flags & KBC_FLAG_PS2)) {
                kbd_log("ATkbc: AMI - read cache\n");
                kbc_transmit(dev, !!(dev->ami_stat & 2));
                return 0;
            }
            break;

        case 0xaf: /* set extended controller RAM */
            kbd_log("ATkbc: set extended controller RAM\n");
            dev->kbc_in = 1;
            return 0;

        case 0xb0 ... 0xb3:
            /* set KBC lines P10-P13 (input port bits 0-3) low */
            kbd_log("ATkbc: set KBC lines P10-P13 (input port bits 0-3) low\n");
            if (!(dev->flags & KBC_FLAG_PS2) || (val > 0xb1)) {
                dev->p1 &= ~(1 << (val & 0x03));
            }
            kbc_transmit(dev, 0x00);
            return 0;

        case 0xb4:
        case 0xb5:
            /* set KBC lines P22-P23 (output port bits 2-3) low */
            kbd_log("ATkbc: set KBC lines P22-P23 (output port bits 2-3) low\n");
            if (!(dev->flags & KBC_FLAG_PS2))
                write_output(dev, dev->p2 & ~(4 << (val & 0x01)));
            kbc_transmit(dev, 0x00);
            return 0;

#if 0
	case 0xb8 ... 0xbb:
#else
        case 0xb9:
#endif
            /* set KBC lines P10-P13 (input port bits 0-3) high */
            kbd_log("ATkbc: set KBC lines P10-P13 (input port bits 0-3) high\n");
            if (!(dev->flags & KBC_FLAG_PS2) || (val > 0xb9)) {
                dev->p1 |= (1 << (val & 0x03));
                kbc_transmit(dev, 0x00);
            }
            return 0;

        case 0xb8:
            kbd_log("ATkbc: AMI MegaKey - memory index\n");
            dev->kbc_in = 1;
            return 0;

        case 0xba:
            kbd_log("ATkbc: AMI MegaKey - read %02X memory from index %02X\n", dev->mem[dev->mem_index], dev->mem_index);
            if (dev->mem_index >= 0x80) {
                switch (dev->mem[0x9b] & 0xc0) {
                    case 0x00:
                        index = 0x0080;
                        break;
                    case 0x40:
                    case 0x80:
                        index = 0x0000;
                        break;
                    case 0xc0:
                        index = 0x0100;
                        break;
                }
                kbc_transmit(dev, dev->mem[index + dev->mem_index]);
            } else if (dev->mem_index == 0x42)
                kbc_transmit(dev, dev->status);
            else if (dev->mem_index >= 0x40)
                kbc_transmit(dev, dev->mem[dev->mem_index - 0x40]);
            else
                kbc_transmit(dev, dev->mem_int[dev->mem_index]);
            return 0;

        case 0xbb:
            kbd_log("ATkbc: AMI MegaKey - write to memory index %02X\n", dev->mem_index);
            dev->kbc_in = 1;
            return 0;

#if 0
	case 0xbc: case 0xbd:
		/* set KBC lines P22-P23 (output port bits 2-3) high */
		kbd_log("ATkbc: set KBC lines P22-P23 (output port bits 2-3) high\n");
		if (!(dev->flags & KBC_FLAG_PS2))
			write_output(dev, dev->p2 | (4 << (val & 0x01)));
		kbc_transmit(dev, 0x00);
		return 0;
#endif

        case 0xbc:
            switch (dev->mem_index) {
                case 0x00: /* STAT8042 */
                    kbc_transmit(dev, dev->status);
                    break;
                case 0x01: /* Password_ptr */
                    kbc_transmit(dev, dev->mem[0x1c]);
                    break;
                case 0x02: /* Wakeup_Tsk_Reg */
                    kbc_transmit(dev, dev->mem[0x1e]);
                    break;
                case 0x03: /* CCB */
                    kbc_transmit(dev, dev->mem[0x20]);
                    break;
                case 0x04: /* Debounce_time */
                    kbc_transmit(dev, dev->mem[0x4d]);
                    break;
                case 0x05: /* Pulse_Width */
                    kbc_transmit(dev, dev->mem[0x4e]);
                    break;
                case 0x06: /* Pk_sel_byte */
                    kbc_transmit(dev, dev->mem[0x4c]);
                    break;
                case 0x07: /* Func_Tsk_Reg */
                    kbc_transmit(dev, dev->mem[0x7e]);
                    break;
                case 0x08: /* TypematicRate */
                    kbc_transmit(dev, dev->mem[0x80]);
                    break;
                case 0x09: /* Led_Flag_Byte */
                    kbc_transmit(dev, dev->mem[0x81]);
                    break;
                case 0x0a: /* Kbms_Command_St */
                    kbc_transmit(dev, dev->mem[0x87]);
                    break;
                case 0x0b: /* Delay_Count_Byte */
                    kbc_transmit(dev, dev->mem[0x86]);
                    break;
                case 0x0c: /* KBC_Flags */
                    kbc_transmit(dev, dev->mem[0x9b]);
                    break;
                case 0x0d: /* SCODE_HK1 */
                    kbc_transmit(dev, dev->mem[0x50]);
                    break;
                case 0x0e: /* SCODE_HK2 */
                    kbc_transmit(dev, dev->mem[0x51]);
                    break;
                case 0x0f: /* SCODE_HK3 */
                    kbc_transmit(dev, dev->mem[0x52]);
                    break;
                case 0x10: /* SCODE_HK4 */
                    kbc_transmit(dev, dev->mem[0x53]);
                    break;
                case 0x11: /* SCODE_HK5 */
                    kbc_transmit(dev, dev->mem[0x54]);
                    break;
                case 0x12: /* SCODE_HK6 */
                    kbc_transmit(dev, dev->mem[0x55]);
                    break;
                case 0x13: /* TASK_HK1 */
                    kbc_transmit(dev, dev->mem[0x56]);
                    break;
                case 0x14: /* TASK_HK2 */
                    kbc_transmit(dev, dev->mem[0x57]);
                    break;
                case 0x15: /* TASK_HK3 */
                    kbc_transmit(dev, dev->mem[0x58]);
                    break;
                case 0x16: /* TASK_HK4 */
                    kbc_transmit(dev, dev->mem[0x59]);
                    break;
                case 0x17: /* TASK_HK5 */
                    kbc_transmit(dev, dev->mem[0x5a]);
                    break;
                /* The next 4 bytes have uncertain correspondences. */
                case 0x18: /* Batt_Poll_delay_Time */
                    kbc_transmit(dev, dev->mem[0x5b]);
                    break;
                case 0x19: /* Batt_Alarm_Reg1 */
                    kbc_transmit(dev, dev->mem[0x5c]);
                    break;
                case 0x1a: /* Batt_Alarm_Reg2 */
                    kbc_transmit(dev, dev->mem[0x5d]);
                    break;
                case 0x1b: /* Batt_Alarm_Tsk_Reg */
                    kbc_transmit(dev, dev->mem[0x5e]);
                    break;
                case 0x1c: /* Kbc_State1 */
                    kbc_transmit(dev, dev->mem[0x9d]);
                    break;
                case 0x1d: /* Aux_Config */
                    kbc_transmit(dev, dev->mem[0x75]);
                    break;
                case 0x1e: /* Kbc_State3 */
                    kbc_transmit(dev, dev->mem[0x73]);
                    break;
                default:
                    kbc_transmit(dev, 0x00);
                    break;
            }
            kbd_log("ATkbc: AMI MegaKey - read from config index %02X\n", dev->mem_index);
            return 0;

        case 0xbd:
            kbd_log("ATkbc: AMI MegaKey - write to config index %02X\n", dev->mem_index);
            dev->kbc_in = 1;
            return 0;

        case 0xc1: /* write input port */
            kbd_log("ATkbc: AMI MegaKey - write input port\n");
            dev->kbc_in = 1;
            return 0;

        case 0xc4:
            /* set KBC line P14 low */
            kbd_log("ATkbc: set KBC line P14 (input port bit 4) low\n");
            dev->p1 &= 0xef;
            kbc_transmit(dev, 0x00);
            return 0;
        case 0xc5:
            /* set KBC line P15 low */
            kbd_log("ATkbc: set KBC line P15 (input port bit 5) low\n");
            dev->p1 &= 0xdf;
            kbc_transmit(dev, 0x00);
            return 0;

        case 0xc8:
        case 0xc9:
            /*
             * (un)block KBC lines P22/P23
             * (allow command D1 to change bits 2/3 of the output port)
             */
            kbd_log("ATkbc: AMI - %sblock KBC lines P22 and P23\n", (val & 1) ? "" : "un");
            dev->p2_locked = (val & 1);
            return 0;

        case 0xcc:
            /* set KBC line P14 high */
            kbd_log("ATkbc: set KBC line P14 (input port bit 4) high\n");
            dev->p1 |= 0x10;
            kbc_transmit(dev, 0x00);
            return 0;
        case 0xcd:
            /* set KBC line P15 high */
            kbd_log("ATkbc: set KBC line P15 (input port bit 5) high\n");
            dev->p1 |= 0x20;
            kbc_transmit(dev, 0x00);
            return 0;

        case 0xef: /* ??? - sent by AMI486 */
            kbd_log("ATkbc: ??? - sent by AMI486\n");
            return 0;
    }

    return write64_generic(dev, val);
}

static uint8_t
write64_ibm_mca(void *priv, uint8_t val)
{
    atkbd_t *dev = (atkbd_t *) priv;

    switch (val) {
        case 0xc1: /*Copy bits 0 to 3 of input port to status bits 4 to 7*/
            kbd_log("ATkbc: copy bits 0 to 3 of input port to status bits 4 to 7\n");
            dev->status &= 0x0f;
            dev->status |= ((((dev->p1 & 0xfc) | 0x84) & 0x0f) << 4);
            return 0;

        case 0xc2: /*Copy bits 4 to 7 of input port to status bits 4 to 7*/
            kbd_log("ATkbc: copy bits 4 to 7 of input port to status bits 4 to 7\n");
            dev->status &= 0x0f;
            dev->status |= (((dev->p1 & 0xfc) | 0x84) & 0xf0);
            return 0;

        case 0xaf:
            kbd_log("ATkbc: bad KBC command AF\n");
            return 1;

        case 0xf0 ... 0xff:
            kbd_log("ATkbc: pulse: %01X\n", (val & 0x03) | 0x0c);
            pulse_output(dev, (val & 0x03) | 0x0c);
            return 0;
    }

    return write64_generic(dev, val);
}

static uint8_t
write60_quadtel(void *priv, uint8_t val)
{
    atkbd_t *dev = (atkbd_t *) priv;

    switch (dev->kbc_cmd) {
        case 0xcf: /*??? - sent by MegaPC BIOS*/
            kbd_log("ATkbc: ??? - sent by MegaPC BIOS\n");
            return 0;
    }

    return 1;
}

static uint8_t
write64_olivetti(void *priv, uint8_t val)
{
    atkbd_t *dev = (atkbd_t *) priv;

    switch (val) {
        /* This appears to be a clone of "Read input port", in which case, the bis would be:
                7: M290 (AT KBC):
                        Keyboard lock (1 = unlocked, 0 = locked);
                   M300 (PS/2 KBC):
                        Bus expansion board present (1 = present, 0 = not present);
                6: Usually:
                        Display (1 = MDA, 0 = CGA, but can have its polarity inverted);
                5: Manufacturing jumper (1 = not installed, 0 = installed (infinite loop));
                4: RAM on motherboard (1 = 256 kB, 0 = 512 kB - which machine actually uses this?);
                3: Fast Ram check (if inactive keyboard works erratically);
                2: Keyboard fuse present
                   This appears to be in-line with PS/2: 1 = no power, 0 = keyboard power normal;
                1: M290 (AT KBC):
                        Unused;
                   M300 (PS/2 KBC):
                        Mouse data in;
                0: M290 (AT KBC):
                        Unused;
                   M300 (PS/2 KBC):
                        Key data in.
        */
        case 0x80: /* Olivetti-specific command */
            /*
             * bit 7: bus expansion board present (M300) / keyboard unlocked (M290)
             * bits 4-6: ???
             * bit 3: fast ram check (if inactive keyboard works erratically)
             * bit 2: keyboard fuse present
             * bits 0-1: ???
             */
            kbc_transmit(dev, 0x0c | (is386 ? 0x00 : 0x80));
            return 0;
    }

    return write64_generic(dev, val);
}

static uint8_t
write64_quadtel(void *priv, uint8_t val)
{
    atkbd_t *dev = (atkbd_t *) priv;

    switch (val) {
        case 0xaf:
            kbd_log("ATkbc: bad KBC command AF\n");
            return 1;

        case 0xcf: /*??? - sent by MegaPC BIOS*/
            kbd_log("ATkbc: ??? - sent by MegaPC BIOS\n");
            dev->kbc_in = 1;
            return 0;
    }

    return write64_generic(dev, val);
}

static uint8_t
write60_toshiba(void *priv, uint8_t val)
{
    atkbd_t *dev = (atkbd_t *) priv;

    switch (dev->kbc_cmd) {
        case 0xb6: /* T3100e - set color/mono switch */
            kbd_log("ATkbc: T3100e - set color/mono switch\n");
            t3100e_mono_set(val);
            return 0;
    }

    return 1;
}

static uint8_t
write64_toshiba(void *priv, uint8_t val)
{
    atkbd_t *dev = (atkbd_t *) priv;

    switch (val) {
        case 0xaf:
            kbd_log("ATkbc: bad KBC command AF\n");
            return 1;

        case 0xb0: /* T3100e: Turbo on */
            kbd_log("ATkbc: T3100e: Turbo on\n");
            t3100e_turbo_set(1);
            return 0;

        case 0xb1: /* T3100e: Turbo off */
            kbd_log("ATkbc: T3100e: Turbo off\n");
            t3100e_turbo_set(0);
            return 0;

        case 0xb2: /* T3100e: Select external display */
            kbd_log("ATkbc: T3100e: Select external display\n");
            t3100e_display_set(0x00);
            return 0;

        case 0xb3: /* T3100e: Select internal display */
            kbd_log("ATkbc: T3100e: Select internal display\n");
            t3100e_display_set(0x01);
            return 0;

        case 0xb4: /* T3100e: Get configuration / status */
            kbd_log("ATkbc: T3100e: Get configuration / status\n");
            kbc_transmit(dev, t3100e_config_get());
            return 0;

        case 0xb5: /* T3100e: Get colour / mono byte */
            kbd_log("ATkbc: T3100e: Get colour / mono byte\n");
            kbc_transmit(dev, t3100e_mono_get());
            return 0;

        case 0xb6: /* T3100e: Set colour / mono byte */
            kbd_log("ATkbc: T3100e: Set colour / mono byte\n");
            dev->kbc_in = 1;
            return 0;

        case 0xb7: /* T3100e: Emulate PS/2 keyboard */
        case 0xb8: /* T3100e: Emulate AT keyboard */
            dev->flags &= ~KBC_FLAG_PS2;
            if (val == 0xb7) {
                kbd_log("ATkbc: T3100e: Emulate PS/2 keyboard\n");
                dev->flags |= KBC_FLAG_PS2;
            }
#ifdef ENABLE_KEYBOARD_AT_LOG
            else
                kbd_log("ATkbc: T3100e: Emulate AT keyboard\n");
#endif
            return 0;

        case 0xbb: /* T3100e: Read 'Fn' key.
                      Return it for right Ctrl and right Alt; on the real
                      T3100e, these keystrokes could only be generated
                      using 'Fn'. */
            kbd_log("ATkbc: T3100e: Read 'Fn' key\n");
            if (keyboard_recv(0xb8) || /* Right Alt */
                keyboard_recv(0x9d))   /* Right Ctrl */
                kbc_transmit(dev, 0x04);
            else
                kbc_transmit(dev, 0x00);
            return 0;

        case 0xbc: /* T3100e: Reset Fn+Key notification */
            kbd_log("ATkbc: T3100e: Reset Fn+Key notification\n");
            t3100e_notify_set(0x00);
            return 0;

        case 0xc0: /*Read input port*/
            kbd_log("ATkbc: read input port\n");

            /* The T3100e returns all bits set except bit 6 which
             * is set by t3100e_mono_set() */
            dev->p1 = (t3100e_mono_get() & 1) ? 0xff : 0xbf;
            kbc_transmit(dev, dev->p1);
            return 0;
    }

    return write64_generic(dev, val);
}

static void
kbd_write(uint16_t port, uint8_t val, void *priv)
{
    atkbd_t *dev = (atkbd_t *) priv;

    kbd_log("[%04X:%08X] ATkbc: write(%04X, %02X)\n", CS, cpu_state.pc, port, val);

    switch (port) {
        case 0x60:
            dev->status = (dev->status & ~STAT_CD) | STAT_IFULL;
            dev->ib     = val;
            // kbd_status("Write %02X: %02X, Status = %02X\n", port, val, dev->status);

#if 0
		if ((dev->fast_a20_phase == 1)/* && ((val == 0xdd) || (val == 0xdf))*/) {
			dev->status &= ~STAT_IFULL;
			write_output(dev, val);
			dev->fast_a20_phase = 0;
		}
#endif
            break;
        case 0x64:
            dev->status |= (STAT_CD | STAT_IFULL);
            dev->ib = val;
            // kbd_status("Write %02X: %02X, Status = %02X\n", port, val, dev->status);

#if 0
		if (val == 0xd1) {
			dev->status &= ~STAT_IFULL;
			dev->fast_a20_phase = 1;
		} else if (val == 0xfe) {
			dev->status &= ~STAT_IFULL;
			pulse_output(dev, 0x0e);
		} else if ((val == 0xad) || (val == 0xae)) {
			dev->status &= ~STAT_IFULL;
			if (val & 0x01)
				dev->mem[0x20] |= 0x10;
			else
				dev->mem[0x20] &= ~0x10;
		} else if (val == 0xa1) {
			dev->status &= ~STAT_IFULL;
			kbc_send_to_ob(dev, 'H', 0, 0x00);
		}
#else
            /* if (val == 0xa1) {
                    dev->status &= ~STAT_IFULL;
                    kbc_send_to_ob(dev, 'H', 0, 0x00);
            } */
            // kbc_process(dev);
#endif
            break;
    }
}

static uint8_t
kbd_read(uint16_t port, void *priv)
{
    atkbd_t *dev = (atkbd_t *) priv;
    uint8_t  ret = 0xff;

    // if (dev->flags & KBC_FLAG_PS2)
    // cycles -= ISA_CYCLES(8);

    switch (port) {
        case 0x60:
            ret = dev->ob;
            dev->status &= ~STAT_OFULL;
            picintc(dev->last_irq);
            dev->last_irq = 0;
            break;

        case 0x64:
            ret = dev->status;
            break;

        default:
            kbd_log("ATkbc: read(%04x) invalid!\n", port);
            break;
    }

    kbd_log("[%04X:%08X] ATkbc: read(%04X) = %02X\n", CS, cpu_state.pc, port, ret);

    return (ret);
}

static void
kbd_reset(void *priv)
{
    atkbd_t *dev = (atkbd_t *) priv;

    if (dev == NULL)
        return;

    dev->status &= ~(STAT_IFULL | STAT_OFULL | STAT_CD);
    dev->last_irq = 0;
    picintc(1 << 1);
    picintc(1 << 12);
    dev->secr_phase = 0;
    dev->kbd_in     = 0;
    dev->ob         = 0xff;

    sc_or = 0;
}

static void
kbd_power_on(atkbd_t *dev)
{
    int     i;
    uint8_t kbc_ven = dev->flags & KBC_VEN_MASK;

    kbd_reset(dev);

    dev->status = STAT_UNLOCKED;
    /* Write the value here first, so that we don't hit a pulse reset. */
    dev->p2 = 0xcf;
    write_output(dev, 0xcf);
    dev->mem[0x20] = 0x01;
    dev->mem[0x20] |= CCB_TRANSLATE;
    dev->ami_mode = !!(dev->flags & KBC_FLAG_PS2);

    /* Set up the correct Video Type bits. */
    dev->p1 = video_is_mda() ? 0xf0 : 0xb0;
    if ((kbc_ven == KBC_VEN_XI8088) || (kbc_ven == KBC_VEN_ACER))
        dev->p1 ^= 0x40;
    if ((kbc_ven == KBC_VEN_AMI) || (dev->flags & KBC_FLAG_PS2))
        dev->inhibit = ((dev->p1 & 0x80) >> 3);
    else
        dev->inhibit = 0x10;
    kbd_log("ATkbc: input port = %02x\n", dev->p1);

    /* Enable keyboard, disable mouse. */
    set_enable_kbd(dev, 1);
    keyboard_scan = 1;
    set_enable_mouse(dev, 0);
    mouse_scan = 0;

    dev->mem[0x31] = 0xfe;

    keyboard_mode = 0x02 | (dev->mem[0x20] & CCB_TRANSLATE);

    for (i = 1; i <= 2; i++)
        kbc_queue_reset(i);

    memset(keyboard_set3_flags, 0, 512);

    set_scancode_map(dev);
}

/* Reset the AT keyboard - this is needed for the PCI TRC and is done
   until a better solution is found. */
void
keyboard_at_reset(void)
{
    kbd_reset(SavedKbd);
}

static void
kbd_close(void *priv)
{
    atkbd_t *dev = (atkbd_t *) priv;

    kbd_reset(dev);

    /* Stop timers. */
    timer_disable(&dev->send_delay_timer);

    keyboard_scan = 0;
    keyboard_send = NULL;

    /* Disable the scancode maps. */
    keyboard_set_table(NULL);

    SavedKbd = NULL;
    free(dev);
}

static void *
kbd_init(const device_t *info)
{
    atkbd_t *dev;

    dev = (atkbd_t *) malloc(sizeof(atkbd_t));
    memset(dev, 0x00, sizeof(atkbd_t));

    dev->flags = info->local;

    video_reset(gfxcard);
    dev->kbc_poll_phase = KBC_RESET;
    kbd_send_to_host(dev, 0xaa);

    io_sethandler(0x0060, 1, kbd_read, NULL, NULL, kbd_write, NULL, NULL, dev);
    io_sethandler(0x0064, 1, kbd_read, NULL, NULL, kbd_write, NULL, NULL, dev);
    keyboard_send = add_data_kbd;

    timer_add(&dev->send_delay_timer, kbd_poll, dev, 1);
    timer_add(&dev->pulse_cb, pulse_poll, dev, 0);

    dev->write60_ven = NULL;
    dev->write64_ven = NULL;

    switch (dev->flags & KBC_VEN_MASK) {
        case KBC_VEN_ACER:
        case KBC_VEN_GENERIC:
        case KBC_VEN_NCR:
        case KBC_VEN_IBM_PS1:
        case KBC_VEN_XI8088:
            dev->write64_ven = write64_generic;
            break;

        case KBC_VEN_OLIVETTI:
            /* The Olivetti controller is a special case - starts directly in the
               main loop instead of the reset loop. */
            dev->kbc_poll_phase = KBC_MAIN_LOOP;
            dev->write64_ven    = write64_olivetti;
            break;

        case KBC_VEN_AMI:
        case KBC_VEN_INTEL_AMI:
            dev->write60_ven = write60_ami;
            dev->write64_ven = write64_ami;
            break;

        case KBC_VEN_IBM_MCA:
            dev->write64_ven = write64_ibm_mca;
            break;

        case KBC_VEN_QUADTEL:
            dev->write60_ven = write60_quadtel;
            dev->write64_ven = write64_quadtel;
            break;

        case KBC_VEN_SAMSUNG:
            //		dev->write60_ven = write60_samsung;
            //		dev->write64_ven = write64_samsung;
            break;

        case KBC_VEN_TOSHIBA:
            dev->write60_ven = write60_toshiba;
            dev->write64_ven = write64_toshiba;
            break;
    }

    kbd_power_on(dev);

    /* We need this, sadly. */
    SavedKbd = dev;

    return (dev);
}

const device_t keyboard_at_device = {
    .name          = "PC/AT Keyboard",
    .internal_name = "keyboard_at",
    .flags         = 0,
    .local         = KBC_TYPE_ISA | KBC_VEN_GENERIC,
    .init          = kbd_init,
    .close         = kbd_close,
    .reset         = kbd_reset,
    { .available = NULL },
    .speed_changed = NULL,
    .force_redraw  = NULL,
    .config        = NULL
};

const device_t keyboard_at_ami_device = {
    .name          = "PC/AT Keyboard (AMI)",
    .internal_name = "keyboard_at_ami",
    .flags         = 0,
    .local         = KBC_TYPE_ISA | KBC_VEN_AMI,
    .init          = kbd_init,
    .close         = kbd_close,
    .reset         = kbd_reset,
    { .available = NULL },
    .speed_changed = NULL,
    .force_redraw  = NULL,
    .config        = NULL
};

const device_t keyboard_at_samsung_device = {
    .name          = "PC/AT Keyboard (Samsung)",
    .internal_name = "keyboard_at_samsung",
    .flags         = 0,
    .local         = KBC_TYPE_ISA | KBC_VEN_SAMSUNG,
    .init          = kbd_init,
    .close         = kbd_close,
    .reset         = kbd_reset,
    { .available = NULL },
    .speed_changed = NULL,
    .force_redraw  = NULL,
    .config        = NULL
};

const device_t keyboard_at_toshiba_device = {
    .name          = "PC/AT Keyboard (Toshiba)",
    .internal_name = "keyboard_at_toshiba",
    .flags         = 0,
    .local         = KBC_TYPE_ISA | KBC_VEN_TOSHIBA,
    .init          = kbd_init,
    .close         = kbd_close,
    .reset         = kbd_reset,
    { .available = NULL },
    .speed_changed = NULL,
    .force_redraw  = NULL,
    .config        = NULL
};

const device_t keyboard_at_olivetti_device = {
    .name          = "PC/AT Keyboard (Olivetti)",
    .internal_name = "keyboard_at_olivetti",
    .flags         = 0,
    .local         = KBC_TYPE_ISA | KBC_VEN_OLIVETTI,
    .init          = kbd_init,
    .close         = kbd_close,
    .reset         = kbd_reset,
    { .available = NULL },
    .speed_changed = NULL,
    .force_redraw  = NULL,
    .config        = NULL
};

const device_t keyboard_at_ncr_device = {
    .name          = "PC/AT Keyboard (NCR)",
    .internal_name = "keyboard_at_ncr",
    .flags         = 0,
    .local         = KBC_TYPE_ISA | KBC_VEN_NCR,
    .init          = kbd_init,
    .close         = kbd_close,
    .reset         = kbd_reset,
    { .available = NULL },
    .speed_changed = NULL,
    .force_redraw  = NULL,
    .config        = NULL
};

const device_t keyboard_ps2_device = {
    .name          = "PS/2 Keyboard",
    .internal_name = "keyboard_ps2_ps2",
    .flags         = 0,
    .local         = KBC_TYPE_PS2_1 | KBC_VEN_GENERIC,
    .init          = kbd_init,
    .close         = kbd_close,
    .reset         = kbd_reset,
    { .available = NULL },
    .speed_changed = NULL,
    .force_redraw  = NULL,
    .config        = NULL
};

const device_t keyboard_ps2_ps1_device = {
    .name          = "PS/2 Keyboard (IBM PS/1)",
    .internal_name = "keyboard_ps2_ps1",
    .flags         = 0,
    .local         = KBC_TYPE_PS2_1 | KBC_VEN_IBM_PS1,
    .init          = kbd_init,
    .close         = kbd_close,
    .reset         = kbd_reset,
    { .available = NULL },
    .speed_changed = NULL,
    .force_redraw  = NULL,
    .config        = NULL
};

const device_t keyboard_ps2_ps1_pci_device = {
    .name          = "PS/2 Keyboard (IBM PS/1)",
    .internal_name = "keyboard_ps2_ps1_pci",
    .flags         = DEVICE_PCI,
    .local         = KBC_TYPE_PS2_1 | KBC_VEN_IBM_PS1,
    .init          = kbd_init,
    .close         = kbd_close,
    .reset         = kbd_reset,
    { .available = NULL },
    .speed_changed = NULL,
    .force_redraw  = NULL,
    .config        = NULL
};

const device_t keyboard_ps2_xi8088_device = {
    .name          = "PS/2 Keyboard (Xi8088)",
    .internal_name = "keyboard_ps2_xi8088",
    .flags         = 0,
    .local         = KBC_TYPE_PS2_1 | KBC_VEN_XI8088,
    .init          = kbd_init,
    .close         = kbd_close,
    .reset         = kbd_reset,
    { .available = NULL },
    .speed_changed = NULL,
    .force_redraw  = NULL,
    .config        = NULL
};

const device_t keyboard_ps2_ami_device = {
    .name          = "PS/2 Keyboard (AMI)",
    .internal_name = "keyboard_ps2_ami",
    .flags         = 0,
    .local         = KBC_TYPE_PS2_1 | KBC_VEN_AMI,
    .init          = kbd_init,
    .close         = kbd_close,
    .reset         = kbd_reset,
    { .available = NULL },
    .speed_changed = NULL,
    .force_redraw  = NULL,
    .config        = NULL
};

const device_t keyboard_ps2_olivetti_device = {
    .name          = "PS/2 Keyboard (Olivetti)",
    .internal_name = "keyboard_ps2_olivetti",
    .flags         = 0,
    .local         = KBC_TYPE_PS2_1 | KBC_VEN_OLIVETTI,
    .init          = kbd_init,
    .close         = kbd_close,
    .reset         = kbd_reset,
    { .available = NULL },
    .speed_changed = NULL,
    .force_redraw  = NULL,
    .config        = NULL
};

const device_t keyboard_ps2_mca_device = {
    .name          = "PS/2 Keyboard",
    .internal_name = "keyboard_ps2_mca",
    .flags         = 0,
    .local         = KBC_TYPE_PS2_1 | KBC_VEN_IBM_MCA,
    .init          = kbd_init,
    .close         = kbd_close,
    .reset         = kbd_reset,
    { .available = NULL },
    .speed_changed = NULL,
    .force_redraw  = NULL,
    .config        = NULL
};

const device_t keyboard_ps2_mca_2_device = {
    .name          = "PS/2 Keyboard",
    .internal_name = "keyboard_ps2_mca_2",
    .flags         = 0,
    .local         = KBC_TYPE_PS2_2 | KBC_VEN_IBM_MCA,
    .init          = kbd_init,
    .close         = kbd_close,
    .reset         = kbd_reset,
    { .available = NULL },
    .speed_changed = NULL,
    .force_redraw  = NULL,
    .config        = NULL
};

const device_t keyboard_ps2_quadtel_device = {
    .name          = "PS/2 Keyboard (Quadtel/MegaPC)",
    .internal_name = "keyboard_ps2_quadtel",
    .flags         = 0,
    .local         = KBC_TYPE_PS2_1 | KBC_VEN_QUADTEL,
    .init          = kbd_init,
    .close         = kbd_close,
    .reset         = kbd_reset,
    { .available = NULL },
    .speed_changed = NULL,
    .force_redraw  = NULL,
    .config        = NULL
};

const device_t keyboard_ps2_pci_device = {
    .name          = "PS/2 Keyboard",
    .internal_name = "keyboard_ps2_pci",
    .flags         = DEVICE_PCI,
    .local         = KBC_TYPE_PS2_1 | KBC_VEN_GENERIC,
    .init          = kbd_init,
    .close         = kbd_close,
    .reset         = kbd_reset,
    { .available = NULL },
    .speed_changed = NULL,
    .force_redraw  = NULL,
    .config        = NULL
};

const device_t keyboard_ps2_ami_pci_device = {
    .name          = "PS/2 Keyboard (AMI)",
    .internal_name = "keyboard_ps2_ami_pci",
    .flags         = DEVICE_PCI,
    .local         = KBC_TYPE_PS2_1 | KBC_VEN_AMI,
    .init          = kbd_init,
    .close         = kbd_close,
    .reset         = kbd_reset,
    { .available = NULL },
    .speed_changed = NULL,
    .force_redraw  = NULL,
    .config        = NULL
};

/* Stub, clone of AMI. */
const device_t keyboard_ps2_ali_pci_device = {
    .name          = "PS/2 Keyboard (ALi M5123/M1543C)",
    .internal_name = "keyboard_ps2_ali_pci",
    .flags         = DEVICE_PCI,
    .local         = KBC_TYPE_PS2_1 | KBC_VEN_AMI,
    .init          = kbd_init,
    .close         = kbd_close,
    .reset         = kbd_reset,
    { .available = NULL },
    .speed_changed = NULL,
    .force_redraw  = NULL,
    .config        = NULL
};

const device_t keyboard_ps2_intel_ami_pci_device = {
    .name          = "PS/2 Keyboard (AMI)",
    .internal_name = "keyboard_ps2_intel_ami_pci",
    .flags         = DEVICE_PCI,
    .local         = KBC_TYPE_PS2_1 | KBC_VEN_INTEL_AMI,
    .init          = kbd_init,
    .close         = kbd_close,
    .reset         = kbd_reset,
    { .available = NULL },
    .speed_changed = NULL,
    .force_redraw  = NULL,
    .config        = NULL
};

const device_t keyboard_ps2_acer_pci_device = {
    .name          = "PS/2 Keyboard (Acer 90M002A)",
    .internal_name = "keyboard_ps2_acer_pci",
    .flags         = DEVICE_PCI,
    .local         = KBC_TYPE_PS2_1 | KBC_VEN_ACER,
    .init          = kbd_init,
    .close         = kbd_close,
    .reset         = kbd_reset,
    { .available = NULL },
    .speed_changed = NULL,
    .force_redraw  = NULL,
    .config        = NULL
};

void
keyboard_at_set_mouse(void (*func)(uint8_t val, void *priv), void *priv)
{
    // mouse_write = func;
    // mouse_p = priv;
}

void
keyboard_at_adddata_mouse(uint8_t val)
{
    atkbd_t *dev = SavedKbd;

    kbc_queue_add(dev, val, 2, 0x00);
}

void
keyboard_at_adddata_mouse_direct(uint8_t val)
{
    // atkbd_t *dev = SavedKbd;

    return;
}

void
keyboard_at_adddata_mouse_cmd(uint8_t val)
{
    // atkbd_t *dev = SavedKbd;

    return;
}

void
keyboard_at_mouse_reset(void)
{
    // atkbd_t *dev = SavedKbd;

    return;
}

uint8_t
keyboard_at_mouse_pos(void)
{
    return ((mouse_queue_end - mouse_queue_start) & 0xf);
}

int
keyboard_at_fixed_channel(void)
{
    // atkbd_t *dev = SavedKbd;

    return 0x000;
}

void
keyboard_at_set_mouse_scan(uint8_t val)
{
    atkbd_t *dev             = SavedKbd;
    uint8_t  temp_mouse_scan = val ? 1 : 0;

    if (temp_mouse_scan == !(dev->mem[0x20] & 0x20))
        return;

    set_enable_mouse(dev, val ? 1 : 0);

    kbd_log("ATkbc: mouse scan %sabled via PCI\n", mouse_scan ? "en" : "dis");
}

uint8_t
keyboard_at_get_mouse_scan(void)
{
    atkbd_t *dev = SavedKbd;

    return ((dev->mem[0x20] & 0x20) ? 0x00 : 0x10);
}

void
keyboard_at_set_a20_key(int state)
{
    atkbd_t *dev = SavedKbd;

    write_output(dev, (dev->p2 & 0xfd) | ((!!state) << 1));
}

void
keyboard_at_set_mode(int ps2)
{
    atkbd_t *dev = SavedKbd;

    if (ps2)
        dev->flags |= KBC_FLAG_PS2;
    else
        dev->flags &= ~KBC_FLAG_PS2;
}
